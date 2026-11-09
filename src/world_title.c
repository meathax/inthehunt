#include "world_title.h"
#include "world_enemy_target.h"
#include "world_frame.h"
#include "world_dispatch.h"
#include <string.h>

uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);
void world_queue_sound(uint8_t *, uint16_t);

static uint16_t word(const uint8_t *p, uint16_t i)
{
    return (uint16_t)(p[i] | ((uint16_t)p[(uint16_t)(i + 1)] << 8));
}

static void put(uint8_t *p, uint16_t i, uint16_t value)
{
    p[i] = (uint8_t)value;
    p[(uint16_t)(i + 1)] = (uint8_t)(value >> 8);
}

static uint16_t countdown(uint8_t *a, unsigned offset)
{
    uint16_t remaining = word(a, offset) - 1;
    put(a, offset, remaining);
    return remaining;
}

static uint16_t spawn(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = world_spawn_child(ram, actor, handler);
    if (!child) return 0x8480;
    put(ram, child + 2, 0xa000);
    return child;
}

static void sound(uint8_t *ram, uint16_t queue, uint16_t buffer, uint16_t command)
{
    uint16_t tail = (word(ram, queue + 2) + 2) & 14;
    if (tail == word(ram, queue)) return;
    put(ram, queue + 2, tail);
    put(ram, buffer + tail, command);
}

static void palette_job(uint8_t *ram, unsigned slot, uint16_t palette, uint16_t mode)
{
    if (word(ram, 0x262) & 0x8800) return;
    uint16_t job = 0x9e06 + slot * 8;
    put(ram, job, palette);
    put(ram, job + 2, 0x80);
    put(ram, job + 4, mode);
    put(ram, job + 6, 0x1020);
}

/* Intro flashes, their palette fades, and the cue that starts the title music. */
static int flash_step(uint8_t *ram, const uint8_t *bank, uint8_t *a, uint16_t handler)
{
    int interrupted = word(ram, 0x2b8) || ram[0x2ac] || ram[0x2b2];
    if (handler == 0x8a16 || handler == 0x8a91 || handler == 0x8b16) {
        a[0x2c] = bank[handler + 3];
        a[0x2d] = bank[handler + 7];
        put(a, 0x2a, word(bank, handler + 11));
        put(a, 0x21, word(bank, handler + 16));
        put(a, 0x24, word(bank, handler + 21));
        put(a, 0x42, word(bank, handler + 26));
        if (handler == 0x8a91) {
            if (!interrupted) {
                palette_job(ram, 11, 0x77, 3);
                palette_job(ram, 8, 0x200, 0);
            }
            sound(ram, 0xa6d6, 0xa6a2, 0x92);
            handler = 0x8ad9;
        } else {
            handler = handler == 0x8a16 ? 0x8a37 : 0x8b37;
        }
        put(a, 0, handler);
    }
    if (handler == 0x8a37 || handler == 0x8b37) {
        if (interrupted) return WORLD_TITLE_FINISH;
        if (!countdown(a, 0x42)) {
            put(a, 0x42, 4);
            if (handler == 0x8a37) palette_job(ram, 8, 0x200, 0);
            sound(ram, 0xa6d6, 0xa6a2, 0x92);
            put(a, 0, handler == 0x8a37 ? 0x8a61 : 0x8b54);
        }
        return 0;
    }
    if (countdown(a, 0x42)) return WORLD_TITLE_DRAW;
    if (!interrupted) {
        unsigned mode = handler == 0x8b54 ? 6 : 5;
        if (handler != 0x8a61) palette_job(ram, 11, handler == 0x8b54 ? 0x77 : 0x78, mode);
        palette_job(ram, 8, 0x202, mode);
        if (handler == 0x8b54) {
            palette_job(ram, 9, 0x203, mode);
            palette_job(ram, 14, 0x20b, mode);
            palette_job(ram, 16, 0x20d, mode);
            sound(ram, 0xa6de, 0xa6c2, 1);
            ram[0x280] |= 8;
        }
    }
    return WORLD_TITLE_DRAW | WORLD_TITLE_FINISH;
}

static int particle_step(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t handler)
{
    uint8_t *a = ram + actor;
    uint16_t kind = word(a, 0x1e);
    if (handler == 0x8d0b) {
        a[0x2c] = 13;
        a[0x2d] = 40;
        put(a, 0x44, 1);
        put(a, 0x24, 0x88);
        put(a, 0x46, word(bank, 0x8884 + kind));
        a[0x29] = bank[(uint16_t)(0x89d0 + (kind >> 1))];
        if (!kind) world_queue_sound(ram, 0x53);
        uint16_t child = spawn(ram, actor, 0x8dd0);
        put(ram, child + 0x21, word(a, 0x21));
        put(ram, child + 0x24, word(a, 0x24));
        ram[child + 0x29] = a[0x29];
        put(ram, child + 0x1e, kind);
        handler = 0x8d69;
        put(a, 0, handler);
    } else if (handler == 0x8dd0) {
        a[0x2c] = 0x8f;
        a[0x2d] = 0;
        put(a, 0x44, 1);
        put(a, 0x46, word(bank, 0x8928 + kind));
        handler = 0x8ded;
        put(a, 0, handler);
    } else if (handler == 0x8e33) {
        a[0x2c] = 0x8c;
        put(a, 0x2d, 32);
        put(a, 0x24, 0xe0);
        put(a, 0x44, 1);
        if (!kind) world_queue_sound(ram, 0x82);
        handler = 0x8e5a;
        put(a, 0, handler);
    }
    if (ram[0x280] & 4) return WORLD_TITLE_FINISH;
    if (!countdown(a, 0x44)) {
        uint16_t step = word(a, 0x40);
        uint16_t row = handler == 0x8d69 ? 0x8896 : 0x893a;
        uint16_t sprite = word(bank, step + (handler == 0x8e5a ? 0x89ae : row + word(a, 0x46)));
        if (sprite == 0xffff) return WORLD_TITLE_FINISH;
        put(a, 0x2a, sprite);
        put(a, 0x44, handler == 0x8e5a ? 4 : word(bank, step + (handler == 0x8d69 ? 0x8910 : 0x898c)));
        put(a, 0x40, step + 2);
        if (handler == 0x8d69 && step == 12) {
            uint16_t child = spawn(ram, actor, 0x8e33);
            put(ram, child + 0x1e, kind);
            put(ram, child + 0x21, word(a, 0x21) + word(bank, 0x899c + kind));
        }
    }
    return WORLD_TITLE_DRAW;
}

int world_title_actor(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    uint8_t *a = ram + actor;
    const uint8_t *bank = rom + 0xa0000;
    switch (handler) {
    case 0x89d9:
        spawn(ram, actor, 0x8e9a);
        spawn(ram, actor, 0x8a16);
        spawn(ram, actor, 0x8c81);
        spawn(ram, actor, 0x8bc6);
        spawn(ram, actor, 0x8ebc);
        put(a, 0, 0x8a15);
        return 0;
    case 0x8a15:
        return 0;
    case 0x8e9a: case 0x8ebc:
        a[0x2c] = bank[handler + 3];
        a[0x2d] = bank[handler + 7];
        put(a, 0x2a, word(bank, handler + 11));
        put(a, 0x21, word(bank, handler + 16));
        put(a, 0x24, word(bank, handler + 21));
        put(a, 0, handler + 28);
        return WORLD_TITLE_DRAW;
    case 0x8eb6: case 0x8ed8: case 0x8c7b:
        return WORLD_TITLE_DRAW;
    case 0x8a16: case 0x8a37: case 0x8a61: case 0x8a91: case 0x8ad9:
    case 0x8b16: case 0x8b37: case 0x8b54:
        return flash_step(ram, bank, a, handler);
    case 0x8d0b: case 0x8dd0: case 0x8e33: case 0x8d69: case 0x8ded: case 0x8e5a:
        return particle_step(ram, bank, actor, handler);
    case 0x8c81:
        put(a, 0x42, 0x88);
        put(a, 0, 0x8c8b);
        /* fall through */
    case 0x8c8b:
        if (!countdown(a, 0x42)) {
            put(a, 0x40, 0);
            put(a, 0x44, 1);
            put(a, 0x42, 0x120);
            put(a, 0, 0x8ca5);
        }
        return 0;
    case 0x8ca5:
        if (ram[0x280] & 4) return WORLD_TITLE_FINISH;
        if (!countdown(a, 0x44)) {
            uint16_t child = spawn(ram, actor, 0x8d0b), step = word(a, 0x40);
            put(ram, child + 0x1e, word(bank, 0x886e + step));
            put(ram, child + 0x21, word(bank, 0x8858 + step) + (world_random_byte(ram) & 15) - 8);
            put(a, 0x44, 4);
            step += 2;
            put(a, 0x40, step < 0x16 ? step : 0);
        }
        return countdown(a, 0x42) ? 0 : WORLD_TITLE_FINISH;
    case 0x8bc6:
        a[0x2c] = 11;
        a[0x2d] = 63;
        put(a, 0x2a, word(bank, 0x8bd1));
        put(a, 0x21, 0x100);
        put(a, 0x24, 0xa0);
        put(a, 0x42, 0x100);
        put(a, 0, 0x8be7);
        /* fall through */
    case 0x8be7:
        if (!countdown(a, 0x42)) {
            put(a, 0x3a, 0x80);
            put(a, 0x42, 0xe0);
            put(a, 0x44, 0x60);
            put(a, 0, 0x8c13);
        }
        if (ram[0x280] & 4) { put(a, 0x24, 0x110); put(a, 0, 0x8c7b); }
        return 0;
    case 0x8c13: {
        uint16_t speed = word(a, 0x3a);
        unsigned fraction = a[0x23] + (speed & 255);
        a[0x23] = (uint8_t)fraction;
        put(a, 0x24, word(a, 0x24) + (int8_t)(speed >> 8) + (fraction >> 8));
        int idle = !word(ram, 0x2b8) && !ram[0x2ac] && !ram[0x2b2];
        if (!countdown(a, 0x44) && idle) { put(a, 0x44, 0x60); spawn(ram, actor, 0x8a91); }
        if (!countdown(a, 0x42)) {
            if (idle) { put(a, 0x42, 0x80); spawn(ram, actor, 0x8b16); }
            put(a, 0, 0x8c58);
        }
        /* The caller draws before applying the fast-forward position. */
        return WORLD_TITLE_DRAW | ((ram[0x280] & 4) ? WORLD_TITLE_SKIP_MOTION : 0);
    }
    case 0x8c58:
        if (!countdown(a, 0x42)) {
            ram[0x280] |= 1; put(a, 0x42, 0x280); put(a, 0, 0x8c70);
        }
        return WORLD_TITLE_DRAW;
    case 0x8c70:
        if (!countdown(a, 0x42)) ram[0x280] |= 2;
        return WORLD_TITLE_DRAW;
    default:
        return -1;
    }
}

void world_credit_step(uint8_t *ram)
{
    if (ram[0x2c4] & 1) return;
    unsigned inputs = ram[0x2a8] >> 2;
    for (unsigned chute = 0; chute < 2; ++chute) {
        ram[0x2ba] = (uint8_t)((ram[0x2ba] << 1) | ((inputs >> chute) & 1));
        if ((ram[0x2ba] & 0x55) != 5) continue;
        sound(ram, 0xa6de, 0xa6c2, 0x2e);
        unsigned meter = 0x2ac + chute * 6;
        put(ram, meter, word(ram, meter) + 1);
        put(ram, meter + 2, word(ram, meter + 2) + 1);
        uint16_t ratio = word(ram, 0x2bc + 2 * chute);
        if (ram[meter] == ratio >> 8) {
            put(ram, meter, 0);
            uint16_t credits = word(ram, 0x2b8) + (ratio & 255);
            put(ram, 0x2b8, credits >= 100 ? 99 : credits);
        }
    }
    ram[0x2bb] = (uint8_t)((ram[0x2bb] << 1) | ((inputs >> 2) & 1));
    if ((ram[0x2bb] & 15) == 3) {
        uint16_t credits = word(ram, 0x2b8) + 1;
        put(ram, 0x2b8, credits >= 100 ? 99 : credits);
        if (credits < 100) sound(ram, 0xa6de, 0xa6c2, 0x2e);
    }
    if (!(word(ram, 0x274) & 31)) {
        for (unsigned off = 0x2ae; off <= 0x2b4; off += 6)
            if (word(ram, off) > word(ram, off + 2)) put(ram, off + 2, word(ram, off + 2) + 1);
    }
}

int world_start_player(uint8_t *ram)
{
    unsigned credits = word(ram, 0x2b8);
    int free_play = ram[0x2c4] & 1;
    if ((ram[0x262] & 2) || (!credits && !free_play)) return 0;
    for (unsigned bit = 1; bit <= 2; bit *= 2) {
        if ((ram[0x264] & (bit << 2)) || !(ram[0x2aa] & bit)) continue;
        unsigned cost = 1 + ((word(ram, 0x288) & 0x8000) && !(ram[0x288] & bit));
        if (!free_play) {
            if (credits < cost) return 0;
            put(ram, 0x2b8, credits - cost);
        }
        ram[0x266] |= bit;
        ram[0x264] |= bit | (bit << 2);
        return 1;
    }
    return 0;
}

static uint16_t title_text(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t ptr)
{
    for (unsigned n = 0; n < 65536 && rom[ptr] != 0xff; ++n, ++ptr) {
        put(tiles, dest, rom[ptr]);
        put(tiles, dest + 2, 0x104);
        dest = (dest & 0xff00) | ((dest + 4) & 255);
    }
    return dest;
}

static void display_credits(uint8_t *ram, uint8_t *tiles, const uint8_t *rom)
{
    if (ram[0x2c4] & 1) {
        title_text(tiles, rom, 0x2d60, 0x5ce3);
        return;
    }
    uint16_t dest = title_text(tiles, rom, 0x2d60, 0x5cd7);
    unsigned credits = word(ram, 0x2b8);
    if (credits > 99) credits = 99;
    for (unsigned col = 0; col < 5; ++col) {
        unsigned tile = col == 0 ? '0' + credits / 10 : col == 1 ? '0' + credits % 10 : 0;
        put(tiles, dest + 4 * col, tile);
        put(tiles, dest + 4 * col + 2, 0x104);
    }
}

static void title_labels(const uint8_t *ram, uint8_t *tiles, const uint8_t *rom)
{
    title_text(tiles, rom, 0x2860, word(ram, 0x2c0));
    title_text(tiles, rom, 0x2a60, word(ram, 0x2c2));
    title_text(tiles, rom, 0x2c4c, 0x1bb1);
    title_text(tiles, rom, 0x25c4, 0x1bcb);
}

static void title_fade(uint8_t *ram, unsigned duration, uint16_t next)
{
    for (unsigned job = 0x9e06; job < 0xa006; job += 8) {
        put(ram, job + 2, 0x82);
        put(ram, job + 4, 5);
        put(ram, job + 6, 0x1020);
    }
    put(ram, 0x86, duration);
    put(ram, 0x80, next);
}

static void init_title_map(uint8_t *ram, uint8_t *tiles, const uint8_t *rom, unsigned index)
{
    uint16_t source = word(rom, index), base = word(rom, index + 3) << 4;
    uint16_t dest = word(rom, index + 6), size = word(rom, index + 9);
    put(ram, 0xb89a, size);
    for (unsigned col = 0; col < size >> 8; ++col) {
        put(ram, 0xb89c, dest);
        for (unsigned row = 0; row < (size & 255); ++row, source += 2)
            world_draw_tile_block(tiles, rom, base, (dest + 0x400 * row) & 0x3fff,
                                  word(rom + 0xa0000, source));
        dest = (dest & 0xff00) | ((dest + 16) & 255);
    }
}

int world_title_boundary(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom)
{
    uint16_t master = word(ram, 0x80);
    if (master == 0x1f52 || master == 0x1f85) {
        if (master == 0x1f52) title_fade(ram, 64, 0x1f85);
        if (!countdown(ram, 0x86)) {
            put(ram, 0xd2, 0);
            put(ram, 0x262, 0);
            put(ram, 0x80, 0xa31);
        }
        return 1;
    }
    if (master == 0xa09 || master == 0x1d52) {
        put(ram, 0x292, 2);
        put(ram, 0x296, 0);
        memset(ram + 0x100, 0, 0x80);
        memset(tiles, 0, 65536);
        memset(ram + 0xb700, 0, 0x2e);
        world_frame_init_palette(ram, palette, rom, 0xe85a);
        init_title_map(ram, tiles, rom, 0x1da1);
        init_title_map(ram, tiles, rom, 0x1db2);
        put(ram, 0x262, 1);
        world_frame_init_actors(ram, 0x1a0e, 0x8000);
        world_frame_reset_sound(ram);
        put(ram, 0x280, 0);
        ram[0x28c] |= 4;
        put(ram, 0x80, 0x1de6);
        return 1;
    }
    if (master != 0x1de6 && master != 0x1e6c && master != 0x1ea5) return 0;
    if (word(ram, 0x2a2)) return 0;
    unsigned tick = word(ram, 0x274);
    if (!(tick & 31)) {
        uint16_t label = 0x1d40;
        if (tick & 32) {
            if (ram[0x2c4] & 1) label = 0x1d1c;
            else if (!word(ram, 0x2b8) || ((word(ram, 0x288) & 0x8000) && word(ram, 0x2b8) < 2)) label = 0x1d0a;
            else label = 0x1d2e;
        }
        title_text(tiles, rom, 0x265c + ((ram[0x2c4] & 1) << 8), label);
    }
    if (master == 0x1e6c) {
        if (!countdown(ram, 0x86)) {
            world_frame_init_palette(ram, palette, rom, 0xe87e);
            ram[0x280] |= 4;
            title_labels(ram, tiles, rom);
            put(ram, 0x80, 0x1ea5);
        }
        return 1;
    }
    int has_coin = word(ram, 0x2b8) || ram[0x2ac] || ram[0x2b2];
    if (master == 0x1de6 && has_coin) {
        title_fade(ram, 48, 0x1e6c);
        return 1;
    }
    if (master == 0x1ea5 && !has_coin && (ram[0x280] & 2)) put(ram, 0x80, 0x1f52);
    if (world_start_player(ram)) title_fade(ram, 64, 0x1f19);
    else if (master == 0x1de6 && (ram[0x280] & 1)) {
        title_labels(ram, tiles, rom);
        put(ram, 0x80, 0x1ea5);
    }
    if (master == 0x1ea5 || word(ram, 0x80) == 0x1f19) display_credits(ram, tiles, rom);
    put(ram, 0xb892, 0);
    return 1;
}
