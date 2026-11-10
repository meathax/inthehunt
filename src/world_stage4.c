/* Recovered World actor handlers for stage 4 (stage index 3).
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

/* 1769 with a code segment: the dummy slot 8480 keeps its segment word. */
static uint16_t spawn_far(uint8_t *ram, uint16_t parent, uint16_t segment, uint16_t handler)
{
    uint16_t child = world_spawn_child(ram, parent, handler);
    if (!child) return 0x8480;
    write_word(ram, child + 2, segment);
    return child;
}

/* 37EB: CH columns of CL blocks each, drawn downwards, columns 16 bytes apart. */
static void draw_block_columns(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t base,
                               uint16_t dest, uint16_t source, uint16_t size)
{
    unsigned rows = size & 255, columns = size >> 8;
    uint16_t origin = dest;
    write_word(ram, 0xb89a, size);
    write_word(ram, 0xb89c, origin);
    for (unsigned column = 0; column < columns; ++column) {
        dest = origin;
        for (unsigned row = 0; row < rows; ++row) {
            world_draw_tile_block(tiles, rom, base, dest, read_word(rom + 0xa0000, source));
            source += 2;
            dest = (dest + 0x400) & 0x3fff;
        }
        if (column + 1 < columns) {
            origin = (origin & 0xff00) | ((origin + 16) & 255);
            write_word(ram, 0xb89c, origin);
        }
    }
}

/* 68DF results: BX is the tile offset, CX the material, DX its bit. */
static uint16_t terrain_cell(const uint8_t *ram, uint16_t x, uint16_t y)
{
    return world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
}

static unsigned terrain_material(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y)
{
    return tiles[0x4000 + terrain_cell(ram, x, y) + 3] >> 3;
}

/* Lower playfield edge used by the stage's actors. */
static int below_playfield(const uint8_t *ram, uint16_t actor)
{
    uint16_t bottom = read_word(ram, 0x26e) & 2 ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
    return bottom < read_word(ram, actor + 0x24);
}

static int above_playfield(const uint8_t *ram, uint16_t actor)
{
    uint16_t top = read_word(ram, 0x26e) & 2 ? 0x68 : 0x10 + read_word(ram, 0xb732);
    return (int16_t)read_word(ram, actor + 0x24) < (int16_t)top;
}

/* 6B2E: sound 5D, then the radial explosion at 6B36. */
static void loud_explosion(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    world_spawn_radial_explosion(ram, rom, actor);
}

/* 7565 table: wake conditions of the wall creatures. Returns the carry. */
static int creature_asleep(const uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    uint16_t camera = read_word(ram, 0xb700);
    uint16_t test = read_word(bank, 0x7565 + ((read_word(ram, actor + 0x1e) & 0x1c) >> 1));
    if (test == 0x7a53) return camera < 0x6e0;
    if (test == 0x7a5a) return camera < 0xe00;
    if (test == 0x7a61) return !(read_word(ram, 0xb8c2) & (1u << ((read_word(ram, actor + 0x1e) >> 2) & 1)));
    if (test == 0x7a76) return camera < 0x320 || read_word(ram, actor + 0x24) < 0xd8;
    return -1;
}

/* ---- Boss: the three-headed dragon (bank 7000, 5AA2..6D23). ---- */

/* The actor n levels up the spawn chain (0x8500 holds the last actor per depth). */
static uint16_t ancestor(const uint8_t *ram, uint16_t actor, unsigned n)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2 * n));
}

/* 657D: velocity from the direction byte [28] and the speed [2E]. */
static void set_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    int16_t speed = read_word(ram, actor + 0x2e);
    write_word(ram, actor + 0x38, (uint16_t)(((int32_t)(int16_t)read_word(rom + 0xa0000, 0x9e12 + angle) * speed) >> 8));
    write_word(ram, actor + 0x3a, (uint16_t)(((int32_t)(int16_t)read_word(rom + 0xa0000, 0x9d92 + angle) * speed) >> 8));
}

/* 3665: second sound ring. */
static void queue_sound_b(uint8_t *ram, uint16_t command)
{
    uint16_t next = (read_word(ram, 0xa6d8) + 2) & 14;
    if (next != read_word(ram, 0xa6d6)) {
        write_word(ram, 0xa6d8, next);
        write_word(ram, 0xa6a2 + next, command);
    }
}

/* 6BCA: sit at a fixed offset from the parent and copy its direction. */
static uint16_t follow_parent(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = ancestor(ram, actor, 1);
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
    ram[actor + 0x28] = ram[parent + 0x28];
    return parent;
}

/* 6BF1: neck segment; the parent's direction arrives eight passes late. */
static uint16_t follow_chain(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t parent = ancestor(ram, actor, 1);
    write_word(ram, actor + 0x2e, read_word(ram, parent + 0x2e));
    set_velocity(ram, rom, actor);
    write_word(ram, actor + 0x31, (uint16_t)(int8_t)ram[actor + 0x39]);
    write_word(ram, actor + 0x34, (uint16_t)(int8_t)ram[actor + 0x3b]);
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
    unsigned slot = read_word(ram, actor + 0x66) & 7;
    ram[actor + 0x28] = ram[actor + 0x68 + slot];
    ram[actor + 0x68 + slot] = ram[parent + 0x28];
    write_word(ram, actor + 0x66, read_word(ram, actor + 0x66) + 1);
    return parent;
}

static void body_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = 1;
    world_projectile_contact(ram, rom, actor);
}

/* A shot on a weak point drains the boss body `body`. Returns 1 when it dies. */
static int drain_boss(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t body, uint16_t shot)
{
    uint16_t damage = read_word(ram, shot + 0x36), health = read_word(ram, body + 0x26);
    if (!health) return 0;
    write_word(ram, body + 0x26, health - damage);
    if (health > damage) return 0;
    if (!(read_word(ram, 0x264) & 3)) {
        write_word(ram, body + 0x26, 0x100);
        return 0;
    }
    write_word(ram, body + 0x26, 0);
    uint16_t record = ((read_word(ram, shot + 0x0b) & 2) << 5) + 0x124;
    write_word(ram, record, read_word(ram, record) | 8);
    write_word(ram, actor + 0x0e, 0x46);
    write_word(ram, 0x262, read_word(ram, 0x262) | 0x80);
    world_award_score(ram, rom, actor, shot);
    return 1;
}

static void palette_job(uint8_t *ram, uint16_t offset, uint16_t palette, uint16_t speed)
{
    uint16_t job = 0x9e06 + offset;
    write_word(ram, job, palette);
    write_word(ram, job + 2, 0x80);
    write_word(ram, job + 4, speed);
    ram[(uint16_t)(job + 6)] = 0x20;
    ram[(uint16_t)(job + 7)] = 0x10;
}

static uint16_t spawn_neck(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = spawn_far(ram, actor, 0x7000, handler);
    write_word(ram, child + 0x14, read_word(ram, actor + 0x14));
    ram[child + 0x2c] = ram[actor + 0x14];
    ram[child + 0x16] = ram[actor + 0x16];
    return child;
}

static int boss_handlers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x70000;
    uint16_t flags = read_word(ram, 0x262);
    uint16_t profile = 0x5a40 + read_word(bank, 0x5a20 + read_word(ram, 0x88));
    switch (address) {
    case 0x75aa2: {
        static const uint16_t parts[] = {0x601d, 0x5ec0, 0x5edb, 0x5e14, 0x5f3d};
        for (unsigned id = 0xa5; id <= 0xa8; ++id) world_acquire_resource(ram, actor, id);
        write_word(ram, actor + 0x0e, 0x46);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, 0xf833);
        write_word(ram, actor + 0x2a, 0x3f97);
        ram[actor + 0x0c] = 8;
        uint16_t health = read_word(bank, profile);
        write_word(ram, actor + 0x26, health);
        write_word(ram, actor + 0x36, health);
        write_word(ram, actor + 0x72, health);
        write_word(ram, actor + 0x74, health >> 2);
        for (unsigned i = 0; i < 5; ++i) {
            uint16_t child = spawn_far(ram, actor, 0x7000, parts[i]);
            ram[child + 0x2c] = ram[actor + 0x2c];
        }
        for (unsigned i = 0; i < 3; ++i) {
            uint16_t child = spawn_neck(ram, actor, 0x6099);
            write_word(ram, child + 0x1e, i);
        }
        write_word(ram, actor + 0x52, 7);
        queue_sound_b(ram, 0x11);
        write_word(ram, actor + 0x42, 0x100);
        write_word(ram, 0xb8c2, 3);
        write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) | 8);
        write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) | 1);
        write_word(ram, 0xb726, 0);
        write_word(ram, actor, 0x5bd5);
        break;
    }
    case 0x75bd5:
        if (decrement(ram, actor + 0x42)) break;
        write_word(ram, actor + 0x38, 0x100);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor + 0x46, 0x5a);
        write_word(ram, actor + 0x5c, 3);
        write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 1);
        write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) | 8);
        write_word(ram, 0xb8c2, 0);
        write_word(ram, actor, 0x5c0b);
        break;
    case 0x75c0b: {
        if (!read_word(ram, actor + 0x26)) {
            write_word(ram, 0x262, flags | 0x80);
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) | 4);
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
            write_word(ram, actor + 0x42, 0x80);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x5d89);
            return boss_handlers(ram, rom, tiles, actor, 0x75d89);
        }
        if ((uint16_t)(read_word(ram, actor + 0x72) - read_word(ram, actor + 0x26)) >= read_word(ram, actor + 0x74)) {
            write_word(ram, actor + 0x72, read_word(ram, actor + 0x72) - read_word(ram, actor + 0x74));
            queue_palette(ram, ram[actor + 0x14] & 0x3f, read_word(bank, 0x5a9a + read_word(ram, actor + 0x40)), 5);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        uint16_t target = world_enemy_target(ram, actor);
        if (target != 0x8480) {
            uint16_t y = read_word(ram, actor + 0x34);
            int move = 0;
            if (y < read_word(ram, target + 0x24)) {
                if ((int16_t)y < 0x110) { write_word(ram, actor + 0x3a, 0x80); move = 1; }
            } else if ((int16_t)y > 0xe0) { write_word(ram, actor + 0x3a, 0xff80); move = 1; }
            if (move) move_actor_axis(ram, actor + 0x33, read_word(ram, actor + 0x3a));
        }
        move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
        {   /* 675C: bob around the 24-bit base Y by the horizontal phase. */
            unsigned phase = (uint8_t)(read_word(ram, actor + 0x21) - read_word(ram, actor + 0x31));
            int32_t wave = (int32_t)(int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * phase) * 16;
            uint32_t low = (uint32_t)(uint16_t)wave + read_word(ram, actor + 0x33);
            write_word(ram, actor + 0x23, (uint16_t)low);
            ram[actor + 0x25] = (uint8_t)((uint32_t)wave >> 16) + ram[actor + 0x35] + (low >> 16);
        }
        queue_sprite(ram, rom, actor, 0);
        uint16_t mode = 0, heads = read_word(ram, 0xb8c2);
        if (read_word(ram, actor + 0x46)) {
            if (!decrement(ram, actor + 0x46)) {
                if (decrement(ram, actor + 0x5c)) {
                    write_word(ram, actor + 0x46, 0x5a);
                    mode |= 1;
                } else {
                    mode |= 2 | 4;
                    write_word(ram, actor + 0x48, 0x50);
                    write_word(ram, actor + 0x5e, 6);
                    heads |= 1u << (read_word(ram, actor + 0x5e) & 1);
                }
            }
        } else if (!decrement(ram, actor + 0x48)) {
            heads &= ~(1u << (read_word(ram, actor + 0x5e) & 1));
            if (decrement(ram, actor + 0x5e)) {
                write_word(ram, actor + 0x48, 0x50);
                mode |= 4;
                heads |= 1u << (read_word(ram, actor + 0x5e) & 1);
            } else {
                mode |= 2 | 1;
                write_word(ram, actor + 0x46, 0x5a);
                write_word(ram, actor + 0x5c, 3);
            }
        }
        write_word(ram, actor + 0x54, mode);
        write_word(ram, 0xb8c2, heads);
        if (!(read_word(ram, actor + 0x52) & 7)) {
            begin_linear_script(ram, rom, actor, 0x5812);
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) | 2);
            write_word(ram, actor, 0x5d5b);
        }
        body_contacts(ram, rom, actor);
        break;
    }
    case 0x75d5b: {
        int step = step_flight_script(ram, rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) & ~2u);
            write_word(ram, actor + 0x52, 7);
            write_word(ram, actor, 0x5c0b);
        }
        queue_sprite(ram, rom, actor, 0);
        body_contacts(ram, rom, actor);
        break;
    }
    case 0x75d89:
    case 0x75da7:
    case 0x75dbd:
        if (address == 0x75d89) {
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x3a, 0xff00);
                write_word(ram, 0x26e, read_word(ram, 0x26e) & ~0x8000u);
                write_word(ram, 0x262, read_word(ram, 0x262) | 0x4000);
                write_word(ram, actor, 0x5da7);
            }
        } else {
            int falling = address == 0x75da7;
            if (falling) {
                move_actor_axis(ram, actor + 0x33, read_word(ram, actor + 0x3a));
                if ((int16_t)read_word(ram, actor + 0x24) <= 0xc0) {
                    write_word(ram, actor + 0x42, 0x80);
                    write_word(ram, actor, 0x5dbd);
                    falling = 0;
                }
            }
            if (!falling && !decrement(ram, actor + 0x42))
                write_word(ram, 0x262, read_word(ram, 0x262) | 0x8000);
        }
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x31) + (world_random_byte(ram) & 2) - 1);
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + (world_random_byte(ram) & 2) - 1);
        queue_sprite(ram, rom, actor, 0);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, rom, actor);
        if (!decrement(ram, actor + 0x44)) {
            /* 7036 */
            uint16_t child = effect_slot(ram, actor, 0x7056);
            write_word(ram, child + 0x60, 0x5880);
            write_word(ram, child + 0x62, 0x7000);
            write_word(ram, child + 0x4c, 4);
            write_word(ram, child + 0x42, 1);
            write_word(ram, actor + 0x44, 0x40);
        }
        break;

    /* Body parts riding on the boss. */
    case 0x75e14:
    case 0x75f3d:
        if (address == 0x75e14) {
            ram[actor + 0x2d] = 0x30;
            write_word(ram, actor + 0x2a, 0x3fe1);
            write_word(ram, actor + 0x36, 0x7f00);
        } else {
            write_word(ram, actor + 0x0e, 0x46);
            ram[actor + 0x2d] = 0x32;
            write_word(ram, actor + 0x3c, 0xf85b);
            write_word(ram, actor + 0x2a, 0x4025);
        }
        address += 0x13 + 5 * (address == 0x75f3d);
        write_word(ram, actor, (uint16_t)address);
        /* fall through */
    case 0x75e27:
    case 0x75f55: {
        uint16_t parent = follow_parent(ram, actor);
        if (read_word(ram, parent + 0x50) & 1) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x42, 0x80);
            write_word(ram, actor, (uint16_t)address + 0x24);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0x75e4b:
    case 0x75f79:
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank, (address == 0x75e4b ? 0x58c4 : 0x58c8) + (read_word(ram, actor + 0x40) & 2)));
            write_word(ram, actor + 0x44, 0x0c);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        address += 0x27;
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, (uint16_t)address);
        /* fall through */
    case 0x75e72:
    case 0x75fa0:
        follow_parent(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        if (flags & 0x80) { write_word(ram, actor, 0x5eb7); break; }
        if (address == 0x75e72) {
            write_word(ram, actor + 0x3c, 0xf83b);
            body_contacts(ram, rom, actor);
            write_word(ram, actor + 0x3c, 0xf843);
            body_contacts(ram, rom, actor);
        } else {
            ram[actor + 0x0d] = 2;
            world_player_contact(ram, rom, actor);
            ram[actor + 0x0d] = 1;
            uint16_t shot = world_projectile_contact(ram, rom, actor);
            if (shot && drain_boss(ram, rom, actor, ancestor(ram, actor, 1), shot))
                write_word(ram, actor, 0x5eb7);
        }
        break;
    case 0x75eb7:
        follow_parent(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x75ec0:
    case 0x75edb:
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, address == 0x75ec0 ? 0xf84b : 0xf853);
        write_word(ram, actor + 0x2a, address == 0x75ec0 ? 0x4059 : 0x408b);
        write_word(ram, actor + 0x36, 0x7f00);
        write_word(ram, actor, 0x5ef3);
        /* fall through */
    case 0x75ef3:
        if (read_word(ram, follow_parent(ram, actor) + 0x50) & 1) write_word(ram, actor, 0x5f08);
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x75f08:
        write_word(ram, actor + 0x31, (read_word(ram, 0x274) & 8) >> 3);
        follow_parent(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        if (flags & 0x80) { write_word(ram, actor, 0x5eb7); break; }
        body_contacts(ram, rom, actor);
        break;
    case 0x7601d:
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x2a, 0x9777);
        write_word(ram, actor, 0x602b);
        /* fall through */
    case 0x7602b:
    case 0x7605c: {
        uint16_t parent = ancestor(ram, actor, 1);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (address != 0x7605c) {
            if (read_word(ram, parent + 0x50) & 1) {
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x605c);
            }
        } else if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank, 0x5a1c + (read_word(ram, actor + 0x40) & 2)));
            write_word(ram, actor + 0x44, 0x14);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }

    /* Neck base: swings the neck out, holds it, pulls it back. */
    case 0x76099: {
        uint16_t child = spawn_neck(ram, actor, 0x62ef);
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        write_word(ram, child + 0x5a, 7);
        write_word(ram, actor + 0x56, read_word(bank, profile + 4));
        write_word(ram, actor + 0x58, read_word(bank, profile + 4));
        unsigned neck = (read_word(ram, actor + 0x1e) & 3) << 2;
        write_word(ram, actor + 0x31, read_word(bank, 0x58cc + neck));
        write_word(ram, actor + 0x34, read_word(bank, 0x58ce + neck));
        write_word(ram, actor, 0x60f4);
    }
        /* fall through */
    case 0x760f4: {
        if (!(read_word(ram, ancestor(ram, actor, 1) + 0x52) & 8)) break;
        unsigned neck = (read_word(ram, actor + 0x1e) & 3) << 1;
        world_queue_sound(ram, 0x75);
        write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) | 8);
        write_word(ram, actor + 0x38, 0xffa7);
        write_word(ram, actor + 0x42, 0xa0);
        write_word(ram, actor + 0x60, read_word(bank, 0x58d8 + neck));
        write_word(ram, actor + 0x62, read_word(bank, 0x58de + neck));
        write_word(ram, actor + 0x64, read_word(bank, 0x58e4 + neck));
        write_word(ram, actor, 0x6147);
        break;
    }
    case 0x76147:
    case 0x7615d:
    case 0x761e5:
    case 0x76284:
    case 0x76296:
    case 0x762a4:
    case 0x762da: {
        uint16_t parent = ancestor(ram, actor, 1);
        int retract_check = 0;
        if (address == 0x76147) {
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            write_word(ram, actor + 0x2e, read_word(ram, actor + 0x2e) + 0x10);
            if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x615d);
        } else if (address == 0x761e5) {
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            uint16_t speed = read_word(ram, actor + 0x2e);
            write_word(ram, actor + 0x2e, speed < 0x40 ? 0 : speed - 0x40);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, parent + 0x52, read_word(ram, parent + 0x52) & ~(1u << (read_word(ram, actor + 0x1e) & 15)));
                write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) | 0x20);
                write_word(ram, actor, 0x621b);
            }
        } else if (address == 0x7615d) {
            int low = !read_word(ram, actor + 0x1e);
            uint16_t mode = read_word(ram, parent + 0x54) & (low ? 3 : 6);
            write_word(ram, actor + 0x54, mode);
            if (mode & 5) {
                write_word(ram, actor + 0x42, low ? 0x21 : 0x28);
                write_word(ram, actor, 0x6284);
            }
            retract_check = 1;
        } else if (address == 0x762da) {
            write_word(ram, actor + 0x2e, read_word(ram, actor + 0x2e) + 0x20);
            if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x615d);
            retract_check = 1;
        } else {
            if (address == 0x76284) {
                if (!decrement(ram, actor + 0x42)) {
                    write_word(ram, actor + 0x42, 0x10);
                    write_word(ram, actor, 0x6296);
                }
            } else if (address == 0x76296) {
                write_word(ram, actor + 0x2e, read_word(ram, actor + 0x2e) - 0x20);
                if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x62a4);
            }
            uint16_t mode = read_word(ram, parent + 0x54) & (read_word(ram, actor + 0x1e) ? 6 : 3);
            write_word(ram, actor + 0x54, mode);
            if (mode & 2) {
                write_word(ram, actor + 0x42, 0x10);
                write_word(ram, actor, 0x62da);
            }
            retract_check = 1;
        }
        if (retract_check && (read_word(ram, actor + 0x52) & 0x10)) {
            write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) & ~8u);
            write_word(ram, actor + 0x38, 0x164);
            write_word(ram, actor + 0x42, 0x28);
            write_word(ram, actor, 0x61e5);
        }
        follow_parent(ram, actor);
        write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) + read_word(ram, actor + 0x62));
        ram[actor + 0x28] = ram[actor + 0x61];
        if (!decrement(ram, actor + 0x64)) {
            write_word(ram, actor + 0x62, -read_word(ram, actor + 0x62));
            write_word(ram, actor + 0x64, read_word(bank, 0x58ea + ((read_word(ram, actor + 0x1e) & 3) << 1)));
        }
        if (flags & 0x80) write_word(ram, actor, 0x62eb);
        break;
    }
    case 0x7621b: {
        uint16_t parent = ancestor(ram, actor, 1);
        if (read_word(ram, parent + 0x50) & 2) {
            write_word(ram, actor + 0x56, 1);
        } else if (!decrement(ram, actor + 0x56)) {
            write_word(ram, actor + 0x56, read_word(ram, actor + 0x58));
            write_word(ram, parent + 0x52, read_word(ram, parent + 0x52) | (1u << (read_word(ram, actor + 0x1e) & 15)));
            write_word(ram, actor + 0x52, (read_word(ram, actor + 0x52) | 8) & ~0x30u);
            write_word(ram, actor + 0x38, 0xffa7);
            write_word(ram, actor + 0x42, 0xa0);
            write_word(ram, actor, 0x6147);
        }
        if (flags & 0x80) write_word(ram, actor, 0x62eb);
        break;
    }
    case 0x762eb:
        follow_parent(ram, actor);
        break;

    /* Neck segments. */
    case 0x762ef: {
        uint16_t left = decrement(ram, actor + 0x5a);
        uint16_t child = spawn_neck(ram, actor, left ? 0x62ef : 0x64ca);
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        if (left) write_word(ram, child + 0x5a, left);
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x2a, 0x3b57);
        write_word(ram, actor + 0x3c, 0xf863);
        write_word(ram, actor + 0x36, 0x7f00);
        memset(ram + actor + 0x68, 0x80, 8);
        write_word(ram, actor, 0x6375);
    }
        /* fall through */
    case 0x76375:
        if (flags & 0x80) { finish_animation(ram, actor); break; }
        if (read_word(ram, ancestor(ram, actor, 1) + 0x52) & 8) {
            write_word(ram, actor + 0x52, 8);
            write_word(ram, actor + 0x54, 0);
            write_word(ram, actor, 0x63bb);
        }
        break;
    case 0x763bb:
    case 0x7641e: {
        uint16_t parent = follow_chain(ram, rom, actor);
        int retracting = (read_word(ram, parent + 0x52) & 0x20) != 0;
        if (retracting) write_word(ram, actor + 0x52, (read_word(ram, actor + 0x52) | 0x20) & ~8u);
        if (address == 0x763bb) {
            if (retracting) write_word(ram, actor, 0x6375);
            else {
                write_word(ram, actor + 0x54, read_word(ram, parent + 0x54));
                if (read_word(ram, parent + 0x54) & 1) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x44, (read_word(ram, actor + 0x5a) << 2) + 1);
                    write_word(ram, actor, 0x641e);
                }
            }
        } else {
            write_word(ram, actor + 0x54, read_word(ram, parent + 0x54));
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x58f0 + frame);
                if (sprite == 0xffff) write_word(ram, actor, 0x63bb);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x590a + frame));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
        }
        queue_sprite(ram, rom, actor, 0);
        if (flags & 0x80) { write_word(ram, actor, 0x648a); break; }
        body_contacts(ram, rom, actor);
        break;
    }
    /* Death: parts shake loose, drop and blink out. */
    case 0x7648a:
    case 0x76930:
        write_word(ram, actor + 0x42, 0x80);
        write_word(ram, actor + 0x2e, address == 0x7648a ? 0x2c0 : 0x340);
        ram[actor + 0x28] = address == 0x7648a ? (world_random_byte(ram) & 0x1c) + 0x50
                                               : (world_random_byte(ram) & 7) + 0x70;
        set_velocity(ram, rom, actor);
        address += 0x22;
        write_word(ram, actor, (uint16_t)address);
        /* fall through */
    case 0x764ac:
    case 0x76952:
        follow_parent(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x2a, address == 0x764ac ? 0x3c35 : 0x3c03);
            ram[actor + 0x2c] = ram[actor + 0x16];
            write_word(ram, actor, 0x6970);
        }
        break;
    case 0x76970:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 0x20);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, rom, actor, 0);
        if ((1u << (terrain_material(ram, tiles, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24)) & 15)) & 0xff02) {
            write_word(ram, actor + 0x42, 0x40);
            write_word(ram, actor, 0x69a4);
        }
        break;
    case 0x769a4:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x42) >= 0x20 || (read_word(ram, actor + 0x42) & 1))
            queue_sprite(ram, rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;

    /* Heads. */
    case 0x764ca:
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x3c, 0xf86b);
        write_word(ram, actor + 0x4e, read_word(bank, profile + 6));
        write_word(ram, actor + 0x4c, read_word(bank, profile + 8));
        write_word(ram, actor, 0x64f3);
        /* fall through */
    case 0x764f3:
        if (flags & 0x80) { finish_animation(ram, actor); break; }
        if (read_word(ram, ancestor(ram, actor, 1) + 0x52) & 8) {
            write_word(ram, actor + 0x54, 0);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x26, read_word(bank, profile + 2));
            write_word(ram, actor + 0x36, read_word(bank, profile + 2));
            write_word(ram, actor, 0x6550);
        }
        break;
    case 0x766cf:
        if (!read_word(ram, actor + 0x1e)) {
            world_queue_sound(ram, 0x75);
            spawn_far(ram, actor, 0x7000, 0x681d);
        }
        write_word(ram, actor, 0x66ee);
        address = 0x766ee;
        /* fall through */
    case 0x76550:
    case 0x766ee:
    case 0x76736:
    case 0x767a3: {
        uint16_t parent = follow_parent(ram, actor);
        uint16_t frame = read_word(ram, actor + 0x40);
        int fire = 0;
        if (address == 0x76550) {
            uint16_t mode = read_word(ram, parent + 0x54);
            if (mode & 5) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, mode & 1 ? 0x21 : 0x28);
                write_word(ram, actor, 0x66cf);
            } else if (!decrement(ram, actor + 0x44)) {
                write_word(ram, actor + 0x2a, read_word(bank, 0x5922 + (frame & 6)));
                write_word(ram, actor + 0x44, read_word(bank, 0x592a + (frame & 6)));
                write_word(ram, actor + 0x40, frame + 2);
            }
        } else if (address == 0x766ee) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t sprite = read_word(bank, 0x5932 + frame);
                if (sprite == 0xffff) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x4a, 1);
                    write_word(ram, actor + 0x46, 0);
                    write_word(ram, actor, 0x6736);
                } else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x593c + frame));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
        } else if (address == 0x76736) {
            if (read_word(ram, parent + 0x54) & 2) {
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 2);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x67a3);
            } else if (!decrement(ram, actor + 0x44)) {
                write_word(ram, actor + 0x2a, read_word(bank, 0x5952 + frame));
                write_word(ram, actor + 0x44, read_word(bank, 0x5958 + frame));
                write_word(ram, actor + 0x40, frame + 2 >= 6 ? 0 : frame + 2);
            }
            fire = 1;
        } else if (!decrement(ram, actor + 0x44)) {
            uint16_t sprite = read_word(bank, 0x5944 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x54, 0);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x6550);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, 0x594c + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, rom, actor, 1);
        if (fire && !read_word(ram, 0x286) && !decrement(ram, actor + 0x4a)) {
            uint16_t child;
            if (!read_word(ram, actor + 0x1e)) {
                /* 6C41: fire breath. */
                world_queue_sound(ram, 0x4c);
                write_word(ram, actor + 0x4a, 0x0c);
                child = spawn_far(ram, actor, 0x7000, 0x6a77);
                ram[child + 0x2d] = 5;
                ram[child + 0x2c] = ram[actor + 0x15];
                write_word(ram, child + 0x46, read_word(ram, actor + 0x46));
                write_word(ram, actor + 0x46, read_word(ram, actor + 0x46) - 0x20);
                write_word(ram, child + 0x44, 1);
                write_word(ram, child + 0x31, 0xffe0);
                write_word(ram, child + 0x34, 0x0a);
                child = spawn_far(ram, actor, 0x7000, 0x6b07);
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21) - 0x20);
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + 0x0a);
                ram[child + 0x2d] = ram[actor + 0x2d];
                ram[child + 0x2c] = ram[actor + 0x15];
                write_word(ram, child + 0x44, 1);
                write_word(ram, child + 0x3a, 0xff00);
            } else {
                /* 6CCE: fireball. */
                world_queue_sound(ram, 0x4a);
                write_word(ram, actor + 0x4a, read_word(ram, actor + 0x4c));
                child = spawn_far(ram, actor, 0x7000, 0x6b7b);
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21) - 0x48);
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + 0x12);
                ram[child + 0x2c] = ram[actor + 0x15];
                write_word(ram, child + 0x38, read_word(ram, actor + 0x4e));
            }
            child = spawn_far(ram, actor, 0x7000, 0x6b47);
            ram[child + 0x2d] = ram[actor + 0x2d] + 1;
            ram[child + 0x2c] = ram[actor + 0x15];
            write_word(ram, child + 0x42, 0x0c);
        }
        /* 65AA: the head is the boss's weak point while the body allows it. */
        if (flags & 0x80) {
            write_word(ram, actor + 0x2a, 0x3a33);
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 2);
            write_word(ram, actor, 0x6930);
            break;
        }
        uint16_t body = ancestor(ram, actor, 9);
        if (!(read_word(ram, body + 0x50) & 8)) break;
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, rom, actor);
        if (ram[actor + 0x19] < 8) {
            ram[actor + 0x0d] = 0;
            uint16_t shot = world_projectile_contact(ram, rom, actor);
            if (shot) {
                if (!ram[actor + 0x19]) world_queue_sound(ram, 0x68);
                if (drain_boss(ram, rom, actor, body, shot)) {
                    write_word(ram, actor + 0x2a, 0x3a33);
                    write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 2);
                    write_word(ram, actor, 0x6930);
                    break;
                }
            }
        }
        if (!read_word(ram, actor + 0x26)) {
            uint16_t base = ancestor(ram, actor, 8);
            write_word(ram, base + 0x52, read_word(ram, base + 0x52) | 0x10);
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 2);
            write_word(ram, actor, 0x668d);
        }
        break;
    }
    case 0x7668d: {
        if (read_word(ram, follow_parent(ram, actor) + 0x52) & 0x20) write_word(ram, actor, 0x64f3);
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, 0x5922 + (frame & 6)));
            write_word(ram, actor + 0x44, read_word(bank, 0x592a + (frame & 6)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 1);
        if (flags & 0x80) write_word(ram, actor, 0x6930);
        break;
    }

    /* Palette flash while a head charges. */
    case 0x767e6: {
        uint16_t child = spawn_far(ram, actor, 0x7000, 0x681d);
        write_word(ram, child + 0x1e, 4);
        write_word(ram, actor, 0x67fb);
    }
        /* fall through */
    case 0x767fb:
        if (read_word(ram, 0xb700) >= read_word(ram, actor + 0x1e)) {
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) | 2);
            finish_animation(ram, actor);
        }
        break;
    case 0x7681d:
        for (int i = 1; i >= 0; --i)
            palette_job(ram, read_word(bank, 0x59f8 + 2 * i), read_word(bank, 0x59fc + 2 * i), 6);
        write_word(ram, actor + 0x42, 0x40);
        write_word(ram, actor, 0x685b);
        break;
    case 0x7685b:
    case 0x76878: {
        int restore = (flags & 0x80) != 0;
        if (!restore && address == 0x7685b) {
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x40, 2);
                write_word(ram, actor + 0x42, 6);
                write_word(ram, actor, 0x6878);
            }
            break;
        }
        if (!restore && (read_word(ram, ancestor(ram, actor, 1) + 0x54) & 2)) restore = 1;
        if (restore) {
            for (int i = 1; i >= 0; --i)
                palette_job(ram, read_word(bank, 0x59f8 + 2 * i),
                            read_word(bank, (uint16_t)(0x5a00 + 2 * i + read_word(ram, actor + 0x1e))), 6);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x42)) {
            unsigned phase = read_word(ram, actor + 0x40) & 2;
            write_word(ram, actor + 0x42, read_word(bank, 0x5a08 + phase));
            for (int i = 1; i >= 0; --i)
                palette_job(ram, read_word(bank, 0x59f8 + 2 * i), read_word(bank, 0x59fc + 2 * i + 2 * phase),
                            read_word(bank, 0x5a0c + phase));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        break;
    }

    /* Projectiles and effects of the heads. */
    case 0x769d4:
        world_acquire_resource(ram, actor, 0xa6);
        ram[actor + 0x28] = bank[0x5a90 + (ram[actor + 0x29] & 1)];
        write_word(ram, actor + 0x2e, 0x400);
        set_velocity(ram, rom, actor);
        write_word(ram, actor + 0x42, 0x10);
        write_word(ram, actor, 0x6a00);
        break;
    case 0x76a00:
        scroll_actor(ram, actor, 0xb71a);
        if (below_playfield(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 0x0c);
            uint16_t child = spawn_far(ram, actor, 0x7000, 0x6aaa);
            ram[child + 0x2d] = 5;
            ram[child + 0x2c] = ram[actor + 0x2c];
            ram[child + 0x29] = ram[actor + 0x29] ^ 1;
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x38, read_word(ram, actor + 0x38));
            write_word(ram, child + 0x3a, read_word(ram, actor + 0x3a));
            write_word(ram, child + 0x44, 1);
        }
        break;
    case 0x76a77:
    case 0x76aaa: {
        if (address == 0x76a77 && read_word(ram, actor + 0x40) < 8) {
            follow_parent(ram, actor);
        } else {
            if (address == 0x76a77) {
                int16_t speed = 0x300 - read_word(ram, actor + 0x46);
                write_word(ram, actor + 0x2e, speed > 0x400 ? 0x400 : speed);
                ram[actor + 0x28] = (uint8_t)(((int8_t)(ram[actor + 0x28] - 0x80) >> 2) + 0x80);
                set_velocity(ram, rom, actor);
                write_word(ram, actor, 0x6aaa);
            }
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0x597c + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0x599e + frame));
            write_word(ram, actor + 0x3c, read_word(bank, 0x59be + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        if (read_word(ram, actor + 0x3c)) {
            ram[actor + 0x0d] = 2;
            world_player_contact(ram, rom, actor);
        }
        break;
    }
    case 0x76b07:
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0x59de + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0x59ec + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x76b47:
        follow_parent(ram, actor);
        write_word(ram, actor + 0x2a, read_word(bank, 0x5a10 + (read_word(ram, actor + 0x40) & 2)));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        queue_sprite(ram, rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    case 0x76b7b:
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, 0xf873);
        write_word(ram, actor, 0x6b89);
        /* fall through */
    case 0x76b89:
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x2a, read_word(bank, 0x5a14 + (read_word(ram, actor + 0x40) & 6)));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        queue_sprite(ram, rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, rom, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}


/* ---- Cave swimmers with a shoulder gun (bank B000, 0879..10B8). ---- */

static uint16_t material_bit(const uint8_t *tiles, uint16_t cell)
{
    return (uint16_t)(1u << ((tiles[0x4000 + ((cell + 3) & 0xffff)] >> 3) & 15));
}

static uint16_t cell_below(uint16_t cell) { return (cell + 0x100) & 0x3fff; }               /* B108C */
static uint16_t cell_right(uint16_t cell) { return (cell & 0xff00) | ((cell + 4) & 0xff); }  /* B10A5 */

/* B0E83: probe the wall ahead of the swimmer; on a hit turn around and pause.
 * The eight probe routines are selected through the table at B000:0778. */
static int probe_run(const uint8_t *tiles, uint16_t *cell, int below, unsigned count, uint16_t mask)
{
    for (unsigned i = 0; i < count; ++i) {
        *cell = below ? cell_below(*cell) : cell_right(*cell);
        if (material_bit(tiles, *cell) & mask) return 1;
    }
    return 0;
}

static void swimmer_wall(uint8_t *ram, const uint8_t *bank, const uint8_t *tiles, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24), cell;
    uint16_t probe = read_word(bank, 0x778 + ((ram[actor + 0x28] & 0xe0) >> 4));
    int hit, flip_x = 1, flip_y = 1;
    unsigned turn = 0x80;
    if (probe == 0xe94 || probe == 0xe9a) {
        cell = terrain_cell(ram, x + (probe == 0xe94 ? 0x10 : -0x10), y + 0x10);
        flip_y = 0;
        hit = (material_bit(tiles, cell) & 0xff02) || probe_run(tiles, &cell, 1, 1, 0xff02);
        if (!hit) {
            /* The last three cells must all be solid. */
            hit = 1;
            for (unsigned i = 0; i < 3 && hit; ++i) {
                cell = cell_below(cell);
                hit = (material_bit(tiles, cell) & 0xff02) != 0;
            }
        }
    } else if (probe == 0xeed || probe == 0xef3) {
        cell = terrain_cell(ram, x - 0x10, y + (probe == 0xeed ? 0x10 : -0x10));
        flip_x = 0;
        turn = 0x40;
        hit = (material_bit(tiles, cell) & 0xff0a) || probe_run(tiles, &cell, 0, 4, 0xff0a);
    } else if (probe == 0xf46 || probe == 0xf94) {
        cell = terrain_cell(ram, x + (probe == 0xf94 ? 0x10 : 0), y + 0x10);
        hit = (material_bit(tiles, cell) & 0xff0a) || probe_run(tiles, &cell, 0, 2, 0xff0a);
        if (!hit) {
            if (probe == 0xf94) cell = (cell & 0xff00) | ((cell - 8) & 0xff);
            hit = probe_run(tiles, &cell, 1, 2, 0xff02);
        }
    } else if (probe == 0xfe8) {
        cell = terrain_cell(ram, x - 0x10, y);
        hit = (material_bit(tiles, cell) & 0xff02) || probe_run(tiles, &cell, 1, 2, 0xff02)
              || probe_run(tiles, &cell, 0, 2, 0xff02);
    } else if (probe == 0x1036) {
        cell = terrain_cell(ram, x, y - 0x10);
        hit = (material_bit(tiles, cell) & 0xff02) || probe_run(tiles, &cell, 0, 2, 0xff02);
        if (!hit) {
            cell = (cell - 0x200) & 0x3fff;
            hit = probe_run(tiles, &cell, 1, 2, 0xff02);
        }
    } else return;
    if (!hit) return;
    write_word(ram, actor + 0x4c, 0x30);
    if (flip_x) write_word(ram, actor + 0x38, -read_word(ram, actor + 0x38));
    if (flip_y) write_word(ram, actor + 0x3a, -read_word(ram, actor + 0x3a));
    ram[actor + 0x28] ^= turn;
    write_word(ram, actor, 0xaca);
}

/* B9A2C: the retreat reuses the patrol steering of the B000:9xxx swimmers. */
static void retreat_steering(uint8_t *ram, const uint8_t *bank, const uint8_t *tiles, uint16_t actor)
{
    if (read_word(ram, actor + 0x4e) && !decrement(ram, actor + 0x4e)) write_word(ram, actor + 0x3a, 0);
    if (read_word(ram, actor + 0x46)) {
        if (decrement(ram, actor + 0x46)) return;
        uint16_t speed = read_word(ram, actor + 0x2e);
        write_word(ram, actor + 0x38, ram[actor + 0x29] & 1 ? -speed : speed);
        write_word(ram, actor + 0x3a, 0);
        write_word(ram, actor + 0x4a, (world_random_byte(ram) & 0xf) + 0x30);
        write_word(ram, actor + 0x4c, 0);
        write_word(ram, actor + 0x64, read_word(ram, actor + 0x64) & ~1u);
        return;
    }
    uint16_t cell = terrain_cell(ram, read_word(ram, actor + 0x21) + read_word(bank, 0x8d58 + 2 * (ram[actor + 0x29] & 1)),
                                 read_word(ram, actor + 0x24));
    unsigned material = tiles[0x4000 + cell + 3] >> 3;
    if (!((1u << (material & 15)) & 0xff02)) {
        cell = (cell + 0x100) & 0x3fff;
        material = tiles[0x4000 + cell + 3] >> 3;
        if (!((1u << (material & 15)) & 0xff02)) {
            cell = (cell - 0x200) & 0x3fff;
            material = tiles[0x4000 + cell + 3] >> 3;
            if (!((1u << (material & 15)) & 0xff02)) return;
        }
    }
    uint16_t index = (uint16_t)(material - 8) & 6;
    write_word(ram, actor + 0x3a, read_word(bank, 0x8d6c + index));
    write_word(ram, actor + 0x38, read_word(bank, 0x8d64 + (index & 4) + ((ram[actor + 0x29] & 1) << 1)));
    write_word(ram, actor + 0x46, 0x0a);
    write_word(ram, actor + 0x4e, 0);
}

/* D3FE (box FB2B) and D3F8 (box FB3B): returns the carry and leaves AX for the caller. */
static int near_box(const uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t box, uint16_t *ax)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (!(ram[0x264] & (1u << i))) continue;
        uint16_t player = read_word(ram, 0x90 + 2 * i);
        uint16_t d = read_word(ram, actor + 0x21) + read_word(rom, box) - read_word(ram, player + 0x21);
        if (d & 0x8000) d = -d;
        *ax = d;
        if (d >= read_word(rom, box + 4)) continue;
        d = read_word(ram, actor + 0x24) + read_word(rom, box + 2) - read_word(ram, player + 0x24);
        if (d & 0x8000) d = -d;
        *ax = d;
        if (d < read_word(rom, box + 6)) return 1;
    }
    return 0;
}

static int swimmer_handlers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0xb0000;
    int contacts = 1;
    switch (address) {
    case 0xb0879:
        write_word(ram, 0xb8a8, 1);
        finish_animation(ram, actor);
        return WORLD_HANDLER_DONE;
    case 0xb0893: {
        if (read_word(ram, 0xc0) >= 8) { finish_animation(ram, actor); return WORLD_HANDLER_DONE; }
        write_word(ram, actor + 0x4e, read_word(ram, 0xc0));
        write_word(ram, 0xc0, read_word(ram, 0xc0) + 1);
        world_acquire_resource(ram, actor, 0x4b);
        world_acquire_resource(ram, actor, 0x4a);
        write_word(ram, actor + 0x0e, 3);
        ram[actor + 0x0c] = 4;
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 0xc0);
        write_word(ram, actor + 0x36, 0x28);
        write_word(ram, actor + 0x3c, 0xf2b3);
        write_word(ram, actor + 0x2a, 0xe010);
        unsigned entry = (read_word(ram, actor + 0x1e) & 0xe) << 2;
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(bank, 0x63e + entry));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0x640 + entry));
        ram[actor + 0x29] = bank[0x642 + entry];
        begin_linear_script(ram, rom, actor, read_word(bank, 0x63c + entry));
        unsigned aim = ram[actor + 0x1f] & 0xc;
        write_word(ram, actor + 0x48, read_word(bank, 0x6b8 + aim));
        write_word(ram, actor + 0x4a, read_word(bank, 0x6ba + aim));
        uint16_t child = spawn_far(ram, actor, 0xb000, 0x0b25);
        write_word(ram, child + 0x14, read_word(ram, actor + 0x14));
        ram[child + 0x2c] = ram[actor + 0x15];
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        write_word(ram, child + 0x4e, read_word(ram, actor + 0x4e));
        write_word(ram, actor + 0x46, 0x400);
        write_word(ram, actor, 0x957);
        return WORLD_HANDLER_DONE;
    }
    case 0xb0957: {
        int step = step_flight_script(ram, rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0) {
            write_word(ram, actor + 0x2e, 0x80);
            write_word(ram, actor + 0x42, 0x20);
            write_word(ram, actor, 0x9b4);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0xb09b4: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t target;
        if ((!read_word(ram, actor + 0x42) || !decrement(ram, actor + 0x42)) &&
            (target = world_enemy_target(ram, actor)) != 0x8480) {
            int16_t dx = read_word(ram, target + 0x21) + read_word(ram, actor + 0x48) - read_word(ram, actor + 0x21);
            int16_t dy = read_word(ram, target + 0x24) + read_word(ram, actor + 0x4a) - read_word(ram, actor + 0x24);
            ram[actor + 0x28] = (vector_angle(rom, dx, dy) + 8) & 0xf0;
            set_velocity(ram, rom, actor);
            unsigned facing = read_word(ram, target + 0x21) >= read_word(ram, actor + 0x21);
            if (ram[actor + 0x29] != facing) {
                write_word(ram, actor + 0x42, 0x12);
                write_word(ram, actor, 0xa21);
            } else {
                write_word(ram, actor + 0x42, 0x40);
                write_word(ram, actor, 0xa73);
            }
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0xb0a21:
    case 0xb0a3f:
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb0a21) {
            if (read_word(ram, actor + 0x62)) { queue_sprite(ram, rom, actor, 0); break; }
            write_word(ram, actor + 0x2a, 0xe02a);
            write_word(ram, actor + 0x60, 1);
            write_word(ram, actor, 0xa3f);
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 0x40);
            write_word(ram, actor + 0x2a, 0xe010);
            write_word(ram, actor + 0x60, 0);
            write_word(ram, actor, 0xa73);
        } else if (read_word(ram, actor + 0x42) == 9) ram[actor + 0x29] ^= 1;
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0xb0a73:
    case 0xb0aca:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb0a73) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            queue_sprite(ram, rom, actor, 0);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x42, 0x10);
                write_word(ram, actor, 0x9b4);
            }
            swimmer_wall(ram, bank, tiles, actor);
        } else {
            queue_sprite(ram, rom, actor, 0);
            if (!decrement(ram, actor + 0x4c)) {
                write_word(ram, actor + 0x42, 0x40);
                write_word(ram, actor, 0xa73);
            }
        }
        if (read_word(ram, 0xb8a8) || !decrement(ram, actor + 0x46)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor + 0x38, 0xff40);
            write_word(ram, actor + 0x3a, 0);
            ram[actor + 0x28] = 0x80;
            ram[actor + 0x29] = 0;
            write_word(ram, actor, 0xae5);
        }
        break;
    case 0xb0ae5: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        retreat_steering(ram, bank, tiles, actor);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || x < 0x40) {
            write_word(ram, 0xc0, read_word(ram, 0xc0) - 1);
            finish_animation(ram, actor);
            return WORLD_HANDLER_DONE;
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }

    /* The gun riding on the swimmer. */
    case 0xb0b25: {
        ram[actor + 0x2d] = 0x20;
        ram[actor + 0x0c] = 4;
        write_word(ram, actor + 0x2a, 0xdf96);
        uint16_t row = 0x708 + read_word(bank, (uint16_t)(0x6c8 + (read_word(ram, actor + 0x1e) & 0x20) + read_word(ram, 0x88)));
        write_word(ram, actor + 0x50, read_word(bank, row));
        write_word(ram, actor + 0x52, read_word(bank, row));
        write_word(ram, actor + 0x54, read_word(bank, row + 2));
        write_word(ram, actor + 0x56, read_word(bank, row + 2));
        write_word(ram, actor + 0x58, read_word(bank, row + 4));
        write_word(ram, actor + 0x5a, read_word(bank, row + 4));
        write_word(ram, actor + 0x5c, read_word(bank, row + 6));
        write_word(ram, actor, 0xb70);
        return WORLD_HANDLER_DONE;
    }
    case 0xb0b70: {
        uint16_t parent = ancestor(ram, actor, 1);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); return WORLD_HANDLER_DONE; }
        if (read_word(ram, parent + 0x60)) return WORLD_HANDLER_DONE;
        uint16_t y = ((read_word(ram, 0x274) & 0x20) >> 5) + read_word(ram, parent + 0x24);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, y);
        ram[actor + 0x29] = ram[parent + 0x29];
        /* B0DD2: burst timing; the original tests [50] against whatever D3FE left in AX. */
        uint16_t ax = (y & 0xff00) | ram[parent + 0x29];
        int fired = 0;
        if (!near_box(ram, rom, actor, 0xfb2b, &ax)) {
            int fire = 0;
            if (!(read_word(ram, actor + 0x50) & ax)) {
                fire = !decrement(ram, actor + 0x50);
            } else if (!decrement(ram, actor + 0x54)) {
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
                if (!decrement(ram, actor + 0x58)) {
                    write_word(ram, actor + 0x58, read_word(ram, actor + 0x5a));
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                }
                fire = 1;
            }
            if (fire) {
                uint16_t shot = spawn_far(ram, actor, 0xb000, 0x0c8d);
                ram[shot + 0x2c] = ram[actor + 0x14];
                ram[shot + 0x28] = ram[actor + 0x28];
                write_word(ram, shot + 0x2e, read_word(ram, actor + 0x5c));
                uint16_t pose = read_word(bank, 0x7e8 + (ram[actor + 0x28] >> 3));
                write_word(ram, shot + 0x5e, pose);
                uint16_t offset = read_word(bank, 0x838 + (pose >> 1));
                ram[shot + 0x29] = ram[actor + 0x29];
                write_word(ram, shot + 0x31, ram[actor + 0x29] & 1 ? -offset : offset);
                write_word(ram, shot + 0x34, read_word(bank, 0x83a + (pose >> 1)));
                world_queue_sound(ram, 0x77);
                write_word(ram, ancestor(ram, actor, 1) + 0x62, 1);
                write_word(ram, actor + 0x40, 0x20);
                write_word(ram, actor + 0x44, 5);
                write_word(ram, actor, 0xc0f);
                fired = 1;
            }
        }
        uint16_t target;
        if ((fired || (read_word(ram, 0x274) & 7) == read_word(ram, actor + 0x4e)) &&
            (target = world_enemy_target(ram, actor)) != 0x8480) {
            int16_t dx = read_word(ram, target + 0x21) - read_word(ram, actor + 0x21);
            int16_t dy = read_word(ram, target + 0x24) - read_word(ram, actor + 0x24);
            unsigned sector = ((vector_angle(rom, dx, dy) + 8) & 0xf0) >> 4;
            if (ram[actor + 0x29] & 1) sector += 0x10;
            ram[actor + 0x28] = bank[0x788 + sector];
            write_word(ram, actor + 0x2a, read_word(bank, 0x7a8 + (ram[actor + 0x28] >> 3)));
        }
        queue_sprite(ram, rom, actor, 0);
        return WORLD_HANDLER_DONE;
    }
    case 0xb0c0f: {
        uint16_t parent = ancestor(ram, actor, 1);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); return WORLD_HANDLER_DONE; }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, ((read_word(ram, 0x274) & 0x20) >> 5) + read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x7a8 + (ram[actor + 0x28] >> 3) + frame)));
            write_word(ram, actor + 0x44, 0x0b);
            write_word(ram, actor + 0x40, frame - 0x20);
            if (frame < 0x20) {
                write_word(ram, ancestor(ram, actor, 1) + 0x62, 0);
                write_word(ram, actor, 0xb70);
            }
        }
        queue_sprite(ram, rom, actor, 0);
        return WORLD_HANDLER_DONE;
    }

    /* The shot: leaves the muzzle, then flies straight. */
    case 0xb0c8d:
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x3c, 0xf2bb);
        write_word(ram, actor + 0x26, 0x7f00);
        write_word(ram, actor + 0x36, 0x100);
        write_word(ram, actor + 0x44, 1);
        set_velocity(ram, rom, actor);
        write_word(ram, actor, 0xcaf);
        /* fall through */
    case 0xb0caf:
    case 0xb0d24: {
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address != 0xb0d24) {
            uint16_t parent = ancestor(ram, actor, 1);
            write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
            write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
            int launch = 0;
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x808 + frame + read_word(ram, actor + 0x5e))));
                write_word(ram, actor + 0x44, read_word(bank, 0x830 + frame));
                write_word(ram, actor + 0x40, frame + 2);
                if (frame + 2 >= 8) {
                    write_word(ram, actor + 0x40, 0);
                    unsigned facing = bank[0x874 + (read_word(ram, actor + 0x5e) >> 3)];
                    if (!(read_word(ram, actor + 0x38) & 0x8000)) facing |= 1;
                    ram[actor + 0x29] = facing;
                    ram[actor + 0x2d] = 4;
                    write_word(ram, actor, 0xd24);
                    launch = 1;
                }
            }
            if (!launch) { queue_sprite(ram, rom, actor, 0); return WORLD_HANDLER_DONE; }
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || x < 0x40 || below_playfield(ram, actor) || above_playfield(ram, actor)) {
            finish_animation(ram, actor);
            return WORLD_HANDLER_DONE;
        }
        write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x84c + (read_word(ram, actor + 0x40) & 6) + read_word(ram, actor + 0x5e))));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        queue_sprite(ram, rom, actor, 0);
        if ((1u << (terrain_material(ram, tiles, x, read_word(ram, actor + 0x24)) & 15)) & 0xff0a) {
            finish_animation(ram, actor);
            return WORLD_HANDLER_DONE;
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, rom, actor)) finish_animation(ram, actor);
        return WORLD_HANDLER_DONE;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    if (contacts) {
        /* B0972 */
        ram[actor + 0x0d] = 0;
        uint16_t attacker = world_player_contact(ram, rom, actor);
        if (!attacker) attacker = read_word(ram, 0x92);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            attacker = world_projectile_contact(ram, rom, actor);
            if (read_word(ram, actor + 0x26)) return WORLD_HANDLER_DONE;
            if (!attacker) return WORLD_HANDLER_STOP;
        }
        world_award_score(ram, rom, actor, attacker);
        loud_explosion(ram, rom, actor);
        write_word(ram, 0xc0, read_word(ram, 0xc0) - 1);
        finish_animation(ram, actor);
    }
    return WORLD_HANDLER_DONE;
}


/* ---- Sea serpent (bank B000, D457..DC4B): head, delayed body chain, fins. ---- */

static int outside_playfield(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21);
    return x > 0x1c0 || x < 0x40 || below_playfield(ram, actor) || above_playfield(ram, actor);
}

/* DBA7: a segment takes the direction its parent had eight passes ago. */
static void serpent_follow(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = ancestor(ram, actor, 1);
    if ((ram[parent + 0x0a] & 2) && outside_playfield(ram, actor)) {
        finish_animation(ram, actor);
        return;
    }
    unsigned slot = read_word(ram, actor + 0x54) & 7;
    ram[actor + 0x60 + slot] = ram[parent + 0x28];
    slot = (slot + 1) & 7;
    ram[actor + 0x28] = ram[actor + 0x60 + slot];
    write_word(ram, actor + 0x54, slot);
}

/* DC2C: when the parent is dying this segment starts sinking too. */
static void serpent_inherit_death(uint8_t *ram, uint16_t actor)
{
    if (ram[ancestor(ram, actor, 1) + 0x0a] & 2) {
        write_word(ram, actor, 0xd647);
        write_word(ram, actor + 2, 0xb000);
    }
}

static void serpent_pose(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t sprites, uint16_t shapes, uint16_t facings)
{
    unsigned sector = ((uint8_t)(ram[actor + 0x28] + 8) & 0xf0) >> 3;
    write_word(ram, actor + 0x2a, read_word(bank, sprites + sector));
    write_word(ram, actor + 0x3c, read_word(bank, shapes + sector));
    ram[actor + 0x29] = bank[facings + (sector >> 1)];
}

static int serpent_handlers(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0xb0000;
    uint16_t profile = read_word(bank, 0xd3fe + read_word(ram, 0x8c));
    switch (address) {
    case 0xbd457: {
        if (read_word(ram, 0xb8bc)) { finish_animation(ram, actor); break; }
        write_word(ram, 0xb8bc, read_word(ram, 0xb8bc) + 1);
        acquire_private_palette(ram, actor, 0xd7);
        write_word(ram, actor + 0x0e, 0x19);
        ram[actor + 0x2d] = 0x28;
        ram[actor + 0x0c] = 4;
        write_word(ram, actor + 0x2e, 0x180);
        unsigned variant = read_word(ram, actor + 0x1e) & 3;
        ram[actor + 0x28] = bank[0xd44e + variant];
        write_word(ram, actor + 0x5a, read_word(bank, 0xd451 + 2 * variant));
        write_word(ram, actor + 0x1e, 0);
        set_velocity(ram, rom, actor);
        write_word(ram, actor + 0x26, read_word(bank, 0xd41e + profile));
        write_word(ram, actor + 0x36, read_word(bank, 0xd41e + profile));
        write_word(ram, actor + 0x48, read_word(bank, 0xd422 + profile));
        write_word(ram, actor + 0x4a, read_word(bank, 0xd424 + profile));
        uint16_t child = spawn_far(ram, actor, 0xb000, 0xd904);
        write_word(ram, child + 0x26, read_word(ram, actor + 0x48));
        ram[child + 0x2c] = ram[actor + 0x2c];
        write_word(ram, child + 0x31, 8);
        write_word(ram, child + 0x34, 8);
        child = spawn_far(ram, actor, 0xb000, 0xd904);
        write_word(ram, child + 0x26, read_word(ram, actor + 0x4a));
        ram[child + 0x2c] = ram[actor + 0x2c];
        ram[child + 0x29] = 2;
        write_word(ram, child + 0x31, 0);
        write_word(ram, child + 0x34, 0xfff8);
        child = spawn_far(ram, actor, 0xb000, 0xd6bf);
        write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
        write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
        ram[child + 0x2c] = ram[actor + 0x2c];
        ram[child + 0x2d] = ram[actor + 0x2d];
        write_word(ram, child + 0x50, 0x14);
        write_word(ram, actor + 0x42, 0x60);
        write_word(ram, actor + 0x58, 0x40);
        ram[actor + 0x4c] = 8;
        write_word(ram, actor + 0x4e, 0xf0);
        write_word(ram, actor, 0xd560);
        break;
    }
    case 0xbd560: {
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 0x10);
            uint16_t target = world_enemy_target(ram, actor);
            uint16_t tx = target != 0x8480 ? read_word(ram, target + 0x21) : 0x100;
            uint16_t ty = target != 0x8480 ? read_word(ram, target + 0x24) : 0x100;
            unsigned lead = read_word(ram, actor + 0x56) & 0xc;
            int16_t dx = tx - read_word(ram, actor + 0x21) + read_word(bank, 0xd43e + lead);
            int16_t dy = ty - read_word(ram, actor + 0x24) + read_word(bank, 0xd440 + lead);
            uint8_t wanted = (uint8_t)((vector_angle(rom, dx, dy) + ram[actor + 0x4c]) & read_word(ram, actor + 0x4e));
            uint8_t heading = ram[actor + 0x28];
            if (wanted != heading) {
                heading += (uint8_t)(wanted - heading) < (uint8_t)(heading - wanted) ? 0x10 : -0x10;
                ram[actor + 0x28] = heading;
                set_velocity(ram, rom, actor);
            }
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        serpent_pose(ram, bank, actor, 0xd29a, 0xd33a, 0xd37a);
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x58)) {
            write_word(ram, actor + 0x56, read_word(ram, actor + 0x56) + 4);
            write_word(ram, actor + 0x58, 0x80);
        }
        ram[actor + 0x0d] = 0;
        uint16_t shot = world_projectile_contact(ram, rom, actor);
        if (shot && !read_word(ram, actor + 0x46)) {
            uint16_t child = spawn_far(ram, actor, 0xb000, 0xdaf3);
            ram[child + 0x2c] = ram[actor + 0x2c];
            write_word(ram, actor + 0x46, 0x32);
        }
        if (read_word(ram, actor + 0x46)) decrement(ram, actor + 0x46);
        if (read_word(ram, actor + 0x26)) break;
        if (!shot) return WORLD_HANDLER_STOP;
        world_award_score(ram, rom, actor, shot);
        queue_palette(ram, ram[actor + 0x2c] & 0x3f, 0xd8, 5);
        write_word(ram, 0xb8bc, read_word(ram, 0xb8bc) - 1);
        write_word(ram, actor, 0xd647);
        break;
    }
    /* Dying: sink while blinking, then leave the list from the tail end. */
    case 0xbd647:
        world_queue_sound(ram, 0x90);
        ram[actor + 0x0a] = 2;
        write_word(ram, actor + 0x42, (world_random_byte(ram) & 0x1f) + 0x20);
        write_word(ram, actor + 0x3a, 0xff80);
        write_word(ram, actor, 0xd66c);
        /* fall through */
    case 0xbd66c:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (decrement(ram, actor + 0x42)) {
            if (read_word(ram, actor + 0x42) & 1) queue_sprite(ram, rom, actor, 1);
        } else if (!read_word(ram, actor + 0x1e)) write_word(ram, actor, 0xd6a9);
        else finish_animation(ram, actor);
        break;
    case 0xbd6a9: {
        uint16_t depth = read_word(ram, actor + 8);
        if (depth < read_word(ram, read_word(ram, actor + 4) + 8)) break;
        if (depth > 0xff) return WORLD_HANDLER_STOP;
        acquire_private_palette(ram, actor, depth);   /* 110B is entered with AX = depth */
        retire_actor(ram, actor);
        break;
    }

    /* Body segments grow one by one behind the head; the last is the tail. */
    case 0xbd6bf:
    case 0xbd843:
        if (address == 0xbd6bf) write_word(ram, actor + 0x0e, 0x2c);
        write_word(ram, actor + 0x2e, 0x180);
        write_word(ram, actor + 0x42, 8);
        write_word(ram, actor + 0x1e, 1);
        ++ram[actor + 0x2d];
        write_word(ram, actor + 0x26, read_word(bank, 0xd420 + profile));
        write_word(ram, actor + 0x36, read_word(bank, 0xd420 + profile));
        write_word(ram, actor, address == 0xbd6bf ? 0xd6f0 : 0xd86f);
        break;
    case 0xbd6f0:
    case 0xbd86f:
        if (ram[ancestor(ram, actor, 1) + 0x0a] & 2) { finish_animation(ram, actor); break; }
        scroll_actor(ram, actor, 0xb71a);
        serpent_follow(ram, actor);
        if (decrement(ram, actor + 0x42)) break;
        if (address == 0xbd86f) { write_word(ram, actor, 0xd8ab); break; }
        {
            uint16_t left = decrement(ram, actor + 0x50);
            uint16_t child = spawn_far(ram, actor, 0xb000, left ? 0xd6bf : 0xd843);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            ram[child + 0x2c] = ram[actor + 0x2c];
            ram[child + 0x2d] = ram[actor + 0x2d];
            write_word(ram, child + 0x50, left);
            write_word(ram, child + 0x52, read_word(ram, actor + 0x52) + 1);
            write_word(ram, actor, 0xd76f);
        }
        break;
    case 0xbd76f:
    case 0xbd7ea:
    case 0xbd8ab: {
        scroll_actor(ram, actor, 0xb71a);
        serpent_follow(ram, actor);
        set_velocity(ram, rom, actor);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (address == 0xbd76f) {
            unsigned phase = read_word(ram, actor + 0x40) & 0x20;
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
            serpent_pose(ram, bank, actor, 0xd2ba + phase, 0xd35a, 0xd38a);
            queue_sprite(ram, rom, actor, 1);
            ram[actor + 0x0d] = 0;
            uint16_t attacker = world_player_contact(ram, rom, actor);
            if (!attacker) attacker = read_word(ram, 0x92);
            if (read_word(ram, actor + 0x26)) {
                ram[actor + 0x0d] = 0;
                attacker = world_projectile_contact(ram, rom, actor);
            }
            if (!read_word(ram, actor + 0x26)) {
                if (!attacker) return WORLD_HANDLER_STOP;
                world_award_score(ram, rom, actor, attacker);
                write_word(ram, actor, 0xd7ea);
            }
        } else {
            if (address == 0xbd7ea) serpent_pose(ram, bank, actor, 0xd2fa, 0xd35a, 0xd37a);
            else serpent_pose(ram, bank, actor, 0xd31a, 0xd35a, 0xd38a);
            queue_sprite(ram, rom, actor, address == 0xbd8ab);
            body_contacts(ram, rom, actor);
        }
        serpent_inherit_death(ram, actor);
        break;
    }

    /* Fins on the head: break off once the head is weak enough. */
    case 0xbd904:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor, 0xd90e);
        break;
    case 0xbd90e: {
        uint16_t parent = ancestor(ram, actor, 1);
        if (bank[0xd39a + ((ram[parent + 0x28] & 0xf0) >> 4)]) break;
        if ((int16_t)read_word(ram, parent + 0x26) > (int16_t)read_word(ram, actor + 0x26)) break;
        write_word(ram, actor + 0x44, 1);
        world_queue_sound(ram, 0x90);
        write_word(ram, actor, 0xd949);
        break;
    }
    case 0xbd949:
    case 0xbd9dd: {
        uint16_t parent = ancestor(ram, actor, 1);
        if (address == 0xbd9dd && ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        unsigned sector = (ram[parent + 0x28] & 0xf0) >> 4;
        if (bank[0xd39a + sector]) break;
        uint16_t dx = read_word(ram, actor + 0x31);
        unsigned mirror = bank[0xd3aa + sector] & 1;
        ram[actor + 0x29] = (ram[actor + 0x29] & 2) | mirror;
        write_word(ram, actor + 0x21, (mirror ? -dx : dx) + read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            if (address == 0xbd9dd) {
                write_word(ram, actor + 0x2a, read_word(bank, 0xd3c4 + frame));
                write_word(ram, actor + 0x44, 8);
                write_word(ram, actor + 0x40, frame + 2 >= 0x12 ? 0 : frame + 2);
            } else if (read_word(bank, 0xd3ba + frame) == 0xffff) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                uint16_t child = spawn_far(ram, actor, 0xb000, 0xda66);
                ram[child + 0x2c] = ram[actor + 0x2c];
                write_word(ram, child + 0x34, 8);
                write_word(ram, actor, 0xd9dd);
            } else {
                write_word(ram, actor + 0x2a, read_word(bank, 0xd3ba + frame));
                write_word(ram, actor + 0x44, read_word(bank, 0xd3c0 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0xbda66:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xda74);
        /* fall through */
    case 0xbda74: {
        uint16_t head = ancestor(ram, actor, 2);
        if (ram[head + 0x0a]) { finish_animation(ram, actor); break; }
        unsigned sector = (ram[head + 0x28] & 0xf0) >> 4;
        if (!bank[0xd39a + sector]) break;
        unsigned facing = bank[0xd3aa + sector];
        uint16_t dy = read_word(ram, actor + 0x34);
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x31) + read_word(ram, head + 0x21));
        ram[actor + 0x29] = facing;
        write_word(ram, actor + 0x24, (facing & 2 ? -dy : dy) + read_word(ram, head + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank, 0xd3d8 + (read_word(ram, actor + 0x40) & 6)));
            write_word(ram, actor + 0x44, 0x0a);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    /* Hit flash on the head. */
    case 0xbdaf3:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x31, 0xfff8);
        write_word(ram, actor + 0x34, 0x10);
        write_word(ram, actor, 0xdb0b);
        /* fall through */
    case 0xbdb0b: {
        uint16_t parent = ancestor(ram, actor, 1);
        ram[actor + 0x29] = bank[0xd3aa + ((ram[parent + 0x28] & 0xf0) >> 4)];
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x31) + read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0xd3e0 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0xd3f0 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}


/* ---- Smaller enemies of the later cave. ---- */
static int cave_handlers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0xb0000;
    switch (address) {
    /* Wavering drifter that spits aimed shots. */
    case 0xbc7f8: {
        world_acquire_resource(ram, actor, 0x47);
        world_acquire_resource(ram, actor, 0x46);
        write_word(ram, actor + 0x0e, 0x17);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x3c, 0xf613);
        write_word(ram, actor + 0x26, 0x60);
        write_word(ram, actor + 0x36, 0x20);
        uint16_t row = read_word(bank, (uint16_t)(0xc7a8 + (read_word(ram, actor + 0x1e) & 0x20) + read_word(ram, 0x88)));
        write_word(ram, actor + 0x52, read_word(bank, 0xc7e8 + row));
        write_word(ram, actor + 0x54, read_word(bank, 0xc7ea + row));
        write_word(ram, actor + 0x50, 0x10);
        write_word(ram, actor + 0x46, 0x10);
        write_word(ram, actor + 0x48, 0x100);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor + 0x38, 0xff80);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xc86b);
        break;
    }
    case 0xbc86b: {
        /* 435F */
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, 0xb71a));
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        {   /* 675C */
            uint16_t travelled = read_word(ram, actor + 0x21) - read_word(ram, actor + 0x31);
            unsigned phase = ((uint16_t)(travelled * read_word(ram, actor + 0x48)) >> 8) & 0xff;
            int32_t wave = (int32_t)(int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * phase) * (int16_t)read_word(ram, actor + 0x46);
            uint32_t low = (uint32_t)(uint16_t)wave + read_word(ram, actor + 0x33);
            write_word(ram, actor + 0x23, (uint16_t)low);
            ram[actor + 0x25] = (uint8_t)((uint32_t)wave >> 16) + ram[actor + 0x35] + (low >> 16);
        }
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank, 0xc790 + (read_word(ram, actor + 0x40) & 6)));
            write_word(ram, actor + 0x44, 4);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        if (!queue_sprite(ram, rom, actor, 1)) { finish_animation(ram, actor); break; }
        uint16_t ax = 0;
        if (!near_box(ram, rom, actor, 0xfb3b, &ax) && read_word(ram, actor + 0x21) >= 0x60) {
            if (read_word(ram, actor + 0x50)) decrement(ram, actor + 0x50);
            else if (read_word(ram, 0xb8a2) & 3) {
                uint16_t target = read_word(ram, read_word(ram, 0xb8a2) & 1 ? 0x90 : 0x92);
                int16_t dx = read_word(ram, target + 0x21) - read_word(ram, actor + 0x21);
                int16_t dy = read_word(ram, target + 0x24) - read_word(ram, actor + 0x24);
                ram[actor + 0x28] = (vector_angle(rom, dx, dy) + 8) & 0xf0;
                uint16_t child = world_spawn_child(ram, actor, 0xc97b);
                if (child) {
                    write_word(ram, child + 2, 0xb000);
                    ram[child + 0x2c] = ram[actor + 0x14];
                    ram[child + 0x28] = ram[actor + 0x28];
                    write_word(ram, child + 0x2e, read_word(ram, actor + 0x54));
                    world_queue_sound(ram, 0x64);
                }
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t attacker = world_player_contact(ram, rom, actor);
        if (!attacker) attacker = read_word(ram, 0x92);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            attacker = world_projectile_contact(ram, rom, actor);
            if (read_word(ram, actor + 0x26)) break;
            if (!attacker) return WORLD_HANDLER_STOP;
        }
        world_award_score(ram, rom, actor, attacker);
        loud_explosion(ram, rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbc97b:
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, 0xf61b);
        set_velocity(ram, rom, actor);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xc993);
        /* fall through */
    case 0xbc993:
    case 0xbc9e5: {
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address != 0xbc9e5) {
            uint16_t parent = ancestor(ram, actor, 1);
            write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
            write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
            int launch = 0;
            if (!decrement(ram, actor + 0x44)) {
                uint16_t sprite = read_word(bank, 0xc798 + read_word(ram, actor + 0x40));
                if (sprite == 0xffff) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor, 0xc9e5);
                    launch = 1;
                } else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
                }
            }
            if (!launch) {
                if (!queue_sprite(ram, rom, actor, 0)) finish_animation(ram, actor);
                break;
            }
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (outside_playfield(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank, 0xc7a0 + (read_word(ram, actor + 0x40) & 6)));
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        if (!queue_sprite(ram, rom, actor, 0)) { finish_animation(ram, actor); break; }
        if ((1u << (terrain_material(ram, tiles, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24)) & 15)) & 0xff1a) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, rom, actor)) finish_animation(ram, actor);
        break;
    }

    /* Cluster that bursts into four falling pieces. */
    case 0xb7cda:
        world_acquire_resource(ram, actor, 0x15);
        ram[actor + 0x2d] = 0x21;
        for (int i = 3; i >= 0; --i) {
            uint16_t child = spawn_far(ram, actor, 0xb000, 0x7e2f);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x3a, read_word(ram, actor + 0x2e));
            write_word(ram, child + 0x38, read_word(bank, 0x78d0 + 2 * i));
        }
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x7d22);
        /* fall through */
    case 0xb7d22: {
        uint16_t parent = ancestor(ram, actor, 1);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0x78b6 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0x78b8 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 1);
        break;
    }
    case 0xb7e2f:
        world_acquire_resource(ram, actor, 0x14);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x3c, 0xf4e3);
        write_word(ram, actor, 0x7e4f);
        /* fall through */
    case 0xb7e4f: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 0x20);
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || x < 0x40) { finish_animation(ram, actor); break; }
        uint16_t spin = read_word(ram, actor + 0x40) + 0x80;
        if (spin >= 0x600) spin = 0;
        write_word(ram, actor + 0x40, spin);
        write_word(ram, actor + 0x2a, read_word(bank, 0x78d8 + 2 * (spin >> 8)));
        queue_sprite(ram, rom, actor, 1);
        unsigned material = terrain_material(ram, tiles, x, read_word(ram, actor + 0x24));
        if ((1u << (material & 15)) & 0xff02) { finish_animation(ram, actor); break; }
        if (material == 3) {
            /* 6BCC */
            uint16_t splash = effect_slot(ram, actor, 0x6efe);
            write_word(ram, splash + 0x4c, read_word(rom, 0x6bda));
            write_word(ram, splash + 0x4a, read_word(rom, 0x6bdf));
            write_word(ram, splash + 0x44, 0x0b);
            ram[splash + 0x2d] = 0x20;
            write_word(ram, splash + 0x56, 0x30);
            write_word(ram, splash + 0x58, 0x80);
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, rom, actor)) finish_animation(ram, actor);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}


int world_stage4_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank7 = main_rom + 0x70000, *bankb = main_rom + 0xb0000;
    (void)pass;
    switch (address) {
    /* Vertical scroll section: the camera moves on while a player is below the
     * line [4C] (actor Y grows upwards) and none has reached 148. */
    case 0x3dfe:
        for (unsigned pos = 0xb716; pos <= 0xb720; pos += 2) write_word(ram, pos, 0);
        for (unsigned pos = 0xb726; pos <= 0xb72c; pos += 2) write_word(ram, pos, 0);
        /* fall through */
    case 0x3e1e:
        write_word(ram, 0x270, 2);
        write_word(ram, actor, 0x3e29);
        /* fall through */
    case 0x3e29: {
        update_camera_limits(ram, main_rom, actor);
        uint16_t flags = read_word(ram, 0x26e) & ~0x4000u;
        int moving = 0, blocked = (flags & 0x8000) != 0;
        uint16_t line = read_word(ram, actor + 0x4c);
        for (unsigned i = 0; i < 2 && !blocked; ++i) {
            if (!(ram[0x264] & (4u << i))) continue;
            uint16_t y = read_word(ram, read_word(ram, 0x90 + 2 * i) + 0x24);
            if (y >= 0x148) blocked = 1;
            else if (y < line) moving = 1;
        }
        if (blocked || !moving) {
            write_word(ram, 0x26e, flags);
            write_word(ram, 0xb71c, 0);
            write_word(ram, 0xb72c, 0);
            write_word(ram, 0xa4, 0);
            break;
        }
        write_word(ram, 0x26e, flags | 0x4000);
        write_word(ram, actor + 0x56, read_word(ram, actor + 0x56) + 1);
        write_word(ram, 0xb70c, read_word(ram, 0xb70c) + 1);
        write_word(ram, 0xb700, read_word(ram, 0xb700) + 1);
        write_word(ram, 0xb71c, 1);
        write_word(ram, 0xb72c, 1);
        advance_scroll_script(ram, main_rom, actor);
        if (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)) return WORLD_HANDLER_STOP;
        break;
    }

    /* Animated background tile blocks. */
    case 0x7711d:
        write_word(ram, actor + 0x46, read_word(bank7, 0x6d28 + (read_word(ram, actor + 0x1e) & 0xe)));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x1e) & 0x70);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x7140);
        break;
    case 0x77140:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            draw_block_columns(ram, main_rom, tiles, 0x4000, read_word(ram, actor + 0x46),
                               0x4cd4 + (read_word(ram, actor + 0x40) & 0x70), 0x204);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x10);
            write_word(ram, actor + 0x42, 7);
        }
        break;
    case 0x77239:
        write_word(ram, actor + 0x46, read_word(bank7, 0x6d4a + (read_word(ram, actor + 0x1e) & 6)));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x1e) & 0x70);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x725c);
        break;
    case 0x7725c:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (read_word(ram, 0xb700) >= 0x620) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            draw_block_columns(ram, main_rom, tiles, 0x8000, read_word(ram, actor + 0x46),
                               0x4d54 + (read_word(ram, actor + 0x40) & 0x70), 0x402);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x10);
            write_word(ram, actor + 0x42, 7);
        }
        break;

    /* Idle scenery creature: waits a random time, then plays one animation. */
    case 0x77187:
        world_acquire_resource(ram, actor, read_word(bank7, 0x6d24 + (read_word(ram, actor + 0x1e) & 2)));
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x44, (world_random_byte(ram) & 0xf) + 1);
        write_word(ram, actor, 0x71af);
        break;
    case 0x771af:
    case 0x772c1:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor, address == 0x771af ? 0x71e3 : 0x72f5);
        }
        break;
    case 0x771e3:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            uint16_t sprite = read_word(bank7, 0x6d38 + read_word(ram, actor + 0x40));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x44, (world_random_byte(ram) & 0xf) + 1);
                write_word(ram, actor, 0x71af);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x42, 8);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* Vent: releases a rising bubble on the fifth frame of its animation. */
    case 0x772a0:
        world_acquire_resource(ram, actor, 0x38);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x44, (world_random_byte(ram) & 0x1f) + 0x30);
        write_word(ram, actor, 0x72c1);
        break;
    case 0x772f5:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank7, 0x6d52 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x44, (world_random_byte(ram) & 0x1f) + 0x80);
                write_word(ram, actor, 0x72c1);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                if (frame == 8) {
                    uint16_t child = spawn_far(ram, actor, 0x7000, 0x737b);
                    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, child + 0x24, read_word(ram, actor + 0x24) - 0x18);
                    ram[child + 0x2c] = ram[actor + 0x2c];
                    write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
                }
                write_word(ram, actor + 0x42, 8);
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7737b:
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x2a, 0xd6b4);
        write_word(ram, actor + 0x3c, 0xfa43);
        write_word(ram, actor + 0x26, 0x80);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x44, 0x10);
        write_word(ram, actor + 0x42, 4);
        write_word(ram, actor, 0x73a2);
        /* fall through */
    case 0x773a2:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (read_word(ram, actor + 0x44)) {
            decrement(ram, actor + 0x44);
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 0x18);
        }
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (!decrement(ram, actor + 0x42)) {
            ram[actor + 0x29] ^= 1;
            write_word(ram, actor + 0x42, 4);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (terrain_material(ram, tiles, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24)) == 4) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 1);
            if (!read_word(ram, actor + 0x1e)) world_queue_sound(ram, 0x8f);
            write_word(ram, actor, 0x7448);
        }
        ram[actor + 0x0d] = 0;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            /* 6C47 */
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c55));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c5a));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x5b);
            finish_animation(ram, actor);
        }
        break;
    case 0x77448:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank7, 0x6d62 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x42, read_word(bank7, 0x6d76 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* Wall creature: sleeps until its wake condition, then strikes. */
    case 0x77575: {
        world_acquire_resource(ram, actor, 0x34);
        ram[actor + 0x2c] = 0x0f;
        ram[actor + 0x2d] = bank7[0x6da4 + (ram[actor + 0x1f] & 1)];
        ram[actor + 0x29] = ram[actor + 0x1e] & 3;
        unsigned kind = ram[actor + 0x1f] & 6;
        uint16_t frames = read_word(bank7, 0x6da6 + kind);
        write_word(ram, actor + 0x4a, read_word(bank7, 0x6df6 + kind));
        write_word(ram, actor + 0x48, frames);
        write_word(ram, actor + 0x2a, read_word(bank7, frames));
        write_word(ram, actor + 0x44, (world_random_byte(ram) & 0x1f) + 1);
        uint16_t rest = (world_random_byte(ram) & 0xf) + 0x38;
        if (read_word(ram, actor + 0x1e) & 0x400) rest -= 0x20;
        write_word(ram, actor + 0x50, rest);
        write_word(ram, actor, 0x75e2);
        break;
    }
    case 0x775e2: {
        scroll_actor(ram, actor, 0xb71a);
        if (below_playfield(ram, actor) || read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (read_word(ram, 0x286) || (read_word(ram, 0x262) & 0x80)) break;
        int asleep = creature_asleep(ram, bank7, actor);
        if (asleep < 0) return WORLD_HANDLER_STOP;
        if (asleep || decrement(ram, actor + 0x44)) break;
        write_word(ram, actor + 0x40, 2);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x768a);
        if ((read_word(ram, actor + 0x1e) & 0x1c) == 0x10) {
            uint16_t child = spawn_far(ram, actor, 0x7000, 0x69d4);
            unsigned facing = ram[actor + 0x29];
            ram[child + 0x29] = facing;
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21) + (facing & 1 ? 0x1e : -0x1e));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24) - 0x0c);
        }
        break;
    }
    case 0x7768a: {
        scroll_actor(ram, actor, 0xb71a);
        if (below_playfield(ram, actor) || read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank7, (uint16_t)(frame + read_word(ram, actor + 0x48)));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x44, read_word(ram, actor + 0x50));
                write_word(ram, actor, 0x75e2);
            } else {
                uint16_t flags = read_word(ram, actor + 0x1e);
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x42, read_word(bank7, (uint16_t)(frame + read_word(ram, actor + 0x4a))));
                write_word(ram, actor + 0x40, frame + 2);
                uint16_t child;
                if (frame == 0x10 && (child = world_spawn_child(ram, actor, 0x7751)) != 0) {
                    write_word(ram, child + 2, 0x7000);
                    ram[child + 0x29] = ram[actor + 0x29];
                    ram[child + 0x2c] = ram[actor + 0x14];
                    write_word(ram, child + 0x1e, flags);
                    ram[child + 0x2d] = ram[actor + 0x2d] - 1;
                    if ((flags & 0x1c) != 0x10) {
                        world_queue_sound(ram, 0x5e);
                        if (!(flags & 0x2000)) {
                            child = spawn_far(ram, actor, 0x7000, 0x7823);
                            write_word(ram, child + 0x1e, flags);
                        }
                    }
                }
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* The creature's striking part, drawn over its parent. */
    case 0x77751: {
        unsigned kind = ram[actor + 0x1f] & 6;
        write_word(ram, actor + 0x48, read_word(bank7, 0x6e2c + kind));
        write_word(ram, actor + 0x4a, read_word(bank7, 0x6ea0 + kind));
        write_word(ram, actor + 0x4c, read_word(bank7, 0x6ef2 + kind));
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor, 0x7780);
        break;
    }
    case 0x77780: {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (ram[parent + 0x0a] & 1) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        uint16_t flags = read_word(ram, actor + 0x1e);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank7, (uint16_t)(frame + read_word(ram, actor + 0x48)));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x42, read_word(bank7, (uint16_t)(frame + read_word(ram, actor + 0x4a))));
            write_word(ram, actor + 0x40, frame + 2);
            if ((flags & 0x1c) == 0x0c)
                write_word(ram, actor + 0x3c, read_word(bank7, (uint16_t)(frame + read_word(ram, actor + 0x4c))));
        }
        if ((flags & 0x400) && !(((ram[actor + 0x1f] & 2) >> 1) ^ (read_word(ram, 0x274) & 1))) {
            write_word(ram, actor + 0x44, 2);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (read_word(ram, actor + 0x3c)) {
            ram[actor + 0x0d] = 0;
            world_player_contact(ram, main_rom, actor);
        }
        break;
    }
    /* Timed emitter of the creature's projectiles. */
    case 0x77823: {
        uint16_t flags = read_word(ram, actor + 0x1e);
        uint16_t row = read_word(bank7, 0x6f2c + 2 * ((flags & 3) + 2 * ((flags >> 8) & 6)));
        write_word(ram, actor + 0x4a, row);
        write_word(ram, actor + 0x44, read_word(bank7, row));
        write_word(ram, actor, 0x7849);
    }
        /* fall through */
    case 0x77849: {
        if (decrement(ram, actor + 0x44)) break;
        uint16_t child = spawn_far(ram, actor, 0x7000, 0x78c5);
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        uint16_t row = read_word(ram, actor + 0x4a);
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        write_word(ram, child + 0x21, read_word(ram, parent + 0x21) + read_word(bank7, row + 2));
        write_word(ram, child + 0x24, read_word(ram, parent + 0x24) + read_word(bank7, row + 4));
        ram[child + 0x2c] = ram[parent + 0x14];
        ram[child + 0x29] = ram[parent + 0x29];
        ram[child + 0x28] = bank7[(uint16_t)(row + 6)];
        write_word(ram, child + 0x2e, read_word(bank7, row + 8));
        uint16_t next = read_word(bank7, row + 10);
        if (next == 0xffff) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x44, next);
        write_word(ram, actor + 0x4a, row + 10);
        break;
    }
    case 0x778c5: {
        write_word(ram, actor + 0x0e, 0x3a);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, 0xfa43);
        write_word(ram, actor + 0x26, 0x80);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x44, 8);
        write_word(ram, actor + 0x52, 8);
        write_word(ram, actor + 0x54, read_word(ram, actor + 0x1e) & 0x400 ? 7 : 4);
        /* 657D: velocity from the direction byte and the speed. */
        unsigned angle = 2 * ram[actor + 0x28];
        int16_t speed = read_word(ram, actor + 0x2e);
        write_word(ram, actor + 0x38, (uint16_t)(((int32_t)(int16_t)read_word(main_rom + 0xa0000, 0x9e12 + angle) * speed) >> 8));
        write_word(ram, actor + 0x3a, (uint16_t)(((int32_t)(int16_t)read_word(main_rom + 0xa0000, 0x9d92 + angle) * speed) >> 8));
        write_word(ram, actor, 0x7907);
    }
        /* fall through */
    case 0x77907: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        if ((int16_t)read_word(ram, actor + 0x3a) > (int16_t)0xfd80)
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 0x20);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (above_playfield(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x2a, read_word(bank7, 0x700e + (read_word(ram, actor + 0x40) & 6)));
            write_word(ram, actor + 0x42, 6);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        if (!queue_sprite(ram, main_rom, actor, 0)) { finish_animation(ram, actor); break; }
        uint16_t attacker = 0;
        int dead = 0, score = 1;
        if (read_word(ram, actor + 0x44)) {
            decrement(ram, actor + 0x44);
        } else {
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            if ((1u << (terrain_material(ram, tiles, x, y) & 15)) & 0xff02) {
                dead = 1;
                attacker = terrain_cell(ram, x, y);
            }
        }
        if (!dead) {
            if (read_word(ram, 0x262) & 0x80) {
                dead = 1;
                score = 0;
            } else {
                ram[actor + 0x0d] = 0;
                attacker = world_player_contact(ram, main_rom, actor);
                if (attacker) dead = 1;
                else {
                    ram[actor + 0x0d] = 0;
                    attacker = world_projectile_contact(ram, main_rom, actor);
                    if (!read_word(ram, actor + 0x26)) {
                        if (!attacker) return WORLD_HANDLER_STOP;
                        dead = 1;
                    }
                }
            }
        }
        if (dead) {
            if (score) world_award_score(ram, main_rom, actor, attacker);
            loud_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x52)) {
            write_word(ram, actor + 0x52, read_word(ram, actor + 0x54));
            /* 17C3: the trail is linked after the effects anchor. */
            uint16_t child = world_spawn_child(ram, read_word(ram, 0x9a), 0x7a08);
            if (child) write_word(ram, child + 2, 0x7000);
            else child = 0x8480;
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            ram[child + 0x2c] = ram[actor + 0x2c];
            ram[child + 0x29] = read_word(ram, actor + 0x56) & 3;
            write_word(ram, actor + 0x56, read_word(ram, actor + 0x56) + 1);
        }
        break;
    }
    case 0x77a08:
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x7a16);
        /* fall through */
    case 0x77a16:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t sprite = read_word(bank7, 0x7016 + read_word(ram, actor + 0x40));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x42, 6);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* Sound cue: plays 22, then 07 after 128 passes. */
    case 0x77a84:
        if (read_word(ram, 0x28c) & 1) { finish_animation(ram, actor); break; }
        world_queue_sound(ram, 0x22);
        write_word(ram, actor + 0x44, 0x80);
        write_word(ram, actor, 0x7a9e);
        /* fall through */
    case 0x77a9e:
        if (!decrement(ram, actor + 0x44)) {
            world_queue_sound(ram, 7);
            finish_animation(ram, actor);
        }
        break;

    /* Ceiling dripper and its falling drop. */
    case 0xb18cb: {
        world_acquire_resource(ram, actor, 0x4f);
        ram[actor + 0x2d] = 0x26;
        uint16_t delay = read_word(bankb, 0x18c3 + (read_word(ram, actor + 0x1e) & 6));
        write_word(ram, actor + 0x42, delay);
        write_word(ram, actor + 0x46, delay);
        write_word(ram, actor, 0x18ee);
    }
        /* fall through */
    case 0xb18ee:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor, 0x191d);
        }
        break;
    case 0xb191d: {
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        uint16_t sprite = read_word(bankb, 0x1196 + 2 * ram[actor + 0x41]);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x40);
        if (sprite != 0xffff) {
            write_word(ram, actor + 0x2a, sprite);
            queue_sprite(ram, main_rom, actor, 1);
            break;
        }
        uint16_t child = spawn_far(ram, actor, 0xb000, 0x19a3);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24) + 0x10;
        write_word(ram, child + 0x21, x);
        write_word(ram, child + 0x31, x);
        write_word(ram, child + 0x24, y);
        write_word(ram, child + 0x34, y);
        ram[child + 0x2c] = ram[actor + 0x2c];
        ram[child + 0x2d] = 0x26;
        write_word(ram, child + 0x3c, 0xf2fb);
        write_word(ram, child + 0x3a, 0xc0);
        write_word(ram, actor + 0x42, (world_random_byte(ram) & 0x3f) + read_word(ram, actor + 0x46));
        write_word(ram, actor, 0x18ee);
        break;
    }
    case 0xb19a3: {
        if (!tiles) return WORLD_HANDLER_STOP;
        /* 4372 */
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        /* 6784: sway around the 24-bit base X by the distance fallen. */
        uint16_t fallen = read_word(ram, actor + 0x24) - read_word(ram, actor + 0x34);
        unsigned phase = ((uint16_t)(fallen * 0x200u) >> 8) & 0xff;
        int32_t sway = (int32_t)(int16_t)read_word(main_rom + 0xa0000, 0x9d92 + 2 * phase) * 8;
        uint32_t low = (uint32_t)(uint16_t)sway + read_word(ram, actor + 0x30);
        write_word(ram, actor + 0x20, (uint16_t)low);
        ram[actor + 0x22] = (uint8_t)((uint32_t)sway >> 16) + ram[actor + 0x32] + (low >> 16);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x1d);
        write_word(ram, actor + 0x2a, read_word(bankb, 0x118e + 2 * (ram[actor + 0x41] & 3)));
        queue_sprite(ram, main_rom, actor, 1);
        for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
            uint16_t player = read_word(ram, slot);
            uint16_t shape = read_word(ram, player + 0x3c);
            if (shape != 0xffff && world_shape_overlap(ram, main_rom, actor, player, read_word(ram, actor + 0x3c), shape))
                write_word(ram, player + 0x33, read_word(ram, actor + 0x3a));
        }
        if (terrain_material(ram, tiles, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24)) == 3)
            write_word(ram, actor, 0x1a3d);
        break;
    }
    case 0xb1a3d:
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0x1a47);
        /* fall through */
    case 0xb1a47: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t sprite = read_word(bankb, 0x11b4 + 2 * ram[actor + 0x41]);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x40);
        if (sprite == 0xffff) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x2a, sprite);
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }

    /* Homing missile, final straight flight. */
    case 0xb2476: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || x < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        unsigned material = terrain_material(ram, tiles, x, read_word(ram, actor + 0x24));
        int dead = ((1u << (material & 15)) & 0xff02) != 0;
        if (!dead) {
            if (material == 3) return WORLD_HANDLER_STOP;   /* 6D4D splash */
            ram[actor + 0x0d] = 2;
            if (world_player_contact(ram, main_rom, actor)) dead = 1;
            else {
                ram[actor + 0x0d] = 0;
                world_projectile_contact(ram, main_rom, actor);
                dead = !read_word(ram, actor + 0x26);
            }
        }
        if (dead) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }

    /* Rising bubble that bursts on rock. */
    case 0xbd09e: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || above_playfield(ram, actor) || x < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bankb, (uint16_t)(0xcb76 + frame)));
            write_word(ram, actor + 0x44, 1);
            frame += 2;
            write_word(ram, actor + 0x40, frame >= 10 ? 0 : frame);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if ((1u << (terrain_material(ram, tiles, x, read_word(ram, actor + 0x24) - 0x10) & 15)) & 0xff02) {
            loud_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x411b:
        write_word(ram, 0xb716, 0);
        write_word(ram, 0xb718, 0);
        break;

    /* Spawner of the B000:C7F8 enemies (shares 7DBAA with the other spawners). */
    case 0x7dd95: {
        unsigned flags = ram[actor + 0x1f];
        unsigned schedule = read_word(bank7, 0xdb7a + ((flags >> 1) & 14));
        unsigned kind = read_word(bank7, 0xdd95 + 13);
        write_word(ram, actor + 0x60, read_word(bank7, 0xdd95 + 3));
        write_word(ram, actor + 0x62, 0xb000);
        write_word(ram, actor + 0x0e, kind);
        write_word(ram, actor + 0x44, (flags & 32) | read_word(bank7, 0xdb8a + kind));
        write_word(ram, actor + 0x42, read_word(bank7, 0xdb42 + ((flags << 1) & 6)));
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x52, read_word(bank7, 0xdb4a + schedule));
        write_word(ram, actor + 0x54, read_word(bank7, 0xdb4c + schedule));
        write_word(ram, actor + 0x56, read_word(bank7, 0xdb4c + schedule));
        write_word(ram, actor + 0x58, read_word(bank7, 0xdb4e + schedule));
        write_word(ram, actor + 0x5a, read_word(bank7, 0xdb4e + schedule));
        write_word(ram, actor + 0x48, read_word(ram, 0xb700) + 16 * ram[actor + 0x1e]);
        write_word(ram, actor, 0xdc27);
        break;
    }

    /* Gate across the passage: holds the players back until B8BC clears. */
    case 0x7704a:
        ram[actor + 0x2c] = 0x0f;
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x2a, 0x85fd);
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor, 0x7062);
        break;
    case 0x77062:
    case 0x7707d:
    case 0x770b7:
        if (address == 0x77062) {
            scroll_actor(ram, actor, 0xb71a);
            if (read_word(ram, 0xb700) >= 0x1000) {
                write_word(ram, 0x26e, read_word(ram, 0x26e) | 0x8000);
                write_word(ram, actor, 0x707d);
            }
        } else if (address == 0x7707d) {
            if (read_word(ram, 0xb8bc)) write_word(ram, actor + 0x44, 0x10);
            else if (!decrement(ram, actor + 0x44)) {
                write_word(ram, 0x26e, read_word(ram, 0x26e) & ~0x8000u);
                write_word(ram, actor + 0x44, 0x40);
                uint16_t child = effect_slot(ram, actor, 0x7056);   /* 7036 */
                write_word(ram, child + 0x60, 0x7024);
                write_word(ram, child + 0x62, 0x7000);
                write_word(ram, child + 0x4c, 4);
                write_word(ram, child + 0x42, 1);
                ram[child + 0x2d] = 0xe0;
                write_word(ram, actor, 0x70b7);
            }
        } else {
            scroll_actor(ram, actor, 0xb71a);
            if (!decrement(ram, actor + 0x44)) { finish_animation(ram, actor); break; }
        }
        queue_sprite(ram, main_rom, actor, 1);
        for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
            uint16_t player = read_word(ram, slot);
            int16_t gap = read_word(ram, actor + 0x21) - read_word(ram, player + 0x21);
            if (gap < 0x3c) write_word(ram, player + 0x21, read_word(ram, player + 0x21) + gap - 0x3c);
        }
        break;
    case 0x770e9:
        if (!tiles) return WORLD_HANDLER_STOP;
        draw_block_columns(ram, main_rom, tiles, 0x4000, read_word(ram, actor + 0x1e), 0x5124, 0x201);
        finish_animation(ram, actor);
        break;

    /* Palette cues. */
    case 0x7748e:
        write_word(ram, actor + 0x44, 0x10);
        write_word(ram, actor, 0x7498);
        /* fall through */
    case 0x77498:
        if (read_word(ram, actor + 0x1e) & 0x100) {
            if (read_word(ram, 0xb8bc)) { write_word(ram, actor + 0x44, 0x10); break; }
            if (decrement(ram, actor + 0x44)) break;
        }
        for (int i = 1; i >= 0; --i)
            queue_palette(ram, read_word(bank7, 0x6d98 + 2 * i) & 0x3f, read_word(bank7, 0x6d94 + 2 * i), 5);
        write_word(ram, actor + 0x44, read_word(ram, actor + 0x1e));
        write_word(ram, actor, 0x74db);
        break;
    case 0x774db:
        if (decrement(ram, actor + 0x44)) break;
        for (int i = 1; i >= 0; --i)
            write_word(ram, (uint16_t)(0x9e08 + 8 * read_word(bank7, 0x6d98 + 2 * i)), 0);
        finish_animation(ram, actor);
        break;
    case 0x7750d: {
        uint16_t flags = read_word(ram, actor + 0x1e);
        if ((flags & 0x100) && !(read_word(ram, 0xb8c2) & 1)) break;
        uint16_t count = ((flags & 1) + 1) << 1;
        write_word(ram, actor + 0x44, count);
        do {
            uint16_t entry = (uint16_t)(2 * (count - 1) + ((flags & 1) << 2));
            queue_palette(ram, read_word(bank7, 0x6d98 + entry) & 0x3f, read_word(bank7, 0x6d88 + entry), 6);
            count = decrement(ram, actor + 0x44);
        } while (count);
        finish_animation(ram, actor);
        break;
    }
    default: {
        int result = boss_handlers(ram, main_rom, tiles, actor, address);
        if (result == WORLD_HANDLER_UNKNOWN) result = swimmer_handlers(ram, main_rom, tiles, actor, address);
        if (result == WORLD_HANDLER_UNKNOWN) result = serpent_handlers(ram, main_rom, actor, address);
        if (result == WORLD_HANDLER_UNKNOWN) result = cave_handlers(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_DONE) return result;
        break;
    }
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
