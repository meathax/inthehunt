#ifndef WORLD_SURFACE_H
#define WORLD_SURFACE_H
#include <stdint.h>

static inline unsigned world_surface_word(const uint8_t *ram, unsigned at)
{
    return ram[at] | (unsigned)ram[at + 1] << 8;
}

/* Screen-space sea boundary from the native stage/row-scroll contract.
 * Stage three's mode 4 uses city parallax instead of wave row-scroll, but
 * retains the static surface at background row 168 (tile row 38, including
 * the renderer's 136-row origin). Verified against city and boss captures.
 * Negative means the whole visible picture is underwater. */
static inline int world_surface_row(const uint8_t *ram)
{
    /* The title uses the middle playfield's wave table starting at FA40,
       raster row 288. Its sky is above water even after the demo returns. */
    switch (world_surface_word(ram, 0x80)) {
    case 0x1de6: case 0x1e6c: case 0x1ea5: case 0x1f19: case 0x1f52: case 0x1f85:
        return 288 - 136 - (int16_t)world_surface_word(ram, 0xb70c);
    }
    if (!(ram[0x292] & 4)) return -1;
    int row;
    switch (ram[0x298] & 14) {
    case 0: row = 112; break;
    case 2: row = 168; break;
    case 4:
        if (world_surface_word(ram, 0x84) != 2) return -1;
        row = 168;
        break;
    case 8: {
        unsigned shift = (world_surface_word(ram, 0xb8ca) << 3) & 0xffff;
        row = (int)((104 - (((shift >> 8) | (shift << 8)) & 0xffff)) & 511);
        break;
    }
    default: return -1;
    }
    int y = (row - (int)world_surface_word(ram, 0xb710)) & 511;
    return y >= 256 ? y - 512 : y;
}
#endif
