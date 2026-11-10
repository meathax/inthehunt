/* Recovered World actor handlers: the credits scene, post-ending name entry and the two-player duel ending.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include "world_frame.h"
#include <string.h>

void world_sub_pose(uint8_t *ram, const uint8_t *rom, uint16_t sub);
void world_sub_wake(uint8_t *ram, const uint8_t *rom, uint16_t sub);
int world_duel_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *master_command);

#define RW(off) read_word(ram, (uint16_t)(actor + (off)))
#define WW(off, value) write_word(ram, (uint16_t)(actor + (off)), (uint16_t)(value))
#define RB(off) ram[(uint16_t)(actor + (off))]

/* 1769: a child whose handler lives in `segment`. A full pool hands back the
 * allocator's discard slot, which callers still write to. */
static uint16_t spawn_in(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t handler)
{
    uint16_t child = world_spawn_child(ram, actor, handler);
    if (!child) return 0x8480;
    write_word(ram, child + 2, segment);
    return child;
}

/* A word store through an arbitrary segment: only video and work RAM take it. */
static void far_store(uint8_t *ram, uint8_t *tiles, uint16_t segment, uint16_t offset, uint16_t value)
{
    uint32_t linear = (((uint32_t)segment << 4) + offset) & 0xfffff;
    if (linear >= 0xd0000 && linear < 0xdffff) write_word(tiles, (uint16_t)(linear - 0xd0000), value);
    else if (linear >= 0xe0000 && linear < 0xeffff) write_word(ram, (uint16_t)(linear - 0xe0000), value);
}

/* 657D: velocity from the heading [28] and the speed [2E]. */
static void heading_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = RB(0x28) * 2;
    int32_t speed = (int16_t)RW(0x2e);
    WW(0x38, (uint32_t)((int16_t)read_word(rom + 0xa0000, 0x9e12 + angle) * speed) >> 8);
    WW(0x3a, (uint32_t)((int16_t)read_word(rom + 0xa0000, 0x9d92 + angle) * speed) >> 8);
}

/* 6761 / 6789: a sine of amplitude CW at phase (AW * BW) >> 8 added to the
 * 24-bit centre at `centre`, stored at `dest`. */
static void sine_wave(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t value,
                      uint16_t rate, uint16_t amplitude, unsigned centre, unsigned dest)
{
    unsigned phase = ((uint16_t)(value * rate) >> 8) & 255;
    int32_t wave = (int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * phase) * (int16_t)amplitude;
    uint32_t sum = (RW(centre) | ((uint32_t)RB(centre + 2) << 16)) + ((uint32_t)wave & 0xffffff);
    WW(dest, sum);
    RB(dest + 2) = (uint8_t)(sum >> 16);
}

static void sine_offset(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t value,
                        unsigned centre, unsigned dest)
{
    sine_wave(ram, rom, actor, value, RW(0x76), RW(0x78), centre, dest);
}

/* D692: load the script record at [7E]. Returns 0 at the FFFF terminator,
 * -1 for a record kind outside the table. */
static int script_load(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)RW(2) << 4);
    uint16_t at = RW(0x7e), kind = read_word(bank, at);
#define REC(n) read_word(bank, (uint16_t)(at + (n)))
    if (kind == 0xffff) return 0;
    if ((kind & 1) || kind > 0x14) return -1;
    WW(0x7a, kind);
    switch (kind) {
    case 0x00:
        WW(0x7e, at + 4); WW(0x38, 0); WW(0x3a, 0);
        break;
    case 0x02:
    case 0x0e:
        RB(0x28) = (uint8_t)REC(4);
        if (kind == 2 && REC(6)) WW(0x2e, REC(6));
        heading_velocity(ram, rom, actor);
        WW(0x78, 0); WW(0x76, 0);
        WW(0x7e, at + 8); RB(0x20) = 0; RB(0x23) = 0;
        break;
    case 0x04: {
        uint16_t speed = RW(0x2e);
        RB(0x28) = (uint8_t)REC(4);
        WW(0x2e, REC(8));
        heading_velocity(ram, rom, actor);
        WW(0x78, RW(0x38)); WW(0x76, RW(0x3a));
        WW(0x2e, speed);
        if (REC(6)) WW(0x2e, REC(6));
        heading_velocity(ram, rom, actor);
        WW(0x7e, at + 10); RB(0x20) = 0; RB(0x23) = 0;
        break;
    }
    case 0x06:
    case 0x08:
        WW(0x1c, REC(4)); WW(0x1a, REC(6));
        WW(0x31, REC(8) + RW(0x21)); WW(0x34, REC(10) + RW(0x24));
        WW(0x78, REC(12)); WW(0x76, kind == 8 ? REC(14) : 0);
        WW(0x7e, at + (kind == 8 ? 16 : 14)); RB(0x20) = 0; RB(0x23) = 0;
        break;
    case 0x0a:
    case 0x0c: {
        uint16_t speed = REC(8);
        WW(0x78, REC(4)); WW(0x76, REC(6));
        WW(kind == 0x0a ? 0x38 : 0x3a, speed);
        RB(0x28) = rom[(kind == 0x0a ? 0xda0f : 0xda11) + (speed >> 15)];
        WW(0x31, RW(0x21)); WW(0x34, RW(0x24));
        WW(0x7e, at + 10); RB(0x20) = 0; RB(0x23) = 0;
        break;
    }
    case 0x10:
        WW(0x78, REC(4)); WW(0x76, REC(6)); WW(0x38, REC(8));
        WW(0x7e, at + 10);
        WW(0x31, 0); WW(0x34, RW(0x24));
        RB(0x20) = 0; RB(0x30) = 0; RB(0x23) = 0;
        break;
    case 0x12:
        WW(0x78, REC(4)); WW(0x76, REC(6)); WW(0x3a, REC(8));
        WW(0x7e, at + 10);
        WW(0x31, RW(0x21)); WW(0x34, 0);
        RB(0x20) = 0; RB(0x23) = 0; RB(0x33) = 0;
        break;
    case 0x14:
        WW(0x38, REC(4)); WW(0x3a, REC(6));
        WW(0x7e, at + 8);
        break;
    }
    WW(0x7c, REC(2));
#undef REC
    return 1;
}

/* D68F. */
static int script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    WW(0x7e, offset);
    return script_load(ram, rom, actor);
}

/* D6B7: one tick. Returns 0 when the script has ended, -1 on an unknown record. */
static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t kind = RW(0x7a);
    if ((kind & 1) || kind > 0x14) return -1;
    switch (kind) {
    case 0x00:
        scroll_actor(ram, actor, 0xb71a);
        break;
    case 0x02:
    case 0x04:
    case 0x0e: {
        unsigned base = kind == 0x0e ? 0x30 : 0x20;
        scroll_actor(ram, actor, 0xb71a);
        WW(0x38, RW(0x38) + RW(0x78));
        move_actor_axis(ram, actor + base, RW(0x38));
        WW(0x3a, RW(0x3a) + RW(0x76));
        move_actor_axis(ram, actor + base + 3, RW(0x3a));
        break;
    }
    case 0x06:
    case 0x08: {
        WW(0x31, RW(0x31) + read_word(ram, 0xb71a));
        WW(0x34, RW(0x34) + read_word(ram, 0xb71c));
        WW(0x1a, RW(0x1a) + RW(0x78));
        WW(0x1c, RW(0x1c) + RW(0x76));
        unsigned angle = RB(0x1b) * 2;
        for (unsigned axis = 0; axis < 2; ++axis) {
            int32_t arm = (int16_t)read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle) * (int16_t)RW(0x1c);
            unsigned centre = 0x30 + 3 * axis, dest = 0x20 + 3 * axis;
            uint32_t sum = (RW(centre) | ((uint32_t)RB(centre + 2) << 16)) + ((uint32_t)arm & 0xffffff);
            WW(dest, sum);
            RB(dest + 2) = (uint8_t)(sum >> 16);
        }
        RB(0x28) = (uint8_t)(((RW(0x78) & 0x8000) ? -0x40 : 0x40) + RB(0x1b));
        break;
    }
    case 0x0a:
        scroll_actor(ram, actor, 0xb71a);
        WW(0x31, RW(0x31) + read_word(ram, 0xb71a));
        WW(0x34, RW(0x34) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        sine_offset(ram, rom, actor, RW(0x21) - RW(0x31), 0x33, 0x23);
        break;
    case 0x0c:
        scroll_actor(ram, actor, 0xb71a);
        WW(0x31, RW(0x31) + read_word(ram, 0xb71a));
        WW(0x34, RW(0x34) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        sine_offset(ram, rom, actor, RW(0x24) - RW(0x34), 0x30, 0x20);
        break;
    case 0x10:
        scroll_actor(ram, actor, 0xb71a);
        WW(0x34, RW(0x34) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x30, RW(0x38));
        sine_offset(ram, rom, actor, RW(0x31), 0x33, 0x23);
        break;
    case 0x12:
        scroll_actor(ram, actor, 0xb71a);
        WW(0x31, RW(0x31) + read_word(ram, 0xb71c));
        move_actor_axis(ram, actor + 0x33, RW(0x3a));
        sine_offset(ram, rom, actor, RW(0x34), 0x30, 0x20);
        break;
    case 0x14:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        break;
    }
    if (!decrement(ram, actor + 0x7c)) return script_load(ram, rom, actor);
    return 1;
}

/* The playfield test shared by the free-moving actors: carry set = outside. */
static int outside_field(const uint8_t *ram, uint16_t actor)
{
    int fixed = read_word(ram, 0x26e) & 2;
    uint16_t x = RW(0x21), y = RW(0x24);
    if (x > 0x1c0 || x < 0x40) return 1;
    if (y > (uint16_t)(fixed ? 0x198 : 0x1c8 + read_word(ram, 0xb732))) return 1;
    return (int16_t)y < (int16_t)(fixed ? 0x68 : 0x10 + read_word(ram, 0xb732));
}

/* 110B: a private palette slot loaded with `resource`. */
static void private_palette(uint8_t *ram, uint16_t actor, uint16_t resource)
{
    if ((read_word(ram, 0x262) & 0x8800) || !read_word(ram, 0xa48a)) {
        ++ram[0xa48e + (resource & 255)];
        ++ram[0xa58e + (resource & 255)];
        ram[0x26d] |= 0x80;
        return;
    }
    decrement(ram, 0xa48a);
    uint16_t cursor = read_word(ram, 0xa486);
    unsigned slot = ram[(uint16_t)(0xa406 + cursor)];
    write_word(ram, 0xa486, (cursor + 1) & 63);
    RB(0x2c) = slot; RB(0x14) = slot; RB(0x10) = (uint8_t)resource;
    uint16_t job = 0x9e06 + 8 * slot;
    write_word(ram, job, resource);
    write_word(ram, job + 2, 0x40);
    write_word(ram, job + 4, 0);
    write_word(ram, job + 6, 0);
}

/* 11D7: queue a palette fade job unless palettes are frozen. */
static void palette_job(uint8_t *ram, unsigned slot, uint16_t palette, uint16_t speed)
{
    if (read_word(ram, 0x262) & 0x8800) return;
    uint16_t job = 0x9e06 + 8 * (slot & 0x3f);
    write_word(ram, job, palette);
    write_word(ram, job + 2, 0x80);
    write_word(ram, job + 4, speed);
    ram[job + 6] = 0x20;
    ram[job + 7] = 0x10;
}

/* 5FC6: double-size glyphs; returns the address of the FF terminator. */
static uint16_t big_text(uint8_t *tiles, const uint8_t *rom, uint16_t *pos_io, uint16_t text, uint16_t attr)
{
    uint16_t pos = *pos_io;
    for (; rom[text] != 0xff; ++text) {
        uint16_t glyph = (uint16_t)((rom[text] - 0x30) * 4 + 0x100);
        uint16_t right = (pos & 0xff00) | ((pos + 4) & 255);
        uint16_t below = (pos + 0x100) & 0x3fff;
        uint16_t below_right = (below & 0xff00) | ((below + 4) & 255);
        write_word(tiles, pos, glyph);             write_word(tiles, pos + 2, attr);
        write_word(tiles, right, glyph + 1);       write_word(tiles, right + 2, attr);
        write_word(tiles, below, glyph + 2);       write_word(tiles, below + 2, attr);
        write_word(tiles, below_right, glyph + 3); write_word(tiles, below_right + 2, attr);
        pos = (pos & 0xff00) | ((pos + 8) & 255);
    }
    *pos_io = pos;
    return text;
}

/* 5F4A: single glyphs; returns the address of the FF terminator. */
static uint16_t small_text(uint8_t *tiles, const uint8_t *rom, uint16_t *pos_io, uint16_t text, uint16_t attr)
{
    uint16_t pos = *pos_io;
    for (; rom[text] != 0xff; ++text) {
        write_word(tiles, pos, rom[text]);
        write_word(tiles, pos + 2, attr);
        pos = (pos & 0xff00) | ((pos + 4) & 255);
    }
    *pos_io = pos;
    return text;
}

/* 37EB: `width` columns of `height` four-by-four tile blocks from a list in bank A000. */
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

/* 68DF: the terrain material under a screen point. */
static unsigned terrain_material(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    return tiles[0x4000 + world_tile_address(map_x, map_y) + 3] >> 3;
}

/* 8B8D: the pose springs and the sprite they select, without the extras of 8B46. */
static unsigned pose_axis(uint8_t *ram, uint16_t position)
{
    uint16_t offset = read_word(ram, position), velocity = read_word(ram, position + 2);
    int16_t force = (int16_t)(uint16_t)(-2 * offset - velocity);
    velocity += (uint16_t)(force >> 4);
    offset += velocity;
    write_word(ram, position + 2, velocity);
    write_word(ram, position, offset);
    int step = (int8_t)((uint16_t)(offset + 128) >> 8);
    if (step < -2) step = -2;
    if (step > 2) step = 2;
    return (unsigned)(step + 2);
}

static unsigned duel_pose(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned row = pose_axis(ram, actor + 0x5c);
    unsigned column = 2 * pose_axis(ram, actor + 0x58);
    unsigned pose = column + 10 * row;
    WW(0x56, column);
    WW(0x72, pose);
    if (RW(0x60) == 3) pose += 0x32;
    WW(0x2a, read_word(rom, 0x7711 + pose));
    return pose;
}

/* 993A: the duel submarine moves at a fixed speed inside the arena. */
static void duel_move(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned row = rom[0x8cab + (read_word(ram, 0xb73e) & 15)];
    WW(0x38, read_word(rom, 0x8cbb + row));
    WW(0x3a, read_word(rom, 0x8cbd + row));
    move_actor_axis(ram, actor + 0x20, RW(0x38));
    move_actor_axis(ram, actor + 0x23, RW(0x3a));
    if ((int16_t)RW(0x21) > 0x188) WW(0x21, 0x188);
    else if ((int16_t)RW(0x21) < 0x78) WW(0x21, 0x78);
    if ((int16_t)RW(0x24) > 0x170) WW(0x24, 0x170);
    else if ((int16_t)RW(0x24) < 0xa0) WW(0x24, 0xa0);
}

/* 96B7: the health bar of a duel submarine on the text layer. */
static void duel_health_bar(const uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor)
{
    unsigned side = ram[actor + 0x0b] & 2;
    uint16_t attr = read_word(rom, 0x8ed5 + side), pos = read_word(rom, 0x8ed9 + side);
    unsigned column = 8;
    for (unsigned n = ram[actor + 0x26]; n; --n) {
        write_word(tiles, pos, 0x70);
        write_word(tiles, pos + 2, attr);
        write_word(tiles, pos + 4, 0x71);
        write_word(tiles, pos + 6, attr);
        pos = (pos & 0xff00) | ((pos + 8) & 255);
        if (!--column) {
            pos = (pos & 0xff00) | ((pos - 0x40) & 255);
            pos = (pos + 0x100) & 0x3fff;
            column = 8;
        }
    }
    write_word(tiles, pos, 0);
    write_word(tiles, pos + 2, attr);
    write_word(tiles, pos + 4, 0);
    write_word(tiles, pos + 6, attr);
}

/* 951A: the three duel weapons, each with its own cooldown and stock. */
static void duel_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned side = (RB(0x29) & 1) << 1;
    if (RW(0x4a)) decrement(ram, actor + 0x4a);
    else if ((read_word(ram, 0xb742) & 0x80) && RW(0x44)) {
        uint16_t shot = world_spawn_child(ram, actor, 0x9be7);
        if (shot) {
            ram[shot + 0x29] = RB(0x29);
            write_word(ram, shot + 0x21, RW(0x21) + read_word(rom, 0x9ba9 + side));
            write_word(ram, shot + 0x24, RW(0x24) + read_word(rom, (uint16_t)(0x9b9f + RW(0x56))));
            ram[shot + 0x2d] = RB(0x2d) + 1;
            ram[shot + 0x0b] = RB(0x0b) | 0x80;
            uint16_t flash = effect_slot(ram, actor, 0xa268);
            ram[flash + 0x29] = RB(0x29);
            write_word(ram, flash + 0x31, read_word(rom, 0x9ba9 + side));
            ram[flash + 0x2d] = RB(0x2d);
            write_word(ram, flash + 0x52, RW(0x52));
            WW(0x52, RW(0x52) ^ 1);
            decrement(ram, actor + 0x44);
            WW(0x4a, 0x10);
            world_queue_sound(ram, 0x3d);
        }
    }
    if (RW(0x4c)) decrement(ram, actor + 0x4c);
    else if ((read_word(ram, 0xb742) & 0x40) && RW(0x46)) {
        uint16_t shot = world_spawn_child(ram, actor, 0x9e73);
        if (shot) {
            ram[shot + 0x29] = RB(0x29);
            write_word(ram, shot + 0x21, RW(0x21) + read_word(rom, 0x9bad + side));
            write_word(ram, shot + 0x24, RW(0x24) + 4);
            ram[shot + 0x2d] = RB(0x2d) + 1;
            ram[shot + 0x0b] = RB(0x0b) | 0x80;
            uint16_t flash = effect_slot(ram, actor, 0xa2c0);
            ram[flash + 0x29] = RB(0x29);
            write_word(ram, flash + 0x31, read_word(rom, 0x78c7 + side));
            write_word(ram, flash + 0x34, 4);
            ram[flash + 0x2d] = RB(0x2d) - 1;
            decrement(ram, actor + 0x46);
            WW(0x4c, 8);
            world_queue_sound(ram, 0x37);
        }
    }
    if (RW(0x4e)) decrement(ram, actor + 0x4e);
    else if ((read_word(ram, 0xb742) & 0x40) && RW(0x48) < 3) {
        uint16_t shot = world_spawn_child(ram, actor, 0xa06b);
        if (shot) {
            write_word(ram, shot + 0x21, RW(0x21));
            write_word(ram, shot + 0x24, RW(0x24) - 8);
            ram[shot + 0x0b] = RB(0x0b) | 0x80;
            write_word(ram, shot + 0x54, RW(0x54) & 3);
            WW(0x54, RW(0x54) + 1);
            WW(0x48, RW(0x48) + 1);
            WW(0x4e, 8);
        }
    }
}

/* BE98E: the duel arena keeps sixteen mines; a destroyed one is replaced after [42] passes. */
static void duel_mines(uint8_t *ram, uint16_t actor)
{
    for (unsigned n = 0; n < 16; ++n) {
        if (RW(0x60) & (1u << n)) continue;
        uint16_t mine = spawn_in(ram, actor, 0xb000, 0xe57b);
        write_word(ram, mine + 0x14, RW(0x14));
        ram[mine + 0x2c] = RB(0x15);
        write_word(ram, mine + 0x42, RW(0x42));
        write_word(ram, mine + 0x1e, n | 0x2000);
        WW(0x60, RW(0x60) | (1u << n));
    }
}

/* A402: a duel shot against the rival submarine (A3C9), then, for player
 * two's shots, against the shots already listed this pass (D318); a shot
 * that did not hit the rival joins the list (D3D0). */
void world_duel_shot_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t rival = read_word(ram, 0x90 + 2 * (RB(0x0b) & 1));
    uint16_t shape = read_word(ram, rival + 0x3c);
    if (shape != 0xffff && world_shape_overlap(ram, rom, actor, rival, RW(0x3c), shape)) {
        WW(0x26, 0);
        if (!ram[rival + 0x19]) {
            decrement(ram, rival + 0x26);
            ram[rival + 0x19] = 0x0a;
            return;
        }
    }
    if (RB(0x0b) & 2) {
        for (uint16_t at = 0; at < read_word(ram, 0xa6e2); at += 2) {
            uint16_t other = read_word(ram, 0xa6e6 + at);
            if (!other || !world_shape_overlap(ram, rom, actor, other, RW(0x3c), read_word(ram, other + 0x3c)))
                continue;
            write_word(ram, 0xa6e6 + at, 0);
            if (ram[other + 0x0b] == RB(0x0b)) continue;
            uint16_t theirs = read_word(ram, other + 0x26), mine = RW(0x26);
            write_word(ram, other + 0x26, theirs >= RW(0x36) ? theirs - RW(0x36) : 0);
            WW(0x26, mine >= read_word(ram, other + 0x36) ? mine - read_word(ram, other + 0x36) : 0);
            RB(0x19) = (RB(0x19) & 1) + 0x0a;
            break;
        }
    }
    if (RW(0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
}

/* Duel scene controllers (masters 333D..342A), reached from the ending controllers. */
int world_duel_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *master_command)
{
    switch (read_word(ram, 0x80)) {
    case 0x333d:
        memset(tiles, 0, 0x10000);
        world_frame_init_palette(ram, palette, rom, 0xeb3e);
        private_palette(ram, 0x80, 0x17c);   /* 110B runs with BP = 80h here */
        memset(ram + 0xb700, 0, 46);
        write_word(ram, 0x292, 0);
        write_word(ram, 0x262, 3);
        world_frame_init_actors(ram, 0x1a66, 0x8000);
        world_frame_reset_sound(ram);
        *master_command = 0x20;
        world_queue_sound(ram, 0x15);
        write_word(ram, 0x264, 3);
        write_word(ram, 0xb8a0, read_word(ram, 0x84) == 8 ? 1 : 0x3c);
        write_word(ram, 0x80, 0x33a9);
    /* fall through */
    case 0x33a9:
        if (read_word(ram, 0xb8a0)) {
            if (read_word(ram, 0x264) != 3) {
                write_word(ram, 0x86, 0x100);
                write_word(ram, 0x272, 2);
                write_word(ram, 0x80, 0x33e4);
            }
        } else {
            write_word(ram, 0x272, 3);
            write_word(ram, 0xb8a0, 0);
            write_word(ram, 0x86, 0);
            write_word(ram, 0x80, 0x342a);
        }
        return 1;
    case 0x33e4:
        if (!decrement(ram, 0x86)) {
            for (unsigned i = 0; i < 64; ++i) {
                uint16_t job = 0x9e06 + 8 * i;
                write_word(ram, job + 2, 0x82);
                write_word(ram, job + 4, 5);
                write_word(ram, job + 6, 0x1020);
            }
            write_word(ram, 0x86, 0x30);
            write_word(ram, 0x80, 0x341e);
        }
        return 1;
    case 0x341e:
        if (!decrement(ram, 0x86)) write_word(ram, 0x80, 0x344d);
        return 1;
    case 0x342a:
        if (read_word(ram, 0x86)) {
            write_word(ram, 0x126, 8);
            write_word(ram, 0x166, 8);
            write_word(ram, 0x80, 0x35be);
        }
        return 1;
    }
    return 0;
}

int world_credits_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank7 = main_rom + 0x70000, *banka = main_rom + 0xa0000, *bankb = main_rom + 0xb0000;
    (void)pass;
    switch (address) {
    /* ---- Both players alive after the last boss: the submarine turns and leaves ---- */
    case 0x07db3:
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        world_sub_pose(ram, main_rom, actor);
        ram[0xb73e] |= 1;
        world_sub_wake(ram, main_rom, actor);
        if ((int16_t)RW(0x21) >= 0x160 || !decrement(ram, actor + 0x42)) {
            WW(0x40, 0);
            WW(0x78, 1);
            write_word(ram, 0xb73e, 0);
            WW(0, 0x7de6);
        }
        break;
    case 0x07de6: {
        if (decrement(ram, actor + 0x78)) break;
        uint16_t frame = RW(0x40);
        uint16_t sprite = read_word(main_rom, (uint16_t)(0x77b1 + frame));
        if (sprite == 0xffff) {
            WW(0x42, 0x60);
            WW(0x38, 0xfe80);
            WW(0x40, 0); WW(0x58, 0); WW(0x5a, 0); WW(0x5c, 0); WW(0x5e, 0);
            write_word(ram, 0xb73e, 0x202);
            WW(0, 0x7e3b);
            break;
        }
        WW(0x2a, sprite);
        WW(0x78, read_word(main_rom, (uint16_t)(0x77c7 + frame)));
        RB(0x29) = main_rom[(uint16_t)(0x77db + (frame >> 1))];
        WW(0x40, frame + 2);
        break;
    }
    case 0x07e3b:
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        world_sub_pose(ram, main_rom, actor);
        ram[0xb73e] |= 2;
        world_sub_wake(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42)) {
            ram[0x263] |= 0x80;
            write_word(ram, 0x272, 2);
        }
        break;
    /* ---- Duel scene root, camera and clock ---- */
    case 0x01a66: {
        write_word(ram, 0x9a, spawn_in(ram, actor, 0, 0x1b62));
        spawn_in(ram, actor, 0xb000, 0xe538);
        spawn_in(ram, actor, 0xa000, 0x9aac);
        write_word(ram, 0x9c, spawn_in(ram, actor, 0, 0x1b62));
        spawn_in(ram, actor, 0, 0x3f11);
        spawn_in(ram, actor, 0, 0x3ad7);
        spawn_in(ram, actor, 0, 0x3bfa);
        spawn_in(ram, actor, 0, 0x57e3);
        uint16_t sub = spawn_in(ram, actor, 0, 0x8f12);
        ram[sub + 0x0b] = 2;
        write_word(ram, 0x92, sub);
        sub = spawn_in(ram, actor, 0, 0x8ee1);
        ram[sub + 0x0b] = 1;
        write_word(ram, 0x90, sub);
        WW(0, 0x1af6);
        WW(2, 0);
        break;
    }
    case 0x01af6:
    case 0x09939:
    case 0x0905e:
        break;
    case 0x04183:
        ram[0x26f] |= 0x80;
        WW(0, 0x418e);
    /* fall through */
    case 0x0418e:
        if (!(ram[0x26f] & 0x80)) WW(0, 0x419c);
        break;
    case 0x041d4:
        if (!(ram[0x26f] & 0x80)) WW(0, 0x41e2);
        break;
    case 0xa9aac: {
        uint16_t pos = 0x2d78;
        big_text(tiles, main_rom, &pos, 0x3333, 0x105);
        static const uint16_t frame[][3] = {
            {0x2d70, 0x72, 0x106}, {0x2e70, 0x74, 0x106}, {0x2d74, 0x73, 0x106}, {0x2e74, 0x75, 0x106},
            {0x2d88, 0x73, 0x306}, {0x2e88, 0x75, 0x306}, {0x2d8c, 0x72, 0x306}, {0x2e8c, 0x74, 0x306} };
        for (unsigned i = 0; i < 8; ++i) {
            write_word(tiles, frame[i][0], frame[i][1]);
            write_word(tiles, frame[i][0] + 2, frame[i][2]);
        }
        WW(0x42, 0x80);
        WW(0x44, 0x40);
        spawn_in(ram, actor, 0xa000, 0x9be4);
        WW(0, 0x9b49);
    }
    /* fall through */
    case 0xa9b49:
        if ((read_word(ram, 0x264) & 3) != 3) break;
        if (!decrement(ram, actor + 0x42)) {
            WW(0x42, 0x80);
            if (!read_word(ram, 0xb8a0)) {
                effect_slot(ram, actor, 0x61b4);
                WW(0x42, 0xc0);
                WW(0, 0x9bbd);
                break;
            }
            decrement(ram, 0xb8a0);
            refresh_stage_time(ram, tiles, actor, read_word(ram, 0xb8a0));
        }
        if (!decrement(ram, actor + 0x44)) {
            int hurry = read_word(ram, 0xb8a0) < 10;
            WW(0x44, hurry ? 0x20 : 0x40);
            palette_job(ram, 0x106, read_word(banka, 0x9944 + (RW(0x40) & 2) + (hurry ? 4 : 0)), hurry ? 5 : 6);
            WW(0x40, RW(0x40) + 2);
        }
        break;
    case 0xa9be4:
        RB(0x2c) = 5;
        WW(0x2a, 0xa4e3);
        WW(0x42, 0x80);
        WW(0x21, 0x100);
        WW(0x24, 0x104);
        WW(0, 0x9c01);
    /* fall through */
    case 0xa9c01:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    /* ---- Duel submarines ---- */
    case 0x08ee1:
    case 0x08f12: {
        int second = address == 0x08f12;
        RB(0x2d) = second ? 0x10 : 8;
        WW(0x3c, 0x78d3);
        RB(0x2c) = second ? 2 : 1;
        WW(0x70, second ? 0x140 : 0x100);
        if (second) RB(0x29) = 1;
        WW(0x21, second ? 0x180 : 0x80);
        WW(0x24, 0x100);
        WW(0x7c, read_word(ram, second ? 0x90 : 0x92));
        if (!second) spawn_in(ram, actor, 0, 0x9939);
        WW(0x58, 0); WW(0x5a, 0); WW(0x5c, 0); WW(0x5e, 0);
        WW(0x26, 8);
        WW(0x44, 3);
        WW(0x46, 2);
        WW(0x4a, 0x80); WW(0x4c, 0x80); WW(0x4e, 0x80);
        WW(0x56, 4);
        WW(0x64, 6);
        WW(0x66, 0x18);
        WW(0x2a, 0x0e42);
        duel_health_bar(ram, main_rom, tiles, actor);
        WW(0, 0x8f82);
        break;
    }
    case 0x08f82: {
        if (read_word(ram, 0x264) == 3) {
            if (!read_word(ram, 0xb8a0)) {
                RB(0x2d) = 0x20;
                WW(0x42, 0x280);
                WW(0, 0x905f);
                goto duel_draw;
            }
            if (!RW(0x26)) {
                duel_health_bar(ram, main_rom, tiles, actor);
                RB(0x19) = 0;
                WW(0x40, 0);
                WW(0x60, 1);
                WW(0x3c, 0xffff);
                uint16_t blast = effect_slot(ram, actor, 0x6efe);   /* 6B69 */
                write_word(ram, blast + 0x4c, 0x74e5);
                write_word(ram, blast + 0x4a, 0x7665);
                ram[blast + 0x2c] = 0x0a;
                ram[blast + 0x2d] = 0x20;
                write_word(ram, blast + 0x56, 0x40);
                write_word(ram, blast + 0x58, 0);
                world_queue_sound(ram, 0x48);
                write_word(ram, 0x264, read_word(ram, 0x264) & ~(1u << ((RB(0x0b) - 1) & 15)));
                spawn_in(ram, actor, 0, 0x9423);
                WW(0, 0x9031);
                goto duel_sinking;
            }
        }
        duel_move(ram, main_rom, actor);
        duel_pose(ram, main_rom, actor);
        queue_sprite(ram, main_rom, actor, 1);
        world_sub_wake(ram, main_rom, actor);
        duel_fire(ram, main_rom, actor);
        if (read_word(ram, 0x264) == 3) duel_health_bar(ram, main_rom, tiles, actor);
        uint16_t rival = RW(0x7c);
        int16_t ahead = (int16_t)(RW(0x21) - read_word(ram, rival + 0x21));
        if (RB(0x29) & 1) ahead = (int16_t)-ahead;
        if (ahead > 0x40) {
            WW(0x40, 0);
            WW(0x60, 1);
            WW(0, 0x93e7);
        }
        break;
    }
    case 0x093e7: {
        duel_move(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x60)) {
            uint16_t frame = RW(0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0x77b1 + frame));
            if (sprite == 0xffff) WW(0, 0x8f82);
            else {
                WW(0x2a, sprite);
                WW(0x60, read_word(main_rom, (uint16_t)(0x77c7 + frame)));
                WW(0x40, frame + 2);
                if (frame == 0x0a) RB(0x29) ^= 1;
            }
        }
        uint16_t sprite = RW(0x2a);
        submit_sprite(ram, actor, sprite, (sprite & 1) ? 0x9000 : 0x8000, RB(0x2c));
        break;
    }
    case 0x09031:
    duel_sinking: {
        if (!decrement(ram, actor + 0x60)) {
            uint16_t frame = RW(0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0x77e5 + frame));
            if (sprite == 0xffff) {
                WW(0, 0x905e);
                break;
            }
            WW(0x2a, sprite);
            WW(0x60, read_word(main_rom, (uint16_t)(0x7803 + frame)));
            WW(0x40, frame + 2);
        }
        uint16_t sprite = RW(0x2a);
        submit_sprite(ram, actor, sprite, (sprite & 1) ? 0x9000 : 0x8000, RB(0x2c));
        break;
    }
    /* ---- Duel time-up: both submarines surface together under the credits ---- */
    case 0x0905f:
    duel_draw:
        if (!decrement(ram, actor + 0x42)) {
            uint16_t shake = effect_slot(ram, actor, 0x7056);   /* 7036 */
            write_word(ram, shake + 0x60, 0x8dfd);
            write_word(ram, shake + 0x62, 0);
            write_word(ram, shake + 0x4c, 2);
            write_word(ram, shake + 0x42, 1);
            WW(0x42, 0x40);
            WW(0, 0x9082);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x09082: {
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            static const uint16_t stream[][9] = {
                /* resource, layer, [50], [52], [5A], [31], [34], [3A], [5C] */
                {0x8e, 0x1f, 0x10, 0, 0, 0, 4, 0x130, 4},
                {0x8e, 0x21, 0x10, 0, 0, 0xfffa, 0x10, 0x140, 6},
                {0x8d, 0x21, 0x10, 0x80, 0x10, 8, 0xfffc, 0x100, 3} };
            unsigned side = (RB(0x29) & 1) << 1;
            WW(0x40, 0); WW(0x58, 0); WW(0x5a, 0); WW(0x5c, 0); WW(0x5e, 0);
            WW(0x56, 4);
            WW(0x31, RW(0x21));
            WW(0x34, RW(0x24));
            WW(0x38, read_word(main_rom, 0x8e85 + (((uint16_t)(read_word(main_rom, 0x8e99 + side) - RW(0x21)) & 0x8000) ? 2 : 0)));
            WW(0x42, (world_random_byte(ram) & 0x3f) + 0xc0);
            uint16_t job = 0x9e06 + 8 * RB(0x2c);
            write_word(ram, job, read_word(main_rom, 0x78cf + (RB(0x0b) & 2)));
            write_word(ram, job + 2, 0x80);
            write_word(ram, job + 4, 0x85);
            write_word(ram, job + 6, 0x20);
            for (unsigned i = 0; i < 3; ++i) {
                uint16_t bubbles = spawn_in(ram, actor, 0, 0x92e6);
                ram[bubbles + 0x29] = RB(0x29);
                write_word(ram, bubbles + 0x10, stream[i][0]);
                ram[bubbles + 0x2d] = (uint8_t)stream[i][1];
                write_word(ram, bubbles + 0x50, stream[i][2]);
                if (i == 2) {
                    write_word(ram, bubbles + 0x52, stream[i][3]);
                    write_word(ram, bubbles + 0x5a, stream[i][4]);
                }
                write_word(ram, bubbles + 0x54, 4);
                write_word(ram, bubbles + 0x56, 4);
                write_word(ram, bubbles + 0x31, stream[i][5]);
                write_word(ram, bubbles + 0x34, stream[i][6]);
                write_word(ram, bubbles + 0x3a, stream[i][7]);
                write_word(ram, bubbles + 0x5c, stream[i][8]);
            }
            WW(0, 0x91db);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x091db:
    case 0x09203: {
        unsigned side = (RB(0x29) & 1) << 1;
        if (address == 0x091db) {
            move_actor_axis(ram, actor + 0x30, RW(0x38));
            uint16_t gap = read_word(main_rom, 0x8e99 + side) - RW(0x31);
            if (gap & 0x8000) gap = (uint16_t)-gap;
            if (gap == 4) WW(0, 0x9203);
        }
        WW(0x34, RW(0x34) + read_word(ram, 0xb71c));   /* 4372 */
        WW(0x31, RW(0x31) + read_word(ram, 0xb71a));
        WW(0x24, RW(0x24) + read_word(ram, 0xb71c));
        int16_t depth = (int16_t)(RW(0x24) - read_word(main_rom, 0x8e9d + side));
        if (depth < 0) depth = 0;
        WW(0x3a, read_word(main_rom, 0x8e89 + ((depth & 0xe0) >> 4)));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        sine_wave(ram, main_rom, actor, RW(0x24) - RW(0x34), 0x200, 8, 0x30, 0x20);
        WW(0x2a, read_word(main_rom, (uint16_t)(0x8e53 + duel_pose(ram, main_rom, actor))));
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(0x42, (world_random_byte(ram) & 0x3f) + 0xc0);
            uint16_t blast = effect_slot(ram, actor, 0x6efe);   /* 6ACB */
            write_word(ram, blast + 0x4c, 0x747f);
            write_word(ram, blast + 0x4a, 0x76c8);
            ram[blast + 0x2c] = 0x0a;
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            unsigned where = world_random_byte(ram) & 0x0c;
            write_word(ram, blast + 0x31, read_word(main_rom, 0x8ea1 + where));
            write_word(ram, blast + 0x34, read_word(main_rom, 0x8ea3 + where));
        }
        break;
    }
    case 0xa9bbd:
        if (decrement(ram, actor + 0x42)) break;
        memset(tiles, 0, 0x4000);   /* 37BB on the text layer */
        spawn_in(ram, actor, 0xa000, 0x9978);
        world_queue_sound(ram, 0x22);
        WW(0, 0x9be3);
        break;
    case 0xa9be3:
        break;
    case 0x0419c: {
        uint16_t old = RW(0x56);
        WW(0x5e, old);
        move_actor_axis(ram, actor + 0x55, 0x80);
        uint16_t delta = RW(0x56) - old;
        write_word(ram, 0xb70c, RW(0x56));
        write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
        write_word(ram, 0xb71c, delta);
        write_word(ram, 0xb72c, delta);
        write_word(ram, 0xb728, delta);
        if (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)) return WORLD_HANDLER_STOP; /* debug readout */
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        break;
    }
    case 0x041e2: {
        unsigned fraction = RB(0x55) + 0x40;
        RB(0x55) = (uint8_t)fraction;
        if (fraction > 0xff) write_word(ram, 0xb710, read_word(ram, 0xb710) + 1);
        write_word(ram, 0xb720, fraction > 0xff);
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        break;
    }
    /* ---- Duel result banner ---- */
    case 0x09423: {
        static const uint16_t part[][3] = {
            {0xa455, 0x100, 0x100}, {0x9b72, 0xf0, 0x100}, {0x9b2c, 0x100, 0x100}, {0x9be0, 0x110, 0x100},
            {0, 0x138, 0x118} };
        private_palette(ram, actor, 0x101);
        WW(0x21, 0x100);
        WW(0x24, 0x110);
        WW(0x2a, 0xa423);
        /* The original stores the delay through IY, which still holds this
         * actor: its parent spawned it as the last thing before returning. */
        WW(0x42, 0x40);
        for (unsigned i = 0; i < 5; ++i) {
            uint16_t piece = spawn_in(ram, actor, 0, 0x9508);
            ram[piece + 0x2c] = RB(0x2c);
            write_word(ram, piece + 0x2a, i == 4 ? read_word(main_rom, 0x8eb1 + (read_word(ram, 0x264) & 2)) : part[i][0]);
            write_word(ram, piece + 0x21, part[i][1]);
            write_word(ram, piece + 0x24, part[i][2]);
            write_word(ram, piece + 0x42, 0x40);
        }
        WW(0, 0x9508);
    }
    /* fall through */
    case 0x09508:
        if (RW(0x42) && decrement(ram, actor + 0x42)) break;
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* ---- Duel arena mines ---- */
    case 0xbe538:
        world_acquire_resource(ram, actor, 0x49);
        world_acquire_resource(ram, actor, 0x48);
        WW(0x42, 0x10);
        duel_mines(ram, actor);
        WW(0x42, 0x200);
        WW(0, 0xe55b);
        break;
    case 0xbe55b:
        if (!(ram[0x26f] & 0x80)) finish_animation(ram, actor);
        else duel_mines(ram, actor);
        break;
    case 0xbe57b: {
        WW(0x0e, 0x1d);
        RB(0x2d) = 0x20;
        WW(0x3c, 0xf6c3);
        WW(0x26, 0x300);
        WW(0x36, 1);
        WW(0x2e, 0x100);
        RB(0x0c) = 4;
        WW(0x44, 1);
        WW(0x52, 1);
        WW(0x21, 0x100);
        WW(0x24, (RW(0x1e) & 8) ? 0x100 - 0x88 : 0x100 + 0x88);
        unsigned slot = (RW(0x1e) & 15) << 2;
        WW(0x31, read_word(bankb, 0xe4f8 + slot));
        WW(0x34, read_word(bankb, 0xe4fa + slot));
        WW(0, 0xe5dd);
        break;
    }
    case 0xbe5dd:
        if (!(ram[0x26f] & 0x80)) finish_animation(ram, actor);
        else if (!decrement(ram, actor + 0x42)) WW(0, 0xe604);
        break;
    case 0xbe604: {
        if (!(ram[0x26f] & 0x80)) WW(0x26, 0);
        if (!decrement(ram, actor + 0x52)) {
            WW(0x52, 1);
            RB(0x28) = vector_angle(main_rom, (int16_t)(RW(0x31) - RW(0x21)), (int16_t)(RW(0x34) - RW(0x24)));
            heading_velocity(ram, main_rom, actor);
        }
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(bankb, 0xe464 + (RW(0x40) & 0x0e)));
            WW(0x44, 4);
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        RB(0x0d) = 1;
        uint16_t sub = world_player_contact(ram, main_rom, actor);
        if (sub) decrement(ram, sub + 0x26);
        else {
            RB(0x0d) = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (RW(0x26)) break;
        }
        uint16_t blast = effect_slot(ram, actor, 0x6efe);   /* 6B91 */
        write_word(ram, blast + 0x4c, 0x74e5);
        write_word(ram, blast + 0x4a, 0x7675);
        ram[blast + 0x2c] = 0x0a;
        ram[blast + 0x2d] = 0x20;
        write_word(ram, blast + 0x56, 0x40);
        write_word(ram, blast + 0x58, 0);
        world_queue_sound(ram, 0x3e);
        if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
        uint16_t keeper = read_word(ram, (uint16_t)(0x8500 + RW(8) - 2));
        write_word(ram, keeper + 0x60, read_word(ram, keeper + 0x60) & ~(1u << (RW(0x1e) & 15)));
        finish_animation(ram, actor);
        break;
    }
    /* ---- Credits scene root ---- */
    case 0x01af7:
        write_word(ram, 0x98, spawn_in(ram, actor, 0x1000, 0x5d98));
        spawn_in(ram, actor, 0, 0x3f11);
        spawn_in(ram, actor, 0, 0x3ad7);
        spawn_in(ram, actor, 0, 0x3bfa);
        spawn_in(ram, actor, 0, 0x57e3);
        write_word(ram, 0x90, spawn_in(ram, actor, 0, 0x9716));
        WW(0, 0x1b4b);
        WW(2, 0);
        break;
    /* ---- The submarine cruising through the credits ---- */
    case 0x09716: {
        RB(0x2d) = 8;
        WW(0x2a, 0x0e42);
        RB(0x2c) = main_rom[0x8c99 + ((read_word(ram, 0x264) & 2) >> 1)];
        unsigned variant = (read_word(ram, 0x272) & 3) << 2;
        WW(0, read_word(main_rom, 0x8c9b + variant));
        WW(2, read_word(main_rom, 0x8c9d + variant));
        break;
    }
    case 0x0974d:
    case 0x0977b: {
        int reverse = address == 0x0977b;
        if (reverse) RB(0x29) = 1;
        WW(0x21, 0x100);
        WW(0x24, reverse ? 0xd0 : 0xe8);
        WW(0x64, 2);
        WW(0x56, 4);
        write_word(ram, 0xa2, reverse ? 0xfef0 : 0x100);
        if (!reverse) write_word(ram, 0x264, read_word(ram, 0x264) | 4);
        if (script_begin(ram, main_rom, actor, reverse ? 0x8da3 : 0x8d0d) < 0) return WORLD_HANDLER_STOP;
        WW(0, 0x97a7);
        break;
    }
    case 0x097a7: {
        if (script_step(ram, main_rom, actor) < 0) return WORLD_HANDLER_STOP;
        if (outside_field(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t sprite = RW(0x2a);
        submit_sprite(ram, actor, sprite, (sprite & 1) ? 0x9000 : 0x8000, RB(0x2c));
        int16_t speed = (int16_t)RW(0x38);
        if (RB(0x29) & 1) speed = (int16_t)-speed;
        if (speed >= 1 && !decrement(ram, actor + 0x64)) {
            WW(0x64, 6);
            uint16_t wake = spawn_in(ram, actor, 0, 0x7f73);
            uint16_t pose = RW(0x56) << 1;
            uint16_t dx = read_word(main_rom, (uint16_t)(0x78b3 + pose));
            ram[wake + 0x29] = RB(0x29);
            if (RB(0x29) & 1) dx = (uint16_t)-dx;
            write_word(ram, wake + 0x21, dx + RW(0x21));
            write_word(ram, wake + 0x24, RW(0x24) + read_word(main_rom, (uint16_t)(0x78b5 + pose)));
            write_word(ram, wake + 0x7c, actor);
        }
        break;
    }
    case 0x09858:
        finish_animation(ram, actor);
        break;
    /* ---- Credits camera: the two scrolling layers ---- */
    case 0x04276: {
        uint16_t old = RW(0x52);
        WW(0x5a, old);
        move_actor_axis(ram, actor + 0x51, read_word(ram, 0xa2));
        uint16_t delta = old - RW(0x52);
        write_word(ram, 0xb70a, RW(0x52));
        write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
        write_word(ram, 0xb71a, delta);
        write_word(ram, 0xb72a, delta);
        write_word(ram, 0xb726, delta);
        if ((int16_t)RW(0x6a) <= (int16_t)read_word(ram, 0xb700)) {
            uint16_t source = RW(0x42), dest = RW(0x44);
            uint16_t base = (uint16_t)((uint32_t)RW(0x60) << 4);
            for (unsigned block = 0; block < 11; block++) {
                world_draw_tile_block(tiles, main_rom, base, dest, read_word(banka, (uint16_t)(source + 2 * block)));
                dest = (dest + 0x400) & 0x3fff;
            }
            WW(0x42, source - RW(0x4e));
            RB(0x44) -= 0x10;
            WW(0x6a, RW(0x6a) + 0x20);
        }
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        if (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)) return WORLD_HANDLER_STOP; /* debug readout */
        break;
    }
    case 0x042d5: {
        uint16_t old = RW(0x52);
        WW(0x5a, old);
        move_actor_axis(ram, actor + 0x51, (uint16_t)((int16_t)read_word(ram, 0xa2) >> 1));
        write_word(ram, 0xb70e, RW(0x52));
        write_word(ram, 0xb71e, old - RW(0x52));
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        break;
    }
    /* ---- Second ending: the camera shakes while the base collapses ---- */
    case 0x041f6: {
        uint16_t old = RW(0x52);
        WW(0x5a, old);
        move_actor_axis(ram, actor + 0x51, 0x140);
        uint16_t delta = RW(0x52) - old;
        write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
        write_word(ram, 0xb71a, -delta);
        write_word(ram, 0xb72a, -delta);
        write_word(ram, 0xb726, -delta);
        write_word(ram, 0xb70a, (world_random_byte(ram) & 2) - 1 + RW(0x52));
        write_word(ram, 0xb70c, (world_random_byte(ram) & 1) - 1);
        if (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)) return WORLD_HANDLER_STOP; /* debug readout */
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        break;
    }
    case 0x0424c: {
        uint16_t old = RW(0x52);
        WW(0x5a, old);
        move_actor_axis(ram, actor + 0x51, 0xa0);
        write_word(ram, 0xb70e, RW(0x52));
        write_word(ram, 0xb71e, old - RW(0x52));
        write_word(ram, 0xb710, read_word(ram, 0xb70c));
        if ((int16_t)RW(0x46) <= (int16_t)read_word(ram, 0xb700)) load_scroll_script(ram, main_rom, actor);
        break;
    }
    /* ---- Second ending: bubble streams ---- */
    case 0x09285: {
        uint16_t stream = spawn_in(ram, actor, 0, 0x92e6);
        write_word(ram, stream + 0x10, read_word(main_rom, 0x8edd + (RW(0x1e) & 2)));
        ram[stream + 0x2d] = 0x1f;
        write_word(ram, stream + 0x50, 0x10);
        write_word(ram, stream + 0x54, 0x40);
        write_word(ram, stream + 0x56, 0x40);
        write_word(ram, stream + 0x5a, 1);
        write_word(ram, stream + 0x3a, 0);
        write_word(ram, stream + 0x5c, 4);
        WW(0, 0x92c7);
        break;
    }
    case 0x092c7:
        scroll_actor(ram, actor, 0xb71a);
        if (RW(0x21) < 0x40) finish_animation(ram, actor);
        break;
    case 0x092e6:
        world_acquire_resource(ram, actor, RW(0x10));
        WW(0, 0x92f3);
    /* fall through */
    case 0x092f3: {
        if (RW(0x50)) {
            if (decrement(ram, actor + 0x50)) break;
            WW(0x58, RW(0x5a));
        } else {
            if (decrement(ram, actor + 0x54)) break;
            WW(0x54, RW(0x56));
            if (!decrement(ram, actor + 0x58)) WW(0x50, RW(0x52));
        }
        uint16_t bubble = spawn_in(ram, actor, 0, 0x93a0);
        uint16_t source = read_word(ram, (uint16_t)(0x8500 + RW(8) - 2));
        uint16_t dx = RW(0x31);
        if (RB(0x29) & 1) dx = (uint16_t)-dx;
        write_word(ram, bubble + 0x21, dx + read_word(ram, source + 0x21));
        write_word(ram, bubble + 0x24, read_word(ram, source + 0x24) + RW(0x34));
        ram[bubble + 0x2c] = RB(0x2c);
        ram[bubble + 0x2d] = RB(0x2d);
        write_word(ram, bubble + 0x3a, RW(0x3a));
        write_word(ram, bubble + 0x5c, RW(0x5c));
        write_word(ram, bubble + 0x40, 0);
        write_word(ram, bubble + 0x60, 1);
        break;
    }
    case 0x09377:
        world_acquire_resource(ram, actor, read_word(main_rom, 0x8edd + (RW(0x1e) & 2)));
        RB(0x2d) = 0x21;
        WW(0x3a, 0x40);
        WW(0x60, 1);
        WW(0x5c, 2);
        WW(0, 0x93a0);
    /* fall through */
    case 0x093a0:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (!decrement(ram, actor + 0x60)) {
            WW(0x2a, read_word(main_rom, (uint16_t)(0x9a73 + RW(0x40) + 0x20)));
            WW(0x60, RW(0x5c));
            WW(0x40, RW(0x40) + 2);
            if (RW(0x40) >= 0x20) {
                finish_animation(ram, actor);
                break;
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* ---- Second ending: the escaping submarine and drifting wreckage ---- */
    case 0x0986c: {
        unsigned side = read_word(ram, 0x264) & 2;
        unsigned slot = main_rom[0x8c99 + (side >> 1)];
        RB(0x2c) = slot;
        uint16_t job = 0x9e06 + 8 * slot;
        write_word(ram, job, read_word(main_rom, 0x78cf + side));
        write_word(ram, job + 2, 0x80);
        write_word(ram, job + 4, 0x85);
        write_word(ram, job + 6, 0x20);
        RB(0x2d) = 0x20;
        WW(0x2a, 0x45d9);
        uint16_t stream = spawn_in(ram, actor, 0, 0x92e6);
        ram[stream + 0x29] = RB(0x29);
        write_word(ram, stream + 0x10, 0x8d);
        ram[stream + 0x2d] = 0x1f;
        write_word(ram, stream + 0x50, 0x10);
        write_word(ram, stream + 0x52, 0x80);
        write_word(ram, stream + 0x54, 4);
        write_word(ram, stream + 0x56, 0x0c);
        write_word(ram, stream + 0x5a, 0);
        write_word(ram, stream + 0x31, 4);
        write_word(ram, stream + 0x34, 0xfffc);
        write_word(ram, stream + 0x3a, 0x80);
        write_word(ram, stream + 0x5c, 3);
        WW(0, 0x9900);
        break;
    }
    case 0x0990b: {
        unsigned which = RW(0x1e) & 6;
        WW(0x2a, read_word(main_rom, 0x8ec5 + which));
        world_acquire_resource(ram, actor, read_word(main_rom, 0x8ecd + which));
        RB(0x2d) = 0x20;
        RB(0x29) = RB(0x1f);
        WW(0x3c, 0xfb4b);
        WW(0, 0x9900);
        break;
    }
    case 0x09900: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t sprite = RW(0x2a);
        submit_sprite(ram, actor, sprite, (sprite & 1) ? 0x9000 : 0x8000, RB(0x2c));
        break;
    }
    case 0xa82c4:
        world_acquire_resource(ram, actor, 0xe4);
        RB(0x2d) = 4;
        WW(0x2a, read_word(banka, 0x82c0 + (RW(0x1e) & 2)));
        WW(0, 0x82e4);
    /* fall through */
    case 0xa82e4:
        scroll_actor(ram, actor, 0xb71a);
        if (RW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* ---- Second ending: darkened backdrop and falling debris ---- */
    case 0xa9c91:
        write_word(ram, 0xb8ca, 0x800);
        for (unsigned n = 0; n < 0x200; ++n)
            write_word(tiles, 0x8000 + 0x1600 + 4 * n + 2, read_word(tiles, 0x8000 + 0x1600 + 4 * n + 2) - 1);
        for (unsigned n = 0; n < 0x80; ++n)
            write_word(tiles, 0x4000 + 0x1600 + 4 * n + 2,
                       (read_word(tiles, 0x4000 + 0x1600 + 4 * n + 2) & 0x7ff) | 0x2000);
        finish_animation(ram, actor);
        break;
    case 0xa9cdd:
        WW(0x42, 0x40);
        WW(0, 0x9ce7);
    /* fall through */
    case 0xa9ce7: {
        if (read_word(ram, 0xb700) >= 0xc80) {
            finish_animation(ram, actor);
            break;
        }
        if (decrement(ram, actor + 0x42)) break;
        uint16_t rock = spawn_in(ram, actor, 0x1000, 0xf4a9);
        uint16_t x = world_random_byte(ram) + 0x80;
        x += world_random_byte(ram) & 0x3f;
        write_word(ram, rock + 0x21, x);
        write_word(ram, rock + 0x24, 0x180);
        write_word(ram, rock + 0x1e, world_random_byte(ram) & 7);
        ram[rock + 0x28] = (uint8_t)((world_random_byte(ram) & 7) + 0xbc);
        write_word(ram, rock + 0x2e, (world_random_byte(ram) & 0xf0) << 1);
        WW(0x42, 0x10);
        break;
    }
    /* ---- Credits text roll ---- */
    case 0x0558f:
        if (read_word(ram, 0x26e) & 0x8000) break;
        private_palette(ram, actor, 0x101);
        RB(0x48) = RB(0x2c);
        private_palette(ram, actor, 0x107);
        RB(0x49) = RB(0x2c);
        WW(0x40, 0x2f30);
        WW(0x42, 0x5055);
        RB(0x44) = 8;
        RB(0x46) = 8;
        WW(0x38, 0x3400000u / read_word(main_rom, (uint16_t)(0x5587 + 2 * read_word(ram, 0x272))));
        WW(0, 0x55df);
    /* fall through */
    case 0x055df: {
        unsigned sum = RW(0x3a) + RW(0x38);
        WW(0x3a, sum);
        if (sum < 0x10000) break;
        write_word(ram, 0xb708, (read_word(ram, 0xb708) + 1) & 0x1ff);
        if (--RB(0x44)) break;
        uint16_t text = RW(0x42);
        if (main_rom[text] >= 8) {
            RB(0x46) = main_rom[text];
            WW(0x42, ++text);
        }
        RB(0x44) = RB(0x46);
        if (RB(0x46) == 0xfd) {
            WW(0x6c, 0xb4);
            world_queue_sound(ram, 0x22);
            WW(0, 0x56a3);
            if (!decrement(ram, actor + 0x6c)) write_word(ram, 0x86, 1);
            break;
        }
        RB(0x41) = (RB(0x41) + 1) & 0x3f;
        uint16_t pos = RW(0x40);
        for (unsigned row = 0; row < 2; ++row) {
            for (unsigned n = 0; n < 0x28; ++n) {
                far_store(ram, tiles, RW(0x60), pos, 0);
                far_store(ram, tiles, RW(0x60), pos + 2, 0);
                pos = (pos & 0xff00) | ((pos + 4) & 255);
            }
            pos = (RW(0x40) & 0x00ff) | (((RW(0x40) >> 8) + 1) & 0x3f) << 8;
        }
        pos = RW(0x40);
        do {
            uint16_t attr = 0x100 | RB(0x48 + (read_word(main_rom, text) & 3));
            ++text;
            text = RB(0x46) == 0x10 ? big_text(tiles, main_rom, &pos, text, attr)
                                    : small_text(tiles, main_rom, &pos, text, attr);
            ++text;
        } while (main_rom[text] != 0xfe);
        WW(0x42, text + 1);
        if (RB(0x46) == 0x10) RB(0x41) = (RB(0x41) + 1) & 0x3f;
        break;
    }
    case 0x056a3:
        if (!decrement(ram, actor + 0x6c)) write_word(ram, 0x86, 1);
        break;
    /* ---- Credits scenery: drifting sprites and the palette cue ---- */
    case 0xa9c1f:
        RB(0x2c) = banka[0x9910 + (read_word(ram, 0x272) & 1)];
        WW(0x44, 1);
        RB(0x2d) = RB(0x1f);
        WW(0x2a, read_word(banka, 0x9912 + (RW(0x1e) & 0x10)));
        WW(0x34, RW(0x24));
        WW(0, 0x9c54);
    /* fall through */
    case 0xa9c54:
        scroll_actor(ram, actor, 0xb71a);
        WW(0x21, RW(0x21) & 0x1ff);
        WW(0x24, read_word(ram, 0xb70c) + RW(0x34));
        if (!decrement(ram, actor + 0x44)) {
            WW(0x2a, read_word(banka, 0x9912 + (RW(0x40) & 0x0e) + (RW(0x1e) & 0x10)));
            WW(0x44, 8);
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa9d63: {
        unsigned which = RW(0x1e) & 6;
        palette_job(ram, read_word(banka, 0x993e + which), read_word(banka, 0x9932 + which),
                    read_word(banka, 0x9938 + which));
        finish_animation(ram, actor);
        break;
    }
    /* ---- Credits cast: schools of fish ---- */
    case 0x7d7b9: {
        unsigned row = (RW(0x1e) & 0x30) >> 1;
        for (unsigned n = 0; n < 4; ++n, row += 2) {
            uint16_t fish = spawn_in(ram, actor, 0x7000, 0xd734);
            write_word(ram, fish + 0x24, RW(0x24));
            ram[fish + 0x1e] = bank7[0xcc89 + row];
            write_word(ram, fish + 0x21, (int8_t)bank7[0xcc8a + row] + RW(0x21));
            write_word(ram, fish + 0x44, 2 * n + 1);
        }
        finish_animation(ram, actor);
        break;
    }
    case 0x7d734:
    case 0x7d73c:
        if (address == 0x7d734) WW(0x2a, 0x4163);
        else WW(0x44, 1);
        world_acquire_resource(ram, actor, 0xd5);
        RB(0x2d) = 0x28;
        WW(0x40, 0);
        WW(0, 0xd757);
    /* fall through */
    case 0x7d757:
        if (!decrement(ram, actor + 0x44)) {
            unsigned kind = RW(0x1e) & 6;
            uint16_t frame = RW(0x40) + 2;
            if (frame >= 8 && (kind || frame >= 10)) frame = 0;
            WW(0x40, frame);
            uint16_t row = read_word(bank7, 0xcbdf + kind) + frame;
            WW(0x2a, read_word(bank7, (uint16_t)(row + 0xcbbd)));
            WW(0x44, read_word(bank7, (uint16_t)(row + 0xcbe7)));
        }
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (RW(0x21) > 0x1c0) finish_animation(ram, actor);
        break;
    case 0x7d813:
        RB(0x2c) = 0x1d;
        RB(0x2d) = 0x28;
        WW(0x3c, 0xfb4b);
        WW(0x2a, read_word(bank7, 0xcc59 + (RW(0x1e) & 0x1e)));
        WW(0, 0xd834);
    /* fall through */
    case 0x7d834:
        scroll_actor(ram, actor, 0xb71a);
        if (RW(0x21) > 0x1c0 || RW(0x21) < 0x40) finish_animation(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* ---- Credits cast: the leaping creature ---- */
    case 0x7d860: {
        RB(0x2c) = 0x1d;
        RB(0x2d) = 0x28;
        WW(0x40, 0);
        WW(0x2a, read_word(bank7, 0xcc29 + ((RW(0x1e) & 2) << 1)));
        WW(0x38, read_word(bank7, 0xcba0 + (bank7[0xcc0c] & 0x0e)));
        uint16_t row = 0;
        do {
            uint16_t part = spawn_in(ram, actor, 0x7000, 0xd5b0);
            write_word(ram, part + 0x21, RW(0x21) + read_word(bank7, (uint16_t)(0xcc09 + row)));
            write_word(ram, part + 0x24, RW(0x24));
            write_word(ram, part + 0x1e, read_word(bank7, (uint16_t)(0xcc0b + row)));
            row += 4;
        } while (read_word(bank7, (uint16_t)(0xcc09 + row)) != 0xffff);
        WW(0x34, RW(0x24) + 8);
        WW(0x42, 0x14);
        WW(0, 0xd8d6);
        break;
    }
    case 0x7d8d6:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            RB(0x0b) |= 1;
            WW(0x42, 0x1e);
            WW(0, 0xd908);
        }
        WW(0x24, RW(0x34) + ((RB(0x42) & 4) ? 2 : 0));
        break;
    case 0x7d908:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) WW(0, 0xd91d);
        break;
    case 0x7d91d:
        WW(0x3a, read_word(bank7, (uint16_t)(0xcc1b + RW(0x40) + (RW(0x1e) & 2))));
        WW(0, 0xd935);
    /* fall through */
    case 0x7d935: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        uint16_t height = RW(0x24) >= RW(0x34) ? RW(0x24) - RW(0x34) : 0;
        uint16_t row = (RW(0x1e) & 2) << 1;
        while (height >= read_word(bank7, (uint16_t)(0xcc27 + row))) row += 8;
        WW(0x2a, read_word(bank7, (uint16_t)(0xcc29 + row)));
        queue_sprite(ram, main_rom, actor, 0);
        WW(0x3a, RW(0x3a) - 0x14);
        if (RW(0x24) >= RW(0x34)) break;
        WW(0x40, RW(0x40) + 4);
        if (read_word(bank7, (uint16_t)(0xcc1b + RW(0x40) + (RW(0x1e) & 2))) != 0xffff) {
            WW(0x42, 0x1e);
            WW(0, 0xd908);
            WW(0x24, RW(0x34) + 2);
        } else WW(0, 0xd970);
        break;
    }
    case 0x7d970: {
        WW(0x3a, RW(0x3a) - 0x14);
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        queue_sprite(ram, main_rom, actor, 0);
        if (terrain_material(ram, tiles, RW(0x21), RW(0x24)) != 4) break;
        uint16_t splash = effect_slot(ram, actor, 0x6f54);
        write_word(ram, splash + 0x21, RW(0x21));
        write_word(ram, splash + 0x24, RW(0x24));
        write_word(ram, splash + 0x4c, 0x7563);
        write_word(ram, splash + 0x4a, 0x768b);
        ram[splash + 0x2c] = 0x0a;
        ram[splash + 0x2d] = 0x20;
        write_word(ram, splash + 0x56, 0x30);
        write_word(ram, splash + 0x58, 0x80);
        {   /* 6920: snap to the water row below. */
            uint16_t phase = read_word(ram, 0xb70c) & 7;
            write_word(ram, splash + 0x24, read_word(ram, splash + 0x24) - 1 - phase);
            write_word(ram, splash + 0x23, read_word(ram, splash + 0x23) & 0xf800);
            write_word(ram, splash + 0x24, read_word(ram, splash + 0x24) + 8 + phase);
        }
        WW(0x3a, 0xffa0);
        WW(0x40, 0);
        WW(0x42, read_word(bank7, 0xcc47));
        WW(0x2a, read_word(bank7, 0xcc49 + (RW(0x1e) & 2)));
        WW(0, 0xd9be);
        break;
    }
    case 0x7d9be:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(0x40, RW(0x40) + 6);
            uint16_t row = RW(0x40);
            WW(0x42, read_word(bank7, (uint16_t)(0xcc47 + row)));
            WW(0x2a, read_word(bank7, (uint16_t)(0xcc47 + row + (RW(0x1e) & 2) + 2)));
        }
        if (RW(0x21) > 0x1c0) finish_animation(ram, actor);
        break;
    case 0x7da10:
        RB(0x28) = RB(0x1e);
        WW(0x2e, read_word(bank7, 0xcc79 + ((RB(0x1f) & 7) << 1)));
        heading_velocity(ram, main_rom, actor);
        WW(0x3c, 0xfb4b);
        WW(0, 0xda36);
    /* fall through */
    case 0x7da36:
        if (RW(0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (outside_field(ram, actor)) finish_animation(ram, actor);
        break;
    /* ---- Credits cast: the swimmer following a script ---- */
    case 0xb33d6: {
        world_acquire_resource(ram, actor, 4);
        unsigned which = RW(0x1e) & 4;
        uint16_t frames = read_word(bankb, 0x29da + which);
        WW(0x6e, frames);
        if (script_begin(ram, main_rom, actor, read_word(bankb, 0x29dc + which)) < 0) return WORLD_HANDLER_STOP;
        WW(0x42, read_word(bankb, frames));
        WW(0x4c, read_word(bankb, (uint16_t)(frames + 2)));
        RB(0x29) = bankb[(uint16_t)(frames + 4)];
        WW(0x40, 0);
        WW(0x4a, 0);
        WW(0, 0x341d);
    }
    /* fall through */
    case 0xb341d: {
        if (script_step(ram, main_rom, actor) < 0) return WORLD_HANDLER_STOP;
        uint16_t bottom = (read_word(ram, 0x26e) & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
        if (RW(0x24) > bottom) finish_animation(ram, actor);
        if (!decrement(ram, actor + 0x42)) {
            WW(0x40, RW(0x40) + 6);
            uint16_t frames = RW(0x6e) + RW(0x40);
            WW(0x42, read_word(bankb, frames));
            WW(0x4c, read_word(bankb, (uint16_t)(frames + 2)));
            RB(0x29) = bankb[(uint16_t)(frames + 4)];
        }
        WW(0x2a, RW(0x4c));
        queue_sprite(ram, main_rom, actor, 1);
        WW(0x48, RW(0x48) + 2);
        if ((int16_t)RW(0x48) >= 6) WW(0x48, 0);
        WW(0x2a, read_word(bankb, (uint16_t)(0x2790 + RW(0x48) + RW(0x4a))));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* ---- Credits cast of the duel winner's ending: a scripted swimmer with a wake ---- */
    case 0xb9f34: {
        world_acquire_resource(ram, actor, 0x60);
        unsigned which = RW(0x1e) & 4;
        uint16_t frames = read_word(bankb, 0x9dee + which);
        WW(0x60, frames);
        if (script_begin(ram, main_rom, actor, read_word(bankb, 0x9dec + which)) < 0) return WORLD_HANDLER_STOP;
        WW(0x42, read_word(bankb, frames));
        WW(0x2a, read_word(bankb, (uint16_t)(frames + 2)));
        RB(0x29) = bankb[(uint16_t)(frames + 4)];
        RB(0x62) = 1;
        WW(0x3c, 0xfb4b);
        WW(0, 0x9f7b);
        break;
    }
    case 0xb9f7b: {
        if (script_step(ram, main_rom, actor) < 0) return WORLD_HANDLER_STOP;
        queue_sprite(ram, main_rom, actor, 0);
        if (RW(0x38) && !--RB(0x62)) {
            RB(0x62) = 8;
            uint16_t wake = effect_slot(ram, read_word(ram, 0x9a), 0x7f73);   /* 17C3 */
            uint8_t facing = RB(0x29) ^ 1;
            ram[wake + 0x29] = facing;
            write_word(ram, wake + 0x21, RW(0x21) + read_word(bankb, 0x902e + 2 * (facing & 1)));
            write_word(ram, wake + 0x24, RW(0x24));
        }
        if (!decrement(ram, actor + 0x42)) {
            WW(0x60, RW(0x60) + 6);
            uint16_t frames = RW(0x60);
            WW(0x42, read_word(bankb, frames));
            WW(0x2a, read_word(bankb, (uint16_t)(frames + 2)));
            RB(0x29) = bankb[(uint16_t)(frames + 4)];
        }
        break;
    }
    /* ---- Ranking screen after the ending: portrait and the flashing row ---- */
    case 0x0ca03: {
        unsigned variant = read_word(ram, 0x272) & 3;
        RB(0x2c) = (uint8_t)(main_rom[0xd1ad + variant] + ((read_word(ram, 0x264) & 2) >> 1));
        WW(0x2a, read_word(main_rom, 0xd1b1 + 2 * variant));
        WW(0x21, read_word(main_rom, 0xd1b9 + 2 * variant));
        WW(0x24, read_word(main_rom, 0xd1c1 + 2 * variant));
        WW(0, 0xca3e);
    }
    /* fall through */
    case 0x0ca3e:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* ---- Flame stream fired by the stage 3 walker (seen in the attract demo) ---- */
    case 0x0baa5:
        WW(0x42, 1);
        WW(0, 0xbaaf);
    /* fall through */
    case 0x0baaf: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t flame = world_spawn_child(ram, actor, 0xbb40);
        if (flame) {
            world_queue_sound(ram, 0x3b);
            uint16_t source = read_word(ram, (uint16_t)(0x8500 + RW(8) - 2));
            ram[flame + 0x29] = ram[source + 0x29];
            write_word(ram, flame + 0x21, read_word(ram, source + 0x21) +
                       read_word(main_rom, 0xba95 + 2 * (ram[source + 0x29] & 1)));
            write_word(ram, flame + 0x24, read_word(ram, source + 0x24) + 4);
            ram[flame + 0x28] = (uint8_t)((ram[source + 0x28] + 4) & 0xf8);
            ram[flame + 0x2d] = RB(0x2d);
            ram[flame + 0x0b] = RB(0x0b);
            write_word(ram, flame + 0x7a, RW(0x58));
            write_word(ram, flame + 0x54, RW(0x54));
            WW(0x58, RW(0x58) + 1);
            WW(0x54, RW(0x54) + 1);
        }
        if (!decrement(ram, actor + 0x7a)) finish_animation(ram, actor);
        else WW(0x42, 4);
        break;
    }
    case 0x0bb40:
    case 0x0bb7e: {
        if (address == 0x0bb40) {
            world_acquire_resource(ram, actor, 0x5a);
            WW(0x3c, 0xba7d);
            WW(0x26, 1);
            WW(0x36, 0x60);
            WW(0x2e, 0x600);
            heading_velocity(ram, main_rom, actor);
            WW(0x4e, (uint8_t)(RB(0x28) - 0x28) & 0xf8);
            WW(0x58, 1);
            WW(0x42, 0x0c);
            WW(0, 0xbb7e);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        move_actor_axis(ram, actor + 0x23, RW(0x3a));
        if (!decrement(ram, actor + 0x58)) {
            unsigned frame = RW(0x40) & 6;
            WW(0x58, read_word(main_rom, 0xb9cb + frame));
            WW(0x2a, read_word(main_rom, (uint16_t)(0xb993 + frame + RW(0x4e))));
            WW(0x40, RW(0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        int spent = !RW(0x26);
        if (!spent) {
            if (RW(0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
            if (!RW(0x42)) {
                if ((int16_t)RW(0x24) >= (int16_t)read_word(main_rom, 0xba99 + ((RW(0x7a) & 3) << 1))) spent = 1;
            } else decrement(ram, actor + 0x42);
            if (!spent && ((1u << (terrain_material(ram, tiles, RW(0x21), RW(0x24)) & 15)) & 0x5502)) spent = 1;
        }
        if (spent) {
            WW(0x36, 0x0c);
            WW(0x3c, 0xba8d);
            WW(0x40, 0);
            WW(0x58, 1);
            WW(0x42, 0x29);
            WW(0, 0xbc14);
        }
        break;
    }
    case 0x0bc14:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = RW(0x40);
            uint16_t sprite = read_word(main_rom, (uint16_t)(0xb9d3 + frame));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(0x2a, sprite);
            WW(0x58, read_word(main_rom, (uint16_t)(0xb9ff + frame)));
            WW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (RW(0x42)) {
            decrement(ram, actor + 0x42);
            WW(0x26, 0x7f00);
            if (!((read_word(ram, 0x274) & 3) ^ RW(0x54)) && RW(0x3c) != 0xffff)
                world_append_candidate(ram + 0xaee6, actor);
        }
        break;
    /* ---- Player's fire-trail secondary weapon (seen in the attract demo and two-player games) ---- */
    case 0x0abb7:
    case 0x0ac0e:
    case 0x0ac65: {
        if (address == 0x0abb7) {
            world_acquire_resource(ram, actor, read_word(main_rom, 0x9bb1 + (RB(0x0b) & 2)));
            WW(0x3c, 0xaba3);
            unsigned level = RW(0x76) << 2;
            WW(0x26, read_word(main_rom, (uint16_t)(0xabab + level)));
            WW(0x6e, RW(0x26));
            WW(0x36, read_word(main_rom, (uint16_t)(0xabad + level)));
            WW(0x38, read_word(main_rom, 0xab8f + ((RB(0x29) & 1) << 1)));
            WW(0x58, 1);
            WW(0x5a, 7);
            WW(0x5c, 8);
            WW(0, 0xac0e);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(0x38));
        if (address != 0x0ac65) {
            if (!decrement(ram, actor + 0x58)) {
                uint16_t frame = RW(0x40);
                uint16_t sprite = read_word(main_rom, (uint16_t)(0xa9ab + frame));
                if (sprite == 0xffff) {
                    WW(0x38, RW(0x38) << 1);
                    WW(0x40, 0);
                    WW(0x42, 8);
                    WW(0x58, 0x2b);
                    WW(0x5a, 3);
                    WW(0x5c, 4);
                    WW(0, 0xac65);
                } else {
                    WW(0x2a, sprite);
                    WW(0x58, read_word(main_rom, (uint16_t)(0xa9b7 + frame)));
                    WW(0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            WW(0x40, RW(0x40) + RW(0x58));
            WW(0x2a, read_word(main_rom, 0xa9c1 + ((RB(0x41) & 3) << 1)));
            queue_sprite(ram, main_rom, actor, 1);
            if (RW(0x42) && !decrement(ram, actor + 0x42)) {
                WW(0x38, RW(0x38) << 1);
                WW(0x58, 0x80);
                WW(0x5a, 2);
                WW(0x5c, 2);
                WW(0x0e, 0x120);
            }
        }
        if (!decrement(ram, actor + 0x5a)) {
            WW(0x5a, RW(0x5c));
            uint16_t flame = spawn_in(ram, actor, 0, 0xad5f);
            write_word(ram, flame + 0x21, RW(0x21));
            write_word(ram, flame + 0x24, RW(0x24));
            ram[flame + 0x0b] = RB(0x0b);
            ram[flame + 0x2d] = RB(0x2d) + 1;
            unsigned level = (RW(0x76) & 3) << 1;
            write_word(ram, flame + 0x76, read_word(main_rom, 0xaa4f + level));
            write_word(ram, flame + 0x6c, level << 4);
            write_word(ram, flame + 0x6a, RW(0x6a));
            WW(0x6a, RW(0x6a) + 1);
        }
        int gone = 0;
        if (!RW(0x26)) {
            spawn_impact(ram, main_rom, actor);
            gone = 1;
        } else {
            WW(0x26, RW(0x6e));
            if ((int16_t)RW(0x21) > 0x200 || (int16_t)RW(0x21) < 0) gone = 1;
            else {
                unsigned material = terrain_material(ram, tiles, RW(0x21) + 0x10, RW(0x24));
                if ((1u << (material & 15)) & 0x5502) {
                    /* 0A3B7 opens a stage 5 wall cell here: not recovered for this weapon. */
                    if (read_word(ram, 0x84) == 4 && material == 1) return WORLD_HANDLER_STOP;
                    spawn_impact(ram, main_rom, actor);
                    gone = 1;
                }
            }
        }
        if (gone) {
            uint16_t owner = read_word(ram, (uint16_t)(0x8500 + RW(8) - 2));
            write_word(ram, owner + 0x46, read_word(ram, owner + 0x46) + 1);
            finish_animation(ram, actor);
        } else if (RW(0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0x0ad5f:
        world_acquire_resource(ram, actor, 0x58);
        WW(0x3c, 0xffff);
        WW(0x58, 1);
        WW(0x0e, 0x121);
        WW(0x36, 0x10);
        WW(0, 0xad81);
        break;
    case 0x0ad81:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = RW(0x40);
            WW(0x58, read_word(main_rom, (uint16_t)(0xaa2f + frame)));
            uint16_t sprite = read_word(main_rom, (uint16_t)(0xa9c9 + frame + RW(0x76)));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(0x2a, sprite);
            WW(0x3c, read_word(main_rom, (uint16_t)(0xaa57 + frame + RW(0x6c))));
            WW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if ((RW(0x6a) & 1) && !(read_word(ram, 0x274) & ((RB(0x0b) & 2) + 1))) {
            WW(0x26, 0x7f00);
            if (RW(0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    /* ---- Ranking screen backdrops of an ordinary game over ---- */
    case 0x0c97a:
        tile_columns(ram, tiles, main_rom, 0x8000, 0x2130, 0x5ab4, 10, 8);
        finish_animation(ram, actor);
        break;
    case 0x0c99f:
    case 0x0c9d1:
        RB(0x2c) = address == 0x0c99f ? 0x10 : 0x0e;
        WW(0x2a, 0x966f);
        WW(0x24, address == 0x0c99f ? 0xd0 : 0x110);
        WW(0, address + 0x13);
    /* fall through */
    case 0x0c9b2:
    case 0x0c9e4:
        for (unsigned x = 0x80; x <= 0x180; x += 0x80) {
            WW(0x21, x);
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    case 0x0cd9c:
        RB(0x2c) = (uint8_t)(RW(0x4e) - 1 + 0x132);
        WW(0x40, 0);
        WW(0x42, 0x24);
        WW(0, 0xcdb5);
    /* fall through */
    case 0x0cdb5:
        if (!decrement(ram, actor + 0x42)) {
            palette_job(ram, RB(0x2c), read_word(main_rom, 0xcead + (RW(0x40) & 4) + ((RW(0x1e) & 1) << 1)), 5);
            WW(0x42, 0x24);
            WW(0x40, RW(0x40) + 4);
        }
        if (read_word(ram, 0x262) & 0x8000) finish_animation(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
