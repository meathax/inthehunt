/* Recovered World actor handlers: handlers found by exploratory play of stage indexes 0-2.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

#define RW(o) read_word(ram, (uint16_t)(o))
#define WW(o, v) write_word(ram, (uint16_t)(o), (uint16_t)(v))
#define CSW(o) read_word(main_rom, (uint16_t)(o))
#define BW(o) read_word(bank, (uint16_t)(o))

static uint16_t parent_of(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, (uint16_t)(0x8500 + read_word(ram, actor + 8) - 2));
}

/* 6B2E: sound 5D and the medium explosion. */
static void blast_6b2e(uint8_t *ram, uint16_t actor)
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

void world_sub_hud(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub);
void world_sub_pose(uint8_t *ram, const uint8_t *rom, uint16_t sub);
void world_sub_wake(uint8_t *ram, const uint8_t *rom, uint16_t sub);

/* The ancestor at depth 4 (BC186..BC197). */
static uint16_t root_actor(const uint8_t *ram, uint16_t actor)
{
    uint16_t depth = read_word(ram, actor + 8);
    return read_word(ram, 0x8500 + depth - 2 * ((uint16_t)(depth - 4) >> 1));
}

/* The four things a tongue can hold: both submarines and the actors at [B8B8]/[B8BA].
 * Returns the target's bit number in [B8B2] (0, 2, 4, 6) or -1. */
static int tongue_target(const uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t *target_out)
{
    uint16_t flags = read_word(ram, 0xb8b2), shape = read_word(ram, actor + 0x3c);
    for (unsigned which = 0; which < 8; which += 2) {
        uint16_t target;
        if (flags & (1u << which)) continue;
        if (which < 4) {
            target = read_word(ram, 0x90 + which);
            if (read_word(ram, target + 0x3c) == 0xffff ||
                !world_shape_overlap(ram, rom, actor, target, shape, 0x78db)) continue;
        } else {
            target = read_word(ram, 0xb8b8 + which - 4);
            if (!target || read_word(ram, target) == 0xffff ||
                !world_shape_overlap(ram, rom, actor, target, shape, read_word(ram, target + 0x3c))) continue;
        }
        *target_out = target;
        return (int)which;
    }
    return -1;
}

/* BC616: an overlapping free target is marked held and gets its own grab actor. */
static void tongue_grab(uint8_t *ram, const uint8_t *rom, uint16_t actor)
{
    uint16_t target = 0;
    int which = tongue_target(ram, rom, actor, &target);
    if (which < 0) return;
    write_word(ram, 0xb8b2, read_word(ram, 0xb8b2) | (1u << which));
    uint16_t grab = bank_b_effect(ram, actor, 0xc0c0);
    write_word(ram, grab + 0x21, read_word(ram, target + 0x21));
    write_word(ram, grab + 0x24, read_word(ram, target + 0x24));
    ram[grab + 0x29] = ram[actor + 0x29];
    write_word(ram, grab + 0x60, read_word(ram, actor + 0x60));
    write_word(ram, grab + 0x68, (uint16_t)which);
    write_word(ram, grab + 0x6a, target);
    ram[grab + 0x0b] = ram[target + 0x0b];
}

/* The dispatcher does not reload IX between actors, and 65B1 leaves it alone while no
 * player is active, so a caller that then tests IX sees what an earlier actor left there.
 * This follows the list backwards for the cases whose last IX load is known:
 *   7DC27/7DC68 (spawner that did not spawn this pass: a new child would sit in between)
 *     never touch IX;
 *   0DA4F uses no IX, except on every eighth pass, when its DC25 call counts IX up to 40h;
 *   7E48A and 7E857 end in D2B7, which loads IX with the actor's shape word [3C]
 *     (a 7E48A that has not run yet still has no sprite);
 *   AFA46 ends by queueing its sprite (0DA4): IX is the queue entry it filled, which is
 *     then the newest entry of the queue.
 * Returns 0 when the value cannot be derived. */
static int inherited_ix(const uint8_t *ram, uint16_t actor, uint16_t *ix)
{
    uint16_t previous = actor;
    for (unsigned n = 0; n < 16; ++n) {
        previous = read_word(ram, previous + 6);
        uint32_t handler = (((uint32_t)read_word(ram, previous + 2) << 4) + read_word(ram, previous)) & 0xfffff;
        if (handler == 0x7dc27 || handler == 0x7dc68) continue;
        if (handler == 0xda4f) {
            if (read_word(ram, 0x274) & 7) continue;
            *ix = 0x40;
            return 1;
        }
        if (handler == 0xafa46) {
            uint16_t entry = read_word(ram, 0x9e04) - 16;
            if (read_word(ram, entry + 2) != read_word(ram, previous + 0x2a) ||
                read_word(ram, entry + 4) != read_word(ram, previous + 0x21) ||
                read_word(ram, entry + 6) != read_word(ram, previous + 0x24)) return 0;
            *ix = entry;
            return 1;
        }
        if (handler == 0x7e857 || (handler == 0x7e48a && read_word(ram, previous + 0x2a))) {
            *ix = read_word(ram, previous + 0x3c);
            return 1;
        }
        return 0;
    }
    return 0;
}

int world_explore1_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    const uint8_t *bank = main_rom + 0xb0000;
    (void)pass;
    switch (address) {
    /* ---- Leaping surface enemy at the turn of its leap while no player is active:
     * the facing test reads the IX an earlier actor left (see inherited_ix). ---- */
    case 0xb2b03:
    case 0xb2c07: {
        uint16_t ix = 0;
        if (RW(0x264) & 3) return WORLD_HANDLER_UNKNOWN;
        if (address == 0xb2b03 ? RW(actor + 0x3a) != 0xffe0 : RW(actor + 0x42) != 1) return WORLD_HANDLER_UNKNOWN;
        if (!inherited_ix(ram, actor, &ix)) return WORLD_HANDLER_STOP;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, RW(actor + 0x3a));
        if (address == 0xb2b03) WW(actor + 0x3a, 0);
        else WW(actor + 0x42, 0);
        if (ix & 2) ram[actor + 0x29] = 1;
        WW(actor, 0x2c46);
        queue_surface_enemy(ram, main_rom, actor);
        register_surface_enemy(ram, actor);
        surface_enemy_hit(ram, main_rom, actor);
        break;
    }
    /* ---- A submarine held by a tongue (its bit in [B8B2] set): no steering, no movement;
     * everything else of the open-water routine still runs. 7BE5 joins at 7C88 while the
     * submarine is still hidden after a respawn. ---- */
    case 0x7be5:
    case 0x7c3b: {
        if (!(RW(0xb8b2) & (1u << (ram[actor + 0x0b] & 2)))) return WORLD_HANDLER_UNKNOWN;
        if (address == 0x7be5 && RW(actor + 0x6e)) return WORLD_HANDLER_UNKNOWN;
        if (!tiles || RW(0x2a4)) return WORLD_HANDLER_STOP;
        if (address == 0x7be5) {
            if ((int16_t)RW(actor + 0x42) > 0x60) {
                ram[0xb73e] = (uint8_t)(1u << (ram[0x26e] & 3));
                ram[0xb742] = 0;
            }
            if (ram[actor + 0x0a]) {
                memset(ram + actor + 0x58, 0, 8);
            } else {
                /* 7C1B: the submarine appears and continues as 7C3B, with [26] just set. */
                WW(actor + 0x3c, CSW(0x7c1e));
                WW(actor + 0x26, 1);
                WW(actor + 0x36, 0x100);
                WW(actor, 0x7c3b);
            }
        } else if (!(RW(0x262) & 0x8080) && !ram[actor + 0x0a] && !RW(actor + 0x26)) {
            WW(actor + 0x26, 1);
        }
        world_sub_hud(ram, main_rom, tiles, actor);
        WW(0xb73e, RW(0xb73e) & 0xfff0);
        WW(actor + 0x6c, 0);
        if (RW(actor + 0x42) && !decrement(ram, actor + 0x42)) {
            WW(actor + 0x26, RW(actor + 0x6e) ? 0 : 1);
            WW(actor + 0x36, 0x100);
        }
        world_sub_pose(ram, main_rom, actor);
        world_player_weapons(ram, main_rom, actor);
        world_sub_wake(ram, main_rom, actor);
        if (RW(0x262) & 0x40) {
            if ((RW(0x264) & 3) == 3) return WORLD_HANDLER_STOP;
            if (RW(0x272)) WW(0x262, RW(0x262) | 0xc000);
            else {
                WW(actor + 0x42, 0x80);
                WW(actor, 0x7da7);
            }
        }
        break;
    }
    /* ---- The grab (BC0C0..BC388): the tongue's catch, shaken off with the stick or shot free. ---- */
    case 0xbc0c0: {
        world_acquire_resource(ram, actor, 0x2f);
        uint16_t cover = bank_b_effect(ram, actor, 0xc2eb);
        WW(cover + 0x21, RW(actor + 0x21));
        WW(cover + 0x24, RW(actor + 0x24));
        ram[cover + 0x2c] = ram[actor + 0x2c];
        ram[cover + 0x29] = ram[actor + 0x29];
        WW(cover + 0x60, RW(actor + 0x60));
        ram[actor + 0x2d] = 7;
        WW(actor + 0x3c, 0xf603);
        WW(actor + 0x26, 0x200);
        WW(actor + 0x36, 0x400);
        WW(actor + 0x44, 1);
        WW(actor + 0x46, BW(RW(actor + 0x60) - 0x4842));
        world_queue_sound(ram, 0x7a);
        WW(actor, 0xc122);
        break;
    }
    case 0xbc122:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = RW(actor + 0x40);
            uint16_t sprite = BW(frame + RW(actor + 0x46));
            if (sprite == 0xffff) {
                WW(actor + 0x54, 0);
                WW(actor + 0x56, 8);
                WW(actor, 0xc166);
            } else {
                WW(actor + 0x2a, sprite);
                if (frame == 4) tongue_grab(ram, main_rom, actor);
                WW(actor + 0x44, 3);
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xbc166: {
        uint16_t which = RW(actor + 0x68);
        uint16_t root = root_actor(ram, actor);
        int release = which >= 4 && !RW(0xb8b8 + which - 4);
        if (!release) {
            scroll_actor(ram, actor, 0xb71a);
            queue_sprite(ram, main_rom, actor, 0);
            release = RW(root + 0x66) & 1;
        }
        if (!release) {
            int shaken = 0;
            if (ram[actor + 0x0b]) {
                unsigned pressed = (ram[actor + 0x0b] == 2 ? ram[0xb742] : ram[0xb743]) & 15;
                if (pressed) {
                    WW(actor + 0x54, 8);
                    shaken = !decrement(ram, actor + 0x56);
                } else if (RW(actor + 0x54) && !decrement(ram, actor + 0x54)) {
                    WW(actor + 0x56, 8);
                }
            }
            if (!shaken) {
                uint16_t other = 0;
                shaken = tongue_target(ram, main_rom, actor, &other) >= 0;
            }
            if (!shaken) {
                ram[actor + 0x0d] = 3;
                uint16_t shot = world_projectile_contact(ram, main_rom, actor);
                if (shot && RW(shot + 0x3c) != 0x79f1) {
                    if (RW(shot + 0x0e) == 0x1b) WW(shot + 0x26, 0);
                    int16_t left = (int16_t)(RW(actor + 0x26) - RW(shot + 0x36));
                    WW(actor + 0x26, left > 0 ? left : 0);
                }
                if (RW(actor + 0x26)) break;
            }
            if (!(RW(root + 0x66) & 2)) {
                WW(actor + 0x26, 0x100);
                break;
            }
            WW(root + 0x66, RW(root + 0x66) | 4);
        }
        WW(actor + 0x26, 0);
        WW(0xb8b2, RW(0xb8b2) & ~(1u << (which & 15)));
        WW(actor + 0x40, 0);
        WW(actor + 0x44, 1);
        WW(actor, 0xc26b);
        uint16_t burst = bank_b_effect(ram, actor, 0xc2ab);
        WW(burst + 0x21, RW(actor + 0x21));
        WW(burst + 0x24, RW(actor + 0x24));
        break;
    }
    case 0xbc26b:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = RW(actor + 0x40);
            uint16_t sprite = BW(frame - 0x47e2);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x44, BW(frame - 0x47ce));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xbc2ab:
        WW(actor + 0x3c, 0xf60b);
        WW(actor + 0x26, 0x200);
        WW(actor + 0x36, 0x400);
        WW(actor + 0x42, 4);
        WW(actor, 0xc2c7);
        /* fall through */
    case 0xbc2c7:
        if (address == 0xbc2c7) scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        else if (RW(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    case 0xbc2eb:
        ram[actor + 0x2d] = 0x3e;
        WW(actor + 0x44, 1);
        WW(actor + 0x46, BW(RW(actor + 0x60) - 0x4812));
        WW(actor, 0xc305);
        break;
    case 0xbc305:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = RW(actor + 0x40);
            uint16_t sprite = BW(frame + RW(actor + 0x46));
            if (sprite == 0xffff) {
                WW(actor, 0xc337);
            } else {
                WW(actor + 0x2a, sprite);
                WW(actor + 0x44, 3);
                WW(actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xbc337:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!RW(parent_of(ram, actor) + 0x26)) {
            WW(actor + 0x2a, 0x8752);
            WW(actor + 0x44, 3);
            WW(actor, 0xc366);
        }
        break;
    case 0xbc366:
        scroll_actor(ram, actor, 0xb71a);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x44)) finish_animation(ram, actor);
        break;
    /* ---- Primary weapon 3: the wave shot and its wake (0ABB7..0AE5B). ---- */
    case 0xabb7:
        world_acquire_resource(ram, actor, CSW(0x9bb1 + (ram[actor + 0x0b] & 2)));
        WW(actor + 0x3c, 0xaba3);
        {
            uint16_t level = RW(actor + 0x76) << 2;
            WW(actor + 0x26, CSW(0xabab + level));
            WW(actor + 0x6e, CSW(0xabab + level));
            WW(actor + 0x36, CSW(0xabad + level));
        }
        WW(actor + 0x38, CSW(0xab8f + 2 * (ram[actor + 0x29] & 1)));
        WW(actor + 0x58, 1);
        WW(actor + 0x5a, 7);
        WW(actor + 0x5c, 8);
        WW(actor, 0xac0e);
        /* fall through */
    case 0xac0e:
    case 0xac65:
        if (!tiles) return WORLD_HANDLER_STOP;
        if (address != 0xabb7) scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(actor + 0x38));
        if (address != 0xac65) {
            if (!decrement(ram, actor + 0x58)) {
                uint16_t frame = RW(actor + 0x40);
                uint16_t sprite = CSW(0xa9ab + frame);
                if (sprite == 0xffff) {
                    WW(actor + 0x38, RW(actor + 0x38) << 1);
                    WW(actor + 0x40, 0);
                    WW(actor + 0x42, 8);
                    WW(actor + 0x58, 0x2b);
                    WW(actor + 0x5a, 3);
                    WW(actor + 0x5c, 4);
                    WW(actor, 0xac65);
                } else {
                    WW(actor + 0x2a, sprite);
                    WW(actor + 0x58, CSW(0xa9b7 + frame));
                    WW(actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            WW(actor + 0x40, RW(actor + 0x40) + RW(actor + 0x58));
            WW(actor + 0x2a, CSW(0xa9c1 + 2 * (ram[actor + 0x41] & 3)));
            queue_sprite(ram, main_rom, actor, 1);
            if (RW(actor + 0x42) && !decrement(ram, actor + 0x42)) {
                WW(actor + 0x38, RW(actor + 0x38) << 1);
                WW(actor + 0x58, 0x80);
                WW(actor + 0x5a, 2);
                WW(actor + 0x5c, 2);
                WW(actor + 0x0e, 0x120);
            }
        }
        if (!decrement(ram, actor + 0x5a)) {
            WW(actor + 0x5a, RW(actor + 0x5c));
            uint16_t wake = effect_slot(ram, actor, 0xad5f);
            WW(wake + 0x21, RW(actor + 0x21));
            WW(wake + 0x24, RW(actor + 0x24));
            ram[wake + 0x0b] = ram[actor + 0x0b];
            ram[wake + 0x2d] = ram[actor + 0x2d] + 1;
            uint16_t level = (RW(actor + 0x76) & 3) << 1;
            WW(wake + 0x76, CSW(0xaa4f + level));
            WW(wake + 0x6c, level << 4);
            WW(wake + 0x6a, RW(actor + 0x6a));
            WW(actor + 0x6a, RW(actor + 0x6a) + 1);
        }
        if (RW(actor + 0x26)) {
            WW(actor + 0x26, RW(actor + 0x6e));
            int16_t x = (int16_t)RW(actor + 0x21);
            if (x <= 0x200 && x >= 0) {
                if (!(world_terrain_mask(ram, tiles + 0x4000, x + 0x10, RW(actor + 0x24)) & 0x5502)) {
                    if (RW(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
                    break;
                }
                /* 0A3B7: the breakable walls of stage index 4 are not recovered for this weapon. */
                if (RW(0x84) == 4) return WORLD_HANDLER_STOP;
                spawn_impact(ram, main_rom, actor);
            }
        } else {
            spawn_impact(ram, main_rom, actor);
        }
        {
            uint16_t parent = parent_of(ram, actor);
            WW(parent + 0x46, RW(parent + 0x46) + 1);
        }
        finish_animation(ram, actor);
        break;
    case 0xad5f:
        world_acquire_resource(ram, actor, 0x58);
        WW(actor + 0x3c, 0xffff);
        WW(actor + 0x58, 1);
        WW(actor + 0x0e, 0x121);
        WW(actor + 0x36, 0x10);
        WW(actor, 0xad81);
        break;
    case 0xad81:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = RW(actor + 0x40);
            WW(actor + 0x58, CSW(0xaa2f + frame));
            uint16_t sprite = CSW(0xa9c9 + frame + RW(actor + 0x76));
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            WW(actor + 0x2a, sprite);
            WW(actor + 0x3c, CSW(0xaa57 + frame + RW(actor + 0x6c)));
            WW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (!(RW(actor + 0x6a) & 1)) break;
        if (RW(0x274) & ((ram[actor + 0x0b] & 2) + 1)) break;
        WW(actor + 0x26, 0x7f00);
        if (RW(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    /* 0ADF0 puts an enemy struck by the wake (kind 121) here: it is swept away and bursts. */
    case 0xae2a:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, RW(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, RW(actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            blast_6b2e(ram, actor);
            finish_animation(ram, actor);
        }
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
