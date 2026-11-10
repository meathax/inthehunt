#include "world_dispatch_internal.h"
#include "world_enemy_target.h"
#include "world_stage.h"
#include "world_title.h"
#include "world_torpedo.h"
#include "world_tile_address.h"
#include <stdlib.h>
#include <string.h>

uint16_t read_word(const uint8_t *ram, uint16_t offset)
{
    return ram[offset] | ((uint16_t)ram[(uint16_t)(offset + 1)] << 8);
}

void write_word(uint8_t *ram, uint16_t offset, uint16_t value)
{
    ram[offset] = (uint8_t)value;
    ram[(uint16_t)(offset + 1)] = (uint8_t)(value >> 8);
}

void swap_bytes(uint8_t *ram, uint16_t offset)
{
    uint8_t low = ram[offset];
    ram[offset] = ram[offset + 1];
    ram[offset + 1] = low;
}

void world_dispatch_begin(WorldDispatch *pass, uint8_t *ram)
{
    write_word(ram, 0xb8a2, 0);
    write_word(ram, 0xa6e2, 0);
    write_word(ram, 0xaee6, 0);
    pass->actor = 0x0400;
    pass->phase = WORLD_FRAME_HEAD;
}

int world_dispatch_target(const WorldDispatch *pass, const uint8_t *ram, uint32_t *address)
{
    uint16_t ip = read_word(ram, pass->actor);
    uint16_t cs = read_word(ram, pass->actor + 2);
    *address = (((uint32_t)cs << 4) + ip) & 0xfffff;
    return *address != 0x096a;
}

void world_dispatch_return(WorldDispatch *pass, uint8_t *ram, uint16_t actor)
{
    if ((pass->phase == WORLD_PLAYER_ONE || pass->phase == WORLD_PLAYER_TWO)
        && read_word(ram, 0x84) < 6) {
        write_word(ram, actor + 0x30, 0);
        write_word(ram, actor + 0x33, 0);
    }
    write_word(ram, 0x8500 + read_word(ram, actor + 8), actor);
    pass->actor = read_word(ram, actor + 4);
    switch (pass->phase) {
    case WORLD_FRAME_HEAD:
        swap_bytes(ram, 0xb8a4);
        pass->phase = WORLD_PLAYER_ONE;
        break;
    case WORLD_PLAYER_ONE:
        pass->phase = WORLD_BEFORE_PLAYER_TWO;
        break;
    case WORLD_BEFORE_PLAYER_TWO:
        if (pass->actor == read_word(ram, 0x92)) {
            swap_bytes(ram, 0xb73e);
            swap_bytes(ram, 0xb742);
            swap_bytes(ram, 0xb74c);
            swap_bytes(ram, 0xb8a4);
            pass->phase = WORLD_PLAYER_TWO;
        }
        break;
    case WORLD_PLAYER_TWO:
        pass->phase = WORLD_AFTER_PLAYER_TWO;
        break;
    case WORLD_AFTER_PLAYER_TWO:
        break;
    }
}

void submit_sprite(uint8_t *ram, uint16_t actor, uint16_t sprite, uint16_t segment, uint8_t palette)
{
    uint16_t entry = read_word(ram, 0x9e04);
    uint16_t list = 0x9c00 + 2 * (ram[actor + 0x2d] & 0x3f);
    ram[entry + 8] = palette;
    write_word(ram, entry, read_word(ram, list));
    write_word(ram, list, entry);
    write_word(ram, entry + 4, read_word(ram, actor + 0x21));
    write_word(ram, entry + 6, read_word(ram, actor + 0x24));
    write_word(ram, entry + 2, sprite);
    ram[entry + 9] = ram[actor + 0x29];
    write_word(ram, entry + 10, segment);
    write_word(ram, 0x9e04, entry + 16);
}

/* Submit one sprite descriptor to its priority list within the frame budget. */
int queue_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, int flash)
{
    uint16_t sprite = read_word(ram, actor + 0x2a);
    uint16_t segment = (sprite & 1) ? 0x9000 : 0x8000;
    uint16_t cost = read_word(rom + ((uint32_t)segment << 4), sprite);
    uint16_t budget = read_word(ram, 0x9e02);

    if ((uint16_t)(cost + 6) >= budget) {
        write_word(ram, 0x026a, read_word(ram, 0x026a) | 0x8000);
        return 0;
    }
    write_word(ram, 0x9e02, budget - cost);

    uint8_t palette = ram[actor + 0x2c];
    if (flash && ram[actor + 0x19] && (--ram[actor + 0x19] & 1))
        palette = 0x0c;
    submit_sprite(ram, actor, sprite, segment, palette);
    return 1;
}

uint16_t decrement(uint8_t *ram, uint16_t address)
{
    uint16_t value = read_word(ram, address) - 1;
    write_word(ram, address, value);
    return value;
}

void finish_animation(uint8_t *ram, uint16_t actor)
{
    write_word(ram, actor, 0x1b4c);
    write_word(ram, actor + 2, 0);
    ram[actor + 0x0a] = 1;
    write_word(ram, actor + 0x3c, 0xffff);
}

void scroll_actor(uint8_t *ram, uint16_t actor, uint16_t delta)
{
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, delta));
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, delta + 2));
}

void update_attached_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    if (handler != 0xa318) {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
        uint16_t dy = handler == 0xa28e ? read_word(rom, 0x9b9f + read_word(ram, parent + 0x56))
                                      : read_word(ram, actor + 0x34);
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + dy);
        if (!decrement(ram, actor + 0x42))
            write_word(ram, actor, 0xa318);
    } else {
        scroll_actor(ram, actor, 0xb716);
    }
    if (!decrement(ram, actor + 0x58)) {
        uint16_t frame = read_word(ram, actor + 0x40);
        uint16_t index = frame + read_word(ram, actor + 0x4c);
        uint16_t sprite = read_word(rom, 0x9a1f + index);
        if (sprite == 0xffff) {
            finish_animation(ram, actor);
            return;
        }
        write_word(ram, actor + 0x2a, sprite);
        write_word(ram, actor + 0x58, read_word(rom, 0x9a35 + index));
        write_word(ram, actor + 0x40, frame + 2);
    }
    queue_sprite(ram, rom, actor, 1);
}

void move_actor_axis(uint8_t *ram, uint16_t position, uint16_t velocity)
{
    unsigned fraction = ram[position] + (uint8_t)velocity;
    ram[position] = (uint8_t)fraction;
    write_word(ram, position + 1, read_word(ram, position + 1) + (int8_t)(velocity >> 8) + (fraction >> 8));
}

void update_intro_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    const uint8_t *bank = rom + 0xa0000;
    scroll_actor(ram, actor, 0xb71a);
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    if (handler == 0xfd29)
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
    uint16_t x = read_word(ram, actor + 0x21);
    if (handler == 0xfdb1 ? (int16_t)x < -32 : x < 0x40 || x > 0x1c0) {
        finish_animation(ram, actor);
        return;
    }
    if (!decrement(ram, actor + 0x44)) {
        unsigned frame = read_word(ram, actor + 0x40);
        uint16_t sprite, duration;
        if (handler == 0xfc67) {
            sprite = read_word(bank, 0xf158 + (frame & 6));
            duration = read_word(ram, actor + 0x42);
        } else if (handler == 0xfd29) {
            sprite = read_word(bank, 0xf168 + (frame & 14));
            duration = 6;
        } else {
            sprite = read_word(bank, 0xf188 + (frame & 14) + read_word(ram, actor + 0x4c));
            duration = 16;
        }
        write_word(ram, actor + 0x2a, sprite);
        write_word(ram, actor + 0x44, duration);
        write_word(ram, actor + 0x40, frame + 2);
    }
    queue_sprite(ram, rom, actor, 0);
}

void animate_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t offset;
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, 0xb716));
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb718));
    if (!decrement(ram, actor + 0x42)) {
        if (!decrement(ram, actor + 0x58)) {
            finish_animation(ram, actor);
            return;
        }
        offset = read_word(ram, actor + 0x40) + read_word(ram, actor + 0x56);
        write_word(ram, actor + 0x2a, read_word(rom, 0x9a73 + offset));
        write_word(ram, actor + 0x42, read_word(rom, 0x9af3 + offset));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
    }
    queue_sprite(ram, rom, actor, 0);
}

void release_actor_resources(uint8_t *ram, uint16_t actor)
{
    uint16_t cursor;
    unsigned count = ram[actor + 0x18];
    while (count) {
        unsigned slot = ram[actor + 0x10 + --count];
        unsigned entry = 0xa006 + 2 * slot;
        if (--ram[entry + 1] == 0) {
            unsigned resource = ram[entry];
            ram[entry] = 0;
            cursor = read_word(ram, 0xa488);
            ram[0xa406 + cursor] = resource;
            write_word(ram, 0xa488, (cursor + 1) & 63);
            write_word(ram, 0xa48a, read_word(ram, 0xa48a) + 1);
            write_word(ram, 0x9e06 + 8 * resource, 0);
        }
    }
    ram[actor + 0x18] = 0;
}

void retire_actor(uint8_t *ram, uint16_t actor)
{
    uint16_t next = read_word(ram, actor + 4);
    uint16_t previous = read_word(ram, actor + 6);
    release_actor_resources(ram, actor);
    uint16_t cursor = read_word(ram, 0x8b02);
    write_word(ram, 0x8900 + cursor, actor);
    write_word(ram, 0x8b02, (cursor + 2) & 0x1fe);
    write_word(ram, previous + 4, next);
    write_word(ram, next + 6, previous);
    write_word(ram, 0x8b04, read_word(ram, 0x8b04) + 1);
    write_word(ram, actor, 0xffff);
}

uint8_t vector_angle(const uint8_t *rom, int16_t dx, int16_t dy)
{
    unsigned x = abs((int)dx), y = abs((int)dy);
    unsigned angle = dx < 0 ? 128 : 0;
    if (x || y) {
        unsigned octant = y > x ? rom[0xd58e + 256 * x / y] : rom[0xd48d + 256 * y / x];
        angle += (dx < 0) != (dy < 0) ? -octant : octant;
    }
    return angle;
}

void apply_radial_impulse(uint8_t *ram, const uint8_t *rom, uint16_t actor,
                                 uint16_t player, unsigned hit_bit)
{
    if (read_word(ram, actor + 0x50) & hit_bit)
        return;
    int16_t dx = read_word(ram, actor + 0x21) - read_word(ram, player + 0x21);
    int16_t dy = read_word(ram, actor + 0x24) - read_word(ram, player + 0x24);
    unsigned x = dx < 0 ? -(int)dx : dx;
    unsigned y = dy < 0 ? -(int)dy : dy;
    uint16_t distance = x + y;
    if (distance >= read_word(ram, actor + 0x56))
        return;

    unsigned angle = vector_angle(rom, dx, dy);
    uint16_t offset = ((angle + 4) & 0xf8) / 2 + (distance & 0xfff0) * 8
                      + read_word(ram, actor + 0x58);
    const uint8_t *table = rom + 0xa0000;
    write_word(ram, player + 0x5a, read_word(ram, player + 0x5a) + read_word(table, 0xa012 + offset));
    write_word(ram, player + 0x5e, read_word(ram, player + 0x5e) + read_word(table, 0xa014 + offset));
    write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) | hit_bit);
}

void update_radial_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, int attached)
{
    if (attached) {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        if (!decrement(ram, actor + 0x42))
            write_word(ram, actor, 0x6fb1);
    } else {
        scroll_actor(ram, actor, 0xb726);
    }
    if (!--ram[actor + 0x48]) {
        uint16_t frame = read_word(ram, actor + 0x40);
        uint16_t sprite = read_word(rom, read_word(ram, actor + 0x4c) + 2 * frame);
        write_word(ram, actor + 0x40, frame + 1);
        if (sprite == 0xffff) {
            finish_animation(ram, actor);
            return;
        }
        write_word(ram, actor + 0x2a, sprite);
        ram[actor + 0x48] = rom[(uint16_t)(read_word(ram, actor + 0x4a) + frame)];
    }
    queue_sprite(ram, rom, actor, 0);
    if (read_word(ram, actor + 0x64)) {
        decrement(ram, actor + 0x64);
        return;
    }
    if (read_word(ram, actor + 0x66)) {
        decrement(ram, actor + 0x66);
        apply_radial_impulse(ram, rom, actor, read_word(ram, 0x90), 1);
    }
    apply_radial_impulse(ram, rom, actor, read_word(ram, 0x92), 2);
}

/* Tile RAM starts at physical D0000. The far layer replaces tile numbers
   while the near layer copies both numbers and attributes. */
void update_scenery(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
                           uint16_t actor, int near_layer)
{
    scroll_actor(ram, actor, near_layer ? 0xb71e : 0xb72a);
    uint16_t bottom = read_word(ram, 0x26e) & 2 ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
    if (read_word(ram, actor + 0x21) < (near_layer ? 0x3f : 0x41)
        || (!near_layer && read_word(ram, actor + 0x24) > bottom)) {
        finish_animation(ram, actor);
        return;
    }
    uint16_t tick = read_word(ram, 0x274);
    if (tick & 7)
        return;
    uint16_t sequence = read_word(ram, actor + 0x52);
    sequence += near_layer ? 0x1de0 + (tick & 0x18) : read_word(ram, actor + 0x54) + (tick & 0x38);
    uint16_t dest = read_word(ram, actor + 0x50);
    uint16_t base = near_layer ? 0x8000 : 0x4000;
    write_word(ram, 0xb89a, 0x0401);
    write_word(ram, 0xb89c, dest);
    for (unsigned strip = 0; strip < 4; strip++) {
        unsigned segment = read_word(rom + 0xa0000, sequence + 2 * strip) & 0xfffc;
        const uint8_t *source = rom + ((uint32_t)segment << 4);
        uint16_t row_dest = dest;
        for (unsigned row = 0; row < (near_layer ? 2u : 4u); row++) {
            for (unsigned col = 0; col < 16; col += near_layer ? 2 : 4)
                write_word(tiles, base + row_dest + col, read_word(source, 16 * row + col));
            row_dest = (row_dest + 0x100) & 0x3fff;
        }
        if (strip != 3) {
            dest = (dest & 0xff00) | ((dest + 16) & 0xff);
            write_word(ram, 0xb89c, dest);
        }
    }
}


int above_cruiser(const uint8_t *ram, uint16_t enemy, uint16_t player)
{
    uint16_t y = read_word(ram, enemy + 0x24);
    uint16_t target_y = read_word(ram, player + 0x24);
    int dx = (int16_t)(read_word(ram, enemy + 0x21) - read_word(ram, player + 0x21));
    return y > 0x90 && dx > -32 && dx < 32 &&
           (uint16_t)(y + 0x30) < target_y && target_y < (uint16_t)(y + 0x80);
}

void fire_cruiser_weapon(uint8_t *ram, const uint8_t *rom, uint16_t actor, int vertical)
{
    uint8_t facing = ram[actor + 0x29];
    unsigned offset = (vertical ? 0x8d7c : 0x8d78) + 2 * (facing & 1);
    uint16_t dx = read_word(rom + 0xb0000, offset);
    uint16_t shot = effect_slot(ram, actor, vertical ? 0x98e4 : 0x97ee);
    if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
    ram[shot + 0x29] = facing;
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + dx);
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + (vertical ? 4 : 0));
    write_word(ram, shot + (vertical ? 0x3a : 0x38), read_word(ram, actor + (vertical ? 0x62 : 0x60)));
    uint16_t flash = effect_slot(ram, actor, vertical ? 0xa2c0 : 0xa268);
    ram[flash + 0x29] = facing;
    write_word(ram, flash + 0x31, dx);
    ram[flash + 0x2d] = 3;
    if (vertical) write_word(ram, flash + 0x34, 4);
    world_queue_sound(ram, vertical ? 0x7c : 0x71);
}

void queue_surface_enemy(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    write_word(ram, actor + 0x2a, read_word(ram, actor + 0x4c));
    queue_sprite(ram, rom, actor, 1);
    uint16_t index = read_word(ram, actor + 0x48) + 2;
    if ((int16_t)index >= 6) index = 0;
    write_word(ram, actor + 0x48, index);
    index += read_word(ram, actor + 0x4a);
    write_word(ram, actor + 0x2a, read_word(rom + 0xb0000, 0x2790 + index));
    queue_sprite(ram, rom, actor, 0);
}

/* 6C78 is used both on initial destruction and on the rising mine's timer. */
void surface_enemy_death_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t blast = effect_slot(ram, actor, 0x6efe);
    write_word(ram, blast + 0x4c, read_word(rom, 0x6c86));
    write_word(ram, blast + 0x4a, read_word(rom, 0x6c8b));
    write_word(ram, blast + 0x44, 0x5d);
    ram[blast + 0x2d] = 0x20;
    write_word(ram, blast + 0x56, 0x30);
    write_word(ram, blast + 0x58, 0x80);
    world_queue_sound(ram, 0x3f);
}

void register_surface_enemy(uint8_t *ram, uint16_t actor)
{
    if (read_word(ram, 0xb6ea) < 16) {
        uint16_t cursor = read_word(ram, 0xb6ea);
        write_word(ram, 0xb6ee + cursor, actor);
        write_word(ram, 0xb6ea, cursor + 2);
    }
}


void surface_enemy_hit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 0;
    uint16_t hit = world_surface_contact(ram, rom, actor);
    if (!read_word(ram, actor + 0x26)) {
        ram[actor + 0x0b] = ram[hit + 0x0b];
        world_award_score(ram, rom, actor, hit);
        unsigned mask = 1u << (ram[actor + 0x1e] & 15);
        write_word(ram, 0xc4, read_word(ram, 0xc4) & ~mask);
        write_word(ram, actor, 0x2eee);
    }
}

void begin_linear_script(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t kind = read_word(bank, offset);
    write_word(ram, actor + 0x7a, kind);
    write_word(ram, actor + 0x7c, read_word(bank, offset + 2));
    if (!kind) {
        write_word(ram, actor + 0x38, 0);
        write_word(ram, actor + 0x3a, 0);
        write_word(ram, actor + 0x7e, offset + 4);
        return;
    }
    if (kind == 6) {
        write_word(ram, actor + 0x1c, read_word(bank, offset + 4));
        write_word(ram, actor + 0x1a, read_word(bank, offset + 6));
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21) + read_word(bank, offset + 8));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24) + read_word(bank, offset + 10));
        write_word(ram, actor + 0x78, read_word(bank, offset + 12));
        write_word(ram, actor + 0x76, 0);
        write_word(ram, actor + 0x7e, offset + 14);
        ram[actor + 0x20] = ram[actor + 0x23] = 0;
        return;
    }
    ram[actor + 0x28] = bank[offset + 4];
    uint16_t speed = read_word(bank, offset + 6);
    if (speed) write_word(ram, actor + 0x2e, speed);
    unsigned angle = ram[actor + 0x28] * 2;
    for (unsigned axis = 0; axis < 2; ++axis) {
        int16_t component = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2 * axis, (component * (int16_t)read_word(ram, actor + 0x2e)) >> 8);
        int16_t acceleration = kind == 4 ? read_word(bank, offset + 8) : 0;
        write_word(ram, actor + 0x78 - 2 * axis, (component * acceleration) >> 8);
    }
    write_word(ram, actor + 0x7e, offset + (kind == 4 ? 10 : 8));
    ram[actor + 0x20] = ram[actor + 0x23] = 0;
}

int step_flight_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t kind = read_word(ram, actor + 0x7a);
    uint16_t next = read_word(ram, actor + 0x7e);
    uint16_t next_kind = read_word(bank, next);
    if ((kind & 1) || kind > 6 || (read_word(ram, actor + 0x7c) == 1 &&
        next_kind != 0xffff && ((next_kind & 1) || next_kind > 6))) return 0;
    if (!kind) {
        scroll_actor(ram, actor, 0xb71a);
    } else if (kind != 6) {
        scroll_actor(ram, actor, 0xb71a);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t speed = read_word(ram, actor + 0x38 + 2*i) + read_word(ram, actor + 0x78 - 2*i);
            write_word(ram, actor + 0x38 + 2*i, speed);
            move_actor_axis(ram, actor + 0x20 + 3*i, speed);
        }
    } else {
        uint16_t phase = read_word(ram, actor + 0x1a) + read_word(ram, actor + 0x78);
        uint16_t radius = read_word(ram, actor + 0x1c) + read_word(ram, actor + 0x76);
        write_word(ram, actor + 0x1a, phase);
        write_word(ram, actor + 0x1c, radius);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t pos = actor + 0x20 + 3*i, center = actor + 0x30 + 3*i;
            write_word(ram, center + 1, read_word(ram, center + 1) + read_word(ram, 0xb71a + 2*i));
            int16_t unit = read_word(rom + 0xa0000, (i ? 0x9d92 : 0x9e12) + 2*(phase >> 8));
            uint32_t coord = ram[center] + 256u*read_word(ram, center + 1) + unit*(int16_t)radius;
            ram[pos] = coord;
            write_word(ram, pos + 1, coord >> 8);
        }
        ram[actor + 0x28] = (phase >> 8) + ((int16_t)read_word(ram, actor + 0x78) < 0 ? -64 : 64);
    }
    if (!decrement(ram, actor + 0x7c)) {
        if (next_kind != 0xffff) begin_linear_script(ram, rom, actor, next);
        else return -1;
    }
    return 1;
}

void update_cruiser_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t burst)
{
    if (!decrement(ram, actor + burst)) {
        if (decrement(ram, actor + 0x5a))
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x58));
        else {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            write_word(ram, actor + 0x5a, read_word(ram, actor + 0x5c));
        }
        fire_cruiser_weapon(ram, rom, actor, 0);
    }
    if (read_word(ram, actor + 0x74))
        decrement(ram, actor + 0x74);
    else if (above_cruiser(ram, actor, read_word(ram, 0x90)) ||
             above_cruiser(ram, actor, read_word(ram, 0x92))) {
        fire_cruiser_weapon(ram, rom, actor, 1);
        write_word(ram, actor + 0x74, read_word(ram, actor + 0x5e));
    }
}

void draw_cruiser_and_check_hits(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned pose = read_word(ram, actor + 0x4e) & 14;
    write_word(ram, actor + 0x2a, read_word(rom + 0xb0000, 0x8cd4 + pose));
    queue_sprite(ram, rom, actor, 1);
    if (!decrement(ram, actor + 0x48)) {
        write_word(ram, actor + 0x48, 8);
        uint16_t wake = effect_slot(ram, read_word(ram, 0x9a), 0x7f73);
        uint8_t facing = ram[actor + 0x29] ^ 1;
        ram[wake + 0x29] = facing;
        uint16_t dx = read_word(rom + 0xb0000, 0x902e + 2 * (facing & 1));
        write_word(ram, wake + 0x21, read_word(ram, actor + 0x21) + dx);
        write_word(ram, wake + 0x24, read_word(ram, actor + 0x24));
    }
    write_word(ram, actor + 0x3c, 0xf723);
    ram[actor + 0x0d] = 0;
    uint16_t attacker = world_player_contact(ram, rom, actor);
    if (read_word(ram, actor + 0x26))
        attacker = world_projectile_contact(ram, rom, actor);
    if (!read_word(ram, actor + 0x26))
        world_kind11_death(ram, rom, actor, attacker);
}

/* The cruising branch. Check branches still handled by the guest before
   changing state so the caller can resume at the original handler entry. */
int update_cruising_enemy(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                 uint16_t actor)
{
    const int turns = read_word(ram, actor) == 0x9601;
    unsigned probe_direction = 0;
    uint16_t burst = read_word(ram, actor + 0x50) ? 0x50 : 0x54;
    unsigned players = read_word(ram, 0x264) & 3;
    if (read_word(ram, actor + 0x46) || (players == 3 && read_word(ram, actor + 0x4a) == 1 &&
        !world_enemy_target_mode_supported(ram[actor + 0x0c])) ||
        !read_word(ram, actor + 0x26))
        return 0;
    for (unsigned i = 0; i < read_word(ram, 0xa6e2); i += 2) {
        uint16_t shot = read_word(ram, 0xa6e6 + i);
        if (shot && read_word(ram, shot + 14) == 0x121)
            return 0;
    }
    uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
    x += read_word(rom + 0xb0000, 0x8d58 + 2 * (ram[actor + 0x29] & 1));
    uint16_t y = read_word(ram, actor + 0x24) + read_word(ram, 0xb71c);
    if (turns && !players && ((ram[actor + 0x64] & 1) || read_word(ram, actor + 0x66) != 1)) {
        unsigned turn = read_word(ram, actor + 0x6e);
        int32_t fixed_x = ((int32_t)read_word(ram, actor + 0x21) + (int16_t)read_word(ram, 0xb71a)) * 256
                        + ram[actor + 0x20] + (int16_t)read_word(ram, actor + 0x38);
        int16_t px = (int16_t)((uint32_t)fixed_x >> 8);
        if ((turn == 1 && (read_word(ram, actor + 0x70) == 1 || px >= 0x160)) ||
            (turn > 1 && px <= (int16_t)read_word(ram, actor + 0x6c)))
            return 0;
    }
    for (int row = -1; row <= 1; row++)
        if (world_terrain_mask(ram, terrain, x, y + 8 * row) & 0xff02)
            return 0;

    scroll_actor(ram, actor, 0xb71a);
    if (read_word(ram, actor + 0x4e) && !decrement(ram, actor + 0x4e))
        write_word(ram, actor + 0x3a, 0);
    if (!turns && !read_word(ram, actor + 0x4a)) {
        if (!decrement(ram, actor + 0x4c)) {
            write_word(ram, actor + 0x4a, 48);
            write_word(ram, actor + 0x38, (uint16_t)-192);
            write_word(ram, actor + 0x3a, 0);
            ram[actor + 0x64] &= 0xfe;
        }
    } else if (!turns && !decrement(ram, actor + 0x4a)) {
        write_word(ram, actor + 0x4c, 32);
        if (players) {
            uint16_t player = world_enemy_target(ram, actor);
            uint16_t target_y = read_word(ram, player + 0x24) - 24;
            ram[actor + 0x64] |= 1;
            write_word(ram, actor + 0x38, (uint16_t)-128);
            write_word(ram, actor + 0x3a, target_y < read_word(ram, actor + 0x24) ? (uint16_t)-128 : 128);
        }
    }
    if (!(ram[actor + 0x64] & 1) && !decrement(ram, actor + 0x66)) {
        write_word(ram, actor + 0x66, read_word(ram, actor + 0x68));
        write_word(ram, actor + 0x3c, 0xf72b);
        ram[actor + 0x6a] = ram[actor + 0x29];
        ram[actor + 0x0d] = 3;
        uint16_t shot = world_projectile_contact(ram, rom, actor);
        probe_direction = read_word(ram, actor + 0x3c) & 1;
        ram[actor + 0x29] = ram[actor + 0x6a];
        uint16_t type = shot ? read_word(ram, shot + 0x0e) : 0;
        if (shot && !(type & 0x200) && type != 0x120) {
            probe_direction = 0;
            uint16_t dy = read_word(ram, actor + 0x24) - read_word(ram, shot + 0x24);
            write_word(ram, actor + 0x3a, read_word(rom + 0xb0000, 0x8d6c + ((dy >> 14) & 2)));
            write_word(ram, actor + 0x4e, 16);
        }
    }
    world_kind11_move(ram, rom, terrain, actor);
    if (read_word(ram, actor + 0x21) < 0x40) {
        finish_animation(ram, actor);
        return 1;
    }
    if (turns && read_word(ram, actor + 0x6e)) {
        unsigned facing = 2 * (ram[actor + 0x29] & 1);
        uint16_t px = read_word(ram, actor + 0x21);
        uint16_t py = read_word(ram, actor + 0x24);
        uint16_t sensor = px + read_word(rom + 0xb0000, 0x8d5c + facing);
        int wall = !!(world_terrain_mask(ram, terrain, sensor, py) & 0xff02);
        int change = wall;
        if (!wall) {
            if (read_word(ram, actor + 0x6e) == 1)
                change = !decrement(ram, actor + 0x70) || (int16_t)px >= 0x160;
            else
                change = (int16_t)px <= (int16_t)read_word(ram, actor + 0x6c);
        }
        if (change) {
            if (!wall) {
                ram[actor + 0x0c] = 12;
                uint16_t target = world_enemy_target(ram, actor);
                /* With neither player active 65B1 leaves IX from the projectile probe. */
                unsigned above = players ? read_word(ram, target + 0x24) < py : probe_direction;
                write_word(ram, actor + 0x3a, read_word(rom + 0xb0000, 0x8d6c + 2 * above));
            }
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x4e, 0);
            write_word(ram, actor + 0x2a, read_word(rom + 0xb0000, 0x96cb));
            ram[actor + 0x6a] = ram[actor + 0x29];
            write_word(ram, actor, 0x9742);
            decrement(ram, actor + 0x6e);
        }
    }
    update_cruiser_fire(ram, rom, actor, burst);
    draw_cruiser_and_check_hits(ram, rom, actor);
    return 1;
}


uint16_t effect_slot(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t slot = world_spawn_child(ram, parent, handler);
    return slot ? slot : 0x8480;
}

uint16_t stage_two_machine_parent(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    if (ram[parent + 0x0a]) {
        finish_animation(ram, actor);
        return 0;
    }
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
    return parent;
}

uint16_t bank_b_effect(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t child = effect_slot(ram, parent, handler);
    if (child != 0x8480) write_word(ram, child + 2, 0xb000);
    return child;
}

int player_near_turret(const uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (!(ram[0x264] & (1u << i))) continue;
        uint16_t player = read_word(ram, 0x90 + 2*i);
        uint16_t dx = read_word(ram, actor + 0x21) + read_word(rom, 0xfb2b) - read_word(ram, player + 0x21);
        uint16_t dy = read_word(ram, actor + 0x24) + read_word(rom, 0xfb2d) - read_word(ram, player + 0x24);
        if (dx & 0x8000) dx = -dx;
        if (dy & 0x8000) dy = -dy;
        if (dx < read_word(rom, 0xfb2f) && dy < read_word(rom, 0xfb31)) return 1;
    }
    return 0;
}

int turret_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + 0xb0000;
    ram[actor + 0x0d] = 2;
    uint16_t hit = world_player_contact(ram, rom, actor);
    if (!hit) {
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
    }
    if (ram[actor + 0x0d] || !read_word(ram, actor + 0x26)) {
        world_award_score(ram, rom, actor, hit);
        world_queue_sound(ram, 0x5d);
        uint16_t blast = effect_slot(ram, actor, 0x6efe);
        write_word(ram, blast + 0x4c, read_word(rom, 0x6b44));
        write_word(ram, blast + 0x4a, read_word(rom, 0x6b49));
        write_word(ram, blast + 0x2c, 0x200a);
        write_word(ram, blast + 0x56, 48);
        write_word(ram, blast + 0x58, 128);
        if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
        ram[actor + 0x19] = 0;
        write_word(ram, actor + 0x2a, read_word(bank, 0xcf98));
        if (ram[actor + 0x29] & 2) {
            uint16_t debris = bank_b_effect(ram, actor, 0xd09e);
            write_word(ram, debris + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, debris + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, debris + 0x2c, read_word(ram, actor + 0x2c));
            write_word(ram, debris + 0x3a, 0xff00);
            write_word(ram, debris + 0x44, 1);
        }
        write_word(ram, actor, 0xd079);
        return 1;
    }
    return 0;
}

void turret_effect(uint8_t *ram, const uint8_t *bank, uint16_t actor, int opening)
{
    uint16_t child = bank_b_effect(ram, actor, 0xce66);
    unsigned side = ram[actor + 0x29] & 2;
    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + read_word(bank, (opening ? 0xcb36 : 0xcb04) + side));
    write_word(ram, child + 0x2c, read_word(ram, actor + 0x2c));
    write_word(ram, child + 0x4a, read_word(bank, opening ? 0xcdb1 : 0xce2d));
    write_word(ram, child + 0x4c, read_word(bank, opening ? 0xcdb6 : 0xce32));
    write_word(ram, child + 0x44, 1);
}

void spawn_stage_pickup(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0xea17);
    if (child != 0x8480) write_word(ram, child + 2, 0x7000);
    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
    write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
}

int outside_pickup_area(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    uint16_t base = read_word(ram, 0xb732);
    uint16_t top = ram[0x26e] & 2 ? 0x68 : base + 0x10;
    uint16_t bottom = ram[0x26e] & 2 ? 0x198 : base + 0x1c8;
    return x > 0x1c0 || x < 0x40 || y > bottom || (int16_t)y < (int16_t)top;
}

/* B7147 attaches a ship part and retires it when the parent is removed. */
int warship_attachment(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    if (ram[parent + 0x0a]) {
        finish_animation(ram, actor);
        return 0;
    }
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
    return 1;
}

void aim_warship_turret(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned facing)
{
    if (decrement(ram, actor + 0x42)) return;
    write_word(ram, actor + 0x42, 32);
    uint8_t angle = ram[actor + 0x28];
    ram[actor + 0x5f] = angle;
    if (!(ram[0x264] & 3)) return;
    uint16_t target = world_enemy_target(ram, actor);
    uint8_t desired = (vector_angle(rom, read_word(ram, target + 0x21) - read_word(ram, actor + 0x21),
                                    read_word(ram, target + 0x24) - read_word(ram, actor + 0x24)) + 8) & 0xf0;
    if (desired == angle) return;
    angle += (uint8_t)(desired - angle) < (uint8_t)(angle - desired) ? 16 : -16;
    ram[actor + 0x28] = angle;
    write_word(ram, actor + 0x40, read_word(rom + 0xb0000, 0x4fea + ((angle & 0xf0) >> 3) + 32*facing));
}

void warship_part_explosion(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    world_spawn_radial_explosion(ram, rom, actor);
}

void boss_chain_move(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    write_word(ram, actor + 0x20, read_word(ram, parent + 0x20));
    write_word(ram, actor + 0x22, read_word(ram, parent + 0x22));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
}

void boss_limb_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    uint16_t speed = read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        uint16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2*axis, ((uint32_t)unit * speed) >> 8);
    }
}

uint16_t spawn_boss_part(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t part = effect_slot(ram, actor, handler);
    if (part != 0x8480) write_word(ram, part + 2, 0x1000);
    return part;
}

void boss_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t entry)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t type = read_word(bank, entry);
    uint16_t setup = read_word(bank, 0x7a4 + type);
    if (type == 0) { world_queue_sound(ram, 0x5d); setup = 0x6b36; }
    uint16_t effect = effect_slot(ram, actor, 0x6efe);
    write_word(ram, effect + 0x4c, read_word(rom, setup + 14));
    write_word(ram, effect + 0x4a, read_word(rom, setup + 19));
    ram[effect + 0x2c] = rom[setup + 24];
    ram[effect + 0x2d] = rom[setup + 28];
    write_word(ram, effect + 0x56, read_word(rom, setup + 32));
    write_word(ram, effect + 0x58, read_word(rom, setup + 37));
    if (type == 4) world_queue_sound(ram, 0x3e);
    if (type < 16 && !(effect & 0x300)) write_word(ram, effect + 0x44, 0x5d);
    write_word(ram, effect + 0x31, read_word(ram, effect + 0x31) + read_word(bank, entry + 2));
    write_word(ram, effect + 0x34, read_word(ram, effect + 0x34) + read_word(bank, entry + 4));
}

int boss_claw_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    uint16_t owner = read_word(ram, actor + 0x50);
    switch (handler) {
    case 0x1723:
        world_acquire_resource(ram, actor, 0x69);
        world_acquire_resource(ram, actor, 0x6a);
        write_word(ram, actor + 0x0e, 0x41);
        write_word(ram, actor + 0x4a, 8);
        write_word(ram, actor, 0x1742);
        /* fall through */
    case 0x1742: {
        unsigned flags = read_word(ram, owner + 0x60);
        if (flags & 2) {
            ram[actor + 0x28] = ram[owner + 0x56];
            write_word(ram, actor + 0x2e, 0x800);
            boss_limb_velocity(ram, rom, actor);
            write_word(ram, parent + 0x62, read_word(ram, parent + 0x62) | 2);
            write_word(ram, parent + 0x5a, read_word(ram, actor + 0x38));
            write_word(ram, parent + 0x5c, read_word(ram, actor + 0x3a));
            ram[parent + 0x56] = ram[actor + 0x28];
            write_word(ram, actor + 0x58, 4);
            uint16_t effect = spawn_boss_part(ram, actor, 0x1b41);
            write_word(ram, effect + 0x50, owner);
            write_word(ram, actor, 0x18a3);
        } else if (!(read_word(ram, actor + 0x52) & 2) && (flags & 1)) {
            ram[actor + 0x28] = 0x80;
            write_word(ram, actor + 0x2e, 0x400);
            boss_limb_velocity(ram, rom, actor);
            write_word(ram, actor + 0x58, 4);
            uint16_t effect = spawn_boss_part(ram, actor, 0x1b41);
            write_word(ram, effect + 0x50, owner);
            write_word(ram, actor, 0x17da);
        } else return 1;
        break;
    }
    case 0x17da:
        write_word(ram, actor + 0x2e, 0x800);
        boss_limb_velocity(ram, rom, actor);
        write_word(ram, actor + 0x42, 8);
        write_word(ram, parent + 0x62, read_word(ram, parent + 0x62) | 1);
        write_word(ram, parent + 0x5a, read_word(ram, actor + 0x38));
        write_word(ram, parent + 0x5c, read_word(ram, actor + 0x3a));
        ram[parent + 0x28] = ram[parent + 0x56] = ram[actor + 0x28];
        write_word(ram, actor, 0x1819);
        break;
    case 0x1819:
    case 0x18bd: {
        unsigned bit = handler == 0x1819 ? 1 : 4;
        if (!(ram[parent + 0x60] & bit)) {
            write_word(ram, actor, 0x1742);
            return 1;
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 2);
            uint8_t desired = (vector_angle(rom, read_word(ram, owner + 0x31) - read_word(ram, actor + 0x21),
                                             read_word(ram, owner + 0x34) - read_word(ram, actor + 0x24)) + 1) & 0xfe;
            uint8_t angle = ram[actor + 0x28];
            if (desired != angle) {
                unsigned step = ram[actor + 0x58];
                angle += (uint8_t)(desired - angle) < (uint8_t)(angle - desired) ? step : -step;
                ram[actor + 0x28] = angle;
                boss_limb_velocity(ram, rom, actor);
                write_word(ram, parent + 0x5a, read_word(ram, actor + 0x38));
                write_word(ram, parent + 0x5c, read_word(ram, actor + 0x3a));
                ram[parent + 0x56] = angle;
            }
        }
        break;
    }
    case 0x18a3:
        if (!(ram[owner + 0x60] & 2)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor, 0x18bd);
        }
        break;
    case 0x19f9:
    case 0x1a09: {
        if (handler == 0x19f9) {
            uint16_t speed = read_word(ram, actor + 0x38) + 16;
            write_word(ram, actor + 0x38, speed);
            if (speed >= 0x200) write_word(ram, actor, 0x1a09);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        if (read_word(ram, actor + 0x21) > 0x1c0) {
            finish_animation(ram, actor);
            return 1;
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned direction = (ram[actor + 0x28] & 0xf8) >> 3;
            ram[actor + 0x29] = bank[0x608 + direction];
            unsigned sprite = read_word(bank, 0x48e + ((direction * 2) & 30));
            write_word(ram, actor + 0x2a, read_word(bank, 0x454 + sprite));
            ram[actor + 0x28] += 8;
            write_word(ram, actor + 0x44, 4);
        }
        queue_sprite(ram, rom, actor, 0);
        return 1;
    }
    case 0x1a63: {
        uint16_t body = read_word(ram, owner + 0x50);
        if (ram[body + 0x0a] & 1) { finish_animation(ram, actor); return 1; }
        write_word(ram, actor + 0x21, read_word(ram, body + 0x21) + read_word(ram, actor + 0x31));
        write_word(ram, actor + 0x24, read_word(ram, body + 0x24) + read_word(ram, actor + 0x34));
        queue_sprite(ram, rom, actor, 0);
        unsigned timer = read_word(ram, actor + 0x42);
        if (!timer) {
            if (ram[body + 0x66] & 1)
                write_word(ram, actor + 0x42, (read_word(ram, actor + 0x52) << 5) + 32);
        } else if (!decrement(ram, actor + 0x42)) {
            uint16_t effect = effect_slot(ram, actor, 0x6efe);
            write_word(ram, effect + 0x4c, read_word(rom, 0x6b9f));
            write_word(ram, effect + 0x4a, read_word(rom, 0x6ba4));
            ram[effect + 0x2c] = rom[0x6ba9];
            ram[effect + 0x2d] = rom[0x6bad];
            write_word(ram, effect + 0x56, read_word(rom, 0x6bb1));
            write_word(ram, effect + 0x58, read_word(rom, 0x6bb6));
            world_queue_sound(ram, 0x3e);
            finish_animation(ram, actor);
        }
        return 1;
    }
    case 0x1adc:
        world_acquire_resource(ram, actor, 0x6c);
        ram[actor + 0x2d] = 0x30;
        ram[actor + 0x44] = 1;
        write_word(ram, actor, 0x1af1);
        /* fall through */
    case 0x1af1: {
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (ram[parent + 0x0a] & 1) { finish_animation(ram, actor); return 1; }
        if (!--ram[actor + 0x44]) {
            unsigned frame = read_word(ram, actor + 0x40) & 15;
            if (!(frame & 1)) write_word(ram, actor + 0x2a, read_word(bank, 0x5a8 + frame));
            ram[actor + 0x44] = bank[0x5b8 + frame];
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
        }
        if (ram[actor + 0x40] & 1) queue_sprite(ram, rom, actor, 1);
        return 1;
    }
    case 0x1b41:
        world_acquire_resource(ram, actor, 0x6b);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x1b57);
        /* fall through */
    case 0x1b57: {
        if (!(ram[owner + 0x60] & 1) || (ram[parent + 0x0a] & 2)) {
            finish_animation(ram, actor);
            return 1;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        ram[actor + 0x29] = ram[parent + 0x29];
        write_word(ram, actor + 0x46, read_word(ram, parent + 0x46));
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x44, read_word(bank, 0x48a + frame));
            write_word(ram, actor + 0x40, frame ^ 2);
        }
        unsigned sprite = 2 * read_word(ram, actor + 0x46) + read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(bank, 0x466 + sprite));
        queue_sprite(ram, rom, actor, 0);
        return 1;
    }
    default: return 0;
    }
    boss_chain_move(ram, actor);
    unsigned direction = ((uint8_t)(ram[actor + 0x28] + 4) & 0xf8) >> 3;
    ram[actor + 0x29] = bank[0x608 + direction];
    unsigned sprite = read_word(bank, 0x48e + ((direction * 2) & 30));
    write_word(ram, actor + 0x2a, read_word(bank, 0x454 + sprite));
    write_word(ram, actor + 0x46, sprite);
    ram[actor + 0x2c] = ram[actor + 0x14 + ((ram[0x274] & 2) >> 1)];
    queue_sprite(ram, rom, actor, 0);
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = 1;
    uint16_t hit = world_projectile_contact(ram, rom, actor);
    if (hit && (ram[hit + 0x0b] & 3)) {
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        else {
            ram[actor + 0x19] = 32;
            if (read_word(ram, actor + 0x4a)) {
                write_word(ram, actor + 0x5e, hit);
                if (!decrement(ram, actor + 0x4a)) write_word(ram, actor + 0x58, 8);
                write_word(ram, owner + 0x26, read_word(ram, actor + 0x4a));
            }
        }
    }
    if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
    if (ram[owner + 0x0a] & 2) {
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x38, 0);
        ram[actor + 0x2d] = 0x1f;
        write_word(ram, actor, 0x19f9);
        return 1;
    }
    uint16_t body = read_word(ram, owner + 0x50);
    if (world_shape_overlap(ram, rom, actor, body, read_word(ram, actor + 0x3c), 0xf773)) {
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21) - read_word(ram, body + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24) - read_word(ram, body + 0x24));
        ram[owner + 0x0a] |= 2;
        write_word(ram, actor + 0x2a, read_word(bank, 0x5a4 + ((ram[actor + 0x28] & 128) ? 2 : 0)));
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x2f;
        ram[actor + 0x29] = 0;
        write_word(ram, actor + 0x42, 0);
        ram[actor + 0x0a] |= 2;
        warship_part_explosion(ram, rom, actor);
        world_award_score(ram, rom, actor, read_word(ram, actor + 0x5e));
        spawn_boss_part(ram, actor, 0x1adc);
        write_word(ram, actor, 0x1a63);
    }
    return 1;
}

void boss_appendage(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    unsigned index = read_word(ram, actor + 0x52);
    switch (handler) {
    case 0x108c:
        if (!(ram[actor + 0x0a] & 2)) {
            if (read_word(ram, parent + 0x62) & 0x100) {
                write_word(ram, actor + 0x4e, read_word(ram, parent + 0x4e));
                write_word(ram, actor + 0x42, 65);
                write_word(ram, actor + 0x58, read_word(bank, 0x418 + 2*(index & 3)));
                write_word(ram, actor, 0x1213);
            } else if (!(index & 2) && (read_word(ram, parent + 0x62) & 16)) {
                write_word(ram, actor + 0x42, 65);
                write_word(ram, actor + 0x58, read_word(bank, 0x410 + 2*(index & 1)));
                write_word(ram, actor, 0x10f8);
            }
        }
        break;
    case 0x10f8:
    case 0x11f6:
    case 0x1213:
    case 0x133a: {
        uint16_t timer = decrement(ram, actor + 0x42);
        unsigned mask = handler == 0x1213 || handler == 0x133a ? 7 : 15;
        if (timer) {
            if (!(timer & mask))
                write_word(ram, actor + 0x56, read_word(ram, actor + 0x56) + read_word(ram, actor + 0x58));
        } else if (handler == 0x10f8) {
            write_word(ram, actor + 0x58, -read_word(ram, actor + 0x58));
            write_word(ram, actor + 0x42, 8);
            write_word(ram, actor + 0x4c, 18);
            write_word(ram, actor, 0x1122);
        } else if (handler == 0x1213) {
            write_word(ram, actor + 0x42, read_word(bank, 0x420 + 2*(index & 3)));
            write_word(ram, actor, 0x1241);
        } else {
            write_word(ram, actor, 0x108c);
        }
        break;
    }
    case 0x1122:
    case 0x1258:
        if (!decrement(ram, actor + 0x42)) {
            unsigned bits = handler == 0x1122 ? 1 : 6;
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) | bits);
            write_word(ram, actor + 0x4c, 0);
            write_word(ram, actor + 0x26, 1);
            uint16_t effect = spawn_boss_part(ram, actor, 0x1436);
            write_word(ram, effect + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, effect + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, actor, handler == 0x1122 ? 0x1155 : 0x1290);
        }
        break;
    case 0x1155:
        if (read_word(ram, parent + 0x62) & 16) {
            if (!read_word(ram, actor + 0x26)) {
                write_word(ram, actor + 0x31, read_word(ram, actor + 0x21) + 32);
                write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
            } else {
                uint16_t target = read_word(ram, parent + 0x4e);
                if (!target) {
                    write_word(ram, actor + 0x31, 0xb0);
                    write_word(ram, actor + 0x34, 0x100);
                } else {
                    uint8_t angle = vector_angle(rom, read_word(ram, target + 0x21) - read_word(ram, actor + 0x21),
                                                 read_word(ram, target + 0x24) - read_word(ram, actor + 0x24));
                    ram[actor + 0x28] = angle;
                    for (unsigned axis = 0; axis < 2; ++axis) {
                        uint16_t component = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2*angle);
                        uint16_t speed = ((uint32_t)component * read_word(ram, actor + 0x2e)) >> 8;
                        write_word(ram, actor + 0x38 + 2*axis, speed);
                        write_word(ram, actor + 0x31 + 3*axis, read_word(ram, actor + 0x21 + 3*axis));
                        move_actor_axis(ram, actor + 0x30 + 3*axis, speed);
                    }
                    write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(bank, 0x8de + 2*(index & 1)));
                }
            }
        } else {
            write_word(ram, actor + 0x42, 80);
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) & ~1u);
            write_word(ram, actor + 0x56, 128);
            write_word(ram, actor, 0x11f6);
        }
        break;
    case 0x1241:
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 8);
            write_word(ram, actor + 0x4c, 18);
            write_word(ram, actor, 0x1258);
        }
        break;
    case 0x1290:
        if (!(read_word(ram, parent + 0x62) & 0x100)) {
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) & ~2u);
            ram[actor + 0x28] = ram[actor + 0x56];
            write_word(ram, actor + 0x58, read_word(bank, 0x410 + 2*(index & 3)));
            write_word(ram, actor + 0x42, 65);
            write_word(ram, actor, 0x12ce);
        }
        break;
    case 0x12ce:
        if (read_word(ram, actor + 0x42) && !(decrement(ram, actor + 0x42) & 15)) {
            unsigned angle = 2*ram[actor + 0x28];
            for (unsigned axis = 0; axis < 2; ++axis) {
                uint16_t component = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
                uint16_t speed = ((uint32_t)component * read_word(ram, actor + 0x2e)) >> 8;
                write_word(ram, actor + 0x38 + 2*axis, speed);
                write_word(ram, actor + 0x31 + 3*axis, read_word(ram, actor + 0x21 + 3*axis));
                move_actor_axis(ram, actor + 0x30 + 3*axis, speed);
            }
            ram[actor + 0x28] += ram[actor + 0x58];
        }
        if (!(read_word(ram, parent + 0x62) & 0x200)) {
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) & ~4u);
            write_word(ram, actor + 0x42, 72);
            write_word(ram, actor + 0x58, -read_word(bank, 0x418 + 2*(index & 3)));
            write_word(ram, actor, 0x133a);
        }
        break;
    }
    if (handler != 0x138b) {
        if (ram[actor + 0x0a] & 2)
            write_word(ram, parent + 0x60, read_word(ram, parent + 0x60) & ~(1u << (index & 15)));
        if (ram[0x262] & 128) {
            ram[actor + 0x2c] = ram[actor + 0x14];
            ram[actor + 0x0a] |= 2;
            write_word(ram, actor, 0x138b);
        }
    } else if (ram[parent + 0x0a] & 1) {
        finish_animation(ram, actor);
        return;
    }
    uint16_t phase = read_word(ram, actor + 0x56);
    uint16_t offset = (index >> 1) | (index << 15);
    offset = (offset >> 8) | (offset << 8);
    offset += phase >> 1;
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(bank, 0x200 + offset));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(bank, 0x202 + offset));
    if (!(ram[0x262] & 128)) {
        if (ram[parent + 0x19]) ram[actor + 0x19] = ram[parent + 0x19] + 1;
        if (ram[actor + 0x19]) {
            unsigned color = (--ram[actor + 0x19]) & 1;
            ram[actor + 0x2c] = ram[actor + 0x14 + color];
        }
    }
    ram[actor + 0x29] = bank[0x608 + (phase >> 3)];
    unsigned sprite = read_word(bank, 0x48e + 2*(phase >> 3)) + read_word(ram, actor + 0x4c);
    write_word(ram, actor + 0x2a, read_word(bank, 0x430 + sprite));
    queue_sprite(ram, rom, actor, 0);
}

void warship_blast_sequence(uint8_t *ram, uint16_t actor, uint16_t table, uint16_t delay)
{
    uint16_t child = effect_slot(ram, actor, 0x7056);
    write_word(ram, child + 0x60, table);
    write_word(ram, child + 0x62, 0xb000);
    write_word(ram, child + 0x4c, delay);
    write_word(ram, child + 0x42, 1);
}

int spawn_warship_parts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + 0xb0000;
    unsigned facing = ram[actor + 0x29];
    for (unsigned group = 0; group < 2; ++group) {
        for (int i = 3; i >= 0; --i) {
            uint16_t child = world_spawn_child(ram, actor, group ? 0x59ea : 0x5b93);
            if (!child) return 0;
            write_word(ram, child + 2, 0xb000);
            ram[child + 0x2d] = 0x28;
            write_word(ram, child + 0x36, 0x80);
            uint16_t dx;
            if (!group) {
                ram[child + 0x2c] = ram[actor + 0x2c];
                ram[child + 0x29] = facing;
                write_word(ram, child + 0x5a, read_word(ram, actor + 0x5a));
                write_word(ram, child + 0x26, 0x400);
                write_word(ram, child + 0x3c, read_word(bank, 0x6e1e));
                write_word(ram, child + 0x34, 16);
                write_word(ram, child + 0x60, 8*i);
                dx = read_word(bank, 0x4fc0 + 2*i);
            } else {
                ram[child + 0x5e] = facing;
                ram[child + 0xc] = 4;
                ram[child + 0x28] = 0xc0;
                write_word(ram, child + 0x50, read_word(ram, actor + 0x50));
                write_word(ram, child + 0x52, read_word(ram, actor + 0x50));
                write_word(ram, child + 0x58, read_word(ram, actor + 0x58));
                write_word(ram, child + 0x3c, read_word(bank, 0x6e7a));
                write_word(ram, child + 0x26, 0x290);
                write_word(ram, child + 0x60, 4*i);
                write_word(ram, child + 0x34, read_word(bank, 0x4de4 + 4*i));
                dx = read_word(bank, 0x4de2 + 4*i);
            }
            write_word(ram, child + 0x31, facing ? -dx : dx);
        }
    }
    /* Operand locations in the eight unrolled part initializers. */
    static const uint16_t sprite_sites[] = {0x6ed0,0x6f21,0,0x6fac,0x7003,0x705a,0x70b6,0x710d};
    static const uint16_t handlers[] = {0x5c73,0x5c73,0x5ce6,0x6744,0x6744,0x6781,0x64b6,0x64b6};
    for (unsigned i = 0; i < 8; ++i) {
        uint16_t child = world_spawn_child(ram, actor, handlers[i]);
        if (!child) return 0;
        write_word(ram, child + 2, 0xb000);
        ram[child + 0x29] = facing;
        if (i == 2) {
            ram[child + 0x16] = ram[actor + 0x14];
            ram[child + 0x15] = ram[actor + 0x15];
            write_word(ram, child + 0x48, read_word(ram, actor + 0x26));
            write_word(ram, child + 0x26, read_word(ram, actor + 0x4a));
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
            continue;
        }
        uint16_t site = sprite_sites[i];
        ram[child + 0x2c] = ram[actor + 0x2c];
        ram[child + 0x2d] = 0x28;
        write_word(ram, child + 0x2a, read_word(bank, site));
        write_word(ram, child + 0x3c, read_word(bank, site + 9));
        if (i > 2) write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        uint16_t dx = read_word(bank, site + (i < 2 ? 12 : 22));
        write_word(ram, child + 0x31, facing ? -dx : dx);
        write_word(ram, child + 0x34, i < 2 ? 64 : i == 5 ? -8 : -16);
        write_word(ram, child + 0x26, i < 2 ? 0x460 : read_word(bank, site + 14));
        write_word(ram, child + 0x36, i < 2 ? 0x80 : read_word(bank, site + 19));
        write_word(ram, child + 0xe, 0x2a);
        write_word(ram, child + 0x60, i == 1 || i == 7 ? 2 : i == 4 ? 8 : i == 5 ? 16 : 0);
        if (i == 5) write_word(ram, child + 0x44, 1);
    }
    return 1;
}

int advance_timed_sprite(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t table)
{
    if (--ram[actor + 0x44]) return 1;
    uint16_t step = read_word(ram, actor + 0x40) + 4;
    if (bank[(uint16_t)(table + step)] == 0xff) return 0;
    if (!bank[(uint16_t)(table + step)]) step = 0;
    write_word(ram, actor + 0x40, step);
    write_word(ram, actor + 0x44, read_word(bank, table + step));
    write_word(ram, actor + 0x2a, read_word(bank, table + step + 2));
    return 1;
}

void spawn_bomb_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t splash = effect_slot(ram, actor, 0x6f54);
    uint16_t y = read_word(ram, actor + 0x24);
    unsigned scroll = read_word(ram, 0xb70c) & 7;
    write_word(ram, splash + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, splash + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
    ram[splash + 0x23] = 0;
    write_word(ram, splash + 0x4c, read_word(rom, 0x6cec));
    write_word(ram, splash + 0x4a, read_word(rom, 0x6cf1));
    write_word(ram, splash + 0x44, 11);
    ram[splash + 0x2d] = 0x20;
    write_word(ram, splash + 0x56, 48);
    write_word(ram, splash + 0x58, 128);
}

uint16_t mission_child(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = effect_slot(ram, actor, handler);
    if (child != 0x8480) write_word(ram, child + 2, 0xa000);
    return child;
}

void mission_text(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    uint16_t stage = read_word(ram, 0x84);
    if (read_word(ram, actor + 0x46) != 1) {
        uint16_t lower = read_word(bank, 0x8f60 + 2 * stage);
        for (unsigned i = 0; i < 40; i += 2) {
            uint16_t shape = read_word(bank, 0x8f38 + i);
            if (shape == 2) shape = read_word(bank, 0x9066 + 2 * (stage + 1));
            write_word(ram, 0xb9f8 + i, shape);
            write_word(ram, 0xba20 + i, read_word(bank, lower + i));
        }
    } else {
        for (unsigned i = 0; i < 80; i += 2) write_word(ram, 0xb9f8 + i, 0);
        unsigned value = read_word(ram, 0xba48);
        write_word(ram, 0xba00, read_word(bank, 0x9066 + 2 * (value / 10)));
        write_word(ram, 0xba02, read_word(bank, 0x9066 + 2 * (value % 10)));
        write_word(ram, 0xba04, 1);
        uint16_t source = read_word(bank, 0x907e + 2 * (read_word(ram, 0xba4a) - 1));
        for (unsigned i = 0; i < 6; i += 2)
            write_word(ram, 0xba06 + i, read_word(bank, source + i));
        for (unsigned i = 0; i < 6; ++i)
            write_word(ram, 0xba0c + 2 * i, read_word(bank, 0x96b4 + 6 * i));
        source = read_word(bank, 0x90cc + 2 * stage);
        for (unsigned i = 0; i < 16; i += 2)
            write_word(ram, 0xba2e + i, read_word(bank, source + i));
    }
}

void start_mission_scroll(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    write_word(ram, actor + 0x21, 32);
    write_word(ram, actor + 0x40, 0);
    write_word(ram, actor + 0x44, 1);
    write_word(ram, actor + 0x50, 0);
    write_word(ram, actor + 0x52, 16);
    write_word(ram, actor + 0x54, 21);
    write_word(ram, actor + 0x42, 232);
    write_word(ram, actor + 0x48, 16);
    write_word(ram, actor + 0x56, 0);
    write_word(ram, actor + 0x5a, 0);
    uint16_t child = mission_child(ram, actor, 0x944b);
    write_word(ram, child + 0x42, 1);
    write_word(ram, child + 0x40, 0xfffe);
    mission_text(ram, bank, actor);
    world_queue_sound(ram, 0x30);
    write_word(ram, actor, 0x923c);
}

void update_mission_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    int stopped = read_word(ram, 0x262) & 0x8000;
    if (!stopped) {
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        int16_t scroll = -24;
        if (read_word(ram, actor + 0x42) >= 86) {
            scroll = read_word(ram, actor + 0x5a) - 4;
            write_word(ram, actor + 0x5a, scroll);
            if (ram[0x274] & 1) scroll += 8;
        }
        write_word(ram, 0xb70e, scroll < -536 ? -536 : scroll);
    }
    if (!decrement(ram, actor + 0x44)) {
        uint16_t phase = read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(rom + 0xa0000, 0x8ef4 + (phase & 6)));
        write_word(ram, actor + 0x44, 6);
        write_word(ram, actor + 0x40, phase + 2);
    }
    write_word(ram, actor + 0x24, 0x140);
    queue_sprite(ram, rom, actor, 0);
    write_word(ram, actor + 0x24, 0xc0);
    queue_sprite(ram, rom, actor, 0);
    if (stopped) return;
    if (!decrement(ram, actor + 0x52) && read_word(ram, actor + 0x54)) {
        decrement(ram, actor + 0x54);
        uint16_t child = mission_child(ram, actor, 0x9496);
        uint16_t index = read_word(ram, actor + 0x50);
        write_word(ram, child + 0x50, index);
        write_word(ram, actor + 0x52, 4);
        write_word(ram, actor + 0x50, index + 1);
    }
    uint16_t column = read_word(ram, actor + 0x56);
    if (column < 20 && !decrement(ram, actor + 0x48)) {
        write_word(ram, actor + 0x48, 4);
        uint16_t top = mission_child(ram, actor, 0x937d);
        write_word(ram, top + 0x56, column);
        write_word(ram, top + 0x24, 0x128);
        write_word(ram, top + 0x1e, 8);
        uint16_t bottom = mission_child(ram, actor, 0x937d);
        write_word(ram, bottom + 0x56, column);
        write_word(ram, bottom + 0x24, 0x100);
        write_word(ram, actor + 0x56, column + 1);
    }
    if (!decrement(ram, actor + 0x42)) {
        if (decrement(ram, actor + 0x46)) write_word(ram, actor, 0x91e5);
        else finish_animation(ram, actor);
    }
}

/* 18A9 builds the initial stage list by inserting each child after the head. */
void init_stage_list(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    static const struct {
        uint16_t handler_operand, destination;
        uint8_t player;
    } calls[] = {
        {0x18ad, 0x9a, 0}, {0x18bc, 0, 1}, {0x18cb, 0, 2},
        {0x18da, 0x98, 0}, {0x18e9, 0x9c, 0}, {0x18f8, 0, 0},
        {0x1903, 0, 0}, {0x190e, 0, 0}, {0x1919, 0, 0},
        {0x1924, 0x92, 2}, {0x1937, 0x90, 1}
    };
    for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; ++i) {
        uint16_t operand = calls[i].handler_operand;
        uint16_t slot = effect_slot(ram, actor, read_word(rom, operand));
        if (slot != 0x8480) write_word(ram, slot + 2, read_word(rom, operand - 3));
        if (calls[i].destination) write_word(ram, calls[i].destination, slot);
        if (calls[i].player) ram[slot + 0x0b] = calls[i].player;
    }
    write_word(ram, actor, 0x1950);
    write_word(ram, actor + 2, 0);
}

void init_player_slot(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned player)
{
    uint16_t config = player == 1 ? 0x100 : 0x140;
    uint8_t priority = (uint8_t)(player * 8);
    ram[actor + 0x2d] = priority;
    write_word(ram, actor + 0x3c, 0xffff);
    ram[actor + 0x0a] = 1;
    ram[actor + 0x14] = 9;
    ram[actor + 0x15] = (uint8_t)player;
    write_word(ram, actor + 0x70, config);
    for (unsigned i = 0; i < 2; ++i) {
        uint16_t slot = effect_slot(ram, actor, read_word(rom, i ? 0x7a2e : 0x7a1b));
        ram[slot + 0x0b] = (uint8_t)player;
        ram[slot + 0x2d] = priority;
    }
    write_word(ram, config + 0x20, 0);
    write_word(ram, config + 0x22, 0);
}

void set_projectile_trail_length(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                        uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a);
    uint16_t y = 0x177 - (read_word(ram, actor + 0x24) + 0x18) + read_word(ram, 0xb70c);
    unsigned cell = world_tile_address(x, y);
    unsigned rows = 0;
    while (rows < 3 && (terrain[cell + 3] >> 3) == 3) {
        ++rows;
        cell = (cell + 0x100) & 0x3fff;
    }
    const uint16_t duration_offsets[] = {0xa3a5, 0xa3ab, 0xa3b1, 0xa39f};
    write_word(ram, actor + 0x58, read_word(rom, duration_offsets[rows]));
}

void init_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                 uint16_t actor)
{
    world_acquire_resource(ram, actor, read_word(rom, 0x9bb1 + (ram[actor + 0x0b] & 2)));
    write_word(ram, actor + 0x3c, read_word(rom, 0x9bfb));
    uint16_t velocity = read_word(rom, 0x9c00);
    write_word(ram, actor + 0x38, ram[actor + 0x29] & 1 ? -velocity : velocity);
    write_word(ram, actor + 0x26, read_word(rom, 0x9c0f));
    write_word(ram, actor + 0x36, read_word(rom, 0x9c14));
    write_word(ram, actor + 0x0e, read_word(rom, 0x9c19));
    write_word(ram, actor + 0x42, read_word(rom, 0x9c1e));
    write_word(ram, actor + 0x5a, 2);
    write_word(ram, actor + 0x5c, 1);
    set_projectile_trail_length(ram, rom, terrain, actor);
    write_word(ram, actor, 0x9c33);
}

void cycle_torpedo_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor,
                                 uint16_t table, uint16_t delay)
{
    if (!decrement(ram, actor + 0x5c)) {
        uint16_t frame = read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(rom, table + frame));
        write_word(ram, actor + 0x5c, delay);
        frame += 2;
        write_word(ram, actor + 0x40, frame < 10 ? frame : 0);
    }
}

void spawn_impact(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    write_word(ram, child + 0x4c, read_word(rom, 0x6ad9));
    write_word(ram, child + 0x4a, read_word(rom, 0x6ade));
    ram[child + 0x2c] = rom[0x6ae3];
    ram[child + 0x2d] = rom[0x6ae7];
    write_word(ram, child + 0x56, read_word(rom, 0x6aeb));
    write_word(ram, child + 0x58, read_word(rom, 0x6af0));
}

void update_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                    uint16_t actor, int accelerating)
{
    Torpedo shot;
    shot.x = read_word(ram, actor + 0x21);
    shot.y = read_word(ram, actor + 0x24);
    shot.velocity = read_word(ram, actor + 0x38);
    shot.timer = accelerating ? read_word(ram, actor + 0x42) : 0;
    shot.facing = ram[actor + 0x29];
    shot.fraction = ram[actor + 0x20];
    world_fly_torpedo(&shot, rom, read_word(ram, 0xb716), read_word(ram, 0xb718));
    if (accelerating) {
        write_word(ram, actor + 0x38, shot.velocity);
        write_word(ram, actor + 0x42, shot.timer);
        if (!shot.timer) {
            uint16_t child = effect_slot(ram, actor, 0x9dab);
            ram[child + 0x2d] = ram[actor + 0x2d];
            ram[child + 0x29] = shot.facing;
            write_word(ram, actor, 0x9c82);
        }
    }
    int impact = !read_word(ram, actor + 0x26);
    if (!impact) {
        ram[actor + 0x20] = shot.fraction;
        write_word(ram, actor + 0x21, shot.x);
        write_word(ram, actor + 0x24, shot.y);
        impact = (world_terrain_mask(ram, terrain, shot.x, shot.y) & 0x5502) != 0;
        if (impact) {
            world_stage5_wall_hit(ram, rom, terrain, actor);
            write_word(ram, actor + 0x40, 0);
        }
    }
    if (impact || shot.x < 0x40 || shot.x > 0x1c0) {
        if (impact) {
            spawn_impact(ram, rom, actor);
        }
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        write_word(ram, parent + 0x44, read_word(ram, parent + 0x44) + 1);
        finish_animation(ram, actor);
        return;
    }
    cycle_torpedo_sprite(ram, rom, actor, 0x9999, 4);
    queue_sprite(ram, rom, actor, 1);
    if (!decrement(ram, actor + 0x5a)) {
        write_word(ram, actor + 0x5a, 2);
        uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
        write_word(ram, trail + 0x21, shot.x);
        write_word(ram, trail + 0x24, shot.y);
        write_word(ram, trail + 0x52, read_word(ram, actor + 0x52));
        write_word(ram, trail + 0x58, read_word(ram, actor + 0x58));
        ram[trail + 0x2c] = 3;
        write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) ^ 1);
    }
    if (read_word(ram, actor + 0x3c) != 0xffff)
        world_append_candidate(ram + 0xa6e2, actor);
}

int update_powered_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                  uint16_t actor, uint32_t handler)
{
    const uint8_t *bank = rom + 0xa0000;
    if (handler == 0xa804c) {
        unsigned side = 2 * (ram[actor + 0x29] & 1);
        unsigned level = 2 * read_word(ram, actor + 0x76);
        world_acquire_resource(ram, actor, read_word(bank, 0x8048 + (ram[actor + 0x0b] & 2)));
        write_word(ram, actor + 0x3c, read_word(bank, 0x802e + level));
        write_word(ram, actor + 0x26, read_word(bank, 0x8034 + level));
        write_word(ram, actor + 0x36, read_word(bank, 0x803a + level));
        write_word(ram, actor + 0x4c, read_word(bank, 0x801e + level));
        write_word(ram, actor + 0x38, read_word(bank, 0x8040 + side));
        write_word(ram, actor + 0x5c, read_word(bank, 0x8044 + side));
        write_word(ram, actor + 0x5e, 1);
        write_word(ram, actor + 0x42, 16);
        write_word(ram, actor + 0x5a, 5);
        set_projectile_trail_length(ram, rom, terrain, actor);
        write_word(ram, actor, 0x80b5);
        return 1;
    }
    if (handler == 0xa80b5) {
        unsigned time = decrement(ram, actor + 0x42);
        if (!(time & 7)) {
            uint16_t acceleration = read_word(ram, actor + 0x5c);
            uint16_t velocity = read_word(ram, actor + 0x38) + acceleration;
            if (!time) {
                velocity += (int16_t)acceleration >> 1;
                uint16_t wake = effect_slot(ram, actor, 0x9dab);
                ram[wake + 0x2d] = ram[actor + 0x2d];
                write_word(ram, actor, 0x80ea);
            }
            write_word(ram, actor + 0x38, velocity);
        }
    }
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    if (!read_word(ram, actor + 0x26)) {
        spawn_impact(ram, rom, actor);
        write_word(ram, parent + 0x44, read_word(ram, parent + 0x44) + 1);
        finish_animation(ram, actor);
        return 1;
    }
    scroll_actor(ram, actor, 0xb716);
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                      0x177 - y + read_word(ram, 0xb70c));
    unsigned material = terrain[cell + 3] >> 3;
    int solid = !!(0x5502 & (1u << (material & 15)));
    if (solid && read_word(ram, 0x84) == 4 && material == 1) return 0;
    if (solid || x > 0x1c0 || x < 0x40) {
        if (solid) {
            spawn_impact(ram, rom, actor);
            write_word(ram, actor + 0x40, 0);
        }
        write_word(ram, parent + 0x44, read_word(ram, parent + 0x44) + 1);
        finish_animation(ram, actor);
        return 1;
    }
    if (!decrement(ram, actor + 0x5e)) {
        unsigned frame = read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x4c) - 0x8000)));
        write_word(ram, actor + 0x5e, read_word(bank, 0x8024 + frame));
        write_word(ram, actor + 0x40, frame + 2 < 10 ? frame + 2 : 0);
    }
    queue_sprite(ram, rom, actor, 0);
    if (!decrement(ram, actor + 0x5a)) {
        write_word(ram, actor + 0x5a, 2);
        uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
        write_word(ram, trail + 0x21, x);
        write_word(ram, trail + 0x24, y);
        write_word(ram, trail + 0x52, read_word(ram, actor + 0x52));
        write_word(ram, trail + 0x58, read_word(ram, actor + 0x58));
        ram[trail + 0x2c] = 3;
        write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) ^ 1);
    }
    if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
    return 1;
}

void update_sub_display(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t sub = read_word(ram, actor + 0x40);
    for (unsigned axis = 0; axis < 2; axis++) {
        unsigned pos = 0x20 + 3 * axis;
        uint16_t delta = read_word(ram, sub + 0x30 + 3 * axis);
        unsigned fraction = ram[sub + pos] + (delta & 0xff);
        uint16_t pixels = read_word(ram, sub + pos + 1) + (int8_t)(delta >> 8)
                        + (fraction >> 8) + read_word(ram, 0xb71a + 2 * axis);
        ram[sub + pos] = ram[actor + pos] = (uint8_t)fraction;
        write_word(ram, sub + pos + 1, pixels);
        write_word(ram, actor + pos + 1, pixels);
    }
    unsigned layers = read_word(ram, sub + 0x6c);
    if (layers & 1) {
        uint16_t sprite = read_word(ram, sub + 0x2a);
        write_word(ram, actor + 0x2a, sprite);
        ram[actor + 0x29] = ram[sub + 0x29];
        uint8_t palette = ram[sub + 0x15];
        uint8_t priority = rom[0x79f9 + ((ram[actor + 0x0b] & 2) >> 1)];
        if (ram[sub + 0x0a]) {
            palette |= 0x80;
            priority = 1;
        }
        ram[actor + 0x2c] = palette;
        ram[actor + 0x2d] = priority;
        submit_sprite(ram, actor, sprite, sprite & 1 ? 0x9000 : 0x8000, palette);
        if (layers & 2) {
            write_word(ram, actor + 0x2a, read_word(ram, sub + 0x6a));
            ram[actor + 0x2c] = ram[sub + 0x14];
            queue_sprite(ram, rom, actor, 0);
        }
    }
    unsigned request = read_word(ram, sub + 0x7a);
    if (request & 3) {
        uint16_t child = effect_slot(ram, actor, request & 1 ? 0x8154 : 0x815c);
        ram[child + 0x0b] = ram[actor + 0x0b];
        write_word(ram, sub + 0x7a, 0);
    }
}

void fire_secondary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned input = ram[0xb73e] & 3;
    uint8_t angle = ram[actor + 0x28];
    if (!input) angle += (angle < 0x40) - (angle > 0x40);
    else if (input & 1) angle -= angle >= 0x28;
    else angle += angle < 0x60;
    ram[actor + 0x28] = angle;
    if (read_word(ram, actor + 0x4a)) {
        decrement(ram, actor + 0x4a);
        return;
    }
    if (read_word(ram, actor + 0x44) || !(ram[0xb742] & 0x40)) return;
    uint16_t shot = world_spawn_child(ram, actor, read_word(rom, 0xa76c));
    if (!shot) return;
    unsigned facing = ram[actor + 0x29], side = 2 * (facing & 1);
    ram[shot + 0x29] = facing;
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + read_word(rom, 0xa99f + side));
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 4);
    ram[shot + 0x2d] = ram[actor + 0x2d];
    ram[shot + 0x0b] = ram[actor + 0x0b];
    unsigned player = ((ram[actor + 0x0b] & 3) - 1) & 15;
    write_word(ram, 0xb8a2, read_word(ram, 0xb8a2) | (1u << player));
    write_word(ram, shot + 0x60, read_word(ram, actor + 0x60));
    ram[shot + 0x28] = (angle + 4) & 0xf8;
    write_word(ram, shot + 0x7a, read_word(ram, actor + 0x7a));
    uint16_t flash = effect_slot(ram, actor, read_word(rom, 0xa7c8));
    ram[flash + 0x29] = facing;
    write_word(ram, flash + 0x31, read_word(rom, 0x78c7 + side));
    write_word(ram, flash + 0x34, 4);
    ram[flash + 0x2d] = ram[actor + 0x2d];
    write_word(ram, flash + 0x60, read_word(ram, actor + 0x60));
    write_word(ram, actor + 0x44, read_word(ram, actor + 0x44) + 1);
    write_word(ram, actor + 0x4a, 32);
}

/* A480/A535/A5EA launch the three powered primary weapons. */
void fire_primary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t mode)
{
    unsigned kind = mode / 2 - 1;
    uint16_t cooldown = actor + 0x4a + 2*kind;
    uint16_t stock = actor + 0x44 + 2*kind;
    if (read_word(ram, cooldown)) {
        decrement(ram, cooldown);
        return;
    }
    if (!read_word(ram, stock) || !(ram[0xb742] & 0x80)) return;
    uint16_t handler = kind == 0 ? 0x804c : kind == 1 ? 0xabb7 : 0xaf63;
    uint16_t shot = world_spawn_child(ram, actor, handler);
    if (!shot) return;
    if (!kind) write_word(ram, shot + 2, 0xa000);
    world_queue_sound(ram, kind == 1 ? 0x34 : 0x3d);
    unsigned side = 2 * (ram[actor + 0x29] & 1);
    ram[shot + 0x29] = ram[actor + 0x29];
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + read_word(rom, 0x9ba9 + side));
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + read_word(rom, 0x9b9f + read_word(ram, actor + 0x56)));
    ram[shot + 0x0b] = ram[actor + 0x0b];
    unsigned owner = ((ram[actor + 0x0b] & 3) - 1) & 15;
    write_word(ram, 0xb8a2, read_word(ram, 0xb8a2) | (1u << owner));
    ram[shot + 0x2d] = ram[actor + 0x2d] + 1;
    write_word(ram, shot + 0x76, read_word(ram, actor + 0x76) - 1);
    uint16_t flash = effect_slot(ram, actor, 0xa268);
    ram[flash + 0x29] = ram[actor + 0x29];
    write_word(ram, flash + 0x31, read_word(rom, 0x9ba9 + side));
    ram[flash + 0x2d] = ram[actor + 0x2d];
    write_word(ram, flash + 0x52, read_word(ram, actor + 0x52));
    write_word(ram, actor + 0x52, read_word(ram, actor + 0x52) ^ 1);
    decrement(ram, stock);
    write_word(ram, cooldown, 16);
}

int update_idle_equipment(uint8_t *ram, const uint8_t *rom, uint16_t actor, int secondary)
{
    uint16_t sub = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
    uint16_t config = read_word(ram, sub + 0x70);
    if (secondary) {
        if (read_word(ram, config + 0x16) && read_word(ram, config + 0x14) != 2)
            return 0;
        write_word(ram, actor + 0x7c, sub);
        write_word(ram, actor + 0x60, read_word(ram, sub + 0x60));
    } else {
        uint16_t mode = read_word(ram, config + 0x10);
        if (mode > 6 || (mode & 1)) return 0;
        write_word(ram, actor + 0x56, read_word(ram, sub + 0x56));
    }
    ram[actor + 0x29] = ram[sub + 0x29];
    write_word(ram, actor + 0x21, read_word(ram, sub + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, sub + 0x24));
    if (!secondary) {
        uint16_t mode = read_word(ram, config + 0x10);
        if (read_word(rom, 0xa6a3 + mode) != 0xffff) {
            write_word(ram, actor + 0x74, mode);
            write_word(ram, actor + 0x76, read_word(ram, config + 0x12));
            fire_primary_launcher(ram, rom, actor, mode);
        }
    }
    if (secondary && read_word(ram, config + 0x16)) {
        write_word(ram, actor + 0x7a, read_word(ram, config + 0x16));
        write_word(ram, actor + 0x40, 2 * (int8_t)ram[sub + 0x41]);
        if (read_word(ram, actor + 0x78) != 2) {
            write_word(ram, actor + 0x78, 2);
            ram[actor + 0x28] = 0x40;
        }
        fire_secondary_launcher(ram, rom, actor);
    }
    return 1;
}

uint16_t draw_join_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label)
{
    while (rom[label] != 0xff) {
        write_word(tiles, dest, rom[label++]);
        write_word(tiles, dest + 2, 0x104);
        dest = (dest & 0xff00) | ((dest + 4) & 255);
    }
    return dest;
}

void tally_tile(uint8_t *tiles, uint16_t dest, uint16_t glyph, unsigned attr)
{
    write_word(tiles, dest, glyph);
    write_word(tiles, dest + 2, attr);
}

void tally_block(uint8_t *tiles, uint16_t dest, unsigned width, unsigned height, unsigned glyph, unsigned attr)
{
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            tally_tile(tiles, (((dest + 256*y) & 0x3f00) | ((dest + 4*x) & 255)), glyph++, attr);
}

void tally_stamp(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t shape, unsigned attr)
{
    tally_block(tiles, dest, rom[shape + 1], rom[shape], read_word(rom, shape + 2), attr);
}

void tally_large_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label, unsigned attr)
{
    for (; rom[label] != 255; ++label) {
        tally_block(tiles, dest, 2, 2, 256 + 4*(rom[label] - 48), attr);
        dest = (dest & 0xff00) | ((dest + 8) & 255);
    }
}

void tally_score(const uint8_t *ram, const uint8_t *rom, uint8_t *tiles, unsigned side)
{
    tally_large_label(tiles, rom, read_word(rom, 0x22ab + 2*side), read_word(rom, 0x2457 + 2*side), 0x105);
    uint16_t score = read_word(ram, 0x258 + 2*side) + 3, dest = read_word(rom, 0x22af + 2*side);
    for (unsigned n = 0; n < 8; ++n) {
        unsigned digit = (ram[score - n/2] >> ((n & 1) ? 0 : 4)) & 15;
        tally_block(tiles, dest, 2, 2, 256 + 4*digit, 0x106);
        dest = (dest & 0xff00) | ((dest + 8) & 255);
    }
}

void name_glyph(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    uint16_t index=read_word(ram,actor+0x40);
    unsigned glyph=(uint16_t)(256+4*(rom[(uint16_t)(0xd036+index)]-48));
    tally_block(tiles,read_word(ram,actor+0x52),2,2,glyph,0x104);
}

void finish_name_selection(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    write_word(ram,actor+0x40,0x27);
    name_glyph(ram,rom,tiles,actor);
    write_word(ram,actor+0x42,64);
    write_word(ram,actor,0xcc89);
}

void name_input(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    write_word(ram,actor+0x56,0);
    unsigned held=read_word(ram,0xb73e)&3;
    if(!held)write_word(ram,actor+0x42,1);
    else {
        write_word(ram,actor+0x56,1);
        if(!decrement(ram,actor+0x42)) {
            write_word(ram,actor+0x42,10);
            if(read_word(ram,actor+0x46)!=6) {
                uint16_t index=read_word(ram,actor+0x40)+read_word(rom,0xcea5+2*held);
                if((int16_t)index<0)index=41;
                else if(index>=42)index=0;
                write_word(ram,actor+0x40,index);
                name_glyph(ram,rom,tiles,actor);
            }
        }
    }
    unsigned buttons=read_word(ram,0xb742);
    int backspace=0;
    if(buttons&0x80) {
        write_word(ram,actor+0x56,1);
        unsigned index=read_word(ram,actor+0x40);
        if(read_word(ram,actor+0x46)==6 || index==41) {
            finish_name_selection(ram,rom,tiles,actor);
            return;
        }
        if(index==40)backspace=1;
        else {
            name_glyph(ram,rom,tiles,actor);
            uint16_t record=read_word(ram,0x258+2*(ram[actor+0x1e]&1));
            unsigned offset=read_word(ram,actor+0x46);
            ram[(uint16_t)(record+offset+8)]=rom[(uint16_t)(0xd036+index)];
            write_word(ram,actor+0x46,offset+2);
            if(offset+2==6)write_word(ram,actor+0x40,41);
            ram[actor+0x52]+=16;
            name_glyph(ram,rom,tiles,actor);
        }
    } else if(buttons&0x40) {
        write_word(ram,actor+0x56,1);
        backspace=1;
    }
    if(backspace && read_word(ram,actor+0x46)) {
        uint16_t record=read_word(ram,0x258+2*(ram[actor+0x1e]&1));
        unsigned offset=read_word(ram,actor+0x46);
        ram[(uint16_t)(record+offset+8)]=32;
        write_word(ram,actor+0x46,offset-2);
        uint16_t previous=read_word(ram,actor+0x40);
        write_word(ram,actor+0x40,39);
        name_glyph(ram,rom,tiles,actor);
        write_word(ram,actor+0x40,previous);
        ram[actor+0x52]-=16;
        name_glyph(ram,rom,tiles,actor);
    }
    if(read_word(ram,actor+0x46)!=6 && !(read_word(ram,0x274)&31)) {
        uint16_t previous=read_word(ram,actor+0x40);
        if(!(read_word(ram,0x274)&32))write_word(ram,actor+0x40,0xffff);
        name_glyph(ram,rom,tiles,actor);
        write_word(ram,actor+0x40,previous);
    }
    if(read_word(ram,actor+0x56))write_word(ram,actor+0x54,640);
    else if(!decrement(ram,actor+0x54))finish_name_selection(ram,rom,tiles,actor);
}

void tally_bonus(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    unsigned side = ram[actor + 0x1e] & 1;
    unsigned mask = (read_word(ram, actor + 0x48) >> ((read_word(ram, 0x84) - 1) & 15)) & 1;
    unsigned small = read_word(ram, actor + 0x54) + read_word(ram, actor + 0x50);
    unsigned large = read_word(ram, actor + 0x56) + read_word(ram, actor + 0x52);
    unsigned bonus = (uint16_t)(100*read_word(ram, actor + 0x50) + 500*read_word(ram, actor + 0x52) +
                                1000*mask*ram[actor + 0x4e]);
    unsigned packed = 0;
    for (unsigned i = 0; i < 5; ++i) { packed |= (bonus % 10) << (4*i); bonus /= 10; }
    write_word(ram, actor + 0x60, packed);
    write_word(ram, actor + 0x62, packed >> 16);
    unsigned carry = 0;
    uint16_t score = read_word(ram, 0x254 + 2*side) + 1;
    for (unsigned n = 0; n < 6; ++n) {
        unsigned shift = (n & 1)*4;
        unsigned sum = ((ram[score + n/2] >> shift) & 15) + ((packed >> (8 + 4*n)) & 15) + carry;
        carry = sum / 10;
        ram[score + n/2] = (ram[score + n/2] & ~(15u << shift)) | ((sum % 10) << shift);
    }
    tally_score(ram, rom, tiles, side);
    write_word(ram, actor + 0x50, 0);
    write_word(ram, actor + 0x52, 0);
    unsigned tens = read_word(ram, actor + 0x58) + small/10;
    unsigned fives = read_word(ram, actor + 0x5a) + large/5;
    unsigned groups = tens / 5 + read_word(ram, actor + 0x5c);
    write_word(ram, actor + 0x58, tens % 5);
    write_word(ram, actor + 0x5a, fives);
    write_word(ram, actor + 0x5c, groups);
    unsigned delta = side * 0x1000;
    for (unsigned type = 0; type < 2; ++type) {
        uint16_t table = type ? 0x22d7 : 0x22bf;
        uint16_t dest = read_word(rom, table) + delta;
        uint16_t empty = type ? 0x2494 : 0x2499;
        draw_join_label(tiles, rom, dest, empty);
        draw_join_label(tiles, rom, dest + (type ? 0x100 : 0x200), empty);
        unsigned digit = type ? large%5 : small%10;
        for (unsigned i = 0; i < digit; ++i) {
            dest = read_word(rom, table + 2*i) + delta;
            if (type) tally_stamp(tiles, rom, dest, 0x2478, 0x107);
            else for (uint16_t k = 0x2461; rom[k] != 255; ++k, dest += 4)
                tally_tile(tiles, dest, rom[k], 0x107);
        }
        unsigned count = type ? fives : tens;
        if (!count) continue;
        dest = read_word(rom, (type ? 0x22e1 : 0x22d3) + 2*side);
        unsigned slots = type ? 5 : 6;
        for (unsigned i = 0; i < slots; ++i) {
            uint16_t shape = type ? (i < fives ? read_word(rom, 0x247c) : 0x2484) :
                i < groups ? read_word(rom, 0x2466) : i < groups + tens%5 ? read_word(rom, 0x2464) : 0x2470;
            tally_stamp(tiles, rom, dest, shape, 0x107);
            dest += type ? 16 : 12;
        }
        write_word(ram, actor + 0x4c, 0);
    }
    write_word(ram, actor + 0x54, 0);
    write_word(ram, actor + 0x56, 0);
    tally_stamp(tiles, rom, read_word(rom, 0x22e5 + 2*side), read_word(rom, 0x2488 + 2*mask), 0x110);
    write_word(ram, actor + 0x42, 64);
    write_word(ram, 0xd2, 1);
    write_word(ram, actor, 0x2a10);
}

int tally_skip_requested(const uint8_t *ram)
{
    return ((ram[0x266] & 1) && (ram[0xb743] & 0xf0)) ||
           ((ram[0x266] & 2) && (ram[0xb742] & 0xf0));
}

void tally_add_count(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, unsigned type)
{
    unsigned side = ram[actor + 0x1e] & 1;
    uint16_t score = read_word(ram, 0x254 + 2*side) + 1;
    unsigned carry = 0;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned source = rom[0x22b3 + 4*type + i], dest = ram[score + i];
        unsigned sum = 10*((source >> 4) + (dest >> 4)) + (source & 15) + (dest & 15) + carry;
        carry = sum >= 100;
        sum %= 100;
        ram[score + i] = (sum / 10)*16 + sum % 10;
    }
    tally_score(ram, rom, tiles, side);
}

void tally_groups(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, int large)
{
    unsigned count = read_word(ram, actor + (large ? 0x5a : 0x58));
    if (!count) return;
    unsigned side = ram[actor + 0x1e] & 1;
    uint16_t dest = read_word(rom, (large ? 0x22e1 : 0x22d3) + 2*side);
    unsigned grouped = 0;
    if (!large) {
        grouped = read_word(ram, actor + 0x5c) + count / 5;
        count %= 5;
        write_word(ram, actor + 0x58, count);
        write_word(ram, actor + 0x5c, grouped);
    }
    unsigned slots = large ? 5 : 6;
    write_word(ram, actor + 0x4c, slots);
    for (unsigned i = 0; i < slots; ++i) {
        uint16_t shape;
        if (large) shape = i < count ? read_word(rom, 0x247c) : 0x2484;
        else shape = i < grouped ? read_word(rom, 0x2466) :
                     i < grouped + count ? read_word(rom, 0x2464) : 0x2470;
        tally_stamp(tiles, rom, dest, shape, 0x107);
        dest = (dest & 0xff00) | ((dest + (large ? 16 : 12)) & 255);
        write_word(ram, actor + 0x4c, read_word(ram, actor + 0x4c) - 1);
    }
}


void update_join_hud(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    unsigned owner = ram[actor + 0x0b] & 3;
    unsigned player_bit = 1u << ((owner - 1) & 1);
    uint16_t index = 2 * (owner - 1);
    unsigned credits = read_word(ram, 0x2b8);
    int free_play = ram[0x2c4] & 1;
    if (!read_word(ram, actor + 0x42)) {
        uint16_t dest = read_word(rom, 0xc25f + index);
        dest = draw_join_label(tiles, rom, dest, free_play ? 0x5ce3 : 0x5cd7);
        if (!free_play) {
            unsigned shown = credits > 99 ? 99 : credits;
            for (unsigned i = 0; i < 5; ++i) {
                unsigned digit = i == 0 ? 48 + shown / 10 : i == 1 ? 48 + shown % 10 : 0;
                write_word(tiles, dest + 4*i, digit);
                write_word(tiles, dest + 4*i + 2, 0x104);
            }
        }
    } else {
        uint16_t timer = decrement(ram, actor + 0x42);
        if (timer == 0x100) {
            draw_join_label(tiles, rom, read_word(rom, 0xc257 + (owner & 2)), 0x7900);
            ram[0x264] |= 1u << (owner + 5);
            ram[0x288] &= ~player_bit;
            unsigned shift = (ram[actor + 0x0b] >> 1) & 15;
            write_word(ram, 0x266, read_word(ram, 0x266) & ~(1u << shift));
            write_word(ram, actor + 0x6e, 0);
        } else if (timer > 0x100) {
            uint16_t remain = timer - 0x100;
            if (!(remain & 127)) {
                uint16_t dest = read_word(rom, 0x78eb + index);
                write_word(tiles, dest, 48 + (((remain >> 7) - 1) & 15));
                write_word(tiles, dest + 2, 0x104);
            } else if ((ram[0x264] & 12) && timer < 0x480 && (ram[0xb742] & 0xc0)) {
                write_word(ram, actor + 0x42, (timer & 0x780) + 1);
            }
        }
    }
    uint16_t tick = read_word(ram, 0x274);
    if (tick & 31)
        return;
    uint16_t label = 0xc2f6;
    if (tick & 32) {
        uint16_t price = read_word(ram, 0x288);
        if (!credits || ((price & 0x8000) && !(price & player_bit) && credits < 2))
            label = free_play ? 0xc2e5 : 0xc290;
        else
            label = read_word(rom, 0xc24f + index);
    }
    uint16_t dest = read_word(rom, 0xc257 + index);
    if (read_word(ram, actor + 0x42))
        dest += 0x100;
    draw_join_label(tiles, rom, dest, label);
}

void update_rank(uint8_t *ram, const uint8_t *rom)
{
    write_word(ram, 0x84a1, 0x200);
    write_word(ram, 0x84a4, 0x120);
    unsigned extra = (ram[0x264] & 3) == 3;
    uint16_t difficulty = read_word(ram, 0x27c) << 3;
    uint16_t maximum = 0;
    for (unsigned i = 0; i < 2; i++) {
        unsigned field = 0x112 + 4 * i;
        unsigned index = read_word(ram, field) + read_word(ram, field + 0x40) + extra;
        uint16_t rank = difficulty + read_word(rom, 0x14e1 + 2 * index);
        write_word(ram, 0x88 + 2 * i, rank);
        if (rank > maximum)
            maximum = rank;
    }
    write_word(ram, 0x8c, maximum);
}

void load_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t row = read_word(ram, actor + 0x40);
    uint16_t x = read_word(rom, row + 2), y = read_word(rom, row + 6);
    if (x != 0xffff)
        write_word(ram, actor + 0x42, x);
    if (y != 0xffff)
        write_word(ram, actor + 0x44, y);
    write_word(ram, actor, read_word(rom, row + 4));
    write_word(ram, actor + 0x4c, read_word(rom, row + 8));
    write_word(ram, actor + 0x64, read_word(rom, row + 10));
    write_word(ram, actor + 0x66, read_word(rom, row + 12));
    write_word(ram, actor + 0x40, row + 14);
    write_word(ram, actor + 0x46, read_word(rom, row + 14));
}

void advance_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if ((int16_t)read_word(ram, actor + 0x46) <= (int16_t)read_word(ram, 0xb700))
        load_scroll_script(ram, rom, actor);
}

void update_camera_limits(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t row = read_word(ram, actor + 0x68);
    if (read_word(rom, row) <= read_word(ram, 0xb700)) {
        for (unsigned i = 0; i < 4; i++)
            write_word(ram, 0xb734 + 2*i, read_word(rom, row + 2 + 2*i));
        ram[0x26e] = rom[(uint16_t)(row + 10)];
        write_word(ram, actor + 0x68, row + 12);
    }
}

void init_tile_layer(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned layer)
{
    static const uint16_t script_table[] = {0x4789, 0x47b5, 0x47f7};
    static const uint16_t child[] = {0x3bb8, 0x3ecf, 0x40d9};
    uint16_t stage = 2 * read_word(ram, 0x84);
    uint16_t height = read_word(rom, 0x4747 + stage);
    write_word(ram, 0xb706 + 4 * layer, 0);
    write_word(ram, 0xb708 + 4 * layer, 0);
    write_word(ram, 0xb716 + 4 * layer, 0);
    write_word(ram, 0xb718 + 4 * layer, 0);
    ram[actor + 0x51] = ram[actor + 0x55] = 0;
    write_word(ram, actor + 0x52, 0);
    write_word(ram, actor + 0x56, height);
    write_word(ram, actor + 0x62, 0);
    write_word(ram, actor + 0x60, 0xd000 + layer * 0x400);
    write_word(ram, actor + 0x6a, layer == 1 ? 32 : 64);
    if (layer == 1) {
        write_word(ram, 0x26e, 0);
        write_word(ram, 0xb700, 0);
        write_word(ram, 0xb6fe, 0);
        write_word(ram, 0xb70c, height);
        write_word(ram, 0xb732, height);
    } else {
        write_word(ram, 0xb710, height);
    }
    if (layer) write_word(ram, actor + 0x4e, read_word(rom, (layer == 1 ? 0x475d : 0x4773) + stage));
    write_word(ram, actor + 0x40, read_word(rom, script_table[layer] + stage));
    load_scroll_script(ram, rom, actor);
    if (layer == 1) {
        write_word(ram, actor + 0x68, read_word(rom, 0x47e1 + stage));
        update_camera_limits(ram, rom, actor);
    }
    world_spawn_child(ram, actor, child[layer]);
}

void update_layer_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    for (unsigned axis = 0; axis < 4; axis += 2) {
        uint16_t delta = read_word(ram, 0xb71a + axis);
        write_word(ram, 0xb716 + axis, delta);
        write_word(ram, 0xb726 + axis, delta);
    }
    advance_scroll_script(ram, rom, actor);
}

void world_draw_tile_block(uint8_t *tiles, const uint8_t *rom, uint16_t base,
                            uint16_t dest, uint16_t descriptor)
{
    const uint8_t *block = rom + ((uint32_t)(descriptor & 0xfffc) << 4);
    unsigned flip = descriptor & 3;
    for (unsigned y = 0; y < 4; y++) {
        unsigned row = flip & 2 ? 3 - y : y;
        unsigned line = (dest + row * 0x100) & 0x3fff;
        for (unsigned x = 0; x < 4; x++) {
            unsigned column = flip & 1 ? 3 - x : x;
            uint16_t cell = base + line + 4 * column;
            unsigned src = 16 * y + 4 * x;
            /* The World double-flip routine skips its third tile-number store. */
            if (flip != 3 || x != 2)
                write_word(tiles, cell, read_word(block, src));
            write_word(tiles, cell + 2, read_word(block, src + 2) ^ (flip << 9));
        }
    }
}

void update_tile_script(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
                               uint16_t actor, uint16_t base)
{
    uint16_t trigger = read_word(ram, actor + 0x46);
    if (trigger == 0xffff) {
        finish_animation(ram, actor);
        return;
    }
    if (trigger > read_word(ram, 0xb700))
        return;
    uint16_t row = read_word(ram, actor + 0x40);
    uint16_t source = read_word(rom, row + 2), origin = read_word(rom, row + 4);
    uint16_t size = read_word(rom, row + 6), mode = read_word(rom, row + 8) & 3;
    unsigned height = (uint8_t)((size & 255) - 1) + 1;
    unsigned width = (uint8_t)((size >> 8) - 1) + 1;
    write_word(ram, 0xb89a, size);
    write_word(ram, 0xb89c, origin);
    unsigned lines = mode < 2 ? width : height;
    unsigned cells = mode < 2 ? height : width;
    for (unsigned i = 0; i < lines; i++) {
        uint16_t dest = origin;
        for (unsigned j = 0; j < cells; j++) {
            world_draw_tile_block(tiles, rom, base, dest, read_word(rom + 0xa0000, source));
            source += 2;
            dest = mode < 2 ? (dest + 0x400) & 0x3fff : (dest & 0xff00) | ((dest + 16) & 255);
        }
        if (i + 1 < lines) {
            origin = mode < 2 ? (origin & 0xff00) | ((origin + 16) & 255)
                              : (origin + (mode == 2 ? 0x400 : -0x400)) & 0x3fff;
            write_word(ram, 0xb89c, origin);
        }
    }
    write_word(ram, actor + 0x40, row + 10);
    trigger = read_word(rom, row + 10);
    if (trigger == 0xffff)
        finish_animation(ram, actor);
    write_word(ram, actor + 0x46, trigger);
}

/* The palette command queue uses eight bytes per slot. */
void queue_palette(uint8_t *ram, unsigned slot, uint16_t palette, uint16_t speed)
{
    if (read_word(ram, 0x262) & 0x8800)
        return;
    uint16_t dest = 0x9e06 + 8 * slot;
    write_word(ram, dest, palette);
    write_word(ram, dest + 2, 0x80);
    write_word(ram, dest + 4, speed);
    write_word(ram, dest + 6, 0x1020);
}

/* The continue digits are 35 points transformed using the board's signed
   eight-bit products. Every intermediate rotation rounds independently. */
int digit_product(int value, int factor)
{
    return (int8_t)((uint16_t)(value * factor) >> 8);
}

void project_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (!decrement(ram, actor + 0x62)) {
        unsigned frame = read_word(ram, actor + 0x60) + 2;
        write_word(ram, actor + 0x60, frame >= 24 ? 0 : frame);
        write_word(ram, actor + 0x62, 3);
    }
    uint16_t model = read_word(ram, actor + 0x48);
    uint16_t distance = read_word(ram, actor + 0x46);
    for (int point = 0; point < 35; ++point) {
        unsigned pos = 0xb8cc + 8 * point;
        int x = (int16_t)read_word(rom, model + point * 8);
        int y = (int16_t)read_word(rom, model + point * 8 + 2);
        int z = (int16_t)read_word(rom, model + point * 8 + 4);
        for (int axis = 0; axis < 3; ++axis) {
            unsigned angle = ram[actor + (axis == 0 ? 0x43 : axis == 1 ? 0x42 : 0x44)] * 2;
            int c = (int16_t)read_word(rom + 0xa0000, 0x9e12 + angle);
            int s = (int16_t)read_word(rom + 0xa0000, 0x9d92 + angle);
            int a = axis == 1 ? y : x;
            int b = axis == 2 ? y : z;
            int rotated = digit_product(a, c) - digit_product(b, s);
            if (axis == 1) y = rotated;
            else x = rotated;
            int other = digit_product(axis == 0 ? -a : a, s) + digit_product(b, c);
            if (axis == 2) y = other;
            else if (axis == 1) z += other;
            else z = other;
        }
        int divisor = (int16_t)(distance - z);
        write_word(ram, pos, 256 + (int16_t)(200 * x) / divisor);
        write_word(ram, pos + 2, 256 + (int16_t)(200 * y) / divisor);
        write_word(ram, pos + 4, z);
        write_word(ram, pos + 6, read_word(rom, model + point * 8 + 6));
    }
    unsigned order = 0xe5d7 + 6 * ((ram[actor+0x43] >> 7) + 2 * (ram[actor+0x42] >> 7));
    uint16_t step = read_word(rom, order), row = read_word(rom, order + 2);
    uint16_t pos = 0xb8cc + read_word(rom, order + 4);
    for (int j = 0; j < 7; ++j) {
        for (int i = 0; i < 5; ++i, pos += step) {
            write_word(ram, actor + 0x21, read_word(ram, pos));
            write_word(ram, actor + 0x24, read_word(ram, pos + 2));
            ram[actor+0x2c] = ram[actor + (ram[pos+6] == 3 ? 0x14 : 0x15)];
            uint16_t depth = distance - read_word(ram, pos + 4) - 170;
            uint16_t table = read_word(rom, 0xdd47 + 2 * (depth >> 5));
            write_word(ram, actor + 0x2a, read_word(rom, table + read_word(ram, actor+0x60)));
            queue_sprite(ram, rom, actor, 1);
        }
        pos += row;
    }
}

void queue_continue_sound(uint8_t *ram, uint16_t command)
{
    uint16_t next = (read_word(ram, 0xa6dc) + 2) & 14;
    if (next != read_word(ram, 0xa6da)) {
        write_word(ram, 0xa6dc, next);
        write_word(ram, 0xa6b2 + next, command);
    }
}

void shatter_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    queue_continue_sound(ram, 0x13);
    ram[0x282] |= 1;
    uint16_t chime = effect_slot(ram, actor, 0xe209);
    ram[chime + 0x43] = 28;
    ram[chime + 0x60] = 4;
    ram[read_word(ram, actor + 0x54) + 0x58] = 0;
    write_word(ram, actor + 0x60, 0);
    for (unsigned i = 0; i < 35; ++i) {
        if (i == 17) continue;
        uint16_t part = world_spawn_child(ram, actor, 0xe234);
        if (!part) continue;
        unsigned pos = 0xb8cc + 8*i;
        uint16_t x = read_word(ram, pos), y = read_word(ram, pos + 2);
        write_word(ram, part + 0x21, x);
        write_word(ram, part + 0x24, y);
        write_word(ram, part + 0x2a, 0x874d);
        if (ram[pos + 6] == 3) {
            ram[part + 0x2c] = ram[actor + 0x14];
            ram[part + 0x2d] = 1;
            ram[part + 0x28] = vector_angle(rom, 256-x, 256-y) + 128;
        } else {
            ram[part + 0x2c] = ram[actor + 0x15];
            write_word(ram, part, 0xe2c9);
            unsigned index = read_word(ram, actor + 0x60);
            ram[part + 0x4e] = rom[0xf0df + index];
            write_word(ram, actor + 0x60, index + 1);
        }
    }
    ram[actor + 0x4e] = 139;
    write_word(ram, actor, 0xe1da);
}

void digit_fragment_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    int16_t speed = read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        int16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2*axis, (unit * speed) >> 8);
    }
}

void acquire_private_palette(uint8_t *ram, uint16_t actor, unsigned resource)
{
    if ((read_word(ram, 0x262) & 0x8800) || !read_word(ram, 0xa48a)) {
        ++ram[0xa48e + resource];
        ++ram[0xa58e + resource];
        ram[0x26d] |= 0x80;
    } else {
        uint16_t cursor = read_word(ram, 0xa486);
        unsigned palette = ram[0xa406 + cursor];
        write_word(ram, 0xa486, (cursor + 1) & 63);
        decrement(ram, 0xa48a);
        ram[actor + 0x2c] = palette;
        ram[actor + 0x14] = palette;
        ram[actor + 0x10] = resource;
        uint16_t job = 0x9e06 + 8*palette;
        write_word(ram, job, resource);
        write_word(ram, job + 2, 64);
        write_word(ram, job + 4, 0);
        write_word(ram, job + 6, 0);
    }
}

void refresh_stage_time(uint8_t *ram, uint8_t *tiles, uint16_t actor, unsigned seconds)
{
    int warning = seconds < 10;
    if (warning) {
        unsigned tail = (read_word(ram, 0xa6d8) + 2) & 14;
        if (tail != read_word(ram, 0xa6d6)) {
            write_word(ram, 0xa6d8, tail);
            write_word(ram, 0xa6a2 + tail, 0x2d);
        }
    }
    if (!!read_word(ram, actor + 0x60) != warning) {
        write_word(ram, actor + 0x60, warning);
        write_word(ram, 0x9e2e, warning ? 0x103 : 0x101);
        write_word(ram, 0x9e30, 0x80);
        write_word(ram, 0x9e32, 0);
        write_word(ram, 0x9e34, 0x1020);
    }
    ram[actor + 0x50] = '0' + seconds / 10;
    ram[actor + 0x51] = '0' + seconds % 10;
    ram[actor + 0x52] = 0xff;
    world_timer_digits(tiles, ram + actor + 0x50);
}

void expire_stage_time(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (!(ram[0x264] & (1u << i)))
            continue;
        uint16_t sub = read_word(ram, 0x90 + 2 * i);
        write_word(ram, sub + 0x26, 0);
        unsigned linked = 1u << (2 * i);
        if (ram[0xb8b2] & linked) {
            uint16_t other = read_word(ram, 0xbaae + 2 * i);
            write_word(ram, other + 0x26, 0);
            ram[0xb8b2] &= ~linked;
        }
    }
    world_spawn_child(ram, actor, read_word(rom, 0x6158));
    write_word(ram, actor + 0x42, 0x100);
    write_word(ram, actor, read_word(rom, 0x6167));
}

void update_stage_trigger(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (read_word(ram, 0x26e) & 0x8000) {
        write_word(ram, actor, 0xeebf);
        return;
    }
    uint16_t camera = read_word(ram, 0xb700);
    uint16_t phase = read_word(ram, 0x270);
    uint16_t handler = read_word(ram, actor);
    if (handler == 0xedd0) {
        if ((int16_t)camera >= (int16_t)read_word(ram, actor + 0x1e)) {
            finish_animation(ram, actor);
            return;
        }
        if (!(ram[0x264] & 3)) return;
        if (camera >= read_word(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, camera + 16);
            write_word(ram, actor + 0x42, 600);
        }
        if (phase == read_word(ram, actor + 0x52) && decrement(ram, actor + 0x42)) return;
    } else if (handler == 0xee4f && phase == read_word(ram, actor + 0x52)) {
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            unsigned index = step & 14;
            const uint8_t *bank = rom + 0xa0000;
            write_word(ram, actor + 0x2a, read_word(bank, 0xed4e + 8 * phase + index));
            write_word(ram, actor + 0x44, read_word(bank, 0xed7e + index));
            step += 2;
            write_word(ram, actor + 0x40, step);
            if (step >= 0x22) {
                write_word(ram, actor + 0x50, camera + 16);
                write_word(ram, actor + 0x52, phase);
                write_word(ram, actor + 0x42, 600);
                write_word(ram, actor, 0xedd0);
                return;
            }
            if (!index) world_queue_sound(ram, 0x33);
        }
        queue_sprite(ram, rom, actor, 0);
        return;
    }
    write_word(ram, actor + 0x52, phase);
    uint16_t index = (phase & 6) * 2;
    write_word(ram, actor + 0x21, read_word(rom + 0xa0000, 0xed8e + index));
    write_word(ram, actor + 0x24, read_word(rom + 0xa0000, 0xed90 + index));
    write_word(ram, actor + 0x40, 0);
    write_word(ram, actor + 0x44, 1);
    write_word(ram, actor, 0xee4f);
}

void enemy_torpedo_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned mask)
{
    int hit = mask & (read_word(ram, actor + 0x0e) == 0x1b ? 0x5502 : 0xff02);
    if (!hit) {
        write_word(ram, actor + 0x36, 1);
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, rom, actor);
        hit = !read_word(ram, actor + 0x26);
        if (!hit) {
            ram[actor + 0x0d] = 2;
            hit = world_player_contact(ram, rom, actor) != 0;
        }
    }
    if (hit) {
        spawn_impact(ram, rom, actor);
        finish_animation(ram, actor);
    } else if (read_word(ram, actor + 0x72)) {
        write_word(ram, actor + 0x36, 0x80);
        world_append_candidate(ram + 0xa6e2, actor);
    }
}

void water_entry_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y)
{
    uint16_t splash = effect_slot(ram, actor, 0x6f54);
    unsigned scroll = read_word(ram, 0xb70c) & 7;
    write_word(ram, splash + 0x21, x);
    write_word(ram, splash + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
    ram[splash + 0x23] = 0;
    write_word(ram, splash + 0x4c, read_word(rom, 0x6d2a));
    write_word(ram, splash + 0x4a, read_word(rom, 0x6d2f));
    write_word(ram, splash + 0x2c, 0x200a);
    write_word(ram, splash + 0x56, 48);
    write_word(ram, splash + 0x58, 128);
}

uint16_t spawn_surface_splash_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor,
                                           uint16_t x, uint16_t y)
{
    unsigned scroll = read_word(ram, 0xb70c) & 7;
    uint16_t splash = effect_slot(ram, actor, 0x6f54);
    write_word(ram, splash + 0x21, x);
    write_word(ram, splash + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
    ram[splash + 0x23] = 0;
    write_word(ram, splash + 0x4c, read_word(rom, 0x6d67));
    write_word(ram, splash + 0x4a, read_word(rom, 0x6d6c));
    ram[splash + 0x2c] = rom[0x6d71];
    ram[splash + 0x2d] = rom[0x6d75];
    write_word(ram, splash + 0x56, read_word(rom, 0x6d79));
    write_word(ram, splash + 0x58, read_word(rom, 0x6d7e));
    world_queue_sound(ram, read_word(rom, 0x6d8a));
    return splash;
}

void spawn_surface_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y)
{
    unsigned scroll = read_word(ram, 0xb70c) & 7;
    y = ((y - scroll) & 0xfff8) + scroll;
    ram[actor + 0x23] = 0;
    write_word(ram, actor + 0x24, y);
    spawn_surface_splash_effect(ram, rom, actor, x, y);
}

int update_vertical_weapon(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                  uint16_t actor, uint16_t handler)
{
    int rising = handler != 0xa0a9;
    int surface = handler == 0x9fcd || handler == 0x9fda;
    if (ram[actor + 0x0b] & 0x80)
        return 0;
    uint16_t velocity = read_word(ram, actor + 0x3a);
    int accelerate = (handler == 0x9eb2 || handler == 0x9fcd) &&
                     read_word(ram, actor + 0x42) == 1;
    if (accelerate)
        velocity *= 2;
    uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb716);
    unsigned fraction = ram[actor + 0x23] + (velocity & 255);
    uint16_t y = read_word(ram, actor + 0x24) + read_word(ram, 0xb718)
               + (int8_t)(velocity >> 8) + (fraction >> 8);
    uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                      0x177 - y + read_word(ram, 0xb70c));
    unsigned material = terrain[cell + 3] >> 3;

    if (handler == 0x9eb2 || handler == 0x9fcd) {
        decrement(ram, actor + 0x42);
        if (accelerate) {
            write_word(ram, actor + 0x3a, velocity);
            if (surface) write_word(ram, actor, 0x9fda);
            else {
                write_word(ram, actor + 0x58, 0x56);
                write_word(ram, actor, 0x9ec4);
            }
        }
    }
    write_word(ram, actor + 0x21, x);
    write_word(ram, actor + 0x24, y);
    ram[actor + 0x23] = (uint8_t)fraction;
    uint16_t frame = read_word(ram, actor + 0x40);
    if (surface) {
        frame += 128;
        if ((int16_t)frame >= 0x300) frame = 0;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(rom, 0x99b5 + 2 * (frame >> 8)));
    } else if (rising) {
        frame += read_word(ram, actor + 0x58);
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(rom, 0x99ad + 2 * ((frame >> 8) & 3)));
    } else if (!decrement(ram, actor + 0x58)) {
        write_word(ram, actor + 0x2a, read_word(rom, 0x99bd + (frame & 14)));
        write_word(ram, actor + 0x58, read_word(rom, 0x99cd + (frame & 14)));
        write_word(ram, actor + 0x40, frame + 2);
    }
    queue_sprite(ram, rom, actor, 1);
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    unsigned mask = 1u << (material & 15);
    if (surface) {
        uint16_t limit = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        int impact = (mask & 0x5502) || (y <= limit && !read_word(ram, actor + 0x26));
        if (impact || y > limit) {
            if (impact) {
                world_stage5_wall_hit(ram, rom, terrain, actor);
                spawn_impact(ram, rom, actor);
            }
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) + 1);
            finish_animation(ram, actor);
        } else if (read_word(ram, actor + 0x3c) != 0xffff) {
            world_append_candidate(ram + 0xaee6, actor);
        }
        return 1;
    }
    if (rising) {
        if (!decrement(ram, actor + 0x5a)) {
            write_word(ram, actor + 0x5a, 3);
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
            write_word(ram, trail + 0x21, x);
            write_word(ram, trail + 0x24, y - 8);
            write_word(ram, trail + 0x52, read_word(ram, actor + 0x52));
            ram[trail + 0x2c] = 3;
            write_word(ram, trail + 0x56, 0x40);
            write_word(ram, trail + 0x58, 0x11);
        }
        uint16_t top = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        int impact = (mask & 0x5502) || (y <= top && !read_word(ram, actor + 0x26));
        if (impact || y > top || material == 3) {
            if (material == 3) {
                spawn_surface_splash(ram, rom, actor, x, y);
            } else if (impact) {
                world_stage5_wall_hit(ram, rom, terrain, actor);
                spawn_impact(ram, rom, actor);
            }
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) + 1);
            finish_animation(ram, actor);
            return 1;
        }
    } else {
        int impact = (mask & 0xff02) || !read_word(ram, actor + 0x26) || !decrement(ram, actor + 0x42);
        int16_t bottom = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
        if (impact || (int16_t)y < bottom) {
            uint16_t count = read_word(ram, parent + 0x48);
            write_word(ram, parent + 0x48, (int16_t)count > 0 ? count - 1 : 0);
            if (impact) {
                world_stage5_wall_hit(ram, rom, terrain, actor);
                release_actor_resources(ram, actor);
                write_word(ram, actor + 0x36, 0x20);
                write_word(ram, actor, 0xa199);
            } else
                finish_animation(ram, actor);
            return 1;
        }
    }
    if (read_word(ram, actor + 0x3c) != 0xffff)
        world_append_candidate(ram + 0xa6e2, actor);
    return 1;
}


void update_vertical_camera(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    int16_t y = read_word(ram, actor + 0x56);
    int16_t low = read_word(ram, actor + 0x64), high = read_word(ram, actor + 0x66);
    uint16_t flags = read_word(ram, 0x26e);
    write_word(ram, actor + 0x5e, y);
    unsigned request = 0;
    if (y < low)
        request = 0x1000;
    else if (y > high)
        request = 0x2000;
    else {
        unsigned players = ram[0x264];
        unsigned input = read_word(ram, 0xb73e);
        unsigned index = (players & 4 ? ((input >> 8) & 12) * 2 : 0)
                       + (players & 8 ? (input & 12) / 2 : 0);
        unsigned handler = read_word(rom, 0x4727 + index);
        if (handler == 0x45c3 || handler == 0x45f3) {
            request = handler == 0x45c3 ? 0x1000 : 0x2000;
            for (unsigned i = 0; i < 2; i++) {
                uint16_t sub = read_word(ram, 0x90 + 2 * i);
                int16_t py = read_word(ram, sub + 0x24);
                if ((players & (4 << i)) && (handler == 0x45c3 ? py >= 0x138 : py <= 0xd8))
                    request = 0;
            }
        }
    }
    flags |= request;
    int delta = flags & 0x1000 ? 1 : -1;
    if (!request || (delta > 0 ? y >= high : y <= low)) {
        flags &= ~0x3000;
        delta = 0;
    } else {
        y += delta;
        write_word(ram, actor + 0x56, y);
        write_word(ram, 0xb70c, y);
        write_word(ram, 0xb732, y & 511);
    }
    write_word(ram, 0x26e, flags);
    write_word(ram, 0xb71c, delta);
    write_word(ram, 0xb72c, delta);
}

void update_background_vertical(uint8_t *ram, uint16_t actor)
{
    uint16_t flags = read_word(ram, 0x26e);
    int dy = !(flags & 0x3000) ? 0 : flags & 0x1000 ? 1 : -1;
    write_word(ram, 0xb720, dy);
    if (dy) {
        uint16_t y = read_word(ram, actor + 0x56) + dy;
        write_word(ram, actor + 0x56, y);
        write_word(ram, 0xb710, y);
    }
}

void update_camera_layers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
                                 uint16_t actor, int background, int vertical_control)
{
    if (background) {
        uint16_t flags = read_word(ram, 0x26e);
        update_background_vertical(ram, actor);
        if (!(flags & 0x4000)) {
            write_word(ram, 0xb71e, 0);
            return;
        }
        int16_t speed = (int16_t)read_word(ram, 0xa4) >> 1;
        uint16_t old = read_word(ram, actor + 0x52);
        unsigned frac = ram[actor + 0x51] + (speed & 255);
        uint16_t x = old + (speed >> 8) + (frac >> 8);
        ram[actor + 0x51] = frac;
        write_word(ram, actor + 0x5a, old);
        write_word(ram, actor + 0x52, x);
        write_word(ram, 0xb70e, x);
        write_word(ram, 0xb71e, old - x);
    } else {
        if (vertical_control) update_vertical_camera(ram, rom, actor);
        update_camera_limits(ram, rom, actor);
        uint16_t p1 = read_word(ram, 0x90), p2 = read_word(ram, 0x92);
        if (!world_horizontal_camera(ram, rom, actor, read_word(ram, p1 + 0x21), read_word(ram, p2 + 0x21))) {
            write_word(ram, 0xb71a, 0);
            write_word(ram, 0xb72a, 0);
            write_word(ram, 0xb726, 0);
            write_word(ram, 0xa4, 0);
            return;
        }
    }
    uint16_t trigger = read_word(ram, actor + 0x6a);
    if ((int16_t)trigger <= (int16_t)read_word(ram, background ? 0xb700 : 0xb70a)) {
        uint16_t source = read_word(ram, actor + 0x42), dest = read_word(ram, actor + 0x44);
        uint16_t base = (uint32_t)read_word(ram, actor + 0x60) << 4;
        for (unsigned block = 0; block < 11; block++) {
            world_draw_tile_block(tiles, rom, base, dest, read_word(rom + 0xa0000, source + 2 * block));
            dest = (dest + 0x400) & 0x3fff;
        }
        write_word(ram, actor + 0x42, source + read_word(ram, actor + 0x4e));
        ram[actor + 0x44] += 16;
        write_word(ram, actor + 0x6a, trigger + (background ? 64 : 32));
    }
    advance_scroll_script(ram, rom, actor);
}


void surface_enemy_pose(uint8_t *ram, const uint8_t *bank, uint16_t actor, int magnitude)
{
    uint16_t index = ((uint16_t)(magnitude - 1) >> 5) & 14;
    write_word(ram, actor + 0x4c, read_word(bank, 0x2760 + index));
    write_word(ram, actor + 0x3c, read_word(bank, 0x2770 + index));
    write_word(ram, actor + 0x4a, read_word(bank, 0x2780 + index));
    write_word(ram, actor + 0x60, (index + 2) * 2);
}

void surface_enemy_fire(uint8_t *ram, uint16_t actor)
{
    if (read_word(ram, actor + 0x24) >= 0x170 || decrement(ram, actor + 0x50)) return;
    int mine = ram[actor + 0x0e] == 14;
    uint16_t interval = read_word(ram, actor + 0x52);
    if (!mine) {
        if (decrement(ram, actor + 0x58)) interval = read_word(ram, actor + 0x54);
        else write_word(ram, actor + 0x58, read_word(ram, actor + 0x5a));
    }
    write_word(ram, actor + 0x50, interval);
    uint16_t child = effect_slot(ram, actor, mine ? 0x30c6 : 0x2f81);
    if (child != 0x8480) write_word(ram, child + 2, 0xb000);
    if (mine) {
        write_word(ram, child + 0x58, read_word(ram, actor + 0x58));
    } else {
        write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
        write_word(ram, child + 0x24, read_word(ram, actor + 0x24) - 8);
        ram[child + 0x29] = ram[actor + 0x29] ^ 1;
        write_word(ram, child + 0x38, (int16_t)read_word(ram, actor + 0x38) >> 1);
        write_word(ram, child + 0x3a, read_word(ram, actor + 0x5c));
        write_word(ram, child + 0x5e, read_word(ram, actor + 0x5e));
        world_queue_sound(ram, 0x7d);
    }
}

int surface_enemy_swim(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles,
                              uint16_t actor, uint16_t handler)
{
    if (!tiles || !read_word(ram, actor + 0x26)) return 0;
    unsigned players = read_word(ram, 0x264) & 3;
    if ((handler == 0x2ca0 || handler == 0x2d80) && players == 3 &&
        !world_enemy_target_mode_supported(ram[actor + 0x0c])) return 0;
    const uint8_t *bank = rom + 0xb0000;
    uint16_t facing = ram[actor + 0x29] & 1;
    uint16_t accel = handler == 0x2c46 ? read_word(bank, 0x28d6 + 2 * facing) : read_word(ram, actor + 0x46);
    uint16_t speed = read_word(ram, actor + 0x38);
    if (handler == 0x2c46 || handler == 0x2c68) speed += accel;
    if (handler == 0x2cd3) speed -= accel;
    unsigned fraction = ram[actor + 0x20] + (speed & 255);
    uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
    if (handler != 0x2d80) {
        x += (int8_t)(speed >> 8) + (fraction >> 8);
        /* At exact clip edges the original score path consumes scratch BX. */
        if (x == 0xffc0 || x == 0x240) return 0;
    }
    if (handler == 0x2c46) {
        write_word(ram, actor + 0x46, accel);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor, 0x2c68);
    }
    if (handler == 0x2d80) {
        scroll_actor(ram, actor, 0xb71a);
        if ((!read_word(ram, actor + 0x42) || !decrement(ram, actor + 0x42)) && players) {
            uint16_t player = world_enemy_target(ram, actor);
            facing = (int16_t)read_word(ram, player + 0x21) <= (int16_t)x;
            ram[actor + 0x29] = facing;
            if (!(world_terrain_mask(ram, tiles + 0x4000, x + read_word(bank, 0x28da + 2 * facing),
                                     read_word(ram, actor + 0x24)) & 0xff02)) {
                if ((int16_t)x <= 0x88) ram[actor + 0x29] = 0;
                write_word(ram, actor, 0x2c46);
            }
            surface_enemy_fire(ram, actor);
            if (!decrement(ram, actor + 0x4e)) {
                write_word(ram, actor + 0x42, 16);
                write_word(ram, actor, 0x2e4a);
            }
        }
        queue_surface_enemy(ram, rom, actor);
        register_surface_enemy(ram, actor);
        surface_enemy_hit(ram, rom, actor);
        return 1;
    }
    int magnitude = (speed & 0x8000) ? (uint16_t)-speed : speed;
    if (handler == 0x2ca0) {
        int continue_swimming = 0;
        if (players) {
            uint16_t player = world_enemy_target(ram, actor);
            int16_t target = read_word(ram, player + 0x21) + read_word(bank, 0x28da + 2 * facing);
            int16_t current = read_word(ram, actor + 0x21);
            continue_swimming = facing ? target < current : target > current;
        }
        if (!continue_swimming) write_word(ram, actor, 0x2cd3);
    } else {
        write_word(ram, actor + 0x38, speed);
        if (handler == 0x2cd3 && !speed) {
            write_word(ram, actor + 0x42, 16);
            write_word(ram, actor + 0x60, 0);
            write_word(ram, actor + 0x4c, 0x172a);
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
            write_word(ram, actor, 0x2d80);
        } else if (handler != 0x2cd3 && (int16_t)magnitude >= 512) {
            write_word(ram, actor + 0x4c, 0x16a8);
            write_word(ram, actor + 0x3c, 0xf363);
            write_word(ram, actor + 0x4a, 18);
            write_word(ram, actor + 0x60, 36);
            write_word(ram, actor, 0x2ca0);
        } else {
            surface_enemy_pose(ram, bank, actor, magnitude);
        }
    }
    scroll_actor(ram, actor, 0xb71a);
    ram[actor + 0x20] = (uint8_t)fraction;
    write_word(ram, actor + 0x21, x);
    if (handler != 0x2cd3 && ((facing && (int16_t)x <= 0xb8) ||
        (world_terrain_mask(ram, tiles + 0x4000, x + read_word(bank, 0x28de + 2 * facing),
                            read_word(ram, actor + 0x24)) & 0xff02))) write_word(ram, actor, 0x2cd3);
    if (!decrement(ram, actor + 0x4e)) write_word(ram, actor, 0x2e05);
    queue_surface_enemy(ram, rom, actor);
    surface_enemy_fire(ram, actor);
    if ((int16_t)x > -64 && (int16_t)x < 576) surface_enemy_hit(ram, rom, actor);
    if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
    return 1;
}

int boss_segment_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    if (handler == 0x1496) {
        write_word(ram, actor + 0x0e, 0x200);
        write_word(ram, actor + 0x3c, 0xf78b);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x400);
        ram[actor + 0x2d] = 0x30;
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x3c, 0xf783);
            write_word(ram, actor, 0x1723);
            return boss_claw_step(ram, rom, actor, 0x1723);
        }
        uint16_t child = spawn_boss_part(ram, actor, 0x1496);
        write_word(ram, child + 0x52, read_word(ram, actor + 0x52));
        write_word(ram, child + 0x42, read_word(ram, actor + 0x42));
        write_word(ram, child + 0x54, read_word(ram, actor + 0x54) + 1);
        write_word(ram, child + 0x50, read_word(ram, actor + 0x50));
        world_acquire_resource(ram, actor, 0x69);
        write_word(ram, actor, 0x14f1);
        handler = 0x14f1;
    }
    if (handler == 0x16b8 || handler == 0x16c8) {
        if (handler == 0x16b8) {
            uint16_t speed = read_word(ram, actor + 0x38) + 16;
            write_word(ram, actor + 0x38, speed);
            if (speed >= 512) write_word(ram, actor, 0x16c8);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (read_word(ram, actor + 0x21) > 0x1c0) {
            finish_animation(ram, actor);
            return 1;
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned index = (ram[actor + 0x28] & 0x7c) >> 2;
            ram[actor + 0x29] = bank[0x5c8 + index];
            write_word(ram, actor + 0x2a, read_word(bank, 0x4ce + read_word(bank, 0x4f0 + 2*index)));
            ram[actor + 0x28] += 4;
            write_word(ram, actor + 0x44, 4);
        }
        queue_sprite(ram, rom, actor, 0);
        return 1;
    }
    if (handler == 0x15bd) {
        write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) & ~5u);
        write_word(ram, actor + 0x38, 0);
        write_word(ram, actor + 0x3a, 0);
        write_word(ram, actor + 0x5a, 0);
        write_word(ram, actor + 0x5c, 0);
        write_word(ram, actor + 0x20, read_word(ram, parent + 0x20));
        write_word(ram, actor + 0x22, read_word(ram, parent + 0x22));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        write_word(ram, actor, 0x14f1);
        return 1;
    }
    if (handler == 0x14f1) {
        if (ram[actor + 0x62] & 2) {
            write_word(ram, actor, 0x15fb);
            boss_chain_move(ram, actor);
            ram[parent + 0x62] |= 2;
            ram[actor + 0x62] &= (uint8_t)~2;
            ram[actor + 0x60] |= 4;
        } else if (ram[actor + 0x62] & 1) {
            write_word(ram, actor, 0x1545);
            write_word(ram, actor + 0x2e, 0x400);
            boss_limb_velocity(ram, rom, actor);
            boss_chain_move(ram, actor);
            ram[actor + 0x60] |= 1;
        } else {
            boss_chain_move(ram, actor);
            return 1;
        }
    } else if (handler == 0x1545) {
        ram[parent + 0x62] |= 1;
        ram[actor + 0x62] &= (uint8_t)~1;
        write_word(ram, actor + 0x2e, 0x800);
        boss_limb_velocity(ram, rom, actor);
        write_word(ram, actor, 0x156f);
    } else if (handler == 0x156f || handler == 0x15fb) {
        unsigned flag = handler == 0x156f ? 1 : 4;
        if (!(ram[parent + 0x60] & flag)) write_word(ram, actor, 0x15bd);
        else if (read_word(ram, actor + 0x54)) {
            write_word(ram, parent + 0x5a, read_word(ram, actor + 0x38));
            write_word(ram, parent + 0x5c, read_word(ram, actor + 0x3a));
            ram[parent + 0x56] = ram[actor + 0x28];
        }
        write_word(ram, actor + 0x38, read_word(ram, actor + 0x5a));
        write_word(ram, actor + 0x3a, read_word(ram, actor + 0x5c));
        ram[actor + 0x28] = ram[actor + 0x56];
        boss_chain_move(ram, actor);
    } else return 0;

    unsigned index = ((uint8_t)(ram[actor + 0x28] + 2) & 0x7c) >> 2;
    ram[actor + 0x29] = bank[0x5c8 + index];
    write_word(ram, actor + 0x2a, read_word(bank, 0x4ce + read_word(bank, 0x4f0 + 2*index)));
    queue_sprite(ram, rom, actor, 0);
    if (!(read_word(ram, actor + 0x54) & 3)) {
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, rom, actor);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, rom, actor);
    }
    uint16_t root = read_word(ram, actor + 0x50);
    if (ram[root + 0x0a] & 2) {
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x38, 0);
        write_word(ram, actor + 0x3a, read_word(bank, 0x594 + (read_word(ram, actor + 0x54) & 14)));
        ram[actor + 0x2d] = 0x1f;
        write_word(ram, actor, 0x16b8);
    }
    return 1;
}

/* Title-screen actors occupy one contiguous address range. */
static int title_actors(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass; (void)tiles;
    switch (address) {
    default: {
        if (address < 0xa89d9 || address > 0xa8ed8) return 0;
        int action = world_title_actor(ram, main_rom, actor, (uint16_t)address);
        if (action < 0) return 0;
        if (action & WORLD_TITLE_DRAW) queue_sprite(ram, main_rom, actor, 0);
        if (action & WORLD_TITLE_FINISH) finish_animation(ram, actor);
        if (action & WORLD_TITLE_SKIP_MOTION) {
            write_word(ram, actor + 0x24, 0x110);
            write_word(ram, actor, 0x8c7b);
        }
        break;
    }
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}

/* Handler groups are tried in order. Stage files come first so newly
 * recovered handlers can be added without touching the older parts. */
static WorldHandlerGroup *const handler_groups[] = {
    world_stage1_handlers, world_stage2_handlers, world_stage3_handlers,
    world_stage4_handlers, world_stage5_handlers, world_stage6_handlers,
    world_gaps_handlers, world_credits_handlers, world_coop_handlers, world_explore1_handlers, world_explore2_handlers,
    world_common_handlers,
    world_actors_1, world_actors_2, world_actors_3, world_actors_4, world_actors_5, world_actors_6,
    title_actors
};

int world_dispatch_native_video(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom, uint8_t *tiles)
{
    uint32_t address;
    uint16_t actor = pass->actor;
    if (!world_dispatch_target(pass, ram, &address))
        return 0;
    int result = WORLD_HANDLER_UNKNOWN;
    world_stale_dx_call(address, 0);
    for (unsigned i = 0; i < sizeof handler_groups / sizeof handler_groups[0]; ++i) {
        result = handler_groups[i](pass, ram, main_rom, tiles, &actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) break;
    }
    if (result != WORLD_HANDLER_DONE) return result == WORLD_HANDLER_UNKNOWN ? 0 : result;
    world_stale_dx_call(address, 1);
    world_dispatch_return(pass, ram, actor);
    return 1;
}




int world_dispatch_native(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom)
{
    return world_dispatch_native_video(pass, ram, main_rom, 0);
}
