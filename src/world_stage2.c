/* Recovered World actor handlers for stage 2 (stage index 1).
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

void world_acquire_resource(uint8_t *ram, uint16_t actor, uint16_t id);
uint8_t world_random_byte(uint8_t *ram);

#define RW(o) read_word(ram, (uint16_t)(o))
#define WW(o, v) write_word(ram, (uint16_t)(o), (uint16_t)(v))
#define A(o) ((uint16_t)(actor + (o)))
#define AW(o) RW(A(o))
#define SAW(o, v) WW(A(o), v)
#define AB(o) ram[A(o)]
#define BW(o) read_word(bank, (uint16_t)(o))
#define DISCARD 0x8480

/* 1769 with a far handler; the allocator's discard slot stands in on failure. */
static uint16_t spawn_far(uint8_t *ram, uint16_t parent, uint16_t segment, uint16_t handler, int *failed)
{
    uint16_t child = world_spawn_child(ram, parent, handler);
    if (failed) *failed = !child;
    if (!child) return DISCARD;
    write_word(ram, child + 2, segment);
    return child;
}

/* 17C3: the same allocation, linked after the actor at RAM[9A]. */
static uint16_t spawn_tail(uint8_t *ram, uint16_t segment, uint16_t handler, int *failed)
{
    return spawn_far(ram, read_word(ram, 0x9a), segment, handler, failed);
}

/* Parent actor through the depth table at 8500. */
static uint16_t parent_of(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2));
}

/* D2B7 and D371 leave BX at the last examined list entry when nothing overlaps. */
static uint16_t contact_list(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t list,
                             uint16_t *bx)
{
    uint16_t count = read_word(ram, list);
    uint16_t hit = list == 0xa6e2 ? world_projectile_contact(ram, rom, actor)
                                  : world_surface_contact(ram, rom, actor);
    if (hit) *bx = hit;
    else if (count) *bx = read_word(ram, (uint16_t)(list + 4 + ((count - 1) & 0xfffe)));
    return hit;
}

/* D237 leaves BX at the touched player, or at player two's slot. */
static uint16_t player_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t *bx)
{
    uint16_t hit = world_player_contact(ram, rom, actor);
    *bx = hit ? hit : read_word(ram, 0x92);
    return hit;
}

/* 65B1: choose a player; IX receives the sign bits of the offset to it. */
static uint16_t choose_target(uint8_t *ram, uint16_t actor, unsigned *ix, int *none)
{
    unsigned active = ram[0x264] & 3;
    *none = !active;
    if (!active) return DISCARD;
    unsigned mode = active == 3 ? ram[actor + 0x0c] / 2u : active - 1;
    uint16_t player = world_enemy_target(ram, actor);
    unsigned dx = (read_word(ram, player + 0x21) - read_word(ram, actor + 0x21)) >> 15 & 1;
    unsigned dy = (read_word(ram, player + 0x24) - read_word(ram, actor + 0x24)) >> 15 & 1;
    if (mode < 4) *ix = dx << 1 | dy;
    else if (mode < 6) *ix = dx << 1;
    else if (mode < 8) *ix = dy;
    return player;
}

/* 6EFE explosion children (6ACB, 6B2E, ...). */
static uint16_t explosion(uint8_t *ram, uint16_t actor, uint16_t frames, uint16_t timing,
                          int palette, unsigned priority, uint16_t reach, uint16_t force, int resource)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    write_word(ram, child + 0x4c, frames);
    write_word(ram, child + 0x4a, timing);
    if (palette >= 0) ram[child + 0x2c] = (uint8_t)palette;
    ram[child + 0x2d] = (uint8_t)priority;
    write_word(ram, child + 0x56, reach);
    write_word(ram, child + 0x58, force);
    if (resource >= 0 && (palette < 0 || !(child & 0x300))) write_word(ram, child + 0x44, (uint16_t)resource);
    return child;
}

static uint16_t blast_6acb(uint8_t *ram, uint16_t actor)
{
    return explosion(ram, actor, 0x747f, 0x76c8, 0x0a, 0x20, 0x30, 0x80, -1);
}

static uint16_t blast_6b2e(uint8_t *ram, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    return explosion(ram, actor, 0x74c3, 0x7675, 0x0a, 0x20, 0x30, 0x80, 0x5d);
}

static uint16_t blast_6b91(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    write_word(ram, child + 0x4c, 0x74e5);
    write_word(ram, child + 0x4a, 0x7675);
    ram[child + 0x2c] = 0x0a;
    ram[child + 0x2d] = 0x20;
    write_word(ram, child + 0x56, 0x40);
    write_word(ram, child + 0x58, 0);
    world_queue_sound(ram, 0x3e);
    if (!(child & 0x300)) write_word(ram, child + 0x44, 0x5d);
    return child;
}

static uint16_t blast_6d92(uint8_t *ram, uint16_t actor)
{
    return explosion(ram, actor, 0x74a1, 0x7675, 0x0a, 0x3f, 0x30, 0x80, 0x5d);
}

static uint16_t blast_6dc5(uint8_t *ram, uint16_t actor)
{
    return explosion(ram, actor, 0x74e5, 0x7675, 0x0a, 0x3f, 0x40, 0, 0x5d);
}

/* 6920: snap an actor's Y to the tile row, compensating the camera phase. */
static void snap_row(uint8_t *ram, uint16_t actor)
{
    uint16_t phase = read_word(ram, 0xb70c) & 7;
    uint16_t y = read_word(ram, actor + 0x24) - 1 - phase;
    ram[actor + 0x23] = 0;
    write_word(ram, actor + 0x24, (uint16_t)((y & 0xfff8) + 8 + phase));
}

/* 6D4D: seabed impact. */
static void seabed_impact(uint8_t *ram, uint16_t actor)
{
    uint16_t child = effect_slot(ram, actor, 0x6f54);
    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
    write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
    write_word(ram, child + 0x4c, 0x7585);
    write_word(ram, child + 0x4a, 0x769b);
    ram[child + 0x2c] = 0x0a;
    ram[child + 0x2d] = 0x20;
    write_word(ram, child + 0x56, 0x30);
    write_word(ram, child + 0x58, 0x80);
    snap_row(ram, child);
    world_queue_sound(ram, 0x82);
}

/* 68DF with the material number that the original leaves in CW. */
static uint16_t terrain_probe(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y,
                              unsigned *material)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    *material = tiles[0x4000 + world_tile_address(map_x, map_y) + 3] >> 3;
    return (uint16_t)(1u << (*material & 15));
}

/* Copy the parent's position plus an offset; returns the parent. */
static uint16_t follow_parent(uint8_t *ram, uint16_t actor, uint16_t dx, uint16_t dy)
{
    uint16_t parent = parent_of(ram, actor);
    write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + dx);
    write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + dy);
    return parent;
}

/* Trail bubble behind a moving actor (BE41 linked at the list tail). */
static uint16_t spawn_wake(uint8_t *ram, uint16_t actor)
{
    uint16_t wake = spawn_tail(ram, 0, 0xbe41, 0);
    write_word(ram, wake + 0x21, read_word(ram, actor + 0x21) + 0x10);
    write_word(ram, wake + 0x24, read_word(ram, actor + 0x24));
    return wake;
}

/* Vertical play-field limits used by several projectiles: nonzero when outside. */
static int outside_rows(const uint8_t *ram, uint16_t actor, int check_bottom)
{
    int deep = !(read_word(ram, 0x26e) & 2);
    uint16_t bottom = deep ? 0x1c8 + read_word(ram, 0xb732) : 0x198;
    uint16_t top = deep ? 0x10 + read_word(ram, 0xb732) : 0x68;
    uint16_t y = read_word(ram, actor + 0x24);
    return (check_bottom && bottom < y) || (int16_t)y < (int16_t)top;
}

/* ADF0: a kind-121 shot knocks the actor away; returns nonzero when it did. */
static int knocked_away(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t shot)
{
    if (read_word(ram, shot + 0x0e) != 0x121) return 0;
    ram[actor + 0x28] = vector_angle(rom, read_word(ram, shot + 0x21) - read_word(ram, actor + 0x21),
                                     read_word(ram, shot + 0x24) - read_word(ram, actor + 0x24));
    write_word(ram, actor + 0x2e, 0x400);
    digit_fragment_velocity(ram, rom, actor);
    write_word(ram, actor + 0x42, 7);
    write_word(ram, actor, 0xae2a);
    write_word(ram, actor + 2, 0);
    return 1;
}

/* 7036: attach a timed burst script (handler 7056) to the actor. */
static uint16_t burst_script(uint8_t *ram, uint16_t actor, uint16_t segment, uint16_t table, uint16_t period)
{
    uint16_t child = effect_slot(ram, actor, 0x7056);
    write_word(ram, child + 0x60, table);
    write_word(ram, child + 0x62, segment);
    write_word(ram, child + 0x4c, period);
    write_word(ram, child + 0x42, 1);
    return child;
}

/* Projectile hit on an armoured part: D2B7 with flag 1, then the part's own damage rule. */
static void armour_hit(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t shot)
{
    (void)rom;
    int rest = (int16_t)read_word(ram, actor + 0x26) - (int16_t)read_word(ram, shot + 0x36);
    write_word(ram, actor + 0x26, rest > 0 ? (uint16_t)rest : 0);
}

/* Terrain cell address used by 6812 (segment D400). */
static uint16_t cell_address(const uint8_t *ram, uint16_t x, uint16_t y)
{
    return world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
}

static uint16_t next_column(uint16_t cell, unsigned step)
{
    return (cell & 0xff00) | ((cell + step) & 0xff);
}

static uint16_t next_row(uint16_t cell, unsigned back)
{
    return (((cell >> 8) + 1) & 0x3f) << 8 | ((cell - back) & 0xff);
}

/* B8AA4: retag the terrain under a sinking hull section. */
static void sink_hull_tiles(const uint8_t *ram, uint8_t *tiles, const uint8_t *bank, uint16_t actor)
{
    uint8_t *layer = tiles + 0x4000;
    uint16_t cell = cell_address(ram, read_word(ram, actor + 0x21) - 0x2c, read_word(ram, actor + 0x24) + 0x14);
    uint16_t row = read_word(bank, 0x7f6e + 2 * (read_word(ram, actor + 0x46) & 3));
    for (unsigned y = 0; y < 11; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            write_word(layer, cell + 2, (read_word(layer, cell + 2) & 0x7ff) | read_word(bank, row));
            cell = next_column(cell, 4);
        }
        if (y == 10) break;
        cell = next_row(cell, 0x40);
        row += 2;
    }
}

/* B8AEE: the same for a destroyed deck gun, including the original's masked address. */
static void sink_gun_tiles(const uint8_t *ram, uint8_t *tiles, const uint8_t *bank, uint16_t actor)
{
    uint8_t *layer = tiles + 0x4000;
    uint16_t cell = cell_address(ram, read_word(ram, actor + 0x21) - 0x0c, read_word(ram, actor + 0x24) + 0x1c);
    for (unsigned y = 0; y < 9; ++y) {
        for (unsigned x = 0; x < 7; ++x) {
            write_word(layer, cell + 2, (read_word(layer, cell + 2) & 0x7ff) | 0x3000);
            cell = next_column(cell, 4);
        }
        if (y == 8) break;
        cell = next_row(cell, 0x1c);
    }
    cell = (((cell >> 8) - 1) & 0x3f) << 8 | ((cell - 8) & 0xff);
    uint16_t row = 0x7fa4;
    for (unsigned y = 0; y < 2; ++y) {
        for (unsigned x = 0; x < 3; ++x) {
            uint16_t value = read_word(layer, cell + 2) | read_word(bank, row);
            cell &= 0x7ff;
            write_word(layer, cell + 2, value);
            cell = next_column(cell, 4);
            row += 2;
        }
        if (y == 1) break;
        cell = next_row(cell, 8);
    }
}

static uint16_t rom_word(const uint8_t *rom, uint16_t offset)
{
    return rom[offset] | (uint16_t)(rom[(uint32_t)offset + 1] << 8);
}

/* B8B60: a sinking hull pushes the players aside and returns nonzero when it
 * lands on a player whose shape is 79F1. Shots still hit it on the way down. */
static int hull_pushes_players(uint8_t *ram, const uint8_t *rom, const uint8_t *bank, uint16_t actor)
{
    for (unsigned slot = 0x90; slot <= 0x92; slot += 2) {
        uint16_t player = read_word(ram, slot);
        uint16_t shape = read_word(ram, player + 0x3c);
        if (!shape) continue;
        for (unsigned part = 4; part; --part) {
            uint16_t own = read_word(bank, 0x7f12 + 2 * ((part - 1) & 3));
            unsigned signs = 0;
            int touching = 0;
            write_word(ram, actor + 0x3c, own);
            uint16_t a = rom_word(rom, own), c = rom_word(rom, shape);
            if (ram[actor + 0x29] & 1) a = -a;
            if (ram[player + 0x29] & 1) c = -c;
            a += read_word(ram, actor + 0x21);
            c += read_word(ram, player + 0x21);
            signs = a < c;
            a -= c;
            if (a & 0x8000) a = -a;
            int16_t depth_x = rom_word(rom, own + 4) + rom_word(rom, shape + 4) + 1;
            int16_t depth_y = 0;
            if ((int)depth_x - (int16_t)a > 0) {
                depth_x -= a;
                a = rom_word(rom, own + 2);
                c = rom_word(rom, shape + 2);
                if (ram[actor + 0x29] & 2) a = -a;
                if (ram[player + 0x29] & 2) c = -c;
                a += read_word(ram, actor + 0x24);
                c += read_word(ram, player + 0x24);
                signs = signs << 1 | (a < c);
                a -= c;
                if (a & 0x8000) a = -a;
                depth_y = rom_word(rom, own + 6) + rom_word(rom, shape + 6) + 1;
                if ((int)depth_y - (int16_t)a > 0) {
                    depth_y -= a;
                    touching = 1;
                    write_word(ram, actor + 0x60, signs);
                }
            }
            if (touching) {
                if (shape == 0x79f1) return 1;
                if (depth_x <= depth_y) {
                    write_word(ram, player + 0x21, read_word(ram, player + 0x21) + (signs & 2 ? depth_x : -depth_x));
                } else if (signs & 1) {
                    write_word(ram, player + 0x24, read_word(ram, player + 0x24) + depth_y);
                } else {
                    write_word(ram, player + 0x24, read_word(ram, player + 0x24) - depth_y);
                    write_word(ram, 0xb8ae, read_word(ram, 0xb8ae) | (1u << (ram[player + 0x0b] & 2)));
                }
            }
            ram[actor + 0x0d] = 0;
            world_projectile_contact(ram, rom, actor);
        }
    }
    return 0;
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

/* 3665: second sound command queue. */
static void queue_music(uint8_t *ram, uint16_t command)
{
    uint16_t next = (read_word(ram, 0xa6d8) + 2) & 14;
    if (next != read_word(ram, 0xa6d6)) {
        write_word(ram, 0xa6d8, next);
        write_word(ram, 0xa6a2 + next, command);
    }
}

/* Ancestor through the depth table: level 1 is the parent. */
static uint16_t ancestor(const uint8_t *ram, uint16_t actor, unsigned level)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2 * level));
}

/* 13C95: draw with the two-palette hit flash. */
static void boss_draw(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (ram[actor + 0x19]) {
        --ram[actor + 0x19];
        ram[actor + 0x2c] = ram[actor + 0x15 + (ram[actor + 0x19] & 1)];
    }
    queue_sprite(ram, rom, actor, 0);
}

/* 13CB1: draw shaking around the anchor at +31/+34. */
static void boss_shake_draw(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    write_word(ram, actor + 0x21, (world_random_byte(ram) & 2) - 1 + read_word(ram, actor + 0x31));
    write_word(ram, actor + 0x24, (world_random_byte(ram) & 2) - 1 + read_word(ram, actor + 0x34));
    queue_sprite(ram, rom, actor, 0);
}

/* 13CE8 / 13CD7: ride an ancestor; nonzero when it is being destroyed. */
static int boss_ride(uint8_t *ram, uint16_t actor, unsigned level, uint16_t *host_out)
{
    uint16_t host = ancestor(ram, actor, level);
    write_word(ram, actor + 0x21, read_word(ram, host + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, host + 0x24));
    ram[actor + 0x19] = ram[host + 0x19] + 1;
    *host_out = host;
    return ram[host + 0x0a] != 0;
}

/* 13D25 / 13D14: ride an ancestor at a mirrored offset, copying its state byte. */
static uint16_t boss_ride_offset(uint8_t *ram, uint16_t actor, unsigned level)
{
    uint16_t host = ancestor(ram, actor, level);
    uint16_t dx = read_word(ram, actor + 0x31);
    ram[actor + 0x29] = ram[host + 0x29];
    if (ram[host + 0x29] & 1) dx = -dx;
    write_word(ram, actor + 0x21, dx + read_word(ram, host + 0x21));
    write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + read_word(ram, host + 0x24));
    ram[actor + 0x0a] = ram[host + 0x0a];
    ram[actor + 0x19] = ram[host + 0x19] + 1;
    return host;
}

/* 13DC8: fire the claw shot with its muzzle flash. */
static void boss_fire(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    world_queue_sound(ram, 0x45);
    uint16_t c = spawn_far(ram, actor, 0x1000, 0x362f, 0);
    unsigned facing = ram[actor + 0x29];
    uint16_t speed = read_word(ram, actor + 0x2e);
    uint16_t row = 2 * (facing & 1) + read_word(ram, actor + 0x6a);
    ram[c + 0x29] = facing;
    write_word(ram, c + 0x38, facing & 1 ? speed : -speed);
    write_word(ram, c + 0x21, read_word(bank, 0x258e + row) + read_word(ram, actor + 0x21));
    write_word(ram, c + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0x2596 + row));
    ram[c + 0x2c] = ram[actor + 0x71];
    c = spawn_far(ram, actor, 0x1000, 0x37f8, 0);
    ram[c + 0x2c] = ram[actor + 0x72];
    ram[c + 0x29] = facing;
    write_word(ram, c + 0x31, read_word(bank, 0x258e + row));
    write_word(ram, c + 0x34, read_word(bank, 0x2596 + row));
    write_word(ram, c + 0x4c, 0x25c6);
    write_word(ram, c + 0x4e, 0x25de);
}

/* 13D7A: burst timing for the claw; nonzero when the burst is over. */
static int boss_burst(uint8_t *ram, const uint8_t *bank, uint16_t actor)
{
    uint16_t host = ancestor(ram, actor, 1);
    if (!(read_word(ram, host + 0x4a) & 1)) {
        if (decrement(ram, actor + 0x54)) return 0;
        write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
        if (decrement(ram, actor + 0x58)) { boss_fire(ram, bank, actor); return 0; }
    }
    write_word(ram, actor + 0x54, 1);
    write_word(ram, actor + 0x40, 0);
    write_word(ram, actor + 0x44, 1);
    write_word(ram, host + 0x60, read_word(ram, host + 0x60) & ~4u);
    return 1;
}

/* 13E4F / 13E87 / 13EBF: two-layer tentacle segment. */
static void boss_segment_draw(uint8_t *ram, const uint8_t *rom, const uint8_t *bank, uint16_t actor, uint16_t table)
{
    uint16_t frame = read_word(ram, actor + 0x40);
    write_word(ram, actor + 0x2a, read_word(bank, table + frame));
    ram[actor + 0x2d] = 7;
    queue_sprite(ram, rom, actor, 0);
    write_word(ram, actor + 0x2a, read_word(bank, table + 2 + frame));
    ram[actor + 0x2d] = 0x11;
    queue_sprite(ram, rom, actor, 0);
    write_word(ram, actor + 0x40, frame + 4 >= 0x14 ? 0 : frame + 4);
}

/* 13EF7: launch four homing shots at the player the boss is not chasing. */
static void boss_launch_homing(uint8_t *ram, uint16_t actor)
{
    for (unsigned n = 4; n; --n) {
        uint16_t c = spawn_far(ram, actor, 0x1000, 0x3935, 0);
        ram[c + 0x29] = ram[actor + 0x29];
        write_word(ram, c + 0x48, read_word(ram, actor + 0x48) == read_word(ram, 0x90) ? read_word(ram, 0x92)
                                                                                 : read_word(ram, 0x90));
        if (n > 1) write_word(ram, c + 0x46, n - 1);
    }
    world_queue_sound(ram, 0x72);
}

/* D1C9 with the original's unwrapped shape reads. */
static int shapes_touch(const uint8_t *ram, const uint8_t *rom, uint16_t a, uint16_t b)
{
    uint16_t sa = read_word(ram, a + 0x3c), sb = read_word(ram, b + 0x3c);
    for (unsigned axis = 0; axis < 2; ++axis) {
        uint16_t pa = rom_word(rom, sa + 2 * axis), pb = rom_word(rom, sb + 2 * axis);
        if (ram[a + 0x29] & (1u << axis)) pa = -pa;
        if (ram[b + 0x29] & (1u << axis)) pb = -pb;
        uint16_t gap = pa + read_word(ram, a + 0x21 + 3 * axis) - (pb + read_word(ram, b + 0x21 + 3 * axis));
        if (gap & 0x8000) gap = -gap;
        if (gap >= (uint16_t)(rom_word(rom, sa + 4 + 2 * axis) + rom_word(rom, sb + 4 + 2 * axis) + 1)) return 0;
    }
    return 1;
}

/* 13F84: push one actor that touches the tentacle. */
static void boss_sweep_one(uint8_t *ram, const uint8_t *rom, const uint8_t *bank, uint16_t actor, uint16_t other)
{
    if (!shapes_touch(ram, rom, actor, other)) return;
    uint16_t group = read_word(ram, actor + 0x64);
    write_word(ram, other + 0x21, read_word(ram, other + 0x21)
               + read_word(bank, 0x257e + 2 * read_word(ram, actor + 0x1e) + group));
    uint16_t push = read_word(bank, 0x2586 + group);
    uint16_t oy = read_word(ram, other + 0x24), y = read_word(ram, actor + 0x24);
    uint16_t gap = oy - y;
    if (oy >= y) push = -push;
    if (gap & 0x8000) gap = -gap;
    if (gap >= 8) write_word(ram, other + 0x24, oy + push);
}

/* 13F34: the tentacle sweeps players and their shots along. */
static void boss_sweep(uint8_t *ram, const uint8_t *rom, const uint8_t *bank, uint16_t actor)
{
    write_word(ram, actor + 0x64, 0);
    if (!(read_word(ram, 0x274) & (1u << (ram[actor + 0x1e] & 15)))) return;
    for (unsigned i = 0; i < 2; ++i)
        if (read_word(ram, 0x264) & (1u << i)) boss_sweep_one(ram, rom, bank, actor, read_word(ram, 0x90 + 2 * i));
    write_word(ram, actor + 0x64, 4);
    for (uint16_t i = 0; i < read_word(ram, 0xa6e2); i += 2) {
        uint16_t shot = read_word(ram, 0xa6e6 + i);
        if (shot) boss_sweep_one(ram, rom, bank, actor, shot);
    }
}

/* 13FC4: decide when the body may start its next attack. */
static void boss_attack_gate(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    if (read_word(ram, actor + 0x64)) { decrement(ram, actor + 0x64); return; }
    uint16_t state = read_word(ram, actor + 0x60), mode = read_word(ram, actor + 0x4a);
    if (state & 0x12) return;
    if (mode & 2) {
        write_word(ram, actor + 0x3c, 0xf7c3);
        ram[actor + 0x0d] = 1;
        if (world_player_contact(ram, rom, actor)) {
            write_word(ram, actor + 0x64, read_word(ram, actor + 0x66));
            write_word(ram, actor + 0x60, state | 2);
        }
    } else if (mode & 0x10) write_word(ram, actor + 0x60, state | 0x10);
}

/* 13B5A: turn a homing shot toward its target and refresh its sprite. */
static void boss_homing_steer(uint8_t *ram, const uint8_t *rom, const uint8_t *bank, uint16_t actor, int turn)
{
    uint16_t target = read_word(ram, actor + 0x48);
    if (turn && target) {
        uint8_t wanted = (vector_angle(rom, read_word(ram, target + 0x21) - read_word(ram, actor + 0x21),
                                       read_word(ram, target + 0x24) - read_word(ram, actor + 0x24)) + 2) & 0xfc;
        uint8_t heading = ram[actor + 0x28];
        if (wanted != heading)
            ram[actor + 0x28] = (uint8_t)(wanted - heading) < (uint8_t)(heading - wanted) ? heading + 8 : heading - 8;
    }
    digit_fragment_velocity(ram, rom, actor);
    unsigned index = (ram[actor + 0x28] & 0xfc) >> 2;
    ram[actor + 0x29] = bank[0x2748 + index];
    write_word(ram, actor + 0x2a, read_word(bank, 0x2708 + 2 * (index & 0x1f)));
}

/* D68F / D6B7 with script kind 14h (constant velocity), which the shared
 * helpers do not cover; every other kind is delegated to them. */
static void script_begin(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    if (read_word(bank, offset) != 0x14) { begin_linear_script(ram, rom, actor, offset); return; }
    write_word(ram, actor + 0x7a, 0x14);
    write_word(ram, actor + 0x38, read_word(bank, offset + 4));
    write_word(ram, actor + 0x3a, read_word(bank, offset + 6));
    write_word(ram, actor + 0x7e, offset + 8);
    write_word(ram, actor + 0x7c, read_word(bank, offset + 2));
}

static int script_step(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    const uint8_t *bank = rom + ((uint32_t)read_word(ram, actor + 2) << 4);
    uint16_t next = read_word(ram, actor + 0x7e), next_kind = read_word(bank, next);
    if (read_word(ram, actor + 0x7a) == 0x14) {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (decrement(ram, actor + 0x7c)) return 1;
    } else if (read_word(ram, actor + 0x7c) == 1 && next_kind == 0x14) {
        write_word(ram, actor + 0x7c, 2);
        if (!step_flight_script(ram, rom, actor)) { write_word(ram, actor + 0x7c, 1); return 0; }
        write_word(ram, actor + 0x7c, 0);
    } else return step_flight_script(ram, rom, actor);
    if (next_kind == 0xffff) return -1;
    if (next_kind != 0x14 && ((next_kind & 1) || next_kind > 6)) return 0;
    script_begin(ram, rom, actor, next);
    return 1;
}

static int off_screen_x(const uint8_t *ram, uint16_t actor)
{
    uint16_t x = read_word(ram, actor + 0x21);
    return x > 0x1c0 || x < 0x40;
}

int world_stage2_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank = main_rom + (address & 0xf0000);
    uint16_t bx = 0;
    switch (address) {
    case 0xb02e0: {
        world_acquire_resource(ram, actor, 0x61);
        SAW(0x3c, 0xf2a3); SAW(0x26, 0x80); SAW(0x36, 0x80);
        AB(0x2d) = 0x20; SAW(0x0e, 2); SAW(0x2a, 0x490e);
        unsigned flags = AW(0x1e);
        AB(0x29) = flags & 1;
        flags &= 6;
        if (flags == 6) flags = world_random_byte(ram) & 6;
        SAW(0x46, BW(0x2d6 + flags));
        int failed;
        uint16_t child = spawn_far(ram, actor, 0xb000, 0x47a, &failed);
        if (failed) { finish_animation(ram, actor); break; }
        ram[child + 0x29] = AB(0x29);
        SAW(0, 0x357);
        break;
    }
    case 0xb0357:
    case 0xb03f1:
    case 0xb042b: {
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb03f1) {
            if (!decrement(ram, A(0x44))) {
                uint16_t frame = AW(0x40), sprite = BW(0x2b0 + frame);
                if (sprite == 0xffff) { SAW(0x2e, 0); SAW(0, 0x42b); }
                else { SAW(0x2a, sprite); SAW(0x44, BW(0x2c0 + frame)); SAW(0x40, frame + 2); }
            }
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            uint16_t speed;
            if (address == 0xb0357) {
                speed = AW(0x46);
                if ((int16_t)speed > 0x10) speed = 0x10;
                speed = (uint16_t)(speed << 8 | speed >> 8) >> 2;
            } else {
                SAW(0x2e, AW(0x2e) + 0x20);
                speed = AW(0x2e);
                if ((int16_t)speed > 0x200) speed = 0x200;
            }
            if (!(AB(0x29) & 1)) speed = -speed;
            SAW(0x38, speed);
            move_actor_axis(ram, A(0x20), speed);
            if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
            queue_sprite(ram, main_rom, actor, 1);
            if (address == 0xb0357 && !decrement(ram, A(0x46))) {
                SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x3f1);
            }
        }
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) break;
        }
        world_award_score(ram, main_rom, actor, bx);
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb047a:
        world_acquire_resource(ram, actor, 0x61);
        SAW(0x3c, 0xf2ab); SAW(0x26, 0xa0); SAW(0x36, 0x80);
        AB(0x2d) = 0x21; AB(0x0c) = 0x0c; SAW(0x2a, 0x4956); SAW(0, 0x4a4);
        break;
    case 0xb04a4: {
        uint16_t parent = parent_of(ram, actor);
        SAW(0x21, RW(parent + 0x21));
        SAW(0x24, RW(parent + 0x24) - 3);
        if (ram[parent + 0x0a] & 1) { finish_animation(ram, actor); break; }
        if (!RW(parent + 0x46)) { SAW(0x42, 8); SAW(0x3a, 0xff00); SAW(0, 0x4f8); }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb04f8:
    case 0xb057b:
    case 0xb05b0: {
        int animate = 1;
        if (address == 0xb04f8) {
            scroll_actor(ram, actor, 0xb71a);
            move_actor_axis(ram, A(0x23), AW(0x3a));
            if (!decrement(ram, A(0x42))) { world_queue_sound(ram, 0x74); SAW(0, 0x57b); }
        } else {
            if (address == 0xb057b) {
                unsigned ix = 0; int none;
                AB(0x2d) = 4; SAW(0x42, 0x40); SAW(0x38, 0xff00); SAW(0x3a, 0); SAW(0x46, 1);
                uint16_t target = choose_target(ram, actor, &ix, &none);
                if (!none) { SAW(0x50, target); SAW(0x3a, ix & 1 ? -0x80 : 0x80); }
                SAW(0, 0x5b0);
            }
            scroll_actor(ram, actor, 0xb71a);
            move_actor_axis(ram, A(0x20), AW(0x38));
            move_actor_axis(ram, A(0x23), AW(0x3a));
            if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
            animate = 0;
            if (AW(0x3a)) {
                uint16_t gap = AW(0x24) - RW(AW(0x50) + 0x24);
                if (gap & 0x8000) gap = -gap;
                if (gap >= 4 && decrement(ram, A(0x42))) animate = 1;
                else { SAW(0x3a, 0); SAW(0x38, 0xfd00); }
            }
            if (!animate && !decrement(ram, A(0x46))) {
                SAW(0x46, 4);
                ram[spawn_wake(ram, actor) + 0x2c] = 3;
            }
        }
        if (animate) {
            uint16_t frame = AW(0x40);
            SAW(0x40, frame + 2);
            if (AW(0x40) >= 6) SAW(0x40, 0);
            SAW(0x2a, BW(0x2ce + frame));
            AB(0x2c) = 3;
            queue_sprite(ram, main_rom, actor, 0);
        }
        SAW(0x2a, 0x4956);
        AB(0x2c) = AB(0x14);
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) break;
            blast_6acb(ram, actor);
        }
        finish_animation(ram, actor);
        break;
    }

    /* B380F: armoured carrier with a bow launcher, hatch, three silos and a bay. */
    case 0xb380f: {
        world_acquire_resource(ram, actor, 0x0c);
        AB(0x2d) = 0x28; SAW(0x0e, 0x0a); SAW(0x38, 0xffc0); SAW(0x3c, 0xf3c3);
        uint16_t variant = (AW(0x1e) & 8) << 2;
        uint16_t p = BW(0x35b2 + RW(0x8a) + variant) + 0x35f2;
        SAW(0x44, BW(p)); SAW(0x46, BW(p)); SAW(0x4e, BW(p + 2)); SAW(0x54, BW(p + 4));
        SAW(0x5a, BW(p + 6)); SAW(0x52, BW(p + 8));
        p = BW(0x35b2 + RW(0x88) + variant) + 0x364c;
        SAW(0x48, BW(p)); SAW(0x4c, BW(p + 2)); SAW(0x56, BW(p + 4)); SAW(0x58, BW(p + 6));
        SAW(0x50, BW(p + 8)); SAW(0x26, BW(p + 8));
        begin_linear_script(ram, main_rom, actor, BW(0x36a6 + ((AW(0x1e) & 0x70) >> 3)));
        uint16_t c = spawn_far(ram, actor, 0xb000, 0x4583, 0);
        WW(c + 0x26, AW(0x50)); WW(c + 0x36, AW(0x50)); WW(c + 0x0e, 0x0a);
        c = spawn_far(ram, actor, 0xb000, 0x3b21, 0);
        ram[c + 0x2c] = AB(0x2c);
        WW(c + 0x26, AW(0x56)); WW(c + 0x36, AW(0x56));
        WW(c + 0x48, AW(0x48)); WW(c + 0x4a, AW(0x48)); WW(c + 0x2e, AW(0x4c)); WW(c + 0x0e, 0x29);
        c = spawn_far(ram, actor, 0xb000, 0x3e64, 0);
        ram[c + 0x2c] = AB(0x2c);
        WW(c + 0x26, AW(0x58)); WW(c + 0x36, AW(0x58)); WW(c + 0x0e, 0x29);
        for (unsigned silo = 0; silo < 6; silo += 2) {
            c = spawn_far(ram, actor, 0xb000, 0x3f05, 0);
            ram[c + 0x2c] = AB(0x2c);
            WW(c + 0x26, AW(0x54)); WW(c + 0x36, AW(0x54)); WW(c + 0x3a, AW(0x4e));
            WW(c + 0x60, silo); WW(c + 0x0e, 0x29);
        }
        SAW(0x60, 0x15);
        c = spawn_far(ram, actor, 0xb000, 0x4262, 0);
        ram[c + 0x2c] = AB(0x2c);
        WW(c + 0x26, AW(0x5a)); WW(c + 0x36, AW(0x5a)); WW(c + 0x52, AW(0x52)); WW(c + 0x1e, AW(0x1e));
        SAW(0, 0x39c5);
        break;
    }
    case 0xb39c5: {
        SAW(0x62, 0);
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0 || (int16_t)AW(0x21) <= 0 || (int16_t)AW(0x24) <= 0x48) {
            if (AW(8) == 6) decrement(ram, parent_of(ram, actor) + 0x44);
            finish_animation(ram, actor);
            break;
        }
        SAW(0x2a, 0x3078);
        queue_sprite(ram, main_rom, actor, 1);
        if (AW(0x38)) {
            SAW(0x40, AW(0x40) + 0x40);
            if (AW(0x40) >= 0x500) SAW(0x40, 0);
        }
        SAW(0x2a, BW(0x3474 + 2 * AB(0x41)));
        queue_sprite(ram, main_rom, actor, 1);
        if (!AW(0x26)) {
            if (AW(8) == 6) decrement(ram, parent_of(ram, actor) + 0x44);
            AB(0x0a) = 1; SAW(0x40, 0); SAW(0x44, 0); SAW(0x42, 1); SAW(0x3a, 0xffc0); SAW(0, 0x3aa6);
            break;
        }
        if (AB(0x60)) {
            if (AW(0x44)) { decrement(ram, A(0x44)); AB(0x61) &= 0x7f; break; }
            AB(0x0d) = 1;
            if (player_contact(ram, main_rom, actor, &bx)) { AB(0x61) |= 0x80; SAW(0x44, AW(0x46)); }
        }
        break;
    }
    case 0xb3aa6:
    case 0xb3ac6: {
        if (address == 0xb3aa6) {
            uint16_t sprite = BW(0x347e + 2 * AB(0x41));
            SAW(0x40, AW(0x40) + 0x1a);
            if (sprite == 0xffff) SAW(0, 0x3ac6); else SAW(0x2a, sprite);
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x20), AW(0x38));
        move_actor_axis(ram, A(0x23), AW(0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (decrement(ram, A(0x42))) break;
        SAW(0x42, 8);
        uint16_t row = AW(0x44);
        SAW(0x44, row + 6);
        uint16_t kind = BW(0x37e9 + row);
        if (kind == 0xffff) { finish_animation(ram, actor); break; }
        uint16_t entry = BW(0x37d9 + kind), c;
        if (BW(0x37db + kind)) return WORLD_HANDLER_STOP;
        if (entry == 0x6b2e) c = blast_6b2e(ram, actor);
        else if (entry == 0x6b91) c = blast_6b91(ram, actor);
        else if (entry == 0x6d92) c = blast_6d92(ram, actor);
        else if (entry == 0x6dc5) c = blast_6dc5(ram, actor);
        else return WORLD_HANDLER_STOP;
        WW(c + 0x31, RW(c + 0x31) + BW(0x37eb + row));
        WW(c + 0x34, RW(c + 0x34) + BW(0x37ed + row));
        break;
    }
    case 0xb3b21:
        AB(0x2d) = 0x28; SAW(0x3c, 0xf3a3); SAW(0x2a, 0x30ec); SAW(0, 0x3b35);
        break;
    case 0xb3b35: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        if (AW(0x21) >= 0xe0) {
            int upper = -1;
            if (AW(0x48)) {
                if (!decrement(ram, A(0x48))) { SAW(0x44, 0x10); upper = 1; }
            } else if (!decrement(ram, A(0x44))) { SAW(0x48, AW(0x4a)); upper = 0; }
            if (upper >= 0) {
                uint16_t c = spawn_far(ram, actor, 0xb000, 0x3c31, 0);
                WW(c + 0x2e, AW(0x2e));
                WW(c + 0x60, upper ? 0 : 2);
                if (upper) world_queue_sound(ram, 0x65);
                c = spawn_far(ram, actor, 0xb000, 0x46d4, 0);
                WW(c + 0x34, upper ? 3 : 0xfffe);
            }
        }
        AB(0x0d) = 0;
        if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) ram[parent + 0x62] |= 1;
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, bx);
        SAW(0x21, AW(0x21) - 0x30);
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb3c31:
        world_acquire_resource(ram, actor, 0x0d);
        AB(0x2d) = 0x28; SAW(0x26, 8); SAW(0x36, 0x80); SAW(0x2a, 0x359e); SAW(0x3c, 0xf403);
        AB(0x0c) = 0x0a; SAW(0x42, 4); SAW(0x0e, 0x0a10); SAW(0, 0x3c64);
        /* fall through */
    case 0xb3c64: {
        uint16_t parent = parent_of(ram, actor);
        if (AB(0x0a)) { finish_animation(ram, actor); break; }
        SAW(0x21, RW(parent + 0x21) - 0x42);
        SAW(0x24, RW(parent + 0x24) + BW(0x3528 + AW(0x60)));
        queue_sprite(ram, main_rom, actor, 1);
        if (decrement(ram, A(0x42))) break;
        SAW(0x42, 4);
        uint16_t sprite = BW(0x351c + AW(0x40));
        SAW(0x2a, sprite);
        SAW(0x40, AW(0x40) + 2);
        if (sprite != 0xffff) break;
        SAW(0x40, 0); AB(0x2d) = 4; SAW(0x2a, 0x35d6);
        unsigned ix = 0, aim = 2; int none;
        uint16_t target = choose_target(ram, actor, &ix, &none);
        if (!none) {
            int16_t slope = (int16_t)(AW(0x24) - RW(target + 0x24) + 0x28) >> 4;
            aim = slope < 0 ? 0 : slope > 4 ? 4 : slope;
            AB(0x28) = bank[0x37d4 + aim];
        } else AB(0x28) = 0x80;
        SAW(0x2a, BW(0x352c + 2 * aim));
        digit_fragment_velocity(ram, main_rom, actor);
        SAW(0x42, 0x18); SAW(0x48, 5); SAW(0, 0x3d2a); SAW(0x0e, 0x0a11);
        break;
    }
    case 0xb3d2a:
    case 0xb3e0d: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x20), AW(0x38));
        if (address == 0xb3d2a) {
            move_actor_axis(ram, A(0x23), AW(0x3a));
            int deep = !(RW(0x26e) & 2);
            uint16_t bottom = deep ? 0x1c8 + RW(0xb732) : 0x198;
            uint16_t top = deep ? 0x10 + RW(0xb732) : 0x68;
            if (off_screen_x(ram, actor) || bottom < AW(0x24) || (int16_t)AW(0x24) < (int16_t)top) {
                finish_animation(ram, actor);
                break;
            }
            if (!decrement(ram, A(0x42))) {
                SAW(0x38, -(AW(0x2e) + 0x100)); SAW(0x2a, 0x35d6); SAW(0, 0x3e0d);
            }
        } else if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, A(0x48))) {
            SAW(0x48, 5);
            uint16_t wake = spawn_wake(ram, actor);
            if (address == 0xb3d2a) WW(wake + 0x1e, 1);
            ram[wake + 0x2c] = 3;
        }
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) break;
        }
        blast_6acb(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb3e64:
        AB(0x2d) = 0x28; SAW(0x3c, 0xf3ab); SAW(0x2a, BW(0x3486 + (RW(0x274) & 2))); SAW(0, 0x3e83);
        break;
    case 0xb3e83: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x0d) = 0;
        if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) AB(0x62) |= 2;
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, bx);
        SAW(0x24, AW(0x24) + 0x10);
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb3f05:
        AB(0x2d) = 0x28; SAW(0x3c, 0xf3b3); SAW(0x2a, BW(0x348a + AW(0x60))); SAW(0, 0x3f1f);
        break;
    case 0xb3f1f:
    case 0xb3fd9:
    case 0xb4059:
    case 0xb4094: {
        uint16_t silo = AW(0x60);
        uint16_t parent = follow_parent(ram, actor, BW(0x3490 + silo), 8);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        if (address == 0xb3f1f) {
            if (ram[parent + 0x61] & 0x80) { SAW(0x42, BW(0x3496 + silo)); SAW(0x40, 0); SAW(0, 0x3fd9); }
        } else if (address == 0xb3fd9) {
            if (AW(0x42)) decrement(ram, A(0x42));
            else {
                uint16_t sprite = BW(0x349c + silo * 8 + AW(0x40));
                if (sprite != 0xffff) { SAW(0x2a, sprite); SAW(0x40, AW(0x40) + 2); }
                else {
                    int failed;
                    SAW(0x42, 0x60);
                    uint16_t c = spawn_far(ram, actor, 0xb000, 0x40e4, &failed);
                    if (!failed) { WW(c + 0x3a, AW(0x3a)); world_queue_sound(ram, 0x54); SAW(0, 0x4059); }
                }
            }
        } else if (address == 0xb4059) {
            if (!decrement(ram, A(0x42))) SAW(0, 0x4094);
        } else {
            SAW(0x2a, BW(0x349c + silo * 8 + AW(0x40) - 2));
            SAW(0x40, AW(0x40) - 2);
            if (!AW(0x40)) SAW(0, 0x3f1f);
        }
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x0d) = 0;
        if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) ram[parent + 0x62] |= 4;
        if (AW(0x26)) break;
        world_award_score(ram, main_rom, actor, bx);
        WW(parent + 0x60, RW(parent + 0x60) & ~(1u << (silo & 15)));
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb40e4:
        world_acquire_resource(ram, actor, 0x0d);
        AB(0x2d) = 0x28; SAW(0x26, 8); SAW(0x36, 0x80); SAW(0x2a, 0x3218); SAW(0x3c, 0xffff);
        SAW(0x42, 4); SAW(0x0e, 0x0c00); SAW(0, 0x4113);
        /* fall through */
    case 0xb4113: {
        follow_parent(ram, actor, 0, 0);
        SAW(0x19, 0);
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, A(0x42))) {
            uint16_t frame = AW(0x40), sprite = BW(0x34cc + frame);
            SAW(0x42, 4);
            SAW(0x3c, BW(0x34ea + frame));
            SAW(0x2a, sprite);
            SAW(0x40, frame + 2);
            if (sprite == 0xffff) {
                SAW(0x40, 0); AB(0x2d) = 4; SAW(0x3c, 0xf3cb);
                SAW(0x24, AW(0x24) + 0x30); SAW(0, 0x41b6); SAW(0x0e, 0x0c01);
            }
        }
        if (AW(0x3c) == 0xffff) break;
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) break;
        }
        blast_6acb(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb41b6: {
        unsigned material;
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        SAW(0x2a, BW(0x3506 + (AW(0x40) & 6)));
        SAW(0x19, 0);
        queue_sprite(ram, main_rom, actor, 1);
        SAW(0x40, AW(0x40) + 2);
        if (!(terrain_probe(ram, tiles, AW(0x21), AW(0x24), &material) & 0xff02)) {
            if (material == 3) seabed_impact(ram, actor);
            else {
                AB(0x0d) = 2;
                if (!player_contact(ram, main_rom, actor, &bx)) {
                    AB(0x0d) = 0;
                    contact_list(ram, main_rom, actor, 0xa6e2, &bx);
                    if (AW(0x26)) break;
                }
                blast_6acb(ram, actor);
            }
        } else blast_6acb(ram, actor);
        SAW(0x40, 0); SAW(0, 0x4226);
        break;
    }
    case 0xb4226: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t sprite = BW(0x350e + 2 * AB(0x41));
        SAW(0x40, AW(0x40) + 0x40);
        if (sprite == 0xffff) { finish_animation(ram, actor); break; }
        SAW(0x2a, sprite);
        SAW(0x19, 0);
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb4262:
        AB(0x2d) = 0x28; SAW(0x3c, 0xf3bb); SAW(0x2a, 0x338e);
        SAW(0x30, BW(0x3728 + (AW(0x1e) & 6))); SAW(0, 0x4285);
        break;
    case 0xb4285:
    case 0xb433e:
    case 0xb44c4: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        if (address == 0xb4285) {
            queue_sprite(ram, main_rom, actor, 1);
            if ((int16_t)AW(0x30) >= (int16_t)AW(0x21)) {
                SAW(0x6e, 1); SAW(0x40, 0); world_queue_sound(ram, 0x89); SAW(0, 0x433e);
            }
        } else {
            if (!decrement(ram, A(0x6e))) {
                uint16_t frame = AW(0x40);
                if (address == 0xb44c4) {
                    SAW(0x2a, BW(0x3554 + frame)); SAW(0x6e, BW(0x3562 + frame)); SAW(0x40, frame + 2);
                } else if (BW(0x3536 + frame) == 0xffff) {
                    SAW(0x6c, BW(0x3730 + (AB(0x1f) & 6)));
                    SAW(0x42, 1); SAW(0x5a, AW(0x26)); SAW(0x26, AW(0x52));
                    SAW(0x3c, 0xf383); SAW(0, 0x43c8);
                } else {
                    SAW(0x2a, BW(0x3536 + frame)); SAW(0x6e, BW(0x3546 + frame)); SAW(0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        }
        AB(0x0d) = 0;
        if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) AB(0x62) |= 8;
        if (AW(0x26)) break;
        SAW(0x0e, 0x29);
        world_award_score(ram, main_rom, actor, bx);
        SAW(0x21, AW(0x21) + 0x20); SAW(0x24, AW(0x24) - 0x10);
        blast_6b2e(ram, actor);
        SAW(0x21, AW(0x21) - 0x20); SAW(0x24, AW(0x24) + 0x10);
        SAW(0x2a, 0x30ca); SAW(0x26, AW(0x52)); SAW(0x3c, 0xf383); SAW(0, 0x451c); SAW(0x0e, 0xd1);
        break;
    }
    case 0xb43c8: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, A(0x42))) {
            uint16_t row = AW(0x6c), kind = BW(row);
            SAW(0x42, BW(row + 2));
            if (kind == 0xffff) {
                SAW(0x6e, 1); SAW(0x40, 0); SAW(0x52, AW(0x26)); SAW(0x26, AW(0x5a));
                SAW(0x3c, 0xf3bb); SAW(0, 0x44c4);
            } else {
                int failed;
                SAW(0x6a, kind);
                uint16_t c = spawn_far(ram, actor, BW(0x3778 + kind), BW(0x3776 + kind), &failed);
                if (!failed) {
                    WW(c + 0x21, AW(0x21) + BW(0x3786 + kind));
                    WW(c + 0x24, AW(0x24) + BW(0x3788 + kind));
                    WW(c + 0x1e, BW(0x3796 + (kind >> 1)));
                }
                SAW(0x6c, AW(0x6c) + 4);
            }
        }
        AB(0x0d) = 0;
        contact_list(ram, main_rom, actor, 0xa6e2, &bx);
        if (AW(0x26)) break;
        SAW(0x0e, 0x0a);
        world_award_score(ram, main_rom, actor, bx);
        WW(0xb8a0, RW(0xb8a0) + 2);
        world_queue_sound(ram, 0x5f);
        WW(parent + 0x26, 0);
        finish_animation(ram, actor);
        break;
    }
    case 0xb451c: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        AB(0x0d) = 0;
        contact_list(ram, main_rom, actor, 0xa6e2, &bx);
        if (AW(0x26)) break;
        SAW(0x0e, 0x0a);
        world_award_score(ram, main_rom, actor, bx);
        WW(parent + 0x26, 0);
        break;
    }
    case 0xb4583: {
        uint16_t parent = follow_parent(ram, actor, 0, 0);
        if (RW(parent + 0x62)) break;
        for (uint16_t shape = 0xf38b; shape <= 0xf39b; shape += 8) {
            SAW(0x3c, shape);
            AB(0x0d) = 0;
            if (player_contact(ram, main_rom, actor, &bx)) break;
            AB(0x0d) = 0;
            if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) break;
        }
        if (!AW(0x26)) {
            WW(parent + 0x26, 0);
            world_award_score(ram, main_rom, actor, bx);
        }
        if (ram[parent + 0x0a] || !AW(0x26)) finish_animation(ram, actor);
        break;
    }
    case 0xb463d:
        SAW(0x6e, ((AW(0x1e) & 0xff00) >> 4) + RW(0xb700));
        SAW(0, 0x4652);
        /* fall through */
    case 0xb4652: {
        if ((int16_t)RW(0xb700) >= (int16_t)AW(0x6e)) { finish_animation(ram, actor); break; }
        if ((int16_t)decrement(ram, A(0x42)) > 0) {
            int16_t rest = AW(0x42), step = (uint16_t)(-RW(0xb71a)) << 2;
            SAW(0x42, rest - step);
            if (rest - step > 0) break;
        }
        int failed;
        uint16_t row = AW(0x40);
        uint16_t c = spawn_far(ram, actor, 0xb000, 0x380f, &failed);
        if (!failed) {
            WW(c + 0x21, 0x1e0);
            WW(c + 0x24, BW(0x37b0 + row) + RW(0xb732));
            WW(c + 0x1e, BW(0x37c2 + row) | (AW(0x1e) & 0xff));
            SAW(0x44, AW(0x44) + 1);
        }
        SAW(0x42, BW(0x379e + row) << 2);
        SAW(0x40, row + 2);
        if (AW(0x40) >= 0x12) SAW(0x40, 0);
        break;
    }
    case 0xb46d4:
        AB(0x2c) = 3; AB(0x2d) = 0x28; SAW(0x6e, 1); SAW(0, 0x46e6);
        /* fall through */
    case 0xb46e6: {
        follow_parent(ram, actor, (uint16_t)-0x32, AW(0x34));
        if (!decrement(ram, A(0x6e))) {
            uint16_t frame = AW(0x40), sprite = BW(0x3570 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SAW(0x2a, sprite); SAW(0x6e, BW(0x3592 + frame)); SAW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    /* B488A: floating gun platform that dives along a scripted path. */
    case 0xb488a: {
        world_acquire_resource(ram, actor, 0x0a);
        world_acquire_resource(ram, actor, 0x0f);
        AB(0x2d) = 0x20; SAW(0x3c, 0xf40b); SAW(0x2a, 0x3b02); AB(0x0c) = 0x0c; SAW(0x0e, 0x0b);
        uint16_t p = BW(0x476c + RW(0x8a) + (AW(0x1e) & 0x20)) + 0x47ac;
        SAW(0x50, BW(p)); SAW(0x52, BW(p)); SAW(0x54, BW(p + 2)); SAW(0x56, BW(p + 2));
        SAW(0x2e, BW(p + 4)); SAW(0x26, BW(p + 6)); SAW(0x36, BW(p + 6));
        SAW(0x42, 0x64); SAW(0x5a, 0x140);
        uint16_t c = spawn_far(ram, actor, 0xb000, 0x4ab7, 0);
        ram[c + 0x2c] = AB(0x2c); ram[c + 0x2d] = 0x21; WW(c + 0x2a, 0x3b1c);
        SAW(0, 0x4915);
        break;
    }
    case 0xb4915:
    case 0xb4a1a:
    case 0xb4a4b:
    case 0xb4a64:
    case 0xb4a94: {
        int fire = 0;
        if (address == 0xb4a4b || address == 0xb4a94) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return WORLD_HANDLER_STOP;
            if (step < 0 && address == 0xb4a4b) { SAW(0x42, 0x12c); SAW(0, 0x4a64); }
            else if (step < 0) { SAW(0x60, 8); SAW(0x42, 0x12c); SAW(0x62, 0); SAW(0, 0x4915); }
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            scroll_actor(ram, actor, 0xb71a);
            queue_sprite(ram, main_rom, actor, 1);
            if (address == 0xb4915) {
                fire = 1;
                if (AW(0x42)) decrement(ram, A(0x42));
                else if (!AW(0x60)) {
                    unsigned trigger = AW(0x1e) & 6, ix = 0;
                    int go = 0, none;
                    if (trigger == 6) go = !decrement(ram, A(0x5a));
                    else if (trigger) {
                        uint16_t target = choose_target(ram, actor, &ix, &none);
                        uint16_t gap = RW(target + 0x21) - AW(0x21);
                        if (gap & 0x8000) gap = -gap;
                        go = !none && gap < (trigger == 2 ? 0x40 : 0x20);
                    }
                    if (go) { SAW(0x60, 1); SAW(0, 0x4a1a); }
                }
            } else if (address == 0xb4a1a) {
                if (AW(0x60) == 2) {
                    begin_linear_script(ram, main_rom, actor, BW(0x47f4 + ((AW(0x1e) & 8) >> 2)));
                    SAW(0x62, 1); SAW(0, 0x4a4b);
                }
            } else {
                fire = 1;
                if (!decrement(ram, A(0x42))) {
                    SAW(0x60, 4);
                    begin_linear_script(ram, main_rom, actor, BW(0x47fc + ((AW(0x1e) & 8) >> 2)));
                    SAW(0, 0x4a94);
                }
            }
        }
        if (fire && AW(0x24) < 0x190) {
            int shoot = 0;
            if (AW(0x50)) {
                if (!decrement(ram, A(0x50))) { SAW(0x58, 4); shoot = 1; }
            } else if (!decrement(ram, A(0x54))) {
                SAW(0x54, AW(0x56));
                if (!decrement(ram, A(0x58))) SAW(0x50, AW(0x52));
                shoot = 1;
            }
            if (shoot) {
                int failed;
                uint16_t c = spawn_far(ram, actor, 0xb000, 0xa2ec, &failed);
                if (!failed) {
                    unsigned side = (AW(0x58) << 1) & 6;
                    WW(c + 0x38, BW(0x4882 + side));
                    WW(c + 0x21, AW(0x21) + BW(0x487a + side));
                    WW(c + 0x24, AW(0x24));
                    ram[c + 0x2c] = AB(0x14);
                    world_queue_sound(ram, 0x95);
                }
            }
        }
        if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        AB(0x0d) = 0;
        player_contact(ram, main_rom, actor, &bx);
        if (AW(0x26)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) {
                AB(0x0d) = 0;
                contact_list(ram, main_rom, actor, 0xaee6, &bx);
                if (AW(0x26)) break;
            }
        }
        world_award_score(ram, main_rom, actor, bx);
        blast_6b91(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xb4ab7:
    case 0xb4b06:
    case 0xb4b73:
    case 0xb4bf2:
    case 0xb4c6e: {
        uint16_t parent = parent_of(ram, actor);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        follow_parent(ram, actor, 0, 0);
        uint16_t frame = AW(0x40);
        if (address == 0xb4ab7) {
            if (RW(parent + 0x60) == 1) { SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x4b06); }
        } else if (address == 0xb4b06) {
            if (!decrement(ram, A(0x44))) {
                if (BW(0x4742 + frame) == 0xffff) { SAW(0x40, 0); SAW(0x42, 0x2f); SAW(0, 0x4b73); }
                else { SAW(0x2a, BW(0x4742 + frame)); SAW(0x44, BW(0x474a + frame)); SAW(0x40, frame + 2); }
            }
        } else if (address == 0xb4b73) {
            SAW(0x40, frame + BW(0x475a + ((AW(0x42) & 0x30) >> 3)));
            SAW(0x2a, BW(0x4752 + 2 * (AB(0x41) & 3)));
            queue_sprite(ram, main_rom, actor, 1);
            if (!decrement(ram, A(0x42))) { WW(parent + 0x60, 2); SAW(0x44, 0x100); SAW(0, 0x4bf2); }
            break;
        } else if (address == 0xb4bf2) {
            int spin = 1;
            if (RW(parent + 0x60) == 4) { SAW(0x44, 0x80); WW(parent + 0x60, 0); }
            else if (RW(parent + 0x60) == 8) { SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x4c6e); spin = 0; }
            if (spin) {
                SAW(0x40, AW(0x40) + AW(0x44));
                SAW(0x2a, BW(0x4752 + 2 * (AB(0x41) & 3)));
            }
        } else if (!decrement(ram, A(0x44))) {
            SAW(0x2a, BW(0x4760 + frame));
            if (BW(0x4766 + frame) == 0xffff) { WW(parent + 0x60, 0); SAW(0, 0x4ab7); }
            else { SAW(0x44, BW(0x4766 + frame)); SAW(0x40, frame + 2); }
        }
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    /* BF308: patrol craft that reverses at walls and drops charges. */
    case 0xbf308:
        world_acquire_resource(ram, actor, 0x5f);
        world_acquire_resource(ram, actor, 0x56);
        world_acquire_resource(ram, actor, 0x5d);
        world_acquire_resource(ram, actor, 0xff);
        SAW(0x0e, 0x23); SAW(0x26, 0x110); SAW(0x36, 0x80); SAW(0x3c, 0xf27b);
        AB(0x2d) = 0x20; AB(0x0c) = 8;
        SAW(0x2e, 0x200); SAW(0x38, 0xfe00); SAW(0x50, 0x20); SAW(0x52, 0x20);
        SAW(0x46, AW(0x1e) & 2 ? 2 : 0);
        SAW(0x56, 2); SAW(0x4a, 1); SAW(0, 0xf379);
        break;
    case 0xbf379:
    case 0xbf5c6:
    case 0xbf5f7: {
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbf379) {
            int turn = 0;
            move_actor_axis(ram, A(0x20), AW(0x38));
            if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
            if (AW(0x46) == 1) turn = !decrement(ram, A(0x42));
            else if (AW(0x46)) {
                unsigned ix = 0; int none;
                choose_target(ram, actor, &ix, &none);
                turn = !none && !(ix & 2);
            }
            if (turn) { SAW(0, 0xf5c6); decrement(ram, A(0x46)); }
            else {
                unsigned material;
                if (!tiles) return WORLD_HANDLER_STOP;
                if (terrain_probe(ram, tiles, BW(0xf304 + 2 * (AB(0x29) & 1)) + AW(0x21), AW(0x24),
                                  &material) & 0xff02)
                    SAW(0, 0xf5c6);
            }
        } else {
            if (address == 0xbf5c6) {
                SAW(0x2e, AW(0x2e) - 0x10);
                if (!AW(0x2e)) { SAW(0x40, 0); SAW(0x42, 1); SAW(0, 0xf63b); }
            } else {
                SAW(0x2e, AW(0x2e) + 0x10);
                if ((int16_t)AW(0x2e) >= 0x200) { SAW(0x42, 0x40); SAW(0, 0xf379); }
                if (!decrement(ram, A(0x5a))) { SAW(0x5a, 2); SAW(0x58, AW(0x58) + 2); }
                SAW(0x2a, BW(0xf298 + (AW(0x58) & 0x0e)));
                AB(0x2c) = AB(0x16);
                queue_sprite(ram, main_rom, actor, 0);
            }
            SAW(0x38, AB(0x29) & 1 ? AW(0x2e) : -AW(0x2e));
            move_actor_axis(ram, A(0x20), AW(0x38));
        }
        int drop = 0, idle = 1;
        if (!AW(0x50)) {
            unsigned index = 2 * AB(0x55);
            SAW(0x54, AW(0x54) + 0x40);
            if (index >= 0x0a) index = 8;
            if (BW(0xf2a8 + index) != 0xffff) { SAW(0x2a, BW(0xf2a8 + index)); idle = 0; }
            else {
                SAW(0x54, 0);
                if (decrement(ram, A(0x56))) drop = 1;
                else { SAW(0x50, AW(0x52)); SAW(0x56, 2); }
            }
        } else if (!decrement(ram, A(0x50))) drop = 1;
        if (drop) {
            int failed;
            uint16_t c = spawn_far(ram, actor, 0xb000, 0xf70a, &failed);
            if (!failed) {
                ram[c + 0x29] = AB(0x29);
                WW(c + 0x21, AW(0x21) + BW(0xf2fc + 2 * (AB(0x29) & 1)));
                WW(c + 0x24, AW(0x24) + 0x10);
                ram[c + 0x2c] = AB(0x17);
                world_queue_sound(ram, 0x61);
            }
        }
        if (idle) SAW(0x2a, 0x45c6);
        AB(0x2c) = AB(0x14);
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, A(0x4a))) {
            SAW(0x48, AW(0x48) + 2);
            SAW(0x4a, BW(0xf288 + (AW(0x48) & 0x0e)));
        }
        SAW(0x2a, BW(0xf278 + (AW(0x48) & 0x0e)));
        AB(0x2c) = AB(0x15);
        queue_sprite(ram, main_rom, actor, 0);
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            int hit;
            AB(0x0d) = 0;
            hit = contact_list(ram, main_rom, actor, 0xa6e2, &bx) != 0;
            if (!hit) { AB(0x0d) = 0; hit = contact_list(ram, main_rom, actor, 0xaee6, &bx) != 0; }
            if (hit) { SAW(0x60, AW(0)); SAW(0x40, 0); AB(0x2c) = AB(0x14); SAW(0, 0xf51b); }
            if (AW(0x26)) break;
        }
        world_award_score(ram, main_rom, actor, bx);
        explosion(ram, actor, 0x7533, 0x7675, -1, 0x20, 0x40, 0, 0x57);
        finish_animation(ram, actor);
        break;
    }
    case 0xbf51b:
    case 0xbf63b:
    case 0xbf646: {
        int dead;
        if (address == 0xbf51b) {
            scroll_actor(ram, actor, 0xb71a);
            move_actor_axis(ram, A(0x20), AW(0x38));
            if (AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
            unsigned index = 2 * AB(0x41);
            SAW(0x40, AW(0x40) + 0x40);
            if (index >= 0x0a) index = 8;
            if (BW(0xf2b2 + index) == 0xffff) { SAW(0, AW(0x60)); SAW(0x50, AW(0x52)); SAW(0x54, 0); }
            else SAW(0x2a, BW(0xf2b2 + index));
            queue_sprite(ram, main_rom, actor, 1);
            AB(0x0d) = 0;
            player_contact(ram, main_rom, actor, &bx);
            dead = !AW(0x26);
        } else {
            if (address == 0xbf63b) { AB(0x2c) = AB(0x14); SAW(0, 0xf646); }
            if (!decrement(ram, A(0x42))) {
                unsigned index = AW(0x40);
                SAW(0x42, 6);
                if (index >= 0x14) index = 0x12;
                if (BW(0xf252 + index) == 0xffff) {
                    SAW(0x2a, 0x45c6); SAW(0x58, 0); SAW(0x5a, 2); SAW(0, 0xf5f7);
                } else {
                    SAW(0x2a, BW(0xf252 + index));
                    SAW(0x3c, BW(0xf266 + index));
                    if (index == 0x0a) AB(0x29) ^= 1;
                    SAW(0x40, AW(0x40) + 2);
                }
            }
            scroll_actor(ram, actor, 0xb71a);
            if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
            queue_sprite(ram, main_rom, actor, 1);
            AB(0x0d) = 2;
            dead = player_contact(ram, main_rom, actor, &bx) != 0;
        }
        if (!dead) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            dead = !AW(0x26);
        }
        if (!dead) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xaee6, &bx);
            dead = !AW(0x26);
        }
        if (!dead) break;
        world_award_score(ram, main_rom, actor, bx);
        explosion(ram, actor, 0x7533, 0x7675, -1, 0x20, 0x40, 0, 0x57);
        finish_animation(ram, actor);
        break;
    }
    /* BDEDD: seabed gun emplacement with a rotating barrel (BE1E8). */
    case 0xbdedd: {
        world_acquire_resource(ram, actor, 0x12);
        world_acquire_resource(ram, actor, 0x0e);
        SAW(0x26, 0x20); SAW(0x36, 0x20); SAW(0x3c, 0xf683); AB(0x2d) = 0x29; SAW(0x0e, 0x1a);
        uint16_t p = BW(0xdcd2 + RW(0x88) + (AW(0x1e) & 0x20)) + 0xdd12;
        SAW(0x44, BW(p)); SAW(0x46, BW(p)); SAW(0x48, BW(p + 2)); SAW(0x4a, BW(p + 4)); SAW(0x4e, BW(p + 6));
        SAW(0x2a, 0x345a); SAW(0x3c, 0xf68b); SAW(0x33, 0x18); SAW(0, 0xdef4);
    }
        /* fall through */
    case 0xbdef4:
    case 0xbdf89: {
        uint16_t parent = parent_of(ram, actor);
        int destroyed = ram[parent + 0x0a] != 0;
        if (!destroyed) {
            SAW(0x21, RW(parent + 0x21) + 0x18);
            if (address == 0xbdf89) {
                SAW(0x24, RW(parent + 0x24) - 0x28);
                if (!decrement(ram, A(0x6e))) {
                    uint16_t frame = AW(0x40);
                    SAW(0x3c, BW(0xdc70 + frame));
                    if (BW(0xdc4e + frame) == 0xffff) {
                        AB(0x2d) = 0x20;
                        begin_linear_script(ram, main_rom, actor, BW(0xdd32 + (AW(0x1e) & 2)));
                        uint16_t c = spawn_far(ram, actor, 0xb000, 0xe1e8, 0);
                        ram[c + 0x2c] = AB(0x2c);
                        WW(c + 0x4c, AW(0x4a));
                        WW(c + 0x2e, AW(0x4e));
                        SAW(0, 0xe01a);
                    } else {
                        SAW(0x2a, BW(0xdc4e + frame)); SAW(0x6e, BW(0xdc60 + frame)); SAW(0x40, frame + 2);
                    }
                }
                queue_sprite(ram, main_rom, actor, 1);
            } else {
                SAW(0x24, RW(parent + 0x24) - 0x28 + AW(0x33));
                queue_sprite(ram, main_rom, actor, 1);
                SAW(0x33, AW(0x33) - 3);
                if ((int16_t)AW(0x33) <= 0) { SAW(0x6e, 1); SAW(0, 0xdf89); }
            }
            AB(0x0d) = 2;
            destroyed = player_contact(ram, main_rom, actor, &bx) != 0;
            if (!destroyed) {
                AB(0x0d) = 0;
                if (contact_list(ram, main_rom, actor, 0xa6e2, &bx) && knocked_away(ram, main_rom, actor, bx)) {
                    SAW(0x2a, 0x3584);
                    break;
                }
                if (AW(0x26)) break;
            }
        }
        SAW(0x21, AW(0x21) + 4);
        SAW(0x24, AW(0x24) + BW(0xdc82 + AW(0x40)));
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbe01a: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0 || outside_rows(ram, actor, 1) || AW(0x21) < 0x40) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        if (!player_near_turret(ram, main_rom, actor) && !decrement(ram, A(0x44))) {
            int failed;
            SAW(0x44, AW(0x46));
            uint16_t c = spawn_far(ram, actor, 0xb000, 0xe0fb, &failed);
            if (!failed) {
                WW(c + 0x21, AW(0x21)); WW(c + 0x24, AW(0x24));
                ram[c + 0x2c] = AB(0x14);
                WW(c + 0x38, AW(0x48));
                world_queue_sound(ram, 0x7e);
            }
        }
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            if (contact_list(ram, main_rom, actor, 0xa6e2, &bx) && knocked_away(ram, main_rom, actor, bx)) break;
            if (AW(0x26)) break;
        }
        world_award_score(ram, main_rom, actor, bx);
        SAW(0x21, AW(0x21) + 4); SAW(0x24, AW(0x24) - 4);
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbe0fb:
    case 0xbe120:
    case 0xbe393:
    case 0xbe3b3: {
        int shell = address == 0xbe0fb || address == 0xbe120;
        if (address == 0xbe0fb) {
            AB(0x2d) = 4; SAW(0x2a, 0x369e); SAW(0x3c, 0xf6a3); SAW(0x26, 8); SAW(0x36, 8);
            SAW(0x42, 0x10); SAW(0, 0xe120);
        } else if (address == 0xbe393) {
            SAW(0x3c, 0xf6ab); AB(0x2d) = 4; SAW(0x26, 8); SAW(0x36, 8);
            digit_fragment_velocity(ram, main_rom, actor);
            SAW(0, 0xe3b3);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x20), AW(0x38));
        if (!shell) move_actor_axis(ram, A(0x23), AW(0x3a));
        if (AW(0x21) > 0x1c0 || (shell && AW(0x21) < 0x40) || outside_rows(ram, actor, shell)
            || AW(0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (shell) {
            if (AW(0x42)) decrement(ram, A(0x42));
            else { SAW(0x2a, BW(0xdc94 + (AW(0x40) & 6))); SAW(0x40, AW(0x40) + 2); }
        }
        queue_sprite(ram, main_rom, actor, 1);
        unsigned material;
        if (!tiles) return WORLD_HANDLER_STOP;
        if (!(terrain_probe(ram, tiles, AW(0x21), AW(0x24), &material) & 0xff02)) {
            AB(0x0d) = 2;
            if (!player_contact(ram, main_rom, actor, &bx)) {
                AB(0x0d) = 0;
                contact_list(ram, main_rom, actor, 0xa6e2, &bx);
                if (AW(0x26)) break;
            }
        }
        blast_6acb(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbe1e8:
        SAW(0x2a, 0x3644); AB(0x2d) = 0x22; AB(0x28) = 0xc0; AB(0x0c) = 4; SAW(0x33, 5);
        SAW(0x50, 8); SAW(0x52, 0x10); SAW(0x54, 0xfffc); SAW(0x4a, AW(0x4c)); SAW(0x42, 0x10);
        SAW(0, 0xe21d);
        /* fall through */
    case 0xbe21d:
        if (!decrement(ram, A(0x42))) SAW(0, 0xe228);
        break;
    case 0xbe228: {
        uint16_t parent = parent_of(ram, actor);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        SAW(0x21, RW(parent + 0x21) + 3);
        SAW(0x24, RW(parent + 0x24) - 0x12 + AW(0x33));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, A(0x33))) SAW(0, 0xe275);
        break;
    }
    case 0xbe275: {
        uint16_t parent = parent_of(ram, actor);
        if (ram[parent + 0x0a]) { finish_animation(ram, actor); break; }
        uint16_t aim = AW(0x52);
        SAW(0x21, RW(parent + 0x21) + BW(0xdcae + aim));
        SAW(0x24, RW(parent + 0x24) + BW(0xdcb0 + aim));
        SAW(0x2a, BW(0xdc9c + (aim >> 1)));
        queue_sprite(ram, main_rom, actor, 1);
        if (AW(0x4a)) decrement(ram, A(0x4a));
        else {
            int fire = 0;
            AB(0x28) = (uint8_t)((((aim >> 1) & 0xff00) | AB(0x52)) << 2) + 0x80;
            for (unsigned i = 0; i < 2 && !fire; ++i) {
                if (!(RW(0x264) & (1u << i))) continue;
                uint16_t player = RW(0x90 + 2 * i);
                uint8_t angle = vector_angle(main_rom, RW(player + 0x21) - AW(0x21), RW(player + 0x24) - AW(0x24));
                fire = AB(0x28) == ((angle + 8) & 0xf0);
            }
            if (fire) {
                uint16_t c = spawn_far(ram, actor, 0xb000, 0xe393, 0);
                WW(c + 0x21, AW(0x21)); WW(c + 0x24, AW(0x24)); WW(c + 0x2a, AW(0x2a));
                ram[c + 0x2c] = AB(0x2c); ram[c + 0x28] = AB(0x28); WW(c + 0x2e, AW(0x2e));
                world_queue_sound(ram, 0x83);
                SAW(0, 0xe1e8);
            }
        }
        if (!decrement(ram, A(0x50))) {
            SAW(0x50, 8);
            SAW(0x52, AW(0x52) + AW(0x54));
            if (!AW(0x52)) SAW(0x54, 4);
            else if ((int16_t)AW(0x52) >= 0x20) SAW(0x54, 0xfffc);
        }
        break;
    }
    /* 7DD4D, 7DDDD: wave spawners sharing the 7DBAA setup (see 7DD3B/7DD71). */
    case 0x7dd4d:
    case 0x7dddd: {
        unsigned flags = AB(0x1f);
        unsigned schedule = BW(0xdb7a + ((flags >> 1) & 14));
        unsigned kind = BW(address + 13);
        SAW(0x60, BW(address + 3)); SAW(0x62, 0xb000); SAW(0x0e, kind);
        SAW(0x44, (flags & 32) | BW(0xdb8a + kind));
        SAW(0x42, BW(0xdb42 + ((flags << 1) & 6)));
        SAW(0x50, 1);
        SAW(0x52, BW(0xdb4a + schedule));
        SAW(0x54, BW(0xdb4c + schedule)); SAW(0x56, BW(0xdb4c + schedule));
        SAW(0x58, BW(0xdb4e + schedule)); SAW(0x5a, BW(0xdb4e + schedule));
        SAW(0x48, RW(0xb700) + 16 * AB(0x1e));
        SAW(0, 0xdc27);
        break;
    }
    /* B8184: convoy of four freighters (hull B8260, deck gun B84F9, funnel B86B5). */
    case 0xb8184:
        world_acquire_resource(ram, actor, 0x20);
        world_acquire_resource(ram, actor, 0x22);
        for (unsigned i = 0; i < 4; ++i) {
            uint16_t c = spawn_far(ram, actor, 0xb000, 0x8260, 0);
            WW(c + 0x14, AW(0x14));
            ram[c + 0x2c] = AB(0x14);
            WW(c + 0x21, AW(0x21) + 0x80 * i);
            WW(c + 0x24, AW(0x24) + 0x20 * i);
            WW(c + 0x46, i);
        }
        SAW(0x48, 0x0f); SAW(0, 0x8244);
        /* fall through */
    case 0xb8244:
        if (!AW(0x48)) finish_animation(ram, actor);
        break;
    case 0xb8260: {
        AB(0x2d) = 0x3e; SAW(0x26, 0x280); SAW(0x36, 0x280); SAW(0x0e, 0x10);
        uint16_t c = spawn_far(ram, actor, 0xb000, 0x84f9, 0);
        ram[c + 0x2d] = 0x3d; WW(c + 0x2a, 0x9d52); WW(c + 0x3c, 0xf50b);
        WW(c + 0x26, 0x180); WW(c + 0x36, 0x180); WW(c + 0x31, 8); WW(c + 0x34, 0xffa2);
        ram[c + 0x2c] = AB(0x15);
        c = spawn_far(ram, actor, 0xb000, 0x86b5, 0);
        ram[c + 0x2d] = 0x3f; WW(c + 0x2a, 0x9d8c); ram[c + 0x2c] = AB(0x2c);
        if (AW(0x46) & 2) {
            c = spawn_far(ram, actor, 0, 0xdb28, 0);
            WW(c + 0x21, AW(0x21) + 0x20);
            WW(c + 0x1e, BW(0x7fb0 + 2 * ((AW(0x46) - 2) & 1)));
        }
        SAW(0, 0x82f5);
        break;
    }
    case 0xb82f5: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t parent = parent_of(ram, actor);
        if ((int16_t)AW(0x21) < -0x20) {
            WW(parent + 0x48, RW(parent + 0x48) & ~(1u << (AW(0x46) & 15)));
            finish_animation(ram, actor);
            break;
        }
        if (!(RW(parent + 0x48) & (1u << ((AW(0x46) + 1) & 15)))) SAW(0x1e, 1);
        SAW(0x2a, BW(0x7f0e + 2 * (AW(0x1e) & 1)));
        if ((int16_t)AW(0x21) < 0x1e0) queue_sprite(ram, main_rom, actor, 0);
        if (AB(0x19)) --AB(0x19);
        uint16_t shot = 0;
        for (uint16_t shape = 0xf4eb; shape <= 0xf503 && !shot; shape += 8) {
            SAW(0x3c, shape);
            AB(0x0d) = 1;
            shot = contact_list(ram, main_rom, actor, 0xa6e2, &bx);
        }
        if (shot) {
            armour_hit(ram, main_rom, actor, shot);
            if (!AB(0x19)) {
                burst_script(ram, actor, 0xb000, 0x7f3a, 2);
                AB(0x19) = (AB(0x19) & 1) + 0x20;
            }
        }
        if (AW(0x26)) break;
        if (AW(0x21) >= 0x190) { SAW(0x26, 0x100); break; }
        AB(0x0a) |= 2; SAW(0x42, 0x10);
        WW(0xb8a0, RW(0xb8a0) + 5);
        world_queue_sound(ram, 0x5f);
        WW(0xc8 + 2 * AW(0x46), actor);
        world_queue_sound(ram, 0x6d);
        SAW(0, 0x840f);
        break;
    }
    case 0xb840f:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, A(0x42))) {
            uint16_t parent = parent_of(ram, actor);
            if (!tiles) return WORLD_HANDLER_STOP;
            SAW(0x2a, 0x9cd8); SAW(0x3a, 0xffc0);
            WW(parent + 0x48, RW(parent + 0x48) & ~(1u << (AW(0x46) & 15)));
            AB(0x0a) |= 4;
            sink_hull_tiles(ram, tiles, bank, actor);
            SAW(0, 0x844b);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xb844b: {
        unsigned material;
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        queue_sprite(ram, main_rom, actor, 0);
        SAW(0x42, AW(0x42) + 1);
        if (terrain_probe(ram, tiles, AW(0x21), AW(0x24) - BW(0x8130 + 2 * AW(0x46)), &material) & 0xff02)
            SAW(0, 0x84a6);
        if (hull_pushes_players(ram, main_rom, bank, actor)) {
            WW(0xc8 + 2 * AW(0x46), 0);
            burst_script(ram, actor, 0xb000, 0x7fb4, 4);
            SAW(0x42, 0x20); SAW(0, 0x84d6);
        }
        break;
    }
    case 0xb84a6:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if ((int16_t)AW(0x21) < -0x20) { WW(0xc8 + 2 * AW(0x46), 0); finish_animation(ram, actor); }
        break;
    case 0xb84d6:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        if (!decrement(ram, A(0x42))) finish_animation(ram, actor);
        break;
    case 0xb84f9: {
        uint16_t parent = parent_of(ram, actor);
        if (ram[parent + 0x0a] & 1) { finish_animation(ram, actor); break; }
        if (ram[parent + 0x0a] & 2) SAW(0x26, 0);
        if (AB(0x19)) --AB(0x19);
        SAW(0x21, (AB(0x19) & 2) + RW(parent + 0x21) + AW(0x31));
        SAW(0x24, RW(parent + 0x24) + AW(0x34));
        if ((int16_t)AW(0x21) < 0x1c0) queue_sprite(ram, main_rom, actor, 0);
        AB(0x0d) = 1;
        uint16_t shot = contact_list(ram, main_rom, actor, 0xa6e2, &bx);
        if (shot) {
            armour_hit(ram, main_rom, actor, shot);
            if (!AB(0x19)) {
                burst_script(ram, actor, 0xb000, 0x7f1a, 2);
                AB(0x19) = (AB(0x19) & 1) + 0x20;
            }
        }
        if (AW(0x26)) break;
        if (AW(0x21) >= 0x198) { SAW(0x26, 0x100); break; }
        if (!tiles) return WORLD_HANDLER_STOP;
        WW(parent + 0x26, 0);
        SAW(0x3a, 0xff80); SAW(0x44, 1);
        uint16_t c = spawn_far(ram, actor, 0xb000, 0x8646, 0);
        WW(c + 0x21, AW(0x21)); WW(c + 0x24, AW(0x24) + 0x40); ram[c + 0x2c] = AB(0x2c);
        sink_gun_tiles(ram, tiles, bank, actor);
        SAW(0, 0x85eb);
        break;
    }
    case 0xb8646:
        AB(0x2d) = 0x3d; SAW(0x3a, 0xff60); SAW(0x44, 1); SAW(0, 0x865a);
        break;
    case 0xb85eb:
    case 0xb865a: {
        uint16_t sprites = address == 0xb85eb ? 0x7f5a : 0x7f62, times = address == 0xb85eb ? 0x7f5e : 0x7f68;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        if (outside_rows(ram, actor, 0)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, A(0x44))) {
            uint16_t frame = AW(0x40);
            SAW(0x2a, BW(sprites + frame)); SAW(0x44, BW(times + frame)); SAW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb86b5: {
        uint16_t parent = parent_of(ram, actor);
        if (ram[parent + 0x0a] & 1) { finish_animation(ram, actor); break; }
        if (ram[parent + 0x0a] & 4) { SAW(0x2a, 0x9dae); SAW(0x3a, 0xff40); SAW(0, 0x8706); }
        SAW(0x21, RW(parent + 0x21)); SAW(0x24, RW(parent + 0x24));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb8706:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        if (AW(0x24) < 0x48) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* B88E1: palette cycle for the stage's water surface. */
    case 0xb88e1:
        AB(0x2c) = 0x16; SAW(0x42, 1); SAW(0, 0x88ef);
        /* fall through */
    case 0xb88ef:
        if (RW(0xb700) >= 0xf00 || (RW(0x262) & 0x800)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, A(0x42))) {
            if (!(RW(0x262) & 0x8800)) {
                uint16_t job = 0x9e06 + 8 * (AB(0x2c) & 0x3f);
                WW(job, BW(0x8104 + (AW(0x40) & 2)));
                WW(job + 2, 0x80); WW(job + 4, 6);
                ram[job + 6] = 0x20; ram[job + 7] = 0x10;
            }
            SAW(0x42, 0x51);
            SAW(0x40, AW(0x40) + 2);
        }
        break;
    /* B8939: armoured gate. */
    case 0xb8939:
        world_acquire_resource(ram, actor, 0x2c);
        AB(0x2d) = 0x1f; SAW(0x3c, 0xf52b); SAW(0x2a, 0xa7ba); SAW(0x26, 0x90); SAW(0x36, 0x90);
        SAW(0x31, AW(0x21)); SAW(0, 0x8965);
        break;
    case 0xb8965:
    case 0xb89fd: {
        SAW(0x31, AW(0x31) + RW(0xb71a));
        SAW(0x34, AW(0x34) + RW(0xb71c));
        SAW(0x24, AW(0x24) + RW(0xb71c));
        if (address == 0xb8965 && AW(0x31) < 0x10) { finish_animation(ram, actor); break; }
        if (AB(0x19)) --AB(0x19);
        SAW(0x21, (AB(0x19) & 2) + AW(0x31));
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xb89fd) {
            if (!decrement(ram, A(0x42))) finish_animation(ram, actor);
            break;
        }
        AB(0x0d) = 1;
        uint16_t shot = contact_list(ram, main_rom, actor, 0xa6e2, &bx);
        if (shot) {
            armour_hit(ram, main_rom, actor, shot);
            if (!AB(0x19)) {
                ram[burst_script(ram, actor, 0xb000, 0x8138, 2) + 0x2d] = 0xfe;
                AB(0x19) = (AB(0x19) & 1) + 0x18;
            }
        }
        if (AW(0x26)) break;
        ram[burst_script(ram, actor, 0xb000, 0x8158, 4) + 0x2d] = 0xff;
        SAW(0x42, 0x20); SAW(0, 0x89fd);
        break;
    }
    /* 0DB28: scenery strip that follows a freighter until it sinks. */
    case 0xdb28: {
        unsigned stage = 2 * RW(0x84);
        SAW(0x50, BW(BW(0xfe5f + stage) + (AW(0x1e) & 6)));
        SAW(0x52, BW(0xfe87 + (AB(0x1f) & 0x1e)));
        SAW(0x54, BW(0xdcff + stage));
        SAW(0, 0xdb64);
    }
        /* fall through */
    case 0xdb64:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (ram[parent_of(ram, actor) + 0x0a] & 2) { SAW(0x52, 0); SAW(0, 0xda4f); }
        update_scenery(ram, main_rom, tiles, actor, 0);
        break;
    /* 0B553: homing torpedo, steering phase (launch 0B4CE/0B52F belongs to stage 1). */
    case 0xb553:
    case 0xb57b:
    case 0xb5eb: {
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address == 0xb553) {
            uint16_t count = RW(0xb6ea), target = 0;
            if (count) {
                uint16_t cursor = RW(0xb6ec);
                if (cursor >= count) cursor = 0;
                target = RW(0xb6ee + cursor);
                WW(0xb6ec, cursor + 2);
            }
            SAW(0x4e, target); SAW(0, 0xb57b);
        }
        if (address != 0xb5eb) {
            uint16_t target = AW(0x4e);
            if (!target || (ram[target + 0x0a] & 1)) SAW(0, 0xb5eb);
            else if (!decrement(ram, A(0x5a))) {
                SAW(0x5a, 2);
                uint8_t wanted = (vector_angle(main_rom, RW(target + 0x21) - AW(0x21),
                                               RW(target + 0x24) - AW(0x24)) + 4) & 0xf8;
                uint8_t heading = AB(0x28);
                if (wanted != heading)
                    wanted = ((uint8_t)(wanted - heading) < (uint8_t)(heading - wanted) ? heading + 8 : heading - 8) & 0xf8;
                AB(0x28) = wanted;
                SAW(0x2a, BW(0xb337 + (wanted >> 2)));
                digit_fragment_velocity(ram, main_rom, actor);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x20), AW(0x38));
        move_actor_axis(ram, A(0x23), AW(0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, A(0x42))) {
            SAW(0x42, 4);
            uint16_t c = spawn_tail(ram, 0, 0x9e17, 0);
            unsigned heading = (AB(0x28) & 0xf8) >> 1;
            WW(c + 0x21, AW(0x21) + BW(0xb3e1 + heading));
            WW(c + 0x24, AW(0x24) + BW(0xb3e3 + heading));
            ram[c + 0x2c] = AB(0x14);
            WW(c + 0x56, 0x60); WW(c + 0x58, 0x11);
        }
        unsigned material;
        int gone = 1;
        if (terrain_probe(ram, tiles, AW(0x21), AW(0x24), &material) & 0x5502) blast_6acb(ram, actor);
        else if (AW(0x4a) && material == 4) seabed_impact(ram, actor);
        else if (off_screen_x(ram, actor) || outside_rows(ram, actor, 1)) { }
        else if (!AW(0x26)) blast_6acb(ram, actor);
        else gone = 0;
        if (gone) {
            decrement(ram, parent_of(ram, actor) + 0x44);
            finish_animation(ram, actor);
        } else if (AW(0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
        break;
    }
    /* 04138: boss-room camera: vertical tracking only. */
    case 0x4138:
        update_vertical_camera(ram, main_rom, actor);
        WW(0x26e, RW(0x26e) & ~0x4000u);
        WW(0xb71a, 0); WW(0xb72a, 0); WW(0xb726, 0);
        break;
    /* 1289C: stage boss: two armoured bodies (129C9) and two seabed vents (13BBF). */
    case 0x1289c: {
        WW(0x26e, RW(0x26e) | 0x8000);
        world_acquire_resource(ram, actor, 0x40);
        world_acquire_resource(ram, actor, 0x1e);
        world_acquire_resource(ram, actor, 0xa9);
        world_acquire_resource(ram, actor, 0x1f);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t c = spawn_far(ram, actor, 0x1000, 0x29c9, 0);
            WW(c + 0x70, AW(0x14)); WW(c + 0x72, AW(0x16));
            WW(c + 0x21, i ? 0x20 : 0x1e0);
            WW(c + 0x24, (i ? 0xd8 : 0x60) + RW(0xb732));
            if (i) { WW(c + 0x1e, 2); ram[c + 0x29] = 1; }
        }
        SAW(0x40, 2);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t c = spawn_far(ram, actor, 0x1000, 0x3bbf, 0);
            WW(c + 0x21, i ? 0x160 : 0xa0);
            WW(c + 0x24, 0x118 + RW(0xb732));
            if (i) WW(c + 0x1e, 2);
        }
        SAW(0x42, 0x20);
        queue_music(ram, 0x11);
        SAW(0, 0x2968);
        break;
    }
    case 0x12968:
        if (!decrement(ram, A(0x42))) {
            palette_job(ram, 0x22, 0x149, 7);
            palette_job(ram, 0x23, 0x146, 7);
            SAW(0, 0x298c);
        }
        /* fall through */
    case 0x1298c:
        if (!AW(0x40)) { WW(0x262, RW(0x262) | 0x80); SAW(0x42, 0x100); SAW(0, 0x29a4); }
        break;
    case 0x129a4:
        if (!decrement(ram, A(0x42))) { WW(0x262, RW(0x262) | 0xc000); finish_animation(ram, actor); }
        break;
    case 0x129c9: {
        world_acquire_resource(ram, actor, 0x1f);
        world_acquire_resource(ram, actor, BW(0x2890 + (AW(0x1e) & 2)));
        world_acquire_resource(ram, actor, 0x1a);
        AB(0x2c) = AB(0x15); SAW(0x0e, 0x42); AB(0x2d) = 0x30; SAW(0x0c, 4); SAW(0x2a, 0x9a57);
        uint16_t p = BW(0x2814 + RW(0x8c));
        SAW(0x26, BW(p + 0x2834)); SAW(0x36, BW(p + 0x2834));
        SAW(0x50, BW(p + 0x283a)); SAW(0x52, BW(p + 0x283a));
        SAW(0x58, BW(p + 0x2838)); SAW(0x5a, BW(p + 0x2838));
        SAW(0x64, BW(p + 0x2842)); SAW(0x66, BW(p + 0x2842));
        uint16_t c = spawn_far(ram, actor, 0x1000, 0x3c40, 0);
        ram[c + 0x2c] = AB(0x2c); WW(c + 0x1e, AW(0x1e)); WW(c + 0x42, 0x20);
        c = spawn_far(ram, actor, 0x1000, 0x336a, 0);
        WW(c + 0x15, AW(0x15)); ram[c + 0x2c] = AB(0x15); WW(c + 0x71, AW(0x71));
        ram[c + 0x29] = AB(0x29); WW(c + 0x6a, 0);
        c = spawn_far(ram, actor, 0x1000, 0x2e09, 0);
        WW(c + 0x15, AW(0x15)); ram[c + 0x2c] = AB(0x15); WW(c + 0x70, AW(0x70)); ram[c + 0x72] = AB(0x72);
        ram[c + 0x29] = AB(0x29); WW(c + 0x1e, (AW(0x1e) & 2) >> 1); WW(c + 0x6a, 4);
        c = spawn_far(ram, actor, 0x1000, 0x3478, 0);
        WW(c + 0x15, AW(0x15)); ram[c + 0x2c] = AB(0x15); WW(c + 0x72, AW(0x72)); ram[c + 0x29] = AB(0x29);
        unsigned side = AW(0x1e) & 2;
        SAW(0x6c, BW(0x2254 + side)); SAW(0x62, BW(0x2298 + side));
        script_begin(ram, main_rom, actor, BW(0x223c + side));
        SAW(0, 0x2af6);
        break;
    }
    case 0x12af6:
    case 0x12c3e:
    case 0x12c52: {
        if (address == 0x12af6) {
            int step = script_step(ram, main_rom, actor);
            if (!step) return WORLD_HANDLER_STOP;
            if (step < 0) {
                unsigned row = AW(0x6e) & 0x1e;
                SAW(0x4a, BW(AW(0x62) + row));
                script_begin(ram, main_rom, actor, BW(AW(0x6c) + row));
                SAW(0x6e, AW(0x6e) + 2);
                if (!(AW(0x4a) & 0x40)) SAW(0x58, AW(0x5a));
                else { SAW(0x50, 0x20); SAW(0x58, 0x13); }
            }
            boss_draw(ram, main_rom, actor);
            if (!(AW(0x4a) & 1) && RW(parent_of(ram, actor) + 0x40) == 1) {
                SAW(0x2e, 0xc0); SAW(0x54, 0x80); SAW(0x56, 0x80); SAW(0x58, 3); SAW(0x5a, 4); SAW(0, 0x2c3e);
            }
            if ((AW(0x4a) & 4) && !(AW(0x60) & 4) && !decrement(ram, A(0x50))) {
                SAW(0x50, AW(0x52));
                SAW(0x60, AW(0x60) | 4);
            }
        } else {
            unsigned material;
            if (!tiles) return WORLD_HANDLER_STOP;
            if (address == 0x12c3e) { SAW(0x4a, 0x2a); SAW(0x5e, 1); SAW(0x68, 1); SAW(0, 0x2c52); }
            if (!decrement(ram, A(0x5e))) {
                SAW(0x5e, 0x20);
                if (AW(0x68)) {
                    unsigned ix = 0; int none;
                    SAW(0x68, 0);
                    uint16_t target = choose_target(ram, actor, &ix, &none);
                    SAW(0x48, none ? RW(0x90) : target);
                }
                uint16_t target = AW(0x48);
                uint16_t x = RW(target + 0x21) + BW(0x2234 + AW(0x1e) + (AW(0x4a) & 1 ? 4 : 0));
                uint16_t y = RW(target + 0x24);
                x = x < 0xa0 ? 0xa0 : x >= 0x160 ? 0x160 : x;
                y = y < 0xd0 ? 0xd0 : y >= 0x130 ? 0x130 : y;
                AB(0x28) = vector_angle(main_rom, x - AW(0x21), y - AW(0x24));
                digit_fragment_velocity(ram, main_rom, actor);
            }
            scroll_actor(ram, actor, 0xb71a);
            if (!(AW(0x6a) & 1)) {
                move_actor_axis(ram, A(0x20), AW(0x38));
                move_actor_axis(ram, A(0x23), AW(0x3a));
            }
            terrain_probe(ram, tiles, AW(0x21), AW(0x24) + 0x10, &material);
            if (material == 3) { SAW(0x3a, -AW(0x3a)); SAW(0x5e, 0x20); }
            boss_draw(ram, main_rom, actor);
            if (AW(0x4a) & 1) {
                if (!decrement(ram, A(0x42))) { SAW(0x54, AW(0x56)); SAW(0x2e, 0xc0); SAW(0x4a, AW(0x4a) & ~1u); }
            } else if (!AW(0x54)) {
                if (!decrement(ram, A(0x42))) { SAW(0x54, AW(0x56)); SAW(0x6a, AW(0x6a) & ~1u); }
                else if (AW(0x42) == 0x20) boss_launch_homing(ram, actor);
            } else if (!decrement(ram, A(0x54))) {
                if (decrement(ram, A(0x58))) { SAW(0x42, 0x40); SAW(0x6a, AW(0x6a) | 1); }
                else {
                    SAW(0x58, AW(0x5a)); SAW(0x2e, 0x80); SAW(0x42, 0x120);
                    SAW(0x4a, AW(0x4a) | 1); SAW(0x68, 1);
                }
            }
        }
        boss_attack_gate(ram, main_rom, actor);
        for (uint16_t shape = 0xf7ab; shape >= 0xf79b; shape -= 8) {
            SAW(0x3c, shape);
            AB(0x0d) = 2;
            player_contact(ram, main_rom, actor, &bx);
            AB(0x0d) = 0;
            if (contact_list(ram, main_rom, actor, 0xa6e2, &bx)) break;
        }
        if (AW(0x26)) break;
        if (!(RW(0x264) & 3)) { SAW(0x26, 0x100); break; }
        uint16_t credit = 0x124 + ((RW(bx + 0x0b) & 2) << 5);
        WW(credit, RW(credit) | 2);
        world_award_score(ram, main_rom, actor, bx);
        burst_script(ram, actor, 0x1000, 0x2788, 4);
        SAW(0x31, AW(0x21)); SAW(0x34, AW(0x24)); SAW(0x42, 0x18);
        AB(0x0a) |= 2; AB(0x19) = 0; AB(0x2c) = AB(0x15); SAW(0, 0x2d79);
        break;
    }
    case 0x12d79:
        SAW(0x31, AW(0x31) + RW(0xb71a)); SAW(0x34, AW(0x34) + RW(0xb71c));
        boss_shake_draw(ram, main_rom, actor);
        if (!decrement(ram, A(0x42))) {
            decrement(ram, parent_of(ram, actor) + 0x40);
            SAW(0x3a, 0xff00);
            AB(0x2d) += 0x0e;
            palette_job(ram, AB(0x15), BW(0x2898 + (AW(0x1e) & 2)), 5);
            SAW(0, 0x2dbd);
        }
        break;
    case 0x12dbd: {
        unsigned material;
        if (!tiles) return WORLD_HANDLER_STOP;
        SAW(0x31, AW(0x31) + RW(0xb71a)); SAW(0x34, AW(0x34) + RW(0xb71c));
        move_actor_axis(ram, A(0x33), AW(0x3a));
        boss_shake_draw(ram, main_rom, actor);
        if (terrain_probe(ram, tiles, AW(0x21), AW(0x24) - 8, &material) & 0xff02) {
            script_begin(ram, main_rom, actor, 0x243c);
            SAW(0, 0x2dec);
        }
        break;
    }
    case 0x12dec: {
        int step = script_step(ram, main_rom, actor);
        if (!step) return WORLD_HANDLER_STOP;
        if (step < 0) SAW(0, 0x2dfe);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x12dfe:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    /* 12E09: upper claw. */
    case 0x12e09: {
        AB(0x2d) = 0x31; SAW(0x2a, 0x9a91);
        SAW(0x2e, BW(BW(0x2814 + RW(0x8c)) + 0x283c));
        uint16_t c = spawn_far(ram, actor, 0x1000, 0x2fc7, 0);
        WW(c + 0x15, AW(0x15)); ram[c + 0x2c] = AB(0x15); ram[c + 0x70] = AB(0x70);
        ram[c + 0x29] = AB(0x29); WW(c + 0x1e, AW(0x1e));
        SAW(0, 0x2e4f);
        break;
    }
    case 0x12e4f: case 0x12e7f: case 0x12eed: case 0x12f20:
    case 0x12ff6: case 0x1305a: case 0x130aa: case 0x130f7:
    case 0x13392: case 0x133ce: case 0x13411: case 0x1343a:
    case 0x134a0: case 0x134e4: case 0x13527: case 0x135dc: {
        uint16_t host, frame = AW(0x40);
        int level = address >= 0x12ff6 && address <= 0x130f7 ? 2 : 1;
        if (boss_ride(ram, actor, level, &host)) {
            AB(0x2d) += 0x0e;
            SAW(0, 0x2f66);
            boss_draw(ram, main_rom, actor);
            break;
        }
        switch (address) {
        case 0x12e4f:
            if (RW(host + 0x4a) & 1) { SAW(0x40, 0); SAW(0x44, 1); SAW(0x66, AW(0x66) | 1); SAW(0, 0x2e7f); }
            break;
        case 0x12e7f:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x246a + frame) == 0xffff) {
                if (RW(host + 0x4a) & 0x20) SAW(0x42, 0x80);
                SAW(0, 0x2eed);
            } else {
                uint16_t pose = BW(0x246a + frame);
                WW(host + 0x40, pose);
                SAW(0x2a, BW(0x24a2 + pose)); SAW(0x44, BW(0x24ba + frame)); SAW(0x40, frame + 2);
                if (frame == 0x14) {
                    uint16_t c = spawn_far(ram, actor, 0x1000, 0x2f76, 0);
                    ram[c + 0x2c] = AB(0x70); ram[c + 0x29] = AB(0x29);
                }
            }
            break;
        case 0x12eed:
            if (!(AW(0x66) & 1)) { SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x2f20); }
            boss_draw(ram, main_rom, actor);
            if (!decrement(ram, A(0x42))) boss_fire(ram, bank, actor);
            goto boss_part_done;
        case 0x12f20:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x248e + frame) == 0xffff) SAW(0, 0x2e4f);
            else {
                uint16_t pose = BW(0x248e + frame);
                WW(host + 0x40, pose);
                SAW(0x2a, BW(0x24a2 + pose)); SAW(0x44, BW(0x24dc + frame)); SAW(0x40, frame + 2);
            }
            break;
        case 0x12ff6:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x2510 + frame) == 0xffff) {
                SAW(0x40, 0); SAW(0x44, 1); SAW(0x42, 4);
                uint16_t c = spawn_far(ram, actor, 0x1000, 0x3129, 0);
                ram[c + 0x2c] = AB(0x70); WW(c + 0x1e, AW(0x1e));
                SAW(0, 0x305a);
            } else { SAW(0x2a, BW(0x2510 + frame)); SAW(0x44, BW(0x2528 + frame)); SAW(0x40, frame + 2); }
            break;
        case 0x1305a:
            for (;;) {
                if (!(RW(host + 0x4a) & 1)) SAW(0, 0x30aa);
                if (decrement(ram, A(0x44))) break;
                SAW(0x2a, BW(0x251e + AW(0x40))); SAW(0x44, AW(0x42)); SAW(0x40, AW(0x40) + 2);
                if (AW(0x40) < 0x0a) break;
                SAW(0x40, 0);
                if (!decrement(ram, A(0x42))) { SAW(0x42, 1); break; }
            }
            break;
        case 0x130aa:
            if (decrement(ram, A(0x44))) break;
            SAW(0x2a, BW(0x251e + frame)); SAW(0x44, AW(0x42)); SAW(0x40, frame + 2);
            if (AW(0x40) >= 0x0a) {
                SAW(0x40, 0);
                SAW(0x42, AW(0x42) + 1);
                if (AW(0x42) >= 5) { SAW(0x42, 0x10); SAW(0, 0x30f7); }
            }
            break;
        case 0x130f7:
            boss_draw(ram, main_rom, actor);
            if (!decrement(ram, A(0x42))) {
                uint16_t parent = parent_of(ram, actor);
                WW(parent + 0x66, RW(parent + 0x66) & ~1u);
                SAW(0, 0x2fd1);
            }
            goto boss_part_done;
        case 0x13392:
            SAW(0x2a, BW(0x2610 + RW(host + 0x40)));
            if (RW(host + 0x60) & 4) { SAW(0x58, RW(host + 0x58)); SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x33ce); }
            break;
        case 0x133ce:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x25f4 + frame) == 0xffff) { SAW(0x54, 1); SAW(0, 0x3411); }
            else { SAW(0x2a, BW(0x25f4 + frame)); SAW(0x44, BW(0x2628 + frame)); SAW(0x40, frame + 2); }
            break;
        case 0x13411:
            if (boss_burst(ram, bank, actor)) { SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x343a); }
            break;
        case 0x1343a:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x2606 + frame) == 0xffff) SAW(0, 0x3392);
            else { SAW(0x2a, BW(0x2606 + frame)); SAW(0x44, BW(0x2638 + frame)); SAW(0x40, frame + 2); }
            break;
        case 0x134a0:
            if (RW(host + 0x60) & 2) SAW(0x60, 1);
            else if (RW(host + 0x60) & 0x10) { SAW(0x60, 4); SAW(0x58, 3); }
            else break;
            SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x34e4);
            break;
        case 0x134e4:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x2640 + frame) == 0xffff) { SAW(0x54, 1); SAW(0, 0x3527); }
            else { SAW(0x2a, BW(0x2640 + frame)); SAW(0x44, BW(0x2672 + frame)); SAW(0x40, frame + 2); }
            break;
        case 0x13527: {
            boss_draw(ram, main_rom, actor);
            if (decrement(ram, A(0x54))) goto boss_part_done;
            SAW(0x54, AW(0x56));
            if (!decrement(ram, A(0x58))) { SAW(0x58, AW(0x5a)); SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x35dc); }
            uint16_t c = spawn_far(ram, actor, 0x1000, 0x3863, 0);
            unsigned lane = AW(0x58) % 3;
            unsigned row = 2 * (AB(0x29) & 1) + 4 * lane;
            ram[c + 0x29] = AB(0x29);
            WW(c + 0x58, lane);
            SAW(0x31, BW(0x26a0 + row)); WW(c + 0x21, AW(0x31) + AW(0x21));
            SAW(0x34, BW(0x26ac + row)); WW(c + 0x24, AW(0x34) + AW(0x24));
            ram[c + 0x2c] = AB(0x73);
            c = spawn_far(ram, actor, 0x1000, 0x37f8, 0);
            ram[c + 0x2c] = AB(0x72); ram[c + 0x29] = AB(0x29);
            WW(c + 0x31, AW(0x31)); WW(c + 0x34, AW(0x34));
            WW(c + 0x4c, 0x26be); WW(c + 0x4e, 0x26d8);
            goto boss_part_done;
        }
        default:
            if (decrement(ram, A(0x44))) break;
            if (BW(0x2658 + frame) == 0xffff) {
                uint16_t parent = parent_of(ram, actor);
                WW(parent + 0x60, RW(parent + 0x60) & ~(1u << (AW(0x60) & 15)));
                SAW(0, 0x34a0);
            } else { SAW(0x2a, BW(0x2658 + frame)); SAW(0x44, BW(0x2688 + frame)); SAW(0x40, frame + 2); }
            break;
        }
        boss_draw(ram, main_rom, actor);
boss_part_done:
        break;
    }
    case 0x12f66:
        AB(0x0a) = 2; SAW(0, 0x2f6f);
        /* fall through */
    case 0x12f6f: {
        uint16_t host;
        boss_ride(ram, actor, 1, &host);
        boss_draw(ram, main_rom, actor);
        break;
    }
    case 0x12f76:
        AB(0x2d) = 0x2c; SAW(0x31, 0xffc0); SAW(0x44, 1); SAW(0, 0x2f89);
        /* fall through */
    case 0x12f89:
        boss_ride_offset(ram, actor, 1);
        if (!decrement(ram, A(0x44))) {
            uint16_t frame = AW(0x40);
            if (BW(0x24ee + frame) == 0xffff) { finish_animation(ram, actor); break; }
            SAW(0x2a, BW(0x24ee + frame)); SAW(0x44, BW(0x2500 + frame)); SAW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x12fc7:
        AB(0x2d) = 0x2d; SAW(0, 0x2fd1);
        break;
    case 0x12fd1:
        if (RW(parent_of(ram, actor) + 0x66) & 1) { SAW(0x40, 0); SAW(0x44, 1); SAW(0, 0x2ff6); }
        break;
    /* 13129: tentacle root; 132A3 its tip; 13308 the trailing segments. */
    case 0x13129: {
        SAW(0x3c, 0xf7d3); SAW(0x31, BW(0x2534)); SAW(0x34, 0xffee); SAW(0x38, BW(0x2538));
        SAW(0x42, 0x18); SAW(0x68, 1);
        uint16_t c = spawn_far(ram, actor, 0x1000, 0x32a3, 0);
        WW(c + 0x31, AW(0x31)); WW(c + 0x34, AW(0x34)); ram[c + 0x2c] = AB(0x2c); WW(c + 0x62, 0x0f);
        SAW(0x62, AW(0x62) | 0x8000);
        SAW(0, 0x3177);
    }
        /* fall through */
    case 0x13177:
    case 0x131fa:
    case 0x13248: {
        uint16_t host = boss_ride_offset(ram, actor, 1);
        if (ram[host + 0x0a]) { finish_animation(ram, actor); break; }
        if (address != 0x131fa) move_actor_axis(ram, A(0x30), AW(0x38));
        boss_sweep(ram, main_rom, bank, actor);
        boss_segment_draw(ram, main_rom, bank, actor, address == 0x131fa ? 0x2568 : 0x2552);
        if (address == 0x13248) {
            if (!(AW(0x42) & 3)) SAW(0x62, AW(0x62) & ~(1u << ((AW(0x42) >> 2) & 15)));
            SAW(0x42, AW(0x42) + 1);
            if (AW(0x42) >= 0x18) { SAW(0x62, AW(0x62) & 0x7fff); finish_animation(ram, actor); }
            break;
        }
        if (address == 0x131fa) {
            if (!(RW(ancestor(ram, actor, 3) + 0x4a) & 1)) {
                SAW(0x40, 0); SAW(0x42, 0); SAW(0x38, -AW(0x38)); SAW(0, 0x3248);
            }
        } else {
            decrement(ram, A(0x42));
            if (!(AW(0x42) & 3)) {
                uint16_t c = spawn_far(ram, actor, 0x1000, 0x3308, 0);
                WW(c + 0x31, AW(0x31)); WW(c + 0x34, AW(0x34)); ram[c + 0x2c] = AB(0x2c);
                WW(c + 0x1e, AW(0x1e)); WW(c + 0x60, AW(0x60));
                SAW(0x60, AW(0x60) + 2);
                WW(c + 0x62, AW(0x42) >> 2);
                SAW(0x62, AW(0x62) | (1u << ((AW(0x42) >> 2) & 15)));
            }
            if (!AW(0x42)) { SAW(0x40, 0); SAW(0, 0x31fa); }
        }
        if (!decrement(ram, A(0x68))) { SAW(0x68, 0x40); world_queue_sound(ram, 0x47); }
        break;
    }
    case 0x132a3:
    case 0x132ad:
    case 0x13308:
    case 0x13315: {
        int tip = address < 0x13308;
        if (address == 0x132a3) { SAW(0x3c, 0xf7db); SAW(0, 0x32ad); }
        if (address == 0x13308) { SAW(0x3c, 0xf7d3); SAW(0, 0x3315); }
        uint16_t host = boss_ride_offset(ram, actor, 2);
        if (ram[host + 0x0a]) { finish_animation(ram, actor); break; }
        if (tip) {
            boss_segment_draw(ram, main_rom, bank, actor, 0x253c);
            AB(0x0d) = 3;
            uint16_t shot = contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (shot) WW(shot + 0x26, 0);
        } else {
            boss_segment_draw(ram, main_rom, bank, actor, 0x2552);
            if (!(AW(0x60) & 3)) boss_sweep(ram, main_rom, bank, actor);
        }
        if (!(RW(parent_of(ram, actor) + 0x62) & (1u << (AW(0x62) & 15)))) finish_animation(ram, actor);
        break;
    }
    /* 1336A: lower claw; 13478: shell launcher. */
    case 0x1336a: {
        uint16_t p = BW(0x2814 + RW(0x8c));
        AB(0x2d) = 0x2e; SAW(0x2a, 0x9ac3);
        SAW(0x56, BW(p + 0x2836)); SAW(0x2e, BW(p + 0x283c));
        SAW(0, 0x3392);
        break;
    }
    case 0x13478: {
        uint16_t p = BW(0x2814 + RW(0x8c));
        AB(0x2d) = 0x30; SAW(0x2a, 0x9add);
        SAW(0x58, BW(p + 0x2840)); SAW(0x5a, BW(p + 0x2840)); SAW(0x56, 0x10);
        SAW(0, 0x34a0);
        break;
    }
    /* 1362F: claw shot that breaks into a slower fragment (13716). */
    case 0x1362f:
        AB(0x2d) = 0x32; SAW(0x3c, 0xf7b3); SAW(0x26, 0x80); SAW(0x36, 0x80);
        SAW(0x44, 1); SAW(0x54, 1); SAW(0x42, 0x10); SAW(0, 0x3656);
        /* fall through */
    case 0x13656:
        if (!decrement(ram, A(0x42))) { AB(0x2d) = 4; SAW(0, 0x3664); }
        /* fall through */
    case 0x13664:
    case 0x13716: {
        int fragment = address == 0x13716;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x20), AW(0x38));
        if (off_screen_x(ram, actor)) { finish_animation(ram, actor); break; }
        if (fragment) {
            SAW(0x2a, BW(0x25a6 + AW(0x40)));
            SAW(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 6) SAW(0x40, 0);
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            if (!decrement(ram, A(0x44))) {
                SAW(0x2a, BW(0x259e + (AW(0x40) & 6))); SAW(0x44, 3); SAW(0x40, AW(0x40) + 2);
            }
            queue_sprite(ram, main_rom, actor, 1);
            if (!decrement(ram, A(0x54))) {
                SAW(0x54, 4);
                uint16_t c = spawn_tail(ram, 0, 0xbe45, 0);
                WW(c + 0x21, AW(0x21)); WW(c + 0x24, AW(0x24));
                ram[c + 0x2c] = 3; ram[c + 0x2d] = AB(0x2d) + 1;
            }
        }
        AB(0x0d) = 2;
        if (!player_contact(ram, main_rom, actor, &bx)) {
            AB(0x0d) = 0;
            contact_list(ram, main_rom, actor, 0xa6e2, &bx);
            if (AW(0x26)) break;
        }
        if (fragment) { blast_6acb(ram, actor); finish_animation(ram, actor); break; }
        uint16_t c = spawn_far(ram, actor, 0x1000, 0x3795, 0);
        ram[c + 0x2c] = AB(0x2c); ram[c + 0x29] = AB(0x29);
        SAW(0x3c, 0xf7bb); SAW(0x40, 0); AB(0x2d) = 4; SAW(0x26, 0x40); SAW(0, 0x3716);
        break;
    }
    case 0x13795:
        AB(0x2d) = 0x30; SAW(0x44, 1); SAW(0, 0x37a3);
        /* fall through */
    case 0x137a3:
    case 0x137f8:
    case 0x13806: {
        int flash = address < 0x137f8;
        uint16_t sprites = 0x25ac, times = 0x25ba;
        if (address == 0x137f8) { AB(0x2d) = 0x2c; SAW(0x44, 1); SAW(0, 0x3806); }
        if (flash) follow_parent(ram, actor, 0, 0);
        else { follow_parent(ram, actor, AW(0x31), AW(0x34)); sprites = AW(0x4c); times = AW(0x4e); }
        if (!decrement(ram, A(0x44))) {
            uint16_t frame = AW(0x40);
            if (BW(sprites + frame) == 0xffff) { finish_animation(ram, actor); break; }
            SAW(0x2a, BW(sprites + frame)); SAW(0x44, BW(times + frame)); SAW(0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, flash);
        break;
    }
    /* 13863: sinking shell. */
    case 0x13863:
        AB(0x2d) = 0x31; SAW(0x3c, 0xf7cb); SAW(0x26, 1); SAW(0x36, 0x80); SAW(0x3a, 0x100);
        SAW(0x42, 0x78); SAW(0x44, 1); SAW(0x50, 0x10); SAW(0, 0x388f);
        /* fall through */
    case 0x1388f:
        if (!decrement(ram, A(0x50))) { AB(0x2d) = 4; SAW(0, 0x389d); }
        /* fall through */
    case 0x1389d: {
        unsigned material;
        if (!tiles) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, A(0x23), AW(0x3a));
        if (!decrement(ram, A(0x44))) {
            SAW(0x2a, BW(0x26b8 + AW(0x40))); SAW(0x44, 6); SAW(0x40, AW(0x40) + 2);
            if (AW(0x40) >= 6) SAW(0x40, 0);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (decrement(ram, A(0x42))) {
            terrain_probe(ram, tiles, AW(0x21), AW(0x24), &material);
            if (material == 3) {
                explosion(ram, actor, 0x7511, 0x7675, -1, 0x20, 0x40, 0, 0x57);
                finish_animation(ram, actor);
                break;
            }
            AB(0x0d) = 2;
            if (!player_contact(ram, main_rom, actor, &bx)) {
                AB(0x0d) = 0;
                contact_list(ram, main_rom, actor, 0xa6e2, &bx);
                if (AW(0x26)) break;
            }
        }
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    /* 13935: homing shot. */
    case 0x13935: {
        world_acquire_resource(ram, actor, 0x1f);
        AB(0x2d) = 0x32; SAW(0x3c, 0xf7e3); SAW(0x26, 1); SAW(0x36, 0x80); SAW(0x2e, 0x80);
        AB(0x28) = bank[(uint16_t)(0x26f0 + 4 * (AB(0x29) & 1) + AW(0x46))];
        unsigned row = 8 * (AB(0x29) & 1) + 4 * (AW(0x46) & 1);
        uint16_t body = ancestor(ram, actor, (AW(8) - 6) >> 1);
        SAW(0x21, RW(body + 0x21) + BW(0x26f8 + row));
        SAW(0x24, RW(body + 0x24) + BW(0x26fa + row));
        boss_homing_steer(ram, main_rom, bank, actor, 0);
        SAW(0x42, 0x20); SAW(0, 0x39d9);
    }
        /* fall through */
    case 0x139d9:
    case 0x13a79:
    case 0x13aee: {
        unsigned material;
        int hit = (ram[parent_of(ram, actor) + 0x0a] & 2) != 0;
        if (!tiles) return WORLD_HANDLER_STOP;
        if (!hit) {
            int trail = address != 0x13935 && address != 0x139d9;
            scroll_actor(ram, actor, 0xb71a);
            if (address == 0x13a79 && !decrement(ram, A(0x50))) {
                SAW(0x50, 2);
                boss_homing_steer(ram, main_rom, bank, actor, 1);
            }
            move_actor_axis(ram, A(0x20), AW(0x38));
            move_actor_axis(ram, A(0x23), AW(0x3a));
            if (address == 0x13aee && (AW(0x21) > 0x1c0 || outside_rows(ram, actor, 0) || AW(0x21) < 0x40)) {
                finish_animation(ram, actor);
                break;
            }
            queue_sprite(ram, main_rom, actor, 1);
            if (address == 0x13a79) { if (!decrement(ram, A(0x42))) SAW(0, 0x3aee); }
            else if (!trail && !decrement(ram, A(0x42))) {
                AB(0x2d) = 4; SAW(0x2e, 0x100); SAW(0x42, 0x80); SAW(0x50, 1); SAW(0x54, 1); SAW(0, 0x3a79);
            }
            if (trail && !decrement(ram, A(0x54))) {
                SAW(0x54, 4);
                uint16_t c = spawn_tail(ram, 0, 0xbe45, 0);
                WW(c + 0x21, AW(0x21)); WW(c + 0x24, AW(0x24));
                ram[c + 0x2c] = 3; WW(c + 0x1e, 1); ram[c + 0x2d] = 0x33;
            }
            if (!(terrain_probe(ram, tiles, AW(0x21), AW(0x24), &material) & 0xff02)) {
                if (material == 3) { seabed_impact(ram, actor); finish_animation(ram, actor); break; }
                AB(0x0d) = 2;
                if (!player_contact(ram, main_rom, actor, &bx)) {
                    AB(0x0d) = 0;
                    contact_list(ram, main_rom, actor, 0xa6e2, &bx);
                    if (AW(0x26)) break;
                }
            }
        }
        blast_6b2e(ram, actor);
        finish_animation(ram, actor);
        break;
    }
    /* 13BBF: seabed vent bubbles; 13C40: body palette pulse. */
    case 0x13bbf:
        world_acquire_resource(ram, actor, 0x30);
        AB(0x2d) = 0x3f; SAW(0x4c, BW(0x2884 + AW(0x1e)));
        SAW(0x42, 0x80); SAW(0x44, 1); SAW(0x46, 5); SAW(0, 0x3beb);
        break;
    case 0x13beb:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, A(0x44))) {
            uint16_t frame = AW(0x40);
            if (frame & 1) SAW(0x44, 1);
            else { SAW(0x2a, BW(AW(0x4c) + (frame & 2))); SAW(0x44, AW(0x46)); }
            SAW(0x40, frame + 1);
        }
        if (AW(0x40) & 1) queue_sprite(ram, main_rom, actor, 0);
        if (AW(0x42)) {
            decrement(ram, A(0x42));
            if (!(AW(0x42) & 0x1f)) decrement(ram, A(0x46));
        }
        break;
    case 0x13c40:
        if (ram[parent_of(ram, actor) + 0x0a]) { finish_animation(ram, actor); break; }
        if (!decrement(ram, A(0x42))) {
            palette_job(ram, AB(0x2c), BW(0x2890 + (AW(0x40) & 4) + (AW(0x1e) & 2)), 5);
            SAW(0x42, 0x28);
            SAW(0x40, AW(0x40) + 4);
        }
        break;
    /* 01A24: root actor of the stage tally screen; 02612 lays out its panels. */
    case 0x1a24:
        effect_slot(ram, actor, 0x2612);
        SAW(0, 0x1a39); SAW(2, 0);
        break;
    case 0x1a39:
    case 0x2680:
        break;
    case 0x2612: {
        if (!tiles) return WORLD_HANDLER_STOP;
        WW(effect_slot(ram, actor, 0x2681) + 0x1e, 0);
        WW(effect_slot(ram, actor, 0x2681) + 0x1e, 1);
        do {
            WW(effect_slot(ram, actor, 0x2b68) + 0x1e, AW(0x42));
            SAW(0x42, AW(0x42) + 1);
        } while (AW(0x42) < 0x0a);
        for (uint16_t row = 0x1f30; row <= 0x2030; row += 0x100)
            for (unsigned i = 0; i < 0x28; ++i) {
                write_word(tiles, row + 4 * i, 1);
                write_word(tiles, row + 4 * i + 2, 0x111);
            }
        SAW(0, 0x2680);
        break;
    }
    /* 0FF00: after a time-out the clock restarts at 99; 061B4 is the time-out banner. */
    case 0xff00:
        if (decrement(ram, A(0x42))) break;
        if (RW(0x286)) { SAW(0x42, AW(0x42) + 1); break; }
        if (!tiles) return WORLD_HANDLER_STOP;
        SAW(0x42, 0x60); WW(0xb8a0, 0x63); SAW(0, 0x60ce);
        refresh_stage_time(ram, tiles, actor, 0x63);
        if (!decrement(ram, A(0x44))) {
            SAW(0x44, 0x40);
            if (!(RW(0x262) & 2)) {
                queue_palette(ram, 6, read_word(main_rom, 0x60c6 + (AW(0x40) & 2)), 6);
                SAW(0x40, AW(0x40) + 2);
            }
        }
        break;
    case 0x61b4:
        AB(0x2c) = 0x85; SAW(0x2a, 0xa4c1); SAW(0x21, 0x100); SAW(0x24, 0x104); SAW(0x42, 0xc0); SAW(0, 0x61d1);
        /* fall through */
    case 0x61d1:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, A(0x42))) finish_animation(ram, actor);
        break;
    /* A85FF, A86D4, A8715 and A87ED run straight into their first update;
       these entries replace the older cases that returned after the setup. */
    case 0xa86d4:
        world_acquire_resource(ram, actor, 0xe7);
        AB(0x2d) = 4; SAW(0x44, 1); SAW(0, 0x86ea);
        return world_actors_1(pass, ram, main_rom, tiles, actor_io, 0xa86ea);
    case 0xa8715:
        world_acquire_resource(ram, actor, 0x57);
        SAW(0x42, 0x40); SAW(0, 0x8727);
        return world_actors_1(pass, ram, main_rom, tiles, actor_io, 0xa8727);
    case 0xa85ff:
        world_acquire_resource(ram, actor, 0xe4);
        AB(0x2d) = 5; SAW(0x36, 0x800); SAW(0x44, 1); SAW(0, 0x861a);
        return world_actors_1(pass, ram, main_rom, tiles, actor_io, 0xa861a);
    case 0xa87ed:
        AB(0x2c) = 3; AB(0x2d) = 0x30; SAW(0x34, 0xffd4); SAW(0, 0x87ff);
        return world_actors_1(pass, ram, main_rom, tiles, actor_io, 0xa87ff);
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
