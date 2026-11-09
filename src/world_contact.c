#include <stdint.h>

static uint16_t word(const uint8_t *segment, uint16_t offset)
{
    return segment[offset] | ((uint16_t)segment[(uint16_t)(offset + 1)] << 8);
}

static void store(uint8_t *ram, uint16_t offset, uint16_t value)
{
    ram[offset] = (uint8_t)value;
    ram[(uint16_t)(offset + 1)] = value >> 8;
}

static uint16_t center(const uint8_t *ram, const uint8_t *rom, uint16_t actor,
                       uint16_t shape, unsigned axis)
{
    uint16_t offset = word(rom, shape + 2 * axis);
    if (ram[(uint16_t)(actor + 0x29)] & (1u << axis))
        offset = -offset;
    return word(ram, actor + 0x21 + 3 * axis) + offset;
}

/* V33 D1C9. Both coordinates and the final unsigned extent comparison wrap. */
static int overlap_shapes(const uint8_t *ram, const uint8_t *rom, uint16_t a, uint16_t b, uint16_t sa, uint16_t sb)
{
    for (unsigned axis = 0; axis < 2; ++axis) {
        uint16_t distance = center(ram, rom, a, sa, axis) - center(ram, rom, b, sb, axis);
        if (distance & 0x8000) distance = -distance;
        unsigned field = 4 + axis * 2;
        uint16_t limit = word(rom, sa + field) + word(rom, sb + field) + 1;
        if (distance >= limit) return 0;
    }
    return 1;
}

static int overlap(const uint8_t *ram, const uint8_t *rom, uint16_t a, uint16_t b)
{
    return overlap_shapes(ram, rom, a, b, word(ram, a + 0x3c), word(ram, b + 0x3c));
}

int world_shape_overlap(const uint8_t *ram, const uint8_t *rom, uint16_t a, uint16_t b,
                        uint16_t shape_a, uint16_t shape_b)
{
    return overlap_shapes(ram, rom, a, b, shape_a, shape_b);
}

int world_ship_contact(const uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    for (unsigned slot = 0xc8; slot <= 0xce; slot += 2) {
        uint16_t ship = word(ram, slot);
        if (!ship) continue;
        for (int offset = 6; offset >= 0; offset -= 2) {
            uint16_t shape = word(rom + 0xb0000, 0x7f12 + offset);
            if (overlap_shapes(ram, rom, actor, ship, word(ram, actor + 0x3c), shape)) return 1;
        }
    }
    return 0;
}

static void damage(uint8_t *ram, uint16_t target, uint16_t source)
{
    uint16_t hp = word(ram, target + 0x26), amount = word(ram, source + 0x36);
    store(ram, target + 0x26, hp > amount ? hp - amount : 0);
}

/* D2B7. The first overlapping slot is consumed even for an immune target.
 * Its actor pointer is the original BX used subsequently for score ownership. */
static uint16_t projectile_contact_list(uint8_t *ram, const uint8_t *rom, uint16_t enemy,
                                        uint16_t list)
{
    unsigned end = word(ram, list);
    for (unsigned i = 0; i < end; i += 2) {
        uint16_t slot = list + 4 + i;
        uint16_t shot = word(ram, slot);
        if (!shot || !overlap(ram, rom, enemy, shot)) continue;
        store(ram, slot, 0);
        uint8_t flags = ram[(uint16_t)(enemy + 0x0d)];
        if (!(flags & 2)) {
            damage(ram, shot, enemy);
            if (!(flags & 1)) {
                damage(ram, enemy, shot);
                uint16_t flash = enemy + 0x19;
                ram[flash] = (ram[flash] & 1) + 10;
            }
        }
        return shot;
    }
    return 0;
}

uint16_t world_projectile_contact(uint8_t *ram, const uint8_t *rom, uint16_t enemy)
{
    return projectile_contact_list(ram, rom, enemy, 0xa6e2);
}

/* D371 uses the second list with the same collision and damage rules. */
uint16_t world_surface_contact(uint8_t *ram, const uint8_t *rom, uint16_t enemy)
{
    return projectile_contact_list(ram, rom, enemy, 0xaee6);
}

/* D237 checks the two player slots in cabinet order and stops at first contact. */
uint16_t world_player_contact(uint8_t *ram, const uint8_t *rom, uint16_t enemy)
{
    for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
        uint16_t player = word(ram, slot);
        if (word(ram, player + 0x3c) == 0xffff || !overlap(ram, rom, enemy, player))
            continue;
        uint8_t flags = ram[enemy + 0x0d];
        if (!(flags & 1))
            store(ram, player + 0x26, 0);
        if (!flags) {
            damage(ram, enemy, player);
            ram[enemy + 0x19] = (ram[enemy + 0x19] & 1) + 10;
        }
        return player;
    }
    return 0;
}
