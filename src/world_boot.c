#include "world_boot.h"
#include "world_title.h"
#include <string.h>

static uint16_t word(const uint8_t *p, unsigned i)
{
    return (uint16_t)(p[i] | ((uint16_t)p[i + 1] << 8));
}

static void put(uint8_t *p, unsigned i, uint16_t value)
{
    p[i] = (uint8_t)value;
    p[i + 1] = (uint8_t)(value >> 8);
}

/* Reset vector 7000:F2CD, ROM test F4C6. World uses unbanked rows with
 * independent sums for both byte lanes. Check bounds before following a row. */
static int rom_test(const uint8_t *rom)
{
    unsigned row = 0x7feb8;
    for (; row + 14 <= 0x80000; row += 14) {
        uint16_t banked = word(rom, row);
        if (banked == 0xffff) return row != 0x7feb8;
        unsigned start = (unsigned)word(rom, row + 2) << 4;
        unsigned length = (unsigned)word(rom, row + 4) << 4;
        if (banked || !length || start + length > 0x100000) return 0;
        uint16_t even = 0, odd = 0;
        for (unsigned i = start; i < start + length; i += 2) {
            even += rom[i];
            odd += rom[i + 1];
        }
        if (even != word(rom, row + 6) || odd != word(rom, row + 8)) return 0;
    }
    return 0;
}

/* F9B7 varies the initial byte by 0x11 per sweep and advances it an
 * extra step on each 256-byte page. FA41 does the same with paired lanes. */
static int ram_test(volatile uint8_t *memory, unsigned size, unsigned lanes)
{
    for (unsigned sweep = 0; sweep < 16; ++sweep) {
        for (unsigned i = 0; i < size; ++i)
            memory[i] = (uint8_t)(17 * sweep + i + lanes * (i >> 8));
        for (unsigned i = 0; i < size; ++i)
            if (memory[i] != (uint8_t)(17 * sweep + i + lanes * (i >> 8))) return 0;
    }
    return 1;
}

static void init_coinage(uint8_t *ram, const uint8_t *rom, uint16_t dips)
{
    unsigned mode = dips & 0xf800;
    if (mode == 0xf000) put(ram, 0x2c4, 1);
    if (mode == 0xe000) put(ram, 0x288, 0x8000);
    unsigned coins = dips >> 10;
    if (coins & 2) {
        unsigned a = coins & 12, b = (coins >> 2) & 12;
        put(ram, 0x2bc, word(rom, 0x5905 + a));
        put(ram, 0x2be, word(rom, 0x5915 + b));
        put(ram, 0x2c0, word(rom, 0x5907 + a));
        put(ram, 0x2c2, word(rom, 0x5917 + b));
    } else {
        unsigned row = 0x5885 + 2 * (coins & 0x3c);
        put(ram, 0x2bc, word(rom, row));
        put(ram, 0x2be, word(rom, row));
        put(ram, 0x2c0, word(rom, row + 2));
        put(ram, 0x2c2, word(rom, row + 4));
    }
}

int world_boot_world(uint8_t *ram, uint8_t *tiles, uint8_t *oam, uint8_t *palette,
                     const uint8_t *rom, uint16_t raw_dips)
{
    if (!ram || !tiles || !oam || !palette || !rom || !(raw_dips & 0x80)) return 0;
    if (!rom_test(rom) || !ram_test(ram, 65536, 1) || !ram_test(tiles, 65536, 2) ||
        !ram_test(oam, 2048, 2) || !ram_test(palette, 2048, 2)) return 0;
    memset(ram, 0, 65536);
    memset(tiles, 0, 65536);
    memset(oam, 0, 2048);
    memset(palette, 0, 2048);
    uint16_t dips = (uint16_t)~raw_dips;
    put(ram, 0x8b28, 0x2000);
    put(ram, 0x278, dips);
    put(ram, 0x27e, (dips >> 2) & 3);
    put(ram, 0x80, 0xa09);
    put(ram, 0x8b2c, 8);
    init_coinage(ram, rom, dips);
    put(ram, 0xb896, 0xffff);

    /* C439 loads the default score records and their position index. */
    memcpy(ram + 0x180, rom + word(rom, 0xc443), 0xc0);
    for (unsigned i = 0; i < 12; ++i) put(ram, 0x240 + 2 * i, 0x180 + 16 * i);

    /* Initial VBlank and sprite queue setup before 0A09 calls 1D52.
     * B892 counts the 084D wait-loop iterations at the World board clock. */
    put(ram, 0x25e, 0x8000);
    put(ram, 0x274, 1);
    put(ram, 0x276, 1);
    put(ram, 0x2a8, 0x80);
    put(ram, 0x2aa, 0x80);
    put(ram, 0xb892, 3814);
    memset(ram + 0x9c00, 0xff, 128);
    put(ram, 0x9e02, 210);
    put(ram, 0x9e04, 0x8c00);
    if (!world_title_boundary(ram, tiles, palette, rom)) return 0;
    /* A second VBlank arrives during the 3996 tile transfer before sound reset. */
    put(ram, 0x276, 2);
    put(ram, 0x2a8, 0);
    put(ram, 0x2aa, 0);
    return 1;
}
