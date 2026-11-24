#ifndef WORLD_TILE_ADDRESS_H
#define WORLD_TILE_ADDRESS_H

#include "world_view.h"

/* Mirrored wings (world_view_wing_mirror) are solid scenery: terrain probes
 * beyond the picture read the reflected cell, so the sub, shots and enemies
 * meet the same walls the picture shows. world_terrain_cam is the camera
 * (RAM b70a) of the current pass; 0 flag means arcade behaviour. */
extern int world_terrain_mirror, world_terrain_cam;

/* Coordinates here already include the work-RAM map scroll offsets. */
static unsigned short world_tile_address(unsigned short x, unsigned short y)
{
    if (world_terrain_mirror) {
        int picture = (int16_t)(unsigned short)(x - world_terrain_cam);   /* field x - 96 */
        if (picture < 0 || picture >= WORLD_VIEW_ARCADE_WIDTH)
            x = (unsigned short)(world_view_mirror_x(picture, world_terrain_cam + 96) + world_terrain_cam);
    }
    unsigned column = (((x & 0xfff8u) >> 1) + 0x30u) & 255u;
    unsigned row = ((y >> 3) + 0x11u) & 63u;
    return (unsigned short)((row << 8) + column);
}
#endif
