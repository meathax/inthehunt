/* Recovered World actor handlers for stage 1 (Arctic, stage index 0).
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

#define W(a) read_word(ram, (uint16_t)(a))
#define SW(a, v) write_word(ram, (uint16_t)(a), (uint16_t)(v))
#define RW(a) read_word(main_rom, (uint16_t)(a))
#define BW(a) read_word(bank, (uint16_t)(a))
#define AW(a) read_word(banka, (uint16_t)(a))
#define TW(a) read_word(bank1, (uint16_t)(a))

/* The actor one slot before this one in the per-pass slot table. */
static uint16_t slot_parent(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
}

/* 68DF: terrain material under a screen point (mask is 1 << (material & 15)). */
static unsigned terrain_material(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    return tiles[0x4000 + world_tile_address(map_x, map_y) + 3] >> 3;
}

/* 657D/6580: velocity from an angle and the speed at [bp+2E] (signed product). */
static void angle_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned angle)
{
    const uint8_t *bank = rom + 0xa0000;
    int16_t speed = (int16_t)read_word(ram, actor + 0x2e);
    angle = 2 * (angle & 255);
    write_word(ram, actor + 0x38, (uint16_t)(((int16_t)read_word(bank, 0x9e12 + angle) * (int32_t)speed) >> 8));
    write_word(ram, actor + 0x3a, (uint16_t)(((int16_t)read_word(bank, 0x9d92 + angle) * (int32_t)speed) >> 8));
}

/* 24-bit position = 24-bit centre + signed 16x16 product (675C..67AC tails). */
static void offset_axis(uint8_t *ram, uint16_t position, uint16_t centre, int16_t unit, int16_t scale)
{
    uint32_t value = ram[centre] + ((uint32_t)read_word(ram, centre + 1) << 8) + (uint32_t)((int32_t)unit * scale);
    ram[position] = (uint8_t)value;
    write_word(ram, position + 1, (uint16_t)(value >> 8));
}

static int16_t sine_of_product(const uint8_t *rom, uint16_t value, uint16_t rate)
{
    unsigned angle = (((uint32_t)value * rate) >> 8) & 255;
    return (int16_t)read_word(rom + 0xa0000, 0x9d92 + 2 * angle);
}

/* D68F/D692: load the flight-script record at `offset` in the actor's own
 * segment. Returns 0 at the end marker. Handles every record kind. */
static int script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    const uint8_t *s = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
#define S(n) read_word(s, (uint16_t)(offset + (n)))
#define A(n, v) write_word(ram, actor + (n), (uint16_t)(v))
    uint16_t kind = S(0);
    write_word(ram, actor + 0x7e, offset);
    if (kind == 0xffff) return 0;
    A(0x7a, kind);
    switch (kind) {
    case 0:
        A(0x7e, offset + 4); A(0x38, 0); A(0x3a, 0);
        break;
    case 2:
    case 14:
        ram[actor + 0x28] = (uint8_t)S(4);
        if (kind == 2 && S(6)) A(0x2e, S(6));
        angle_velocity(ram, rom, actor, ram[actor + 0x28]);
        A(0x78, 0); A(0x76, 0); A(0x7e, offset + 8);
        ram[actor + 0x20] = ram[actor + 0x23] = 0;
        break;
    case 4: {
        uint16_t speed = read_word(ram, actor + 0x2e);
        ram[actor + 0x28] = (uint8_t)S(4);
        A(0x2e, S(8));
        angle_velocity(ram, rom, actor, ram[actor + 0x28]);
        A(0x78, read_word(ram, actor + 0x38));
        A(0x76, read_word(ram, actor + 0x3a));
        A(0x2e, S(6) ? S(6) : speed);
        angle_velocity(ram, rom, actor, ram[actor + 0x28]);
        A(0x7e, offset + 10);
        ram[actor + 0x20] = ram[actor + 0x23] = 0;
        break;
    }
    case 6:
    case 8:
        A(0x1c, S(4)); A(0x1a, S(6));
        A(0x31, S(8) + read_word(ram, actor + 0x21));
        A(0x34, S(10) + read_word(ram, actor + 0x24));
        A(0x78, S(12));
        A(0x76, kind == 8 ? S(14) : 0);
        A(0x7e, offset + (kind == 8 ? 16 : 14));
        ram[actor + 0x20] = ram[actor + 0x23] = 0;
        break;
    case 10:
    case 12:
        A(0x78, S(4)); A(0x76, S(6));
        A(kind == 10 ? 0x38 : 0x3a, S(8));
        ram[actor + 0x28] = rom[0xda0f + (kind == 12 ? 2 : 0) + (S(8) >> 15)];
        A(0x31, read_word(ram, actor + 0x21));
        A(0x34, read_word(ram, actor + 0x24));
        A(0x7e, offset + 10);
        ram[actor + 0x20] = ram[actor + 0x23] = 0;
        break;
    case 16:
        A(0x78, S(4)); A(0x76, S(6)); A(0x38, S(8));
        A(0x7e, offset + 10);
        A(0x31, 0);
        A(0x34, read_word(ram, actor + 0x24));
        ram[actor + 0x20] = ram[actor + 0x30] = ram[actor + 0x23] = 0;
        break;
    case 18:
        A(0x78, S(4)); A(0x76, S(6)); A(0x3a, S(8));
        A(0x7e, offset + 10);
        A(0x31, read_word(ram, actor + 0x21));
        A(0x34, 0);
        ram[actor + 0x20] = ram[actor + 0x23] = ram[actor + 0x33] = 0;
        break;
    case 20:
        A(0x38, S(4)); A(0x3a, S(6));
        A(0x7e, offset + 8);
        break;
    default:
        return -1;
    }
    A(0x7c, S(2));
#undef S
#undef A
    return 1;
}

/* D6B7: advance the flight script one tick. 1 running, 0 script ended, -1 unknown record. */
static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t kind = read_word(ram, actor + 0x7a);
    uint16_t rate = read_word(ram, actor + 0x76), scale = read_word(ram, actor + 0x78);
    if (kind > 20 || (kind & 1)) return -1;
    if (kind == 6 || kind == 8) {
        scroll_actor(ram, actor + 0x10, 0xb71a);
        write_word(ram, actor + 0x1a, read_word(ram, actor + 0x1a) + scale);
        write_word(ram, actor + 0x1c, read_word(ram, actor + 0x1c) + rate);
        unsigned angle = 2 * ram[actor + 0x1b];
        int16_t radius = (int16_t)read_word(ram, actor + 0x1c);
        offset_axis(ram, actor + 0x20, actor + 0x30, (int16_t)read_word(rom + 0xa0000, 0x9e12 + angle), radius);
        offset_axis(ram, actor + 0x23, actor + 0x33, (int16_t)read_word(rom + 0xa0000, 0x9d92 + angle), radius);
        ram[actor + 0x28] = ram[actor + 0x1b] + ((scale & 0x8000) ? -0x40 : 0x40);
    } else {
        scroll_actor(ram, actor, 0xb71a);
        switch (kind) {
        case 2:
        case 4:
        case 14:
            write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) + scale);
            move_actor_axis(ram, actor + (kind == 14 ? 0x30 : 0x20), read_word(ram, actor + 0x38));
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) + rate);
            move_actor_axis(ram, actor + (kind == 14 ? 0x33 : 0x23), read_word(ram, actor + 0x3a));
            break;
        case 10:
            scroll_actor(ram, actor + 0x10, 0xb71a);
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            offset_axis(ram, actor + 0x23, actor + 0x33,
                        sine_of_product(rom, read_word(ram, actor + 0x21) - read_word(ram, actor + 0x31), rate), (int16_t)scale);
            break;
        case 12:
            scroll_actor(ram, actor + 0x10, 0xb71a);
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            offset_axis(ram, actor + 0x20, actor + 0x30,
                        sine_of_product(rom, read_word(ram, actor + 0x24) - read_word(ram, actor + 0x34), rate), (int16_t)scale);
            break;
        case 16:
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            offset_axis(ram, actor + 0x23, actor + 0x33, sine_of_product(rom, read_word(ram, actor + 0x31), rate), (int16_t)scale);
            break;
        case 18:
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71c));
            move_actor_axis(ram, actor + 0x33, read_word(ram, actor + 0x3a));
            offset_axis(ram, actor + 0x20, actor + 0x30, sine_of_product(rom, read_word(ram, actor + 0x34), rate), (int16_t)scale);
            break;
        case 20:
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            break;
        default:
            break;
        }
    }
    if (decrement(ram, actor + 0x7c)) return 1;
    return script_begin(ram, rom, actor, read_word(ram, actor + 0x7e));
}

/* Play-field limits shared by the player weapons: nonzero when (x, y) is outside. */
static int outside_field(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
    int fixed = ram[0x26e] & 2;
    if (x > 0x1c0 || x < 0x40) return 1;
    if (y > (uint16_t)(fixed ? 0x198 : 0x1c8 + read_word(ram, 0xb732))) return 1;
    return (int16_t)y < (int16_t)(fixed ? 0x68 : 0x10 + read_word(ram, 0xb732));
}

/* Top and side limits used by enemy shots (no lower limit). */
static int outside_top_sides(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21);
    if (x > 0x1c0 || x < 0x40) return 1;
    return (int16_t)read_word(ram, actor + 0x24) <
           (int16_t)((ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732));
}

/* D237 then D2B7 with B7250: returns the BX the original leaves behind. */
static uint16_t part_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor, int always)
{
    uint16_t bx = world_player_contact(ram, rom, actor);
    if (!always && !read_word(ram, actor + 0x26)) return bx;
    if (!bx) bx = read_word(ram, 0x92);
    uint16_t shot = world_projectile_contact(ram, rom, actor);
    if (shot) {
        uint16_t parent = slot_parent(ram, actor);
        ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 0x20;
        return shot;
    }
    uint16_t end = read_word(ram, 0xa6e2);
    return end ? read_word(ram, 0xa6e6 + end - 2) : bx;
}

/* B7269: a sinking wreck drags an overlapping player down. */
static void wreck_drag(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t player)
{
    if (!world_shape_overlap(ram, rom, actor, player, read_word(ram, actor + 0x3c), read_word(ram, player + 0x3c)))
        return;
    write_word(ram, player + 0x24, read_word(ram, player + 0x24) + 8);
    write_word(ram, player + 0x5a, read_word(ram, player + 0x5a) + 0x10);
    write_word(ram, actor + 0x5e, read_word(ram, actor + 0x5e) + 0x20);
}

/* B721D: periodic smoke puff behind a shell. */
static void shell_smoke(uint8_t *ram, uint16_t actor)
{
    if (decrement(ram, actor + 0x54)) return;
    write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
    uint16_t puff = effect_slot(ram, read_word(ram, 0x9a), 0xbe45);
    write_word(ram, puff + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, puff + 0x24, read_word(ram, actor + 0x24));
    ram[puff + 0x2c] = ram[actor + 0x14];
    ram[puff + 0x2d] = 0x28;
    write_word(ram, puff + 0x1e, read_word(ram, actor + 0x6e));
}

/* BC52A: the depth-4 ancestor recorded in the slot table. */
static uint16_t root_actor(const uint8_t *ram, uint16_t actor)
{
    uint16_t depth = read_word(ram, actor + 8);
    return read_word(ram, 0x8500 + depth - 2 * ((uint16_t)(depth - 4) >> 1));
}

static uint8_t *terrain_cell(uint8_t *tiles, uint16_t cell)
{
    return tiles + 0x4000 + ((cell + 2) & 0xffff);
}

static uint16_t next_cell_row(uint16_t cell)
{
    return (uint16_t)((((cell >> 8) + 1) & 0x3f) << 8 | ((cell - 0x10) & 0xff));
}

/* BC564: stamp a web of solid cells from a ROM shape and remember where. */
static void web_stamp(uint8_t *ram, const uint8_t *bank, uint8_t *tiles, uint16_t actor, uint16_t shape)
{
    ram[actor + 0x66] |= 1;
    uint16_t x = read_word(ram, actor + 0x21) + read_word(bank, shape);
    uint16_t y = read_word(ram, actor + 0x24) + read_word(bank, shape + 2);
    shape += 4;
    uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
    write_word(ram, actor + 0x4c, cell);
    for (;;) {
        for (unsigned n = 0; n < 4; ++n) {
            uint16_t attr = read_word(bank, shape);
            if (attr == 0xffff) return;
            uint8_t *t = terrain_cell(tiles, cell);
            uint16_t value = ((t[0] | t[1] << 8) & 0x7ff) | attr;
            t[0] = (uint8_t)value; t[1] = (uint8_t)(value >> 8);
            shape += 2;
            cell = (cell & 0xff00) | ((cell + 4) & 0xff);
        }
        cell = next_cell_row(cell);
        write_word(ram, actor + 0x4a, read_word(ram, actor + 0x4a) + 1);
    }
}

/* BC5AF clears the web; BC5DB weakens its 4000 cells to 4800. */
static void web_rewrite(uint8_t *ram, uint8_t *tiles, uint16_t actor, int clear)
{
    uint16_t cell = read_word(ram, actor + 0x4c);
    unsigned rows = clear ? read_word(ram, actor + 0x4a) : ram[actor + 0x4a];
    do {
        for (unsigned n = 0; n < 4; ++n) {
            uint8_t *t = terrain_cell(tiles, cell);
            uint16_t value = t[0] | t[1] << 8;
            if (clear) value = (value & 0x7ff) | 0x3000;
            else if ((value & 0xf800) == 0x4000) value = (value & 0x7ff) | 0x4800;
            t[0] = (uint8_t)value; t[1] = (uint8_t)(value >> 8);
            cell = (cell & 0xff00) | ((cell + 4) & 0xff);
        }
        cell = next_cell_row(cell);
        rows = clear ? (rows - 1) & 0xffff : (rows - 1) & 0xff;
        if (clear) write_word(ram, actor + 0x4a, (uint16_t)rows);
    } while (rows);
}

/* BC616: a tongue tip that overlaps a free target spawns the grab actor. */
static void tongue_grab(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t flags = read_word(ram, 0xb8b2), shape = read_word(ram, actor + 0x3c);
    uint16_t target = 0, which = 0;
    int found = 0;
    for (which = 0; which < 8 && !found; which += 2) {
        if (flags & (1u << which)) continue;
        if (which < 4) {
            target = read_word(ram, 0x90 + which);
            found = read_word(ram, target + 0x3c) != 0xffff &&
                    world_shape_overlap(ram, rom, actor, target, shape, 0x78db);
        } else {
            target = read_word(ram, 0xb8b8 + which - 4);
            found = target && read_word(ram, target) != 0xffff &&
                    world_shape_overlap(ram, rom, actor, target, shape, read_word(ram, target + 0x3c));
        }
        if (found) break;
    }
    if (!found) return;
    write_word(ram, 0xb8b2, flags | (1u << which));
    uint16_t grab = bank_b_effect(ram, actor, 0xc0c0);
    write_word(ram, grab + 0x21, read_word(ram, target + 0x21));
    write_word(ram, grab + 0x24, read_word(ram, target + 0x24));
    ram[grab + 0x29] = ram[actor + 0x29];
    write_word(ram, grab + 0x60, read_word(ram, actor + 0x60));
    write_word(ram, grab + 0x68, which);
    write_word(ram, grab + 0x6a, target);
    ram[grab + 0x0b] = ram[target + 0x0b];
}

/* B9C9F with B9D08: escort ship fire, shots parented to the list head at [9C]. */
static void escort_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + 0xb0000;
    uint8_t facing = ram[actor + 0x29];
    unsigned side = 2 * (facing & 1);
    if (!decrement(ram, actor + 0x50)) {
        write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
        uint16_t shot = effect_slot(ram, read_word(ram, 0x9c), 0x97ee);
        if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
        ram[shot + 0x29] = facing;
        write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + read_word(bank, 0x8d78 + side));
        write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
        write_word(ram, shot + 0x38, read_word(ram, actor + 0x60));
        write_word(ram, shot + 0x72, 1);
        write_word(ram, shot + 0x0e, 0x1b);
        uint16_t flash = effect_slot(ram, actor, 0xa268);
        ram[flash + 0x29] = facing;
        write_word(ram, flash + 0x31, read_word(bank, 0x8d78 + side));
        ram[flash + 0x2d] = 3;
    }
    if (read_word(ram, actor + 0x74)) { decrement(ram, actor + 0x74); return; }
    if (!above_cruiser(ram, actor, read_word(ram, 0x90)) && !above_cruiser(ram, actor, read_word(ram, 0x92)))
        return;
    uint16_t shot = effect_slot(ram, read_word(ram, 0x9c), 0x98e4);
    if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
    ram[shot + 0x29] = facing;
    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21) + read_word(bank, 0x8d7c + side));
    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 4);
    write_word(ram, shot + 0x3a, read_word(ram, actor + 0x62));
    write_word(ram, shot + 0x72, 1);
    write_word(ram, shot + 0x0e, 0x1b);
    uint16_t flash = effect_slot(ram, actor, 0xa2c0);
    ram[flash + 0x29] = facing;
    write_word(ram, flash + 0x31, read_word(bank, 0x8d7c + side));
    write_word(ram, flash + 0x34, 4);
    ram[flash + 0x2d] = 3;
    write_word(ram, actor + 0x74, read_word(ram, actor + 0x5e));
}

static uint16_t swap16(uint16_t value) { return (uint16_t)(value >> 8 | value << 8); }

/* DC25: copy 4x4-cell tile strips (every other word) from ROM into a tile layer. */
static void draw_strips(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t base,
                        uint16_t dest, uint16_t sequence, unsigned columns, unsigned groups)
{
    unsigned left = columns;
    write_word(ram, 0xb89a, (uint16_t)(groups << 8 | columns));
    write_word(ram, 0xb89c, dest);
    for (;;) {
        const uint8_t *source = rom + ((uint32_t)(read_word(rom + 0xa0000, sequence) & 0xfffc) << 4);
        uint16_t row_dest = dest;
        for (unsigned row = 0; row < 4; ++row) {
            for (unsigned col = 0; col < 16; col += 4)
                write_word(tiles, (uint16_t)(base + (uint16_t)(row_dest + col)), read_word(source, 16 * row + col));
            row_dest = (row_dest + 0x100) & 0x3fff;
        }
        sequence += 2;
        if (--left) { dest = (dest + 0x400) & 0x3fff; continue; }
        if (!--groups) break;
        left = columns;
        ram[0xb89c] += 0x10;
        dest = read_word(ram, 0xb89c);
    }
}

/* 11F36: boss shell contact tail. */
static void shell_contacts(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    ram[actor + 0x0d] = 2;
    int hit = world_player_contact(ram, rom, actor) != 0;
    if (!hit) {
        ram[actor + 0x0d] = 0;
        world_projectile_contact(ram, rom, actor);
        hit = !read_word(ram, actor + 0x26);
    }
    if (hit) {
        warship_part_explosion(ram, rom, actor);
        finish_animation(ram, actor);
    } else if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
}

int world_stage1_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank = main_rom + 0xb0000, *banka = main_rom + 0xa0000, *bank1 = main_rom + 0x10000;
    (void)pass;
    if (!tiles) return WORLD_HANDLER_UNKNOWN;
    switch (address) {
    /* Launch flash that follows the submarine. */
    case 0x07fb9:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x3e;
        SW(actor + 0x58, 1);
        SW(actor + 0x42, 0x14);
        SW(actor, 0x7fd1);
        break;
    case 0x07fd1:
    case 0x07ff5:
        if (address == 0x07fd1) {
            uint16_t sub = W(actor + 0x7c);
            SW(actor + 0x21, W(sub + 0x21) + RW(0x7955 + W(actor + 0x68)));
            SW(actor + 0x24, W(sub + 0x24) + 4);
            if (!decrement(ram, actor + 0x42)) {
                SW(actor, 0x7ff5);
                scroll_actor(ram, actor, 0xb716);
            }
        } else scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40), sprite = RW(0x7835 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x58, RW(0x7857 + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* Radial blast attached to the previous slot. */
    case 0x06f34: {
        uint16_t parent = slot_parent(ram, actor);
        SW(actor + 0x21, W(parent + 0x21) + W(actor + 0x31));
        SW(actor + 0x24, W(parent + 0x24) + W(actor + 0x34));
        if (W(actor + 0x44)) world_acquire_resource(ram, actor, W(actor + 0x44));
        SW(actor + 0x3c, 0xffff);
        SW(actor + 0x40, 0);
        ram[actor + 0x48] = 1;
        SW(actor + 0x64, 0x10);
        SW(actor + 0x66, 0x10);
        SW(actor + 0x42, 8);
        SW(actor, 0x6fb1);
        break;
    }
    /* Homing missile. */
    case 0x0b4ce:
    case 0x0b52f:
    case 0x0b553:
    case 0x0b57b:
    case 0x0b5eb: {
        if (address == 0x0b4ce) {
            world_acquire_resource(ram, actor, 0x5b);
            world_acquire_resource(ram, actor, RW(0x9bb1 + (ram[actor + 0x0b] & 2)));
            SW(actor + 0x3c, 0xb3c3);
            SW(actor + 0x26, 4);
            SW(actor + 0x36, 0xa0);
            SW(actor + 0x2e, 0x200);
            SW(actor + 0x42, 4);
            SW(actor + 0x5a, 0x10);
            SW(actor + 0x4a, 0);
            ram[actor + 0x28] += main_rom[(uint16_t)(0xb3d7 + W(actor + 0x78))];
            SW(actor + 0x2a, RW(0xb337 + (ram[actor + 0x28] >> 2)));
            angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
            SW(actor, 0xb52f);
            address = 0x0b52f;
        }
        if (address == 0x0b52f) {
            if (!decrement(ram, actor + 0x5a)) {
                SW(actor + 0x4a, 1);
                SW(actor + 0x2e, 0x400);
                angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
                SW(actor, 0xb553);
                SW(actor + 0x5a, 1);
            }
        } else if (address == 0x0b553 || address == 0x0b57b) {
            if (address == 0x0b553) {
                uint16_t count = W(0xb6ea), target = 0;
                if (count) {
                    uint16_t cursor = W(0xb6ec);
                    if (cursor >= count) cursor = 0;
                    target = W(0xb6ee + cursor);
                    SW(0xb6ec, cursor + 2);
                }
                SW(actor + 0x4e, target);
                SW(actor, 0xb57b);
            }
            uint16_t target = W(actor + 0x4e);
            if (!target || (ram[target + 0x0a] & 1)) SW(actor, 0xb5eb);
            else if (!decrement(ram, actor + 0x5a)) {
                SW(actor + 0x5a, 2);
                uint8_t aim = vector_angle(main_rom, (int16_t)(W(target + 0x21) - W(actor + 0x21)),
                                           (int16_t)(W(target + 0x24) - W(actor + 0x24)));
                uint8_t heading = ram[actor + 0x28];
                aim = (aim + 4) & 0xf8;
                if (aim != heading)
                    aim = ((uint8_t)(aim - heading) < (uint8_t)(heading - aim) ? heading + 8 : heading - 8) & 0xf8;
                ram[actor + 0x28] = aim;
                SW(actor + 0x2a, RW(0xb337 + (aim >> 2)));
                angle_velocity(ram, main_rom, actor, aim & 0xfc);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            SW(actor + 0x42, 4);
            uint16_t trail = effect_slot(ram, W(0x9a), 0x9e17);
            unsigned index = (ram[actor + 0x28] & 0xf8) >> 1;
            SW(trail + 0x21, W(actor + 0x21) + RW(0xb3e1 + index));
            SW(trail + 0x24, W(actor + 0x24) + RW(0xb3e3 + index));
            ram[trail + 0x2c] = ram[actor + 0x14];
            SW(trail + 0x56, 0x60);
            SW(trail + 0x58, 0x11);
        }
        unsigned material = terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24));
        if ((1u << (material & 15)) & 0x5502) spawn_impact(ram, main_rom, actor);
        else if (W(actor + 0x4a) && material == 4)
            spawn_surface_splash_effect(ram, main_rom, actor, W(actor + 0x21), W(actor + 0x24));
        else if (outside_field(ram, actor)) { }
        else if (!W(actor + 0x26)) spawn_impact(ram, main_rom, actor);
        else {
            if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
            break;
        }
        uint16_t parent = slot_parent(ram, actor);
        SW(parent + 0x44, W(parent + 0x44) - 1);
        finish_animation(ram, actor);
        break;
    }
    /* Warship deck launcher: opening animation, then the launched depth charge. */
    case 0xb6150:
        world_acquire_resource(ram, actor, 0x66);
        world_acquire_resource(ram, actor, 0xe7);
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 3;
        SW(actor + 0x44, 1);
        SW(actor, 0x6176);
        /* fall through */
    case 0xb6176:
    case 0xb643d: {
        uint16_t parent = slot_parent(ram, actor);
        uint16_t place = 2 * W(actor + 0x46) + 2 * W(actor + 0x60);
        if (address != 0xb643d) {
            SW(actor + 0x21, BW(0x51b6 + place) + W(parent + 0x21));
            SW(actor + 0x24, BW(0x51b8 + place) + W(parent + 0x24));
            if (decrement(ram, actor + 0x44)) break;
            uint16_t frame = BW(0x5192 + W(actor + 0x46) + W(actor + 0x60)) + W(actor + 0x40);
            uint16_t sprite = BW(0x50a8 + frame);
            if (sprite != 0xffff) {
                SW(actor + 0x2a, sprite);
                SW(actor + 0x44, 3);
                SW(actor + 0x40, W(actor + 0x40) + 2);
                queue_sprite(ram, main_rom, actor, 0);
                break;
            }
            if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
            uint16_t shot = world_spawn_child(ram, actor, 0x6272);
            if (shot) {
                uint16_t barrel = W(actor + 0x46);
                SW(shot + 2, 0xb000);
                SW(shot + 0x14, W(actor + 0x15));
                ram[shot + 0x2c] = ram[actor + 0x16];
                SW(shot + 0x2e, W(actor + 0x2e));
                SW(shot + 0x40, barrel);
                SW(shot + 0x46, BW(0x537e + barrel + (W(actor + 0x60) ? 0x12 : 0)));
                ram[shot + 0x29] = bank[(uint16_t)(0x56e9 + (barrel >> 1))];
                SW(shot + 0x60, 2 * W(actor + 0x60));
                SW(shot + 0x21, W(parent + 0x21));
                SW(shot + 0x24, W(parent + 0x24));
                world_queue_sound(ram, 0x57);
            }
            ram[actor + 0x2c] = ram[actor + 0x14];
            SW(actor + 0x40, 0);
            SW(actor + 0x44, 1);
            SW(actor, 0x643d);
        }
        SW(actor + 0x21, BW(0x52fc + place) + W(parent + 0x21));
        SW(actor + 0x24, BW(0x52fe + place) + W(parent + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = BW(0x52d8 + W(actor + 0x46) + W(actor + 0x60)) + W(actor + 0x40);
            uint16_t sprite = BW(0x51fe + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, BW(0x52c4 + W(actor + 0x40)));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb6272: {
        uint16_t barrel = W(actor + 0x40);
        ram[actor + 0x2d] = 4;
        SW(actor + 0x26, 1);
        SW(actor + 0x36, 0x800);
        SW(actor + 0x3c, BW(0x53a2 + barrel));
        uint16_t place = 2 * barrel + W(actor + 0x60);
        SW(actor + 0x21, W(actor + 0x21) + BW(0x53b8 + place));
        SW(actor + 0x24, W(actor + 0x24) + BW(0x53ba + place));
        ram[actor + 0x28] = bank[(uint16_t)(0x56fa + (barrel >> 1))];
        angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        SW(actor + 0x42, 8);
        SW(actor, 0x62cc);
        break;
    }
    case 0xb62cc:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) SW(actor, 0x62dc);
        break;
    case 0xb62dc:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if (outside_top_sides(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t phase = W(actor + 0x40) & 2;
            SW(actor + 0x2a, BW(0x5356 + phase + W(actor + 0x46)));
            SW(actor + 0x44, BW(0x53b4 + phase));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        if (world_player_contact(ram, main_rom, actor)) {
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if ((int16_t)W(actor + 0x24) > (int16_t)(W(0xb732) - 8) &&
            !((1u << (terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24) + 4) & 15)) & 0xff02))
            break;
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x28;
        SW(actor + 0x3c, 0xf48b);
        SW(actor, 0x63b3);
        break;
    case 0xb63b3:
        scroll_actor(ram, actor, 0xb71a);
        if (W(actor + 0x21) < 1) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = BW(0x4d28 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, BW(0x4d4c + frame));
            SW(actor + 0x3c, BW(0x4d6e + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (W(actor + 0x3c) && !(W(0x274) & 3)) {
            if (ram[0x264] & 1) wreck_drag(ram, main_rom, actor, W(0x90));
            if (ram[0x264] & 2) wreck_drag(ram, main_rom, actor, W(0x92));
        }
        break;
    /* Warship gun turret and its shell. */
    case 0xb64f3:
    case 0xb653e: {
        if (address == 0xb64f3) {
            ram[actor + 0x0c] = 4;
            SW(actor + 0x26, 0x300);
            SW(actor + 0x36, 0x300);
            SW(actor + 0x40, 0);
            SW(actor + 0x44, 1);
            SW(actor + 0x42, 0x40);
            ram[actor + 0x28] = 0xc0;
            uint16_t cfg = BW(0x5512 + (W(actor + 0x1e) & 0x20) + W(0x8c)) + 0x55d2;
            SW(actor + 0x50, BW(cfg + 4));
            SW(actor + 0x52, BW(cfg + 4));
            SW(actor + 0x2e, BW(cfg + 6));
            SW(actor, 0x653e);
        }
        if (!warship_attachment(ram, actor)) break;
        aim_warship_turret(ram, main_rom, actor, ram[actor + 0x29] & 1);
        SW(actor + 0x2a, BW(0x5058 + BW(0x507c + W(actor + 0x60)) + W(actor + 0x40)));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = W(actor + 0x21);
        if (x <= 0x1c0 && x >= 0x40 && !decrement(ram, actor + 0x50)) {
            SW(actor + 0x50, W(actor + 0x52));
            SW(actor + 0x42, 0x20);
            uint16_t barrel = BW(0x4fea + ((ram[actor + 0x28] & 0xf0) >> 3));
            SW(actor + 0x46, barrel);
            SW(actor + 0x38, BW(0x5080 + 2 * barrel));
            SW(actor + 0x3a, BW(0x5082 + 2 * barrel));
            uint16_t shell = world_spawn_child(ram, actor, 0x665e);
            if (shell) {
                SW(shell + 2, 0xb000);
                SW(shell + 0x21, x + W(actor + 0x38));
                SW(shell + 0x24, W(actor + 0x24) + W(actor + 0x3a));
                ram[shell + 0x2c] = ram[actor + 0x2c];
                SW(shell + 0x2e, W(actor + 0x2e));
                SW(shell + 0x46, barrel);
                uint16_t flash = world_spawn_child(ram, actor, 0xa2c0);
                if (flash) {
                    SW(flash + 0x31, W(actor + 0x38));
                    SW(flash + 0x34, W(actor + 0x3a));
                    ram[flash + 0x29] = ram[actor + 0x29];
                    ram[flash + 0x2d] = ram[actor + 0x2d];
                    SW(flash + 0x60, 4);
                }
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = part_contacts(ram, main_rom, actor, 0);
        if (W(actor + 0x26)) break;
        world_award_score(ram, main_rom, actor, hit);
        warship_part_explosion(ram, main_rom, actor);
        SW(actor + 0x2a, BW(0x50a4 + W(actor + 0x60)));
        SW(actor, 0x6928);
        break;
    }
    case 0xb665e:
    case 0xb66bb: {
        if (address == 0xb665e) {
            uint16_t barrel = W(actor + 0x46);
            ram[actor + 0x2d] = 4;
            SW(actor + 0x3c, 0xf47b);
            SW(actor + 0x26, 1);
            SW(actor + 0x36, 0x800);
            SW(actor + 0x54, 4);
            SW(actor + 0x56, 6);
            SW(actor + 0x6e, 1);
            ram[actor + 0x14] = 3;
            SW(actor + 0x46, BW(0x5344 + barrel));
            ram[actor + 0x28] = bank[(uint16_t)(0x5703 + (barrel >> 1))];
            angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
            SW(actor + 0x2a, BW(0x4f2c + (((ram[actor + 0x28] + 8) & 0xf0) >> 3)));
            SW(actor, 0x66bb);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if (outside_top_sides(ram, actor)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        shell_smoke(ram, actor);
        ram[actor + 0x0d] = 2;
        if (world_player_contact(ram, main_rom, actor) ||
            ((1u << (terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24)) & 15)) & 0xff02)) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    /* Warship mine chute: drops rising mines, then burns as a wreck. */
    case 0xb67f3:
    case 0xb6844: {
        if (address == 0xb67f3) {
            SW(actor + 0x26, 0x300);
            SW(actor + 0x36, 0x300);
            SW(actor + 0x40, 0);
            SW(actor + 0x44, 1);
            SW(actor + 0x42, 0x40);
            SW(actor + 0x46, 6);
            uint16_t cfg = BW(0x5512 + (W(actor + 0x1e) & 0x20) + W(0x8c)) + 0x55d2;
            SW(actor + 0x50, BW(cfg));
            SW(actor + 0x52, BW(cfg));
            SW(actor + 0x2e, BW(cfg + 2));
            SW(actor + 0x54, 2);
            SW(actor + 0x56, 0);
            SW(actor, 0x6844);
        }
        if (!warship_attachment(ram, actor)) break;
        if (!decrement(ram, actor + 0x44)) {
            SW(actor + 0x2a, BW(0x5418 + (W(actor + 0x40) & 6) + W(actor + 0x60)));
            SW(actor + 0x44, BW(0x5430 + W(actor + 0x46)));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (W(actor + 0x46) && !decrement(ram, actor + 0x42)) {
            SW(actor + 0x42, 0x40);
            SW(actor + 0x46, W(actor + 0x46) - 2);
        }
        int drop = 0;
        if (W(actor + 0x50)) drop = !decrement(ram, actor + 0x50);
        else if (!decrement(ram, actor + 0x54)) {
            SW(actor + 0x54, 2);
            SW(actor + 0x56, W(actor + 0x56) + 1);
            if (W(actor + 0x56) >= 0x3c) {
                SW(actor + 0x50, W(actor + 0x52));
                SW(actor + 0x56, 0);
            }
            drop = 1;
        }
        if (drop) {
            uint16_t mine = world_spawn_child(ram, actor, 0x696e);
            if (mine) {
                SW(mine + 2, 0xb000);
                SW(mine + 0x24, W(actor + 0x24) - 8);
                SW(mine + 0x3a, -W(actor + 0x2e));
                SW(mine + 0x42, (W(actor + 0x56) + 2) >> 1);
                SW(mine + 0x40, W(actor + 0x56) & 0xf);
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = part_contacts(ram, main_rom, actor, 0);
        if (W(actor + 0x26)) break;
        world_award_score(ram, main_rom, actor, hit);
        SW(actor + 0x2a, BW(0x5438 + (W(actor + 0x60) >> 2)));
        SW(actor, 0x6928);
        break;
    }
    case 0xb6928: {
        if (!warship_attachment(ram, actor)) break;
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        SW(actor + 0x26, 0x7f00);
        ram[actor + 0x0d] = 0;
        /* D237 ran with flag 2 above; BX on entry to D2B7 is its player slot. */
        uint16_t bx;
        {
            uint16_t first = W(0x90);
            int p1 = W(first + 0x3c) != 0xffff &&
                     world_shape_overlap(ram, main_rom, actor, first, W(actor + 0x3c), W(first + 0x3c));
            bx = p1 ? first : W(0x92);
            uint16_t shot = world_projectile_contact(ram, main_rom, actor);
            uint16_t end = W(0xa6e2);
            if (shot) {
                uint16_t parent = slot_parent(ram, actor);
                ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 0x20;
                bx = shot;
            } else if (end) bx = W(0xa6e6 + end - 2);
        }
        uint16_t parent = slot_parent(ram, actor);
        int16_t pool = (int16_t)(W(parent + 0x48) - (0x7f00 - W(actor + 0x26)));
        SW(parent + 0x48, pool);
        if (pool <= 0) {
            SW(parent + 0x26, 0);
            SW(parent + 0x74, bx);
        }
        break;
    }
    case 0xb696e:
        world_acquire_resource(ram, actor, 0x71);
        ram[actor + 0x2d] = 0x29;
        SW(actor + 0x3c, 0xf47b);
        SW(actor + 0x26, 0x80);
        SW(actor + 0x36, 0x80);
        SW(actor + 0x2a, 0);
        SW(actor + 0x44, 1);
        SW(actor, 0x6998);
        /* fall through */
    case 0xb6998: {
        scroll_actor(ram, actor, 0xb71a);
        SW(actor + 0x21, W(slot_parent(ram, actor) + 0x21));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if ((int16_t)W(actor + 0x24) < (int16_t)((ram[0x26e] & 2) ? 0x68 : 0x10 + W(0xb732))) {
            finish_animation(ram, actor);
            break;
        }
        int draw = 1;
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40);
            if (frame & 1) draw = 0;
            else SW(actor + 0x2a, BW(0x5400 + (frame & 0xe)));
            SW(actor + 0x44, 2);
            SW(actor + 0x40, frame + 1);
        }
        if (draw) queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42) ||
            ((1u << (terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24) - 8) & 15)) & 0xff02))
            finish_animation(ram, actor);
        break;
    }
    /* Scripted surface enemy: replaces the older case that stopped on some script records. */
    case 0xb92ce: {
        if (W(actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        int step = script_step(ram, main_rom, actor);
        if (step < 0) return WORLD_HANDLER_STOP;
        if (!step) {
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        update_cruiser_fire(ram, main_rom, actor, W(actor + 0x50) ? 0x50 : 0x54);
        draw_cruiser_and_check_hits(ram, main_rom, actor);
        break;
    }
    /* Secondary launcher that follows the submarine (all weapon kinds). */
    case 0x0a6b3: {
        uint16_t sub = slot_parent(ram, actor);
        uint16_t config = W(sub + 0x70);
        SW(actor + 0x7c, sub);
        ram[actor + 0x29] = ram[sub + 0x29];
        SW(actor + 0x21, W(sub + 0x21));
        SW(actor + 0x24, W(sub + 0x24));
        SW(actor + 0x60, W(sub + 0x60));
        if (!W(config + 0x16)) break;
        uint16_t kind = W(config + 0x14);
        if (kind != 2 && kind != 4) return WORLD_HANDLER_STOP;
        SW(actor + 0x7a, W(config + 0x16));
        SW(actor + 0x40, 2 * (int8_t)ram[sub + 0x41]);
        if (kind != W(actor + 0x78)) {
            SW(actor + 0x78, kind);
            ram[actor + 0x28] = 0x40;
        }
        if (kind == 2) { fire_secondary_launcher(ram, main_rom, actor); break; }
        unsigned input = ram[0xb73e] & 3;
        uint8_t angle = ram[actor + 0x28];
        if (!input) angle += angle == 0x40 ? 0 : angle < 0x40 ? 2 : -2;
        else if (input & 1) angle -= angle > 0x28 ? 2 : 0;
        else angle += angle < 0x58 ? 2 : 0;
        ram[actor + 0x28] = angle;
        if (W(actor + 0x4c)) { decrement(ram, actor + 0x4c); break; }
        if (!(ram[0xb742] & 0x40)) {
            if (!W(actor + 0x48)) SW(actor + 0x46, 0);
            break;
        }
        unsigned facing = ram[actor + 0x29], side = 2 * (facing & 1);
        unsigned owner = ((ram[actor + 0x0b] & 3) - 1) & 15;
        uint16_t shot;
        if (W(actor + 0x60) == 3) {
            shot = world_spawn_child(ram, actor, 0xbaa5);
            if (!shot) break;
            ram[shot + 0x2d] = ram[actor + 0x2d];
            ram[shot + 0x0b] = ram[actor + 0x0b];
            SW(0xb8a2, W(0xb8a2) | (1u << owner));
            SW(shot + 0x60, W(actor + 0x60));
            SW(shot + 0x7a, W(actor + 0x7a) + 1);
            SW(actor + 0x4c, RW(0xa9a3 + 2 * ((W(actor + 0x7a) - 1) & 3)));
        } else {
            if ((int16_t)W(actor + 0x48) >= (int16_t)(W(actor + 0x7a) + 1)) break;
            shot = world_spawn_child(ram, actor, 0xbc73);
            if (!shot) break;
            world_queue_sound(ram, 0x38);
            ram[shot + 0x29] = facing;
            SW(shot + 0x21, W(actor + 0x21) + RW(0xa99f + side));
            SW(shot + 0x24, W(actor + 0x24) + 6);
            ram[shot + 0x2d] = ram[actor + 0x2d] + 1;
            ram[shot + 0x0b] = ram[actor + 0x0b];
            SW(0xb8a2, W(0xb8a2) | (1u << owner));
            SW(shot + 0x60, W(actor + 0x60));
            ram[shot + 0x28] = (angle + 4) & 0xf8;
            SW(shot + 0x46, W(actor + 0x46));
            SW(shot + 0x54, W(actor + 0x54) & 3);
            SW(actor + 0x54, W(actor + 0x54) + 1);
            SW(actor + 0x46, W(actor + 0x46) + 1);
        }
        uint16_t flash = effect_slot(ram, actor, 0xa2c0);
        ram[flash + 0x29] = facing;
        SW(flash + 0x31, RW(0x78c7 + side));
        SW(flash + 0x34, 4);
        ram[flash + 0x2d] = ram[actor + 0x2d];
        SW(flash + 0x60, W(actor + 0x60));
        if (W(actor + 0x60) != 3) {
            SW(actor + 0x48, W(actor + 0x48) + 1);
            SW(actor + 0x4c, 8);
        }
        break;
    }
    /* Lobbed depth bomb of the second weapon kind. */
    case 0x0bc73:
        world_acquire_resource(ram, actor, 0x8f);
        SW(actor + 0x3c, 0xba85);
        SW(actor + 0x26, 1);
        SW(actor + 0x36, 0x90);
        SW(actor + 0x2e, 0x300);
        ram[actor + 0x28] = main_rom[0xbaa1 + (W(actor + 0x46) & 3)];
        angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
        SW(actor + 0x5a, 0x80);
        SW(actor + 0x58, 1);
        SW(actor, 0xbcb3);
        break;
    case 0x0bcb3:
    case 0x0bcf3:
    case 0x0bd9a: {
        int live = 1;
        if (address == 0x0bd9a) {
            SW(actor + 0x31, W(actor + 0x31) + W(0xb71a));
            SW(actor + 0x34, W(actor + 0x34) + W(0xb71c));
            SW(actor + 0x21, W(actor + 0x21) + W(0xb71a));
            offset_axis(ram, actor + 0x23, actor + 0x33,
                        sine_of_product(main_rom, W(actor + 0x21) - W(actor + 0x31), 0x400), 3);
            decrement(ram, actor + 0x31);
            if (W(actor + 0x26)) {
                if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
            } else live = 0;
            if (live && !decrement(ram, actor + 0x58)) {
                SW(actor + 0x2a, RW(0xba69 + W(actor + 0x40)));
                SW(actor + 0x58, 8);
                SW(actor + 0x40, W(actor + 0x40) + 2 >= 0x14 ? 0 : W(actor + 0x40) + 2);
            }
        } else {
            if (!decrement(ram, actor + 0x58)) {
                uint16_t frame = W(actor + 0x40);
                if (address == 0x0bcf3) {
                    SW(actor + 0x2a, RW(0xba55 + frame));
                    SW(actor + 0x58, 8);
                    SW(actor + 0x40, frame + 2 >= 0x14 ? 0 : frame + 2);
                } else if (RW(0xba2b + frame) == 0xffff) {
                    SW(actor + 0x40, 0);
                    SW(actor + 0x58, 1);
                    SW(actor + 0x38, 0);
                    SW(actor + 0x3a, 0x100);
                    SW(actor, 0xbcf3);
                } else {
                    SW(actor + 0x2a, RW(0xba2b + frame));
                    SW(actor + 0x58, RW(0xba41 + frame));
                    SW(actor + 0x40, frame + 2);
                }
            }
            scroll_actor(ram, actor, 0xb716);
            move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
            move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
            if (!W(actor + 0x26)) live = 0;
            else {
                if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                unsigned material = terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24) + 8);
                if ((1u << (material & 15)) & 0x5502) {
                    if (W(0x84) == 4 && material == 1) return WORLD_HANDLER_STOP;
                    live = 0;
                } else if (material == 3) {
                    SW(actor + 0x31, W(actor + 0x21));
                    SW(actor + 0x34, W(actor + 0x24) - 2);
                    SW(actor + 0x40, 0);
                    SW(actor + 0x58, 1);
                    SW(actor, 0xbd9a);
                }
            }
        }
        if (!live) {
            uint16_t parent = slot_parent(ram, actor);
            SW(parent + 0x48, W(parent + 0x48) - 1);
            SW(actor + 0x36, 0xc);
            SW(actor, 0xa199);
        }
        if (address == 0x0bd9a && !live) {
            /* BDDA still runs the sprite step after the state change. */
            if (!decrement(ram, actor + 0x58)) {
                SW(actor + 0x2a, RW(0xba69 + W(actor + 0x40)));
                SW(actor + 0x58, 8);
                SW(actor + 0x40, W(actor + 0x40) + 2 >= 0x14 ? 0 : W(actor + 0x40) + 2);
            }
        }
        if (!decrement(ram, actor + 0x5a)) SW(actor + 0x26, 0);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* Formation spawner: one child per table record, then retires. */
    case 0xb9036: {
        uint16_t record = BW(0x8d98 + (W(actor + 0x1e) & 0xe));
        for (;;) {
            SW(actor + 0x40, record);
            unsigned which = (W(actor + 0x1e) & 1) << 2;
            uint16_t child = world_spawn_child(ram, actor, BW(0x8e00 + which));
            if (!child) break;
            SW(child + 2, BW(0x8e02 + which));
            SW(child + 0x21, W(actor + 0x21) + BW(record));
            SW(child + 0x24, W(actor + 0x24) + BW(record + 2));
            SW(child + 0x1e, BW(record + 4));
            record += 6;
            if (BW(record) == 0xffff) break;
        }
        finish_animation(ram, actor);
        break;
    }
    /* Ice block: animated body plus a second contact shape carried by a child. */
    case 0xba3ba: {
        ram[actor + 0x2d] = 0x28;
        SW(actor + 0x26, 0x7f00);
        SW(actor + 0x36, 0x180);
        uint16_t half = bank_b_effect(ram, actor, 0xa466);
        SW(half + 0x26, 0x7f00);
        SW(half + 0x36, 0x180);
        SW(actor + 0x44, 1);
        SW(actor, 0xa3e7);
    }
        /* fall through */
    case 0xba3e7: {
        scroll_actor(ram, actor, 0xb71a);
        if (W(actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = BW(0x9fd0 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, BW(0x9ff2 + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t shape = BW(0xa044 + 2 * (uint16_t)(W(actor + 0x40) - 2));
        if (shape) {
            SW(actor + 0x3c, shape);
            ram[actor + 0x0d] = 1;
            world_player_contact(ram, main_rom, actor);
            if (shape != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    case 0xba466: {
        uint16_t body = W(0x8500 + W(actor + 8));
        if (ram[body + 0x0a]) { finish_animation(ram, actor); break; }
        SW(actor + 0x21, W(body + 0x21));
        SW(actor + 0x24, W(body + 0x24));
        uint16_t shape = BW(0xa046 + 2 * (uint16_t)(W(body + 0x40) - 2));
        if (shape) {
            SW(actor + 0x3c, shape);
            ram[actor + 0x0d] = 1;
            world_player_contact(ram, main_rom, actor);
            if (shape != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    /* Repaint a block of terrain attributes from a ROM table, then retire. */
    case 0xbb8e0: {
        uint16_t cell = 0x2a98, source = 0xb8a4;
        for (unsigned row = 0; row < 6; ++row) {
            uint16_t c = cell;
            for (unsigned col = 0; col < 5; ++col) {
                uint8_t *t = terrain_cell(tiles, c);
                uint16_t value = ((t[0] | t[1] << 8) & 0x7ff) | BW(source);
                t[0] = (uint8_t)value; t[1] = (uint8_t)(value >> 8);
                source += 2;
                c = (c & 0xff00) | ((c + 4) & 0xff);
            }
            cell = (cell + 0x100) & 0x3fff;
        }
        finish_animation(ram, actor);
        break;
    }
    /* Wall creature with a tongue that webs the passage. */
    case 0xbb92b: {
        uint16_t kind = W(actor + 0x1e) & 0xe;
        SW(actor + 0x60, kind);
        world_acquire_resource(ram, actor, BW(kind - 0x519a));
        SW(actor + 0x0e, 0x16);
        ram[actor + 0x2d] = 0x3f;
        SW(actor + 0x3c, 0xf5d3);
        uint16_t cfg = BW((W(actor + 0x1e) & 0x20) + W(0x8c) - 0x47ac);
        SW(actor + 0x26, BW(cfg - 0x476c));
        SW(actor + 0x36, BW(cfg - 0x476c));
        SW(actor + 0x52, BW(cfg - 0x476a));
        SW(actor + 0x50, 1);
        SW(actor + 0x2a, BW(kind - 0x51aa));
        SW(actor + 0x46, BW(kind - 0x505a));
        SW(actor + 0x42, BW((ram[actor + 0x1f] & 0xe) - 0x47bc));
        SW(actor, 0xb99d);
        break;
    }
    case 0xbb99d:
    case 0xbbaf8:
        scroll_actor(ram, actor, 0xb71a);
        if (W(actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xbb99d && W(actor + 0x42) >= W(actor + 0x21)) {
            SW(actor + 0x44, 1);
            SW(actor, 0xb9d4);
        }
        break;
    case 0xbb9d4:
    case 0xbba49: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t kind = W(actor + 0x60);
        if (address == 0xbb9d4) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = W(actor + 0x40), sprite = BW(frame + W(actor + 0x46));
                if (sprite == 0xffff) SW(actor, 0xba49);
                else {
                    SW(actor + 0x2a, sprite);
                    SW(actor + 0x44, BW(frame - 0x4fde));
                    SW(actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
            if (W(actor + 0x40) < 8) break;
        } else {
            if (W(actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
            queue_sprite(ram, main_rom, actor, 0);
            int open;
            if (kind == 0xe && !(W(0xb8b4) & 0x8000)) open = W(0xb8b4) & 1;
            else {
                open = !decrement(ram, actor + 0x50);
                if (open) SW(actor + 0x50, W(actor + 0x52));
            }
            if (open) {
                SW(actor + 0x46, BW(kind - 0x4fbe));
                SW(actor + 0x40, 0);
                SW(actor + 0x44, 1);
                if (kind == 6) SW(0xb8b4, W(0xb8b4) | 1);
                SW(0xb8b4, W(0xb8b4) & ~2u);
                SW(actor, 0xbb1d);
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t shot = world_projectile_contact(ram, main_rom, actor);
        if (W(actor + 0x26)) break;
        world_award_score(ram, main_rom, actor, shot);
        warship_part_explosion(ram, main_rom, actor);
        SW(actor + 0x2a, BW(kind - 0x4fce));
        ram[actor + 0x0a] = 2;
        if (kind == 6) SW(0xb8b4, W(0xb8b4) | 0x8000);
        SW(actor, 0xbaf8);
        break;
    }
    case 0xbbb1d:
    case 0xbbb51:
    case 0xbbb9f: {
        if (address == 0xbbb1d) {
            if (W(actor + 0x60) == 6) SW(0xb8b4, W(0xb8b4) & ~1u);
            SW(actor + 0x66, 0);
            SW(actor + 0x64, 0);
            uint16_t tongue = bank_b_effect(ram, actor, 0xbd06);
            SW(tongue + 0x60, W(actor + 0x60));
            world_queue_sound(ram, 0x79);
            SW(actor, 0xbb51);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbbb9f) {
            SW(actor + 0x2a, BW((W(actor + 0x40) & 2) + W(actor + 0x46)));
            SW(actor + 0x40, W(actor + 0x40) + 2);
            queue_sprite(ram, main_rom, actor, 0);
            if (W(actor + 0x64)) {
                SW(actor + 0x3c, BW(W(actor + 0x60) - 0x4902));
                SW(actor + 0x4e, W(actor + 0x26));
                SW(actor + 0x26, 0x300);
                SW(actor + 0x66, W(actor + 0x66) | 2);
                SW(actor, 0xbbe6);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = W(actor + 0x40), delay = BW(frame - 0x4f4a);
                if (delay == 0xffff) {
                    SW(actor + 0x40, 0);
                    SW(actor, 0xbb9f);
                } else {
                    SW(actor + 0x44, delay);
                    SW(actor + 0x2a, BW((frame & 2) + W(actor + 0x46)));
                    SW(actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        ram[actor + 0x0d] = 3;
        uint16_t shot = world_projectile_contact(ram, main_rom, actor);
        if (shot) SW(shot + 0x26, 0);
        break;
    }
    case 0xbbbe6: {
        uint16_t kind = W(actor + 0x60);
        scroll_actor(ram, actor, 0xb71a);
        if ((int16_t)W(actor + 0x21) <= (int16_t)BW(kind - 0x4f96)) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = W(actor + 0x21), y = W(actor + 0x24);
        uint16_t path = BW(kind - 0x48e2);
        int paired = kind == 6 || kind == 0xe, cut = 0, flag = 0;
        SW(actor + 0x42, BW(kind - 0x48f2));
        SW(actor + 0x21, x + BW(path));
        SW(actor + 0x24, y + BW(path + 2));
        SW(actor + 0x31, BW(path + 4));
        SW(actor + 0x34, BW(path + 6));
        for (;;) {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (!W(actor + 0x26) || (W(actor + 0x66) & 4)) { cut = flag = 1; break; }
            if (paired && (W(0xb8b4) & 2)) { cut = 1; break; }
            SW(actor + 0x21, W(actor + 0x21) + W(actor + 0x31));
            SW(actor + 0x24, W(actor + 0x24) + W(actor + 0x34));
            if (!decrement(ram, actor + 0x42)) break;
        }
        SW(actor + 0x21, x);
        SW(actor + 0x24, y);
        if (!cut) break;
        if (flag && paired) SW(0xb8b4, W(0xb8b4) | 2);
        SW(actor + 0x42, BW(kind - 0x48f2));
        SW(actor + 0x31, BW(path));
        SW(actor + 0x34, BW(path + 2));
        do {
            uint16_t piece = bank_b_effect(ram, actor, 0xc4bb);
            SW(piece + 0x21, x + W(actor + 0x31));
            SW(piece + 0x24, y + W(actor + 0x34));
            SW(piece + 0x60, kind);
            SW(actor + 0x31, W(actor + 0x31) + BW(path + 4));
            SW(actor + 0x34, W(actor + 0x34) + BW(path + 6));
        } while (decrement(ram, actor + 0x42));
        SW(actor + 0x66, W(actor + 0x66) | 1);
        SW(actor + 0x26, W(actor + 0x4e));
        SW(actor, 0xba49);
        break;
    }
    /* Tongue root: grows, launches the tip, lays web sections, then signals the creature. */
    case 0xbbd06:
    case 0xbbd31: {
        uint16_t kind = W(actor + 0x60);
        if (address == 0xbbd06) {
            world_acquire_resource(ram, actor, 0x2f);
            ram[actor + 0x2d] = 0x3f;
            SW(actor + 0x46, BW(kind - 0x4f76));
            ram[actor + 0x29] = bank[(uint16_t)((kind >> 1) - 0x4f3c)];
            SW(actor + 0x44, 1);
            SW(actor, 0xbd31);
        }
        uint16_t root = root_actor(ram, actor);
        if (ram[root + 0x0a]) { finish_animation(ram, actor); break; }
        SW(actor + 0x21, W(root + 0x21));
        SW(actor + 0x24, W(root + 0x24));
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = BW(frame + W(actor + 0x46));
            if (sprite == 0xffff) {
                SW(actor + 0x2a, BW(kind - 0x4f34));
                uint16_t part = bank_b_effect(ram, actor, 0xbf0c);
                SW(part + 0x60, kind);
                ram[part + 0x2c] = ram[actor + 0x2c];
                ram[part + 0x29] = ram[actor + 0x29];
                SW(actor, 0xbda2);
            } else {
                SW(actor + 0x2a, sprite);
                SW(actor + 0x44, BW(frame - 0x4f4a));
                SW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbbda2:
        SW(actor + 0x42, BW(W(actor + 0x60) - 0x4ec8));
        SW(actor, 0xbdb2);
        /* fall through */
    case 0xbbdb2:
    case 0xbbe4b:
    case 0xbbeba: {
        uint16_t kind = W(actor + 0x60);
        uint16_t root = root_actor(ram, actor);
        if (ram[root + 0x0a]) { finish_animation(ram, actor); break; }
        SW(actor + 0x21, W(root + 0x21));
        SW(actor + 0x24, W(root + 0x24));
        if (W(0x274) & 1) queue_sprite(ram, main_rom, actor, 0);
        if (decrement(ram, actor + 0x42)) {
            if (address == 0xbbda2 || address == 0xbbdb2) {
                if (W(actor + 0x42) & 1) break;
                uint16_t link = bank_b_effect(ram, actor, 0xbfc9);
                SW(link + 0x60, kind);
                ram[link + 0x2c] = ram[actor + 0x2c];
                ram[link + 0x29] = ram[actor + 0x29];
                SW(link + 0x42, 2 * (W(actor + 0x42) - 2));
            }
            break;
        }
        if (address == 0xbbda2 || address == 0xbbdb2) {
            SW(actor + 0x40, 0);
            SW(actor + 0x42, 1);
            SW(actor + 0x48, BW(kind - 0x4e64));
            uint16_t tip = bank_b_effect(ram, actor, 0xc02d);
            SW(tip + 0x21, W(actor + 0x21));
            SW(tip + 0x24, W(actor + 0x24));
            SW(tip + 0x60, kind);
            ram[tip + 0x29] = ram[actor + 0x29];
            SW(actor, 0xbe4b);
        } else if (address == 0xbbe4b) {
            uint16_t step = W(actor + 0x40), delay = BW(step + W(actor + 0x48));
            if (delay == 0xffff) {
                SW(actor + 0x42, 0xc);
                SW(actor, 0xbeba);
                break;
            }
            SW(actor + 0x42, delay);
            uint16_t web = bank_b_effect(ram, actor, 0xc389);
            SW(web + 0x60, kind);
            SW(web + 0x62, step);
            ram[web + 0x2c] = ram[actor + 0x2c];
            ram[web + 0x29] = ram[actor + 0x29];
            SW(actor + 0x40, step + 2);
        } else {
            SW(slot_parent(ram, actor) + 0x64, 1);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xbbf0c: {
        uint16_t kind = W(actor + 0x60);
        ram[actor + 0x2d] = 0x3f;
        SW(actor + 0x3c, BW(kind - 0x4f86));
        SW(actor + 0x46, BW(kind - 0x4f24));
        SW(actor + 0x42, BW(kind - 0x4ec8));
        SW(actor + 0x31, BW(2 * kind - 0x4f08));
        SW(actor + 0x34, BW(2 * kind - 0x4f06));
        SW(actor + 0x38, BW(2 * kind - 0x4ee8));
        SW(actor + 0x3a, BW(2 * kind - 0x4ee6));
        SW(actor, 0xbf53);
        break;
    }
    case 0xbbfc9: {
        uint16_t kind = W(actor + 0x60);
        ram[actor + 0x2d] = 0x3f;
        SW(actor + 0x2a, BW(kind - 0x4eb8));
        uint16_t place = BW(kind - 0x4ea8) + W(actor + 0x42);
        SW(actor + 0x31, BW(place));
        SW(actor + 0x34, BW(place + 2));
        SW(actor, 0xbff3);
        break;
    }
    case 0xbbf53:
    case 0xbbff3: {
        uint16_t root = root_actor(ram, actor);
        if (ram[root + 0x0a]) { finish_animation(ram, actor); break; }
        SW(actor + 0x21, W(root + 0x21));
        SW(actor + 0x24, W(root + 0x24));
        if (W(root + 0x64)) { finish_animation(ram, actor); break; }
        if (address == 0xbbf53) {
            move_actor_axis(ram, actor + 0x30, W(actor + 0x38));
            move_actor_axis(ram, actor + 0x33, W(actor + 0x3a));
        }
        SW(actor + 0x21, W(actor + 0x21) + W(actor + 0x31));
        SW(actor + 0x24, W(actor + 0x24) + W(actor + 0x34));
        if (W(0x274) & 1) {
            if (address == 0xbbf53) {
                SW(actor + 0x2a, BW(W(actor + 0x40) + W(actor + 0x46)));
                SW(actor + 0x40, W(actor + 0x40) + 2 >= 6 ? 0 : W(actor + 0x40) + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (address == 0xbbf53 && W(actor + 0x42) && !decrement(ram, actor + 0x42)) {
            SW(actor + 0x38, 0);
            SW(actor + 0x3a, 0);
        }
        break;
    }
    /* Tongue tip: flies out in steps and grabs what it touches. */
    case 0xbc02d: {
        uint16_t kind = W(actor + 0x60);
        SW(actor + 0x3c, 0xf5fb);
        SW(actor + 0x42, BW(kind - 0x4882));
        SW(actor + 0x44, BW(kind - 0x4852));
        SW(actor + 0x21, W(actor + 0x21) + BW(2 * kind - 0x48a2));
        SW(actor + 0x24, W(actor + 0x24) + BW(2 * kind - 0x48a0));
        SW(actor + 0x38, BW(2 * kind - 0x4872));
        SW(actor + 0x3a, BW(2 * kind - 0x4870));
        SW(actor + 0x2a, 0x7e4);
        SW(actor, 0xc072);
        break;
    }
    case 0xbc072:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            SW(actor + 0x42, 1);
            SW(actor, 0xc087);
        }
        break;
    case 0xbc087:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) {
            if (!decrement(ram, actor + 0x44)) { finish_animation(ram, actor); break; }
            SW(actor + 0x42, 1);
            move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
            move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        }
        tongue_grab(ram, main_rom, actor);
        break;
    /* Web section: unfolds, turns its cells solid, and is removed with the creature. */
    case 0xbc389: {
        uint16_t kind = W(actor + 0x60), step = W(actor + 0x62);
        ram[actor + 0x2d] = 0x3e;
        SW(actor + 0x3c, 0xf5db);
        SW(actor + 0x26, 0x300);
        SW(actor + 0x36, 0x400);
        SW(actor + 0x46, BW(BW(kind - 0x4d82) + step));
        SW(actor + 0x48, BW(step - 0x4c96));
        uint16_t place = BW(kind - 0x4e1a) + 2 * step;
        SW(actor + 0x31, BW(place));
        SW(actor + 0x34, BW(place + 2));
        SW(actor + 0x44, 1);
        SW(actor, 0xc3d8);
        break;
    }
    case 0xbc3d8:
    case 0xbc469: {
        uint16_t root = root_actor(ram, actor);
        int gone = ram[root + 0x0a] != 0;
        if (!gone) {
            SW(actor + 0x21, W(root + 0x21));
            SW(actor + 0x24, W(root + 0x24));
            gone = address == 0xbc469 && (W(root + 0x66) & 1);
        }
        if (gone) {
            if (address == 0xbc469 && (W(actor + 0x66) & 1)) web_rewrite(ram, tiles, actor, 1);
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xbc469) {
            if ((W(root + 0x66) & 2) && !(W(actor + 0x66) & 2)) {
                web_rewrite(ram, tiles, actor, 0);
                SW(actor + 0x66, W(actor + 0x66) | 2);
            }
        }
        SW(actor + 0x21, W(actor + 0x21) + W(actor + 0x31));
        SW(actor + 0x24, W(actor + 0x24) + W(actor + 0x34));
        if (address == 0xbc3d8 && !decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = BW(frame + W(actor + 0x46));
            if (sprite == 0xffff) {
                uint16_t kind = W(actor + 0x60), step = W(actor + 0x62);
                sprite = BW(BW(kind - 0x4c60) + step);
                if (!sprite) { finish_animation(ram, actor); break; }
                SW(actor + 0x2a, sprite);
                uint16_t shape = BW(BW(kind - 0x4c00) + step);
                if (shape) web_stamp(ram, bank, tiles, actor, shape);
                SW(actor, 0xc469);
            } else {
                SW(actor + 0x2a, sprite);
                SW(actor + 0x44, BW(frame + W(actor + 0x48)));
                SW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbc4bb:
        world_acquire_resource(ram, actor, 0x2f);
        ram[actor + 0x2d] = 0x3f;
        ram[actor + 0x29] = bank[(uint16_t)((W(actor + 0x60) >> 1) - 0x490a)];
        SW(actor + 0x46, BW(W(actor + 0x60) - 0x4954));
        SW(actor + 0x44, 1);
        SW(actor, 0xc4e9);
        /* fall through */
    case 0xbc4e9:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = BW(frame + W(actor + 0x46));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, BW(frame - 0x491c));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* Falling debris that sinks until it meets terrain or the surface. */
    case 0xb75b6:
    case 0xb75e9: {
        if (address == 0xb75b6) {
            if (W(actor + 0x21) >= 0x198) SW(actor + 0x21, 0x198);
            else if ((int16_t)W(actor + 0x21) < 0x68) SW(actor + 0x21, 0x68);
            SW(actor + 0x40, 0);
            SW(actor + 0x44, 1);
            SW(actor + 0x3a, 0);
            SW(actor + 0x42, 0x20);
            SW(actor, 0x75e9);
        }
        if (W(actor + 0x42)) {
            SW(actor + 0x3a, W(actor + 0x3a) - 0x10);
            decrement(ram, actor + 0x42);
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if (outside_top_sides(ram, actor)) { finish_animation(ram, actor); break; }
        unsigned material = terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24));
        if ((1u << (material & 15)) & 0x5502) {
            surface_enemy_death_blast(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (material == 4) {
            water_entry_splash(ram, main_rom, actor, W(actor + 0x21), W(actor + 0x24));
            SW(actor + 0x3a, 0xffc0);
            SW(actor, 0x76a6);
        }
        if (!decrement(ram, actor + 0x44)) {
            SW(actor + 0x2a, BW(0x728a + W(actor + 0x40)));
            SW(actor + 0x44, 6);
            SW(actor + 0x40, W(actor + 0x40) + 2 >= 0xc ? 0 : W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    /* Escort pair generator and the escort ships it sends in from the right. */
    case 0xb92f7:
        world_acquire_resource(ram, actor, 0x60);
        SW(actor + 0x40, 2);
        SW(actor + 0x42, 0x80);
        SW(actor, 0x930f);
        break;
    case 0xb930f:
        if (W(actor + 0x40) && !decrement(ram, actor + 0x42)) {
            SW(actor + 0x42, 0x80);
            uint16_t ship = world_spawn_child(ram, actor, 0x9380);
            if (ship) {
                unsigned lane = (W(actor + 0x4a) & 1) ? 2 : 0;
                SW(ship + 2, 0xb000);
                SW(ship + 0x21, 0x1c0);
                SW(actor + 0x4a, W(actor + 0x4a) | (1u << lane));
                SW(ship + 0x4a, lane);
                SW(0xb8b2, W(0xb8b2) & ~(1u << (lane + 4)));
                SW(ship + 0x24, BW(lane - 0x728c));
                ram[ship + 0x2c] = ram[actor + 0x2c];
                decrement(ram, actor + 0x40);
            }
        }
        if (W(0xb700) >= W(actor + 0x1e)) finish_animation(ram, actor);
        break;
    case 0xb9380:
        SW(actor + 0x0e, 0x1b);
        SW(actor + 0x26, 0x20);
        SW(actor + 0x36, 0x20);
        SW(actor + 0x3c, 0xf723);
        ram[actor + 0x2d] = 0x20;
        SW(actor + 0x2a, 0x4032);
        SW(actor + 0x38, 0xff40);
        SW(actor + 0x50, 0x40);
        SW(actor + 0x52, 0x60);
        SW(actor + 0x60, 0x200);
        SW(actor + 0x74, 0x80);
        SW(actor + 0x5e, 0x80);
        SW(actor + 0x62, 0xe0);
        SW(0xb8b8 + W(actor + 0x4a), actor);
        SW(actor + 0x48, 1);
        SW(actor, 0x93d2);
        break;
    case 0xb93d2:
    case 0xb94d6:
    case 0xb950e: {
        uint16_t lane = W(actor + 0x4a);
        int dead = 0, credit = 0;
        uint16_t attacker = 0;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb93d2) move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        if (W(actor + 0x21) < 0x40) dead = 1;
        else {
            queue_sprite(ram, main_rom, actor, 1);
            if (address == 0xb93d2 && !decrement(ram, actor + 0x48)) {
                SW(actor + 0x48, 8);
                uint16_t wake = effect_slot(ram, W(0x9a), 0x7f73);
                uint8_t facing = ram[actor + 0x29] ^ 1;
                ram[wake + 0x29] = facing;
                SW(wake + 0x21, W(actor + 0x21) + BW(0x902e + 2 * (facing & 1)));
                SW(wake + 0x24, W(actor + 0x24));
            }
            escort_fire(ram, main_rom, actor);
            if (address == 0xb94d6) {
                if (W(actor + 0x42)) {
                    if (!decrement(ram, actor + 0x42)) SW(actor, 0x93d2);
                } else if (!(W(0xb8b2) & (1u << ((lane + 4) & 15)))) SW(actor + 0x42, 3);
                break;
            }
            int blocked = ((1u << (terrain_material(ram, tiles, W(actor + 0x21) - 0x18, W(actor + 0x24)) & 15)) & 0xff02) != 0;
            if (address == 0xb93d2) {
                if ((int16_t)W(actor + 0x21) > 0x60 && blocked) SW(actor, 0x950e);
            } else if (!blocked) SW(actor, 0x93d2);
            ram[actor + 0x0d] = 0;
            attacker = world_player_contact(ram, main_rom, actor);
            if (attacker) dead = credit = 1;
            else {
                ram[actor + 0x0d] = 3;
                uint16_t shot = world_projectile_contact(ram, main_rom, actor);
                if (shot && W(shot + 0x0e) == 0x121) {
                    ram[actor + 0x28] = vector_angle(main_rom, (int16_t)(W(shot + 0x21) - W(actor + 0x21)),
                                                     (int16_t)(W(shot + 0x24) - W(actor + 0x24)));
                    SW(actor + 0x2e, 0x400);
                    angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
                    SW(actor + 0x42, 7);
                    SW(actor, 0xae2a);
                    SW(actor + 2, 0);
                    uint16_t parent = slot_parent(ram, actor);
                    SW(parent + 0x40, W(parent + 0x40) + 1);
                    SW(parent + 0x4a, W(parent + 0x4a) & ~(1u << (lane & 15)));
                    SW(0xb8b8 + lane, 0);
                    break;
                }
                if (shot && W(shot + 0x0e) != 0x1b) {
                    int16_t left = (int16_t)(W(shot + 0x26) - W(actor + 0x36));
                    SW(shot + 0x26, left > 0 ? left : 0);
                    left = (int16_t)(W(actor + 0x26) - W(shot + 0x36));
                    SW(actor + 0x26, left);
                    if (left <= 0) { dead = credit = 1; attacker = shot; }
                    else ram[actor + 0x19] = (ram[actor + 0x19] & 1) + 10;
                }
            }
            if (!dead) {
                if (W(0xb8b2) & (1u << ((lane + 4) & 15))) SW(actor, 0x94d6);
                break;
            }
        }
        if (credit) {
            world_award_score(ram, main_rom, actor, attacker);
            world_spawn_radial_explosion(ram, main_rom, actor);
            world_queue_sound(ram, 0x98);
        }
        uint16_t parent = slot_parent(ram, actor);
        SW(parent + 0x40, W(parent + 0x40) + 1);
        SW(parent + 0x4a, W(parent + 0x4a) & ~(1u << (lane & 15)));
        SW(0xb8b8 + lane, 0);
        finish_animation(ram, actor);
        break;
    }
    /* Overlay sprite that follows its parent and copies its hit flash. */
    case 0xbd13b:
        ram[actor + 0x2d] = 0x21;
        SW(actor + 0x2a, 0x6bf0);
        SW(actor, 0xd149);
        /* fall through */
    case 0xbd149: {
        uint16_t parent = slot_parent(ram, actor);
        SW(actor + 0x21, W(parent + 0x21));
        SW(actor + 0x24, W(parent + 0x24));
        if (ram[parent + 0x19]) ram[actor + 0x19] = ram[parent + 0x19] + 1;
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    /* Animated two-strip scenery on the far layer. */
    case 0x0daa9:
        SW(actor + 0x50, RW((W(actor + 0x1e) & 0xe) - 0x159));
        SW(actor + 0x52, RW((ram[actor + 0x1f] & 2) - 0x22f1));
        SW(actor, 0xdacd);
        /* fall through */
    case 0x0dacd:
        scroll_actor(ram, actor, 0xb72a);
        if (W(actor + 0x21) < 0x3f ||
            W(actor + 0x24) > (uint16_t)((ram[0x26e] & 2) ? 0x198 : 0x1c8 + W(0xb732))) {
            finish_animation(ram, actor);
            break;
        }
        if (!(W(0x274) & 7))
            draw_strips(ram, main_rom, tiles, 0x4000, W(actor + 0x50),
                        (W(0x274) & 0x38) + W(actor + 0x52) + 0x1188, 1, 2);
        break;
    /* Stage-clear helper: spawns the tally actor, then idles. */
    case 0x01a24:
        effect_slot(ram, actor, 0x2612);
        SW(actor, 0x1a39);
        SW(actor + 2, 0);
        break;
    case 0x01a39:
        break;
    /* Ice floe generator before the boss and its drifting floes. */
    case 0xaf4a2:
        world_acquire_resource(ram, actor, 0x6d);
        SW(actor + 0x42, 0x64);
        SW(actor + 0x50, 0x1720);
        SW(actor, 0xf4b9);
        /* fall through */
    case 0xaf4b9:
    case 0xaf4c7: {
        if (address != 0xaf4c7) {
            if (W(0xb700) < W(actor + 0x50)) break;
            SW(actor, 0xf4c7);
        }
        if (W(0x262) & 0x8000) finish_animation(ram, actor);
        if (decrement(ram, actor + 0x42)) break;
        uint16_t delay = (world_random_byte(ram) & 0x7f) + 0x20;
        SW(actor + 0x42, delay);
        uint16_t floe = effect_slot(ram, actor, 0xf52f);
        if (floe != 0x8480) SW(floe + 2, 0xa000);
        SW(floe + 0x21, 0x50);
        SW(floe + 0x24, (delay & 0x1f) + AW((delay & 0xe) - 0x1096));
        ram[floe + 0x2c] = ram[actor + 0x2c];
        SW(floe + 0x1e, delay & 0xf);
        SW(actor + 0x40, W(actor + 0x40) + 2);
        break;
    }
    case 0xaf52f: {
        uint16_t kind = 2 * W(actor + 0x1e);
        ram[actor + 0x2d] = 0x20;
        SW(actor + 0x3c, 0xfaeb);
        SW(actor + 0x26, 1);
        SW(actor + 0x36, 0x80);
        SW(actor + 0x44, 1);
        SW(actor + 0x48, AW(kind - 0x1066));
        SW(actor + 0x38, AW(kind - 0x1086));
        SW(actor + 0x4c, AW((kind & 2) - 0x1046));
        SW(actor, 0xf56d);
    }
        /* fall through */
    case 0xaf56d: {
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        if (W(actor + 0x21) > 0x1c0) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            SW(actor + 0x2a, AW((W(actor + 0x40) & 0xe) + W(actor + 0x4c)));
            SW(actor + 0x44, W(actor + 0x48));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        uint16_t player = world_player_contact(ram, main_rom, actor);
        if (player) {
            SW(player + 0x5a, 0xc0);
            SW(player + 0x5e, 0xc0);
        } else {
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (W(actor + 0x26)) {
                if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                break;
            }
        }
        SW(actor + 0x1e, (W(actor + 0x1e) & 1) << 1);
        SW(actor, 0xf5f1);
        break;
    }
    case 0xaf5f1: {
        uint16_t kind = W(actor + 0x1e) & 2;
        ram[actor + 0x2d] = 0x20;
        SW(actor + 0x4c, AW(kind - 0x1022));
        SW(actor + 0x48, AW(kind - 0xff2));
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        SW(actor, 0xf61b);
    }
        /* fall through */
    case 0xaf61b:
        scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = AW(frame + W(actor + 0x4c));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, AW(frame + W(actor + 0x48)));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* Palette cycle between two ROM palettes. */
    case 0xafdff:
        ram[actor + 0x2c] = 0x1f;
        SW(actor, 0xfe08);
        /* fall through */
    case 0xafe08:
        queue_palette(ram, ram[actor + 0x2c] & 0x3f, AW((W(actor + 0x40) & 2) - 0x205), 0);
        SW(actor + 0x40, W(actor + 0x40) + 2);
        break;
    /* Boss launcher hatch: opens, fires a salvo of shells, closes. */
    case 0x11be1:
    case 0x11c78:
    case 0x11c9e:
    case 0x11d41:
    case 0x11d8a: {
        if (address == 0x11c78) {
            SW(actor + 0x40, 0);
            SW(actor + 0x42, 8);
            uint16_t list = TW(0x130 + (world_random_byte(ram) & 0xe));
            SW(actor + 0x46, list);
            SW(actor + 0x48, TW(list));
            SW(actor, 0x1c9e);
            address = 0x11c9e;
        }
        uint16_t parent = slot_parent(ram, actor);
        if (ram[parent + 0x0a] & 2) { finish_animation(ram, actor); break; }
        ram[actor + 0x2c] = ram[parent + 0x2c];
        if (address == 0x11be1) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = W(actor + 0x40), sprite = TW(0x20 + frame);
                if (sprite == 0xffff) SW(actor, 0x1c78);
                else {
                    SW(actor + 0x2a, sprite);
                    SW(actor + 0x44, TW(0x38 + frame));
                    SW(actor + 0x40, frame + 2);
                    if (frame == 8) {
                        SW(actor + 0x42, 5);
                        do {
                            uint16_t puff = world_spawn_child(ram, W(0x9a), 0x2180);
                            if (!puff) break;
                            SW(puff + 2, 0x1000);
                            uint16_t place = (uint16_t)(W(actor + 0x42) - 1) << 2;
                            SW(puff + 0x21, W(actor + 0x21) + TW(0x4e + place));
                            SW(puff + 0x24, W(actor + 0x24) + TW(0x50 + place));
                        } while (decrement(ram, actor + 0x42));
                    }
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        } else if (address == 0x11c9e) {
            queue_sprite(ram, main_rom, actor, 0);
            if (decrement(ram, actor + 0x42)) break;
            world_queue_sound(ram, 0x3c);
            SW(actor + 0x42, 0x18);
            for (;;) {
                uint16_t slot = TW(W(actor + 0x48));
                if (slot == 0xffff) break;
                SW(actor + 0x40, slot);
                uint16_t shell = spawn_boss_part(ram, actor, 0x1e07);
                SW(shell + 0x21, W(actor + 0x21));
                SW(shell + 0x24, W(actor + 0x24));
                SW(shell + 0x54, W(actor + 0x54));
                SW(shell + 0x40, slot);
                ram[shell + 0x14] = ram[actor + 0x14];
                SW(actor + 0x48, W(actor + 0x48) + 2);
            }
            SW(actor + 0x46, W(actor + 0x46) + 2);
            uint16_t next = TW(W(actor + 0x46));
            if (next != 0xffff) SW(actor + 0x48, next);
            else {
                SW(actor + 0x42, 0x20);
                SW(actor, 0x1d41);
            }
        } else if (address == 0x11d41) {
            queue_sprite(ram, main_rom, actor, 1);
            if (!decrement(ram, actor + 0x42)) {
                SW(actor + 0x40, 0);
                SW(actor + 0x44, 1);
                SW(actor, 0x1d8a);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = W(actor + 0x40), sprite = TW(0x62 + frame);
                if (sprite == 0xffff) {
                    SW(parent + 0x62, W(parent + 0x62) & ~1u);
                    finish_animation(ram, actor);
                    break;
                }
                SW(actor + 0x2a, sprite);
                SW(actor + 0x44, TW(0x70 + frame));
                SW(actor + 0x40, frame + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    }
    case 0x11e07: {
        uint16_t slot = W(actor + 0x40);
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x2d;
        SW(actor + 0x3c, 0xffff);
        SW(actor + 0x21, W(actor + 0x21) + TW(0x94 + slot));
        SW(actor + 0x24, W(actor + 0x24) + TW(0x96 + slot));
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        SW(actor, 0x1e36);
    }
        /* fall through */
    case 0x11e36:
        if (W(actor + 0x40) >= 0xa) scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = TW(0xbc + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, TW(0xd8 + frame));
            if (frame == 8) {
                uint16_t shell = spawn_boss_part(ram, actor, 0x1ea7);
                SW(shell + 0x21, W(actor + 0x21) - 0x10);
                SW(shell + 0x24, W(actor + 0x24));
                SW(shell + 0x38, W(actor + 0x54));
                ram[shell + 0x2c] = ram[actor + 0x14];
            }
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x11ea7: {
        unsigned pick = 6;
        ram[actor + 0x2d] = 0x2e;
        SW(actor + 0x3c, 0xf77b);
        SW(actor + 0x26, 0x100);
        SW(actor + 0x36, 0x400);
        if (W(0x26e) & 0x100) pick = world_random_byte(ram) & 0xe;
        SW(actor + 0x60, TW(0x1a4 + pick));
        SW(actor, 0x1edb);
    }
        /* fall through */
    case 0x11edb:
    case 0x11f70:
    case 0x11fbf:
    case 0x12040: {
        uint16_t wear = swap16((uint16_t)(((W(actor + 0x26) - 1) & 0xff80) << 2));
        if (address == 0x11fbf) {
            uint16_t speed = W(actor + 0x2e) + 0x20;
            SW(actor + 0x2e, speed);
            if ((int16_t)speed > 0x500) speed = 0x500;
            else move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
            SW(actor + 0x38, speed);
        }
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        if (address == 0x11edb || address == 0x11ea7) {
            if (W(actor + 0x21) > 0x1c0 || W(actor + 0x21) < 0x40) { finish_animation(ram, actor); break; }
            if (W(actor + 0x60)) {
                uint16_t step = -W(actor + 0x38), left = W(actor + 0x60);
                SW(actor + 0x60, left - step);
                if (left <= step) SW(actor, 0x1f70);
            }
            SW(actor + 0x2a, TW(0x1c0 + (wear & 2)));
        } else if (address == 0x11f70) {
            uint16_t speed = W(actor + 0x38) + 0x20;
            SW(actor + 0x38, speed);
            /* blt after the add: taken while the sum is negative without overflow. */
            int overflow = (~(uint16_t)(speed - 0x20) & speed & 0x8000) != 0;
            if (((speed & 0x8000) != 0) == overflow) {
                SW(actor + 0x42, 0x40);
                SW(actor + 0x2e, 0);
                SW(actor + 0x3a, 0x40);
                SW(actor, 0x1fbf);
            }
            SW(actor + 0x2a, TW(0x1b4 + ((wear + swap16((uint16_t)((-speed & 0x300) << 2))) & 0xe)));
        } else {
            if (W(actor + 0x21) > 0x1c0) { finish_animation(ram, actor); break; }
            if (address == 0x11fbf) {
                uint16_t index = wear + swap16((uint16_t)((W(actor + 0x2e) & 0xff00) << 2));
                if (index >= 0x38 || TW(0x1c4 + index) == 0xffff) SW(actor, 0x2040);
                else {
                    SW(actor + 0x2a, TW(0x1c4 + index));
                    ram[actor + 0x29] = bank1[0x8d0 + (index >> 2)];
                }
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        shell_contacts(ram, main_rom, actor);
        break;
    }
    case 0x12180:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x2d;
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        SW(actor, 0x2197);
        /* fall through */
    case 0x12197:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40), sprite = TW(0x628 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x44, TW(0x648 + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* Enemy depth charge. Replaces the older case: at the surface the original
     * calls 6D4D directly and leaves the charge's own position untouched. */
    case 0xb98e4:
    case 0xb990c:
    case 0xb9926: {
        if (address == 0xb98e4) {
            world_acquire_resource(ram, actor, BW(0x98e5));
            SW(actor + 0x26, 1);
            SW(actor + 0x36, 1);
            SW(actor + 0x3c, BW(0x98f9));
            ram[actor + 0x2d] = 4;
            SW(actor + 0x42, 32);
            SW(actor, 0x990c);
        } else {
            if (address == 0xb990c && !decrement(ram, actor + 0x42)) {
                uint16_t child = effect_slot(ram, actor, BW(0x9915));
                SW(child + 0x2c, 3);
                SW(actor, 0x9926);
            }
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        uint16_t y = W(actor + 0x24);
        if (y > (uint16_t)((ram[0x26e] & 2) ? 0x198 : 0x1c8 + W(0xb732))) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t frame = W(actor + 0x40) + 0x80;
        if (frame >= 0x600) frame = 0;
        SW(actor + 0x40, frame);
        SW(actor + 0x2a, BW(0x8d1c + 2 * (frame >> 8)));
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t x = W(actor + 0x21);
        unsigned material = terrain_material(ram, tiles, x, y);
        if (!((1u << (material & 15)) & 0x5502) && material == 3) {
            spawn_surface_splash_effect(ram, main_rom, actor, x, y);
            finish_animation(ram, actor);
        } else enemy_torpedo_contact(ram, main_rom, actor, 1u << (material & 15));
        break;
    }
    /* Stage-clear tally root: spawns the two player columns and ten counters,
     * draws the two divider rows, then idles. */
    case 0x02612: {
        uint16_t child = effect_slot(ram, actor, 0x2681);
        SW(child + 0x1e, 0);
        child = effect_slot(ram, actor, 0x2681);
        SW(child + 0x1e, 1);
        do {
            child = effect_slot(ram, actor, 0x2b68);
            SW(child + 0x1e, W(actor + 0x42));
            SW(actor + 0x42, W(actor + 0x42) + 1);
        } while (W(actor + 0x42) < 0xa);
        for (unsigned row = 0; row < 2; ++row) {
            uint16_t cell = row ? 0x2030 : 0x1f30;
            for (unsigned n = 0; n < 0x28; ++n) {
                write_word(tiles, cell, 1);
                write_word(tiles, cell + 2, 0x111);
                cell = (cell & 0xff00) | ((cell + 4) & 0xff);
            }
        }
        SW(actor, 0x2680);
        break;
    }
    case 0x02680:
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
