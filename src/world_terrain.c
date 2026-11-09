#include <stdint.h>
#include "world_tile_address.h"

static uint16_t word(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

static void put(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = value >> 8;
}

/* 68DF. Columns occupy four bytes each; rows wrap around the 64-row
 * playfield. The upper five attribute bits select a material, and SET1
 * on the V33 uses the low four bits to select a bit in a word. */
uint16_t world_terrain_mask(const uint8_t *ram, const uint8_t *tiles,
                            uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + word(ram + 0xb70a);
    uint16_t map_y = 0x177 - y + word(ram + 0xb70c);
    unsigned material = tiles[world_tile_address(map_x, map_y) + 3] >> 3;
    return (uint16_t)(1u << (material & 15));
}

static void move_axis(uint8_t *position, uint16_t velocity)
{
    unsigned sum = position[0] + (velocity & 255);
    int pixels = (int)(velocity >> 8);
    if (pixels >= 128) pixels -= 256;
    position[0] = (uint8_t)sum;
    put(position + 1, word(position + 1) + pixels + (sum >> 8));
}

/* B000:915C..9192. Camera displacement and steering have already run.
 * Solid terrain restores only the previous whole-pixel Y coordinate. */
void world_kind11_move(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles, uint16_t enemy)
{
    uint8_t *actor = ram + enemy;
    uint16_t vy = word(actor + 0x3a);
    move_axis(actor + 0x20, word(actor + 0x38));
    put(actor + 0x34, word(actor + 0x24));
    move_axis(actor + 0x23, vy);
    uint16_t sensor_y = word(actor + 0x24) + word(rom + 0xb8d60 + 2 * (vy >> 15));
    uint16_t terrain = world_terrain_mask(ram, tiles, word(actor + 0x21), sensor_y);
    if (terrain & word(rom + 0xb9189))
        put(actor + 0x24, word(actor + 0x34));
}
