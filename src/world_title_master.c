#include "world_title_master.h"
#include "world_dispatch.h"
#include "world_frame.h"
#include "world_title.h"
#include <string.h>
void world_queue_sound(uint8_t *, uint16_t);
void world_acquire_private_palette(uint8_t *, uint16_t, unsigned);

static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void put(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void tile_rectangle(uint8_t *ram, uint8_t *tiles, const uint8_t *rom,
                           uint16_t source, uint16_t base, uint16_t origin,
                           unsigned width, unsigned height)
{
    put(ram + 0xb89a, (width << 8) | height);
    for (unsigned x = 0; x < width; ++x) {
        put(ram + 0xb89c, origin);
        uint16_t pos = origin;
        for (unsigned y = 0; y < height; ++y) {
            world_draw_tile_block(tiles, rom, base, pos, word(rom + 0xa0000 + source));
            source += 2;
            pos = (pos + 0x400) & 0x3fff;
        }
        origin = (origin & 0xff00) | ((origin + 16) & 255);
    }
}

static void clear_palette_jobs(uint8_t *ram)
{
    for (unsigned i = 0; i < 64; ++i) {
        memset(ram + 0x9e06 + 8*i, 0, 8);
        put(ram + 0x9e08 + 8*i, 0x40);
    }
}

static void fade_palette(uint8_t *ram, unsigned first, unsigned duration)
{
    for (unsigned i = first; i < 64; ++i) {
        put(ram + 0x9e08 + 8*i, 0x82);
        put(ram + 0x9e0a + 8*i, duration);
        put(ram + 0x9e0c + 8*i, 0x1020);
    }
}

/* 32A9 carries the time-dependent difficulty credit into the next stage. */
static void stage_difficulty_credit(uint8_t *ram, const uint8_t *rom)
{
    unsigned index = 0;
    while ((int16_t)word(ram + 0x8e) >= (int16_t)word(rom + 0x24ac + index)) {
        index += 2;
        if (index > 14) break;
    }
    uint16_t credit = word(ram + 0xba48) + word(rom + 0x24ba + index);
    index = (uint16_t)(2 * (word(ram + 0xba4a) - 1));
    for (;;) {
        uint16_t cost = word(rom + (uint16_t)(0x24ca + index));
        credit -= cost;
        if ((int16_t)credit <= 0) {
            credit += cost;
            break;
        }
        index = (uint16_t)(index + 2);
    }
    put(ram + 0xba48, credit);
    put(ram + 0xba4a, (index >> 1) + 1);
}

int world_title_master(uint8_t *ram, uint8_t *tiles, uint8_t *oam,
                       uint8_t *palette, const uint8_t *rom, int busy_system)
{
    unsigned stage = word(ram + 0x27e);
    switch (word(ram + 0x80)) {
    case 0x0bb3:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            fade_palette(ram, 1, 6);
            put(ram + 0x86, 80);
            put(ram + 0x80, 0xbed);
        }
        return 1;
    case 0x0bed:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            memset(tiles, 0, 65536);
            clear_palette_jobs(ram);
            put(ram + 0x80, 0xced);
        }
        return 1;
    case 0x0ced: {
        put(ram + 0x262, word(ram + 0x262) & ~0x8001u);
        unsigned next = (uint16_t)(word(ram + 0x84) + 1);
        put(ram + 0x84, next);
        if (next == 6) put(ram + 0x80, word(ram + 0x272) >= 2 ? 0x333d : 0x344d);
        else {
            clear_palette_jobs(ram);
            put(ram + 0x80, 0x24e2);
        }
        return 1;
    }
    /* 0:333D..3444: the true ending (272 >= 2 at the stage-six clear).
       Both players sail the 8EE1/8F12 epilogue for 3Ch seconds of B8A0
       (1 when word 84 is 8); if both are still in play when it runs out the
       level becomes 3 and 342A waits for word 86, then marks both 126
       records 8 and goes to 35BE. If either leaves play first the level
       stays 2, the scene fades and the normal ending at 344D runs. */
    case 0x333d:
        memset(tiles, 0, 65536);                                        /* 0:37C7 */
        world_frame_init_palette(ram, palette, rom, 0xeb3e);
        world_acquire_private_palette(ram, 0x80, 0x17c);                /* 0:110B, BP=80h */
        memset(ram + 0xb700, 0, 46);                                    /* 0:439F */
        put(ram + 0x292, 0);
        put(ram + 0x262, 3);
        world_frame_init_actors(ram, 0x1a66, 0x8000);                   /* 0:16DD */
        world_frame_reset_sound(ram);                                   /* 0:360D */
        world_queue_sound(ram, 0x15);
        put(ram + 0x264, 3);
        put(ram + 0xb8a0, word(ram + 0x84) == 8 ? 1 : 0x3c);
        put(ram + 0x80, 0x33a9);
        /* fall through */
    case 0x33a9:
        if (word(ram + 0xb8a0)) {
            if ((word(ram + 0x264) ^ 3) == 0) return 1;
            put(ram + 0x86, 0x100);
            put(ram + 0x272, 2);
            put(ram + 0x80, 0x33e4);
        } else {
            put(ram + 0x272, 3);
            put(ram + 0xb8a0, 0);
            put(ram + 0x86, 0);
            put(ram + 0x80, 0x342a);
        }
        return 1;
    case 0x33e4:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            fade_palette(ram, 0, 5);
            put(ram + 0x86, 0x30);
            put(ram + 0x80, 0x341e);
        }
        return 1;
    case 0x341e:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) put(ram + 0x80, 0x344d);
        return 1;
    case 0x342a:
        if (word(ram + 0x86)) {
            put(ram + 0x126, 8);
            put(ram + 0x166, 8);
            put(ram + 0x80, 0x35be);
        }
        return 1;
    /* 0:344D..360B: the ending. The first scene (by 272: bonus level)
       draws its picture, records the clear in each player's 126 slot and
       runs the 1A50 actors until one sets word 86; then fades, shows the
       second scene (1AF7 actors, sound by 272) and goes on to C466. */
    case 0x344d: {
        unsigned bonus = 2 * (word(ram + 0x272) & 3);
        unsigned clear = word(rom + 0x3445 + bonus);
        unsigned slot = (word(ram + 0x264) & 2) << 5;
        put(ram + 0x126 + slot, clear);
        if (word(ram + 0x272) == 2) put(ram + 0x126 + (slot ^ 0x40), clear - 1);
        memset(tiles, 0, 65536);
        world_frame_init_palette(ram, palette, rom, 0xe9a4);
        memset(ram + 0xb700, 0, 46);
        put(ram + 0x292, 0);
        unsigned size = word(rom + 0x3301 + bonus);
        tile_rectangle(ram, tiles, rom, word(rom + 0x32e9 + bonus), 0x0000, word(rom + 0x32f9 + bonus),
                       size >> 8, size & 255);
        tile_rectangle(ram, tiles, rom, word(rom + 0x32f1 + bonus), 0x4000, 0x1330, 10, 4);
        uint16_t cell = 0x2340;                                         /* 0:5F4A */
        for (uint16_t text = 0xfeb7; rom[text] != 0xff; ++text) {
            put(tiles + cell, rom[text]);
            put(tiles + (uint16_t)(cell + 2), 0);
            cell = (cell & 0xff00) | (uint8_t)(cell + 4);
        }
        put(ram + 0x262, 3);
        world_frame_init_actors(ram, 0x1a50, 0x8000);
        world_frame_reset_sound(ram);
        put(ram + 0x86, 0);
        put(ram + 0x80, 0x3513);
    }
        /* fall through */
    case 0x3513:
        if (word(ram + 0x86)) put(ram + 0x80, 0x3522);
        return 1;
    case 0x3522:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            fade_palette(ram, 0, 7);
            put(ram + 0x86, 0x80);
            put(ram + 0x80, 0x355b);
        }
        return 1;
    case 0x355b:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            ram[0x262] &= (uint8_t)~1u;
            put(ram + 0x80, 0x356e);
        }
        return 1;
    case 0x356e: {
        unsigned bonus = 2 * (word(ram + 0x272) & 3);
        memset(tiles, 0, 65536);
        world_frame_init_palette(ram, palette, rom, word(rom + 0x3309 + bonus));
        put(ram + 0x262, 3);
        world_frame_init_actors(ram, 0x1af7, 0x8000);
        world_frame_reset_sound(ram);
        world_queue_sound(ram, word(rom + 0x3323 + bonus));
        put(ram + 0x86, 0);
        put(ram + 0x80, 0x35be);
        return 1;
    }
    case 0x35be:
        if (word(ram + 0x86)) {
            fade_palette(ram, 0, 6);
            put(ram + 0x86, 0x50);
            put(ram + 0x80, 0x35f9);
        }
        return 1;
    case 0x35f9:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            put(ram + 0x262, 0);
            put(ram + 0x80, 0xc466);
        }
        return 1;
    case 0x24e2: {
        stage_difficulty_credit(ram, rom);
        memset(tiles, 0, 65536);
        memset(ram + 0xb700, 0, 46);
        put(ram + 0x292, 0);
        unsigned players = word(ram + 0x264) & 3;
        world_frame_init_palette(ram, palette, rom, word(rom + 0x22a3 + 2*players));
        tile_rectangle(ram, tiles, rom, 0x4dd4, 0x4000, 0x1030, 10, 4);
        if (busy_system >= 0 && !world_frame_busy_vblank(ram, (uint16_t)busy_system)) return 0;
        tile_rectangle(ram, tiles, rom, 0x4e24, 0x8000, 0x2030, 10, 4);
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned col = 0; col < 40; ++col) {
                unsigned pos = 0x8000 + ((0x2032 + 256*row + 4*col) & 0xffff);
                put(tiles + pos, word(tiles + pos) + 2);
            }
        put(ram + 0x262, 1);
        world_frame_init_actors(ram, 0x1a24, 0x8000);
        world_frame_reset_sound(ram);
        world_queue_sound(ram, 0x0a);
        put(ram + 0x86, 384);
        put(ram + 0xd2, 0);
        put(ram + 0x258, word(ram + 0x254));
        put(ram + 0x25a, word(ram + 0x256));
        put(ram + 0x80, 0x2576);
        /* Initialization runs the new master immediately. */
        /* fall through */
    }
    case 0x2576:
    case 0x25b5: {
        unsigned master = word(ram + 0x80);
        if (word(ram + 0x2a2) && (word(ram + 0x2a8) & 0x100)) return 0;
        put(ram + 0xb892, 0);
        world_start_player(ram);
        if (master == 0x2576) {
            unsigned players = word(ram + 0x266), buttons = word(ram + 0xb742);
            if (word(ram + 0xd2) || ((players & 1) && (buttons & 0xf0)) ||
                ((players & 2) && (buttons & 0xf000))) {
                put(ram + 0x86, 256);
                put(ram + 0x80, 0x25b5);
            }
        } else {
            put(ram + 0x86, word(ram + 0x86) - 1);
            if (!word(ram + 0x86)) {
                ram[0x263] |= 0x80;
                fade_palette(ram, 0, 5);
                put(ram + 0x86, 48);
                put(ram + 0x80, 0x25ff);
            }
        }
        return 1;
    }
    case 0x25ff:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (!word(ram + 0x86)) {
            put(ram + 0x262, 0);
            put(ram + 0x80, 0xa0e);
        }
        return 1;
    case 0x1f19:
        put(ram + 0x86, word(ram + 0x86) - 1);
        if (word(ram + 0x86)) return 1;
        put(ram + 0x262, 0);
        put(ram + 0xba48, word(rom + 0x1d02 + 2 * (stage & 3)));
        put(ram + 0xba4a, 4);
        for (unsigned i = 0; i < 64; ++i) {
            uint8_t *job = ram + 0x9e06 + 8 * i;
            memset(job, 0, 8);
            put(job + 2, 0x40);
        }
        put(ram + 0x80, word(rom + 0x1cd6 + 2 * word(ram + 0x84)));
        return 1;
    case 0x0a0e:
        put(ram + 0xba4c, 0);
        put(ram + 0x28c, 0);
        put(ram + 0x27c, stage);
        put(ram + 0xb74e, word(ram + 0x274));
        put(ram + 0xb750, -word(ram + 0x274));
        memset(tiles, 0, 0x10000);
        memset(oam, 0, 0x800);
        world_frame_init_palette(ram, palette, rom, 0xe8a2);
        memset(ram + 0xb700, 0, 46);
        put(ram + 0x292, 0);
        tile_rectangle(ram, tiles, rom, word(rom + 0xfed8 + 2 * word(ram + 0x84)),
                       0x4000, 0x1030, 10, 16);
        if (busy_system >= 0 && !world_frame_busy_vblank(ram, (uint16_t)busy_system)) return 0;
        tile_rectangle(ram, tiles, rom, 0x30b8, 0x8000, 0x10c0, 6, 8);
        put(ram + 0x262, 1);
        world_frame_init_actors(ram, 0x19e2, 0x8000);
        world_frame_reset_sound(ram);
        put(ram + 0x86, 0x1a0);
        put(ram + 0x80, 0x2218);
        world_frame_briefing_master(ram, 0x2218);
        return 1;
    }
    return 0;
}
