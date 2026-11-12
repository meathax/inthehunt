#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
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
    MAX_SPRITES = 256, MAX_RINGS = 6, MAX_PARTICLES = 320
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

/* Per game frame: palette indices of each layer around the current scroll. */
static uint16_t layer_idx[3][LN];
static uint8_t layer_pri[3][LN];
static int layer_on[3], layer_rows[3];           /* enabled; per-row scroll (no offsets) */
/* Per refresh: colours after the per-layer effects, and the water mask. */
static uint32_t layer_col[3][LN];
static uint8_t layer_lum[3][LN], layer_wat[3][LN];

static uint16_t sprite_px[MAXK * MAXK * N];      /* palette index | 0x8000 behind tiles */
static uint32_t eye[MAXK * MAXK * N], history[MAXK * MAXK * N];
static int have_history;
static uint16_t lamp[3][GW * GH];                /* sprite light, from the previous refresh */
static uint32_t hot_sum[3][GW * GH];
static uint16_t hot_count[GW * GH], hot_before[GW * GH];
static uint8_t water_map[N];                     /* 1x: open water under this pixel */
static int8_t field_x[N], field_y[N];            /* shockwave displacement, 1/4 px */
static int field_on;
static uint8_t corner[N];
static int16_t sine[8192];
static uint16_t divide[LW * MAXK + 8];           /* subpixel -> buffer pixel for the current K */
static int divide_k;
static int ready;

static Ring rings[MAX_RINGS];
static Particle particles[MAX_PARTICLES];
static float shake;
static unsigned rumble;
static uint32_t last_clock, seed = 0x1234567u;
static int waterline_used;

/* One render's parameters, shared with the worker threads. */
static struct {
    int k, ow, oh;
    uint32_t *out;
    const WorldSceneFx *fx;
    int off_x[3], off_y[3];            /* subpixels, including the margin */
    int shake_x, shake_y;
    int row_dx[H];                     /* subpixels: shimmer and surface ripple */
    int waterline, T, Q;
    int scroll_x[3], scroll_y[3];
} g;

/* ---- small helpers ------------------------------------------------------- */

static inline unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static inline int S(int a) { return sine[(a * 8) & 8191]; }      /* 1024 steps per turn */
static inline int F(int a) { return sine[a & 8191]; }            /* 8192 steps per turn */
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float frand(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 16777216.0f;
}

static void prepare(void)
{
    for (int i = 0; i < 8192; ++i) sine[i] = (int16_t)lrint(256.0 * sin(i * 6.283185307179586 / 8192.0));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double dx = (x - 159.5) / 160.0, dy = (y - 119.5) / 120.0, d = dx * dx + dy * dy;
            corner[y * W + x] = (uint8_t)lrint(255.0 * (1.0 - 0.34 * (d > 0.55 ? (d - 0.55) / 1.45 : 0.0)));
        }
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
    have_history = 0;
    memset(rings, 0, sizeof rings);
    memset(particles, 0, sizeof particles);
    shake = 0;
}

void world_scene_submit(const WorldSceneFrame *frame)
{
    if (!ready) prepare();
    if (!gfx1 || !gfx2) return;
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
    if (abs(dx) > 32 || abs(dy) > 32) dx = dy = 0;
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

static inline void sprite_block(int px, int py, uint16_t value)
{
    int k = g.k;
    if (px <= -k || py <= -k || px >= g.ow || py >= g.oh) return;
    int x0 = px < 0 ? 0 : px, x1 = px + k > g.ow ? g.ow : px + k;
    int y0 = py < 0 ? 0 : py, y1 = py + k > g.oh ? g.oh : py + k;
    for (int y = y0; y < y1; ++y) {
        uint16_t *row = sprite_px + (size_t)y * g.ow;
        for (int x = x0; x < x1; ++x)
            if (row[x] == NONE) row[x] = value;      /* the first sprite owns the pixel */
    }
}

/* Draw the sprite list at K times the resolution. Each sprite sits at its
 * own subpixel position: between its previous and current place for an
 * in-between frame, and shifted with the camera's fraction when it moves
 * with the playfield. Also gathers the light the sprites give off. */
static void draw_sprites(double alpha)
{
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

    for (int i = 0; i < count; ++i) {
        const Sprite *s = &now[i];
        int height = s->rows * 16;
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
                    sprite_block(px, py, index | flags);
                    /* Fire, flashes and tracer light their surroundings. */
                    if (sx >= 0 && sx < W && screen_y >= 0 && screen_y < HUD) {
                        uint32_t colour = cur.colors[index];
                        unsigned r = (colour >> 16) & 255, gr = (colour >> 8) & 255, b = colour & 255;
                        if (r >= 216 && r >= b + 70) {
                            int cell = (screen_y / 8) * GW + sx / 8;
                            hot_sum[0][cell] += r; hot_sum[1][cell] += gr; hot_sum[2][cell] += b;
                            ++hot_count[cell];
                        }
                    }
                }
            }
        }
    }
}

/* ---- blurs ----------------------------------------------------------------- */

static void blur16(uint16_t *plane, int width, int height, int r)
{
    static uint16_t line[W > H ? W : H];
    int span = 2 * r + 1;
    for (int y = 0; y < height; ++y) {
        uint16_t *row = plane + y * width;
        int sum = 0;
        for (int i = -r; i <= r; ++i) sum += row[i < 0 ? 0 : i >= width ? width - 1 : i];
        for (int x = 0; x < width; ++x) {
            line[x] = (uint16_t)(sum / span);
            int add = x + r + 1, drop = x - r;
            sum += row[add >= width ? width - 1 : add] - row[drop < 0 ? 0 : drop];
        }
        memcpy(row, line, (size_t)width * sizeof *row);
    }
    for (int x = 0; x < width; ++x) {
        int sum = 0;
        for (int i = -r; i <= r; ++i) sum += plane[(i < 0 ? 0 : i >= height ? height - 1 : i) * width + x];
        for (int y = 0; y < height; ++y) {
            line[y] = (uint16_t)(sum / span);
            int add = y + r + 1, drop = y - r;
            sum += plane[(add >= height ? height - 1 : add) * width + x] - plane[(drop < 0 ? 0 : drop) * width + x];
        }
        for (int y = 0; y < height; ++y) plane[y * width + x] = line[y];
    }
}

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
    for (int x = 0; x < LW; ++x) {
        for (int y = 0; y < LH; ++y)
            line[y] = MIX(c[(y ? y - 1 : 0) * LW + x], c[y * LW + x], c[(y < LH - 1 ? y + 1 : y) * LW + x]);
        for (int y = 0; y < LH; ++y) c[y * LW + x] = line[y];
    }
#undef MIX
}

/* ---- per refresh: layer colours and per-layer effects ------------------- */

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
    for (int layer = 1; layer < 3; ++layer) {
        if (!layer_on[layer]) continue;
        for (int by = from; by < to; ++by) {
            int y = by - MARGIN;
            int wy = y + g.scroll_y[layer];
            int depth = y - g.waterline;
            int under = g.waterline < 0 ? 256 : depth < 0 ? 0 : depth >= 8 ? 256 : (depth + 1) * 32;
            if (depth < 0) depth = 0;
            int ray_fade = depth < 190 ? 190 - depth : 0;
            int gy = clampi(y - 4, 0, H - 1), cy = gy / 8, fy = gy & 7, cy1 = cy + 1 < GH ? cy + 1 : GH - 1;
            for (int bx = 0; bx < LW; ++bx) {
                int i = by * LW + bx;
                uint32_t p = layer_col[layer][i];
                if (!(p >> 24)) { layer_wat[layer][i] = 0; continue; }
                int r = (p >> 16) & 255, gr = (p >> 8) & 255, b = p & 255;
                int x = bx - MARGIN, wx = x + g.scroll_x[layer];
                /* Open water: blue-dominant and not bright, below the surface. */
                int lum = layer_lum[layer][i];
                int blue = (b - r) * 3;
                if (gr > b) blue -= (gr - b) * 4;
                int w = clamp255(clamp255(blue) * (255 - lum) / 200) * under >> 8;
                layer_wat[layer][i] = (uint8_t)w;

                if (fx->depth && w) {
                    int d = (depth > 239 ? 239 : depth) * w / 255;
                    r = r * (256 - d * 110 / 240) >> 8;
                    gr = gr * (256 - d * 60 / 240) >> 8;
                    b = b * (256 - d * 26 / 240) >> 8;
                }
                if (fx->haze && layer == 2 && under) {
                    /* Distance in water: the far layer drifts toward blue. */
                    int t = 62 * under >> 8;
                    r += (14 - r) * t >> 8; gr += (58 - gr) * t >> 8; b += (98 - b) * t >> 8;
                }
                if (fx->relief && bx > 0 && by > 0 && bx < LW - 1 && by < LH - 1 && w < 128) {
                    /* Light from the upper left: brighter facing it, darker away. */
                    int slope = layer_lum[layer][i - LW - 1] - layer_lum[layer][i + LW + 1];
                    int add = slope * (128 - w) / 300;
                    r += add; gr += add; b += add;
                }
                if (fx->lights) {
                    int gx = clampi(x - 4, 0, W - 1), cx = gx / 8, fx8 = gx & 7, cx1 = cx + 1 < GW ? cx + 1 : GW - 1;
                    int light[3];
                    for (int c = 0; c < 3; ++c) {
                        int a = lamp[c][cy * GW + cx] * (8 - fx8) + lamp[c][cy * GW + cx1] * fx8;
                        int d = lamp[c][cy1 * GW + cx] * (8 - fx8) + lamp[c][cy1 * GW + cx1] * fx8;
                        light[c] = (a * (8 - fy) + d * fy) >> 6;
                    }
                    r += (r + 40) * light[0] >> 9;
                    gr += (gr + 40) * light[1] >> 9;
                    b += (b + 40) * light[2] >> 9;
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
                        int add = ridge * w * fade / (255 * 300) * 7 / 20;
                        r += add * 50 >> 8; gr += add * 95 >> 8; b += add * 105 >> 8;
                    }
                }
                if (fx->rays && w && ray_fade) {
                    int s = (wx * 2 + wy) * 8;
                    int n = F(s * 2 + g.Q) + F(s * 5 - g.Q * 2 + 2400) + F(s + g.Q / 2 + 5600);
                    if (n > 60) {
                        int add = (n - 60) * ray_fade * w / (190 * 255);
                        r += add * 20 >> 8; gr += add * 36 >> 8; b += add * 42 >> 8;
                    }
                }
                layer_col[layer][i] = 0xff000000u | (uint32_t)clamp255(r) << 16 | (uint32_t)clamp255(gr) << 8 |
                                      (uint32_t)clamp255(b);
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
    const int shadow_keep[3] = {256, 150, 190};
    const uint32_t backdrop = cur.colors[0];
    for (int Y = from; Y < to; ++Y) {
        int y1 = Y / k;
        uint32_t *o = g.out + (size_t)Y * ow;
        int row_dx = g.row_dx[y1] + g.shake_x, row_dy = g.shake_y;
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
            uint32_t c = backdrop;
            int id = -1, wet = 0, at[3] = {-1, -1, -1};
            for (int layer = 0; layer < 3; ++layer) {
                if (!layer_on[layer]) continue;
                int i = layer ? divide[V + g.off_y[layer]] * LW + divide[U + g.off_x[layer]]
                              : divide[Y + g.off_y[0]] * LW + divide[X + g.off_x[0]];
                at[layer] = i;
                uint32_t p = layer_col[layer][i];
                if (!(p >> 24)) continue;
                c = p;
                id = layer;
                wet = layer ? layer_wat[layer][i] : 0;
                break;
            }
            int qx = U, qy = V;
            if (fx->shadows && id > 0 && wet < 90) {
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
                                i = divide[V + g.off_y[layer]] * LW + divide[U + g.off_x[layer]];
                            blocked = layer_pri[layer][i] && (layer_col[layer][i] >> 24);
                        }
                    }
                    if (!blocked) {
                        c = cur.colors[sprite & 1023];
                        id = 3;
                    }
                }
            }
            if (block_row && X % k == 0) {
                water_map[y1 * W + x1] = (uint8_t)(id > 0 && id < 3 ? wet : 0);
            }

            /* Most pixels need none of the screen-space colour work. */
            int cell = y1 * W + x1;
            int needs = glint || fx->vivid || (fx->vignette && corner[cell] != 255);
            if (needs) {
                int r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                if (glint && id != 0) {
                    int wx = x1 + g.scroll_x[1];
                    int n = S(wx * 41 + g.T * 6) + S(wx * 17 - g.T * 4) + S(wx * 7 + y1 * 90 + g.T * 2);
                    if (n > 430) { int add = (n - 430) / 2; r += add; gr += add; b += add; }
                }
                if (fx->vivid) {
                    int lum = (r * 3 + gr * 5 + b * 2) / 10;
                    r = lum + (r - lum) * 21 / 16; gr = lum + (gr - lum) * 21 / 16; b = lum + (b - lum) * 21 / 16;
                    r = (r - 128) * 18 / 16 + 128; gr = (gr - 128) * 18 / 16 + 128; b = (b - 128) * 18 / 16 + 128;
                }
                if (fx->vignette) {
                    int m = corner[y1 * W + x1] + 1;
                    r = r * m >> 8; gr = gr * m >> 8; b = b * m >> 8;
                }
                c = 0xff000000u | (uint32_t)clamp255(r) << 16 | (uint32_t)clamp255(gr) << 8 | (uint32_t)clamp255(b);
            }
            o[X] = c;
        }
    }
}

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
    for (int cy = 0; cy < HUD / 8; ++cy)
        for (int cx = 0; cx < GW; ++cx) {
            int cell = cy * GW + cx;
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
            if (fx->shake && size >= 110) {       /* only the big ones shake the screen */
                shake += 1.6f;
                if (shake > 4.0f) shake = 4.0f;
                if (rumble < 40000) rumble += 22000;
            }
            if (fx->particles) {
                for (int i = 0; i < 10; ++i) spawn_particle(x + (frand() - 0.5f) * 14, y + (frand() - 0.5f) * 14, 0);
                for (int i = 0; i < 8; ++i) spawn_particle(x + (frand() - 0.5f) * 20, y + (frand() - 0.5f) * 12, 1);
            }
        }
    /* Burning things keep shedding the odd ember. */
    if (fx->particles)
        for (int cell = 0; cell < GW * (HUD / 8); ++cell)
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
    if (!any || !fx->shock) return;
    memset(field_x, 0, sizeof field_x);
    memset(field_y, 0, sizeof field_y);
    field_on = 1;
    for (int i = 0; i < MAX_RINGS; ++i) {
        const Ring *ring = &rings[i];
        if (ring->life <= 0) continue;
        float t = ring->age / ring->life, radius = 6.0f + 110.0f * t, band = 9.0f;
        float strength = 14.0f * (1.0f - t) * ring->strength;        /* quarter pixels */
        int x0 = clampi((int)(ring->x - radius - band), 0, W - 1), x1 = clampi((int)(ring->x + radius + band), 0, W - 1);
        int y0 = clampi((int)(ring->y - radius - band), 0, HUD - 1), y1 = clampi((int)(ring->y + radius + band), 0, HUD - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - ring->x, dy = y - ring->y, d = sqrtf(dx * dx + dy * dy);
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
        if (py < 0 || py >= g.oh) continue;
        for (int i = 0; i < size; ++i) {
            int px = x + i;
            if (px < 0 || px >= g.ow) continue;
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
        if (p->life <= 0) continue;
        p->age += dt;
        if (p->age >= p->life) { p->life = 0; continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->kind == 0) { p->vx *= 1.0f - 2.0f * dt; p->vy -= 30.0f * dt; }
        else p->x += sinf(p->age * 9.0f + (float)i) * 10.0f * dt;
        if (!fx->particles) continue;
        int x = (int)p->x, y = (int)p->y;
        if (x < 0 || y < 0 || x >= W || y >= HUD) { if (y < -4) p->life = 0; continue; }
        if (p->kind == 1 && g.waterline >= 0 && y < g.waterline + 2) { p->life = 0; continue; }
        float fade = 1.0f - p->age / p->life;
        int px = (int)(p->x * k), py = (int)(p->y * k);
        if (p->kind == 0) add_block(out, px, py, k, (int)(230 * fade), (int)(120 * fade), (int)(30 * fade));
        else add_block(out, px, py, k < 2 ? 1 : k - 1, (int)(70 * fade), (int)(110 * fade), (int)(120 * fade));
    }
    /* Marine snow on two parallax layers, over open water only. */
    if (fx->snow)
        for (unsigned n = 0; n < 110; ++n) {
            unsigned h = n * 2654435761u;
            int layer = (int)(n & 1);
            unsigned speed = layer ? 9 : 5;
            int drift_x = layer ? g.scroll_x[1] : g.scroll_x[2], drift_y = layer ? g.scroll_y[1] : g.scroll_y[2];
            int x = (int)((h >> 8) % W) - (int)(time_ms * speed / 1000 % W) + (S((int)(time_ms / 9 + (h >> 4))) >> 6) - drift_x;
            int y = (int)((h >> 18) % HUD) + (int)(time_ms * (speed + 2) / 1400 % HUD) - drift_y;
            x = ((x % W) + W) % W;
            y = ((y % HUD) + HUD) % HUD;
            if (water_map[y * W + x] < 70) continue;
            int add = layer ? 70 : 38;
            add_block(out, x * k, y * k, k, add, add, add);
        }
}

/* ---- render ---------------------------------------------------------------- */

/* Stage timing for tuning (WORLD_SCENE_RUN prints it). */
double world_scene_stage_ms[6];
unsigned world_scene_explosions;      /* explosions detected so far */
static double stamp(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

static void compose(uint32_t *out, int disparity)
{
    int k = g.k;
    g.out = out;
    int keep_x[3], near_shift = k * 3 / 4 > 0 ? k * 3 / 4 : 1;
    memcpy(keep_x, g.off_x, sizeof keep_x);
    g.off_x[1] += disparity * near_shift;            /* behind the screen plane */
    g.off_x[2] += disparity * near_shift * 2;
    parallel(compose_rows, g.oh);
    memcpy(g.off_x, keep_x, sizeof keep_x);
}

int world_scene_render(uint32_t *out, unsigned scale, double alpha, const WorldSceneFx *fx, uint32_t clock)
{
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
    waterline_used = cur.waterline < 0 ? 0 : cur.waterline > HUD ? HUD : cur.waterline;
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

    /* Row displacement of the water: shimmer, and ripples under the surface. */
    for (int y = 0; y < H; ++y) {
        int shift = 0;
        int below = g.waterline < 0 ? 400 : y - g.waterline;
        if (y < HUD && below >= 0) {
            if (fx->shimmer) shift += (S(y * 37 + g.T * 5) + S(y * 11 - g.T * 3)) * k / 300;
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

    double t0 = stamp();
    parallel(colour_rows, LH);
    if (fx->dof && layer_on[2]) { soften(layer_col[2]); soften(layer_col[2]); }
    double t1 = stamp();
    parallel(layer_rows_fx, LH);
    double t2 = stamp();
    draw_sprites(alpha);
    double t3 = stamp();
    if (new_frame) {
        detect_explosions(fx);
        new_frame = 0;
    }
    build_field(fx, dt);

    if (fx->stereo) {
        compose(eye, -1);
        compose(out, 1);
        for (size_t i = 0, n = (size_t)g.ow * g.oh; i < n; ++i)
            out[i] = (eye[i] & 0xffff0000u) | (out[i] & 0x0000ffffu);
    } else
        compose(out, 0);
    double t4 = stamp();

    /* Light for the next refresh: what the sprites gave off. */
    if (fx->lights)
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < GW * GH; ++i) {
                unsigned v = hot_sum[c][i] / 6;
                lamp[c][i] = (uint16_t)(v > 1023 ? 1023 : v);
            }
            blur16(lamp[c], GW, GH, 1);
            blur16(lamp[c], GW, GH, 2);
        }
    draw_particles(out, fx, dt, time_ms);

    if (fx->trails) {
        size_t n = (size_t)g.ow * (size_t)(HUD * k);
        if (have_history)
            for (size_t i = 0; i < n; ++i) {
                uint32_t p = out[i], q = history[i];
                unsigned r = ((q >> 16) & 255) * 150 >> 8, gr = ((q >> 8) & 255) * 150 >> 8, b = (q & 255) * 150 >> 8;
                unsigned pr = (p >> 16) & 255, pg = (p >> 8) & 255, pb = p & 255;
                out[i] = 0xff000000u | (pr > r ? pr : r) << 16 | (pg > gr ? pg : gr) << 8 | (pb > b ? pb : b);
            }
        memcpy(history, out, n * sizeof *out);
        have_history = 1;
    } else
        have_history = 0;
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
