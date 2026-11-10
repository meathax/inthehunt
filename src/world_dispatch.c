#include "world_dispatch.h"
#include "world_enemy_target.h"
#include "world_stage.h"
#include "world_title.h"
#include "world_torpedo.h"
#include "world_tile_address.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_word(const uint8_t *ram, uint16_t offset)
{
    return ram[offset] | ((uint16_t)ram[(uint16_t)(offset + 1)] << 8);
}

static void write_word(uint8_t *ram, uint16_t offset, uint16_t value)
{
    ram[offset] = (uint8_t)value;
    ram[(uint16_t)(offset + 1)] = (uint8_t)(value >> 8);
}

static void swap_bytes(uint8_t *ram, uint16_t offset)
{
    uint8_t low = ram[offset];
    ram[offset] = ram[offset + 1];
    ram[offset + 1] = low;
}

static int si_known;
static uint32_t si_source;
/* 0DA4/0DD6 leave SI pointing at the display-list entry they filled, and
   leave it alone when the sprite budget is exhausted (entry 0 here). */
static uint16_t queued_sprite_entry;
static void leave_si(uint16_t value);
static void keep_si(void);

void world_dispatch_begin(WorldDispatch *pass, uint8_t *ram)
{
    write_word(ram, 0xb8a2, 0);
    write_word(ram, 0xa6e2, 0);
    write_word(ram, 0xaee6, 0);
    pass->actor = 0x0400;
    pass->phase = WORLD_FRAME_HEAD;
    world_dispatch_detail = "";
    si_known = 0;
    si_source = 0;
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

static void submit_sprite(uint8_t *ram, uint16_t actor, uint16_t sprite, uint16_t segment, uint8_t palette)
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
static int queue_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, int flash)
{
    uint16_t sprite = read_word(ram, actor + 0x2a);
    uint16_t segment = (sprite & 1) ? 0x9000 : 0x8000;
    uint16_t cost = read_word(rom + ((uint32_t)segment << 4), sprite);
    uint16_t budget = read_word(ram, 0x9e02);

    if ((uint16_t)(cost + 6) >= budget) {
        write_word(ram, 0x026a, read_word(ram, 0x026a) | 0x8000);
        queued_sprite_entry = 0;
        return 0;
    }
    write_word(ram, 0x9e02, budget - cost);
    queued_sprite_entry = read_word(ram, 0x9e04);

    uint8_t palette = ram[actor + 0x2c];
    if (flash && ram[actor + 0x19] && (--ram[actor + 0x19] & 1))
        palette = 0x0c;
    submit_sprite(ram, actor, sprite, segment, palette);
    return 1;
}

static uint16_t decrement(uint8_t *ram, uint16_t address)
{
    uint16_t value = read_word(ram, address) - 1;
    write_word(ram, address, value);
    return value;
}

static void finish_animation(uint8_t *ram, uint16_t actor)
{
    write_word(ram, actor, 0x1b4c);
    write_word(ram, actor + 2, 0);
    ram[actor + 0x0a] = 1;
    write_word(ram, actor + 0x3c, 0xffff);
}

static void scroll_actor(uint8_t *ram, uint16_t actor, uint16_t delta)
{
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, delta));
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, delta + 2));
}

static void update_attached_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
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

static void move_actor_axis(uint8_t *ram, uint16_t position, uint16_t velocity)
{
    unsigned fraction = ram[position] + (uint8_t)velocity;
    ram[position] = (uint8_t)fraction;
    write_word(ram, position + 1, read_word(ram, position + 1) + (int8_t)(velocity >> 8) + (fraction >> 8));
}

static void update_intro_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
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

static void animate_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void release_actor_resources(uint8_t *ram, uint16_t actor)
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

static void retire_actor(uint8_t *ram, uint16_t actor)
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

static uint8_t vector_angle(const uint8_t *rom, int16_t dx, int16_t dy)
{
    unsigned x = abs((int)dx), y = abs((int)dy);
    unsigned angle = dx < 0 ? 128 : 0;
    if (x || y) {
        unsigned octant = y > x ? rom[0xd58e + 256 * x / y] : rom[0xd48d + 256 * y / x];
        angle += (dx < 0) != (dy < 0) ? -octant : octant;
    }
    return angle;
}

static void apply_radial_impulse(uint8_t *ram, const uint8_t *rom, uint16_t actor,
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

static void update_radial_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, int attached)
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
/* DA13 and DB86 load SI with the variant bits before running into the
   update; -1 when the update was entered directly. */
static int scenery_init_si = -1;

static void update_scenery(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
                           uint16_t actor, int near_layer)
{
    /* Paths that draw nothing leave SI as it was on reaching DA4F/DBAF. */
    if (scenery_init_si >= 0) leave_si((uint16_t)scenery_init_si);
    else keep_si();
    scenery_init_si = -1;
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
    /* Each DC25 strip copy ends with SI = 40h, each DC9C copy with 20h. */
    leave_si(near_layer ? 0x20 : 0x40);
}

uint16_t world_player_contact(uint8_t *, const uint8_t *, uint16_t);
int world_ship_contact(const uint8_t *, const uint8_t *, uint16_t);
uint16_t world_projectile_contact(uint8_t *, const uint8_t *, uint16_t);
int world_shape_overlap(const uint8_t *, const uint8_t *, uint16_t, uint16_t, uint16_t, uint16_t);
void world_kind11_death(uint8_t *, const uint8_t *, uint16_t, uint16_t);
void world_spawn_radial_explosion(uint8_t *, const uint8_t *, uint16_t);
void world_kind11_move(uint8_t *, const uint8_t *, const uint8_t *, uint16_t);
uint16_t world_terrain_mask(const uint8_t *, const uint8_t *, uint16_t, uint16_t);
static uint16_t effect_slot(uint8_t *, uint16_t, uint16_t);
void world_queue_sound(uint8_t *, uint16_t);

/* Set by a handler that stops on a known untranslated branch. */
const char *world_dispatch_detail = "";

/* The 0:08AB actor loop never loads SI, so a handler can observe the value
   its predecessor in the actor list left there. Each handler's exit value is
   unknown unless it calls leave_si(); a consumer stops on an unknown value. */
static int si_known, entry_si_known;
static uint16_t si_value, entry_si;
static uint32_t si_source, entry_si_source;

static void leave_si(uint16_t value)
{
    si_known = 1;
    si_value = value;
}

/* For a path verified never to load SI: the next actor sees what this one saw. */
static void keep_si(void)
{
    si_known = entry_si_known;
    si_value = entry_si;
    si_source = entry_si_source;
}

/* For a path whose last SI load, if any, is its final 0DA4/0DD6 call. */
static void si_after_sprite(void)
{
    if (queued_sprite_entry) leave_si(queued_sprite_entry);
    else keep_si();
}

static const char *untracked_si_detail(void)
{
    static char text[96];
    snprintf(text, sizeof text, "needs SI left by handler %05lX (0 = frame start)",
             (unsigned long)entry_si_source);
    return text;
}

static int above_cruiser(const uint8_t *ram, uint16_t enemy, uint16_t player)
{
    uint16_t y = read_word(ram, enemy + 0x24);
    uint16_t target_y = read_word(ram, player + 0x24);
    int dx = (int16_t)(read_word(ram, enemy + 0x21) - read_word(ram, player + 0x21));
    return y > 0x90 && dx > -32 && dx < 32 &&
           (uint16_t)(y + 0x30) < target_y && target_y < (uint16_t)(y + 0x80);
}

static void fire_cruiser_weapon(uint8_t *ram, const uint8_t *rom, uint16_t actor, int vertical)
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

static void queue_surface_enemy(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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
static void surface_enemy_death_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void register_surface_enemy(uint8_t *ram, uint16_t actor)
{
    if (read_word(ram, 0xb6ea) < 16) {
        uint16_t cursor = read_word(ram, 0xb6ea);
        write_word(ram, 0xb6ee + cursor, actor);
        write_word(ram, 0xb6ea, cursor + 2);
    }
}

uint16_t world_surface_contact(uint8_t *, const uint8_t *, uint16_t);
void world_award_score(uint8_t *, const uint8_t *, uint16_t, uint16_t);

static void surface_enemy_hit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void begin_linear_script(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
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
    if (kind == 0x10) {
        /* D980: a vertical sine about the current height. */
        write_word(ram, actor + 0x78, read_word(bank, offset + 4));
        write_word(ram, actor + 0x76, read_word(bank, offset + 6));
        write_word(ram, actor + 0x38, read_word(bank, offset + 8));
        write_word(ram, actor + 0x7e, offset + 10);
        write_word(ram, actor + 0x31, 0);
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        ram[actor + 0x20] = ram[actor + 0x30] = ram[actor + 0x23] = 0;
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

static int step_flight_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t kind = read_word(ram, actor + 0x7a);
    uint16_t next = read_word(ram, actor + 0x7e);
    uint16_t next_kind = read_word(bank, next);
    if ((kind & 1) || (kind > 6 && kind != 0x10) || (read_word(ram, actor + 0x7c) == 1 &&
        next_kind != 0xffff && ((next_kind & 1) || (next_kind > 6 && next_kind != 0x10)))) return 0;
    if (!kind) {
        scroll_actor(ram, actor, 0xb71a);
    } else if (kind == 0x10) {
        /* D781 and 6761: the phase at 30..32 advances by 38; the height is
           the 16.8 base at 33..35 plus sine * amplitude. */
        uint16_t speed = read_word(ram, actor + 0x38);
        scroll_actor(ram, actor, 0xb71a);
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        unsigned fraction = ram[actor + 0x30] + (speed & 255);
        uint16_t phase = read_word(ram, actor + 0x31) + (int8_t)(speed >> 8) + (fraction >> 8);
        ram[actor + 0x30] = (uint8_t)fraction;
        write_word(ram, actor + 0x31, phase);
        uint8_t angle = (uint8_t)(((uint32_t)phase * read_word(ram, actor + 0x76)) >> 8);
        int32_t wave = (int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * angle) * (int16_t)read_word(ram, actor + 0x78);
        uint32_t height = (ram[actor + 0x33] | ((uint32_t)read_word(ram, actor + 0x34) << 8)) + ((uint32_t)wave & 0xffffff);
        ram[actor + 0x23] = (uint8_t)height;
        write_word(ram, actor + 0x24, (uint16_t)(height >> 8));
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

static void update_cruiser_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t burst)
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

/* 68DF's CX result: the five-bit material of the map cell under (x, y). */
static unsigned terrain_material(const uint8_t *ram, const uint8_t *terrain, uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    return terrain[world_tile_address(map_x, map_y) + 3] >> 3;
}

/* 0:D459. Direction of (dx, dy) as a 256-step angle. */
static uint8_t aim_angle(const uint8_t *rom, int16_t dx, int16_t dy)
{
    unsigned x = dx < 0 ? -(int)dx : dx, y = dy < 0 ? -(int)dy : dy;
    uint8_t angle = dx < 0 ? 0x80 : 0;
    int steep = y > x;
    unsigned small = steep ? x : y, large = steep ? y : x;
    if (!large) return angle;
    uint8_t octant = rom[(uint16_t)((steep ? 0xd58e : 0xd48d) + (small << 8) / large)];
    return angle + (((dx < 0) != (dy < 0)) ? (uint8_t)-octant : octant);
}

/* 0:ADF0. A type-121 blast throws the enemy toward 0:AE2A, which destroys
   it seven ticks later. Returns 1 when the attacker was such a blast. */
static int blast_knockback(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t attacker)
{
    if (!attacker || read_word(ram, attacker + 0x0e) != 0x121) return 0;
    ram[actor + 0x28] = aim_angle(rom,
        read_word(ram, attacker + 0x21) - read_word(ram, actor + 0x21),
        read_word(ram, attacker + 0x24) - read_word(ram, actor + 0x24));
    write_word(ram, actor + 0x2e, 0x400);
    for (unsigned axis = 0; axis < 2; ++axis) {
        int16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
        write_word(ram, actor + 0x38 + 2 * axis, (unit * 0x400) >> 8);
    }
    write_word(ram, actor + 0x42, 7);
    write_word(ram, actor, 0xae2a);
    write_word(ram, actor + 2, 0);
    return 1;
}

/* B000:96F3..9741, shared by the cruising and turning states. */
static void cruiser_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 0;
    uint16_t attacker = world_player_contact(ram, rom, actor);
    if (read_word(ram, actor + 0x26)) {
        attacker = world_projectile_contact(ram, rom, actor);
        if (blast_knockback(ram, rom, actor, attacker)) return;
        if (read_word(ram, actor + 0x26)) {
            /* D2B7 loaded SI with the collision shape and nothing follows. */
            leave_si(read_word(ram, actor + 0x3c));
            return;
        }
    }
    world_kind11_death(ram, rom, actor, attacker);
}

static void draw_cruiser_and_check_hits(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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
    cruiser_contacts(ram, rom, actor);
}

/* B000:9C9F: the paired submarine's guns. Shots are linked after the 9C
   anchor (1812) and marked as type 1B so siblings ignore them. */
static void paired_sub_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + 0xb0000;
    uint8_t facing = ram[actor + 0x29];
    if (!decrement(ram, actor + 0x50)) {
        uint16_t dx = read_word(bank, 0x8d78 + 2 * (facing & 1));
        write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
        uint16_t shot = effect_slot(ram, read_word(ram, 0x9c), 0x97ee);
        if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
        ram[shot + 0x29] = facing;
        write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + dx);
        write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
        write_word(ram, shot + 0x38, read_word(ram, actor + 0x60));
        write_word(ram, shot + 0x72, 1);
        write_word(ram, shot + 0x0e, 0x1b);
        uint16_t flash = effect_slot(ram, actor, 0xa268);
        ram[flash + 0x29] = facing;
        write_word(ram, flash + 0x31, dx);
        ram[flash + 0x2d] = 3;
    }
    if (read_word(ram, actor + 0x74)) {
        decrement(ram, actor + 0x74);
        return;
    }
    if (!above_cruiser(ram, actor, read_word(ram, 0x90)) && !above_cruiser(ram, actor, read_word(ram, 0x92)))
        return;
    uint16_t dx = read_word(bank, 0x8d7c + 2 * (facing & 1));
    uint16_t shot = effect_slot(ram, read_word(ram, 0x9c), 0x98e4);
    if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
    ram[shot + 0x29] = facing;
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + dx);
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 4);
    write_word(ram, shot + 0x3a, read_word(ram, actor + 0x62));
    write_word(ram, shot + 0x72, 1);
    write_word(ram, shot + 0x0e, 0x1b);
    uint16_t flash = effect_slot(ram, actor, 0xa2c0);
    ram[flash + 0x29] = facing;
    write_word(ram, flash + 0x31, dx);
    write_word(ram, flash + 0x34, 4);
    ram[flash + 0x2d] = 3;
    write_word(ram, actor + 0x74, read_word(ram, actor + 0x5e));
}

/* The cruising branch. Check branches still handled by the guest before
   changing state so the caller can resume at the original handler entry. */
static int update_cruising_enemy(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                 uint16_t actor)
{
    const int turns = read_word(ram, actor) == 0x9601;
    unsigned probe_direction = 0;
    uint16_t burst = read_word(ram, actor + 0x50) ? 0x50 : 0x54;
    unsigned players = read_word(ram, 0x264) & 3;
    if (players == 3 && read_word(ram, actor + 0x4a) == 1 &&
        !world_enemy_target_mode_supported(ram[actor + 0x0c])) {
        world_dispatch_detail = "two-player target mode";
        return 0;
    }
    if (!read_word(ram, actor + 0x26)) {
        world_dispatch_detail = "enemy entered with no health";
        return 0;
    }
    uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
    x += read_word(rom + 0xb0000, 0x8d58 + 2 * (ram[actor + 0x29] & 1));
    uint16_t y = read_word(ram, actor + 0x24) + read_word(ram, 0xb71c);
    /* SI on reaching 96A6: the projectile probe's result when 9B24 ran its
       scan this tick, otherwise the value the previous actor left. */
    int probe_ran = 0;
    scroll_actor(ram, actor, 0xb71a);
    if (read_word(ram, actor + 0x4e) && !decrement(ram, actor + 0x4e))
        write_word(ram, actor + 0x3a, 0);
    /* B000:9A3D..9AD1. While the escape timer runs (99F8), or when one of the
       three forward terrain probes is solid, steering and the projectile
       probe are skipped for this tick. */
    int evading = 0;
    if (read_word(ram, actor + 0x46)) {
        evading = 1;
        if (!decrement(ram, actor + 0x46)) {
            uint16_t speed = read_word(ram, actor + 0x2e);
            write_word(ram, actor + 0x38, (ram[actor + 0x29] & 1) ? (uint16_t)-speed : speed);
            write_word(ram, actor + 0x3a, 0);
            write_word(ram, actor + 0x4a, (world_random_byte(ram) & 15) + 0x30);
            write_word(ram, actor + 0x4c, 0);
            ram[actor + 0x64] &= 0xfe;
        }
    } else {
        static const int rows[3] = { 0, -8, 8 };
        for (unsigned i = 0; i < 3 && !evading; i++) {
            uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
            uint16_t map_y = 0x177 - (uint16_t)(y + rows[i]) + read_word(ram, 0xb70c);
            unsigned material = terrain[world_tile_address(map_x, map_y) + 3] >> 3;
            if (!(0xff02 & (1u << (material & 15)))) continue;
            unsigned index = (material - 8) & 6;
            write_word(ram, actor + 0x3a, read_word(rom + 0xb0000, 0x8d6c + index));
            write_word(ram, actor + 0x38, read_word(rom + 0xb0000,
                       0x8d64 + (index & 4) + 2 * (ram[actor + 0x29] & 1)));
            write_word(ram, actor + 0x46, 10);
            write_word(ram, actor + 0x4e, 0);
            evading = 1;
        }
    }
    if (evading) {
    } else if (!turns && !read_word(ram, actor + 0x4a)) {
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
    if (!evading && !(ram[actor + 0x64] & 1) && !decrement(ram, actor + 0x66)) {
        write_word(ram, actor + 0x66, read_word(ram, actor + 0x68));
        write_word(ram, actor + 0x3c, 0xf72b);
        ram[actor + 0x6a] = ram[actor + 0x29];
        ram[actor + 0x0d] = 3;
        uint16_t shot = world_projectile_contact(ram, rom, actor);
        probe_ran = 1;
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
                /* With neither player active 65B1 leaves SI untouched. */
                if (!players && !probe_ran) {
                    if (!entry_si_known) {
                        world_dispatch_detail = untracked_si_detail();
                        return 0;
                    }
                    probe_direction = entry_si & 1;
                }
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

uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);
uint16_t world_append_candidate(uint8_t *, uint16_t);

static uint16_t effect_slot(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t slot = world_spawn_child(ram, parent, handler);
    return slot ? slot : 0x8480;
}

static uint16_t stage_two_machine_parent(uint8_t *ram, uint16_t actor)
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

static uint16_t bank_b_effect(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t child = effect_slot(ram, parent, handler);
    if (child != 0x8480) write_word(ram, child + 2, 0xb000);
    return child;
}

static int player_near_turret(const uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static int turret_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void turret_effect(uint8_t *ram, const uint8_t *bank, uint16_t actor, int opening)
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

static void spawn_stage_pickup(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0xea17);
    if (child != 0x8480) write_word(ram, child + 2, 0x7000);
    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
    write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
}

static int outside_pickup_area(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    uint16_t base = read_word(ram, 0xb732);
    uint16_t top = ram[0x26e] & 2 ? 0x68 : base + 0x10;
    uint16_t bottom = ram[0x26e] & 2 ? 0x198 : base + 0x1c8;
    return x > 0x1c0 || x < 0x40 || y > bottom || (int16_t)y < (int16_t)top;
}

/* B7147 attaches a ship part and retires it when the parent is removed. */
static int warship_attachment(uint8_t *ram, uint16_t actor)
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

static void aim_warship_turret(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned facing)
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

static void warship_part_explosion(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    world_spawn_radial_explosion(ram, rom, actor);
}

static void boss_chain_move(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    write_word(ram, actor + 0x20, read_word(ram, parent + 0x20));
    write_word(ram, actor + 0x22, read_word(ram, parent + 0x22));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
}

static void boss_limb_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    uint16_t speed = read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        uint16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2*axis, ((uint32_t)unit * speed) >> 8);
    }
}

static uint16_t spawn_boss_part(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t part = effect_slot(ram, actor, handler);
    if (part != 0x8480) write_word(ram, part + 2, 0x1000);
    return part;
}

static void boss_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t entry)
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

static int boss_claw_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
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

static void boss_appendage(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
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

/* 0:7036: a 7056 actor that plays the blast list at segment:table. */
static void blast_sequence(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t table, uint16_t delay)
{
    uint16_t child = effect_slot(ram, actor, 0x7056);
    write_word(ram, child + 0x60, table);
    write_word(ram, child + 0x62, segment);
    write_word(ram, child + 0x4c, delay);
    write_word(ram, child + 0x42, 1);
}

static void warship_blast_sequence(uint8_t *ram, uint16_t actor, uint16_t table, uint16_t delay)
{
    blast_sequence(ram, actor, 0xb000, table, delay);
}

static int spawn_warship_parts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static int advance_timed_sprite(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t table)
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

static void spawn_bomb_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static uint16_t mission_child(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = effect_slot(ram, actor, handler);
    if (child != 0x8480) write_word(ram, child + 2, 0xa000);
    return child;
}

static void mission_text(uint8_t *ram, const uint8_t *bank, uint16_t actor)
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

static void start_mission_scroll(uint8_t *ram, const uint8_t *bank, uint16_t actor)
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

static void update_mission_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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
static void init_stage_list(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void init_player_slot(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned player)
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

static void set_projectile_trail_length(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
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

static void init_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
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

static void cycle_torpedo_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor,
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

static void spawn_impact(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    write_word(ram, child + 0x4c, read_word(rom, 0x6ad9));
    write_word(ram, child + 0x4a, read_word(rom, 0x6ade));
    ram[child + 0x2c] = rom[0x6ae3];
    ram[child + 0x2d] = rom[0x6ae7];
    write_word(ram, child + 0x56, read_word(rom, 0x6aeb));
    write_word(ram, child + 0x58, read_word(rom, 0x6af0));
}

static void update_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
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
        if (impact)
            write_word(ram, actor + 0x40, 0);
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

static int update_powered_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
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

/* B000:C52A. Every part of the freeze launcher follows the depth-4 actor
   of its chain, read from the 8500 depth table. Returns 0 once that actor
   has retired. */
static int freeze_follow_root(uint8_t *ram, uint16_t actor, uint16_t *root)
{
    uint16_t depth = read_word(ram, actor + 8);
    *root = read_word(ram, (uint16_t)(0x8500 + depth - 2 * (uint16_t)((uint16_t)(depth - 4) >> 1)));
    if (ram[*root + 0x0a]) return 0;
    write_word(ram, actor + 0x21, read_word(ram, *root + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, *root + 0x24));
    return 1;
}

/* B000:C616 / C6FD. The four things the beam can freeze: both submarines
   (tested against shape 78DB) and the paired enemy submarines registered
   at B8B8/B8BA. Each owns one bit of B8B2. With capture set, the first
   unfrozen victim touching the actor's shape is sealed in a C0C0 block. */
static int freeze_victim(uint8_t *ram, const uint8_t *rom, uint16_t actor, int capture)
{
    uint16_t shape = read_word(ram, actor + 0x3c);
    for (unsigned slot = 0; slot < 8; slot += 2) {
        if (read_word(ram, 0xb8b2) & (1u << slot)) continue;
        uint16_t victim, other;
        if (slot < 4) {
            victim = read_word(ram, 0x90 + slot);
            if (read_word(ram, victim + 0x3c) == 0xffff) continue;
            other = 0x78db;
        } else {
            victim = read_word(ram, 0xb8b8 + slot - 4);
            if (!victim || read_word(ram, victim) == 0xffff) continue;
            other = read_word(ram, victim + 0x3c);
        }
        if (!world_shape_overlap(ram, rom, actor, victim, shape, other)) continue;
        if (!capture) return 1;
        write_word(ram, 0xb8b2, read_word(ram, 0xb8b2) | (1u << slot));
        uint16_t block = bank_b_effect(ram, actor, 0xc0c0);
        write_word(ram, block + 0x21, read_word(ram, victim + 0x21));
        write_word(ram, block + 0x24, read_word(ram, victim + 0x24));
        ram[block + 0x29] = ram[actor + 0x29];
        write_word(ram, block + 0x60, read_word(ram, actor + 0x60));
        write_word(ram, block + 0x68, slot);
        write_word(ram, block + 0x6a, victim);
        ram[block + 0x0b] = ram[victim + 0x0b];
        return 1;
    }
    return 0;
}

/* The frozen tile patch remembered at 4C (cell) and 4A (rows): four cells
   per row, rows wrapping at 64, columns stepping within the low byte. */
static uint16_t freeze_next_cell(uint16_t cell)
{
    return (cell & 0xff00) | (uint8_t)(cell + 4);
}

static uint16_t freeze_next_row(uint16_t cell)
{
    return (uint16_t)((((cell >> 8) + 1) & 0x3f) << 8) | (uint8_t)(cell - 0x10);
}

/* B000:C564: write the material words listed at table over the map cells
   starting at the given offset from the actor. */
static void freeze_tiles(uint8_t *ram, const uint8_t *bank, uint8_t *terrain, uint16_t actor, uint16_t table)
{
    write_word(ram, actor + 0x66, read_word(ram, actor + 0x66) | 1);
    uint16_t x = read_word(ram, actor + 0x21) + read_word(bank, table);
    uint16_t y = read_word(ram, actor + 0x24) + read_word(bank, (uint16_t)(table + 2));
    uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
    table += 4;
    write_word(ram, actor + 0x4c, cell);
    for (;;) {
        for (unsigned i = 0; i < 4; ++i) {
            uint16_t material = read_word(bank, table);
            if (material == 0xffff) return;
            write_word(terrain, (uint16_t)(cell + 2), (read_word(terrain, (uint16_t)(cell + 2)) & 0x7ff) | material);
            table += 2;
            cell = freeze_next_cell(cell);
        }
        cell = freeze_next_row(cell);
        write_word(ram, actor + 0x4a, read_word(ram, actor + 0x4a) + 1);
    }
}

/* B000:C5AF (thaw to 3000h) and C5DB (4000h cells become 4800h). */
static void freeze_patch_tiles(uint8_t *ram, uint8_t *terrain, uint16_t actor, int thaw)
{
    uint16_t cell = read_word(ram, actor + 0x4c);
    unsigned rows = thaw ? read_word(ram, actor + 0x4a) : ram[actor + 0x4a];
    do {
        for (unsigned i = 0; i < 4; ++i) {
            uint16_t word = read_word(terrain, (uint16_t)(cell + 2));
            if (thaw) write_word(terrain, (uint16_t)(cell + 2), (word & 0x7ff) | 0x3000);
            else if ((word & 0xf800) == 0x4000) write_word(terrain, (uint16_t)(cell + 2), (word & 0x7ff) | 0x4800);
            cell = freeze_next_cell(cell);
        }
        cell = freeze_next_row(cell);
        rows = thaw ? (uint16_t)(rows - 1) : (uint8_t)(rows - 1);
        if (thaw) write_word(ram, actor + 0x4a, rows);
    } while (rows);
}

/* 0:6B2E: sound 5D and the 6EFE blast preset at 6B36. 1769 leaves the
   actor's previous successor in SI when it finds a slot. */
static void spawn_kill_blast(uint8_t *ram, uint16_t actor)
{
    uint16_t successor = read_word(ram, actor + 4);
    world_queue_sound(ram, 0x5d);
    uint16_t blast = effect_slot(ram, actor, 0x6efe);
    write_word(ram, blast + 0x4c, 0x74c3);
    write_word(ram, blast + 0x4a, 0x7675);
    ram[blast + 0x2c] = 0x0a;
    ram[blast + 0x2d] = 0x20;
    write_word(ram, blast + 0x56, 0x30);
    write_word(ram, blast + 0x58, 0x80);
    if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
    if (blast != 0x8480) leave_si(successor);
}

/* D237 loads SI with the actor's shape before testing any player whose own
   shape is present. Call before world_player_contact. */
static void player_contact_si(const uint8_t *ram, uint16_t actor)
{
    if (read_word(ram, read_word(ram, 0x90) + 0x3c) != 0xffff ||
        read_word(ram, read_word(ram, 0x92) + 0x3c) != 0xffff)
        leave_si(read_word(ram, actor + 0x3c));
}

/* 0DA4/0DD6 as the latest SI load on a path that continues. */
static void sprite_si(void)
{
    if (queued_sprite_entry) leave_si(queued_sprite_entry);
}

static uint16_t swap16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

/* 1000:1F36: shared tail of the boss torpedo states. */
static void boss_torpedo_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    queue_sprite(ram, rom, actor, 1);
    ram[actor + 0x0d] = 2;
    int dead = world_player_contact(ram, rom, actor) != 0;
    if (!dead) {
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, rom, actor);
        dead = !read_word(ram, actor + 0x26);
    }
    if (dead) {
        spawn_kill_blast(ram, actor);
        finish_animation(ram, actor);
    } else if (read_word(ram, actor + 0x3c) != 0xffff)
        world_append_candidate(ram + 0xa6e2, actor);
}

/* 0:ABB7 (init), 0:AC0E (launch) and 0:AC65 (cruise): the second powered
   primary weapon. It drops 0:AD5F blast actors along its path and returns
   its stock to the launcher at parent + 46 when it ends. */
static int update_blast_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                uint16_t actor, uint32_t handler)
{
    if (handler == 0xabb7) {
        unsigned level = 4 * read_word(ram, actor + 0x76);
        uint16_t health = read_word(rom, (uint16_t)(0xabab + level));
        world_acquire_resource(ram, actor, read_word(rom, 0x9bb1 + (ram[actor + 0x0b] & 2)));
        write_word(ram, actor + 0x3c, 0xaba3);
        write_word(ram, actor + 0x26, health);
        write_word(ram, actor + 0x6e, health);
        write_word(ram, actor + 0x36, read_word(rom, (uint16_t)(0xabad + level)));
        write_word(ram, actor + 0x38, read_word(rom, 0xab8f + 2 * (ram[actor + 0x29] & 1)));
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor + 0x5a, 7);
        write_word(ram, actor + 0x5c, 8);
        write_word(ram, actor, 0xac0e);
    } else
        scroll_actor(ram, actor, 0xb71a);
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    if (handler != 0xac65) {
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(rom, (uint16_t)(0xa9ab + frame));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) << 1);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x42, 8);
                write_word(ram, actor + 0x58, 0x2b);
                write_word(ram, actor + 0x5a, 3);
                write_word(ram, actor + 0x5c, 4);
                write_word(ram, actor, 0xac65);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x58, read_word(rom, (uint16_t)(0xa9b7 + frame)));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, rom, actor, 1);
    } else {
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + read_word(ram, actor + 0x58));
        write_word(ram, actor + 0x2a, read_word(rom, 0xa9c1 + 2 * (ram[actor + 0x41] & 3)));
        queue_sprite(ram, rom, actor, 1);
        if (read_word(ram, actor + 0x42) && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) << 1);
            write_word(ram, actor + 0x58, 0x80);
            write_word(ram, actor + 0x5a, 2);
            write_word(ram, actor + 0x5c, 2);
            write_word(ram, actor + 0x0e, 0x120);
        }
    }
    if (!decrement(ram, actor + 0x5a)) {
        write_word(ram, actor + 0x5a, read_word(ram, actor + 0x5c));
        uint16_t blast = effect_slot(ram, actor, 0xad5f);
        unsigned index = 2 * (read_word(ram, actor + 0x76) & 3);
        write_word(ram, blast + 0x21, read_word(ram, actor + 0x21));
        write_word(ram, blast + 0x24, read_word(ram, actor + 0x24));
        ram[blast + 0x0b] = ram[actor + 0x0b];
        ram[blast + 0x2d] = ram[actor + 0x2d] + 1;
        write_word(ram, blast + 0x76, read_word(rom, 0xaa4f + index));
        write_word(ram, blast + 0x6c, index << 4);
        write_word(ram, blast + 0x6a, read_word(ram, actor + 0x6a));
        write_word(ram, actor + 0x6a, read_word(ram, actor + 0x6a) + 1);
    }
    if (read_word(ram, actor + 0x26)) {
        int16_t x = read_word(ram, actor + 0x21);
        write_word(ram, actor + 0x26, read_word(ram, actor + 0x6e));
        if (x <= 0x200 && x >= 0) {
            uint16_t cell = world_tile_address(x + 0x10 - 0x60 + read_word(ram, 0xb70a),
                                              0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
            unsigned material = terrain[cell + 3] >> 3;
            if (!(0x5502 & (1u << (material & 15)))) {
                if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                return 1;
            }
            /* A3B7 calls 7000:9D93 for this stage-five material. */
            if (read_word(ram, 0x84) == 4 && material == 1) return 0;
            spawn_impact(ram, rom, actor);
        }
    } else
        spawn_impact(ram, rom, actor);
    uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
    write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) + 1);
    finish_animation(ram, actor);
    return 1;
}

static void update_sub_display(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void fire_secondary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

/* A2C0 flash shared by the secondary launchers (A7C4, A89B, A94F). */
static void secondary_launch_flash(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t flash = effect_slot(ram, actor, 0xa2c0);
    ram[flash + 0x29] = ram[actor + 0x29];
    write_word(ram, flash + 0x31, read_word(rom, 0x78c7 + 2 * (ram[actor + 0x29] & 1)));
    write_word(ram, flash + 0x34, 4);
    ram[flash + 0x2d] = ram[actor + 0x2d];
    write_word(ram, flash + 0x60, read_word(ram, actor + 0x60));
}

/* 0:A7FD: secondary launcher mode 4. Level 3 releases one BAA5 shot and
   reloads from the A9A3 table; lower levels release BC73 shots while fewer
   than level + 1 are out. */
static void fire_secondary_spread(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned input = ram[0xb73e] & 3;
    uint8_t angle = ram[actor + 0x28];
    if (!input) {
        if (angle < 0x40) angle += 2;
        else if (angle > 0x40) angle -= 2;
    } else if (input & 1) {
        if (angle > 0x28) angle -= 2;
    } else if (angle < 0x58)
        angle += 2;
    ram[actor + 0x28] = angle;
    if (read_word(ram, actor + 0x4c)) {
        decrement(ram, actor + 0x4c);
        return;
    }
    if (!(ram[0xb742] & 0x40)) {
        if (!read_word(ram, actor + 0x48)) write_word(ram, actor + 0x46, 0);
        return;
    }
    unsigned owner = ((ram[actor + 0x0b] & 3) - 1) & 15;
    uint16_t level = read_word(ram, actor + 0x7a);
    if (read_word(ram, actor + 0x60) == 3) {
        uint16_t shot = world_spawn_child(ram, actor, 0xbaa5);
        if (!shot) return;
        ram[shot + 0x2d] = ram[actor + 0x2d];
        ram[shot + 0x0b] = ram[actor + 0x0b];
        write_word(ram, 0xb8a2, read_word(ram, 0xb8a2) | (1u << owner));
        write_word(ram, shot + 0x60, read_word(ram, actor + 0x60));
        write_word(ram, shot + 0x7a, level + 1);
        write_word(ram, actor + 0x4c, read_word(rom, 0xa9a3 + 2 * ((level - 1) & 3)));
        secondary_launch_flash(ram, rom, actor);
        return;
    }
    if ((int16_t)read_word(ram, actor + 0x48) >= (int16_t)(level + 1)) return;
    uint16_t shot = world_spawn_child(ram, actor, 0xbc73);
    if (!shot) return;
    world_queue_sound(ram, 0x38);
    ram[shot + 0x29] = ram[actor + 0x29];
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + read_word(rom, 0xa99f + 2 * (ram[actor + 0x29] & 1)));
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 6);
    ram[shot + 0x2d] = ram[actor + 0x2d] + 1;
    ram[shot + 0x0b] = ram[actor + 0x0b];
    write_word(ram, 0xb8a2, read_word(ram, 0xb8a2) | (1u << owner));
    write_word(ram, shot + 0x60, read_word(ram, actor + 0x60));
    ram[shot + 0x28] = (angle + 4) & 0xf8;
    write_word(ram, shot + 0x46, read_word(ram, actor + 0x46));
    write_word(ram, shot + 0x54, read_word(ram, actor + 0x54) & 3);
    write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) + 1);
    write_word(ram, actor + 0x46, read_word(ram, actor + 0x46) + 1);
    secondary_launch_flash(ram, rom, actor);
    write_word(ram, actor + 0x48, read_word(ram, actor + 0x48) + 1);
    write_word(ram, actor + 0x4c, 8);
}

/* A480/A535/A5EA launch the three powered primary weapons. */
static void fire_primary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t mode)
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

static int update_idle_equipment(uint8_t *ram, const uint8_t *rom, uint16_t actor, int secondary)
{
    uint16_t sub = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
    uint16_t config = read_word(ram, sub + 0x70);
    if (secondary) {
        uint16_t launcher = read_word(ram, config + 0x14);
        if (read_word(ram, config + 0x16) && launcher != 2 && launcher != 4)
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
        /* A993 start routines for modes 2 and 4 both centre the aim; A999
           selects A70D or A7FD. */
        uint16_t launcher = read_word(ram, config + 0x14);
        if (read_word(ram, actor + 0x78) != launcher) {
            write_word(ram, actor + 0x78, launcher);
            ram[actor + 0x28] = 0x40;
        }
        if (launcher == 2) fire_secondary_launcher(ram, rom, actor);
        else fire_secondary_spread(ram, rom, actor);
    }
    return 1;
}

static uint16_t draw_join_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label)
{
    while (rom[label] != 0xff) {
        write_word(tiles, dest, rom[label++]);
        write_word(tiles, dest + 2, 0x104);
        dest = (dest & 0xff00) | ((dest + 4) & 255);
    }
    return dest;
}

static void tally_tile(uint8_t *tiles, uint16_t dest, uint16_t glyph, unsigned attr)
{
    write_word(tiles, dest, glyph);
    write_word(tiles, dest + 2, attr);
}

static void tally_block(uint8_t *tiles, uint16_t dest, unsigned width, unsigned height, unsigned glyph, unsigned attr)
{
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            tally_tile(tiles, (((dest + 256*y) & 0x3f00) | ((dest + 4*x) & 255)), glyph++, attr);
}

static void tally_stamp(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t shape, unsigned attr)
{
    tally_block(tiles, dest, rom[shape + 1], rom[shape], read_word(rom, shape + 2), attr);
}

static void tally_large_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label, unsigned attr)
{
    for (; rom[label] != 255; ++label) {
        tally_block(tiles, dest, 2, 2, 256 + 4*(rom[label] - 48), attr);
        dest = (dest & 0xff00) | ((dest + 8) & 255);
    }
}

static void tally_score(const uint8_t *ram, const uint8_t *rom, uint8_t *tiles, unsigned side)
{
    tally_large_label(tiles, rom, read_word(rom, 0x22ab + 2*side), read_word(rom, 0x2457 + 2*side), 0x105);
    uint16_t score = read_word(ram, 0x258 + 2*side) + 3, dest = read_word(rom, 0x22af + 2*side);
    for (unsigned n = 0; n < 8; ++n) {
        unsigned digit = (ram[score - n/2] >> ((n & 1) ? 0 : 4)) & 15;
        tally_block(tiles, dest, 2, 2, 256 + 4*digit, 0x106);
        dest = (dest & 0xff00) | ((dest + 8) & 255);
    }
}

static void name_glyph(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    uint16_t index=read_word(ram,actor+0x40);
    unsigned glyph=(uint16_t)(256+4*(rom[(uint16_t)(0xd036+index)]-48));
    tally_block(tiles,read_word(ram,actor+0x52),2,2,glyph,0x104);
}

static void finish_name_selection(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    write_word(ram,actor+0x40,0x27);
    name_glyph(ram,rom,tiles,actor);
    write_word(ram,actor+0x42,64);
    write_word(ram,actor,0xcc89);
}

static void name_input(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
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

static void tally_bonus(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
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

static int tally_skip_requested(const uint8_t *ram)
{
    return ((ram[0x266] & 1) && (ram[0xb743] & 0xf0)) ||
           ((ram[0x266] & 2) && (ram[0xb742] & 0xf0));
}

static void tally_add_count(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, unsigned type)
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

static void tally_groups(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, int large)
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


static void update_join_hud(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
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

static void update_rank(uint8_t *ram, const uint8_t *rom)
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

static void load_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void advance_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if ((int16_t)read_word(ram, actor + 0x46) <= (int16_t)read_word(ram, 0xb700))
        load_scroll_script(ram, rom, actor);
}

static void update_camera_limits(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t row = read_word(ram, actor + 0x68);
    if (read_word(rom, row) <= read_word(ram, 0xb700)) {
        for (unsigned i = 0; i < 4; i++)
            write_word(ram, 0xb734 + 2*i, read_word(rom, row + 2 + 2*i));
        ram[0x26e] = rom[(uint16_t)(row + 10)];
        write_word(ram, actor + 0x68, row + 12);
    }
}

static void init_tile_layer(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned layer)
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

static void update_layer_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void update_tile_script(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
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
static void queue_palette(uint8_t *ram, unsigned slot, uint16_t palette, uint16_t speed)
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
static int digit_product(int value, int factor)
{
    return (int8_t)((uint16_t)(value * factor) >> 8);
}

static void project_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void queue_continue_sound(uint8_t *ram, uint16_t command)
{
    uint16_t next = (read_word(ram, 0xa6dc) + 2) & 14;
    if (next != read_word(ram, 0xa6da)) {
        write_word(ram, 0xa6dc, next);
        write_word(ram, 0xa6b2 + next, command);
    }
}

static void shatter_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void digit_fragment_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    int16_t speed = read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        int16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2*axis, (unit * speed) >> 8);
    }
}

static void acquire_private_palette(uint8_t *ram, uint16_t actor, unsigned resource)
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

static void refresh_stage_time(uint8_t *ram, uint8_t *tiles, uint16_t actor, unsigned seconds)
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

static void expire_stage_time(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void update_stage_trigger(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void enemy_torpedo_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned mask)
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

static void water_entry_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y)
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

static uint16_t spawn_surface_splash_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor,
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

static void spawn_surface_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y)
{
    unsigned scroll = read_word(ram, 0xb70c) & 7;
    y = ((y - scroll) & 0xfff8) + scroll;
    ram[actor + 0x23] = 0;
    write_word(ram, actor + 0x24, y);
    spawn_surface_splash_effect(ram, rom, actor, x, y);
}

static int update_vertical_weapon(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain,
                                  uint16_t actor, uint16_t handler)
{
    int rising = handler != 0xa0a9;
    int surface = handler == 0x9fcd || handler == 0x9fda;
    if ((ram[actor + 0x0b] & 0x80) || read_word(ram, 0x84) == 4)
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
            if (impact) spawn_impact(ram, rom, actor);
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
            } else if (impact)
                spawn_impact(ram, rom, actor);
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

int world_horizontal_camera(uint8_t *, const uint8_t *, uint16_t, uint16_t, uint16_t);

static void update_vertical_camera(uint8_t *ram, const uint8_t *rom, uint16_t actor)
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

static void update_background_vertical(uint8_t *ram, uint16_t actor)
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

static void update_camera_layers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles,
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

int world_sub_open_water(uint8_t *, const uint8_t *, uint8_t *, uint16_t);
void world_sub_lives_hud(const uint8_t *, uint8_t *, unsigned, uint16_t);
void world_sub_entry_step(uint8_t *, const uint8_t *, uint8_t *, uint16_t);

static void surface_enemy_pose(uint8_t *ram, const uint8_t *bank, uint16_t actor, int magnitude)
{
    uint16_t index = ((uint16_t)(magnitude - 1) >> 5) & 14;
    write_word(ram, actor + 0x4c, read_word(bank, 0x2760 + index));
    write_word(ram, actor + 0x3c, read_word(bank, 0x2770 + index));
    write_word(ram, actor + 0x4a, read_word(bank, 0x2780 + index));
    write_word(ram, actor + 0x60, (index + 2) * 2);
}

static void surface_enemy_fire(uint8_t *ram, uint16_t actor)
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

static int surface_enemy_swim(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles,
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

static int boss_segment_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler)
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

/* ---- Stage-five ice boss, 7000:7FFE..93F7 and its 7000:9DD9 ice debris ---- */

static uint16_t s5_spawn(uint8_t *ram, uint16_t parent, uint16_t handler)
{
    uint16_t child = effect_slot(ram, parent, handler);
    if (child != 0x8480) write_word(ram, child + 2, 0x7000);
    return child;
}

static uint16_t s5_parent(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, (uint16_t)(0x84fe + read_word(ram, actor + 8)));
}

/* 0:6812: playfield (D400) cell under a screen position. */
static uint16_t s5_cell(const uint8_t *ram, uint16_t x, uint16_t y)
{
    uint16_t bw = (uint16_t)((uint16_t)(x - 0x60 + read_word(ram, 0xb70a)) & 0xfff8) >> 1;
    uint16_t cw = (uint16_t)((uint16_t)(0x177 - y + read_word(ram, 0xb70c)) & 0xfff8) >> 3;
    return (uint16_t)(((uint8_t)(cw + 0x11) << 8) | (uint8_t)(bw + 0x30)) & 0x3fff;
}

static uint16_t s5_tile(const uint8_t *tiles, uint16_t cell)
{
    return read_word(tiles, 0x4000 + cell);
}

static void s5_set_tile(uint8_t *tiles, uint16_t cell, uint16_t code, uint16_t attribute)
{
    write_word(tiles, 0x4000 + cell, code);
    write_word(tiles, 0x4000 + cell + 2, attribute);
}

static int s5_is_ice(const uint8_t *tiles, uint16_t cell)
{
    return (s5_tile(tiles, cell + 2) & 0xf800) == 0x800;
}

/* 7000:9FD1, the shared tail of the three edge routines. */
static void s5_ice_tail(uint8_t *tiles, uint16_t bw, uint16_t iy)
{
    if ((s5_tile(tiles, iy + 2) & 0xf880) == 0x3080)
        s5_set_tile(tiles, iy, (((bw >> 1) + 2) & 15) + 0x310, 0x300e);
}

/* 7000:9F72 (phase 0) and 7000:9FF4 (phase 2). */
static void s5_ice_edge(uint8_t *tiles, const uint8_t *bank, uint16_t bw, unsigned phase)
{
    uint16_t above = (bw - 0x100) & 0x3fff;
    if (s5_is_ice(tiles, above)) {
        unsigned kind = ((s5_tile(tiles, above + 2) & 0x7f) - 8) & 3;
        uint16_t row = read_word(bank, 0x97b8 + 2 * kind) + (((bw >> 1) + phase) & 6);
        s5_set_tile(tiles, bw, read_word(bank, row), 0x308f);
    } else
        s5_set_tile(tiles, bw, (((bw >> 1) + (phase ? 8 : 0)) & 15) + 0x310, 0x300e);
    s5_ice_tail(tiles, bw, phase ? (bw + 0x100) & 0x3fff : (above + 0x200) & 0x3fff);
}

/* 7000:A05F. */
static void s5_ice_clear(uint8_t *tiles, uint16_t bw)
{
    s5_set_tile(tiles, bw, (((bw >> 1) + 12) & 15) + 0x310, 0x300e);
    s5_ice_tail(tiles, bw, (bw + 0x100) & 0x3fff);
}

/* 7000:9DAF: break the ice cell at BW and throw a 7000:9DD9 shard from the
   actor's (temporarily moved) position. BW is preserved for the caller. */
static void s5_break_ice(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint16_t bw)
{
    const uint8_t *bank = rom + 0x70000;
    if (!s5_is_ice(tiles, bw)) return;
    unsigned kind = ((s5_tile(tiles, bw + 2) & 0x7f) - 8) & 3;
    if (kind < 2) {
        s5_ice_edge(tiles, bank, bw, 0);
        bw = (bw & 0xff00) | (uint8_t)(bw + (kind ? -4 : 4));
        if (s5_is_ice(tiles, bw)) s5_ice_edge(tiles, bank, bw, 0);
    } else if (kind == 2) {
        s5_ice_edge(tiles, bank, bw, 2);
        bw = (bw + 0x100) & 0x3fff;
        if (s5_is_ice(tiles, bw)) s5_ice_clear(tiles, bw);
    } else {
        s5_ice_clear(tiles, bw);
        bw = (bw - 0x100) & 0x3fff;
        if (s5_is_ice(tiles, bw)) s5_ice_edge(tiles, bank, bw, 2);
    }
    uint16_t shard = s5_spawn(ram, read_word(ram, 0x9a), 0x9dd9);
    write_word(ram, shard + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, shard + 0x24, read_word(ram, actor + 0x24));
    if (kind < 2)
        write_word(ram, shard + 0x42, 0x20);
    else {
        write_word(ram, shard + 0x1e, 2);
        write_word(ram, shard + 0x42, 0x14);
    }
}

/* 7000:95A8 / 95E8 / 9628: four rows of `columns` cells, walking upward. */
static void s5_break_rows(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor,
                          uint16_t bw, unsigned columns, unsigned cell_back, unsigned x_back)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    for (unsigned row = 4; row; --row) {
        if ((int16_t)read_word(ram, actor + 0x24) < 0x88) break;
        for (unsigned column = columns; column; --column) {
            s5_break_ice(ram, rom, tiles, actor, bw);
            bw = (bw & 0xff00) | (uint8_t)(bw + 4);
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + 8);
        }
        bw = (bw & 0xff00) | (uint8_t)(bw - cell_back);
        bw = (bw & 0x00ff) | (((bw >> 8) + 1) & 0x3f) << 8;
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - x_back);
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 8);
    }
    write_word(ram, actor + 0x21, x);
    write_word(ram, actor + 0x24, y);
}

/* 7000:9546: the hull breaks the ice above it. */
static void s5_hull_break(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    write_word(ram, actor + 0x21, x - 0x14);
    write_word(ram, actor + 0x24, y + 0x6c);
    uint16_t bw = s5_cell(ram, x - 0x14, y + 0x6c);
    s5_break_rows(ram, rom, tiles, actor, bw, 6, 0x18, 0x30);
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - 0x10);
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 0x10);
    bw = ((bw & 0xff00) | (uint8_t)(bw - 8)) + 0x200;
    bw &= 0x3fff;
    s5_break_rows(ram, rom, tiles, actor, bw, 2, 8, 0x50);
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + 0x40);
    bw = (bw & 0xff00) | (uint8_t)(bw + 0x20);
    s5_break_rows(ram, rom, tiles, actor, bw, 2, 8, 0x50);
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - 0x78);
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 0x30);
    bw = ((bw & 0xff00) | (uint8_t)(bw - 0x3c)) + 0x600;
    bw &= 0x3fff;
    s5_break_rows(ram, rom, tiles, actor, bw, 7, 0x1c, 0x38);
    write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + 0x88);
    bw = (bw & 0xff00) | (uint8_t)(bw + 0x44);
    s5_break_rows(ram, rom, tiles, actor, bw, 7, 0x1c, 0x38);
    write_word(ram, actor + 0x21, x);
    write_word(ram, actor + 0x24, y);
}

/* 7000:9668: an arm frame breaks the ice patch listed for it at 7DD8. */
static void s5_arm_break(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint16_t frame)
{
    const uint8_t *bank = rom + 0x70000;
    uint16_t patch = read_word(bank, (uint16_t)(0x7dd8 + frame));
    if (!patch || (int16_t)read_word(ram, actor + 0x24) < 0x88) return;
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    uint16_t px = read_word(bank, (uint16_t)(patch + 2 * (ram[actor + 0x29] & 1))) + x - read_word(ram, actor + 0x31);
    uint16_t py = y + read_word(bank, (uint16_t)(patch + 4));
    write_word(ram, actor + 0x21, px);
    write_word(ram, actor + 0x24, py);
    write_word(ram, actor + 0x4e, read_word(bank, (uint16_t)(patch + 6)));
    uint16_t bw = s5_cell(ram, px, py);
    uint8_t rows = ram[actor + 0x4e];
    do {
        uint8_t columns = ram[actor + 0x4f];
        do {
            s5_break_ice(ram, rom, tiles, actor, bw);
            bw = (bw & 0xff00) | (uint8_t)(bw + 4);
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + 8);
        } while (--columns);
        uint16_t back = 8 * ram[actor + 0x4f];
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - back);
        bw = (bw & 0xff00) | (uint8_t)(bw - (back >> 1));
        bw = (bw & 0x00ff) | (((bw >> 8) + 1) & 0x3f) << 8;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 8);
    } while (--rows);
    write_word(ram, actor + 0x21, x);
    write_word(ram, actor + 0x24, y);
}

/* 7000:96E8: one splash and three chunks, facing as the caller. */
static void s5_debris(uint8_t *ram, uint16_t actor)
{
    uint16_t child = s5_spawn(ram, actor, 0x8cfb);
    ram[(uint16_t)(child + 0x29)] = ram[actor + 0x29];
    for (unsigned kind = 0; kind < 12; kind += 4) {
        child = s5_spawn(ram, actor, 0x8d90);
        ram[(uint16_t)(child + 0x29)] = ram[actor + 0x29];
        write_word(ram, child + 0x1e, kind);
    }
    world_queue_sound(ram, 0x58);
}

/* Palette command slot 9EA6 written directly (no 262h gate). */
static void s5_flash(uint8_t *ram, uint16_t palette, uint16_t speed)
{
    write_word(ram, 0x9ea6, palette);
    write_word(ram, 0x9ea8, 0x80);
    write_word(ram, 0x9eaa, speed);
    ram[0x9eac] = 0x20;
    ram[0x9ead] = 0x10;
}

/* 0:D237 with 0D=2, then 0:D2B7 with 0D=`shots`. *bx receives the BX that
   D2B7 leaves: the consumed shot, else the last slot it read, else D237's. */
static uint16_t s5_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint8_t shots, uint16_t *bx)
{
    ram[actor + 0x0d] = 2;
    uint16_t player = world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = shots;
    uint16_t count = read_word(ram, 0xa6e2);
    uint16_t left = player ? player : read_word(ram, 0x92);
    if (count) left = read_word(ram, (uint16_t)(0xa6e6 + ((count - 1) & 0xfffe)));
    uint16_t hit = world_projectile_contact(ram, rom, actor);
    if (bx) *bx = hit ? hit : left;
    return hit;
}

/* 0:657D / 0:6580: velocity from angle byte 28 and speed word 2E. */
static void s5_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    int16_t speed = read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        int16_t unit = read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2 * axis, (uint16_t)(((int32_t)unit * speed) >> 8));
    }
}

/* 7000:945E: the core rides the hull, shaking for 78h ticks after a hit. */
static void s5_core_follow(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = s5_parent(ram, actor), shake = read_word(ram, actor + 0x78);
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) - (shake & 2));
    if (shake) write_word(ram, actor + 0x78, shake - 1);
}

/* 7000:948B. */
static void s5_core_guarded(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t hit = s5_contacts(ram, rom, actor, 1, 0);
    if (!hit || read_word(ram, hit + 0x0e) != 0x2f) return;
    write_word(ram, actor + 0x78, 0x20);
    uint16_t parent = s5_parent(ram, actor);
    if ((read_word(ram, parent + 0x50) & 0x20) && read_word(ram, actor + 0x26)) {
        decrement(ram, actor + 0x26);
        write_word(ram, parent + 0x5e, read_word(ram, parent + 0x5e) + 0x20);
    }
}

/* 7000:94CE. */
static void s5_core_exposed(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t bx;
    uint16_t hit = s5_contacts(ram, rom, actor, 0, &bx);
    if (hit && read_word(ram, hit + 0x0e) == 0x2f) write_word(ram, actor + 0x78, 0x20);
    if (read_word(ram, actor + 0x26)) return;
    if (!(read_word(ram, 0x264) & 3)) {
        write_word(ram, actor + 0x26, 0x100);
        return;
    }
    write_word(ram, actor + 0x26, 0);
    ram[actor + 0x0a] |= 2;
    uint16_t tally = (uint16_t)(0x124 + ((read_word(ram, (uint16_t)(bx + 0x0b)) & 2) << 5));
    write_word(ram, tally, read_word(ram, tally) | 0x10);
    world_award_score(ram, rom, actor, bx);
    ram[0x262] |= 0x80;
}

/* 7000:952A. */
static void s5_core_draw(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (ram[actor + 0x19]) {
        --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x15 + (ram[actor + 0x19] & 1)];
    }
    queue_sprite(ram, rom, actor, 0);
}

static int stage_five_boss(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x70000;
#define R(o) read_word(ram, (uint16_t)(actor + (o)))
#define S(o, v) write_word(ram, (uint16_t)(actor + (o)), (uint16_t)(v))
#define T(o) read_word(bank, (uint16_t)(o))
#define B(o) ram[(uint16_t)(actor + (o))]
#define DRAW() queue_sprite(ram, rom, actor, 0)
#define MOVE_X() move_actor_axis(ram, actor + 0x20, R(0x38))
#define MOVE_Y() move_actor_axis(ram, actor + 0x23, R(0x3a))
#define MOVE_OFFSET() move_actor_axis(ram, actor + 0x33, R(0x3a))
    uint16_t parent = s5_parent(ram, actor);
    int16_t floor_y = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));

    switch (address) {
    /* ---- hull ---- */
    case 0x77ffe: {
        ram[0x26f] |= 0x80;
        S(0x0e, 0x48);
        B(0x2c) = 0x17;
        B(0x2d) = 0x30;
        S(0x2a, 0x5f49);
        S(0x3c, 0xf8eb);
        S(0x36, 0x7f00);
        S(0x60, 0x80);
        uint16_t rank = T(0x7fce + read_word(ram, 0x8a));
        S(0x58, T(0x7ff0 + rank));
        S(0x5a, T(0x7ff0 + rank));
        s5_spawn(ram, actor, 0x8eb5);
        uint16_t child = s5_spawn(ram, actor, 0x8a19);
        write_word(ram, child + 0x31, 0x40);
        write_word(ram, 0xb8c6, child);
        child = s5_spawn(ram, actor, 0x8a19);
        write_word(ram, child + 0x31, 0xffc0);
        ram[(uint16_t)(child + 0x29)] = 1;
        write_word(ram, 0xb8c8, child);
        child = s5_spawn(ram, actor, 0x8337);
        write_word(ram, 0xb8c4, child);
        S(0, 0x8085);
        return 1;
    }
    case 0x78085:
        S(0x24, R(0x24) + read_word(ram, 0xb71c));
        DRAW();
        if (R(0x50) & 2) {
            ram[0x26f] &= 0x7f;
            S(0x40, 6);
            S(0x44, 1);
            S(0x52, R(0x52) | 3);
            S(0, 0x81ed);
        }
        s5_contacts(ram, rom, actor, 1, 0);
        return 1;
    case 0x780c9: {
        S(0x24, R(0x24) + read_word(ram, 0xb71c));
        if (R(0x5e)) {
            S(0x3a, T(0x7fca + (R(0x5e) & 2)));
            MOVE_Y();
            if (!(R(0x5e) & 15)) {
                B(0x29) = 1;
                s5_debris(ram, actor);
                B(0x29) = 0;
                s5_debris(ram, actor);
            }
            decrement(ram, actor + 0x5e);
        }
        if (R(0x78)) {
            S(0x24, R(0x24) + 1 - (R(0x78) & 2));
            decrement(ram, actor + 0x78);
        }
        DRAW();
        if (!(R(0x50) & 0x20) && (int16_t)R(0x60) <= (int16_t)read_word(ram, 0xb700)) {
            S(0x60, R(0x60) + 0x80);
            S(0x62, 0x80);
            S(0x40, 0);
            S(0x44, 1);
            S(0x52, R(0x52) | 3);
            S(0, 0x81ed);
        }
        if (read_word(ram, 0xb700) >= 0xb00 && !(R(0x54) & 2) && !decrement(ram, actor + 0x58)) {
            S(0x58, R(0x5a));
            S(0x54, R(0x54) | 2);
        }
        uint16_t hit = s5_contacts(ram, rom, actor, 1, 0);
        if (hit && read_word(ram, hit + 0x0e) == 0x2f) {
            S(0x78, (R(0x78) & 3) + 0x20);
            S(0x6c, 1);
            S(0x6e, 0);
        }
        if (R(0x6c) && !decrement(ram, actor + 0x6c)) {
            uint16_t step = R(0x6e);
            s5_flash(ram, T(0x7f18 + step), T(0x7f1a + step));
            S(0x6c, T(0x7f1c + step));
            S(0x6e, step + 6);
        }
        if (B(0x0a) & 2) {
            ram[0x262] |= 0x80;
            S(0, 0x826a);
        }
        return 1;
    }
    case 0x781ed:
        if (!tiles) return 0;
        S(0x24, R(0x24) + read_word(ram, 0xb71c));
        if (R(0x50) & 4) {
            S(0x50, R(0x50) & ~4);
            S(0x24, R(0x24) + 0x20);
            S(0x62, R(0x62) - 0x20);
            S(0x70, R(0x70) + 1);
            s5_hull_break(ram, rom, tiles, actor);
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7c10 + frame);
            if (sprite == 0xffff) {
                if (read_word(ram, 0xb700) >= 0xb00 && (int16_t)R(0x24) > 0x88) {
                    unsigned tail = (read_word(ram, 0xa6d8) + 2) & 14;
                    if (tail != read_word(ram, 0xa6d6)) {
                        write_word(ram, 0xa6d8, tail);
                        write_word(ram, 0xa6a2 + tail, 0x11);
                    }
                    S(0x50, R(0x50) | 0x20);
                }
                S(0, 0x80c9);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7c26 + frame));
                S(0x40, frame + 2);
            }
        }
        DRAW();
        s5_contacts(ram, rom, actor, 1, 0);
        return 1;
    case 0x7826a:
        s5_flash(ram, 0x1ee, 4);
        S(0x42, 0x40);
        S(0x40, 3);
        S(0, 0x8295);
        /* fall through */
    case 0x78295:
        if (!decrement(ram, actor + 0x42)) {
            if (!decrement(ram, actor + 0x40)) {
                S(0x3a, 0);
                S(0x42, 0x100);
                B(0x0a) |= 4;
                S(0, 0x8304);
            } else {
                S(0x42, 0x20);
                S(0, 0x82c6);
            }
        }
        DRAW();
        return 1;
    case 0x782c6:
        S(0x3a, T(0x7fca + (R(0x42) & 2)));
        MOVE_Y();
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            S(0x42, 0x20);
            S(0, 0x8295);
        }
        if (!(R(0x42) & 15)) {
            B(0x29) = 1;
            s5_debris(ram, actor);
            B(0x29) = 0;
            s5_debris(ram, actor);
        }
        return 1;
    case 0x78304:
        S(0x3a, R(0x3a) - 0x10);
        MOVE_Y();
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            ram[0x263] |= 0xc0;
            finish_animation(ram, actor);
        }
        return 1;

    /* ---- core ---- */
    case 0x78337:
        world_acquire_resource(ram, actor, 0xc5);
        S(0x0e, 0x48);
        B(0x2c) = 0x17;
        B(0x15) = 0x17;
        B(0x16) = 0x1d;
        B(0x2d) = 0x30;
        S(0x2a, 0x0af3);
        S(0x3c, 0xf8d3);
        S(0x44, 0x80);
        S(0x26, 4);
        S(0x36, 0x7f00);
        S(0, 0x8373);
        return 1;
    case 0x78373:
        s5_core_follow(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            S(0x44, 0x9c);
            S(0x42, 0xa5);
            s5_spawn(ram, actor, 0x89b1);
            S(0, 0x839e);
        }
        DRAW();
        s5_core_guarded(ram, rom, actor);
        return 1;
    case 0x7839e:
        if (!tiles) return 0;
        s5_core_follow(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7ac0 + frame);
            if (sprite == 0xffff) {
                write_word(ram, parent + 0x50, read_word(ram, parent + 0x50) | 2);
                S(0x42, 0x200);
                S(0, 0x8442);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7ac8 + frame));
                S(0x40, frame + 2);
            }
        }
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            world_queue_sound(ram, 0x40);
            uint16_t slot = 0x9e96;
            for (uint16_t source = 0xeb62; read_word(rom + 0xa0000, source) != 0xffff; source += 2, slot += 8) {
                write_word(ram, slot, read_word(rom + 0xa0000, source) + 0x100);
                write_word(ram, slot + 2, 0x80);
                write_word(ram, slot + 4, 5);
                ram[(uint16_t)(slot + 6)] = 0x20;
                ram[(uint16_t)(slot + 7)] = 0x10;
            }
            /* 0:37D6 mode 3: eight rows of ten blocks, drawn bottom row first. */
            uint16_t source = 0x5bbc, row = 0x2d30;
            write_word(ram, 0xb89a, 0x0a08);
            for (unsigned y = 0; y < 8; ++y) {
                uint16_t dest = row;
                for (unsigned x = 0; x < 10; ++x) {
                    world_draw_tile_block(tiles, rom, 0x4000, dest, read_word(rom + 0xa0000, source));
                    source += 2;
                    dest = (dest & 0xff00) | (uint8_t)(dest + 0x10);
                }
                if (y != 7) row = (row - 0x400) & 0x3fff;
            }
            write_word(ram, 0xb89c, row);
        }
        s5_core_guarded(ram, rom, actor);
        return 1;
    case 0x78442:
        s5_core_follow(ram, actor);
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            S(0x40, 0);
            S(0x44, 1);
            S(0x42, 0x50);
            S(0, 0x84a3);
        }
        s5_core_guarded(ram, rom, actor);
        if ((int16_t)R(0x26) <= 0) {
            uint16_t health = T(0x7fee + T(0x7fce + read_word(ram, 0x8a)));
            S(0x26, health);
            S(0x36, health);
            S(0x40, 0);
            S(0x44, 1);
            write_word(ram, parent + 0x50, read_word(ram, parent + 0x50) | 8);
            S(0, 0x84fa);
        }
        return 1;
    case 0x784a3:
        s5_core_follow(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7af8 + frame);
            if (sprite == 0xffff) {
                S(0x42, 0x100);
                S(0, 0x8442);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7b0c + frame));
                S(0x40, frame + 2);
                if (frame == 8) world_queue_sound(ram, 0x42);
            }
        }
        DRAW();
        if (!decrement(ram, actor + 0x42)) s5_spawn(ram, actor, 0x87c5);
        s5_core_guarded(ram, rom, actor);
        return 1;
    case 0x784fa:
        s5_core_follow(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7b7c + frame);
            if (sprite == 0xffff) {
                S(0x42, 0x100);
                S(0, 0x8534);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7b8c + frame));
                S(0x40, frame + 2);
            }
        }
        DRAW();
        s5_core_guarded(ram, rom, actor);
        return 1;
    case 0x78534:
        s5_core_follow(ram, actor);
        s5_core_draw(ram, rom, actor);
        if (!decrement(ram, actor + 0x42)) {
            S(0x40, 0);
            S(0x44, 1);
            S(0, 0x855e);
        }
        s5_core_exposed(ram, rom, actor);
        if (!R(0x26)) S(0, 0x85e2);
        return 1;
    case 0x7855e:
        s5_core_follow(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7b9a + frame);
            if (sprite == 0xffff) {
                S(0x42, 4);
                do {
                    uint16_t shot = s5_spawn(ram, actor, 0x862b);
                    uint16_t index = R(0x42) - 1;
                    ram[(uint16_t)(shot + 0x28)] = bank[(uint16_t)(0x7bfc + index)];
                    index <<= 2;
                    write_word(ram, shot + 0x21, R(0x21) + T(0x7c00 + index));
                    write_word(ram, shot + 0x24, R(0x24) + T(0x7c02 + index));
                    ram[(uint16_t)(shot + 0x2c)] = B(0x14);
                } while (decrement(ram, actor + 0x42));
                S(0x42, 0x100);
                S(0, 0x8534);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7ba8 + frame));
                S(0x40, frame + 2);
            }
        }
        s5_core_draw(ram, rom, actor);
        s5_core_exposed(ram, rom, actor);
        if (!R(0x26)) S(0, 0x85e2);
        return 1;
    case 0x785e2:
        blast_sequence(ram, actor, 0x7000, 0x7f56, 2);
        S(0x42, 0x28);
        S(0, 0x85fa);
        /* fall through */
    case 0x785fa:
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            ram[(uint16_t)(parent + 0x0a)] |= 2;
            finish_animation(ram, actor);
        }
        return 1;

    /* ---- core shot: turns toward the nearer player for 300h ticks ---- */
    case 0x7862b: {
        B(0x2d) = 4;
        S(0x3c, 0xf94b);
        S(0x2e, 0x80);
        B(0x0c) = 4;
        S(0x26, 0x80);
        S(0x36, 0x80);
        S(0x44, 1);
        S(0x42, 0x80);
        S(0x46, 0x300);
        s5_velocity(ram, rom, actor);
        unsigned facing = B(0x28) >> 4;
        B(0x29) = bank[0x7bec + facing];
        S(0x4c, T(0x7bdc + ((facing << 1) & 7)));
        S(0, 0x8680);
    }
        /* fall through */
    case 0x78680:
        if (ram[(uint16_t)(parent + 0x0a)] & 2) {
            finish_animation(ram, actor);
            return 1;
        }
        if (R(0x46)) {
            decrement(ram, actor + 0x46);
            if (!decrement(ram, actor + 0x42)) {
                S(0x42, 0x10);
                if (read_word(ram, 0x264) & 3) {
                    if (!world_enemy_target_mode_supported(B(0x0c))) {
                        world_dispatch_detail = "boss shot target mode";
                        return 0;
                    }
                    uint16_t target = world_enemy_target(ram, actor);
                    uint8_t wanted = (uint8_t)(aim_angle(rom, read_word(ram, target + 0x21) - R(0x21),
                                                         read_word(ram, target + 0x24) - R(0x24)) + 8) & 0xf0;
                    uint8_t angle = B(0x28);
                    if (wanted != angle) {
                        angle += (uint8_t)(wanted - angle) < (uint8_t)(angle - wanted) ? 8 : -8;
                        B(0x28) = angle;
                        s5_velocity(ram, rom, actor);
                        unsigned facing = angle >> 4;
                        B(0x29) = bank[0x7bec + facing];
                        S(0x4c, T(0x7bdc + ((facing & 7) << 1)));
                        S(0x44, 1);
                    }
                }
            }
        }
        MOVE_X();
        MOVE_Y();
        {
            uint16_t x = R(0x21), y = R(0x24);
            uint16_t ceiling = (read_word(ram, 0x26e) & 2) ? 0x198 : (uint16_t)(0x1c8 + read_word(ram, 0xb732));
            if (x > 0x1c0 || x < 0x40 || y > ceiling || (int16_t)y < floor_y) {
                finish_animation(ram, actor);
                return 1;
            }
        }
        if (!decrement(ram, actor + 0x44)) {
            S(0x2a, T(0x7bb4 + (R(0x40) & 6) + R(0x4c)));
            S(0x44, 4);
            S(0x40, R(0x40) + 2);
        }
        DRAW();
        B(0x0d) = 2;
        if (world_player_contact(ram, rom, actor)) {
            finish_animation(ram, actor);
            return 1;
        }
        B(0x0d) = 0;
        world_projectile_contact(ram, rom, actor);
        if (!R(0x26)) finish_animation(ram, actor);
        return 1;

    /* ---- core burst and its five falling pieces ---- */
    case 0x787c5:
        B(0x2c) = 0x19;
        B(0x2d) = 6;
        S(0x44, 1);
        S(0, 0x87d8);
        return 1;
    case 0x787d8:
        S(0x21, read_word(ram, parent + 0x21));
        S(0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7b1e + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                return 1;
            }
            S(0x2a, sprite);
            S(0x44, T(0x7b30 + frame));
            S(0x40, frame + 2);
            if (frame == 4) {
                S(0x42, 0);
                do {
                    uint16_t piece = s5_spawn(ram, actor, 0x885f);
                    uint16_t index = R(0x42);
                    write_word(ram, piece + 0x31, T(0x7b68 + index));
                    write_word(ram, piece + 0x34, T(0x7b6a + index));
                    S(0x42, index + 4);
                } while (R(0x42) < 0x11);
            }
        }
        DRAW();
        return 1;
    case 0x7885f:
        B(0x2c) = 0x19;
        B(0x2d) = 7;
        S(0x3c, 0xf953);
        S(0x44, 1);
        S(0, 0x8877);
        return 1;
    case 0x78877:
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24) + R(0x34));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7b40 + frame);
            if (sprite == 0xffff) {
                S(0x40, 0);
                S(0x44, 1);
                S(0x3a, 0x80);
                S(0x42, 0x140);
                S(0, 0x88da);
            } else {
                S(0x2a, sprite);
                S(0x44, T(0x7b4a + frame));
                S(0x40, frame + 2);
            }
        }
        DRAW();
        return 1;
    case 0x788da:
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        MOVE_Y();
        if (R(0x24) >= 0x150 || !decrement(ram, actor + 0x42) ||
            (world_terrain_mask(ram, tiles + 0x4000, R(0x21), R(0x24) + 0x14) & 0xff02)) {
            S(0x40, 0);
            S(0x44, 1);
            S(0, 0x8971);
        } else if (!decrement(ram, actor + 0x44)) {
            S(0x2a, T(0x7b52 + (R(0x40) & 6)));
            S(0x44, 5);
            S(0x40, R(0x40) + 2);
        }
        DRAW();
        for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
            uint16_t player = read_word(ram, slot), shape = read_word(ram, player + 0x3c);
            if (shape != 0xffff && world_shape_overlap(ram, rom, actor, player, R(0x3c), shape))
                write_word(ram, player + 0x33, R(0x3a));
        }
        return 1;
    case 0x78971:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7b5a + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                return 1;
            }
            S(0x2a, sprite);
            S(0x44, T(0x7b62 + frame));
            S(0x40, frame + 2);
        }
        DRAW();
        return 1;
    case 0x789b1:
        B(0x2c) = 0x17;
        B(0x2d) = 0x30;
        S(0x44, 1);
        S(0, 0x89c4);
        return 1;
    case 0x789c4:
        S(0x21, read_word(ram, parent + 0x21));
        S(0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7ace + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                return 1;
            }
            S(0x2a, sprite);
            S(0x44, T(0x7ae4 + frame));
            S(0x40, frame + 2);
        }
        DRAW();
        return 1;

    /* ---- the two arms ---- */
    case 0x78a19: {
        B(0x2c) = 0x18;
        B(0x2d) = 0x2f;
        S(0x2a, 0x63ed);
        S(0x36, 0x7f00);
        S(0x48, T(0x7d56 + ((B(0x29) & 1) << 1)));
        for (unsigned part = 0; part < 2; ++part) {
            uint16_t child = s5_spawn(ram, actor, 0x8e51);
            ram[(uint16_t)(child + 0x29)] = B(0x29);
            ram[(uint16_t)(child + 0x2c)] = 0x18;
            ram[(uint16_t)(child + 0x2d)] = 0x30;
            write_word(ram, child + 0x2a, part ? 0x8283 : 0x82a5);
            write_word(ram, child + 0x48, 0x7c66);
            write_word(ram, child + 0x1e, part ? 0 : 4);
        }
        S(0, 0x8a92);
        return 1;
    }
    case 0x78a92: {
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24));
        uint16_t bit = 1u << (B(0x29) & 15);
        if (read_word(ram, parent + 0x52) & bit) {
            write_word(ram, parent + 0x52, read_word(ram, parent + 0x52) & ~bit);
            S(0, 0x8ad5);
        }
        DRAW();
        S(0x3c, 0xf8f3);
        B(0x0d) = 1;
        world_projectile_contact(ram, rom, actor);
        return 1;
    }
    case 0x78ad5:
        S(0x40, 0xfffe);
        S(0x44, 1);
        S(0, 0x8ae4);
        /* fall through */
    case 0x78ae4: {
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            S(0x40, R(0x40) + 2);
            uint16_t frame = R(0x40), sprite = T(0x7c3a + frame);
            if (sprite == 0xffff) {
                S(0x40, frame - 2);
                S(0x48, T(0x7d5a + ((B(0x29) & 1) << 1)));
                S(0, 0x8bac);
            } else {
                S(0x2a, sprite);
                S(0x34, T(0x7d5e + frame));
                S(0x44, T(0x7d06 + frame + R(0x48)));
                if (!(B(0x29) & 1) && frame == 10)
                    s5_spawn(ram, read_word(ram, 0x9c), 0x93e6);
                else if (frame == 8)
                    s5_debris(ram, actor);
            }
        }
        S(0x24, R(0x24) - R(0x34));
        DRAW();
        uint16_t shape = T(0x7d8a + (uint16_t)(R(0x40) << 1));
        if (shape) {
            S(0x3c, shape);
            B(0x0d) = 1;
            world_projectile_contact(ram, rom, actor);
        }
        S(0x3c, T(0x7d88 + (uint16_t)(R(0x40) << 1)));
        B(0x0d) = 1;
        world_projectile_contact(ram, rom, actor);
        return 1;
    }
    case 0x78bac: {
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24) - R(0x34));
        if (ram[(uint16_t)(parent + 0x0a)] & 4) S(0x2a, 0x6537);
        uint16_t bit = 1u << (B(0x29) & 15);
        if (read_word(ram, parent + 0x52) & bit) {
            write_word(ram, parent + 0x52, read_word(ram, parent + 0x52) & ~bit);
            S(0, 0x8c0c);
        }
        DRAW();
        S(0x3c, 0xf8fb);
        B(0x0d) = 1;
        world_projectile_contact(ram, rom, actor);
        S(0x3c, 0xf903);
        B(0x0d) = 1;
        world_projectile_contact(ram, rom, actor);
        return 1;
    }
    case 0x78c0c:
        S(0x40, 0xfffe);
        S(0x44, 1);
        S(0x52, R(0x52) | 4);
        S(0, 0x8c20);
        /* fall through */
    case 0x78c20: {
        if (!tiles) return 0;
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24) - R(0x34));
        if (!decrement(ram, actor + 0x44)) {
            S(0x40, R(0x40) + 2);
            uint16_t frame = R(0x40), sprite = T(0x7c4a + frame);
            if (sprite == 0xffff) {
                S(0x40, frame - 2);
                S(0, 0x8bac);
            } else {
                S(0x2a, sprite);
                S(0x34, T(0x7d6c + frame));
                S(0x44, T(0x7d22 + frame + R(0x48)));
                s5_arm_break(ram, rom, tiles, actor, frame);
                if (!(B(0x29) & 1) && frame == 0x16)
                    s5_spawn(ram, read_word(ram, 0x9c), 0x93e6);
                else if (frame == 0x14)
                    s5_debris(ram, actor);
                else if (!(B(0x29) & 1) && frame < 8)
                    write_word(ram, parent + 0x50, read_word(ram, parent + 0x50) | 4);
            }
        }
        DRAW();
        uint16_t shape = T(0x7da6 + (uint16_t)(R(0x40) << 1));
        if (shape) {
            S(0x3c, shape);
            B(0x0d) = 1;
            world_projectile_contact(ram, rom, actor);
        }
        S(0x3c, T(0x7da4 + (uint16_t)(R(0x40) << 1)));
        B(0x0d) = 1;
        world_projectile_contact(ram, rom, actor);
        return 1;
    }

    /* ---- splash (8CFB) and chunks (8D90) thrown by 96E8 ---- */
    case 0x78cfb:
    case 0x78d45:
        if (address == 0x78cfb) {
            world_acquire_resource(ram, actor, 0xc6);
            B(0x2d) = 0x28;
            S(0x44, 1);
            int mirrored = B(0x29) & 1;
            S(0x38, mirrored ? 0x0100 : 0xff00);
            S(0x21, read_word(ram, parent + 0x21) + (mirrored ? -0x98 : 0x98));
            S(0x24, read_word(ram, parent + 0x24) + 0x80);
            S(0, 0x8d45);
        } else
            scroll_actor(ram, actor, 0xb71a);
        MOVE_X();
        S(0x3a, R(0x3a) - 0x10);
        MOVE_Y();
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40), sprite = T(0x7e22 + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                return 1;
            }
            S(0x2a, sprite);
            S(0x44, 8);
            S(0x40, frame + 2);
        }
        DRAW();
        return 1;
    case 0x78d90:
    case 0x78dec:
        if (address == 0x78d90) {
            world_acquire_resource(ram, actor, 0xc7);
            S(0x3c, 0xfb4b);
            B(0x2d) = 0x28;
            S(0x44, 1);
            int mirrored = B(0x29) & 1;
            uint16_t kind = R(0x1e), speed = T(0x7e44 + kind);
            S(0x38, mirrored ? -speed : speed);
            S(0x3a, T(0x7e46 + kind));
            S(0x21, read_word(ram, parent + 0x21) + (mirrored ? -0x98 : 0x98));
            S(0x24, read_word(ram, parent + 0x24) + 0x80);
            S(0, 0x8dec);
        } else
            scroll_actor(ram, actor, 0xb71a);
        MOVE_X();
        S(0x3a, R(0x3a) - 0x10);
        MOVE_Y();
        if ((int16_t)R(0x24) < floor_y) {
            finish_animation(ram, actor);
            return 1;
        }
        if (!decrement(ram, actor + 0x44)) {
            S(0x2a, T(0x7e34 + (R(0x40) & 14)));
            S(0x44, 4);
            S(0x40, R(0x40) + 2);
        }
        DRAW();
        return 1;

    /* ---- arm segments riding an arm ---- */
    case 0x78e51:
        if (read_word(ram, parent + 0x52) & 4) {
            S(0x48, 0x7c9e);
            S(0, 0x8e70);
        }
        /* fall through */
    case 0x78e70: {
        uint16_t frame = read_word(ram, parent + 0x40);
        S(0x40, frame);
        uint16_t entry = (uint16_t)((frame << 2) + R(0x48) + R(0x1e));
        uint16_t dx = T(entry);
        S(0x31, dx);
        S(0x21, ((B(0x29) & 1) ? -dx : dx) + read_word(ram, parent + 0x21));
        S(0x34, T(entry + 2));
        S(0x24, T(entry + 2) + read_word(ram, parent + 0x24));
        DRAW();
        return 1;
    }

    /* ---- tentacle base (8EB5) ---- */
    case 0x78eb5: {
        B(0x2c) = 0x1e;
        B(0x2d) = 0x2f;
        B(0x0c) = 12;
        S(0x2a, 0x63db);
        uint16_t link = s5_spawn(ram, actor, 0x9153);
        write_word(ram, link + 0x1e, 3);
        S(0, 0x8edc);
        return 1;
    }
    case 0x78edc:
    case 0x78f3b: {
        int waiting = address == 0x78edc;
        if (waiting && read_word(ram, 0xb700) < 0xb00) return 1;
        if (!waiting) {
            S(0x21, read_word(ram, parent + 0x21));
            S(0x24, read_word(ram, parent + 0x24));
        }
        if ((read_word(ram, parent + 0x50) & 8) && (read_word(ram, parent + 0x54) & 2)) {
            unsigned players = read_word(ram, 0x264) & 3;
            if (players == 3 && !world_enemy_target_mode_supported(B(0x0c))) {
                world_dispatch_detail = "boss tentacle target mode";
                return 0;
            }
            uint16_t target = world_enemy_target(ram, actor);
            int left = players && read_word(ram, target + 0x21) < 0x100;
            if (waiting) {
                S(0x31, left ? -0x58 : 0x58);
                if (left) B(0x29) = 1;
                S(0x34, 0xff90);
                S(0x3a, 0x80);
                S(0x42, 0xe0);
                S(0x54, R(0x54) | 1);
                S(0, 0x900f);
            } else if (!players)
                write_word(ram, parent + 0x54, 0);
            else {
                S(0x31, left ? -0x58 : 0x58);
                S(0x3a, 0xff80);
                S(0x42, 0x48);
                S(0, 0x8fb1);
            }
        }
        if (!waiting) {
            B(0x29) = 0;
            DRAW();
            B(0x29) = 1;
            DRAW();
        }
        return 1;
    }
    case 0x78fb1:
    case 0x790fc:
        MOVE_OFFSET();
        S(0x21, read_word(ram, parent + 0x21));
        S(0x24, read_word(ram, parent + 0x24) + R(0x34));
        B(0x29) = 1;
        DRAW();
        B(0x29) = 0;
        DRAW();
        if (!decrement(ram, actor + 0x42)) {
            if (address == 0x78fb1) {
                if (R(0x31) & 0x8000) B(0x29) = 1;
                S(0x34, 0xff90);
                S(0x3a, 0x80);
                S(0x42, 0xe0);
                S(0x54, R(0x54) | 1);
                S(0, 0x900f);
            } else {
                write_word(ram, parent + 0x54, 0);
                S(0x54, 0);
                S(0, 0x8f3b);
            }
        }
        return 1;
    case 0x7900f:
    case 0x79044:
    case 0x79079:
    case 0x790ae:
        if (address == 0x7900f || address == 0x790ae) MOVE_OFFSET();
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24) + R(0x34));
        if (!decrement(ram, actor + 0x42)) {
            if (address == 0x7900f) {
                S(0x42, 0x20);
                S(0, 0x9044);
            } else if (address == 0x79044) {
                S(0x42, 0xa0);
                S(0x54, R(0x54) | 2);
                S(0, 0x9079);
            } else if (address == 0x79079) {
                S(0x3a, 0xff80);
                S(0x42, 0xe0);
                S(0, 0x90ae);
            } else {
                S(0x31, 0);
                S(0x34, 0xffdc);
                S(0x3a, 0x80);
                S(0x42, 0x48);
                S(0x54, 4);
                S(0, 0x90fc);
            }
        }
        return 1;

    /* ---- tentacle links (9153): each follows the one before it ---- */
    case 0x79153: {
        if (decrement(ram, actor + 0x1e)) {
            uint16_t link = s5_spawn(ram, actor, 0x9153);
            write_word(ram, link + 0x1e, R(0x1e));
        } else {
            S(0x3c, 0xf95b);
            S(0x36, 0x7f00);
        }
        B(0x2c) = 0x1e;
        B(0x2d) = 0x2f;
        S(0x2a, T(0x7eca + ((R(0x1e) & 3) << 1)));
        S(0, 0x9194);
    }
        /* fall through */
    case 0x79194:
        if (read_word(ram, parent + 0x54) & 1) {
            uint8_t facing = ram[(uint16_t)(parent + 0x29)];
            B(0x29) = facing;
            unsigned index = (R(0x1e) & 3) << 1;
            int16_t reach = T(0x7ed0 + index);
            S(0x34, reach);
            reach >>= 1;
            S(0x31, (facing & 1) ? -reach : reach);
            S(0x3a, T(0x7ed6 + index));
            S(0x54, 1);
            S(0, 0x91e5);
        }
        return 1;
    case 0x791e5:
    case 0x7923a:
    case 0x7928d:
    case 0x792ef:
    case 0x79343:
    case 0x79391:
        if (address == 0x7923a || address == 0x79343) MOVE_OFFSET();
        if (address == 0x7928d && !(decrement(ram, actor + 0x42) & 1)) {
            S(0x3a, -R(0x3a));
            MOVE_OFFSET();
        }
        S(0x21, read_word(ram, parent + 0x21) + R(0x31));
        S(0x24, read_word(ram, parent + 0x24) + R(0x34));
        if (address == 0x791e5) {
            if (read_word(ram, parent + 0x54) & 2) {
                S(0x42, 0x25);
                S(0x54, R(0x54) | 2);
                S(0, 0x923a);
            }
            DRAW();
        } else if (address == 0x79391) {
            if (read_word(ram, parent + 0x54) & 4) {
                S(0x54, 4);
                S(0, 0x9194);
            }
            DRAW();
        } else {
            DRAW();
            if (address == 0x7923a) {
                if (!decrement(ram, actor + 0x42)) {
                    S(0x42, 4);
                    S(0, 0x928d);
                }
            } else if (address == 0x7928d) {
                if (!R(0x42)) {
                    S(0x42, 0x40);
                    S(0, 0x92ef);
                }
            } else if (address == 0x792ef) {
                if (!decrement(ram, actor + 0x42)) {
                    S(0x3a, -(uint16_t)((int16_t)R(0x3a) >> 1));
                    S(0x42, 0x38);
                    S(0, 0x9343);
                }
            } else if (!decrement(ram, actor + 0x42))
                S(0, 0x9391);
        }
        if (!R(0x1e)) s5_contacts(ram, rom, actor, 1, 0);
        return 1;

    /* ---- screen shake (93E6) ---- */
    case 0x793e6:
        S(0x42, T(0x7e50));
        S(0x6c, 1);
        S(0, 0x93f7);
        /* fall through */
    case 0x793f7: {
        uint16_t step = R(0x40), x = T(0x7e54 + step);
        write_word(ram, 0xb70a, x);
        write_word(ram, 0xb71a, T(0x7e52 + step) - x);
        S(0x40, step + 2);
        if (!decrement(ram, actor + 0x6c)) {
            uint16_t flash = R(0x6e);
            S(0x6c, T(0x7ee0 + flash));
            s5_flash(ram, T(0x7edc + flash), T(0x7ede + flash));
            S(0x6e, flash + 6);
        }
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        return 1;
    }

    /* ---- ice shard (9DD9) ---- */
    case 0x79dd9:
        write_word(ram, 0xe0, read_word(ram, 0xe0) + 1);
        S(0x3c, 0xfb4b);
        B(0x2d) = 0;
        B(0x2c) = 0x9c;
        S(0x46, T(0x97d8 + (R(0x1e) & 2)));
        S(0x48, T(0x97dc + (R(0x1e) & 2)));
        S(0x3a, 0x100);
        S(0x44, 1);
        B(0x29) = (actor >> 8) & 1;
        S(0, 0x9e1a);
        return 1;
    case 0x79e1a:
        scroll_actor(ram, actor, 0xb71a);
        MOVE_Y();
        if ((int16_t)R(0x3a) > -0x200) S(0x3a, R(0x3a) - 0x10);
        if ((int16_t)R(0x24) < floor_y) {
            write_word(ram, 0xe0, read_word(ram, 0xe0) - 1);
            finish_animation(ram, actor);
            return 1;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = R(0x40);
            S(0x2a, T(frame + R(0x46)));
            S(0x44, T(frame + R(0x48)));
            S(0x40, frame + 2);
            if (R(0x40) >= R(0x42)) S(0x40, 0);
        }
        /* 9E93: a shard that misses the sprite budget retires without the E0 count. */
        if (!DRAW()) finish_animation(ram, actor);
        return 1;
    }
    world_dispatch_detail = "stage-five boss state not translated";
    return 0;
#undef R
#undef S
#undef T
#undef B
#undef DRAW
#undef MOVE_X
#undef MOVE_Y
#undef MOVE_OFFSET
}

/* Stage-two gunboat B000:380F and its parts: hull collision 4583, deck
   launcher 3B21 (missile 3C31), bridge 3E64, three pods 3F05 (depth charge
   40E4) and the hatch 4262 that releases B000:1C16 / DEDD. */
static int s2_score(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t hit)
{
    if (!hit) {
        /* A D2B7 miss leaves BX at the last shot slot it read. */
        uint16_t count = read_word(ram, 0xa6e2);
        if (!count) {
            world_dispatch_detail = "gunboat part scored with an empty shot list; 0:6237 reads leftover BX";
            return 0;
        }
        hit = read_word(ram, (uint16_t)(0xa6e6 + ((count - 1) & 0xfffe)));
    }
    world_award_score(ram, rom, actor, hit);
    return 1;
}

/* Copy the parent's position, then retire with a dead parent. */
static int s2_follow(uint8_t *ram, uint16_t actor, uint16_t parent)
{
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
    if (!ram[parent + 0x0a]) return 1;
    finish_animation(ram, actor);
    return 0;
}

/* The far table at B000:37D9: 0:6B2E, 6B91, 6D92, 6DC5. */
static uint16_t s2_wreck_blast(uint8_t *ram, uint16_t actor, uint16_t kind)
{
    if (kind == 0) world_queue_sound(ram, 0x5d);
    uint16_t blast = effect_slot(ram, actor, 0x6efe);
    write_word(ram, blast + 0x4c, kind == 0 ? 0x74c3 : kind == 8 ? 0x74a1 : 0x74e5);
    write_word(ram, blast + 0x4a, 0x7675);
    ram[blast + 0x2c] = 0x0a;
    ram[blast + 0x2d] = kind >= 8 ? 0x3f : 0x20;
    write_word(ram, blast + 0x56, (kind & 4) ? 0x40 : 0x30);
    write_word(ram, blast + 0x58, (kind & 4) ? 0 : 0x80);
    if (kind == 4) world_queue_sound(ram, 0x3e);
    if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
    return blast;
}

static void s2_smoke(uint8_t *ram, uint16_t actor, int tagged)
{
    uint16_t smoke = effect_slot(ram, read_word(ram, 0x9a), 0xbe41);
    write_word(ram, smoke + 0x21, read_word(ram, actor + 0x21) + 0x10);
    write_word(ram, smoke + 0x24, read_word(ram, actor + 0x24));
    if (tagged) write_word(ram, smoke + 0x1e, 1);
    ram[smoke + 0x2c] = 3;
}

static int stage_two_gunboat(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0xb0000;
    uint16_t parent = s5_parent(ram, actor), hit;
    switch (address) {
    case 0xb380f: {
        world_acquire_resource(ram, actor, 0x0c);
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x0e, 0x0a);
        write_word(ram, actor + 0x38, 0xffc0);
        write_word(ram, actor + 0x3c, 0xf3c3);
        uint16_t variant = (read_word(ram, actor + 0x1e) & 8) << 2;
        uint16_t row = 0x35f2 + read_word(bank, (uint16_t)(0x35b2 + read_word(ram, 0x8a) + variant));
        write_word(ram, actor + 0x44, read_word(bank, row));
        write_word(ram, actor + 0x46, read_word(bank, row));
        write_word(ram, actor + 0x4e, read_word(bank, (uint16_t)(row + 2)));
        write_word(ram, actor + 0x54, read_word(bank, (uint16_t)(row + 4)));
        write_word(ram, actor + 0x5a, read_word(bank, (uint16_t)(row + 6)));
        write_word(ram, actor + 0x52, read_word(bank, (uint16_t)(row + 8)));
        row = 0x364c + read_word(bank, (uint16_t)(0x35b2 + read_word(ram, 0x88) + variant));
        write_word(ram, actor + 0x48, read_word(bank, row));
        write_word(ram, actor + 0x4c, read_word(bank, (uint16_t)(row + 2)));
        write_word(ram, actor + 0x56, read_word(bank, (uint16_t)(row + 4)));
        write_word(ram, actor + 0x58, read_word(bank, (uint16_t)(row + 6)));
        write_word(ram, actor + 0x50, read_word(bank, (uint16_t)(row + 8)));
        write_word(ram, actor + 0x26, read_word(bank, (uint16_t)(row + 8)));
        begin_linear_script(ram, rom, actor, read_word(bank, 0x36a6 + ((read_word(ram, actor + 0x1e) & 0x70) >> 3)));
        uint16_t part = bank_b_effect(ram, actor, 0x4583);
        write_word(ram, part + 0x26, read_word(ram, actor + 0x50));
        write_word(ram, part + 0x36, read_word(ram, actor + 0x50));
        write_word(ram, part + 0x0e, 0x0a);
        part = bank_b_effect(ram, actor, 0x3b21);
        ram[part + 0x2c] = ram[actor + 0x2c];
        write_word(ram, part + 0x26, read_word(ram, actor + 0x56));
        write_word(ram, part + 0x36, read_word(ram, actor + 0x56));
        write_word(ram, part + 0x48, read_word(ram, actor + 0x48));
        write_word(ram, part + 0x4a, read_word(ram, actor + 0x48));
        write_word(ram, part + 0x2e, read_word(ram, actor + 0x4c));
        write_word(ram, part + 0x0e, 0x29);
        part = bank_b_effect(ram, actor, 0x3e64);
        ram[part + 0x2c] = ram[actor + 0x2c];
        write_word(ram, part + 0x26, read_word(ram, actor + 0x58));
        write_word(ram, part + 0x36, read_word(ram, actor + 0x58));
        write_word(ram, part + 0x0e, 0x29);
        for (unsigned pod = 0; pod < 6; pod += 2) {
            part = bank_b_effect(ram, actor, 0x3f05);
            ram[part + 0x2c] = ram[actor + 0x2c];
            write_word(ram, part + 0x26, read_word(ram, actor + 0x54));
            write_word(ram, part + 0x36, read_word(ram, actor + 0x54));
            write_word(ram, part + 0x3a, read_word(ram, actor + 0x4e));
            write_word(ram, part + 0x60, pod);
            write_word(ram, part + 0x0e, 0x29);
        }
        write_word(ram, actor + 0x60, 0x15);
        part = bank_b_effect(ram, actor, 0x4262);
        ram[part + 0x2c] = ram[actor + 0x2c];
        write_word(ram, part + 0x26, read_word(ram, actor + 0x5a));
        write_word(ram, part + 0x36, read_word(ram, actor + 0x5a));
        write_word(ram, part + 0x52, read_word(ram, actor + 0x52));
        write_word(ram, part + 0x1e, read_word(ram, actor + 0x1e));
        write_word(ram, actor, 0x39c5);
        break;
    }
    case 0xb39c5: {
        write_word(ram, actor + 0x62, 0);
        int step = step_flight_script(ram, rom, actor);
        if (!step) {
            world_dispatch_detail = "gunboat flight script kind";
            return 0;
        }
        if (step < 0 || (int16_t)read_word(ram, actor + 0x21) <= 0 ||
            (int16_t)read_word(ram, actor + 0x24) <= 0x48) {
            if (read_word(ram, actor + 8) == 6) decrement(ram, parent + 0x44);
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x2a, 0x3078);
        queue_sprite(ram, rom, actor, 1);
        if (read_word(ram, actor + 0x38)) {
            uint16_t wake = read_word(ram, actor + 0x40) + 0x40;
            write_word(ram, actor + 0x40, wake < 0x500 ? wake : 0);
        }
        write_word(ram, actor + 0x2a, read_word(bank, 0x3474 + 2 * ram[actor + 0x41]));
        queue_sprite(ram, rom, actor, 1);
        if (!read_word(ram, actor + 0x26)) {
            if (read_word(ram, actor + 8) == 6) decrement(ram, parent + 0x44);
            ram[actor + 0x0a] = 1;
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 0);
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor + 0x3a, 0xffc0);
            write_word(ram, actor, 0x3aa6);
            break;
        }
        if (!ram[actor + 0x60]) break;
        if (read_word(ram, actor + 0x44)) {
            decrement(ram, actor + 0x44);
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) & 0x7fff);
            break;
        }
        ram[actor + 0x0d] = 1;
        if (world_player_contact(ram, rom, actor)) {
            write_word(ram, actor + 0x60, read_word(ram, actor + 0x60) | 0x8000);
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
        }
        break;
    }
    case 0xb3aa6: {
        uint8_t frame = ram[actor + 0x41];
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x1a);
        uint16_t sprite = read_word(bank, 0x347e + 2 * frame);
        if (sprite == 0xffff) write_word(ram, actor, 0x3ac6);
        else write_word(ram, actor + 0x2a, sprite);
    }
        /* fall through */
    case 0xb3ac6: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, rom, actor, 1);
        if (decrement(ram, actor + 0x42)) break;
        write_word(ram, actor + 0x42, 8);
        uint16_t row = read_word(ram, actor + 0x44);
        write_word(ram, actor + 0x44, row + 6);
        uint16_t kind = read_word(bank, (uint16_t)(0x37e9 + row));
        if (kind == 0xffff) {
            finish_animation(ram, actor);
            break;
        }
        if (kind > 12 || (kind & 3)) {
            world_dispatch_detail = "gunboat wreck blast index";
            return 0;
        }
        uint16_t blast = s2_wreck_blast(ram, actor, kind);
        write_word(ram, blast + 0x31, read_word(ram, blast + 0x31) + read_word(bank, (uint16_t)(0x37eb + row)));
        write_word(ram, blast + 0x34, read_word(ram, blast + 0x34) + read_word(bank, (uint16_t)(0x37ed + row)));
        break;
    }
    case 0xb3b21:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, 0xf3a3);
        write_word(ram, actor + 0x2a, 0x30ec);
        write_word(ram, actor, 0x3b35);
        break;
    case 0xb3b35: {
        if (!s2_follow(ram, actor, parent)) break;
        queue_sprite(ram, rom, actor, 1);
        if (read_word(ram, actor + 0x21) >= 0xe0) {
            int second = -1;
            if (read_word(ram, actor + 0x48)) {
                if (!decrement(ram, actor + 0x48)) {
                    write_word(ram, actor + 0x44, 0x10);
                    second = 0;
                }
            } else if (!decrement(ram, actor + 0x44)) {
                write_word(ram, actor + 0x48, read_word(ram, actor + 0x4a));
                second = 1;
            }
            if (second >= 0) {
                uint16_t shot = bank_b_effect(ram, actor, 0x3c31);
                write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                write_word(ram, shot + 0x60, second ? 2 : 0);
                if (!second) world_queue_sound(ram, 0x65);
                uint16_t flash = bank_b_effect(ram, actor, 0x46d4);
                write_word(ram, flash + 0x34, second ? 0xfffe : 3);
            }
        }
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        if (hit) write_word(ram, parent + 0x62, read_word(ram, parent + 0x62) | 1);
        if (read_word(ram, actor + 0x26)) break;
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - 0x30);
        spawn_kill_blast(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb3c31:
        world_acquire_resource(ram, actor, 0x0d);
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x26, 8);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x2a, 0x359e);
        write_word(ram, actor + 0x3c, 0xf403);
        ram[actor + 0x0c] = 0x0a;
        write_word(ram, actor + 0x42, 4);
        write_word(ram, actor + 0x0e, 0xa10);
        write_word(ram, actor, 0x3c64);
        /* fall through */
    case 0xb3c64: {
        if (ram[actor + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) - 0x42);
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) +
                   read_word(bank, (uint16_t)(0x3528 + read_word(ram, actor + 0x60))));
        queue_sprite(ram, rom, actor, 1);
        if (decrement(ram, actor + 0x42)) break;
        write_word(ram, actor + 0x42, 4);
        uint16_t frame = read_word(ram, actor + 0x40);
        uint16_t sprite = read_word(bank, (uint16_t)(0x351c + frame));
        write_word(ram, actor + 0x2a, sprite);
        write_word(ram, actor + 0x40, frame + 2);
        if (sprite != 0xffff) break;
        write_word(ram, actor + 0x40, 0);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x2a, 0x35d6);
        uint16_t slot = 2;
        uint8_t angle = 0x80;
        if (read_word(ram, 0x264) & 3) {
            uint16_t target = world_enemy_target(ram, actor);
            int16_t band = (int16_t)(read_word(ram, actor + 0x24) - read_word(ram, target + 0x24) + 0x28) >> 4;
            slot = band < 0 ? 0 : band > 4 ? 4 : band;
            angle = bank[0x37d4 + slot];
        }
        ram[actor + 0x28] = angle;
        write_word(ram, actor + 0x2a, read_word(bank, 0x352c + 2 * slot));
        s5_velocity(ram, rom, actor);
        write_word(ram, actor + 0x42, 0x18);
        write_word(ram, actor + 0x48, 5);
        write_word(ram, actor, 0x3d2a);
        write_word(ram, actor + 0x0e, 0xa11);
        break;
    }
    case 0xb3d2a:
    case 0xb3e0d: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        if (address == 0xb3d2a) move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int out = x > 0x1c0 || x < 0x40;
        if (!out && address == 0xb3d2a) {
            int low = read_word(ram, 0x26e) & 2;
            uint16_t bottom = low ? 0x198 : (uint16_t)(0x1c8 + read_word(ram, 0xb732));
            int16_t top = low ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
            out = y > bottom || (int16_t)y < top;
        }
        if (out) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xb3d2a && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x38, (uint16_t)-(read_word(ram, actor + 0x2e) + 0x100));
            write_word(ram, actor + 0x2a, 0x35d6);
            write_word(ram, actor, 0x3e0d);
        }
        if (!decrement(ram, actor + 0x48)) {
            write_word(ram, actor + 0x48, 5);
            s2_smoke(ram, actor, address == 0xb3d2a);
        }
        queue_sprite(ram, rom, actor, 1);
        ram[actor + 0x0d] = 2;
        if (!world_player_contact(ram, rom, actor)) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, rom, actor);
            if (read_word(ram, actor + 0x26)) break;
        }
        spawn_impact(ram, rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb3e64:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, 0xf3ab);
        write_word(ram, actor + 0x2a, read_word(bank, 0x3486 + (read_word(ram, 0x274) & 2)));
        write_word(ram, actor, 0x3e83);
        break;
    case 0xb3e83:
        if (!s2_follow(ram, actor, parent)) break;
        queue_sprite(ram, rom, actor, 1);
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        /* The original sets the bit in this actor's own word 62. */
        if (hit) write_word(ram, actor + 0x62, read_word(ram, actor + 0x62) | 2);
        if (read_word(ram, actor + 0x26)) break;
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + 0x10);
        spawn_kill_blast(ram, actor);
        finish_animation(ram, actor);
        break;
    case 0xb3f05:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, 0xf3b3);
        write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x348a + read_word(ram, actor + 0x60))));
        write_word(ram, actor, 0x3f1f);
        break;
    case 0xb3f1f:
    case 0xb3fd9:
    case 0xb4059:
    case 0xb4094: {
        uint16_t pod = read_word(ram, actor + 0x60);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(bank, (uint16_t)(0x3490 + pod)));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + 8);
        if (ram[parent + 0x0a]) {
            if (address != 0xb3f1f) spawn_kill_blast(ram, actor);
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xb3f1f) {
            if (read_word(ram, parent + 0x60) & 0x8000) {
                write_word(ram, actor + 0x42, read_word(bank, (uint16_t)(0x3496 + pod)));
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor, 0x3fd9);
            }
        } else if (address == 0xb3fd9) {
            if (read_word(ram, actor + 0x42)) decrement(ram, actor + 0x42);
            else {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(0x349c + (pod << 3) + frame));
                if (sprite != 0xffff) {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x40, frame + 2);
                } else {
                    write_word(ram, actor + 0x42, 0x60);
                    uint16_t charge = world_spawn_child(ram, actor, 0x40e4);
                    if (charge) {
                        write_word(ram, charge + 2, 0xb000);
                        write_word(ram, charge + 0x3a, read_word(ram, actor + 0x3a));
                        world_queue_sound(ram, 0x54);
                        write_word(ram, actor, 0x4059);
                    }
                }
            }
        } else if (address == 0xb4059) {
            if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x4094);
        } else {
            uint16_t frame = read_word(ram, actor + 0x40) - 2;
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x349c + (pod << 3) + frame)));
            write_word(ram, actor + 0x40, frame);
            if (!frame) write_word(ram, actor, 0x3f1f);
        }
        queue_sprite(ram, rom, actor, 1);
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        if (hit) write_word(ram, parent + 0x62, read_word(ram, parent + 0x62) | 4);
        if (read_word(ram, actor + 0x26)) break;
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, parent + 0x60, read_word(ram, parent + 0x60) & ~(1u << (ram[actor + 0x60] & 15)));
        spawn_kill_blast(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb40e4:
        world_acquire_resource(ram, actor, 0x0d);
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x26, 8);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x2a, 0x3218);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x42, 4);
        write_word(ram, actor + 0x0e, 0xc00);
        write_word(ram, actor, 0x4113);
        /* fall through */
    case 0xb4113: {
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        write_word(ram, actor + 0x19, 0);
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 4);
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x34cc + frame));
            write_word(ram, actor + 0x3c, read_word(bank, (uint16_t)(0x34ea + frame)));
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x40, frame + 2);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x40, 0);
                ram[actor + 0x2d] = 4;
                write_word(ram, actor + 0x3c, 0xf3cb);
                write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + 0x30);
                write_word(ram, actor, 0x41b6);
                write_word(ram, actor + 0x0e, 0xc01);
            }
        }
        if (read_word(ram, actor + 0x3c) == 0xffff) break;
        ram[actor + 0x0d] = 2;
        if (!world_player_contact(ram, rom, actor)) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, rom, actor);
            if (read_word(ram, actor + 0x26)) break;
        }
        spawn_impact(ram, rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb41b6: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        write_word(ram, actor + 0x2a, read_word(bank, 0x3506 + (read_word(ram, actor + 0x40) & 6)));
        write_word(ram, actor + 0x19, 0);
        queue_sprite(ram, rom, actor, 1);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02)
            spawn_impact(ram, rom, actor);
        else if (terrain_material(ram, tiles + 0x4000, x, y) == 3)
            spawn_surface_splash_effect(ram, rom, actor, x, y);     /* 0:6D4D */
        else {
            ram[actor + 0x0d] = 2;
            if (!world_player_contact(ram, rom, actor)) {
                ram[actor + 0x0d] = 0;
                world_projectile_contact(ram, rom, actor);
                if (read_word(ram, actor + 0x26)) break;
            }
            spawn_impact(ram, rom, actor);
        }
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0x4226);
        break;
    }
    case 0xb4226: {
        scroll_actor(ram, actor, 0xb71a);
        uint8_t frame = ram[actor + 0x41];
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x40);
        uint16_t sprite = read_word(bank, 0x350e + 2 * frame);
        if (sprite == 0xffff) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x2a, sprite);
        write_word(ram, actor + 0x19, 0);
        queue_sprite(ram, rom, actor, 1);
        break;
    }
    case 0xb4262:
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, 0xf3bb);
        write_word(ram, actor + 0x2a, 0x338e);
        write_word(ram, actor + 0x30, read_word(bank, 0x3728 + (read_word(ram, actor + 0x1e) & 6)));
        write_word(ram, actor, 0x4285);
        break;
    case 0xb4285:
        if (!s2_follow(ram, actor, parent)) break;
        queue_sprite(ram, rom, actor, 1);
        if ((int16_t)read_word(ram, actor + 0x30) >= (int16_t)read_word(ram, actor + 0x21)) {
            write_word(ram, actor + 0x6e, 1);
            write_word(ram, actor + 0x40, 0);
            world_queue_sound(ram, 0x89);
            write_word(ram, actor, 0x433e);
        }
        goto hatch_tail;
    case 0xb433e:
        if (!s2_follow(ram, actor, parent)) break;
        if (!decrement(ram, actor + 0x6e)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x3536 + frame));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x6c, read_word(bank, 0x3730 + (ram[actor + 0x1f] & 6)));
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor + 0x5a, read_word(ram, actor + 0x26));
                write_word(ram, actor + 0x26, read_word(ram, actor + 0x52));
                write_word(ram, actor + 0x3c, 0xf383);
                write_word(ram, actor, 0x43c8);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x6e, read_word(bank, (uint16_t)(0x3546 + frame)));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, rom, actor, 1);
        goto hatch_tail;
    case 0xb44c4:
        if (!s2_follow(ram, actor, parent)) break;
        if (!decrement(ram, actor + 0x6e)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x3554 + frame)));
            write_word(ram, actor + 0x6e, read_word(bank, (uint16_t)(0x3562 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 1);
    hatch_tail:
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        /* The original sets the bit in this actor's own word 62. */
        if (hit) write_word(ram, actor + 0x62, read_word(ram, actor + 0x62) | 8);
        if (read_word(ram, actor + 0x26)) break;
        write_word(ram, actor + 0x0e, 0x29);
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + 0x20);
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 0x10);
        spawn_kill_blast(ram, actor);
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) - 0x20);
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + 0x10);
        write_word(ram, actor + 0x2a, 0x30ca);
        write_word(ram, actor + 0x26, read_word(ram, actor + 0x52));
        write_word(ram, actor + 0x3c, 0xf383);
        write_word(ram, actor, 0x451c);
        write_word(ram, actor + 0x0e, 0xd1);
        break;
    case 0xb43c8: {
        if (!s2_follow(ram, actor, parent)) break;
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t list = read_word(ram, actor + 0x6c);
            uint16_t kind = read_word(bank, list);
            write_word(ram, actor + 0x42, read_word(bank, (uint16_t)(list + 2)));
            if (kind == 0xffff) {
                write_word(ram, actor + 0x6e, 1);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x52, read_word(ram, actor + 0x26));
                write_word(ram, actor + 0x26, read_word(ram, actor + 0x5a));
                write_word(ram, actor + 0x3c, 0xf3bb);
                write_word(ram, actor, 0x44c4);
            } else {
                write_word(ram, actor + 0x6a, kind);
                uint16_t craft = world_spawn_child(ram, actor, read_word(bank, (uint16_t)(0x3776 + kind)));
                if (craft) {
                    uint16_t segment = read_word(bank, (uint16_t)(0x3778 + kind));
                    if (segment) write_word(ram, craft + 2, segment);
                    write_word(ram, craft + 0x21, read_word(ram, actor + 0x21) + read_word(bank, (uint16_t)(0x3786 + kind)));
                    write_word(ram, craft + 0x24, read_word(ram, actor + 0x24) + read_word(bank, (uint16_t)(0x3788 + kind)));
                    write_word(ram, craft + 0x1e, read_word(bank, (uint16_t)(0x3796 + (kind >> 1))));
                }
                write_word(ram, actor + 0x6c, list + 4);
            }
        }
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        if (read_word(ram, actor + 0x26)) break;
        write_word(ram, actor + 0x0e, 0x0a);
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 2);
        world_queue_sound(ram, 0x5f);
        write_word(ram, parent + 0x26, 0);
        finish_animation(ram, actor);
        break;
    }
    case 0xb451c:
        if (!s2_follow(ram, actor, parent)) break;
        queue_sprite(ram, rom, actor, 1);
        ram[actor + 0x0d] = 0;
        hit = world_projectile_contact(ram, rom, actor);
        if (read_word(ram, actor + 0x26)) break;
        write_word(ram, actor + 0x0e, 0x0a);
        if (!s2_score(ram, rom, actor, hit)) return 0;
        write_word(ram, parent + 0x26, 0);
        break;
    case 0xb4583: {
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (read_word(ram, parent + 0x62)) break;
        hit = 0;
        for (uint16_t shape = 0xf38b; shape <= 0xf39b; shape += 8) {
            write_word(ram, actor + 0x3c, shape);
            ram[actor + 0x0d] = 0;
            if ((hit = world_player_contact(ram, rom, actor)) != 0) break;
            ram[actor + 0x0d] = 0;
            if ((hit = world_projectile_contact(ram, rom, actor)) != 0) break;
        }
        if (!read_word(ram, actor + 0x26)) {
            write_word(ram, parent + 0x26, 0);
            if (!s2_score(ram, rom, actor, hit)) return 0;
        }
        if (ram[parent + 0x0a] || !read_word(ram, actor + 0x26)) finish_animation(ram, actor);
        break;
    }
    default:
        return 0;
    }
    return 1;
}

int world_dispatch_native_video(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom, uint8_t *tiles)
{
    uint32_t address;
    uint16_t actor = pass->actor;
    if (!world_dispatch_target(pass, ram, &address))
        return 0;
    entry_si_known = si_known;
    entry_si = si_value;
    entry_si_source = si_source;
    si_known = 0;
    si_source = address;
    switch (address) {
    case 0xb7960: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, read_word(bank, 0x7958 + (variant & 6)));
        write_word(ram, actor + 0x0e, 15);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 0x80);
        write_word(ram, actor + 0x36, 0x20);
        write_word(ram, actor + 0x3c, 0xf4db);
        ram[actor + 0x0c] = 10;
        write_word(ram, actor + 0x38, (variant & 1) ? 0x100 : -0x100);
        unsigned profile = 0x7944 + read_word(bank, 0x7904 + (variant & 32) + read_word(ram, 0x88));
        write_word(ram, actor + 0x50, read_word(bank, profile));
        write_word(ram, actor + 0x52, read_word(bank, profile));
        write_word(ram, actor + 0x2e, read_word(bank, profile + 2));
        write_word(ram, actor, 0x7aa9);
        break;
    }
    case 0xb79c5:
    case 0xb79d4:
    case 0xb7aa9:
    case 0xb7acd: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        int jumping = address == 0xb7aa9 || address == 0xb7acd;
        if (address == 0xb79c5) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x79d4);
        }
        if (address == 0xb7aa9) {
            world_queue_sound(ram, 0x8d);
            write_word(ram, actor + 0x2a, 0x3e56);
            unsigned index = ((read_word(ram, actor + 0x50) - 1) & 3) * 2;
            write_word(ram, actor + 0x3a, read_word(bank, 0x78e4 + index));
            write_word(ram, actor, 0x7acd);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (jumping) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
            int16_t lower = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            unsigned x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40 || (int16_t)read_word(ram, actor + 0x24) < lower) {
                finish_animation(ram, actor);
                break;
            }
        }
        unsigned phase = (uint16_t)(read_word(ram, actor + 0x46) + 64);
        if (phase >= 0xa00) phase = 0;
        write_word(ram, actor + 0x46, phase);
        write_word(ram, actor + 0x2a, read_word(bank, 0x7872 + 2*(phase >> 8)));
        queue_sprite(ram, main_rom, actor, 1);
        ++ram[actor + 0x19];
        if (!jumping) {
            if (!decrement(ram, actor + 0x44)) {
                unsigned frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x7886 + frame);
                if (sprite == 0xffff) write_word(ram, actor, 0x7aa9);
                else {
                    write_word(ram, actor + 0x54, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x7892 + frame));
                    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0x789c + frame));
                    if (frame == 4 && read_word(ram, actor + 0x21) >= 0xa0 &&
                        (ram[actor + 0x1e] & 4) && !decrement(ram, actor + 0x50)) {
                        if (ram[actor + 0x1e] & 2) {
                            uint16_t shot = bank_b_effect(ram, actor, 0x7cda);
                            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                            uint16_t speed = read_word(ram, actor + 0x2e);
                            write_word(ram, shot + 0x2e, speed + (speed >> 1));
                        }
                        write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                    }
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            write_word(ram, actor + 0x2a, read_word(ram, actor + 0x54));
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            write_word(ram, actor + 0x2a, 0x3e56);
            queue_sprite(ram, main_rom, actor, 1);
            if (read_word(ram, actor + 0x21) >= 0xa0 && !(ram[actor + 0x1e] & 4) &&
                !read_word(ram, actor + 0x3a) && !decrement(ram, actor + 0x50)) {
                if (ram[actor + 0x1e] & 2) {
                    uint16_t shot = bank_b_effect(ram, actor, 0x7cda);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                } else if (ram[0x264] & 3) {
                    uint16_t player = world_enemy_target(ram, actor);
                    uint16_t shot = bank_b_effect(ram, actor, 0x7c2e);
                    ram[shot + 0x29] = read_word(ram, player + 0x21) >= read_word(ram, actor + 0x21);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                } else {
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) + 1);
                    goto stage2_jumper_terrain;
                }
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            }
stage2_jumper_terrain:
            if ((int16_t)read_word(ram, actor + 0x3a) < 0 &&
                (world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                                     read_word(ram, actor + 0x24) - 8) & 0xff02)) {
                unsigned scroll = read_word(ram, 0xb70c) & 7;
                uint16_t y = read_word(ram, actor + 0x24);
                ram[actor + 0x23] = 0;
                write_word(ram, actor + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
                write_word(ram, actor, 0x79c5);
            } else {
                uint16_t x = read_word(ram, actor + 0x21) + ((int16_t)read_word(ram, actor + 0x38) >> 5);
                uint16_t y = read_word(ram, actor + 0x24) + 8;
                unsigned cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
                for (unsigned row = 0; row < 3; ++row) {
                    unsigned material = tiles[0x4000 + ((cell + 256*row) & 0x3fff) + 3] >> 3;
                    unsigned mask = 1u << (material & 15);
                    if (!(mask & 0xff02)) continue;
                    write_word(ram, actor + 0x60, mask);
                    write_word(ram, actor + 0x38, -read_word(ram, actor + 0x38));
                    break;
                }
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (read_word(ram, actor + 0x26) && !world_ship_contact(ram, main_rom, actor)) break;
        world_award_score(ram, main_rom, actor, hit);
        world_queue_sound(ram, 0x5d);
        world_spawn_radial_explosion(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbf70a:
    case 0xbf74a:
    case 0xbf784: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xbf70a) {
            write_word(ram, actor + 0x0e, 0x123);
            write_word(ram, actor + 0x26, 8);
            write_word(ram, actor + 0x36, 0x80);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3c, 0xf703);
            write_word(ram, actor + 0x38, read_word(bank, 0xf300 + 2*(ram[actor + 0x29] & 1)));
            write_word(ram, actor + 0x3a, 0x200);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xf74a);
        } else scroll_actor(ram, actor, 0xb71a);
        if (address != 0xbf784) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
        } else {
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)read_word(ram, actor + 0x24) < limit) {
                finish_animation(ram, actor);
                break;
            }
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (address == 0xbf784) {
            if ((1u << (material & 15)) & 0xff02) {
                spawn_impact(ram, main_rom, actor);
                finish_animation(ram, actor);
                break;
            }
        } else if (material == 4) {
            spawn_bomb_splash(ram, main_rom, actor);
            write_word(ram, actor + 0x3a, 0xfe40);
            write_word(ram, actor + 0x62, 16);
            write_word(ram, actor, 0xf784);
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = (read_word(ram, actor + 0x40) & 14) + (read_word(ram, actor + 0x62) & 16);
            write_word(ram, actor + 0x2a, read_word(bank, 0xf2bc + frame));
            write_word(ram, actor + 0x44, read_word(bank, 0xf2dc + frame));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (!hit) {
                ram[actor + 0x0d] = 0;
                hit = world_surface_contact(ram, main_rom, actor);
            }
        }
        if (hit) finish_animation(ram, actor);
        break;
    }
    case 0xa8309:
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x2a, 0x8375);
        write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor, 0x832a);
        break;
    case 0xa832a:
    case 0xa836b:
    case 0xa83e6:
    case 0xa843f: {
        const uint8_t *bank = main_rom + 0xa0000;
        scroll_actor(ram, actor, 0xb72a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xa832a) {
            queue_sprite(ram, main_rom, actor, 0);
            if (read_word(ram, 0xb8aa) & (1u << (ram[actor + 0x1e] & 7))) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x836b);
            }
            break;
        }
        if (address == 0xa836b) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x81ec + frame);
                if (sprite == 0xffff) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x42, 0xb7);
                    uint16_t child = effect_slot(ram, actor, 0x848d);
                    if (child != 0x8480) write_word(ram, child + 2, 0xa000);
                    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, child + 0x24, read_word(ram, actor + 0x24) - 0x40);
                    write_word(ram, actor, 0x83e6);
                    address = 0xa83e6;
                } else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x8216 + (frame & 2)));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            if (address == 0xa836b) {
                queue_sprite(ram, main_rom, actor, 0);
                break;
            }
        }
        if (address == 0xa83e6) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, 0x821a + (frame & 6)));
                write_word(ram, actor + 0x44, read_word(bank, 0x8222 + (frame & 6)));
                write_word(ram, actor + 0x40, frame + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x843f);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x822a + frame);
                if (sprite == 0xffff) write_word(ram, actor, 0x832a);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x823e + frame));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    }
    case 0xa848d: {
        write_word(ram, 0xb8ac, read_word(ram, 0xb8ac) + 1);
        world_acquire_resource(ram, actor, 0xe5);
        write_word(ram, actor + 0x0e, 5);
        write_word(ram, actor + 0x3c, 0xf303);
        ram[actor + 0x2d] = 5;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x3a, 0xc0);
        write_word(ram, actor + 0x40, 0xfffe);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, 5);
        write_word(ram, actor + 0x4a, 0xb7);
        write_word(ram, actor + 0x4c, 16);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t child = effect_slot(ram, actor, 0x85ff);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x3c, i ? 0xf313 : 0xf30b);
            write_word(ram, child + 0x46, i ? 4 : -4);
            write_word(ram, child + 0x3a, i ? -64 : 64);
            if (i) {
                write_word(ram, child + 0x1e, 32);
                ram[child + 0x29] = 2;
            }
        }
        uint16_t child = effect_slot(ram, actor, 0x87ed);
        if (child != 0x8480) write_word(ram, child + 2, 0xa000);
        write_word(ram, actor, 0x8518);
        break;
    }
    case 0xa8518: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb72a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if ((int16_t)x <= 0x50 || (int16_t)y > 0x1b8) {
            write_word(ram, actor + 0x48, 3);
            decrement(ram, 0xb8ac);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40) + 2;
            write_word(ram, actor + 0x40, frame);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x8250 + (frame & 14)));
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        unsigned flags = read_word(ram, actor + 0x48);
        uint16_t sample_y = flags & 1 ? y - 0x40 : y + 0x38;
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - sample_y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (!(flags & 1) && material == 3) {
            uint16_t splash = spawn_surface_splash_effect(ram, main_rom, actor, x, y);
            write_word(ram, splash + 0x24, read_word(ram, splash + 0x24) + 0x38);
            ram[splash + 0x2d] = 4;
            uint16_t child = effect_slot(ram, actor, 0x8715);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x21, x);
            write_word(ram, child + 0x24, y + 0x38);
            ram[actor + 0x48] |= 1;
        } else if ((flags & 1) && !(flags & 2) && material == 3) {
            uint16_t child = effect_slot(ram, actor, 0x86d4);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, actor + 0x3a, 0x300);
            write_word(ram, actor + 0x4c, 4);
            ram[actor + 0x48] |= 2;
        }
        break;
    }
    case 0xa85ff:
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 5;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x861a);
        break;
    case 0xa861a:
    case 0xa8671: {
        uint16_t parent = stage_two_machine_parent(ram, actor);
        if (!parent) break;
        if (address == 0xa861a) {
            int detach = !(ram[actor + 0x29] & 2) && (ram[parent + 0x48] & 2);
            if (detach) write_word(ram, actor + 0x42, 0);
            else {
                ram[actor + 0x0d] = 2;
                world_player_contact(ram, main_rom, actor);
                ram[actor + 0x0d] = 1;
                if (!world_projectile_contact(ram, main_rom, actor)) break;
                write_word(ram, actor + 0x42, 32);
                write_word(ram, parent + 0x3a,
                           read_word(ram, parent + 0x3a) + read_word(ram, actor + 0x3a));
            }
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) + read_word(ram, actor + 0x46));
            write_word(ram, actor, 0x8671);
        } else {
            unsigned frame = (read_word(ram, actor + 0x40) & 2) + read_word(ram, actor + 0x1e) +
                             2 * (read_word(ram, parent + 0x40) & 14);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x8260 + frame));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
            queue_sprite(ram, main_rom, actor, 0);
            ram[actor + 0x0d] = 2;
            world_player_contact(ram, main_rom, actor);
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, parent + 0x46,
                           read_word(ram, parent + 0x46) - read_word(ram, actor + 0x46));
                write_word(ram, parent + 0x3a,
                           read_word(ram, parent + 0x3a) - read_word(ram, actor + 0x3a));
                write_word(ram, actor, 0x861a);
            }
        }
        break;
    }
    case 0xa86d4:
        world_acquire_resource(ram, actor, 0xe7);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x86ea);
        break;
    case 0xa86ea:
        if (stage_two_machine_parent(ram, actor) && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82a0 + (frame & 2)));
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x82a4 + (frame & 2)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa8715:
        world_acquire_resource(ram, actor, 0x57);
        write_word(ram, actor + 0x42, 64);
        write_word(ram, actor, 0x8727);
        break;
    case 0xa8727:
        scroll_actor(ram, actor, 0xb72a);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x8737);
        break;
    case 0xa8737: {
        scroll_actor(ram, actor, 0xb72a);
        unsigned frame = read_word(ram, actor + 0x40) & 14;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (!frame && (ram[parent + 0x48] & 2)) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82a8 + frame));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xa87ed:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x34, -44);
        write_word(ram, actor, 0x87ff);
        break;
    case 0xa87ff:
        if (stage_two_machine_parent(ram, actor)) {
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 44);
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82b8 + (frame & 6)));
            write_word(ram, actor + 0x40, frame + 2);
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    case 0xa8780:
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x44, 32);
        write_word(ram, actor + 0x4e, 1);
        write_word(ram, actor, 0x8795);
        break;
    case 0xa8795:
        write_word(ram, 0xb8aa, 0);
        if (read_word(ram, 0xb700) >= read_word(ram, actor + 0x1e)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, read_word(ram, actor + 0x44));
            write_word(ram, 0xb8aa, read_word(ram, 0xb8aa) | (1u << (read_word(ram, actor + 0x40) & 7)));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
        }
        if (read_word(ram, 0xb8ac) && !decrement(ram, actor + 0x4e)) {
            world_queue_sound(ram, 0x53);
            write_word(ram, actor + 0x4e, 60);
        }
        break;
    case 0x11496: case 0x114f1: case 0x11545: case 0x1156f:
    case 0x115bd: case 0x115fb: case 0x116b8: case 0x116c8:
        if (!boss_segment_step(ram, main_rom, actor, (uint16_t)address)) return 0;
        break;
    case 0xb30c6:
        world_acquire_resource(ram, actor, read_word(main_rom + 0xb0000, 0x30c7));
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x0e, 14);
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor, 0x30e1);
        /* fall through */
    case 0xb30e1: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a] & 1) {
            finish_animation(ram, actor);
            break;
        }
        unsigned pose = read_word(ram, parent + 0x60);
        ram[actor + 0x29] = ram[parent + 0x29];
        unsigned facing = ram[actor + 0x29] & 1;
        uint16_t dx = read_word(bank, 0x28e2 + pose);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + (facing ? -dx : dx));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(bank, 0x28e4 + pose));
        uint16_t frame = read_word(ram, actor + 0x40);
        int16_t next = frame + 2;
        write_word(ram, actor + 0x40, next >= 6 ? 0 : next);
        write_word(ram, actor + 0x2a, read_word(bank, 0x27d2 + frame + (pose ? 0 : 6)));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, 4);
            world_queue_sound(ram, 0x96);
            uint16_t shot = bank_b_effect(ram, actor, 0x31c8);
            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
            ram[shot + 0x2c] = ram[actor + 0x2c];
            ram[shot + 0x28] = bank[(uint16_t)(0x2912 + (pose >> 2) + 12*facing)];
            if (!decrement(ram, actor + 0x58)) finish_animation(ram, actor);
        }
        break;
    }
    case 0xb31c8:
    case 0xb31f2:
    case 0xb3269: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb31c8) {
            write_word(ram, actor + 0x3c, read_word(bank, 0x31cb));
            write_word(ram, actor + 0x26, 96);
            write_word(ram, actor + 0x36, 128);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x0e, 14);
            write_word(ram, actor + 0x2e, 768);
            for (unsigned axis = 0; axis < 2; ++axis) {
                uint16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
                write_word(ram, actor + 0x38 + 2*axis, 3 * unit);
            }
            write_word(ram, actor, 0x31f2);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (address == 0xb3269) {
            ram[actor + 0x0d] = 2;
            if (world_player_contact(ram, main_rom, actor) || !decrement(ram, actor + 0x42)) {
                finish_animation(ram, actor);
                break;
            }
        } else {
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0x5502) {
                finish_animation(ram, actor);
                break;
            }
            if (material == 4) {
                spawn_bomb_splash(ram, main_rom, actor);
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor, 0x3269);
            } else {
                int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
                if (x > 0x1c0 || x < 64 || (int16_t)y < limit) {
                    finish_animation(ram, actor);
                    break;
                }
            }
        }
        uint16_t frame = read_word(ram, actor + 0x40) + 128;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, 0x27de + 2*((frame >> 8) & 1)));
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb2eee:
        if (read_word(ram, actor + 0x0e) != 14) {
            scroll_actor(ram, actor, 0xb71a);
            surface_enemy_death_blast(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (!tiles) return 0;
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x36, 0x280);
        ram[actor + 0x0a] = 1;
        write_word(ram, actor, 0x2f25);
        /* fall through */
    case 0xb2f25: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t speed = read_word(ram, actor + 0x3a) - 32;
        write_word(ram, actor + 0x3a, speed);
        move_actor_axis(ram, actor + 0x23, speed);
        write_word(ram, actor + 0x26, 96);
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
        queue_surface_enemy(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42)) {
            surface_enemy_death_blast(ram, main_rom, actor);
            write_word(ram, actor + 0x42, 8);
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x187 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if ((1u << (material & 15)) & 0x551a) {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6b01));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6b06));
            ram[blast + 0x2c] = main_rom[0x6b0b];
            ram[blast + 0x2d] = main_rom[0x6b0f];
            write_word(ram, blast + 0x56, read_word(main_rom, 0x6b13));
            write_word(ram, blast + 0x58, read_word(main_rom, 0x6b18));
            world_queue_sound(ram, read_word(main_rom, 0x6b1b));
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, read_word(main_rom, 0x6b2b));
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb2f81:
        world_acquire_resource(ram, actor, read_word(main_rom + 0xb0000, 0x2f82));
        write_word(ram, actor + 0x0e, 0x109);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x2f91));
        write_word(ram, actor + 0x26, 32);
        write_word(ram, actor + 0x36, 128);
        ram[actor + 0x2d] = 33;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x2fac);
        break;
    case 0xb2fac:
    case 0xb3033: {
        if (!tiles) return 0;
        int airborne = address == 0xb2fac;
        scroll_actor(ram, actor, 0xb71a);
        if (airborne) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (airborne) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x27b0 + frame));
                write_word(ram, actor + 0x44, 2);
                write_word(ram, actor + 0x40, frame + 2);
                if ((uint16_t)(frame + 2) >= 18) {
                    write_word(ram, actor + 0x44, 0xffff);
                    write_word(ram, actor + 0x38, 0);
                    ram[actor + 0x2d] = 4;
                }
            }
        } else {
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)y < limit) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (!queue_sprite(ram, main_rom, actor, 1) && !airborne) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int impact = (1u << (material & 15)) & (airborne ? 0x5502 : 0xff02);
        if (airborne && material == 4) {
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x5e));
            write_word(ram, actor + 0x40, 0);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x300f));
            water_entry_splash(ram, main_rom, actor, x, y);
            write_word(ram, actor, 0x3033);
        }
        if (!impact) {
            ram[actor + 0x0d] = 0;
            if (airborne) world_surface_contact(ram, main_rom, actor);
            else {
                uint16_t hit = world_projectile_contact(ram, main_rom, actor);
                if (hit && read_word(ram, hit + 0x0e) == 0x121) {
                    int16_t dx = read_word(ram, hit + 0x21) - x, dy = read_word(ram, hit + 0x24) - y;
                    unsigned ax = abs((int)dx), ay = abs((int)dy);
                    unsigned direction = dx < 0 ? 128 : 0;
                    if (ax || ay) {
                        unsigned octant = ay > ax ? main_rom[0xd58e + 256*ax/ay] : main_rom[0xd48d + 256*ay/ax];
                        direction += (dx < 0) != (dy < 0) ? -octant : octant;
                    }
                    ram[actor + 0x28] = (uint8_t)direction;
                    write_word(ram, actor + 0x2e, 0x400);
                    for (unsigned axis = 0; axis < 2; ++axis) {
                        uint16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2*(uint8_t)direction);
                        write_word(ram, actor + 0x38 + 2*axis, unit * 4);
                    }
                    write_word(ram, actor + 0x42, 7);
                    write_word(ram, actor, 0xae2a);
                    write_word(ram, actor + 2, 0);
                    break;
                }
            }
            impact = !read_word(ram, actor + 0x26);
            if (!impact) {
                ram[actor + 0x0d] = 2;
                impact = world_player_contact(ram, main_rom, actor) || world_ship_contact(ram, main_rom, actor);
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb6c6c:
    case 0xb6ca8:
    case 0xb6cf7: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb6c6c) {
            world_acquire_resource(ram, actor, read_word(bank, 0x6c6d));
            write_word(ram, actor + 0x3c, read_word(bank, 0x6c77));
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 0x800);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3a, 0x200);
            unsigned side = (ram[actor + 0x29] & 1) * 2;
            unsigned index = read_word(ram, actor + 0x60) >> 1;
            write_word(ram, actor + 0x38, read_word(bank, 0x4fc8 + index + side));
            write_word(ram, actor, 0x6ca8);
        }
        int underwater = address == 0xb6cf7;
        scroll_actor(ram, actor, 0xb71a);
        if (!underwater) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (underwater) {
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)y < limit) {
                finish_animation(ram, actor);
                break;
            }
        } else {
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
        }
        uint16_t frame = read_word(ram, actor + 0x40) + 128;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, (underwater ? 0xf2cc : 0xf2bc) + 2*((frame >> 8) & 7)));
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (!underwater) {
            if (material == 4) {
                write_word(ram, actor + 0x3a, read_word(ram, actor + 0x5a));
                water_entry_splash(ram, main_rom, actor, x, y);
                write_word(ram, actor, 0x6cf7);
            }
            break;
        }
        int impact = (1u << (material & 15)) & 0xff02;
        if (!impact) {
            ram[actor + 0x0d] = 2;
            impact = world_player_contact(ram, main_rom, actor) != 0;
            if (!impact) {
                ram[actor + 0x0d] = 0;
                world_projectile_contact(ram, main_rom, actor);
                impact = !read_word(ram, actor + 0x26);
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb5b93:
    case 0xb5bfd: {
        if (!warship_attachment(ram, actor)) break;
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        unsigned index = read_word(ram, actor + 0x60);
        if (address == 0xb5b93) {
            write_word(ram, actor + 0x2a, read_word(bank, 0x4f8c + index));
            queue_sprite(ram, main_rom, actor, 0);
            if (!read_word(ram, parent + 0x54) && read_word(ram, parent + 0x66) == index) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x5bfd);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                unsigned frame = read_word(ram, actor + 0x40);
                if (frame == 4) {
                    uint16_t shot = effect_slot(ram, actor, 0x6c6c);
                    if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 8);
                    write_word(ram, shot + 0x60, index);
                    ram[shot + 0x29] = ram[actor + 0x29];
                    write_word(ram, shot + 0x5a, read_word(ram, actor + 0x5a));
                    /* 364B leaves BX at the attempted sound-ring slot. */
                    frame = (read_word(ram, 0xa6d4) + 2) & 14;
                    world_queue_sound(ram, 0x76);
                }
                if (frame >= 10) write_word(ram, actor, 0x5b93);
                else {
                    write_word(ram, actor + 0x44, read_word(bank, 0x4fac + frame));
                    unsigned sprite = index + read_word(bank, 0x4fb6 + frame);
                    write_word(ram, actor + 0x2a, read_word(bank, 0x4f8c + sprite));
                    write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (address == 0xb5b93 && (read_word(ram, parent + 0x68) & (1u << ((index >> 3) & 2)))) {
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb5c73:
    case 0xb5cc6:
    case 0xb5cd0: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb5cc6) {
            write_word(ram, actor + 0x42, 16);
            write_word(ram, actor, 0x5cd0);
        }
        int active = warship_attachment(ram, actor);
        if (address == 0xb5c73 && !active) break;
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xb5c73) {
            ram[actor + 0x0d] = 0;
            uint16_t hit = world_surface_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26)) {
                world_award_score(ram, main_rom, actor, hit);
                warship_part_explosion(ram, main_rom, actor);
                unsigned index = read_word(ram, actor + 0x60);
                write_word(ram, actor + 0x2a, read_word(bank, 0x4db2 + index));
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                write_word(ram, parent + 0x68, read_word(ram, parent + 0x68) | (1u << (index & 15)));
                write_word(ram, actor + 0x3c, 0xffff);
                write_word(ram, actor, 0x5cc6);
            }
            register_surface_enemy(ram, actor);
        } else if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 16);
            uint16_t effect = effect_slot(ram, actor, 0x6d91);
            if (effect != 0x8480) write_word(ram, effect + 2, 0xb000);
            write_word(ram, effect + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, effect + 0x24, read_word(ram, actor + 0x24));
        }
        break;
    }
    case 0xb46a:
        ram[actor + 0x2c] = 3;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x42, 0);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor, 0xb482);
        /* fall through */
    case 0xb482: {
        unsigned level = read_word(ram, actor + 0x7a);
        int homing = read_word(ram, actor + 0x60) == 3;
        unsigned index = read_word(ram, actor + 0x42);
        unsigned count = homing ? (level >= index ? level - index + 1 : 1) : level + 1;
        for (unsigned i = 0; i < count; ++i) {
            uint16_t child = world_spawn_child(ram, actor, homing ? 0xb4ce : 0xb6df);
            if (child) {
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + (homing ? 0 : 4));
                ram[child + 0x0b] = ram[actor + 0x0b];
                ram[child + 0x2d] = ram[actor + 0x2d];
                if (homing) {
                    write_word(ram, child + 0x78, read_word(ram, actor + 0x42));
                    ram[child + 0x29] = ram[actor + 0x29];
                    ram[child + 0x28] = ram[actor + 0x28];
                    write_word(ram, child + 0x44, read_word(ram, actor + 0x44));
                } else {
                    write_word(ram, child + 0x52, count - i - 1);
                    write_word(ram, child + 0x7a, (level - 1) * 2);
                }
                write_word(ram, actor + 0x44, read_word(ram, actor + 0x44) + 1);
            }
            if (homing) write_word(ram, actor + 0x42, read_word(ram, actor + 0x42) + 1);
        }
        world_queue_sound(ram, 0x35);
        write_word(ram, actor, 0xb4b0);
        break;
    }
    case 0xb6df:
    case 0xb730:
    case 0xb77c: {
        if (!tiles) return 0;
        if (address == 0xb6df) {
            world_acquire_resource(ram, actor, read_word(main_rom, 0x9bb1 + (ram[actor + 0x0b] & 2)));
            write_word(ram, actor + 0x3c, 0xb3cb);
            write_word(ram, actor + 0x26, 8);
            write_word(ram, actor + 0x36, 0xa0);
            write_word(ram, actor + 0x42, 5);
            write_word(ram, actor + 0x5a, 5);
            write_word(ram, actor + 0x58, 1);
            unsigned sequence = read_word(main_rom, 0xb3db + read_word(ram, actor + 0x7a));
            ram[actor + 0x28] = main_rom[(uint16_t)(0xb461 + sequence + read_word(ram, actor + 0x52))];
            write_word(ram, actor + 0x2e, 0x200);
            unsigned angle = 2 * ram[actor + 0x28];
            write_word(ram, actor + 0x38, ((uint32_t)read_word(main_rom + 0xa0000, 0x9e12 + angle) * 0x200) >> 8);
            write_word(ram, actor + 0x3a, ((uint32_t)read_word(main_rom + 0xa0000, 0x9d92 + angle) * 0x200) >> 8);
            write_word(ram, actor, 0xb730);
        }
        if (address != 0xb77c && !decrement(ram, actor + 0x58)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x58, read_word(main_rom, 0xb3ab + frame));
            if (read_word(ram, actor + 0x52) & 1) frame += 26;
            uint16_t sprite = read_word(main_rom, 0xb377 + frame);
            if (sprite != 0xffff) {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
            } else {
                write_word(ram, actor + 0x2a, 0x2332);
                write_word(ram, actor + 0x38, 0);
                write_word(ram, actor + 0x3a, 0x600);
                write_word(ram, actor + 0x5a, 3);
                write_word(ram, actor + 0x42, 2);
                write_word(ram, actor, 0xb77c);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, read_word(ram, actor + 0x5a));
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
            write_word(ram, trail + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, trail + 0x24, read_word(ram, actor + 0x24) - 4);
            ram[trail + 0x2c] = 3;
            write_word(ram, trail + 0x56, read_word(ram, actor + 0x58) ? 0x20 : 0x40);
            write_word(ram, trail + 0x58, 0x11);
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int outside = outside_pickup_area(ram, actor);
        int impact = (0x5502 & (1u << (material & 15))) ||
                     (material != 3 && !outside && !read_word(ram, actor + 0x26));
        if (impact && read_word(ram, 0x84) == 4) return 0;
        if (impact || material == 3 || outside) {
            if (impact) spawn_impact(ram, main_rom, actor);
            else if (material == 3)
                spawn_surface_splash(ram, main_rom, actor, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24));
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            decrement(ram, parent + 0x44);
            finish_animation(ram, actor);
        } else if (read_word(ram, actor + 0x3c) != 0xffff) {
            world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    case 0xb4b0:
        if (!read_word(ram, actor + 0x44)) {
            uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
            decrement(ram, parent + 0x44);
            retire_actor(ram, actor);
        }
        break;
    case 0xde69:
        ram[actor + 0x2c] = 0x85;
        write_word(ram, actor, 0xde72);
        /* fall through */
    case 0xde72: {
        uint16_t parent = read_word(ram, actor + 0x54);
        if (!ram[parent + 0x58] || !read_word(ram, 0x286)) {
            finish_animation(ram, actor);
            break;
        }
        for (uint16_t row = 0xde37; read_word(main_rom, row) != 0xffff; row += 6) {
            write_word(ram, actor + 0x21, read_word(main_rom, row));
            write_word(ram, actor + 0x24, read_word(main_rom, row + 2));
            write_word(ram, actor + 0x2a, read_word(main_rom, row + 4));
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    }
    case 0xe1da:
        if (!--ram[actor + 0x4e]) {
            for (unsigned i = 0; i < 8; ++i) {
                uint16_t child = effect_slot(ram, actor, 0xf1b5);
                static const unsigned dest[] = {0x4e, 0x2a, 0x21, 0x24, 0x31, 0x34};
                for (unsigned n = 0; n < 6; ++n)
                    write_word(ram, child + dest[n], read_word(main_rom, 0xf101 + 12*i + 2*n));
                ram[child + 0x2c] = 0x85;
            }
            queue_continue_sound(ram, 0x14);
            write_word(ram, actor, 0xe1ef);
            ram[actor + 0x4e] = 99;
        }
        break;
    case 0xe1ef:
        if (!--ram[actor + 0x4e]) finish_animation(ram, actor);
        break;
    case 0xe209:
        if (!--ram[actor + 0x43]) {
            ram[actor + 0x43] = 28;
            if (--ram[actor + 0x60]) queue_continue_sound(ram, 0x6f);
            else finish_animation(ram, actor);
        }
        break;
    case 0xe234:
        if (read_word(ram, actor + 0x3c) != 0xffff)
            world_append_candidate(ram + 0xa6e2, actor);
        write_word(ram, actor + 0x2e, 0x450);
        ram[actor + 0x4e] = 1;
        write_word(ram, actor + 0x62, 1);
        write_word(ram, actor + 0x60, 0);
        write_word(ram, actor + 0x4c, 0xdd8f);
        write_word(ram, actor, 0xe256);
        /* fall through */
    case 0xe256:
    case 0xe2e5: {
        int flying = address != 0xe2e5;
        if (flying && !--ram[actor + 0x4e]) {
            ram[actor + 0x4e] = 3;
            digit_fragment_velocity(ram, main_rom, actor);
            unsigned speed = read_word(ram, actor + 0x2e);
            write_word(ram, actor + 0x4c, read_word(main_rom, 0xf0ef + 2*(speed/157)));
            write_word(ram, actor + 0x2e, speed - 48);
            if (speed < 48) { finish_animation(ram, actor); break; }
        }
        if (!decrement(ram, actor + 0x62)) {
            write_word(ram, actor + 0x62, flying ? 2 : 3);
            unsigned index = read_word(ram, actor + 0x60) + 2;
            write_word(ram, actor + 0x60, index >= 24 ? 0 : index);
        }
        uint16_t table = flying ? read_word(ram, actor + 0x4c) : 0xdd8f;
        write_word(ram, actor + 0x2a, read_word(main_rom, table + read_word(ram, actor + 0x60)));
        if (!flying) write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a)-64);
        queue_sprite(ram, main_rom, actor, 1);
        if (flying) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        int16_t top = ram[0x26e]&2 ? 0x68 : read_word(ram, 0xb732)+16;
        if (!flying && (int16_t)read_word(ram, actor + 0x24)<top) finish_animation(ram, actor);
        break;
    }
    case 0xe2c9:
        queue_sprite(ram, main_rom, actor, 1);
        if (!--ram[actor + 0x4e]) {
            write_word(ram, actor + 0x62, 1);
            write_word(ram, actor + 0x60, 0);
            write_word(ram, actor, 0xe2e5);
        }
        break;
    case 0xf1b5:
        if (!--ram[actor + 0x4e]) {
            ram[actor + 0x28] = vector_angle(main_rom,
                read_word(ram, actor+0x31) - read_word(ram, actor+0x21),
                read_word(ram, actor+0x34) - read_word(ram, actor+0x24));
            write_word(ram, actor + 0x2e, 0xc00);
            digit_fragment_velocity(ram, main_rom, actor);
            write_word(ram, actor, 0xf1df);
        }
        break;
    case 0xf1df:
        move_actor_axis(ram, actor+0x20, read_word(ram, actor+0x38));
        move_actor_axis(ram, actor+0x23, read_word(ram, actor+0x3a));
        if (((read_word(ram, actor+0x31)-read_word(ram, actor+0x21)) ^ read_word(ram, actor+0x38)) &
            ((read_word(ram, actor+0x34)-read_word(ram, actor+0x24)) ^ read_word(ram, actor+0x3a)) & 0x8000) {
            write_word(ram, actor+0x21, read_word(ram, actor+0x31));
            write_word(ram, actor+0x24, read_word(ram, actor+0x34));
            write_word(ram, actor, 0xf210);
        }
        /* fall through */
    case 0xf210:
        queue_sprite(ram, main_rom, actor, 1);
        break;
    case 0xe06b:
    case 0xe09d: {
        uint16_t parent = read_word(ram, actor + 0x54);
        if (address == 0xe06b) {
            world_acquire_resource(ram, actor, 0xfe);
            world_acquire_resource(ram, actor, 0xfd);
            write_word(ram, actor + 0x14, read_word(ram, actor + 0x14) | 0x8080);
            write_word(ram, actor + 0x3c, 0xfb4b);
            write_word(ram, actor + 0x60, 8);
            write_word(ram, actor + 0x62, 1);
            write_word(ram, actor + 0x64, 136);
            write_word(ram, actor, 0xe09d);
        }
        if (read_word(ram, parent) == 0xe00e) {
            write_word(ram, actor + 0x62, 0);
            write_word(ram, actor + 0x60, 0);
        } else if (read_word(ram, parent) == 0xe039 && !read_word(ram, actor+0x60)) {
            write_word(ram, actor + 0x62, 0);
        }
        if (ram[actor+10] == 1) { shatter_continue_digit(ram, main_rom, actor); break; }
        if (ram[actor+10] || !read_word(ram, 0x286)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x64)) {
            if (read_word(ram, actor + 0x4c) == 0x7e) { shatter_continue_digit(ram, main_rom, actor); break; }
            finish_animation(ram, actor);
            break;
        }
        memcpy(ram + actor + 0x42, ram + parent + 0x42, 3);
        write_word(ram, actor + 0x46, read_word(ram, parent + 0x46));
        project_continue_digit(ram, main_rom, actor);
        break;
    }
    case 0xdecd:
        write_word(ram, actor, 0xded2);
        /* fall through */
    case 0xded2:
    case 0xdf93:
    case 0xe00e:
    case 0xe039: {
        uint16_t state = read_word(ram, actor + 0x40);
        uint16_t child = read_word(ram, actor + 0x56);
        int active = read_word(ram, 0x286) != 0;
        if (address == 0xdf93 && !state) {
            for (int i = 0; i < 3; ++i) ram[actor + 0x42 + i] += ram[actor + 0x50 + i];
            uint16_t distance = read_word(ram, actor + 0x46) - 12;
            write_word(ram, actor + 0x46, distance);
            if (distance >= 210) break;
            write_word(ram, actor, 0xe039);
            write_word(ram, actor + 0x46, 210);
            memset(ram + actor + 0x42, 0, 3);
            write_word(ram, actor + 0x40, 0);
            break;
        }
        if (address == 0xe039 && !state) break;
        if (address == 0xe00e && active && --ram[actor + 0x4e]) break;
        if (address == 0xded2 || address == 0xdecd) {
            ram[actor + 0x58] = 0;
            if (!state || state == 0xffff) break;
            ram[actor + 0x58] = 0xff;
            uint16_t banner = effect_slot(ram, actor, 0xde69);
            write_word(ram, banner + 0x54, actor);
            write_word(ram, actor + 0x4c, 0);
        } else if (address == 0xdf93 && active) {
            write_word(ram, actor, 0xe00e);
            ram[actor + 0x4e] = 6;
            write_word(ram, actor + 0x46, 210);
            memset(ram + actor + 0x42, 0, 3);
            write_word(ram, actor + 0x40, 0);
            break;
        } else ram[child + 0x0a] = 0xff;
        write_word(ram, actor + 0x40, 0);
        if (!active) {
            write_word(ram, actor, 0xded2);
            ram[child + 0x0a] = 0xff;
            break;
        }
        uint16_t row = read_word(ram, actor + 0x4c);
        if ((int16_t)row >= 140) {
            write_word(ram, actor, 0xded2);
            ram[child + 0x0a] = 1;
            break;
        }
        write_word(ram, actor, 0xdf93);
        child = effect_slot(ram, actor, 0xe06b);
        write_word(ram, actor + 0x56, child);
        write_word(ram, child + 0x54, actor);
        write_word(ram, child + 0x3c, read_word(main_rom, 0xdf49));
        write_word(ram, child + 0x4c, row);
        write_word(ram, child + 0x48, read_word(main_rom, 0xe54b + row));
        for (int i = 0; i < 3; ++i) {
            ram[actor + 0x42 + i] = main_rom[0xe553 + row + 2*i];
            ram[actor + 0x50 + i] = main_rom[0xe54d + row + 2*i];
        }
        write_word(ram, actor + 0x4c, row + 14);
        write_word(ram, actor + 0x46, 1000);
        break;
    }
    case 0xfeee:
        write_word(ram, actor + 0x42, 128);
        write_word(ram, actor + 0x44, 64);
        write_word(ram, actor, 0x60ce);
        /* fall through */
    case 0x60ce:
        if (!read_word(ram, 0x286) && !(ram[0x262] & 0x80)) {
            if (read_word(ram, actor + 0x42) == 1) {
                unsigned seconds = read_word(ram, 0xb8a0);
                /* Entering the refresh with zero would fault at the guest DIV. */
                if (!tiles || !seconds)
                    return 0;
                write_word(ram, actor + 0x42, 128);
                seconds = (seconds >= 100 ? 99 : seconds) - 1;
                write_word(ram, 0xb8a0, seconds);
                if (!seconds)
                    expire_stage_time(ram, main_rom, actor);
                refresh_stage_time(ram, tiles, actor, seconds);
            } else {
                decrement(ram, actor + 0x42);
            }
        }
    stage_timer_palette:
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x44, 64);
            if (!(ram[0x262] & 2)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                unsigned index = frame & 2;
                unsigned speed = 6;
                if (read_word(ram, 0xb8a0) < 10) {
                    index += 4;
                    speed = 5;
                    write_word(ram, actor + 0x44, 32);
                }
                queue_palette(ram, 6, read_word(main_rom, 0x60c6 + index), speed);
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        break;
    /* 0:FF00: after time runs out, wait 256 ticks (longer while 286 is
       set), then restart the clock at 99 and resume 60CE at 6169. */
    case 0xff00:
        if (!tiles) return 0;
        if (decrement(ram, actor + 0x42)) break;
        if (read_word(ram, 0x286)) {
            write_word(ram, actor + 0x42, 1);
            break;
        }
        write_word(ram, actor + 0x42, 0x60);
        write_word(ram, 0xb8a0, 0x63);
        write_word(ram, actor, 0x60ce);
        refresh_stage_time(ram, tiles, actor, 0x63);
        goto stage_timer_palette;
    case 0x7b3c:
    case 0x7b6f: {
        unsigned owner = ram[actor + 0x0b] & 3;
        if (!tiles || !owner || owner > 2 || read_word(ram, 0x84) == 4 ||
            (ram[0x262] & 0x40) || (ram[0xb8b2] & (1u << (owner & 2)))) {
            world_dispatch_detail = !tiles ? "no tile memory" : !owner || owner > 2 ? "sub owner"
                : read_word(ram, 0x84) == 4 ? "word 84 = 4" : (ram[0x262] & 0x40) ? "262h bit 6" : "B8B2 owner bit";
            return 0;
        }
        uint16_t config = read_word(ram, actor + 0x70);
        unsigned side = 2 * (owner & 2);
        int16_t y = (int16_t)read_word(ram, 0xb736 + side);
        int16_t ceiling = (int16_t)read_word(ram, 0x25c);
        if (address == 0x7b3c) {
            write_word(ram, config + 0x26, read_word(ram, 0x84));
            write_word(ram, actor + 0x21, read_word(ram, 0xb734 + side));
            write_word(ram, actor + 0x24, y <= ceiling ? y : ceiling);
            write_word(ram, actor + 0x42, 127);
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 30);
        }
        if (!read_word(ram, config + 0x18))
            write_word(ram, config + 0x18, read_word(main_rom, 0x78e3 + 2 * (ram[0x278] & 3)));
        uint16_t score = read_word(ram, 0x254 + 2 * (owner - 1));
        if (read_word(ram, actor + 0x6e)) {
            unsigned a = ram[score], b = main_rom[0xc307];
            unsigned sum = (a >> 4) * 10 + (a & 15) + (b >> 4) * 10 + (b & 15);
            ram[score] = (uint8_t)((sum / 10 % 10) * 16 + sum % 10);
        } else if (ram[0x264] & (1u << (owner + 5))) {
            ram[0x264] &= ~(1u << (owner + 5));
            for (unsigned i = 0; i < 8; ++i) ram[(uint16_t)(score + i)] = 0;
            write_word(ram, config + 0x1a, 0);
            write_word(ram, config + 0x1c, 0);
            write_word(ram, config + 0x24, 0);
            write_word(ram, config + 0x20, 0);
            write_word(ram, config + 0x22, 0);
        }
        draw_join_label(tiles, main_rom, owner == 1 ? 0x2d30 : 0x2d90, owner == 1 ? 0xc26b : 0xc27c);
        world_sub_lives_hud(main_rom, tiles, owner, read_word(ram, config + 0x18));
        ram[actor + 0x28] = 255;
        write_word(ram, actor + 0x2e, read_word(ram, 0xa2));
        ram[actor + 0x29] = 0;
        write_word(ram, actor + 0x40, 0);
        for (unsigned i = 0x58; i <= 0x5e; i += 2) write_word(ram, actor + i, 0);
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor + 0x46, 2);
        write_word(ram, actor + 0x48, 0);
        write_word(ram, actor + 0x56, 4);
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x64, 6);
        write_word(ram, actor + 0x66, 24);
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x7bd9));
        write_word(ram, actor + 0x6e, 1);
        write_word(ram, actor, 0x7be5);
    }
        /* fall through */
    case 0x7be5:
        if (!tiles) return 0;
        if ((int16_t)read_word(ram, actor + 0x42) > 96) {
            ram[0xb73e] = (uint8_t)(1u << (ram[0x26e] & 3));
            ram[0xb742] = 0;
        }
        if (ram[actor + 0xa]) {
            if (read_word(ram, actor + 0x6e)) {
                memset(ram + actor + 0x58, 0, 8);
                world_sub_entry_step(ram, main_rom, tiles, actor);
            } else if (!world_sub_open_water(ram, main_rom, tiles, actor)) {
                return 0;
            }
            break;
        }
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x7c1e));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 256);
        if (read_word(ram, actor + 0x6e)) write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x7c3b);
        if (read_word(ram, actor + 0x6e))
            world_sub_entry_step(ram, main_rom, tiles, actor);
        else if (!world_sub_open_water(ram, main_rom, tiles, actor))
            return 0;
        break;
    case 0x7c3b:
        if (read_word(ram, 0x2a4) || (read_word(ram, 0x262) & 0x8080)
            || ram[actor + 0xa] || read_word(ram, actor + 0x26)
            || (read_word(ram, 0xb8b2) & (1u << (ram[actor + 0xb] & 2)))) {
            if (!tiles || !world_sub_open_water(ram, main_rom, tiles, actor)) return 0;
            break;
        }
        /* fall through */
    case 0x7e5e: {
        ram[actor + 0xa] = 1;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x60, 0);
        write_word(ram, actor + 0x6c, 1);
        ram[actor + 0x2c] = ram[actor + 0x15];
        unsigned player = ((ram[actor + 0xb] & 3) - 1) & 15;
        write_word(ram, 0x264, read_word(ram, 0x264) & ~(1u << player));
        uint16_t stats = read_word(ram, actor + 0x70);
        memset(ram + stats + 0x10, 0, 8);
        uint16_t blast = effect_slot(ram, actor, 0x6efe);
        write_word(ram, blast + 0x4c, read_word(main_rom, 0x6b77));
        write_word(ram, blast + 0x4a, read_word(main_rom, 0x6b7c));
        ram[blast + 0x2c] = 10;
        ram[blast + 0x2d] = 0x20;
        write_word(ram, blast + 0x56, 0x40);
        write_word(ram, blast + 0x58, 0);
        world_queue_sound(ram, 0x48);
        write_word(ram, actor, 0x7eb1);
    }
        /* fall through */
    case 0x7eb1:
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, 0x77e5 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x42, 16);
                write_word(ram, actor + 0x6c, 0);
                write_word(ram, actor, 0x7ee5);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x42, read_word(main_rom, 0x7803 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        break;
    case 0x7ee5: {
        uint16_t stats = read_word(ram, actor + 0x70);
        if (!tiles && read_word(ram, actor + 0x42) == 1 && read_word(ram, stats + 0x18) == 1)
            return 0;
        if (decrement(ram, actor + 0x42)) break;
        unsigned owner = ram[actor + 0xb] & 3;
        if (decrement(ram, stats + 0x18)) {
            ram[0x264] |= 1u << ((owner - 1) & 7);
            write_word(ram, actor + 0x7a, 1);
            write_word(ram, actor + 0x3c, read_word(main_rom, 0x7f08));
            write_word(ram, actor + 0x36, 4);
            ram[actor + 0xa] = 1;
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 30);
            world_queue_sound(ram, 0x5f);
            write_word(ram, actor, 0x7b6f);
        } else {
            write_word(ram, actor + 0x6e, 1);
            ram[0x288] |= 1u << (owner >> 1);
            ram[0x264] &= ~(1u << (2 + (owner >> 1)));
            uint16_t label = read_word(main_rom, 0xc257 + 2 * (owner - 1));
            draw_join_label(tiles, main_rom, label, 0xc2f6);
            write_word(ram, actor, owner & 1 ? 0x7a6a : 0x7b08);
            write_word(ram, actor + 0x42, 0x601);
            draw_join_label(tiles, main_rom, label, 0x78ef);
        }
        break;
    }
    case 0x79fb:
    case 0x7a9e: {
        unsigned player = address == 0x79fb ? 1 : 2;
        int active = ram[0x264] & (2u << player);
        if (!active && !tiles) return 0;
        init_player_slot(ram, main_rom, actor, player);
        if (active) {
            ram[actor + 0x0a] = 0;
            write_word(ram, actor + 0x3c, 0x78d3);
            if (player == 1) write_word(ram, actor + 0x36, 0x100);
            write_word(ram, actor, 0x7b3c);
            break;
        }
        address = player == 1 ? 0x7a6a : 0x7b08;
        write_word(ram, actor, (uint16_t)address);
    }
        /* fall through */
    case 0x7a6a:
    case 0x7b08: {
        if (!tiles)
            return 0;
        uint16_t sub = read_word(ram, address == 0x7a6a ? 0x92 : 0x90);
        write_word(ram, actor + 0x21, read_word(ram, sub + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, sub + 0x24));
        update_join_hud(ram, main_rom, tiles, actor);
        if (ram[0x264] & (address == 0x7a6a ? 4 : 8)) {
            write_word(ram, actor + 0x7a, 2);
            write_word(ram, actor + 0x3c, 0x79f1);
            write_word(ram, actor + 0x36, 4);
            ram[actor + 0x0a] = 1;
            write_word(ram, actor, 0x7b3c);
        }
        break;
    }
    case 0x3c68:
    case 0x3c88:
        if (!tiles || (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
        if (address == 0x3c68) {
            for (unsigned pos = 0xb716; pos <= 0xb720; pos += 2) write_word(ram, pos, 0);
            for (unsigned pos = 0xb726; pos <= 0xb72c; pos += 2) write_word(ram, pos, 0);
        }
        write_word(ram, 0x270, 0);
        write_word(ram, actor, 0x3ca4);
        update_camera_layers(ram, main_rom, tiles, actor, 0, 0);
        break;
    case 0x3c96:
        if (!tiles || (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
        write_word(ram, 0x270, 0);
        write_word(ram, actor, 0x3ca1);
        update_camera_layers(ram, main_rom, tiles, actor, 0, 1);
        break;
    case 0x3ca4:
    case 0x3ca1:
    case 0x3f68:
        if (!tiles || ((address == 0x3ca1 || address == 0x3ca4) &&
                       read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
    {
        int16_t camera = read_word(ram, 0xb700);
        int scrolling = address == 0x3f68 && (read_word(ram, 0x26e) & 0x4000);
        int column = scrolling && (int16_t)read_word(ram, actor + 0x6a) <= camera;
        int script = scrolling && (int16_t)read_word(ram, actor + 0x46) <= camera;
        uint16_t row = read_word(ram, actor + 0x40);
        update_camera_layers(ram, main_rom, tiles, actor, address == 0x3f68, address == 0x3ca1);
        if (address == 0x3f68) {
            /* 3F68: 4621 leaves the advanced script cursor in SI; otherwise
               each 390B block copy ends with SI = 40h; 459B never loads it. */
            if (script) leave_si(row + 0x0e);
            else if (column) leave_si(0x40);
            else keep_si();
        }
        break;
    }
    case 0x57e3:
    case 0x57fc: {
        int first = address == 0x57e3;
        uint16_t row = first ? read_word(main_rom, 0x56af + 2 * read_word(ram, 0x84))
                             : read_word(ram, actor + 0x40);
        int due = first || read_word(ram, actor + 0x42) <= read_word(ram, 0xb700);
        uint16_t callback = due ? read_word(main_rom, row + 16) : read_word(ram, actor + 0x44);
        if (callback != 0xffff)
            return 0;
        if (due) {
            write_word(ram, 0x29a, read_word(ram, 0x29a) | 7);
            for (unsigned i = 0; i < 3; i++)
                write_word(ram, 0x29c + 2 * i, read_word(main_rom, row + 2 + 2 * i));
            for (unsigned i = 0; i < 4; i++)
                write_word(ram, 0x292 + 2 * i, read_word(main_rom, row + 8 + 2 * i));
            write_word(ram, actor + 0x44, callback);
            write_word(ram, actor + 0x40, row + 18);
            uint16_t next = read_word(main_rom, row + 18);
            if (next == 0xffff)
                finish_animation(ram, actor);
            write_word(ram, actor + 0x42, next);
            if (first) write_word(ram, actor, 0x57fc);
        }
        break;
    }
    case 0x9eb2:
    case 0x9ec4:
    case 0x9fcd:
    case 0x9fda:
    case 0xa0a9:
        if (!tiles || !update_vertical_weapon(ram, main_rom, tiles + 0x4000, actor, address))
            return 0;
        break;
    case 0xba0f4: {
        const uint8_t *bank = main_rom + 0xb0000;
        for (unsigned i = 0; i < 3; i++)
            world_acquire_resource(ram, actor, read_word(bank, 0xa0f5 + 8 * i));
        write_word(ram, actor + 0x0e, 0x12);
        write_word(ram, actor + 0x2a, read_word(bank, 0xa114));
        ram[actor + 0x2d] = bank[0xa119];
        write_word(ram, actor + 0x3c, read_word(bank, 0xa11d));
        uint16_t flags = read_word(ram, actor + 0x1e);
        ram[actor + 0x29] = flags & 2;
        uint16_t index = read_word(bank, 0xa0a4 + (flags & 0x20) + read_word(ram, 0x88));
        uint16_t speed = read_word(bank, 0xa0e4 + index);
        uint16_t delay = read_word(bank, 0xa0e6 + index);
        write_word(ram, actor + 0x26, speed);
        write_word(ram, actor + 0x36, speed);
        write_word(ram, actor + 0x50, delay);
        write_word(ram, actor + 0x52, delay);
        write_word(ram, actor, 0xa152);
        break;
    }
    case 0xba152: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        int fire;
        if (read_word(ram, actor + 0x50)) {
            fire = !decrement(ram, actor + 0x50);
            if (fire) write_word(ram, actor + 0x56, 4);
        } else fire = !decrement(ram, actor + 0x54);
        if (fire) {
            write_word(ram, actor + 0x54, 16);
            uint16_t flash = bank_b_effect(ram, read_word(ram, 0x9a), 0xa4bd);
            write_word(ram, flash + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, flash + 0x24, read_word(ram, actor + 0x24));
            ram[flash + 0x29] = ram[actor + 0x29];
            ram[flash + 0x2c] = ram[actor + 0x15];
            uint16_t shot = bank_b_effect(ram, actor, 0xa2ec);
            unsigned direction = ram[actor + 0x29] & 2;
            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xa084 + direction));
            write_word(ram, shot + 0x3a, read_word(bank, 0xa088 + direction));
            write_word(ram, shot + 0x38, read_word(bank, 0xa08c + 2 * (read_word(ram, actor + 0x56) - 1)));
            ram[shot + 0x2c] = ram[actor + 0x14];
            world_queue_sound(ram, 0x7b);
            if (!decrement(ram, actor + 0x56))
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 2);
            world_queue_sound(ram, 0x5f);
            world_queue_sound(ram, 0x5a);
            uint16_t blast = bank_b_effect(ram, read_word(ram, 0x9c), 0xa3ba);
            write_word(ram, blast + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, blast + 0x24, read_word(ram, actor + 0x24));
            ram[blast + 0x29] = ram[actor + 0x29];
            ram[blast + 0x2c] = ram[actor + 0x15];
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xa279);
        }
        break;
    }
    case 0xba4bd:
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xa4cc);
        break;
    case 0xba4cc: {
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xb0000, 0xa012 + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0xa02c + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        if (!(ram[0x274] & 1)) queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xba2ec:
    case 0xba30c: {
        if (!tiles) return 0;
        if (address == 0xba2ec) {
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3c, 0xf53b);
            write_word(ram, actor + 0x26, 128);
            write_word(ram, actor + 0x36, 128);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xa30c);
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        int16_t velocity = read_word(ram, actor + 0x3a);
        if (velocity > -512) velocity -= 16;
        write_word(ram, actor + 0x3a, velocity);
        move_actor_axis(ram, actor + 0x23, (uint16_t)velocity);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        uint16_t minimum = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
        if (x > 0x1c0 || x < 0x40 || (int16_t)y < (int16_t)minimum) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0xa094 + (frame & 14)));
            write_word(ram, actor + 0x44, 2);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        unsigned cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int impact = !!(0xff02 & (1u << (material & 15)));
        if (!impact) {
            ram[actor + 0x0d] = 2;
            impact = world_player_contact(ram, main_rom, actor) != 0;
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xba279:
    case 0xba2c7: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xba279 && !decrement(ram, actor + 0x44)) {
            uint16_t index = read_word(ram, actor + 0x40);
            uint16_t frame = read_word(bank, 0x9fbe + index);
            if (frame == 0xffff) write_word(ram, actor, 0xa2c7);
            else {
                write_word(ram, actor + 0x2a, frame);
                write_word(ram, actor + 0x44, read_word(bank, 0x9ff2 + index));
                write_word(ram, actor + 0x40, index + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* 7000:A54C (init), A588 (idle), A5D0 (delay) and A619 (play): a
       background animation that replays whenever the map has scrolled. */
    case 0x7a54c: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned variant = read_word(ram, actor + 0x1e) & 2;
        write_word(ram, actor + 0x46, read_word(bank, 0x99a0 + variant));
        write_word(ram, actor + 0x48, read_word(bank, 0x99a4 + variant));
        world_acquire_resource(ram, actor, read_word(bank, 0x99a8 + variant));
        ram[actor + 0x2c] |= 0x80;
        write_word(ram, actor + 0x56, read_word(ram, actor + 0x21) < 0x100 ? 0x1a : 1);
        write_word(ram, actor, 0xa588);
        break;
    }
    case 0x7a588:
    case 0x7a5d0:
    case 0x7a619: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        int16_t top = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
        if ((int16_t)read_word(ram, actor + 0x24) < top) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0x7a588) {
            if (read_word(ram, 0xb70a)) {
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
                write_word(ram, actor, 0xa5d0);
            }
        } else if (address == 0x7a5d0) {
            if (!decrement(ram, actor + 0x54)) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xa619);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
                if (sprite == 0xffff) {
                    write_word(ram, actor, 0xa588);
                    break;
                }
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x48))));
                write_word(ram, actor + 0x40, frame + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    }
    /* 1000:E720/E769 and its 1000:E7A1/E7C8 companion: paired scenery
       pieces that loop a flight script. 1000:E800/E828 is a fixed piece. */
    case 0x1e720:
    case 0x1e7a1: {
        const uint8_t *bank = main_rom + 0x10000;
        unsigned variant = read_word(ram, actor + 0x1e) & 2;
        int lead = address == 0x1e720;
        ram[actor + 0x2c] = 0x15;
        ram[actor + 0x2d] = lead ? 0x3e : 0x3f;
        if (lead) write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor + 0x2a, read_word(bank, (lead ? 0xd44a : 0xd44e) + variant));
        begin_linear_script(ram, main_rom, actor, read_word(bank, (lead ? 0xd452 : 0xd456) + variant));
        if (lead) {
            uint16_t companion = effect_slot(ram, actor, 0xe7a1);
            if (companion != 0x8480) write_word(ram, companion + 2, 0x1000);
            write_word(ram, companion + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, companion + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, companion + 0x1e, read_word(ram, actor + 0x1e));
        }
        write_word(ram, actor, lead ? 0xe769 : 0xe7c8);
        break;
    }
    case 0x1e769:
    case 0x1e7c8: {
        const uint8_t *bank = main_rom + 0x10000;
        int lead = address == 0x1e769;
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0)
            begin_linear_script(ram, main_rom, actor,
                                read_word(bank, (lead ? 0xd452 : 0xd456) + (read_word(ram, actor + 0x1e) & 2)));
        uint16_t x = read_word(ram, actor + 0x21);
        /* E781 retires the lead piece only at exactly 40h; E7E0 below it. */
        if (lead ? x == 0x40 : x < 0x40) finish_animation(ram, actor);
        else queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x1e800: {
        const uint8_t *bank = main_rom + 0x10000;
        unsigned variant = read_word(ram, actor + 0x1e) & 6;
        write_word(ram, actor + 0x2a, read_word(bank, 0xd4b2 + variant));
        world_acquire_resource(ram, actor, read_word(bank, 0xd4ba + variant));
        ram[actor + 0x2d] = 0x3e;
        write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor, 0xe828);
        break;
    }
    /* 1000:E552 (init) and E561: four 8A cues on the second sound queue,
       42 ticks apart. */
    case 0x1e552:
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x40, 4);
        write_word(ram, actor, 0xe561);
        /* fall through */
    case 0x1e561:
        keep_si();
        if (decrement(ram, actor + 0x42)) break;
        write_word(ram, actor + 0x42, 0x2a);
        {
            unsigned tail = (read_word(ram, 0xa6d8) + 2) & 14;
            if (tail != read_word(ram, 0xa6d6)) {
                write_word(ram, 0xa6d8, tail);
                write_word(ram, 0xa6a2 + tail, 0x8a);
            }
        }
        if (!decrement(ram, actor + 0x40)) finish_animation(ram, actor);
        break;
    /* 1000:E58C (init) and E5C6: a floating target bobbing on a sine. When
       destroyed it scores, bursts (E62D), then rises (E66B) until it meets
       solid ground and leaves the E6D9 wreck. */
    case 0x1e58c:
    case 0x1e5c6: {
        if (address == 0x1e58c) {
            world_acquire_resource(ram, actor, 0xd9);
            write_word(ram, actor + 0x0e, 0x3a);
            ram[actor + 0x2d] = 0x30;
            write_word(ram, actor + 0x26, 0x100);
            write_word(ram, actor + 0x36, 0x100);
            write_word(ram, actor + 0x3c, 0xf9b3);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x10000, 0xe4ec + (read_word(ram, actor + 0x1e) & 2)));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
            write_word(ram, actor, 0xe5c6);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        uint16_t phase = read_word(ram, actor + 0x31);
        write_word(ram, actor + 0x31, phase + 4);
        /* 6761 with BX = 100h, CX = 2. */
        int32_t wave = (int16_t)read_word(main_rom + 0xa0000, 0x9d92 + 2 * (phase & 255)) * 2;
        uint32_t height = (ram[actor + 0x33] | ((uint32_t)read_word(ram, actor + 0x34) << 8)) + ((uint32_t)wave & 0xffffff);
        ram[actor + 0x23] = (uint8_t)height;
        write_word(ram, actor + 0x24, (uint16_t)(height >> 8));
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) break;
        if (!hit) {
            world_dispatch_detail = "target destroyed without a projectile; 6237 reads leftover BX";
            return 0;
        }
        world_award_score(ram, main_rom, actor, hit);
        blast_sequence(ram, actor, 0x1000, 0xe4f0, 4);
        write_word(ram, actor + 0x42, 0x10);
        write_word(ram, actor, 0xe62d);
        break;
    }
    case 0x1e62d:
    case 0x1e66b: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0x1e62d) {
            queue_sprite(ram, main_rom, actor, 0);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x2a, 0x24bb);
                write_word(ram, actor + 0x3a, 0xffd0);
                write_word(ram, actor + 0x42, 0x20);
                write_word(ram, actor, 0xe66b);
            }
            break;
        }
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) blast_sequence(ram, actor, 0x1000, 0xe510, 0x20);
        if (world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                               read_word(ram, actor + 0x24) - 8) & 0xff02) {
            blast_sequence(ram, actor, 0x1000, 0xe52a, 4);
            /* 6EDC, then E6CE lifts the blast eight pixels. */
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x44, 0x7b);
            write_word(ram, blast + 0x4c, 0x762d);
            write_word(ram, blast + 0x4a, 0x7706);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x34, 0xfff8);
            write_word(ram, actor, 0xe6d9);
        }
        break;
    }
    /* 1000:E6FE queues palette 1CB into slot 10h and retires. */
    case 0x1e6fe:
        queue_palette(ram, 0x10, 0x1cb, 8);
        finish_animation(ram, actor);
        keep_si();
        break;
    case 0x1e6d9:
    case 0x1e828:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            keep_si();
        } else {
            queue_sprite(ram, main_rom, actor, 0);
            si_after_sprite();
        }
        break;
    /* 1000:1BE1 (opening), 1C78/1C9E (salvo), 1D41 (pause) and 1D8A
       (closing): the boss launcher bay. Each salvo row at the list chosen
       from 1000:0130 releases 1E07 launch flashes; closing clears bit 0 of
       the parent's word 62. */
    case 0x11be1:
    case 0x11c78:
    case 0x11c9e:
    case 0x11d41:
    case 0x11d8a: {
        const uint8_t *bank = main_rom + 0x10000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        uint32_t state = address;
        if (state == 0x11c78) {
            uint16_t list = read_word(bank, 0x130 + (world_random_byte(ram) & 0x0e));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 8);
            write_word(ram, actor + 0x46, list);
            write_word(ram, actor + 0x48, read_word(bank, list));
            write_word(ram, actor, 0x1c9e);
            state = 0x11c9e;
        }
        if (ram[parent + 0x0a] & 2) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x2c] = ram[parent + 0x2c];
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (state == 0x11be1) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(0x20 + frame));
                if (sprite == 0xffff)
                    write_word(ram, actor, 0x1c78);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x38 + frame)));
                    write_word(ram, actor + 0x40, frame + 2);
                    if (frame == 8) {
                        write_word(ram, actor + 0x42, 5);
                        do {
                            uint16_t puff = world_spawn_child(ram, read_word(ram, 0x9a), 0x2180);
                            if (!puff) break;
                            write_word(ram, puff + 2, 0x1000);
                            unsigned row = 4 * (uint16_t)(read_word(ram, actor + 0x42) - 1);
                            write_word(ram, puff + 0x21, x + read_word(bank, (uint16_t)(0x4e + row)));
                            write_word(ram, puff + 0x24, y + read_word(bank, (uint16_t)(0x50 + row)));
                        } while (decrement(ram, actor + 0x42));
                    }
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        } else if (state == 0x11c9e) {
            queue_sprite(ram, main_rom, actor, 0);
            if (decrement(ram, actor + 0x42)) break;
            world_queue_sound(ram, 0x3c);
            write_word(ram, actor + 0x42, 0x18);
            for (;;) {
                uint16_t cursor = read_word(ram, actor + 0x48);
                uint16_t offset = read_word(bank, cursor);
                if (offset == 0xffff) break;
                write_word(ram, actor + 0x40, offset);
                uint16_t flash = effect_slot(ram, actor, 0x1e07);
                if (flash != 0x8480) write_word(ram, flash + 2, 0x1000);
                write_word(ram, flash + 0x21, x);
                write_word(ram, flash + 0x24, y);
                write_word(ram, flash + 0x54, read_word(ram, actor + 0x54));
                write_word(ram, flash + 0x40, offset);
                ram[flash + 0x14] = ram[actor + 0x14];
                write_word(ram, actor + 0x48, cursor + 2);
            }
            uint16_t list = read_word(ram, actor + 0x46) + 2;
            write_word(ram, actor + 0x46, list);
            uint16_t next = read_word(bank, list);
            if (next == 0xffff) {
                write_word(ram, actor + 0x42, 0x20);
                write_word(ram, actor, 0x1d41);
            } else
                write_word(ram, actor + 0x48, next);
        } else if (state == 0x11d41) {
            queue_sprite(ram, main_rom, actor, 1);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x1d8a);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(0x62 + frame));
                if (sprite == 0xffff) {
                    write_word(ram, parent + 0x62, read_word(ram, parent + 0x62) & ~1u);
                    finish_animation(ram, actor);
                    break;
                }
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x70 + frame)));
                write_word(ram, actor + 0x40, frame + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    }
    /* 1000:1E07 (init) and 1E36: a launch flash that releases one 1EA7
       torpedo on its fifth frame. */
    case 0x11e07:
    case 0x11e36: {
        const uint8_t *bank = main_rom + 0x10000;
        if (address == 0x11e07) {
            uint16_t offset = read_word(ram, actor + 0x40);
            ram[actor + 0x2c] = 3;
            ram[actor + 0x2d] = 0x2d;
            write_word(ram, actor + 0x3c, 0xffff);
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(bank, (uint16_t)(0x94 + offset)));
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, (uint16_t)(0x96 + offset)));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x1e36);
        }
        if (read_word(ram, actor + 0x40) >= 0x0a) scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0xbc + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0xd8 + frame)));
            if (frame == 8) {
                uint16_t torpedo = effect_slot(ram, actor, 0x1ea7);
                if (torpedo != 0x8480) write_word(ram, torpedo + 2, 0x1000);
                write_word(ram, torpedo + 0x21, read_word(ram, actor + 0x21) - 0x10);
                write_word(ram, torpedo + 0x24, read_word(ram, actor + 0x24));
                write_word(ram, torpedo + 0x38, read_word(ram, actor + 0x54));
                ram[torpedo + 0x2c] = ram[actor + 0x14];
            }
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* 1000:1EA7 (init), 1EDB (run out), 1F70 (slow and turn), 1FBF (dive
       back) and 2040 (straight): a boss torpedo. */
    case 0x11ea7:
    case 0x11edb:
    case 0x11f70:
    case 0x11fbf:
    case 0x12040: {
        const uint8_t *bank = main_rom + 0x10000;
        if (address == 0x11ea7) {
            unsigned pick = (read_word(ram, 0x26e) & 0x100) ? (world_random_byte(ram) & 0x0e) : 6;
            ram[actor + 0x2d] = 0x2e;
            write_word(ram, actor + 0x3c, 0xf77b);
            write_word(ram, actor + 0x26, 0x100);
            write_word(ram, actor + 0x36, 0x400);
            write_word(ram, actor + 0x60, read_word(bank, 0x1a4 + pick));
            write_word(ram, actor, 0x1edb);
        }
        uint16_t damage = swap16((uint16_t)(((uint16_t)(read_word(ram, actor + 0x26) - 1) & 0xff80) << 2));
        if (address == 0x11ea7 || address == 0x11edb) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            uint16_t x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
            uint16_t range = read_word(ram, actor + 0x60);
            if (range) {
                uint16_t step = (uint16_t)-read_word(ram, actor + 0x38);
                write_word(ram, actor + 0x60, range - step);
                if (range <= step) write_word(ram, actor, 0x1f70);
            }
            write_word(ram, actor + 0x2a, read_word(bank, 0x1c0 + (damage & 2)));
        } else if (address == 0x11f70) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            int16_t speed = (int16_t)(read_word(ram, actor + 0x38) + 0x20);
            write_word(ram, actor + 0x38, (uint16_t)speed);
            if (speed >= 0) {
                write_word(ram, actor + 0x42, 0x40);
                write_word(ram, actor + 0x2e, 0);
                write_word(ram, actor + 0x3a, 0x40);
                write_word(ram, actor, 0x1fbf);
            }
            uint16_t turn = swap16((uint16_t)(((uint16_t)-(uint16_t)speed & 0x300) << 2));
            write_word(ram, actor + 0x2a, read_word(bank, 0x1b4 + ((damage + turn) & 0x0e)));
        } else if (address == 0x11fbf) {
            uint16_t speed = read_word(ram, actor + 0x2e) + 0x20;
            write_word(ram, actor + 0x2e, speed);
            if ((int16_t)speed > 0x500)
                speed = 0x500;
            else
                move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            write_word(ram, actor + 0x38, speed);
            move_actor_axis(ram, actor + 0x20, speed);
            if (read_word(ram, actor + 0x21) > 0x1c0) {
                finish_animation(ram, actor);
                break;
            }
            uint16_t frame = damage + swap16((uint16_t)((read_word(ram, actor + 0x2e) & 0xff00) << 2));
            uint16_t sprite = frame < 0x38 ? read_word(bank, (uint16_t)(0x1c4 + frame)) : 0xffff;
            if (sprite == 0xffff)
                write_word(ram, actor, 0x2040);
            else {
                write_word(ram, actor + 0x2a, sprite);
                ram[actor + 0x29] = bank[(uint16_t)(0x8d0 + (frame >> 2))];
            }
        } else {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            if (read_word(ram, actor + 0x21) > 0x1c0) {
                finish_animation(ram, actor);
                break;
            }
        }
        boss_torpedo_contacts(ram, main_rom, actor);
        break;
    }
    /* 1000:2180/2197: a puff released when the bay opens. */
    case 0x12180:
    case 0x12197: {
        const uint8_t *bank = main_rom + 0x10000;
        if (address == 0x12180) {
            ram[actor + 0x2c] = 3;
            ram[actor + 0x2d] = 0x2d;
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x2197);
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x628 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x648 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* A000:FDFF/FE08: alternate two palettes in slot 1Fh every tick. */
    case 0xafdff:
        ram[actor + 0x2c] = 0x1f;
        write_word(ram, actor, 0xfe08);
        /* fall through */
    case 0xafe08: {
        uint16_t frame = read_word(ram, actor + 0x40);
        queue_palette(ram, ram[actor + 0x2c] & 63, read_word(main_rom + 0xa0000, 0xfdfb + (frame & 2)), 0);
        write_word(ram, actor + 0x40, frame + 2);
        keep_si();
        break;
    }
    /* A000:F4A2 (init), F4B9 (wait for the camera) and F4C7: releases F52F
       swimmers from the left edge at random intervals. */
    case 0xaf4a2:
    case 0xaf4b9:
    case 0xaf4c7: {
        if (address == 0xaf4a2) {
            world_acquire_resource(ram, actor, 0x6d);
            write_word(ram, actor + 0x42, 0x64);
            write_word(ram, actor + 0x50, 0x1720);
            write_word(ram, actor, 0xf4b9);
        }
        if (address != 0xaf4c7) {
            if (read_word(ram, 0xb700) < read_word(ram, actor + 0x50)) break;
            write_word(ram, actor, 0xf4c7);
        }
        if (read_word(ram, 0x262) & 0x8000) finish_animation(ram, actor);
        if (decrement(ram, actor + 0x42)) break;
        uint16_t delay = (world_random_byte(ram) & 0x7f) + 0x20;
        write_word(ram, actor + 0x42, delay);
        uint16_t swimmer = effect_slot(ram, actor, 0xf52f);
        if (swimmer != 0x8480) write_word(ram, swimmer + 2, 0xa000);
        write_word(ram, swimmer + 0x21, 0x50);
        write_word(ram, swimmer + 0x24, (delay & 0x1f) + read_word(main_rom + 0xa0000, 0xef6a + (delay & 0x0e)));
        ram[swimmer + 0x2c] = ram[actor + 0x2c];
        write_word(ram, swimmer + 0x1e, delay & 0x0f);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        break;
    }
    /* A000:F52F (init) and F56D: a swimmer crossing left to right. Touching
       a submarine shoves it (words 5A and 5E); touched or shot, it turns
       into the F5F1/F61B scatter animation. */
    case 0xaf52f:
    case 0xaf56d: {
        const uint8_t *bank = main_rom + 0xa0000;
        if (address == 0xaf52f) {
            uint16_t kind = 2 * read_word(ram, actor + 0x1e);
            ram[actor + 0x2d] = 0x20;
            write_word(ram, actor + 0x3c, 0xfaeb);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 0x80);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x48, read_word(bank, (uint16_t)(0xef9a + kind)));
            write_word(ram, actor + 0x38, read_word(bank, (uint16_t)(0xef7a + kind)));
            write_word(ram, actor + 0x4c, read_word(bank, 0xefba + (kind & 2)));
            write_word(ram, actor, 0xf56d);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        if (read_word(ram, actor + 0x21) > 0x1c0) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(read_word(ram, actor + 0x4c) + (frame & 0x0e))));
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x48));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        uint16_t sub = world_player_contact(ram, main_rom, actor);
        if (sub) {
            write_word(ram, sub + 0x5a, 0xc0);
            write_word(ram, sub + 0x5e, 0xc0);
        } else {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (read_word(ram, actor + 0x26)) {
                if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                break;
            }
        }
        write_word(ram, actor + 0x1e, (read_word(ram, actor + 0x1e) & 1) << 1);
        write_word(ram, actor, 0xf5f1);
        break;
    }
    case 0xaf5f1:
    case 0xaf61b: {
        const uint8_t *bank = main_rom + 0xa0000;
        if (address == 0xaf5f1) {
            unsigned kind = read_word(ram, actor + 0x1e) & 2;
            ram[actor + 0x2d] = 0x20;
            write_word(ram, actor + 0x4c, read_word(bank, 0xefde + kind));
            write_word(ram, actor + 0x48, read_word(bank, 0xf00e + kind));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xf61b);
        }
        scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x4c)));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x48))));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* A000:EC7E (init) and ECAE: a scripted swimmer that faces along its
       horizontal velocity and cycles a per-leg animation. */
    case 0xaec7e:
    case 0xaecae: {
        const uint8_t *bank = main_rom + 0xa0000;
        if (address == 0xaec7e) {
            unsigned variant = ram[actor + 0x1f] & 6;
            ram[actor + 0x2c] = 0x0e;
            ram[actor + 0x2d] = 0x3f;
            write_word(ram, actor + 0x3c, 0xfb4b);
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor + 0x4c, read_word(bank, 0xec6a + variant));
            begin_linear_script(ram, main_rom, actor, read_word(bank, 0xec26 + variant));
            write_word(ram, actor, 0xecae);
        }
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x29] = !(read_word(ram, actor + 0x38) >> 15);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t leg = read_word(ram, actor + 0x4c);
            uint16_t set = read_word(bank, leg + 2);
            write_word(ram, actor + 0x42, read_word(bank, leg));
            write_word(ram, actor + 0x4a, read_word(bank, (uint16_t)(0xec04 + set)));
            write_word(ram, actor + 0x46, read_word(bank, (uint16_t)(0xec22 + set)));
            write_word(ram, actor + 0x48, read_word(bank, (uint16_t)(0xebc8 + set + (read_word(ram, actor + 0x1e) & 4))));
            write_word(ram, actor + 0x4c, leg + 4);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x48))));
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x4a))));
            frame += 2;
            write_word(ram, actor + 0x40, frame >= read_word(ram, actor + 0x46) ? 0 : frame);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xaeda6:
        ram[actor + 0x2c] = main_rom[0xaeda9];
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x42, 600);
        write_word(ram, actor + 0x50, read_word(ram, 0xb700) + 16);
        write_word(ram, actor + 0x52, read_word(main_rom + 0xa0000, 0xed9a + 2 * read_word(ram, 0x84)));
        write_word(ram, actor, 0xedd0);
        break;
    case 0xaedd0:
    case 0xaee4f:
    case 0xaeebf:
        update_stage_trigger(ram, main_rom, actor);
        break;
    case 0x7ea17: {
        const uint8_t *bank = main_rom + 0x70000;
        world_acquire_resource(ram, actor, read_word(bank, 0xea18));
        ram[actor + 0x2d] = 32;
        unsigned variant = ram[actor + 0x1e] & 6;
        if (variant == 4) variant = world_random_byte(ram) & 2;
        write_word(ram, actor + 0x50, read_word(bank, 0xe74e + variant));
        write_word(ram, actor + 0x2a, read_word(bank, 0xe74a + (variant & 2)));
        write_word(ram, actor + 0x3c, read_word(bank, 0xe6e6 + (variant & 2)));
        begin_linear_script(ram, main_rom, actor, 0xe756);
        write_word(ram, actor, 0xea62);
        break;
    }
    case 0x7ea62:
    case 0x7eb1c: {
        if (!tiles) return 0;
        int step = -1;
        if (address == 0x7ea62) {
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0xff02) scroll_actor(ram, actor, 0xb71a);
            else if (material != 3) step = step_flight_script(ram, main_rom, actor);
        } else step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0) {
            begin_linear_script(ram, main_rom, actor, 0xe774);
            write_word(ram, actor, 0xeb1c);
        }
        if (outside_pickup_area(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        uint16_t player = world_player_contact(ram, main_rom, actor);
        if (player) {
            uint16_t stats = read_word(ram, player + 0x70), value = read_word(ram, actor + 0x50);
            write_word(ram, stats + 0x1a, read_word(ram, stats + 0x1a) + value);
            write_word(ram, stats + 0x1c, read_word(ram, stats + 0x1c) + value);
            world_queue_sound(ram, 0x31);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7ece5:
        ram[actor + 0x2c] = main_rom[0x7ece8];
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0xffff);
        write_word(ram, 0x844c, actor);
        write_word(ram, actor, 0xecfe);
        /* fall through */
    case 0x7ecfe: {
        uint16_t parent = read_word(ram, actor + 0x40);
        if (parent != 0xffff && !(ram[parent + 0x0a] & 1)) {
            write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
            write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
            uint16_t phase = read_word(ram, actor + 0x46) + 0x20;
            write_word(ram, actor + 0x46, phase);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xef9a + 2 * ((phase >> 8) & 3)));
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    }
    case 0xb2c46: case 0xb2c68: case 0xb2ca0: case 0xb2cd3: case 0xb2d80:
        if (!surface_enemy_swim(ram, main_rom, tiles, actor, (uint16_t)address)) return 0;
        break;
    case 0x7e21c: {
        const uint8_t *bank = main_rom + 0x70000;
        uint16_t script = read_word(bank, 0xde52 + 2*read_word(ram, actor + 0x54));
        if (read_word(bank, script) != 2) return 0;
        world_acquire_resource(ram, actor, 0xff);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(bank, 0xe22b));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        begin_linear_script(ram, main_rom, actor, script);
        write_word(ram, actor, 0xe24c);
        break;
    }
    case 0x7e24c:
    case 0x7e260:
    case 0x7e30e: {
        if (!tiles) return 0;
        if (address == 0x7e24c) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0) {
                write_word(ram, actor + 0x3a, -512);
                write_word(ram, actor, 0xe260);
            }
        } else {
            scroll_actor(ram, actor, 0xb71a);
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t top = ram[0x26e] & 2 ? 0x68 : read_word(ram, 0xb732) + 0x10;
        if (x > 0x1c0 || x < 0x40 || (int16_t)y < top) { finish_animation(ram, actor); break; }
        if (address == 0x7e30e) {
            queue_sprite(ram, main_rom, actor, 0);
            int hit = world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02;
            if (!hit) {
                ram[actor + 0xd] = 2;
                hit = world_player_contact(ram, main_rom, actor) != 0;
                if (!hit) {
                    ram[actor + 0xd] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    hit = !read_word(ram, actor + 0x26);
                }
            }
            if (hit) { warship_part_explosion(ram, main_rom, actor); finish_animation(ram, actor); }
            break;
        }
        unsigned direction = ram[actor + 0x28] >> 3;
        ram[actor + 0x29] = main_rom[0xb1b38 + direction];
        uint16_t table = 0x1af8 + 2*(direction & 15);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, table + read_word(ram, actor + 0x40)));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - (y - 8) + read_word(ram, 0xb70c));
        if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
            write_word(ram, actor + 0x3a, -320);
            uint16_t splash = effect_slot(ram, actor, 0x6f54);
            unsigned scroll = read_word(ram, 0xb70c) & 7;
            write_word(ram, splash + 0x21, x);
            write_word(ram, splash + 0x24, ((y - 8 - scroll - 1) & 0xfff8) + scroll + 8);
            ram[splash + 0x23] = 0;
            write_word(ram, splash + 0x4c, read_word(main_rom, 0x6d2a));
            write_word(ram, splash + 0x4a, read_word(main_rom, 0x6d2f));
            write_word(ram, splash + 0x2c, 0x200a);
            write_word(ram, splash + 0x56, 48);
            write_word(ram, splash + 0x58, 128);
            write_word(ram, actor + 0x40, 32);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, table + 32));
            write_word(ram, actor, 0xe30e);
        }
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb6a4d: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned choice = 2*(ram[actor + 0x29] & 1) + 2*read_word(ram, actor + 0x62);
        uint16_t script = read_word(bank, 0x4e16 + choice);
        if (read_word(bank, script) != 2) return 0;
        begin_linear_script(ram, main_rom, actor, script);
        ram[actor + 0x29] = 0;
        ram[actor + 0xc] = 12;
        write_word(ram, actor + 0x3c, read_word(bank, 0x6a71));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x800);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x6e, 1);
        write_word(ram, actor + 0x54, 3);
        write_word(ram, actor + 0x56, 6);
        write_word(ram, actor, 0x6a95);
    }
        /* fall through */
    case 0xb6a95:
    case 0xb6ad6:
    case 0xb6b25:
    case 0xb6b7c:
    case 0xb6bd8: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t state = read_word(ram, actor);
        int descending = state == 0x6b7c || state == 0x6bd8;
        if (descending && !tiles) return 0;
        if (state == 0x6a95 || state == 0x6b25) {
            int result = step_flight_script(ram, main_rom, actor);
            if (!result) return 0;
            if (result < 0) {
                if (state == 0x6a95 && ram[actor + 0x28] != 0xc0) {
                    write_word(ram, actor + 0x38, ram[actor + 0x28] & 128 ? -512 : 512);
                    write_word(ram, actor, 0x6ad6);
                } else {
                    write_word(ram, actor + 0x3a, -512);
                    write_word(ram, actor, 0x6b7c);
                }
            }
            unsigned angle = (ram[actor + 0x28] + 8) & 0xf0;
            write_word(ram, actor + 0x2a, read_word(bank, 0x4f2c + (angle >> 3)));
        } else {
            scroll_actor(ram, actor, 0xb71a);
            unsigned axis = descending ? 1 : 0;
            move_actor_axis(ram, actor + 0x20 + 3*axis, read_word(ram, actor + 0x38 + 2*axis));
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t floor = ram[0x26e] & 2 ? 0x68 : read_word(ram, 0xb732) + 0x10;
        if (descending && (x < 0x40 || x > 0x1c0 || (state == 0x6bd8 && (int16_t)y < floor))) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (state == 0x6ad6) {
            int turn = !(ram[0x264] & 3);
            if (!turn) {
                uint16_t target = world_enemy_target(ram, actor);
                int16_t tx = read_word(ram, target + 0x21), threshold = x - 32;
                turn = ram[actor + 0x28] & 128 ? threshold <= tx : threshold >= tx;
            }
            if (turn) {
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x4f08 + ((ram[actor + 0x28] >> 6) & 2)));
                write_word(ram, actor, 0x6b25);
            }
        }
        if (state == 0x6b7c) {
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
                uint16_t splash = effect_slot(ram, actor, 0x6f54);
                unsigned scroll = read_word(ram, 0xb70c) & 7;
                write_word(ram, splash + 0x21, x);
                write_word(ram, splash + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
                ram[splash + 0x23] = 0;
                write_word(ram, splash + 0x4c, read_word(main_rom, 0x6d2a));
                write_word(ram, splash + 0x4a, read_word(main_rom, 0x6d2f));
                write_word(ram, splash + 0x2c, 0x200a);
                write_word(ram, splash + 0x56, 48);
                write_word(ram, splash + 0x58, 128);
                write_word(ram, actor + 0x6e, 1);
                ram[actor + 0x14] = 3;
                write_word(ram, actor + 0x3a, -384);
                write_word(ram, actor, 0x6bd8);
            }
        }
        if (!decrement(ram, actor + 0x54)) {
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0xbe45);
            write_word(ram, trail + 0x21, x);
            write_word(ram, trail + 0x24, y);
            ram[trail + 0x2c] = ram[actor + 0x14];
            ram[trail + 0x2d] = 0x28;
            write_word(ram, trail + 0x1e, read_word(ram, actor + 0x6e));
        }
        int hit = 0;
        if (state == 0x6bd8) {
            hit = world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02;
            if (!hit) {
                ram[actor + 0xd] = 2;
                hit = world_player_contact(ram, main_rom, actor) != 0;
                if (!hit) {
                    ram[actor + 0xd] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    hit = !read_word(ram, actor + 0x26);
                }
            }
        } else {
            ram[actor + 0xd] = 0;
            world_surface_contact(ram, main_rom, actor);
            hit = !read_word(ram, actor + 0x26);
        }
        if (hit) { spawn_impact(ram, main_rom, actor); finish_animation(ram, actor); }
        break;
    }
    case 0xbe45:
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, read_word(main_rom, 0xbe3d + 2*(ram[actor + 0x1e] & 1)));
        write_word(ram, actor, 0xbe6b);
        break;
    case 0x11723: case 0x11742: case 0x117da: case 0x11819: case 0x118a3: case 0x118bd:
    case 0x119f9: case 0x11a09: case 0x11a63: case 0x11adc: case 0x11af1:
    case 0x11b41: case 0x11b57:
        if (!boss_claw_step(ram, main_rom, actor, (uint16_t)address)) return 0;
        break;
    case 0x1108c: case 0x110f8: case 0x11122: case 0x11155: case 0x111f6:
    case 0x11213: case 0x11241: case 0x11258: case 0x11290: case 0x112ce:
    case 0x1133a: case 0x1138b:
        boss_appendage(ram, main_rom, actor, (uint16_t)address);
        break;
    case 0x11436:
        world_queue_sound(ram, 0x46);
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x2d;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x1450);
        /* fall through */
    case 0x11450: {
        const uint8_t *bank = main_rom + 0x10000;
        if (read_word(ram, actor + 0x40) >= 10) scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0xf2 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0x112 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x11035: {
        const uint8_t *bank = main_rom + 0x10000;
        world_acquire_resource(ram, actor, 0x69);
        world_acquire_resource(ram, actor, 0xea);
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x2f;
        ram[actor + 0x0c] = 4;
        uint16_t index = read_word(ram, actor + 0x52);
        write_word(ram, actor + 0x2e, read_word(bank, 0x58c + 2 * (index & 1)));
        uint16_t child = spawn_boss_part(ram, actor, 0x1496);
        write_word(ram, child + 0x52, index);
        write_word(ram, child + 0x42, read_word(bank, 0x428 + 2 * (index & 3)));
        write_word(ram, child + 0x50, actor);
        write_word(ram, actor, 0x108c);
        break;
    }
    case 0x12069: {
        if (read_word(ram, 0x262) & 0x8800) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t phase = read_word(ram, actor + 0x40);
        uint16_t job = 0x9e06 + 0x128;
        write_word(ram, job, read_word(main_rom + 0x10000, 0x8cc + (phase & 2)));
        write_word(ram, job + 2, 0x40);
        write_word(ram, job + 4, 0);
        write_word(ram, job + 6, 0);
        write_word(ram, actor + 0x40, phase + 2);
        break;
    }
    case 0x120b3: {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (ram[parent + 0x0a] & 1) {
            finish_animation(ram, actor);
        } else if (!decrement(ram, actor + 0x42)) {
            uint16_t phase = read_word(ram, actor + 0x40);
            uint16_t palette = read_word(main_rom + 0x10000, 0x8c8 + (phase & 2));
            queue_palette(ram, ram[actor + 0x2c] & 63, palette, 5);
            write_word(ram, actor + 0x42, 33);
            write_word(ram, actor + 0x40, phase + 2);
        }
        break;
    }
    case 0x10917: {
        const uint8_t *bank = main_rom + 0x10000;
        world_acquire_resource(ram, actor, 0x67);
        world_acquire_resource(ram, actor, 0xea);
        ram[actor + 0x2c] = ram[actor + 0x14];
        write_word(ram, actor + 0xe, 0x40);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, read_word(bank, 0x939));
        write_word(ram, actor + 0x2a, read_word(bank, 0x93e));
        uint16_t cfg = read_word(bank, 0x766 + read_word(ram, 0x88));
        uint16_t hp = read_word(bank, 0x786 + cfg);
        write_word(ram, actor + 0x26, hp);
        write_word(ram, actor + 0x36, hp);
        write_word(ram, actor + 0x4a, hp);
        write_word(ram, actor + 0x50, read_word(bank, 0x788 + cfg));
        write_word(ram, actor + 0x52, read_word(bank, 0x788 + cfg));
        write_word(ram, actor + 0x54, read_word(bank, 0x78a + cfg));
        write_word(ram, actor + 0x60, 15);
        spawn_boss_part(ram, actor, 0x2069);
        uint16_t child = spawn_boss_part(ram, actor, 0x20b3);
        ram[child + 0x2c] = ram[actor + 0x2c];
        write_word(ram, child + 0x42, 1);
        for (unsigned i = 0; i < 4; ++i) {
            child = spawn_boss_part(ram, actor, 0x1035);
            write_word(ram, child + 0x52, i);
            write_word(ram, child + 0x56, read_word(bank, 0x9a3 + 29*i));
            write_word(ram, child + 0x50, actor);
            write_word(ram, child + 0xe, 0x100 + i);
        }
        begin_linear_script(ram, main_rom, actor, 0x666);
        world_queue_sound(ram, 0x8c);
        uint16_t tail = (read_word(ram, 0xa6d8) + 2) & 14;
        if (tail != read_word(ram, 0xa6d6)) {
            write_word(ram, 0xa6d8, tail);
            write_word(ram, 0xa6a2 + tail, 0x11);
        }
        write_word(ram, actor, 0xa22);
        break;
    }
    case 0x10a22: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0) write_word(ram, actor, 0xa46);
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0xd] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;
    }
    case 0x10a46:
    case 0x10a6b:
    case 0x10f69:
    case 0x10fab:
    case 0x10fce:
    case 0x10fe5:
    case 0x11009:
    case 0x11025:
    case 0x11bcf: {
        const uint8_t *bank = main_rom + 0x10000;
        if (address == 0x10a46 || address == 0x10f69) {
            ram[actor + 0xc] = 4;
            uint16_t target = world_enemy_target(ram, actor);
            unsigned choice = 0;
            if (address == 0x10a46) {
                if (ram[0x264] & 3) {
                    choice = read_word(ram, target + 0x24) < read_word(ram, actor + 0x24) ? 2 : 0;
                    write_word(ram, actor + 0x56, choice);
                }
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x694 + choice));
                address = 0x10a6b;
            } else {
                choice = 2;
                if (ram[0x264] & 3) {
                    uint16_t y = read_word(ram, target + 0x24);
                    choice = y < 0xa8 ? 4 : y >= 0x160 ? 0 : 2;
                } else target = 0;
                write_word(ram, actor + 0x4e, target);
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x6c4 + choice));
                address = 0x10fab;
            }
            write_word(ram, actor, (uint16_t)address);
        }
        if (address == 0x10fe5 || address == 0x11009) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0 && address == 0x10fe5) {
                ram[actor + 0x63] &= (uint8_t)~1u;
                ram[actor + 0x63] |= 2;
                begin_linear_script(ram, main_rom, actor, 0x758);
                write_word(ram, actor, 0x1009);
            } else if (step < 0) {
                ram[actor + 0x63] &= (uint8_t)~2u;
                write_word(ram, actor + 0x42, 80);
                write_word(ram, actor, 0x1025);
            }
        } else if (address == 0x11025 && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor, 0xa46);
        }
        int calm = !(ram[actor + 0x66] & 1);
        if (calm && (address == 0x10a6b || address == 0x10fab)) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0 && address == 0x10fab) {
                write_word(ram, actor + 0x42, 96);
                ram[actor + 0x62] &= ~16u;
                write_word(ram, actor, 0xfce);
            } else if (step < 0) {
                uint16_t arms = read_word(ram, actor + 0x60), sequence = read_word(ram, actor + 0x58);
                if (arms == 15 && read_word(bank, 0x18 + (sequence & 6))) {
                    write_word(ram, actor, 0xfe5);
                    ram[actor + 0x63] |= 1;
                    begin_linear_script(ram, main_rom, actor, 0x746);
                } else if (arms & 3) {
                    write_word(ram, actor, 0xf69);
                    ram[actor + 0x62] |= 16;
                } else {
                    uint16_t side = read_word(ram, actor + 0x56) + 2;
                    write_word(ram, actor + 0x56, side);
                    begin_linear_script(ram, main_rom, actor, read_word(bank, 0x694 + (side & 2)));
                }
                if ((arms == 15 && read_word(bank, 0x18 + (sequence & 6))) || (arms & 3)) {
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                    write_word(ram, actor + 0x58, sequence + 2);
                }
            }
        } else if (calm && address == 0x10fce && !decrement(ram, actor + 0x42)) write_word(ram, actor, 0xa46);
        if (ram[actor + 0x19]) {
            --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x14 + (ram[actor + 0x19] & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0x11bcf && !(ram[actor + 0x62] & 1)) write_word(ram, actor, 0xa6b);
        if (calm && address == 0x10a6b && !decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            ram[actor + 0x62] |= 1;
            uint16_t child = spawn_boss_part(ram, actor, 0x1be1);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            ram[child + 0x14] = ram[actor + 0x14];
            write_word(ram, child + 0x54, read_word(ram, actor + 0x54));
            ram[child + 0x2d] = 0x2e;
            write_word(ram, child + 0x44, 1);
            world_queue_sound(ram, 0x66);
            write_word(ram, actor, 0x1bcf);
        }
        if (calm) write_word(ram, actor + 0x26, read_word(ram, actor + 0x4a));
        ram[actor + 0xd] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0xd] = calm ? 0 : 1;
        uint16_t attacker = world_projectile_contact(ram, main_rom, actor);
        if (calm && !read_word(ram, actor + 0x26)) {
            if (ram[0x264] & 3) {
                ram[0x124 + 32*(ram[attacker + 0xb] & 2)] |= 1;
                world_award_score(ram, main_rom, actor, attacker);
                ram[actor + 0xa] |= 2;
                write_word(ram, actor + 0x3a, -128);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x42, 1);
                ram[actor + 0x2c] = ram[actor + 0x14];
                ram[0x262] |= 128;
                write_word(ram, actor, 0xea3);
                break;
            }
            write_word(ram, actor + 0x26, 256);
        }
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        if (!calm) {
            if (!decrement(ram, actor + 0x68)) {
                ram[actor + 0x66] = (ram[actor + 0x66] & ~1u) | 4;
                write_word(ram, actor + 0x68, 64);
            }
            break;
        }
        write_word(ram, actor + 0x4a, read_word(ram, actor + 0x26));
        if (!(ram[actor + 0x66] & 2) && !(ram[actor + 0x60] & 3)) {
            write_word(ram, actor + 0x66, 3);
            write_word(ram, actor + 0x2a, read_word(bank, 0xc0f));
            write_word(ram, actor + 0x68, 256);
            write_word(ram, actor + 0x50, 8);
            write_word(ram, actor + 0x52, 32);
            uint16_t part = spawn_boss_part(ram, actor, 0xcd1);
            ram[part + 0x2c] = ram[actor + 0x14];
            spawn_boss_part(ram, actor, 0xe0b);
            part = spawn_boss_part(ram, actor, 0xc8f);
            ram[part + 0x2d] = ram[actor + 0x2d];
        } else if ((ram[actor + 0x66] & 4) && !decrement(ram, actor + 0x68)) {
            write_word(ram, actor + 0x68, 32);
            uint16_t step = read_word(ram, actor + 0x40);
            boss_blast(ram, main_rom, actor, 0x8a2 + step);
            write_word(ram, actor + 0x40, step + 6 < 36 ? step + 6 : 0);
        }
        break;
    }
    case 0x10ea3:
    case 0x10f16: {
        if (address == 0x10ea3) {
            if (!tiles) return 0;
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            if (world_terrain_mask(ram, tiles + 0x4000, x, y - 48) & 0xff02) {
                write_word(ram, actor + 0x38, -512);
                write_word(ram, actor + 0x31, x);
                write_word(ram, actor + 0x34, y);
                write_word(ram, actor, 0xf16);
            }
        } else {
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            uint16_t x = read_word(ram, actor + 0x31) + (world_random_byte(ram) & 2) - 1;
            write_word(ram, actor + 0x21, x);
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + (world_random_byte(ram) & 2) - 1);
            if ((int16_t)x <= 0) {
                ram[0x263] |= 0xc0;
                finish_animation(ram, actor);
                break;
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 3);
            uint16_t step = read_word(ram, actor + 0x40);
            boss_blast(ram, main_rom, actor, 0x7b8 + step);
            step += 6;
            write_word(ram, actor + 0x40, step < 60 ? step : 0);
        }
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0x7df0e:
    case 0x7dffe: {
        const uint8_t *bank = main_rom + 0x70000;
        int base = address == 0x7df0e;
        if (base) world_acquire_resource(ram, actor, 0x2a);
        write_word(ram, actor + 0xe, 0x24);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, base ? 0xffff : read_word(bank, 0xe00a));
        write_word(ram, actor + 0x2a, read_word(bank, base ? 0xdf27 : 0xe013));
        uint16_t cfg = read_word(bank, 0xdeba + (read_word(ram, actor + 0x1e) & 32) + read_word(ram, 0x8a));
        uint16_t hp = read_word(bank, 0xdefa + cfg);
        write_word(ram, actor + 0x26, hp);
        write_word(ram, actor + 0x36, hp);
        if (base) {
            uint16_t child = effect_slot(ram, actor, 0xdffe);
            if (child != 0x8480) write_word(ram, child + 2, 0x7000);
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
            ram[child + 0x2c] = ram[actor + 0x2c];
            write_word(ram, actor, 0xdf66);
            break;
        }
        ram[actor + 0xc] = 8;
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x52, read_word(bank, 0xdefc + cfg));
        write_word(ram, actor, 0xe042);
    }
        /* fall through */
    case 0x7e042:
    case 0x7e106: {
        const uint8_t *bank = main_rom + 0x70000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0xa]) { finish_animation(ram, actor); break; }
        uint16_t x = read_word(ram, parent + 0x21), y = read_word(ram, parent + 0x24);
        write_word(ram, actor + 0x21, x);
        write_word(ram, actor + 0x24, y);
        if (address != 0x7e106) {
            queue_sprite(ram, main_rom, actor, 1);
            if ((ram[0x264] & 3) && !decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                uint16_t target = world_enemy_target(ram, actor);
                unsigned side = read_word(ram, target + 0x21) < x ? 2 : 0;
                write_word(ram, actor + 0x54, side);
                write_word(ram, actor + 0x48, read_word(bank, 0xde02 + side));
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xe106);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t step = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, read_word(ram, actor + 0x48) + step);
                if (sprite == 0xffff) write_word(ram, actor, 0xe042);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0xde32 + step));
                    write_word(ram, actor + 0x40, step + 2);
                    if (step == 4 || step == 10) {
                        uint16_t side = read_word(ram, actor + 0x54) + (step == 10);
                        write_word(ram, actor + 0x54, side);
                        uint16_t dx = read_word(bank, 0xde4e + (side & 2));
                        uint16_t flash = effect_slot(ram, actor, 0xa2c0);
                        write_word(ram, flash + 0x31, dx);
                        write_word(ram, flash + 0x34, 29);
                        ram[flash + 0x2d] = 3;
                        write_word(ram, flash + 0x60, 3);
                        uint16_t shot = effect_slot(ram, actor, 0xe21c);
                        if (shot != 0x8480) write_word(ram, shot + 2, 0x7000);
                        write_word(ram, shot + 0x54, side);
                        write_word(ram, shot + 0x21, x + dx);
                        write_word(ram, shot + 0x24, y + 29);
                        world_queue_sound(ram, 0x63);
                    }
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        }
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26) && read_word(ram, parent + 0x26)) register_surface_enemy(ram, actor);
        else {
            world_award_score(ram, main_rom, actor, hit);
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            write_word(ram, parent + 0x46, 1);
            write_word(ram, actor + 0x2a, read_word(bank, 0xe0f8));
            write_word(ram, actor, 0xe1d6);
        }
        break;
    }
    case 0x7e1d6: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        uint16_t speed = read_word(ram, actor + 0x3a) - 16;
        write_word(ram, actor + 0x3a, speed);
        move_actor_axis(ram, actor + 0x23, speed);
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t y = read_word(ram, actor + 0x24);
        uint16_t cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                           0x177 - y + read_word(ram, 0xb70c));
        if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
            write_word(ram, actor + 0x3a, -256);
            unsigned scroll = read_word(ram, 0xb70c) & 7;
            write_word(ram, actor + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
            ram[actor + 0x23] = 0;
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c03));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c08));
            write_word(ram, blast + 0x44, 0x57);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 64);
            write_word(ram, blast + 0x58, 0);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7df66:
    case 0x7dfb4: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t x = read_word(ram, actor + 0x21);
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        if (address == 0x7dfb4 && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xde46 + (frame & 6)));
            write_word(ram, actor + 0x44, 9);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (address == 0x7df66) {
            ram[actor + 0xd] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26) || read_word(ram, actor + 0x46)) {
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xdfb4);
            }
        }
        break;
    }
    case 0x7e41e: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, read_word(bank, 0xe41f));
        world_acquire_resource(ram, actor, read_word(bank, 0xe41a + 2 * (variant & 1)));
        world_acquire_resource(ram, actor, read_word(bank, 0xe43a));
        ram[actor + 0x2c] = ram[actor + 0x15];
        write_word(ram, actor + 0xe, 0x1c);
        write_word(ram, actor + 0x26, 0x120);
        write_word(ram, actor + 0x36, 0x120);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x3c, read_word(bank, 0xe45d));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        unsigned height = variant & 6;
        if (!height) height = world_random_byte(ram) & 6;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xe3d0 + height));
        write_word(ram, actor, 0xe48a);
        break;
    }
    case 0x7e48a: {
        const uint8_t *bank = main_rom + 0x70000;
        if (!read_word(ram, actor + 0x26)) return 0;
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            keep_si();                      /* 432B never loads SI */
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t phase = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, 0xe3a0 + phase));
            write_word(ram, actor + 0x46, read_word(bank, 0xe3b8 + phase));
            write_word(ram, actor + 0x44, 8);
            phase += 2;
            write_word(ram, actor + 0x40, phase < 24 ? phase : 0);
        }
        uint16_t x = read_word(ram, actor + 0x31) + read_word(ram, actor + 0x46);
        write_word(ram, actor + 0x21, x);
        if (ram[actor + 0x19]) {
            unsigned flash = --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x15 + ((flash >> 1) & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) ram[actor + 0x19] = (ram[actor + 0x19] & 1) + 20;
            if (read_word(ram, actor + 0x26)) {
                leave_si(read_word(ram, actor + 0x3c));   /* D2B7 at 7E50D */
                break;
            }
        }
        ram[actor + 0xb] = ram[hit + 0xb];
        world_award_score(ram, main_rom, actor, hit);
        uint16_t y = read_word(ram, actor + 0x24) + 0x70;
        if (ram[actor + 0x1e] & 1) {
            for (int direction = 7; direction >= 0; --direction) {
                uint16_t child = effect_slot(ram, actor, 0xe5d8);
                if (child != 0x8480) write_word(ram, child + 2, 0x7000);
                write_word(ram, child + 0x21, x);
                write_word(ram, child + 0x24, y);
                ram[child + 0x28] = direction * 32;
            }
        }
        uint16_t blast = effect_slot(ram, read_word(ram, 0x9c), 0xe587);
        if (blast != 0x8480) write_word(ram, blast + 2, 0x7000);
        write_word(ram, blast + 0x21, x);
        write_word(ram, blast + 0x24, y);
        ram[blast + 0x2c] = ram[actor + 0x14];
        ram[blast + 0x2d] = 0x20;
        write_word(ram, blast + 0x3c, read_word(bank, 0xe55f));
        write_word(ram, blast + 0x36, 0x120);
        write_word(ram, blast + 0x44, 1);
        world_queue_sound(ram, 0x5e);
        finish_animation(ram, actor);
        break;
    }
    case 0x7e587: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        uint16_t frame = read_word(ram, actor + 0x40);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t sprite = read_word(bank, 0xe3d8 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); keep_si(); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0xe3fa + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (frame >= 4 && frame < 8 && read_word(ram, actor + 0x3c) != 0xffff)
            world_append_candidate(ram + 0xa6e2, actor);
        si_after_sprite();
        break;
    }
    case 0xb2e05:
    case 0xb2e4a:
    case 0xb2e5c: {
        uint16_t speed = read_word(ram, actor + 0x38);
        int16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
        int magnitude = abs((int16_t)speed);
        if (address == 0xb2e05) {
            if (magnitude <= 512) speed -= read_word(ram, actor + 0x46);
            unsigned fraction = ram[actor + 0x20] + (speed & 255);
            x += (int8_t)(speed >> 8) + (fraction >> 8);
        }
        /* Offscreen kills also consume the collision routine's scratch BX. */
        if (x <= -64 || x >= 576) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb2e05) {
            write_word(ram, actor + 0x38, speed);
            if (speed || magnitude > 512) {
                surface_enemy_pose(ram, main_rom + 0xb0000, actor, abs((int16_t)speed));
                move_actor_axis(ram, actor + 0x20, speed);
            } else {
                write_word(ram, actor, 0x2e4a);
                write_word(ram, actor + 0x4c, 0x172a);
                write_word(ram, actor + 0x4a, 0);
                write_word(ram, actor + 0x3a, 0);
                write_word(ram, actor + 0x42, 16);
                address = 0xb2e4a;
            }
        }
        if (address == 0xb2e4a && !decrement(ram, actor + 0x42)) write_word(ram, actor, 0x2e5c);
        if (address == 0xb2e5c) {
            uint16_t dy = read_word(ram, actor + 0x3a) + 32;
            write_word(ram, actor + 0x3a, dy);
            move_actor_axis(ram, actor + 0x23, dy);
            uint16_t limit = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
            if (read_word(ram, actor + 0x24) > limit) {
                unsigned bit = 1u << (ram[actor + 0x1e] & 15);
                write_word(ram, 0xc4, read_word(ram, 0xc4) & ~bit);
                finish_animation(ram, actor);
                break;
            }
        }
        ram[actor + 0x29] = speed >> 15;
        queue_surface_enemy(ram, main_rom, actor);
        surface_enemy_hit(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        break;
    }
    case 0x7e5d8: {
        if (!tiles) return 0;
        world_acquire_resource(ram, actor, 10);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(main_rom + 0x70000, 0xe5e7));
        write_word(ram, actor + 0x2e, 0x200);
        unsigned angle = ram[actor + 0x28] * 2;
        write_word(ram, actor + 0x38, 2 * read_word(main_rom + 0xa0000, 0x9e12 + angle));
        write_word(ram, actor + 0x3a, 2 * read_word(main_rom + 0xa0000, 0x9d92 + angle));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe5fd);
    }
        /* fall through */
    case 0x7e5fd: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xa787 + (frame & 14)));
            write_word(ram, actor + 0x44, 2);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!(world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                                  read_word(ram, actor + 0x24)) & 0xff0a)) {
            ram[actor + 0xd] = 0;
            if (!world_player_contact(ram, main_rom, actor)) break;
        }
        spawn_impact(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0x7e817: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned variant = read_word(ram, actor + 0x1f) & 2;
        ram[actor + 0x2c] = 7;
        write_word(ram, actor + 0xe, 7);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 64);
        write_word(ram, actor + 0x36, 16);
        write_word(ram, actor + 0x3c, read_word(bank, 0xe6e2 + variant));
        write_word(ram, actor + 0x48, read_word(bank, 0xe6ea + variant));
        write_word(ram, actor + 0x46, read_word(bank, 0xe6f2 + variant));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe857);
    }
        /* fall through */
    case 0x7e857: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        if (outside_pickup_area(ram, actor)) {
            finish_animation(ram, actor);
            if (address == 0x7e857) keep_si();     /* only 4311 ran */
            break;
        }
        if (!(ram[actor + 0x1e] & 1)) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t step = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x48) + (step & 6)));
                write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
                write_word(ram, actor + 0x40, step + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        ram[actor + 0xd] = 1;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0xd] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (read_word(ram, actor + 0x26)) {
                leave_si(read_word(ram, actor + 0x3c));   /* D2B7 loaded the shape */
                break;
            }
        }
        world_award_score(ram, main_rom, actor, hit);
        unsigned variant = read_word(ram, actor + 0x1f) & 2;
        write_word(ram, actor + 0x48, read_word(bank, 0xe6ee + variant));
        write_word(ram, actor + 0x4a, read_word(bank, 0xe6f6 + variant));
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe922);
        break;
    }
    case 0x7e922:
        if (ram[actor + 0x1e] & 1) {
            spawn_stage_pickup(ram, actor);
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor, 0xe95f);
        /* fall through */
    case 0x7e95f: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, read_word(ram, actor + 0x48) + step);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, read_word(ram, actor + 0x4a) + step));
            write_word(ram, actor + 0x40, step + 2);
            if (step == 6) spawn_stage_pickup(ram, actor);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* B000:047A (init), 04A4 (carried), 04F8 (lift off) and 057B/05B0
       (flight): the missile carried by the 0357 enemy. It rises, levels
       with a submarine, then runs left. */
    case 0xb047a:
        world_acquire_resource(ram, actor, 0x61);
        write_word(ram, actor + 0x3c, 0xf2ab);
        write_word(ram, actor + 0x26, 0xa0);
        write_word(ram, actor + 0x36, 0x80);
        ram[actor + 0x2d] = 0x21;
        ram[actor + 0x0c] = 0x0c;
        write_word(ram, actor + 0x2a, 0x4956);
        write_word(ram, actor, 0x4a4);
        break;
    case 0xb04a4: {
        uint16_t carrier = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, carrier + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, carrier + 0x24) - 3);
        if (ram[carrier + 0x0a] & 1) {
            finish_animation(ram, actor);
            break;
        }
        if (!read_word(ram, carrier + 0x46)) {
            write_word(ram, actor + 0x42, 8);
            write_word(ram, actor + 0x3a, 0xff00);
            write_word(ram, actor, 0x4f8);
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb04f8:
    case 0xb057b:
    case 0xb05b0: {
        const uint8_t *bank = main_rom + 0xb0000;
        int flame = 0;
        if (address == 0xb057b) {
            unsigned players = read_word(ram, 0x264) & 3;
            if (players == 3) {
                world_dispatch_detail = "two-player target; SI after 65B1 not recovered";
                return 0;
            }
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x42, 0x40);
            write_word(ram, actor + 0x38, 0xff00);
            write_word(ram, actor + 0x3a, 0);
            write_word(ram, actor + 0x46, 1);
            if (players) {
                uint16_t target = world_enemy_target(ram, actor);
                write_word(ram, actor + 0x50, target);
                /* 65DA/65F4 leave SI bit 0 set when the target is higher. */
                write_word(ram, actor + 0x3a,
                           read_word(ram, target + 0x24) < read_word(ram, actor + 0x24) ? 0xff80 : 0x80);
            }
            write_word(ram, actor, 0x5b0);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb04f8) {
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            if (!decrement(ram, actor + 0x42)) {
                world_queue_sound(ram, 0x74);
                write_word(ram, actor, 0x57b);
            }
            flame = 1;
        } else {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            uint16_t x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
            if (read_word(ram, actor + 0x3a)) {
                uint16_t target = read_word(ram, actor + 0x50);
                uint16_t gap = read_word(ram, actor + 0x24) - read_word(ram, target + 0x24);
                if (gap & 0x8000) gap = -gap;
                if (gap >= 4 && decrement(ram, actor + 0x42))
                    flame = 1;
                else {
                    write_word(ram, actor + 0x3a, 0);
                    write_word(ram, actor + 0x38, 0xfd00);
                }
            }
            if (!flame && !decrement(ram, actor + 0x46)) {
                write_word(ram, actor + 0x46, 4);
                uint16_t smoke = effect_slot(ram, read_word(ram, 0x9a), 0xbe41);
                write_word(ram, smoke + 0x21, x + 0x10);
                write_word(ram, smoke + 0x24, read_word(ram, actor + 0x24));
                ram[smoke + 0x2c] = 3;
            }
        }
        if (flame) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x40, frame + 2 < 6 ? frame + 2 : 0);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x2ce + frame)));
            ram[actor + 0x2c] = 3;
            queue_sprite(ram, main_rom, actor, 0);
        }
        write_word(ram, actor + 0x2a, 0x4956);
        ram[actor + 0x2c] = ram[actor + 0x14];
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor)) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    /* B000:02E0: init of the 0357 enemy; it needs its 047A companion. */
    case 0xb02e0: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t flags = read_word(ram, actor + 0x1e);
        unsigned pick = flags & 6;
        world_acquire_resource(ram, actor, 0x61);
        write_word(ram, actor + 0x3c, 0xf2a3);
        write_word(ram, actor + 0x26, 0x80);
        write_word(ram, actor + 0x36, 0x80);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x0e, 2);
        write_word(ram, actor + 0x2a, 0x490e);
        ram[actor + 0x29] = flags & 1;
        if (pick == 6) pick = world_random_byte(ram) & 6;
        write_word(ram, actor + 0x46, read_word(bank, 0x2d6 + pick));
        uint16_t companion = world_spawn_child(ram, actor, 0x47a);
        if (!companion) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, companion + 2, 0xb000);
        ram[companion + 0x29] = ram[actor + 0x29];
        write_word(ram, actor, 0x357);
        break;
    }
    case 0xb0357:
    case 0xb03f1:
    case 0xb042b: {
        scroll_actor(ram, actor, 0xb71a);
        if (address != 0xb03f1) {
            uint16_t speed;
            if (address == 0xb0357) {
                uint16_t time = read_word(ram, actor + 0x46);
                if ((int16_t)time > 16) time = 16;
                speed = (uint16_t)((time << 8) | (time >> 8)) >> 2;
            } else {
                uint16_t acceleration = read_word(ram, actor + 0x2e) + 32;
                write_word(ram, actor + 0x2e, acceleration);
                speed = (int16_t)acceleration > 512 ? 512 : acceleration;
            }
            if (!(ram[actor + 0x29] & 1)) speed = (uint16_t)-speed;
            write_word(ram, actor + 0x38, speed);
            move_actor_axis(ram, actor + 0x20, speed);
            uint16_t x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (address == 0xb03f1 && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xb0000, 0x2b0 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x2e, 0);
                write_word(ram, actor, 0x42b);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0x2c0 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (address == 0xb0357 && !decrement(ram, actor + 0x46)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x3f1);
        }
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (hit && (ram[actor + 0x0d] == 2 || !read_word(ram, actor + 0x26))) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        } else if (!read_word(ram, actor + 0x26)) {
            /* The no-contact death branch consumes an inherited BX. */
            return 0;
        }
        break;
    }
    /* B000:46D4/46E6: an animation held 32h left of its parent. */
    case 0xb46d4:
    case 0xb46e6: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb46d4) {
            ram[actor + 0x2c] = 3;
            ram[actor + 0x2d] = 0x28;
            write_word(ram, actor + 0x6e, 1);
            write_word(ram, actor, 0x46e6);
        }
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) - 0x32);
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        if (!decrement(ram, actor + 0x6e)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x3570 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x6e, read_word(bank, (uint16_t)(0x3592 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    /* B000:463D: set the 4652 spawner's end point from the variant. */
    case 0xb463d:
        write_word(ram, actor + 0x6e, ((read_word(ram, actor + 0x1e) & 0xff00) >> 4) + read_word(ram, 0xb700));
        write_word(ram, actor, 0x4652);
        /* fall through */
    case 0xb4652: {
        const uint8_t *bank = main_rom + 0xb0000;
        if ((int16_t)read_word(ram, 0xb700) >= (int16_t)read_word(ram, actor + 0x6e)) {
            finish_animation(ram, actor);
            break;
        }
        int16_t time = decrement(ram, actor + 0x42);
        if (time > 0) {
            uint16_t scroll = (uint16_t)-read_word(ram, 0xb71a);
            time = (uint16_t)time - (uint16_t)(scroll << 2);
            write_word(ram, actor + 0x42, (uint16_t)time);
            if (time > 0) break;
        }
        uint16_t child = world_spawn_child(ram, actor, 0x380f);
        unsigned frame = read_word(ram, actor + 0x40);
        if (child) {
            write_word(ram, child + 2, 0xb000);
            write_word(ram, child + 0x21, 0x1e0);
            write_word(ram, child + 0x24, read_word(bank, 0x37b0 + frame) + read_word(ram, 0xb732));
            write_word(ram, child + 0x1e, read_word(bank, 0x37c2 + frame) | ram[actor + 0x1e]);
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x44) + 1);
        }
        write_word(ram, actor + 0x42, read_word(bank, 0x379e + frame) << 2);
        frame += 2;
        write_word(ram, actor + 0x40, frame < 18 ? frame : 0);
        break;
    }
    case 0xb8244:
        if (!read_word(ram, actor + 0x48)) finish_animation(ram, actor);
        break;
    case 0xb876c:
        world_acquire_resource(ram, actor, 0x28);
        ram[actor + 0x2d] = 1;
        write_word(ram, actor + 0x26, 0x180);
        write_word(ram, actor + 0x36, 0x180);
        {
            unsigned kind = 2 * (ram[actor + 0x1e] & 15);
            write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x7fec + kind));
            write_word(ram, actor + 0x50, read_word(main_rom + 0xb0000, 0x8002 + kind));
        }
        write_word(ram, actor, 0x87a1);
        break;
    case 0xb87a1: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned kind = ram[actor + 0x1e] & 15;
        unsigned paint = read_word(bank, 0x80ee + 2*kind);
        if (paint != 0x8c85 && paint != 0x8ca8 && paint != 0x8cb0) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        unsigned damage = (uint16_t)(0x180 - read_word(ram, actor + 0x26));
        unsigned frame = 2 * ((damage >> 8) & 15);
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x50) + frame));
        queue_sprite(ram, main_rom, actor, 0);
        if (read_word(ram, actor + 0x42)) decrement(ram, actor + 0x42);
        ram[actor + 0x0d] = (ram[actor + 0x1f] & 1) ? 1 : 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (ram[actor + 0x1f] & 1) break;
        if (hit && !read_word(ram, actor + 0x42)) {
            for (unsigned i = 0; i < 2; ++i) {
                uint16_t burst = effect_slot(ram, actor, 0x6efe);
                write_word(ram, burst + 0x4c, 0x75df);
                write_word(ram, burst + 0x4a, 0x76f6);
                write_word(ram, burst + 0x2c, 0x200a);
                write_word(ram, burst + 0x56, 0);
                write_word(ram, burst + 0x58, 0);
                ram[burst + 0x2d] = 1;
                ram[burst + 0x4e] = 0x80;
                write_word(ram, burst + 0x31, i ? 24 : 8);
                write_word(ram, burst + 0x34, i ? -16 : -8);
            }
            write_word(ram, actor + 0x42, 16);
        }
        if (!read_word(ram, actor + 0x26)) {
            write_word(ram, actor + 0x44, 1);
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, 0x74e5);
            write_word(ram, blast + 0x4a, 0x7675);
            write_word(ram, blast + 0x2c, 0x200a);
            write_word(ram, blast + 0x56, 64);
            write_word(ram, blast + 0x58, 0);
            world_queue_sound(ram, 0x3e);
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
            write_word(ram, blast + 0x31, 16);
            write_word(ram, blast + 0x34, -16);
            uint16_t cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                              0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
            write_word(ram, actor + 0x52, cell);
            unsigned width = paint == 0x8c85 ? 4 : 5, height = paint == 0x8cb0 ? 4 : 6;
            for (unsigned row = 0; row < height; ++row)
                for (unsigned col = 0; col < width; ++col) {
                    unsigned dest = 0x4000 + ((cell + row*256) & 0x3f00) + ((cell + col*4) & 255);
                    write_word(tiles, dest + 2, 0x3016);
                }
            world_queue_sound(ram, 0x6d);
            write_word(ram, actor, 0x886d);
        }
        break;
    }
    case 0xb886d:
    case 0xb88bc: {
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xb886d && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            const uint8_t *bank = main_rom + 0xb0000;
            uint16_t sprite = read_word(bank, read_word(ram, actor + 0x50) + frame);
            if (sprite == 0xffff) write_word(ram, actor, 0x88bc);
            else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, 0x80de + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb0084: {
        uint16_t variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, 2);
        write_word(ram, actor + 0xe, 1);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x9e));
        ram[actor + 0x2d] = 0x20;
        ram[actor + 0x29] = variant & 1;
        write_word(ram, actor + 0x2e, 0xc0);
        write_word(ram, actor + 0x38, variant & 1 ? 0xc0 : -0xc0);
        write_word(ram, actor + 0x50, (variant >> 8) & 14);
        write_word(ram, actor + 0x44, 16);
        write_word(ram, actor + 0x46, 0x100);
        write_word(ram, actor + 0x48, 8);
        write_word(ram, actor, 0xde);
        break;
    }
    case 0xb00de:
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor, 0xef);
        /* fall through */
    case 0xb00ef:
    case 0xb01f6: {
        const uint8_t *bank = main_rom + 0xb0000;
        int turning = read_word(ram, actor) == 0x1f6;
        if (!turning && !tiles) return 0;
        if (turning) {
            if (!decrement(ram, actor + 0x42)) {
                uint16_t step = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x20 + step);
                write_word(ram, actor + 0x42, 6);
                if (sprite == 0xffff) write_word(ram, actor, 0xde);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x3c, read_word(bank, 0x56 + step));
                    if (step == 14) ram[actor + 0x29] ^= 1;
                    uint16_t speed = read_word(bank, 0x3c + step);
                    write_word(ram, actor + 0x38, ram[actor + 0x29] & 1 ? speed : -speed);
                    write_word(ram, actor + 0x40, step + 2);
                }
            }
            scroll_actor(ram, actor, 0xb71a);
        } else {
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, 0xb71a));
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        uint16_t dx = x - read_word(ram, actor + 0x31);
        if (!turning) {
            uint16_t phase = (uint32_t)dx * read_word(ram, actor + 0x46);
            int16_t wave = read_word(main_rom + 0xa0000, 0x9d92 + 2 * (phase >> 8));
            uint32_t y = ram[actor + 0x33] + 256u * read_word(ram, actor + 0x34);
            y += wave * (int16_t)read_word(ram, actor + 0x44);
            ram[actor + 0x23] = y;
            write_word(ram, actor + 0x24, y >> 8);
        }
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        uint16_t y = read_word(ram, actor + 0x24);
        if (!turning) {
            uint16_t table = read_word(bank, 0x74 + (((dx + 0x40) >> 5) & 4));
            uint16_t pose = ((y - read_word(ram, actor + 0x34) + 0x12) >> 1) & 14;
            write_word(ram, actor + 0x2a, read_word(bank, table + pose));
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (!turning) {
            uint8_t facing = ram[actor + 0x29];
            if (!decrement(ram, actor + 0x48)) {
                write_word(ram, actor + 0x48, 8);
                uint16_t wake = effect_slot(ram, read_word(ram, 0x9a), 0x7f73);
                ram[wake + 0x29] = facing ^ 1;
                write_word(ram, wake + 0x21, x + read_word(bank, 0x80 + 2 * ((facing ^ 1) & 1)));
                write_word(ram, wake + 0x24, y - 2);
            }
            if (world_terrain_mask(ram, tiles + 0x4000, x + read_word(bank, 0x7c + 2 * (facing & 1)), y) & 0xff02) {
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor, 0x1f6);
            }
        }
        ram[actor + 0xd] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0xd] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (read_word(ram, actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        world_queue_sound(ram, 0x5d);
        uint16_t impact = effect_slot(ram, actor, 0x6efe);
        write_word(ram, impact + 0x4c, read_word(main_rom, 0x6b44));
        write_word(ram, impact + 0x4a, read_word(main_rom, 0x6b49));
        ram[impact + 0x2c] = 10;
        ram[impact + 0x2d] = 0x20;
        write_word(ram, impact + 0x56, 0x30);
        write_word(ram, impact + 0x58, 0x80);
        if (!(impact & 0x300)) write_word(ram, impact + 0x44, 0x5d);
        release_actor_resources(ram, actor);
        if (x >= 0x1a0) write_word(ram, actor + 0x21, 0x1a0);
        write_word(ram, actor, 0x76a6);
        break;
    }
    case 0xb6744:
    case 0xb6781:
    /* B000:6150 (init), 6176 and 643D: the gun flash that follows its
       parent, releases the B000:6272 shell, then plays a closing animation. */
    case 0xb6150:
    case 0xb6176:
    case 0xb643d: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        uint16_t barrel = read_word(ram, actor + 0x46), side = read_word(ram, actor + 0x60);
        uint16_t offset = 2 * barrel + 2 * side;
        if (address == 0xb6150) {
            world_acquire_resource(ram, actor, 0x66);
            world_acquire_resource(ram, actor, 0xe7);
            world_acquire_resource(ram, actor, 0xe4);
            ram[actor + 0x2d] = 3;
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x6176);
        } else
            keep_si();
        if (address != 0xb643d) {
            write_word(ram, actor + 0x21, read_word(bank, (uint16_t)(0x51b6 + offset)) + read_word(ram, parent + 0x21));
            write_word(ram, actor + 0x24, read_word(bank, (uint16_t)(0x51b8 + offset)) + read_word(ram, parent + 0x24));
            if (decrement(ram, actor + 0x44)) break;
            uint16_t row = read_word(bank, (uint16_t)(0x5192 + barrel + side)) + read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x50a8 + row));
            if (sprite != 0xffff) {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, 3);
                write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
                queue_sprite(ram, main_rom, actor, 0);
                sprite_si();
                break;
            }
            if (ram[parent + 0x0a]) {
                finish_animation(ram, actor);
                break;
            }
            uint16_t shell = world_spawn_child(ram, actor, 0x6272);
            if (shell) {
                write_word(ram, shell + 2, 0xb000);
                write_word(ram, shell + 0x14, read_word(ram, actor + 0x15));
                ram[shell + 0x2c] = ram[actor + 0x16];
                write_word(ram, shell + 0x2e, read_word(ram, actor + 0x2e));
                write_word(ram, shell + 0x40, barrel);
                write_word(ram, shell + 0x46, read_word(bank, (uint16_t)(0x537e + barrel + (side ? 0x12 : 0))));
                ram[shell + 0x29] = bank[(uint16_t)(0x56e9 + (barrel >> 1))];
                write_word(ram, shell + 0x60, side << 1);
                write_word(ram, shell + 0x21, read_word(ram, parent + 0x21));
                write_word(ram, shell + 0x24, read_word(ram, parent + 0x24));
                world_queue_sound(ram, 0x57);
                leave_si(shell);            /* B000:6245 mov ix,iy */
            }
            ram[actor + 0x2c] = ram[actor + 0x14];
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x643d);
        }
        write_word(ram, actor + 0x21, read_word(bank, (uint16_t)(0x52fc + offset)) + read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(bank, (uint16_t)(0x52fe + offset)) + read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t row = read_word(bank, (uint16_t)(0x52d8 + barrel + side)) + frame;
            uint16_t sprite = read_word(bank, (uint16_t)(0x51fe + row));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x52c4 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        sprite_si();
        break;
    }
    /* B000:6272 (init), 62CC (delay), 62DC (flight) and 63B3 (surface
       splash): the shell released by the gun flash. */
    case 0xb6272: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t barrel = read_word(ram, actor + 0x40);
        uint16_t offset = 2 * barrel + read_word(ram, actor + 0x60);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x3c, read_word(bank, (uint16_t)(0x53a2 + barrel)));
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(bank, (uint16_t)(0x53b8 + offset)));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, (uint16_t)(0x53ba + offset)));
        ram[actor + 0x28] = bank[(uint16_t)(0x56fa + (barrel >> 1))];
        for (unsigned axis = 0; axis < 2; ++axis) {
            int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
            write_word(ram, actor + 0x38 + 2 * axis, (unit * (int16_t)read_word(ram, actor + 0x2e)) >> 8);
        }
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x42, 8);
        write_word(ram, actor, 0x62cc);
        keep_si();
        break;
    }
    case 0xb62cc:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x62dc);
        keep_si();
        break;
    case 0xb62dc: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        keep_si();
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        int16_t top = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
        if (x > 0x1c0 || (int16_t)read_word(ram, actor + 0x24) < top || x < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40) & 2;
            uint16_t table = read_word(ram, actor + 0x46);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x5356 + frame + table)));
            write_word(ram, actor + 0x44, read_word(bank, 0x53b4 + frame));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
            leave_si(table);                /* B000:6334 mov ix,[bp+46h] */
        }
        queue_sprite(ram, main_rom, actor, 0);
        sprite_si();
        ram[actor + 0x0d] = 0;
        player_contact_si(ram, actor);
        if (world_player_contact(ram, main_rom, actor)) {
            spawn_kill_blast(ram, actor);
            finish_animation(ram, actor);
            break;
        }
        int16_t y = read_word(ram, actor + 0x24);
        if (y <= (int16_t)(0xfff8 + read_word(ram, 0xb732)) ||
            (world_terrain_mask(ram, tiles + 0x4000, x, y + 4) & 0xff02)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            ram[actor + 0x2c] = ram[actor + 0x14];
            ram[actor + 0x2d] = 0x28;
            write_word(ram, actor + 0x3c, 0xf48b);
            write_word(ram, actor, 0x63b3);
        }
        break;
    }
    case 0xb63b3: {
        const uint8_t *bank = main_rom + 0xb0000;
        keep_si();
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 1) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x4d28 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x4d4c + frame)));
            write_word(ram, actor + 0x3c, read_word(bank, (uint16_t)(0x4d6e + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        sprite_si();
        uint16_t shape = read_word(ram, actor + 0x3c);
        if (shape && !(read_word(ram, 0x274) & 3)) {
            /* B000:7269 pushes an overlapping submarine down. */
            for (unsigned i = 0; i < 2; ++i) {
                if (!(read_word(ram, 0x264) & (1u << i))) continue;
                uint16_t player = read_word(ram, 0x90 + 2 * i);
                leave_si(shape);
                if (!world_shape_overlap(ram, main_rom, actor, player, shape, read_word(ram, player + 0x3c)))
                    continue;
                write_word(ram, player + 0x24, read_word(ram, player + 0x24) + 8);
                write_word(ram, player + 0x5a, read_word(ram, player + 0x5a) + 0x10);
                write_word(ram, actor + 0x5e, read_word(ram, actor + 0x5e) + 0x20);
            }
        }
        break;
    }
    case 0xb64b6: {
        if (!warship_attachment(ram, actor)) break;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (address == 0xb6781 && !read_word(ram, parent + 0x6c) && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40), phase = frame & 2;
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x5410 + phase));
            write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0x5414 + phase));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            if (address == 0xb6781) write_word(ram, parent + 0x6c, 1);
            else {
                world_queue_sound(ram, 0x5d);
                world_spawn_radial_explosion(ram, main_rom, actor);
            }
            write_word(ram, actor, address == 0xb64b6 ? 0x64f3 : 0x67f3);
        }
        break;
    }
    /* B000:64F3 (init) and 653E: the gun exposed when a warship cover is
       destroyed. It tracks a submarine, fires 665E shells, and when
       destroyed becomes the 6928 wreck. */
    case 0xb64f3:
    case 0xb653e: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb64f3) {
            uint16_t cfg = 0x55d2 + read_word(bank, 0x5512 + (read_word(ram, actor + 0x1e) & 32) + read_word(ram, 0x8c));
            ram[actor + 0x0c] = 4;
            write_word(ram, actor + 0x26, 0x300);
            write_word(ram, actor + 0x36, 0x300);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x42, 0x40);
            ram[actor + 0x28] = 0xc0;
            write_word(ram, actor + 0x50, read_word(bank, cfg + 4));
            write_word(ram, actor + 0x52, read_word(bank, cfg + 4));
            write_word(ram, actor + 0x2e, read_word(bank, cfg + 6));
            write_word(ram, actor, 0x653e);
        }
        if (!warship_attachment(ram, actor)) break;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        aim_warship_turret(ram, main_rom, actor, ram[actor + 0x29] & 1);
        write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x5058 +
                   read_word(bank, (uint16_t)(0x507c + read_word(ram, actor + 0x60))) + read_word(ram, actor + 0x40))));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = read_word(ram, actor + 0x21);
        if (x >= 0x40 && x <= 0x1c0 && !decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            write_word(ram, actor + 0x42, 0x20);
            uint16_t barrel = read_word(bank, 0x4fea + ((ram[actor + 0x28] & 0xf0) >> 3));
            write_word(ram, actor + 0x46, barrel);
            write_word(ram, actor + 0x38, read_word(bank, (uint16_t)(0x5080 + 2 * barrel)));
            write_word(ram, actor + 0x3a, read_word(bank, (uint16_t)(0x5082 + 2 * barrel)));
            uint16_t shell = world_spawn_child(ram, actor, 0x665e);
            if (shell) {
                write_word(ram, shell + 2, 0xb000);
                write_word(ram, shell + 0x21, x + read_word(ram, actor + 0x38));
                write_word(ram, shell + 0x24, read_word(ram, actor + 0x24) + read_word(ram, actor + 0x3a));
                ram[shell + 0x2c] = ram[actor + 0x2c];
                write_word(ram, shell + 0x2e, read_word(ram, actor + 0x2e));
                write_word(ram, shell + 0x46, barrel);
                uint16_t flash = world_spawn_child(ram, actor, 0xa2c0);
                if (flash) {
                    write_word(ram, flash + 0x31, read_word(ram, actor + 0x38));
                    write_word(ram, flash + 0x34, read_word(ram, actor + 0x3a));
                    ram[flash + 0x29] = ram[actor + 0x29];
                    ram[flash + 0x2d] = ram[actor + 0x2d];
                    write_word(ram, flash + 0x60, 4);
                }
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
            if (read_word(ram, actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        spawn_kill_blast(ram, actor);
        write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(0x50a4 + read_word(ram, actor + 0x60))));
        write_word(ram, actor, 0x6928);
        break;
    }
    /* B000:6928: a destroyed gun. Further hits drain the hull pool at
       parent + 48; emptying it kills the warship and records the attacker. */
    case 0xb6928: {
        if (!warship_attachment(ram, actor)) break;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        write_word(ram, actor + 0x26, 0x7f00);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        int16_t pool = (int16_t)(read_word(ram, parent + 0x48) - (0x7f00 - read_word(ram, actor + 0x26)));
        write_word(ram, parent + 0x48, (uint16_t)pool);
        if (pool <= 0) {
            if (!hit) {
                world_dispatch_detail = "hull pool empty without a hit; B000:696A stores leftover BX";
                return 0;
            }
            write_word(ram, parent + 0x26, 0);
            write_word(ram, parent + 0x74, hit);
        }
        break;
    }
    /* B000:665E (init) and 66BB: the shell fired by the exposed gun. */
    case 0xb665e:
    case 0xb66bb: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb665e) {
            uint16_t barrel = read_word(ram, actor + 0x46);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3c, 0xf47b);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 0x800);
            write_word(ram, actor + 0x54, 4);
            write_word(ram, actor + 0x56, 6);
            write_word(ram, actor + 0x6e, 1);
            ram[actor + 0x14] = 3;
            write_word(ram, actor + 0x46, read_word(bank, (uint16_t)(0x5344 + barrel)));
            ram[actor + 0x28] = bank[(uint16_t)(0x5703 + (barrel >> 1))];
            for (unsigned axis = 0; axis < 2; ++axis) {
                int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
                write_word(ram, actor + 0x38 + 2 * axis, (unit * (int16_t)read_word(ram, actor + 0x2e)) >> 8);
            }
            write_word(ram, actor + 0x2a, read_word(bank, 0x4f2c + ((((uint8_t)(ram[actor + 0x28] + 8)) & 0xf0) >> 3)));
            write_word(ram, actor, 0x66bb);
        } else
            scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t top = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
        if (x > 0x1c0 || (int16_t)y < top || x < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x54)) {
            /* B000:721D smoke trail. */
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
            uint16_t smoke = effect_slot(ram, read_word(ram, 0x9a), 0xbe45);
            write_word(ram, smoke + 0x21, x);
            write_word(ram, smoke + 0x24, y);
            ram[smoke + 0x2c] = ram[actor + 0x14];
            ram[smoke + 0x2d] = 0x28;
            write_word(ram, smoke + 0x1e, read_word(ram, actor + 0x6e));
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor) ||
            (world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02)) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb5ce6: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned resource = 0x64;
        acquire_private_palette(ram, actor, resource);
        ram[actor + 0x2c] = ram[actor + 0x15];
        write_word(ram, actor + 0xe, 0x2a);
        ram[actor + 0xc] = 4;
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x31, 0);
        write_word(ram, actor + 0x34, -32);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x42, 32);
        ram[actor + 0x28] = 0x40;
        uint16_t cfg = 0x55d2 + read_word(bank, 0x5512 + (read_word(ram, actor + 0x1e) & 32) + read_word(ram, 0x8c));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 8) >> 1);
        write_word(ram, actor + 0x54, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x56, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x2e, read_word(bank, cfg + 12));
        write_word(ram, actor + 0x4c, 40);
        write_word(ram, actor, 0x5d59);
    }
        /* fall through */
    case 0xb5d59: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        uint16_t x = read_word(ram, actor + 0x21);
        aim_warship_turret(ram, main_rom, actor, ram[actor + 0x29] & 1);
        write_word(ram, actor + 0x2a, read_word(bank, 0x4fd8 + read_word(ram, actor + 0x40)));
        queue_sprite(ram, main_rom, actor, 0);
        if (x >= 0x40 && x <= 0x1c0 && !read_word(ram, 0x286)) {
            int fire = 0;
            if (read_word(ram, actor + 0x50)) fire = !decrement(ram, actor + 0x50);
            else if (!decrement(ram, actor + 0x4c)) {
                fire = 1;
                write_word(ram, actor + 0x4c, 40);
                if (!decrement(ram, actor + 0x54)) {
                    write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                }
            }
            if (fire) {
                write_word(ram, actor + 0x42, 96);
                uint16_t barrel = read_word(bank, 0x4fea + ((ram[actor + 0x28] & 0xf0) >> 3));
                write_word(ram, actor + 0x46, barrel);
                uint16_t shot = world_spawn_child(ram, actor, 0x6150);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x46, barrel);
                    ram[shot + 0x29] = bank[0x56e0 + (barrel >> 1)];
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                }
            }
        }
        uint16_t hit = 0;
        for (unsigned i = 0; i < 2; ++i) {
            write_word(ram, actor + 0x3c, read_word(bank, i ? 0x5e3e : 0x5e16));
            ram[actor + 0xd] = 0;
            hit = world_player_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26)) break;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (!i && hit) {
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
            }
            if (!read_word(ram, actor + 0x26)) break;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x26, 0x200);
            write_word(ram, actor + 0x3c, read_word(bank, 0x5e7c));
            ram[actor + 0xa] |= 2;
            write_word(ram, actor, 0x5e8b);
        }
        break;
    }
    case 0xb5e8b: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        if (!decrement(ram, actor + 0x44)) {
            uint16_t tick = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x44, read_word(bank, 0x502e + (tick & 14)));
            write_word(ram, actor + 0x2a, read_word(bank, 0x502a + (tick & 2)));
            ram[actor + 0x2c] = ram[actor + 0x14 + ((tick >> 1) & 1)];
            write_word(ram, actor + 0x40, tick + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) {
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
            }
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, actor + 0x26, read_word(ram, actor + 0x48));
            write_word(ram, actor + 0x2a, read_word(bank, 0x5ef2));
            write_word(ram, actor + 0x42, 32);
            warship_blast_sequence(ram, actor, 0x5490, 4);
            write_word(ram, actor, 0x5f0d);
        }
        break;
    }
    case 0xb5f0d:
        warship_attachment(ram, actor);
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            for (unsigned i = 0; i < 2; ++i) {
                uint16_t child = effect_slot(ram, actor, 0x5f64);
                if (child != 0x8480) write_word(ram, child + 2, 0xb000);
                ram[child + 0x2c] = ram[actor + 0x2c];
                write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
                ram[child + 0x28] = i ? 0x20 : 0x60;
                write_word(ram, child + 0x60, i*4);
            }
            ram[actor + 0xa] = 0;
            write_word(ram, actor, 0x6084);
        }
        break;
    case 0xb5f64: {
        const uint8_t *bank = main_rom + 0xb0000;
        write_word(ram, actor + 0xe, 0x2a);
        ram[actor + 0xc] = 4;
        ram[actor + 0x2d] = 0x29;
        write_word(ram, actor + 0x3c, read_word(bank, 0x5f74));
        write_word(ram, actor + 0x26, 0x600);
        write_word(ram, actor + 0x36, 0x300);
        write_word(ram, actor + 0x42, 1);
        unsigned side = read_word(ram, actor + 0x60);
        write_word(ram, actor + 0x31, read_word(bank, 0x5050 + side));
        write_word(ram, actor + 0x34, read_word(bank, 0x5052 + side));
        uint16_t config = 0x55d2 + read_word(bank, 0x5512 + (ram[actor + 0x1e] & 32) + read_word(ram, 0x8c));
        uint16_t delay = read_word(bank, config + 4);
        write_word(ram, actor + 0x52, delay);
        write_word(ram, actor + 0x50, delay == 0xffff ? delay : 200);
        write_word(ram, actor + 0x2e, read_word(bank, config + 6));
        write_word(ram, actor, 0x5fca);
    }
        /* fall through */
    case 0xb5fca: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) {
            warship_part_explosion(ram, main_rom, actor);
            break;
        }
        aim_warship_turret(ram, main_rom, actor, 0);
        uint16_t barrel = read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(bank, 0x503e + barrel));
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        uint16_t x = read_word(ram, actor + 0x21);
        if (x >= 0x40 && x <= 0x1c0 && !decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            write_word(ram, actor + 0x42, 96);
            uint16_t child = world_spawn_child(ram, actor, 0x6150);
            if (child) {
                write_word(ram, child + 2, 0xb000);
                write_word(ram, child + 0x46, barrel);
                ram[child + 0x29] = bank[0x56e0 + (barrel >> 1)];
                write_word(ram, child + 0x2e, read_word(ram, actor + 0x2e));
                write_word(ram, child + 0x60, 0x12);
            }
        }
        break;
    }
    case 0xb6084:
        ram[actor + 0x2c] = ram[actor + 0x16];
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0x6099);
        /* fall through */
    case 0xb6099: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) {
            unsigned slot = ram[actor + 0x14];
            uint16_t cursor = read_word(ram, 0xa488);
            ram[0xa406 + cursor] = slot;
            write_word(ram, 0xa488, (cursor + 1) & 63);
            write_word(ram, 0xa48a, read_word(ram, 0xa48a) + 1);
            write_word(ram, 0x9e06 + 8*slot, 0);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t step = read_word(ram, actor + 0x40);
            queue_palette(ram, ram[actor + 0x2c], read_word(bank, 0x56dc + (step & 2)), 6);
            write_word(ram, actor + 0x42, 80);
            write_word(ram, actor + 0x40, step + 2);
        }
        ram[actor + 0xd] = 1;
        world_player_contact(ram, main_rom, actor);
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x3c, read_word(bank, 0x60ed));
        ram[actor + 0xd] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        uint16_t hp = read_word(ram, actor + 0x26);
        if (!hp) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, parent + 0x26, 0);
            write_word(ram, parent + 0x74, hit);
        }
        write_word(ram, actor + 0x3c, read_word(bank, 0x6135));
        write_word(ram, actor + 0x48, hp);
        if (world_projectile_contact(ram, main_rom, actor)) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        write_word(ram, actor + 0x26, hp);
        break;
    }
    case 0xb59ea:
        world_acquire_resource(ram, actor, 0x5b);
        world_acquire_resource(ram, actor, 0x62);
        write_word(ram, actor + 0xe, 0x2a);
        write_word(ram, actor, 0x5a04);
        /* fall through */
    case 0xb5a04: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        uint16_t slot = read_word(ram, actor + 0x60);
        ram[actor + 0x29] = ram[actor + 0x5e];
        write_word(ram, actor + 0x2a, read_word(bank, 0x4db6 + slot));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        uint8_t angle = ram[actor + 0x28];
        if (!(ram[0x274] & 7) && (ram[0x264] & 3)) {
            uint16_t target = world_enemy_target(ram, actor);
            uint8_t desired = (vector_angle(main_rom, read_word(ram, target + 0x21) - x,
                                             read_word(ram, target + 0x24) - y) + 4) & 0xf8;
            if (desired != angle) {
                if ((uint8_t)(desired - angle) < (uint8_t)(angle - desired)) {
                    if ((int8_t)angle < -32) angle += 8;
                } else if ((int8_t)angle > -96) angle -= 8;
            }
            ram[actor + 0x28] = angle;
        }
        uint16_t dir = ((uint16_t)((angle & 0xf8) - 0xa0)) >> 3;
        ram[actor + 0x29] = bank[0x56e0 + dir];
        uint16_t barrel = read_word(bank, 0x4dd0 + 2 * dir);
        write_word(ram, actor + 0x62, barrel);
        write_word(ram, actor + 0x2a, read_word(bank, 0x4dc6 + barrel));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (x >= 0x40 && x <= 0x1c0 && !read_word(ram, parent + 0x50) && read_word(ram, parent + 0x64) == slot) {
            uint16_t flash = world_spawn_child(ram, actor, 0xa2c0);
            if (flash) {
                write_word(ram, flash + 0x31, read_word(ram, actor + 0x38));
                write_word(ram, flash + 0x34, read_word(ram, actor + 0x3a));
                ram[flash + 0x29] = ram[actor + 0x29];
                ram[flash + 0x2d] = 4;
                write_word(ram, flash + 0x60, 3);
                uint16_t shot = world_spawn_child(ram, actor, 0x6a4d);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    ram[shot + 0x2c] = ram[actor + 0x2c];
                    ram[shot + 0x14] = ram[actor + 0x14];
                    ram[shot + 0x29] = ram[actor + 0x29];
                    write_word(ram, shot + 0x62, barrel);
                    uint16_t dx = read_word(bank, 0x4df2 + 2*barrel), dy = read_word(bank, 0x4df4 + 2*barrel);
                    if (ram[actor + 0x29] & 1) dx = -dx;
                    write_word(ram, actor + 0x38, dx);
                    write_word(ram, actor + 0x3a, dy);
                    write_word(ram, shot + 0x21, x + dx);
                    write_word(ram, shot + 0x24, y + dy);
                    write_word(ram, shot + 0x58, read_word(ram, actor + 0x58));
                    world_queue_sound(ram, 0x5c);
                }
            }
        }
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            write_word(ram, actor + 0x2a, read_word(bank, 0x4db8 + slot));
            ram[actor + 0x29] = ram[actor + 0x5e];
            write_word(ram, actor + 0x3c, 0xffff);
            ram[actor + 0xa] = 1;
            write_word(ram, actor, 0x5cc6);
        }
        register_surface_enemy(ram, actor);
        break;
    }
    case 0xb571c: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t variant = read_word(ram, actor + 0x1e);
        uint16_t script = read_word(bank, 0x5662 + (variant & 6));
        if (read_word(bank, script) > 2) return 0;
        world_acquire_resource(ram, actor, 0x63);
        world_acquire_resource(ram, actor, 0x62);
        write_word(ram, actor + 0xe, 12);
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, read_word(bank, 0x5738));
        write_word(ram, actor + 0x2a, read_word(bank, 0x573d));
        uint16_t base = 0x5512 + (variant & 0x20);
        uint16_t cfg = 0x5552 + read_word(bank, base + read_word(ram, 0x8a));
        write_word(ram, actor + 0x26, read_word(bank, cfg));
        write_word(ram, actor + 0x36, read_word(bank, cfg));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x70, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x4c, 0);
        write_word(ram, actor + 0x58, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x54, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x56, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x4e, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x72, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x5a, read_word(bank, cfg + 12));
        write_word(ram, actor + 0x4a, read_word(bank, 0x5560 + read_word(bank, base + read_word(ram, 0x88))));
        write_word(ram, actor + 0x48, read_word(bank, 0x55e0 + read_word(bank, base + read_word(ram, 0x8c))));
        ram[actor + 0x29] = variant & 1;
        write_word(ram, actor + 0x38, read_word(bank, 0x5652 + 2*(variant & 1)));
        write_word(ram, actor + 0x6c, read_word(bank, 0x56d4 + (variant & 6)));
        begin_linear_script(ram, main_rom, actor, script);
        if (!spawn_warship_parts(ram, main_rom, actor)) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor, 0x5824);
        break;
    }
    case 0xb5824: {
        const uint8_t *bank = main_rom + 0xb0000;
        int moving = !read_word(ram, actor + 0x6c);
        uint16_t kind = read_word(ram, actor + 0x7a);
        uint16_t next = read_word(ram, actor + 0x7e);
        uint16_t next_kind = read_word(bank, next);
        if (moving && (kind > 2 || (read_word(ram, actor + 0x7c) == 1 && next_kind > 2 && next_kind != 0xffff))) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (moving) {
            if (kind == 2) {
                for (unsigned i = 0; i < 2; ++i) {
                    uint16_t speed = read_word(ram, actor + 0x38 + 2*i) + read_word(ram, actor + 0x78 - 2*i);
                    write_word(ram, actor + 0x38 + 2*i, speed);
                    move_actor_axis(ram, actor + 0x20 + 3*i, speed);
                }
            }
            if (!decrement(ram, actor + 0x7c)) {
                if (next_kind == 0xffff) {
                    next = 0x56ca;
                    write_word(ram, actor + 0x42, 0xffff);
                }
                begin_linear_script(ram, main_rom, actor, next);
            }
        }
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        if ((int16_t)read_word(ram, actor + 0x21) <= -48) { finish_animation(ram, actor); break; }
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + (ram[actor + 0x19] & 2));
        queue_sprite(ram, main_rom, actor, 0);
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, read_word(ram, actor + 0x74));
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 20);
            world_queue_sound(ram, 0x5f);
            warship_blast_sequence(ram, actor, 0x54b6, 8);
            write_word(ram, actor, 0x595d);
            break;
        }
        for (unsigned group = 0; group < 2; ++group) {
            uint16_t timer = group ? 0x50 : 0x54;
            uint16_t active = group ? 0x4c : 0x4e;
            unsigned mask = group ? 12 : 24;
            unsigned chosen = group ? 0x64 : 0x66;
            if (!read_word(ram, actor + timer)) write_word(ram, actor + timer, read_word(ram, actor + timer + 2));
            if (!read_word(ram, actor + active)) continue;
            if (!decrement(ram, actor + active))
                write_word(ram, actor + (group ? 0x4e : 0x4c), read_word(ram, actor + (group ? 0x72 : 0x70)));
            if (!decrement(ram, actor + timer)) {
                unsigned pick = world_random_byte(ram) & mask;
                if (pick == read_word(ram, actor + chosen)) pick = (pick + 8) & mask;
                write_word(ram, actor + chosen, pick);
            }
        }
        write_word(ram, actor + 0x26, 0x7f00);
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x42) && read_word(ram, actor + 0x21) <
            read_word(bank, 0x565a + (read_word(ram, actor + 0x1e) & 6))) {
            write_word(ram, actor + 0x6c, 1);
            write_word(ram, actor + 0x42, 0);
        }
        break;
    }
    case 0xb595d:
        ram[actor + 0xa] = 2;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x3a, -32);
        write_word(ram, actor, 0x5975);
        /* fall through */
    case 0xb5975:
    case 0xb59b3:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (address == 0xb59b3) {
            if ((int16_t)read_word(ram, actor + 0x21) <= -32 || !decrement(ram, actor + 0x42))
                finish_animation(ram, actor);
            else if (ram[actor + 0x42] & 1) queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            const uint8_t *bank = main_rom + 0xb0000;
            uint16_t sprite = read_word(bank, 0x4d90 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x42, 64);
                write_word(ram, actor, 0x59b3);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, 0x4da2 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xbf986: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 8);
        write_word(ram, actor + 0x3c, read_word(bank, 0xf991));
        write_word(ram, actor + 0x36, 128);
        ram[actor + 0x2d] = 0x28;
        ram[actor + 0xe] = 0x27;
        uint16_t variant = read_word(ram, actor + 0x1e);
        uint16_t cfg = 0xf876 + read_word(bank, 0xf836 + (variant & 32) + read_word(ram, 0x8a));
        write_word(ram, actor + 0x26, read_word(bank, cfg));
        write_word(ram, actor + 0x46, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x48, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x4e, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x4a, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x4c, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x5c, 160);
        uint16_t speed = read_word(bank, 0xf8c6 + (variant & 2));
        write_word(ram, actor + 0x38, speed);
        ram[actor + 0x5e] = speed >> 15;
        write_word(ram, actor + 0x56, 0x1e0);
        write_word(ram, actor + 0x44, read_word(bank, 0xf8ca));
        write_word(ram, actor + 0x2a, read_word(bank, 0xf8cc));
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x60, 44);
        write_word(ram, actor, 0xfa25);
        break;
    }
    case 0xbfa25: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t orientation = read_word(ram, actor + 0x29);
        ram[actor + 0x29] = ram[actor + 0x5e];
        queue_sprite(ram, main_rom, actor, 1);
        write_word(ram, actor + 0x29, orientation);
        advance_timed_sprite(ram, bank, actor, 0xf8ca);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (!read_word(ram, actor + 0x54)) {
            if (!decrement(ram, actor + 0x48)) {
                write_word(ram, actor + 0x48, read_word(ram, actor + 0x46));
                write_word(ram, actor + 0x54, 0xffff);
            }
        } else if (!decrement(ram, actor + 0x4c)) {
            write_word(ram, actor + 0x4c, read_word(ram, actor + 0x4a));
            if (y < 0x180) {
                uint16_t shot = world_spawn_child(ram, actor, 0xfbf9);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x21, x);
                    write_word(ram, shot + 0x24, y - 8);
                    write_word(ram, shot + 0x3a, read_word(ram, actor + 0x52));
                    world_queue_sound(ram, 0x7d);
                }
            }
            if (!decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x4e));
                write_word(ram, actor + 0x54, 0);
            }
        }
        if (!decrement(ram, actor + 0x5c)) {
            write_word(ram, actor + 0x5c, 160);
            ram[actor + 0x29] ^= 1;
            if (y < 0x180) {
                uint16_t child = effect_slot(ram, actor, 0x30c6);
                if (child != 0x8480) write_word(ram, child + 2, 0xb000);
                write_word(ram, child + 0x58, 4);
            }
        }
        int16_t vx = read_word(ram, actor + 0x38);
        if (vx >= 0 && (int16_t)decrement(ram, actor + 0x56) < 0) write_word(ram, actor + 0x3a, 128);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        uint16_t bottom = ram[0x26e] & 2 ? 0x198 : read_word(ram, 0xb732) + 0x1c8;
        if ((int16_t)read_word(ram, actor + 0x26) <= 0) {
            ram[actor + 0xb] = ram[hit + 0xb];
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, actor, 0xfac6);
        } else if (x < 0x28 || x > 0x1d8 || y > bottom) finish_animation(ram, actor);
        else {
            if (vx >= 0 && x >= 0x80) write_word(ram, actor + 0x38, 256);
            register_surface_enemy(ram, actor);
        }
        break;
    }
    case 0xbfbf9:
    case 0xbfd45: {
        const uint8_t *bank = main_rom + 0xb0000;
        int drop = address == 0xbfbf9;
        world_acquire_resource(ram, actor, 9);
        ram[actor + 0x2d] = drop ? 0x29 : 4;
        ram[actor + 0xe] = 0x27;
        uint16_t table = drop ? 0xf918 : 0xf960;
        write_word(ram, actor + 0x44, read_word(bank, table));
        write_word(ram, actor + 0x2a, read_word(bank, table + 2));
        write_word(ram, actor + 0x40, 0);
        if (drop) {
            write_word(ram, actor + 0x3c, read_word(bank, 0xfc04));
            write_word(ram, actor + 0x36, 0);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x42, 8);
        }
        write_word(ram, actor, drop ? 0xfc37 : 0xfd6f);
        break;
    }
    case 0xbfc37:
    case 0xbfcda:
    case 0xbfcf4:
    case 0xbfd6f: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!tiles && address != 0xbfd6f) return 0;
        if (address == 0xbfcda) {
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            if (tiles[0x4000 + cell + 3] >> 3 == 4) {
                write_word(ram, actor, 0xfcf4);
                spawn_bomb_splash(ram, main_rom, actor);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (address == 0xbfd6f) {
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            if (!advance_timed_sprite(ram, bank, actor, 0xf960) || (ram[parent + 0xa] & 1)) finish_animation(ram, actor);
            break;
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (address != 0xbfc37) {
            if ((int16_t)decrement(ram, actor + 0x42) < 0) {
                queue_sprite(ram, main_rom, actor, 0);
                advance_timed_sprite(ram, bank, actor, 0xf952);
            }
            if (x < 0x40) { finish_animation(ram, actor); break; }
            ram[actor + 0xd] = 0;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        advance_timed_sprite(ram, bank, actor, 0xf918);
        ram[actor + 0xd] = 3;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (hit || material == 4) {
            write_word(ram, actor, hit ? 0xfcda : 0xfcf4);
            if (!hit) spawn_bomb_splash(ram, main_rom, actor);
            write_word(ram, actor + 0x44, read_word(bank, 0xf952));
            write_word(ram, actor + 0x2a, read_word(bank, 0xf954));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x3a, -384);
            uint16_t child = effect_slot(ram, actor, 0xfd45);
            if (child != 0x8480) write_word(ram, child + 2, 0xb000);
            write_word(ram, child + 0x21, x);
            write_word(ram, child + 0x24, y);
            write_word(ram, child + 0x3a, -384);
            write_word(ram, actor + 0x42, 4);
            write_word(ram, actor + 0x36, 128);
        } else if (material == 8) finish_animation(ram, actor);
        else {
            if (!decrement(ram, actor + 0x42)) ram[actor + 0x2d] = 4;
            ram[actor + 0xd] = 0;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        }
        break;
    }
    case 0xb73c0: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 0xa4);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0xe, 13);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0x73de));
        write_word(ram, actor + 0x2a, read_word(bank, 0x73e3));
        unsigned facing = (int16_t)read_word(ram, actor + 0x21) <= 0x100;
        write_word(ram, actor + 0x38, facing ? 128 : -128);
        ram[actor + 0x29] = facing;
        uint16_t kind = (read_word(ram, actor + 0x1e) >> 8) & 14;
        write_word(ram, actor + 0x50, kind);
        uint16_t child = effect_slot(ram, actor, 0x74a0);
        if (child != 0x8480) write_word(ram, child + 2, 0xb000);
        ram[child + 0x29] = facing;
        child = effect_slot(ram, actor, 0x7511);
        if (child != 0x8480) write_word(ram, child + 2, 0xb000);
        ram[child + 0x29] = facing;
        ram[child + 0x2c] = ram[actor + 0x2c];
        write_word(ram, child + 0x50, kind);
        write_word(ram, actor, 0x743b);
        break;
    }
    case 0xb743b: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        else {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
            ram[actor + 0xa] |= 2;
        }
        break;
    }
    case 0xb74a0:
        world_acquire_resource(ram, actor, 4);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x74b3);
        break;
    case 0xb7511:
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 64);
        write_word(ram, actor + 0x36, 0);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x7522));
        write_word(ram, actor + 0x34, -12);
        write_word(ram, actor + 0x31, ram[actor + 0x29] & 1 ? -18 : 18);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x7542);
        /* fall through */
    case 0xb7542:
    case 0xb74b3: {
        int offset = address != 0xb74b3;
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (!offset && ram[parent + 0xa]) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + (offset ? read_word(ram, actor + 0x31) : 0));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + (offset ? read_word(ram, actor + 0x34) : 0));
        if (offset && (ram[parent + 0xa] & 2)) write_word(ram, actor, 0x75b6);
        else if (offset && (ram[parent + 0xa] & 1)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, (offset ? 0x7284 : 0x73b6) + frame));
            write_word(ram, actor + 0x44, 4);
            write_word(ram, actor + 0x40, frame + 2 < 6 ? frame + 2 : 0);
        }
        queue_sprite(ram, main_rom, actor, !offset);
        break;
    }
    /* B000:75B6 (init) and 75E9: a released bomb decelerates upward, bursts
       on solid ground, and becomes the 76A6 floater on reaching water. */
    case 0xb75b6:
    case 0xb75e9: {
        if (!tiles) return 0;
        if (address == 0xb75b6) {
            uint16_t x = read_word(ram, actor + 0x21);
            if (x >= 0x198) write_word(ram, actor + 0x21, 0x198);
            else if ((int16_t)x < 0x68) write_word(ram, actor + 0x21, 0x68);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x3a, 0);
            write_word(ram, actor + 0x42, 0x20);
            write_word(ram, actor, 0x75e9);
        }
        if (read_word(ram, actor + 0x42)) {
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 0x10);
            decrement(ram, actor + 0x42);
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t top = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
        if (x > 0x1c0 || (int16_t)y < top || x < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        unsigned material = terrain_material(ram, tiles + 0x4000, x, y);
        if (0x5502 & (1u << (material & 15))) {
            surface_enemy_death_blast(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (material == 4) {
            water_entry_splash(ram, main_rom, actor, x, y);
            write_word(ram, actor + 0x3a, 0xffc0);
            write_word(ram, actor, 0x76a6);
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x728a + frame));
            write_word(ram, actor + 0x44, 6);
            write_word(ram, actor + 0x40, frame + 2 < 0x0c ? frame + 2 : 0);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb76a6: {
        const uint8_t *bank = main_rom + 0xb0000;
        ram[actor + 0x2c] = 8;
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(bank, 0x76b1));
        ram[actor + 0x29] = 0;
        write_word(ram, actor + 0x3a, 0x40);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        uint16_t kind = read_word(ram, actor + 0x50);
        if (!kind) {
            kind = read_word(bank, 0x73bc + (world_random_byte(ram) & 2));
            write_word(ram, actor + 0x50, kind);
        }
        uint16_t sequence = read_word(bank, 0x7296 + kind);
        write_word(ram, actor + 0x52, sequence);
        write_word(ram, actor + 0x4c, read_word(bank, 0x72ce + read_word(bank, sequence)));
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x56, 4);
        write_word(ram, actor, 0x7702);
    }
        /* fall through */
    case 0xb7702:
    case 0xb7846:
    case 0xb786a: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t state = read_word(ram, actor);
        if (state == 0x7702 && !tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (state == 0x7702) {
            if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                               0x177 - y - 8 + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0xff02) write_word(ram, actor, 0x786a);
            else if (material == 3) {
                write_word(ram, actor + 0x34, y);
                write_word(ram, actor, 0x7846);
            }
        } else if (state == 0x7846) {
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
            uint16_t target = read_word(ram, actor + 0x34) + read_word(ram, 0xb71c);
            write_word(ram, actor + 0x34, target);
            uint16_t speed = read_word(ram, actor + 0x3a);
            move_actor_axis(ram, actor + 0x23, speed);
            int16_t error = 8 * (target - read_word(ram, actor + 0x24)) - speed;
            write_word(ram, actor + 0x3a, speed + (error >> 5));
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x4c) + frame));
            write_word(ram, actor + 0x44, read_word(bank, 0x736e + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame >= 24 ? 0 : frame);
            if (frame >= 24 && !decrement(ram, actor + 0x56)) {
                write_word(ram, actor + 0x56, 4);
                uint16_t step = read_word(ram, actor + 0x54) + 2;
                uint16_t sequence = read_word(ram, actor + 0x52);
                uint16_t kind = read_word(bank, sequence + step);
                if (kind == 0xffff) { step = 0; kind = read_word(bank, sequence); }
                write_word(ram, actor + 0x54, step);
                write_word(ram, actor + 0x50, kind);
                write_word(ram, actor + 0x4c, read_word(bank, 0x72ce + kind));
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0xd] = 1;
        uint16_t player = world_player_contact(ram, main_rom, actor);
        if (player) {
            uint16_t kind = read_word(ram, actor + 0x50);
            uint16_t weapon = read_word(bank, 0x7386 + kind * 2);
            if (weapon) {
                world_queue_sound(ram, read_word(bank, 0x73a6 + kind));
                uint16_t stats = read_word(ram, player + 0x70);
                uint16_t slot = stats + read_word(bank, 0x7388 + kind * 2);
                write_word(ram, slot + 0x10, weapon);
                uint16_t level = read_word(ram, slot + 0x12) + 1;
                if ((int16_t)level > 3) {
                    level = 3;
                    write_word(ram, stats + 0x1a, read_word(ram, stats + 0x1a) + 1);
                    write_word(ram, stats + 0x1c, read_word(ram, stats + 0x1c) + 1);
                }
                write_word(ram, slot + 0x12, level);
            }
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7ed34:
        break;
    case 0x7dc27:
    case 0x7dc68:
        /* 7DC68 loads SI only through a successful 1769 at 7DCD6. */
        if (address == 0x7dc68) keep_si();
        if (read_word(ram, actor + 0x48) < read_word(ram, 0xb700)) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 0x46)) {
            if (address == 0x7dc68)
                write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
            uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
            write_word(ram, actor + 0x21, x);
            if (x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (address == 0x7dc27) {
            if (!decrement(ram, actor + 0x42))
                write_word(ram, actor, 0xdc68);
        } else if (!read_word(ram, 0x286)) {
            if (read_word(ram, actor + 0x50)) {
                if (decrement(ram, actor + 0x50)) break;
            } else {
                if (decrement(ram, actor + 0x58)) break;
                write_word(ram, actor + 0x58, read_word(ram, actor + 0x5a));
            }
            if (!decrement(ram, actor + 0x54)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
            }
            uint16_t successor = read_word(ram, actor + 4);
            uint16_t child = effect_slot(ram, actor, read_word(ram, actor + 0x60));
            if (child != 0x8480) {
                write_word(ram, child + 2, read_word(ram, actor + 0x62));
                leave_si(successor);
            }
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x44));
        }
        break;
    case 0xb8a38:
        ram[actor + 0x2c] = 0x13;
        write_word(ram, actor + 0x42, 256);
        write_word(ram, actor, 0x8a46);
        /* fall through */
    case 0xb8a46: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!decrement(ram, actor + 0x42)) {
            unsigned frame = read_word(ram, actor + 0x40);
            queue_palette(ram, ram[actor + 0x2c] & 63, read_word(bank, 0x8108 + frame), 0);
            write_word(ram, actor + 0x42, read_word(bank, 0x811c + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame < 20 ? frame : 0);
        }
        if (ram[0x263] & 0x80) {
            queue_palette(ram, ram[actor + 0x2c] & 63, read_word(bank, 0x810a), 0);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7e7b6:
        write_word(ram, actor + 0x42, 32);
        write_word(ram, actor, 0xe7c0);
        /* fall through */
    case 0x7e7c0:
        if (read_word(ram, 0x262) & 0x8000)
            finish_animation(ram, actor);
        else if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            queue_palette(ram, 7, read_word(main_rom + 0x70000, 0xe7ae + (frame & 2)), 5);
            queue_palette(ram, 8, read_word(main_rom + 0x70000, 0xe7b2 + (frame & 2)), 5);
            write_word(ram, actor + 0x42, 33);
            write_word(ram, actor + 0x40, frame + 2);
        }
        break;
    case 0xc309:
        write_word(ram, actor + 0x50, effect_slot(ram, actor, 0xdecd));
        write_word(ram, 0x286, 0);
        /* fall through */
    case 0xc322:
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0xc32c);
        /* fall through */
    case 0xc32c:
        if (!(ram[0x264] & 12)) {
            write_word(ram, actor + 0x42, 0x501);
            write_word(ram, actor + 0x5a, 0x24);
            write_word(ram, 0x286, 1);
            write_word(ram, actor, 0xc35e);
        }
        break;
    case 0xc35e: {
        uint16_t display = read_word(ram, actor + 0x50);
        if (ram[0x264] & 12) {
            write_word(ram, display + 0x40, 0xffff);
            write_word(ram, 0x286, 0);
            write_word(ram, actor, 0xc322);
            break;
        }
        if (!decrement(ram, actor + 0x5a)) {
            write_word(ram, actor + 0x5a, 36);
            world_queue_sound(ram, 0x50);
        }
        unsigned waiting = ram[0x266] & 3;
        for (unsigned i = 0; i < 2; ++i) {
            if (!(waiting & (1u << i))) continue;
            uint16_t player = read_word(ram, 0x90 + 2*i);
            if (read_word(ram, player + 0x42)) write_word(ram, player + 0x42, read_word(ram, actor + 0x42) + 256);
        }
        uint16_t count = decrement(ram, actor + 0x42);
        if (!count) {
            ram[0x262] |= 2;
            write_word(ram, display + 0x40, 1);
            write_word(ram, actor + 0x42, 238);
            write_word(ram, actor, 0xc419);
        } else if (read_word(ram, actor + 0x58)) decrement(ram, actor + 0x58);
        else if (!(count & 127)) {
            write_word(ram, display + 0x40, 1);
            write_word(ram, actor + 0x58, 9);
        } else if (count < 0x380 && (((waiting & 1) && (ram[0xb743] & 0xc0)) ||
                                      ((waiting & 2) && (ram[0xb742] & 0xc0)))) {
            write_word(ram, actor + 0x42, (count & 0x780) + 1);
        }
        break;
    }
    case 0xc419:
        if (!decrement(ram, actor + 0x42)) {
            ram[0x263] |= 8;
            finish_animation(ram, actor);
        }
        break;
    case 0x19e2:
        for (unsigned entry = 0x19e2; entry <= 0x19f8; entry += 11) {
            uint16_t child = effect_slot(ram, actor, read_word(main_rom, entry + 4));
            if (child != 0x8480) write_word(ram, child + 2, read_word(main_rom, entry + 1));
        }
        write_word(ram, actor, 0x1a0d);
        write_word(ram, actor + 2, 0);
        break;
    case 0x19a6: {
        effect_slot(ram,actor,0xca44);
        uint16_t player=effect_slot(ram,actor,0xca9c);
        ram[player+0x0b]=2;write_word(ram,0x92,player);
        player=effect_slot(ram,actor,0xca96);
        ram[player+0x0b]=1;write_word(ram,0x90,player);
        write_word(ram,actor,0x19e1);
        break;
    }
    case 0x19e1:
    /* 0:61B4 (init) and 61D1: a fixed centre-screen plate shown for 192
       ticks. */
    case 0x61b4:
        ram[actor + 0x2c] = 0x85;
        write_word(ram, actor + 0x2a, 0xa4c1);
        write_word(ram, actor + 0x21, 0x100);
        write_word(ram, actor + 0x24, 0x104);
        write_word(ram, actor + 0x42, 0xc0);
        write_word(ram, actor, 0x61d1);
        /* fall through */
    case 0x61d1:
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0x61d1) si_after_sprite();
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    /* 0:C99F/C9B2 and C9D1/C9E4: a three-piece banner row; 0:CA03/CA3E: a
       single plate chosen by 272 and the active player. */
    case 0xc99f:
    case 0xc9d1:
        ram[actor + 0x2c] = address == 0xc99f ? 0x10 : 0x0e;
        write_word(ram, actor + 0x2a, 0x966f);
        write_word(ram, actor + 0x24, address == 0xc99f ? 0xd0 : 0x110);
        write_word(ram, actor, address == 0xc99f ? 0xc9b2 : 0xc9e4);
        /* fall through */
    case 0xc9b2:
    case 0xc9e4:
        for (unsigned x = 0x80; x <= 0x180; x += 0x80) {
            write_word(ram, actor + 0x21, x);
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    case 0xca03: {
        unsigned which = read_word(ram, 0x272) & 3;
        ram[actor + 0x2c] = main_rom[0xd1ad + which] + ((read_word(ram, 0x264) & 2) >> 1);
        write_word(ram, actor + 0x2a, read_word(main_rom, 0xd1b1 + 2 * which));
        write_word(ram, actor + 0x21, read_word(main_rom, 0xd1b9 + 2 * which));
        write_word(ram, actor + 0x24, read_word(main_rom, 0xd1c1 + 2 * which));
        write_word(ram, actor, 0xca3e);
    }
        /* fall through */
    case 0xca3e:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xca63:
        break;
    case 0xca44:
        do {
            uint16_t child=effect_slot(ram,actor,0xca64);
            write_word(ram,child+0x1e,read_word(ram,actor+0x42));
            write_word(ram,actor+0x42,read_word(ram,actor+0x42)+1);
        } while(read_word(ram,actor+0x42)<9);
        write_word(ram,actor,0xca63);
        break;
    case 0xca64: {
        ram[actor+0x2c]=0x84;ram[actor+0x2d]=0;
        write_word(ram,actor+0x3c,0xfb4b);
        unsigned offset=2*read_word(ram,actor+0x1e);
        write_word(ram,actor+0x2a,read_word(main_rom,0xce7d+offset));
        write_word(ram,actor+0x21,read_word(main_rom,0xce8f+offset));
        write_word(ram,actor+0x24,0x100);
        write_word(ram,actor,0xca90);
        /* fall through */
    }
    case 0xca90:
        queue_sprite(ram,main_rom,actor,0);
        break;
    case 0xca96:
    case 0xca9c: {
        unsigned side=address==0xca9c;
        if(side)write_word(ram,actor+0x1e,1);
        write_word(ram,actor+0x48,read_word(ram,0x124+64*side));
        write_word(ram,actor+0x42,read_word(ram,0x84)+1);
        while(decrement(ram,actor+0x42)) {
            unsigned medals=read_word(ram,actor+0x48);
            write_word(ram,actor+0x48,medals>>1);
            if(medals&1) {
                uint16_t child=effect_slot(ram,actor,0x2e60);
                write_word(ram,child+0x1e,read_word(ram,actor+0x40)+read_word(ram,actor+0x1e));
            }
            write_word(ram,actor+0x40,read_word(ram,actor+0x40)+2);
        }
        for(unsigned i=0;i<2;++i) {
            uint16_t child=effect_slot(ram,actor,0x2b18);
            write_word(ram,child+0x1e,read_word(ram,actor+0x1e)+2*i);
        }
        unsigned place=read_word(ram,0x11e + 64*side);
        if(place>=11) {
            decrement(ram,0xd2);
            finish_animation(ram,actor);
            break;
        }
        write_word(ram,actor+0x50,(place-1)*32);
        tally_score(ram,main_rom,tiles,side);
        write_word(ram,actor+0x42,1);
        write_word(ram,actor+0x40,0);
        write_word(ram,actor+0x46,0);
        write_word(ram,actor+0x54,640);
        write_word(ram,actor+0x52,read_word(main_rom,0xcea1+2*side));
        name_glyph(ram,main_rom,tiles,actor);
        write_word(ram,actor,0xcb44);
        /* The initial entry performs its first input step immediately. */
        /* fall through */
    }
    case 0xcb44:
        name_input(ram,main_rom,tiles,actor);
        break;
    case 0xcc89:
        if((read_word(ram,0xb742)&15) || !decrement(ram,actor+0x42)) {
            decrement(ram,0xd2);
            finish_animation(ram,actor);
        }
        break;
    case 0xccb7:
        finish_animation(ram,actor);
        break;
    /* 0:2612: start the stage tally: one 2681 column per player, ten 2B68
       actors, and two forty-cell rules on the first layer; then idle. */
    case 0x2612: {
        if (!tiles) return 0;
        for (unsigned side = 0; side < 2; ++side) {
            uint16_t column = effect_slot(ram, actor, 0x2681);
            write_word(ram, column + 0x1e, side);
        }
        do {
            uint16_t item = effect_slot(ram, actor, 0x2b68);
            write_word(ram, item + 0x1e, read_word(ram, actor + 0x42));
            write_word(ram, actor + 0x42, read_word(ram, actor + 0x42) + 1);
        } while (read_word(ram, actor + 0x42) < 0x0a);
        for (unsigned line = 0; line < 2; ++line) {
            uint16_t cell = line ? 0x2030 : 0x1f30;
            for (unsigned i = 0; i < 0x28; ++i) {
                write_word(tiles, cell, 1);
                write_word(tiles, (uint16_t)(cell + 2), 0x111);
                cell = (cell & 0xff00) | (uint8_t)(cell + 4);
            }
        }
        write_word(ram, actor, 0x2680);
        break;
    }
    case 0x2680:
        break;
    /* 0:1A24: the list head that starts the 0:2612 actor, then idles. */
    case 0x1a24:
        effect_slot(ram, actor, 0x2612);
        write_word(ram, actor, 0x1a39);
        write_word(ram, actor + 2, 0);
        break;
    case 0x1a39:
        break;
    case 0x1a3a:
        effect_slot(ram, actor, 0xcccb);
        write_word(ram, actor, 0x1a4f);
        write_word(ram, actor+2, 0);
        break;
    case 0x1a4f:
        break;
    case 0xcccb: {
        if (read_word(ram, 0xd2)) {
            for (unsigned p=0; p<2; ++p) {
                unsigned rank=read_word(ram, 0x11e + 64*p);
                if(rank<11) {
                    uint16_t child=effect_slot(ram, actor, 0xcd9c);
                    write_word(ram,child+0x1e,p); write_word(ram,child+0x4e,rank);
                }
            }
        }
        unsigned stage=read_word(ram,0x84);
        uint16_t child=effect_slot(ram,actor,read_word(main_rom,0xd181+4*stage));
        if(child!=0x8480) write_word(ram,child+2,read_word(main_rom,0xd183+4*stage));
        ram[actor+0x42]=main_rom[0xd0c6+stage];
        write_word(ram,actor,0xcd34);
        /* fall through */
    }
    case 0xcd34:
        if (!--ram[actor+0x42]) {
            unsigned row=read_word(main_rom,0xd060+2*read_word(ram,0x84));
            unsigned slot=0;
            do {write_word(ram,0x9e08+8*slot++,0);row+=2;} while(read_word(main_rom+0xa0000,row)!=0xffff);
            finish_animation(ram, actor);
        }
        break;
    case 0xc930:
        finish_animation(ram, actor);
        break;
    case 0x1a0e: {
        uint16_t child = effect_slot(ram, actor, read_word(main_rom, 0x1a12));
        if (child != 0x8480) write_word(ram, child + 2, read_word(main_rom, 0x1a0f));
        write_word(ram, actor, 0x1a23);
        write_word(ram, actor + 2, 0);
        break;
    }
    case 0x1a0d:
    case 0x1a23:
        break;
    case 0x18a9:
        init_stage_list(ram, main_rom, actor);
        /* fall through */
    case 0x1950:
        update_rank(ram, main_rom);
        break;
    case 0x3b26: {
        /* Only the 4621 script advance loads SI, with the new cursor. */
        int script = (int16_t)read_word(ram, actor + 0x46) <= (int16_t)read_word(ram, 0xb700);
        uint16_t row = read_word(ram, actor + 0x40);
        update_layer_scroll(ram, main_rom, actor);
        if (script) leave_si(row + 0x0e);
        else keep_si();
        break;
    }
    case 0x4177:
        update_background_vertical(ram, actor);
        /* fall through */
    case 0x417a:
        write_word(ram, 0xb71e, 0);
        write_word(ram, 0xb720, 0);
        keep_si();
        break;
    case 0x3fcd:
        update_background_vertical(ram, actor);
        /* fall through */
    case 0x3fd0: {
        if (!(read_word(ram, 0x26e) & 0x4000)) {
            write_word(ram, 0xb71e, 0);
            break;
        }
        int speed = (int16_t)read_word(ram, 0xa4) >> 1;
        uint16_t old_x = read_word(ram, actor + 0x52);
        unsigned frac = ram[actor + 0x51] + (speed & 255);
        int delta = (speed >> 8) + (int)(frac >> 8);
        ram[actor + 0x51] = frac;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        write_word(ram, 0xb70e, old_x + delta);
        write_word(ram, 0xb71e, -delta);
        if (read_word(ram, actor + 0x46)) advance_scroll_script(ram, main_rom, actor);
        break;
    }
    case 0x3b45:
    case 0x3b5d:
        if (address == 0x3b45) {
            write_word(ram, 0xb716, 0);
            write_word(ram, 0xb718, 0);
            write_word(ram, 0xb726, 0);
            write_word(ram, 0xb728, 0);
            write_word(ram, actor + 0x6c, 640);
            write_word(ram, actor, 0x3b5d);
        }
        if (!decrement(ram, actor + 0x6c)) {
            write_word(ram, actor + 0x6e, 0xff80);
            write_word(ram, actor + 0x6c, 25);
            write_word(ram, actor, 0x3b72);
        }
        break;
    case 0x3d93:
        write_word(ram, 0xb71a, 0);
        write_word(ram, 0xb72a, 0);
        write_word(ram, 0xb726, 0);
        write_word(ram, 0x26e, 0x4000);
        write_word(ram, actor + 0x6c, 1);
        write_word(ram, actor, 0x3db1);
        /* fall through */
    case 0x3db1:
        if (read_word(ram, 0xa4) >= 0x400) {
            write_word(ram, 0xa4, 0x400);
            write_word(ram, actor, 0x3dd6);
        } else if (!(decrement(ram, actor + 0x6c) & 15)) {
            write_word(ram, 0xa4, read_word(ram, 0xa4) + 64);
        }
        /* fall through */
    case 0x3dd6: {
        uint16_t old_x = read_word(ram, actor + 0x52);
        uint16_t velocity = read_word(ram, 0xa4);
        unsigned fraction = ram[actor + 0x51] + (velocity & 255);
        int delta = (int8_t)(velocity >> 8) + (int)(fraction >> 8);
        ram[actor + 0x51] = (uint8_t)fraction;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        write_word(ram, 0xb70a, old_x + delta);
        write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
        write_word(ram, 0xb72a, -delta);
        write_word(ram, 0xb726, -delta);
        break;
    }
    case 0x3d1a:
    case 0x3d32:
        if (address == 0x3d1a) {
            write_word(ram, actor + 0x6c, 640);
            write_word(ram, 0xb71a, 0);
            write_word(ram, 0xb72a, 0);
            write_word(ram, 0xa4, 0);
            write_word(ram, 0x26e, 0);
            write_word(ram, actor, 0x3d32);
        }
        if (!decrement(ram, actor + 0x6c)) {
            write_word(ram, 0x26e, read_word(ram, 0x26e) | 0x4000);
            write_word(ram, 0xa4, 128);
            write_word(ram, actor + 0x6c, 33);
            write_word(ram, actor, 0x3d4e);
        }
        break;
    case 0x3b72:
    case 0x3b91:
    case 0x3d4e:
    case 0x3d6e: {
        int layer_one = address == 0x3b72 || address == 0x3b91;
        uint16_t speed = layer_one ? actor + 0x6e : 0xa4;
        if (address == 0x3d4e || address == 0x3b72) {
            uint16_t ticks = decrement(ram, actor + 0x6c);
            if (!(ticks & (layer_one ? 7 : 15))) write_word(ram, speed, read_word(ram, speed) << 1);
            if (!ticks) {
                write_word(ram, 0x26e, read_word(ram, 0x26e) | 0x100);
                write_word(ram, actor, layer_one ? 0x3b91 : 0x3d6e);
            }
        }
        uint16_t old_x = read_word(ram, actor + 0x52);
        uint16_t velocity = read_word(ram, speed);
        unsigned fraction = ram[actor + 0x51] + (velocity & 255);
        int delta = (int8_t)(velocity >> 8) + (int)(fraction >> 8);
        ram[actor + 0x51] = (uint8_t)fraction;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        if (layer_one) {
            write_word(ram, 0xb706, old_x + delta);
            write_word(ram, 0xb726, -delta);
            write_word(ram, 0xb716, (-delta) >> 3);
        } else {
            write_word(ram, 0xb70a, old_x + delta);
            write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
            write_word(ram, 0xb72a, -delta);
        }
        break;
    }
    case 0x3ad7: case 0x3bfa: case 0x3f11:
        init_tile_layer(ram, main_rom, actor, address == 0x3ad7 ? 0 : address == 0x3bfa ? 1 : 2);
        break;
    case 0x3bb8: case 0x3ecf: case 0x40d9: {
        if (!tiles) return 0;
        uint16_t stage = 2 * read_word(ram, 0x84);
        uint16_t table = address == 0x3bb8 ? 0x479f : address == 0x3ecf ? 0x47cb : 0x480d;
        uint16_t row = read_word(main_rom, table + stage);
        write_word(ram, actor + 0x40, row);
        write_word(ram, actor + 0x46, read_word(main_rom, row));
        write_word(ram, actor, (uint16_t)address + 25);
        update_tile_script(ram, main_rom, tiles, actor,
                           address == 0x3bb8 ? 0 : address == 0x3ecf ? 0x4000 : 0x8000);
        break;
    }
    case 0x3bd1:
    case 0x3ee8:
    case 0x40f2:
        if (!tiles)
            return 0;
    {
        uint16_t row = read_word(ram, actor + 0x40);
        update_tile_script(ram, main_rom, tiles, actor,
                           address == 0x3bd1 ? 0 : address == 0x3ee8 ? 0x4000 : 0x8000);
        /* 4665 ends by advancing the script cursor through SI; the idle and
           finished paths never load it. */
        if (read_word(ram, actor + 0x40) != row) leave_si(row + 10);
        else keep_si();
        break;
    }
    case 0xa419:
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor + 0x46, 1);
        write_word(ram, actor + 0x48, 1);
        write_word(ram, actor + 0x50, 16);
        write_word(ram, actor, 0xa432);
        /* fall through */
    case 0xa432:
    case 0xa6b3:
        if (!update_idle_equipment(ram, main_rom, actor, address == 0xa6b3))
            return 0;
        break;
    case 0x803f:
        write_word(ram, actor + 0x40, read_word(ram, ram[actor + 0x0b] == 1 ? 0x90 : 0x92));
        write_word(ram, actor, 0x8053);
        /* fall through */
    case 0x8053:
        update_sub_display(ram, main_rom, actor);
        break;
    case 0x8154:
    case 0x815c:
        write_word(ram, actor + 0x4c, read_word(main_rom, (uint16_t)address + 3));
        world_acquire_resource(ram, actor, read_word(main_rom, 0x7965 + (ram[actor + 0x0b] & 2)));
        ram[actor + 0x2c] |= 0x80;
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x817d));
        write_word(ram, actor + 0x36, 4);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor, 0x818f);
        break;
    case 0x818f: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        uint16_t frame = read_word(ram, actor + 0x40);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t sprite = read_word(main_rom, read_word(ram, actor + 0x4c) + ((frame & 0xfffc) >> 1));
            if (sprite == 0xffff) {
                uint16_t player = read_word(ram, (ram[actor + 0x0b] & 1) ? 0x90 : 0x92);
                ram[player + 0x0a] = 0;
                write_word(ram, player + 0x26, 1);
                write_word(ram, player + 0x36, 0x100);
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x58, 4);
        }
        if (!(frame & 1)) queue_sprite(ram, main_rom, actor, 0);
        write_word(ram, actor + 0x40, frame + 1);
        world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0x8126: {
        int16_t limit = (int16_t)read_word(ram, actor + 0x1e);
        if (limit && (int16_t)read_word(ram, 0xb700) > limit)
            finish_animation(ram, actor);
        else {
            scroll_actor(ram, actor, 0xb71a);
            write_word(ram, 0x25c, read_word(ram, actor + 0x24));
        }
        break;
    }
    case 0x9dab:
        ram[actor + 0x2c] = 3;
        write_word(ram, actor + 0x5c, 1);
        write_word(ram, actor, 0x9db9);
        /* fall through */
    case 0x9db9: {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        cycle_torpedo_sprite(ram, main_rom, actor, 0x99a3, 5);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x9e73:
    case 0x9fa1:
    case 0xa06b: {
        uint16_t fields;
        uint16_t resource = read_word(main_rom, 0x9bb1 + (ram[actor + 0x0b] & 2));
        if (address == 0x9fa1)
            resource = read_word(main_rom, 0x9fa2);
        world_acquire_resource(ram, actor, resource);
        if (address == 0xa06b) {
            fields = 0xa07f;
            ram[actor + 0x2d] = main_rom[0xa093];
            write_word(ram, actor + 0x0e, read_word(main_rom, 0xa097));
            write_word(ram, actor + 0x42, read_word(main_rom, 0xa09c));
            write_word(ram, actor + 0x58, read_word(main_rom, 0xa0a1));
            write_word(ram, actor, 0xa0a9);
        } else {
            fields = address == 0x9e73 ? 0x9e87 : 0x9fac;
            write_word(ram, actor + 0x0e, read_word(main_rom, fields + 20));
            write_word(ram, actor + 0x42, read_word(main_rom, fields + 25));
            if (address == 0x9e73) {
                write_word(ram, actor + 0x58, read_word(main_rom, 0x9ea5));
                write_word(ram, actor + 0x5a, read_word(main_rom, 0x9eaa));
                write_word(ram, actor, 0x9eb2);
            } else {
                write_word(ram, actor, 0x9fcd);
            }
        }
        write_word(ram, actor + 0x3c, read_word(main_rom, fields));
        write_word(ram, actor + 0x3a, read_word(main_rom, fields + 5));
        write_word(ram, actor + 0x26, read_word(main_rom, fields + 10));
        write_word(ram, actor + 0x36, read_word(main_rom, fields + 15));
        break;
    }
    case 0xa804c:
    case 0xa80b5:
    case 0xa80ea:
        if (!tiles || !update_powered_torpedo(ram, main_rom, tiles + 0x4000, actor, address)) return 0;
        break;
    case 0xabb7:
    case 0xac0e:
    case 0xac65:
        if (!tiles || !update_blast_torpedo(ram, main_rom, tiles + 0x4000, actor, address)) return 0;
        break;
    case 0xad5f:
        world_acquire_resource(ram, actor, 0x58);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor + 0x0e, 0x121);
        write_word(ram, actor + 0x36, 0x10);
        write_word(ram, actor, 0xad81);
        break;
    case 0xad81:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0xa9c9 + frame + read_word(ram, actor + 0x76)));
            write_word(ram, actor + 0x58, read_word(main_rom, (uint16_t)(0xaa2f + frame)));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                keep_si();
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x3c,
                       read_word(main_rom, (uint16_t)(0xaa57 + frame + read_word(ram, actor + 0x6c))));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if ((read_word(ram, actor + 0x6a) & 1) &&
            !(read_word(ram, 0x274) & ((ram[actor + 0x0b] & 2) + 1))) {
            write_word(ram, actor + 0x26, 0x7f00);
            if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        si_after_sprite();
        break;
    /* 0:B4CE (init), B52F (launch), B553 (pick target), B57B (homing) and
       B5EB (free flight): the homing missile released by B482. Targets come
       from the surface-enemy list at B6EE. */
    case 0xb4ce:
    case 0xb52f:
    case 0xb553:
    case 0xb57b:
    case 0xb5eb: {
        if (!tiles) return 0;
        uint32_t state = address;
        if (state == 0xb4ce) {
            world_acquire_resource(ram, actor, 0x5b);
            world_acquire_resource(ram, actor, read_word(main_rom, 0x9bb1 + (ram[actor + 0x0b] & 2)));
            write_word(ram, actor + 0x3c, 0xb3c3);
            write_word(ram, actor + 0x26, 4);
            write_word(ram, actor + 0x36, 0xa0);
            write_word(ram, actor + 0x2e, 0x200);
            write_word(ram, actor + 0x42, 4);
            write_word(ram, actor + 0x5a, 0x10);
            write_word(ram, actor + 0x4a, 0);
            ram[actor + 0x28] += main_rom[(uint16_t)(0xb3d7 + read_word(ram, actor + 0x78))];
            write_word(ram, actor + 0x2a, read_word(main_rom, 0xb337 + (ram[actor + 0x28] >> 2)));
            write_word(ram, actor, 0xb52f);
            state = 0xb52f;
        }
        int retarget = state == 0xb4ce;
        if (address == 0xb4ce) retarget = 1;
        if (state == 0xb52f) {
            if (!decrement(ram, actor + 0x5a)) {
                write_word(ram, actor + 0x4a, 1);
                write_word(ram, actor + 0x2e, 0x400);
                write_word(ram, actor, 0xb553);
                write_word(ram, actor + 0x5a, 1);
                retarget = 1;
            }
        } else if (state == 0xb553 || state == 0xb57b) {
            if (state == 0xb553) {
                uint16_t count = read_word(ram, 0xb6ea), target = 0;
                if (count) {
                    uint16_t cursor = read_word(ram, 0xb6ec);
                    if (cursor >= count) cursor = 0;
                    target = read_word(ram, (uint16_t)(0xb6ee + cursor));
                    write_word(ram, 0xb6ec, cursor + 2);
                }
                write_word(ram, actor + 0x4e, target);
                write_word(ram, actor, 0xb57b);
            }
            uint16_t target = read_word(ram, actor + 0x4e);
            if (!target || (ram[target + 0x0a] & 1))
                write_word(ram, actor, 0xb5eb);
            else if (!decrement(ram, actor + 0x5a)) {
                write_word(ram, actor + 0x5a, 2);
                uint8_t heading = ram[actor + 0x28];
                uint8_t wanted = (aim_angle(main_rom,
                                            read_word(ram, target + 0x21) - read_word(ram, actor + 0x21),
                                            read_word(ram, target + 0x24) - read_word(ram, actor + 0x24)) + 4) & 0xf8;
                if (wanted != heading)
                    wanted = ((uint8_t)(wanted - heading) < (uint8_t)(heading - wanted) ? heading + 8 : heading - 8) & 0xf8;
                ram[actor + 0x28] = wanted;
                write_word(ram, actor + 0x2a, read_word(main_rom, 0xb337 + (wanted >> 2)));
                retarget = 1;
            }
        }
        if (retarget)
            for (unsigned axis = 0; axis < 2; ++axis) {
                int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
                write_word(ram, actor + 0x38 + 2 * axis, (unit * (int16_t)read_word(ram, actor + 0x2e)) >> 8);
            }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 4);
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
            unsigned offset = (ram[actor + 0x28] & 0xf8) >> 1;
            write_word(ram, trail + 0x21, x + read_word(main_rom, 0xb3e1 + offset));
            write_word(ram, trail + 0x24, y + read_word(main_rom, 0xb3e3 + offset));
            ram[trail + 0x2c] = ram[actor + 0x14];
            write_word(ram, trail + 0x56, 0x60);
            write_word(ram, trail + 0x58, 0x11);
        }
        unsigned material = terrain_material(ram, tiles + 0x4000, x, y);
        int impact = !!(0x5502 & (1u << (material & 15))), gone = impact;
        if (!impact) {
            int16_t top = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
            uint16_t bottom = (read_word(ram, 0x26e) & 2) ? 0x198 : (uint16_t)(0x1c8 + read_word(ram, 0xb732));
            if (read_word(ram, actor + 0x4a) && material == 4) {
                spawn_surface_splash_effect(ram, main_rom, actor, x, y);
                gone = 1;
            } else if (x > 0x1c0 || x < 0x40 || y > bottom || (int16_t)y < top)
                gone = 1;
            else if (!read_word(ram, actor + 0x26))
                impact = gone = 1;
        }
        if (impact) spawn_impact(ram, main_rom, actor);
        if (gone) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            decrement(ram, parent + 0x44);
            finish_animation(ram, actor);
        } else if (read_word(ram, actor + 0x3c) != 0xffff)
            world_append_candidate(ram + 0xaee6, actor);
        break;
    }
    /* 0:BAA5 (init) and BAAF: the level-3 salvo. Every fourth tick it
       releases one BB40 missile from the launcher above it, 7A times. */
    case 0xbaa5:
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0xbaaf);
        /* fall through */
    case 0xbaaf: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t missile = world_spawn_child(ram, actor, 0xbb40);
        if (missile) {
            uint16_t launcher = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            world_queue_sound(ram, 0x3b);
            ram[missile + 0x29] = ram[launcher + 0x29];
            write_word(ram, missile + 0x21, read_word(ram, launcher + 0x21) +
                       read_word(main_rom, 0xba95 + 2 * (ram[launcher + 0x29] & 1)));
            write_word(ram, missile + 0x24, read_word(ram, launcher + 0x24) + 4);
            ram[missile + 0x28] = (ram[launcher + 0x28] + 4) & 0xf8;
            ram[missile + 0x2d] = ram[actor + 0x2d];
            ram[missile + 0x0b] = ram[actor + 0x0b];
            write_word(ram, missile + 0x7a, read_word(ram, actor + 0x58));
            write_word(ram, missile + 0x54, read_word(ram, actor + 0x54));
            write_word(ram, actor + 0x58, read_word(ram, actor + 0x58) + 1);
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x54) + 1);
        }
        if (!decrement(ram, actor + 0x7a)) finish_animation(ram, actor);
        else write_word(ram, actor + 0x42, 4);
        break;
    }
    /* 0:BB40 (init) and BB7E: a salvo missile. It bursts (BC14) when hit,
       on solid ground, or above its per-missile height once armed. */
    case 0xbb40:
    case 0xbb7e: {
        if (!tiles) return 0;
        if (address == 0xbb40) {
            world_acquire_resource(ram, actor, 0x5a);
            write_word(ram, actor + 0x3c, 0xba7d);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 0x60);
            write_word(ram, actor + 0x2e, 0x600);
            for (unsigned axis = 0; axis < 2; ++axis) {
                int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
                write_word(ram, actor + 0x38 + 2 * axis, (unit * 0x600) >> 8);
            }
            write_word(ram, actor + 0x4e, (uint8_t)(ram[actor + 0x28] - 0x28) & 0xf8);
            write_word(ram, actor + 0x58, 1);
            write_word(ram, actor + 0x42, 0x0c);
            write_word(ram, actor, 0xbb7e);
        } else
            scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40) & 6;
            write_word(ram, actor + 0x58, read_word(main_rom, 0xb9cb + frame));
            write_word(ram, actor + 0x2a, read_word(main_rom, (uint16_t)(0xb993 + frame + read_word(ram, actor + 0x4e))));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        int burst = !read_word(ram, actor + 0x26);
        if (!burst) {
            if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
            if (read_word(ram, actor + 0x42))
                decrement(ram, actor + 0x42);
            else if ((int16_t)read_word(ram, actor + 0x24) >=
                     (int16_t)read_word(main_rom, 0xba99 + 2 * (read_word(ram, actor + 0x7a) & 3)))
                burst = 1;
            if (!burst)
                burst = !!(world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                                              read_word(ram, actor + 0x24)) & 0x5502);
        }
        if (burst) {
            write_word(ram, actor + 0x36, 0x0c);
            write_word(ram, actor + 0x3c, 0xba8d);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x58, 1);
            write_word(ram, actor + 0x42, 0x29);
            write_word(ram, actor, 0xbc14);
        }
        break;
    }
    case 0xbc14:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0xb9d3 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x58, read_word(main_rom, (uint16_t)(0xb9ff + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (read_word(ram, actor + 0x42)) {
            decrement(ram, actor + 0x42);
            write_word(ram, actor + 0x26, 0x7f00);
            if ((read_word(ram, 0x274) & 3) == read_word(ram, actor + 0x54) &&
                read_word(ram, actor + 0x3c) != 0xffff)
                world_append_candidate(ram + 0xaee6, actor);
        }
        break;
    /* 0:BC73 (init), BCB3 (launch), BCF3 (flight) and BD9A (floating): the
       mode-4 secondary shot. It ends as the A199 burst and returns its slot
       to the launcher at parent + 48. */
    case 0xbc73:
        world_acquire_resource(ram, actor, 0x8f);
        write_word(ram, actor + 0x3c, 0xba85);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x90);
        write_word(ram, actor + 0x2e, 0x300);
        ram[actor + 0x28] = main_rom[0xbaa1 + (read_word(ram, actor + 0x46) & 3)];
        for (unsigned axis = 0; axis < 2; ++axis) {
            int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
            write_word(ram, actor + 0x38 + 2 * axis, (unit * 0x300) >> 8);
        }
        write_word(ram, actor + 0x5a, 0x80);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor, 0xbcb3);
        break;
    case 0xbcb3:
    case 0xbcf3:
    case 0xbd9a: {
        if (!tiles) return 0;
        int expire = 0;
        uint16_t frame = read_word(ram, actor + 0x40);
        if (address == 0xbcb3 && !decrement(ram, actor + 0x58)) {
            uint16_t sprite = read_word(main_rom, (uint16_t)(0xba2b + frame));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x58, 1);
                write_word(ram, actor + 0x38, 0);
                write_word(ram, actor + 0x3a, 0x100);
                write_word(ram, actor, 0xbcf3);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x58, read_word(main_rom, (uint16_t)(0xba41 + frame)));
                write_word(ram, actor + 0x40, frame + 2);
            }
        } else if (address == 0xbcf3 && !decrement(ram, actor + 0x58)) {
            write_word(ram, actor + 0x2a, read_word(main_rom, (uint16_t)(0xba55 + frame)));
            write_word(ram, actor + 0x58, 8);
            write_word(ram, actor + 0x40, frame + 2 < 0x14 ? frame + 2 : 0);
        }
        if (address != 0xbd9a) {
            scroll_actor(ram, actor, 0xb716);
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            if (!read_word(ram, actor + 0x26))
                expire = 1;
            else {
                uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
                if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                unsigned material = terrain_material(ram, tiles + 0x4000, x, y + 8);
                if (0x5502 & (1u << (material & 15))) {
                    /* A3B7 calls 7000:9D93 for this stage-five material. */
                    if (read_word(ram, 0x84) == 4 && material == 1) return 0;
                    expire = 1;
                } else if (material == 3) {
                    write_word(ram, actor + 0x31, x);
                    write_word(ram, actor + 0x34, y - 2);
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x58, 1);
                    write_word(ram, actor, 0xbd9a);
                }
            }
        } else {
            /* 435F, then 675C: bob on a sine of the distance drifted. */
            uint16_t dx = read_word(ram, 0xb71a);
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + dx);
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + dx);
            uint16_t drift = read_word(ram, actor + 0x21) - read_word(ram, actor + 0x31);
            uint8_t angle = (uint8_t)(((uint32_t)drift * 0x400) >> 8);
            int32_t wave = (int16_t)read_word(main_rom + 0xa0000, 0x9d92 + 2 * angle) * 3;
            uint32_t height = (ram[actor + 0x33] | ((uint32_t)read_word(ram, actor + 0x34) << 8)) + ((uint32_t)wave & 0xffffff);
            ram[actor + 0x23] = (uint8_t)height;
            write_word(ram, actor + 0x24, (uint16_t)(height >> 8));
            decrement(ram, actor + 0x31);
            if (!read_word(ram, actor + 0x26)) expire = 1;
            else if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        if (expire) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            decrement(ram, parent + 0x48);
            write_word(ram, actor + 0x36, 0x0c);
            write_word(ram, actor, 0xa199);
        }
        if (address == 0xbd9a && !decrement(ram, actor + 0x58)) {
            frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom, (uint16_t)(0xba69 + frame)));
            write_word(ram, actor + 0x58, 8);
            write_word(ram, actor + 0x40, frame + 2 < 0x14 ? frame + 2 : 0);
        }
        if (!decrement(ram, actor + 0x5a)) write_word(ram, actor + 0x26, 0);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* 0:AE2A: an enemy thrown by a type-121 blast. */
    case 0xae2a:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            spawn_kill_blast(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    case 0x9be7:
        if (!tiles)
            return 0;
        init_standard_torpedo(ram, main_rom, tiles + 0x4000, actor);
        break;
    case 0x9c33:
    case 0x9c82:
        if (!tiles || (ram[actor + 0x0b] & 0x80) || read_word(ram, 0x84) == 4 ||
            (address == 0x9c33 && !read_word(ram, actor + 0x42)))
            return 0;
        update_standard_torpedo(ram, main_rom, tiles + 0x4000, actor, address == 0x9c33);
        break;
    case 0xa933f:
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xa0000, 0x9342));
        ram[actor + 0x2c] = 5;
        ram[actor + 0x2d] = 17;
        write_word(ram, actor + 0x42, 160 - 4 * read_word(ram, actor + 0x56));
        write_word(ram, actor, 0x935f);
        /* fall through */
    case 0xa935f:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    case 0xa937d: {
        unsigned column = read_word(ram, actor + 0x56);
        unsigned side = read_word(ram, actor + 0x1e);
        uint16_t glyph = read_word(ram, 0xb9f8 + 2 * column + (side ? 0 : 40));
        if (!glyph) {
            finish_animation(ram, actor);
            break;
        }
        if (glyph != 1) {
            write_word(ram, actor + 0x21, 104 + side + 16 * column);
            uint16_t child = effect_slot(ram, actor, 0x933f);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x2a, glyph);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x56, column);
        }
        write_word(ram, actor + 0x58, glyph == 1 ? 0x9134 : 0x9122);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, glyph == 1 ? 0x93b8 : 0x93fb));
        write_word(ram, actor + 0x44, glyph == 1 ? 4 : 8);
        ram[actor + 0x2c] = 6;
        ram[actor + 0x2d] = 18;
        write_word(ram, actor, 0x940f);
    }
        /* fall through */
    case 0xa940f:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t index = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xa0000, read_word(ram, actor + 0x58) + index);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x912c + index));
            write_word(ram, actor + 0x40, index + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa9496:
    case 0xa94f8:
        write_word(ram, actor + 0x40, 0xfffe);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, address == 0xa9496 ? 0x94a5 : 0x9507);
        /* fall through */
    case 0xa94a5:
    case 0xa9507:
    case 0xa944b: {
        if (read_word(ram, 0x262) & 0x8000) {
            finish_animation(ram, actor);
            break;
        }
        if (decrement(ram, actor + 0x42)) break;
        uint16_t phase = read_word(ram, actor + 0x40) + 2;
        write_word(ram, actor + 0x40, phase);
        int fixed_slot = address == 0xa944b;
        unsigned span = fixed_slot ? 12 : 8;
        if (phase >= span) {
            finish_animation(ram, actor);
            break;
        }
        int high_slot = address == 0xa94f8 || address == 0xa9507;
        unsigned base = fixed_slot ? 0x9140 : high_slot ? 0x8f20 : 0x8f08;
        uint16_t slot = fixed_slot ? 5 : read_word(ram, actor + 0x50);
        if (!fixed_slot)
            slot = (slot & 0xff00) | ((slot + (high_slot ? 32 : 11)) & 255);
        const uint8_t *row = main_rom + 0xa0000 + base + phase;
        queue_palette(ram, slot & 63, read_word(row, 0), read_word(row, span));
        if (!fixed_slot) {
            unsigned dest = read_word(ram, 0x262) & 0x8800 ? slot : 0x9e06 + 8 * (slot & 63);
            ram[(uint16_t)(dest + 7)] = 5;
        }
        write_word(ram, actor + 0x42, read_word(row, 2 * span));
        break;
    }
    case 0xa955a:
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x9564);
        /* fall through */
    case 0xa9564:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x8efc + (step & 6)));
            write_word(ram, 0xb70c, read_word(main_rom + 0xa0000, 0x8f04 + (step & 2)));
            write_word(ram, actor + 0x40, step + 2);
        }
        break;
    case 0xa9164: {
        const uint8_t *bank = main_rom + 0xa0000;
        write_word(ram, actor + 0x2c, 0x0808);
        write_word(ram, actor + 0x21, 0x70);
        write_word(ram, actor + 0x24, 0x170);
        write_word(ram, actor + 0x2a, read_word(bank, 0x9179));
        uint16_t middle = mission_child(ram, actor, 0x91c8);
        write_word(ram, middle + 0x2c, 0x0808);
        write_word(ram, middle + 0x21, 0x70);
        write_word(ram, middle + 0x24, 0x100);
        write_word(ram, middle + 0x2a, read_word(bank, 0x919b));
        uint16_t bottom = mission_child(ram, actor, 0x91c8);
        write_word(ram, bottom + 0x2c, 0x0808);
        ram[bottom + 0x29] = 2;
        write_word(ram, bottom + 0x21, 0x70);
        write_word(ram, bottom + 0x24, 0x90);
        write_word(ram, bottom + 0x2a, read_word(bank, 0x91c1));
        write_word(ram, actor, 0x91c8);
        /* fall through */
    }
    case 0xa91c8:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa91ce:
        ram[actor + 0x2c] = 4;
        ram[actor + 0x2d] = 16;
        write_word(ram, actor + 0x38, 0x400);
        write_word(ram, actor + 0x46, 2);
        /* fall through */
    case 0xa91e5:
        start_mission_scroll(ram, main_rom + 0xa0000, actor);
        /* fall through */
    case 0xa923c:
        update_mission_scroll(ram, main_rom, actor);
        break;
    case 0xa9589:
        if (!tiles) return 0;
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x9593);
        /* fall through */
    case 0xa9593: {
        if (!tiles) return 0;
        if (decrement(ram, actor + 0x42)) break;
        uint16_t row = read_word(ram, actor + 0x40);
        uint16_t dest = 0x1648 + 2 * (uint16_t)((row >> 8) | (row << 8));
        draw_join_label(tiles, main_rom, dest, read_word(main_rom, 0x2109 + row));
        row += 2;
        write_word(ram, actor + 0x40, row);
        write_word(ram, actor + 0x42, 20);
        if (row >= 10) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 324);
            write_word(ram, actor, 0x95d4);
        }
        break;
    }
    case 0xa95d4: {
        if (!tiles) return 0;
        if (decrement(ram, actor + 0x42)) break;
        uint16_t row = read_word(ram, actor + 0x40);
        do {
            draw_join_label(tiles, main_rom, 0x1648 + row, read_word(main_rom, 0x2113));
            row += 0x200;
        } while (row < 0x1400);
        write_word(ram, actor + 0x40, row);
        finish_animation(ram, actor);
        break;
    }
    case 0xafc1b:
    case 0xafce1: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned flags = ram[actor + 0x1e];
        uint16_t resource = address == 0xafc1b ? read_word(bank, 0xfc1c) : read_word(bank, 0xf160 + 2 * (flags & 3));
        world_acquire_resource(ram, actor, resource);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x26, 128);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xfd05));
        if (address == 0xafc1b) {
            ram[actor + 0x29] = flags & 1;
            write_word(ram, actor + 0x38, read_word(bank, 0xf140 + 2 * (flags & 7)));
            write_word(ram, actor + 0x42, read_word(bank, 0xf150 + (flags & 6)));
            write_word(ram, actor, 0xfc67);
        } else {
            write_word(ram, actor + 0x38, read_word(bank, 0xf178 + (flags & 12)));
            write_word(ram, actor + 0x3a, read_word(bank, 0xf17a + (flags & 12)));
            write_word(ram, actor, 0xfd29);
        }
        write_word(ram, actor + 0x44, 1);
        break;
    }
    case 0xba6d6: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned route = ram[actor + 0x1f] & 14;
        uint16_t script = read_word(bank, 0xa5f8 + route);
        if (read_word(bank, script) != 2) return 0;
        world_acquire_resource(ram, actor, read_word(bank, 0xa6d7));
        write_word(ram, actor + 0x0e, 19);
        ram[actor + 0x2d] = 32;
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xa6f4));
        write_word(ram, actor + 0x2a, read_word(bank, 0xa6f9));
        begin_linear_script(ram, main_rom, actor, script);
        if ((int16_t)read_word(ram, actor + 0x38) >= 0) ram[actor + 0x29] = 1;
        uint16_t child = bank_b_effect(ram, actor, 0xa7a6);
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        ram[child + 0x29] = ram[actor + 0x29];
        write_word(ram, actor, 0xa734);
        break;
    }
    case 0xba734: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0 || read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        unsigned pose = ((ram[actor + 0x28] + 8) & 0xf0) >> 3;
        write_word(ram, actor + 0x40, pose);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0xa52e + pose));
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        else {
            world_award_score(ram, main_rom, actor, hit);
            uint16_t effect = effect_slot(ram, actor, 0x6efe);
            write_word(ram, effect + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, effect + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, effect + 0x44, 0x5d);
            ram[effect + 0x2d] = 32;
            write_word(ram, effect + 0x56, 48);
            write_word(ram, effect + 0x58, 128);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xba7a6: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, read_word(bank, 0xa7a7));
        write_word(ram, actor + 0x0e, 19);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xa7c0));
        write_word(ram, actor + 0x2a, read_word(bank, 0xa7c5));
        uint16_t config = 0xa59e + read_word(bank, 0xa55e + (ram[actor + 0x1e] & 32) + read_word(ram, 0x8a));
        write_word(ram, actor + 0x52, read_word(bank, config + 2));
        write_word(ram, actor + 0x54, read_word(bank, config + 4));
        write_word(ram, actor + 0x50, read_word(bank, 0xa5f0 + (ram[actor + 0x1f] & 14)));
        write_word(ram, actor, 0xa7fd);
    }
        /* fall through */
    case 0xba7fd: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) - 6);
        ram[actor + 0x2d] = main_rom[0xba6c6 + (read_word(ram, parent + 0x40) >> 1)];
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x3a, -256);
            world_queue_sound(ram, 0x70);
            write_word(ram, actor, 0xa863);
        }
        break;
    }
    case 0xba863:
    case 0xba8e0:
    case 0xba968:
    case 0xba9ee:
    case 0xbaa8f: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbaa8f) {
            if (read_word(ram, actor + 0x54)) {
                write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) - read_word(ram, actor + 0x52));
                decrement(ram, actor + 0x54);
            }
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            if (read_word(ram, actor + 0x42)) {
                decrement(ram, actor + 0x42);
                move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            }
        } else move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (x < 64) {
            finish_animation(ram, actor);
            break;
        }
        if ((address == 0xba863 || address == 0xba9ee) && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0xa550 + frame);
            if (sprite != 0xffff) {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, 4);
                write_word(ram, actor + 0x40, frame + (address == 0xba863 ? 2 : -2));
            } else if (address == 0xba863) {
                write_word(ram, actor + 0x3a, -512);
                write_word(ram, actor, 0xa8e0);
            } else {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x50, 1);
                write_word(ram, actor + 0x3a, read_word(bank, 0xa5d0 + (ram[actor + 0x1e] & 6)));
                write_word(ram, actor + 0x42, 16);
                write_word(ram, actor, 0xaa8f);
            }
        } else if (address == 0xbaa8f) {
            uint16_t frame = read_word(ram, actor + 0x40) + 64;
            if (frame >= 768) frame = 0;
            write_word(ram, actor + 0x40, frame);
            write_word(ram, actor + 0x2a, read_word(bank, 0xa558 + 2*(frame >> 8)));
        }
        queue_sprite(ram, main_rom, actor, 1);
        int impact = 0;
        if (address == 0xba863 || address == 0xba8e0) {
            ram[actor + 0x0d] = 0;
            world_surface_contact(ram, main_rom, actor);
            impact = !read_word(ram, actor + 0x26);
            if (!impact && address == 0xba8e0) {
                uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
                unsigned material = tiles[0x4000 + cell + 3] >> 3;
                if (material == 4) {
                    write_word(ram, actor + 0x3a, -256);
                    write_word(ram, actor + 0x56, read_word(bank, 0xa5c8 + (ram[actor + 0x1e] & 6)));
                    ram[actor + 0x2d] = 4;
                    write_word(ram, actor, 0xa968);
                } else impact = (1u << (material & 15)) & 0xff02;
            }
        } else {
            if (address == 0xbaa8f)
                impact = world_terrain_mask(ram, tiles + 0x4000, x - 16, y) & 0xff02;
            if (!impact) {
                ram[actor + 0x0d] = 2;
                impact = world_player_contact(ram, main_rom, actor) != 0;
                if (!impact) {
                    ram[actor + 0x0d] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    impact = !read_word(ram, actor + 0x26);
                }
            }
            if (!impact && address == 0xba968) {
                impact = world_terrain_mask(ram, tiles + 0x4000, x, y - 2) & 0xff02;
                if (!impact && !decrement(ram, actor + 0x56)) {
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x40, 4);
                    write_word(ram, actor, 0xa9ee);
                }
            }
            if (!impact && address == 0xbaa8f && !decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, 3);
                uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0xbe41);
                write_word(ram, trail + 0x21, x - 4);
                write_word(ram, trail + 0x24, y);
                ram[trail + 0x2c] = 3;
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xbab67: {
        const uint8_t *bank = main_rom + 0xb0000;
        int16_t limit = read_word(ram, actor + 0x1e) & 0xfff0;
        if ((int16_t)read_word(ram, 0xb700) >= limit) {
            finish_animation(ram, actor);
            break;
        }
        int16_t delay = read_word(ram, actor + 0x42) - 4;
        if (delay > 0) {
            uint16_t scroll = -read_word(ram, 0xb71a);
            delay -= scroll >> 4;
        }
        if (delay <= 0) {
            for (int offset = 6; offset >= 0; offset -= 2) {
                uint16_t child = bank_b_effect(ram, actor, 0xa6d6);
                if (child == 0x8480) continue;
                write_word(ram, child + 0x21, read_word(bank, 0xa5d8 + offset));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xa5e0 + offset));
                write_word(ram, child + 0x1e, read_word(bank, 0xa5e8 + offset));
            }
            delay = 1024;
        }
        write_word(ram, actor + 0x42, delay);
        break;
    }
    case 0x2681: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1, stage = read_word(ram, 0x84);
        if (!stage || stage > 16) return 0;
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t child = effect_slot(ram, actor, 0x2b18);
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e) + 2*i);
        }
        unsigned earned = read_word(ram, 0x124 + 64*side);
        write_word(ram, actor + 0x48, earned);
        write_word(ram, actor + 0x42, 0);
        write_word(ram, actor + 0x40, 2*(stage - 1));
        for (unsigned i = 0; i + 1 < stage; ++i) {
            if (earned & 1) {
                uint16_t child = effect_slot(ram, actor, 0x2e60);
                write_word(ram, child + 0x1e, 2*i + read_word(ram, actor + 0x1e));
            }
            earned >>= 1;
        }
        write_word(ram, actor + 0x48, earned);
        if (!(ram[0x264] & (1u << side))) {
            write_word(ram, actor, 0x2abb);
            address = 0x2abb;
        } else {
            tally_score(ram, main_rom, tiles, side);
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) | (1u << side));
            unsigned small = read_word(ram, 0x120 + 64*side), large = read_word(ram, 0x122 + 64*side);
            write_word(ram, actor + 0x50, small >= 190 ? 189 : small);
            write_word(ram, actor + 0x52, large >= 30 ? 29 : large);
            write_word(ram, actor + 0x48, read_word(ram, 0x124 + 64*side));
            write_word(ram, actor + 0x42, 64);
            write_word(ram, actor + 0x4e, 10);
            write_word(ram, actor, 0x2756);
            address = 0x2756;
        }
    }
        /* fall through */
    case 0x2756:
    case 0x2abb: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1;
        if (address == 0x2abb) {
            uint16_t clock = read_word(ram, 0x274);
            if (clock & 31) break;
            uint16_t label = 0xc2f6;
            if (clock & 32) {
                unsigned credits = read_word(ram, 0x2b8);
                if (ram[0x264] & (1u << side)) label = read_word(main_rom, 0xc253 + 2*side);
                else if (ram[0x2c4] & 1) label = 0xc2e5;
                else if (!credits || ((ram[0x289] & 128) && credits < 2)) label = 0xc290;
                else label = read_word(main_rom, 0xc24f + 2*side);
            }
            tally_large_label(tiles, main_rom, read_word(main_rom, 0xc25b + 2*side), label, 0x106);
        } else {
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor, 0x2769);
            }
            if (tally_skip_requested(ram))
                tally_bonus(ram, main_rom, tiles, actor);
        }
        break;
    }
    case 0x2769:
    case 0x2811:
    case 0x28d3:
    case 0x290d:
    case 0x2942:
    case 0x29b8: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1;
        unsigned stage = (read_word(ram, 0x84) - 1) & 15;
        int completed = 0;
        if (address == 0x2769 || address == 0x2811) {
            if (!decrement(ram, actor + 0x42)) {
                int large = address == 0x2811;
                uint16_t count = actor + (large ? 0x52 : 0x50);
                write_word(ram, actor + 0x42, 2);
                if (!read_word(ram, count)) {
                    write_word(ram, actor, large ? 0x28d3 : 0x2811);
                    if (large) {
                        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                        write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) & ~(1u << side));
                    }
                } else {
                    decrement(ram, count);
                    tally_add_count(ram, main_rom, tiles, actor, large);
                    uint16_t partial = actor + (large ? 0x56 : 0x54);
                    unsigned item = read_word(ram, partial);
                    uint16_t dest = read_word(main_rom, (large ? 0x22d7 : 0x22bf) + 2*item) + side*0x1000;
                    if (large) tally_stamp(tiles, main_rom, dest, 0x2478, 0x107);
                    else for (uint16_t text = 0x2461; main_rom[text] != 255; ++text) {
                        tally_tile(tiles, dest, main_rom[text], 0x107);
                        dest = (dest & 0xff00) | ((dest + 4) & 255);
                    }
                    write_word(ram, partial, item + 1);
                    if (item + 1 == (large ? 5 : 10)) {
                        dest = read_word(main_rom, large ? 0x22d7 : 0x22bf) + side*0x1000;
                        draw_join_label(tiles, main_rom, dest, large ? 0x2494 : 0x2499);
                        draw_join_label(tiles, main_rom, dest + (large ? 256 : 512), large ? 0x2494 : 0x2499);
                        write_word(ram, partial, 0);
                        uint16_t groups = actor + (large ? 0x5a : 0x58);
                        write_word(ram, groups, read_word(ram, groups) + 1);
                        tally_groups(ram, main_rom, tiles, actor, large);
                    }
                }
            }
        } else if (address == 0x28d3) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            if (!read_word(ram, parent + 0x46)) {
                tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side), 0x248c, 0x110);
                write_word(ram, actor + 0x42, 32);
                write_word(ram, actor, 0x290d);
            }
        } else if (address == 0x290d) {
            if (!decrement(ram, actor + 0x42)) {
                int both = (ram[0x264] & 3) == 3;
                write_word(ram, actor + 0x42, both ? 16 : 2);
                write_word(ram, actor, both ? 0x2942 : 0x29b8);
                if (both) {
                    write_word(ram, actor + 0x40, 4);
                    write_word(ram, actor + 0x4a, 8);
                }
            }
        } else if (address == 0x2942) {
            if (!decrement(ram, actor + 0x42)) {
                unsigned frame = read_word(ram, actor + 0x40);
                uint16_t shape = read_word(main_rom, 0x2488 + 2*(frame & 1));
                tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side), shape, 0x110);
                if (!decrement(ram, actor + 0x40)) {
                    write_word(ram, actor + 0x40, 4);
                    unsigned delay = read_word(ram, actor + 0x4a) >> 1;
                    write_word(ram, actor + 0x4a, delay);
                    if (!delay) {
                        unsigned medal = (read_word(ram, actor + 0x48) >> stage) & 1;
                        tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side),
                                    read_word(main_rom, 0x2488 + 2*medal), 0x110);
                        write_word(ram, actor + 0x42, 2);
                        write_word(ram, actor, 0x29b8);
                    } else write_word(ram, actor + 0x42, delay);
                } else write_word(ram, actor + 0x42, read_word(ram, actor + 0x4a));
            }
        } else if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 2);
            if (read_word(ram, actor + 0x48) & (1u << stage))
                tally_add_count(ram, main_rom, tiles, actor, 2);
            if (!decrement(ram, actor + 0x4e)) {
                write_word(ram, actor + 0x42, 64);
                write_word(ram, 0xd2, 1);
                write_word(ram, actor, 0x2a10);
                completed = 1;
            }
        }
        if (!completed && tally_skip_requested(ram)) tally_bonus(ram, main_rom, tiles, actor);
        break;
    }
    case 0x2a10:
        if (!tiles) return 0;
        if (!decrement(ram, actor + 0x42)) {
            unsigned side = ram[actor + 0x1e] & 1;
            memset(tiles + read_word(main_rom, 0x22e9 + 2*side), 0, 0xe00);
            tally_score(ram, main_rom, tiles, side);
            write_word(ram, actor + 0x42, 32);
            write_word(ram, actor, 0x2a3d);
        }
        break;
    case 0x2a3d:
        if (!decrement(ram, actor + 0x42)) {
            unsigned stage = read_word(ram, 0x84) - 1;
            if (read_word(ram, actor + 0x48) & (1u << (stage & 15))) {
                uint16_t child = effect_slot(ram, actor, 0x2c04);
                write_word(ram, child + 0x1e, read_word(main_rom, 0x22ed + 2*(2*stage + read_word(ram, actor + 0x1e))));
                write_word(ram, actor + 0x2e, 0x600);
                write_word(ram, actor + 0x38, 0x1000);
            }
            write_word(ram, actor, 0x2a7c);
        }
        break;
    case 0x2a7c: {
        uint16_t amplitude = read_word(ram, actor + 0x2e);
        if (amplitude) {
            uint16_t x = read_word(ram, actor + 0x21) + 2, speed = read_word(ram, actor + 0x38);
            write_word(ram, actor + 0x21, x);
            unsigned angle = ((uint32_t)x * speed >> 8) & 255;
            int16_t unit = read_word(main_rom + 0xa0000, 0x9d92 + 2*angle);
            uint32_t y = ram[actor + 0x33] + 256u*read_word(ram, actor + 0x34) + unit*(amplitude >> 8);
            ram[actor + 0x23] = y;
            write_word(ram, actor + 0x24, y >> 8);
            write_word(ram, actor + 0x38, speed - 9);
            if ((int16_t)amplitude > 0x300) write_word(ram, actor + 0x2e, amplitude - 32);
        }
        write_word(ram, ram[actor + 0x1e] & 1 ? 0xb710 : 0xb70c, read_word(ram, actor + 0x24));
        break;
    }
    case 0x2b18: {
        unsigned side = ram[actor + 0x1e] & 3;
        ram[actor + 0x2d] = 63;
        ram[actor + 0x2c] = main_rom[0x2417 + side];
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x241b + 2*side));
        write_word(ram, actor + 0x21, read_word(main_rom, 0x2423 + 4*side));
        write_word(ram, actor + 0x34, read_word(main_rom, 0x2425 + 4*side));
        write_word(ram, actor, 0x2b4c);
    }
        /* fall through */
    case 0x2b4c:
        write_word(ram, actor + 0x24, read_word(ram, (ram[actor + 0x1e] & 1) ? 0xb710 : 0xb70c) + read_word(ram, actor + 0x34));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x2b68:
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x2b6b));
        ram[actor + 0x2c] = 0x84;
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x2301 + 2*read_word(ram, actor + 0x1e)));
        write_word(ram, actor + 0x21, 256);
        write_word(ram, actor + 0x24, 256);
        write_word(ram, actor + 0x42, 16);
        write_word(ram, actor, 0x2b96);
        /* fall through */
    case 0x2b96:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor, 0x2bab);
        }
        break;
    case 0x2bab:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            unsigned index = 2*read_word(ram, actor + 0x1e);
            write_word(ram, actor + 0x31, 256);
            write_word(ram, actor + 0x34, 256);
            write_word(ram, actor + 0x1a, read_word(main_rom, 0x2329 + index));
            write_word(ram, actor + 0x1c, read_word(main_rom, 0x233d + index));
            write_word(ram, actor + 0x42, 64);
            write_word(ram, actor, 0x2bdf);
        }
        break;
    case 0x2bdf: {
        uint16_t phase = read_word(ram, actor + 0x1a) - 256;
        write_word(ram, actor + 0x1a, phase);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t pos = actor + 0x20 + 3*i, center = pos + 16;
            int16_t unit = read_word(main_rom + 0xa0000, (i ? 0x9d92 : 0x9e12) + 2*(phase >> 8));
            uint32_t coord = ram[center] + 256u*read_word(ram, center + 1) + unit*(int16_t)read_word(ram, actor + 0x1c);
            ram[pos] = coord;
            write_word(ram, pos + 1, coord >> 8);
        }
        write_word(ram, actor + 0x24, 256);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x2bfe);
        break;
    }
    case 0x2c04: {
        world_acquire_resource(ram, actor, 0x99);
        world_acquire_resource(ram, actor, 0);
        unsigned index=4*(read_word(ram,actor+0x1e)&15);
        uint16_t x=read_word(main_rom,0x23ef+index), y=read_word(main_rom,0x23f1+index);
        write_word(ram,actor+0x21,x);
        write_word(ram,actor+0x24,y);
        for (unsigned layer=0;layer<2;++layer) {
            uint16_t child=effect_slot(ram,actor,layer ? 0x2de7 : 0x2d6e);
            write_word(ram,child+0x21,x);
            write_word(ram,child+0x34,y);
            write_word(ram,child+0x1e,read_word(ram,actor+0x1e));
            if (!layer) world_queue_sound(ram,0x67);
        }
        for (int i=4;i>=0;--i) {
            uint16_t part=effect_slot(ram,actor,0x2d0d);
            ram[part+0x2c]=ram[actor+0x14];
            ram[part+0x29]=main_rom[0x23a9+i];
            ram[part+0x28]=main_rom[0x23ae + i];
            write_word(ram,part+0x21,x+read_word(main_rom,0x23b3+4*i));
            write_word(ram,part+0x24,y+read_word(main_rom,0x23b5+4*i));
        }
        write_word(ram,actor+0x42,0);
        ram[actor+0x2d]=15;
        write_word(ram,actor+0x44,1);
        write_word(ram,actor,0x2cc7);
        break;
    }
    case 0x2d0d:
        ram[actor+0x2d]=15;
        write_word(ram,actor+0x2e,0x300);
        write_word(ram,actor+0x44,1);
        digit_fragment_velocity(ram,main_rom,actor);
        write_word(ram,actor,0x2d25);
        /* fall through */
    case 0x2d25:
    case 0x2cc7: {
        int particle=address!=0x2cc7;
        if (particle) {
            move_actor_axis(ram,actor+0x20,read_word(ram,actor+0x38));
            move_actor_axis(ram,actor+0x23,read_word(ram,actor+0x3a));
            write_word(ram,actor+0x3a,read_word(ram,actor+0x3a)-32);
        }
        if (!decrement(ram,actor+0x44)) {
            uint16_t frame=read_word(ram,actor+0x40);
            uint16_t sprite=read_word(main_rom,(particle?0x237f:0x2351)+frame);
            if(sprite==0xffff) {finish_animation(ram,actor);break;}
            write_word(ram,actor+0x2a,sprite);
            write_word(ram,actor+0x44,read_word(main_rom,(particle?0x2395:0x2369)+frame));
            write_word(ram,actor+0x40,frame+2);
            if (!particle && frame==2) ram[actor+0x2c]=ram[actor+0x14];
        }
        queue_sprite(ram,main_rom,actor,0);
        break;
    }
    case 0x2d6e:
    case 0x2de7: {
        int back=address==0x2de7;
        write_word(ram,actor+0x3c,0xfb4b);
        acquire_private_palette(ram,actor,back?0x99:1);
        ram[actor+0x2d]=back?17:16;
        unsigned index=2*(read_word(ram,actor+0x1e)&15);
        write_word(ram,actor+0x2a,read_word(main_rom,(back?0x23db:0x23c7)+index));
        write_word(ram,actor+0x42,1);
        write_word(ram,actor,back?0x2e14:0x2d9b);
        break;
    }
    case 0x2d9b:
    case 0x2e14: {
        unsigned side=ram[actor+0x1e]&1;
        write_word(ram,actor+0x24,read_word(ram,actor+0x34)+read_word(ram,side?0xb710:0xb70c));
        queue_sprite(ram,main_rom,actor,0);
        if (!decrement(ram,actor+0x42)) {
            uint16_t frame=read_word(ram,actor+0x40);
            unsigned table=address==0x2d9b?0x2433+12*side:0x244b+6*side;
            write_word(ram,actor+0x42,read_word(main_rom,table+frame+4));
            queue_palette(ram,ram[actor+0x2c]&63,read_word(main_rom,table+frame),read_word(main_rom,table+frame+2));
            write_word(ram,actor+0x40,frame+6);
        }
        break;
    }
    case 0x2e60:
    case 0x2ee1: {
        unsigned index = read_word(ram, actor + 0x1e) & 15;
        unsigned side = index & 1;
        int back = address == 0x2ee1;
        write_word(ram, actor + 0x3c, 0xfb4b);
        ram[actor + 0x2c] = main_rom[(back ? 0x24aa : 0x24a8) + side];
        ram[actor + 0x2d] = back ? 17 : 16;
        write_word(ram, actor + 0x2a, read_word(main_rom, (back ? 0x23db : 0x23c7) + 2*index));
        if (!back) {
            write_word(ram, actor + 0x21, read_word(main_rom, 0x23ef + 4*index));
            write_word(ram, actor + 0x34, read_word(main_rom, 0x23f1 + 4*index));
            uint16_t child = effect_slot(ram, actor, 0x2ee1);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x34, read_word(ram, actor + 0x34));
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        }
        write_word(ram, actor, 0x2ec5);
        /* fall through */
    }
    case 0x2ec5:
        write_word(ram, actor + 0x24,
                   read_word(ram, (ram[actor + 0x1e] & 1) ? 0xb710 : 0xb70c) +
                   read_word(ram, actor + 0x34));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x2bfe:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x621c:
        write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + read_word(main_rom, 0x6214 + (ram[actor + 0x1e] & 6)));
        write_word(ram, actor, 0x1b4c);
        write_word(ram, actor + 2, 0);
        break;
    case 0xaf95d: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned variant = ram[actor + 0x1e] & 6;
        uint16_t init = read_word(bank, 0xf93d + variant);
        world_acquire_resource(ram, actor, read_word(bank, 0xf95e));
        world_acquire_resource(ram, actor, read_word(bank, 0xf966));
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x38;
        write_word(ram, actor + 0x36, 0x200);
        write_word(ram, actor + 0x38, 0xffe0);
        write_word(ram, actor + 0x3c, read_word(bank, init + 3));
        write_word(ram, actor + 0x2a, read_word(bank, init + 8));
        write_word(ram, actor + 0x26, read_word(bank, init + 13));
        if (ram[actor + 0x1e] & 1) {
            uint16_t child = bank_b_effect(ram, actor, 0x29e2);
            write_word(ram, child + 0x1e, 3);
            write_word(ram, child + 0x34, read_word(bank, 0xf955 + variant));
        }
        write_word(ram, actor, 0xf9bb);
        break;
    }
    case 0xaf9bb:
    case 0xafa46: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned variant = ram[actor + 0x1e] & 6;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 16) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xafa46) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, read_word(ram, actor + 0x4c) + frame);
                if (sprite != 0xffff) {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, read_word(ram, actor + 0x48) + frame));
                    write_word(ram, actor + 0x40, frame + 2);
                } else if (variant == 6) {
                    finish_animation(ram, actor);
                } else {
                    uint16_t next = read_word(bank, 0xf94d + variant);
                    ram[actor + 0x19] = 1;
                    write_word(ram, actor + 0x3c, read_word(bank, next + 7));
                    write_word(ram, actor + 0x26, read_word(bank, next + 12));
                    write_word(ram, actor + 0x1e, variant + 2);
                    write_word(ram, actor, 0xf9bb);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        if (ram[actor + 0x19]) {
            --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x14 + (ram[actor + 0x19] & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        int shattered = 0;
        if (world_player_contact(ram, main_rom, actor)) {
            uint16_t hp = read_word(ram, actor + 0x26);
            write_word(ram, actor + 0x26, hp - 16);
            shattered = hp < 16;
            if (!shattered) {
                ram[actor + 0x19] = (ram[actor + 0x19] & 1) + 10;
                world_queue_sound(ram, 0x6e);
            }
        }
        if (!shattered) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            shattered = !read_word(ram, actor + 0x26);
        }
        if (shattered) {
            world_queue_sound(ram, 0x51);
            ram[actor + 0x19] = 1;
            if (variant == 0 || variant == 4) {
                uint16_t child = effect_slot(ram, actor, 0xfa46);
                if (child != 0x8480) write_word(ram, child + 2, 0xa000);
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
                ram[child + 0x2c] = ram[actor + 0x14];
                ram[child + 0x2d] = 0x38;
                write_word(ram, child + 0x44, 1);
                write_word(ram, child + 0x4c, read_word(bank, variant ? 0xfb74 : 0xfb08));
                write_word(ram, child + 0x48, read_word(bank, variant ? 0xfb79 : 0xfb0d));
                write_word(ram, child + 0x1e, 6);
            }
            if (variant == 4) {
                write_word(ram, actor + 0x2a, read_word(bank, 0xfb87));
                write_word(ram, actor + 0x3c, read_word(bank, 0xfb8c));
                write_word(ram, actor + 0x26, 128);
                write_word(ram, actor + 0x1e, 6);
            } else {
                if (variant) write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                uint16_t table = variant == 0 ? 0xfb20 : variant == 2 ? 0xfb3e : 0xfbaf;
                write_word(ram, actor + 0x4c, read_word(bank, table));
                write_word(ram, actor + 0x48, read_word(bank, table + 5));
                write_word(ram, actor, 0xfa46);
            }
            ram[actor + 0x0a] = 2;
        }
        break;
    }
    case 0xaf1d4: {
        const uint8_t *bank = main_rom + 0xa0000;
        world_acquire_resource(ram, actor, read_word(bank, 0xf1d5));
        ram[actor + 0x2d] = 31;
        write_word(ram, actor + 0x2a, read_word(bank, 0xf1e3));
        write_word(ram, actor + 0x3c, read_word(bank, 0xf1e8));
        write_word(ram, actor + 0x26, 384);
        write_word(ram, actor + 0x36, 384);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor, 0xf200);
        break;
    }
    case 0xaf200:
    case 0xaf294: {
        uint16_t x = read_word(ram, actor + 0x31) + read_word(ram, 0xb71a);
        uint16_t dy = read_word(ram, 0xb71c);
        write_word(ram, actor + 0x31, x);
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + dy);
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + dy);
        if (address == 0xaf200 && x < 32) {
            finish_animation(ram, actor);
            break;
        }
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        write_word(ram, actor + 0x21, x + (ram[actor + 0x19] & 2));
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xaf294) {
            if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 1;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) {
            int16_t hp = read_word(ram, actor + 0x26) - read_word(ram, hit + 0x36);
            write_word(ram, actor + 0x26, hp > 0 ? hp : 0);
        }
        int flash = hit && !ram[actor + 0x19];
        int destroyed = !read_word(ram, actor + 0x26);
        for (unsigned i = 0; i < 2; ++i) {
            if (!(i ? destroyed : flash)) continue;
            uint16_t child = effect_slot(ram, actor, 0x7056);
            write_word(ram, child + 0x60, read_word(main_rom + 0xa0000, i ? 0xf27f : 0xf25e));
            write_word(ram, child + 0x62, 0xa000);
            write_word(ram, child + 0x4c, i ? 4 : 2);
            write_word(ram, child + 0x42, 1);
            if (!i) {
                ram[child + 0x2d] = 0xfe;
                ram[actor + 0x19] = 20;
            }
        }
        if (destroyed) {
            write_word(ram, actor + 0x42, 32);
            write_word(ram, actor, 0xf294);
        }
        break;
    }
    case 0xafd7d: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned variant = ram[actor + 0x1e] & 6;
        write_word(ram, actor + 0x38, read_word(bank, 0xf1be + variant));
        world_acquire_resource(ram, actor, read_word(bank, 0xf1b8 + variant));
        write_word(ram, actor + 0x4c, variant << 3);
        ram[actor + 0x2d] = 0x38;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xfdb1);
        break;
    }
    case 0xafc67:
        if (ram[actor + 0x1f] & 1) return 0;
        /* fall through */
    case 0xafd29:
    case 0xafdb1:
        update_intro_sprite(ram, main_rom, actor, (uint16_t)address);
        break;
    case 0xb29e2:
    case 0xb2a58: {
        unsigned variant = ram[actor + 0x1e] & 15;
        unsigned mask = 1u << variant;
        if (read_word(ram, 0xc4) & mask) {
            finish_animation(ram, actor);
            break;
        }
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned rank = read_word(ram, 0x8a) + (ram[actor + 0x1e] & 32);
        int mine = address == 0xb2a58;
        uint16_t config = (mine ? 0x289e : 0x2822) + read_word(bank, (mine ? 0x285e : 0x27e2) + rank);
        uint16_t cooldown = read_word(bank, config);
        write_word(ram, actor + 0x0e, mine ? 14 : 9);
        write_word(ram, actor + 0x26, 96);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x52, cooldown);
        write_word(ram, actor + 0x50, mine ? cooldown : cooldown >> 1);
        write_word(ram, actor + 0x58, read_word(bank, config + 2));
        if (!mine) {
            write_word(ram, actor + 0x5a, read_word(bank, config + 2));
            write_word(ram, actor + 0x54, read_word(bank, config + 4));
            write_word(ram, actor + 0x5c, read_word(bank, config + 6));
            write_word(ram, actor + 0x5e, read_word(bank, config + 8));
        }
        world_acquire_resource(ram, actor, mine ? 6 : 4);
        ram[actor + 0x2d] = 32;
        write_word(ram, actor + 0x4e, 0x320);
        write_word(ram, actor + 0x4c, read_word(bank, 0x2ac2));
        write_word(ram, actor + 0x3c, read_word(bank, 0x2ac7));
        ram[actor + 0x0c] = 4;
        write_word(ram, actor + 0x4a, 0);
        write_word(ram, 0xc4, read_word(ram, 0xc4) | mask);
        if (variant & 1) {
            write_word(ram, actor + 0x42, 63);
            write_word(ram, actor, 0x2b43);
        } else {
            write_word(ram, actor + 0x3a, read_word(bank, 0x28b6 + variant));
            write_word(ram, actor, 0x2b03);
        }
        break;
    }
    case 0xb2b43:
    case 0xb2b8b: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        uint16_t x = read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31);
        write_word(ram, actor + 0x21, x);
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        write_word(ram, actor + 0x2a, read_word(ram, actor + 0x4c));
        queue_sprite(ram, main_rom, actor, 1);
        if (address == 0xb2b43) {
            if (ram[parent + 0xa] || (int16_t)x <= 0x160) write_word(ram, actor, 0x2b8b);
            write_word(ram, actor + 0x2a, read_word(bank, 0x2b83));
        } else {
            uint16_t rate = (read_word(ram, actor + 0x42) & 0x30) >> 3;
            uint16_t phase = read_word(ram, actor + 0x48) + read_word(bank, 0x27a8 + rate);
            if ((int16_t)phase >= 0x300) phase = 0;
            write_word(ram, actor + 0x48, phase);
            write_word(ram, actor + 0x2a, read_word(bank, 0x2790 + 2*(phase >> 8)));
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xb2b8b && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, read_word(bank, 0x28c6 + (ram[actor + 0x1e] & 14)));
            write_word(ram, actor + 0x3a, 128);
            write_word(ram, actor, 0x2c07);
        }
        break;
    }
    case 0xb2c07:
        if (read_word(ram, actor + 0x42) == 1 && !(ram[0x264] & 3)) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (!decrement(ram, actor + 0x42)) {
            uint16_t target = world_enemy_target(ram, actor);
            if (read_word(ram, target + 0x21) < read_word(ram, actor + 0x21))
                ram[actor + 0x29] = 1;
            write_word(ram, actor, 0x2c46);
        }
        queue_surface_enemy(ram, main_rom, actor);
        register_surface_enemy(ram, actor);
        surface_enemy_hit(ram, main_rom, actor);
        break;
    case 0xb2b03: {
        uint16_t velocity = read_word(ram, actor + 0x3a);
        /* With no active player 65B1 leaves SI as the previous actor left it,
           and B2B18 tests bit 1 of that. */
        int idle = !(read_word(ram, 0x264) & 3);
        if (velocity == 0xffe0 && idle && !entry_si_known) {
            world_dispatch_detail = untracked_si_detail();
            return 0;
        }
        scroll_actor(ram, actor, 0xb71a);
        unsigned fraction = ram[actor + 0x23] + (velocity & 255);
        ram[actor + 0x23] = (uint8_t)fraction;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24)
                   + (int8_t)(velocity >> 8) + (fraction >> 8));
        velocity += 32;
        write_word(ram, actor + 0x3a, velocity);
        if (!velocity) {
            uint16_t target = world_enemy_target(ram, actor);
            if (idle ? (entry_si & 2) != 0
                     : read_word(ram, target + 0x21) < read_word(ram, actor + 0x21))
                ram[actor + 0x29] = 1;
            write_word(ram, actor, 0x2c46);
        }
        queue_surface_enemy(ram, main_rom, actor);
        register_surface_enemy(ram, actor);
        surface_enemy_hit(ram, main_rom, actor);
        break;
    }
    /* B000:BBE6: the launcher after its beam is spent. The beam path is
       probed for hits; when the launcher is destroyed, flagged by a block,
       or (types 6 and E) told through B8B4, it drops a row of C4BB debris,
       restores its health and returns to BA49. */
    case 0xbbbe6: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60);
        scroll_actor(ram, actor, 0xb71a);
        if ((int16_t)read_word(ram, actor + 0x21) <= (int16_t)read_word(bank, 0xb06a + type)) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        uint16_t row = read_word(bank, 0xb71e + type);
        int linked = type == 6 || type == 0x0e, collapse = 0;
        write_word(ram, actor + 0x42, read_word(bank, 0xb70e + type));
        write_word(ram, actor + 0x21, x + read_word(bank, row));
        write_word(ram, actor + 0x24, y + read_word(bank, (uint16_t)(row + 2)));
        write_word(ram, actor + 0x31, read_word(bank, (uint16_t)(row + 4)));
        write_word(ram, actor + 0x34, read_word(bank, (uint16_t)(row + 6)));
        for (;;) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26) || (read_word(ram, actor + 0x66) & 4)) {
                if (linked) write_word(ram, 0xb8b4, read_word(ram, 0xb8b4) | 2);
                collapse = 1;
                break;
            }
            if (linked && (read_word(ram, 0xb8b4) & 2)) {
                collapse = 1;
                break;
            }
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, actor + 0x31));
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, actor + 0x34));
            if (!decrement(ram, actor + 0x42)) break;
        }
        write_word(ram, actor + 0x21, x);
        write_word(ram, actor + 0x24, y);
        if (!collapse) break;
        write_word(ram, actor + 0x42, read_word(bank, 0xb70e + type));
        write_word(ram, actor + 0x31, read_word(bank, row));
        write_word(ram, actor + 0x34, read_word(bank, (uint16_t)(row + 2)));
        do {
            uint16_t debris = bank_b_effect(ram, actor, 0xc4bb);
            write_word(ram, debris + 0x21, x + read_word(ram, actor + 0x31));
            write_word(ram, debris + 0x24, y + read_word(ram, actor + 0x34));
            write_word(ram, debris + 0x60, type);
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(bank, (uint16_t)(row + 4)));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(bank, (uint16_t)(row + 6)));
        } while (decrement(ram, actor + 0x42));
        write_word(ram, actor + 0x66, read_word(ram, actor + 0x66) | 1);
        write_word(ram, actor + 0x26, read_word(ram, actor + 0x4e));
        write_word(ram, actor, 0xba49);
        break;
    }
    /* B000:BD06 (init), BD31 (charge), BDA2/BDB2 (glow), BE4B (beam out)
       and BEBA (fade): the emitter riding on the launcher. */
    case 0xbbd06:
    case 0xbbd31:
    case 0xbbda2:
    case 0xbbdb2:
    case 0xbbe4b:
    case 0xbbeba: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60), root;
        uint32_t state = address;
        if (state == 0xbbd06) {
            world_acquire_resource(ram, actor, 0x2f);
            ram[actor + 0x2d] = 0x3f;
            write_word(ram, actor + 0x46, read_word(bank, 0xb08a + type));
            ram[actor + 0x29] = bank[0xb0c4 + (type >> 1)];
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xbd31);
            state = 0xbbd31;
        }
        if (state == 0xbbda2) {
            write_word(ram, actor + 0x42, read_word(bank, 0xb138 + type));
            write_word(ram, actor, 0xbdb2);
            state = 0xbbdb2;
        }
        if (!freeze_follow_root(ram, actor, &root)) {
            finish_animation(ram, actor);
            break;
        }
        if (state == 0xbbd31) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
                if (sprite == 0xffff) {
                    write_word(ram, actor + 0x2a, read_word(bank, 0xb0cc + type));
                    uint16_t spark = bank_b_effect(ram, actor, 0xbf0c);
                    write_word(ram, spark + 0x60, type);
                    ram[spark + 0x2c] = ram[actor + 0x2c];
                    ram[spark + 0x29] = ram[actor + 0x29];
                    write_word(ram, actor, 0xbda2);
                } else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0xb0b6 + frame)));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        if (read_word(ram, 0x274) & 1) queue_sprite(ram, main_rom, actor, 0);
        if (state == 0xbbdb2) {
            if (decrement(ram, actor + 0x42)) {
                if (read_word(ram, actor + 0x42) & 1) break;
                uint16_t ring = bank_b_effect(ram, actor, 0xbfc9);
                write_word(ram, ring + 0x60, type);
                ram[ring + 0x2c] = ram[actor + 0x2c];
                ram[ring + 0x29] = ram[actor + 0x29];
                write_word(ram, ring + 0x42, (uint16_t)(read_word(ram, actor + 0x42) - 2) << 1);
                break;
            }
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor + 0x48, read_word(bank, 0xb19c + type));
            uint16_t probe = bank_b_effect(ram, actor, 0xc02d);
            write_word(ram, probe + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, probe + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, probe + 0x60, type);
            ram[probe + 0x29] = ram[actor + 0x29];
            write_word(ram, actor, 0xbe4b);
        } else if (state == 0xbbe4b) {
            if (decrement(ram, actor + 0x42)) break;
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t hold = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x48)));
            if (hold == 0xffff) {
                write_word(ram, actor + 0x42, 0x0c);
                write_word(ram, actor, 0xbeba);
                break;
            }
            write_word(ram, actor + 0x42, hold);
            uint16_t segment = bank_b_effect(ram, actor, 0xc389);
            write_word(ram, segment + 0x60, type);
            write_word(ram, segment + 0x62, frame);
            ram[segment + 0x2c] = ram[actor + 0x2c];
            ram[segment + 0x29] = ram[actor + 0x29];
            write_word(ram, actor + 0x40, frame + 2);
        } else if (!decrement(ram, actor + 0x42)) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            write_word(ram, parent + 0x64, 1);
            finish_animation(ram, actor);
        }
        break;
    }
    /* B000:BF0C/BF53 (drifting spark) and BFC9/BFF3 (ring): charge effects
       that last until the launcher's word 64 is set. */
    case 0xbbf0c: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60);
        ram[actor + 0x2d] = 0x3f;
        write_word(ram, actor + 0x3c, read_word(bank, 0xb07a + type));
        write_word(ram, actor + 0x46, read_word(bank, 0xb0dc + type));
        write_word(ram, actor + 0x42, read_word(bank, 0xb138 + type));
        write_word(ram, actor + 0x31, read_word(bank, 0xb0f8 + 2 * type));
        write_word(ram, actor + 0x34, read_word(bank, 0xb0fa + 2 * type));
        write_word(ram, actor + 0x38, read_word(bank, 0xb118 + 2 * type));
        write_word(ram, actor + 0x3a, read_word(bank, 0xb11a + 2 * type));
        write_word(ram, actor, 0xbf53);
        break;
    }
    case 0xbbfc9: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60);
        uint16_t row = read_word(bank, 0xb158 + type) + read_word(ram, actor + 0x42);
        ram[actor + 0x2d] = 0x3f;
        write_word(ram, actor + 0x2a, read_word(bank, 0xb148 + type));
        write_word(ram, actor + 0x31, read_word(bank, row));
        write_word(ram, actor + 0x34, read_word(bank, (uint16_t)(row + 2)));
        write_word(ram, actor, 0xbff3);
        break;
    }
    case 0xbbf53:
    case 0xbbff3: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t root;
        if (!freeze_follow_root(ram, actor, &root) || read_word(ram, root + 0x64)) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xbbf53) {
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x33, read_word(ram, actor + 0x3a));
        }
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, actor + 0x31));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, actor + 0x34));
        if (read_word(ram, 0x274) & 1) {
            if (address == 0xbbf53) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46))));
                write_word(ram, actor + 0x40, frame + 2 < 6 ? frame + 2 : 0);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (address == 0xbbf53 && read_word(ram, actor + 0x42) && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x38, 0);
            write_word(ram, actor + 0x3a, 0);
        }
        break;
    }
    /* B000:C02D (init), C072 (delay) and C087 (travel): the invisible front
       of the beam, which freezes whatever it reaches. */
    case 0xbc02d: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60);
        write_word(ram, actor + 0x3c, 0xf5fb);
        write_word(ram, actor + 0x42, read_word(bank, 0xb77e + type));
        write_word(ram, actor + 0x44, read_word(bank, 0xb7ae + type));
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(bank, 0xb75e + 2 * type));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xb760 + 2 * type));
        write_word(ram, actor + 0x38, read_word(bank, 0xb78e + 2 * type));
        write_word(ram, actor + 0x3a, read_word(bank, 0xb790 + 2 * type));
        write_word(ram, actor + 0x2a, 0x7e4);
        write_word(ram, actor, 0xc072);
        break;
    }
    case 0xbc072:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor, 0xc087);
        }
        break;
    case 0xbc087:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            if (!decrement(ram, actor + 0x44)) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x42, 1);
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        }
        freeze_victim(ram, main_rom, actor, 1);
        break;
    /* B000:C0C0 (init), C122 (forming) and C166 (holding): the ice block
       around a frozen victim. It breaks when shot enough, when the victim's
       pilot hammers the buttons, when another victim touches it, or when
       the launcher goes; C26B is the break animation. */
    case 0xbc0c0: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 0x2f);
        uint16_t shine = bank_b_effect(ram, actor, 0xc2eb);
        write_word(ram, shine + 0x21, read_word(ram, actor + 0x21));
        write_word(ram, shine + 0x24, read_word(ram, actor + 0x24));
        ram[shine + 0x2c] = ram[actor + 0x2c];
        ram[shine + 0x29] = ram[actor + 0x29];
        write_word(ram, shine + 0x60, read_word(ram, actor + 0x60));
        ram[actor + 0x2d] = 7;
        write_word(ram, actor + 0x3c, 0xf603);
        write_word(ram, actor + 0x26, 0x200);
        write_word(ram, actor + 0x36, 0x400);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, read_word(bank, 0xb7be + read_word(ram, actor + 0x60)));
        world_queue_sound(ram, 0x7a);
        write_word(ram, actor, 0xc122);
        break;
    }
    case 0xbc122: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x54, 0);
                write_word(ram, actor + 0x56, 8);
                write_word(ram, actor, 0xc166);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                if (frame == 4) freeze_victim(ram, main_rom, actor, 1);
                write_word(ram, actor + 0x44, 3);
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbc166: {
        uint16_t slot = read_word(ram, actor + 0x68), root = 0;
        int shatter = 0, pressure = 0;
        if (slot >= 4 && !read_word(ram, (uint16_t)(0xb8b8 + slot - 4)))
            shatter = 1;
        else {
            scroll_actor(ram, actor, 0xb71a);
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t depth = read_word(ram, actor + 8);
            root = read_word(ram, (uint16_t)(0x8500 + depth - 2 * (uint16_t)((uint16_t)(depth - 4) >> 1)));
            if (read_word(ram, root + 0x66) & 1)
                shatter = 1;
            else {
                if (ram[actor + 0x0b]) {
                    uint16_t edges = read_word(ram, 0xb742);
                    if ((ram[actor + 0x0b] == 2 ? edges : edges >> 8) & 0x0f) {
                        write_word(ram, actor + 0x54, 8);
                        if (!decrement(ram, actor + 0x56)) pressure = 1;
                    } else if (read_word(ram, actor + 0x54) && !decrement(ram, actor + 0x54))
                        write_word(ram, actor + 0x56, 8);
                }
                if (!pressure) pressure = freeze_victim(ram, main_rom, actor, 0);
                if (!pressure) {
                    ram[actor + 0x0d] = 3;
                    uint16_t shot = world_projectile_contact(ram, main_rom, actor);
                    if (shot && read_word(ram, shot + 0x3c) != 0x79f1) {
                        if (read_word(ram, shot + 0x0e) == 0x1b) write_word(ram, shot + 0x26, 0);
                        int16_t left = (int16_t)(read_word(ram, actor + 0x26) - read_word(ram, shot + 0x36));
                        write_word(ram, actor + 0x26, left > 0 ? (uint16_t)left : 0);
                    }
                    if (read_word(ram, actor + 0x26)) break;
                }
                if (!(read_word(ram, root + 0x66) & 2)) {
                    write_word(ram, actor + 0x26, 0x100);
                    break;
                }
                write_word(ram, root + 0x66, read_word(ram, root + 0x66) | 4);
                shatter = 1;
            }
        }
        if (shatter) {
            write_word(ram, actor + 0x26, 0);
            write_word(ram, 0xb8b2, read_word(ram, 0xb8b2) & ~(1u << (slot & 15)));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xc26b);
            uint16_t burst = bank_b_effect(ram, actor, 0xc2ab);
            write_word(ram, burst + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, burst + 0x24, read_word(ram, actor + 0x24));
        }
        break;
    }
    case 0xbc26b: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0xb81e + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0xb832 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* B000:C2AB/C2C7: the break itself, a four-tick damaging shape. */
    case 0xbc2ab:
    case 0xbc2c7:
        if (address == 0xbc2ab) {
            write_word(ram, actor + 0x3c, 0xf60b);
            write_word(ram, actor + 0x26, 0x200);
            write_word(ram, actor + 0x36, 0x400);
            write_word(ram, actor + 0x42, 4);
            write_word(ram, actor, 0xc2c7);
        } else
            scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        else if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    /* B000:C2EB (init), C305 (grow), C337 (hold) and C366 (end): the shine
       drawn over a block until the block's health reaches zero. */
    case 0xbc2eb:
        ram[actor + 0x2d] = 0x3e;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, read_word(main_rom + 0xb0000, 0xb7ee + read_word(ram, actor + 0x60)));
        write_word(ram, actor, 0xc305);
        break;
    case 0xbc305:
    case 0xbc337:
    case 0xbc366: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbc305 && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
            if (sprite == 0xffff)
                write_word(ram, actor, 0xc337);
            else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, 3);
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xbc337) {
            uint16_t block = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            if (!read_word(ram, block + 0x26)) {
                write_word(ram, actor + 0x2a, 0x8752);
                write_word(ram, actor + 0x44, 3);
                write_word(ram, actor, 0xc366);
            }
        } else if (address == 0xbc366 && !decrement(ram, actor + 0x44))
            finish_animation(ram, actor);
        break;
    }
    /* B000:C389 (init), C3D8 (extend) and C469 (standing): one segment of
       the beam. Its last frame can freeze a patch of map cells, thawed
       again when the segment goes. */
    case 0xbc389: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60), index = read_word(ram, actor + 0x62);
        uint16_t offsets = read_word(bank, 0xb1e6 + type) + 2 * index;
        ram[actor + 0x2d] = 0x3e;
        write_word(ram, actor + 0x3c, 0xf5db);
        write_word(ram, actor + 0x26, 0x300);
        write_word(ram, actor + 0x36, 0x400);
        write_word(ram, actor + 0x46, read_word(bank, (uint16_t)(read_word(bank, 0xb27e + type) + index)));
        write_word(ram, actor + 0x48, read_word(bank, (uint16_t)(0xb36a + index)));
        write_word(ram, actor + 0x31, read_word(bank, offsets));
        write_word(ram, actor + 0x34, read_word(bank, (uint16_t)(offsets + 2)));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xc3d8);
        break;
    }
    case 0xbc3d8:
    case 0xbc469: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60), index = read_word(ram, actor + 0x62), root;
        int alive = freeze_follow_root(ram, actor, &root);
        if (address == 0xbc469) {
            if (!alive || (read_word(ram, root + 0x66) & 1)) {
                if (read_word(ram, actor + 0x66) & 1) freeze_patch_tiles(ram, tiles + 0x4000, actor, 1);
                finish_animation(ram, actor);
                break;
            }
            if ((read_word(ram, root + 0x66) & 2) && !(read_word(ram, actor + 0x66) & 2)) {
                freeze_patch_tiles(ram, tiles + 0x4000, actor, 0);
                write_word(ram, actor + 0x66, read_word(ram, actor + 0x66) | 2);
            }
        } else if (!alive) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, actor + 0x31));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, actor + 0x34));
        if (address == 0xbc3d8 && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
            if (sprite == 0xffff) {
                uint16_t standing = read_word(bank, (uint16_t)(read_word(bank, 0xb3a0 + type) + index));
                if (!standing) {
                    finish_animation(ram, actor);
                    break;
                }
                write_word(ram, actor + 0x2a, standing);
                uint16_t patch = read_word(bank, (uint16_t)(read_word(bank, 0xb400 + type) + index));
                if (patch) freeze_tiles(ram, bank, tiles + 0x4000, actor, patch);
                write_word(ram, actor, 0xc469);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x48))));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* B000:C4BB/C4E9: debris dropped when the beam collapses. */
    case 0xbc4bb:
    case 0xbc4e9: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xbc4bb) {
            uint16_t type = read_word(ram, actor + 0x60);
            world_acquire_resource(ram, actor, 0x2f);
            ram[actor + 0x2d] = 0x3f;
            ram[actor + 0x29] = bank[0xb6f6 + (type >> 1)];
            write_word(ram, actor + 0x46, read_word(bank, 0xb6ac + type));
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xc4e9);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0xb6e4 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* B000:B8E0: stamp the thirty material words at B8A4 onto a 5 x 6 block
       of map cells at 2A98, then retire. */
    case 0xbb8e0: {
        if (!tiles) return 0;
        uint8_t *map = tiles + 0x4000;
        uint16_t table = 0xb8a4, row = 0x2a98;
        for (unsigned y = 0; y < 6; ++y) {
            uint16_t cell = row;
            for (unsigned x = 0; x < 5; ++x) {
                write_word(map, (uint16_t)(cell + 2), (read_word(map, (uint16_t)(cell + 2)) & 0x7ff) |
                           read_word(main_rom + 0xb0000, table));
                table += 2;
                if (x != 4) cell = (cell & 0xff00) | (uint8_t)(cell + 4);
            }
            row = (row + 0x100) & 0x3fff;
        }
        finish_animation(ram, actor);
        break;
    }
    /* B000:D13B/D149: a second sprite drawn on its parent, sharing the
       parent's hit flash. */
    case 0xbd13b:
        ram[actor + 0x2d] = 0x21;
        write_word(ram, actor + 0x2a, 0x6bf0);
        write_word(ram, actor, 0xd149);
        /* fall through */
    case 0xbd149: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (ram[parent + 0x19]) ram[actor + 0x19] = ram[parent + 0x19] + 1;
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    /* B000:92F7 (init) and 930F: releases two 9380 submarines, one per
       lane, 128 ticks apart, and retires once the camera passes word 1E. */
    case 0xb92f7:
        world_acquire_resource(ram, actor, 0x60);
        write_word(ram, actor + 0x40, 2);
        write_word(ram, actor + 0x42, 0x80);
        write_word(ram, actor, 0x930f);
        break;
    case 0xb930f:
        if (read_word(ram, actor + 0x40) && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 0x80);
            uint16_t sub = world_spawn_child(ram, actor, 0x9380);
            if (sub) {
                unsigned lane = (read_word(ram, actor + 0x4a) & 1) ? 2 : 0;
                write_word(ram, sub + 2, 0xb000);
                write_word(ram, sub + 0x21, 0x1c0);
                write_word(ram, actor + 0x4a, read_word(ram, actor + 0x4a) | (1u << lane));
                write_word(ram, sub + 0x4a, lane);
                write_word(ram, 0xb8b2, read_word(ram, 0xb8b2) & ~(1u << (lane + 4)));
                write_word(ram, sub + 0x24, read_word(main_rom + 0xb0000, 0x8d74 + lane));
                ram[sub + 0x2c] = ram[actor + 0x2c];
                decrement(ram, actor + 0x40);
            }
        }
        if (read_word(ram, 0xb700) >= read_word(ram, actor + 0x1e)) finish_animation(ram, actor);
        break;
    /* B000:9380 (init), 93D2 (cruise), 94D6 (held by B8B2) and 950E (against
       terrain): one of the paired submarines. Its slot at B8B8 + lane and
       the spawner's count and lane bit are released when it goes. */
    case 0xb9380:
        write_word(ram, actor + 0x0e, 0x1b);
        write_word(ram, actor + 0x26, 0x20);
        write_word(ram, actor + 0x36, 0x20);
        write_word(ram, actor + 0x3c, 0xf723);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x2a, 0x4032);
        write_word(ram, actor + 0x38, 0xff40);
        write_word(ram, actor + 0x50, 0x40);
        write_word(ram, actor + 0x52, 0x60);
        write_word(ram, actor + 0x60, 0x200);
        write_word(ram, actor + 0x74, 0x80);
        write_word(ram, actor + 0x5e, 0x80);
        write_word(ram, actor + 0x62, 0xe0);
        write_word(ram, (uint16_t)(0xb8b8 + read_word(ram, actor + 0x4a)), actor);
        write_word(ram, actor + 0x48, 1);
        write_word(ram, actor, 0x93d2);
        break;
    case 0xb93d2:
    case 0xb94d6:
    case 0xb950e: {
        if (!tiles) return 0;
        uint16_t lane = read_word(ram, actor + 0x4a);
        uint16_t spawner = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        int release = 0, kill = 0, contacts = address != 0xb94d6;
        uint16_t attacker = 0;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb93d2) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x < 0x40) {
            release = 1;
            contacts = 0;
        } else {
            queue_sprite(ram, main_rom, actor, 1);
            if (address == 0xb93d2 && !decrement(ram, actor + 0x48)) {
                /* B000:9DBF wake. */
                write_word(ram, actor + 0x48, 8);
                uint16_t wake = effect_slot(ram, read_word(ram, 0x9a), 0x7f73);
                uint8_t facing = ram[actor + 0x29] ^ 1;
                ram[wake + 0x29] = facing;
                write_word(ram, wake + 0x21, x + read_word(main_rom + 0xb0000, 0x902e + 2 * (facing & 1)));
                write_word(ram, wake + 0x24, read_word(ram, actor + 0x24));
            }
            paired_sub_fire(ram, main_rom, actor);
            int wall = !!(world_terrain_mask(ram, tiles + 0x4000, x - 0x18, read_word(ram, actor + 0x24)) & 0xff02);
            if (address == 0xb93d2) {
                if ((int16_t)x > 0x60 && wall) write_word(ram, actor, 0x950e);
            } else if (address == 0xb950e) {
                if (!wall) write_word(ram, actor, 0x93d2);
            } else if (read_word(ram, actor + 0x42)) {
                if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x93d2);
            } else if (!(read_word(ram, 0xb8b2) & (1u << ((lane + 4) & 15))))
                write_word(ram, actor + 0x42, 3);
        }
        if (contacts) {
            int hold = 1;
            ram[actor + 0x0d] = 0;
            attacker = world_player_contact(ram, main_rom, actor);
            if (attacker)
                kill = 1;
            else {
                ram[actor + 0x0d] = 3;
                attacker = world_projectile_contact(ram, main_rom, actor);
                if (attacker) {
                    if (blast_knockback(ram, main_rom, actor, attacker)) {
                        /* B000:9436: thrown; release the lane but stay alive. */
                        write_word(ram, spawner + 0x40, read_word(ram, spawner + 0x40) + 1);
                        write_word(ram, spawner + 0x4a, read_word(ram, spawner + 0x4a) & ~(1u << (lane & 15)));
                        write_word(ram, (uint16_t)(0xb8b8 + lane), 0);
                        break;
                    }
                    if (read_word(ram, attacker + 0x0e) != 0x1b) {
                        int16_t theirs = (int16_t)(read_word(ram, attacker + 0x26) - read_word(ram, actor + 0x36));
                        write_word(ram, attacker + 0x26, theirs > 0 ? (uint16_t)theirs : 0);
                        int16_t mine = (int16_t)(read_word(ram, actor + 0x26) - read_word(ram, attacker + 0x36));
                        write_word(ram, actor + 0x26, (uint16_t)mine);
                        if (mine <= 0) {
                            kill = 1;
                            hold = 0;
                        } else
                            ram[actor + 0x19] = (ram[actor + 0x19] & 1) + 10;
                    }
                }
            }
            if (!kill && hold && (read_word(ram, 0xb8b2) & (1u << ((lane + 4) & 15))))
                write_word(ram, actor, 0x94d6);
        }
        if (kill) {
            world_award_score(ram, main_rom, actor, attacker);
            /* 6B36: the 6B2E blast without its sound. */
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, 0x74c3);
            write_word(ram, blast + 0x4a, 0x7675);
            ram[blast + 0x2c] = 0x0a;
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
            world_queue_sound(ram, 0x98);
            release = 1;
        }
        if (release) {
            write_word(ram, spawner + 0x40, read_word(ram, spawner + 0x40) + 1);
            write_word(ram, spawner + 0x4a, read_word(ram, spawner + 0x4a) & ~(1u << (lane & 15)));
            write_word(ram, (uint16_t)(0xb8b8 + lane), 0);
            finish_animation(ram, actor);
        }
        break;
    }
    /* B000:9036: spawn a formation from the six-byte rows selected by the
       variant, then retire. */
    case 0xb9036: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t flags = read_word(ram, actor + 0x1e);
        uint16_t row = read_word(bank, 0x8d98 + (flags & 0x0e));
        unsigned kind = (flags & 1) << 2;
        for (;;) {
            write_word(ram, actor + 0x40, row);
            uint16_t child = world_spawn_child(ram, actor, read_word(bank, 0x8e00 + kind));
            if (!child) break;
            write_word(ram, child + 2, read_word(bank, 0x8e02 + kind));
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21) + read_word(bank, row));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + read_word(bank, (uint16_t)(row + 2)));
            write_word(ram, child + 0x1e, read_word(bank, (uint16_t)(row + 4)));
            row += 6;
            if (read_word(bank, row) == 0xffff) break;
        }
        finish_animation(ram, actor);
        break;
    }
    /* B000:A3BA (init) and A3E7: a timed animation whose frames carry a
       contact shape; B000:A466 is its second shape, read through the 8500
       slot of its own depth exactly as the original does. */
    case 0xba3ba:
    case 0xba3e7: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xba3ba) {
            ram[actor + 0x2d] = 0x28;
            write_word(ram, actor + 0x26, 0x7f00);
            write_word(ram, actor + 0x36, 0x180);
            uint16_t second = bank_b_effect(ram, actor, 0xa466);
            write_word(ram, second + 0x26, 0x7f00);
            write_word(ram, second + 0x36, 0x180);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xa3e7);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, (uint16_t)(0x9fd0 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0x9ff2 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t shape = read_word(bank, (uint16_t)(0xa044 + 2 * (uint16_t)(read_word(ram, actor + 0x40) - 2)));
        if (shape) {
            write_word(ram, actor + 0x3c, shape);
            ram[actor + 0x0d] = 1;
            world_player_contact(ram, main_rom, actor);
            if (shape != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    case 0xba466: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t owner = read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8)));
        if (ram[owner + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, owner + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, owner + 0x24));
        uint16_t shape = read_word(bank, (uint16_t)(0xa046 + 2 * (uint16_t)(read_word(ram, owner + 0x40) - 2)));
        if (shape) {
            write_word(ram, actor + 0x3c, shape);
            ram[actor + 0x0d] = 1;
            world_player_contact(ram, main_rom, actor);
            if (shape != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    /* B000:B92B (init), B99D (approach), B9D4 (open), BA49 (armed), BAF8
       (wreck), BB1D/BB51 (firing) and BB9F (sustain): a launcher that
       releases a BD06 child. The BBE6 state it enters afterwards is not
       recovered yet. */
    case 0xbb92b: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t flags = read_word(ram, actor + 0x1e);
        uint16_t type = flags & 0x0e;
        uint16_t cfg = read_word(bank, (uint16_t)(0xb854 + (flags & 0x20) + read_word(ram, 0x8c)));
        write_word(ram, actor + 0x60, type);
        world_acquire_resource(ram, actor, read_word(bank, 0xae66 + type));
        write_word(ram, actor + 0x0e, 0x16);
        ram[actor + 0x2d] = 0x3f;
        write_word(ram, actor + 0x3c, 0xf5d3);
        write_word(ram, actor + 0x26, read_word(bank, (uint16_t)(0xb894 + cfg)));
        write_word(ram, actor + 0x36, read_word(bank, (uint16_t)(0xb894 + cfg)));
        write_word(ram, actor + 0x52, read_word(bank, (uint16_t)(0xb896 + cfg)));
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x2a, read_word(bank, 0xae56 + type));
        write_word(ram, actor + 0x46, read_word(bank, 0xafa6 + type));
        write_word(ram, actor + 0x42, read_word(bank, 0xb844 + (ram[actor + 0x1f] & 0x0e)));
        write_word(ram, actor, 0xb99d);
        break;
    }
    case 0xbb99d:
    case 0xbb9d4:
    case 0xbba49:
    case 0xbbaf8:
    case 0xbbb1d:
    case 0xbbb51:
    case 0xbbb9f: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t type = read_word(ram, actor + 0x60);
        uint32_t state = address;
        int check = 0, probe = 0;
        if (state == 0xbbb1d) {
            if (type == 6) write_word(ram, 0xb8b4, read_word(ram, 0xb8b4) & ~1u);
            write_word(ram, actor + 0x66, 0);
            write_word(ram, actor + 0x64, 0);
            uint16_t child = bank_b_effect(ram, actor, 0xbd06);
            write_word(ram, child + 0x60, type);
            world_queue_sound(ram, 0x79);
            write_word(ram, actor, 0xbb51);
            state = 0xbbb51;
        }
        scroll_actor(ram, actor, 0xb71a);
        if ((state == 0xbb99d || state == 0xbba49 || state == 0xbbaf8) && read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (state == 0xbb99d) {
            queue_sprite(ram, main_rom, actor, 0);
            if (read_word(ram, actor + 0x42) >= read_word(ram, actor + 0x21)) {
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xb9d4);
            }
            break;
        }
        if (state == 0xbbaf8) {
            queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        if (state == 0xbb9d4) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, (uint16_t)(frame + read_word(ram, actor + 0x46)));
                if (sprite == 0xffff)
                    write_word(ram, actor, 0xba49);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, (uint16_t)(0xb022 + frame)));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
            check = read_word(ram, actor + 0x40) >= 8;
        } else if (state == 0xbba49) {
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t gate = read_word(ram, 0xb8b4);
            int fire;
            if (type == 0x0e && !(gate & 0x8000))
                fire = gate & 1;
            else {
                fire = !decrement(ram, actor + 0x50);
                if (fire) write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            }
            if (fire) {
                write_word(ram, actor + 0x46, read_word(bank, 0xb042 + type));
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                if (type == 6) write_word(ram, 0xb8b4, read_word(ram, 0xb8b4) | 1);
                write_word(ram, 0xb8b4, read_word(ram, 0xb8b4) & ~2u);
                write_word(ram, actor, 0xbb1d);
            }
            check = 1;
        } else if (state == 0xbbb51) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t hold = read_word(bank, (uint16_t)(0xb0b6 + frame));
                if (hold == 0xffff) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor, 0xbb9f);
                } else {
                    write_word(ram, actor + 0x44, hold);
                    write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)((frame & 2) + read_word(ram, actor + 0x46))));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
            probe = 1;
        } else {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, (uint16_t)((frame & 2) + read_word(ram, actor + 0x46))));
            write_word(ram, actor + 0x40, frame + 2);
            queue_sprite(ram, main_rom, actor, 0);
            if (read_word(ram, actor + 0x64)) {
                write_word(ram, actor + 0x3c, read_word(bank, 0xb6fe + type));
                write_word(ram, actor + 0x4e, read_word(ram, actor + 0x26));
                write_word(ram, actor + 0x26, 0x300);
                write_word(ram, actor + 0x66, read_word(ram, actor + 0x66) | 2);
                write_word(ram, actor, 0xbbe6);
            }
            probe = 1;
        }
        if (probe) {
            /* BB8E: any shot touching the open launcher is cancelled. */
            ram[actor + 0x0d] = 3;
            uint16_t shot = world_projectile_contact(ram, main_rom, actor);
            if (shot) write_word(ram, shot + 0x26, 0);
        }
        if (check) {
            ram[actor + 0x0d] = 0;
            uint16_t hit = world_projectile_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26)) {
                if (!hit) {
                    world_dispatch_detail = "launcher destroyed without a projectile; 6237 reads leftover BX";
                    return 0;
                }
                world_award_score(ram, main_rom, actor, hit);
                spawn_kill_blast(ram, actor);
                write_word(ram, actor + 0x2a, read_word(bank, 0xb032 + type));
                ram[actor + 0x0a] = 2;
                if (type == 6) write_word(ram, 0xb8b4, read_word(ram, 0xb8b4) | 0x8000);
                write_word(ram, actor, 0xbaf8);
            }
        }
        break;
    }
    case 0xb921a: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t variant = read_word(ram, actor + 0x1e);
        uint16_t script = read_word(bank, 0x8e18 + (variant & 14));
        if (read_word(bank, script) > 2) return 0;
        world_acquire_resource(ram, actor, 0x60);
        write_word(ram, actor + 0xe, 0x11);
        write_word(ram, actor + 0x26, 0x20);
        write_word(ram, actor + 0x36, 0x20);
        write_word(ram, actor + 0x3c, read_word(bank, 0x9234));
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x2a, read_word(bank, 0x923d));
        uint16_t config = 0x8f86 + read_word(bank, 0x8f46 + (variant & 0x20) + read_word(ram, 0x88));
        write_word(ram, actor + 0x52, read_word(bank, config + 4));
        write_word(ram, actor + 0x54, read_word(bank, config + 8));
        write_word(ram, actor + 0x58, read_word(bank, config + 8));
        write_word(ram, actor + 0x5a, read_word(bank, config + 10));
        write_word(ram, actor + 0x5c, read_word(bank, config + 10));
        write_word(ram, actor + 0x60, read_word(bank, config + 6));
        write_word(ram, actor + 0x74, read_word(bank, config));
        write_word(ram, actor + 0x5e, read_word(bank, config));
        write_word(ram, actor + 0x62, read_word(bank, config + 2));
        write_word(ram, actor + 0x50, read_word(bank, 0x9032 + ((variant & 0x20) >> 4)));
        write_word(ram, actor + 0x6c, read_word(bank, 0x8e08 + (variant & 14)));
        begin_linear_script(ram, main_rom, actor, script);
        write_word(ram, actor, 0x92b6);
        break;
    }
    case 0xb92b6:
    case 0xb92ce: {
        int fire = 1;
        if (address == 0xb92b6) {
            scroll_actor(ram, actor, 0xb71a);
            fire = read_word(ram, actor + 0x21) < read_word(ram, actor + 0x6c);
            if (fire) write_word(ram, actor, 0x92ce);
        } else {
            if (read_word(ram, actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
            /* B000:92D4: D6B7 steps the flight script; at its end the
               enemy bursts (6B2E) and retires. */
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) {
                world_dispatch_detail = "flight script kind not recovered";
                return 0;
            }
            if (step < 0) {
                spawn_kill_blast(ram, actor);
                finish_animation(ram, actor);
                break;
            }
        }
        if (fire) update_cruiser_fire(ram, main_rom, actor, read_word(ram, actor + 0x50) ? 0x50 : 0x54);
        draw_cruiser_and_check_hits(ram, main_rom, actor);
        break;
    }
    case 0xb914c:
    case 0xb9601:
        if (!tiles || !update_cruising_enemy(ram, main_rom, tiles + 0x4000, actor))
            return 0;
        break;
    case 0xbd175:
    case 0xbd1cd:
    case 0xbd1db: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xbd175) {
            ram[actor + 0x2d] = 33;
            write_word(ram, actor + 0x3c, read_word(bank, 0xd17c));
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 128);
            unsigned angle = ram[actor + 0x28];
            unsigned offset = (angle & 0xf0) >> 2;
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(bank, 0xcbdc + offset));
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xcbde + offset));
            write_word(ram, actor + 0x4c, read_word(bank, 0xcbac + (offset >> 1)));
            ram[actor + 0x29] = bank[0xcbcc + (offset >> 2)];
            for (unsigned axis = 0; axis < 2; ++axis) {
                int16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2*angle);
                int16_t speed = read_word(ram, actor + 0x2e);
                write_word(ram, actor + 0x38 + 2*axis, (unit * speed) >> 8);
            }
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x42, 5);
            write_word(ram, actor, 0xd1cd);
        } else {
            if (address == 0xbd1cd && !decrement(ram, actor + 0x42)) {
                ram[actor + 0x2d] = 4;
                write_word(ram, actor, 0xd1db);
            }
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (outside_pickup_area(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, 0xcb8e + frame + read_word(ram, actor + 0x4c)));
            write_word(ram, actor + 0x44, 2);
            write_word(ram, actor + 0x40, frame + 2 >= 6 ? 0 : frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        unsigned terrain = world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24));
        if (terrain & 0xff0a) finish_animation(ram, actor);
        else {
            ram[actor + 0x0d] = 2;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        }
        break;
    }
    case 0xbccac: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t flags = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, read_word(bank, 0xccad));
        world_acquire_resource(ram, actor, read_word(bank, 0xccbd) + ((flags >> 9) & 1));
        write_word(ram, actor + 0x0e, 24);
        ram[actor + 0x2d] = 33;
        write_word(ram, actor + 0x3c, read_word(bank, 0xccd0));
        ram[actor + 0x0c] = 8;
        write_word(ram, actor + 0x2a, read_word(bank, 0xccd9));
        write_word(ram, actor + 0x46, read_word(bank, 0xcb6e + 2 * (flags & 3)));
        unsigned side = (flags & 4) >> 1;
        ram[actor + 0x29] = side;
        write_word(ram, actor + 0x48, read_word(bank, 0xcb82 + side));
        write_word(ram, actor + 0x42, 8);
        uint16_t config = 0xcc5c + read_word(bank, 0xcc1c + (flags & 32) + read_word(ram, 0x88));
        write_word(ram, actor + 0x26, read_word(bank, config));
        write_word(ram, actor + 0x36, read_word(bank, config));
        write_word(ram, actor + 0x50, 32);
        write_word(ram, actor + 0x52, read_word(bank, config + 6));
        write_word(ram, actor + 0x56, read_word(bank, config + 2));
        write_word(ram, actor + 0x5a, read_word(bank, config + 4));
        write_word(ram, actor + 0x2e, read_word(bank, config + 8));
        if (flags & 256) {
            uint16_t child = bank_b_effect(ram, actor, 0xd13b);
            ram[child + 0x2c] = ram[actor + 0x2c];
        }
        write_word(ram, actor, 0xcd61);
        break;
    }
    case 0xbcd61:
        scroll_actor(ram, actor, 0xb71a);
        if (!player_near_turret(ram, main_rom, actor) && read_word(ram, actor + 0x21) <= read_word(ram, actor + 0x46)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            turret_effect(ram, main_rom + 0xb0000, actor, 1);
            write_word(ram, actor, 0xcdc8);
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    case 0xbcea8:
    case 0xbcfd4:
    case 0xbd079: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xbd079) {
            queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        if (address == 0xbcea8) {
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x42, 8);
                uint16_t player = world_enemy_target(ram, actor);
                if (player != 0x8480) {
                    int16_t dx = read_word(ram, player + 0x21) - read_word(ram, actor + 0x21);
                    int16_t dy = read_word(ram, player + 0x24) - read_word(ram, actor + 0x24);
                    unsigned x = abs((int)dx), y = abs((int)dy);
                    unsigned direction = dx < 0 ? 128 : 0;
                    if (x || y) {
                        unsigned octant = y > x ? main_rom[0xd58e + 256*x/y] : main_rom[0xd48d + 256*y/x];
                        direction += (dx < 0) != (dy < 0) ? -octant : octant;
                    }
                    unsigned target = (direction + 8) & 0xf0;
                    write_word(ram, actor + 0x60, target);
                    int angle = read_word(ram, actor + 0x48);
                    if (target != (unsigned)angle) {
                        unsigned side = read_word(ram, actor + 0x1e) & 4;
                        if ((uint8_t)(target - angle) < (uint8_t)(angle - target)) {
                            int high = (int16_t)read_word(bank, 0xcb88 + side);
                            angle += 16;
                            if (angle > high) angle = high;
                        } else {
                            int low = (int16_t)read_word(bank, 0xcb86 + side);
                            angle -= 16;
                            if (angle < low) angle = low;
                        }
                        write_word(ram, actor + 0x48, angle);
                        uint16_t index = read_word(bank, 0xcb4c + ((uint16_t)angle >> 3));
                        write_word(ram, actor + 0x2a, read_word(bank, 0xcb3a + index));
                    }
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
            if (!player_near_turret(ram, main_rom, actor) && !read_word(ram, 0x286) && !decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x54, 1);
                write_word(ram, actor + 0x58, read_word(ram, actor + 0x5a));
                write_word(ram, actor, 0xcfd4);
            }
        } else {
            queue_sprite(ram, main_rom, actor, 1);
            if (!decrement(ram, actor + 0x54)) {
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
                if (!decrement(ram, actor + 0x58)) {
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                    write_word(ram, actor, 0xcea8);
                }
                if (read_word(ram, actor + 0x24) >= 0x90) {
                    unsigned facing = ram[actor + 0x48];
                    unsigned offset = (facing & 0xf0) >> 2;
                    uint16_t flash = effect_slot(ram, actor, 0xa2c0);
                    write_word(ram, flash + 0x31, read_word(bank, 0xcbdc + offset));
                    write_word(ram, flash + 0x34, read_word(bank, 0xcbde + offset));
                    ram[flash + 0x2d] = ram[actor + 0x2d];
                    uint16_t shot = bank_b_effect(ram, actor, 0xd175);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                    ram[shot + 0x28] = facing;
                    ram[shot + 0x2c] = ram[actor + 0x14];
                    world_queue_sound(ram, 0x62);
                }
            }
        }
        turret_contact(ram, main_rom, actor);
        break;
    }
    case 0xbce66:
    case 0xbcdc8: {
        const uint8_t *bank = main_rom + 0xb0000;
        int effect = address == 0xbce66;
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, frame + (effect ? read_word(ram, actor + 0x4a) : 0xca9c));
            if (sprite == 0xffff) {
                if (effect) {
                    finish_animation(ram, actor);
                    break;
                }
                write_word(ram, actor, 0xcea8);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, frame + (effect ? read_word(ram, actor + 0x4c) : 0xcac0)));
                write_word(ram, actor + 0x40, frame + 2);
                if (!effect && frame == 22) turret_effect(ram, bank, actor, 0);
            }
        }
        if (!effect && read_word(ram, actor + 0x40) >= 24) {
            if (turret_contact(ram, main_rom, actor)) break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb98e4:
    case 0xb990c:
    case 0xb9926: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb98e4) {
            world_acquire_resource(ram, actor, read_word(bank, 0x98e5));
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 1);
            write_word(ram, actor + 0x3c, read_word(bank, 0x98f9));
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x42, 32);
            write_word(ram, actor, 0x990c);
        } else {
            if (address == 0xb990c && !decrement(ram, actor + 0x42)) {
                uint16_t child = effect_slot(ram, actor, read_word(bank, 0x9915));
                write_word(ram, child + 0x2c, 3);
                write_word(ram, actor, 0x9926);
            }
            scroll_actor(ram, actor, 0xb71a);
        }
        uint16_t velocity = read_word(ram, actor + 0x3a);
        unsigned fraction = ram[actor + 0x23] + (velocity & 255);
        ram[actor + 0x23] = (uint8_t)fraction;
        uint16_t y = read_word(ram, actor + 0x24) + (int8_t)(velocity >> 8) + (fraction >> 8);
        write_word(ram, actor + 0x24, y);
        uint16_t limit = ram[0x26e] & 2 ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        if (y > limit) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t frame = read_word(ram, actor + 0x40) + 0x80;
        if (frame >= 0x600) frame = 0;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, 0x8d1c + 2 * (frame >> 8)));
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t x = read_word(ram, actor + 0x21);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (material == 3) {
            spawn_surface_splash(ram, main_rom, actor, x, y);
            finish_animation(ram, actor);
        } else {
            enemy_torpedo_contact(ram, main_rom, actor, 1u << (material & 15));
        }
        break;
    }
    case 0xb9742: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x44, 6);
            uint16_t index = read_word(ram, actor + 0x40);
            unsigned direction = read_word(ram, actor + 0x3a) >> 15;
            uint16_t row = read_word(bank, 0x8ce4 + 2 * direction);
            uint16_t sprite = read_word(bank, row + index);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x3a, 0);
                write_word(ram, actor, 0x9601);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x3c, read_word(bank, 0x8d40 + index));
                if (index == 12) ram[actor + 0x29] ^= 1;
                uint16_t speed = read_word(bank, 0x8d28 + index);
                write_word(ram, actor + 0x38, ram[actor + 0x6a] & 1 ? speed : -speed);
                write_word(ram, actor + 0x40, index + 2);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        uint16_t speed_y = read_word(ram, actor + 0x3a);
        uint16_t sensor = read_word(ram, actor + 0x24) + read_word(bank, 0x8d60 + 2 * (speed_y >> 15));
        if (!(world_terrain_mask(ram, tiles + 0x4000, x, sensor) & 0xff0a))
            move_actor_axis(ram, actor + 0x23, speed_y);
        if (x < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        cruiser_contacts(ram, main_rom, actor);
        break;
    }
    case 0xb97ee:
    case 0xb9825: {
        if (!tiles) return 0;
        if (address == 0xb97ee) {
            world_acquire_resource(ram, actor, 0x5e);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 1);
            write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x9803));
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x9808));
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x42, 4);
            if (!(ram[actor + 0x29] & 1))
                write_word(ram, actor + 0x38, -read_word(ram, actor + 0x38));
            write_word(ram, actor, 0x9825);
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        uint16_t speed = read_word(ram, actor + 0x38);
        unsigned fraction = ram[actor + 0x20] + (speed & 255);
        ram[actor + 0x20] = (uint8_t)fraction;
        uint16_t x = read_word(ram, actor + 0x21) + (int8_t)(speed >> 8) + (fraction >> 8);
        uint16_t y = read_word(ram, actor + 0x24);
        write_word(ram, actor + 0x21, x);
        if (x < 0x40 || x > 0x1c0) {
            finish_animation(ram, actor);
            /* 9825 reaches here through 4311 and 5E9B only. */
            if (address == 0xb9825) keep_si();
            break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 4);
            uint16_t smoke = effect_slot(ram, read_word(ram, 0x9a), 0xbe41);
            write_word(ram, smoke + 0x21, x);
            write_word(ram, smoke + 0x24, y);
            write_word(ram, smoke + 0x2c, 3);
        }
        uint16_t successor = read_word(ram, actor + 4);
        enemy_torpedo_contact(ram, main_rom, actor, world_terrain_mask(ram, tiles + 0x4000, x, y));
        if (read_word(ram, actor) != 0x1b4c)
            /* B000:98A2. D2B7 loaded SI with the shape; D237 and D3D0 keep it. */
            leave_si(read_word(ram, actor + 0x3c));
        else if (read_word(ram, actor + 4) != successor)
            /* 6ACB found a slot: 1769 leaves the previous successor in SI. */
            leave_si(successor);
        break;
    }
    case 0xbeae:
    case 0xbec6: {
        if (address == 0xbeae) {
            ram[actor + 0x2d] = 0x30;
            write_word(ram, actor + 0x3c, 0xffff);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xbec6);
        }
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom, 0xbe35 + (frame & 6)));
            write_word(ram, actor + 0x44, 3);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbe41:
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, read_word(main_rom, 0xbe3d + 2 * (ram[actor + 0x1e] & 1)));
        write_word(ram, actor, 0xbe6b);
        break;
    case 0xbe6b:
        scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, 0xbe01 + frame + read_word(ram, actor + 0x46));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(main_rom, 0xbe25 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7dd3b:
    case 0x7dd71: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned flags = ram[actor + 0x1f];
        unsigned schedule = read_word(bank, 0xdb7a + ((flags >> 1) & 14));
        unsigned kind = read_word(bank, (uint16_t)(address + 13));
        write_word(ram, actor + 0x60, read_word(bank, (uint16_t)(address + 3)));
        write_word(ram, actor + 0x62, 0xb000);
        write_word(ram, actor + 0x0e, kind);
        write_word(ram, actor + 0x44, (flags & 32) | read_word(bank, 0xdb8a + kind));
        write_word(ram, actor + 0x42, read_word(bank, 0xdb42 + ((flags << 1) & 6)));
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x52, read_word(bank, 0xdb4a + schedule));
        uint16_t burst = read_word(bank, 0xdb4c + schedule);
        uint16_t delay = read_word(bank, 0xdb4e + schedule);
        write_word(ram, actor + 0x54, burst);
        write_word(ram, actor + 0x56, burst);
        write_word(ram, actor + 0x58, delay);
        write_word(ram, actor + 0x5a, delay);
        write_word(ram, actor + 0x48, read_word(ram, 0xb700) + 16 * ram[actor + 0x1e]);
        write_word(ram, actor, 0xdc27);
        break;
    }
    case 0x15d98: {
        uint16_t successor = read_word(ram, actor + 4);
        world_stage_spawns(ram, main_rom, actor);
        /* Only 1769 loads SI here: each successful spawn leaves the spawner's
           previous successor, which the new child also stores as its link. */
        uint16_t newest = read_word(ram, actor + 4);
        if (newest == successor) keep_si();
        else leave_si(read_word(ram, newest + 4));
        break;
    }
    case 0xb909f:
        world_kind11_init(ram, main_rom, actor);
        break;
    case 0xb9540:
        world_kind25_init(ram, main_rom, actor);
        break;
    case 0xdb86:
        if (!tiles) return 0;
        write_word(ram, actor + 0x3c, 0xfab3);
        write_word(ram, actor + 0x50, read_word(main_rom, 0xdd13 + (ram[actor + 0x1e] & 6)));
        write_word(ram, actor + 0x52, read_word(main_rom, 0xdd1b + (ram[actor + 0x1f] & 30)));
        write_word(ram, actor, 0xdbaf);
        scenery_init_si = read_word(ram, actor + 0x1e) & 6;
        update_scenery(ram, main_rom, tiles, actor, 1);
        break;
    /* 0:DAA9 (init) and DACD: far-layer scenery two blocks wide, animated
       from the list at 1188h. */
    case 0xdaa9:
    case 0xdacd: {
        if (!tiles) return 0;
        if (address == 0xdaa9) {
            uint16_t flags = read_word(ram, actor + 0x1e);
            write_word(ram, actor + 0x50, read_word(main_rom, 0xfea7 + (flags & 0x0e)));
            write_word(ram, actor + 0x52, read_word(main_rom, 0xdd0f + ((flags >> 8) & 2)));
            write_word(ram, actor, 0xdacd);
            leave_si(flags & 0x0e);         /* DAAC mov ix,bw; and ix,0Eh */
        } else
            keep_si();
        scroll_actor(ram, actor, 0xb72a);
        uint16_t bottom = read_word(ram, 0x26e) & 2 ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        if (read_word(ram, actor + 0x21) < 0x3f || read_word(ram, actor + 0x24) > bottom) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t tick = read_word(ram, 0x274);
        if (tick & 7) break;
        uint16_t sequence = (tick & 0x38) + read_word(ram, actor + 0x52) + 0x1188;
        uint16_t dest = read_word(ram, actor + 0x50);
        write_word(ram, 0xb89a, 0x0201);
        write_word(ram, 0xb89c, dest);
        for (unsigned strip = 0; strip < 2; strip++) {
            unsigned segment = read_word(main_rom + 0xa0000, (uint16_t)(sequence + 2 * strip)) & 0xfffc;
            const uint8_t *source = main_rom + ((uint32_t)segment << 4);
            uint16_t row_dest = dest;
            for (unsigned row = 0; row < 4; row++) {
                for (unsigned col = 0; col < 16; col += 4)
                    write_word(tiles, 0x4000 + row_dest + col, read_word(source, 16 * row + col));
                row_dest = (row_dest + 0x100) & 0x3fff;
            }
            if (strip != 1) {
                dest = (dest & 0xff00) | ((dest + 16) & 0xff);
                write_word(ram, 0xb89c, dest);
            }
        }
        leave_si(0x40);
        break;
    }
    /* 0:DB28 (init) and DB64: DA4F scenery that drops its animation offset
       once its parent's byte 0A has bit 1 set. */
    case 0xdb28: {
        if (!tiles) return 0;
        unsigned stage = 2 * read_word(ram, 0x84);
        uint16_t list = read_word(main_rom, 0xfe5f + stage);
        write_word(ram, actor + 0x50, read_word(main_rom, (uint16_t)(list + (ram[actor + 0x1e] & 6))));
        write_word(ram, actor + 0x52, read_word(main_rom, 0xfe87 + (ram[actor + 0x1f] & 30)));
        write_word(ram, actor + 0x54, read_word(main_rom, 0xdcff + stage));
        write_word(ram, actor, 0xdb64);
    }
        /* fall through */
    case 0xdb64: {
        if (!tiles) return 0;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a] & 2) {
            write_word(ram, actor + 0x52, 0);
            write_word(ram, actor, 0xda4f);
        }
        update_scenery(ram, main_rom, tiles, actor, 0);
        si_known = 0;                       /* not verified for this entry */
        break;
    }
    /* 0:DBF3 (init) and DBFD: every seventh tick redraw a row of sixteen
       blocks on the first layer from an eight-frame animation. */
    case 0xdbf3:
        write_word(ram, actor + 0x42, 7);
        write_word(ram, actor, 0xdbfd);
        /* fall through */
    case 0xdbfd:
        if (!tiles) return 0;
        keep_si();
        if (!decrement(ram, actor + 0x42)) {
            uint16_t source = 0x3118 + read_word(ram, actor + 0x52), dest = 0x2430;
            write_word(ram, 0xb89a, 0x1001);
            write_word(ram, 0xb89c, dest);
            for (unsigned strip = 0; strip < 16; strip++) {
                world_draw_tile_block(tiles, main_rom, 0, dest, read_word(main_rom + 0xa0000, source));
                source += 2;
                if (strip != 15) {
                    dest = (dest & 0xff00) | ((dest + 16) & 255);
                    write_word(ram, 0xb89c, dest);
                }
            }
            write_word(ram, actor + 0x52, (read_word(ram, actor + 0x52) + 0x20) & 0xe0);
            write_word(ram, actor + 0x42, 7);
            leave_si(0x40);                 /* every 37EB block copy ends there */
        }
        break;
    case 0xda13: {
        if (!tiles) return 0;
        unsigned stage = 2 * read_word(ram, 0x84);
        uint16_t row = read_word(main_rom, 0xfe5f + stage);
        write_word(ram, actor + 0x50, read_word(main_rom, row + (ram[actor + 0x1e] & 6)));
        write_word(ram, actor + 0x52, read_word(main_rom, 0xfe87 + (ram[actor + 0x1f] & 30)));
        write_word(ram, actor + 0x54, read_word(main_rom, 0xdcff + stage));
        write_word(ram, actor, 0xda4f);
        scenery_init_si = read_word(ram, actor + 0x1e) & 6;
    }
        /* fall through */
    case 0xda4f:
    case 0xdbaf:
        if (!tiles)
            return 0;
        update_scenery(ram, main_rom, tiles, actor, address == 0xdbaf);
        break;
    case 0x7056: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0xa] & 1) { finish_animation(ram, actor); break; }
        uint16_t segment = read_word(ram, actor + 0x62);
        if (segment > 0xf000) return 0;
        const uint8_t *bank = main_rom + ((uint32_t)segment << 4);
        uint16_t table = read_word(ram, actor + 0x60), step = read_word(ram, actor + 0x40);
        uint16_t type = read_word(bank, table + step);
        if (type == 0xffff && read_word(ram, actor + 0x1e)) { step = 0; type = read_word(bank, table); }
        if (read_word(ram, actor + 0x42) == 1 && type != 0xffff && (type > 32 || (type & 1))) return 0;
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (decrement(ram, actor + 0x42)) break;
        write_word(ram, actor + 0x42, read_word(ram, actor + 0x4c));
        write_word(ram, actor + 0x40, step + 6);
        if (type == 0xffff) { finish_animation(ram, actor); break; }
        uint16_t setup = read_word(main_rom, 0x7643 + type);
        uint16_t effect = effect_slot(ram, actor, read_word(main_rom, setup + 4));
        write_word(ram, effect + 0x4c, read_word(main_rom, setup + 14));
        write_word(ram, effect + 0x4a, read_word(main_rom, setup + 19));
        unsigned has_palette = type < 8 || type >= 26;
        unsigned attribute = has_palette ? 28 : 29;
        if (has_palette) ram[effect + 0x2c] = main_rom[setup + 24];
        else write_word(ram, effect + 0x44, read_word(main_rom, setup + 24));
        uint8_t order = main_rom[setup + attribute] + ram[actor + 0x2d];
        ram[effect + 0x2d] = order < 64 ? order : 63;
        write_word(ram, effect + 0x56, read_word(main_rom, setup + attribute + 4));
        write_word(ram, effect + 0x58, read_word(main_rom, setup + attribute + 9));
        if (type < 8 || type == 16 || type == 20 || type == 28)
            world_queue_sound(ram, read_word(main_rom, setup + attribute + 12));
        if (type < 8 && !(effect & 0x300)) write_word(ram, effect + 0x44, 0x5d);
        write_word(ram, effect + 0x31, read_word(bank, table + step + 2));
        write_word(ram, effect + 0x34, read_word(bank, table + step + 4));
        break;
    }
    /* 0:6F34 places the effect at its parent plus the 31/34 offsets, then
       runs into the 6F54 initializer. */
    case 0x6f34: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + read_word(ram, actor + 0x31));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
        address = 0x6f54;
    }
        /* fall through */
    case 0x6f54:
    case 0x6efe:
        if (read_word(ram, actor + 0x44))
            world_acquire_resource(ram, actor, read_word(ram, actor + 0x44));
        if (address == 0x6efe) ram[actor + 0x2c] |= ram[actor + 0x4e];
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0);
        ram[actor + 0x48] = 1;
        write_word(ram, actor + 0x64, 16);
        write_word(ram, actor + 0x66, 16);
        write_word(ram, actor + 0x42, 8);
        write_word(ram, actor, address == 0x6efe ? 0x6f84 : 0x6fb1);
        break;
    case 0x6f84:
    case 0x6fb1:
        update_radial_effect(ram, main_rom, actor, address == 0x6f84);
        break;
    case 0x7f73:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x3e;
        write_word(ram, actor, 0x7f81);
        keep_si();
        break;
    case 0x7f81: {
        uint16_t index = 2 * ram[actor + 0x41];
        uint16_t sprite = read_word(main_rom, 0x781f + index);
        scroll_actor(ram, actor, 0xb71a);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x80);
        if (sprite == 0xffff) {
            finish_animation(ram, actor);
            keep_si();
        } else {
            write_word(ram, actor + 0x2a, sprite);
            queue_sprite(ram, main_rom, actor, 0);
            si_after_sprite();
        }
        break;
    }
    /* 07FB9: parent-attached effect. It follows the parent actor for 20
       ticks (07FD1), then drifts with the b716 scroll delta (07FF5) while
       stepping the sprite/duration tables at 7835/7857. */
    case 0x7fb9:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x3e;
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor + 0x42, 0x14);
        write_word(ram, actor, 0x7fd1);
        keep_si();
        break;
    case 0x7fd1:
    case 0x7ff5: {
        int detached = address == 0x7ff5;
        if (!detached) {
            uint16_t parent = read_word(ram, actor + 0x7c);
            uint16_t offset = read_word(main_rom, (uint16_t)(0x7955 + read_word(ram, actor + 0x68)));
            write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + offset);
            write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + 4);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor, 0x7ff5);
                detached = 1;
            }
        }
        if (detached) scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0x7835 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                keep_si();
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x58, read_word(main_rom, (uint16_t)(0x7857 + frame)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        si_after_sprite();
        break;
    }
    case 0xa199:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x0a] = 0;
        write_word(ram, actor + 0x3c, 0x9b93);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor + 0x42, 41);
        write_word(ram, actor + 0x0e, 0x8000);
        write_word(ram, actor, 0xa1bf);
        /* fall through */
    case 0xa1bf:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, 0x99dd + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x58, read_word(main_rom, 0x99ff + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (read_word(ram, actor + 0x42)) {
            decrement(ram, actor + 0x42);
            write_word(ram, actor + 0x26, 0x7f00);
            if (((ram[0x274] & 7) >> 1) == read_word(ram, actor + 0x54))
                world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    case 0xa268:
    case 0xa2c0:
        ram[actor + 0x2c] = 3;
        if (address == 0xa268)
            ram[actor + 0x2d] += main_rom[(uint16_t)(0x9b9b + read_word(ram, actor + 0x52))];
        else if (read_word(ram, actor + 0x60) == 3) {
            world_acquire_resource(ram, actor, 0x5b);
            write_word(ram, actor + 0x4c, 0x2a);
        }
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x42, 16);
        write_word(ram, actor + 0x58, 1);
        address = address == 0xa268 ? 0xa28e : 0xa2eb;
        write_word(ram, actor, address);
        /* Both initializers run the first animation step immediately. */
        /* fall through */
    case 0xa28e:
    case 0xa2eb:
    case 0xa318:
        update_attached_effect(ram, main_rom, actor, address);
        break;
    case 0x9e17:
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x9e30);
        break;
    case 0x9e30:
        animate_sprite(ram, main_rom, actor);
        break;
    case 0x1b4c:
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 8) >= read_word(ram, read_word(ram, actor + 4) + 8)) {
            /* 1861 unlinks through SI = the retired actor's successor. */
            leave_si(read_word(ram, actor + 4));
            retire_actor(ram, actor);
        } else
            keep_si();
        break;
    case 0x1b4b:
    case 0x1b62:
        keep_si();
        break;
    /* Stage-two gunboat B000:380F and its parts. */
    case 0xb380f: case 0xb39c5: case 0xb3aa6: case 0xb3ac6: case 0xb3b21: case 0xb3b35:
    case 0xb3c31: case 0xb3c64: case 0xb3d2a: case 0xb3e0d: case 0xb3e64: case 0xb3e83:
    case 0xb3f05: case 0xb3f1f: case 0xb3fd9: case 0xb4059: case 0xb4094: case 0xb40e4:
    case 0xb4113: case 0xb41b6: case 0xb4226: case 0xb4262: case 0xb4285: case 0xb433e:
    case 0xb43c8: case 0xb44c4: case 0xb451c: case 0xb4583:
        if (!stage_two_gunboat(ram, main_rom, tiles, actor, address)) return 0;
        break;
    /* Stage-five ice boss: hull 7FFE, core 8337, arms 8A19, tentacle 8EB5. */
    case 0x77ffe: case 0x78085: case 0x780c9: case 0x781ed: case 0x7826a: case 0x78295:
    case 0x782c6: case 0x78304: case 0x78337: case 0x78373: case 0x7839e: case 0x78442:
    case 0x784a3: case 0x784fa: case 0x78534: case 0x7855e: case 0x785e2: case 0x785fa:
    case 0x7862b: case 0x78680: case 0x787c5: case 0x787d8: case 0x7885f: case 0x78877:
    case 0x788da: case 0x78971: case 0x789b1: case 0x789c4: case 0x78a19: case 0x78a92:
    case 0x78ad5: case 0x78ae4: case 0x78bac: case 0x78c0c: case 0x78c20: case 0x78cfb:
    case 0x78d45: case 0x78d90: case 0x78dec: case 0x78e51: case 0x78e70: case 0x78eb5:
    case 0x78edc: case 0x78f3b: case 0x78fb1: case 0x7900f: case 0x79044: case 0x79079:
    case 0x790ae: case 0x790fc: case 0x79153: case 0x79194: case 0x791e5: case 0x7923a:
    case 0x7928d: case 0x792ef: case 0x79343: case 0x79391: case 0x793e6: case 0x793f7:
    case 0x79dd9: case 0x79e1a:
        if (!stage_five_boss(ram, main_rom, tiles, actor, address)) return 0;
        break;
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
    world_dispatch_return(pass, ram, actor);
    return 1;
}




int world_dispatch_native(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom)
{
    return world_dispatch_native_video(pass, ram, main_rom, 0);
}
