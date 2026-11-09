#include <stdint.h>
#include "world_torpedo.h"

static uint16_t word(const uint8_t *rom, uint16_t offset)
{
    return rom[offset] | ((uint16_t)rom[(uint16_t)(offset + 1)] << 8);
}

void world_launch_torpedo(Torpedo *shot, const uint8_t *rom, uint16_t x,
                          uint16_t y, uint8_t facing, uint16_t pose)
{
    unsigned side = facing & 1;
    shot->x = x + word(rom, 0x9ba9 + 2 * side);
    shot->y = y + word(rom, 0x9b9f + pose);
    shot->fraction = 0;
    shot->facing = facing;
    shot->velocity = word(rom, 0x9c00);
    if (side) shot->velocity = -shot->velocity;
    shot->timer = word(rom, 0x9c1e); /* initial acceleration countdown */
}

void world_fly_torpedo(Torpedo *shot, const uint8_t *rom, uint16_t scroll_x,
                       uint16_t scroll_y)
{
    unsigned side = (shot->facing & 1) * 2;
    if (shot->timer) {
        --shot->timer;
        if (!(shot->timer & 7)) shot->velocity += word(rom, 0x9bb5 + side);
        if (!shot->timer) shot->velocity += word(rom, 0x9bb9 + side);
    }
    unsigned fraction = shot->fraction + (shot->velocity & 255);
    unsigned pixels = shot->velocity >> 8;
    if (pixels & 128) pixels += 0xff00;
    shot->fraction = (uint8_t)fraction;
    shot->x += scroll_x + pixels + (fraction >> 8);
    shot->y += scroll_y;
}
