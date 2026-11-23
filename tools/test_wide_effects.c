/* Widescreen compositor/D3D12 regression. Link with world_video.c, world_gpu.cpp
 * and post/post_chain.cpp. Each pair uses the same synthetic board scene and
 * animation phase, with an active receiver or event for the option under test. */
#include "../src/world_gpu.h"
#include <stddef.h>
#include "../src/world_scene.c"
static uint8_t test_tiles[65536], test_sprites[2048], test_gfx1[2097152], test_gfx2[4194304];
static uint32_t test_colors[1024];
static WorldSceneFrame frame;
static void put16(uint8_t *p, unsigned v) {
    p[0] = v;
    p[1] = v >> 8;
}
static void pen_tile(int code, int x, int y, int pen) {
    for (int b = 0; b < 4; ++b)
        if (pen & (1 << b))
            test_gfx1[code * 8 + y + b * 0x80000] |= 1 << (7 - x);
}
static void pen_sprite(int code, int x, int y, int pen) {
    for (int b = 0; b < 4; ++b)
        if (pen & (1 << b))
            test_gfx2[code * 32 + y + ((x & 8) << 1) + b * 0x100000] |= 1 << (7 - (x & 7));
}
static void fixture(void) {
    for (int p = 0; p < 1024; ++p)
        test_colors[p] = 0xff000000u | ((p * 5 % 45) << 16) | ((p * 9 % 79 + 58) << 8) | (p * 11 % 83 + 99);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            pen_tile(1, x, y, 1 + (x + y) % 14);
            pen_tile(2, x, y, 6);
        }
    for (int i = 0; i < 2048; ++i) {
        put16(test_tiles + i * 4, 1);
        put16(test_tiles + i * 4 + 2, (i % 8) * 16);
    }
    /* Layer 1 has a solid column, all other cells transparent. */
    for (int row = 0; row < 64; ++row) {
        put16(test_tiles + 0x4000 + (row * 64 + 15) * 4, 2);
        put16(test_tiles + 0x4000 + (row * 64 + 15) * 4 + 2, 0x100);
    }
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 16; ++x)
            if (x > 1 && x < 14 && y > 1 && y < 30)
                pen_sprite(0xc1e + y / 16, x, y & 15, 2 + (x + y) % 14);
    put16(test_sprites, (360 - 100 - 16) | 0x200);
    put16(test_sprites + 2, 0xc1e);
    put16(test_sprites + 4, 0x81);
    put16(test_sprites + 6, 96 + 340);
    frame = (WorldSceneFrame){.tiles = test_tiles,
                              .sprites = test_sprites,
                              .colors = test_colors,
                              .layers = {{.master = 0x10}, {.master = 1}, {.master = 0}},
                              .waterline = 60,
                              .scene_id = 0xb2e,
                              .video_control = 0x2000};
    frame.width = WORLD_VIEW_WIDE_WIDTH;
    frame.left = -WORLD_VIEW_WING;
    frame.lamp[0][0] = 365;
    frame.lamp[0][1] = 110;
    frame.lamp[0][2] = 1;
    world_scene_init(test_gfx1, test_gfx2);
    world_scene_reset();
    world_scene_submit(&frame);
}

static uint32_t picture[MAX_N];
#define TEST(field) {#field, offsetof(WorldSceneFx, field)}
static const struct {
    const char *name;
    size_t field;
} options[] = {TEST(rays),
               TEST(caustics),
               TEST(sunshadow),
               TEST(lights),
               TEST(glow),
               TEST(depth),
               TEST(snow),
               TEST(vivid),
               TEST(trails),
               TEST(haze),
               TEST(surface),
               TEST(reflect),
               TEST(shock),
               TEST(particles),
               TEST(shake),
               TEST(wakes),
               TEST(debris),
               TEST(silt),
               TEST(splashes),
               TEST(sparks),
               TEST(impact_motes),
               TEST(pressure),
               TEST(sediment_light),
               TEST(water_volume),
               TEST(ocean_depth),
               TEST(aftermath),
               TEST(headlights),
               TEST(rim),
               TEST(bubble_light),
               TEST(contact_shade),
               TEST(blast_reflect),
               TEST(materials),
               TEST(headlight_level),
               TEST(light_strength),
               TEST(shadow_strength),
               TEST(depth_strength),
               TEST(shaft_strength),
               TEST(rim_strength),
               TEST(caustic_strength)};
enum { OPTIONS = sizeof options / sizeof *options, CASES = OPTIONS * 2 + 1 };
static WorldLightScene *scenes[CASES];
static uint32_t *input[CASES], *output[CASES];
static double times[CASES][8];
int main(void) {
    int off[OPTIONS], lit[OPTIONS];
    int i = -1;
    for (int opt = 0; opt < OPTIONS; opt++)
        for (int on = 0; on <= 1; on++) {
            ++i;
            const char *name = options[opt].name;
            if (on && !strcmp(name, "trails")) {
                // A bright moving object's previous frame must enter the same GPU history.
                input[i] = calloc(MAX_N, 4);
                output[i] = calloc(MAX_N, 4);
                scenes[i] = calloc(1, sizeof *scenes[i]);
                scenes[i]->width = MAX_W;
                scenes[i]->scale = 1;
                scenes[i]->flags = 8;
                scenes[i]->effects.history_reset = 1;
                for (int y = 120; y < 129; y++)
                    for (int x = 392; x < 405; x++)
                        input[i][y * MAX_W + x] = 0xfeffffff;
                ++i;
            }
            if (on)
                lit[opt] = i;
            else
                off[opt] = i;
            scenes[i] = calloc(1, sizeof *scenes[i]);
            input[i] = calloc(MAX_N, 4);
            output[i] = calloc(MAX_N, 4);
            fixture();
            WeaponEmitter lamp = {346, 111, 0, 0, GUN_FLASH, 0, 88};
            world_scene_weapons(&lamp, 1);
            WorldSceneFx fx = {0};
            if (!strcmp(name, "bubble_light"))
                fx.wakes = fx.lights = 1;
            if (!strcmp(name, "headlight_level"))
                fx.headlights = 1;
            if (!strcmp(name, "light_strength"))
                fx.lights = 1;
            if (!strcmp(name, "depth_strength"))
                fx.depth = 1;
            if (!strcmp(name, "shadow_strength"))
                fx.sunshadow = 1;
            if (!strcmp(name, "caustic_strength"))
                fx.caustics = 1;
            if (!strcmp(name, "rim_strength"))
                fx.rim = 1;
            if (!strcmp(name, "shaft_strength"))
                fx.rays = 1;
            *(unsigned *)((char *)&fx + options[opt].field) = strstr(name, "strength") ? (on ? 200 : 25) : on;
            world_scene_light_target(scenes[i]);
            world_scene_render(picture, 1, 1, &fx, 8000);
            seed = 123;
            shake = 2.5f;
            rings[0] = (Ring){398, 92, .08, .5, 1};
            particles[0] = (Particle){393, 90, 0, 0, .1, .5, 1};
            cinema_events[0] = (CinemaEvent){.x = 402,
                                             .y = 79,
                                             .age = .35f,
                                             .life = 2,
                                             .power = 1,
                                             .seabed = 1,
                                             .tint = 0xff8040,
                                             .light = 1,
                                             .hold = .1f};
            for (int j = 0; j < WF_TYPES; j++) {
                WaterFx *p = wf_spawn(j, 396, j == WF_SPLASH ? 59 : 83, 1);
                if (p) {
                    p->age = .22f;
                    p->size = 3;
                    p->vx = 18;
                    p->vy = 6;
                    p->light = 255;
                }
            }
            world_scene_render(input[i], 1, 1, &fx, !strcmp(name, "shake") ? 1017 * 8 : 8400);

            world_scene_light_target(NULL);
        }
    char err[256] = {0};
    if (!world_gpu_light_benchmark((const WorldLightScene *const *)scenes, (const uint32_t *const *)input,
                                   CASES, NULL, 1, 1, times, output, err, sizeof err)) {
        puts(err);
        return 1;
    }
    int failed = 0;
    for (int n = 0; n < OPTIONS; n++) {
        int changed = 0, wing = 0;
        for (int j = 0; j < MAX_N; j++)
            if ((output[off[n]][j] ^ output[lit[n]][j]) & 0xffffff) {
                changed++;
                if (j % MAX_W < WORLD_VIEW_WING || j % MAX_W >= MAX_W - WORLD_VIEW_WING)
                    wing++;
            }
        if (!wing)
            failed = 1;
        printf("%s %d pixels (%d in wings)\n", options[n].name, changed, wing);
    }
    return failed;
}
