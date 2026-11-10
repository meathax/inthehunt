/* Recovered World actor handlers for stage 6 (stage index 5) and the endings.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include "world_frame.h"
#include <string.h>

/* 1769 with AW = 7000: the child runs in this bank. A full pool hands back
 * the allocator's discard slot, which callers still write to. */
static uint16_t spawn7(uint8_t *ram, uint16_t actor, uint16_t handler)
{
    uint16_t child = world_spawn_child(ram, actor, handler);
    if (!child) return 0x8480;
    write_word(ram, child + 2, 0x7000);
    return child;
}

/* The actor stored one or two list levels above this one. */
static uint16_t ancestor(const uint8_t *ram, uint16_t actor, unsigned levels)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2 * levels));
}

/* 3665: the second sound ring. */
static void queue_sound_b(uint8_t *ram, uint16_t command)
{
    uint16_t tail = (read_word(ram, 0xa6d8) + 2) & 14;
    if (tail != read_word(ram, 0xa6d6)) {
        write_word(ram, 0xa6d8, tail);
        write_word(ram, 0xa6a2 + tail, command);
    }
}

/* 37EB: CH columns of CL four-by-four tile blocks from a list in bank A000. */
static void tile_columns(uint8_t *ram, uint8_t *tiles, const uint8_t *rom, uint16_t base,
                         uint16_t origin, uint16_t source, unsigned width, unsigned height)
{
    write_word(ram, 0xb89a, (uint16_t)((width << 8) | height));
    for (unsigned x = 0; x < width; ++x) {
        uint16_t pos = origin;
        write_word(ram, 0xb89c, origin);
        for (unsigned y = 0; y < height; ++y) {
            world_draw_tile_block(tiles, rom, base, pos, read_word(rom + 0xa0000, source));
            source += 2;
            pos = (pos + 0x400) & 0x3fff;
        }
        origin = (origin & 0xff00) | ((origin + 16) & 255);
    }
}

/* 5F4D: a byte string from bank 0 onto the text layer. */
static void tile_text(uint8_t *tiles, const uint8_t *rom, uint16_t pos, uint16_t text, uint16_t attr)
{
    for (; rom[text] != 0xff; ++text) {
        write_word(tiles, pos, rom[text]);
        write_word(tiles, pos + 2, attr);
        pos = (pos & 0xff00) | ((pos + 4) & 255);
    }
}

/* 7C9A4: redraw the rising gate strip and its label. */
static void draw_gate_strip(uint8_t *ram, uint8_t *tiles, const uint8_t *rom, uint16_t actor)
{
    uint16_t origin = 0x1b30 - read_word(ram, 0xb8ca);
    tile_columns(ram, tiles, rom, 0, origin, 0x5c5c + read_word(ram, actor + 0x40), 16, 1);
    tile_text(tiles, rom, origin + 0x400, 0x6381, 0x104);
}

/* 7C95E: follow the parent at a vertical offset. */
static uint16_t follow_parent(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = ancestor(ram, actor, 1);
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(ram, actor + 0x34));
    return parent;
}

/* 7C8B8: shared tail of the gate machinery parts. */
static void gate_part_tail(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    queue_sprite(ram, rom, actor, 0);
    if (read_word(ram, actor + 0x42) && !decrement(ram, actor + 0x42)) {
        write_word(ram, actor + 0x42, 0x3c);
        queue_sound_b(ram, 0x91);
    }
}

/* 68DF without the final bit selection: the material under a screen point. */
static unsigned terrain_material(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    return tiles[0x4000 + world_tile_address(map_x, map_y) + 3] >> 3;
}

/* ---- Final boss: the command submarine (7ABDC..7C311) ---- */

#define T7(disp, index) read_word(bank7, (uint16_t)((index) - 0x##disp))
#define RW(off) read_word(ram, (uint16_t)(actor + (off)))
#define WW(off, value) write_word(ram, (uint16_t)(actor + (off)), (uint16_t)(value))

/* The difficulty row: 7000:AB3D plus the offset selected by RAM[88]. */
static uint16_t boss_row(const uint8_t *ram, const uint8_t *bank7, unsigned field)
{
    uint16_t row = (uint16_t)(read_word(bank7, (uint16_t)(0xab1d + read_word(ram, 0x88))) + 0xab3d);
    return read_word(bank7, (uint16_t)(row + field));
}

/* 7C278 */
static void boss_follow(uint8_t *ram, uint16_t actor, uint16_t parent)
{
    WW(0x21, read_word(ram, parent + 0x21) + RW(0x31));
    WW(0x24, read_word(ram, parent + 0x24) + RW(0x34));
}

static void boss_touch(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = 1;
    world_projectile_contact(ram, rom, actor);
}

/* 7C21B: the hull is drawn once and collides with two shapes. */
static void boss_hull(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if ((int16_t)RW(0x21) >= 0x1c0) return;
    queue_sprite(ram, rom, actor, 0);
    WW(0x3c, 0xf963);
    boss_touch(ram, rom, actor);
    WW(0x3c, 0xf96b);
    boss_touch(ram, rom, actor);
}

/* 7C259 */
static void boss_section(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if ((int16_t)RW(0x21) >= 0x1d0) return;
    queue_sprite(ram, rom, actor, 0);
    boss_touch(ram, rom, actor);
}

/* 6761 with BW = 100: vertical position on a sine around the 24-bit centre. */
static void boss_wave(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t amplitude)
{
    int16_t unit = (int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * ram[actor + 0x65]);
    uint32_t centre = ram[actor + 0x33] | (uint32_t)ram[actor + 0x34] << 8 | (uint32_t)ram[actor + 0x35] << 16;
    uint32_t y = centre + (uint32_t)((int32_t)unit * (int16_t)amplitude);
    ram[actor + 0x23] = (uint8_t)y;
    ram[actor + 0x24] = (uint8_t)(y >> 8);
    ram[actor + 0x25] = (uint8_t)(y >> 16);
}

/* 7C2F1 */
static void boss_jitter(uint8_t *ram, uint16_t actor)
{
    WW(0x21, (world_random_byte(ram) & 2) - 1 + RW(0x31));
    WW(0x24, (world_random_byte(ram) & 2) - 1 + RW(0x34));
}

/* 7C2E7 */
static void boss_sink(uint8_t *ram, uint16_t actor)
{
    move_actor_axis(ram, actor + 0x30, RW(0x38));
    move_actor_axis(ram, actor + 0x33, RW(0x3a));
    boss_jitter(ram, actor);
}

/* 7036: a timed chain of blasts described by a table in the caller's bank. */
static uint16_t blast_chain(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t table, uint16_t count)
{
    uint16_t child = effect_slot(ram, actor, 0x7056);
    write_word(ram, child + 0x60, table);
    write_word(ram, child + 0x62, segment);
    write_word(ram, child + 0x4c, count);
    write_word(ram, child + 0x42, 1);
    return child;
}

/* 6AF3 */
static void medium_blast(uint8_t *ram, uint16_t actor)
{
    uint16_t blast = effect_slot(ram, actor, 0x6efe);
    write_word(ram, blast + 0x4c, 0x74a1);
    write_word(ram, blast + 0x4a, 0x7675);
    ram[blast + 0x2c] = 0x0a;
    ram[blast + 0x2d] = 0x20;
    write_word(ram, blast + 0x56, 0x30);
    write_word(ram, blast + 0x58, 0x80);
    world_queue_sound(ram, 0x5e);
    if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
}

/* 6B2E */
static void loud_radial_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    world_spawn_radial_explosion(ram, rom, actor);
}

/* 7C28B: four missiles and their launch flashes. */
static void boss_missiles(uint8_t *ram, const uint8_t *bank7, uint16_t actor)
{
    WW(0x50, RW(0x52));
    for (unsigned n = 4; n; --n) {
        uint16_t child = spawn7(ram, actor, 0xbe50);
        ram[child + 0x2c] = ram[actor + 0x14];
        write_word(ram, child + 0x60, n - 1);
        write_word(ram, child + 0x1e, RW(0x1e));
        write_word(ram, child + 0x2e, RW(0x62));
        world_queue_sound(ram, 0x69);
        child = effect_slot(ram, actor, 0xa220);
        write_word(ram, child + 0x31, 0x38);
        write_word(ram, child + 0x34, T7(595f, 2 * (n - 1)));
        ram[child + 0x2d] = 3;
    }
}

/* 7AE11: the hull dies when its armour is gone. */
static void boss_hull_fate(uint8_t *ram, uint16_t actor)
{
    if (RW(0x26)) return;
    WW(0x38, 0xff80);
    WW(0x31, RW(0x21));
    WW(0x34, RW(0x24));
    uint16_t child = blast_chain(ram, actor, 0x7000, 0xa9cb, 4);
    write_word(ram, child + 0x1e, 1);
    ram[0x262] |= 0x82;
    WW(0, 0xaed8);
}

/* A hit on a weapon is taken from the hull's armour; the last hit ends the stage. */
static void boss_core_damage(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t shot)
{
    uint16_t hull = ancestor(ram, actor, 1);
    uint16_t armour = read_word(ram, hull + 0x26), amount = read_word(ram, shot + 0x36);
    write_word(ram, hull + 0x26, armour - amount);
    if (armour > amount) return;
    if (!(read_word(ram, 0x264) & 3)) {
        write_word(ram, hull + 0x26, 0x100);
        return;
    }
    write_word(ram, hull + 0x26, 0);
    world_award_score(ram, rom, actor, shot);
    ram[0x262] |= 0x82;
}

/* 7B231: the head's animation, shared by its three states. */
static void boss_head_tail(uint8_t *ram, const uint8_t *rom, const uint8_t *bank7, uint16_t actor)
{
    if (!decrement(ram, actor + 0x44)) {
        uint16_t step = RW(0x40);
        uint16_t frame = read_word(bank7, (uint16_t)(RW(0x4c) + step));
        if (frame == 0xffff) {
            WW(0x40, 0);
            WW(0x44, 1);
            if (!decrement(ram, actor + 0x50)) {
                WW(0x50, 4);
                uint16_t pick = world_random_byte(ram) & 6;
                if (pick > 2) pick = 2;
                WW(0x4c, T7(5869, pick + RW(0x1e)));
            }
        } else {
            WW(0x2a, frame);
            WW(0x44, T7(57d9, step));
            WW(0x40, step + 2);
        }
    }
    if (ram[actor + 0x19]) {
        --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x15 + ((ram[actor + 0x19] & 2) >> 1)];
    }
    queue_sprite(ram, rom, actor, 0);
}

/* 7B592: a hatch flashes, dies with its section, and passes hits on to it. */
static void boss_hatch_tail(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (ram[actor + 0x19]) --ram[actor + 0x19];
    ram[actor + 0x2c] = ram[actor + 0x14 + (((ram[actor + 0x19] & 2) ^ 2) >> 1)];
    queue_sprite(ram, rom, actor, 0);
    uint16_t section = ancestor(ram, actor, 1);
    if (ram[section + 0x0a]) {
        loud_radial_blast(ram, rom, actor);
        finish_animation(ram, actor);
        return;
    }
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    if (ram[actor + 0x6e]) {
        --ram[actor + 0x6e];
        return;
    }
    ram[actor + 0x0d] = 0;
    uint16_t shot = world_projectile_contact(ram, rom, actor);
    if (!shot) return;
    ram[actor + 0x6e] = 2;
    uint16_t armour = read_word(ram, section + 0x26), amount = read_word(ram, shot + 0x36);
    write_word(ram, section + 0x26, armour < amount ? 0 : armour - amount);
}

/* 7BF67: a missile dies on touching a player or when shot down. */
static void boss_missile_tail(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    if (!world_player_contact(ram, rom, actor)) {
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, rom, actor);
        if (RW(0x26)) return;
    }
    medium_blast(ram, actor);
    finish_animation(ram, actor);
}

/* 7BF27 */
static void boss_missile_fly(uint8_t *ram, const uint8_t *rom, const uint8_t *bank7, uint16_t actor)
{
    WW(0x2a, T7(5937, RW(0x40) & 2));
    WW(0x40, RW(0x40) + 2);
    queue_sprite(ram, rom, actor, 0);
    if (!decrement(ram, actor + 0x50)) {
        WW(0x50, 5);
        /* 17C3: the trail is linked behind the effects anchor. */
        uint16_t trail = world_spawn_child(ram, read_word(ram, 0x9a), 0xbe41);
        if (!trail) trail = 0x8480;
        write_word(ram, trail + 0x21, RW(0x21) - 0x10);
        write_word(ram, trail + 0x24, RW(0x24));
        ram[trail + 0x2c] = 3;
    }
    boss_missile_tail(ram, rom, actor);
}

/* 7C06A */
static void boss_shell_tail(uint8_t *ram, const uint8_t *rom, const uint8_t *bank7, uint16_t actor)
{
    if (!decrement(ram, actor + 0x44)) {
        WW(0x2a, T7(5879, RW(0x40) & 0xe));
        WW(0x44, 2);
        WW(0x40, RW(0x40) + 2);
    }
    queue_sprite(ram, rom, actor, 0);
    ram[actor + 0x0d] = 2;
    if (!world_player_contact(ram, rom, actor)) {
        ram[actor + 0x0d] = 1;
        if (!world_projectile_contact(ram, rom, actor)) return;
    }
    if (!RW(0x5e)) return;
    medium_blast(ram, actor);
    finish_animation(ram, actor);
}

static int final_boss(uint8_t *ram, const uint8_t *main_rom, uint8_t *tiles, uint16_t actor, uint32_t address)
{
    const uint8_t *bank7 = main_rom + 0x70000;
    uint16_t parent = ancestor(ram, actor, 1);
    switch (address) {
    case 0x7abdc: {
        world_acquire_resource(ram, actor, 0x3f);
        WW(0x0e, 0x4a);
        ram[actor + 0x2c] = 0x23;
        ram[actor + 0x2d] = 0x30;
        WW(0x2a, 0x32b3);
        WW(0x38, 0x40);
        WW(0x68, 0x80);
        WW(0x6a, 0x1a);
        WW(0x26, boss_row(ram, bank7, 2));
        WW(0x36, boss_row(ram, bank7, 2));
        WW(0x50, boss_row(ram, bank7, 0x1c));
        WW(0x52, boss_row(ram, bank7, 0x1c));
        WW(0x62, boss_row(ram, bank7, 0x1e));
        uint16_t child = spawn7(ram, actor, 0xb37e);
        write_word(ram, child + 0x31, 0xfee0);
        write_word(ram, child + 0x1e, 0);
        child = spawn7(ram, actor, 0xb912);
        write_word(ram, child + 0x31, 0xff30);
        write_word(ram, child + 0x1e, 1);
        write_word(ram, child + 0x50, 0x82);
        child = spawn7(ram, actor, 0xb37e);
        write_word(ram, child + 0x31, 0xff70);
        write_word(ram, child + 0x1e, 2);
        child = spawn7(ram, actor, 0xb912);
        write_word(ram, child + 0x31, 0xffc0);
        write_word(ram, child + 0x1e, 3);
        write_word(ram, child + 0x50, 0x42);
        spawn7(ram, actor, 0xbdef);
        spawn7(ram, actor, 0xaf1c);
        WW(0x34, RW(0x24));
        queue_sound_b(ram, 0x11);
        WW(0, 0xacb5);
        break;
    }
    case 0x7acb5:
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        WW(0x64, RW(0x64) + RW(0x68));
        boss_wave(ram, main_rom, actor, RW(0x6a));
        if ((int16_t)RW(0x21) >= 0x2a0) {
            WW(0x48, RW(0x48) | 1u << (RW(0x4a) & 15));
            WW(0, 0xacf4);
        }
        if ((int16_t)RW(0x21) <= 0x160 && !decrement(ram, actor + 0x50)) boss_missiles(ram, bank7, actor);
        boss_hull(ram, main_rom, actor);
        break;
    case 0x7acf4:
        WW(0x64, RW(0x64) + RW(0x68));
        if (!RW(0x64) && !RW(0x48)) {
            uint16_t phase = RW(0x4a);
            WW(0x38, 0xff80);
            WW(0x68, T7(56c5, 2 * phase));
            WW(0x6a, T7(56bd, 2 * phase));
            if (phase != 3) {
                WW(0x42, T7(56cd, 2 * phase));
                WW(0, 0xad9a);
            } else {
                WW(0x26, boss_row(ram, bank7, 2));
                WW(0x36, boss_row(ram, bank7, 2));
                WW(0x50, boss_row(ram, bank7, 0x20));
                WW(0x52, boss_row(ram, bank7, 0x20));
                WW(0x62, boss_row(ram, bank7, 0x22));
                WW(0x42, 8);
                WW(0x5c, 1);
                spawn7(ram, actor, 0xb114);
                write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 0x1e);
                world_queue_sound(ram, 0x5f);
                WW(0, 0xadcb);
            }
        }
        boss_wave(ram, main_rom, actor, RW(0x6a));
        boss_hull(ram, main_rom, actor);
        break;
    case 0x7ad9a:
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        WW(0x64, RW(0x64) + RW(0x68));
        boss_wave(ram, main_rom, actor, RW(0x6a));
        if (!decrement(ram, actor + 0x42)) {
            WW(0x4a, RW(0x4a) + 1);
            WW(0x48, RW(0x48) | 1u << (RW(0x4a) & 15));
            WW(0, 0xacf4);
        }
        boss_hull(ram, main_rom, actor);
        break;
    case 0x7adcb:
        boss_hull(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42)) WW(0, 0xae5f);
        break;
    case 0x7add9:
        WW(0x64, RW(0x64) + RW(0x68));
        if (!RW(0x64) && !decrement(ram, actor + 0x42)) {
            begin_linear_script(ram, main_rom, actor, 0xa94b);
            WW(0, 0xae4e);
        }
        boss_wave(ram, main_rom, actor, 0x28);
        boss_hull(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x50)) {
            WW(0x1e, 8);
            boss_missiles(ram, bank7, actor);
        }
        boss_hull_fate(ram, actor);
        break;
    case 0x7ae4e:
    case 0x7aebc: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0) {
            if (address == 0x7ae4e) WW(0, 0xae5f);
            else {
                WW(0x42, 4);
                WW(0x5c, 3);
                WW(0, 0xadd9);
            }
        }
        boss_hull(ram, main_rom, actor);
        boss_hull_fate(ram, actor);
        break;
    }
    case 0x7ae5f:
    case 0x7ae7b:
        if (address == 0x7ae5f) {
            WW(0x42, 0x1f7);
            WW(0x50, 0xa8);
            WW(0x44, 3);
            WW(0x6c, 1);
            WW(0, 0xae7b);
        } else WW(0x6c, 0);
        WW(0x64, RW(0x64) + 0x100);
        if (!RW(0x64) && !decrement(ram, actor + 0x44)) {
            begin_linear_script(ram, main_rom, actor, 0xa991);
            WW(0, 0xaebc);
        }
        boss_wave(ram, main_rom, actor, 0x28);
        boss_hull(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x50)) {
            WW(0x50, 0xb0);
            WW(0x6c, 1);
        }
        boss_hull_fate(ram, actor);
        break;
    case 0x7aed8:
        move_actor_axis(ram, actor + 0x30, RW(0x38));
        if ((int16_t)RW(0x21) <= -16) {
            ram[0x262] |= 0x40;
            finish_animation(ram, actor);
            break;
        }
        boss_jitter(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7af1c:
        world_acquire_resource(ram, actor, 0xf0);
        world_acquire_resource(ram, actor, 0xf9);
        ram[actor + 0x16] = 0x23;
        ram[actor + 0x2c] = 0x23;
        WW(0x0e, 0x4b);
        ram[actor + 0x2d] = 0x31;
        WW(0x2a, 0x992b);
        WW(0x3c, 0xf973);
        WW(0x26, 0x100);
        WW(0x36, 0x100);
        WW(0, 0xaf56);
    /* fall through */
    case 0x7af56: {
        WW(0x21, read_word(ram, parent + 0x21) + 0x50);
        WW(0x24, read_word(ram, parent + 0x24));
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x15 + (((ram[actor + 0x19] + 2) & 2) >> 1)];
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        uint16_t mode = read_word(ram, parent + 0x5c);
        if (!mode) {
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
            break;
        }
        if (mode != 3) {
            ram[actor + 0x0d] = 0;
            uint16_t shot = world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
            world_award_score(ram, main_rom, actor, shot);
        }
        blast_chain(ram, actor, 0x7000, 0xaaeb, 2);
        WW(0x31, 0);
        WW(0x42, 0x70);
        WW(0x3c, 0xf97b);
        WW(0x2a, 0x9935);
        ram[actor + 0x19] = 0;
        ram[actor + 0x2c] = ram[actor + 0x14];
        WW(0, 0xb009);
        break;
    }
    case 0x7b009:
        WW(0x21, read_word(ram, parent + 0x21) + RW(0x31));
        WW(0x24, read_word(ram, parent + 0x24));
        if ((int16_t)RW(0x31) < 0x50) WW(0x31, RW(0x31) + 1);
        if (RW(0x42) && !decrement(ram, actor + 0x42)) {
            WW(0x40, 0);
            WW(0x44, 1);
            WW(0, 0xb050);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7b050: {
        WW(0x21, read_word(ram, parent + 0x21) + RW(0x31));
        WW(0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40) & 0x1e;
            WW(0x2a, T7(57b9, step));
            WW(0x44, T7(5799, step & 2));
            WW(0x40, RW(0x40) + 2);
        }
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x14 + ((ram[actor + 0x19] & 2) >> 1)];
        queue_sprite(ram, main_rom, actor, 0);
        if (!read_word(ram, parent + 0x26)) break;
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 0;
        uint16_t shot = world_projectile_contact(ram, main_rom, actor);
        if (shot) boss_core_damage(ram, main_rom, actor, shot);
        break;
    }
    case 0x7b114: {
        world_acquire_resource(ram, actor, 0xf3);
        world_acquire_resource(ram, actor, 0xf0);
        world_acquire_resource(ram, actor, 0xf9);
        ram[actor + 0x2c] = ram[actor + 0x15];
        WW(0x0e, 0x4a);
        ram[actor + 0x2d] = 0x31;
        WW(0x3c, 0xf99b);
        WW(0x38, 0xff00);
        WW(0x36, 0x1000);
        WW(0x4c, 0xa79f);
        WW(0x44, 1);
        WW(0x50, 4);
        WW(0x42, 0x30);
        write_word(ram, 0x272, 0);
        unsigned players = read_word(ram, 0x264);
        int extra = 0;
        if ((players & 3) != 3) {
            uint16_t side = players & 2, record = side << 5;
            if ((read_word(ram, record + 0x124) & 0x1f) == 0x1f) {
                if (!ram[read_word(ram, side + 0x254)] || (int16_t)read_word(ram, record + 0x11a) >= 0x16d) extra = 1;
                else {
                    int16_t rank = (int16_t)read_word(ram, 0xba4a);
                    extra = rank < 0x0b || (rank == 0x0b && (int16_t)read_word(ram, 0xba48) <= 3);
                }
            }
        }
        if (extra) {
            WW(0x42, 0x70);
            write_word(ram, 0x272, 1);
            uint16_t child = spawn7(ram, actor, 0xb347);
            ram[child + 0x2d] = 0x31;
            write_word(ram, child + 0x44, 1);
            write_word(ram, child + 0x42, 0x30);
            write_word(ram, child + 0x31, 0x30);
            write_word(ram, child + 0x4c, 0xa7e3);
            write_word(ram, child + 0x3c, 0xf9a3);
            write_word(ram, child + 0x36, 0x800);
            write_word(ram, child + 0x1e, 4);
        }
        write_word(ram, parent + 0x42, RW(0x42));
        WW(0, 0xb1ff);
    }
    /* fall through */
    case 0x7b1ff:
        move_actor_axis(ram, actor + 0x30, RW(0x38));
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x42)) WW(0, 0xb2a4);
        boss_touch(ram, main_rom, actor);
        boss_head_tail(ram, main_rom, bank7, actor);
        break;
    case 0x7b2a4: {
        boss_follow(ram, actor, parent);
        if (!read_word(ram, parent + 0x26)) {
            boss_head_tail(ram, main_rom, bank7, actor);
            break;
        }
        if (read_word(ram, parent + 0x6c)) {
            for (unsigned n = 0x0b; n; --n) {
                uint16_t child = spawn7(ram, actor, 0xc0ef);
                ram[child + 0x2c] = ram[actor + 0x14];
                write_word(ram, child + 0x1e, n - 1);
            }
        }
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        if (ram[actor + 0x6e]) --ram[actor + 0x6e];
        else {
            ram[actor + 0x0d] = 0;
            uint16_t shot = world_projectile_contact(ram, main_rom, actor);
            if (shot) {
                ram[actor + 0x6e] = 2;
                boss_core_damage(ram, main_rom, actor, shot);
            }
        }
        boss_head_tail(ram, main_rom, bank7, actor);
        break;
    }
    case 0x7b347:
        if (!decrement(ram, actor + 0x42)) WW(0, 0xb352);
        break;
    case 0x7b352:
        ram[actor + 0x2c] = ram[parent + 0x2c];
        boss_follow(ram, actor, parent);
        boss_touch(ram, main_rom, actor);
        boss_head_tail(ram, main_rom, bank7, actor);
        break;
    case 0x7b37e:
        ram[actor + 0x2c] = 0x23;
        ram[actor + 0x2d] = 0x30;
        WW(0x2a, 0x3291);
        WW(0x3c, 0xf963);
        WW(0x26, boss_row(ram, bank7, 0));
        WW(0x36, boss_row(ram, bank7, 0));
        spawn7(ram, actor, 0xbdef);
        WW(0, 0xb3b7);
        break;
    case 0x7b3b7:
        if (read_word(ram, parent + 0x48) >> (RW(0x1e) & 15) & 1) {
            uint16_t child = spawn7(ram, actor, 0xb4ea);
            write_word(ram, child + 0x1e, RW(0x1e));
            write_word(ram, child + 0x34, 0x18);
            child = spawn7(ram, actor, 0xb4ea);
            write_word(ram, child + 0x1e, RW(0x1e));
            write_word(ram, child + 0x34, 0xffe8);
            ram[child + 0x29] = 2;
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 0x0a);
            world_queue_sound(ram, 0x5f);
            WW(0, 0xb425);
        }
        boss_follow(ram, actor, parent);
        boss_section(ram, main_rom, actor);
        break;
    case 0x7b425:
    case 0x7ba24:
        if (!RW(0x26)) {
            int upper = address == 0x7b425;
            WW(0x38, 0xff00);
            WW(0x3a, upper ? 0xffc0 : 0x40);
            WW(0x31, RW(0x21));
            WW(0x34, RW(0x24));
            ram[actor + 0x0a] = 2;
            uint16_t child = blast_chain(ram, actor, 0x7000, upper ? 0xaa4b : 0xaa9b, 4);
            write_word(ram, child + 0x1e, 1);
            write_word(ram, parent + 0x48, read_word(ram, parent + 0x48) & ~(1u << (RW(0x1e) & 15)));
            WW(0, upper ? 0xb483 : 0xba83);
        }
        boss_follow(ram, actor, parent);
        boss_section(ram, main_rom, actor);
        if (address == 0x7ba24) goto section_b_timer;
        break;
    case 0x7b483:
    case 0x7ba83: {
        boss_sink(ram, actor);
        if (RW(0x21) < 0x20) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0x7b483) {
            unsigned material = terrain_material(ram, tiles, RW(0x21), RW(0x24) - 0x30);
            if ((1u << (material & 15)) & 0xff02) WW(0, 0xb4bf);
        } else if (terrain_material(ram, tiles, RW(0x21), RW(0x24) + 0x20) == 3) WW(0, 0xbabe);
        break;
    }
    case 0x7b4bf:
    case 0x7babe:
        WW(0x31, RW(0x31) + (uint16_t)((int16_t)read_word(ram, 0xb72a) >> 1));
        if (RW(0x21) < 0x20) {
            finish_animation(ram, actor);
            break;
        }
        boss_jitter(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7b4ea: {
        world_acquire_resource(ram, actor, 0xf7);
        ram[actor + 0x15] = 0x23;
        ram[actor + 0x2c] = 0x23;
        ram[actor + 0x2d] = 0x33;
        WW(0x2a, 0x330f);
        WW(0x3c, 0xf98b);
        WW(0x38, 0xff00);
        WW(0x42, 0x18);
        WW(0x31, 0xfff8);
        WW(0x36, 0x100);
        unsigned base = RW(0x1e) ? 0x0a : 4;
        WW(0x52, boss_row(ram, bank7, base));
        WW(0x56, boss_row(ram, bank7, base + 2));
        WW(0x5a, boss_row(ram, bank7, base + 4));
        WW(0x50, 0x40 - ((ram[actor + 0x29] & 2) << 4));
        WW(0, 0xb572);
    }
    /* fall through */
    case 0x7b572:
        move_actor_axis(ram, actor + 0x30, RW(0x38));
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x42)) WW(0, 0xb61e);
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7b61e:
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x50)) {
            WW(0x40, 0);
            WW(0x44, 1);
            world_queue_sound(ram, 0x4f);
            WW(0, 0xb64e);
        }
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7b64e:
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40), frame = T7(5929, step);
            if (frame == 0xffff) {
                WW(0x54, 1);
                WW(0x58, RW(0x5a));
                WW(0, 0xb696);
            } else {
                WW(0x2a, frame);
                WW(0x44, T7(591b, step));
                WW(0x40, step + 2);
            }
        }
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7b696:
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x54)) {
            WW(0x54, RW(0x56));
            if (!decrement(ram, actor + 0x58)) {
                WW(0x40, 0);
                WW(0x44, 1);
                WW(0, 0xb6ef);
            }
            uint16_t child = spawn7(ram, actor, 0xb732);
            write_word(ram, child + 0x1e, RW(0x1e));
            write_word(ram, child + 0x21, RW(0x21));
            write_word(ram, child + 0x24, RW(0x24));
        }
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7b6ef:
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40), frame = T7(590f, step);
            if (frame == 0xffff) {
                WW(0x50, RW(0x52));
                WW(0, 0xb61e);
            } else {
                WW(0x2a, frame);
                WW(0x44, T7(58ff, step));
                WW(0x40, step + 2);
            }
        }
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7b732: {
        uint16_t kind = RW(0x1e) & 2;
        WW(0x4c, T7(5791, kind));
        WW(0x3c, T7(574d, kind));
        WW(0x42, T7(5745, kind));
        WW(0x21, RW(0x21) + T7(5741, kind));
        world_acquire_resource(ram, actor, T7(5749, kind));
        ram[actor + 0x72] = bank7[0xa8c3 + (kind >> 1)];
        ram[actor + 0x2d] = 0x34;
        ram[actor + 0x0c] = 4;
        WW(0x26, 0x40);
        WW(0x36, 0x10);
        WW(0x38, 0xfb00);
        WW(0x44, 1);
        if (kind) spawn7(ram, actor, 0xb8ab);
        WW(0, 0xb7a8);
        break;
    }
    case 0x7b7a8:
    case 0x7b7f8: {
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        if (address == 0x7b7a8) {
            WW(0x24, read_word(ram, parent + 0x24));
            if (!decrement(ram, actor + 0x42)) {
                unsigned players = read_word(ram, 0x264) & 3;
                if (players == 3 && !world_enemy_target_mode_supported(ram[actor + 0x0c])) return WORLD_HANDLER_STOP;
                WW(0x38, 0xff80);
                WW(0x42, 0x20);
                if (!players) WW(0x70, 0);
                else {
                    uint16_t target = world_enemy_target(ram, actor);
                    WW(0x70, target);
                    WW(0x3a, read_word(ram, target + 0x24) < RW(0x24) ? 0xff00 : 0x100);
                }
                WW(0, 0xb7f8);
            }
        } else {
            move_actor_axis(ram, actor + 0x23, RW(0x3a));
            if (RW(0x21) < 0x40) {
                finish_animation(ram, actor);
                break;
            }
            uint16_t target = RW(0x70);
            int level = 0;
            if (target) {
                uint16_t gap = RW(0x24) - read_word(ram, target + 0x24);
                if (gap & 0x8000) gap = (uint16_t)-gap;
                level = gap < 4;
            }
            if (level || !decrement(ram, actor + 0x42)) {
                WW(0x3a, 0);
                WW(0x38, 0xfb00);
            }
        }
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(bank7, (uint16_t)((RW(0x40) & 0xe) + RW(0x4c))));
            WW(0x44, 4);
            WW(0x40, RW(0x40) + 2);
        }
        ram[actor + 0x19] &= ram[actor + 0x72];
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 0;
        if (!world_player_contact(ram, main_rom, actor)) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
        }
        if (RW(0x1e) & 2) break;
        loud_radial_blast(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0x7b8ab:
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 0x34;
        WW(0, 0xb8bc);
    /* fall through */
    case 0x7b8bc:
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        WW(0x21, read_word(ram, parent + 0x21));
        WW(0x24, read_word(ram, parent + 0x24));
        WW(0x2a, T7(576d, (RW(0x40) & 2) + ((read_word(ram, parent + 0x40) & 0xe) << 1)));
        WW(0x40, RW(0x40) + 1);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7b912:
        world_acquire_resource(ram, actor, 0xf8);
        world_acquire_resource(ram, actor, 0x3c);
        world_acquire_resource(ram, actor, 0xfa);
        world_acquire_resource(ram, actor, 0x48);
        ram[actor + 0x2c] = 0x23;
        ram[actor + 0x2d] = 0x32;
        WW(0x2a, 0x32dd);
        WW(0x3c, 0xf983);
        WW(0x5e, 1);
        WW(0x26, boss_row(ram, bank7, 0));
        WW(0x36, boss_row(ram, bank7, 0));
        WW(0x52, boss_row(ram, bank7, 0x10));
        for (unsigned n = 8; n; --n) {
            uint16_t child = spawn7(ram, actor, 0xbb94);
            write_word(ram, child + 0x14, RW(0x14));
            ram[child + 0x2c] = ram[actor + 0x15];
            write_word(ram, child + 0x16, RW(0x16));
            ram[child + 0x2d] = ram[actor + 0x2d];
            write_word(ram, child + 0x60, n - 1);
            write_word(ram, child + 0x1e, 2);
        }
        WW(0, 0xb99e);
        break;
    case 0x7b99e:
        if (read_word(ram, parent + 0x48) >> (RW(0x1e) & 15) & 1) {
            uint16_t child = spawn7(ram, actor, 0xbae9);
            write_word(ram, child + 0x1e, RW(0x1e));
            write_word(ram, child + 0x14, RW(0x14));
            ram[child + 0x2c] = ram[actor + 0x14];
            write_word(ram, child + 0x16, RW(0x16));
            WW(0x5e, 0);
            WW(0x50, 0x20);
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 0x0a);
            world_queue_sound(ram, 0x5f);
            WW(0, 0xba24);
        }
        boss_follow(ram, actor, parent);
        boss_section(ram, main_rom, actor);
    section_b_timer:
        if (!RW(0x50)) WW(0x50, RW(0x52));
        if ((int16_t)RW(0x21) > 0x80 && (int16_t)RW(0x21) < 0x1a0) WW(0x50, RW(0x50) - 1);
        break;
    case 0x7bae9:
        ram[actor + 0x2d] = 0x33;
        WW(0x2a, 0x33b5);
        WW(0x3c, 0xf993);
        WW(0x38, 0xff00);
        WW(0x42, 0x38);
        WW(0x36, 0x100);
        WW(0x52, boss_row(ram, bank7, RW(0x1e) == 1 ? 0x12 : 0x14));
        WW(0x50, 0x40);
        for (unsigned n = 4; n; --n) {
            uint16_t child = spawn7(ram, actor, 0xbb94);
            write_word(ram, child + 0x14, RW(0x14));
            ram[child + 0x2c] = ram[actor + 0x15];
            write_word(ram, child + 0x16, RW(0x16));
            ram[child + 0x2d] = ram[actor + 0x2d];
            write_word(ram, child + 0x60, n - 1);
        }
        WW(0, 0xbb61);
    /* fall through */
    case 0x7bb61:
        if (RW(0x42)) {
            WW(0x42, RW(0x42) - 1);
            move_actor_axis(ram, actor + 0x30, RW(0x38));
        }
        boss_follow(ram, actor, parent);
        if (!RW(0x50)) WW(0x50, RW(0x52));
        WW(0x50, RW(0x50) - 1);
        boss_hatch_tail(ram, main_rom, actor);
        break;
    case 0x7bb94: {
        uint16_t kind = RW(0x1e), slot = RW(0x60);
        WW(0x2a, T7(58d5, kind));
        WW(0x31, read_word(bank7, (uint16_t)(T7(58f1, kind) + (slot & 6))));
        WW(0x34, read_word(bank7, (uint16_t)(T7(58e1, kind) + ((slot & 1) << 1))));
        ram[actor + 0x29] = (uint8_t)((slot & 1) << 1);
        WW(0x56, boss_row(ram, bank7, 0x16));
        WW(0x5a, boss_row(ram, bank7, kind ? 0x1a : 0x18));
        WW(0, 0xbbfd);
    }
    /* fall through */
    case 0x7bbfd:
    case 0x7bc3c:
    case 0x7bc89:
    case 0x7bce5:
    case 0x7bd6c:
    case 0x7bdbd: {
        uint16_t kind = RW(0x1e);
        if (address != 0x7bdbd && (ram[parent + 0x0a] & 2)) {
            WW(0, 0xbdbd);
            address = 0x7bdbd;
        }
        if (address == 0x7bdbd) {
            if (ram[parent + 0x0a] & 1) {
                finish_animation(ram, actor);
                break;
            }
            boss_follow(ram, actor, parent);
        } else if (address == 0x7bbfd || address == 0x7bb94) {
            boss_follow(ram, actor, parent);
            if (!read_word(ram, parent + 0x50)) {
                WW(0x42, ((RW(0x60) & 6) << 3) + 1);
                WW(0, 0xbc3c);
            }
        } else if (address == 0x7bc3c) {
            boss_follow(ram, actor, parent);
            if (!decrement(ram, actor + 0x42)) {
                WW(0x40, 0);
                WW(0x44, 1);
                WW(0x4c, T7(58d1, kind));
                WW(0x4e, T7(58bb, kind));
                WW(0, 0xbc89);
            }
        } else if (address == 0x7bce5) {
            WW(0x5e, read_word(ram, parent + 0x5e));
            boss_follow(ram, actor, parent);
            if (!decrement(ram, actor + 0x54)) {
                WW(0x54, RW(0x56));
                if (!decrement(ram, actor + 0x58)) {
                    WW(0x40, 0);
                    WW(0x44, 1);
                    WW(0x4c, T7(58a9, kind));
                    WW(0x4e, T7(588f, kind));
                    WW(0, 0xbd6c);
                }
                uint16_t child = spawn7(ram, actor, 0xc018);
                write_word(ram, child + 0x21, RW(0x21));
                write_word(ram, child + 0x24, RW(0x24));
                ram[child + 0x29] = ram[actor + 0x29];
                write_word(ram, child + 0x5e, RW(0x5e));
                ram[child + 0x2c] = ram[(uint16_t)(actor + 0x16 + RW(0x5e))];
            }
        } else {
            boss_follow(ram, actor, parent);
            if (!decrement(ram, actor + 0x44)) {
                uint16_t step = RW(0x40);
                uint16_t frame = read_word(bank7, (uint16_t)(RW(0x4c) + step));
                if (frame == 0xffff) {
                    if (address == 0x7bc89) {
                        WW(0x54, 1);
                        WW(0x58, RW(0x5a));
                        WW(0, 0xbce5);
                    } else WW(0, 0xbbfd);
                } else {
                    WW(0x2a, frame);
                    WW(0x44, read_word(bank7, (uint16_t)(RW(0x4e) + step)));
                    WW(0x40, step + 2);
                }
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x7bdef:
        ram[actor + 0x2c] = 0x23;
        ram[actor + 0x2d] = 0x31;
        WW(0, 0xbdfd);
        break;
    case 0x7bdfd:
        if (ram[parent + 0x0a] & 1) {
            finish_animation(ram, actor);
            break;
        }
        boss_follow(ram, actor, parent);
        WW(0x2a, T7(596b, RW(0x40)));
        WW(0x40, RW(0x40) + 2 >= 0x0c ? 0 : RW(0x40) + 2);
        if ((int16_t)RW(0x21) < 0x1c0) queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7be50: {
        ram[actor + 0x2d] = 4;
        WW(0x3c, 0xf9ab);
        WW(0x26, 0x80);
        WW(0x36, 0x80);
        uint16_t slot = (uint16_t)(RW(0x60) << 1);
        WW(0x21, read_word(ram, parent + 0x21) + 0x38);
        WW(0x24, T7(595f, slot) + read_word(ram, parent + 0x24));
        WW(0x42, T7(593f, slot));
        WW(0x38, T7(5957, slot));
        WW(0x3a, T7(594f, slot + RW(0x1e)));
        WW(0, 0xbeaa);
    }
    /* fall through */
    case 0x7beaa:
        if (!decrement(ram, actor + 0x42)) {
            if (RW(0x1e)) {
                WW(0x42, 0x20);
                WW(0x38, 0x280);
            } else WW(0x38, RW(0x2e));
            WW(0x50, 1);
            WW(0, 0xbee0);
        }
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        boss_missile_fly(ram, main_rom, bank7, actor);
        break;
    case 0x7bee0:
        if (RW(0x42)) {
            if (!decrement(ram, actor + 0x42)) {
                WW(0x40, 0);
                WW(0x44, 1);
                WW(0, 0xbf9b);
                goto missile_turn;
            }
            move_actor_axis(ram, actor + 0x20, RW(0x38));
        } else {
            move_actor_axis(ram, actor + 0x20, RW(0x38));
            if (RW(0x21) > 0x1c0) {
                finish_animation(ram, actor);
                break;
            }
        }
        boss_missile_fly(ram, main_rom, bank7, actor);
        break;
    case 0x7bf9b:
    missile_turn:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40), frame = T7(5933, step);
            if (frame == 0xffff) {
                WW(0x40, 0);
                WW(0, 0xbfd2);
            } else {
                WW(0x2a, frame);
                WW(0x44, 4);
                WW(0x40, step + 2);
            }
        }
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        queue_sprite(ram, main_rom, actor, 0);
        boss_missile_tail(ram, main_rom, actor);
        break;
    case 0x7bfd2:
        if ((int16_t)RW(0x38) > (int16_t)RW(0x2e)) WW(0x38, RW(0x38) - 0x14);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        if (RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        WW(0x2a, T7(592d, RW(0x40) & 2));
        WW(0x40, RW(0x40) + 2);
        queue_sprite(ram, main_rom, actor, 0);
        boss_missile_tail(ram, main_rom, actor);
        break;
    case 0x7c018:
        ram[actor + 0x2d] = 4;
        WW(0x3c, 0xf53b);
        WW(0x26, 0x40);
        WW(0x36, 0x440);
        WW(0x44, 1);
        WW(0x3a, (ram[actor + 0x29] & 2) ? 0xff40 : 0xc0);
        WW(0x42, (world_random_byte(ram) & 0x1f) + 0x0a);
        world_queue_sound(ram, 0x7b);
        WW(0, 0xc05b);
    /* fall through */
    case 0x7c05b:
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (!decrement(ram, actor + 0x42)) WW(0, 0xc0c2);
        boss_shell_tail(ram, main_rom, bank7, actor);
        break;
    case 0x7c0c2:
        if ((int16_t)RW(0x38) > (int16_t)0xfe00) WW(0x38, RW(0x38) - 0x10);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        if (RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        boss_shell_tail(ram, main_rom, bank7, actor);
        break;
    case 0x7c0ef: {
        ram[actor + 0x2d] = 4;
        WW(0x3c, 0xf53b);
        WW(0x26, 0x80);
        WW(0x36, 0x80);
        WW(0x2e, 0x500);
        uint16_t slot = RW(0x1e);
        ram[actor + 0x28] = bank7[(uint16_t)(0xabcd + slot)];
        WW(0x31, T7(5725, 2 * slot));
        WW(0x34, T7(570f, 2 * slot));
        WW(0x42, T7(56f9, 2 * slot));
        WW(0x50, T7(56e3, 2 * slot));
        unsigned angle = 2 * ram[actor + 0x28];
        WW(0x38, ((uint32_t)read_word(main_rom + 0xa0000, 0x9e12 + angle) * 0x500) >> 8);
        WW(0x3a, ((uint32_t)read_word(main_rom + 0xa0000, 0x9d92 + angle) * 0x500) >> 8);
        WW(0, 0xc13f);
        break;
    }
    case 0x7c13f:
        if (!decrement(ram, actor + 0x42)) {
            WW(0x44, 1);
            WW(0, 0xc14f);
        }
        break;
    case 0x7c14f:
        if (ram[0x262] & 0x80) {
            finish_animation(ram, actor);
            break;
        }
        boss_follow(ram, actor, parent);
        if (!decrement(ram, actor + 0x50)) WW(0, 0xc195);
        WW(0x2a, T7(573b, RW(0x40)));
        WW(0x40, RW(0x40) + 2 >= 0x12 ? 0 : RW(0x40) + 2);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7c195:
        world_queue_sound(ram, 0x59);
        WW(0x40, 0);
        WW(0x44, 1);
        WW(0, 0xc1ac);
    /* fall through */
    case 0x7c1ac: {
        if (ram[0x262] & 0x80) {
            finish_animation(ram, actor);
            break;
        }
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, T7(5729, RW(0x40) & 2));
            WW(0x44, 2);
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        unsigned material = terrain_material(ram, tiles, RW(0x21), RW(0x24));
        if (((1u << (material & 15)) & 0xff02) || material == 3) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    return WORLD_HANDLER_DONE;
}

/* D68F/D6B7 with motion kind 10 (a horizontal run with a sine in Y), which the
 * shared script helpers do not cover. Other kinds go through the shared ones. */
static void script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    if (read_word(bank, offset) != 10) {
        begin_linear_script(ram, rom, actor, offset);
        return;
    }
    uint16_t speed = read_word(bank, offset + 8);
    write_word(ram, actor + 0x7a, 10);
    write_word(ram, actor + 0x78, read_word(bank, offset + 4));
    write_word(ram, actor + 0x76, read_word(bank, offset + 6));
    write_word(ram, actor + 0x38, speed);
    ram[actor + 0x28] = rom[0xda0f + (speed >> 15)];
    write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
    write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
    write_word(ram, actor + 0x7e, offset + 10);
    ram[actor + 0x20] = ram[actor + 0x23] = 0;
    write_word(ram, actor + 0x7c, read_word(bank, offset + 2));
}

/* 1 stepped, -1 script ended, 0 not recovered. */
static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t next_kind = read_word(bank, read_word(ram, actor + 0x7e));
    if (read_word(ram, actor + 0x7a) != 10) {
        if (read_word(ram, actor + 0x7c) != 1 || next_kind != 10) return step_flight_script(ram, rom, actor);
        write_word(ram, actor + 0x7c, 2);
        if (!step_flight_script(ram, rom, actor)) return 0;
        script_begin(ram, rom, actor, read_word(ram, actor + 0x7e));
        return 1;
    }
    if (read_word(ram, actor + 0x7c) == 1 && next_kind != 0xffff && next_kind != 10 &&
        ((next_kind & 1) || next_kind > 6)) return 0;
    scroll_actor(ram, actor, 0xb71a);
    scroll_actor(ram, actor + 0x10, 0xb71a);
    move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
    uint32_t phase = (uint32_t)(uint16_t)(read_word(ram, actor + 0x21) - read_word(ram, actor + 0x31)) *
                     read_word(ram, actor + 0x76);
    int16_t unit = (int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * ((phase >> 8) & 255));
    uint32_t centre = ram[actor + 0x33] | (uint32_t)ram[actor + 0x34] << 8 | (uint32_t)ram[actor + 0x35] << 16;
    uint32_t y = centre + (uint32_t)((int32_t)unit * (int16_t)read_word(ram, actor + 0x78));
    ram[actor + 0x23] = (uint8_t)y;
    ram[actor + 0x24] = (uint8_t)(y >> 8);
    ram[actor + 0x25] = (uint8_t)(y >> 16);
    if (!decrement(ram, actor + 0x7c)) {
        if (next_kind == 0xffff) return -1;
        script_begin(ram, rom, actor, read_word(ram, actor + 0x7e));
    }
    return 1;
}

int world_stage6_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank7 = main_rom + 0x70000, *bankb = main_rom + 0xb0000,
                  *banka = main_rom + 0xa0000;
    (void)pass;
    if (address >= 0x7abdc && address < 0x7c312) {
        int result = final_boss(ram, main_rom, tiles, actor, address);
        if (result != WORLD_HANDLER_DONE) return result;
        *actor_io = actor;
        return result;
    }
    switch (address) {
    case 0x7c312: {
        unsigned index = read_word(ram, actor + 0x1e) & 0xe;
        ram[actor + 0x2c] = 0x23;
        write_word(ram, actor + 0x2a, read_word(bank7, 0xa67c + index));
        write_word(ram, actor + 0x46, read_word(bank7, 0xa686 + index));
        ram[actor + 0x2d] = bank7[0xa690 + index / 2];
        write_word(ram, actor, 0xc33d);
        break;
    }
    case 0x7c33d:
        scroll_actor(ram, actor, 0xb71a);
        if ((int16_t)read_word(ram, actor + 0x21) < (int16_t)read_word(ram, actor + 0x46))
            finish_animation(ram, actor);
        else queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7c3aa: {
        unsigned index = read_word(ram, actor + 0x1e) & 1;
        ram[actor + 0x2c] = bank7[0xc3a8 + index];
        write_word(ram, actor + 0x4a, index << 2);
        write_word(ram, actor + 0x42, 0x80);
        write_word(ram, actor, 0xc3c9);
    }
    /* fall through */
    case 0x7c3c9: {
        uint16_t limit = read_word(ram, actor + 0x1e) & 0xfff0;
        if ((ram[0x263] & 0x80) || (limit && (int16_t)read_word(ram, 0xb700) >= (int16_t)limit)) {
            finish_animation(ram, actor);
            break;
        }
        if (decrement(ram, actor + 0x42)) break;
        uint16_t index = (read_word(ram, actor + 0x40) & 2) + read_word(ram, actor + 0x4a);
        queue_palette(ram, ram[actor + 0x2c] & 0x3f, read_word(bank7, (uint16_t)(0xc3a0 + index)), 6);
        write_word(ram, actor + 0x42, 0x41);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        break;
    }
    case 0x7c41c:
        write_word(ram, actor + 0x42, 1);
        write_word(ram, 0xb8ca, 0);
        write_word(ram, actor, 0xc42c);
    /* fall through */
    case 0x7c42c:
        if (!decrement(ram, actor + 0x42)) {
            draw_gate_strip(ram, tiles, main_rom, actor);
            write_word(ram, actor + 0x40, (read_word(ram, actor + 0x40) + 0x20) & 0xe0);
            write_word(ram, actor + 0x42, 8);
        } else if (read_word(ram, actor + 0x4c) == read_word(ram, 0xb8ca)) {
            draw_gate_strip(ram, tiles, main_rom, actor);
        }
        write_word(ram, actor + 0x4c, read_word(ram, 0xb8ca));
        break;
    case 0x7c458:
        world_acquire_resource(ram, actor, 0x3a);
        world_acquire_resource(ram, actor, 0xf5);
        world_acquire_resource(ram, actor, 0x39);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x2a, 0x2fed);
        write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor + 0x42, 4);
        do {
            uint16_t child = spawn7(ram, actor, 0xc75e);
            uint16_t index = read_word(ram, actor + 0x42) - 1;
            write_word(ram, child + 0x52, index);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21) +
                       read_word(bank7, (uint16_t)(0xc36c + 2 * index)));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            ram[child + 0x2c] = ram[actor + 0x2c];
        } while (decrement(ram, actor + 0x42));
        write_word(ram, actor, 0xc4b9);
        break;
    case 0x7c4b9:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (read_word(ram, 0xb700) >= 0x570) {
            ram[0x26f] |= 0x80;
            write_word(ram, actor + 0x3a, 0xffc0);
            write_word(ram, actor + 0x42, 0x40);
            write_word(ram, actor, 0xc4e1);
        }
        break;
    case 0x7c4e1:
        if (read_word(ram, actor + 0x42)) {
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            if (!decrement(ram, actor + 0x42)) {
                uint16_t child = spawn7(ram, actor, 0xc5c7);
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + 0x40);
                write_word(ram, child + 0x14, read_word(ram, actor + 0x14));
                ram[child + 0x2c] = ram[actor + 0x14];
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (ram[actor + 0x50] & 1) {
            write_word(ram, actor + 0x42, 0x40);
            write_word(ram, actor, 0xc52c);
        }
        break;
    case 0x7c52c:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            ram[actor + 0x50] |= 2;
            write_word(ram, actor + 0x42, 0x80);
            write_word(ram, actor, 0xc546);
        }
        break;
    case 0x7c546:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor + 0x40, 8);
            write_word(ram, actor, 0xc560);
        }
        break;
    case 0x7c560: {
        queue_sprite(ram, main_rom, actor, 0);
        if (decrement(ram, actor + 0x42)) break;
        uint16_t rise = read_word(ram, 0xb8ca);
        for (unsigned band = 0; band < 3; ++band) {
            uint16_t pos = (uint16_t)(0x1ae8 + 0x300 * band - rise);
            for (unsigned n = 0; n < 0x24; ++n) {
                uint16_t cell = (uint16_t)(pos + 2);
                write_word(tiles + 0x4000, cell, (read_word(tiles + 0x4000, cell) & 0x7ff) | (0x1800 + 0x800 * band));
                pos = (pos & 0xff00) | ((pos + 4) & 255);
            }
        }
        uint16_t pos = (uint16_t)(0x1d00 - rise);
        for (unsigned n = 0; n < 0x40; ++n) {
            uint16_t cell = (uint16_t)(pos + 2);
            write_word(tiles + 0x8000, cell, read_word(tiles + 0x8000, cell) - 1);
            pos = (pos & 0xff00) | ((pos + 4) & 255);
        }
        write_word(ram, 0xb8ca, rise + 0x100);
        write_word(ram, actor + 0x42, 0x40);
        if (!decrement(ram, actor + 0x40)) {
            ram[0x26f] &= 0x7f;
            write_word(ram, actor, 0xc5bc);
        }
        break;
    }
    case 0x7c5bc:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7c5c7: {
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x2a, 0x832f);
        write_word(ram, actor + 0x3a, 0xff80);
        write_word(ram, actor + 0x42, 0xd0);
        uint16_t child = spawn7(ram, actor, 0xc62d);
        write_word(ram, child + 0x14, read_word(ram, actor + 0x14));
        ram[child + 0x2c] = ram[actor + 0x14];
        write_word(ram, actor, 0xc5f3);
    }
    /* fall through */
    case 0x7c5f3:
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0xc608);
        break;
    case 0x7c608:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) finish_animation(ram, actor);
        else queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7c62d:
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x2a, 0x82c7);
        write_word(ram, actor + 0x3c, 0xfaa3);
        write_word(ram, actor + 0x26, 0x1000);
        write_word(ram, actor + 0x36, 0x100);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xc64f);
    /* fall through */
    case 0x7c64f: {
        uint16_t parent = ancestor(ram, actor, 1);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) - 0x68);
        if (!read_word(ram, parent + 0x42)) write_word(ram, actor, 0xc690);
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 1;
        world_surface_contact(ram, main_rom, actor);
        break;
    }
    case 0x7c690: {
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x2a, read_word(bank7, 0xc364 + (read_word(ram, actor + 0x40) & 6)));
            write_word(ram, actor + 0x44, 6);
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        if (ram[actor + 0x19]) {
            --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x14 + (ram[actor + 0x19] & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) break;
        surface_enemy_death_blast(ram, main_rom, actor);
        ram[ancestor(ram, actor, 2) + 0x50] |= 1;
        write_word(ram, ancestor(ram, actor, 1) + 0x2a, 0x8341);
        write_word(ram, actor + 0x2a, 0x835b);
        write_word(ram, actor + 0x3a, 0xffc0);
        ram[actor + 0x2c] = ram[actor + 0x14];
        write_word(ram, actor, 0xc719);
        break;
    }
    case 0x7c719: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        int gone = read_word(ram, actor + 0x21) < 0x40;
        if (!gone) {
            uint16_t top = (ram[0x26e] & 2) ? 0x68 : (uint16_t)(0x10 + read_word(ram, 0xb732));
            gone = (int16_t)read_word(ram, actor + 0x24) < (int16_t)top;
        }
        if (gone) finish_animation(ram, actor);
        else queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x7c75e:
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x2a, 0x300f);
        write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor, 0xc772);
        break;
    case 0x7c772:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (ram[ancestor(ram, actor, 1) + 0x50] & 2) {
            uint16_t child = spawn7(ram, actor, 0xc7d7);
            write_word(ram, child + 0x52, read_word(ram, actor + 0x52));
            write_word(ram, actor, 0xc7a8);
        }
        break;
    case 0x7c7a8:
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + (read_word(ram, 0x274) & 2) - 1);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7c7d7:
        world_acquire_resource(ram, actor, 0xf2);
        world_acquire_resource(ram, actor, 0x3b);
        ram[actor + 0x2d] = 0x31;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x34, 0xffe8);
        if (read_word(ram, actor + 0x52) == 3) write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0xc805);
    /* fall through */
    case 0x7c805:
        follow_parent(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(bank7, (uint16_t)(0xc374 + read_word(ram, actor + 0x40)));
            if (frame != 0xffff) {
                write_word(ram, actor + 0x2a, frame);
                write_word(ram, actor + 0x44, 5);
                write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
            } else {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor + 0x46, 5);
                write_word(ram, actor + 0x4e, 6);
                write_word(ram, actor + 0x48, 0xc380);
                uint16_t child = spawn7(ram, actor, 0xc8d7);
                ram[child + 0x2c] = ram[actor + 0x2c];
                ram[child + 0x2d] = 0x31;
                write_word(ram, child + 0x44, 1);
                write_word(ram, child + 0x46, 5);
                write_word(ram, child + 0x4e, 6);
                write_word(ram, child + 0x48, 0xc388);
                write_word(ram, child + 0x34, 0xffb0);
                write_word(ram, child + 0x1e, 1);
                child = spawn7(ram, actor, 0xc8d7);
                ram[child + 0x2c] = ram[actor + 0x14];
                ram[child + 0x2d] = 0x31;
                write_word(ram, child + 0x44, 1);
                write_word(ram, child + 0x46, 6);
                write_word(ram, child + 0x4e, 0xe);
                write_word(ram, child + 0x48, 0xc390);
                write_word(ram, child + 0x3c, 0xfaab);
                write_word(ram, child + 0x34, 0xffb0);
                write_word(ram, child + 0x1e, 1);
                write_word(ram, actor, 0xc8d7);
            }
        }
        gate_part_tail(ram, main_rom, actor);
        break;
    case 0x7c8d7: {
        if (!read_word(ram, actor + 0x1e)) {
            uint16_t rise = (uint16_t)(read_word(ram, 0xb8ca) << 3);
            write_word(ram, actor + 0x34, (uint16_t)(((rise >> 8) | (rise << 8)) - 0x18));
        }
        uint16_t parent = follow_parent(ram, actor);
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t index = read_word(ram, actor + 0x40) & read_word(ram, actor + 0x4e);
            write_word(ram, actor + 0x2a, read_word(bank7, (uint16_t)(read_word(ram, actor + 0x48) + index)));
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        if (read_word(ram, actor + 0x3c)) {
            uint16_t player = read_word(ram, (ram[0x274] & 1) ? 0x92 : 0x90);
            uint16_t shape = read_word(ram, player + 0x3c);
            if (shape != 0xffff && shape != 0x79f1 &&
                world_shape_overlap(ram, main_rom, actor, player, read_word(ram, actor + 0x3c), shape))
                write_word(ram, player + 0x33, 0xfe40);
        }
        gate_part_tail(ram, main_rom, actor);
        break;
    }
    case 0xb7c2e: {
        world_acquire_resource(ram, actor, 0x15);
        ram[actor + 0x2d] = 0x21;
        uint16_t index = (uint16_t)(((ram[actor + 0x29] & 1) << 2) + read_word(ram, actor + 0x42));
        for (unsigned n = 0; n < 3; ++n, ++index) {
            uint16_t child = bank_b_effect(ram, actor, 0x7d77);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x2e, read_word(ram, actor + 0x2e));
            ram[child + 0x28] = bankb[(uint16_t)(0x78c4 + index)];
        }
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x7c80);
    }
    /* fall through */
    case 0xb7c80: {
        uint16_t parent = ancestor(ram, actor, 1);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            uint16_t frame = read_word(bankb, (uint16_t)(0x78a6 + step + 2 * read_word(ram, actor + 0x42)));
            if (frame == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, frame);
            write_word(ram, actor + 0x44, read_word(bankb, (uint16_t)(0x78b8 + step)));
            write_word(ram, actor + 0x40, step + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb7d77: {
        world_acquire_resource(ram, actor, 0x12);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x3c, 0xf4e3);
        /* 657D: velocity from the angle byte and the speed word. */
        unsigned angle = 2 * ram[actor + 0x28], speed = read_word(ram, actor + 0x2e);
        write_word(ram, actor + 0x38, (uint16_t)(((uint32_t)read_word(main_rom + 0xa0000, 0x9e12 + angle) * speed) >> 8));
        write_word(ram, actor + 0x3a, (uint16_t)(((uint32_t)read_word(main_rom + 0xa0000, 0x9d92 + angle) * speed) >> 8));
        write_word(ram, actor, 0x7d9c);
    }
    /* fall through */
    case 0xb7d9c: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x > 0x1c0 || x < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t phase = read_word(ram, actor + 0x40) + 0x80;
        if (phase >= 0x600) phase = 0;
        write_word(ram, actor + 0x40, phase);
        write_word(ram, actor + 0x2a, read_word(bankb, 0x78d8 + 2 * (phase >> 8)));
        queue_sprite(ram, main_rom, actor, 1);
        unsigned material = terrain_material(ram, tiles, x, read_word(ram, actor + 0x24));
        if ((1u << (material & 15)) & 0xff02) {
            finish_animation(ram, actor);
        } else if (material == 3) {
            /* 6BCC: small blast. */
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, 0x7507);
            write_word(ram, blast + 0x4a, 0x76e2);
            write_word(ram, blast + 0x44, 0x0b);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            finish_animation(ram, actor);
        } else {
            ram[actor + 0x0d] = 2;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        }
        break;
    }
    case 0x7dd5f:
    case 0x7ddcb:
    case 0x7dddd: {
        /* Spawner entries: three immediate stores, then the shared setup at 7DBAA. */
        const uint8_t *entry = main_rom + address;
        uint16_t kind = read_word(entry, 13);
        write_word(ram, actor + 0x60, read_word(entry, 3));
        write_word(ram, actor + 0x62, read_word(entry, 8));
        write_word(ram, actor + 0x0e, kind);
        unsigned flags = ram[actor + 0x1f];
        write_word(ram, actor + 0x44, (flags & 0x20) | read_word(bank7, (uint16_t)(0xdb8a + kind)));
        write_word(ram, actor + 0x42, read_word(bank7, 0xdb42 + ((flags << 1) & 6)));
        uint16_t rate = read_word(bank7, 0xdb7a + ((flags >> 1) & 0xe));
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x52, read_word(bank7, (uint16_t)(0xdb4a + rate)));
        write_word(ram, actor + 0x54, read_word(bank7, (uint16_t)(0xdb4c + rate)));
        write_word(ram, actor + 0x56, read_word(bank7, (uint16_t)(0xdb4c + rate)));
        write_word(ram, actor + 0x58, read_word(bank7, (uint16_t)(0xdb4e + rate)));
        write_word(ram, actor + 0x5a, read_word(bank7, (uint16_t)(0xdb4e + rate)));
        write_word(ram, actor + 0x48, (uint16_t)((ram[actor + 0x1e] << 4) + read_word(ram, 0xb700)));
        write_word(ram, actor, 0xdc27);
        break;
    }
    /* ---- Gunboat with two rocket racks (B1292..B18C2) ---- */
    case 0xb1292: {
        world_acquire_resource(ram, actor, 0x4f);
        world_acquire_resource(ram, actor, 0xa3);
        world_acquire_resource(ram, actor, 0xa2);
        WW(0x0e, 4);
        ram[actor + 0x2d] = 0x28;
        WW(0x2a, 0x4c5);
        WW(0x3c, 0xf2eb);
        ram[actor + 0x0c] = 8;
        uint16_t row = read_word(bankb, (uint16_t)(0x11f2 + (RW(0x1e) & 0x20) + read_word(ram, 0x88)));
        WW(0x26, read_word(bankb, (uint16_t)(row + 0x1232)));
        WW(0x36, RW(0x26));
        WW(0x50, read_word(bankb, (uint16_t)(row + 0x1234)));
        WW(0x52, RW(0x50));
        WW(0x54, read_word(bankb, (uint16_t)(row + 0x1236)));
        WW(0x58, read_word(bankb, (uint16_t)(row + 0x1238)));
        WW(0x5c, read_word(bankb, (uint16_t)(row + 0x123a)));
        WW(0x5e, read_word(bankb, (uint16_t)(row + 0x123c)));
        WW(0x46, 0x4b0);
        WW(0x48, 0x5a);
        WW(0x34, RW(0x24));
        WW(0x4c, RW(0x24));
        WW(0x4e, read_word(bankb, 0x11c2 + (RW(0x1e) & 2)));
        for (unsigned n = 0; n < 2; ++n) {
            uint16_t child = bank_b_effect(ram, actor, n ? 0x150a : 0x14ef);
            write_word(ram, child + 0x14, RW(0x14));
            ram[child + 0x2c] = ram[actor + 0x2c];
            write_word(ram, child + 0x54, RW(0x54));
            write_word(ram, child + 0x56, RW(0x54));
            write_word(ram, child + 0x58, RW(0x58));
            write_word(ram, child + 0x5a, RW(0x58));
            write_word(ram, child + 0x5c, RW(0x5c));
            write_word(ram, child + 0x5e, RW(0x5e));
        }
        WW(0, 0x1399);
        break;
    }
    case 0xb1399:
    case 0xb14bd: {
        if (address == 0xb1399) {
            WW(0x64, 0);
            scroll_actor(ram, actor, 0xb71a);
            WW(0x34, RW(0x34) + read_word(ram, 0xb71c));
            WW(0x4c, RW(0x4c) + read_word(ram, 0xb71c));
            uint16_t reach = RW(0x21) < 0x120 ? 0 : RW(0x21) - 0x120;
            if (reach > 0x60) reach = 0x60;
            WW(0x38, read_word(bankb, 0x10ba + ((reach & 0x70) >> 3)));
            move_actor_axis(ram, actor + 0x20, RW(0x38));
            /* The original tests the wait counter against the leftover velocity sign word. */
            if (RW(0x48) & (uint16_t)(int16_t)(int8_t)(RW(0x38) >> 8)) WW(0x48, RW(0x48) - 1);
            else {
                unsigned players = read_word(ram, 0x264) & 3;
                if (players == 3 && !world_enemy_target_mode_supported(ram[actor + 0x0c])) return WORLD_HANDLER_STOP;
                if (players) {
                    uint16_t target = world_enemy_target(ram, actor);
                    int chase = 1;
                    if (read_word(ram, target + 0x24) >= RW(0x34)) {
                        if ((int16_t)(RW(0x34) - RW(0x4c)) > (int16_t)RW(0x4e)) chase = 0;
                        else WW(0x3a, 0x80);
                    } else {
                        if ((int16_t)(RW(0x4c) - RW(0x34)) > (int16_t)RW(0x4e)) chase = 0;
                        else WW(0x3a, 0xff80);
                    }
                    if (chase) move_actor_axis(ram, actor + 0x33, RW(0x3a));
                }
                int16_t unit = (int16_t)read_word(main_rom + 0xa0000, 0x9d92 + 2 * (((uint32_t)RW(0x4a) * 0xc0 >> 8) & 255));
                uint32_t centre = ram[actor + 0x33] | (uint32_t)ram[actor + 0x34] << 8 | (uint32_t)ram[actor + 0x35] << 16;
                uint32_t y = centre + (uint32_t)((int32_t)unit * 0x18);
                ram[actor + 0x23] = (uint8_t)y;
                ram[actor + 0x24] = (uint8_t)(y >> 8);
                ram[actor + 0x25] = (uint8_t)(y >> 16);
                WW(0x4a, RW(0x4a) + 1);
            }
            if (!decrement(ram, actor + 0x50)) {
                WW(0x50, RW(0x52));
                WW(0x64, 1);
            }
            if (!decrement(ram, actor + 0x46)) WW(0, 0x14bd);
        } else {
            scroll_actor(ram, actor, 0xb71a);
            if ((int16_t)RW(0x38) > (int16_t)0xfe00) WW(0x38, RW(0x38) - 0x10);
            move_actor_axis(ram, actor + 0x20, RW(0x38));
            if (RW(0x21) < 0x20) {
                finish_animation(ram, actor);
                break;
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (RW(0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 0x0a);
        world_queue_sound(ram, 0x5f);
        blast_chain(ram, actor, 0xb000, 0x11c6, 3);
        WW(0x42, 0x30);
        ram[actor + 0x0a] = 2;
        WW(0x64, 0);
        WW(0, 0x1499);
        break;
    }
    case 0xb1499:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    case 0xb14ef:
    case 0xb150a: {
        int lower = address == 0xb150a;
        ram[actor + 0x2d] = lower ? 0x28 : 0x25;
        WW(0x2a, lower ? 0x50f : 0x63b);
        WW(0x60, lower ? 0x10e2 : 0x10ca);
        WW(0x62, lower ? 0x1112 : 0x10fc);
        WW(0x1e, lower ? 2 : 0);
        WW(0, 0x1528);
        break;
    }
    case 0xb1528:
    case 0xb1579:
    case 0xb15e7:
    case 0xb169e: {
        uint16_t boat = ancestor(ram, actor, 1);
        if (ram[boat + 0x0a] & 1) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xb1528 && read_word(ram, boat + 0x64)) {
            WW(0x40, 0);
            WW(0x44, 1);
            WW(0, 0x1579);
        }
        WW(0x21, read_word(ram, boat + 0x21));
        WW(0x24, read_word(ram, boat + 0x24));
        if (address == 0xb1579 && !decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40);
            WW(0x2a, read_word(bankb, (uint16_t)(RW(0x60) + step)));
            uint16_t hold = read_word(bankb, (uint16_t)(RW(0x62) + step));
            if (!hold) {
                WW(0x54, 1);
                WW(0x58, RW(0x5a));
                WW(0, 0x15e7);
                hold = RW(0x5a); /* AW still holds the reload count */
            }
            WW(0x44, hold);
            WW(0x40, step + 2);
        } else if (address == 0xb169e && !decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40);
            uint16_t frame = read_word(bankb, (uint16_t)(RW(0x60) + step));
            if (frame == 0xffff) WW(0, 0x1528);
            else {
                WW(0x2a, frame);
                WW(0x44, read_word(bankb, (uint16_t)(RW(0x62) + step)));
                WW(0x40, step + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xb15e7 && !decrement(ram, actor + 0x54)) {
            WW(0x54, RW(0x56));
            for (unsigned n = 0; n < 2; ++n) {
                uint16_t rocket = bank_b_effect(ram, actor, 0x1779);
                ram[rocket + 0x2c] = ram[actor + 0x15];
                write_word(ram, rocket + 0x1e, RW(0x1e) + 4 * n);
                write_word(ram, rocket + 0x5c, RW(0x5c));
                write_word(ram, rocket + 0x5e, RW(0x5e));
            }
            uint16_t flash = bank_b_effect(ram, actor, 0x1704);
            ram[flash + 0x2c] = ram[actor + 0x14];
            world_queue_sound(ram, 0x8b);
            if (!decrement(ram, actor + 0x58)) {
                WW(0x44, 1);
                WW(0, 0x169e);
            }
        }
        break;
    }
    case 0xb1704:
        ram[actor + 0x2d] = 0x26;
        WW(0x44, 1);
        WW(0, 0x1712);
    /* fall through */
    case 0xb1712:
    case 0xb172f:
        if (address == 0xb172f) scroll_actor(ram, actor, 0xb71a);
        else {
            uint16_t rack = ancestor(ram, actor, 1);
            WW(0x21, read_word(ram, rack + 0x21));
            WW(0x24, read_word(ram, rack + 0x24));
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = RW(0x40), frame = read_word(bankb, (uint16_t)(0x114c + step));
            if (frame == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(0x2a, frame);
            WW(0x44, read_word(bankb, (uint16_t)(0x116e + step)));
            WW(0x40, step + 2);
            if (step == 0x12) WW(0, 0x172f);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xb1779: {
        uint16_t kind = RW(0x1e);
        ram[actor + 0x2d] = (uint8_t)((kind & 2) + 0x27);
        WW(0x3c, 0xf2f3);
        WW(0x26, 0x300);
        WW(0x36, 0x80);
        WW(0x31, 0xffe0);
        WW(0x34, read_word(bankb, (uint16_t)(0x112a + kind)));
        WW(0x38, read_word(bankb, (uint16_t)(0x1132 + kind)));
        WW(0x3a, read_word(bankb, (uint16_t)(0x113a + kind)));
        WW(0x42, 0x20);
        WW(0x44, 1);
        WW(0x66, 2);
        WW(0, 0x17c7);
    }
    /* fall through */
    case 0xb17c7:
    case 0xb1867:
        if (address != 0xb1867) {
            move_actor_axis(ram, actor + 0x30, RW(0x38));
            move_actor_axis(ram, actor + 0x33, RW(0x3a));
            uint16_t rack = ancestor(ram, actor, 1);
            WW(0x21, read_word(ram, rack + 0x21) + RW(0x31));
            WW(0x24, read_word(ram, rack + 0x24) + RW(0x34));
            if (!decrement(ram, actor + 0x42)) {
                ram[actor + 0x2d] = 4;
                WW(0x66, 1);
                WW(0x50, 4);
                WW(0, 0x1867);
            }
        } else {
            scroll_actor(ram, actor, 0xb71a);
            if ((int16_t)RW(0x38) > (int16_t)RW(0x5c)) WW(0x38, RW(0x38) - 0x20);
            move_actor_axis(ram, actor + 0x20, RW(0x38));
            if (RW(0x21) < 0x40) {
                finish_animation(ram, actor);
                break;
            }
            if (!decrement(ram, actor + 0x50)) {
                WW(0x50, RW(0x5e));
                uint16_t trail = world_spawn_child(ram, read_word(ram, 0x9a), 0xbe41);
                if (!trail) trail = 0x8480;
                write_word(ram, trail + 0x21, RW(0x21) + 0x10);
                write_word(ram, trail + 0x24, RW(0x24));
                ram[trail + 0x2c] = 3;
            }
        }
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(bankb, (uint16_t)(0x1142 + RW(0x40))));
            WW(0x44, RW(0x66));
            WW(0x40, RW(0x40) + 2 >= 0x0a ? 0 : RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        if (!world_player_contact(ram, main_rom, actor)) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
        }
        loud_radial_blast(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    /* ---- Torpedo carrier with a rider (BEEDC..BF251) ---- */
    case 0xbeedc: {
        world_acquire_resource(ram, actor, 0x4a);
        world_acquire_resource(ram, actor, 0x42);
        world_acquire_resource(ram, actor, 0x4d);
        world_acquire_resource(ram, actor, 0x4c);
        WW(0x0e, 0x21);
        ram[actor + 0x2d] = 0x20;
        WW(0x3c, 0xf6f3);
        WW(0x2a, 0xe1be);
        uint16_t row = read_word(bankb, (uint16_t)(0xee2c + (RW(0x1e) & 0x20) + read_word(ram, 0x88)));
        WW(0x26, read_word(bankb, (uint16_t)(row + 0xee6c)));
        WW(0x36, 0x30);
        WW(0x50, read_word(bankb, (uint16_t)(row + 0xee6e)));
        WW(0x52, RW(0x50));
        WW(0x54, read_word(bankb, (uint16_t)(row + 0xee70)));
        script_begin(ram, main_rom, actor, read_word(bankb, 0xeea2 + (RW(0x1e) & 0xe)));
        uint16_t rider = bank_b_effect(ram, actor, 0xf180);
        ram[rider + 0x2c] = ram[actor + 0x14];
        WW(0, 0xef68);
        break;
    }
    case 0xbef68: {
        int step = script_step(ram, main_rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0 || RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            ram[actor + 0x0a] |= 2;
            break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (RW(0x50)) {
            if (!decrement(ram, actor + 0x50)) {
                uint16_t tube = world_spawn_child(ram, actor, 0xf036);
                if (!tube) WW(0x50, RW(0x52));
                else {
                    write_word(ram, tube + 2, 0xb000);
                    write_word(ram, tube + 0x15, RW(0x15));
                    ram[tube + 0x2c] = ram[actor + 0x16];
                    write_word(ram, tube + 0x54, RW(0x54));
                    WW(0x40, 0);
                    WW(0x44, 1);
                }
            }
        } else if (!decrement(ram, actor + 0x44)) {
            uint16_t at = RW(0x40), frame = read_word(bankb, (uint16_t)(0xede0 + at));
            if (frame == 0xffff) WW(0x50, RW(0x52));
            else {
                WW(0x2a, frame);
                WW(0x44, read_word(bankb, (uint16_t)(0xedee + at)));
                WW(0x40, at + 2);
            }
        }
        if (!ram[actor + 0x0a]) {
            ram[actor + 0x0d] = 0;
            uint16_t hit = world_player_contact(ram, main_rom, actor);
            if (RW(0x26)) {
                ram[actor + 0x0d] = 0;
                hit = world_projectile_contact(ram, main_rom, actor);
                if (RW(0x26)) break;
            }
            world_award_score(ram, main_rom, actor, hit);
            /* 6B91 */
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, 0x74e5);
            write_word(ram, blast + 0x4a, 0x7675);
            ram[blast + 0x2c] = 0x0a;
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x40);
            write_word(ram, blast + 0x58, 0);
            world_queue_sound(ram, 0x3e);
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
        }
        finish_animation(ram, actor);
        break;
    }
    case 0xbf036:
        ram[actor + 0x2d] = 0x20;
        WW(0x44, 1);
        WW(0, 0xf045);
        break;
    case 0xbf045: {
        uint16_t carrier = ancestor(ram, actor, 1);
        if (ram[carrier + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        WW(0x21, read_word(ram, carrier + 0x21));
        WW(0x24, read_word(ram, carrier + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t at = RW(0x40), frame = read_word(bankb, (uint16_t)(0xedfa + at));
            if (frame == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(0x2a, frame);
            WW(0x44, read_word(bankb, (uint16_t)(0xee0e + at)));
            WW(0x40, at + 2);
            if (at == 0x0c) {
                uint16_t shot = bank_b_effect(ram, actor, 0xf0ea);
                write_word(ram, shot + 0x21, RW(0x21) - 0x30);
                write_word(ram, shot + 0x24, RW(0x24) - 8);
                ram[shot + 0x2c] = ram[actor + 0x15];
                write_word(ram, shot + 0x38, RW(0x54));
                world_queue_sound(ram, 0x77);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbf0ea:
    case 0xbf10a: {
        if (address == 0xbf0ea) {
            ram[actor + 0x2d] = 4;
            WW(0x3c, 0xf6fb);
            WW(0x26, 0x800);
            WW(0x36, 0x800);
            WW(0x44, 1);
            WW(0, 0xf10a);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        if (RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(bankb, 0xee20 + (RW(0x40) & 2)));
            WW(0x44, 2);
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        unsigned material = terrain_material(ram, tiles, RW(0x21), RW(0x24));
        if ((1u << (material & 15)) & 0xff02) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        break;
    }
    case 0xbf180:
        WW(0x0e, 0x21);
        ram[actor + 0x2d] = 0x20;
        WW(0x3c, 0xf2b3);
        WW(0x26, 0xc0);
        WW(0x36, 0x28);
        WW(0x1e, 0x0a);
        WW(0x44, 1);
        WW(0, 0xf1a8);
        break;
    case 0xbf1a8: {
        uint16_t carrier = ancestor(ram, actor, 1);
        if (ram[carrier + 0x0a]) {
            if (ram[carrier + 0x0a] & 2) finish_animation(ram, actor);
            else WW(0, 0x893);
            break;
        }
        WW(0x21, read_word(ram, carrier + 0x21));
        WW(0x24, ((read_word(ram, 0x274) & 0x10) >> 4) + read_word(ram, carrier + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(bankb, 0xee24 + (RW(0x40) & 6)));
            WW(0x44, 0x10);
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (RW(0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        loud_radial_blast(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0x07da7:
        if (!decrement(ram, actor + 0x42)) ram[0x263] |= 0x80;
        break;
    /* ---- Ending, first scene: portrait and typed message ---- */
    case 0x01a50: {
        uint16_t child = effect_slot(ram, actor, 0x994c);
        if (child != 0x8480) write_word(ram, child + 2, 0xa000);
        WW(0, 0x1a65);
        WW(2, 0);
        break;
    }
    case 0x01a65:
    case 0xa9977:
        break;
    case 0xa994c: {
        uint16_t child = effect_slot(ram, actor, 0x9a6b);
        if (child != 0x8480) write_word(ram, child + 2, 0xa000);
        WW(0x42, 0x50);
        WW(0, 0x9961);
    }
    /* fall through */
    case 0xa9961:
        if (!decrement(ram, actor + 0x42)) {
            uint16_t child = effect_slot(ram, actor, 0x9978);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            WW(0, 0x9977);
        }
        break;
    case 0xa9978: {
        unsigned variant = (read_word(ram, 0x272) & 3) << 1;
        int duel = variant == 6;
        WW(0x54, read_word(banka, 0x98f8 + variant));
        WW(0x56, read_word(banka, 0x9712 + variant));
        write_word(ram, 0x84, read_word(banka, 0x9908 + variant));
        WW(0x50, duel ? 0x1a48 : 0x2548);
        WW(0x52, duel ? 0x1a48 : 0x2548);
        WW(0x42, 1);
        WW(0x4a, duel ? 3 : 6);
        WW(0, 0x99d4);
    }
    /* fall through */
    case 0xa99d4: {
        if (decrement(ram, actor + 0x42)) break;
        /* [54] is the tile segment the text is typed into. */
        uint8_t *layer = tiles + (uint16_t)((RW(0x54) - 0xd000) << 4);
        uint8_t glyph = banka[RW(0x56)];
        if (glyph == 0xff) {
            uint16_t line = (RW(0x52) + 0x200) & 0x3fff;
            WW(0x50, line);
            WW(0x52, line);
            WW(0x56, RW(0x56) + 1);
            glyph = banka[RW(0x56)];
            if (glyph == 0xff) {
                WW(0x42, 0x60);
                WW(0, 0x9a2c);
                break;
            }
        }
        write_word(layer, RW(0x50), glyph);
        write_word(layer, RW(0x50) + 2, 0x104);
        WW(0x56, RW(0x56) + 1);
        WW(0x50, RW(0x50) + 4);
        WW(0x42, RW(0x4a));
        break;
    }
    case 0xa9a2c:
        if (decrement(ram, actor + 0x42)) break;
        if (read_word(ram, 0x272) == 3) {
            memset(tiles, 0, 0x4000);   /* 37BB: the duel draw clears the text layer and goes on */
            world_queue_sound(ram, 0x17);
            ram[0x26f] &= (uint8_t)~0x80u;
        } else write_word(ram, 0x86, 0x80);
        finish_animation(ram, actor);
        break;
    case 0xa9a6b: {
        unsigned variant = read_word(ram, 0x272) & 3;
        ram[actor + 0x2c] = (uint8_t)(banka[0x96f6 + variant] + ((read_word(ram, 0x264) & 2) >> 1));
        WW(0x2a, read_word(banka, 0x96fa + 2 * variant));
        WW(0x21, read_word(banka, 0x9702 + 2 * variant));
        WW(0x24, read_word(banka, 0x970a + 2 * variant));
        WW(0, 0x9aa6);
    }
    /* fall through */
    case 0xa9aa6:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x04124:
        finish_animation(ram, actor);
        break;
    case 0x0a220:
        ram[actor + 0x2c] = 3;
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x42, 0x10);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor, 0xa238);
    /* fall through */
    case 0x0a238:
        update_attached_effect(ram, main_rom, actor, 0xa2eb);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}

/* ---- Ending scene controllers (masters 344D..35F9), called from the frame boundary ---- */

static void ending_fade(uint8_t *ram, unsigned speed)
{
    for (unsigned i = 0; i < 64; ++i) {
        uint16_t job = 0x9e06 + 8 * i;
        write_word(ram, job + 2, 0x82);
        write_word(ram, job + 4, speed);
        write_word(ram, job + 6, 0x1020);
    }
}

/* Returns 0 for a master that is not an ending controller. *master_command
 * receives the sound-board reset the controller issues, if any. */
int world_duel_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *master_command);
int world_ending_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *master_command)
{
    unsigned variant = (read_word(ram, 0x272) & 3) << 1;
    switch (read_word(ram, 0x80)) {
    case 0x344d: {
        /* The ending number goes into the surviving player's record. */
        uint16_t ending = read_word(rom, 0x3445 + variant);
        uint16_t record = (read_word(ram, 0x264) & 2) << 5;
        write_word(ram, record + 0x126, ending);
        if (read_word(ram, 0x272) == 2) write_word(ram, (record ^ 0x40) + 0x126, ending - 1);
        memset(tiles, 0, 0x10000);
        world_frame_init_palette(ram, palette, rom, 0xe9a4);
        memset(ram + 0xb700, 0, 46);
        write_word(ram, 0x292, 0);
        uint16_t size = read_word(rom, 0x3301 + variant);
        tile_columns(ram, tiles, rom, 0, read_word(rom, 0x32f9 + variant), read_word(rom, 0x32e9 + variant),
                     size >> 8, size & 255);
        tile_columns(ram, tiles, rom, 0x4000, 0x1330, read_word(rom, 0x32f1 + variant), 10, 4);
        tile_text(tiles, rom, 0x2340, 0xfeb7, 0);
        write_word(ram, 0x262, 3);
        world_frame_init_actors(ram, 0x1a50, 0x8000);
        world_frame_reset_sound(ram);
        *master_command = 0x20;
        write_word(ram, 0x86, 0);
        write_word(ram, 0x80, 0x3513);
    }
    /* fall through */
    case 0x3513:
        if (read_word(ram, 0x86)) write_word(ram, 0x80, 0x3522);
        return 1;
    case 0x3522:
        if (!decrement(ram, 0x86)) {
            ending_fade(ram, 7);
            write_word(ram, 0x86, 0x80);
            write_word(ram, 0x80, 0x355b);
        }
        return 1;
    case 0x355b:
        if (!decrement(ram, 0x86)) {
            ram[0x262] &= (uint8_t)~1u;
            write_word(ram, 0x80, 0x356e);
        }
        return 1;
    case 0x356e:
        memset(tiles, 0, 0x10000);
        world_frame_init_palette(ram, palette, rom, read_word(rom, 0x3309 + variant));
        write_word(ram, 0x262, 3);
        world_frame_init_actors(ram, 0x1af7, 0x8000);
        world_frame_reset_sound(ram);
        *master_command = 0x20;
        world_queue_sound(ram, read_word(rom, 0x3323 + variant));
        write_word(ram, 0x86, 0);
        write_word(ram, 0x80, 0x35be);
        return 1;
    case 0x35be:
        if (read_word(ram, 0x86)) {
            ending_fade(ram, 6);
            write_word(ram, 0x86, 0x50);
            write_word(ram, 0x80, 0x35f9);
        }
        return 1;
    case 0x35f9:
        if (!decrement(ram, 0x86)) {
            write_word(ram, 0x262, 0);
            write_word(ram, 0x80, 0xc466);
        }
        return 1;
    }
    return world_duel_master(ram, tiles, palette, rom, master_command); /* 333D..342A, world_credits.c */
}
