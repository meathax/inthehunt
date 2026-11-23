#include "../src/world_video.h"
#include <stdio.h>
#include <string.h>

/* Check the entire scanline against a modulo-512 silhouette. Eight
   columns on one descriptor exercise wrapping within a composite. */
int main(void)
{
    static uint8_t graphics[0x400000], oam[2048];
    uint16_t pixels[428];
    WorldVideoLayer layers[3] = {{0x10,0,0},{0x10,0,0},{0x10,0,0}};
    memset(graphics, 0xff, 0x100000);
    oam[0] = 104;
    oam[1] = 0x19; /* row zero, eight columns, top = 0 */
    for (int mode = 0; mode < 2; ++mode) {
        int left = mode ? -54 : 0, width = mode ? 428 : 320;
        for (int field = 0; field < 512; ++field) {
            oam[6] = (uint8_t)field;
            oam[7] = (uint8_t)(field >> 8);
            world_video_scanline(pixels, 0, NULL, NULL, graphics, oam,
                                 layers, 0, width, left);
            for (int x = 0; x < width; ++x) {
                unsigned offset = (unsigned)(left + x + 96 - field) & 511;
                if (pixels[x] != (offset < 128)) {
                    fprintf(stderr, "width %d field %d pixel %d\n", width, field, x);
                    return 1;
                }
            }
        }
    }
    puts("All 512 composite positions passed at both widths.");
    return 0;
}
