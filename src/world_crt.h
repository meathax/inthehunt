#ifndef WORLD_CRT_H
#define WORLD_CRT_H

typedef enum {
    WORLD_CRT_PIXELS,
    WORLD_CRT_LOTTES_FLAT,
    WORLD_CRT_LOTTES_BLOOM,
    WORLD_CRT_HYLLIAN_BEAM,
    WORLD_CRT_COUNT
} WorldCrtProfile;

/* Four 16-byte HLSL constant-buffer rows. Source/SRV and output/RTV use
 * ordinary UNORM. The CRT shader handles its own transfer functions. */
typedef struct {
    float source_size[2], viewport_size[2];
    float viewport_origin[2], hard_scan, hard_pixel;
    float mask_dark, mask_light, bloom_amount, brightness;
    float beam_min, beam_max, scanline_strength, profile;
} WorldCrtConstants;

/* The pixel shader for every profile, embedded from shaders/world_crt.hlsl
 * when the executable is built (world_crt_shader.c). Entry point "main". */
const char *world_crt_source(void);
const char *world_crt_id(WorldCrtProfile profile);
const char *world_crt_name(WorldCrtProfile profile);
int world_crt_parse(const char *id, WorldCrtProfile *profile);
void world_crt_constants(WorldCrtConstants *constants, WorldCrtProfile profile,
                         unsigned viewport_width, unsigned viewport_height,
                         unsigned viewport_x, unsigned viewport_y);

#endif
