/* Recovered World actor handlers: paths missed so far: surface weapon variants, stale-register cases, rarely reached states.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include "world_stale.h"
#include <string.h>

/* See world_stale.h. */
static struct { int known, loaded, fade; uint16_t dx, fade_dx; } stale;

void world_stale_dx_set(uint16_t value) { stale.dx = value; stale.known = stale.loaded = 1; }
void world_stale_dx_forget(void) { stale.known = 0; stale.loaded = 1; }
int world_stale_dx(uint16_t *value) { *value = stale.dx; return stale.known; }
void world_stale_dx_fade(uint16_t value) { stale.fade_dx = value; stale.fade = 1; }

void world_stale_dx_boundary(int pieces, uint16_t dx, unsigned master)
{
    if (pieces) world_stale_dx_set(dx);
    if (stale.fade) world_stale_dx_set(stale.fade_dx);
    stale.fade = 0;
    /* Only the tally controllers are known to keep DX (25B5 loads it when it
     * starts the fade-out; world_title_master.c records that). */
    if (master != 0x25b5 && master != 0x25ff) world_stale_dx_forget();
}

void world_stale_dx_call(uint32_t handler, int done)
{
    if (!done) { stale.loaded = 0; return; }
    if (stale.loaded) return;
    switch (handler) {
    /* Tally handlers that never load DX (0DA4 and 1067 keep it). */
    case 0x1a39: case 0x2680: case 0x2b18: case 0x2b4c: case 0x2b68:
    case 0x2b96: case 0x2bab: case 0x2bfe: case 0x2abb:
        break;
    default:
        stale.known = 0;
    }
}

#define W(a) read_word(ram, (uint16_t)(a))
#define SW(a, v) write_word(ram, (uint16_t)(a), (uint16_t)(v))
#define RW(a) read_word(main_rom, (uint16_t)(a))

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

int world_knocked_away(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t shot)
{
    if (read_word(ram, shot + 0x0e) != 0x121) return 0;
    ram[actor + 0x28] = vector_angle(rom, (int16_t)(read_word(ram, shot + 0x21) - read_word(ram, actor + 0x21)),
                                     (int16_t)(read_word(ram, shot + 0x24) - read_word(ram, actor + 0x24)));
    write_word(ram, actor + 0x2e, 0x400);
    angle_velocity(ram, rom, actor, ram[actor + 0x28]);
    write_word(ram, actor + 0x42, 7);
    write_word(ram, actor, 0xae2a);
    write_word(ram, actor + 2, 0);
    return 1;
}

/* The terrain cell under a screen point, as 68DF leaves it in BW. */
static uint16_t terrain_cell(const uint8_t *ram, uint16_t x, uint16_t y)
{
    return world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
}

/* 6B2E: the medium explosion with its sound. */
static void medium_explosion(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    world_queue_sound(ram, 0x5d);
    uint16_t child = effect_slot(ram, actor, 0x6efe);
    write_word(ram, child + 0x4c, read_word(rom, 0x6b44));
    write_word(ram, child + 0x4a, read_word(rom, 0x6b49));
    ram[child + 0x2c] = rom[0x6b4e];
    ram[child + 0x2d] = rom[0x6b52];
    write_word(ram, child + 0x56, read_word(rom, 0x6b56));
    write_word(ram, child + 0x58, read_word(rom, 0x6b5b));
    if (!(child & 0x300)) write_word(ram, child + 0x44, 0x5d);
}

/* BC616 (grab != 0) and BC6FD: does the tongue shape overlap a target that
 * is not held yet? Targets 0/2 are the players, 4/6 the actors at B8B8/B8BA.
 * BC616 marks the target held and spawns the grab actor on it. */
static int tongue_target(uint8_t *ram, const uint8_t *rom, uint16_t actor, int grab)
{
    uint16_t flags = read_word(ram, 0xb8b2), shape = read_word(ram, actor + 0x3c);
    uint16_t target = 0, which;
    int found = 0;
    for (which = 0; which < 8; which += 2) {
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
    if (!found || !grab) return found;
    write_word(ram, 0xb8b2, flags | (1u << which));
    uint16_t held = bank_b_effect(ram, actor, 0xc0c0);
    write_word(ram, held + 0x21, read_word(ram, target + 0x21));
    write_word(ram, held + 0x24, read_word(ram, target + 0x24));
    ram[held + 0x29] = ram[actor + 0x29];
    write_word(ram, held + 0x60, read_word(ram, actor + 0x60));
    write_word(ram, held + 0x68, which);
    write_word(ram, held + 0x6a, target);
    ram[held + 0x0b] = ram[target + 0x0b];
    return 1;
}

/* BC186/BC20E: the creature that owns the tongue, through the slot table. */
static uint16_t tongue_owner(const uint8_t *ram, uint16_t actor)
{
    uint16_t slot = read_word(ram, actor + 8);
    return read_word(ram, 0x8500 + (uint16_t)(slot - 2 * ((uint16_t)(slot - 4) >> 1)));
}

int world_gaps_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    /* Surface variant of the secondary weapon: a burst spawner that releases
     * one rising shot every fourth pass. */
    case 0x0baa5:
        SW(actor + 0x42, 1);
        SW(actor, 0xbaaf);
        /* fall through */
    case 0x0baaf: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t shot = world_spawn_child(ram, actor, 0xbb40);
        if (shot) {
            uint16_t launcher = slot_parent(ram, actor);
            world_queue_sound(ram, 0x3b);
            ram[shot + 0x29] = ram[launcher + 0x29];
            SW(shot + 0x21, W(launcher + 0x21) + RW(0xba95 + 2 * (ram[launcher + 0x29] & 1)));
            SW(shot + 0x24, W(launcher + 0x24) + 4);
            ram[shot + 0x28] = (ram[launcher + 0x28] + 4) & 0xf8;
            ram[shot + 0x2d] = ram[actor + 0x2d];
            ram[shot + 0x0b] = ram[actor + 0x0b];
            SW(shot + 0x7a, W(actor + 0x58));
            SW(shot + 0x54, W(actor + 0x54));
            SW(actor + 0x58, W(actor + 0x58) + 1);
            SW(actor + 0x54, W(actor + 0x54) + 1);
        }
        if (!decrement(ram, actor + 0x7a)) finish_animation(ram, actor);
        else SW(actor + 0x42, 4);
        break;
    }
    /* Rising shot of the surface burst. */
    case 0x0bb40:
    case 0x0bb7e:
        if (address == 0x0bb40) {
            world_acquire_resource(ram, actor, 0x5a);
            SW(actor + 0x3c, 0xba7d);
            SW(actor + 0x26, 1);
            SW(actor + 0x36, 0x60);
            SW(actor + 0x2e, 0x600);
            angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
            SW(actor + 0x4e, (uint8_t)(ram[actor + 0x28] - 0x28) & 0xf8);
            SW(actor + 0x58, 1);
            SW(actor + 0x42, 0x0c);
            SW(actor, 0xbb7e);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40) & 6;
            SW(actor + 0x58, RW(0xb9cb + frame));
            SW(actor + 0x2a, RW(0xb993 + frame + W(actor + 0x4e)));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        {
            int burst = !W(actor + 0x26);
            if (!burst) {
                if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
                if (W(actor + 0x42)) decrement(ram, actor + 0x42);
                else if ((int16_t)W(actor + 0x24) >= (int16_t)RW(0xba99 + 2 * (W(actor + 0x7a) & 3))) burst = 1;
                if (!burst && ((1u << (terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24)) & 15)) & 0x5502))
                    burst = 1;
            }
            if (burst) {
                SW(actor + 0x36, 0x0c);
                SW(actor + 0x3c, 0xba8d);
                SW(actor + 0x40, 0);
                SW(actor + 0x58, 1);
                SW(actor + 0x42, 0x29);
                SW(actor, 0xbc14);
            }
        }
        break;
    /* The shot's blast. */
    case 0x0bc14:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40);
            if (RW(0xb9d3 + frame) == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, RW(0xb9d3 + frame));
            SW(actor + 0x58, RW(0xb9ff + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (W(actor + 0x42)) {
            decrement(ram, actor + 0x42);
            SW(actor + 0x26, 0x7f00);
            if (!((W(0x274) & 3) ^ W(actor + 0x54)) && W(actor + 0x3c) != 0xffff)
                world_append_candidate(ram + 0xaee6, actor);
        }
        break;
    /* Primary weapon mode 4: a drilling shot that speeds up twice and sheds
     * a ring (kind 121) every few passes. */
    case 0x0abb7:
    case 0x0ac0e:
    case 0x0ac65: {
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address == 0x0abb7) {
            unsigned level = 4 * W(actor + 0x76);
            world_acquire_resource(ram, actor, RW(0x9bb1 + (ram[actor + 0x0b] & 2)));
            SW(actor + 0x3c, 0xaba3);
            SW(actor + 0x26, RW(0xabab + level));
            SW(actor + 0x6e, RW(0xabab + level));
            SW(actor + 0x36, RW(0xabad + level));
            SW(actor + 0x38, RW(0xab8f + 2 * (ram[actor + 0x29] & 1)));
            SW(actor + 0x58, 1);
            SW(actor + 0x5a, 7);
            SW(actor + 0x5c, 8);
            SW(actor, 0xac0e);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        if (address != 0x0ac65) {
            if (!decrement(ram, actor + 0x58)) {
                uint16_t frame = W(actor + 0x40);
                if (RW(0xa9ab + frame) == 0xffff) {
                    SW(actor + 0x38, W(actor + 0x38) << 1);
                    SW(actor + 0x40, 0);
                    SW(actor + 0x42, 8);
                    SW(actor + 0x58, 0x2b);
                    SW(actor + 0x5a, 3);
                    SW(actor + 0x5c, 4);
                    SW(actor, 0xac65);
                } else {
                    SW(actor + 0x2a, RW(0xa9ab + frame));
                    SW(actor + 0x58, RW(0xa9b7 + frame));
                    SW(actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            SW(actor + 0x40, W(actor + 0x40) + W(actor + 0x58));
            SW(actor + 0x2a, RW(0xa9c1 + 2 * (ram[actor + 0x41] & 3)));
            queue_sprite(ram, main_rom, actor, 1);
            if (W(actor + 0x42) && !decrement(ram, actor + 0x42)) {
                SW(actor + 0x38, W(actor + 0x38) << 1);
                SW(actor + 0x58, 0x80);
                SW(actor + 0x5a, 2);
                SW(actor + 0x5c, 2);
                SW(actor + 0x0e, 0x120);
            }
        }
        if (!decrement(ram, actor + 0x5a)) {
            unsigned level = 2 * (W(actor + 0x76) & 3);
            SW(actor + 0x5a, W(actor + 0x5c));
            uint16_t ring = effect_slot(ram, actor, 0xad5f);
            SW(ring + 0x21, W(actor + 0x21));
            SW(ring + 0x24, W(actor + 0x24));
            ram[ring + 0x0b] = ram[actor + 0x0b];
            ram[ring + 0x2d] = ram[actor + 0x2d] + 1;
            SW(ring + 0x76, RW(0xaa4f + level));
            SW(ring + 0x6c, level << 4);
            SW(ring + 0x6a, W(actor + 0x6a));
            SW(actor + 0x6a, W(actor + 0x6a) + 1);
        }
        int spent = 1;
        if (!W(actor + 0x26)) spawn_impact(ram, main_rom, actor);
        else {
            SW(actor + 0x26, W(actor + 0x6e));
            int16_t x = (int16_t)W(actor + 0x21);
            if (x <= 0x200 && x >= 0) {
                uint16_t cell = terrain_cell(ram, W(actor + 0x21) + 0x10, W(actor + 0x24));
                unsigned material = tiles[0x4000 + cell + 3] >> 3;
                if ((1u << (material & 15)) & 0x5502) {
                    if (material == 1) world_stage5_wall_hit_cell(ram, main_rom, tiles + 0x4000, actor, cell);
                    spawn_impact(ram, main_rom, actor);
                } else {
                    spent = 0;
                    if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                }
            }
        }
        if (spent) {
            uint16_t launcher = slot_parent(ram, actor);
            SW(launcher + 0x46, W(launcher + 0x46) + 1);
            finish_animation(ram, actor);
        }
        break;
    }
    /* The ring left behind by the drilling shot. */
    case 0x0ad5f:
        world_acquire_resource(ram, actor, 0x58);
        SW(actor + 0x3c, 0xffff);
        SW(actor + 0x58, 1);
        SW(actor + 0x0e, 0x121);
        SW(actor + 0x36, 0x10);
        SW(actor, 0xad81);
        break;
    case 0x0ad81:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40);
            SW(actor + 0x58, RW(0xaa2f + frame));
            uint16_t sprite = RW(0xa9c9 + frame + W(actor + 0x76));
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, sprite);
            SW(actor + 0x3c, RW(0xaa57 + frame + W(actor + 0x6c)));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if ((W(actor + 0x6a) & 1) && !(W(0x274) & ((ram[actor + 0x0b] & 2) + 1))) {
            SW(actor + 0x26, 0x7f00);
            if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    /* An actor knocked away by a ring (ADF0) flies for seven passes and bursts. */
    case 0x0ae2a:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            medium_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    /* Tongue grab: the cocoon that holds a caught submarine or actor until
     * it is shaken off, shot open or swallowed. */
    case 0xbc0c0: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 0x2f);
        uint16_t cover = bank_b_effect(ram, actor, 0xc2eb);
        SW(cover + 0x21, W(actor + 0x21));
        SW(cover + 0x24, W(actor + 0x24));
        ram[cover + 0x2c] = ram[actor + 0x2c];
        ram[cover + 0x29] = ram[actor + 0x29];
        SW(cover + 0x60, W(actor + 0x60));
        ram[actor + 0x2d] = 7;
        SW(actor + 0x3c, 0xf603);
        SW(actor + 0x26, 0x200);
        SW(actor + 0x36, 0x400);
        SW(actor + 0x44, 1);
        SW(actor + 0x46, read_word(bank, 0xb7be + W(actor + 0x60)));
        world_queue_sound(ram, 0x7a);
        SW(actor, 0xc122);
        break;
    }
    case 0xbc122:
    case 0xbc305: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40);
            uint16_t sprite = read_word(bank, frame + W(actor + 0x46));
            if (sprite == 0xffff) {
                if (address == 0xbc122) {
                    SW(actor + 0x54, 0);
                    SW(actor + 0x56, 8);
                    SW(actor, 0xc166);
                } else SW(actor, 0xc337);
            } else {
                SW(actor + 0x2a, sprite);
                if (address == 0xbc122 && frame == 4) tongue_target(ram, main_rom, actor, 1);
                SW(actor + 0x44, 3);
                SW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbc166: {
        uint16_t which = W(actor + 0x68);
        int release = which >= 4 && !W(0xb8b8 + which - 4);
        if (!release) {
            scroll_actor(ram, actor, 0xb71a);
            queue_sprite(ram, main_rom, actor, 0);
            release = W(tongue_owner(ram, actor) + 0x66) & 1;
        }
        if (!release) {
            int swallow = 0;
            if (ram[actor + 0x0b]) {
                /* The held player shakes free with eight separate stick pushes. */
                uint8_t stick = ram[actor + 0x0b] == 2 ? ram[0xb742] : ram[0xb743];
                if (stick & 0x0f) {
                    SW(actor + 0x54, 8);
                    if (!decrement(ram, actor + 0x56)) swallow = 1;
                } else if (W(actor + 0x54) && !decrement(ram, actor + 0x54)) SW(actor + 0x56, 8);
            }
            if (!swallow) swallow = tongue_target(ram, main_rom, actor, 0);
            if (!swallow) {
                ram[actor + 0x0d] = 3;
                uint16_t shot = world_projectile_contact(ram, main_rom, actor);
                if (shot && W(shot + 0x3c) != 0x79f1) {
                    if (W(shot + 0x0e) == 0x1b) SW(shot + 0x26, 0);
                    SW(actor + 0x26, W(actor + 0x26) - W(shot + 0x36));
                    if ((int16_t)W(actor + 0x26) <= 0) SW(actor + 0x26, 0);
                }
                if (W(actor + 0x26)) break;
            }
            uint16_t owner = tongue_owner(ram, actor);
            if (!(W(owner + 0x66) & 2)) { SW(actor + 0x26, 0x100); break; }
            SW(owner + 0x66, W(owner + 0x66) | 4);
        }
        SW(actor + 0x26, 0);
        SW(0xb8b2, W(0xb8b2) & ~(1u << (which & 15)));
        SW(actor + 0x40, 0);
        SW(actor + 0x44, 1);
        SW(actor, 0xc26b);
        uint16_t burst = bank_b_effect(ram, actor, 0xc2ab);
        SW(burst + 0x21, W(actor + 0x21));
        SW(burst + 0x24, W(actor + 0x24));
        break;
    }
    case 0xbc26b: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = W(actor + 0x40);
            if (read_word(bank, 0xb81e + frame) == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, read_word(bank, 0xb81e + frame));
            SW(actor + 0x44, read_word(bank, 0xb832 + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xbc2ab:
    case 0xbc2c7:
        if (address == 0xbc2ab) {
            SW(actor + 0x3c, 0xf60b);
            SW(actor + 0x26, 0x200);
            SW(actor + 0x36, 0x400);
            SW(actor + 0x42, 4);
            SW(actor, 0xc2c7);
        } else scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        else if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    case 0xbc2eb:
        ram[actor + 0x2d] = 0x3e;
        SW(actor + 0x44, 1);
        SW(actor + 0x46, read_word(main_rom + 0xb0000, 0xb7ee + W(actor + 0x60)));
        SW(actor, 0xc305);
        break;
    case 0xbc337:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!W(slot_parent(ram, actor) + 0x26)) {
            SW(actor + 0x2a, 0x8752);
            SW(actor + 0x44, 3);
            SW(actor, 0xc366);
        }
        break;
    case 0xbc366:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x44)) finish_animation(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
