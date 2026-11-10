#include <stdint.h>

static uint16_t word(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static void put(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = v >> 8; }

/* World 43AF and 3CAC. Player coordinates are sampled before their next
 * update. Two-player averaging intentionally uses a wrapping 16-bit sum. */
int world_horizontal_camera(uint8_t *ram, const uint8_t *rom, uint16_t controller,
                            uint16_t p1_x, uint16_t p2_x)
{
    uint8_t *actor = ram + controller;
    unsigned players = ram[0x264] & 12;
    uint16_t origin = word(actor + 0x4c);
    unsigned positions[2] = {p1_x, p2_x};
    uint16_t distance = 0;
    int scrolling = 0;
    ram[0x26f] &= ~0x40;
    if (ram[0x26f] & 0x80) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(players & (4u << i))) continue;
        if (positions[i] < 0xa0) return 0;
        scrolling |= positions[i] >= origin;
        distance += (uint16_t)(positions[i] - origin);
    }
    if (!scrolling) return 0;
    ram[0x26f] |= 0x40;
    if (players == 12) distance >>= 1;
    if ((int16_t)distance >= 0xc0) distance = 0xbf;
    uint16_t index = (distance >> 2) & 0xfffe;
    uint16_t offset = 0x46f7 + index;
    uint16_t speed = word(ram + 0xa2) + (rom[offset] | (rom[(uint16_t)(offset+1)] << 8));
    put(ram + 0xa4, speed);
    uint16_t old = word(actor + 0x52);
    put(actor + 0x5a, old);
    unsigned fraction = actor[0x51] + (speed & 255);
    int pixels = speed >> 8;
    if (pixels >= 128) pixels -= 256;
    pixels += fraction >> 8;
    actor[0x51] = (uint8_t)fraction;
    put(actor + 0x52, old + pixels);
    put(ram + 0xb70a, old + pixels);
    put(ram + 0xb700, word(ram + 0xb700) + pixels);
    put(ram + 0xb71a, -pixels);
    put(ram + 0xb72a, -pixels);
    put(ram + 0xb726, -pixels);
    return 1;
}
