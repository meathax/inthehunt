#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <emmintrin.h>  /* SSE2 is baseline on the Windows x64 target. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world_scene.h"

enum {
    W = 320, H = 240, N = W * H, HUD = 224,
    MARGIN = 32,                       /* layer buffers reach this far past the picture: 17 px of
                                          scroll between frames plus 11 px of displacement */
    LW = W + 2 * MARGIN, LH = H + 2 * MARGIN, LN = LW * LH,
    MAXK = WORLD_SCENE_MAX_K,
    GW = W / 8, GH = H / 8,            /* light grid */
    NONE = 0x7fff,                     /* empty sprite pixel */
    MAX_SPRITES = 256, MAX_RINGS = 6, MAX_PARTICLES = 224
};

typedef struct {
    uint8_t tiles[65536];
    uint8_t sprites[2048];
    uint32_t colors[1024];
    WorldVideoLayer layers[3];
    uint16_t video_control;
    int waterline;
    int frac[3];
    unsigned scene_id;
    int valid;
} Snapshot;

typedef struct { int left, top, cols, rows; unsigned code, attr; } Sprite;
typedef struct { float x, y, age, life, strength; } Ring;
typedef struct { float x, y, vx, vy, age, life; int kind; } Particle;

static const uint8_t *gfx1, *gfx2;
static Snapshot cur, prev;
static int new_frame;
static int scene_cache_valid, cached_scale;
static uint32_t layer_clock;
static double sprite_alpha;
static WorldSceneFx cached_fx;

/* Per game frame: palette indices of each layer around the current scroll. */
static uint16_t layer_idx[3][LN];
static uint8_t layer_pri[3][LN];
static int layer_on[3], layer_rows[3];           /* enabled; per-row scroll (no offsets) */
/* Per refresh: colours after the per-layer effects, and the water mask. */
static uint32_t layer_col[3][LN];
static uint8_t layer_lum[3][LN], layer_wat[3][LN];

static uint16_t sprite_px[MAXK * MAXK * N];      /* palette index | 0x8000 behind tiles */
static uint32_t history[MAXK * MAXK * N];
static int have_history;
/* Only opaque HUD pixels are exempt; water continues behind their gaps. */
static uint32_t hud_overlay[W * (H - HUD) * MAXK * MAXK];
static uint32_t hot_sum[3][GW * GH];
static uint16_t hot_count[GW * GH], hot_before[GW * GH];
static uint8_t water_map[N];                     /* 1x: open water under this pixel */
static int8_t field_x[N], field_y[N];            /* shockwave displacement, 1/4 px */
static int field_on;
static int16_t sine[8192];
static float light_absorb[3][320];
static uint16_t absorb[3][320];                  /* 256 = nothing absorbed, by depth */
static uint16_t divide[LW * MAXK + 8];           /* subpixel -> buffer pixel for the current K */
static int divide_k;
static int ready;

static Ring rings[MAX_RINGS];
static Particle particles[MAX_PARTICLES];
static float shake;
static unsigned rumble;
static uint32_t last_clock, seed = 0x1234567u;
static int waterline_used;
static void cinema_reset(void);
static void cinema_scroll(int dx,int dy);
static void cinema_blast(float x,float y,int size,const WorldSceneFx *fx);
static void waterfx_reset(void);
static void waterfx_scroll(int dx, int dy);
static int transport_valid;                      /* the light transport below has run this scene */

/* One render's parameters, shared with the worker threads. */
static struct {
    int k, ow, oh;
    uint32_t *out;
    const WorldSceneFx *fx;
    int off_x[3], off_y[3];            /* subpixels, including the margin */
    int shake_x, shake_y;
    int row_dx[H];                     /* subpixels: surface ripple */
    int waterline, T, Q;
    int scroll_x[3], scroll_y[3];
} g;

/* ---- small helpers ------------------------------------------------------- */

static inline unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static inline int S(int a) { return sine[(a * 8) & 8191]; }      /* 1024 steps per turn */
static inline int F(int a) { return sine[a & 8191]; }            /* 8192 steps per turn */
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Limit an overbright RGB triplet with ONE scale, preserving its hue. */
static uint32_t light_rgb(int r,int gr,int b)
{
    if(r<0)r=0;if(gr<0)gr=0;if(b<0)b=0;
    int peak=r>gr?r:gr;if(b>peak)peak=b;
    if(peak>255){float scale=255.0f/peak;r=(int)(r*scale);gr=(int)(gr*scale);b=(int)(b*scale);}
    return 0xff000000u|(unsigned)r<<16|(unsigned)gr<<8|(unsigned)b;
}
static void light_shoulder(float *r,float *gr,float *b)
{
    float peak=fmaxf(*r,fmaxf(*gr,*b)),scale=1/(1+.65f*peak);
    *r*=scale;*gr*=scale;*b*=scale;
}
/* Length below the surface, including rays crossing the air/water boundary.
 * Native pixel units; coefficients are an artistic water calibration, not metres. */
static int submerged_path(float y0,float y1,float distance)
{
    float fraction=1;
    if(g.waterline>=0){float a=y0-g.waterline,b=y1-g.waterline;
        if(a<0 && b<0)return 0;
        if(a<0)fraction=b/(b-a);else if(b<0)fraction=a/(a-b);
    }
    return clampi((int)(distance*fraction+.5f),0,319);
}
static float frand(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 16777216.0f;
}

static void prepare(void)
{
    for (int i = 0; i < 8192; ++i) sine[i] = (int16_t)lrint(256.0 * sin(i * 6.283185307179586 / 8192.0));
    /* Beer-Lambert per channel: red goes first, then green; blue lasts. */
    static const double rate[3] = {0.0036, 0.0018, 0.0010};
    for (int c = 0; c < 3; ++c)
        for (int d = 0; d < 320; ++d) absorb[c][d] = (uint16_t)lrint(256.0 * exp(-rate[c] * d));
    static const float extinction[3]={.012f,.005f,.0025f};
    for(int c=0;c<3;++c)for(int d=0;d<320;++d)light_absorb[c][d]=expf(-extinction[c]*d);
    ready = 1;
}

/* ---- worker threads: rows of a job are split over four threads ---------- */

enum { WORKERS = 7 };
static HANDLE worker_go[WORKERS], worker_done[WORKERS];
static void (*job)(int, int);
static int job_from[WORKERS], job_to[WORKERS];
static int workers_ready;

static DWORD WINAPI worker(void *index)
{
    int i = (int)(intptr_t)index;
    for (;;) {
        WaitForSingleObject(worker_go[i], INFINITE);
        job(job_from[i], job_to[i]);
        SetEvent(worker_done[i]);
    }
}

static void parallel(void (*fn)(int, int), int rows)
{
    if (!workers_ready) {
        workers_ready = 1;
        for (int i = 0; i < WORKERS; ++i) {
            worker_go[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
            worker_done[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
            HANDLE thread = worker_go[i] && worker_done[i]
                ? CreateThread(NULL, 0, worker, (void *)(intptr_t)i, 0, NULL) : NULL;
            if (!thread) workers_ready = -1;
            else {
                SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
                CloseHandle(thread);
            }
        }
    }
    if (workers_ready < 0 || rows < 64) { fn(0, rows); return; }
    int part = rows / (WORKERS + 1);
    job = fn;
    for (int i = 0; i < WORKERS; ++i) {
        job_from[i] = i * part;
        job_to[i] = (i + 1) * part;
        SetEvent(worker_go[i]);
    }
    fn(WORKERS * part, rows);
    WaitForMultipleObjects(WORKERS, worker_done, TRUE, INFINITE);
}

/* ---- per game frame ------------------------------------------------------ */

/* The tile layers as world_video_playfields samples them, one layer at a
 * time, over the picture plus a margin. */
static void render_layer(int layer)
{
    unsigned master = cur.layers[layer].master;
    layer_on[layer] = !(master & 0x10);
    layer_rows[layer] = (master & 0x40) != 0;
    if (!layer_on[layer]) return;
    unsigned wide = master & 4, columns = wide ? 128 : 64;
    for (int by = 0; by < LH; ++by) {
        unsigned sy = (unsigned)(by - MARGIN + 136 + cur.layers[layer].scroll_y) & 511;
        unsigned scroll = cur.layers[layer].scroll_x;
        if (master & 0x40) scroll = word(cur.tiles + 0xf400 + layer * 0x400 + sy * 2);
        uint16_t *idx = layer_idx[layer] + by * LW;
        uint8_t *pri = layer_pri[layer] + by * LW;
        for (int bx = 0; bx < LW; ++bx) {
            unsigned sx = (unsigned)(bx - MARGIN + 80) + scroll - 2 * (unsigned)layer + (wide ? 256 : 0);
            unsigned entry = (master & 3) * 0x4000 + ((sy / 8) * columns + ((sx / 8) & (columns - 1))) * 4;
            const uint8_t *tile = cur.tiles + (entry & 0xffff);
            unsigned attr = word(tile + 2);
            unsigned pen = world_video_tile_pen(gfx1, word(tile), (attr & 0x200) ? ~sx : sx, (attr & 0x400) ? ~sy : sy);
            if (!pen && layer != 2) { idx[bx] = 0xffff; pri[bx] = 0; continue; }
            unsigned bank = (attr & 0x7f) | ((cur.video_control >> 7) & 0x180);
            idx[bx] = (uint16_t)((bank * 16 + pen) & 1023);
            pri[bx] = pen && ((attr & 0x100) || ((attr & 0x80) && pen >= 8));
        }
    }
}

void world_scene_init(const uint8_t *tiles_rom, const uint8_t *sprites_rom)
{
    gfx1 = tiles_rom;
    gfx2 = sprites_rom;
}

void world_scene_reset(void)
{
    cur.valid = prev.valid = 0;
    transport_valid = 0;
    have_history = 0;
    memset(rings, 0, sizeof rings);
    memset(particles, 0, sizeof particles);
    shake = 0;
    waterfx_reset();
    scene_cache_valid=0;last_clock=0;rumble=0;
    memset(hot_before,0,sizeof hot_before);
}

void world_scene_submit(const WorldSceneFrame *frame)
{
    if (!ready) prepare();
    if (!gfx1 || !gfx2) return;
    if (!cur.valid || cur.scene_id != frame->scene_id) {
        waterfx_reset();memset(rings,0,sizeof rings);memset(particles,0,sizeof particles);
        memset(hot_before,0,sizeof hot_before);have_history=0;shake=0;rumble=0;
        scene_cache_valid=0;transport_valid=0;last_clock=0;
    }
    if (cur.valid) memcpy(&prev, &cur, sizeof prev);
    memcpy(cur.tiles, frame->tiles, sizeof cur.tiles);
    memcpy(cur.sprites, frame->sprites, sizeof cur.sprites);
    memcpy(cur.colors, frame->colors, sizeof cur.colors);
    memcpy(cur.layers, frame->layers, sizeof cur.layers);
    memcpy(cur.frac, frame->frac, sizeof cur.frac);
    cur.video_control = frame->video_control;
    cur.waterline = frame->waterline;
    cur.scene_id = frame->scene_id;
    cur.valid = 1;
    if (!prev.valid || prev.scene_id != cur.scene_id) memcpy(&prev, &cur, sizeof prev);
    for (int layer = 0; layer < 3; ++layer) render_layer(layer);
    /* Rings and particles live in the playfield: follow its scroll. */
    int dx = (int16_t)(cur.layers[1].scroll_x - prev.layers[1].scroll_x);
    int dy = (int16_t)(cur.layers[1].scroll_y - prev.layers[1].scroll_y);
    if (abs(dx) > 32 || abs(dy) > 32) {
        waterfx_reset();memset(rings,0,sizeof rings);memset(particles,0,sizeof particles);
        memset(hot_before,0,sizeof hot_before);have_history=0;scene_cache_valid=0;dx=dy=0;
    }
    waterfx_scroll(dx, dy);
    for (int i = 0; i < MAX_RINGS; ++i) { rings[i].x -= (float)dx; rings[i].y -= (float)dy; }
    for (int i = 0; i < MAX_PARTICLES; ++i) { particles[i].x -= (float)dx; particles[i].y -= (float)dy; }
    new_frame = 1;
}

/* ---- sprites -------------------------------------------------------------- */

static int parse_sprites(const uint8_t *buffer, Sprite *list)
{
    int count = 0;
    for (unsigned offset = 0; offset < 2048 && count < MAX_SPRITES; ) {
        const uint8_t *entry = buffer + offset;
        unsigned geometry = word(entry);
        Sprite *s = &list[count++];
        s->cols = 1 << ((geometry >> 11) & 3);
        s->rows = 1 << ((geometry >> 9) & 3);
        offset += 8u * (unsigned)s->cols;
        s->top = 360 - (int)(geometry & 511) - 16 * (s->rows - 1);
        s->left = (int)((word(entry + 6) - 16u) & 511) - 80;
        s->code = word(entry + 2);
        s->attr = word(entry + 4);
    }
    return count;
}

#include "world_scene_materials.h"

static uint32_t drawing_material;
static inline void sprite_block(int px, int py, uint16_t value)
{
    int k = g.k;
    if (px <= -k || py <= -k || px >= g.ow || py >= g.oh) return;
    int x0 = px < 0 ? 0 : px, x1 = px + k > g.ow ? g.ow : px + k;
    int y0 = py < 0 ? 0 : py, y1 = py + k > g.oh ? g.oh : py + k;
    for (int y = y0; y < y1; ++y) {
        uint16_t *row = sprite_px + (size_t)y * g.ow;
        for (int x = x0; x < x1; ++x)
            if (row[x] == NONE) { row[x] = value; if(g.fx->materials) sprite_material[(size_t)y*g.ow+x]=drawing_material; }      /* the first sprite owns the pixel */
    }
}

/* Draw the sprite list at K times the resolution. Each sprite sits at its
 * own subpixel position: between its previous and current place for an
 * in-between frame, and shifted with the camera's fraction when it moves
 * with the playfield. Also gathers the light the sprites give off. */
static void draw_sprites(double alpha)
{
    if(g.fx->materials)material_palette_prepare();
    static Sprite now[MAX_SPRITES], before[MAX_SPRITES];
    int k = g.k;
    for (size_t i = 0, n = (size_t)g.ow * g.oh; i < n; ++i) sprite_px[i] = NONE;
    memset(hot_sum, 0, sizeof hot_sum);
    memset(hot_count, 0, sizeof hot_count);
    if (cur.video_control & 0x80) return;
    int count = parse_sprites(cur.sprites, now);
    int old = parse_sprites(prev.sprites, before);
    int moving = g.fx->interp && alpha < 0.999 && memcmp(cur.sprites, prev.sprites, sizeof cur.sprites);
    int camera = (int16_t)(cur.layers[1].scroll_x - prev.layers[1].scroll_x);
    int independent = (cur.video_control & 0x2000) != 0;
    double fraction_now = g.fx->smooth ? cur.frac[1] / 256.0 : 0.0;
    double fraction_before = g.fx->smooth ? prev.frac[1] / 256.0 : 0.0;

    /* Light is gathered per sprite and kept only for the game's explosion
       graphics (fire-coloured paint alone also matches submarines, coins and
       the CONTINUE lettering). Palette banks 1-2 (the players' submarines)
       and 11 (the HUD's GO arrow) never glow. */
    static uint32_t part_sum[3][GW * GH];
    static uint16_t part_count[GW * GH], part_cells[GW * GH];
    for (int i = 0; i < count; ++i) {
        const Sprite *s = &now[i];
        int height = s->rows * 16;
        int touched = 0;
        unsigned own_bank = s->attr & 0x7f;
        int may_glow = own_bank != 1 && own_bank != 2 && own_bank != 11;
        /* The game's explosion graphics, in whatever palette. */
        unsigned family = s->code >> 4;
        int blast = (family >= 0x0a9 && family <= 0x0ab) || family == 0x0cb || family == 0x0cc ||
                    family == 0x0fc || family == 0x0fd || (family >= 0x103 && family <= 0x109);
        if (s->top >= H + 8 || s->top + height <= -8) continue;
        /* The same sprite one game frame ago: nearby in the list, same shape
           and palette, and close on screen. */
        int match = -1, best = 1 << 30;
        if (moving || g.fx->smooth)
            for (int j = i - 8; j <= i + 8; ++j) {
                if (j < 0 || j >= old) continue;
                const Sprite *p = &before[j];
                if (p->cols != s->cols || p->rows != s->rows || ((p->attr ^ s->attr) & 0xff)) continue;
                int dx = ((s->left - p->left + 256) & 511) - 256, dy = s->top - p->top;
                if (abs(dx) > 20 || abs(dy) > 20) continue;
                int score = (abs(dx) + abs(dy)) * 4 + abs(i - j);
                if (score < best) { best = score; match = j; }
            }
        double x = s->left, y = s->top;
        int anchored = 1;                          /* moves with the playfield */
        if (match >= 0) {
            int dx = ((s->left - before[match].left + 256) & 511) - 256, dy = s->top - before[match].top;
            anchored = dx == -camera;
            if (moving) {
                double from_x = s->left - dx - (anchored ? fraction_before : 0.0);
                double to_x = s->left - (anchored ? fraction_now : 0.0);
                x = from_x + (to_x - from_x) * alpha;
                y = s->top - dy * (1.0 - alpha);
            } else if (anchored)
                x -= fraction_now;
        } else
            x -= fraction_now;
        int left = (int)floor(x * k + 0.5), top = (int)floor(y * k + 0.5);

        int flip_x = (s->attr & 0x100) != 0, flip_y = (s->attr & 0x200) != 0;
        unsigned bank = (s->attr & (independent ? 0x7f : 0xff)) | ((cur.video_control & 0x8000) ? 0x100 : 0);
        uint16_t flags = (!independent || !(s->attr & 0x80)) ? 0x8000 : 0;
        for (int row = 0; row < height; ++row) {
            int screen_y = s->top + row;
            if (screen_y < -8 || screen_y >= H + 8) continue;
            int local = flip_y ? height - 1 - row : row;
            for (int c = 0; c < s->cols; ++c) {
                int shown = flip_x ? s->cols - 1 - c : c;
                unsigned code = s->code + (unsigned)local / 16 + 8u * (unsigned)c;
                for (int col = 0; col < 16; ++col) {
                    unsigned pen = world_video_sprite_pen(gfx2, code, (unsigned)(flip_x ? 15 - col : col), (unsigned)local);
                    if (!pen) continue;
                    uint16_t index = (uint16_t)((bank * 16 + pen) & 1023);
                    /* A column starting at 497 or beyond wraps to the left edge. */
                    int wrap = s->left + shown * 16 >= 497 ? 512 : 0;
                    int sx = s->left + shown * 16 + col - wrap;
                    int px = left + (shown * 16 + col - wrap) * k, py = top + row * k;
                    drawing_material = g.fx->materials && height>=32 && own_bank!=3 && own_bank!=10 && own_bank!=11 && own_bank!=39 && own_bank!=43 && own_bank!=55 && !blast
                        ? material_surface(code,bank,pen,flip_x?15-col:col,local&15,flip_x,flip_y) : 0;
                    sprite_block(px, py, index | flags);
                    /* Fire, flashes and tracer light their surroundings. */
                    if (may_glow && blast && sx >= 0 && sx < W && screen_y >= 0 && screen_y < H) {
                        uint32_t colour = cur.colors[index];
                        unsigned r = (colour >> 16) & 255, gr = (colour >> 8) & 255, b = colour & 255;
                        if ((r >= 176 && r >= b + 40) || (r >= 236 && gr >= 220)) {
                            int cell = (screen_y / 8) * GW + sx / 8;
                            if (!part_count[cell]) part_cells[touched++] = (uint16_t)cell;
                            part_sum[0][cell] += r; part_sum[1][cell] += gr; part_sum[2][cell] += b;
                            ++part_count[cell];
                        }
                    }
                }
            }
        }
        int glows = blast;
        for (int t = 0; t < touched; ++t) {
            int cell = part_cells[t];
            if (glows) {
                for (int c = 0; c < 3; ++c) hot_sum[c][cell] += part_sum[c][cell];
                hot_count[cell] = (uint16_t)(hot_count[cell] + part_count[cell]);
            }
            part_sum[0][cell] = part_sum[1][cell] = part_sum[2][cell] = 0;
            part_count[cell] = 0;
        }
    }
}

/* ---- blurs ----------------------------------------------------------------- */

/* [1 2 1] blur of an opaque colour layer, both directions. */
static void soften(uint32_t *c)
{
    static uint32_t line[LW > LH ? LW : LH];
#define MIX(a, b, d) (0xff000000u | \
    (((((a) >> 16) & 255) + 2 * (((b) >> 16) & 255) + (((d) >> 16) & 255) + 2) >> 2) << 16 | \
    (((((a) >> 8) & 255) + 2 * (((b) >> 8) & 255) + (((d) >> 8) & 255) + 2) >> 2) << 8 | \
    ((((a) & 255) + 2 * ((b) & 255) + ((d) & 255) + 2) >> 2))
    for (int y = 0; y < LH; ++y) {
        uint32_t *row = c + y * LW;
        for (int x = 0; x < LW; ++x) line[x] = MIX(row[x ? x - 1 : 0], row[x], row[x < LW - 1 ? x + 1 : x]);
        memcpy(row, line, sizeof(uint32_t) * LW);
    }
    /* Walk rows in display order, retaining the unblurred previous row. */
    memcpy(line, c, LW * sizeof *line);
    for (int y = 0; y < LH; ++y) {
        uint32_t *row = c + y * LW;
        const uint32_t *next = y + 1 < LH ? row + LW : row;
        for (int x = 0; x < LW; ++x) {
            uint32_t center = row[x];
            row[x] = MIX(line[x], center, next[x]);
            line[x] = center;
        }
    }
#undef MIX
}

/* ---- light transport (per game frame, half resolution) ------------------- */

/* The playfield's rock is traced as a 2D scene at half resolution: a
 * distance field of the solid pixels lets rays step quickly through open
 * water. Lights are the burning sprites (fire, flashes, exhaust), gathered
 * per 8x8 cell. Each probe gets
 *   - direct light from every light in range, with a soft shadow from a
 *     sphere-traced ray through the rock;
 *   - bounced light: a few rays per frame in rotating directions pick up the
 *     light leaving the first rock they hit (its colour times the light it
 *     received last frame), so repeated frames add further bounces;
 *   - sunlight visibility along the light-shaft angle, up to the surface.
 * Everything is presentation only. */
enum { PW = W / 2, PH = H / 2, PN = PW * PH, MAX_LIGHTS = 140, BOUNCE_RAYS = 3 };

typedef struct { float x, y, r, g, b, range; } Light;

static uint8_t solid[PN];                 /* rock in the playfield, below the surface */
static float albedo[3][PN];               /* rock colour, 0..1 */
static float sdf[PN];                     /* probes to the nearest rock (0 inside) */
static float direct[3][PN], bounce[3][PN], total[3][PN];
static float light_dx[PN], light_dy[PN];  /* where the light comes from, weighted */
static float sun_vis[PN];                 /* 0..1 */
static Light lights[MAX_LIGHTS];
static unsigned tile_glow_sum[3][GW * GH];
static uint16_t tile_glow_count[GW * GH];
static int light_count, transport_frame;
static unsigned transport_scene = 0xffffffffu;
static int transport_waterline;
static const WorldSceneFx *transport_fx;
/* Conservative light bins; skip only contributions the old 1% test rejects. */
enum { LT = 8, LTX = (PW + LT - 1) / LT, LTY = (PH + LT - 1) / LT };
static uint16_t light_bins[LTX * LTY][MAX_LIGHTS], light_bin_count[LTX * LTY];
static uint8_t traced[PN];
typedef struct { float r, g, b, dx, dy, sun; } PixelLight;
static PixelLight pixel_light[N];
static void cache_transport(void);
static void bin_lights(void)
{
    memset(light_bin_count, 0, sizeof light_bin_count);
    for (int i = 0; i < light_count; ++i) {
        const Light *l = &lights[i];
        float peak = fmaxf(l->r, fmaxf(l->g, l->b)), lo = 0, hi = l->range;
        for (int j = 0; j < 16; ++j) {
            float d = (lo + hi) * 0.5f, cut = 1 - d / l->range;
            if (peak * cut * cut / (1 + d * d * 0.012f) >= 0.01f) lo = d; else hi = d;
        }
        /* Upper bound plus epsilon: never remove a contributing old ray. */
        float radius2 = (hi + 0.01f) * (hi + 0.01f);
        for (int ty = 0; ty < LTY; ++ty) for (int tx = 0; tx < LTX; ++tx) {
            float x = l->x, y = l->y;
            float x0 = tx * LT, x1 = x0 + LT, y0 = ty * LT, y1 = y0 + LT;
            float dx = x < x0 ? x0-x : x > x1 ? x-x1 : 0;
            float dy = y < y0 ? y0-y : y > y1 ? y-y1 : 0;
            if (dx*dx+dy*dy <= radius2) {
                int b = ty*LTX+tx; light_bins[b][light_bin_count[b]++] = (uint16_t)i;
            }
        }
    }
}
static void trace_mask(void)
{
    for (int y=0;y<PH;++y) for (int x=0;x<PW;++x) {
        int p=y*PW+x, x0=x&~1, y0=y&~1;
        int x1=x0+2<PW?x0+2:x0, y1=y0+2<PH?y0+2:y0;
        traced[p]=(!(x&1)&&!(y&1)) || x==PW-1 || y==PH-1 ||
            solid[p]!=solid[y0*PW+x0] || solid[p]!=solid[y0*PW+x1] ||
            solid[p]!=solid[y1*PW+x0] || solid[p]!=solid[y1*PW+x1];
    }
}
static void reconstruct_transport(void)
{
    float *maps[] = {direct[0],direct[1],direct[2],bounce[0],bounce[1],bounce[2],light_dx,light_dy,sun_vis};
    for (int y=0;y<PH;++y) for (int x=0;x<PW;++x) {
        int p=y*PW+x; if (traced[p]) continue;
        int x0=x&~1,y0=y&~1,x1=x0+2<PW?x0+2:x0,y1=y0+2<PH?y0+2:y0;
        float ax=(x&1)*0.5f,ay=(y&1)*0.5f;
        for (unsigned m=0;m<sizeof maps/sizeof maps[0];++m) {
            float *v=maps[m];
            v[p]=(v[y0*PW+x0]*(1-ax)+v[y0*PW+x1]*ax)*(1-ay)+
                 (v[y1*PW+x0]*(1-ax)+v[y1*PW+x1]*ax)*ay;
        }
    }
}


/* Open water: blue-dominant and not bright (0..255). */
static inline int water_amount(uint32_t p)
{
    int r = (p >> 16) & 255, gr = (p >> 8) & 255, b = p & 255;
    int lum = (r * 3 + gr * 5 + b * 2) / 10;
    int blue = (b - r) * 3;
    if (gr > b) blue -= (gr - b) * 4;
    return clamp255(clamp255(blue) * (255 - lum) / 200);
}

/* Rock is what the playfield layer draws below the surface (its open water
   is transparent, showing the far layer). Where the playfield instead covers
   the whole picture, open water is told apart by colour. */
static void build_solid(void)
{
    int wl = g.waterline, opaque = 0, seen = 0;
    for (int sy = 1; sy < H; sy += 4)
        for (int sx = 1; sx < W; sx += 4) {
            ++seen;
            if (layer_on[1] && (layer_col[1][(sy + MARGIN) * LW + sx + MARGIN] >> 24)) ++opaque;
        }
    int by_colour = opaque * 100 > seen * 88;
    for (int py = 0; py < PH; ++py)
        for (int px = 0; px < PW; ++px) {
            int p = py * PW + px, sx = px * 2 + 1, sy = py * 2 + 1;
            solid[p] = 0;
            albedo[0][p] = albedo[1][p] = albedo[2][p] = 0;
            /* The surface's own wave tiles are not rock. */
            if (!layer_on[1] || sy >= H || (wl >= 0 && sy <= wl + 20)) continue;
            uint32_t c = layer_col[1][(sy + MARGIN) * LW + sx + MARGIN];
            if (!(c >> 24) || (by_colour && water_amount(c) >= 96)) continue;
            solid[p] = 1;
            albedo[0][p] = ((c >> 16) & 255) / 255.0f;
            albedo[1][p] = ((c >> 8) & 255) / 255.0f;
            albedo[2][p] = (c & 255) / 255.0f;
        }
    /* Chamfer distance transform (1, 1.414), two passes. */
    const float big = 1e6f, diag = 1.41421356f;
    for (int p = 0; p < PN; ++p) sdf[p] = solid[p] ? 0.0f : big;
    for (int y = 0; y < PH; ++y)
        for (int x = 0; x < PW; ++x) {
            float *d = &sdf[y * PW + x];
            if (x > 0 && d[-1] + 1 < *d) *d = d[-1] + 1;
            if (y > 0) {
                if (d[-PW] + 1 < *d) *d = d[-PW] + 1;
                if (x > 0 && d[-PW - 1] + diag < *d) *d = d[-PW - 1] + diag;
                if (x < PW - 1 && d[-PW + 1] + diag < *d) *d = d[-PW + 1] + diag;
            }
        }
    for (int y = PH - 1; y >= 0; --y)
        for (int x = PW - 1; x >= 0; --x) {
            float *d = &sdf[y * PW + x];
            if (x < PW - 1 && d[1] + 1 < *d) *d = d[1] + 1;
            if (y < PH - 1) {
                if (d[PW] + 1 < *d) *d = d[PW] + 1;
                if (x < PW - 1 && d[PW + 1] + diag < *d) *d = d[PW + 1] + diag;
                if (x > 0 && d[PW - 1] + diag < *d) *d = d[PW - 1] + diag;
            }
        }
}

/* Glowing scenery (lava, fires, flares painted into the tiles) shines too:
   per 8x8 cell, the fire-coloured share of the nearest world layer. */
static void gather_tile_glow(void)
{
    for (int cy = 0; cy < H / 8; ++cy)
        for (int cx = 0; cx < GW; ++cx) {
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int j = 1; j < 8; j += 2)
                for (int i = 1; i < 8; i += 2) {
                    int sx = cx * 8 + i, sy = cy * 8 + j;
                    for (int layer = 1; layer < 3; ++layer) {
                        if (!layer_on[layer]) continue;
                        uint32_t c = layer_col[layer][(sy + MARGIN) * LW + sx + MARGIN];
                        if (!(c >> 24)) continue;
                        unsigned r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                        if (r >= 200 && gr + 40 <= r && b + 110 <= r) { sum[0] += r; sum[1] += gr; sum[2] += b; ++n; }
                        break;                                /* the nearer layer hides the far one */
                    }
                }
            tile_glow_count[cy * GW + cx] = (uint16_t)n;
            for (int c = 0; c < 3; ++c) tile_glow_sum[c][cy * GW + cx] = sum[c];
        }
}

static void gather_lights(void)
{
    light_count = 0;
    gather_tile_glow();
    for (int cell = 0; cell < GW * (H / 8) && light_count < MAX_LIGHTS; ++cell) {
        int n = hot_count[cell];
        if (n < 3) continue;
        Light *l = &lights[light_count++];
        /* Cell centre in probes; strength grows with the burning area. */
        l->x = (float)((cell % GW) * 4 + 2);
        l->y = (float)((cell / GW) * 4 + 2);
        float s = (n > 64 ? 64 : n) / 64.0f;
        s = 0.35f + 1.25f * s;
        float r0 = hot_sum[0][cell] / (255.0f * n);
        l->r = r0 * r0 * s; /* approximate linear emission from display-encoded art */
        float g0 = hot_sum[1][cell] / (255.0f * n);
        l->g = g0 * g0 * s; /* approximate linear emission from display-encoded art */
        float b0 = hot_sum[2][cell] / (255.0f * n);
        l->b = b0 * b0 * s; /* approximate linear emission from display-encoded art */
        l->range = 26.0f + 46.0f * s;              /* probes */
    }
    /* Then the glowing scenery, softer (it is lit paint, not flame). */
    for (int cell = 0; cell < GW * (H / 8) && light_count < MAX_LIGHTS; ++cell) {
        int n = tile_glow_count[cell];
        if (n < 4 || hot_count[cell] >= 3) continue;
        Light *l = &lights[light_count++];
        l->x = (float)((cell % GW) * 4 + 2);
        l->y = (float)((cell / GW) * 4 + 2);
        float s = 0.18f + 0.30f * n / 16.0f;
        float r0 = tile_glow_sum[0][cell] / (255.0f * n);
        l->r = r0 * r0 * s; /* approximate linear emission from display-encoded art */
        float g0 = tile_glow_sum[1][cell] / (255.0f * n);
        l->g = g0 * g0 * s; /* approximate linear emission from display-encoded art */
        float b0 = tile_glow_sum[2][cell] / (255.0f * n);
        l->b = b0 * b0 * s; /* approximate linear emission from display-encoded art */
        l->range = 30.0f;
    }
}

static inline float sdf_at(float x, float y)
{
    int ix = (int)x, iy = (int)y;
    if (ix < 0 || iy < 0 || ix >= PW || iy >= PH) return 4.0f;
    return sdf[iy * PW + ix];
}

/* Soft visibility from (x, y) toward a point dist away along (ux, uy). A
 * probe inside rock may reach up to two probes out of it (lit faces). */
static float trace_soft(float x, float y, float ux, float uy, float dist, float k)
{
    float t = 0.0f, res = 1.0f;
    int inside = 0;
    while (t < dist && sdf_at(x + ux * t, y + uy * t) <= 0.0f) {
        if (++inside > 2) return 0.0f;
        t += 1.0f;
    }
    while (t < dist - 1.0f) {
        float h = sdf_at(x + ux * t, y + uy * t);
        if (h < 0.5f) return 0.0f;
        float v = k * (h - 0.5f) / (t + 1.0f);
        if (v < res) res = v;
        t += h > 0.8f ? h : 0.8f;
    }
    return res < 0 ? 0 : res;
}

static void transport_rows(int from, int to)
{
    const WorldSceneFx *fx = transport_fx;
    float surface = transport_waterline < 0 ? -1.0f : transport_waterline / 2.0f;
    const float sun_x = 0.4472136f, sun_y = -0.8944272f;   /* along the light shafts, up and right */
    for (int y = from; y < to; ++y)
        for (int x = 0; x < PW; ++x) {
            int p = y * PW + x;
            if (!traced[p]) continue;
            int bin = (y / LT) * LTX + x / LT;
            float fx0 = x + 0.5f, fy0 = y + 0.5f;
            float dr = 0, dg = 0, db = 0, vx = 0, vy = 0;
            if (y < PH && fx->lights)
                for (int j = 0; j < light_bin_count[bin]; ++j) {
                    const Light *l = &lights[light_bins[bin][j]];
                    float dx = l->x - fx0, dy = l->y - fy0, d2 = dx * dx + dy * dy;
                    if (d2 >= l->range * l->range) continue;
                    float d = sqrtf(d2), cut = 1.0f - d / l->range;
                    float fall = cut * cut / (1.0f + d2 * 0.012f);
                    /* Bound the unoccluded contribution before tracing: distant
                       explosion cells otherwise spend most of a busy frame on
                       shadow rays for less than 1% light per colour channel. */
                    if (fall * fmaxf(l->r, fmaxf(l->g, l->b)) < 0.01f) continue;
                    float vis = d < 1.5f ? 1.0f : trace_soft(fx0, fy0, dx / d, dy / d, d, 9.0f);
                    if (vis <= 0.0f) continue;
                    float e = fall * vis;
                    int wet=submerged_path(fy0*2,l->y*2,d*2);
                    dr += l->r * e * light_absorb[0][wet]; dg += l->g * e * light_absorb[1][wet]; db += l->b * e * light_absorb[2][wet];
                    float lum = (l->r + l->g + l->b) * e;
                    if (d > 0.01f) { vx += dx / d * lum; vy += dy / d * lum; }
                }
            direct[0][p] = dr; direct[1][p] = dg; direct[2][p] = db;
            light_dx[p] = vx; light_dy[p] = vy;

            /* Bounce: the rock each ray meets reflects what it received. */
            float br = 0, bg = 0, bb = 0;
            if (y < PH && fx->lights) {
                unsigned h = (unsigned)p * 2654435761u + (unsigned)transport_frame * 40503u;
                float base = (h >> 8) / 16777216.0f * 6.2831853f / BOUNCE_RAYS;
                for (int r = 0; r < BOUNCE_RAYS; ++r) {
                    float a = base + r * (6.2831853f / BOUNCE_RAYS);
                    float ux = cosf(a), uy = sinf(a), t = 0.0f;
                    int inside = 0;
                    while (sdf_at(fx0 + ux * t, fy0 + uy * t) <= 0.0f && inside < 3) { ++inside; t += 1.0f; }
                    if (inside >= 3) continue;
                    for (int step = 0; step < 24 && t < 48.0f; ++step) {
                        float hx = fx0 + ux * t, hy = fy0 + uy * t;
                        int ix = (int)hx, iy = (int)hy;
                        if (ix < 0 || iy < 0 || ix >= PW || iy >= PH) break;
                        float s = sdf[iy * PW + ix];
                        if (s <= 0.0f) {
                            int q = iy * PW + ix;
                            float fade = 1.0f / (1.0f + t * 0.05f);
                            int wet=submerged_path(fy0*2,hy*2,t*2);
                            br += albedo[0][q] * total[0][q] * fade * light_absorb[0][wet];
                            bg += albedo[1][q] * total[1][q] * fade * light_absorb[1][wet];
                            bb += albedo[2][q] * total[2][q] * fade * light_absorb[2][wet];
                            break;
                        }
                        t += s > 0.8f ? s : 0.8f;
                    }
                }
                float gain = 0.9f / BOUNCE_RAYS;
                br *= gain; bg *= gain; bb *= gain;
            }
            /* Temporal accumulation: noise from few rays settles over frames. */
            float keep = transport_valid ? 0.7f : 0.0f;
            bounce[0][p] = bounce[0][p] * keep + br * (1 - keep);
            bounce[1][p] = bounce[1][p] * keep + bg * (1 - keep);
            bounce[2][p] = bounce[2][p] * keep + bb * (1 - keep);

            /* Sunlight: a soft ray toward the surface along the shafts. */
            float sv = 1.0f;
            if (fx->sunshadow && y < PH) {
                float limit = surface < 0 ? fy0 - 6.0f : fy0 - surface - 12.0f;
                sv = limit <= 0.0f ? 1.0f : trace_soft(fx0, fy0, sun_x, sun_y, limit / -sun_y, 10.0f);
                /* Fade into occlusion as the water column gains thickness.
                   A short ray crossing the first solid probe otherwise casts
                   a full-strength shadow along a single horizontal row. */
                float coverage = fminf(1.0f, fmaxf(0.0f, limit / 16.0f));
                sv = 1.0f - coverage * (1.0f - fminf(sv, 1.0f));
            }
            /* Sunlit rock contributes to the same radiance field that bounce
               rays sample on the next frame. Water filters the incoming ray. */
            if (fx->sunshadow && fx->lights && solid[p] && sv > 0.0f) {
                float depth = fmaxf(0.0f, fy0 - fmaxf(surface, 0.0f));
                int wet = clampi((int)(2.0f * depth / -sun_y), 0, 319);
                float sun = 0.48f * sv;
                direct[0][p] += sun * light_absorb[0][wet];
                direct[1][p] += sun * 0.96f * light_absorb[1][wet];
                direct[2][p] += sun * 0.86f * light_absorb[2][wet];
            }
            sun_vis[p] = sv;
        }
}

static void transport(const WorldSceneFx *fx)
{
    /* Glow has its own switch: request radiance without enabling surface lighting. */
    WorldSceneFx requested=*fx;
    requested.lights=fx->lights || fx->glow;
    fx=&requested;
    if (!fx->lights && !fx->sunshadow) {
        if (fx->debris || fx->silt || fx->sparks || fx->impact_motes || fx->pressure || fx->sediment_light || fx->water_volume || fx->aftermath) build_solid();
        transport_valid = 0; return;
    }
    if (transport_scene != cur.scene_id) { transport_valid = 0; transport_scene = cur.scene_id; }
    transport_fx = fx;
    transport_waterline = g.waterline;
    build_solid();
    if (fx->lights) gather_lights();
    else light_count = 0;
    if (!transport_valid) { memset(total, 0, sizeof total); memset(bounce, 0, sizeof bounce); }
    bin_lights();
    trace_mask();
    parallel(transport_rows, PH);
    reconstruct_transport();
    for (int c = 0; c < 3; ++c)
        for (int p = 0; p < PN; ++p) total[c][p] = direct[c][p] + bounce[c][p];
    /* A soft shoulder so a big blast tints its surroundings instead of
       burning them to white. The bounce pass reads total, so it stays in the
       same range. */
    for (int p = 0; p < PN; ++p) light_shoulder(&total[0][p],&total[1][p],&total[2][p]);
    cache_transport();
    ++transport_frame;
    transport_valid = 1;
}

/* Bilinear look-ups at a screen pixel. */
static inline float probe_sample(const float *map, int x, int y)
{
    float fx = (x - 1) * 0.5f, fy = (y - 1) * 0.5f;
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int ix = (int)fx, iy = (int)fy;
    if (ix >= PW - 1) ix = PW - 2;
    if (iy >= PH - 1) iy = PH - 2;
    float ax = fx - ix, ay = fy - iy;
    if (ax > 1) ax = 1;
    if (ay > 1) ay = 1;
    const float *m = map + iy * PW + ix;
    return (m[0] * (1 - ax) + m[1] * ax) * (1 - ay) + (m[PW] * (1 - ax) + m[PW + 1] * ax) * ay;
}

/* Shared by both layers and both display refreshes; no repeated bilinear
 * lookups or light-direction normalization in the per-layer pixel loop. */
static PixelLight average_light(PixelLight a, PixelLight b)
{
    return (PixelLight){(a.r+b.r)*0.5f,(a.g+b.g)*0.5f,(a.b+b.b)*0.5f,
        (a.dx+b.dx)*0.5f,(a.dy+b.dy)*0.5f,(a.sun+b.sun)*0.5f};
}
static void cache_transport(void)
{
    /* Exact separable form of probe_sample at integer screen coordinates.
     * Copy anchors once, then average the gaps instead of six bilinear calls
     * per pixel. Keep raw direction until after interpolation. */
    for (int y=0;y<PH;++y) {
        PixelLight *row=pixel_light+(2*y+1)*W;
        for (int x=0;x<PW;++x) {
            int p=y*PW+x;
            row[2*x+1]=(PixelLight){total[0][p],total[1][p],total[2][p],light_dx[p],light_dy[p],sun_vis[p]};
        }
        row[0]=row[1];
        for (int x=1;x<PW;++x) row[2*x]=average_light(row[2*x-1],row[2*x+1]);
    }
    memcpy(pixel_light,pixel_light+W,W*sizeof *pixel_light);
    for (int y=2;y<H;y+=2) for (int x=0;x<W;++x)
        pixel_light[y*W+x]=average_light(pixel_light[(y-1)*W+x],pixel_light[(y+1)*W+x]);
    for (int p=0;p<W*H;++p) {
        PixelLight *l=&pixel_light[p];float inv=1.0f/sqrtf(l->dx*l->dx+l->dy*l->dy+0.0001f);
        l->dx*=inv;l->dy*=inv;
    }
}

/* ---- per refresh: layer colours and per-layer effects ------------------- */

static int wf_rock(float x,float y);
static void wf_pixel(uint32_t *out,int x,int y,int r,int gr,int b,int a,int underwater);
#include "world_scene_cinema.h"

static void colour_rows(int from, int to)
{
    for (int layer = 0; layer < 3; ++layer) {
        if (!layer_on[layer]) continue;
        for (int i = from * LW; i < to * LW; ++i) {
            unsigned index = layer_idx[layer][i];
            uint32_t c = index == 0xffff ? 0 : cur.colors[index];
            layer_col[layer][i] = c;
            layer_lum[layer][i] = (uint8_t)((((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 5 + (c & 255) * 2) / 10);
        }
    }
}

/* Effects that belong to the scenery itself, on the two world layers. They
 * are computed in the layer's own coordinates, so they stay fixed to it. */
static void layer_rows_fx(int from, int to)
{
    const WorldSceneFx *fx = g.fx;
    const int lit = (fx->lights || fx->glow) && transport_valid, shadowed = fx->sunshadow && transport_valid;
    /* Toward the sun, along the light shafts (screen y points down). */
    const float sun_x = 0.40f, sun_y = -0.80f, sun_z = 0.45f;
    for (int layer = 1; layer < 3; ++layer) {
        if (!layer_on[layer]) continue;
        const float reach = layer == 1 ? 1.0f : 0.55f;     /* the far layer is behind the lit plane */
        for (int by = from; by < to; ++by) {
            int y = by - MARGIN;
            if(y>=H){memset(layer_wat[layer]+by*LW,0,LW);continue;}
            int wy = y + g.scroll_y[layer];
            int depth = y - g.waterline;
            int under = g.waterline < 0 ? 256 : depth < 0 ? 0 : depth >= 8 ? 256 : (depth + 1) * 32;
            if (depth < 0) depth = 0;
            int ray_fade = depth < 190 ? 190 - depth : 0;
            float sun_depth = !under ? 1.0f : depth < 300 ? 1.0f - depth / 360.0f : 0.17f;
            int sy = clampi(y, 0, H - 1), in_play = y >= 0 && y < H;
            for (int bx = 0; bx < LW; ++bx) {
                int i = by * LW + bx;
                uint32_t p = layer_col[layer][i];
                if (!(p >> 24)) { layer_wat[layer][i] = 0; continue; }
                int r = (p >> 16) & 255, gr = (p >> 8) & 255, b = p & 255;
                const int r0 = r, g0 = gr, b0 = b;          /* the surface's own colour */
                int x = bx - MARGIN, wx = x + g.scroll_x[layer], sx = clampi(x, 0, W - 1);
                int lum = layer_lum[layer][i];
                int w = water_amount(p) * under >> 8;
                layer_wat[layer][i] = (uint8_t)w;
                int surface = w < 128;                       /* rock, ice, wrecks: not open water */
                float sv = shadowed && in_play ? pixel_light[sy*W+sx].sun : 1.0f;

                if (fx->depth && under) {
                    /* Water absorbs red first, then green: the light that
                       reaches this depth from the surface. Rock and wrecks keep most of their colour. */
                    int d = depth > 319 ? 319 : depth;
                    int amount = w + ((255 - w) >> 2);
                    r = r * (256 - (amount * (256 - absorb[0][d]) >> 8)) >> 8;
                    gr = gr * (256 - (amount * (256 - absorb[1][d]) >> 8)) >> 8;
                    b = b * (256 - (amount * (256 - absorb[2][d]) >> 8)) >> 8;
                }
                if (fx->haze && layer == 2 && under) {
                    /* Distance in water: the far layer drifts toward blue. */
                    int t = 23 * under >> 8;
                    r += (6 - r) * t >> 8; gr += (34 - gr) * t >> 8; b += (64 - b) * t >> 8;
                }
                /* Expose ambient scenery before adding direct and scattered light.
                   Submerged distance and traced sunlight set the local exposure. */
                if (fx->lights && under) {
                    float distance = clampi(depth, 0, 220) / 220.0f;
                    float exposure = 1.0f - (under / 256.0f) *
                        (0.09f + 0.24f * distance + 0.24f * (1.0f - sv));
                    r = (int)(r * exposure);
                    gr = (int)(gr * exposure);
                    b = (int)(b * exposure);
                }
                /* Surface normal from the pixel art's shading (bright = raised). */
                float nx = 0.0f, ny = 0.0f, nz = 1.0f;
                if (fx->normals && surface && under && bx > 0 && by > 0 && bx < LW - 1 && by < LH - 1) {
                    const uint8_t *L = layer_lum[layer] + i;
                    int gx = (L[-LW + 1] + 2 * L[1] + L[LW + 1]) - (L[-LW - 1] + 2 * L[-1] + L[LW - 1]);
                    int gy = (L[LW - 1] + 2 * L[LW] + L[LW + 1]) - (L[-LW - 1] + 2 * L[-LW] + L[-LW + 1]);
                    nx = -gx / 360.0f; ny = -gy / 360.0f;
                    float inv = 1.0f / sqrtf(nx * nx + ny * ny + 1.0f);
                    nx *= inv; ny *= inv; nz = inv;
                    /* Sunlight on the slope, relative to a flat surface, with
                       a highlight on bright (wet, icy) pixels. */
                    float ndl = nx * sun_x + ny * sun_y + nz * sun_z;
                    float sun = sv * sun_depth * (128 - w) / 128.0f * under / 256.0f;
                    int add = (int)((ndl - sun_z) * 190.0f * sun);
                    r += add; gr += add; b += add;
                    if (lum > 110) {
                        float hx = sun_x, hy = sun_y, hz = sun_z + 1.0f;
                        float hl = 1.0f / sqrtf(hx * hx + hy * hy + hz * hz);
                        float ndh = (nx * hx + ny * hy + nz * hz) * hl;
                        if (ndh > 0.9f) {
                            float s = (ndh - 0.9f) * 10.0f;
                            s = s * s * s * s;
                            int spec = (int)(s * sun * (lum - 110) * 0.9f);
                            r += spec; gr += spec; b += spec;
                        }
                    }
                }
                if (shadowed && surface && under && in_play) {
                    /* Rock out of the sun's reach is a little darker. */
                    int keep = 222 + (int)(34 * sv);
                    r = r * keep >> 8; gr = gr * keep >> 8; b = b * keep >> 8;
                }
                if (lit && in_play) {
                    const PixelLight *light = &pixel_light[sy*W+sx];
                    float lr = light->r, lg = light->g, lb = light->b;
                    if (lr + lg + lb > 0.004f) {
                        if (surface && fx->lights) {
                            /* Lit surfaces show their own colour (not the
                               water's absorption), facing the light more. */
                            float shade = 1.0f;
                            if (fx->normals) {
                                float ndl = (nx * light->dx + ny * light->dy) * 0.8f + nz * 0.6f;
                                shade = 0.25f + 1.1f * (ndl < 0 ? 0 : ndl);
                            }
                            float k = 1.18f * reach * shade * (128 - w) / 128.0f;
                            r += (int)((r0 + 70) * lr * k); gr += (int)((g0 + 70) * lg * k); b += (int)((b0 + 70) * lb * k);
                        }
                        if (fx->glow && under) {
                            /* Light scattered by the water itself: halos,
                               strongest in open water. */
                            float k = reach * (w * 0.5f + 40.0f) * under / 256.0f * (fx->water_volume ? .65f : 1.0f);
                            r += (int)(lr * k); gr += (int)(lg * k); b += (int)(lb * k);
                        }
                    }
                }
                if (fx->caustics && w) {
                    int u = wx * 216 + (F(wy * 152 + g.Q * 3) >> 1) * 8;
                    int v = wy * 264 + (F(wx * 184 - g.Q * 2) >> 1) * 8;
                    int n = F(u + g.Q * 2) + F(v - g.Q * 3) + F((u + v) / 2 + g.Q);
                    if (n < 0) n = -n;
                    int ridge = 256 - n * 2 / 3;
                    if (ridge > 0) {
                        ridge = ridge * ridge >> 8;
                        ridge = ridge * ridge >> 8;
                        int fade = depth < 300 ? 300 - depth : 0;
                        int add = (int)(ridge * w * fade / (255 * 300) * 0.43f * sv);
                        r += add * 50 >> 8; gr += add * 95 >> 8; b += add * 105 >> 8;
                    }
                }
                if (fx->rays && w && ray_fade) {
                    int s = (wx * 2 + wy) * 8;
                    int n = F(s * 2 + g.Q) + F(s * 5 - g.Q * 2 + 2400) + F(s + g.Q / 2 + 5600);
                    if (n > 60) {
                        int add = (int)((n - 60) * ray_fade * w / (190 * 255) * sv * (fx->water_volume ? .65f : 1.0f));
                        r += add * 20 >> 8; gr += add * 36 >> 8; b += add * 42 >> 8;
                    }
                }
                layer_col[layer][i] = y < H ? light_rgb(r,gr,b) :
                    0xff000000u | (uint32_t)clamp255(r)<<16 | (uint32_t)clamp255(gr)<<8 | (uint32_t)clamp255(b);
            }
        }
    }
}

/* ---- composite ------------------------------------------------------------ */

static void compose_rows(int from, int to)
{
    const WorldSceneFx *fx = g.fx;
    const int k = g.k, ow = g.ow, oh = g.oh;
    const int shadow_x[3] = {0, 2 * k, 5 * k}, shadow_y[3] = {0, 3 * k, 6 * k};
    const int shadow_keep[3] = {256, 180, 208};
    const uint32_t backdrop = cur.colors[0];
    struct Memo {int mat_cell,post_cell;uint32_t mat,base,shaded,input,result;} memo[W];
    if(fx->materials || fx->vivid || fx->surface)memset(memo,0,sizeof memo);
    for (int Y = from; Y < to; ++Y) {
        int y1 = Y / k;
        uint32_t *o = g.out + (size_t)Y * ow;

        int row_dx = g.row_dx[y1]+g.shake_x, row_dy = g.shake_y;
        int glint = fx->surface && g.waterline >= 0 && y1 >= g.waterline - 3 && y1 <= g.waterline + 1;
        int block_row = Y % k == 0;
        for (int X = 0; X < ow; ++X) {
            int x1 = divide[X];
            int dx = row_dx, dy = row_dy;
            if (field_on) {
                dx += field_x[y1 * W + x1] * k / 4;
                dy += field_y[y1 * W + x1] * k / 4;
            }
            /* Nearest layer first: the first opaque one is the pixel. Offsets
               stay inside the margin, so no clamping is needed. */
            int U = X + dx, V = Y + dy;
            int protect=fx->pressure || fx->shock || fx->surface;
            int base_u=protect?X+g.shake_x:U,base_v=protect?Y+g.shake_y:V;
            uint32_t c = backdrop;
            int id = -1, wet = 0, at[3] = {-1, -1, -1};
            for (int layer = 0; layer < 3; ++layer) {
                if (!layer_on[layer]) continue;
                int i = layer ? divide[base_v + g.off_y[layer]] * LW + divide[base_u + g.off_x[layer]]
                              : divide[Y + g.off_y[0]] * LW + divide[X + g.off_x[0]];
                at[layer] = i;
                uint32_t p = layer_col[layer][i];
                if(layer==2 && !layer_pri[2][i] && protect){
                    int far_i=divide[V+g.off_y[2]]*LW+divide[U+g.off_x[2]];
                    p=layer_col[2][far_i];
                }
                if (!(p >> 24)) continue;
                c = p;
                id = layer;
                wet = layer ? layer_wat[layer][i] : 0;
                break;
            }
            c=cinema_background(c,id,x1,y1);
            int qx = y1 >= HUD ? X : base_u, qy = y1 >= HUD ? Y : base_v;
            /* Drop shadows belong underwater, never on above-surface scenery or sky. */
            if (fx->shadows && (g.waterline < 0 || y1 >= g.waterline) && id > 0 && wet < 90) {
                int sx = qx - shadow_x[id], sy = qy - shadow_y[id];
                if (sx >= 0 && sy >= 0 && sx < ow && sy < oh && sprite_px[(size_t)sy * ow + sx] != NONE) {
                    int keep = shadow_keep[id];
                    c = 0xff000000u | ((((c >> 16) & 255) * keep >> 8) << 16) | ((((c >> 8) & 255) * keep >> 8) << 8) |
                        ((c & 255) * keep >> 8);
                }
            }
            if (qx >= 0 && qy >= 0 && qx < ow && qy < oh) {
                uint16_t sprite = sprite_px[(size_t)qy * ow + qx];
                if (sprite != NONE) {
                    int blocked = 0;
                    if (sprite & 0x8000) {
                        /* A sprite behind the tiles is hidden by any layer's
                           front pens here, including layers behind the pixel. */
                        for (int layer = 0; layer < 3 && !blocked; ++layer) {
                            if (!layer_on[layer]) continue;
                            int i = at[layer];
                            if (i < 0)
                                i = divide[base_v + g.off_y[layer]] * LW + divide[base_u + g.off_x[layer]];
                            blocked = layer_pri[layer][i] && (layer_col[layer][i] >> 24);
                        }
                    }
                    if (!blocked) {
                        c = cur.colors[sprite & 1023];
                        id = 3;
                        uint32_t mat=fx->materials?sprite_material[(size_t)qy*ow+qx]:0;
                        if(mat && y1<HUD){
                            int cell=y1*W+x1+1;struct Memo *m=&memo[x1];
                            if(cell!=m->mat_cell || mat!=m->mat || c!=m->base){
                                m->mat_cell=cell;m->mat=mat;m->base=c;m->shaded=material_shade(c,mat,x1,y1);
                            }
                            c=m->shaded;
                        }
                        else if (fx->lights && transport_valid && y1 < HUD) {
                            float lr = pixel_light[y1*W+x1].r, lg = pixel_light[y1*W+x1].g,
                                  lb = pixel_light[y1*W+x1].b;
                            int r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                            /* Fire does not light itself; darker paint takes the most. */
                            if (lr + lg + lb > 0.004f && !(r >= 176 && r >= b + 40)) {
                                float k = 0.45f * (300 - (r * 3 + gr * 5 + b * 2) / 10) / 300.0f;
                                r += (int)((r + 20) * lr * k); gr += (int)((gr + 20) * lg * k);
                                b += (int)((b + 20) * lb * k);
                                c = light_rgb(r,gr,b);
                            }
                        }
                    }
                }
            }
            if (block_row && X % k == 0) {
                water_map[y1 * W + x1] = (uint8_t)(id > 0 && id < 3 ? wet : 0);
            }

            /* Most pixels need none of the screen-space colour work. */
            int cell = y1 * W + x1;
            int hud_pixel = y1 >= HUD && (id == 0 || id == 3);
            if (y1 >= HUD) hud_overlay[(size_t)(Y - HUD*k)*ow + X] = hud_pixel ? c : 0;
            int needs = !hud_pixel && (glint || fx->vivid);
            struct Memo *post=&memo[x1];uint32_t post_input=c;
            int post_tag=(cell+1)*2+(glint && id>0 && id<3 && wet>60);
            if(needs && post->post_cell==post_tag && post->input==c)c=post->result;
            else if (needs) {
                int r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                if (glint && id>0 && id<3 && wet>60) {
                    int wx = x1 + g.scroll_x[1];
                    int n = S(wx * 41 + g.T * 6) + S(wx * 17 - g.T * 4) + S(wx * 7 + y1 * 90 + g.T * 2);
                    if (n > 430) { int add = (n - 430) / 2; r += add; gr += add; b += add; }
                }
                if (fx->vivid) {
                    int lum = (r * 3 + gr * 5 + b * 2) / 10;
                    r = lum + (r - lum) * 17 / 16; gr = lum + (gr - lum) * 17 / 16; b = lum + (b - lum) * 17 / 16;
                }
                c = light_rgb(r,gr,b);
                post->post_cell=post_tag;post->input=post_input;post->result=c;
            }
            o[X] = c;
        }
    }
}

/* ---- the sea surface as a mirror ----------------------------------------- */

/* Just below the waterline the water reflects what is above it: the rows
 * above, mirrored, rippling and fading with depth, on open water only. */
static void reflect_surface(uint32_t *out)
{
    int wl = g.waterline, k = g.k;
    if (wl < 12 || wl > H - 12) return;
    int reach = wl < 30 ? wl : 30;
    if (wl + reach > H) reach = H - wl;
    for (int d = 0; d < reach; ++d) {
        int a = (int)(80.0f * (1.0f - (float)d / reach));
        int ripple = (S(d * 53 + g.T * 6) + S(d * 19 - g.T * 4)) * (d + 3) * k / 2400;
        for (int sub = 0; sub < k; ++sub) {
            int Y = (wl + d) * k + sub, SY = (wl - 1 - d) * k + (k - 1 - sub);
            if (SY < 0 || Y >= g.oh) continue;
            uint32_t *o = out + (size_t)Y * g.ow;
            const uint32_t *src = out + (size_t)SY * g.ow;
            const uint8_t *wet = water_map + (wl + d) * W;
            for (int X = 0; X < g.ow; ++X) {
                int w = wet[divide[X]];
                if (w < 60) continue;
                int sx=X+g.shake_x,sy=Y+g.shake_y;
                if(sx>=0 && sx<g.ow && sy>=0 && sy<g.oh && sprite_px[(size_t)sy*g.ow+sx]!=NONE)continue;
                int t = a * w >> 8;
                int SX = clampi(X + ripple, 0, g.ow - 1);
                uint32_t q = o[X], m = src[SX];
                int r = (int)((q >> 16) & 255), gr = (int)((q >> 8) & 255), b = (int)(q & 255);
                r += ((int)((m >> 16) & 255) - r) * t >> 8;
                gr += ((int)((m >> 8) & 255) - gr) * t >> 8;
                b += ((int)(m & 255) - b) * t >> 8;
                o[X] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)gr << 8 | (uint32_t)b;
            }
        }
    }
}

#include "world_scene_waterfx.h"

/* ---- explosions: rings, shake, particles -------------------------------- */
extern unsigned world_scene_explosions;

static void spawn_particle(float x, float y, int kind)
{
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *p = &particles[i];
        if (p->life > 0 && p->age < p->life) continue;
        p->x = x; p->y = y; p->age = 0; p->kind = kind;
        if (kind == 0) {            /* ember */
            p->vx = (frand() - 0.5f) * 70.0f; p->vy = -20.0f - frand() * 50.0f; p->life = 0.35f + frand() * 0.45f;
        } else {                    /* bubble */
            p->vx = (frand() - 0.5f) * 26.0f; p->vy = -24.0f - frand() * 40.0f; p->life = 0.8f + frand() * 1.0f;
        }
        return;
    }
}

/* A cell full of fire where there was none a frame ago is a new explosion. */
static void detect_explosions(const WorldSceneFx *fx)
{
    for (int cy = 0; cy < H / 8; ++cy)
        for (int cx = 0; cx < GW; ++cx) {
            int cell = cy * GW + cx;
            if (fx->silt && hot_count[cell] >= 8 && hot_before[cell] < 3)
                waterfx_impact(cx * 8 + 4.0f, cy * 8 + 4.0f);
            if ((fx->debris || fx->sparks || fx->impact_motes || fx->pressure || fx->sediment_light || fx->water_volume || fx->aftermath) && hot_count[cell] >= 10 && hot_before[cell] < 3) {
                int area = 0, peak = 1;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        int x = cx + i, y = cy + j;
                        if (x < 0 || x >= GW || y < 0 || y >= H / 8) continue;
                        int q = y * GW + x;
                        area += hot_count[q];
                        if (hot_count[q] > hot_count[cell] || (hot_count[q] == hot_count[cell] && q < cell)) peak = 0;
                    }
                if (peak && area >= 24) waterfx_blast(cx * 8 + 4.0f, cy * 8 + 4.0f, area, fx);
            }
            if (hot_count[cell] < 26) continue;
            /* A real fireball is big, is the brightest cell of its patch, and
               sits where nothing burned a frame ago (moving flames, shots and
               the submarine itself always have a hot neighbour behind them). */
            int around = 0, size = 0, peak = 1;
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i) {
                    int x = cx + i, y = cy + j;
                    if (x < 0 || y < 0 || x >= GW || y >= GH) continue;
                    if (hot_before[y * GW + x] > around) around = hot_before[y * GW + x];
                    if (abs(i) <= 1 && abs(j) <= 1) size += hot_count[y * GW + x];
                    if (hot_count[y * GW + x] > hot_count[cell]) peak = 0;
                }
            if (around > 3 || size < 70 || !peak) continue;
            float x = cx * 8 + 4.0f, y = cy * 8 + 4.0f;
            int near_ring = 0;
            for (int i = 0; i < MAX_RINGS; ++i)
                if (rings[i].life > 0 && rings[i].age < 0.2f && fabsf(rings[i].x - x) < 28 && fabsf(rings[i].y - y) < 28)
                    near_ring = 1;
            if (near_ring) continue;
            int slot = 0;
            for (int i = 0; i < MAX_RINGS; ++i) {
                if (rings[i].life <= 0 || rings[i].age >= rings[i].life) { slot = i; break; }
                if (rings[i].age > rings[slot].age) slot = i;
            }
            rings[slot] = (Ring){x, y, 0, 0.5f, 1.0f};
            ++world_scene_explosions;
            waterfx_blast(x, y, size, fx);
            if (fx->shake && size >= 110) {       /* only the big ones shake the screen */
                shake += 1.0f;
                if (shake > 2.5f) shake = 2.5f;
                if (rumble < 40000) rumble += 22000;
            }
            if (fx->particles) {
                for (int i = 0; i < 7; ++i) spawn_particle(x + (frand() - 0.5f) * 14, y + (frand() - 0.5f) * 14, 0);
                for (int i = 0; i < 5; ++i) spawn_particle(x + (frand() - 0.5f) * 20, y + (frand() - 0.5f) * 12, 1);
            }
        }
    /* Burning things keep shedding the odd ember. */
    if (fx->particles)
        for (int cell = 0; cell < GW * (H / 8); ++cell)
            if (hot_count[cell] >= 24 && frand() < 0.10f)
                spawn_particle((cell % GW) * 8 + frand() * 8, (cell / GW) * 8 + frand() * 8, 0);
    memcpy(hot_before, hot_count, sizeof hot_before);
}

static void build_field(const WorldSceneFx *fx, float dt)
{
    field_on = 0;
    int any = 0;
    for (int i = 0; i < MAX_RINGS; ++i) {
        Ring *ring = &rings[i];
        if (ring->life <= 0) continue;
        ring->age += dt;
        if (ring->age >= ring->life) { ring->life = 0; continue; }
        any = 1;
    }
    if (!any || !fx->shock || fx->pressure) return;
    memset(field_x, 0, sizeof field_x);
    memset(field_y, 0, sizeof field_y);
    field_on = 1;
    for (int i = 0; i < MAX_RINGS; ++i) {
        const Ring *ring = &rings[i];
        if (ring->life <= 0) continue;
        float t = ring->age / ring->life, radius = 6.0f + 110.0f * t, band = 9.0f;
        float strength = 10.0f * (1.0f - t) * ring->strength;        /* quarter pixels */
        float inner = radius > band ? radius - band : 0.0f, outer = radius + band;
        int x0 = clampi((int)(ring->x - radius - band), 0, W - 1), x1 = clampi((int)(ring->x + radius + band), 0, W - 1);
        int y0 = clampi((int)(ring->y - radius - band), g.waterline<0?0:clampi(g.waterline,0,H-1), H - 1), y1 = clampi((int)(ring->y + radius + band), 0, H - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - ring->x, dy = y - ring->y, d2 = dx * dx + dy * dy;
                /* Only the thin ring needs a square root, not its entire box. */
                if (d2 >= outer * outer || d2 < inner * inner) continue;
                float d = sqrtf(d2);
                float off = fabsf(d - radius);
                if (off >= band || d < 1.0f) continue;
                float push = strength * (1.0f - off / band);
                int j = y * W + x;
                field_x[j] = (int8_t)clampi(field_x[j] - (int)(dx / d * push), -16, 16);
                field_y[j] = (int8_t)clampi(field_y[j] - (int)(dy / d * push), -16, 16);
            }
    }
}

static inline void add_block(uint32_t *out, int x, int y, int size, int r, int gr, int b)
{
    for (int j = 0; j < size; ++j) {
        int py = y + j;
        if (py < 0 || py >= H*g.k) continue;
        for (int i = 0; i < size; ++i) {
            int px = x + i;
            if (px < 0 || px >= g.ow) continue;
            int sx=px+g.shake_x,sy=py+g.shake_y;
            if(sx>=0 && sx<g.ow && sy>=0 && sy<g.oh && sprite_px[(size_t)sy*g.ow+sx]!=NONE)continue;
            if(water_map[(py/g.k)*W+px/g.k]<60)continue;
            uint32_t p = out[(size_t)py * g.ow + px];
            out[(size_t)py * g.ow + px] = 0xff000000u | (uint32_t)clamp255((int)((p >> 16) & 255) + r) << 16 |
                                          (uint32_t)clamp255((int)((p >> 8) & 255) + gr) << 8 |
                                          (uint32_t)clamp255((int)(p & 255) + b);
        }
    }
}

static void draw_particles(uint32_t *out, const WorldSceneFx *fx, float dt, uint32_t time_ms)
{
    int k = g.k;
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *p = &particles[i];
        if (!fx->particles) {p->life=0;continue;}
        if (p->life <= 0) continue;
        p->age += dt;
        if (p->age >= p->life) { p->life = 0; continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->kind == 0) {
            int wet=g.waterline<0 || p->y>=g.waterline;
            p->vx/=1+dt*(wet?7:2);p->vy-=dt*(wet?10:30);
            if(wet && p->life>.28f)p->life=.28f;
        }
        else p->x += sinf(p->age * 9.0f + (float)i) * 10.0f * dt;
        if (!fx->particles) continue;
        int x = (int)p->x, y = (int)p->y;
        if (x < 0 || y < 0 || x >= W || y >= H) { if (y < -4) p->life = 0; continue; }
        if (p->kind == 1 && g.waterline >= 0 && y < g.waterline + 2) { p->life = 0; continue; }
        float fade = fmaxf(0,1.0f - p->age / p->life);
        int px = (int)(p->x * k), py = (int)(p->y * k);
        if (p->kind == 0) add_block(out, px, py, k, (int)(230 * fade), (int)(120 * fade), (int)(30 * fade));
        else add_block(out, px, py, k < 2 ? 1 : k - 1, (int)(70 * fade), (int)(110 * fade), (int)(120 * fade));
    }
    /* Marine snow on two parallax layers, over open water only. */
    if (fx->snow)
        for (unsigned n = 0; n < 80; ++n) {
            unsigned h = n * 2654435761u;
            int layer = (int)(n & 1);
            unsigned speed = layer ? 9 : 5;
            int drift_x = layer ? g.scroll_x[1] : g.scroll_x[2], drift_y = layer ? g.scroll_y[1] : g.scroll_y[2];
            int x = (int)((h >> 8) % W) - (int)(time_ms * speed / 1000 % W) + (S((int)(time_ms / 9 + (h >> 4))) >> 6) - drift_x;
            int y = (int)((h >> 18) % H) + (int)(time_ms * (speed + 2) / 1400 % H) - drift_y;
            x = ((x % W) + W) % W;
            y = ((y % H) + H) % H;
            if (water_map[y * W + x] < 70) continue;
            int add=(layer?42:24)+(S((int)(h>>12)+g.T/6)>>6);
            add_block(out,x*k,y*k,k,add*2/3,add,add*9/8);
        }
}

/* ---- render ---------------------------------------------------------------- */

/* Stage timing for tuning (WORLD_SCENE_RUN prints it). */
double world_scene_stage_ms[6];
double world_scene_waterfx_draw_ms;
unsigned world_scene_explosions;      /* explosions detected so far */
static double stamp(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

int world_scene_render(uint32_t *out, unsigned scale, double alpha, const WorldSceneFx *fx, uint32_t clock)
{
    static int audit_selection=-2;
    if(audit_selection==-2){const char *v=getenv("WORLD_SCENE_OPTION");audit_selection=v?atoi(v):-1;}
    WorldSceneFx audit_fx={0};
    if(audit_selection>=0){
        switch(audit_selection){
        case 1: audit_fx.caustics=1;break;
        case 2: audit_fx.rays=1;break;
        case 3: audit_fx.depth=1;break;
        case 4: audit_fx.snow=1;break;
        case 5: audit_fx.vivid=1;break;
        case 7: audit_fx.trails=1;break;
        case 8: audit_fx.lights=1;break;
        case 9: audit_fx.shadows=1;break;
        case 10: audit_fx.dof=1;break;
        case 11: audit_fx.haze=1;break;
        case 12: audit_fx.normals=1;break;
        case 13: audit_fx.sunshadow=1;break;
        case 14: audit_fx.glow=1;break;
        case 15: audit_fx.reflect=1;break;
        case 17: audit_fx.shock=1;break;
        case 18: audit_fx.particles=1;break;
        case 19: audit_fx.shake=1;break;
        case 20: audit_fx.surface=1;break;
        case 21: audit_fx.smooth=1;break;
        case 22: audit_fx.interp=1;break;
        case 23: audit_fx.wakes=1;break;
        case 24: audit_fx.debris=1;break;
        case 25: audit_fx.silt=1;break;
        case 26: audit_fx.splashes=1;break;
        case 27: audit_fx.sparks=1;break;
        case 28: audit_fx.impact_motes=1;break;
        case 29: audit_fx.pressure=1;break;
        case 30: audit_fx.sediment_light=1;break;
        case 31: audit_fx.water_volume=1;break;
        case 32: audit_fx.ocean_depth=1;break;
        case 33: audit_fx.aftermath=1;break;
        case 34: audit_fx.materials=1;break;
        default:break;
        }
        fx=&audit_fx;
    }
    if (!cur.valid || !out) return 0;
    if (!ready) prepare();
    int k = scale < 1 ? 1 : scale > MAXK ? MAXK : (int)scale;
    g.k = k; g.ow = W * k; g.oh = H * k; g.fx = fx;
    if (divide_k != k) {
        for (int i = 0; i < LW * MAXK + 8; ++i) divide[i] = (uint16_t)(i / k);
        divide_k = k;
        have_history = 0;
    }
    uint32_t time_ms = clock / 8;
    float dt = last_clock ? (float)(clock - last_clock) / 8000.0f : 0.0f;
    if (dt < 0 || dt > 0.1f) dt = 0.016f;
    last_clock = clock;
    g.T = (int)(time_ms / 12);
    g.Q = (int)(clock / 12);
    g.waterline = cur.waterline;
    waterline_used = cur.waterline < 0 ? 0 : cur.waterline > H ? H : cur.waterline;
    if (alpha < 0) alpha = 0;
    if (alpha > 1 || !fx->interp) alpha = 1;

    /* Where each layer is: between the previous and the current true scroll
       (whole pixels plus the camera's fraction), in subpixels. */
    for (int layer = 0; layer < 3; ++layer) {
        g.scroll_x[layer] = cur.layers[layer].scroll_x;
        g.scroll_y[layer] = cur.layers[layer].scroll_y;
        double now_x = fx->smooth ? cur.frac[layer] / 256.0 : 0.0, now_y = 0.0;
        double x = now_x, y = now_y;
        int dx = (int16_t)(prev.layers[layer].scroll_x - cur.layers[layer].scroll_x);
        int dy = (int16_t)(prev.layers[layer].scroll_y - cur.layers[layer].scroll_y);
        if (alpha < 1 && abs(dx) <= 16 && abs(dy) <= 16) {
            double before_x = dx + (fx->smooth ? prev.frac[layer] / 256.0 : 0.0);
            x = before_x + (now_x - before_x) * alpha;
            y = dy + (now_y - dy) * alpha;
        }
        if (layer_rows[layer]) x = 0;                 /* per-row scroll: left as the game set it */
        g.off_x[layer] = (int)floor(x * k + 0.5) + MARGIN * k;
        g.off_y[layer] = (int)floor(y * k + 0.5) + MARGIN * k;
    }

    /* Ripples stay local to the surface; full-screen shimmer was removed. */
    for (int y = 0; y < H; ++y) {
        int shift = 0;
        int below = g.waterline < 0 ? 400 : y - g.waterline;
        if (y < H && below >= 0) {
            if (fx->surface && below < 28) shift += S(below * 46 - g.T * 7) * (28 - below) * 2 * k / (28 * 256);
        }
        g.row_dx[y] = shift;
    }
    if (fx->shake && shake > 0.05f) {
        g.shake_x = (int)(shake * k * sinf(time_ms * 0.09f));
        g.shake_y = (int)(shake * k * cosf(time_ms * 0.07f));
        shake *= expf(-dt * 7.0f);
    } else {
        g.shake_x = g.shake_y = 0;
        if (!fx->shake) shake = 0;
    }

    int changed_frame=new_frame;
    int fx_changed=!scene_cache_valid || memcmp(&cached_fx,fx,sizeof *fx)!=0;
    int layers_dirty=changed_frame || fx_changed || (uint32_t)(clock-layer_clock)>=133;
    double t0 = stamp();
    if(layers_dirty){
        parallel(colour_rows,LH);
        if(fx->dof && layer_on[2])soften(layer_col[2]);
    }
    double t1 = stamp();
    if(changed_frame || fx_changed || cached_scale!=k || ((fx->smooth || fx->interp) && sprite_alpha!=alpha))draw_sprites(alpha);
    if(changed_frame || fx_changed)transport(fx);
    if (new_frame) {
        waterfx_begin_frame();
        waterfx_motion(fx);
        detect_explosions(fx);
        new_frame = 0;
    }
    double t2 = stamp();
    if(layers_dirty){parallel(layer_rows_fx,LH);layer_clock=clock;}
    double t3 = stamp();
    build_field(fx, dt);
    cinema_volume_dirty=changed_frame || fx_changed || layers_dirty;
    cinema_update(dt);
    if(changed_frame || fx_changed)material_prepare();

    g.out = out;
    parallel(compose_rows, g.oh);
    double t4 = stamp();

    if (fx->reflect) reflect_surface(out);
    double waterfx_start = stamp();
    waterfx_draw(out, fx, dt);
    cinema_draw(out);
    world_scene_waterfx_draw_ms = stamp() - waterfx_start;
    draw_particles(out, fx, dt, time_ms);

    if (fx->trails) {
        size_t n = (size_t)g.ow * (size_t)(H * k);
        if (have_history) {
            /* Four BGRA pixels at once. 7/16 is the exact phosphor decay. */
            const __m128i zero = _mm_setzero_si128(), seven = _mm_set1_epi16(7);
            for (size_t i = 0; i < n; i += 4) {
                __m128i old = _mm_loadu_si128((const __m128i *)(history + i));
                __m128i lo = _mm_unpacklo_epi8(old, zero), hi = _mm_unpackhi_epi8(old, zero);
                lo = _mm_srli_epi16(_mm_mullo_epi16(lo, seven), 4);
                hi = _mm_srli_epi16(_mm_mullo_epi16(hi, seven), 4);
                __m128i color = _mm_loadu_si128((const __m128i *)(out + i));
                color = _mm_max_epu8(color, _mm_packus_epi16(lo, hi));
                _mm_storeu_si128((__m128i *)(out + i), color);
                _mm_storeu_si128((__m128i *)(history + i), color);
            }
        } else {
            memcpy(history, out, n * sizeof *out);
        }
        have_history = 1;
    } else
        have_history = 0;
    for (size_t i=0, n=(size_t)g.ow*(H-HUD)*k; i<n; ++i)
        if (hud_overlay[i]) out[(size_t)g.ow*HUD*k+i]=hud_overlay[i];
    cached_fx=*fx;cached_scale=k;sprite_alpha=alpha;scene_cache_valid=1;
    double t5 = stamp();
    world_scene_stage_ms[0] += t1 - t0; world_scene_stage_ms[1] += t2 - t1; world_scene_stage_ms[2] += t3 - t2;
    world_scene_stage_ms[3] += t4 - t3; world_scene_stage_ms[4] += t5 - t4; world_scene_stage_ms[5] += 1;
    return 1;
}

unsigned world_scene_take_rumble(void)
{
    unsigned value = rumble > 65535 ? 65535 : rumble;
    rumble = 0;
    return value;
}

int world_scene_waterline(void) { return waterline_used; }

void world_scene_debug(uint32_t *out)
{
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int p = (y / 2) * PW + x / 2;
            int r, gr, b;
            if (solid[p]) { r = (int)(albedo[0][p] * 255); gr = (int)(albedo[1][p] * 255); b = (int)(albedo[2][p] * 255); }
            else r = gr = b = (int)(sun_vis[p] * 90);
            r += (int)(total[0][p] * 200); gr += (int)(total[1][p] * 200); b += (int)(total[2][p] * 200);
            out[y * W + x] = light_rgb(r,gr,b);
        }
    for (int i = 0; i < light_count; ++i) {
        int x = (int)(lights[i].x * 2), y = (int)(lights[i].y * 2);
        if (x >= 0 && y >= 0 && x < W && y < H) out[y * W + x] = 0xffff00ffu;
    }
}
