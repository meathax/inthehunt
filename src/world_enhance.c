#include "world_enhance.h"
#include <math.h>
#include <string.h>

enum { W = 320, H = 240, N = W * H, HUD = 224, LW = W / 8, LH = H / 8 };

static int16_t sine[1024];          /* 256 * sin */
static uint8_t corner[N];           /* vignette multiplier, 255 = none */
static int ready;
static uint32_t history[N];
static int have_history;

static void prepare(void)
{
    for (int i = 0; i < 1024; ++i) sine[i] = (int16_t)lrint(256.0 * sin(i * 6.283185307179586 / 1024.0));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double dx = (x - 159.5) / 160.0, dy = (y - 119.5) / 120.0;
            double d = dx * dx + dy * dy;           /* 0 centre .. 2 corner */
            double m = 1.0 - 0.34 * (d > 0.55 ? (d - 0.55) / 1.45 : 0.0);
            corner[y * W + x] = (uint8_t)lrint(255.0 * m);
        }
    ready = 1;
}

static inline int S(int angle) { return sine[angle & 1023]; }
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Box blur of one 16-bit plane, radius r, horizontally then vertically. */
static void blur(uint16_t *plane, int width, int height, int r)
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

void world_enhance_reset(void) { have_history = 0; }

void world_enhance_apply(const uint32_t *in, uint32_t *out, const WorldEnhance *fx, uint32_t time_ms)
{
    static uint32_t base[N];
    static uint8_t water[N];
    static uint16_t glow[3][N], lamp[3][LW * LH];
    if (!ready) prepare();
    int T = (int)(time_ms / 12);                   /* animation phase */

    /* Refraction: rows of the playfield slide by up to one pixel. */
    if (fx->shimmer) {
        for (int y = 0; y < H; ++y) {
            int shift = y < HUD ? (S(y * 37 + T * 5) + S(y * 11 - T * 3)) / 300 : 0;
            for (int x = 0; x < W; ++x) {
                int sx = x + shift;
                base[y * W + x] = in[y * W + (sx < 0 ? 0 : sx >= W ? W - 1 : sx)];
            }
        }
    } else
        memcpy(base, in, sizeof base);

    /* Open water is recognised by colour: blue-dominant and not bright. The
     * HUD strip is never treated as water. */
    for (int i = 0; i < N; ++i) {
        uint32_t p = base[i];
        int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        int lum = (r * 3 + g * 5 + b * 2) / 10;
        int blue = (b - r) * 3;
        if (g > b) blue -= (g - b) * 4;
        int w = clamp255(blue) * (255 - lum) / 200;
        water[i] = i >= HUD * W ? 0 : (uint8_t)clamp255(w);
    }

    /* Bright pass for the bloom, and warm emitters for the dynamic lights. */
    if (fx->bloom) {
        for (int i = 0; i < N; ++i) {
            uint32_t p = base[i];
            int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            int top = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int keep = i < HUD * W && top > 236;
            glow[0][i] = (uint16_t)(keep ? r : 0);
            glow[1][i] = (uint16_t)(keep ? g : 0);
            glow[2][i] = (uint16_t)(keep ? b : 0);
        }
        for (int c = 0; c < 3; ++c) { blur(glow[c], W, H, 2); blur(glow[c], W, H, 3); }
    }
    if (fx->lights) {
        static uint32_t sum[3][LW * LH];
        memset(sum, 0, sizeof sum);
        for (int y = 0; y < HUD; ++y)
            for (int x = 0; x < W; ++x) {
                uint32_t p = base[y * W + x];
                int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                if (r < 216 || r < b + 70) continue;        /* fire, flashes, tracer */
                int cell = (y / 8) * LW + x / 8;
                sum[0][cell] += (unsigned)r; sum[1][cell] += (unsigned)g; sum[2][cell] += (unsigned)b;
            }
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < LW * LH; ++i) {
                unsigned v = sum[c][i] / 6;                 /* a couple of dozen hot pixels saturate a cell */
                lamp[c][i] = (uint16_t)(v > 1023 ? 1023 : v);
            }
            blur(lamp[c], LW, LH, 1);
            blur(lamp[c], LW, LH, 2);
        }
    }

    for (int y = 0; y < H; ++y) {
        int ray_fade = y < 190 ? 190 - y : 0;               /* shafts die out with depth */
        int ly = y - 4, cy = ly < 0 ? 0 : ly / 8, fy = ly < 0 ? 0 : ly & 7;
        int cy1 = cy + 1 < LH ? cy + 1 : LH - 1;
        for (int x = 0; x < W; ++x) {
            int i = y * W + x;
            uint32_t p = base[i];
            int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            int w = water[i];

            if (fx->depth && w) {
                /* Deeper water loses red first, then green. */
                int d = y * w / 255;                        /* 0..239 */
                r = r * (256 - d * 110 / 240) >> 8;
                g = g * (256 - d * 60 / 240) >> 8;
                b = b * (256 - d * 26 / 240) >> 8;
            }
            if (fx->lights && y < HUD) {
                int lx = x - 4, cx = lx < 0 ? 0 : lx / 8, fx8 = lx < 0 ? 0 : lx & 7;
                int cx1 = cx + 1 < LW ? cx + 1 : LW - 1;
                int light[3];
                for (int c = 0; c < 3; ++c) {
                    int a = lamp[c][cy * LW + cx] * (8 - fx8) + lamp[c][cy * LW + cx1] * fx8;
                    int d = lamp[c][cy1 * LW + cx] * (8 - fx8) + lamp[c][cy1 * LW + cx1] * fx8;
                    light[c] = (a * (8 - fy) + d * fy) >> 6;   /* 0..1023 */
                }
                /* Lit surfaces brighten in proportion to their own colour,
                 * plus a little haze in the water itself. */
                r += (r + 40) * light[0] >> 9;
                g += (g + 40) * light[1] >> 9;
                b += (b + 40) * light[2] >> 9;
            }
            if (fx->caustics && w) {
                int u = x * 27 + (S(y * 19 + T * 3) >> 1);
                int v = y * 33 + (S(x * 23 - T * 2) >> 1);
                int n = S(u + T * 2) + S(v - T * 3) + S((u + v) / 2 + T);
                if (n < 0) n = -n;
                int ridge = 256 - n * 2 / 3;                   /* bright where the waves cancel */
                if (ridge > 0) {
                    ridge = ridge * ridge >> 8;
                    ridge = ridge * ridge >> 8;
                    int add = ridge * w * (300 - y) / (255 * 300);
                    r += add * 50 >> 8; g += add * 95 >> 8; b += add * 105 >> 8;
                }
            }
            if (fx->rays && w && ray_fade) {
                int s = x * 2 + y;
                int n = S(s * 2 + T) + S(s * 5 - T * 2 + 300) + S(s + T / 2 + 700);
                if (n > 60) {
                    int add = (n - 60) * ray_fade * w / (190 * 255);    /* 0..~700 */
                    r += add * 30 >> 8; g += add * 52 >> 8; b += add * 60 >> 8;
                }
            }
            if (fx->bloom) {
                r += glow[0][i] * 120 >> 8; g += glow[1][i] * 120 >> 8; b += glow[2][i] * 120 >> 8;
            }
            if (fx->vivid) {
                int lum = (r * 3 + g * 5 + b * 2) / 10;
                r = lum + (r - lum) * 21 / 16; g = lum + (g - lum) * 21 / 16; b = lum + (b - lum) * 21 / 16;
                r = (r - 128) * 18 / 16 + 128; g = (g - 128) * 18 / 16 + 128; b = (b - 128) * 18 / 16 + 128;
            }
            if (fx->vignette) {
                int m = corner[i] + 1;
                r = r * m >> 8; g = g * m >> 8; b = b * m >> 8;
            }
            out[i] = 0xff000000u | (uint32_t)clamp255(r) << 16 | (uint32_t)clamp255(g) << 8 | (uint32_t)clamp255(b);
        }
    }

    /* Marine snow: specks on two parallax layers, drawn only over water. */
    if (fx->snow) {
        for (unsigned k = 0; k < 110; ++k) {
            unsigned h = k * 2654435761u;
            int layer = (int)(k & 1);
            unsigned speed = layer ? 9 : 5;
            int x = (int)((h >> 8) % W) - (int)(time_ms * speed / 1000 % W) + (S((int)(time_ms / 9 + (h >> 4))) >> 6);
            int y = (int)((h >> 18) % HUD) + (int)(time_ms * (speed + 2) / 1400 % HUD);
            x = ((x % W) + W) % W;
            y %= HUD;
            int i = y * W + x;
            if (water[i] < 70) continue;
            uint32_t p = out[i];
            int add = layer ? 70 : 38;
            out[i] = 0xff000000u | (uint32_t)clamp255((int)((p >> 16) & 255) + add) << 16 |
                     (uint32_t)clamp255((int)((p >> 8) & 255) + add) << 8 | (uint32_t)clamp255((int)(p & 255) + add);
        }
    }

    /* Persistence: each channel decays instead of vanishing at once. */
    if (fx->trails) {
        if (have_history)
            for (int i = 0; i < HUD * W; ++i) {
                uint32_t p = out[i], q = history[i];
                int r = (int)((q >> 16) & 255) * 150 >> 8, g = (int)((q >> 8) & 255) * 150 >> 8, b = (int)(q & 255) * 150 >> 8;
                int pr = (p >> 16) & 255, pg = (p >> 8) & 255, pb = p & 255;
                out[i] = 0xff000000u | (uint32_t)(pr > r ? pr : r) << 16 | (uint32_t)(pg > g ? pg : g) << 8 |
                         (uint32_t)(pb > b ? pb : b);
            }
        memcpy(history, out, sizeof history);
        have_history = 1;
    } else
        have_history = 0;
}
