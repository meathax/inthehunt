#include <stdint.h>
#include "world_tile_address.h"
#include "world_torpedo.h"

static uint16_t word(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static void put(uint8_t *p, uint16_t value) { p[0] = (uint8_t)value; p[1] = value >> 8; }

/* 7CA3..7D02. The original direction table also defines opposed inputs. */
static void sub_velocity(const uint8_t *ram, const uint8_t *rom, uint8_t *a, unsigned held)
{
    unsigned direction = rom[0x7897 + (held & 15)];
    a[0x28] = (uint8_t)direction;
    uint16_t vx = 0, vy = 0;
    if (direction != 255) {
        uint16_t speed = word(ram + 0xa2);
        uint16_t delay = word(a + 0x42);
        if ((int16_t)delay > 0x60)
            speed += word(rom + 0x78a7 + (((delay - 0x60) & 0xf8) >> 2));
        vx = (uint32_t)word(rom + 0x7877 + direction) * speed;
        vy = (uint32_t)word(rom + 0x7879 + direction) * (uint16_t)(speed - 0x40);
    }
    put(a + 0x38, vx);
    put(a + 0x3a, vy);
}

void world_sub_velocity(uint8_t *ram, const uint8_t *rom, uint16_t sub, unsigned held)
{
    sub_velocity(ram, rom, ram + sub, held);
}

/* Unobstructed water; this path has zero external actor displacement. */
void world_sub_free_motion(uint8_t *ram, uint16_t sub)
{
    uint8_t *a = ram + sub;
    for (unsigned axis = 0; axis < 2; ++axis) {
        unsigned pos = 0x20 + axis * 3;
        uint16_t velocity = word(a + 0x38 + axis * 2);
        unsigned fraction = a[pos] + (velocity & 255);
        int pixels = velocity >> 8;
        if (pixels >= 128) pixels -= 256;
        a[pos] = (uint8_t)fraction;
        put(a + pos + 1, word(a + pos + 1) + pixels + (fraction >> 8));
    }
    uint16_t timer = word(a + 0x42);
    if (timer) put(a + 0x42, timer - 1);
}
/* P1/P2 active-low cabinet word. Duplicate held/pressed words feed the
 * game's per-player input selection later in the frame. */
void world_latch_cabinet(uint8_t *ram, uint16_t raw)
{
    uint16_t held = (uint16_t)~raw;
    uint16_t edges = held & (uint16_t)~word(ram + 0xb73c);
    put(ram + 0xb73c, held);
    put(ram + 0xb73e, held);
    put(ram + 0xb740, edges);
    put(ram + 0xb742, edges);
}

/* The sprite pose is selected from two damped, signed 8.8 displacements. */
static unsigned advance_pose_axis(uint8_t *position)
{
    uint16_t offset = word(position), velocity = word(position + 2);
    int16_t force = (uint16_t)(-2 * offset - velocity);
    velocity += force >> 4;
    offset += velocity;
    put(position + 2, velocity);
    put(position, offset);
    int step = (int8_t)((uint16_t)(offset + 128) >> 8);
    if (step < -2) step = -2;
    if (step > 2) step = 2;
    return (unsigned)(step + 2);
}

void world_sub_pose(uint8_t *ram, const uint8_t *rom, uint16_t sub)
{
    uint8_t *a = ram + sub;
    a[0x6c] |= 1;
    if (ram[0x274] & 1) return;
    unsigned row = advance_pose_axis(a + 0x5c);
    unsigned column = 2 * advance_pose_axis(a + 0x58);
    unsigned pose = column + 10 * row;
    put(a + 0x56, column);
    put(a + 0x72, pose);
    unsigned bank = word(a + 0x60) == 3 ? 50 : 0;
    put(a + 0x2a, word(rom + 0x7711 + bank + pose));
    if (word(a + 0x62) != 7) return;
    uint16_t sprite;
    if (pose == 24) {
        uint16_t frame = word(a + 0x40) + 2;
        if ((int16_t)frame >= 10) frame = 0;
        put(a + 0x40, frame);
        sprite = word(rom + (uint16_t)(0x77a7 + frame));
    } else {
        sprite = word(rom + 0x7775 + pose);
    }
    put(a + 0x6a, sprite);
    a[0x6c] |= 2;
}

uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);

static uint8_t *spawn_wake(uint8_t *ram, uint16_t handler, uint16_t sub)
{
    uint16_t slot = world_spawn_child(ram, word(ram + 0x9a), handler);
    uint8_t *effect = ram + (slot ? slot : 0x8480);
    put(effect + 0x7c, sub);
    return effect;
}

void world_sub_wake(uint8_t *ram, const uint8_t *rom, uint16_t sub)
{
    uint8_t *a = ram + sub;
    uint16_t timer = word(a + 0x64) - 1;
    put(a + 0x64, timer);
    if (!timer) {
        put(a + 0x64, 6);
        unsigned side = a[0x29] & 1;
        if (ram[0xb73e] & (1u << side)) {
            uint8_t *effect = spawn_wake(ram, word(rom + 0x8c2f), sub);
            uint16_t pose = 2 * word(a + 0x56);
            uint16_t dx = word(rom + (uint16_t)(0x78b3 + pose));
            effect[0x29] = a[0x29];
            put(effect + 0x21, word(a + 0x21) + (side ? -dx : dx));
            put(effect + 0x24, word(a + 0x24) + word(rom + (uint16_t)(0x78b5 + pose)));
        }
    }
    if (!(ram[0xb73e] & 4)) {
        put(a + 0x66, 24);
        return;
    }
    timer = word(a + 0x66) - 1;
    put(a + 0x66, timer);
    if (!timer) {
        put(a + 0x66, 32);
        uint8_t *effect = spawn_wake(ram, word(rom + 0x8c78), sub);
        uint16_t sequence = word(a + 0x68);
        put(effect + 0x68, sequence & 6);
        put(a + 0x68, sequence + 2);
    }
}

void world_queue_sound(uint8_t *, uint16_t);

static void hud_tile(uint8_t *tiles, uint16_t dest, uint16_t code, uint16_t attributes)
{
    put(tiles + dest, code);
    put(tiles + (uint16_t)(dest + 2), attributes);
}

void world_sub_lives_hud(const uint8_t *rom, uint8_t *tiles, unsigned owner, uint16_t lives)
{
    if (!owner) return;
    unsigned index = 2 * (owner - 1);
    uint16_t dest = word(rom + (uint16_t)(0xc25f + index));
    uint16_t attributes = word(rom + (uint16_t)(0xc263 + index));
    uint16_t left = 0x70, right = 0x71;
    for (unsigned i = 0; i < 8; i++) {
        if (!--lives) left = right = 0;
        hud_tile(tiles, dest + 8*i, left, attributes);
        hud_tile(tiles, dest + 8*i + 4, right, attributes);
    }
}

void world_sub_hud(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub)
{
    unsigned owner = ram[sub + 0x0b] & 3;
    uint16_t index = 2 * (owner - 1);
    uint16_t base = word(rom + (uint16_t)(0xc267 + index));
    uint16_t score = word(ram + (uint16_t)(0x254 + index));
    for (unsigned i = 0; i < 4; i++) {
        unsigned bcd = ram[(uint16_t)(score + 3 - i)];
        hud_tile(tiles, base + 8*i, 48 + (bcd >> 4), 0x104);
        hud_tile(tiles, base + 8*i + 4, 48 + (bcd & 15), 0x104);
    }
    uint8_t *config = ram + word(ram + sub + 0x70);
    uint16_t count = word(config + 0x1a);
    if (count >= 100) {
        count -= 100;
        put(config + 0x1a, count);
        uint16_t lives = word(config + 0x18) + 1;
        put(config + 0x18, lives);
        world_sub_lives_hud(rom, tiles, owner, lives);
        world_queue_sound(ram, 0x4b);
    }
    hud_tile(tiles, base + 0x2c, 48 + count / 10, 0x104);
    hud_tile(tiles, base + 0x30, 48 + count % 10, 0x104);
}

static uint16_t probe_address(const uint8_t *ram, uint16_t x, uint16_t y)
{
    return world_tile_address(x - 0x60 + word(ram + 0xb70a), 0x177 - y + word(ram + 0xb70c));
}

static unsigned row_material(const uint8_t *map, uint16_t cell, unsigned column)
{
    unsigned offset = (cell & 0xff00) | ((cell + 4 * column) & 255);
    return map[offset + 3] >> 3;
}

static uint16_t next_pixel(const uint8_t *position, uint16_t velocity)
{
    unsigned frac = position[0] + (velocity & 255);
    return word(position + 1) + (int8_t)(velocity >> 8) + (frac >> 8);
}

static void update_water_medium(uint8_t *ram, const uint8_t *rom, uint8_t *a,
                                unsigned center, uint16_t medium)
{
    if (word(a + 0x62) != center) {
        unsigned index = (a[0x0b] & 2) + 2 * ((center - 4) & 2);
        uint8_t *job = ram + 0x9e06 + 8*a[0x15];
        put(job, word(rom + 0x78cb + index));
        put(job + 2, 0x80);
        put(job + 4, 0x85);
        put(job + 6, 0x20);
    }
    put(a + 0x62, center);
    if (medium == 3 && word(a + 0x60) != 3) put(a + 0x5a, word(a + 0x5a) - 256);
    put(a + 0x60, medium);
    ram[0xb8ae] &= ~(1u << (a[0x0b] & 2));
}

/* 848F: the surfacing submarine uses two five-tile sensors until both clear. */
void world_sub_entry_step(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub)
{
    uint8_t *a = ram + sub, *cfg = ram + word(a + 0x70);
    world_sub_hud(ram, rom, tiles, sub);
    sub_velocity(ram, rom, a, ram[0xb73e]);
    uint16_t delay = word(a + 0x42);
    world_sub_free_motion(ram, sub);
    int16_t x = (int16_t)word(a + 0x21), y = (int16_t)word(a + 0x24);
    if ((int16_t)delay <= 96) {
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t cell = probe_address(ram, x - 16, y + (i ? -8 : 7));
            unsigned mask = 0;
            for (unsigned col = 0; col < 5; ++col)
                mask |= 1u << (row_material(tiles + 0x4000, cell, col) & 15);
            put(cfg + 2 * i, mask);
        }
        if (!((word(cfg) | word(cfg + 2)) & 0xff02)) put(a + 0x6e, 0);
    }
    int16_t ceiling = (int16_t)word(ram + 0x25c);
    put(a + 0x21, x > 0x188 ? 0x188 : x < 0x70 ? 0x70 : x);
    put(a + 0x24, y < 0xa0 ? 0xa0 : y > ceiling ? ceiling : y);
    update_water_medium(ram, rom, a, cfg[10], cfg[14] & 8 ? 3 : 0);
    put(a + 0x6c, 0);
    if (delay == 1) {
        put(a + 0x26, word(a + 0x6e) ? 0 : 1);
        put(a + 0x36, 256);
    }
    if (!delay || !word(ram + 0x2a4) || !(a[0x42] & 1))
        world_sub_pose(ram, rom, sub);
    world_player_weapons(ram, rom, sub);
    world_sub_wake(ram, rom, sub);
}

/* 8480 samples horizontal sensors before vertical movement and resolves
   solid tile edges in the scrolling playfield's eight-pixel grid. */
int world_sub_open_water(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub)
{
    uint8_t *a = ram + sub;
    unsigned held = ram[0xb73e], owner_bit = 1u << (a[0x0b] & 2);
    unsigned flags = word(ram + 0x262);
    int hidden_entry = word(a) == 0x7be5 && a[0x0a];
    /* 7C58..7CA0: a submarine sealed in ice (its B8B2 bit) cannot die or
       steer. After the HUD it clears the held directions and resumes at
       7D1B, skipping the velocity and the 8480 terrain step. */
    if ((word(ram + 0xb8b2) & owner_bit) && word(a) == 0x7c3b && !word(a + 0x6e) &&
        !(flags & 0x40) && !word(ram + 0x2a4)) {
        if (!(flags & 0x8080) && !a[0x0a] && !word(a + 0x26)) put(a + 0x26, 1);
        world_sub_hud(ram, rom, tiles, sub);
        put(ram + 0xb73e, word(ram + 0xb73e) & 0xfff0);
        put(a + 0x6c, 0);
        unsigned timer = word(a + 0x42);
        if (timer) {
            put(a + 0x42, timer - 1);
            if (timer == 1) {
                put(a + 0x26, 1);
                put(a + 0x36, 0x100);
            }
        }
        world_sub_pose(ram, rom, sub);
        world_player_weapons(ram, rom, sub);
        world_sub_wake(ram, rom, sub);
        return 1;
    }
    if (word(a + 0x6e) || (!hidden_entry && !word(a + 0x26) && !(flags & 0x8080))
        || (word(ram + 0xb8b2) & owner_bit) || (flags & 0x40)
        || (!hidden_entry && word(ram + 0x2a4)))
        return 0;
    uint8_t predicted[128];
    for (unsigned i = 0; i < sizeof predicted; i++) predicted[i] = a[i];
    sub_velocity(ram, rom, predicted, held);
    uint16_t x = next_pixel(a + 0x20, word(predicted + 0x38));
    uint16_t old_y = word(a + 0x24), y = next_pixel(a + 0x23, word(predicted + 0x3a));
    const uint8_t *map = tiles + 0x4000;
    uint16_t left = 0, right = 0, rows[3], masks[3] = {0};
    const int offsets[3] = {7, 0, -8};
    for (unsigned i = 0; i < 3; i++) {
        uint16_t cell = probe_address(ram, x - 16, old_y + offsets[i]);
        left |= 1u << (row_material(map, cell, 0) & 15);
        right |= 1u << (row_material(map, cell, 4) & 15);
        rows[i] = cell;
    }
    unsigned x_fraction = (uint8_t)(a[0x20] + word(predicted + 0x38));
    uint16_t health = word(a + 0x26);
    unsigned horizontal_scroll = word(ram + 0xb70a) & 7;
    /* During the scene fade, 8480 deliberately skips horizontal limits. */
    if (!(flags & 0x8000)) {
        if (right & 0xff02) {
            if (ram[0xb8ae] & owner_bit) health = 0;
            else {
                x = ((uint16_t)(x + horizontal_scroll) & 0xfff8) - 1 - horizontal_scroll;
                x_fraction = 0;
            }
        } else if (x > 0x188) x = 0x188;
        else if (left & 0xff02) {
            if (ram[0xb8ae] & owner_bit) health = 0;
            else {
                x = ((uint16_t)(x - 1 + horizontal_scroll) & 0xfff8) + 8 - horizontal_scroll;
                x_fraction = 0;
            }
        } else if (x < 0x70) x = 0x70;
    }
    uint16_t vertical[3];
    for (unsigned i = 0; i < 3; i++) {
        vertical[i] = probe_address(ram, x - 16, y + offsets[i]);
        for (unsigned j = 0; j < 5; j++)
            masks[i] |= 1u << (row_material(map, vertical[i], j) & 15);
    }
    unsigned center = row_material(map, probe_address(ram, x, y - 4), 0);
    unsigned above = row_material(map, probe_address(ram, x, y + 16), 0);
    uint16_t medium = 0;
    unsigned y_fraction = (uint8_t)(a[0x23] + word(predicted + 0x3a));
    unsigned vertical_scroll = word(ram + 0xb70c) & 7;
    unsigned cleared_inputs = 0;
    if (masks[2] & 0xff02) {
        medium = 8;
        if (ram[0xb8ae] & owner_bit) health = 0;
        else {
            y = ((uint16_t)(y - vertical_scroll) & 0xfff8) + 8 + vertical_scroll;
            y_fraction = 0;
        }
        cleared_inputs |= 4;
    } else if (y < 0xa0) {
        y = 0xa0;
        cleared_inputs |= 4;
    }
    if (medium != 8) {
        if (masks[0] & 0xff02) {
            medium = 5;
            y = ((uint16_t)(y - 1 - vertical_scroll) & 0xfff8) + vertical_scroll;
            y_fraction = 0;
            cleared_inputs |= 8;
        } else if (y > 0x170) {
            y = 0x170;
            cleared_inputs |= 8;
        } else if (masks[1] & 8) {
            y = ((uint16_t)(y - vertical_scroll) & 0xfff8) + vertical_scroll;
            y_fraction = 0;
            cleared_inputs |= 8;
        }
    }
    if ((above & 15) == 3) medium = 3;
    if (hidden_entry) {
        for (unsigned i = 0x58; i < 0x60; ++i) a[i] = 0;
    }
    world_sub_hud(ram, rom, tiles, sub);
    a[0x28] = predicted[0x28];
    put(a + 0x38, word(predicted + 0x38));
    put(a + 0x3a, word(predicted + 0x3a));
    unsigned timer = word(a + 0x42);
    world_sub_free_motion(ram, sub);
    put(a + 0x21, x);
    a[0x20] = (uint8_t)x_fraction;
    a[0x23] = (uint8_t)y_fraction;
    put(a + 0x26, health);
    ram[0xb74c] = (ram[0xb74c] | (held & 12)) & (uint8_t)~cleared_inputs;
    put(a + 0x24, y);
    uint8_t *cfg = ram + word(a + 0x70);
    put(cfg, left);
    put(cfg + 2, right);
    put(cfg + 4, masks[0]);
    put(cfg + 6, masks[2]);
    put(cfg + 8, masks[1]);
    cfg[10] = center;
    put(cfg + 14, 1u << (above & 15));
    put(cfg + 0x30, vertical[0]);
    put(cfg + 0x32, vertical[2]);
    put(cfg + 0x34, rows[2]);
    update_water_medium(ram, rom, a, center, medium);
    put(a + 0x6c, 0);
    if (timer == 1) {
        put(a + 0x26, 1);
        put(a + 0x36, 0x100);
    }
    if (!timer || !word(ram + 0x2a4) || !(a[0x42] & 1))
        world_sub_pose(ram, rom, sub);
    world_player_weapons(ram, rom, sub);
    world_sub_wake(ram, rom, sub);
    return 1;
}
