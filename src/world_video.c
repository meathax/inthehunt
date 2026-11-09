#include "world_video.h"

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint8_t planar_pen(const uint8_t *row, unsigned plane_size, unsigned x)
{
    unsigned shift = 7 - (x & 7);
    unsigned pen = 0;
    for (unsigned plane = 0; plane < 4; ++plane)
        pen |= ((row[plane * plane_size] >> shift) & 1u) << plane;
    return (uint8_t)pen;
}

uint8_t world_video_tile_pen(const uint8_t *gfx1, unsigned code, unsigned x, unsigned y)
{
    unsigned row = (code & 0xffff) * 8 + (y & 7);
    return planar_pen(gfx1 + row, 0x80000, x);
}

uint8_t world_video_sprite_pen(const uint8_t *gfx2, unsigned code, unsigned x, unsigned y)
{
    unsigned row = (code & 0x7fff) * 32 + (y & 15) + ((x & 8) << 1);
    return planar_pen(gfx2 + row, 0x100000, x);
}

WorldVideoPixel world_video_playfields(const uint8_t *tiles, const uint8_t *gfx1,
                                      const WorldVideoLayer layers[3], uint16_t video_control,
                                      unsigned x, unsigned y)
{
    WorldVideoPixel pixel = { 0, 0 };
    for (int layer = 2; layer >= 0; --layer) {
        unsigned master = layers[layer].master;
        if (master & 0x10) continue;

        unsigned sy = (y + 136 + layers[layer].scroll_y) & 511;
        unsigned scroll = layers[layer].scroll_x;
        if (master & 0x40)
            scroll = word(tiles + 0xf400 + layer * 0x400 + sy * 2);
        unsigned wide = master & 4;
        unsigned columns = wide ? 128 : 64;
        unsigned sx = x + 80 + scroll - 2 * layer + (wide ? 256 : 0);
        unsigned entry = (master & 3) * 0x4000 +
                         ((sy / 8) * columns + ((sx / 8) & (columns - 1))) * 4;
        const uint8_t *tile = tiles + (entry & 0xffff);
        unsigned attr = word(tile + 2);
        unsigned pen = world_video_tile_pen(gfx1, word(tile),
            (attr & 0x200) ? ~sx : sx, (attr & 0x400) ? ~sy : sy);
        if (!pen && layer != 2) continue;

        unsigned bank = (attr & 0x7f) | ((video_control >> 7) & 0x180);
        pixel.palette_index = (uint16_t)(bank * 16 + pen);
        if (pen && ((attr & 0x100) || ((attr & 0x80) && pen >= 8)))
            pixel.sprite_priority = 1;
    }
    return pixel;
}

static void sprite_row(uint16_t output[320], uint8_t priority[320], const uint8_t *gfx2,
                       unsigned code, unsigned local_y, int left, int flip_x,
                       unsigned bank, int behind)
{
    for (int col = 0; col < 16; ++col) {
        int x = left + col;
        if (x < 0 || x >= 320 || priority[x] == 2) continue;
        unsigned pen = world_video_sprite_pen(gfx2, code, flip_x ? 15 - col : col, local_y);
        if (!pen) continue;
        if (!behind || !priority[x]) output[x] = (uint16_t)(bank * 16 + pen);
        /* The first opaque sprite pixel owns this position, even behind a tile. */
        priority[x] = 2;
    }
}

void world_video_scanline(uint16_t output[320], unsigned y, const uint8_t *tiles,
                          const uint8_t *gfx1, const uint8_t *gfx2, const uint8_t *sprite_buffer,
                          const WorldVideoLayer layers[3], uint16_t video_control)
{
    uint8_t priority[320];
    for (unsigned x = 0; x < 320; ++x) {
        WorldVideoPixel pixel = world_video_playfields(tiles, gfx1, layers, video_control, x, y);
        output[x] = pixel.palette_index;
        priority[x] = pixel.sprite_priority;
    }
    if (video_control & 0x80) return;

    int independent_priority = (video_control & 0x2000) != 0;
    for (unsigned offset = 0; offset < 2048; ) {
        const uint8_t *entry = sprite_buffer + offset;
        unsigned geometry = word(entry);
        unsigned columns = 1u << ((geometry >> 11) & 3);
        unsigned rows = 1u << ((geometry >> 9) & 3);
        offset += 8 * columns;

        int top = 360 - (int)(geometry & 511) - 16 * (int)(rows - 1);
        int local_y = (int)y - top;
        if (local_y < 0 || (unsigned)local_y >= rows * 16) continue;
        unsigned attr = word(entry + 4);
        int flip_x = (attr & 0x100) != 0;
        if (attr & 0x200) local_y = (int)(rows * 16) - 1 - local_y;
        unsigned code = word(entry + 2) + (unsigned)local_y / 16;
        unsigned bank = (attr & (independent_priority ? 0x7f : 0xff)) |
                        ((video_control & 0x8000) ? 0x100 : 0);
        int behind = !independent_priority || !(attr & 0x80);
        int left = (int)((word(entry + 6) - 16u) & 511) - 80;
        for (unsigned i = 0; i < columns; ++i) {
            unsigned display_col = flip_x ? columns - 1 - i : i;
            int xpos = left + (int)display_col * 16;
            if (xpos < 320 && xpos > -16)
                sprite_row(output, priority, gfx2, code, local_y, xpos, flip_x, bank, behind);
            if (xpos >= 497)
                sprite_row(output, priority, gfx2, code, local_y, xpos - 512, flip_x, bank, behind);
            code += 8;
        }
    }
}

uint32_t world_video_rgb(uint16_t palette_word)
{
    unsigned r = palette_word & 31, g = (palette_word >> 5) & 31, b = (palette_word >> 10) & 31;
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return 0xff000000u | r << 16 | g << 8 | b;
}
