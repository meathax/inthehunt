#include "world_shadow.h"
#include "world_view.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* See world_shadow.h. All world columns are absolute (mod 8192) so the
 * ring-to-world assignment stays valid while the camera moves. */

enum { PAGES = 4, ROWS = 64, RING = 64, SLOTS = WORLD_SHADOW_COLUMNS, NONE = -32768 };

static uint8_t cells[PAGES][ROWS][SLOTS][4];
static int16_t tags[PAGES][ROWS][SLOTS];
/* World column held by each ring column, and whether its content is known
   (a column rebased in without shadow data keeps its old bytes until the
   game writes it). */
static int16_t ring_world[PAGES][ROWS][RING];
static uint8_t ring_known[PAGES][ROWS][RING];
static uint8_t ring_pending[PAGES][ROWS][RING][4];
static int row_offset[PAGES][ROWS];
static uint8_t row_fixed[PAGES][ROWS];
static uint8_t shadowed[PAGES];
static int active, focus;
uint8_t *world_shadow_debug_tiles;
static uint16_t layer_master_dbg[3];
static uint16_t pass_scroll[3];

const uint8_t *world_tile_shadow_page[4];
const int16_t *world_tile_shadow_tags[4];
const uint8_t *world_tile_shadow_rows[4];
int world_tile_shadow_clip;

static struct { uint16_t base, origin; unsigned width, height; int upward; } opening[4];
static unsigned openings;

static uint16_t word(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int floor_div8(int v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); }

static int wrap_world(int w) { return ((w % 8192) + 8192) % 8192; }

static uint8_t *ring_cell(uint8_t *tiles, unsigned page, unsigned row, unsigned col)
{
    return tiles + page * 0x4000 + row * 256 + (col & 63) * 4;
}

int world_shadow_active(void) { return active; }

void world_dispatch_reset_lookahead(void);

void world_shadow_reset(void)
{
    world_dispatch_reset_lookahead();
    active = 0;
    openings = 0;
    for (unsigned p = 0; p < PAGES; ++p) { world_tile_shadow_page[p] = 0; world_tile_shadow_tags[p] = 0; world_tile_shadow_rows[p] = 0; }
    world_tile_shadow_clip = 0;
}

int world_shadow_world_column(unsigned page, unsigned row, int screen_x)
{
    return wrap_world(floor_div8(screen_x + row_offset[page & 3][row & 63]));
}

int world_shadow_get_column(unsigned page, int world, unsigned row, uint8_t cell[4])
{
    page &= 3; row &= 63;
    int w = wrap_world(world);
    unsigned slot = (unsigned)w & (SLOTS - 1);
    if (!active || !shadowed[page] || row_fixed[page][row] || tags[page][row][slot] != w) return 0;
    memcpy(cell, cells[page][row][slot], 4);
    return 1;
}

/* First world column of the 64-column window centred on a picture X. */
static int window_base(unsigned page, unsigned row, int screen_x)
{
    return wrap_world(floor_div8(screen_x - 256 + row_offset[page][row]));
}

static void sync_row(uint8_t *tiles, unsigned p, unsigned r)
{
    if (row_fixed[p][r]) return;
    for (unsigned c = 0; c < RING; ++c) {
        int w = ring_world[p][r][c];
        if (w == NONE) continue;
        uint8_t *cell = ring_cell(tiles, p, r, c);
        unsigned slot = (unsigned)w & (SLOTS - 1);
        memcpy(cells[p][r][slot], cell, 4);
        tags[p][r][slot] = (int16_t)w;
    }
}

static void load_row(uint8_t *tiles, unsigned p, unsigned r, int base)
{
    for (int k = 0; k < RING; ++k) {
        int w = wrap_world(base + k);
        unsigned c = (unsigned)w & 63;
        if (ring_world[p][r][c] == w) continue;
        uint8_t *cell = ring_cell(tiles, p, r, c);
        unsigned slot = (unsigned)w & (SLOTS - 1);
        ring_world[p][r][c] = (int16_t)w;
        if (tags[p][r][slot] == w) {
            memcpy(cell, cells[p][r][slot], 4);
            ring_known[p][r][c] = 1;
        } else {
            ring_known[p][r][c] = 0;
            memcpy(ring_pending[p][r][c], cell, 4);
        }
    }
}

static void refocus(uint8_t *tiles, int screen_x)
{
    for (unsigned p = 0; p < PAGES; ++p) {
        if (!shadowed[p]) continue;
        for (unsigned r = 0; r < ROWS; ++r) {
            if (row_fixed[p][r]) continue;
            int base = window_base(p, r, screen_x);
            /* The window is contiguous: matching ends mean the ring holds it. */
            if (ring_world[p][r][(unsigned)base & 63] == base &&
                ring_world[p][r][(unsigned)(base + 63) & 63] == wrap_world(base + 63)) continue;
            sync_row(tiles, p, r);
            load_row(tiles, p, r, base);
        }
    }
    focus = screen_x;
}

void world_shadow_focus(uint8_t *ram, uint8_t *tiles, int screen_x)
{
    (void)ram;
    if (!active) return;
    /* Rebasing on every actor would thrash: actors touch tiles within about
       128 pixels of themselves, inside the 256-pixel half window. */
    if (screen_x >= focus - 96 && screen_x <= focus + 96) return;
    refocus(tiles, screen_x);
}

/* Column producers write one column ahead of the picture's right edge.
   The camera handlers (layer actors 3C68..3F68) and the tile-script
   painters (3BB8..40F2) focus on the leading edge. */
static int producer(uint32_t a)
{
    switch (a) {
    case 0x3c68: case 0x3c88: case 0x3c96: case 0x3ca4: case 0x3ca1: case 0x3f68:
    case 0x424c: case 0x4276: case 0x42d5: case 0x41f6:
        return 400;     /* ahead-drawn columns land at picture X 450..482 */
    case 0x3bb8: case 0x3ecf: case 0x40d9: case 0x3bd1: case 0x3ee8: case 0x40f2:
        return 240;     /* the opening map from X 0, set pieces up to X 482 */
    }
    return 0;
}

void world_shadow_actor(uint8_t *ram, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    if (!active) return;
    int lead = producer(address);
    if (lead) {
        if (focus != lead) refocus(tiles, lead);
        return;
    }
    int x = (int16_t)word(ram + actor + 0x21) - 96;
    if (x < -1024 || x > 1536) return;      /* not a picture position */
    /* Windows stay within 128 columns of the picture so no shadow slot a
       visible column needs is reused. */
    if (x < -240) x = -240;
    if (x > 560) x = 560;
    world_shadow_focus(ram, tiles, x);
}

static void apply_openings(uint8_t *tiles)
{
    for (unsigned i = 0; i < openings; ++i) {
        unsigned p = (opening[i].base >> 14) & 3;
        if (!shadowed[p]) continue;
        for (unsigned r = 0; r < ROWS; ++r) sync_row(tiles, p, r);
        uint16_t origin = opening[i].origin;
        int first = (origin & 255) / 4, end = first + 4 * (int)opening[i].width;
        for (unsigned y = 0; y < opening[i].height * 4; ++y) {
            int dy = opening[i].upward ? (int)(y % 4) - (int)(y & ~3u) : (int)y;
            unsigned r = (unsigned)(((origin >> 8) & 0x3f) + dy) & 63;
            if (row_fixed[p][r]) continue;
            /* A full 64-column map is authored to the right and streamed
               on by the game; only the left of world column 0 is missing. */
            int stop = opening[i].width >= 16 ? first : end + 16;
            for (int w = first - 16; w < stop; ++w) {
                if (w >= first && w < end) continue;
                int src = w < first ? 2 * first - 1 - w : 2 * end - 1 - w;
                unsigned ss = (unsigned)wrap_world(src) & (SLOTS - 1);
                if (tags[p][r][ss] != wrap_world(src)) continue;
                uint8_t cell[4];
                memcpy(cell, cells[p][r][ss], 4);
                cell[3] ^= 0x02;            /* attr bit 9: horizontal flip */
                world_shadow_put_column(tiles, p, w, r, cell);
            }
        }
    }
    openings = 0;
}

void world_shadow_begin_pass(const uint8_t *ram, uint8_t *tiles, const uint16_t layer_master[3])
{
    world_shadow_debug_tiles = tiles;
    memcpy(layer_master_dbg, layer_master, sizeof layer_master_dbg);
    for (unsigned l = 0; l < 3; ++l) pass_scroll[l] = word(ram + 0xb706 + 4 * l);
    /* Both wide views: Widescreen's 428-pixel picture plus the game's
       ahead-drawn columns (picture X 449..473) also exceed the 512-pixel
       ring, which wrapped those columns into the leftmost wing pixels. */
    int want = world_view_wing() != 0 && word(ram + 0x80) == 0xb2e;
    if (!want) {
        if (active) world_shadow_reset();
        return;
    }
    unsigned newly = !active;
    memset(shadowed, 0, sizeof shadowed);
    for (int l = 2; l >= 0; --l) {
        unsigned m = layer_master[l];
        if (m & 0x14) continue;                 /* disabled or a native 128-column layer */
        unsigned p = m & 3;
        /* Layers 1 and 2 are playfields. Layer 0 carries the HUD and is a
           playfield only where a stage scrolls it per line. */
        if (l == 0 && !(m & 0x40)) continue;
        unsigned scroll_reg = word(ram + 0xb706 + 4 * l);
        unsigned scroll_y = word(ram + 0xb708 + 4 * l);
        /* Alias choice follows the layer register, which advances smoothly
           with the camera. Per-line scroll bands sample the same world
           within their offset from it; lines that do not scroll (the HUD,
           static sky bands) are read straight from the ring. */
        for (unsigned r = 0; r < ROWS; ++r) {
            int direct = l == 0 && !(m & 0x40);
            if (m & 0x40) {
                /* The HUD lines of a line-scrolled layer 0 hold its
                   unscrolled value (the 16 + 2 * layer sample bias). */
                unsigned sy = (r * 8 + 4) & 511, v = word(tiles + 0xf400 + l * 0x400 + sy * 2);
                direct = v == 0 || (l == 0 && v == 16);
            }
            row_offset[p][r] = 96 + (int)scroll_reg;
            row_fixed[p][r] = (uint8_t)(direct ? (l == 0 ? 2 : 1) : 0);
        }
        shadowed[p] = 1;
    }
    if (newly) {
        for (unsigned p = 0; p < PAGES; ++p)
            for (unsigned r = 0; r < ROWS; ++r) {
                for (unsigned s = 0; s < SLOTS; ++s) tags[p][r][s] = NONE;
                for (unsigned c = 0; c < RING; ++c) ring_world[p][r][c] = NONE;
            }
        active = 1;
        focus = 0x7fff;
    }
    for (unsigned p = 0; p < PAGES; ++p) {
        if (!shadowed[p]) { world_tile_shadow_page[p] = 0; world_tile_shadow_tags[p] = 0; world_tile_shadow_rows[p] = 0; continue; }
        for (unsigned r = 0; r < ROWS; ++r) {
            if (ring_world[p][r][0] != NONE || row_fixed[p][r]) continue;
            /* A page entering the shadow: its ring is the authored window. */
            int base = window_base(p, r, 160);
            for (int k = 0; k < RING; ++k) {
                int w = wrap_world(base + k);
                ring_world[p][r][w & 63] = (int16_t)w;
                ring_known[p][r][w & 63] = 1;
            }
            sync_row(tiles, p, r);
        }
        world_tile_shadow_page[p] = &cells[p][0][0][0];
        world_tile_shadow_tags[p] = &tags[p][0][0];
        world_tile_shadow_rows[p] = row_fixed[p];
    }
    world_tile_shadow_clip = 1;
    focus = 0x7fff;
    refocus(tiles, 160);
    if (openings && word(ram + 0xb700) < 32) apply_openings(tiles);
}

void world_shadow_end_pass(uint8_t *ram, uint8_t *tiles)
{
    (void)ram;
    if (!active) return;
    for (unsigned p = 0; p < PAGES; ++p)
        if (shadowed[p])
            for (unsigned r = 0; r < ROWS; ++r) sync_row(tiles, p, r);
    if (focus != 160) refocus(tiles, 160);
}

void world_shadow_extend_opening(uint8_t *tiles, uint16_t base, uint16_t origin,
                                 unsigned width, unsigned height, int upward)
{
    (void)tiles;
    if (openings >= 4) return;
    opening[openings].base = base;
    opening[openings].origin = origin;
    opening[openings].width = width;
    opening[openings].height = height;
    opening[openings].upward = upward;
    ++openings;
}

void world_shadow_put_column(uint8_t *tiles, unsigned page, int world, unsigned row, const uint8_t cell[4])
{
    page &= 3; row &= 63;
    int w = wrap_world(world);
    unsigned slot = (unsigned)w & (SLOTS - 1);
    memcpy(cells[page][row][slot], cell, 4);
    tags[page][row][slot] = (int16_t)w;
    unsigned c = (unsigned)w & 63;
    if (ring_world[page][row][c] == w) {
        memcpy(ring_cell(tiles, page, row, c), cell, 4);
        ring_known[page][row][c] = 1;
    }
}

int world_shadow_page_offset(unsigned page, int *offset)
{
    for (unsigned l = 1; l < 3; ++l) {
        unsigned m = layer_master_dbg[l];
        if ((m & 0x54) || (m & 3) != (page & 3)) continue;
        *offset = 96 + (int)pass_scroll[l];
        return 1;
    }
    return 0;
}

void world_shadow_put_word(uint8_t *tiles, unsigned page, int world, unsigned row, unsigned byte, uint16_t value)
{
    page &= 3; row &= 63; byte &= 2;
    int w = wrap_world(world);
    unsigned slot = (unsigned)w & (SLOTS - 1);
    if (!active || !shadowed[page] || row_fixed[page][row] || tags[page][row][slot] != w) return;
    cells[page][row][slot][byte] = (uint8_t)value;
    cells[page][row][slot][byte + 1] = (uint8_t)(value >> 8);
    unsigned c = (unsigned)w & 63;
    if (ring_world[page][row][c] == w) {
        uint8_t *cell = ring_cell(tiles, page, row, c);
        cell[byte] = (uint8_t)value;
        cell[byte + 1] = (uint8_t)(value >> 8);
    }
}

/* WORLD_SHADOW_DEBUG=1 (test-only): tag coverage of the visible columns. */
void world_shadow_debug(const uint8_t *ram, unsigned long ticks)
{
    static int on = -1;
    if (on < 0) on = getenv("WORLD_SHADOW_DEBUG") != 0;
    if (!on || !active) return;
    fprintf(stderr, "%lu shadow b700 %04x cam %04x", ticks, word(ram + 0xb700), word(ram + 0xb70a));
    if (getenv("WORLD_SHADOW_ROWS")) {
        fprintf(stderr, " regs %04x %04x %04x rs:", word(ram + 0xb706), word(ram + 0xb70a), word(ram + 0xb70e));
        extern uint8_t *world_shadow_debug_tiles;
static uint16_t layer_master_dbg[3];
        fprintf(stderr, " m %04x %04x %04x", layer_master_dbg[0], layer_master_dbg[1], layer_master_dbg[2]);
        for (unsigned p = 0; p < 4; ++p) { fprintf(stderr, " P%u:", p); for (unsigned r = 40; r < 50; ++r) fprintf(stderr, "%u", row_fixed[p][r]); }
        for (unsigned l = 0; l < 3; ++l) { fprintf(stderr, " L%u", l);
            for (unsigned y = 344; y < 392; y += 8) fprintf(stderr, " %d", (int16_t)word(world_shadow_debug_tiles + 0xf400 + l * 0x400 + y * 2)); }
    }
    for (unsigned p = 0; p < PAGES; ++p) {
        if (!shadowed[p]) continue;
        unsigned r = 32, good = 0, total = 0;
        for (int x = -124; x < 444; x += 8) {
            int w = wrap_world(floor_div8(x + row_offset[p][r]));
            ++total; good += tags[p][r][w & 127] == w;
            if (tags[p][r][w & 127] != w) fprintf(stderr, " [%d:%d]", x, tags[p][r][w & 127] == NONE ? -1 : tags[p][r][w & 127] - w);
        }
        fprintf(stderr, " p%u off %d fixed %u %u/%u", p, row_offset[p][r], row_fixed[p][r], good, total);
    }
    fputc(10, stderr);
}

/* WORLD_SHADOW_WRITES=1 (test-only): log tile writes made by each handler
   with the focus it ran under. */
static uint8_t before_image[0xc000];
static int write_log = -1;
void world_shadow_before_handler(const uint8_t *tiles)
{
    if (write_log < 0) write_log = getenv("WORLD_SHADOW_WRITES") != 0;
    if (write_log && active) memcpy(before_image, tiles, sizeof before_image);
}
void world_shadow_after_handler(const uint8_t *ram, const uint8_t *tiles, uint16_t actor, uint32_t address)
{
    if (!write_log || !active) return;
    for (unsigned p = 0; p < 3; ++p) {
        if (!shadowed[p]) continue;
        int lo = 99999, hi = -99999, n = 0;
        for (unsigned r = 0; r < ROWS; ++r) {
            if (row_fixed[p][r]) continue;
            for (unsigned c = 0; c < RING; ++c) {
                unsigned o = p * 0x4000 + r * 256 + c * 4;
                if (!memcmp(before_image + o, tiles + o, 4)) continue;
                int w = ring_world[p][r][c];
                int sx = w * 8 - row_offset[p][r];
                while (sx > 4096) sx -= 65536;
                while (sx < -4096) sx += 65536;
                if (sx < lo) lo = sx;
                if (sx > hi) hi = sx;
                ++n;
            }
        }
        if (n) fprintf(stderr, "W %05x actor %04x x %d focus %d page %u cells %d screen %d..%d%c", (unsigned)address, actor,
                       (int16_t)word(ram + actor + 0x21) - 96, focus, p, n, lo, hi, 10);
    }
}
