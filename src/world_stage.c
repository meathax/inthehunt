#include "world_stage.h"
#include "world_shadow.h"
#include "world_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);

/* These stage actors paint the map or follow an authored location. Their
 * spawn field is relative to the camera at the scheduled trigger. */
static int world_anchored_kind(uint16_t segment, uint16_t kind)
{
    switch (((uint32_t)segment << 4) + kind) {
    case 0x0da13: case 0x0daa9: case 0x0db86: case 0x0dbf3: /* tile animation */
    case 0x1e800: case 0x1e84d: /* fixed scenery and surface target */
    case 0x1e9d2: case 0x1eace: /* breakable structures and rubble */
    case 0x1f1ba: case 0x1f66d: /* bridge sections and building windows */
    case 0x1ec04:               /* bridge weak point */
    case 0x7704a:               /* gate and player stop */
    case 0x7711d: case 0x77187: /* far-layer animation and background life */
    case 0x77239: case 0x772a0: /* near-layer blocks and egg emitter */
    case 0x7c458:               /* lift and its four shaft shutters */
    case 0x7a54c:               /* scroll-triggered background animation */
    case 0x7c312:               /* stage-six scenery */
    case 0x7df0e:               /* gun base and attached gun */
    case 0x7e41e: case 0x7e817: /* hazards tied to a scroll-relative origin */
    case 0xa8309:               /* scroll-locked decoration */
    case 0xaf1d4: case 0xaf95d: /* destructible scenery */
    case 0xb18cb:               /* bubble vent */
    case 0xba0f4:               /* fixed salvo gun and its wreck */
    case 0xbb92b:               /* freeze launcher and its beam */
    case 0xbccac:               /* mounted turret */
        return 1;
    default:
        return 0;
    }
}

static uint16_t word(const uint8_t *p, uint16_t a)
{
    return p[a] | ((uint16_t)p[(uint16_t)(a + 1)] << 8);
}

static void put(uint8_t *p, uint16_t a, uint16_t value)
{
    p[a] = (uint8_t)value;
    p[(uint16_t)(a + 1)] = value >> 8;
}

static uint16_t stage_spawn_x(uint16_t segment, uint16_t kind, uint16_t x,
                              uint16_t flags, int late)
{
    if (world_view_width() == WORLD_VIEW_ARCADE_WIDTH || world_anchored_kind(segment, kind))
        return x - late;
    const int left = 96 - world_view_wing(), right = left + (int)world_view_width();
    /* Stage two's paired submarines straddle the old right boundary.
       The first 4032 hull piece starts 24 pixels left of the pivot. */
    if (segment == 0xb000 && kind == 0x909f && x >= 0x198 && x <= 0x1a0)
        return right + 24 + (x - 0x198);
    /* The eel's horizontal routes and the left-entry formation start
       inside the arcade frame. Use the leading end of their artwork. */
    if (segment == 0xb000 && kind == 0xd457 && (flags & 3) < 2)
        return (flags & 1) ? left - 48 : right + 48;
    if (segment == 0xb000 && kind == 0xa6d6 && x <= 0x60)
        return left - 26 - (0x60 - x);
    /* These swimmers bob vertically. Horizontal camera motion brings
       their formation in from the right, keeping its 32-pixel spacing. */
    if (segment == 0xa000 && kind == 0xec7e && (x == 0x80 || x == 0xa0))
        return right + 24 + (x - 0x80);
    /* The dock troopers wait in place for their first animation. Let the
       approaching camera reveal them from the right. */
    if (segment == 0xb000 && kind == 0x1c16 && (flags & 0x0e00) == 0x0600 &&
        x >= 0x60 && x < 0x1a0)
        return right + 16;
    /* A delayed allocation still needs the authored run-in distance.
       The flipped B000:571C warship extends 128 pixels past its pivot. */
    int moved = (int16_t)world_view_spawn_x(x);
    if (segment == 0xb000 && kind == 0x571c && (int16_t)x < 0)
        return moved < left - 128 ? moved : left - 128;
    if (x && (int16_t)x >= 0x1a0)
        return moved < right + 16 ? right + 16 : moved;
    if (x && (int16_t)x < 0x60)
        return moved > left - 16 ? left - 16 : moved;
    return moved - late;
}

/* Stage one, surface after the rock island: the far layer (page 2) turns
 * from plain sky to the mountain range at world column 180. The arcade
 * keeps that join behind the island until it leaves the left edge, but the
 * island outruns the slower far layer, so in the wider views the join and
 * the plain sky left of it come out into the left wing. Once the join is
 * left of picture X 0 (the island still covers the wing there), the sky
 * columns that can still reach the wing take the range's 16-column
 * repeat. The tile checks make this a no-op anywhere else. */
enum { FAR_PAGE = 2, FAR_JOIN = 180, FAR_REPEAT = 16, FAR_ROW = 18 };


/* Ring cell of a far-layer world column (Widescreen has no shadow). */
static uint8_t *far_ring(uint8_t *tiles, int world, unsigned row)
{
    return tiles + FAR_PAGE * 0x4000 + (row & 63) * 256 + ((unsigned)world & 63) * 4;
}

static uint16_t cell_tile(const uint8_t *cell) { return (uint16_t)(cell[0] | cell[1] << 8); }

void world_stage_one_far_mountains(const uint8_t *ram, uint8_t *tiles)
{
    int wing = world_view_wing();
    if (!wing || word(ram, 0x84) != 0 || word(ram, 0x80) != 0xb2e) return;
    /* World column of picture X: floor((x + offset) / 8), from the line
       scroll of the mountain rows (world_video_playfields). */
    int offset = (int)word(tiles, (uint16_t)(0xf400 + FAR_PAGE * 0x400 + 2 * (FAR_ROW * 8 + 4))) + 80 - 2 * FAR_PAGE;
    int join_x = 8 * FAR_JOIN - offset;
    if (join_x >= 0 || join_x < -wing - 8 * FAR_REPEAT) return;
    int shadow = world_shadow_active();
    uint8_t join[4], cell[4];
    if (shadow ? !world_shadow_get_column(FAR_PAGE, FAR_JOIN, FAR_ROW, join)
               : !memcpy(join, far_ring(tiles, FAR_JOIN, FAR_ROW), 4)) return;
    /* Tile numbers 213F (range) and 1Fxx (plain sky); bit 13 is the bank. */
    if (cell_tile(join) != 0x213f) return;
    for (int w = FAR_JOIN - (wing + 7) / 8 - 1; w < FAR_JOIN; ++w) {
        int src = w + FAR_REPEAT * ((FAR_JOIN - w + FAR_REPEAT - 1) / FAR_REPEAT);
        if (shadow) {
            if (!world_shadow_get_column(FAR_PAGE, w, FAR_ROW, cell) || (cell_tile(cell) & 0xff00) != 0x1f00) continue;
            for (unsigned row = 0; row < 64; ++row)
                if (world_shadow_get_column(FAR_PAGE, src, row, cell))
                    world_shadow_put_column(tiles, FAR_PAGE, w, row, cell);
            continue;
        }
        /* The 64-column ring: the game draws ahead to picture X 473, so a
           column at X -39 or further left already holds the range 512
           pixels ahead (same 16-column phase). Patch only sky still there. */
        if (8 * w - offset <= 473 - 512 || (cell_tile(far_ring(tiles, w, FAR_ROW)) & 0xff00) != 0x1f00) continue;
        for (unsigned row = 0; row < 64; ++row)
            memcpy(far_ring(tiles, w, row), far_ring(tiles, src, row), 4);
    }
}

void world_stage_three_boss_wall(const uint8_t *ram, uint8_t *tiles)
{
    if (world_view_width() <= WORLD_VIEW_ARCADE_WIDTH || word(ram, 0x84) != 2 ||
        word(ram, 0x80) != 0xb2e || word(ram, 0xb70a) != 0x11bf) return;
    /* At the final horizontal lock, the authored industrial wall ends at
       field column 43 in the 64-tile ring. Its girders repeat every 16
       tiles. Publish the next seven columns (Widescreen) or sixteen
       (Ultrawide, through picture X 448), including collision material. */
    unsigned extra = world_view_width() == WORLD_VIEW_ULTRA_WIDTH ? 16 : 7;
    for (unsigned row = 0; row < 64; ++row) {
        uint8_t *line = tiles + 0x4000 + row * 256;
        for (unsigned k = 0; k < extra; ++k) {
            const uint8_t *cell = line + (28 + k % 16) * 4;
            if (world_shadow_active()) {
                /* Ring column 44 + k is at picture X 321 + 8k at this lock. */
                int world = world_shadow_world_column(1, row, 321 + 8 * (int)k);
                uint8_t copy[4];
                memcpy(copy, cell, 4);
                world_shadow_put_column(tiles, 1, world, row, copy);
            } else
                memcpy(line + (44 + k) * 4, cell, 4);
        }
    }
}

/* Map-locked sprite actors (mines, pods, turrets, guns, vents). Each
 * follows the playfield scroll from its spawn point and shows nothing
 * until then, so in the arcade it appears 24+ pixels beyond the right
 * edge. A wider view would reveal that point, making the actor pop in
 * already on screen. These are spawned one wing of camera travel early at
 * the same world position, so they enter from outside the wider view
 * exactly as they do in the arcade. Tile painters, parallax-layer and
 * camera-stopping kinds keep their authored schedule. */
static int world_prespawn_kind(uint16_t segment, uint16_t kind)
{
    switch (((uint32_t)segment << 4) + kind) {
    case 0x7e41e: case 0x7e817: /* tethered mines and item pods */
    case 0x7df0e:               /* gun base and attached gun */
    case 0x77187:               /* background life */
    case 0xaf1d4: case 0xaf95d: /* destructible scenery */
    case 0xb18cb:               /* bubble vent */
    case 0xba0f4:               /* fixed salvo gun */
    case 0xbb92b:               /* freeze launcher */
    case 0xbccac:               /* mounted turret */
        return 1;
    default:
        return 0;
    }
}

/* Records already spawned ahead of their trigger, by record index. Reset
 * whenever the table or its cursor goes backwards (a new stage). */
enum { PRESPAWN_RECORDS = 1024 };
static uint8_t prespawned[PRESPAWN_RECORDS / 8];
static uint16_t prespawn_table, prespawn_cursor;

void world_stage_reset_prespawn(void)
{
    memset(prespawned, 0, sizeof prespawned);
    prespawn_table = prespawn_cursor = 0;
}

static int prespawn_marked(unsigned index)
{
    return index < PRESPAWN_RECORDS && (prespawned[index / 8] >> (index % 8) & 1);
}

/* Spawn one record: 1 spawned, 2 skipped by its player gate, 0 pool full. */
static int spawn_record(uint8_t *ram, const uint8_t *bank, uint16_t parent, uint16_t row,
                        uint16_t camera)
{
    uint16_t trigger = word(bank, row), flags = word(bank, row + 10);
    unsigned gate = flags >> 14, players = ram[0x264] & 12;
    if (gate && (gate == 1 ? players == 0 : players != 12)) return 2;   /* gated out */
    uint16_t child = world_spawn_child(ram, parent, word(bank, row + 2));
    if (!child) return 0;
    put(ram, child + 2, word(bank, row + 4));
    uint16_t authored_x = word(bank, row + 6);
    uint16_t fx = stage_spawn_x(word(bank, row + 4), word(bank, row + 2),
                                authored_x, flags, (int16_t)(camera - trigger));
    put(ram, child + 0x21, fx);
    uint16_t y = word(bank, row + 8);
    if (!(ram[0x26e] & 2)) y += word(ram, 0xb732);
    put(ram, child + 0x24, y);
    put(ram, child + 0x1e, flags);
    static int spawn_trace = -1;
    if (spawn_trace < 0) spawn_trace = !!getenv("WORLD_SPAWN_TRACE");
    if (spawn_trace)
        fprintf(stderr, "spawn trg=%04x cam=%04x kind=%04x:%04x ax=%04x fx=%04x y=%04x fl=%04x\n",
                trigger, camera, word(bank, row + 4), word(bank, row + 2),
                word(bank, row + 6), fx, y, flags);
    return 1;
}

/* 1000:5D98. Drain due twelve-byte records in ROM order. A full actor
 * pool leaves the current record pending for the next tick. */
void world_stage_spawns(uint8_t *ram, const uint8_t *rom, uint16_t parent)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t table = word(bank, 0x5e42 + 2 * word(ram, 0x84));
    uint16_t camera = word(ram, 0xb700);
    if (table != prespawn_table || word(ram, parent + 0x40) < prespawn_cursor) {
        memset(prespawned, 0, sizeof prespawned);
        prespawn_table = table;
    }
    for (;;) {
        uint16_t row = table + word(ram, parent + 0x40);
        uint16_t trigger = word(bank, row);
        if (trigger == UINT16_MAX || (int16_t)trigger > (int16_t)camera) break;
        put(ram, parent + 0x42, trigger);
        if (!prespawn_marked(word(ram, parent + 0x40) / 12u) && !spawn_record(ram, bank, parent, row, camera))
            break;
        put(ram, parent + 0x40, word(ram, parent + 0x40) + 12);
    }
    prespawn_cursor = word(ram, parent + 0x40);
    /* Look one wing ahead for map-locked sprites (see world_prespawn_kind). */
    int lead = world_view_wing();
    if (lead) {
        for (uint16_t offset = prespawn_cursor;; offset += 12) {
            uint16_t row = table + offset, trigger = word(bank, row);
            unsigned index = offset / 12u;
            if (trigger == UINT16_MAX || (int16_t)(trigger - camera) > lead || index >= PRESPAWN_RECORDS) break;
            if (prespawn_marked(index) || !world_prespawn_kind(word(bank, row + 4), word(bank, row + 2)))
                continue;
            int spawned = spawn_record(ram, bank, parent, row, camera);
            if (!spawned) break;
            /* A gated record is judged again when it falls due. */
            if (spawned == 1) prespawned[index / 8] |= (uint8_t)(1u << (index % 8));
        }
    }
    put(ram, 0xb6ea, 0);
    put(ram, 0xb6ec, 0);
}

/* 1067: acquire a shared graphics resource or reserve a free slot. */
void world_acquire_resource(uint8_t *ram, uint16_t actor, uint16_t id)
{
    unsigned index = ram[actor + 0x18];
    uint16_t entry = 0xa006 + 2 * (id & 511);
    if ((int8_t)index >= 4 || (!ram[entry+1] && !(word(ram,0x262) & 0x8800) && !word(ram,0xa48a))) {
        ++ram[0xa48e + (uint8_t)id];
        ++ram[0xa58e + (uint8_t)id];
        ram[0x26d] |= 0x80;
        return;
    }
    if (!ram[entry+1]) {
        if (word(ram,0x262) & 0x8800) return;
        uint16_t cursor = word(ram,0xa486);
        uint8_t slot = ram[0xa406 + cursor];
        put(ram,0xa486,(cursor + 1) & 63);
        put(ram,0xa48a,word(ram,0xa48a)-1);
        ram[entry] = slot;
        uint16_t job = 0x9e06 + 8 * slot;
        put(ram,job,id & 511);
        put(ram,job+2,64);
        put(ram,job+4,0);
        put(ram,job+6,0);
    }
    ram[actor+0x10+index] = (uint8_t)id;
    ram[actor+0x14+index] = ram[actor+0x2c] = ram[entry];
    ++ram[entry+1];
    ++ram[actor+0x18];
}

/* B000:909F. Difficulty, spawn variant and behavior tables come from the
 * player's ROM. All table addresses below are offsets within segment B000. */
void world_kind11_init(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *b = rom + 0xb0000;
    uint8_t *a = ram + actor;
    world_acquire_resource(ram,actor,word(b,0x90a0));
    put(a,14,word(b,0x90aa));
    a[0x2d]=b[0x90af];
    put(a,0x26,word(b,0x90b3));
    put(a,0x36,word(b,0x90b8));
    a[12]=b[0x90bd];
    put(a,0x2a,word(b,0x90c1));
    unsigned variant = a[0x1e] & 32;
    uint16_t profile = 0x8f86 + word(b,0x8f46 + variant + word(ram,0x88));
    put(a,0x52,word(b,profile+4));
    put(a,0x54,word(b,profile+8));
    put(a,0x58,word(b,profile+8));
    put(a,0x5a,word(b,profile+10));
    put(a,0x5c,word(b,profile+10));
    put(a,0x60,word(b,profile+6));
    put(a,0x74,word(b,profile));
    put(a,0x5e,word(b,profile));
    put(a,0x62,word(b,profile+2));
    put(a,0x50,word(b,0x9032+(variant>>4)));
    uint16_t timer = word(b,0x8d80 + (a[0x1f]&14));
    put(a,0x66,timer);
    put(a,0x68,timer);
    put(a,0x38,word(b,0x9130));
    put(a,0x2e,word(b,0x9135));
    put(a,0x42,word(b,0x913a));
    put(a,0x4a,word(b,0x913f));
    put(a,0x48,word(b,0x9144));
    put(a,0,word(b,0x9149));
}



void world_kind25_init(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + 0xb0000;
    uint8_t *a = ram + actor;
    unsigned variant = a[0x1e] & 32;
    unsigned profile = 0x900e + word(bank, 0x8fce + variant + word(ram, 0x88));
    world_acquire_resource(ram, actor, word(bank, 0x9541));
    put(a, 0x0e, 0x25);
    a[0x2d] = 0x20;
    put(a, 0x26, 32);
    put(a, 0x36, 32);
    a[0x0c] = 4;
    put(a, 0x2a, word(bank, 0x9562));
    put(a, 0x52, word(bank, profile + 4));
    put(a, 0x5a, 1);
    put(a, 0x5c, 1);
    put(a, 0x60, word(bank, profile + 6));
    put(a, 0x74, word(bank, profile));
    put(a, 0x5e, word(bank, profile));
    put(a, 0x62, word(bank, profile + 2));
    put(a, 0x50, word(bank, 0x9032 + (variant >> 4)));
    put(a, 0x6c, word(bank, 0x8d90 + (a[0x1e] & 6)));
    unsigned probe = word(bank, 0x8d80 + (a[0x1f] & 14));
    put(a, 0x66, probe);
    put(a, 0x68, probe);
    put(a, 0x6e, 2);
    put(a, 0x4a, 16);
    put(a, 0x42, 1);
    put(a, 0x70, 512);
    put(a, 0x48, 1);
    put(a, 0x2e, (uint16_t)-384);
    put(a, 0x38, (uint16_t)-384);
    put(a, 0x3a, 0);
    put(a, 0, 0x9601);
}

/* The HUD timer occupies two tile rows. Its text is terminated by FF. */
void world_timer_digits(uint8_t *tiles, const uint8_t *digits)
{
    uint16_t left = 0x2d78;
    while (*digits != 0xff) {
        uint16_t glyph = 0x100 + 4 * (*digits++ - '0');
        for (unsigned part = 0; part < 4; ++part) {
            unsigned row = (left + (part / 2) * 0x100) & 0x3f00;
            unsigned dest = row | ((left + 4 * (part & 1)) & 255);
            put(tiles, dest, glyph + part);
            put(tiles, dest + 2, 0x105);
        }
        left = (left & 0xff00) | ((left + 8) & 255);
    }
}

void world_stage_opening_hud(uint8_t *ram, uint8_t *tiles, const uint8_t *rom)
{
    memset(tiles + 0x2d30, 0, 160);
    memset(tiles + 0x2e30, 0, 160);
    world_timer_digits(tiles, rom + 0xc28d);
    /* The fixed frame surrounding the numbers is drawn by sixteen literal
       VRAM stores. Read the tile numbers and attributes from the World image. */
    for (unsigned i = 0; i < 16; ++i) {
        unsigned store = 0xbf47 + i * 7;
        put(tiles, word(rom, store + 3), word(rom, store + 5));
    }
    if (!word(ram, 0x84)) {
        memset(ram + 0x100, 0, 128);
        for (unsigned slot = 0; slot < 2; ++slot) {
            uint16_t dest = word(ram, 0x254 + 2 * slot);
            for (unsigned i = 0; i < 8; ++i) put(ram, dest + 2 * i, 0);
        }
    }
}
