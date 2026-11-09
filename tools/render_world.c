#include "../src/world_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_image(const char *path, uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static unsigned word(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

int main(int argc, char **argv)
{
    static uint8_t gfx1[0x200000], gfx2[0x400000], tiles[0x10000], sprites[2048], palette[16384];
    uint8_t header[24], rgb[320 * 3], reference[320 * 4];
    if (argc != 5) {
        fprintf(stderr, "usage: render_world FRAME GFX1 GFX2 RGB_OUTPUT\n");
        return 2;
    }
    if (!read_image(argv[2], gfx1, sizeof gfx1) || !read_image(argv[3], gfx2, sizeof gfx2)) {
        fprintf(stderr, "World graphics regions are missing or have the wrong size\n");
        return 2;
    }
    FILE *frame = fopen(argv[1], "rb");
    if (!frame || fread(header, 1, sizeof header, frame) != sizeof header ||
        (memcmp(header, "WRV1", 4) && memcmp(header, "WRN1", 4)) || fread(tiles, 1, sizeof tiles, frame) != sizeof tiles ||
        fread(sprites, 1, sizeof sprites, frame) != sizeof sprites ||
        fread(palette, 1, sizeof palette, frame) != sizeof palette) {
        fprintf(stderr, "Invalid World video capture\n");
        return 2;
    }
    WorldVideoLayer layers[3];
    for (unsigned i = 0; i < 3; ++i) {
        layers[i].master = word(header + 4 + i * 6);
        layers[i].scroll_y = word(header + 6 + i * 6);
        layers[i].scroll_x = word(header + 8 + i * 6);
    }
    FILE *output = fopen(argv[4], "wb");
    if (!output) return 2;
    int compare = memcmp(header, "WRV1", 4) == 0;
    unsigned mismatches = 0;
    for (unsigned y = 0; y < 240; ++y) {
        uint16_t line[320];
        world_video_scanline(line, y, tiles, gfx1, gfx2, sprites, layers, word(header + 22));
        if (compare && fread(reference, 1, sizeof reference, frame) != sizeof reference) return 2;
        for (unsigned x = 0; x < 320; ++x) {
            uint32_t color = world_video_rgb((uint16_t)word(palette + line[x] * 2));
            rgb[x * 3] = (uint8_t)(color >> 16);
            rgb[x * 3 + 1] = (uint8_t)(color >> 8);
            rgb[x * 3 + 2] = (uint8_t)color;
            const uint8_t *ref = reference + x * 4;
            if (compare) mismatches += rgb[x * 3] != ref[2] || rgb[x * 3 + 1] != ref[1] || rgb[x * 3 + 2] != ref[0];
        }
        if (fwrite(rgb, 1, sizeof rgb, output) != sizeof rgb) return 2;
    }
    if (fgetc(frame) != EOF || fclose(output)) return 2;
    fclose(frame);
    if (compare) printf("%u/76800 native pixels match the World board\n", 76800 - mismatches);
    return mismatches ? 1 : 0;
}
