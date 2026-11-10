#include <stdint.h>
#include "world_torpedo.h"

static uint16_t word(const uint8_t *p, uint16_t i)
{
    return p[i] | ((uint16_t)p[(uint16_t)(i + 1)] << 8);
}

static void put(uint8_t *p, uint16_t i, uint16_t value)
{
    p[i] = value;
    p[(uint16_t)(i + 1)] = value >> 8;
}

/* 1769. Take a slot from the free ring and insert it after the parent. */
uint16_t world_spawn_child(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t remaining = word(ram, 0x8b04) - 1;
    if (remaining & 0x8000) {
        ram[0x269] |= 0x80;
        return 0;
    }
    put(ram, 0x8b04, remaining);
    uint16_t cursor = word(ram, 0x8b00);
    uint16_t child = word(ram, 0x8900 + cursor);
    for (unsigned n = 0; n < 128; n++) ram[(uint16_t)(child + n)] = 0;
    put(ram, child, handler);
    put(ram, 0x8b00, (cursor + 2) & 0x1fe);
    uint16_t next = word(ram, parent + 4);
    put(ram, child + 4, next);
    put(ram, parent + 4, child);
    put(ram, child + 6, parent);
    put(ram, next + 6, child);
    put(ram, child + 8, word(ram, parent + 8) + 2);
    return child;
}

void world_queue_sound(uint8_t *ram, uint16_t command)
{
    unsigned tail = (word(ram, 0xa6d4) + 2) & 14;
    if (tail != word(ram, 0xa6d2)) {
        put(ram, 0xa6d4, tail);
        put(ram, 0xa692 + tail, command);
    }
}

/* 8210..82C7. Input edges already reflect this player's active cabinet. */
uint16_t world_fire_standard(uint8_t *ram, const uint8_t *rom, uint16_t sub, Torpedo *flight)
{
    if (word(ram, word(ram, sub + 0x70) + 0x10)) return 0;
    uint16_t cooldown = word(ram, sub + 0x4a);
    if (cooldown) {
        put(ram, sub + 0x4a, cooldown - 1);
        return 0;
    }
    uint16_t stock = word(ram, sub + 0x44);
    if (!(ram[0xb742] & 0x80) || !stock) return 0;

    world_queue_sound(ram, word(rom, 0x823f));
    uint16_t shot = world_spawn_child(ram, sub, word(rom, 0x824a));
    if (!shot) return 0;
    uint8_t facing = ram[sub + 0x29], owner = ram[sub + 0x0b];
    world_launch_torpedo(flight, rom, word(ram, sub + 0x21), word(ram, sub + 0x24),
                         facing, word(ram, sub + 0x56));
    put(ram, shot + 0x21, flight->x);
    put(ram, shot + 0x24, flight->y);
    ram[shot + 0x29] = facing;
    ram[shot + 0x0b] = owner;
    put(ram, 0xb8a2, word(ram, 0xb8a2) | (1u << (((owner & 3) - 1u) & 15)));
    ram[shot + 0x2d] = ram[sub + 0x2d] + 1;

    uint16_t flash = world_spawn_child(ram, sub, word(rom, 0x8294));
    if (!flash) flash = 0x8480; /* allocator's discard slot */
    ram[flash + 0x29] = facing;
    put(ram, flash + 0x31, word(rom, 0x9ba9 + 2 * (facing & 1)));
    ram[flash + 0x2d] = ram[sub + 0x2d];
    uint16_t alternating = word(ram, sub + 0x52);
    put(ram, flash + 0x52, alternating);
    put(ram, sub + 0x52, alternating ^ 1);
    put(ram, sub + 0x44, stock - 1);
    put(ram, sub + 0x4a, word(rom, 0x82c6));
    return shot;
}

/* World 82C8..83AD. The upward shot uses a different actor at the surface. */
static void fire_upward(uint8_t *ram, const uint8_t *rom, uint16_t sub, uint16_t config)
{
    if (word(ram, config + 0x14)) return;
    uint16_t delay = word(ram, sub + 0x4c);
    if (delay) {
        put(ram, sub + 0x4c, delay - 1);
        return;
    }
    uint16_t stock = word(ram, sub + 0x46);
    if (!(ram[0xb742] & 0x40) || !stock) return;
    uint16_t medium = word(ram, sub + 0x60);
    uint16_t shot = world_spawn_child(ram, sub, word(rom, medium == 3 ? 0x831b : 0x8300));
    if (!shot) return;
    world_queue_sound(ram, word(rom, medium == 3 ? 0x8328 : 0x830d));
    uint8_t facing = ram[sub + 0x29], owner = ram[sub + 0x0b];
    unsigned side = 2 * (facing & 1);
    ram[shot + 0x29] = facing;
    ram[shot + 0x0b] = owner;
    put(ram, shot + 0x21, word(ram, sub + 0x21) + word(rom, 0x9bad + side));
    put(ram, shot + 0x24, word(ram, sub + 0x24) + 4);
    put(ram, 0xb8a2, word(ram, 0xb8a2) | (1u << (((owner & 3) - 1u) & 15)));
    ram[shot + 0x2d] = ram[sub + 0x2d] + 1;
    put(ram, shot + 0x60, medium);
    uint16_t flash = world_spawn_child(ram, sub, word(rom, 0x8377));
    if (!flash) flash = 0x8480;
    ram[flash + 0x29] = facing;
    ram[flash + 0x2d] = ram[sub + 0x2d] - 1;
    put(ram, flash + 0x31, word(rom, 0x78c7 + side));
    put(ram, flash + 0x34, 4);
    put(ram, flash + 0x60, medium);
    put(ram, sub + 0x46, stock - 1);
    put(ram, sub + 0x4c, 8);
}

static void fire_downward(uint8_t *ram, const uint8_t *rom, uint16_t sub, uint16_t config)
{
    uint16_t delay = word(ram, sub + 0x4e);
    if (delay) {
        put(ram, sub + 0x4e, delay - 1);
        return;
    }
    unsigned mode = (2 * word(ram, config + 0x16)) & 6;
    uint16_t in_flight = word(ram, sub + 0x48);
    if (!(ram[0xb742] & 0x40) || (int16_t)word(rom, 0x795d + mode) <= (int16_t)in_flight)
        return;
    uint16_t shot = world_spawn_child(ram, sub, word(rom, 0x83dd));
    if (!shot) return;
    uint8_t owner = ram[sub + 0x0b];
    put(ram, shot + 0x21, word(ram, sub + 0x21));
    put(ram, shot + 0x24, word(ram, sub + 0x24) - 8);
    ram[shot + 0x0b] = owner;
    put(ram, 0xb8a2, word(ram, 0xb8a2) | (1u << (((owner & 3) - 1u) & 15)));
    uint16_t sequence = word(ram, sub + 0x54);
    put(ram, shot + 0x54, sequence & 3);
    put(ram, sub + 0x54, sequence + 1);
    put(ram, sub + 0x48, in_flight + 1);
    put(ram, sub + 0x4e, 8);
}

void world_player_weapons(uint8_t *ram, const uint8_t *rom, uint16_t sub)
{
    Torpedo shot;
    uint16_t config = word(ram, sub + 0x70);
    world_fire_standard(ram, rom, sub, &shot);
    fire_upward(ram, rom, sub, config);
    fire_downward(ram, rom, sub, config);
    if (!word(ram, 0x2a2)) return;
    for (unsigned i = 0; i < 2; i++) {
        if (!(ram[0xb742] & (0x20 >> i))) continue;
        uint16_t selection = word(ram, sub + 0x74 + 2*i) + 1;
        if (selection >= (i ? 7 : 10)) selection = 0;
        put(ram, sub + 0x74 + 2*i, selection);
        uint16_t table = (i ? 0x7939 : 0x7911) + 4 * selection;
        put(ram, config + 0x10 + 4*i, word(rom, table));
        put(ram, config + 0x12 + 4*i, word(rom, table + 2));
    }
}
