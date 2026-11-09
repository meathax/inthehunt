#ifndef WORLD_TILE_ADDRESS_H
#define WORLD_TILE_ADDRESS_H

/* Coordinates here already include the work-RAM map scroll offsets. */
#ifdef __WATCOMC__
static unsigned short world_tile_address(
#else
static inline unsigned short world_tile_address(
#endif
        unsigned short x, unsigned short y)
{
    unsigned column = (((x & 0xfff8u) >> 1) + 0x30u) & 255u;
    unsigned row = ((y >> 3) + 0x11u) & 63u;
    return (unsigned short)((row << 8) + column);
}
#endif
