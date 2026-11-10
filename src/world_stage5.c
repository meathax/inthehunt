/* Recovered World actor handlers for stage 5 (stage index 4).
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

#define RW(o) read_word(ram, (uint16_t)(o))
#define WW(o, v) write_word(ram, (uint16_t)(o), (uint16_t)(v))
#define CW(o) read_word(cs, (uint16_t)(o))
#define SET(o, mask) WW((o), RW(o) | (mask))
#define CLR(o, mask) WW((o), RW(o) & ~(unsigned)(mask))

/* The stage's code segment is 7000; its tables are read through it. */
#define STAGE_SEGMENT 0x7000u

static uint16_t parent_of(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2));
}

/* 1769 with a far handler. A failed allocation leaves the discard slot in IY. */
static uint16_t spawn_far(uint8_t *ram, uint16_t parent, uint16_t segment, uint16_t handler)
{
    uint16_t child = world_spawn_child(ram, parent, handler);
    if (!child) return 0x8480;
    write_word(ram, child + 2, segment);
    return child;
}

/* 17C3 (list 9A) and 1812 (list 9C): the new actor follows a fixed list head. */
static uint16_t spawn_after_head(uint8_t *ram, uint16_t head_pointer, uint16_t segment, uint16_t handler)
{
    uint16_t remaining = read_word(ram, 0x8b04) - 1;
    if (remaining & 0x8000) {
        ram[0x269] |= 0x80;
        return 0x8480;
    }
    write_word(ram, 0x8b04, remaining);
    uint16_t cursor = read_word(ram, 0x8b00);
    uint16_t child = read_word(ram, 0x8900 + cursor);
    memset(ram + child, 0, 128);
    write_word(ram, child, handler);
    write_word(ram, child + 2, segment);
    write_word(ram, 0x8b00, (cursor + 2) & 0x1fe);
    uint16_t head = read_word(ram, head_pointer);
    uint16_t next = read_word(ram, head + 4);
    write_word(ram, child + 4, next);
    write_word(ram, head + 4, child);
    write_word(ram, child + 6, head);
    write_word(ram, next + 6, child);
    write_word(ram, child + 8, read_word(ram, head + 8) + 2);
    return child;
}

/* 3665: the second sound command ring. */
static void queue_sound_b(uint8_t *ram, uint16_t command)
{
    uint16_t tail = (read_word(ram, 0xa6d8) + 2) & 14;
    if (tail != read_word(ram, 0xa6d6)) {
        write_word(ram, 0xa6d8, tail);
        write_word(ram, 0xa6a2 + tail, command);
    }
}

static void palette_job(uint8_t *ram, uint16_t job, uint16_t palette, uint16_t speed)
{
    write_word(ram, job, palette);
    write_word(ram, job + 2, 0x80);
    write_word(ram, job + 4, speed);
    ram[job + 6] = 0x20;
    ram[job + 7] = 0x10;
}

/* The shared "left the bottom of the view" test. */
static int below_view(const uint8_t *ram, uint16_t actor)
{
    int16_t limit = (read_word(ram, 0x26e) & 2) ? 0x68 : (int16_t)(0x10 + read_word(ram, 0xb732));
    return (int16_t)read_word(ram, actor + 0x24) < limit;
}

static void move_x(uint8_t *ram, uint16_t actor) { move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38)); }
static void move_y(uint8_t *ram, uint16_t actor) { move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a)); }
static void move_offset_y(uint8_t *ram, uint16_t actor) { move_actor_axis(ram, actor + 0x33, read_word(ram, actor + 0x3a)); }

static void touch(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned shot_mode)
{
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = (uint8_t)shot_mode;
    world_projectile_contact(ram, rom, actor);
}

static void follow(uint8_t *ram, uint16_t actor, uint16_t parent, uint16_t dx, uint16_t dy)
{
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + dx);
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + dy);
}

/* 657D/6580: velocity from the heading byte and the speed word. */
static void heading_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned angle = 2 * ram[actor + 0x28];
    int speed = (int16_t)read_word(ram, actor + 0x2e);
    for (unsigned axis = 0; axis < 2; ++axis) {
        int unit = (int16_t)read_word(rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + angle);
        write_word(ram, actor + 0x38 + 2 * axis, (uint16_t)((uint32_t)(unit * speed) >> 8));
    }
}

/* True when the actor is inside the view on the listed sides (the original's inline tests). */
static int above_view(const uint8_t *ram, uint16_t actor)
{
    uint16_t limit = (read_word(ram, 0x26e) & 2) ? 0x198 : (uint16_t)(0x1c8 + read_word(ram, 0xb732));
    return limit < read_word(ram, actor + 0x24);
}

static int outside_columns(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, actor + 0x21) > 0x1c0 || read_word(ram, actor + 0x21) < 0x40;
}

static int outside_view(const uint8_t *ram, uint16_t actor)
{
    return outside_columns(ram, actor) || above_view(ram, actor) || below_view(ram, actor);
}

/* 0D68F/0D692/0D6B7: the flight script interpreter, for the two step kinds the
 * driller's scripts use: 2 (heading and speed) and E (the same heading step applied
 * to the offset pair 30/33, keeping the speed). Other kinds are left to the caller. */
static int script_kind_supported(uint16_t kind)
{
    return kind == 2 || kind == 0x0e;
}

static int script_load(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t entry = read_word(ram, actor + 0x7e);
    uint16_t kind = read_word(bank, entry);
    if (kind == 0xffff) return 0;
    write_word(ram, actor + 0x7a, kind);
    ram[actor + 0x28] = bank[(uint16_t)(entry + 4)];
    if (kind == 2 && read_word(bank, entry + 6)) write_word(ram, actor + 0x2e, read_word(bank, entry + 6));
    heading_velocity(ram, rom, actor);
    write_word(ram, actor + 0x78, 0);
    write_word(ram, actor + 0x76, 0);
    write_word(ram, actor + 0x7e, entry + 8);
    ram[actor + 0x20] = ram[actor + 0x23] = 0;
    write_word(ram, actor + 0x7c, read_word(bank, entry + 2));
    return 1;
}

static int script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t entry)
{
    write_word(ram, actor + 0x7e, entry);
    return script_load(ram, rom, actor);
}

/* False when the current step, or the step that would be loaded now, is not handled here. */
static int script_supported(const uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t next = read_word(bank, read_word(ram, actor + 0x7e));
    if (!script_kind_supported(read_word(ram, actor + 0x7a))) return 0;
    return read_word(ram, actor + 0x7c) != 1 || next == 0xffff || script_kind_supported(next);
}

/* Returns 0 when the script has ended. */
static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    unsigned base = read_word(ram, actor + 0x7a) == 0x0e ? 0x30 : 0x20;
    scroll_actor(ram, actor, 0xb71a);
    write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) + read_word(ram, actor + 0x78));
    move_actor_axis(ram, actor + base, read_word(ram, actor + 0x38));
    write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) + read_word(ram, actor + 0x76));
    move_actor_axis(ram, actor + base + 3, read_word(ram, actor + 0x3a));
    if (!decrement(ram, actor + 0x7c)) return script_load(ram, rom, actor);
    return 1;
}

/* 6B2E: the medium explosion with its sound. */
static void spawn_explosion(uint8_t *ram, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    uint16_t effect = effect_slot(ram, actor, 0x6efe);
    write_word(ram, effect + 0x4c, 0x74c3);
    write_word(ram, effect + 0x4a, 0x7675);
    ram[effect + 0x2c] = 0x0a;
    ram[effect + 0x2d] = 0x20;
    write_word(ram, effect + 0x56, 0x30);
    write_word(ram, effect + 0x58, 0x80);
    if (!(effect & 0x300)) write_word(ram, effect + 0x44, 0x5d);
}

/* 37EB/3847/38A9 family: a rectangle of 4x4 tile blocks (see update_tile_script). */
static void tile_rect(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t base,
                      uint16_t source, uint16_t origin, uint16_t size, unsigned mode)
{
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
}

/* ---- Breakable wall tiles (79DAF and its four edge routines). ---- */

static int wall_cell(const uint8_t *map, uint16_t cell)
{
    return (read_word(map, cell + 2) & 0xf800) == 0x800;
}

static void rubble_below(uint8_t *map, uint16_t cell, uint16_t other)
{
    if ((read_word(map, other + 2) & 0xf880) == 0x3080) {
        write_word(map, other, ((((cell >> 1) + 2) & 15)) + 0x310);
        write_word(map, other + 2, 0x300e);
    }
}

/* The routines leave a pointer in IX that one caller (the entering submarine) goes on
 * to use as if it were still its own; wall_ix follows that register. */
static uint16_t wall_ix;

static void open_edge(uint8_t *map, const uint8_t *cs, uint16_t cell, unsigned phase, unsigned plain)
{
    uint16_t above = (cell - 0x100) & 0x3fff;
    if (wall_cell(map, above)) {
        uint16_t row = CW(0x97b8 + 2 * (((read_word(map, above + 2) & 0x7f) - 8) & 3));
        wall_ix = row + (((cell >> 1) + phase) & 6);
        write_word(map, cell, CW(wall_ix));
        write_word(map, cell + 2, 0x308f);
    } else {
        write_word(map, cell, (((cell >> 1) + plain) & 15) + 0x310);
        write_word(map, cell + 2, 0x300e);
    }
}

static void open_cell_9f72(uint8_t *map, const uint8_t *cs, uint16_t cell)
{
    open_edge(map, cs, cell, 0, 0);
    rubble_below(map, cell, (cell + 0x100) & 0x3fff);
}

static void open_cell_9ff4(uint8_t *map, const uint8_t *cs, uint16_t cell)
{
    open_edge(map, cs, cell, 2, 8);
    rubble_below(map, cell, (cell + 0x100) & 0x3fff);
}

static void open_cell_a05f(uint8_t *map, uint16_t cell)
{
    write_word(map, cell, (((cell >> 1) + 12) & 15) + 0x310);
    write_word(map, cell + 2, 0x300e);
    rubble_below(map, cell, (cell + 0x100) & 0x3fff);
}

static void spawn_debris(uint8_t *ram, uint16_t actor, int tall)
{
    uint16_t child = spawn_after_head(ram, 0x9a, STAGE_SEGMENT, 0x9dd9);
    if (child != 0x8480) wall_ix = RW(child + 4);
    WW(child + 0x21, RW(actor + 0x21));
    WW(child + 0x24, RW(actor + 0x24));
    if (tall) WW(child + 0x1e, 2);
    WW(child + 0x42, tall ? 0x14 : 0x20);
}

static void break_wall_cell(uint8_t *ram, const uint8_t *cs, uint8_t *map, uint16_t actor, uint16_t cell)
{
    if (!wall_cell(map, cell)) return;
    unsigned kind = ((read_word(map, cell + 2) & 0x7f) - 8) & 3;
    wall_ix = 2 * kind;
    if (kind < 2) {
        open_cell_9f72(map, cs, cell);
        cell = (cell & 0xff00) | ((cell + (kind ? -4 : 4)) & 255);
        if (wall_cell(map, cell)) open_cell_9f72(map, cs, cell);
        spawn_debris(ram, actor, 0);
    } else if (kind == 2) {
        open_cell_9ff4(map, cs, cell);
        cell = (cell + 0x100) & 0x3fff;
        if (wall_cell(map, cell)) open_cell_a05f(map, cell);
        spawn_debris(ram, actor, 1);
    } else {
        open_cell_a05f(map, cell);
        cell = (cell - 0x100) & 0x3fff;
        if (wall_cell(map, cell)) open_cell_9ff4(map, cs, cell);
        spawn_debris(ram, actor, 1);
    }
}

static uint16_t actor_cell(const uint8_t *ram, uint16_t actor)
{
    return world_tile_address(RW(actor + 0x21) - 0x60 + RW(0xb70a), 0x177 - RW(actor + 0x24) + RW(0xb70c));
}

/* 795A8/795E8/79628: four rows of wall cells; the actor position walks with the cell. */
static void break_wall_rows(uint8_t *ram, const uint8_t *cs, uint8_t *map, uint16_t actor,
                            uint16_t cell, unsigned columns, unsigned cell_back, unsigned x_back)
{
    uint16_t x = RW(actor + 0x21), y = RW(actor + 0x24);
    for (unsigned row = 0; row < 4; ++row) {
        if ((int16_t)RW(actor + 0x24) < 0x88) break;
        for (unsigned i = 0; i < columns; ++i) {
            break_wall_cell(ram, cs, map, actor, cell);
            cell = (cell & 0xff00) | ((cell + 4) & 255);
            WW(actor + 0x21, RW(actor + 0x21) + 8);
        }
        cell = (cell & 0xff00) | ((cell - cell_back) & 255);
        cell = (cell & 0x00ff) | ((((cell >> 8) + 1) & 0x3f) << 8);
        WW(actor + 0x21, RW(actor + 0x21) - x_back);
        WW(actor + 0x24, RW(actor + 0x24) - 8);
    }
    WW(actor + 0x24, y);
    WW(actor + 0x21, x);
}

static uint16_t cell_add(uint16_t cell, int column_bytes, int rows)
{
    cell = (cell & 0xff00) | ((cell + column_bytes) & 255);
    return (uint16_t)((cell + rows * 0x100) & 0x3fff);
}

/* 79546: the body clears the wall around itself after each step up. */
static void break_wall_under_body(uint8_t *ram, const uint8_t *cs, uint8_t *map, uint16_t actor)
{
    uint16_t x = RW(actor + 0x21), y = RW(actor + 0x24);
    WW(actor + 0x21, x - 0x14);
    WW(actor + 0x24, y + 0x6c);
    uint16_t cell = actor_cell(ram, actor);
    break_wall_rows(ram, cs, map, actor, cell, 6, 0x18, 0x30);
    WW(actor + 0x21, RW(actor + 0x21) - 0x10);
    WW(actor + 0x24, RW(actor + 0x24) - 0x10);
    cell = cell_add(cell, -8, 2);
    break_wall_rows(ram, cs, map, actor, cell, 2, 8, 0x50);
    WW(actor + 0x21, RW(actor + 0x21) + 0x40);
    cell = (cell & 0xff00) | ((cell + 0x20) & 255);
    break_wall_rows(ram, cs, map, actor, cell, 2, 8, 0x50);
    WW(actor + 0x21, RW(actor + 0x21) - 0x78);
    WW(actor + 0x24, RW(actor + 0x24) - 0x30);
    cell = cell_add(cell, -0x3c, 6);
    break_wall_rows(ram, cs, map, actor, cell, 7, 0x1c, 0x38);
    WW(actor + 0x21, RW(actor + 0x21) + 0x88);
    cell = (cell & 0xff00) | ((cell + 0x44) & 255);
    break_wall_rows(ram, cs, map, actor, cell, 7, 0x1c, 0x38);
    WW(actor + 0x24, y);
    WW(actor + 0x21, x);
}

/* 79668: an arm clears the wall cells listed for its animation frame. */
static void break_wall_under_arm(uint8_t *ram, const uint8_t *cs, uint8_t *map, uint16_t actor, uint16_t frame)
{
    uint16_t entry = CW(frame + 0x7dd8);
    if (!entry || (int16_t)RW(actor + 0x24) < 0x88) return;
    uint16_t x = RW(actor + 0x21), y = RW(actor + 0x24);
    WW(actor + 0x21, CW(entry + 2 * (ram[actor + 0x29] & 1)) + x - RW(actor + 0x31));
    WW(actor + 0x24, y + CW(entry + 4));
    WW(actor + 0x4e, CW(entry + 6));
    uint16_t cell = actor_cell(ram, actor);
    unsigned rows = ram[actor + 0x4e];
    do {
        unsigned columns = ram[actor + 0x4f];
        do {
            break_wall_cell(ram, cs, map, actor, cell);
            cell = (cell & 0xff00) | ((cell + 4) & 255);
            WW(actor + 0x21, RW(actor + 0x21) + 8);
        } while ((columns = (columns - 1) & 255) != 0);
        unsigned back = 8 * ram[actor + 0x4f];
        WW(actor + 0x21, RW(actor + 0x21) - back);
        cell = (cell & 0xff00) | ((cell - (back >> 1)) & 255);
        cell = (cell & 0x00ff) | ((((cell >> 8) + 1) & 0x3f) << 8);
        WW(actor + 0x24, RW(actor + 0x24) - 8);
    } while ((rows = (rows - 1) & 255) != 0);
    WW(actor + 0x24, y);
    WW(actor + 0x21, x);
}

/* 796E8: one dust cloud and three rocks thrown up on one side. */
static void spawn_slam_debris(uint8_t *ram, uint16_t actor)
{
    uint16_t child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8cfb);
    ram[child + 0x29] = ram[actor + 0x29];
    for (unsigned i = 0; i < 3; ++i) {
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8d90);
        ram[child + 0x29] = ram[actor + 0x29];
        WW(child + 0x1e, 4 * i);
    }
    world_queue_sound(ram, 0x58);
}

/* 7945E: ride the parent, shaking while the hit timer runs. */
static void ride_parent_shaking(uint8_t *ram, uint16_t actor)
{
    uint16_t parent = parent_of(ram, actor);
    WW(actor + 0x21, RW(parent + 0x21));
    WW(actor + 0x24, RW(parent + 0x24) - (RW(actor + 0x78) & 2));
    if (RW(actor + 0x78)) WW(actor + 0x78, RW(actor + 0x78) - 1);
}

/* 7948B: contacts for the head while it is armoured. A falling ceiling plate (kind 2F)
 * shakes it, and in the fight costs it one point and makes the body recoil. */
static void head_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = 1;
    uint16_t shot = world_projectile_contact(ram, rom, actor);
    if (!shot || RW(shot + 0x0e) != 0x2f) return;
    WW(actor + 0x78, 0x20);
    uint16_t parent = parent_of(ram, actor);
    if ((RW(parent + 0x50) & 0x20) && RW(actor + 0x26)) {
        WW(actor + 0x26, RW(actor + 0x26) - 1);
        WW(parent + 0x5e, RW(parent + 0x5e) + 0x20);
    }
}

/* 0A3B7 -> 7000:9D93. Player weapons call this where they strike terrain. */
void world_stage5_wall_hit(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor)
{
    if (RW(0x84) != 4) return;
    break_wall_cell(ram, rom + 0x70000, (uint8_t *)terrain, actor, actor_cell(ram, actor));
}

/* 0A3B7 where the caller probed terrain away from the actor's own position:
 * `cell` is the probed cell the original still holds in BW. */
void world_stage5_wall_hit_cell(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor, uint16_t cell)
{
    if (RW(0x84) != 4) return;
    break_wall_cell(ram, rom + 0x70000, (uint8_t *)terrain, actor, cell);
}

/* 08683. The submarine's own position is moved over each cell for the debris. */
uint16_t world_stage5_sub_entry_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub)
{
    wall_ix = RW(sub + 0x70);
    if (RW(0x84) != 4) return wall_ix;
    uint16_t x = RW(sub + 0x21), y = RW(sub + 0x24);
    WW(sub + 0x21, x - 0x14);
    WW(sub + 0x24, y + 0x0c);
    uint16_t cell = actor_cell(ram, sub);
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned i = 0; i < 6; ++i) {
            break_wall_cell(ram, rom + 0x70000, tiles + 0x4000, sub, cell);
            cell = (cell & 0xff00) | ((cell + 4) & 255);
            WW(sub + 0x21, RW(sub + 0x21) + 8);
        }
        cell = (cell & 0xff00) | ((cell - 0x18) & 255);
        cell = (cell + 0x100) & 0x3fff;
        WW(sub + 0x21, RW(sub + 0x21) - 0x30);
        WW(sub + 0x24, RW(sub + 0x24) - 8);
    }
    WW(sub + 0x24, y);
    WW(sub + 0x21, x);
    return wall_ix;
}

/* 08763. Not limited to this stage in the original; only this stage has such cells. */
void world_stage5_sub_side_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub,
                                 uint16_t x, const uint16_t rows[3])
{
    if (RW(0x274) & 15) return;
    const uint8_t *cs = rom + 0x70000;
    uint8_t *map = tiles + 0x4000;
    uint16_t old_x = RW(sub + 0x21), y = RW(sub + 0x24);
    unsigned ahead = (RW(0xb73e) & 1) << 4;
    WW(sub + 0x21, x + 2 * ahead - 0x10);
    break_wall_cell(ram, cs, map, sub, ahead + rows[1]);
    WW(sub + 0x24, y + 7);
    break_wall_cell(ram, cs, map, sub, ahead + rows[0]);
    WW(sub + 0x24, y - 8);
    break_wall_cell(ram, cs, map, sub, ahead + rows[2]);
    WW(sub + 0x24, y);
    WW(sub + 0x21, old_x);
}

/* 08886. */
void world_stage5_sub_row_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub,
                                uint16_t x, uint16_t y, uint16_t upper, uint16_t lower)
{
    if (RW(0x274) & 15) return;
    uint16_t old_x = RW(sub + 0x21), old_y = RW(sub + 0x24);
    int down = (RW(0xb73e) & 4) != 0;
    uint16_t cell = down ? lower : upper;
    WW(sub + 0x21, x - 0x10);
    WW(sub + 0x24, down ? y - 8 : y + 7);
    for (unsigned i = 0; i < 5; ++i) {
        break_wall_cell(ram, rom + 0x70000, tiles + 0x4000, sub, cell);
        cell = (cell & 0xff00) | ((cell + 4) & 255);
        WW(sub + 0x21, RW(sub + 0x21) + 8);
    }
    WW(sub + 0x24, old_y);
    WW(sub + 0x21, old_x);
}

/* 7952A: draw with the hit flash taken from the actor's two palette slots. */
static void draw_flashing(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (ram[actor + 0x19]) {
        --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x15 + (ram[actor + 0x19] & 1)];
    }
    queue_sprite(ram, rom, actor, 0);
}

/* 794CE: contacts for the exposed head; its destruction ends the fight. */
static void exposed_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    world_player_contact(ram, rom, actor);
    ram[actor + 0x0d] = 0;
    uint16_t shot = world_projectile_contact(ram, rom, actor);
    if (shot && RW(shot + 0x0e) == 0x2f) WW(actor + 0x78, 0x20);
    if (RW(actor + 0x26)) return;
    if (!(RW(0x264) & 3)) {
        WW(actor + 0x26, 0x100);
        return;
    }
    WW(actor + 0x26, 0);
    ram[actor + 0x0a] |= 2;
    SET(0x124 + ((RW(shot + 0x0b) & 2) << 5), 0x10);
    world_award_score(ram, rom, actor, shot);
    SET(0x262, 0x80);
}

/* 79C3D: does a bridge block overlap the golem's head or either arm? */
static int touches_golem(const uint8_t *ram, const uint8_t *rom, const uint8_t *cs, uint16_t actor)
{
    uint16_t shape = RW(actor + 0x3c);
    uint16_t head = RW(0xb8c4);
    if (world_shape_overlap(ram, rom, actor, head, shape, RW(head + 0x3c))) return 1;
    for (unsigned slot = 0xb8c6; slot <= 0xb8c8; slot += 2) {
        uint16_t arm = RW(slot);
        if (world_shape_overlap(ram, rom, actor, arm, shape, RW(arm + 0x3c))) return 1;
        uint16_t extra = CW((uint16_t)(2 * (RW(arm + 0x40) - 2)) + 0x7da6);
        if (extra && world_shape_overlap(ram, rom, actor, arm, shape, extra)) return 1;
    }
    return 0;
}

/* 7A488: a shot against the ceiling leaves a spark; no damage either way. */
static void ceiling_sparks(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (RW(actor + 0x4e)) {
        WW(actor + 0x4e, RW(actor + 0x4e) - 1);
        return;
    }
    ram[actor + 0x0d] = 3;
    uint16_t shot = world_projectile_contact(ram, rom, actor);
    if (!shot) return;
    uint16_t spark = spawn_after_head(ram, 0x9a, STAGE_SEGMENT, 0xa43b);
    WW(spark + 0x21, RW(shot + 0x21));
    WW(spark + 0x24, 0x168);
    WW(actor + 0x4e, 0x10);
}

/* 7A4BC: a loosened plate twitches through randomly chosen frame lists. */
static void plate_wobble(uint8_t *ram, const uint8_t *rom, const uint8_t *cs, uint16_t actor)
{
    for (;;) {
        if (RW(actor + 0x44)) {
            WW(actor + 0x44, RW(actor + 0x44) - 1);
            break;
        }
        uint16_t sprite = CW(RW(actor + 0x40) + RW(actor + 0x46) + 0x9848);
        if (sprite != 0xffff) {
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, 1);
            WW(actor + 0x40, RW(actor + 0x40) + 2);
            break;
        }
        WW(actor + 0x2a, 0x250f);
        WW(actor + 0x44, world_random_byte(ram) & 15);
        WW(actor + 0x40, 0);
        WW(actor + 0x46, CW(0x98a0 + (world_random_byte(ram) & 2)));
    }
    queue_sprite(ram, rom, actor, 0);
}

/* 44AB: the view climbs while a player is high enough in it and none is at the bottom. */
static int climb_allowed(uint8_t *ram, uint16_t actor)
{
    CLR(0x26e, 0x4000);
    if (RW(0x26e) & 0x8000) return 0;
    uint16_t line = RW(actor + 0x4c);
    for (unsigned i = 0; i < 2; ++i) {
        if (!(RW(0x264) & (4u << i))) continue;
        uint16_t sub = RW(0x90 + 2 * i);
        if (RW(sub + 0x24) < 0xc8) { CLR(0x26e, 0x4000); return 0; }
        if (RW(sub + 0x24) >= line) SET(0x26e, 0x4000);
    }
    return (RW(0x26e) & 0x4000) != 0;
}

int world_stage5_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *cs = main_rom + 0x70000;
    uint8_t *map = tiles ? tiles + 0x4000 : 0;
    uint16_t parent, child, frame, sprite;
    switch (address) {

    /* ---- Camera: the view climbs the shaft with the players. ---- */
    case 0x3e61:
        WW(0x270, 4);
        WW(actor, 0x3e6c);
        /* fall through */
    case 0x3e6c:
        if (!tiles || (RW(0x2a2) && (RW(0x2a8) & 0x100))) return WORLD_HANDLER_STOP;
        update_camera_limits(ram, main_rom, actor);
        if (!climb_allowed(ram, actor)) {
            WW(0xb71c, 0);
            WW(0xb72c, 0);
            WW(0xa4, 0);
            break;
        }
        WW(actor + 0x56, RW(actor + 0x56) - 1);
        WW(0xb70c, RW(0xb70c) - 1);
        WW(0xb700, RW(0xb700) + 1);
        WW(0xb71c, 0xffff);
        WW(0xb72c, 0xffff);
        if ((int16_t)RW(actor + 0x6a) >= (int16_t)RW(0xb70c)) {
            uint16_t source = RW(actor + 0x42), dest = RW(actor + 0x44);
            uint16_t base = (uint16_t)(RW(actor + 0x60) << 4);
            for (unsigned block = 0; block < 12; ++block) {
                world_draw_tile_block(tiles, main_rom, base, dest, read_word(main_rom + 0xa0000, source + 2 * block));
                dest = (dest & 0xff00) | ((dest + 16) & 255);
            }
            WW(actor + 0x42, source + RW(actor + 0x4e));
            WW(actor + 0x44, (RW(actor + 0x44) - 0x400) & 0x3fff);
            WW(actor + 0x6a, RW(actor + 0x6a) - 0x20);
        }
        advance_scroll_script(ram, main_rom, actor);
        break;
    case 0x409e: {
        if (!(RW(0x26e) & 0x4000)) {
            WW(0xb71e, 0);
            break;
        }
        uint16_t old = RW(actor + 0x56);
        WW(actor + 0x5e, old);
        move_actor_axis(ram, actor + 0x55, 0x80);
        uint16_t y = -RW(actor + 0x56);
        WW(0xb710, y);
        WW(0xb720, y + old);
        advance_scroll_script(ram, main_rom, actor);
        break;
    }

    /* ---- Shared player handlers whose common translation leaves stage index 4 out. ---- */
    case 0x9c33:
    case 0x9c82:
        if (RW(0x84) != 4) return WORLD_HANDLER_UNKNOWN;
        if (!tiles || (ram[actor + 0x0b] & 0x80) || (address == 0x9c33 && !RW(actor + 0x42)))
            return WORLD_HANDLER_STOP;
        update_standard_torpedo(ram, main_rom, map, actor, address == 0x9c33);
        break;
    case 0xb6df:
    case 0xb730:
    case 0xb77c: {
        /* The common translation stops where this weapon strikes terrain in this stage,
         * with everything before the impact already done; finish the impact here. */
        if (RW(0x84) != 4) return WORLD_HANDLER_UNKNOWN;
        if (!tiles) return WORLD_HANDLER_STOP;
        int result = world_actors_1(pass, ram, main_rom, tiles, actor_io, address);
        if (result != WORLD_HANDLER_STOP) return result;
        world_stage5_wall_hit(ram, main_rom, map, actor);
        spawn_impact(ram, main_rom, actor);
        decrement(ram, RW(0x84fe + RW(actor + 8)) + 0x44);
        finish_animation(ram, actor);
        break;
    }
    case 0x7b3c:
    case 0x7b6f: {
        /* Same entry as the common 7B3C/7B6F; it then continues in the common 7BE5. */
        if (RW(0x84) != 4) return WORLD_HANDLER_UNKNOWN;
        unsigned owner = ram[actor + 0x0b] & 3;
        if (!tiles || !owner || owner > 2 || (ram[0x262] & 0x40) || (ram[0xb8b2] & (1u << (owner & 2))))
            return WORLD_HANDLER_STOP;
        uint16_t config = RW(actor + 0x70);
        unsigned side = 2 * (owner & 2);
        if (address == 0x7b3c) {
            int16_t y = (int16_t)RW(0xb736 + side), ceiling = (int16_t)RW(0x25c);
            WW(config + 0x26, RW(0x84));
            WW(actor + 0x21, RW(0xb734 + side));
            WW(actor + 0x24, y <= ceiling ? y : ceiling);
            WW(actor + 0x42, 127);
            WW(0xb8a0, RW(0xb8a0) + 30);
        }
        if (!RW(config + 0x18)) WW(config + 0x18, read_word(main_rom, 0x78e3 + 2 * (ram[0x278] & 3)));
        uint16_t score = RW(0x254 + 2 * (owner - 1));
        if (RW(actor + 0x6e)) {
            unsigned a = ram[score], b = main_rom[0xc307];
            unsigned sum = (a >> 4) * 10 + (a & 15) + (b >> 4) * 10 + (b & 15);
            ram[score] = (uint8_t)((sum / 10 % 10) * 16 + sum % 10);
        } else if (ram[0x264] & (1u << (owner + 5))) {
            ram[0x264] &= ~(1u << (owner + 5));
            for (unsigned i = 0; i < 8; ++i) ram[(uint16_t)(score + i)] = 0;
            WW(config + 0x1a, 0);
            WW(config + 0x1c, 0);
            WW(config + 0x24, 0);
            WW(config + 0x20, 0);
            WW(config + 0x22, 0);
        }
        draw_join_label(tiles, main_rom, owner == 1 ? 0x2d30 : 0x2d90, owner == 1 ? 0xc26b : 0xc27c);
        world_sub_lives_hud(main_rom, tiles, owner, RW(config + 0x18));
        ram[actor + 0x28] = 255;
        WW(actor + 0x2e, RW(0xa2));
        ram[actor + 0x29] = 0;
        WW(actor + 0x40, 0);
        for (unsigned i = 0x58; i <= 0x5e; i += 2) WW(actor + i, 0);
        WW(actor + 0x44, 2);
        WW(actor + 0x46, 2);
        WW(actor + 0x48, 0);
        WW(actor + 0x56, 4);
        WW(actor + 0x54, 0);
        WW(actor + 0x64, 6);
        WW(actor + 0x66, 24);
        WW(actor + 0x2a, read_word(main_rom, 0x7bd9));
        WW(actor + 0x6e, 1);
        WW(actor, 0x7be5);
        return world_actors_2(pass, ram, main_rom, tiles, actor_io, 0x7be5);
    }

    /* ---- The golem's body. ---- */
    case 0x77ffe:
        SET(0x26e, 0x8000);
        WW(actor + 0x0e, 0x48);
        ram[actor + 0x2c] = 0x17;
        ram[actor + 0x2d] = 0x30;
        WW(actor + 0x2a, 0x5f49);
        WW(actor + 0x3c, 0xf8eb);
        WW(actor + 0x36, 0x7f00);
        WW(actor + 0x60, 0x80);
        frame = CW(CW(RW(0x8a) + 0x7fce) + 0x7ff0);
        WW(actor + 0x58, frame);
        WW(actor + 0x5a, frame);
        spawn_far(ram, actor, STAGE_SEGMENT, 0x8eb5);
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8a19);
        WW(child + 0x31, 0x40);
        WW(0xb8c6, child);
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8a19);
        WW(child + 0x31, 0xffc0);
        ram[child + 0x29] = 1;
        WW(0xb8c8, child);
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8337);
        WW(0xb8c4, child);
        WW(actor, 0x8085);
        break;
    case 0x78085:
        WW(actor + 0x24, RW(actor + 0x24) + RW(0xb71c));
        queue_sprite(ram, main_rom, actor, 0);
        if (RW(actor + 0x50) & 2) {
            CLR(0x26e, 0x8000);
            WW(actor + 0x40, 6);
            WW(actor + 0x44, 1);
            SET(actor + 0x52, 3);
            WW(actor, 0x81ed);
        }
        touch(ram, main_rom, actor, 1);
        break;
    case 0x780c9: {
        WW(actor + 0x24, RW(actor + 0x24) + RW(0xb71c));
        if (RW(actor + 0x5e)) {
            WW(actor + 0x3a, CW(0x7fca + (RW(actor + 0x5e) & 2)));
            move_y(ram, actor);
            if (!(RW(actor + 0x5e) & 15)) {
                ram[actor + 0x29] = 1;
                spawn_slam_debris(ram, actor);
                ram[actor + 0x29] = 0;
                spawn_slam_debris(ram, actor);
            }
            WW(actor + 0x5e, RW(actor + 0x5e) - 1);
        }
        if (RW(actor + 0x78)) {
            WW(actor + 0x24, RW(actor + 0x24) + 1 - (RW(actor + 0x78) & 2));
            WW(actor + 0x78, RW(actor + 0x78) - 1);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!(RW(actor + 0x50) & 0x20) && (int16_t)RW(actor + 0x60) <= (int16_t)RW(0xb700)) {
            WW(actor + 0x60, RW(actor + 0x60) + 0x80);
            WW(actor + 0x62, 0x80);
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            SET(actor + 0x52, 3);
            WW(actor, 0x81ed);
        }
        if (RW(0xb700) >= 0xb00 && !(RW(actor + 0x54) & 2) && !decrement(ram, actor + 0x58)) {
            WW(actor + 0x58, RW(actor + 0x5a));
            SET(actor + 0x54, 2);
        }
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 1;
        uint16_t shot = world_projectile_contact(ram, main_rom, actor);
        if (shot && RW(shot + 0x0e) == 0x2f) {
            WW(actor + 0x78, (RW(actor + 0x78) & 3) + 0x20);
            WW(actor + 0x6c, 1);
            WW(actor + 0x6e, 0);
        }
        if (RW(actor + 0x6c) && !decrement(ram, actor + 0x6c)) {
            uint16_t row = RW(actor + 0x6e);
            palette_job(ram, 0x9ea6, CW(row + 0x7f18), CW(row + 0x7f1a));
            WW(actor + 0x6c, CW(row + 0x7f1c));
            WW(actor + 0x6e, row + 6);
        }
        if (ram[actor + 0x0a] & 2) {
            SET(0x262, 0x80);
            WW(actor, 0x826a);
        }
        break;
    }
    case 0x781ed:
        if (!tiles) return WORLD_HANDLER_STOP;
        WW(actor + 0x24, RW(actor + 0x24) + RW(0xb71c));
        if (RW(actor + 0x50) & 4) {
            CLR(actor + 0x50, 4);
            WW(actor + 0x24, RW(actor + 0x24) + 0x20);
            WW(actor + 0x62, RW(actor + 0x62) - 0x20);
            WW(actor + 0x70, RW(actor + 0x70) + 1);
            break_wall_under_body(ram, cs, map, actor);
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7c10);
            if (sprite == 0xffff) {
                if (RW(0xb700) >= 0xb00 && (int16_t)RW(actor + 0x24) > 0x88) {
                    queue_sound_b(ram, 0x11);
                    SET(actor + 0x50, 0x20);
                }
                WW(actor, 0x80c9);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7c26));
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        touch(ram, main_rom, actor, 1);
        break;

    /* ---- The golem's head. ---- */
    case 0x78337:
        world_acquire_resource(ram, actor, 0xc5);
        WW(actor + 0x0e, 0x48);
        ram[actor + 0x2c] = 0x17;
        ram[actor + 0x15] = 0x17;
        ram[actor + 0x16] = 0x1d;
        ram[actor + 0x2d] = 0x30;
        WW(actor + 0x2a, 0x0af3);
        WW(actor + 0x3c, 0xf8d3);
        WW(actor + 0x44, 0x80);
        WW(actor + 0x26, 4);
        WW(actor + 0x36, 0x7f00);
        WW(actor, 0x8373);
        break;
    case 0x78373:
        ride_parent_shaking(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            WW(actor + 0x44, 0x9c);
            WW(actor + 0x42, 0xa5);
            spawn_far(ram, actor, STAGE_SEGMENT, 0x89b1);
            WW(actor, 0x839e);
        }
        queue_sprite(ram, main_rom, actor, 0);
        head_contacts(ram, main_rom, actor);
        break;
    case 0x7839e:
        if (!tiles) return WORLD_HANDLER_STOP;
        ride_parent_shaking(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7ac0);
            if (sprite == 0xffff) {
                parent = parent_of(ram, actor);
                SET(parent + 0x50, 2);
                WW(actor + 0x42, 0x200);
                WW(actor, 0x8442);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7ac8));
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            world_queue_sound(ram, 0x40);
            uint16_t job = 0x9e96, source = 0xeb62, palette;
            while ((palette = read_word(main_rom + 0xa0000, source)) != 0xffff) {
                palette_job(ram, job, palette + 0x100, 5);
                job += 8;
                source += 2;
            }
            tile_rect(ram, main_rom, tiles, 0x4000, 0x5bbc, 0x2d30, 0x0a08, 3);
        }
        head_contacts(ram, main_rom, actor);
        break;
    case 0x78442:
        ride_parent_shaking(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor + 0x42, 0x50);
            WW(actor, 0x84a3);
        }
        head_contacts(ram, main_rom, actor);
        if ((int16_t)RW(actor + 0x26) <= 0) {
            frame = CW(CW(RW(0x8a) + 0x7fce) + 0x7fee);
            WW(actor + 0x26, frame);
            WW(actor + 0x36, frame);
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            parent = parent_of(ram, actor);
            SET(parent + 0x50, 8);
            WW(actor, 0x84fa);
        }
        break;
    case 0x784a3:
        ride_parent_shaking(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7af8);
            if (sprite == 0xffff) {
                WW(actor + 0x42, 0x100);
                WW(actor, 0x8442);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7b0c));
                WW(actor + 0x40, frame + 2);
                if (frame == 8) world_queue_sound(ram, 0x42);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) spawn_far(ram, actor, STAGE_SEGMENT, 0x87c5);
        head_contacts(ram, main_rom, actor);
        break;

    /* ---- The head's shot and the four fragments it bursts into. ---- */
    case 0x787c5:
        ram[actor + 0x2c] = 0x19;
        ram[actor + 0x2d] = 6;
        WW(actor + 0x44, 1);
        WW(actor, 0x87d8);
        break;
    case 0x787d8:
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, 0, 0);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7b1e);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x7b30));
            WW(actor + 0x40, frame + 2);
            if (frame == 4) {
                WW(actor + 0x42, 0);
                do {
                    child = spawn_far(ram, actor, STAGE_SEGMENT, 0x885f);
                    uint16_t row = RW(actor + 0x42);
                    WW(child + 0x31, CW(row + 0x7b68));
                    WW(child + 0x34, CW(row + 0x7b6a));
                    WW(actor + 0x42, row + 4);
                } while (RW(actor + 0x42) < 0x11);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7885f:
        ram[actor + 0x2c] = 0x19;
        ram[actor + 0x2d] = 7;
        WW(actor + 0x3c, 0xf953);
        WW(actor + 0x44, 1);
        WW(actor, 0x8877);
        break;
    case 0x78877:
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), RW(actor + 0x34));
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7b40);
            if (sprite == 0xffff) {
                WW(actor + 0x40, 0);
                WW(actor + 0x44, 1);
                WW(actor + 0x3a, 0x80);
                WW(actor + 0x42, 0x140);
                WW(actor, 0x88da);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7b4a));
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x788da:
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_y(ram, actor);
        if (RW(actor + 0x24) >= 0x150 || !decrement(ram, actor + 0x42) ||
            (world_terrain_mask(ram, map, RW(actor + 0x21), RW(actor + 0x24) + 0x14) & 0xff02)) {
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor, 0x8971);
        } else if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, CW(0x7b52 + (frame & 6)));
            WW(actor + 0x44, 5);
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
            uint16_t sub = RW(slot), shape = RW(sub + 0x3c);
            if (shape != 0xffff && world_shape_overlap(ram, main_rom, actor, sub, RW(actor + 0x3c), shape))
                WW(sub + 0x33, RW(actor + 0x3a));
        }
        break;
    case 0x78971:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7b5a);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x7b62));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x789b1:
        ram[actor + 0x2c] = 0x17;
        ram[actor + 0x2d] = 0x30;
        WW(actor + 0x44, 1);
        WW(actor, 0x89c4);
        break;
    case 0x789c4:
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, 0, 0);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7ace);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x7ae4));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* ---- The two climbing arms. ---- */
    case 0x78a19:
        ram[actor + 0x2c] = 0x18;
        ram[actor + 0x2d] = 0x2f;
        WW(actor + 0x2a, 0x63ed);
        WW(actor + 0x36, 0x7f00);
        WW(actor + 0x48, CW(0x7d56 + 2 * (ram[actor + 0x29] & 1)));
        for (unsigned i = 0; i < 2; ++i) {
            child = spawn_far(ram, actor, STAGE_SEGMENT, 0x8e51);
            ram[child + 0x29] = ram[actor + 0x29];
            ram[child + 0x2c] = 0x18;
            ram[child + 0x2d] = 0x30;
            WW(child + 0x2a, i ? 0x8283 : 0x82a5);
            WW(child + 0x48, 0x7c66);
            WW(child + 0x1e, i ? 0 : 4);
        }
        WW(actor, 0x8a92);
        break;
    case 0x78a92: {
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), 0);
        unsigned bit = 1u << (ram[actor + 0x29] & 15);
        if (RW(parent + 0x52) & bit) {
            CLR(parent + 0x52, bit);
            WW(actor, 0x8ad5);
        }
        queue_sprite(ram, main_rom, actor, 0);
        WW(actor + 0x3c, 0xf8f3);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;
    }
    case 0x78ad5:
        WW(actor + 0x40, 0xfffe);
        WW(actor + 0x44, 1);
        WW(actor, 0x8ae4);
        /* fall through */
    case 0x78ae4:
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), 0);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40) + 2;
            WW(actor + 0x40, frame);
            sprite = CW(frame + 0x7c3a);
            if (sprite == 0xffff) {
                WW(actor + 0x40, frame - 2);
                WW(actor + 0x48, CW(0x7d5a + 2 * (ram[actor + 0x29] & 1)));
                WW(actor, 0x8bac);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x34, CW(frame + 0x7d5e));
                WW(actor + 0x44, CW(frame + RW(actor + 0x48) + 0x7d06));
                if (!(ram[actor + 0x29] & 1) && frame == 10)
                    spawn_after_head(ram, 0x9c, STAGE_SEGMENT, 0x93e6);
                else if (frame == 8)
                    spawn_slam_debris(ram, actor);
            }
        }
        WW(actor + 0x24, RW(actor + 0x24) - RW(actor + 0x34));
        queue_sprite(ram, main_rom, actor, 0);
        frame = RW(actor + 0x40) * 2;
        if (CW(frame + 0x7d8a)) {
            WW(actor + 0x3c, CW(frame + 0x7d8a));
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
        }
        WW(actor + 0x3c, CW(frame + 0x7d88));
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;
    case 0x78bac: {
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), -RW(actor + 0x34));
        if (ram[parent + 0x0a] & 4) WW(actor + 0x2a, 0x6537);
        unsigned bit = 1u << (ram[actor + 0x29] & 15);
        if (RW(parent + 0x52) & bit) {
            CLR(parent + 0x52, bit);
            WW(actor, 0x8c0c);
        }
        queue_sprite(ram, main_rom, actor, 0);
        WW(actor + 0x3c, 0xf8fb);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        WW(actor + 0x3c, 0xf903);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;
    }
    case 0x78c0c:
        WW(actor + 0x40, 0xfffe);
        WW(actor + 0x44, 1);
        SET(actor + 0x52, 4);
        WW(actor, 0x8c20);
        /* fall through */
    case 0x78c20:
        if (!tiles) return WORLD_HANDLER_STOP;
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), -RW(actor + 0x34));
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40) + 2;
            WW(actor + 0x40, frame);
            sprite = CW(frame + 0x7c4a);
            if (sprite == 0xffff) {
                WW(actor + 0x40, frame - 2);
                WW(actor, 0x8bac);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x34, CW(frame + 0x7d6c));
                WW(actor + 0x44, CW(frame + RW(actor + 0x48) + 0x7d22));
                break_wall_under_arm(ram, cs, map, actor, frame);
                if (!(ram[actor + 0x29] & 1) && RW(actor + 0x40) == 0x16)
                    spawn_after_head(ram, 0x9c, STAGE_SEGMENT, 0x93e6);
                else if (RW(actor + 0x40) == 0x14)
                    spawn_slam_debris(ram, actor);
                else if (!(ram[actor + 0x29] & 1) && RW(actor + 0x40) < 8)
                    SET(parent_of(ram, actor) + 0x50, 4);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        frame = RW(actor + 0x40) * 2;
        if (CW(frame + 0x7da6)) {
            WW(actor + 0x3c, CW(frame + 0x7da6));
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
        }
        WW(actor + 0x3c, CW(frame + 0x7da4));
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;

    /* ---- Dust and rocks thrown up when an arm lands. ---- */
    case 0x78cfb:
    case 0x78d45:
        if (address == 0x78cfb) {
            world_acquire_resource(ram, actor, 0xc6);
            ram[actor + 0x2d] = 0x28;
            WW(actor + 0x44, 1);
            parent = parent_of(ram, actor);
            int mirrored = ram[actor + 0x29] & 1;
            WW(actor + 0x38, mirrored ? 0x0100 : 0xff00);
            follow(ram, actor, parent, mirrored ? -0x98 : 0x98, 0x80);
            WW(actor, 0x8d45);
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_x(ram, actor);
        WW(actor + 0x3a, RW(actor + 0x3a) - 0x10);
        move_y(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7e22);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, 8);
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x78d90:
    case 0x78dec:
        if (address == 0x78d90) {
            world_acquire_resource(ram, actor, 0xc7);
            WW(actor + 0x3c, 0xfb4b);
            ram[actor + 0x2d] = 0x28;
            WW(actor + 0x44, 1);
            parent = parent_of(ram, actor);
            uint16_t row = RW(actor + 0x1e);
            int mirrored = ram[actor + 0x29] & 1;
            WW(actor + 0x38, mirrored ? -CW(row + 0x7e44) : CW(row + 0x7e44));
            WW(actor + 0x3a, CW(row + 0x7e46));
            follow(ram, actor, parent, mirrored ? -0x98 : 0x98, 0x80);
            WW(actor, 0x8dec);
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_x(ram, actor);
        WW(actor + 0x3a, RW(actor + 0x3a) - 0x10);
        move_y(ram, actor);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, CW(0x7e34 + (frame & 14)));
            WW(actor + 0x44, 4);
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* ---- Arm joints: placed from the arm's current frame. ---- */
    case 0x78e51:
        parent = parent_of(ram, actor);
        if (RW(parent + 0x52) & 4) {
            WW(actor + 0x48, 0x7c9e);
            WW(actor, 0x8e70);
        }
        /* fall through */
    case 0x78e70: {
        parent = parent_of(ram, actor);
        frame = RW(parent + 0x40);
        WW(actor + 0x40, frame);
        uint16_t row = frame * 4 + RW(actor + 0x48) + RW(actor + 0x1e);
        uint16_t dx = CW(row);
        WW(actor + 0x31, dx);
        if (ram[actor + 0x29] & 1) dx = -dx;
        WW(actor + 0x21, dx + RW(parent + 0x21));
        WW(actor + 0x34, CW(row + 2));
        WW(actor + 0x24, CW(row + 2) + RW(parent + 0x24));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }

    /* ---- The chain: a lead piece and three trailing segments. ---- */
    case 0x78eb5:
        ram[actor + 0x2c] = 0x1e;
        ram[actor + 0x2d] = 0x2f;
        ram[actor + 0x0c] = 0x0c;
        WW(actor + 0x2a, 0x63db);
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x9153);
        WW(child + 0x1e, 3);
        WW(actor, 0x8edc);
        break;
    case 0x78edc:
        if (RW(0xb700) < 0xb00) break;
        parent = parent_of(ram, actor);
        if (!(RW(parent + 0x50) & 8) || !(RW(parent + 0x54) & 2)) break;
        WW(actor + 0x31, 0x58);
        if (RW(0x264) & 3) {
            uint16_t target = world_enemy_target(ram, actor);
            if (RW(target + 0x21) < 0x100) WW(actor + 0x31, -RW(actor + 0x31));
        }
        if (RW(actor + 0x31) & 0x8000) ram[actor + 0x29] = 1;
        WW(actor + 0x34, 0xff90);
        WW(actor + 0x3a, 0x80);
        WW(actor + 0x42, 0xe0);
        SET(actor + 0x54, 1);
        WW(actor, 0x900f);
        break;
    case 0x79153:
        if (decrement(ram, actor + 0x1e)) {
            child = spawn_far(ram, actor, STAGE_SEGMENT, 0x9153);
            WW(child + 0x1e, RW(actor + 0x1e));
        } else {
            WW(actor + 0x3c, 0xf95b);
            WW(actor + 0x36, 0x7f00);
        }
        ram[actor + 0x2c] = 0x1e;
        ram[actor + 0x2d] = 0x2f;
        WW(actor + 0x2a, CW(0x7eca + 2 * (RW(actor + 0x1e) & 3)));
        WW(actor, 0x9194);
        /* fall through */
    case 0x79194: {
        parent = parent_of(ram, actor);
        if (!(RW(parent + 0x54) & 1)) break;
        unsigned facing = ram[parent + 0x29];
        ram[actor + 0x29] = (uint8_t)facing;
        uint16_t row = 2 * (RW(actor + 0x1e) & 3);
        int16_t reach = (int16_t)CW(row + 0x7ed0);
        WW(actor + 0x34, reach);
        reach >>= 1;
        WW(actor + 0x31, (facing & 1) ? -reach : reach);
        WW(actor + 0x3a, CW(row + 0x7ed6));
        WW(actor + 0x54, 1);
        WW(actor, 0x91e5);
        break;
    }

    /* ---- Screen shake when an arm lands. ---- */
    case 0x793e6:
        WW(actor + 0x42, CW(0x7e50));
        WW(actor + 0x6c, 1);
        WW(actor, 0x93f7);
        /* fall through */
    case 0x793f7: {
        frame = RW(actor + 0x40);
        uint16_t x = CW(frame + 0x7e54);
        WW(0xb70a, x);
        WW(0xb71a, CW(frame + 0x7e52) - x);
        WW(actor + 0x40, frame + 2);
        if (!decrement(ram, actor + 0x6c)) {
            uint16_t row = RW(actor + 0x6e);
            WW(actor + 0x6c, CW(row + 0x7ee0));
            palette_job(ram, 0x9ea6, CW(row + 0x7edc), CW(row + 0x7ede));
            WW(actor + 0x6e, row + 6);
        }
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    }

    /* ---- Wall debris. ---- */
    case 0x79dd9:
        WW(0xe0, RW(0xe0) + 1);
        WW(actor + 0x3c, 0xfb4b);
        ram[actor + 0x2d] = 0;
        ram[actor + 0x2c] = 0x9c;
        WW(actor + 0x46, CW(0x97d8 + (RW(actor + 0x1e) & 2)));
        WW(actor + 0x48, CW(0x97dc + (RW(actor + 0x1e) & 2)));
        WW(actor + 0x3a, 0x100);
        WW(actor + 0x44, 1);
        ram[actor + 0x29] = (actor >> 8) & 1;
        WW(actor, 0x9e1a);
        break;
    case 0x79e1a:
        scroll_actor(ram, actor, 0xb71a);
        move_y(ram, actor);
        if ((int16_t)RW(actor + 0x3a) > (int16_t)0xfe00) WW(actor + 0x3a, RW(actor + 0x3a) - 0x10);
        if (below_view(ram, actor)) {
            WW(0xe0, RW(0xe0) - 1);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, CW(frame + RW(actor + 0x46)));
            WW(actor + 0x44, CW(frame + RW(actor + 0x48)));
            WW(actor + 0x40, frame + 2);
            if (RW(actor + 0x40) >= RW(actor + 0x42)) WW(actor + 0x40, 0);
        }
        if (!queue_sprite(ram, main_rom, actor, 0)) finish_animation(ram, actor);
        break;

    /* ---- Camera lock for the fight at the top of the shaft. ---- */
    case 0x414d:
        CLR(0x26e, 0x4000);
        WW(0xb71c, 0xffff);
        WW(0xb72c, 0xffff);
        WW(actor, 0x4162);
        break;
    case 0x4162:
        CLR(0x26e, 0x4000);
        WW(0xb71a, 0);
        WW(0xb72a, 0);
        WW(0xb71c, 0);
        WW(0xb72c, 0);
        break;

    /* ---- Body, after the head is destroyed. ---- */
    case 0x7826a:
        palette_job(ram, 0x9ea6, 0x1ee, 4);
        WW(actor + 0x42, 0x40);
        WW(actor + 0x40, 3);
        WW(actor, 0x8295);
        /* fall through */
    case 0x78295:
        if (!decrement(ram, actor + 0x42)) {
            if (!decrement(ram, actor + 0x40)) {
                WW(actor + 0x3a, 0);
                WW(actor + 0x42, 0x100);
                ram[actor + 0x0a] |= 4;
                WW(actor, 0x8304);
            } else {
                WW(actor + 0x42, 0x20);
                WW(actor, 0x82c6);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x782c6:
        WW(actor + 0x3a, CW(0x7fca + (RW(actor + 0x42) & 2)));
        move_y(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(actor + 0x42, 0x20);
            WW(actor, 0x8295);
        }
        if (!(RW(actor + 0x42) & 15)) {
            ram[actor + 0x29] = 1;
            spawn_slam_debris(ram, actor);
            ram[actor + 0x29] = 0;
            spawn_slam_debris(ram, actor);
        }
        break;
    case 0x78304:
        WW(actor + 0x3a, RW(actor + 0x3a) - 0x10);
        move_y(ram, actor);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            SET(0x262, 0xc000);
            finish_animation(ram, actor);
        }
        break;

    /* ---- Head, once its armour is gone: exposed, launching homing shots. ---- */
    case 0x784fa:
        ride_parent_shaking(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7b7c);
            if (sprite == 0xffff) {
                WW(actor + 0x42, 0x100);
                WW(actor, 0x8534);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7b8c));
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        head_contacts(ram, main_rom, actor);
        break;
    case 0x78534:
        ride_parent_shaking(ram, actor);
        draw_flashing(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42)) {
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor, 0x855e);
        }
        exposed_contacts(ram, main_rom, actor);
        if (!RW(actor + 0x26)) WW(actor, 0x85e2);
        break;
    case 0x7855e:
        ride_parent_shaking(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x7b9a);
            if (sprite == 0xffff) {
                WW(actor + 0x42, 4);
                do {
                    child = spawn_far(ram, actor, STAGE_SEGMENT, 0x862b);
                    uint16_t index = RW(actor + 0x42) - 1;
                    ram[child + 0x28] = cs[(uint16_t)(index + 0x7bfc)];
                    WW(child + 0x21, RW(actor + 0x21) + CW(4 * index + 0x7c00));
                    WW(child + 0x24, RW(actor + 0x24) + CW(4 * index + 0x7c02));
                    ram[child + 0x2c] = ram[actor + 0x14];
                } while (decrement(ram, actor + 0x42));
                WW(actor + 0x42, 0x100);
                WW(actor, 0x8534);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, CW(frame + 0x7ba8));
                WW(actor + 0x40, frame + 2);
            }
        }
        draw_flashing(ram, main_rom, actor);
        exposed_contacts(ram, main_rom, actor);
        if (!RW(actor + 0x26)) WW(actor, 0x85e2);
        break;
    case 0x785e2:
        /* 7036: a chain of explosions read from 7000:7F56. */
        child = effect_slot(ram, actor, 0x7056);
        WW(child + 0x60, 0x7f56);
        WW(child + 0x62, STAGE_SEGMENT);
        WW(child + 0x4c, 2);
        WW(child + 0x42, 1);
        WW(actor + 0x42, 0x28);
        WW(actor, 0x85fa);
        /* fall through */
    case 0x785fa:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            ram[parent_of(ram, actor) + 0x0a] |= 2;
            finish_animation(ram, actor);
        }
        break;

    /* ---- Homing shot from the head. ---- */
    case 0x7862b: {
        ram[actor + 0x2d] = 4;
        WW(actor + 0x3c, 0xf94b);
        WW(actor + 0x2e, 0x80);
        ram[actor + 0x0c] = 4;
        WW(actor + 0x26, 0x80);
        WW(actor + 0x36, 0x80);
        WW(actor + 0x44, 1);
        WW(actor + 0x42, 0x80);
        WW(actor + 0x46, 0x300);
        heading_velocity(ram, main_rom, actor);
        unsigned sector = ram[actor + 0x28] >> 4;
        ram[actor + 0x29] = cs[0x7bec + sector];
        WW(actor + 0x4c, CW(0x7bdc + ((2 * sector) & 7)));
        WW(actor, 0x8680);
    }
        /* fall through */
    case 0x78680: {
        if (ram[parent_of(ram, actor) + 0x0a] & 2) {
            finish_animation(ram, actor);
            break;
        }
        if (RW(actor + 0x46)) {
            WW(actor + 0x46, RW(actor + 0x46) - 1);
            if (!decrement(ram, actor + 0x42)) {
                WW(actor + 0x42, 0x10);
                if (RW(0x264) & 3) {
                    uint16_t target = world_enemy_target(ram, actor);
                    uint8_t wanted = (uint8_t)((vector_angle(main_rom, RW(target + 0x21) - RW(actor + 0x21),
                                                             RW(target + 0x24) - RW(actor + 0x24)) + 8) & 0xf0);
                    uint8_t heading = ram[actor + 0x28];
                    if (wanted != heading) {
                        heading += (uint8_t)(wanted - heading) < (uint8_t)(heading - wanted) ? 8 : -8;
                        ram[actor + 0x28] = heading;
                        heading_velocity(ram, main_rom, actor);
                        unsigned sector = heading >> 4;
                        ram[actor + 0x29] = cs[0x7bec + sector];
                        WW(actor + 0x4c, CW(0x7bdc + 2 * (sector & 7)));
                        WW(actor + 0x44, 1);
                    }
                }
            }
        }
        move_x(ram, actor);
        move_y(ram, actor);
        if (outside_view(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, CW((frame & 6) + RW(actor + 0x4c) + 0x7bb4));
            WW(actor + 0x44, 4);
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor)) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, main_rom, actor);
        if (!RW(actor + 0x26)) finish_animation(ram, actor);
        break;
    }

    /* ---- The chain's lead piece in the fight: swing out, hold, withdraw. ---- */
    case 0x78f3b:
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, 0, 0);
        if ((RW(parent + 0x50) & 8) && (RW(parent + 0x54) & 2)) {
            if (!(RW(0x264) & 3)) {
                WW(parent_of(ram, actor) + 0x54, 0);
            } else {
                uint16_t target = world_enemy_target(ram, actor);
                WW(actor + 0x31, RW(target + 0x21) < 0x100 ? -0x58 : 0x58);
                WW(actor + 0x3a, 0xff80);
                WW(actor + 0x42, 0x48);
                WW(actor, 0x8fb1);
            }
        }
        ram[actor + 0x29] = 0;
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x29] = 1;
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x78fb1:
        move_offset_y(ram, actor);
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, 0, RW(actor + 0x34));
        ram[actor + 0x29] = 1;
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x29] = 0;
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            if (RW(actor + 0x31) & 0x8000) ram[actor + 0x29] = 1;
            WW(actor + 0x34, 0xff90);
            WW(actor + 0x3a, 0x80);
            WW(actor + 0x42, 0xe0);
            SET(actor + 0x54, 1);
            WW(actor, 0x900f);
        }
        break;
    case 0x7900f:
    case 0x79044:
    case 0x79079:
    case 0x790ae:
        if (address == 0x7900f || address == 0x790ae) move_offset_y(ram, actor);
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), RW(actor + 0x34));
        if (decrement(ram, actor + 0x42)) break;
        if (address == 0x7900f) {
            WW(actor + 0x42, 0x20);
            WW(actor, 0x9044);
        } else if (address == 0x79044) {
            WW(actor + 0x42, 0xa0);
            SET(actor + 0x54, 2);
            WW(actor, 0x9079);
        } else if (address == 0x79079) {
            WW(actor + 0x3a, 0xff80);
            WW(actor + 0x42, 0xe0);
            WW(actor, 0x90ae);
        } else {
            WW(actor + 0x31, 0);
            WW(actor + 0x34, 0xffdc);
            WW(actor + 0x3a, 0x80);
            WW(actor + 0x42, 0x48);
            WW(actor + 0x54, 4);
            WW(actor, 0x90fc);
        }
        break;
    case 0x790fc:
        move_offset_y(ram, actor);
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, 0, RW(actor + 0x34));
        ram[actor + 0x29] = 1;
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x29] = 0;
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(parent_of(ram, actor) + 0x54, 0);
            WW(actor + 0x54, 0);
            WW(actor, 0x8f3b);
        }
        break;

    /* ---- Chain segments following the lead piece's phases. ---- */
    case 0x791e5:
    case 0x7923a:
    case 0x7928d:
    case 0x792ef:
    case 0x79343:
    case 0x79391:
        if (address == 0x7923a || address == 0x79343) move_offset_y(ram, actor);
        if (address == 0x7928d) {
            if (!(decrement(ram, actor + 0x42) & 1)) {
                WW(actor + 0x3a, -RW(actor + 0x3a));
                move_offset_y(ram, actor);
            }
        }
        parent = parent_of(ram, actor);
        follow(ram, actor, parent, RW(actor + 0x31), RW(actor + 0x34));
        if (address == 0x791e5 && (RW(parent + 0x54) & 2)) {
            WW(actor + 0x42, 0x25);
            SET(actor + 0x54, 2);
            WW(actor, 0x923a);
        }
        if (address == 0x79391 && (RW(parent + 0x54) & 4)) {
            WW(actor + 0x54, 4);
            WW(actor, 0x9194);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0x7923a && !decrement(ram, actor + 0x42)) {
            WW(actor + 0x42, 4);
            WW(actor, 0x928d);
        } else if (address == 0x7928d && !RW(actor + 0x42)) {
            WW(actor + 0x42, 0x40);
            WW(actor, 0x92ef);
        } else if (address == 0x792ef && !decrement(ram, actor + 0x42)) {
            WW(actor + 0x3a, -((int16_t)RW(actor + 0x3a) >> 1));
            WW(actor + 0x42, 0x38);
            WW(actor, 0x9343);
        } else if (address == 0x79343 && !decrement(ram, actor + 0x42)) {
            WW(actor, 0x9391);
        }
        if (!RW(actor + 0x1e)) touch(ram, main_rom, actor, 1);
        break;

    /* ---- Rock bridges: a row of blocks that falls when one is shot or touched. ---- */
    case 0x79a32:
        WW(actor + 0x36, 1);
        WW(actor + 0x42, CW(0x9746 + (RW(actor + 0x1e) & 14)));
        ram[actor + 0x29] = cs[0x9a2c + ((RW(actor + 0x1e) & 14) >> 1)];
        child = spawn_far(ram, actor, STAGE_SEGMENT, 0x9ad1);
        WW(child + 0x21, RW(actor + 0x21));
        WW(child + 0x24, RW(actor + 0x24));
        ram[child + 0x29] = ram[actor + 0x29];
        WW(child + 0x42, RW(actor + 0x42));
        WW(child + 0x62, RW(actor + 8));
        WW(actor, 0x9a81);
        break;
    case 0x79a81:
        if (RW(actor + 0x60)) {
            WW(actor + 0x42, 0x20);
            WW(actor, 0x9a93);
        }
        break;
    case 0x79a93:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (decrement(ram, actor + 0x42)) break;
        if ((int16_t)RW(actor + 0x24) >= 0x90) {
            uint16_t row = RW(actor + 0x1e) & 14;
            tile_rect(ram, main_rom, tiles, 0x4000, CW(0x9752 + row), CW(0x975e + row), CW(0x976a + row), 0);
        }
        finish_animation(ram, actor);
        break;
    case 0x79ad1:
        WW(actor + 0x3c, 0xfa93);
        WW(actor + 0x26, 1);
        WW(actor + 0x36, 1);
        ram[actor + 0x2c] = 0x1b;
        ram[actor + 0x2d] = 1;
        WW(actor + 0x2a, RW(actor + 0x1e) ? 0x2b61 : 0x2b47);
        if ((uint16_t)(RW(actor + 0x1e) + 1) < RW(actor + 0x42)) {
            child = spawn_far(ram, actor, STAGE_SEGMENT, 0x9ad1);
            ram[child + 0x29] = ram[actor + 0x29];
            WW(child + 0x21, RW(actor + 0x21) + ((ram[actor + 0x29] & 1) ? -0x20 : 0x20));
            WW(child + 0x24, RW(actor + 0x24));
            WW(child + 0x1e, RW(actor + 0x1e) + 1);
            WW(child + 0x42, RW(actor + 0x42));
            WW(child + 0x62, RW(actor + 8));
        }
        WW(actor, 0x9b43);
        break;
    case 0x79b43:
        scroll_actor(ram, actor, 0xb71a);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (RW(actor + 0x26) && RW(parent_of(ram, actor) + 0x36) && !touches_golem(ram, main_rom, cs, actor))
            break;
        WW(RW(0x8500 + RW(actor + 8) - 2 * ((RW(actor + 8) - RW(actor + 0x62)) >> 1)) + 0x60, 1);
        ram[actor + 0x2c] |= 0x80;
        WW(actor + 0x44, 1);
        WW(actor + 0x3a, 0x100);
        WW(actor + 0x42, 0x20);
        WW(actor, 0x9bd0);
        break;
    case 0x79bd0:
        scroll_actor(ram, actor, 0xb71a);
        move_y(ram, actor);
        if ((int16_t)RW(actor + 0x3a) > (int16_t)0xff00) WW(actor + 0x3a, RW(actor + 0x3a) - 8);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x9776);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x9794));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            WW(actor + 0x36, 0);
            WW(parent_of(ram, actor) + 0x26, 0);
        }
        break;

    /* ---- The gate the golem's head pushes through. ---- */
    case 0x79cb0:
        ram[actor + 0x2c] = 0x9b;
        ram[actor + 0x2d] = 1;
        WW(actor + 0x2a, 0x24dd);
        WW(actor + 0x3c, 0xfa9b);
        WW(actor + 0x21, RW(actor + 0x21) - RW(0xb70a));
        WW(actor, 0x9cce);
        break;
    case 0x79cce: {
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t head = RW(0xb8c4);
        if (!world_shape_overlap(ram, main_rom, actor, head, RW(actor + 0x3c), RW(head + 0x3c))) break;
        uint16_t cell = 0x974;
        for (unsigned row = 0; row < 0x20; ++row) {
            for (unsigned i = 0; i < 6; ++i) {
                write_word(map, cell + 2, (read_word(map, cell + 2) & 0x7ff) | 0x3000);
                cell = (cell & 0xff00) | ((cell + 4) & 255);
            }
            cell = (cell & 0xff00) | ((cell - 0x18) & 255);
            cell = (cell & 0x00ff) | ((((cell >> 8) + 1) & 0x3f) << 8);
        }
        WW(actor + 0x3a, 0x100);
        WW(actor, 0x9d50);
        break;
    }
    case 0x79d50:
        scroll_actor(ram, actor, 0xb71a);
        move_y(ram, actor);
        WW(actor + 0x3a, RW(actor + 0x3a) - 8);
        if (below_view(ram, actor)) finish_animation(ram, actor);
        else queue_sprite(ram, main_rom, actor, 0);
        break;

    /* ---- The ceiling at the top: six plates that can be shot loose onto the golem. ---- */
    case 0x7a082:
        WW(actor + 0x3c, 0xfa8b);
        WW(actor + 0x31, 0x88);
        WW(actor + 0x42, 6);
        do {
            child = spawn_far(ram, actor, STAGE_SEGMENT, 0xa1a2);
            WW(child + 0x21, RW(actor + 0x31));
            WW(child + 0x24, 0x198);
            WW(child + 0x1e, RW(actor + 0x42) - 1);
            WW(actor + 0x31, RW(actor + 0x31) + 0x30);
        } while (decrement(ram, actor + 0x42));
        WW(actor, 0xa0bd);
        break;
    case 0x7a0bd:
        if (RW(actor + 0x52)) {
            WW(actor + 0x40, 0);
            WW(actor + 0x42, 1);
            WW(actor, 0xa0d7);
        }
        ceiling_sparks(ram, main_rom, actor);
        break;
    case 0x7a0d7:
    case 0x7a143: {
        if (RW(0x262) & 0x8000) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x42)) {
            uint16_t row = RW(actor + 0x40) + (address == 0x7a0d7 ? 0x98e6 : 0x9924);
            if (CW(row) == 0xffff) {
                WW(actor + 0x40, 0);
                WW(actor + 0x42, 1);
                if (address == 0x7a0d7) WW(actor, 0xa143);
                else if (!RW(actor + 0x52)) WW(actor, 0xa0bd);
                break;
            }
            WW(0x9eae, CW(row));
            WW(0x9eb0, 0x80);
            WW(0x9eb2, CW(row + 2));
            WW(0x9eb4, 0x20);
            WW(0x9eb5, 0x10);
            WW(actor + 0x42, CW(row + 4));
            WW(actor + 0x40, RW(actor + 0x40) + 6);
        }
        ceiling_sparks(ram, main_rom, actor);
        break;
    }
    case 0x7a1a2:
        WW(actor + 0x0e, 0x2f);
        ram[actor + 0x2c] = 0x1a;
        ram[actor + 0x2d] = 0x28;
        WW(actor + 0x3c, 0xfa83);
        WW(actor + 0x36, 0x180);
        WW(actor, 0xa1bf);
        break;
    case 0x7a1bf:
        ram[actor + 0x0d] = 1;
        if (!world_projectile_contact(ram, main_rom, actor)) break;
        SET(parent_of(ram, actor) + 0x52, 1u << (RW(actor + 0x1e) & 15));
        WW(actor + 0x2a, 0x250f);
        WW(actor + 0x40, 0);
        WW(actor + 0x42, 0x10);
        WW(actor + 0x44, 0x10);
        WW(actor + 0x3a, 0xfc00);
        WW(actor + 0x50, 3);
        WW(actor + 0x46, CW(0x98a0 + (world_random_byte(ram) & 2)));
        world_queue_sound(ram, 0x4d);
        WW(actor, 0xa21c);
        break;
    case 0x7a21c:
    case 0x7a27b:
    case 0x7a2b3:
        if ((RW(0x262) & 0x80) || (address == 0x7a2b3 && !RW(actor + 0x26))) {
            /* 7A2C2: shatter into eight pieces. */
            ram[actor + 0x28] = 0;
            do {
                child = spawn_after_head(ram, 0x9a, STAGE_SEGMENT, 0xa38d);
                WW(child + 0x21, RW(actor + 0x21));
                WW(child + 0x24, RW(actor + 0x24));
                ram[child + 0x28] = (uint8_t)((world_random_byte(ram) & 15) - 8 + ram[actor + 0x28]);
                ram[actor + 0x28] += 0x20;
            } while (ram[actor + 0x28]);
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor + 0x3a, 0xffc0);
            WW(actor + 0x42, 0x20);
            world_queue_sound(ram, 0x6b);
            WW(actor, 0xa339);
            goto plate_crumble;
        }
        if (address == 0x7a21c) {
            if (!(decrement(ram, actor + 0x42) & 3)) move_y(ram, actor);
            plate_wobble(ram, main_rom, cs, actor);
            if (!RW(actor + 0x42)) {
                if (!decrement(ram, actor + 0x50)) {
                    CLR(parent_of(ram, actor) + 0x52, 1u << (RW(actor + 0x1e) & 15));
                    WW(actor + 0x3a, 0);
                    WW(actor + 0x26, 0x1000);
                    WW(actor, 0xa2b3);
                } else {
                    WW(actor, 0xa27b);
                }
            }
            ram[actor + 0x0d] = 0;
            world_player_contact(ram, main_rom, actor);
        } else if (address == 0x7a27b) {
            plate_wobble(ram, main_rom, cs, actor);
            ram[actor + 0x0d] = 0;
            world_player_contact(ram, main_rom, actor);
            ram[actor + 0x0d] = 1;
            uint16_t shot = world_projectile_contact(ram, main_rom, actor);
            if (shot) {
                ram[actor + 0x0b] = ram[shot + 0x0b];
                WW(actor + 0x42, 0x10);
                world_queue_sound(ram, 0x4d);
                WW(actor, 0xa21c);
            }
        } else {
            if ((int16_t)RW(actor + 0x3a) > (int16_t)0xff00) WW(actor + 0x3a, RW(actor + 0x3a) - 4);
            move_y(ram, actor);
            if (below_view(ram, actor)) {
                WW(actor + 0x42, 0x40);
                WW(actor, 0xa37d);
            }
            plate_wobble(ram, main_rom, cs, actor);
            ram[actor + 0x0d] = 0;
            world_player_contact(ram, main_rom, actor);
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
            if (RW(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    case 0x7a339:
    plate_crumble:
        if (RW(actor + 0x42)) WW(actor + 0x42, RW(actor + 0x42) - 1);
        else move_y(ram, actor);
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x98a4);
            if (sprite == 0xffff) {
                WW(actor + 0x42, 0x10);
                WW(actor, 0xa37d);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x98c6));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7a37d:
        if (!decrement(ram, actor + 0x42)) {
            WW(actor + 0x24, 0x198);
            WW(actor, 0xa1a2);
        }
        break;
    case 0x7a38d: {
        ram[actor + 0x2c] = 0x1a;
        ram[actor + 0x2d] = 0x29;
        WW(actor + 0x2e, 0x200);
        WW(actor + 0x44, 1);
        heading_velocity(ram, main_rom, actor);
        unsigned pick = world_random_byte(ram) & 2;
        WW(actor + 0x42, CW(0x995e + pick));
        WW(actor + 0x46, CW(0x9962 + pick));
        WW(actor, 0xa3c2);
    }
        /* fall through */
    case 0x7a3c2:
        move_x(ram, actor);
        move_y(ram, actor);
        WW(actor + 0x3a, RW(actor + 0x3a) - 0x10);
        if (RW(actor + 0x21) > 0x1c0 || below_view(ram, actor) || RW(actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, CW(frame + RW(actor + 0x46) + 0x9932));
            WW(actor + 0x44, RW(actor + 0x42));
            WW(actor + 0x40, frame + 2);
            if (RW(actor + 0x40) >= 0x14) WW(actor + 0x40, 0);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x7a43b:
        ram[actor + 0x2c] = 0x1a;
        ram[actor + 0x2d] = 0x27;
        WW(actor + 0x44, 1);
        WW(actor, 0xa44d);
        /* fall through */
    case 0x7a44d:
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + 0x9966);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + 0x9984));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    /* ---- Bank B000 enemies first met in this stage. ---- */
    case 0xbe694:
        if (RW(0xc6) >= 0x10) {
            finish_animation(ram, actor);
            break;
        }
        WW(0xc6, RW(0xc6) + 1);
        world_acquire_resource(ram, actor, 0x49);
        world_acquire_resource(ram, actor, 0x48);
        WW(actor + 0x0e, 0x1d);
        ram[actor + 0x2d] = 0x20;
        WW(actor + 0x3c, 0xf6c3);
        WW(actor + 0x26, 0x180);
        WW(actor + 0x36, 0x180);
        WW(actor + 0x2e, 0x100);
        ram[actor + 0x0c] = 4;
        WW(actor + 0x50, 0x320);
        WW(actor + 0x44, 1);
        WW(actor + 0x52, read_word(main_rom + 0xb0000, 0xe4f4 + (RW(actor + 0x1e) & 2)));
        WW(actor, 0xe703);
        break;
    case 0xbe703:
    case 0xbe80f: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbe703) {
            if (above_view(ram, actor) || outside_columns(ram, actor) || RW(actor + 0x24) < 0x78) {
                WW(0xc6, RW(0xc6) - 1);
                finish_animation(ram, actor);
                break;
            }
            if (RW(actor + 0x50)) {
                WW(actor + 0x50, RW(actor + 0x50) - 1);
                if (!decrement(ram, actor + 0x52)) {
                    WW(actor + 0x52, 0x78);
                    WW(actor + 0x54, 0x18);
                    if (RW(0x264) & 3) {
                        uint16_t target = world_enemy_target(ram, actor);
                        ram[actor + 0x28] = (uint8_t)((vector_angle(main_rom, RW(target + 0x21) - RW(actor + 0x21),
                                                                    RW(target + 0x24) - RW(actor + 0x24)) + 8) & 0xf0);
                        heading_velocity(ram, main_rom, actor);
                        WW(actor, 0xe80f);
                    }
                }
            }
        } else {
            move_x(ram, actor);
            move_y(ram, actor);
            if (outside_view(ram, actor)) {
                WW(0xc6, RW(0xc6) - 1);
                finish_animation(ram, actor);
                break;
            }
            if (!decrement(ram, actor + 0x54)) WW(actor, 0xe703);
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            WW(actor + 0x2a, read_word(bank, 0xe464 + (frame & 14)));
            WW(actor + 0x44, 4);
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 2;
        uint16_t attacker = world_player_contact(ram, main_rom, actor);
        if (!attacker) {
            ram[actor + 0x0d] = 0;
            attacker = world_projectile_contact(ram, main_rom, actor);
            if (RW(actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, attacker);
        child = spawn_after_head(ram, 0x9c, 0xb000, 0xe883);
        WW(child + 0x21, RW(actor + 0x21));
        WW(child + 0x24, RW(actor + 0x24));
        WW(child + 0x14, RW(actor + 0x14));
        ram[child + 0x2c] = ram[actor + 0x15];
        WW(child + 0x0e, RW(actor + 0x0e));
        if (RW(actor + 0x1e) & 0x2000) CLR(parent_of(ram, actor) + 0x60, 1u << (RW(actor + 0x1e) & 15));
        finish_animation(ram, actor);
        break;
    }
    case 0xbe883:
        ram[actor + 0x2d] = 0x20;
        WW(actor + 0x36, 0x180);
        WW(actor + 0x40, 0);
        WW(actor + 0x44, 1);
        WW(actor, 0xe89b);
        /* fall through */
    case 0xbe89b: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (RW(actor + 0x0e) != 0x1d) {
            finish_animation(ram, actor);
            break;
        }
        scroll_actor(ram, actor, 0xb71a);
        if (outside_view(ram, actor)) {
            WW(0xc6, RW(0xc6) - 1);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = read_word(bank, frame + 0xe474);
            if (sprite == 0xffff) {
                WW(0xc6, RW(0xc6) - 1);
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, read_word(bank, frame + 0xe4a0));
            WW(actor + 0x3c, read_word(bank, frame + 0xe4ca));
            WW(actor + 0x40, frame + 2);
            if (frame == 10) ram[actor + 0x2c] = ram[actor + 0x14];
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (RW(actor + 0x3c)) {
            ram[actor + 0x0d] = 2;
            world_player_contact(ram, main_rom, actor);
            if (RW(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    case 0xbebc0:
        if (RW(0xb700) >= RW(actor + 0x1e)) finish_animation(ram, actor);
        else if (RW(0xb8c0) && !(RW(0x276) & 0x1f)) queue_sound_b(ram, 0x93);
        break;
    case 0xbebf5: {
        uint16_t script = read_word(main_rom + 0xb0000, 0xea06 + (RW(actor + 0x1e) & 6));
        if (!script_kind_supported(read_word(main_rom + 0xb0000, script))) return WORLD_HANDLER_STOP;
        WW(0xb8c0, RW(0xb8c0) + 1);
        world_acquire_resource(ram, actor, 0xa1);
        WW(actor + 0x0e, 0x1e);
        WW(actor + 0x2d, 0x20);
        WW(actor + 0x3c, 0xf6e3);
        WW(actor + 0x26, 0x200);
        WW(actor + 0x36, 0x200);
        script_begin(ram, main_rom, actor, script);
        WW(actor + 0x44, 1);
        WW(actor + 0x42, 1);
        WW(actor, 0xec3b);
        break;
    }
    case 0xbec3b: {
        /* A driller: it follows its script and opens the wall cells ahead of it. */
        const uint8_t *bank = main_rom + 0xb0000;
        if (!tiles) return WORLD_HANDLER_STOP;
        if (RW(actor + 0x24) >= 0x168) {
            scroll_actor(ram, actor, 0xb71a);
        } else {
            if (!script_supported(ram, main_rom, actor)) return WORLD_HANDLER_STOP;
            if (!script_step(ram, main_rom, actor) || outside_view(ram, actor)) {
                WW(0xb8c0, RW(0xb8c0) - 1);
                finish_animation(ram, actor);
                break;
            }
            if (!decrement(ram, actor + 0x44)) {
                unsigned sector = ram[actor + 0x28] >> 4;
                ram[actor + 0x29] = bank[0xe9f6 + sector];
                uint16_t row = read_word(bank, 0xe9e6 + ((2 * sector) & 14));
                WW(actor + 0x2a, read_word(bank, RW(actor + 0x40) + row + 0xe9c8));
                WW(actor + 0x44, 2);
                WW(actor + 0x40, RW(actor + 0x40) + 2);
                if (RW(actor + 0x40) >= 6) WW(actor + 0x40, 0);
            }
            if (!(ram[actor + 0x28] & 0x3f) && !decrement(ram, actor + 0x42)) {
                WW(actor + 0x42, 8);
                unsigned side = (RW(actor + 0x28) >> 4) & 12;
                uint16_t x = RW(actor + 0x21), y = RW(actor + 0x24);
                WW(actor + 0x21, x + read_word(bank, 0xebac + side));
                WW(actor + 0x24, y + read_word(bank, 0xebae + side));
                uint16_t cell = actor_cell(ram, actor);
                for (unsigned i = 0; i < 4; ++i) {
                    break_wall_cell(ram, cs, map, actor, cell);
                    if (i == 3) break;
                    if (side & 4) {
                        cell = (cell & 0xff00) | ((cell + 4) & 255);
                        WW(actor + 0x21, RW(actor + 0x21) + 8);
                    } else {
                        cell = (cell + 0x100) & 0x3fff;
                        WW(actor + 0x24, RW(actor + 0x24) - 8);
                    }
                }
                WW(actor + 0x24, y);
                WW(actor + 0x21, x);
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 0;
        uint16_t attacker = world_player_contact(ram, main_rom, actor);
        if (RW(actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            attacker = world_projectile_contact(ram, main_rom, actor);
            if (RW(actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, attacker);
        spawn_explosion(ram, actor);
        WW(0xb8c0, RW(0xb8c0) - 1);
        finish_animation(ram, actor);
        break;
    }

    case 0xb21ae: {
        /* Approach state of a bank B000 enemy; this stage alone enters it this way. */
        scroll_actor(ram, actor, 0xb71a);
        if (RW(actor + 0x21) < 0x40) {
            WW(0xc2, RW(0xc2) - 1);
            finish_animation(ram, actor);
            break;
        }
        if (RW(actor + 0x21) < 0x190) {
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor, 0x20de);
        }
        if (!queue_sprite(ram, main_rom, actor, 1)) {
            finish_animation(ram, actor);
            break;
        }
        ram[actor + 0x0d] = 2;
        uint16_t attacker = world_player_contact(ram, main_rom, actor);
        if (!attacker) {
            ram[actor + 0x0d] = 0;
            attacker = RW(actor + 0x46) ? world_projectile_contact(ram, main_rom, actor)
                                        : world_surface_contact(ram, main_rom, actor);
            if (RW(actor + 0x26)) {
                if (!RW(actor + 0x46) && RW(0xb6ea) < 0x10) {
                    WW(0xb6ee + RW(0xb6ea), actor);
                    WW(0xb6ea, RW(0xb6ea) + 2);
                }
                break;
            }
        }
        world_award_score(ram, main_rom, actor, attacker);
        spawn_explosion(ram, actor);
        WW(0xc2, RW(0xc2) - 1);
        finish_animation(ram, actor);
        break;
    }

    /* ---- Entry of the bank B000 flying enemy as this stage spawns it. ---- */
    case 0xbde26: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t script = read_word(bank, 0xdd36 + (RW(actor + 0x1e) & 14));
        uint16_t kind = read_word(bank, script);
        if (kind > 6 || (kind & 1)) return WORLD_HANDLER_STOP;
        world_acquire_resource(ram, actor, 0x12);
        world_acquire_resource(ram, actor, 0x0e);
        WW(actor + 0x26, 0x20);
        WW(actor + 0x36, 0x20);
        WW(actor + 0x3c, 0xf683);
        WW(actor + 0x0e, 0x1a);
        uint16_t row = read_word(bank, 0xdcd2 + RW(0x88) + (RW(actor + 0x1e) & 0x20)) + 0xdd12;
        WW(actor + 0x44, read_word(bank, row));
        WW(actor + 0x46, read_word(bank, row));
        WW(actor + 0x48, read_word(bank, row + 2));
        WW(actor + 0x4a, read_word(bank, row + 4));
        WW(actor + 0x4e, read_word(bank, row + 6));
        ram[actor + 0x2d] = 0x20;
        WW(actor + 0x2a, 0x3584);
        begin_linear_script(ram, main_rom, actor, script);
        child = spawn_far(ram, actor, 0xb000, 0xe1e8);
        ram[child + 0x2c] = ram[actor + 0x2c];
        WW(child + 0x4c, RW(actor + 0x4a));
        WW(child + 0x2e, RW(actor + 0x4e));
        WW(actor, 0xe01a);
        break;
    }

    /* ---- Background machinery animation. ---- */
    case 0x7a54c:
        WW(actor + 0x46, CW(0x99a0 + (RW(actor + 0x1e) & 2)));
        WW(actor + 0x48, CW(0x99a4 + (RW(actor + 0x1e) & 2)));
        world_acquire_resource(ram, actor, CW(0x99a8 + (RW(actor + 0x1e) & 2)));
        ram[actor + 0x2c] |= 0x80;
        WW(actor + 0x56, RW(actor + 0x21) < 0x100 ? 0x1a : 1);
        WW(actor, 0xa588);
        break;
    case 0x7a588:
        scroll_actor(ram, actor, 0xb71a);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
        } else if (RW(0xb70a)) {
            WW(actor + 0x54, RW(actor + 0x56));
            WW(actor, 0xa5d0);
        }
        break;
    case 0x7a5d0:
        scroll_actor(ram, actor, 0xb71a);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
        } else if (!decrement(ram, actor + 0x54)) {
            WW(actor + 0x40, 0);
            WW(actor + 0x44, 1);
            WW(actor, 0xa619);
        }
        break;
    case 0x7a619:
        scroll_actor(ram, actor, 0xb71a);
        if (below_view(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            frame = RW(actor + 0x40);
            sprite = CW(frame + RW(actor + 0x46));
            if (sprite == 0xffff) {
                WW(actor, 0xa588);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, CW(frame + RW(actor + 0x48)));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;

    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
