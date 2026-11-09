/* Recovered World actor handlers for stage 3 (stage index 2).
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

#define AW(o) read_word(ram, (uint16_t)(actor + (o)))
#define SET(o, v) write_word(ram, (uint16_t)(actor + (o)), (uint16_t)(v))
#define AB(o) ram[(uint16_t)(actor + (o))]
#define CW(c, o) read_word(ram, (uint16_t)((c) + (o)))
#define CSET(c, o, v) write_word(ram, (uint16_t)((c) + (o)), (uint16_t)(v))
#define CB(c, o) ram[(uint16_t)((c) + (o))]

static void scroll(uint8_t *ram, uint16_t actor) { scroll_actor(ram, actor, 0xb71a); }
static void move_x(uint8_t *ram, uint16_t actor) { move_actor_axis(ram, actor + 0x20, AW(0x38)); }
static void move_y(uint8_t *ram, uint16_t actor) { move_actor_axis(ram, actor + 0x23, AW(0x3a)); }

/* 3665: second sound ring. */
static void queue_sound_b(uint8_t *ram, uint16_t command)
{
    unsigned tail = (read_word(ram, 0xa6d8) + 2) & 14;
    if (tail != read_word(ram, 0xa6d6)) {
        write_word(ram, 0xa6d8, tail);
        write_word(ram, 0xa6a2 + tail, command);
    }
}

/* 7036: scripted effect child reading its table from segment:table. */
static uint16_t script_effect(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t table, uint16_t period)
{
    uint16_t child = effect_slot(ram, actor, 0x7056);
    CSET(child, 0x60, table);
    CSET(child, 0x62, segment);
    CSET(child, 0x4c, period);
    CSET(child, 0x42, 1);
    return child;
}

/* 1000:F400: spawner that walks a twelve-byte debris table. */
static uint16_t debris_spawner(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t table)
{
    uint16_t child = spawn_boss_part(ram, actor, 0xf41b);
    CSET(child, 0x48, table);
    CSET(child, 0x4a, segment);
    CSET(child, 0x42, 1);
    return child;
}

static uint16_t spawn_bank7(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = effect_slot(ram, actor, handler);
    if (child != 0x8480) CSET(child, 2, 0x7000);
    return child;
}

/* 6E86..6EDC family: one-shot effect with a sound tag. */
static uint16_t tagged_effect(uint8_t *ram, uint16_t actor, uint16_t tag, uint16_t frames)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    CSET(child, 0x44, tag);
    CSET(child, 0x4c, frames);
    CSET(child, 0x4a, 0x7706);
    CB(child, 0x2d) = 0x20;
    return child;
}

/* 6B36 with the child returned. */
static uint16_t radial_explosion(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    CSET(child, 0x4c, 0x74c3);
    CSET(child, 0x4a, 0x7675);
    CB(child, 0x2c) = 0x0a;
    CB(child, 0x2d) = 0x20;
    CSET(child, 0x56, 0x30);
    CSET(child, 0x58, 0x80);
    if (!(child & 0x300)) CSET(child, 0x44, 0x5d);
    return child;
}

/* 6920: snap an actor to the water row below it. */
static void snap_row_below(uint8_t *ram, uint16_t actor)
{
    uint16_t phase = read_word(ram, 0xb70c) & 7;
    SET(0x24, AW(0x24) - 1 - phase);
    SET(0x23, AW(0x23) & 0xf800);
    SET(0x24, AW(0x24) + 8 + phase);
}

/* 6CD2, 6D10, 6D4D, 6DF8: splash at the actor, snapped to the surface. */
static uint16_t surface_splash(uint8_t *ram, uint16_t actor, uint16_t frames, uint16_t timing)
{
    uint16_t child = effect_slot(ram, actor, 0x6f54);
    CSET(child, 0x21, AW(0x21));
    CSET(child, 0x24, AW(0x24));
    CSET(child, 0x4c, frames);
    CSET(child, 0x4a, timing);
    return child;
}

static void splash_6d10(uint8_t *ram, uint16_t actor)
{
    uint16_t child = surface_splash(ram, actor, 0x7563, 0x768b);
    CB(child, 0x2c) = 0x0a;
    CB(child, 0x2d) = 0x20;
    CSET(child, 0x56, 0x30);
    CSET(child, 0x58, 0x80);
    snap_row_below(ram, child);
}

/* 68DF with the tile cell (BX) and material number (CX) as well as the mask (DX). */
static uint16_t terrain_cell(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y, uint16_t *cell)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    *cell = world_tile_address(map_x, map_y);
    return tiles[0x4000 + *cell + 3] >> 3;
}

static uint16_t terrain(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y, unsigned *material)
{
    uint16_t cell;
    unsigned m = terrain_cell(ram, tiles, x, y, &cell);
    if (material) *material = m;
    return (uint16_t)(1u << (m & 15));
}

/* 37EB: draw CH columns of CL four-by-four tile blocks. */
static void tile_grid(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t base,
                      uint16_t dest, uint16_t source, uint16_t size)
{
    unsigned rows = (uint8_t)((size & 255) - 1) + 1, columns = (uint8_t)((size >> 8) - 1) + 1;
    write_word(ram, 0xb89a, size);
    write_word(ram, 0xb89c, dest);
    for (unsigned i = 0; i < columns; i++) {
        uint16_t cell = dest;
        for (unsigned j = 0; j < rows; j++) {
            world_draw_tile_block(tiles, rom, base, cell, read_word(rom + 0xa0000, source));
            source += 2;
            cell = (cell + 0x400) & 0x3fff;
        }
        if (i + 1 < columns) {
            dest = (dest & 0xff00) | ((dest + 16) & 255);
            write_word(ram, 0xb89c, dest);
        }
    }
}

/* 6761/6789: position = base (bp+33 or bp+30) + sine(phase * rate >> 8) * amplitude. */
static void wave(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned dest, unsigned base,
                 uint16_t table, unsigned index, uint16_t amplitude)
{
    int32_t product = (int16_t)read_word(rom + 0xa0000, table + 2 * (index & 255)) * (int16_t)amplitude;
    uint32_t sum = (AW(base) | (uint32_t)AB(base + 2) << 16) + ((uint32_t)product & 0xffffff);
    SET(dest, sum);
    AB(dest + 2) = (uint8_t)(sum >> 16);
}

static void sine_y(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t phase, uint16_t rate, uint16_t amplitude)
{
    wave(ram, rom, actor, 0x23, 0x33, 0x9d92, (uint32_t)phase * rate >> 8, amplitude);
}

static void sine_x(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t phase, uint16_t rate, uint16_t amplitude)
{
    wave(ram, rom, actor, 0x20, 0x30, 0x9d92, (uint32_t)phase * rate >> 8, amplitude);
}

/* 657D: velocity from heading (bp+28) and speed (bp+2E). */
static void heading_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * AB(0x28);
    int16_t speed = AW(0x2e);
    SET(0x38, ((int16_t)read_word(rom + 0xa0000, 0x9e12 + angle) * speed) >> 8);
    SET(0x3a, ((int16_t)read_word(rom + 0xa0000, 0x9d92 + angle) * speed) >> 8);
}

/* D68F/D692: load the motion script record at offset; returns 0 at the FFFF terminator. */
static int script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    const uint8_t *bank = rom + ((uint32_t)AW(2) << 4);
    SET(0x7e, offset);
    uint16_t kind = read_word(bank, offset);
    if (kind == 0xffff) return 0;
    SET(0x7a, kind);
    switch (kind) {
    case 0:
        SET(0x7e, offset + 4);
        SET(0x38, 0);
        SET(0x3a, 0);
        break;
    case 2:
    case 14:
        AB(0x28) = bank[(uint16_t)(offset + 4)];
        if (kind == 2 && read_word(bank, offset + 6)) SET(0x2e, read_word(bank, offset + 6));
        heading_velocity(ram, rom, actor);
        SET(0x78, 0);
        SET(0x76, 0);
        SET(0x7e, offset + 8);
        AB(0x20) = AB(0x23) = 0;
        break;
    case 4: {
        AB(0x28) = bank[(uint16_t)(offset + 4)];
        uint16_t speed = AW(0x2e);
        SET(0x2e, read_word(bank, offset + 8));
        heading_velocity(ram, rom, actor);
        SET(0x78, AW(0x38));
        SET(0x76, AW(0x3a));
        SET(0x2e, speed);
        if (read_word(bank, offset + 6)) SET(0x2e, read_word(bank, offset + 6));
        heading_velocity(ram, rom, actor);
        SET(0x7e, offset + 10);
        AB(0x20) = AB(0x23) = 0;
        break;
    }
    case 6:
    case 8:
        SET(0x1c, read_word(bank, offset + 4));
        SET(0x1a, read_word(bank, offset + 6));
        SET(0x31, AW(0x21) + read_word(bank, offset + 8));
        SET(0x34, AW(0x24) + read_word(bank, offset + 10));
        SET(0x78, read_word(bank, offset + 12));
        SET(0x76, kind == 8 ? read_word(bank, offset + 14) : 0);
        SET(0x7e, offset + (kind == 8 ? 16 : 14));
        AB(0x20) = AB(0x23) = 0;
        break;
    case 10:
    case 12: {
        SET(0x78, read_word(bank, offset + 4));
        SET(0x76, read_word(bank, offset + 6));
        uint16_t speed = read_word(bank, offset + 8);
        SET(kind == 10 ? 0x38 : 0x3a, speed);
        AB(0x28) = rom[(kind == 10 ? 0xda0f : 0xda11) + (speed >> 15)];
        SET(0x31, AW(0x21));
        SET(0x34, AW(0x24));
        SET(0x7e, offset + 10);
        AB(0x20) = AB(0x23) = 0;
        break;
    }
    case 16:
        SET(0x78, read_word(bank, offset + 4));
        SET(0x76, read_word(bank, offset + 6));
        SET(0x38, read_word(bank, offset + 8));
        SET(0x7e, offset + 10);
        SET(0x31, 0);
        SET(0x34, AW(0x24));
        AB(0x20) = AB(0x30) = AB(0x23) = 0;
        break;
    case 18:
        SET(0x78, read_word(bank, offset + 4));
        SET(0x76, read_word(bank, offset + 6));
        SET(0x3a, read_word(bank, offset + 8));
        SET(0x7e, offset + 10);
        SET(0x31, AW(0x21));
        SET(0x34, 0);
        AB(0x20) = AB(0x23) = AB(0x33) = 0;
        break;
    case 20:
        SET(0x38, read_word(bank, offset + 4));
        SET(0x3a, read_word(bank, offset + 6));
        SET(0x7e, offset + 8);
        break;
    default:
        return -1;
    }
    SET(0x7c, read_word(bank, offset + 2));
    return 1;
}

/* D6B7: one tick of the motion script. Returns 1 when the script ended (carry),
 * 0 while it runs and -1 on an unknown record kind. */
static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t kind = AW(0x7a);
    if (kind != 6 && kind != 8) scroll(ram, actor);
    switch (kind) {
    case 0:
        break;
    case 2:
    case 4:
        SET(0x38, AW(0x38) + AW(0x78));
        move_x(ram, actor);
        SET(0x3a, AW(0x3a) + AW(0x76));
        move_y(ram, actor);
        break;
    case 6:
    case 8:
        SET(0x31, AW(0x31) + read_word(ram, 0xb71a));
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        SET(0x1a, AW(0x1a) + AW(0x78));
        SET(0x1c, AW(0x1c) + AW(0x76));
        wave(ram, rom, actor, 0x20, 0x30, 0x9e12, AB(0x1b), AW(0x1c));
        wave(ram, rom, actor, 0x23, 0x33, 0x9d92, AB(0x1b), AW(0x1c));
        AB(0x28) = (uint8_t)(AB(0x1b) + ((AW(0x78) & 0x8000) ? -0x40 : 0x40));
        break;
    case 10:
        SET(0x31, AW(0x31) + read_word(ram, 0xb71a));
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        move_x(ram, actor);
        sine_y(ram, rom, actor, AW(0x21) - AW(0x31), AW(0x76), AW(0x78));
        break;
    case 12:
        SET(0x31, AW(0x31) + read_word(ram, 0xb71a));
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        move_y(ram, actor);
        sine_x(ram, rom, actor, AW(0x24) - AW(0x34), AW(0x76), AW(0x78));
        break;
    case 14:
        SET(0x38, AW(0x38) + AW(0x78));
        move_actor_axis(ram, actor + 0x30, AW(0x38));
        SET(0x3a, AW(0x3a) + AW(0x76));
        move_actor_axis(ram, actor + 0x33, AW(0x3a));
        break;
    case 16:
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x30, AW(0x38));
        sine_y(ram, rom, actor, AW(0x31), AW(0x76), AW(0x78));
        break;
    case 18:
        SET(0x31, AW(0x31) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x33, AW(0x3a));
        sine_x(ram, rom, actor, AW(0x34), AW(0x76), AW(0x78));
        break;
    case 20:
        move_x(ram, actor);
        move_y(ram, actor);
        break;
    default:
        return -1;
    }
    if (!decrement(ram, actor + 0x7c)) {
        int next = script_begin(ram, rom, actor, AW(0x7e));
        return next < 0 ? -1 : !next;
    }
    return 0;
}

/* Shared kill test of the debris handlers at 1F4E4 and 1F5C8. */
static int debris_outside(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = AW(0x21);
    int16_t top = (ram[0x26e] & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
    return x > 0x1c0 || (int16_t)AW(0x24) < top || x < 0x40;
}

static void debris_animate(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    if (!decrement(ram, actor + 0x44)) {
        SET(0x2a, read_word(bank, 0xd986 + (AW(0x40) & 14) + AW(0x46)));
        SET(0x44, AW(0x42));
        SET(0x40, AW(0x40) + 2);
    }
}

/* 6B2E: explosion sound and radial blast. */
static uint16_t loud_explosion(uint8_t *ram, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    return radial_explosion(ram, actor);
}

/* 6ACB: small burst. */
static uint16_t small_burst(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    CSET(child, 0x4c, 0x747f);
    CSET(child, 0x4a, 0x76c8);
    CB(child, 0x2c) = 0x0a;
    CB(child, 0x2d) = 0x20;
    CSET(child, 0x56, 0x30);
    CSET(child, 0x58, 0x80);
    return child;
}

/* 65B1: target player plus the side bits the original leaves in IX
 * (bit 1: target is left of the actor, bit 0: target is above it). */
static uint16_t target_side(uint8_t *ram, uint16_t actor, unsigned *side)
{
    uint16_t target = world_enemy_target(ram, actor);
    *side = 0;
    if (ram[0x264] & 3)
        *side = (CW(target, 0x21) < AW(0x21) ? 2 : 0) | (CW(target, 0x24) < AW(0x24) ? 1 : 0);
    return target;
}

/* 1000:3B63: turn the heading eight steps toward the target and pick the sprite. */
static void steer_toward(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t target)
{
    uint8_t desired = (uint8_t)((vector_angle(rom, CW(target, 0x21) - AW(0x21), CW(target, 0x24) - AW(0x24)) + 2) & 0xfc);
    uint8_t heading = AB(0x28);
    if (desired != heading)
        AB(0x28) = (uint8_t)(desired - heading) < (uint8_t)(heading - desired) ? heading + 8 : heading - 8;
    heading_velocity(ram, rom, actor);
    unsigned index = (AB(0x28) & 0xfc) >> 2;
    AB(0x29) = rom[0x12748 + index];
    SET(0x2a, read_word(rom + 0x10000, 0x2708 + 2 * (index & 31)));
}

/* Deck soldiers, their grenades and bullets: B000:1C16..2634. */
enum { SOLDIER_LIMIT = 10 };

static void soldier_remove(uint8_t *ram, uint16_t actor)
{
    decrement(ram, 0xc2);
    finish_animation(ram, actor);
}

static int soldier_offside(const uint8_t *ram, uint16_t actor)
{
    return AW(0x21) > 0x1c0 || AW(0x21) < 0x40;
}

/* B273A: pick the attack delay by the material under the soldier. */
static unsigned soldier_ground(uint8_t *ram, const uint8_t *tiles, uint16_t actor)
{
    unsigned wet = (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xf0) ? 2 : 0;
    SET(0x56, AW(wet ? 0x52 : 0x54));
    SET(0x58, AW(0x56));
    SET(0x46, wet);
    return wet;
}

/* B2659: rank-dependent timing and initial facing. */
static void soldier_rank(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    uint16_t row = 0x1bcc + read_word(bank, 0x1b8c + (AW(0x1e) & 0x20) + read_word(ram, 0x8c));
    SET(0x50, read_word(bank, row));
    SET(0x52, read_word(bank, row + 2));
    SET(0x54, read_word(bank, row + 4));
    AB(0x29) = AB(0x1f) & 1;
    SET(0x38, read_word(bank, 0x1aec + 2 * AB(0x29)));
}

static void soldier_start_fall(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    SET(0x3a, read_word(bank, 0x1af4 + (AW(0x1e) & 2)));
    SET(0x2a, 0x655a);
    SET(0x40, 0);
    SET(0x44, 1);
    world_queue_sound(ram, 0x8e);
}

/* B1D8C: draw, then player and shot contacts. */
static void soldier_tail(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (!queue_sprite(ram, rom, actor, 1)) { finish_animation(ram, actor); return; }
    AB(0x0d) = 2;
    uint16_t attacker = world_player_contact(ram, rom, actor);
    if (!attacker) {
        AB(0x0d) = 0;
        attacker = AW(0x46) ? world_projectile_contact(ram, rom, actor) : world_surface_contact(ram, rom, actor);
        if (AW(0x26)) {
            if (!AW(0x46)) register_surface_enemy(ram, actor);
            return;
        }
    }
    world_award_score(ram, rom, actor, attacker);
    loud_explosion(ram, actor);
    soldier_remove(ram, actor);
}

static void soldier_fall_frame(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    if (!decrement(ram, actor + 0x44)) {
        unsigned frame = AW(0x40) & 2;
        SET(0x2a, read_word(bank, 0x1ac6 + frame));
        SET(0x44, read_word(bank, 0x1aca + frame));
        SET(0x40, AW(0x40) + 2);
    }
}

/* B1FB4: falling frame, then the landing test. */
static void soldier_fall_tail(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles, uint16_t actor)
{
    soldier_fall_frame(ram, rom + 0xb0000, actor);
    if (terrain(ram, tiles, AW(0x21), AW(0x24) - 0x11, 0) & 0xff02) {
        world_queue_sound(ram, 0x80);
        uint16_t child = tagged_effect(ram, actor, 0x7b, 0x762d);
        CSET(child, 0x34, 0xfff0);
        CB(child, 0x2d) = AB(0x2d) + 1;
        SET(0x40, 0);
        SET(0x44, 1);
        soldier_ground(ram, tiles, actor);
        SET(0, 0x20de);
    }
    soldier_tail(ram, rom, actor);
}

/* Returns 0 when the frame list ended. */
static int soldier_sequence(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t sprites, uint16_t times)
{
    if (decrement(ram, actor + 0x44)) return 1;
    uint16_t frame = AW(0x40), sprite = read_word(bank, sprites + frame);
    if (sprite == 0xffff) return 0;
    SET(0x2a, sprite);
    SET(0x44, read_word(bank, times + frame));
    SET(0x40, frame + 2);
    return 1;
}

/* B23AC: bullet against terrain, water, players and shots. */
static void bullet_tail(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles, uint16_t actor)
{
    unsigned material;
    if (!(terrain(ram, tiles, AW(0x21), AW(0x24), &material) & 0xff02)) {
        if (material == 3) {
            uint16_t child = surface_splash(ram, actor, 0x7585, 0x769b);
            CB(child, 0x2c) = 0x0a;
            CB(child, 0x2d) = 0x20;
            CSET(child, 0x56, 0x30);
            CSET(child, 0x58, 0x80);
            snap_row_below(ram, child);
            world_queue_sound(ram, 0x82);
            finish_animation(ram, actor);
            return;
        }
        AB(0x0d) = 2;
        if (!world_player_contact(ram, rom, actor)) {
            AB(0x0d) = 0;
            world_projectile_contact(ram, rom, actor);
            if (AW(0x26)) return;
        }
    }
    small_burst(ram, actor);
    finish_animation(ram, actor);
}

static int soldier(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0xb0000;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0xb1c16: {
        if (read_word(ram, 0xc2) >= SOLDIER_LIMIT) { finish_animation(ram, actor); break; }
        write_word(ram, 0xc2, read_word(ram, 0xc2) + 1);
        SET(0x0e, 6);
        AB(0x2d) = 0x20;
        SET(0x26, 0x80);
        SET(0x36, 0x80);
        SET(0x3c, 0xf323);
        switch (read_word(bank, 0x1bfc + (AB(0x1f) & 14))) {
        case 0x2696:
            world_acquire_resource(ram, actor, 0x18);
            SET(0, 0x1c6f);
            break;
        case 0x26a4:
            world_acquire_resource(ram, actor, 0x18);
            soldier_start_fall(ram, bank, actor);
            SET(0, 0x2028);
            break;
        case 0x26d8:
            world_acquire_resource(ram, actor, 0x18);
            SET(0x3a, read_word(bank, 0x1af4 + (AW(0x1e) & 2)));
            SET(0x2a, 0x655a);
            AB(0x2d) = 0x29;
            SET(0x42, 0x12);
            SET(0x40, 0);
            SET(0x44, 1);
            world_queue_sound(ram, 0x8e);
            SET(0, 0x2017);
            break;
        case 0x2715:
            world_acquire_resource(ram, actor, 0x18);
            SET(0x2a, 0x6526);
            SET(0, 0x21ae);
            break;
        case 0x2728:
            acquire_private_palette(ram, actor, 0);
            AB(0x2d) = 0x3f;
            SET(0, 0x2150);
            break;
        default:
            return WORLD_HANDLER_STOP;
        }
        soldier_rank(ram, bank, actor);
        SET(0x48, read_word(bank, 0x1af4 + soldier_ground(ram, tiles, actor)));
        SET(0x44, 1);
        break;
    }
    case 0xb1c6f: {
        scroll(ram, actor);
        move_x(ram, actor);
        if (soldier_offside(ram, actor)) { soldier_remove(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = AW(0x40) & 6;
            SET(0x2a, read_word(bank, 0x1a7e + frame));
            SET(0x44, read_word(bank, 0x1a86 + frame));
            SET(0x40, AW(0x40) + 2);
        }
        if (AW(0x24) >= 0x90 && !decrement(ram, actor + 0x56)) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0, 0x1e24);
        }
        uint16_t ahead = AW(0x21) + read_word(bank, 0x1af0 + 2 * (AB(0x29) & 1));
        if (terrain(ram, tiles, ahead, AW(0x24) - 0x0c, 0) & 0xff02) {
            SET(0x2a, 0x64d6);
            SET(0x42, 0x20);
            SET(0, 0x1df0);
        } else if (!(terrain(ram, tiles, AW(0x21), AW(0x24) - 0x11, 0) & 0xff02)) {
            soldier_start_fall(ram, bank, actor);
            SET(0, 0x2028);
        } else if (!(terrain(ram, tiles, ahead, AW(0x24) - 0x11, 0) & 0xff02)) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0x38, AW(0x38) << 1);
            world_queue_sound(ram, 0x8e);
            SET(0, 0x1efe);
        }
        soldier_tail(ram, rom, actor);
        break;
    }
    case 0xb1df0:
        scroll(ram, actor);
        if (!decrement(ram, actor + 0x42)) {
            AB(0x29) ^= 1;
            SET(0x38, read_word(bank, 0x1aec + 2 * (AB(0x29) & 1)));
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0, 0x1c6f);
        }
        soldier_tail(ram, rom, actor);
        break;
    case 0xb1e24:
        SET(0x56, 0x18);
        SET(0x5a, 0x10);
        SET(0, 0x1e33);
        /* fall through */
    case 0xb1e33: {
        scroll(ram, actor);
        if (!soldier_sequence(ram, bank, actor, 0x1a8e, 0x1aa0)) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0x56, AW(0x58));
            SET(0, 0x1c6f);
        }
        unsigned facing = AB(0x29) & 1;
        uint16_t child = 0;
        if (AW(0x46)) {
            if (!decrement(ram, actor + 0x56)) {
                world_queue_sound(ram, 0x78);
                child = bank_b_effect(ram, actor, 0x2349);
                CSET(child, 0x50, AW(0x50));
                CB(child, 0x29) = AB(0x29) ^ 1;
                CB(child, 0x28) = bank[0x1c14 + facing];
                CSET(child, 0x2a, 0x9109);
            }
        } else if (AW(0x56) ? !decrement(ram, actor + 0x56) : !decrement(ram, actor + 0x5a)) {
            world_queue_sound(ram, 0x7f);
            child = bank_b_effect(ram, actor, 0x21ea);
        }
        if (child) {
            CSET(child, 0x21, AW(0x21) + read_word(bank, 0x1c10 + 2 * facing));
            CSET(child, 0x24, AW(0x24) + 4);
        }
        soldier_tail(ram, rom, actor);
        break;
    }
    case 0xb1efe:
        scroll(ram, actor);
        if (soldier_offside(ram, actor)) { soldier_remove(ram, actor); break; }
        if (!soldier_sequence(ram, bank, actor, 0x1ab0, 0x1abc)) {
            SET(0x3a, 0x200);
            SET(0, 0x1f5a);
        }
        soldier_tail(ram, rom, actor);
        break;
    case 0xb1f5a:
        SET(0x40, 0);
        SET(0x44, 1);
        SET(0, 0x1f69);
        /* fall through */
    case 0xb1f69:
        scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        SET(0x3a, AW(0x3a) - 0x20);
        if ((int16_t)AW(0x3a) <= (int16_t)AW(0x48)) {
            SET(0x40, 0);
            SET(0, 0x2028);
        }
        if (soldier_offside(ram, actor)) { soldier_remove(ram, actor); break; }
        soldier_fall_tail(ram, rom, tiles, actor);
        break;
    case 0xb2017:
    case 0xb2028:
    case 0xb2032: {
        if (address == 0xb2017) {
            if (!decrement(ram, actor + 0x42)) {
                AB(0x2d) = 0x20;
                SET(0, 0x2032);
            }
        } else if (address == 0xb2028) {
            SET(0x44, 1);
            SET(0, 0x2032);
        }
        scroll(ram, actor);
        move_y(ram, actor);
        if (debris_outside(ram, actor)) { soldier_remove(ram, actor); break; }
        unsigned material;
        if (!AW(0x46) && (terrain(ram, tiles, AW(0x21), AW(0x24) - 0x10, &material), material == 4)) {
            SET(0x46, 2);
            SET(0x48, read_word(bank, 0x1af4 + 2));
            SET(0x56, AW(0x52));
            SET(0x58, AW(0x52));
            SET(0x24, AW(0x24) - 0x10);
            uint16_t child = surface_splash(ram, actor, 0x75a7, 0x76d8);
            CSET(child, 0x44, 0x0b);
            CB(child, 0x2d) = 0x20;
            CSET(child, 0x56, 0x30);
            CSET(child, 0x58, 0x80);
            snap_row_below(ram, child);
            SET(0x24, AW(0x24) + 0x10);
        }
        soldier_fall_frame(ram, bank, actor);
        soldier_fall_tail(ram, rom, tiles, actor);
        break;
    }
    case 0xb20de:
        scroll(ram, actor);
        if (soldier_offside(ram, actor)) { soldier_remove(ram, actor); break; }
        if (!soldier_sequence(ram, bank, actor, 0x1ace, 0x1ad6)) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0x38, read_word(bank, 0x1aec + 2 * (AB(0x29) & 1)));
            SET(0, 0x1c6f);
        }
        soldier_tail(ram, rom, actor);
        break;
    case 0xb2150:
        queue_palette(ram, AB(0x2c) & 0x3f, 0x18, 6);
        SET(0x42, 0x60);
        SET(0, 0x2168);
        /* fall through */
    case 0xb2168:
        scroll(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = AW(0x40) & 6;
            SET(0x2a, read_word(bank, 0x1adc + frame));
            SET(0x44, read_word(bank, 0x1ae4 + frame));
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            /* 11AD: hand the private palette back. */
            uint16_t cursor = read_word(ram, 0xa488);
            ram[0xa406 + cursor] = AB(0x14);
            write_word(ram, 0xa488, (cursor + 1) & 63);
            write_word(ram, 0xa48a, read_word(ram, 0xa48a) + 1);
            write_word(ram, 0x9e06 + 8 * AB(0x14), 0);
            world_acquire_resource(ram, actor, 0x18);
            AB(0x2d) = 0x20;
            SET(0, 0x1c6f);
        }
        break;
    case 0xb21ae:
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { soldier_remove(ram, actor); break; }
        if (AW(0x21) < 0x190) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0, 0x20de);
        }
        soldier_tail(ram, rom, actor);
        break;
    case 0xb21ea: {
        unsigned side;
        world_acquire_resource(ram, actor, 0xff);
        AB(0x0c) = 8;
        SET(0x26, 0x20);
        SET(0x36, 0x80);
        AB(0x2d) = 4;
        SET(0x3c, 0xf32b);
        target_side(ram, actor, &side);
        if (script_begin(ram, rom, actor, read_word(bank, 0x1b58 + (side & 2))) < 0) return WORLD_HANDLER_STOP;
        SET(0, 0x2224);
        break;
    }
    case 0xb2224:
    case 0xb2238: {
        if (address == 0xb2224) {
            int step = script_step(ram, rom, actor);
            if (step < 0) return WORLD_HANDLER_STOP;
            if (step) {
                SET(0x3a, 0xfe00);
                SET(0, 0x2238);
            }
        } else {
            scroll(ram, actor);
            move_y(ram, actor);
        }
        if (soldier_offside(ram, actor)) { finish_animation(ram, actor); break; }
        unsigned index = (AB(0x28) & 0xf8) >> 3, material;
        AB(0x29) = bank[0x1b38 + index];
        SET(0x2a, read_word(bank, 0x1af8 + 2 * (index & 15) + AW(0x40)));
        queue_sprite(ram, rom, actor, 1);
        int burst = (terrain(ram, tiles, AW(0x21), AW(0x24), &material) & 0x5502) != 0;
        if (!burst) {
            if (material == 4) {
                splash_6d10(ram, actor);
                SET(0x40, 0x20);
                SET(0x2a, 0x8fbb);
                SET(0x3a, 0xff00);
                SET(0, 0x22e7);
            }
            AB(0x0d) = 0;
            world_surface_contact(ram, rom, actor);
            burst = !AW(0x26);
            if (!burst) {
                AB(0x0d) = 2;
                burst = world_player_contact(ram, rom, actor) != 0;
            }
        }
        if (burst) {
            small_burst(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb22e7: {
        scroll(ram, actor);
        move_y(ram, actor);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 1);
        int burst = (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff02) != 0;
        if (!burst) {
            AB(0x0d) = 0;
            world_projectile_contact(ram, rom, actor);
            burst = !AW(0x26);
            if (!burst) {
                AB(0x0d) = 2;
                burst = world_player_contact(ram, rom, actor) != 0;
            }
        }
        if (burst) {
            small_burst(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb2349:
    case 0xb237f:
        if (address == 0xb2349) {
            world_acquire_resource(ram, actor, 0x1f);
            AB(0x0c) = 4;
            SET(0x26, 8);
            SET(0x36, 0x80);
            AB(0x2d) = 4;
            SET(0x3c, 0xf32b);
            SET(0x2e, 0x200);
            SET(0x42, 8);
            heading_velocity(ram, rom, actor);
            SET(0, 0x237f);
        } else scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x2e, 0x180);
            SET(0x42, 0x18);
            SET(0x56, 1);
            SET(0, 0x2412);
        }
        bullet_tail(ram, rom, tiles, actor);
        break;
    case 0xb2412:
        scroll(ram, actor);
        if (!decrement(ram, actor + 0x56)) {
            SET(0x56, 2);
            uint16_t target = world_enemy_target(ram, actor);
            if (ram[0x264] & 3) steer_toward(ram, rom, actor, target);
        }
        move_x(ram, actor);
        move_y(ram, actor);
        if (soldier_offside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x2e, AW(0x50));
            heading_velocity(ram, rom, actor);
            SET(0, 0x2476);
        }
        bullet_tail(ram, rom, tiles, actor);
        break;
    case 0xb2476:
        scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        if (soldier_offside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 1);
        bullet_tail(ram, rom, tiles, actor);
        break;
    case 0xb24af:
        if (read_word(ram, 0xc2) >= SOLDIER_LIMIT) { finish_animation(ram, actor); break; }
        write_word(ram, 0xc2, read_word(ram, 0xc2) + 1);
        world_acquire_resource(ram, actor, 0x18);
        SET(0x0e, 6);
        AB(0x2d) = 0x29;
        SET(0x26, 0x80);
        SET(0x36, 0x80);
        SET(0x2a, 0x6540);
        SET(0x3c, 0xf323);
        soldier_rank(ram, bank, actor);
        SET(0x56, AW(0x52));
        SET(0x58, AW(0x52));
        SET(0x46, 2);
        SET(0, 0x250a);
        break;
    case 0xb250a: {
        uint16_t parent = read_word(ram, 0x84fe + AW(8));
        if (CB(parent, 0x0a)) { soldier_remove(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        if (CW(parent, 0x46)) SET(0, 0x2554);
        queue_sprite(ram, rom, actor, 1);
        break;
    }
    case 0xb2554: {
        unsigned material;
        scroll(ram, actor);
        if ((int16_t)AW(0x3a) > -0x180) SET(0x3a, AW(0x3a) - 0x20);
        move_y(ram, actor);
        queue_sprite(ram, rom, actor, 1);
        terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (material == 4) {
            AB(0x2d) = 0x20;
            SET(0x3a, 0xff00);
            SET(0, 0x25bf);
        }
        AB(0x0d) = 0;
        world_surface_contact(ram, rom, actor);
        if (!AW(0x26)) {
            surface_enemy_death_blast(ram, rom, actor);
            soldier_remove(ram, actor);
        } else register_surface_enemy(ram, actor);
        break;
    }
    case 0xb25bf:
        scroll(ram, actor);
        move_y(ram, actor);
        queue_sprite(ram, rom, actor, 1);
        if (terrain(ram, tiles, AW(0x21), AW(0x24) - 0x40, 0) & 0xff02) {
            SET(0x24, AW(0x24) - 0x0c);
            SET(0x2a, 0x655a);
            SET(0x40, 0);
            world_queue_sound(ram, 0x8e);
            SET(0, 0x2028);
        }
        AB(0x0d) = 2;
        if (!world_player_contact(ram, rom, actor)) {
            AB(0x0d) = 0;
            world_projectile_contact(ram, rom, actor);
            if (AW(0x26)) break;
        }
        loud_explosion(ram, actor);
        soldier_remove(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* 1000:D413: follow the parent at offset (bp+31, bp+34) and mirror its hit flash.
 * Returns 0 when the parent has been destroyed. */
static uint16_t wagon_attach(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + AW(8));
    if (CB(parent, 0x0a) & 2) return 0;
    SET(0x21, CW(parent, 0x21) + AW(0x31));
    SET(0x24, CW(parent, 0x24) + AW(0x34));
    uint8_t flash = CB(parent, 0x19);
    AB(0x19) = flash ? flash + 1 : 0;
    return parent;
}

static void wagon_part_removed(uint8_t *ram, uint16_t actor)
{
    finish_animation(ram, actor);
    AB(0x0a) |= 2;
}

/* Missile wagon and its parts: 1000:CCEA..D412. */
static int wagon(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x10000;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0x1ccea: {
        static const int16_t tubes[3][2] = {{-0x2d, 0x11}, {-0x1d, 9}, {-0x0d, 1}};
        world_acquire_resource(ram, actor, 0xdd);
        world_acquire_resource(ram, actor, 0xdc);
        SET(0x0e, 0x15);
        AB(0x0c) = 8;
        AB(0x2d) = 0x2a;
        SET(0x3c, 0xf5b3);
        uint16_t rank = read_word(bank, 0xccaa + read_word(ram, 0x8a));
        SET(0x26, read_word(bank, 0xccca + rank));
        SET(0x36, read_word(bank, 0xccca + rank));
        SET(0x50, read_word(bank, 0xccce + rank));
        SET(0x52, read_word(bank, 0xccce + rank));
        SET(0x54, read_word(bank, 0xccd0 + rank));
        SET(0x56, AW(0x26) - AW(0x54));
        uint16_t child;
        for (unsigned i = 0; i < 3; i++) {
            child = spawn_boss_part(ram, actor, 0xd00f);
            CSET(child, 0x14, AW(0x14));
            CB(child, 0x2c) = AB(0x15);
            CSET(child, 0x31, tubes[i][0]);
            CSET(child, 0x34, 0x18);
            CSET(child, 0x46, tubes[i][1]);
        }
        child = spawn_boss_part(ram, actor, 0xcfa1);
        CB(child, 0x2c) = AB(0x2c);
        CSET(child, 0x31, 0x30);
        CSET(child, 0x34, 0);
        write_word(ram, 0xb8b0, child);
        SET(0, 0xcdca);
        break;
    }
    case 0x1cdca:
    case 0x1cef3: {
        scroll(ram, actor);
        if (address == 0x1cdca) {
            uint16_t x = AW(0x21);
            unsigned zone = x < 0x70 ? 0 : x - 0x70 >= 0x160 ? 0x15f : x - 0x70;
            zone = (zone >> 4) & 0x1e;
            SET(0x38, read_word(bank, 0xcbd0 + zone));
            SET(0x48, read_word(bank, 0xcbe6 + zone));
            move_x(ram, actor);
            if (!AW(0x4a)) {
                if (read_word(ram, 0xb700) >= (uint16_t)((AW(0x1e) & 0xfff) << 4)) SET(0x4a, 0x40);
            } else if (!decrement(ram, actor + 0x4a)) {
                SET(0x38, 0);
                SET(0, 0xcef3);
            }
            SET(0x40, AW(0x40) + AW(0x48));
            if ((int8_t)AB(0x41) < 0) AB(0x41) = 2;
            else if ((int8_t)AB(0x41) > 2) AB(0x41) = 0;
            SET(0x2a, read_word(bank, 0xcbca + 2 * AB(0x41)));
        } else if ((int16_t)AW(0x21) < 0x10) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, rom, actor, 1);
        if (!read_word(ram, 0x286)) {
            if (!AW(0x50)) SET(0x50, AW(0x52));
            else if (!decrement(ram, actor + 0x50)) {
                SET(0x4c, 0);
                uint16_t target = world_enemy_target(ram, actor);
                if (ram[0x264] & 3) SET(0x4c, target);
            }
        }
        AB(0x0d) = 0;
        uint16_t shot = world_surface_contact(ram, rom, actor);
        if (shot) SET(0x62, shot);
        if (AW(0x58) < 12 && (int16_t)AW(0x26) < (int16_t)AW(0x56)) {
            SET(0x56, AW(0x56) - AW(0x54));
            uint16_t child = spawn_boss_part(ram, actor, 0xcf5e);
            CB(child, 0x2c) = AB(0x2c);
            CSET(child, 0x58, AW(0x58));
            SET(0x58, AW(0x58) + 2);
        }
        if (!AW(0x26)) {
            SET(0x42, 0x30);
            script_effect(ram, actor, 0x1000, 0xcc72, 4);
            AB(0x0a) |= 1;
            SET(0, 0xcf1a);
        } else register_surface_enemy(ram, actor);
        break;
    }
    case 0x1cf1a:
        scroll(ram, actor);
        queue_sprite(ram, rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x2a, 0x7b6b);
            AB(0x0a) |= 2;
            SET(0, 0xcf39);
        }
        break;
    case 0x1cf39:
        scroll(ram, actor);
        if ((int16_t)AW(0x21) < 0x10) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1cf5e: {
        uint16_t slot = AW(0x58);
        AB(0x2d) = 0x2a;
        SET(0x2a, read_word(bank, 0xcc4e + slot));
        SET(0x31, read_word(bank, 0xcc5a + slot));
        SET(0x34, read_word(bank, 0xcc66 + slot));
        SET(0, 0xcf82);
    }
        /* fall through */
    case 0x1cf82:
        if (!wagon_attach(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 1);
        break;
    case 0x1cfa1:
        AB(0x2d) = 0x2a;
        SET(0x2a, 0x7ad5);
        SET(0x3c, 0xf5cb);
        SET(0, 0xcfb4);
        /* fall through */
    case 0x1cfb4: {
        uint16_t parent = wagon_attach(ram, actor);
        if (parent) {
            if (CW(parent, 0x38)) SET(0x21, AW(0x21) + ((decrement(ram, actor + 0x42) & 4) >> 2));
            queue_sprite(ram, rom, actor, 0);
            break;
        }
        write_word(ram, 0xb8b0, 0);
        SET(0x2a, 0x7af7);
        surface_enemy_death_blast(ram, rom, actor);
        SET(0, 0xcfea);
    }
        /* fall through */
    case 0x1cfea:
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1d00f:
        AB(0x2d) = 0x2a;
        SET(0x2a, 0x7ba5);
        SET(0, 0xd01d);
        /* fall through */
    case 0x1d01d:
    case 0x1d058: {
        uint16_t parent = wagon_attach(ram, actor);
        if (!parent) {
            surface_enemy_death_blast(ram, rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (address != 0x1d058) {
            if (!CW(parent, 0x50)) {
                SET(0x40, 0);
                SET(0x44, AW(0x46));
                SET(0, 0xd058);
            }
        } else if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40), sprite = read_word(bank, 0xcbfc + frame);
            if (sprite == 0xffff) SET(0, 0xd01d);
            else {
                SET(0x2a, sprite);
                SET(0x44, read_word(bank, 0xcc0e + frame));
                SET(0x40, frame + 2);
                if (frame == 6 || frame == 8) {
                    uint16_t child = spawn_boss_part(ram, actor, 0xd0be);
                    CSET(child, 0x14, AW(0x14));
                    CB(child, 0x2c) = AB(0x15);
                    CSET(child, 0x21, AW(0x21));
                    CSET(child, 0x24, AW(0x24));
                    world_queue_sound(ram, 0x55);
                }
            }
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0x1d0be:
        AB(0x2d) = 0x2b;
        SET(0x3a, 0x300);
        SET(0x26, 0x300);
        SET(0x36, 0x100);
        SET(0x3c, 0xf5bb);
        SET(0x44, 1);
        spawn_boss_part(ram, actor, 0xd3be);
        SET(0, 0xd0eb);
        /* fall through */
    case 0x1d0eb:
    case 0x1d1d2:
    case 0x1d1fe: {
        unsigned material;
        scroll(ram, actor);
        move_y(ram, actor);
        if (address == 0x1d1fe) {
            int16_t top = (ram[0x26e] & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
            if ((int16_t)AW(0x24) < top) { wagon_part_removed(ram, actor); break; }
            AB(0x0d) = 0;
            int burst = world_player_contact(ram, rom, actor) != 0;
            if (!burst) {
                AB(0x0d) = 0;
                world_projectile_contact(ram, rom, actor);
                burst = !AW(0x26);
            }
            if (!burst && (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff02)) {
                snap_row_below(ram, actor);
                SET(0x24, AW(0x24) - 4);
                for (unsigned i = 0; i < 2; i++) {
                    uint16_t child = world_spawn_child(ram, read_word(ram, 0x9a), 0xd2cd);
                    if (child) CSET(child, 2, 0x1000); else child = 0x8480;
                    CB(child, 0x2c) = AB(0x14);
                    CSET(child, 0x21, AW(0x21) + (i ? -4 : 4));
                    CSET(child, 0x24, AW(0x24));
                    if (i) CB(child, 0x29) = 1;
                    CSET(child, 0x1e, 0x10);
                }
                burst = 1;
            }
            if (burst) {
                small_burst(ram, actor);
                wagon_part_removed(ram, actor);
                break;
            }
        } else {
            if (address != 0x1d1d2) {
                if (AW(0x24) >= 0x1c0) {
                    SET(0x42, 0x10);
                    SET(0, 0xd179);
                    break;
                }
            } else if (terrain(ram, tiles, AW(0x21), AW(0x24), &material), material == 4) {
                SET(0x3a, 0xfe80);
                splash_6d10(ram, actor);
                SET(0, 0xd1fe);
            }
            AB(0x0d) = 0;
            world_surface_contact(ram, rom, actor);
            if (!AW(0x26)) {
                small_burst(ram, actor);
                wagon_part_removed(ram, actor);
                break;
            }
        }
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0xcc1e + AW(0x40)));
            SET(0x44, 3);
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 6) SET(0x40, 0);
        }
        if (!queue_sprite(ram, rom, actor, 0)) wagon_part_removed(ram, actor);
        break;
    }
    case 0x1d179: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t aim = CW(read_word(ram, 0x84fc + AW(8)), 0x4c);
        uint16_t x = aim ? CW(aim, 0x21) : 0xc0;
        x += world_random_byte(ram) & 0x3f;
        x += (world_random_byte(ram) & 0x1f) - 0x10;
        SET(0x21, x);
        SET(0x24, 0x180);
        AB(0x29) = 2;
        AB(0x2d) = 4;
        SET(0x3a, 0xfd00);
        SET(0, 0xd1d2);
        break;
    }
    case 0x1d2cd:
        AB(0x2d) = 0x2a;
        SET(0x36, 0x100);
        SET(0x3c, 0xf5c3);
        SET(0x44, 1);
        SET(0x42, 4);
        SET(0, 0xd2ea);
        /* fall through */
    case 0x1d2ea: {
        scroll(ram, actor);
        if (AW(0x21) < 0x58 || AW(0x21) >= 0x1a8) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40), sprite = read_word(bank, 0xcc24 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(bank, 0xcc34 + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        if (AW(0x40) < 8) {
            AB(0x0d) = 0;
            world_player_contact(ram, rom, actor);
        }
        if (decrement(ram, actor + 0x42)) break;
        if (!decrement(ram, actor + 0x1e)) { finish_animation(ram, actor); break; }
        uint16_t child = world_spawn_child(ram, read_word(ram, 0x9a), 0xd2cd);
        if (child) CSET(child, 2, 0x1000); else child = 0x8480;
        CB(child, 0x2c) = AB(0x2c);
        CB(child, 0x29) = AB(0x29);
        CSET(child, 0x1e, AW(0x1e));
        CSET(child, 0x21, AW(0x21) + read_word(bank, 0xcc42 + 2 * (AB(0x29) & 1)));
        uint16_t cell, step = 0;
        unsigned here = terrain_cell(ram, tiles, AW(0x21), AW(0x24) - 4, &cell);
        if (!((1u << (here & 15)) & 0xff02)) step = 0xfff8;
        else {
            uint32_t above = 0xd4000 + (uint16_t)(cell - 0x100) + 3;
            unsigned material = (above < 0xe0000 ? tiles[above - 0xd0000] : ram[above - 0xe0000]) >> 3;
            if ((1u << (material & 15)) & 0xff02) step = 8;
        }
        CSET(child, 0x24, AW(0x24) + step);
        break;
    }
    case 0x1d3be:
        AB(0x2c) = 3;
        AB(0x2d) = 0x2b;
        SET(0x42, 4);
        SET(0, 0xd3d0);
        /* fall through */
    case 0x1d3d0:
        if (!decrement(ram, actor + 0x42)) SET(0, 0xd3ef);
        break;
    case 0x1d3ef: {
        uint16_t parent = wagon_attach(ram, actor);
        if (!parent) { finish_animation(ram, actor); break; }
        AB(0x29) = CB(parent, 0x29);
        SET(0x2a, read_word(bank, 0xcc46 + (AW(0x40) & 6)));
        SET(0x40, AW(0x40) + 2);
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* Word in the near playfield segment D400; offsets may run past its 16 KiB. */
static uint8_t *near_tile(uint8_t *ram, uint8_t *tiles, uint16_t offset)
{
    uint32_t linear = 0xd4000 + offset;
    return linear < 0xe0000 ? tiles + (linear - 0xd0000) : ram + (linear - 0xe0000);
}

/* Collapsing tower, its wall panels, rubble and smoke: 1000:F66D..FF97. */
static int tower(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x10000;
    unsigned kind = AW(0x1e) & 6;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0x1f66d:
    case 0x1fe2f: {
        int base = address == 0x1f66d;
        if (base) {
            SET(0x0e, 0x3d);
            SET(0x3c, read_word(bank, 0xdca8 + kind));
            SET(0x2a, read_word(bank, 0xdcb0 + kind));
        }
        SET(0x42, read_word(bank, 0xe2b0 + kind));
        if (base) {
            SET(0x31, read_word(bank, 0xe448 + kind));
            world_acquire_resource(ram, actor, read_word(bank, 0xe2a8 + kind));
            AB(0x2c) = AB(0x15) = bank[0xe2a4 + (kind >> 1)];
            AB(0x2d) = 0x3e;
            SET(0x26, 0x80);
            SET(0x36, 0x80);
        }
        do {
            uint16_t child = spawn_boss_part(ram, actor, 0xfc89);
            CSET(child, 0x1e, AW(0x1e) | (base ? 0 : 1));
            uint16_t row = read_word(bank, 0xe2b8 + kind) + ((AW(0x42) - 1) << 2);
            CSET(child, 0x21, AW(0x21) + read_word(bank, row));
            CSET(child, 0x24, AW(0x24) + read_word(bank, row + 2));
            if (base) CB(child, 0x2c) = AB(0x14);
        } while (decrement(ram, actor + 0x42));
        if (base) SET(0, 0xf70a);
        else finish_animation(ram, actor);
        break;
    }
    case 0x1f70a:
        scroll(ram, actor);
        if (AW(0x21) < AW(0x31)) {
            SET(0x56, 1);
            SET(0, 0xf722);
        }
        break;
    case 0x1f722:
        scroll(ram, actor);
        if (AW(8) >= CW(AW(4), 8)) {
            SET(0x31, read_word(bank, 0xe450 + kind));
            SET(0, 0xf747);
        }
        break;
    case 0x1f747:
    case 0x1f7ac:
    case 0x1f801:
    case 0x1f88d:
    case 0x1f94f: {
        scroll(ram, actor);
        if ((int16_t)AW(0x21) <= (int16_t)AW(0x31)) { finish_animation(ram, actor); break; }
        if (address != 0x1f94f) queue_sprite(ram, rom, actor, 0);
        uint16_t child;
        if (address == 0x1f747) {
            AB(0x0d) = 0;
            uint16_t attacker = world_surface_contact(ram, rom, actor);
            if (AW(0x26)) break;
            world_award_score(ram, rom, actor, attacker);
            child = script_effect(ram, actor, read_word(bank, 0xdcba + 2 * kind), read_word(bank, 0xdcb8 + 2 * kind), 4);
            CB(child, 0x2d) = 0x1f;
            SET(0x42, 0x26);
            SET(0, 0xf7ac);
            break;
        }
        if (decrement(ram, actor + 0x42)) break;
        if (address == 0x1f7ac) {
            child = script_effect(ram, actor, read_word(bank, 0xddba + 2 * kind), read_word(bank, 0xddb8 + 2 * kind), 8);
            CB(child, 0x2d) = 0x1d;
            SET(0x42, 0x60);
            SET(0, 0xf801);
        } else if (address == 0x1f801) {
            child = script_effect(ram, actor, read_word(bank, 0xde2a + 2 * kind), read_word(bank, 0xde28 + 2 * kind), 1);
            CB(child, 0x2d) = 0x1d;
            debris_spawner(ram, actor, read_word(bank, 0xdf8a + 2 * kind), read_word(bank, 0xdf88 + 2 * kind));
            SET(0x50, read_word(bank, 0xe232 + kind));
            SET(0x52, read_word(bank, 0xe23a + kind));
            SET(0x54, read_word(bank, 0xe242 + kind));
            SET(0x42, 0x20);
            SET(0, 0xf88d);
        } else if (address == 0x1f88d) {
            int16_t left = AW(0x21) - 0x50;
            int draw = 1;
            if (left <= 0) {
                /* Clip the columns that already scrolled off the left edge. */
                uint16_t hidden = (uint16_t)(left < 0 ? -left : left) & 0x1e0;
                unsigned columns = (hidden >> 5) + 1;
                int remaining = (int8_t)AB(0x55) - (int)(columns & 255);
                AB(0x55) = (uint8_t)remaining;
                if (remaining <= 0) draw = 0;
                else {
                    AB(0x50) += (uint8_t)((hidden >> 1) + 0x10);
                    SET(0x52, AW(0x52) + columns * 6);
                }
            }
            if (draw) tile_grid(ram, rom, tiles, 0x4000, AW(0x50), AW(0x52), AW(0x54));
            child = spawn_boss_part(ram, actor, 0xf9fc);
            CSET(child, 0x21, AW(0x21) + 0x20);
            CSET(child, 0x24, AW(0x24) + 0x80);
            CSET(child, 0x1e, 0);
            child = spawn_boss_part(ram, actor, 0xf9fc);
            CSET(child, 0x21, AW(0x21) + 0x90);
            CSET(child, 0x24, AW(0x24) + 0xa0);
            CSET(child, 0x1e, 8);
            SET(0x42, 0x40);
            SET(0x44, 0x20);
            SET(0, 0xf94f);
        } else {
            SET(0x42, 8);
            for (unsigned i = 0; i < 3; i++) {
                child = spawn_boss_part(ram, actor, 0xfbc6);
                CSET(child, 0x21, AW(0x21));
                CSET(child, 0x24, AW(0x24));
                CSET(child, 0x1e, (AW(0x1e) << 3) + 4 * i);
            }
            if (!decrement(ram, actor + 0x44)) finish_animation(ram, actor);
        }
        break;
    }
    case 0x1f9fc:
        world_acquire_resource(ram, actor, 0x79);
        AB(0x2d) = 0x20;
        SET(0x3a, 0xfe00);
        SET(0x2a, read_word(bank, 0xdc46 + (AW(0x1e) & 8)));
        SET(0x42, 5);
        SET(0, 0xfa27);
        break;
    case 0x1fa27: {
        unsigned material;
        scroll(ram, actor);
        move_y(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x42, 8);
            uint16_t child = tagged_effect(ram, actor, 0x7a, 0x7601);
            CSET(child, 0x31, CW(child, 0x31) - 0x18);
            CB(child, 0x2d) = AB(0x2d);
            child = tagged_effect(ram, actor, 0x7a, 0x7601);
            CSET(child, 0x34, CW(child, 0x34) - 0x10);
            CB(child, 0x2d) = AB(0x2d);
            child = tagged_effect(ram, actor, 0x7a, 0x7601);
            CSET(child, 0x31, CW(child, 0x31) + 0x1a);
            CSET(child, 0x34, CW(child, 0x34) + 0x18);
            CB(child, 0x2d) = AB(0x2d) + 1;
        }
        terrain(ram, tiles, AW(0x21), AW(0x24) - 0x10, &material);
        if (material == 4) {
            uint16_t child = script_effect(ram, actor, 0x1000, 0xdc74, 2);
            CB(child, 0x2d) = 0xff;
            child = script_effect(ram, actor, 0x1000, 0xdc9a, 5);
            CB(child, 0x2d) = 1;
            SET(0x42, 8);
            SET(0x40, 2);
            SET(0x44, 1);
            SET(0, 0xfad9);
        }
        break;
    }
    case 0x1fad9:
    case 0x1fb6f: {
        int sinking = address == 0x1fb6f;
        scroll(ram, actor);
        if (AW(0x42)) {
            SET(0x3a, AW(0x3a) + (sinking ? 0x10 : 0x20));
            decrement(ram, actor + 0x42);
        }
        move_y(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(bank, (sinking ? 0xdc5e : 0xdc46) + frame + AW(0x1e)));
            SET(0x44, read_word(bank, (sinking ? 0xdc6e : 0xdc56) + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        if (!sinking && (terrain(ram, tiles, AW(0x21), AW(0x24) - 0x0c + 2 * AW(0x1e), 0) & 0xff02)) {
            uint16_t child = tagged_effect(ram, actor, 0x7b, 0x762d);
            CSET(child, 0x34, CW(child, 0x34) + 2 * AW(0x1e) + 4);
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0x42, 0x10);
            SET(0, 0xfb6f);
        }
        break;
    }
    case 0x1fbc6: {
        unsigned slot = AW(0x1e) & 0x3c;
        world_acquire_resource(ram, actor, 0x7a);
        AB(0x2d) = 0x3f;
        SET(0x44, 1);
        SET(0x3a, 0x80);
        SET(0x21, AW(0x21) + read_word(bank, 0xe24a + slot));
        SET(0x24, AW(0x24) + read_word(bank, 0xe24c + slot));
        SET(0x31, AW(0x21));
        SET(0x34, AW(0x24));
        SET(0, 0xfc05);
        break;
    }
    case 0x1fc05: {
        SET(0x31, AW(0x31) + read_word(ram, 0xb71a));
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        SET(0x21, AW(0x21) + read_word(ram, 0xb71a));
        move_y(ram, actor);
        uint16_t bottom = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        if (bottom < AW(0x24) || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        sine_x(ram, rom, actor, AW(0x24) - AW(0x34), 0x200, 8);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40), sprite = read_word(bank, 0xe27a + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(bank, 0xe290 + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0x1fc89: {
        if (AW(0x1e) & 1) AB(0x2c) = bank[0xe54e + (kind >> 1)];
        AB(0x2d) = 0x3e;
        SET(0x26, 0x80);
        SET(0x36, 0x80);
        uint16_t frames = read_word(bank, 0xe3c8 + kind);
        SET(0x46, frames);
        SET(0x2a, read_word(bank, frames + AW(0x40)));
        uint16_t cell;
        terrain_cell(ram, tiles, AW(0x21), AW(0x24), &cell);
        SET(0x50, cell + 2);
        SET(0, 0xfcde);
        break;
    }
    case 0x1fcde: {
        int fragment = AW(0x1e) & 1;
        scroll(ram, actor);
        if (fragment ? (int16_t)AW(0x21) > 0x1b0 : AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (AW(0x42)) {
            if (!decrement(ram, actor + 0x42)) SET(0x40, 0);
            else SET(0x40, AW(0x40) + 2);
            SET(0x2a, read_word(bank, AW(0x40) + AW(0x46)));
        }
        queue_sprite(ram, rom, actor, 0);
        if (!fragment && CW(read_word(ram, 0x84fe + AW(8)), 0x56)) {
            SET(0x3c, read_word(bank, 0xe440 + (AW(0x1e) & 2)));
            AB(0x0d) = 0;
            world_surface_contact(ram, rom, actor);
            if (!AW(0x26) || (near_tile(ram, tiles, AW(0x50))[1] & 0x10)) {
                world_queue_sound(ram, 0x6c);
                SET(0x44, 1);
                SET(0, 0xfdbb);
                break;
            }
        }
        if (!AW(0x42)) {
            SET(0x3c, read_word(bank, 0xe444 + (AW(0x1e) & 2)));
            AB(0x0d) = 3;
            if (world_surface_contact(ram, rom, actor)) SET(0x42, 5);
        }
        break;
    }
    case 0x1fdbb:
        scroll(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            static const int16_t around[6] = {-8, -12, 8, 12, 0x300, -0x300};
            uint16_t frame = AW(0x40), sprite = read_word(bank, frame + AW(0x46));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(bank, 0xe41c + frame));
            SET(0x40, frame + 2);
            if (frame == 12)
                for (unsigned i = 0; i < 6; i++) near_tile(ram, tiles, AW(0x50) + around[i])[1] |= 0x10;
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1fe8f: {
        static const uint8_t resources[3] = {0x80, 0x7f, 0x81};
        for (unsigned i = 0; i < 3; i++) {
            world_acquire_resource(ram, actor, resources[i]);
            uint16_t job = 0x9e06 + 8 * (AB(0x2c) & 0x3f);
            write_word(ram, job + 2, 0x80);
            write_word(ram, job + 4, 5);
            write_word(ram, job + 6, 0x1020);
        }
        finish_animation(ram, actor);
        break;
    }
    case 0x1ff27:
        AB(0x2c) = 0x11;
        SET(0, 0xff30);
        /* fall through */
    case 0x1ff30:
        SET(0x40, read_word(bank, 0xe458 + (world_random_byte(ram) & 6)));
        SET(0x42, 1);
        SET(0, 0xff4b);
        /* fall through */
    case 0x1ff4b: {
        if (ram[0x262] & 0x80) { finish_animation(ram, actor); break; }
        if (decrement(ram, actor + 0x42)) break;
        uint16_t row = AW(0x40);
        if (read_word(bank, row) == 0xffff) { SET(0, 0xff30); break; }
        queue_palette(ram, AB(0x2c) & 0x3f, read_word(bank, row), read_word(bank, row + 2));
        SET(0x42, read_word(bank, row + 4));
        SET(0x40, row + 6);
        break;
    }
    case 0x1ff98:
        for (unsigned i = 0; i < 16; i += 4) {
            uint16_t job = read_word(bank, 0xe53e + i);
            write_word(ram, 0x9e06 + job, read_word(bank, 0xe540 + i));
            write_word(ram, 0x9e08 + job, 0x40);
        }
        finish_animation(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* 6C78 with the child returned. */
static uint16_t death_blast(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    CSET(child, 0x4c, 0x74c3);
    CSET(child, 0x4a, 0x7675);
    CSET(child, 0x44, 0x5d);
    CB(child, 0x2d) = 0x20;
    CSET(child, 0x56, 0x30);
    CSET(child, 0x58, 0x80);
    world_queue_sound(ram, 0x3f);
    return child;
}

/* 1000:FFCC: follow the parent at offset (bp+31, bp+34). */
static uint16_t follow_parent(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = read_word(ram, 0x84fe + AW(8));
    SET(0x21, CW(parent, 0x21) + AW(0x31));
    SET(0x24, CW(parent, 0x24) + AW(0x34));
    return parent;
}

static void clear_plank_tiles(uint8_t *ram, uint8_t *tiles, uint16_t cell)
{
    for (unsigned i = 2; i < 16; i += 4) near_tile(ram, tiles, cell + i)[1] &= 7;
}

static int plank_gone(const uint8_t *ram, uint16_t parent, uint16_t index)
{
    return CW(parent, 0x50 + ((index & 0x10) >> 3)) >> (index & 15) & 1;
}

/* Barrier gate, its tower and the plank bridge: 1000:EC04..F3FF. */
static int gate(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t child, parent;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0x1ec04:
        AB(0x2c) = 0x0d;
        child = world_spawn_child(ram, actor, 0xedb3);
        if (!child) SET(0x50, AW(0x50) | 1);
        else {
            CSET(child, 2, 0x1000);
            CSET(child, 0x31, 0);
            CSET(child, 0x34, 0xff88);
            CB(child, 0x2c) = AB(0x2c);
            spawn_bank7(ram, actor, 0xcca9);
            child = spawn_boss_part(ram, actor, 0xef18);
            CSET(child, 0x31, 0xff30);
            CSET(child, 0x34, 0x10);
            CSET(child, 0x2a, 0xc064);
            child = spawn_boss_part(ram, actor, 0xef60);
            CSET(child, 0x31, 0xff68);
            CSET(child, 0x34, 8);
            CB(child, 0x2c) = AB(0x2c);
            child = spawn_boss_part(ram, actor, 0xefea);
            CSET(child, 0x31, 0xff80);
            CSET(child, 0x34, 8);
            CB(child, 0x2c) = AB(0x2c);
            CSET(child, 0x1e, 6);
            child = spawn_boss_part(ram, actor, 0xf188);
            CSET(child, 0x31, 0x38);
            CSET(child, 0x34, 0x28);
            CSET(child, 0x2a, 0xc014);
        }
        SET(0, 0xecad);
        break;
    case 0x1ecad:
        scroll(ram, actor);
        if ((int16_t)read_word(ram, 0xb700) >= 0xdbf) {
            ram[0x26f] |= 0x80;
            SET(0, 0xecc6);
        }
        break;
    case 0x1ecc6:
        if (AW(0x50) & 1) {
            SET(0x42, 0x40);
            SET(0, 0xecd8);
        }
        break;
    case 0x1ecd8:
        if (decrement(ram, actor + 0x42)) break;
        AB(0x2d) = 0x3e;
        SET(0x2a, 0xc286);
        AB(0x2c) = 0x23;
        child = script_effect(ram, actor, 0x1000, 0xd768, 2);
        CB(child, 0x2d) = 0x1f;
        SET(0x42, 0x40);
        SET(0, 0xed07);
        break;
    case 0x1ed07:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        script_effect(ram, actor, 0x1000, 0xd7ac, 1);
        SET(0x42, 0x10);
        SET(0, 0xed2a);
        break;
    case 0x1ed2a:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        tile_grid(ram, rom, tiles, 0x4000, 0x1070, 0x3c90, 0x408);
        SET(0x50, AW(0x50) | 2);
        ram[0x26f] &= 0x7f;
        finish_animation(ram, actor);
        break;
    case 0x1ed64:
        if (CB(read_word(ram, 0x84fe + AW(8)), 0x0a) & 1) { finish_animation(ram, actor); break; }
        if (decrement(ram, actor + 0x42)) break;
        queue_palette(ram, AB(0x2c) & 0x3f, read_word(bank, 0xd714 + (AW(0x40) & 2)), 5);
        SET(0x42, 0x21);
        SET(0x40, AW(0x40) + 2);
        break;
    case 0x1edb3:
        AB(0x2d) = 0x2e;
        SET(0x2a, 0xbf82);
        SET(0x26, 1);
        child = world_spawn_child(ram, actor, 0xee3d);
        if (child) CSET(child, 2, 0x1000);
        else SET(0x26, 0);
        SET(0, 0xedd9);
        break;
    case 0x1edd9:
        follow_parent(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        if (AW(0x26)) break;
        script_effect(ram, actor, 0x1000, 0xd718, 2);
        debris_spawner(ram, actor, 0x1000, 0xd808);
        SET(0x42, 0x20);
        SET(0, 0xee0c);
        break;
    case 0x1ee0c:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        parent = read_word(ram, 0x84fe + AW(8));
        CSET(parent, 0x50, CW(parent, 0x50) | 1);
        finish_animation(ram, actor);
        break;
    case 0x1ee3d:
        AB(0x2c) = 9;
        SET(0x0e, 0x38);
        AB(0x2d) = 0x2e;
        SET(0x3c, 0xf9f3);
        SET(0x44, 0);
        SET(0x2a, read_word(bank, 0xd6f0));
        SET(0x26, read_word(bank, 0xd6f4 + read_word(ram, 0x88)));
        SET(0x36, AW(0x26));
        child = spawn_boss_part(ram, actor, 0xed64);
        CB(child, 0x2c) = AB(0x2c);
        CSET(child, 0x42, 2);
        SET(0, 0xee85);
        /* fall through */
    case 0x1ee85:
    case 0x1ee97: {
        int open = (int16_t)read_word(ram, 0xb700) >= 0xdbf;
        if (address != 0x1ee97 && open) {
            SET(0x44, 1);
            SET(0, 0xee97);
        }
        parent = follow_parent(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0xd6f0 + (AW(0x40) & 2)));
            SET(0x44, AB(0x19) ? 1 : 2);
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        if (AB(0x19)) decrement(ram, actor + 0x19);
        if (!open) break;
        AB(0x0d) = 0;
        uint16_t attacker = world_projectile_contact(ram, rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, rom, actor, attacker);
        CSET(parent, 0x26, 0);
        uint16_t cursor = read_word(ram, 0xa488);
        ram[0xa406 + cursor] = AB(0x14);
        write_word(ram, 0xa488, (cursor + 1) & 63);
        write_word(ram, 0xa48a, read_word(ram, 0xa48a) + 1);
        write_word(ram, 0x9e06 + 8 * AB(0x14), 0);
        finish_animation(ram, actor);
        break;
    }
    case 0x1ef18:
        AB(0x2c) = 0x21;
        AB(0x2d) = 0x2e;
        SET(0, 0xef26);
        break;
    case 0x1ef26:
        parent = follow_parent(ram, actor);
        if (CW(parent, 0x50) & 1) SET(0, 0xef3b);
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1ef3b:
        scroll(ram, actor);
        if (AW(0x21) < 0x20) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1ef60:
        AB(0x2d) = 0x3d;
        SET(0x2a, 0xbfdc);
        SET(0, 0xef6f);
        break;
    case 0x1ef6f:
        parent = follow_parent(ram, actor);
        if (CW(parent, 0x50) & 1) {
            SET(0x42, read_word(bank, 0xd942));
            SET(0, 0xef8b);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1ef8b:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        SET(0x42, 0x10);
        death_blast(ram, actor);
        debris_spawner(ram, actor, 0x1000, 0xd8a6);
        SET(0, 0xefb0);
        break;
    case 0x1efb0:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        SET(0x2a, 0xc00a);
        SET(0, 0xefc5);
        break;
    case 0x1efc5:
        scroll(ram, actor);
        if (AW(0x21) == 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1efea:
        AB(0x2d) = (uint8_t)(2 * AW(0x1e) + 0x30);
        SET(0x2a, 0xbfe6);
        SET(0x3c, 0xfb4b);
        if (decrement(ram, actor + 0x1e)) {
            child = spawn_boss_part(ram, actor, 0xefea);
            CSET(child, 0x31, 0x20);
            CB(child, 0x2c) = AB(0x2c);
            CSET(child, 0x1e, AW(0x1e));
        } else SET(0x2a, 0xbff8);
        SET(0, 0xf02e);
        break;
    case 0x1f02e:
        parent = follow_parent(ram, actor);
        if (CW(parent, 0x50) & 1) {
            SET(0x50, AW(0x50) | 1);
            SET(0x42, read_word(bank, 0xd936 + 2 * AW(0x1e)));
            SET(0, 0xf055);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1f055:
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        child = death_blast(ram, actor);
        CSET(child, 0x31, 0xfff0);
        CB(child, 0x2d) = AB(0x2d) + 1;
        debris_spawner(ram, actor, 0x1000, 0xd8a6);
        SET(0x40, 0);
        SET(0x44, 1);
        SET(0x42, 0x28);
        SET(0x3a, 0x100);
        clear_plank_tiles(ram, tiles, read_word(bank, 0xd944 + 2 * (AW(0x1e) & 7)));
        SET(0, 0xf0c1);
        break;
    case 0x1f0c1:
    case 0x1f0cf: {
        unsigned material;
        if (address == 0x1f0c1) {
            SET(0x3a, AW(0x3a) - 0x10);
            if (!decrement(ram, actor + 0x42)) SET(0, 0xf0cf);
        }
        scroll(ram, actor);
        move_y(ram, actor);
        SET(0x60, AW(0x60) + 1);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(bank, 0xd90a + frame));
            SET(0x44, read_word(bank, 0xd920 + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (material == 4) {
            child = surface_splash(ram, actor, 0x7585, 0x769b);
            CB(child, 0x2c) = 0x0a;
            CB(child, 0x2d) = 0x20;
            CSET(child, 0x56, 0x30);
            CSET(child, 0x58, 0x80);
            snap_row_below(ram, child);
            world_queue_sound(ram, 0x82);
            SET(0x3a, 0xff00);
            SET(0x2a, 0xc25c);
            SET(0, 0xf122);
        }
        break;
    }
    case 0x1f122:
        if (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff02) {
            SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 1));
            SET(0, 0xf13b);
        }
        /* fall through */
    case 0x1f13b:
        scroll(ram, actor);
        move_y(ram, actor);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1f188:
        AB(0x2c) = 0x23;
        AB(0x2d) = 0x2e;
        SET(0, 0xf196);
        break;
    case 0x1f196:
        parent = follow_parent(ram, actor);
        if (CW(parent, 0x50) & 2) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1f1ba:
        AB(0x2c) = 0x0d;
        SET(0x42, read_word(ram, 0xb700) - 0x20);
        SET(0x31, AW(0x21));
        SET(0x50, 0);
        SET(0x52, 0xfffe);
        SET(0, 0xf1dd);
        break;
    case 0x1f1dd:
        scroll(ram, actor);
        SET(0x31, AW(0x31) + read_word(ram, 0xb71a));
        if ((int16_t)AW(0x42) > (int16_t)read_word(ram, 0xb700)) break;
        SET(0x42, AW(0x42) + 0x20);
        child = spawn_boss_part(ram, actor, 0xf23b);
        CSET(child, 0x21, AW(0x31));
        CSET(child, 0x24, 0x118);
        CB(child, 0x2c) = AB(0x2c);
        CSET(child, 0x1e, AW(0x44) & 0x1f);
        SET(0x31, AW(0x31) + 0x20);
        SET(0x44, AW(0x44) + 1);
        if (AW(0x44) >= 0x11) finish_animation(ram, actor);
        break;
    case 0x1f23b:
        SET(0x0e, 0x3b);
        AB(0x2d) = 0x2e;
        SET(0x3c, 0xf9fb);
        SET(0x26, 0x200);
        SET(0x36, 0x200);
        if (AW(0x1e) & 3) SET(0x2a, 0xbfe6);
        SET(0, 0xf265);
        break;
    case 0x1f265: {
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        parent = read_word(ram, 0x84fe + AW(8));
        int post = !(AW(0x1e) & 3);
        int left = plank_gone(ram, parent, AW(0x1e) - 1), right = plank_gone(ram, parent, AW(0x1e) + 1);
        if (post) {
            SET(0x2a, 0xc01e);
            if (left && right) SET(0x50, 1);
            else if (left) SET(0x2a, 0xc052);
            else if (right) SET(0x2a, 0xc038);
        }
        queue_sprite(ram, rom, actor, 1);
        if (post ? !AW(0x50) : !left && !right) {
            AB(0x0d) = 0;
            uint16_t attacker = world_surface_contact(ram, rom, actor);
            if (AW(0x26)) break;
            world_award_score(ram, rom, actor, attacker);
        }
        child = death_blast(ram, actor);
        CB(child, 0x2d) = AB(0x2d) + 1;
        SET(0x42, 0x10);
        SET(0, 0xf377);
        break;
    }
    case 0x1f377: {
        scroll(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        parent = read_word(ram, 0x84fe + AW(8));
        uint16_t word = parent + 0x50 + ((AW(0x1e) & 0x10) >> 3);
        write_word(ram, word, read_word(ram, word) | 1u << (AW(0x1e) & 15));
        death_blast(ram, actor);
        debris_spawner(ram, actor, 0x1000, 0xd8d8);
        SET(0x40, 0);
        SET(0x44, 1);
        SET(0x42, 0x28);
        SET(0x3a, 0x100);
        clear_plank_tiles(ram, tiles, read_word(bank, 0xd956 + 2 * (AW(0x1e) & 15)));
        SET(0, 0xf0c1);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* 6CA9: large blast without sound. */
static uint16_t large_blast(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    CSET(child, 0x4c, 0x74e5);
    CSET(child, 0x4a, 0x7675);
    CSET(child, 0x44, 0x5d);
    CB(child, 0x2d) = 0x20;
    CSET(child, 0x56, 0x40);
    CSET(child, 0x58, 0);
    return child;
}

/* 7000:DAFE: a cart that ran off its track starts to fall. Returns 1 when it does. */
static int cart_runs_off(uint8_t *ram, const uint8_t *tiles, uint16_t actor)
{
    if (AW(0x21) >= 0x1a0 || (terrain(ram, tiles, AW(0x21) + 0x10, AW(0x24) - 4, 0) & 0xff02)) return 0;
    SET(0x4c, (AW(0x1e) & 3) << 4);
    SET(0x40, 0);
    SET(0x44, 1);
    SET(0x4e, AW(0x4e) | 1);
    SET(0, 0xceec);
    return 1;
}

/* Mine carts, their gunner and wreckage: 7000:CCA9..D2DC. */
static int carts(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x70000;
    uint16_t child;
    unsigned material;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0x7cca9:
        world_acquire_resource(ram, actor, 0xd1);
        world_acquire_resource(ram, actor, 0xd1);
        world_acquire_resource(ram, actor, 0xd2);
        world_acquire_resource(ram, actor, 0xd4);
        SET(0x46, read_word(bank, 0xc9d4 + read_word(ram, 0x8a)));
        SET(0x42, 0x40);
        SET(0, 0xcce0);
        break;
    case 0x7cce0:
        if (ram[0x26f] & 0x80) SET(0, 0xccee);
        break;
    case 0x7ccee:
        if (decrement(ram, actor + 0x42)) break;
        child = spawn_bank7(ram, actor, 0xcdcc);
        CSET(child, 0x21, 0x1b8);
        CSET(child, 0x24, 0x120);
        CB(child, 0x2c) = AB(0x17);
        CSET(child, 0x1e, 3);
        SET(0x42, 0x28);
        SET(0x52, 0x40);
        world_queue_sound(ram, 0x81);
        SET(0, 0xcd2b);
        break;
    case 0x7cd2b:
        if (!decrement(ram, actor + 0x42)) {
            SET(0x42, 0x28);
            child = spawn_bank7(ram, actor, 0xcdcc);
            CSET(child, 0x21, 0x1b8);
            CSET(child, 0x24, 0x120);
            uint16_t type = read_word(bank, 0xc9f4 + (AW(0x40) & 14) + AW(0x46));
            CSET(child, 0x1e, type);
            CB(child, 0x2c) = AB(0x14 + (type & 3));
            CSET(child, 0x14, AW(0x15));
            SET(0x40, AW(0x40) + 2);
        }
        if (AW(0x4a)) {
            if (!decrement(ram, actor + 0x4a)) SET(0x48, 0);
        } else if (AW(0x48) & 1) SET(0x4a, 0x18);
        if (CW(read_word(ram, 0x84fe + AW(8)), 0x50) & 2) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x52)) {
            SET(0x52, 0x40);
            world_queue_sound(ram, 0x81);
        }
        break;
    case 0x7cdcc: {
        unsigned type = AW(0x1e) & 3;
        SET(0x0e, 0x28);
        SET(0x2d, 0x3e);
        SET(0x3c, 0xf753);
        SET(0x38, 0xfe00);
        SET(0x26, read_word(bank, 0xca34 + 2 * type));
        SET(0x36, AW(0x26));
        SET(0x4c, 4 * type);
        SET(0x44, 1);
        if (!type) {
            child = spawn_bank7(ram, actor, 0xcfb4);
            CSET(child, 0x1e, AW(0x1e));
            SET(0, 0xcea2);
            address = 0x7cea2;
        } else {
            SET(0, 0xce23);
            address = 0x7ce23;
        }
    }
        /* fall through */
    case 0x7ce23:
    case 0x7cea2: {
        int loaded = address == 0x7ce23;
        scroll(ram, actor);
        move_x(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0xca3c + (AW(0x40) & 2) + (loaded ? AW(0x4c) : 0)));
            SET(0x44, 7);
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, rom, actor, loaded);
        if (cart_runs_off(ram, tiles, actor) || !loaded) break;
        AB(0x0d) = 0;
        uint16_t attacker = world_surface_contact(ram, rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, rom, actor, attacker);
        script_effect(ram, actor, 0x7000, 0xca92, 4);
        for (unsigned n = 5; n; n--) {
            child = world_spawn_child(ram, read_word(ram, 0x9a), 0xd1b1);
            if (child) CSET(child, 2, 0x7000); else child = 0x8480;
            CSET(child, 0x21, AW(0x21));
            CSET(child, 0x24, AW(0x24));
            CSET(child, 0x14, AW(0x14));
            CB(child, 0x2c) = AB(0x14);
            CB(child, 0x28) = bank[0xcbb8 + n - 1];
            CSET(child, 0x1e, read_word(bank, 0xcad4 + 2 * (n - 1)));
        }
        SET(0x1e, 0);
        SET(0, 0xcea2);
        break;
    }
    case 0x7ceec:
        scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        SET(0x3a, AW(0x3a) - 0x20);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(bank, 0xca4c + frame + AW(0x4c)));
            SET(0x44, read_word(bank, 0xca8c + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (material == 4) {
            SET(0x2a, read_word(bank, 0xca52 + AW(0x4c)));
            SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 1));
            SET(0, 0xcf61);
        }
        break;
    case 0x7cf61:
        scroll(ram, actor);
        move_y(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        if (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff02) {
            loud_explosion(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    case 0x7cfb4:
        world_acquire_resource(ram, actor, 0xd3);
        SET(0x0e, 0x2d);
        AB(0x2d) = 0x3e;
        SET(0x3c, 0xf75b);
        SET(0x26, 0x7f00);
        SET(0x36, 0x400);
        SET(0x2a, 0x1955);
        SET(0, 0xcfde);
        /* fall through */
    case 0x7cfde: {
        uint16_t parent = read_word(ram, 0x84fe + AW(8)), convoy = read_word(ram, 0x84fc + AW(8));
        if (CB(parent, 0x0a)) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        int jump = CW(parent, 0x4e) & 1;
        if (!jump && AW(0x21) < 0x168 && AW(0x21) >= 0xe0 && !(CW(convoy, 0x48) & 1)) {
            uint16_t reach = CW(read_word(ram, 0x90), 0x21) + 0x20 + ((AW(0x1e) & 4) ? 0x53 : 0);
            jump = AW(0x21) < reach || AW(0x21) < (uint16_t)(CW(read_word(ram, 0x92), 0x21) + 0x20);
        }
        if (jump) {
            CSET(convoy, 0x48, CW(convoy, 0x48) | 1);
            SET(0x42, 15);
            AB(0x2d) = 0x20;
            SET(0x40, 0);
            SET(0x44, 1);
            if (AW(0x1e) & 4) SET(0x38, 0xfe80);
            SET(0, 0xd0c9);
        }
        queue_sprite(ram, rom, actor, 1);
        AB(0x0d) = 0;
        world_surface_contact(ram, rom, actor);
        if (!AW(0x26)) {
            large_blast(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7d0c9:
        scroll(ram, actor);
        if (AW(0x42)) SET(0x3a, AW(0x3a) - 0x10);
        move_y(ram, actor);
        move_x(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(bank, 0xcab2 + frame));
            SET(0x44, read_word(bank, 0xcab6 + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (material == 4) {
            SET(0x3a, 0xff00);
            SET(0x2a, 0x19c3);
            script_effect(ram, actor, 0x7000, 0xcaba, 3);
            SET(0, 0xd15a);
        }
        AB(0x0d) = 0;
        world_surface_contact(ram, rom, actor);
        if (!AW(0x26)) {
            large_blast(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    case 0x7d15a: {
        scroll(ram, actor);
        move_y(ram, actor);
        queue_sprite(ram, rom, actor, 0);
        int explode = (terrain(ram, tiles, AW(0x21), AW(0x24) + 6, 0) & 0xff02) != 0;
        if (!explode) {
            AB(0x0d) = 2;
            explode = world_player_contact(ram, rom, actor) != 0;
        }
        if (!explode) {
            AB(0x0d) = 0;
            world_projectile_contact(ram, rom, actor);
            explode = !AW(0x26);
        }
        if (explode) {
            /* 6B91 */
            child = effect_slot(ram, actor, 0x6efe);
            CSET(child, 0x4c, 0x74e5);
            CSET(child, 0x4a, 0x7675);
            CB(child, 0x2c) = 0x0a;
            CB(child, 0x2d) = 0x20;
            CSET(child, 0x56, 0x40);
            CSET(child, 0x58, 0);
            world_queue_sound(ram, 0x3e);
            if (!(child & 0x300)) CSET(child, 0x44, 0x5d);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7d1b1:
        AB(0x2d) = 0x20;
        SET(0x2e, 0x400);
        SET(0x44, 1);
        heading_velocity(ram, rom, actor);
        if ((int16_t)AW(0x3a) < -0x100) SET(0x3a, 0xff00);
        SET(0, 0xd1d6);
        break;
    case 0x7d1d6:
    case 0x7d260: {
        int sinking = address == 0x7d260;
        scroll(ram, actor);
        if (!sinking) move_x(ram, actor);
        move_y(ram, actor);
        if (!sinking && (int16_t)AW(0x3a) > -0x400) SET(0x3a, AW(0x3a) - 0x20);
        if ((!sinking && AW(0x21) > 0x1c0) || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t type = AW(0x1e);
            SET(0x2a, read_word(bank, 0xcade + AW(0x40) + type));
            SET(0x44, read_word(bank, (sinking ? 0xcaec : 0xcaea) + type));
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 12) SET(0x40, 0);
        }
        queue_sprite(ram, rom, actor, 0);
        uint16_t mask = terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (!sinking) {
            if (material == 4) {
                AB(0x2c) = AB(0x15);
                SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 1));
                SET(0, 0xd260);
            }
        } else if (mask & 0xff02) {
            tagged_effect(ram, actor, 0x7b, 0x7601);
            finish_animation(ram, actor);
        }
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

static void shot_candidate(uint8_t *ram, uint16_t actor)
{
    if (AW(0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
}

/* Trail puff behind the cluster torpedo (17C3 with handler 0000:9E17). */
static void cluster_trail(uint8_t *ram, uint16_t actor)
{
    if (decrement(ram, actor + 0x5c)) return;
    SET(0x5c, 3);
    uint16_t child = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
    CSET(child, 0x21, AW(0x21));
    CSET(child, 0x24, AW(0x24));
    CSET(child, 0x58, AW(0x5a));
    CB(child, 0x2c) = 3;
}

/* Cluster torpedo and its fragments: 0000:AF63..B335. */
static int cluster(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    uint16_t child, parent;
    unsigned facing = AB(0x29) & 1;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0xaf63: {
        world_acquire_resource(ram, actor, 0x56);
        SET(0x3c, 0xaf09);
        SET(0x26, 1);
        SET(0x36, 0x90);
        SET(0x2a, 0x2692);
        AB(0x2d) = 0x20;
        SET(0x38, read_word(rom, 0xaf2d + 2 * facing + 4 * (ram[0xb73e] & 1)));
        SET(0x42, 0x14);
        SET(0x5c, 3);
        /* A363: trail length by the depth of open water below. */
        uint16_t cell, length = 0x11;
        if (terrain_cell(ram, tiles, AW(0x21), AW(0x24) + 0x18, &cell) == 3) {
            length = 0x0e;
            cell = (cell + 0x100) & 0x3fff;
            if (tiles[0x4000 + cell + 3] >> 3 == 3) {
                length = 0x0a;
                cell = (cell + 0x100) & 0x3fff;
                if (tiles[0x4000 + cell + 3] >> 3 == 3) length = 6;
            }
        }
        SET(0x58, length);
        SET(0x5a, length);
        child = effect_slot(ram, actor, 0xb1da);
        CSET(child, 0x3c, 0xaf11);
        CSET(child, 0x26, 1);
        CB(child, 0x29) = AB(0x29);
        SET(0, 0xafd3);
        break;
    }
    case 0xafd3: {
        scroll(ram, actor);
        move_x(ram, actor);
        queue_sprite(ram, rom, actor, 1);
        cluster_trail(ram, actor);
        int burst = !decrement(ram, actor + 0x42), checked = burst;
        if (!burst && AW(0x26)) {
            uint16_t cell;
            unsigned material = terrain_cell(ram, tiles, AW(0x21) + 0x20, AW(0x24), &cell);
            checked = burst = ((1u << (material & 15)) & 0xff02) != 0;
            if (!burst) {
                cell = (cell & 0xff00) | ((cell - 0x10) & 0xff);
                material = tiles[0x4000 + cell + 3] >> 3;
                checked = burst = ((1u << ((material - 1) & 15)) & 0xff02) != 0;
            }
            if (!burst) {
                if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) {
                    parent = read_word(ram, 0x84fe + AW(8));
                    CSET(parent, 0x48, CW(parent, 0x48) + 1);
                    finish_animation(ram, actor);
                } else shot_candidate(ram, actor);
                break;
            }
        }
        if (checked && read_word(ram, 0x84) == 4) return WORLD_HANDLER_STOP;
        SET(0x40, 0);
        SET(0x58, 1);
        SET(0x42, 10);
        SET(0x38, read_word(rom, 0xaf2d + 2 * facing));
        AB(0x0a) = 2;
        SET(0x3c, 0xaf21);
        SET(0x36, 8);
        world_queue_sound(ram, 0x39);
        SET(0, 0xb0c4);
        break;
    }
    case 0xb0c4:
        scroll(ram, actor);
        if (AW(0x38)) {
            move_x(ram, actor);
            SET(0x38, AW(0x38) - read_word(rom, 0xaf35 + 2 * facing));
            cluster_trail(ram, actor);
        }
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = AW(0x40), sprite = read_word(rom, 0xae5d + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x58, read_word(rom, 0xae7d + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 1);
        if (AW(0x42)) {
            if (!decrement(ram, actor + 0x42)) {
                for (unsigned n = AW(0x76) + 2; n; n--) {
                    child = world_spawn_child(ram, actor, 0xb234);
                    if (!child) break;
                    CSET(child, 0x21, AW(0x21) + read_word(rom, 0xaf29 + 2 * facing));
                    CB(child, 0x29) = AB(0x29);
                    CSET(child, 0x24, AW(0x24));
                    CB(child, 0x0b) = AB(0x0b);
                    CB(child, 0x2d) = AB(0x2d);
                    CSET(child, 0x52, (n - 1) * 2);
                    CSET(child, 0x76, AW(0x76));
                    CSET(child, 0x54, AW(0x54));
                    SET(0x54, AW(0x54) + 1);
                }
                SET(0x78, 0x10);
            }
        } else if (!decrement(ram, actor + 0x78)) {
            parent = read_word(ram, 0x84fe + AW(8));
            CSET(parent, 0x48, CW(parent, 0x48) + 1);
        }
        shot_candidate(ram, actor);
        break;
    case 0xb1da:
        parent = read_word(ram, 0x84fe + AW(8));
        if (CB(parent, 0x0a)) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        if (!AW(0x26)) {
            CSET(parent, 0x26, 0);
            finish_animation(ram, actor);
        } else shot_candidate(ram, actor);
        break;
    case 0xb234: {
        uint16_t spread = read_word(rom, 0xaf5d + 2 * AW(0x76)) + AW(0x52);
        world_acquire_resource(ram, actor, read_word(rom, 0x9bb1 + (AB(0x0b) & 2)));
        SET(0x3c, 0xaf19);
        SET(0x26, 1);
        SET(0x36, 0x40);
        AB(0x28) = rom[(uint16_t)(0xaf39 + spread + facing)];
        SET(0x2e, 0x300);
        heading_velocity(ram, rom, actor);
        SET(0x4c, read_word(rom, 0xaeb9 + read_word(rom, 0xaecb + 2 * AW(0x76)) + AW(0x52)));
        SET(0x42, read_word(rom, 0xaf4b + spread));
        SET(0, 0xb2ab);
        break;
    }
    case 0xb2ab: {
        scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        SET(0x40, AW(0x40) + 0x34);
        if (AW(0x40) >= 0x700) SET(0x40, 0);
        SET(0x2a, read_word(rom, AW(0x4c) + 2 * AB(0x41)));
        queue_sprite(ram, rom, actor, 1);
        if (decrement(ram, actor + 0x42) && AW(0x26)) {
            if (!(terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff0a)) {
                if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) finish_animation(ram, actor);
                else shot_candidate(ram, actor);
                break;
            }
            if (read_word(ram, 0x84) == 4) return WORLD_HANDLER_STOP;
        }
        release_actor_resources(ram, actor);
        SET(0x36, 0x20);
        SET(0, 0xa199);
        shot_candidate(ram, actor);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* ---- Stage boss: walking fortress, 1000:451A..5D97 ---- */
#define PARENT read_word(ram, 0x84fe + AW(8))
#define GRANDPARENT read_word(ram, 0x84fc + AW(8))
#define ANCESTOR3 read_word(ram, 0x84fa + AW(8))
#define ANCESTOR4 read_word(ram, 0x84f8 + AW(8))

static uint16_t boss_rank(const uint8_t *ram, const uint8_t *bank, uint16_t column)
{
    return read_word(bank, column + read_word(bank, 0x447c + read_word(ram, 0x8a)));
}

/* 1000:5CA2: place the actor on the anchor's barrel line at distance bp+2E. */
static void beam_place(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t anchor)
{
    unsigned angle = 2 * CB(anchor, 0x28);
    int16_t reach = AW(0x2e);
    AB(0x28) = CB(anchor, 0x28);
    SET(0x21, CW(anchor, 0x21) + (uint16_t)(((int16_t)read_word(rom + 0xa0000, 0x9e12 + angle) * reach) >> 8));
    SET(0x24, CW(anchor, 0x24) + (uint16_t)(((int16_t)read_word(rom + 0xa0000, 0x9d92 + angle) * reach) >> 8));
}

/* 1000:5D50: centre the arc between two beam tips that nearly touch. */
static int arc_between_beams(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = PARENT;
    uint16_t a = CW(parent, 0x68 + AW(0x70)), b = CW(parent, 0x68 + AW(0x72));
    if (!a || !b) return 0;
    uint16_t ax = CW(a, 0x21), bx = CW(b, 0x21), gap = ax - bx;
    if (gap & 0x8000) gap = -gap;
    if (gap >= 0x10) return 0;
    SET(0x21, (uint16_t)((int16_t)(ax + bx) >> 1));
    return 1;
}

/* 1000:479D: shared tail of the hull: flash palette, register, draw both halves. */
static void boss_hull_tail(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (AB(0x19)) {
        --AB(0x19);
        AB(0x2c) = AB(0x15 + (AB(0x19) & 1));
    }
    register_surface_enemy(ram, actor);
    AB(0x29) = 0;
    queue_sprite(ram, rom, actor, 0);
    AB(0x29) = 1;
    queue_sprite(ram, rom, actor, 0);
}

static void boss_hull_shapes(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned player_mode, unsigned shot_mode)
{
    static const uint16_t shapes[2] = {0xf7eb, 0xf7f3};
    for (unsigned i = 0; i < 2; i++) {
        SET(0x3c, shapes[i]);
        if (player_mode) {
            AB(0x0d) = player_mode;
            world_player_contact(ram, rom, actor);
        }
        AB(0x0d) = shot_mode;
        world_surface_contact(ram, rom, actor);
    }
}

static int boss(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank = rom + 0x10000;
    uint16_t child, parent;
    unsigned material, facing = AB(0x29) & 1;
    if (!tiles) return WORLD_HANDLER_STOP;
    switch (address) {
    case 0x1451a: {
        world_acquire_resource(ram, actor, 0x45);
        world_acquire_resource(ram, actor, 0x41);
        world_acquire_resource(ram, actor, 0xaa);
        AB(0x2c) = AB(0x15);
        AB(0x2d) = 0x30;
        SET(0x2a, 0xa978);
        SET(0x0e, 0x43);
        SET(0x21, 0x1e0);
        SET(0x24, 0x138);
        uint16_t hp = boss_rank(ram, bank, 0x449c);
        SET(0x26, hp);
        SET(0x36, hp);
        SET(0x60, (hp >> 1) + (hp >> 2));
        SET(0x62, hp >> 2);
        child = spawn_boss_part(ram, actor, 0x4b11);
        CB(child, 0x15) = AB(0x2c);
        CB(child, 0x2d) = 0x31;
        CSET(child, 0x3c, 0xf813);
        CSET(child, 0x26, 0x400);
        CSET(child, 0x36, 0x400);
        CSET(child, 0x34, 0xfff8);
        CSET(child, 0x44, 1);
        CSET(child, 0x42, 6);
        SET(0x48, 0x5a);
        SET(0x4e, 1);
        SET(0x72, 3);
        SET(0, 0x45ba);
        break;
    }
    case 0x145ba:
    case 0x14614:
    case 0x14640:
        if (address == 0x145ba) {
            scroll(ram, actor);
            if (read_word(ram, 0xb700) >= 0x11bf) {
                for (unsigned i = 0; i < 2; i++) {
                    child = spawn_boss_part(ram, actor, 0x4cba);
                    CSET(child, 0x15, AW(0x15));
                    CB(child, 0x2c) = AB(0x15);
                    CSET(child, 0x26, AW(0x26));
                    if (!i) CB(child, 0x29) = 1;
                }
                queue_sound_b(ram, 0x11);
                SET(0x42, 0x80);
                SET(0, 0x4614);
            }
        } else if (address == 0x14614) {
            if (!decrement(ram, actor + 0x42)) {
                static const int16_t turrets[4][6] = {
                    {-0x18, -0x20, 0x9c, 0xe4, 2, 0}, {0x18, -0x20, 0x9c, 0xe4, -2, 0},
                    {-0x28, -0x10, 0x9c, 0xc0, 2, 1}, {0x28, -0x10, 0xc0, 0xe4, -2, 0}};
                for (unsigned i = 0; i < 4; i++) {
                    child = spawn_boss_part(ram, actor, 0x4f2c);
                    CSET(child, 0x31, turrets[i][0]);
                    CSET(child, 0x34, turrets[i][1]);
                    CSET(child, 0x70, turrets[i][2]);
                    CSET(child, 0x72, turrets[i][3]);
                    CSET(child, 0x74, turrets[i][4]);
                    CSET(child, 0x1e, i);
                    if (turrets[i][5]) CB(child, 0x29) = 1;
                }
                for (unsigned i = 0; i < 4; i++) {
                    child = spawn_boss_part(ram, actor, 0x55ad);
                    CSET(child, 0x14, AW(0x14));
                    CB(child, 0x2c) = AB(0x14);
                    CSET(child, 0x1e, i);
                    if (i & 1) CB(child, 0x29) = 1;
                    if (i & 2) CSET(child, 0x34, 0xfffb);
                }
                SET(0x50, 0);
                SET(0, 0x4640);
            }
        } else if ((AW(0x50) & 15) == 15) {
            static const uint8_t arcs[5][3] = {{0, 2, 0}, {0, 4, 1}, {0, 6, 1}, {2, 4, 0}, {2, 6, 0}};
            SET(0x54, 1);
            for (unsigned i = 0; i < 5; i++) {
                child = spawn_boss_part(ram, actor, 0x5407);
                CSET(child, 0x70, arcs[i][0]);
                CSET(child, 0x72, arcs[i][1]);
                if (arcs[i][2]) CB(child, 0x29) = 1;
            }
            SET(0, 0x465a);
        }
        boss_hull_shapes(ram, rom, actor, 0, 1);
        boss_hull_tail(ram, rom, actor);
        break;
    case 0x1465a:
    case 0x146a5:
    case 0x146e4: {
        if (address == 0x1465a) {
            if (AW(0x60) >= AW(0x26)) {
                SET(0x2a, 0xa9fe);
                AB(0x19) = 0x20;
                for (unsigned i = 0; i < 2; i++) {
                    child = spawn_boss_part(ram, actor, 0x4c54);
                    CSET(child, 0x21, AW(0x21));
                    CSET(child, 0x24, AW(0x24));
                    if (i) CB(child, 0x29) = 1;
                }
                SET(0, 0x46a5);
            }
        } else if (address == 0x146a5 && AW(0x62) >= AW(0x26)) {
            SET(0x2a, 0xaa38);
            AB(0x19) = 0x20;
            child = spawn_boss_part(ram, actor, 0x4c41);
            CSET(child, 0x21, AW(0x21));
            CSET(child, 0x24, AW(0x24));
            CB(child, 0x2c) = AB(0x15);
            CB(child, 0x2d) = 0x30;
            CSET(child, 0x2a, 0xaa72);
            SET(0, 0x46e4);
        }
        int volley = 0;
        if (AW(0x4e)) {
            if (!decrement(ram, actor + 0x4e)) {
                child = spawn_boss_part(ram, actor, 0x5b3a);
                CSET(child, 0x66, AW(0x66));
                SET(0x56, 0);
                SET(0x5e, 5);
                unsigned order = world_random_byte(ram) & 0x18;
                SET(0x64, order);
                SET(0x58, read_word(bank, 0x42a0 + (order >> 2)));
                volley = 1;
            }
        } else volley = !decrement(ram, actor + 0x5c);
        if (volley) {
            SET(0x5c, 0x29);
            if (decrement(ram, actor + 0x5e)) {
                SET(0x56, AW(0x56) | 1u << (read_word(bank, 0x4280 + AW(0x64)) & 15));
                SET(0x64, AW(0x64) + 2);
            } else {
                if (!decrement(ram, actor + 0x72)) {
                    SET(0x72, 3);
                    SET(0x66, AW(0x66) ^ 2);
                }
                SET(0x4e, read_word(bank, 0x42a8 + AW(0x66)));
            }
        }
        SET(0x3c, 0xf7eb);
        AB(0x0d) = 0;
        if (!world_surface_contact(ram, rom, actor)) {
            SET(0x3c, 0xf7f3);
            AB(0x0d) = 0;
            world_surface_contact(ram, rom, actor);
        }
        if (!AW(0x26)) {
            AB(0x0a) |= 2;
            SET(0x31, AW(0x21));
            SET(0x42, 0x40);
            SET(0, 0x47cb);
        }
        boss_hull_tail(ram, rom, actor);
        break;
    }
    case 0x147cb:
    case 0x14811:
    case 0x14837:
        if (address == 0x147cb) {
            if (!decrement(ram, actor + 0x42)) {
                AB(0x0a) |= 8;
                if (script_begin(ram, rom, actor, 0x40fc) < 0) return WORLD_HANDLER_STOP;
                child = spawn_boss_part(ram, actor, 0x5af5);
                CSET(child, 0x42, 0x30);
                world_queue_sound(ram, 0x49);
                SET(0, 0x4811);
            }
            SET(0x21, read_word(bank, 0x40f4 + 2 * (AW(0x42) & 3)) + AW(0x31));
        } else {
            /* The original leaves BX at the dispatcher's value here: the depth
             * word of the actor that ran just before this one. */
            if (address == 0x14811 &&
                (terrain(ram, tiles, CW(AW(6), 8), AW(0x24) - 0x20, &material), material == 4)) {
                script_effect(ram, actor, 0x1000, 0x44d4, 2);
                SET(0, 0x4837);
            }
            int step = script_step(ram, rom, actor);
            if (step < 0) return WORLD_HANDLER_STOP;
            if (step) {
                SET(0x26, boss_rank(ram, bank, 0x449e));
                SET(0x36, AW(0x26));
                SET(0x70, 1);
                if (script_begin(ram, rom, actor, 0x4134) < 0) return WORLD_HANDLER_STOP;
                SET(0x4c, (world_random_byte(ram) & 0x3f) + 0x40);
                SET(0, 0x48a4);
            }
        }
        boss_hull_shapes(ram, rom, actor, 2, 1);
        boss_hull_tail(ram, rom, actor);
        break;
    case 0x148a4:
    case 0x149ab:
    case 0x149ce: {
        if (address == 0x148a4) {
            int step = script_step(ram, rom, actor);
            if (step < 0) return WORLD_HANDLER_STOP;
            if (step && script_begin(ram, rom, actor, 0x4134) < 0) return WORLD_HANDLER_STOP;
            if (!decrement(ram, actor + 0x48)) {
                SET(0x4a, 0x20);
                SET(0, 0x49ab);
            }
        } else {
            if (!decrement(ram, actor + 0x4a)) {
                SET(0x48, address == 0x149ab ? 0x11a : 0x5a);
                SET(0, 0x48a4);
            }
            if (address == 0x149ab) SET(0x24, AW(0x24) + read_word(bank, 0x44d0 + ((AW(0x4a) & 4) >> 1)));
        }
        static const uint16_t shapes[2] = {0xf7eb, 0xf7f3};
        uint16_t attacker = 0;
        for (unsigned i = 0; i < 2 && !attacker; i++) {
            SET(0x3c, shapes[i]);
            AB(0x0d) = 2;
            world_player_contact(ram, rom, actor);
            AB(0x0d) = 0;
            attacker = world_projectile_contact(ram, rom, actor);
            if (!attacker) {
                AB(0x0d) = 0;
                attacker = world_surface_contact(ram, rom, actor);
            }
        }
        if (!AW(0x26)) {
            if (!(ram[0x264] & 3)) SET(0x26, 0x100);
            else {
                ram[0x124 + ((CB(attacker, 0x0b) & 2) << 5)] |= 4;
                ram[0x262] |= 0x80;
                world_award_score(ram, rom, actor, attacker);
                script_effect(ram, actor, 0x1000, 0x41bc, 4);
                if (script_begin(ram, rom, actor, 0x4506) < 0) return WORLD_HANDLER_STOP;
                SET(0x42, 0x28);
                AB(0x19) = 1;
                AB(0x0a) |= 4;
                SET(0, 0x4c19);
            }
        }
        if (!decrement(ram, actor + 0x4c)) {
            SET(0x4c, (world_random_byte(ram) & 0x7f) + 0x40);
            child = world_spawn_child(ram, actor, 0x49e0);
            if (child) {
                CSET(child, 2, 0x1000);
                CSET(child, 0x15, AW(0x15));
                CB(child, 0x2c) = AB(0x15);
                CB(child, 0x2d) = AB(0x2d);
                CSET(child, 0x44, 1);
                world_queue_sound(ram, 0x41);
            }
        }
        boss_hull_tail(ram, rom, actor);
        break;
    }
    case 0x149e0:
        parent = PARENT;
        if ((ram[0x262] & 0x80) || (CB(parent, 0x0a) & 1)) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        AB(0x2c) = AB(0x15 + (CB(parent, 0x19) & 1));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40), sprite = read_word(bank, 0x43f6 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(bank, 0x440e + frame));
            SET(0x40, frame + 2);
            if (frame == 12) {
                child = spawn_boss_part(ram, actor, 0x4a81);
                CSET(child, 0x21, AW(0x21));
                CSET(child, 0x24, AW(0x24));
            }
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x14a81: {
        SET(0x42, 1);
        SET(0x40, 4);
        uint16_t x = AW(0x21) < 0x88 ? 0 : AW(0x21) - 0x88, spread = 0;
        while (x >= 0x3c) { x -= 0x3c; spread += 0x10; }
        if (spread >= 0x40) spread = 0x30;
        SET(0x44, spread);
        SET(0, 0x4ab0);
    }
        /* fall through */
    case 0x14ab0:
        if (ram[0x262] & 0x80) { finish_animation(ram, actor); break; }
        if (decrement(ram, actor + 0x42)) break;
        SET(0x42, 4);
        child = world_spawn_child(ram, actor, 0x595b);
        if (child) {
            parent = PARENT;
            CSET(child, 2, 0x1000);
            CSET(child, 0x21, CW(parent, 0x21));
            CSET(child, 0x24, CW(parent, 0x24) + 0x18);
            CSET(child, 0x40, AW(0x40) - 1 + AW(0x44));
        }
        if (!decrement(ram, actor + 0x40)) finish_animation(ram, actor);
        break;
    case 0x14b11:
        world_acquire_resource(ram, actor, 0x40);
        AB(0x2c) = AB(0x15);
        SET(0, 0x4b24);
        /* fall through */
    case 0x14b24:
    case 0x14b4e:
        parent = PARENT;
        if (address != 0x14b4e && CW(parent, 0x48)) {
            SET(0x21, CW(parent, 0x21) + AW(0x31));
            SET(0x24, CW(parent, 0x24) + AW(0x34));
        } else {
            if (address != 0x14b4e) {
                if (script_begin(ram, rom, actor, 0x4156) < 0) return WORLD_HANDLER_STOP;
                SET(0x42, 1);
                SET(0, 0x4b4e);
            }
            /* The script moves the offset from the hull, kept in bp+30..35. */
            memcpy(ram + actor + 0x20, ram + actor + 0x30, 6);
            int step = script_step(ram, rom, actor);
            if (step < 0) return WORLD_HANDLER_STOP;
            memcpy(ram + actor + 0x30, ram + actor + 0x20, 6);
            if (step) SET(0, 0x4b24);
            SET(0x21, CW(parent, 0x21) + AW(0x31));
            SET(0x24, CW(parent, 0x24) + AW(0x34));
            AB(0x0d) = 2;
            world_player_contact(ram, rom, actor);
            AB(0x0d) = 1;
            world_projectile_contact(ram, rom, actor);
        }
        if (CB(parent, 0x0a) & 1) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0x43ec + AW(0x40)));
            SET(0x44, AW(0x42));
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 10) SET(0x40, 0);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x14c19:
        if (!AW(0x42)) {
            int step = script_step(ram, rom, actor);
            if (step < 0) return WORLD_HANDLER_STOP;
            if (step) {
                ram[0x263] |= 0xc0;
                SET(0x42, 0xffff);
            }
        } else decrement(ram, actor + 0x42);
        boss_hull_tail(ram, rom, actor);
        break;
    case 0x14c41:
        AB(0x29) = 0;
        queue_sprite(ram, rom, actor, 0);
        AB(0x29) = 1;
        /* fall through */
    case 0x14c4e:
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x14c54:
        world_acquire_resource(ram, actor, 0x42);
        AB(0x2d) = 0x2e;
        SET(0, 0x4c65);
        /* fall through */
    case 0x14c65:
        SET(0x42, (world_random_byte(ram) & 0x1f) + 0x20);
        SET(0, 0x4c79);
        /* fall through */
    case 0x14c79:
        if (!decrement(ram, actor + 0x42)) {
            SET(0x40, 0);
            world_queue_sound(ram, 0x94);
            SET(0, 0x4c91);
        }
        break;
    case 0x14c91:
        if (!(AW(0x40) & 1)) {
            uint16_t sprite = read_word(bank, 0x43de + (AW(0x40) & 14));
            if (sprite == 0xffff) { SET(0, 0x4c65); break; }
            SET(0x2a, sprite);
            queue_sprite(ram, rom, actor, 0);
        }
        SET(0x40, AW(0x40) + 1);
        break;
    case 0x14cba:
        AB(0x2d) = 0x2e;
        SET(0x2a, 0xa99a);
        SET(0x3c, 0xf7fb);
        SET(0x21, read_word(bank, 0x4024 + 2 * facing));
        SET(0x24, 0x138);
        if (script_begin(ram, rom, actor, read_word(bank, 0x402c + 2 * facing)) < 0) return WORLD_HANDLER_STOP;
        SET(0x60, AW(0x26) >> 1);
        child = spawn_boss_part(ram, actor, 0x4dea);
        CSET(child, 0x15, AW(0x15));
        CB(child, 0x2c) = AB(0x15);
        CB(child, 0x29) = AB(0x29);
        SET(0, 0x4d10);
        break;
    case 0x14d10: {
        int step = script_step(ram, rom, actor);
        if (step < 0) return WORLD_HANDLER_STOP;
        if (step) SET(0, 0x4d1f);
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0x14d1f:
    case 0x14d42:
    case 0x14d90:
        parent = PARENT;
        if (address == 0x14d1f) {
            if (AW(0x60) >= CW(parent, 0x26)) {
                SET(0x2a, 0xaa8c);
                SET(0, 0x4d42);
            }
        } else if (address == 0x14d42) {
            if (CB(parent, 0x0a) & 8) {
                SET(0x2a, 0xaace);
                child = spawn_boss_part(ram, actor, 0x4c4e);
                CSET(child, 0x21, AW(0x21));
                CSET(child, 0x24, AW(0x24));
                CB(child, 0x2c) = AB(0x15);
                CB(child, 0x29) = AB(0x29);
                CB(child, 0x2d) = 0x2e;
                CSET(child, 0x2a, 0xaaf0);
                SET(0, 0x4d90);
            }
        } else if (CB(parent, 0x0a) & 1) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        AB(0x19) = CB(parent, 0x19);
        AB(0x2c) = AB(0x15 + (AB(0x19) & 1));
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x14dea:
        AB(0x2d) = 0x2c;
        SET(0x2a, 0xa9dc);
        SET(0, 0x4df9);
        break;
    case 0x14df9:
    case 0x14e2f: {
        uint16_t hull = GRANDPARENT;
        int armed = address == 0x14e2f;
        if (!armed && CW(hull, 0x70)) {
            SET(0x26, boss_rank(ram, bank, 0x449e) >> (facing ? 2 : 1));
            SET(0, 0x4e2f);
            armed = 1;
        }
        if (armed && (int16_t)CW(hull, 0x26) <= (int16_t)AW(0x26)) {
            AB(0x28) = bank[0x4514 + facing];
            SET(0x2e, 0x400);
            heading_velocity(ram, rom, actor);
            child = loud_explosion(ram, actor);
            CSET(child, 0x31, read_word(bank, 0x4516 + 2 * facing));
            AB(0x2c) = AB(0x15);
            SET(0, 0x4edb);
            address = 0x14edb;
        } else {
            parent = PARENT;
            if (CB(parent, 0x0a) & 1) { finish_animation(ram, actor); break; }
            unsigned flash = CW(hull, 0x19) & 1;
            SET(0x21, CW(parent, 0x21));
            SET(0x24, CW(parent, 0x24) + flash);
            AB(0x2c) = AB(0x15 + flash);
            queue_sprite(ram, rom, actor, 0);
            break;
        }
    }
        /* fall through */
    case 0x14edb:
        move_x(ram, actor);
        move_y(ram, actor);
        SET(0x3a, AW(0x3a) - 0x20);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x14f2c:
        world_acquire_resource(ram, actor, 0x40);
        world_acquire_resource(ram, actor, 0xa9);
        world_acquire_resource(ram, actor, 0x41);
        SET(0x0e, 0x44);
        AB(0x2d) = 0x2f;
        SET(0x3c, 0xf803);
        AB(0x28) = bank[0x427e + (AW(0x1e) & 1)];
        SET(0x60, boss_rank(ram, bank, 0x44a0));
        SET(0x36, AW(0x60));
        SET(0, 0x4f7a);
        /* fall through */
    case 0x14f7a:
        parent = PARENT;
        if (CB(parent, 0x0a) & 2) { finish_animation(ram, actor); break; }
        if (CW(parent, 0x50) >> (AW(0x1e) & 15) & 1) {
            SET(0x26, AW(0x60));
            world_queue_sound(ram, 0x4e);
            SET(0, 0x4fc0);
        }
        break;
    case 0x14fc0: {
        parent = PARENT;
        SET(0x21, CW(parent, 0x21) + AW(0x31));
        SET(0x24, CW(parent, 0x24) + AW(0x34));
        if (CB(parent, 0x0a) & 2) {
            SET(0x26, 0);
            surface_enemy_death_blast(ram, rom, actor);
            finish_animation(ram, actor);
            break;
        }
        unsigned slot = AW(0x1e) & 3;
        if (CW(parent, 0x56) >> slot & 1) {
            CSET(parent, 0x56, CW(parent, 0x56) & ~(1u << slot));
            SET(0x66, CW(parent, 0x66));
            child = spawn_boss_part(ram, actor, 0x5134);
            CSET(child, 0x14, AW(0x14));
            CB(child, 0x2c) = AB(0x14);
            CSET(child, 0x1e, AW(0x1e));
            CSET(child, 0x66, AW(0x66));
            SET(0x42, read_word(bank, 0x42a8 + AW(0x66)) - 0x50);
        }
        if (AW(0x42)) {
            decrement(ram, actor + 0x42);
            if (!(read_word(ram, 0x274) & 7)) {
                uint8_t step = AB(0x74);
                AB(0x28) += step;
                if (AB(0x28) == ((step & 0x80) ? AB(0x70) : AB(0x72))) SET(0x74, -AW(0x74));
            }
        }
        unsigned pose = ((uint8_t)(AB(0x28) + 4) & 0xf8) >> 3;
        if (AW(0x1e) < 2) AB(0x29) = bank[0x425e + pose];
        SET(0x2a, read_word(bank, 0x41fa + read_word(bank, 0x420e + 2 * pose) + (AW(0x1e) & 2)));
        queue_sprite(ram, rom, actor, 0);
        if (CW(parent, 0x54)) {
            AB(0x0d) = 0;
            uint16_t attacker = world_surface_contact(ram, rom, actor);
            if (!AW(0x26)) {
                world_award_score(ram, rom, actor, attacker);
                surface_enemy_death_blast(ram, rom, actor);
                SET(0x42, 0xc0);
                SET(0, 0x50f9);
            }
        }
        break;
    }
    case 0x150f9:
        parent = PARENT;
        if (CB(parent, 0x0a) & 2) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x42)) {
            CSET(parent, 0x50, CW(parent, 0x50) & ~(1u << (AW(0x1e) & 15)));
            SET(0, 0x4f7a);
        }
        break;
    case 0x15134:
        AB(0x2d) = 4;
        SET(0x2e, 0x10);
        SET(0x44, 1);
        SET(0x4e, 0x14);
        SET(0x5e, read_word(bank, 0x42ac + (AW(0x1e) & 2)));
        SET(0x42, read_word(bank, 0x42a8 + AW(0x66)) + 0x14);
        SET(0, 0x5169);
        /* fall through */
    case 0x15169:
    case 0x151ae:
        if (address != 0x151ae && !decrement(ram, actor + 0x4e)) {
            SET(0x4e, 4);
            child = spawn_boss_part(ram, actor, 0x521c);
            CSET(child, 0x14, AW(0x14));
            CB(child, 0x2c) = AB(0x14);
            CSET(child, 0x1e, AW(0x1e));
            CSET(child, 0x66, AW(0x66));
            CSET(child, 0x5c, AW(0x5c));
            SET(0x5c, AW(0x5c) + 1);
            if (AW(0x5c) >= AW(0x5e)) {
                CSET(child, 0x5a, 1);
                SET(0, 0x51ae);
            }
        }
        parent = PARENT;
        if (!CW(parent, 0x26)) { finish_animation(ram, actor); break; }
        beam_place(ram, rom, actor, parent);
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0x42b0 + AW(0x40)));
            SET(0x44, 2);
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 6) SET(0x40, 0);
            queue_sprite(ram, rom, actor, 0);
        }
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    case 0x1521c:
        AB(0x2d) = 5;
        SET(0x3c, 0xfb4b);
        SET(0x2e, (AW(0x5c) << 3) + 0x10);
        SET(0x42, -(uint16_t)((AW(0x5c) << 3) - read_word(bank, 0x42a8 + AW(0x66)) + 0x14));
        SET(0x44, 1);
        if (AW(0x5a)) {
            child = spawn_boss_part(ram, actor, 0x5311);
            CSET(child, 0x14, AW(0x14));
            CB(child, 0x2c) = AB(0x14);
            CSET(child, 0x1e, AW(0x1e));
            CSET(ANCESTOR3, 0x68 + 2 * (AW(0x1e) & 3), child);
        }
        SET(0, 0x528e);
        /* fall through */
    case 0x1528e: {
        uint16_t turret = GRANDPARENT;
        if (!CW(turret, 0x26)) { finish_animation(ram, actor); break; }
        beam_place(ram, rom, actor, turret);
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0x42b6 + AW(0x40)));
            SET(0x44, 2);
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 0x38) SET(0x40, 0);
            terrain(ram, tiles, AW(0x21), AW(0x24), &material);
            if (material != 4 && material != 5) queue_sprite(ram, rom, actor, 0);
        }
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    }
    case 0x15311:
        AB(0x2d) = 4;
        SET(0x44, 1);
        SET(0x24, 0xd0);
        SET(0x3c, 0xf823);
        SET(0, 0x5329);
        /* fall through */
    case 0x15329:
        parent = PARENT;
        if (CB(parent, 0x0a)) {
            CSET(ANCESTOR4, 0x68 + 2 * (AW(0x1e) & 3), 0);
            finish_animation(ram, actor);
            break;
        }
        SET(0x21, CW(parent, 0x21));
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(bank, 0x42ee + (AW(0x40) & 6)));
            SET(0x44, 1);
            SET(0x40, AW(0x40) + 2);
            queue_sprite(ram, rom, actor, 0);
        }
        AB(0x0d) = 2;
        world_player_contact(ram, rom, actor);
        break;
    case 0x1539f:
        AB(0x2d) = 5;
        SET(0x34, 0x30);
        SET(0x44, 1);
        SET(0, 0x53b2);
        /* fall through */
    case 0x153b2:
        parent = PARENT;
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24) + AW(0x34));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t sprite = read_word(bank, 0x4340 + AW(0x40));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, 4);
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x15407:
        world_acquire_resource(ram, actor, 0x40);
        AB(0x2d) = 3;
        SET(0x24, 0xd0);
        SET(0, 0x541d);
        /* fall through */
    case 0x1541d:
        if (CB(PARENT, 0x0a) & 8) { finish_animation(ram, actor); break; }
        if (arc_between_beams(ram, actor)) {
            SET(0x3c, 0xf823);
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0, 0x5460);
        }
        break;
    case 0x15460:
    case 0x154e7:
    case 0x1552a:
        if (address != 0x1552a && !arc_between_beams(ram, actor)) {
            if (address == 0x154e7) {
                SET(0x40, 0);
                SET(0x44, 1);
            }
            SET(0, 0x552a);
            address = 0x1552a;
        }
        if (address == 0x1552a) {
            if (!decrement(ram, actor + 0x44)) {
                SET(0x2a, read_word(bank, 0x42f6 + AW(0x40)));
                SET(0x44, 1);
                if (AW(0x40) < 2) SET(0, 0x541d);
                SET(0x40, AW(0x40) - 2);
            }
            queue_sprite(ram, rom, actor, 0);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            if (address == 0x15460) {
                SET(0x2a, read_word(bank, 0x42f6 + AW(0x40)));
                SET(0x44, 1);
                SET(0x40, AW(0x40) + 2);
                if (AW(0x40) >= 8) {
                    SET(0x40, 0);
                    SET(0x44, 1);
                    SET(0x3c, 0xf82b);
                    for (unsigned i = 0; i < 2; i++) {
                        child = spawn_boss_part(ram, actor, 0x5550);
                        CB(child, 0x2c) = AB(0x2c);
                        CSET(child, 0x24, AW(0x24) - 0x38);
                        if (i) CB(child, 0x29) = 1;
                    }
                    SET(0, 0x54e7);
                }
            } else {
                SET(0x2a, read_word(bank, 0x42fe + (AW(0x40) & 6)));
                SET(0x44, 1);
                SET(0x40, AW(0x40) + 2);
            }
        }
        queue_sprite(ram, rom, actor, 0);
        AB(0x0d) = 2;
        world_player_contact(ram, rom, actor);
        break;
    case 0x15550:
        AB(0x2d) = 3;
        SET(0x44, 1);
        SET(0, 0x555e);
        /* fall through */
    case 0x1555e:
        SET(0x21, CW(PARENT, 0x21));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40), sprite = read_word(bank, 0x4306 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(bank, 0x4324 + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x155ad:
        SET(0x0e, 0x45);
        SET(0x3c, 0xf80b);
        AB(0x2d) = 0x2e;
        SET(0x21, read_word(bank, 0x4028 + 2 * facing));
        SET(0x24, 0x170);
        SET(0x26, boss_rank(ram, bank, 0x44a2));
        SET(0x36, AW(0x26));
        child = spawn_boss_part(ram, actor, 0x572a);
        CSET(child, 0x1e, AW(0x1e));
        CSET(child, 0x14, AW(0x14));
        CB(child, 0x2c) = AB(0x14);
        CB(child, 0x29) = AB(0x29);
        CSET(child, 0x34, AW(0x34) - 0x40);
        SET(0, 0x5613);
        /* fall through */
    case 0x15613: {
        parent = PARENT;
        unsigned slot = AW(0x1e) & 15, side = (AW(0x1e) & 1) + 4;
        if ((CW(parent, 0x50) >> slot & 1) || (CW(parent, 0x50) >> side & 1)) break;
        CSET(parent, 0x50, CW(parent, 0x50) | 1u << side);
        if (script_begin(ram, rom, actor, read_word(bank, 0x40ac + 2 * facing)) < 0) return WORLD_HANDLER_STOP;
        SET(0x52, 1);
        SET(0, 0x5658);
        break;
    }
    case 0x15658:
    case 0x1568f: {
        int step = script_step(ram, rom, actor);
        if (step < 0) return WORLD_HANDLER_STOP;
        if (step) {
            parent = PARENT;
            if (address == 0x15658) {
                CSET(parent, 0x50, CW(parent, 0x50) | 1u << (AW(0x1e) & 15));
                if (script_begin(ram, rom, actor, read_word(bank, 0x40cc + 2 * facing)) < 0) return WORLD_HANDLER_STOP;
                SET(0, 0x568f);
            } else {
                CSET(parent, 0x50, CW(parent, 0x50) & ~(1u << ((AW(0x1e) & 1) + 4)));
                SET(0x52, 0);
                SET(0, 0x5613);
            }
        }
        SET(0x24, AW(0x24) + AW(0x34));
        SET(0x2a, 0xb294);
        queue_sprite(ram, rom, actor, 0);
        SET(0x24, AW(0x24) - AW(0x34));
        SET(0x2a, 0xb2ae);
        queue_sprite(ram, rom, actor, 0);
        AB(0x0d) = 0;
        uint16_t attacker = world_surface_contact(ram, rom, actor);
        if (!AW(0x26)) {
            world_award_score(ram, rom, actor, attacker);
            AB(0x0a) |= 2;
            large_blast(ram, actor);
            SET(0, 0x5700);
        }
        break;
    }
    case 0x15700:
        SET(0x24, AW(0x24) + AW(0x34));
        SET(0x2a, 0xbae4);
        queue_sprite(ram, rom, actor, 0);
        SET(0x24, AW(0x24) - AW(0x34));
        SET(0x2a, 0xb2ae);
        queue_sprite(ram, rom, actor, 0);
        AB(0x0d) = 1;
        world_surface_contact(ram, rom, actor);
        break;
    case 0x1572a:
        SET(0x3c, 0xfb4b);
        AB(0x2d) = 0x2e;
        child = spawn_boss_part(ram, actor, 0x5805);
        CSET(child, 0x1e, AW(0x1e));
        CSET(child, 0x14, AW(0x14));
        CB(child, 0x2c) = AB(0x15);
        CB(child, 0x29) = AB(0x29);
        SET(0, 0x5758);
        /* fall through */
    case 0x15758:
        parent = PARENT;
        if (CB(parent, 0x0a) & 2) { finish_animation(ram, actor); break; }
        if (CW(parent, 0x52)) {
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0, 0x5798);
        }
        break;
    case 0x15798:
        parent = PARENT;
        if (CB(parent, 0x0a) & 2) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24) + AW(0x34));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t pose = read_word(bank, 0x437c + AW(0x40));
            if (pose == 0xffff) { SET(0, 0x5758); break; }
            SET(0x46, pose);
            SET(0x2a, read_word(bank, 0x4370 + pose));
            SET(0x44, read_word(bank, 0x4394 + pose));
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x15805:
        SET(0x3c, 0xfb4b);
        AB(0x2d) = 0x2e;
        child = spawn_boss_part(ram, actor, 0x58f7);
        CB(child, 0x2c) = AB(0x14);
        CB(child, 0x29) = AB(0x29);
        SET(0, 0x582a);
        /* fall through */
    case 0x1582a:
        if (CW(GRANDPARENT, 0x52)) {
            SET(0x2a, read_word(bank, 0x43aa + (AW(0x1e) & 2)));
            SET(0x40, read_word(bank, 0x43b2 + (AW(0x1e) & 2)));
            SET(0, 0x585c);
        }
        break;
    case 0x1585c:
    case 0x1588a: {
        if (address == 0x1585c) {
            if (CW(ANCESTOR3, 0x50) >> (AW(0x1e) & 15) & 1) {
                SET(0x2a, read_word(bank, 0x43ae + (AW(0x1e) & 2)));
                SET(0, 0x588a);
            }
        } else if (!CW(GRANDPARENT, 0x52)) SET(0, 0x582a);
        parent = PARENT;
        if (CB(parent, 0x0a) & 1) { finish_animation(ram, actor); break; }
        uint16_t pose = 2 * CW(parent, 0x46), dx = read_word(bank, 0x43c6 + pose);
        SET(0x21, CW(parent, 0x21) + (facing ? -dx : dx));
        SET(0x24, CW(parent, 0x24) + read_word(bank, 0x43c8 + pose));
        queue_sprite(ram, rom, actor, 0);
        break;
    }
    case 0x158f7:
        AB(0x2d) = 0x2e;
        SET(0, 0x5900);
        /* fall through */
    case 0x15900:
        if (!CW(ANCESTOR3, 0x52)) break;
        parent = PARENT;
        if (CB(parent, 0x0a) & 1) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        SET(0x2a, read_word(bank, 0x43b6 + CW(parent, 0x40)));
        queue_sprite(ram, rom, actor, 0);
        break;
    case 0x1595b:
        world_acquire_resource(ram, actor, 0x44);
        AB(0x2d) = 4;
        SET(0x3c, 0xf81b);
        SET(0x26, 0x80);
        SET(0x36, 0x80);
        SET(0x2e, 0x400);
        AB(0x28) = (uint8_t)((world_random_byte(ram) & 3) - 2 + bank[(uint16_t)(0x442c + AW(0x40))]);
        heading_velocity(ram, rom, actor);
        SET(0, 0x599c);
        /* fall through */
    case 0x1599c:
    case 0x15a60: {
        int sinking = address == 0x15a60;
        int burst = (CB(ANCESTOR3, 0x0a) & 4) != 0, loud = 0;
        if (!burst) {
            if (!sinking) {
                move_x(ram, actor);
                move_y(ram, actor);
                SET(0x3a, AW(0x3a) - 0x18);
                if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
            } else {
                move_y(ram, actor);
                int16_t top = (ram[0x26e] & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
                if ((int16_t)AW(0x24) < top) { finish_animation(ram, actor); break; }
            }
            SET(0x2a, read_word(bank, 0x4424 + (AW(0x40) & 6)));
            SET(0x40, AW(0x40) + 2);
            queue_sprite(ram, rom, actor, 0);
            if (!sinking) {
                terrain(ram, tiles, AW(0x21), AW(0x24), &material);
                if (material == 4) {
                    SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 2));
                    splash_6d10(ram, actor);
                    SET(0, 0x5a60);
                }
                AB(0x0d) = 0;
                world_surface_contact(ram, rom, actor);
                burst = !AW(0x26);
            } else {
                AB(0x0d) = 2;
                loud = world_player_contact(ram, rom, actor) != 0;
                if (!loud) {
                    AB(0x0d) = 0;
                    world_projectile_contact(ram, rom, actor);
                    loud = !AW(0x26);
                }
            }
        }
        if (burst) surface_enemy_death_blast(ram, rom, actor);
        if (loud) loud_explosion(ram, actor);
        if (burst || loud) finish_animation(ram, actor);
        break;
    }
    case 0x15af5:
        if (!(ram[0x262] & 0x80)) {
            if (decrement(ram, actor + 0x42)) break;
            for (SET(0x40, 0); AW(0x40) < 0x14; SET(0x40, AW(0x40) + 4))
                queue_palette(ram, read_word(bank, 0x44bc + AW(0x40)) & 0x3f, read_word(bank, 0x44be + AW(0x40)), 7);
        }
        finish_animation(ram, actor);
        break;
    case 0x15b3a:
        SET(0x42, read_word(bank, 0x42a8 + (AW(0x66) & 2)));
        SET(0x44, 1);
        SET(0, 0x5b53);
        /* fall through */
    case 0x15b53:
        if (!decrement(ram, actor + 0x44)) {
            world_queue_sound(ram, 0x43);
            SET(0x44, 0x20);
        }
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

int world_stage3_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *b1 = main_rom + 0x10000, *b7 = main_rom + 0x70000;
    (void)pass;
    if (address >= 0x1ccea && address <= 0x1d3ef) {
        int result = wagon(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0x1ec04 && address <= 0x1f377) {
        int result = gate(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0x7cca9 && address <= 0x7d260) {
        int result = carts(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0xaf63 && address <= 0xb2ab) {
        int result = cluster(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0x1451a && address <= 0x15b53) {
        int result = boss(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0x1f66d && address <= 0x1ff98) {
        int result = tower(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    if (address >= 0xb1c16 && address <= 0xb25bf) {
        int result = soldier(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_UNKNOWN) return result;
    }
    switch (address) {
    case 0xdbf3:
        SET(0x42, 7);
        SET(0, 0xdbfd);
        /* fall through */
    case 0xdbfd:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (!decrement(ram, actor + 0x42)) {
            tile_grid(ram, main_rom, tiles, 0, 0x2430, 0x3118 + AW(0x52), 0x1001);
            SET(0x52, (AW(0x52) + 0x20) & 0xe0);
            SET(0x42, 7);
        }
        break;
    case 0xaec7e: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned kind = AB(0x1f) & 6;
        AB(0x2c) = 0x0e;
        AB(0x2d) = 0x3f;
        SET(0x3c, 0xfb4b);
        SET(0x42, 1);
        SET(0x4c, read_word(bank, 0xec6a + kind));
        script_begin(ram, main_rom, actor, read_word(bank, 0xec26 + kind));
        SET(0, 0xecae);
    }
        /* fall through */
    case 0xaecae: {
        const uint8_t *bank = main_rom + 0xa0000;
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        int step = script_step(ram, main_rom, actor);
        if (step < 0) return WORLD_HANDLER_STOP;
        if (step) { finish_animation(ram, actor); break; }
        AB(0x29) = (AW(0x38) & 0x8000) ? 0 : 1;
        if (!decrement(ram, actor + 0x42)) {
            uint16_t row = AW(0x4c);
            SET(0x42, read_word(bank, row));
            uint16_t pose = read_word(bank, row + 2);
            SET(0x4a, read_word(bank, 0xec04 + pose));
            SET(0x46, read_word(bank, 0xec22 + pose));
            SET(0x48, read_word(bank, 0xebc8 + pose + (AW(0x1e) & 4)));
            SET(0x4c, row + 4);
            SET(0x40, 0);
            SET(0x44, 1);
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(bank, frame + AW(0x48)));
            SET(0x44, read_word(bank, frame + AW(0x4a)));
            SET(0x40, frame + 2);
            if (AW(0x40) >= AW(0x46)) SET(0x40, 0);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x1e552:
        SET(0x42, 1);
        SET(0x40, 4);
        SET(0, 0xe561);
        /* fall through */
    case 0x1e561:
        if (decrement(ram, actor + 0x42)) break;
        SET(0x42, 0x2a);
        queue_sound_b(ram, 0x8a);
        if (!decrement(ram, actor + 0x40)) finish_animation(ram, actor);
        break;
    case 0x1e58c:
        world_acquire_resource(ram, actor, 0xd9);
        SET(0x0e, 0x3a);
        AB(0x2d) = 0x30;
        SET(0x26, 0x100);
        SET(0x36, 0x100);
        SET(0x3c, 0xf9b3);
        SET(0x2a, read_word(b1, 0xe4ec + (AW(0x1e) & 2)));
        SET(0x34, AW(0x24));
        SET(0, 0xe5c6);
        /* fall through */
    case 0x1e5c6: {
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        SET(0x34, AW(0x34) + read_word(ram, 0xb71c));
        uint16_t phase = AW(0x31);
        SET(0x31, phase + 4);
        sine_y(ram, main_rom, actor, phase, 0x100, 2);
        queue_sprite(ram, main_rom, actor, 0);
        AB(0x0d) = 0;
        uint16_t attacker = world_projectile_contact(ram, main_rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, attacker);
        script_effect(ram, actor, 0x1000, 0xe4f0, 4);
        SET(0x42, 0x10);
        SET(0, 0xe62d);
        break;
    }
    case 0x1e62d:
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x2a, 0x24bb);
            SET(0x3a, 0xffd0);
            SET(0x42, 0x20);
            SET(0, 0xe66b);
        }
        break;
    case 0x1e66b:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        move_y(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) script_effect(ram, actor, 0x1000, 0xe510, 0x20);
        if (terrain(ram, tiles, AW(0x21), AW(0x24) - 8, 0) & 0xff02) {
            script_effect(ram, actor, 0x1000, 0xe52a, 4);
            uint16_t child = tagged_effect(ram, actor, 0x7b, 0x762d);
            CSET(child, 0x34, 0xfff8);
            SET(0, 0xe6d9);
        }
        break;
    case 0x1e6d9:
    case 0x1e828:
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x1e6fe:
        queue_palette(ram, 0x10, 0x1cb, 8);
        finish_animation(ram, actor);
        break;
    case 0x1e720: {
        unsigned kind = AW(0x1e) & 2;
        AB(0x2c) = 0x15;
        AB(0x2d) = 0x3e;
        SET(0x3c, 0xfb4b);
        SET(0x2a, read_word(b1, 0xd44a + kind));
        script_begin(ram, main_rom, actor, read_word(b1, 0xd452 + kind));
        uint16_t child = spawn_boss_part(ram, actor, 0xe7a1);
        CSET(child, 0x21, AW(0x21));
        CSET(child, 0x24, AW(0x24));
        CSET(child, 0x1e, AW(0x1e));
        SET(0, 0xe769);
        break;
    }
    case 0x1e7a1: {
        unsigned kind = AW(0x1e) & 2;
        AB(0x2c) = 0x15;
        AB(0x2d) = 0x3f;
        SET(0x2a, read_word(b1, 0xd44e + kind));
        script_begin(ram, main_rom, actor, read_word(b1, 0xd456 + kind));
        SET(0, 0xe7c8);
        break;
    }
    case 0x1e769:
    case 0x1e7c8: {
        int step = script_step(ram, main_rom, actor);
        if (step < 0) return WORLD_HANDLER_STOP;
        if (step)
            script_begin(ram, main_rom, actor,
                read_word(b1, (address == 0x1e769 ? 0xd452 : 0xd456) + (AW(0x1e) & 2)));
        if (address == 0x1e769 ? AW(0x21) == 0x40 : AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x1e800: {
        unsigned kind = AW(0x1e) & 6;
        SET(0x2a, read_word(b1, 0xd4b2 + kind));
        world_acquire_resource(ram, actor, read_word(b1, 0xd4ba + kind));
        AB(0x2d) = 0x3e;
        SET(0x3c, 0xfb4b);
        SET(0, 0xe828);
        break;
    }
    case 0x1e84d: {
        unsigned kind = AW(0x1e) & 2;
        uint16_t frames = read_word(b1, 0xd4c2 + kind);
        SET(0x46, frames);
        SET(0x2a, read_word(b1, frames));
        world_acquire_resource(ram, actor, read_word(b1, 0xd52e + kind));
        SET(0x0e, 0x39);
        SET(0x2d, 0x30);
        SET(0x26, 0xc0);
        SET(0x36, 0x100);
        SET(0x3c, 0xf9eb);
        SET(0, 0xe88b);
        break;
    }
    case 0x1e88b: {
        static const uint16_t debris[4][3] = {{1, 0x34, 0x380}, {1, 0x4c, 0x3a0}, {6, 0x38, 0x3c0}, {1, 0x40, 0x400}};
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        AB(0x0d) = 0;
        uint16_t attacker = world_surface_contact(ram, main_rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, attacker);
        SET(0x40, 2);
        SET(0x44, 1);
        world_queue_sound(ram, 0x5d);
        uint16_t child = radial_explosion(ram, actor);
        CB(child, 0x2d) = 0x31;
        for (unsigned i = 0; i < 4; i++) {
            child = spawn_boss_part(ram, actor, 0xf4a9);
            CSET(child, 0x21, AW(0x21));
            CSET(child, 0x24, AW(0x24));
            CSET(child, 0x1e, debris[i][0]);
            CB(child, 0x28) = (uint8_t)debris[i][1];
            CSET(child, 0x2e, debris[i][2]);
        }
        SET(0, 0xe974);
        break;
    }
    case 0x1e974:
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            uint16_t sprite = read_word(b1, frame + AW(0x46));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SET(0x2a, sprite);
            SET(0x44, read_word(b1, 0xd50e + frame - 2));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x1e9d2: {
        unsigned kind = AW(0x1e) & 0x1e;
        SET(0x0e, 0x3b);
        SET(0x50, read_word(b1, 0xd532 + kind));
        SET(0x52, read_word(b1, 0xd552 + kind));
        SET(0x54, read_word(b1, 0xd572 + kind));
        SET(0x3c, read_word(b1, 0xd592 + kind));
        SET(0x26, read_word(b1, 0xd5b2 + kind));
        SET(0x36, read_word(b1, 0xd5b2 + kind));
        SET(0, 0xea0f);
        break;
    }
    case 0x1ea0f:
        scroll(ram, actor);
        if (AW(0x21) < 0x180) SET(0, 0xea2a);
        AB(0x0d) = 1;
        world_surface_contact(ram, main_rom, actor);
        break;
    case 0x1ea2a: {
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        AB(0x0d) = 0;
        uint16_t attacker = world_surface_contact(ram, main_rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, attacker);
        SET(0x42, 0x10);
        unsigned kind = AW(0x1e) & 0x1e;
        script_effect(ram, actor, read_word(b1, 0xd5d4 + 2 * kind), read_word(b1, 0xd5d2 + 2 * kind), 2);
        debris_spawner(ram, actor, 0x1000, read_word(b1, 0xda74 + kind));
        world_queue_sound(ram, 0x73);
        SET(0, 0xea9f);
        break;
    }
    case 0x1ea9f:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll(ram, actor);
        if (!decrement(ram, actor + 0x42)) {
            tile_grid(ram, main_rom, tiles, 0x4000, AW(0x50), AW(0x52), AW(0x54));
            finish_animation(ram, actor);
        }
        break;
    case 0x1eace: {
        unsigned wait = world_random_byte(ram) & 7;
        if (!wait) { finish_animation(ram, actor); break; }
        SET(0x42, wait + 8);
        SET(0x0e, 0x3a);
        SET(0x3c, 0xfa03);
        SET(0x26, 0x80);
        SET(0x36, 0x80);
        SET(0x50, read_word(b1, 0xd976 + (AW(0x1e) & 14)));
        SET(0, 0xeb08);
        break;
    }
    case 0x1eb08:
    case 0x1eb87: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (decrement(ram, actor + 0x42)) break;
        SET(0x42, (world_random_byte(ram) & 15) + 8);
        AB(0x0d) = 0;
        uint16_t attacker = world_surface_contact(ram, main_rom, actor);
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, attacker);
        surface_enemy_death_blast(ram, main_rom, actor);
        if (address == 0x1eb08) {
            tile_grid(ram, main_rom, tiles, 0x4000, AW(0x50), 0x3cd0, 0x201);
            SET(0x26, 0x80);
            debris_spawner(ram, actor, 0x1000, 0xdbee);
            if (!(world_random_byte(ram) & 7)) finish_animation(ram, actor);
            else SET(0, 0xeb87);
        } else {
            tile_grid(ram, main_rom, tiles, 0x4000, AW(0x50), 0x3bd4, 0x202);
            debris_spawner(ram, actor, 0x1000, 0xdc14);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x1f41b: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t parent = read_word(ram, 0x84fe + AW(8));
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        const uint8_t *bank = main_rom + ((uint32_t)AW(0x4a) << 4);
        uint16_t row = AW(0x48);
        if (read_word(bank, row) == 0xffff) { finish_animation(ram, actor); break; }
        uint16_t child = world_spawn_child(ram, read_word(ram, 0x9a), 0xf4a9);
        if (child) CSET(child, 2, 0x1000); else child = 0x8480;
        CSET(child, 0x21, AW(0x21) + read_word(bank, row + 2));
        CSET(child, 0x24, AW(0x24) + read_word(bank, row + 4));
        CSET(child, 0x1e, read_word(bank, row));
        CB(child, 0x28) = bank[(uint16_t)(row + 6)];
        CSET(child, 0x2e, read_word(bank, row + 8));
        SET(0x42, read_word(bank, row + 10));
        SET(0x48, row + 12);
        break;
    }
    case 0x1f4a9:
    case 0x1f4e4: {
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address == 0x1f4a9) {
            unsigned kind = AW(0x1e) & 15;
            SET(0x46, kind << 4);
            world_acquire_resource(ram, actor, read_word(b1, 0xda16 + 2 * kind));
            AB(0x2d) = 0x20;
            SET(0x44, 1);
            SET(0x42, 5);
            SET(0x3c, 0xfb4b);
            heading_velocity(ram, main_rom, actor);
            SET(0, 0xf4e4);
        } else scroll(ram, actor);
        move_x(ram, actor);
        move_y(ram, actor);
        if ((int16_t)AW(0x3a) >= -0x300) SET(0x3a, AW(0x3a) - 0x20);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        debris_animate(ram, b1, actor);
        queue_sprite(ram, main_rom, actor, 0);
        unsigned material;
        uint16_t mask = terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        uint16_t bounce = (mask & 0xff02) ? read_word(b1, 0xda3a + 2 * (AW(0x1e) & 15)) : 0;
        if (bounce) {
            SET(0x1e, bounce);
            SET(0x46, (bounce & 15) << 4);
            SET(0x3a, -AW(0x3a));
            SET(0x42, AW(0x42) - 2);
            tagged_effect(ram, actor, 0x7a, 0x7617);
        } else if (material == 4) {
            splash_6d10(ram, actor);
            SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 1));
            SET(0x42, AW(0x42) + 2);
            world_acquire_resource(ram, actor, read_word(b1, 0xda28 + 2 * AW(0x1e)));
            SET(0, 0xf5c8);
        }
        break;
    }
    case 0x1f5c8: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll(ram, actor);
        move_y(ram, actor);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        debris_animate(ram, b1, actor);
        queue_sprite(ram, main_rom, actor, 0);
        if (terrain(ram, tiles, AW(0x21), AW(0x24), 0) & 0xff02) {
            uint16_t entry = read_word(b1, 0xda5a + read_word(b1, 0xda62 + 2 * (AW(0x1e) & 15)));
            if (entry == 0x6e8c) tagged_effect(ram, actor, 0x7b, 0x7601);
            else if (entry == 0x6eb4) tagged_effect(ram, actor, 0x7b, 0x7617);
            else return WORLD_HANDLER_STOP;
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7dd29:
    case 0x7dd83: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned flags = AB(0x1f);
        unsigned schedule = read_word(bank, 0xdb7a + ((flags >> 1) & 14));
        unsigned kind = address == 0x7dd29 ? 6 : 0x10;
        SET(0x60, address == 0x7dd29 ? 0x1c16 : 0xac0a);
        SET(0x62, 0xb000);
        SET(0x0e, kind);
        SET(0x44, (flags & 32) | read_word(bank, 0xdb8a + kind));
        SET(0x42, read_word(bank, 0xdb42 + ((flags << 1) & 6)));
        SET(0x50, 1);
        SET(0x52, read_word(bank, 0xdb4a + schedule));
        SET(0x54, read_word(bank, 0xdb4c + schedule));
        SET(0x56, read_word(bank, 0xdb4c + schedule));
        SET(0x58, read_word(bank, 0xdb4e + schedule));
        SET(0x5a, read_word(bank, 0xdb4e + schedule));
        SET(0x48, read_word(ram, 0xb700) + 16 * AB(0x1e));
        SET(0, 0xdc27);
        break;
    }
    case 0xbac0a: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 0x19);
        AB(0x2d) = 0x20;
        SET(0x0e, 0x14);
        SET(0x3c, 0xf4cb);
        SET(0x2a, 0x645e);
        SET(0x60, 0x28);
        uint16_t rank = read_word(bank, 0xabde + read_word(ram, 0x8a));
        SET(0x26, read_word(bank, 0xabfe + rank));
        SET(0x36, read_word(bank, 0xabfe + rank));
        SET(0x50, read_word(bank, 0xac00 + rank));
        SET(0x52, read_word(bank, 0xac00 + rank));
        SET(0x42, read_word(bank, 0xabda + (AW(0x1e) & 2)));
        AB(0x29) = AB(0x1e) & 1;
        SET(0x38, AB(0x29) ? 0x80 : 0xff80);
        uint16_t child = bank_b_effect(ram, actor, 0xadf2);
        CB(child, 0x29) = AB(0x29);
        CB(child, 0x2c) = AB(0x2c);
        bank_b_effect(ram, actor, 0x24af);
        SET(0x1e, AB(0x29) << 8 & 0x100);
        SET(0x48, 0x258);
        SET(0x58, 4);
        AB(0x29) ^= 1;
        SET(0, 0xacb0);
        break;
    }
    case 0xbacb0:
    case 0xbad59:
    case 0xbada7: {
        scroll(ram, actor);
        if (address == 0xbacb0) move_x(ram, actor);
        if (address == 0xbada7) {
            SET(0x3a, AW(0x3a) + 0x20);
            move_y(ram, actor);
            uint16_t bottom = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
            if (bottom < AW(0x24)) { finish_animation(ram, actor); break; }
        } else if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        AB(0x29) ^= 1;
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x29) ^= 1;
        if (address == 0xbacb0) {
            if (AW(0x42)) {
                if (!decrement(ram, actor + 0x42)) {
                    SET(0x42, 0x40);
                    SET(0, 0xad59);
                }
            } else if (!decrement(ram, actor + 0x50)) {
                SET(0x50, AW(0x52));
                uint16_t child = bank_b_effect(ram, actor, 0x30c6);
                CSET(child, 0x58, AW(0x58));
            }
        } else if (address == 0xbad59) {
            if (!decrement(ram, actor + 0x42)) SET(0, 0xacb0);
            if (AW(0x42) == 0x20) SET(0x46, 1);
        }
        if (address != 0xbada7 && !decrement(ram, actor + 0x48)) SET(0, 0xada7);
        AB(0x0d) = 0;
        world_surface_contact(ram, main_rom, actor);
        if (!AW(0x26)) {
            uint16_t child = effect_slot(ram, actor, 0x6efe);
            CSET(child, 0x4c, 0x74e5);
            CSET(child, 0x4a, 0x7675);
            CSET(child, 0x44, 0x5d);
            CB(child, 0x2d) = 0x20;
            CSET(child, 0x56, 0x40);
            CSET(child, 0x58, 0);
            finish_animation(ram, actor);
        } else register_surface_enemy(ram, actor);
        break;
    }
    case 0xbadf2:
        SET(0x0e, 0x14);
        SET(0x44, 1);
        SET(0, 0xae02);
        break;
    case 0xbae02: {
        uint16_t parent = read_word(ram, 0x84fe + AW(8));
        if (CB(parent, 0x0a)) { finish_animation(ram, actor); break; }
        SET(0x21, CW(parent, 0x21));
        SET(0x24, CW(parent, 0x24));
        SET(0x2a, read_word(main_rom + 0xb0000, 0xabd4 + AW(0x40)));
        queue_sprite(ram, main_rom, actor, 1);
        SET(0x40, AW(0x40) + 2);
        if (AW(0x40) >= 6) SET(0x40, 0);
        break;
    }
    case 0x7d2dd:
        world_acquire_resource(ram, actor, 0xd0);
        SET(0x0e, 0x2e);
        AB(0x2d) = 0x29;
        SET(0x3c, 0xf763);
        SET(0x26, 0x100);
        SET(0x36, 0x100);
        SET(0x44, 1);
        SET(0, 0xd308);
        break;
    case 0x7d308: {
        scroll(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(b7, 0xcb0e + (AW(0x40) & 2)));
            SET(0x44, 15);
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t other = read_word(ram, 0xb8b0);
        if (other && world_shape_overlap(ram, main_rom, actor, other, AW(0x3c), CW(other, 0x3c))) {
            surface_enemy_death_blast(ram, main_rom, actor);
            SET(0x40, 0);
            SET(0x44, 1);
            spawn_bank7(ram, actor, 0xd45f);
            uint16_t child = spawn_bank7(ram, actor, 0xd45f);
            CSET(child, 0x1e, 2);
            SET(0, 0xd390);
        }
        break;
    }
    case 0x7d390: {
        if (!tiles) return WORLD_HANDLER_STOP;
        unsigned material;
        scroll(ram, actor);
        SET(0x3a, AW(0x3a) - 0x10);
        move_y(ram, actor);
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = AW(0x40);
            SET(0x2a, read_word(b7, 0xcb12 + frame));
            SET(0x44, read_word(b7, 0xcb1c + frame));
            SET(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        terrain(ram, tiles, AW(0x21), AW(0x24), &material);
        if (material == 4) {
            SET(0x2a, 0x14f9);
            SET(0x3a, (uint16_t)((int16_t)AW(0x3a) >> 1));
            spawn_bank7(ram, actor, 0xd50b);
            uint16_t child = spawn_bank7(ram, actor, 0xd50b);
            CSET(child, 0x1e, 2);
            SET(0, 0xd412);
        }
        break;
    }
    case 0x7d412:
        scroll(ram, actor);
        move_y(ram, actor);
        if (debris_outside(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7d45f:
    case 0x7d50b: {
        int first = address == 0x7d45f;
        uint16_t parent = read_word(ram, 0x84fe + AW(8));
        SET(0x21, CW(parent, 0x21) + 0x20);
        SET(0x24, CW(parent, 0x24));
        AB(0x2c) = CB(parent, 0x2c);
        AB(0x2d) = 0x31;
        SET(0x44, 1);
        unsigned kind = AW(0x1e) & 2;
        uint16_t speed = read_word(b7, (first ? 0xcb2c : 0xcb38) + kind);
        SET(0x38, speed);
        AB(0x29) = (speed & 0x8000) ? 0 : 1;
        SET(0x42, read_word(b7, (first ? 0xcb30 : 0xcb3c) + kind));
        SET(0, first ? 0xd4b4 : 0xd560);
        break;
    }
    case 0x7d4b4:
    case 0x7d560:
        scroll(ram, actor);
        move_x(ram, actor);
        if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            if (address == 0x7d4b4) SET(0x2a, read_word(b7, 0xcb26 + AW(0x40)));
            else SET(0x2a, read_word(b7, 0xcb34 + (AW(0x40) & 2)));
            SET(0x44, AW(0x42));
            SET(0x40, AW(0x40) + 2);
            if (address == 0x7d4b4 && AW(0x40) >= 6) SET(0x40, 0);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7d5b0: {
        world_acquire_resource(ram, actor, 0xd5);
        AB(0x2d) = 0x28;
        SET(0x44, 1);
        SET(0x4c, read_word(b7, 0xcb90 + (AW(0x1e) & 14)));
        unsigned kind = AB(0x1f) & 14;
        uint16_t speed = read_word(b7, 0xcba0 + kind);
        SET(0x38, speed);
        AB(0x29) = (speed & 0x8000) ? 0 : 1;
        SET(0x42, read_word(b7, 0xcbb0 + (kind & 6)));
        SET(0, 0xd5fd);
        break;
    }
    case 0x7d5fd:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll(ram, actor);
        move_x(ram, actor);
        if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if ((AW(0x1e) & 0x8000) && (CB(read_word(ram, 0x84fe + AW(8)), 0x0b) & 1)) SET(0x38, 0);
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(b7, 0xcb40 + AW(0x40) + AW(0x4c)));
            SET(0x44, AW(0x42));
            SET(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 12) SET(0x40, 0);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if ((AW(0x1e) & 1) && !(terrain(ram, tiles, AW(0x21), AW(0x24) - 4, 0) & 0xff02)) {
            SET(0x4c, (AW(0x1e) & 14) << 1);
            SET(0x40, 0);
            SET(0x44, 1);
            SET(0x38, (uint16_t)((int16_t)AW(0x38) >> 1));
            SET(0, 0xd6af);
        }
        break;
    case 0x7d6af:
    case 0x7d6c3:
    case 0x7d6e7:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address == 0x7d6af) {
            SET(0x3a, AW(0x3a) - 8);
            if ((int16_t)AW(0x3a) <= -0x100) {
                SET(0x38, (uint16_t)((int16_t)AW(0x38) >> 3));
                SET(0, 0xd6c3);
            }
        }
        if (address != 0x7d6e7) {
            unsigned material;
            move_x(ram, actor);
            move_y(ram, actor);
            terrain(ram, tiles, AW(0x21), AW(0x24), &material);
            if (material == 4) {
                uint16_t child = surface_splash(ram, actor, 0x7555, 0x7685);
                CSET(child, 0x44, 0x0b);
                CB(child, 0x2d) = 0x20;
                CSET(child, 0x56, 0x30);
                CSET(child, 0x58, 0x80);
                snap_row_below(ram, child);
                SET(0, 0xd6e7);
            }
        }
        scroll(ram, actor);
        if (AW(0x21) > 0x1c0 || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            SET(0x2a, read_word(b7, 0xcb7c + (AW(0x40) & 2) + AW(0x4c)));
            SET(0x44, 6);
            SET(0x40, AW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x400c: {
        /* Far background: three parallax bands written straight to the row-scroll table. */
        if (!tiles) return WORLD_HANDLER_STOP;
        if (!(read_word(ram, 0x26e) & 0x4000)) { write_word(ram, 0xb71e, 0); break; }
        int16_t speed = read_word(ram, 0xa4);
        move_actor_axis(ram, actor + 0x51, (uint16_t)(speed >> 1));
        write_word(ram, 0xb70e, AW(0x52));
        uint16_t bias = read_word(main_rom, 0x5051 + ((ram[0x279] & 1) ? 0 : 2));
        move_actor_axis(ram, actor + 0x70, (uint16_t)(speed >> 2));
        for (unsigned i = 0; i < 0x70; i++) write_word(tiles, 0xfd10 + 2 * i, AW(0x71) + AW(0x52) + bias);
        move_actor_axis(ram, actor + 0x73, (uint16_t)(speed >> 3));
        for (unsigned i = 0; i < 0x60; i++) write_word(tiles, 0xfd80 + 2 * i, AW(0x74) + AW(0x52) + bias);
        for (unsigned i = 0; i < 0x60; i++) write_word(tiles, 0xfde0 + 2 * i, AW(0x52) + bias);
        write_word(ram, 0xb71e, (uint16_t)((int16_t)read_word(ram, 0xb71a) >> 1));
        advance_scroll_script(ram, main_rom, actor);
        break;
    }
    case 0x413b:
        ram[0x26f] &= (uint8_t)~0x40u;
        write_word(ram, 0xb71a, 0);
        write_word(ram, 0xb72a, 0);
        write_word(ram, 0xb726, 0);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
