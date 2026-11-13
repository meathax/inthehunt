/* Private scene-renderer effects. Fixed pools, bounded stamps, no allocations.
 * Positions are native screen pixels; camera deltas move existing effects.
 * Emission is once per game frame, animation once per displayed refresh. */
enum { WF_WAKE, WF_DEBRIS, WF_SILT, WF_SPLASH, WF_TYPES,
       WF_COUNT = 128 + 48 + 16 + 16 };
typedef struct {
    float x, y, vx, vy, age, life, size, phase;
    int light, settled;
} WaterFx;
static WaterFx waterfx[WF_COUNT];
static const int wf_start[WF_TYPES + 1] = {0, 128, 176, 192, WF_COUNT};
static unsigned wf_seed = 0x754a10u, wf_frame;
static int wf_spawned[WF_TYPES];
static const int wf_spawn_limit[WF_TYPES] = {12, 16, 2, 2};
static unsigned wf_samples; /* native-pixel drawing work, useful for regression checks */
unsigned world_scene_waterfx_emitted[WF_TYPES]; /* headless diagnostic counters */

static float wf_random(void)
{
    wf_seed = wf_seed * 1664525u + 1013904223u;
    return (wf_seed >> 8) / 16777216.0f;
}

static void waterfx_reset(void)
{
    memset(waterfx, 0, sizeof waterfx);
    memset(wf_spawned, 0, sizeof wf_spawned);
    wf_frame = 0;
    wf_seed = 0x754a10u;
}

static void waterfx_scroll(int dx, int dy)
{
    for (int i = 0; i < WF_COUNT; ++i) {
        waterfx[i].x -= dx; waterfx[i].y -= dy;
    }
}

static void waterfx_begin_frame(void)
{
    memset(wf_spawned, 0, sizeof wf_spawned);
    ++wf_frame;
}

static WaterFx *wf_spawn(int kind, float x, float y, float life)
{
    if (wf_spawned[kind] >= wf_spawn_limit[kind]) return NULL;
    for (int i = wf_start[kind]; i < wf_start[kind + 1]; ++i) {
        WaterFx *p = &waterfx[i];
        if (p->life > 0) continue;
        *p = (WaterFx){0};
        p->x = x; p->y = y; p->life = life;
        p->phase = wf_random() * 1024;
        ++wf_spawned[kind];
        ++world_scene_waterfx_emitted[kind];
        return p;
    }
    return NULL;
}

static int wf_rock(float x, float y)
{
    int ix = (int)x, iy = (int)y;
    if (x < 0 || y < 0 || ix >= W || iy >= HUD) return 0;
    return solid[(iy / 2) * PW + ix / 2];
}

/* Small impacts only lift silt when rock is actually nearby. Deduplicate
 * adjacent explosion cells so a large blast cannot flood the cloud pool. */
static void waterfx_impact(float x, float y)
{
    if (g.waterline >= 0 && y < g.waterline + 8) return;
    if (wf_spawned[WF_SILT] >= wf_spawn_limit[WF_SILT]) return;
    for (int i = wf_start[WF_SILT]; i < wf_start[WF_SILT + 1]; ++i)
        if (waterfx[i].life > 0 && waterfx[i].age < 0.35f &&
            fabsf(waterfx[i].x - x) < 24 && fabsf(waterfx[i].y - y) < 24) return;
    int hit = 0;
    float rx = x, ry = y;
    for (int radius = 2; radius <= 18 && !hit; radius += 4)
        for (int side = 0; side < 5; ++side) {
            float sx = x + (side - 2) * radius * 0.5f;
            float sy = y + (side == 0 || side == 4 ? 0 : radius);
            if (wf_rock(sx, sy)) { rx = sx; ry = sy - 3; hit = 1; break; }
        }
    if (!hit) return;
    WaterFx *p = wf_spawn(WF_SILT, rx, ry, 2.8f);
    if (p) { p->vx = (wf_random() - 0.5f) * 7; p->vy = -4; p->size = 5; }
}

static void waterfx_blast(float x, float y, int size, const WorldSceneFx *fx)
{
    if (y >= HUD || (g.waterline >= 0 && y < g.waterline)) return;
    if (fx->silt) waterfx_impact(x, y);
    if (!fx->debris) return;
    for (int i = wf_start[WF_DEBRIS]; i < wf_start[WF_DEBRIS + 1]; ++i)
        if (waterfx[i].life > 0 && waterfx[i].age < 0.25f &&
            fabsf(waterfx[i].x - x) < 24 && fabsf(waterfx[i].y - y) < 24) return;
    int count = size > 160 ? 10 : size >= 70 ? 6 : 3;
    for (int n = 0; n < count; ++n) {
        WaterFx *p = wf_spawn(WF_DEBRIS, x + (wf_random() - 0.5f) * 8,
                             y + (wf_random() - 0.5f) * 8, 2.0f + wf_random() * 1.4f);
        if (!p) break;
        p->vx = (wf_random() - 0.5f) * 60;
        p->vy = -12 - wf_random() * 28;
        p->light = n % 4 == 0;
        p->size = p->light ? 1.0f : 1.5f + wf_random();
    }
}

static void wf_crossing(float x, const WorldSceneFx *fx)
{
    if (!fx->splashes || g.waterline < 3 || g.waterline >= HUD - 4) return;
    for (int i = wf_start[WF_SPLASH]; i < WF_COUNT; ++i)
        if (waterfx[i].life > 0 && waterfx[i].age < 0.25f && fabsf(waterfx[i].x - x) < 18) return;
    WaterFx *p = wf_spawn(WF_SPLASH, x, (float)g.waterline, 0.85f);
    if (p) p->size = 1;
}

static void waterfx_motion(const WorldSceneFx *fx)
{
    if ((!fx->wakes && !fx->splashes && !fx->debris && !fx->silt) || (cur.video_control & 0x80)) return;
    Sprite now[MAX_SPRITES], old[MAX_SPRITES];
    int n = parse_sprites(cur.sprites, now), m = parse_sprites(prev.sprites, old);
    int cx = (int16_t)(cur.layers[1].scroll_x - prev.layers[1].scroll_x);
    int cy = (int16_t)(cur.layers[1].scroll_y - prev.layers[1].scroll_y);
    if (abs(cx) > 16 || abs(cy) > 16) return;
    /* Identity is inferred from nearby shape/palette/code-family matches;
     * consume each old entry once, rejecting teleports and HUD lettering. */
    uint8_t used[MAX_SPRITES] = {0};
    for (int i = 0; i < n; ++i) {
        const Sprite *s = &now[i];
        int bank = s->attr & 0x7f, match = -1, best = 10000;
        int width = s->cols * 16, height = s->rows * 16;
        if (bank == 11 || s->top < -height || s->top >= HUD || s->left < -width || s->left >= W) continue;
        for (int j = i - 6; j <= i + 6; ++j) {
            if (j < 0 || j >= m || used[j]) continue;
            const Sprite *p = &old[j];
            if (p->cols != s->cols || p->rows != s->rows || ((p->attr ^ s->attr) & 0xff) ||
                (p->code >> 6) != (s->code >> 6)) continue;
            int dx = s->left - p->left, dy = s->top - p->top;
            if (abs(dx) > 16 || abs(dy) > 16) continue;
            int score = abs(dx + cx) + abs(dy + cy) + abs(j - i);
            if (score < best) { match = j; best = score; }
        }
        if (match < 0) {
            /* Captured white blast/smoke sprites use bank 10 and are absent
             * from the warm-fire light grid. A newly appearing piece can
             * emit debris/silt without adding any light-tracing workload. */
            unsigned family = s->code >> 4;
            if (bank == 10 && (family == 0x178 || family == 0x20f || family == 0x210))
                waterfx_blast(s->left + width * 0.5f, s->top + height * 0.5f, width * height / 8, fx);
            continue;
        }
        used[match] = 1;
        float dx = (float)(s->left - old[match].left + cx);
        float dy = (float)(s->top - old[match].top + cy);
        float x = s->left + width * 0.5f, y = s->top + height * 0.5f;
        float old_y = old[match].top + height * 0.5f;
        if (fx->splashes && prev.waterline >= 0 && cur.waterline >= 0 && fabsf(dy) >= 1 &&
            ((old_y < prev.waterline && y >= cur.waterline) || (old_y >= prev.waterline && y < cur.waterline)))
            wf_crossing(x, fx);
        /* Player palette banks carry the sub and its weapons. Thin, fast
         * sprites also qualify as torpedoes; other moving scenery does not. */
        int sub = (bank == 1 || bank == 2) && height >= 32;
        int torpedo = width <= 32 && height <= 16 && fabsf(dx) >= 3 && fabsf(dy) <= 2;
        if (!fx->wakes || (!sub && !torpedo) || (wf_frame & 1) || fabsf(dx) + fabsf(dy) < 0.5f ||
            (g.waterline >= 0 && y < g.waterline + 3)) continue;
        float direction = dx < 0 ? -1.0f : 1.0f;
        /* The real sub is three adjacent 16x32 sprite pieces. Emit only
         * from the trailing piece, not from the middle and bow as well. */
        if (sub) {
            int stern = 1;
            for (int j = 0; j < n; ++j) {
                if (j == i || (now[j].attr & 0x7f) != (unsigned)bank || now[j].rows != s->rows ||
                    abs(now[j].top - s->top) > 8) continue;
                int offset = now[j].left - s->left;
                if (offset * direction < 0 && abs(offset) <= 48) { stern = 0; break; }
            }
            if (!stern) continue;
        }
        int count = sub ? 3 : 1;
        for (int k = 0; k < count; ++k) {
            WaterFx *p = wf_spawn(WF_WAKE, x - direction * width * 0.45f,
                                 y + (wf_random() - 0.5f) * (sub ? 6 : 2), sub ? 1.15f : 0.6f);
            if (!p) break;
            p->vx = -direction * (8 + wf_random() * 12);
            p->vy = -3 - wf_random() * 5;
            p->size = sub ? 1.4f : 0.8f;
        }
    }
}

/* Blend small native-pixel stamps into the final image. Respect displaced
 * sprite coverage and the HUD: no effect hides gameplay. Underwater effects
 * also respect foreground terrain. Airborne spray must pass in front of
 * stage three's city backdrop, which happens to use layer zero. */
static void wf_pixel(uint32_t *out, int x, int y, int r, int gr, int b, int a, int underwater)
{
    if (x < 0 || x >= W || y < 0 || y >= HUD || a <= 0) return;
    if (underwater && ((g.waterline >= 0 && y < g.waterline) || water_map[y * W + x] < 60)) return;
    ++wf_samples;
    int k = g.k;
    for (int yy = y * k; yy < (y + 1) * k; ++yy)
        for (int xx = x * k; xx < (x + 1) * k; ++xx) {
            int u = xx + g.row_dx[y] + g.shake_x, v = yy + g.shake_y;
            if (field_on) { u += field_x[y * W + x] * k / 4; v += field_y[y * W + x] * k / 4; }
            if (u >= 0 && v >= 0 && u < g.ow && v < g.oh && sprite_px[(size_t)v * g.ow + u] != NONE) continue;
            if (underwater && layer_on[0]) {
                int i = divide[yy + g.off_y[0]] * LW + divide[xx + g.off_x[0]];
                if (layer_col[0][i] >> 24) continue;
            }
            uint32_t *o = out + (size_t)yy * g.ow + xx, c = *o;
            int rr = (c >> 16) & 255, gg = (c >> 8) & 255, bb = c & 255;
            rr += (r - rr) * a / 256; gg += (gr - gg) * a / 256; bb += (b - bb) * a / 256;
            *o = 0xff000000u | (unsigned)rr << 16 | (unsigned)gg << 8 | (unsigned)bb;
        }
}

static void waterfx_draw(uint32_t *out, const WorldSceneFx *fx, float dt)
{
    const unsigned enabled[WF_TYPES] = {fx->wakes, fx->debris, fx->silt, fx->splashes};
    wf_samples = 0;
    for (int kind = 0; kind < WF_TYPES; ++kind) {
        /* Each type has its own work allowance, so dense silt cannot starve a splash. */
        int budget = kind == WF_SILT ? 12000 : 4000;
        for (int i = wf_start[kind]; i < wf_start[kind + 1]; ++i) {
            WaterFx *p = &waterfx[i];
            if (!enabled[kind]) { p->life = 0; continue; }
            if (p->life <= 0) continue;
            p->age += dt;
            if (p->age >= p->life) { p->life = 0; continue; }
            float t = p->age / p->life, fade = 1 - t;
            if (kind == WF_SPLASH) {
                if (g.waterline < 3 || g.waterline >= HUD - 4) { p->life = 0; continue; }
                p->y = (float)g.waterline;
            } else if (!p->settled) {
                float nx = p->x + p->vx * dt, ny = p->y + p->vy * dt;
                if (kind == WF_DEBRIS) {
                    p->vy += (p->light ? -8 : 30) * dt;
                    p->vx *= 1 - dt * 1.5f;
                    if (wf_rock(nx, ny) && p->vy > 0) { p->settled = 1; p->vx = p->vy = 0; }
                }
                if (!p->settled) { p->x = nx; p->y = ny; }
                if (kind == WF_WAKE) p->x += S((int)(p->phase + p->age * 360)) / 256.0f * dt * 5;
            }
            if (p->x < -32 || p->x >= W + 32 || p->y < -24 || p->y >= HUD + 16) { p->life = 0; continue; }
            if (kind == WF_WAKE && g.waterline >= 0 && p->y < g.waterline) { p->life = 0; continue; }
            if (budget <= 0) continue;
            if (kind == WF_SPLASH) {
                int radius = (int)(3 + p->age * 32);
                for (int dx = -radius; dx <= radius && budget > 0; ++dx, --budget) {
                    int yy = (int)p->y + 1 + (radius * radius - dx * dx) * 2 / (radius * radius);
                    int a = (int)(fade * 95);
                    wf_pixel(out, (int)p->x + dx, yy, 130, 205, 220, a, 1);
                }
                for (int j = 0; j < 7 && budget > 0; ++j, --budget) {
                    /* Clear the arcade's existing foam sprite: the original
                     * short arc hid almost all added spray inside that sprite. */
                    float vx = (j - 3) * 24.0f, vy = -52.0f - (3 - abs(j - 3)) * 10;
                    int x = (int)(p->x + vx * p->age);
                    int y = (int)(p->y + vy * p->age + 90 * p->age * p->age);
                    wf_pixel(out, x, y, 185, 230, 240, (int)(fade * 190), 0);
                    wf_pixel(out, x, y + 1, 140, 205, 225, (int)(fade * 80), 0);
                    /* Paired bubbles fan downward beneath the ripple. */
                    wf_pixel(out, (int)(p->x + vx * p->age * 0.6f),
                             (int)(p->y + 2 + p->age * 24 - p->age * p->age * 20),
                             100, 185, 205, (int)(fade * 100), 1);
                }
            } else if (kind == WF_DEBRIS) {
                int dx = (int)(S((int)(p->phase + p->age * 210)) * p->size / 256);
                int dy = (int)(S((int)(p->phase + p->age * 210) + 256) * p->size / 256);
                wf_pixel(out, (int)p->x, (int)p->y, 65, 88, 95, (int)(fade * 220), 1);
                wf_pixel(out, (int)p->x + dx, (int)p->y + dy, 156, 175, 170, (int)(fade * 210), 1);
                budget -= 2;
            } else {
                int radius = kind == WF_SILT ? (int)(p->size + 11 * t) : (int)(p->size + t);
                int r2 = radius * radius + 1;
                int alpha = kind == WF_SILT ? (int)(fade * (p->age < 0.2f ? p->age * 5 : 1) * 58) : (int)(fade * 110);
                for (int dy = -radius; dy <= radius && budget > 0; ++dy)
                    for (int dx = -radius; dx <= radius && budget > 0; ++dx, --budget) {
                        int d2 = dx * dx + dy * dy;
                        if (d2 >= r2) continue;
                        int a = alpha * (r2 - d2) / r2;
                        if (kind == WF_SILT) {
                            a = a * (192 + S((int)p->phase + dx * 61 + dy * 39) / 4) / 256;
                            wf_pixel(out, (int)p->x + dx, (int)p->y + dy / 2, 100, 112, 104, a, 1);
                        } else {
                            /* Pale crescent highlight rather than solid white dots. */
                            wf_pixel(out, (int)p->x + dx, (int)p->y + dy, 100, 190, 210, a, 1);
                        }
                    }
            }
        }
    }
}
