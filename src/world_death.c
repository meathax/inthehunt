#include <stdint.h>

uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);
void world_queue_sound(uint8_t *, uint16_t);
void world_award_score(uint8_t *, const uint8_t *, uint16_t, uint16_t);

static uint16_t word(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

static void put(uint8_t *p, uint16_t value)
{
    p[0] = value;
    p[1] = value >> 8;
}

/* 6B36 creates a radial explosion attached to its parent. */
void world_spawn_radial_explosion(uint8_t *ram, const uint8_t *rom, uint16_t enemy)
{
    uint16_t effect = world_spawn_child(ram, enemy, word(rom + 0x6b3a));
    if (!effect) effect = 0x8480;
    uint8_t *child = ram + effect;
    put(child + 0x4c, word(rom + 0x6b44));
    put(child + 0x4a, word(rom + 0x6b49));
    child[0x2c] = rom[0x6b4e];
    child[0x2d] = rom[0x6b52];
    put(child + 0x56, word(rom + 0x6b56));
    put(child + 0x58, word(rom + 0x6b5b));
    if (!(effect & 0x300)) put(child + 0x44, word(rom + 0x6b66));
}

void world_kind11_death(uint8_t *ram, const uint8_t *rom, uint16_t enemy, uint16_t attacker)
{
    world_award_score(ram, rom, enemy, attacker);
    world_spawn_radial_explosion(ram, rom, enemy);
    world_queue_sound(ram, word(rom + 0xb91f9));
    put(ram + enemy, word(rom + 0xb9203));
    put(ram + enemy + 2, word(rom + 0xb9208));
    ram[enemy + 0x0a] = rom[0xb920d];
    put(ram + enemy + 0x3c, word(rom + 0xb9211));
}
