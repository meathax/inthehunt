#include "world_crt.h"
#include <string.h>

static const char *const ids[WORLD_CRT_COUNT] = {
    "pixels", "lottes-flat", "lottes-bloom", "hyllian-beam"
};
static const char *const names[WORLD_CRT_COUNT] = {
    "Pixels", "Lottes Flat", "Lottes Bloom", "Hyllian Beam"
};

const char *world_crt_id(WorldCrtProfile profile)
{
    return profile >= 0 && profile < WORLD_CRT_COUNT ? ids[profile] : ids[0];
}

const char *world_crt_name(WorldCrtProfile profile)
{
    return profile >= 0 && profile < WORLD_CRT_COUNT ? names[profile] : names[0];
}

int world_crt_parse(const char *id, WorldCrtProfile *profile)
{
    if (!id || !profile) return 0;
    for (unsigned i = 0; i < WORLD_CRT_COUNT; ++i)
        if (!strcmp(id, ids[i])) {
            *profile = (WorldCrtProfile)i;
            return 1;
        }
    return 0;
}

void world_crt_constants(WorldCrtConstants *c, WorldCrtProfile profile,
                         unsigned width, unsigned height, unsigned x, unsigned y)
{
    memset(c, 0, sizeof *c);
    if (profile < 0 || profile >= WORLD_CRT_COUNT) profile = WORLD_CRT_PIXELS;
    c->source_size[0] = 320.0f;
    c->source_size[1] = 240.0f;
    c->viewport_size[0] = (float)width;
    c->viewport_size[1] = (float)height;
    c->viewport_origin[0] = (float)x;
    c->viewport_origin[1] = (float)y;
    c->hard_scan = -8.0f;
    c->hard_pixel = -3.0f;
    c->mask_dark = 0.5f;
    c->mask_light = 1.5f;
    c->bloom_amount = profile == WORLD_CRT_LOTTES_BLOOM ? 0.12f : 0.0f;
    c->brightness = 1.0f;
    c->beam_min = 0.72f;
    c->beam_max = 1.0f;
    c->scanline_strength = 0.56f;
    c->profile = (float)profile;
    if (profile == WORLD_CRT_HYLLIAN_BEAM) {
        c->mask_dark = 0.65f;
        c->mask_light = 1.15f;
        c->brightness = 1.1f;
    }
}

