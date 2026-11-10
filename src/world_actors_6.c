/* Recovered World actor handlers, part 6 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_6(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
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
        if (velocity == 0xffe0 && !(read_word(ram, 0x264) & 3)) return 0;
        scroll_actor(ram, actor, 0xb71a);
        unsigned fraction = ram[actor + 0x23] + (velocity & 255);
        ram[actor + 0x23] = (uint8_t)fraction;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24)
                   + (int8_t)(velocity >> 8) + (fraction >> 8));
        velocity += 32;
        write_word(ram, actor + 0x3a, velocity);
        if (!velocity) {
            uint16_t target = world_enemy_target(ram, actor);
            if (read_word(ram, target + 0x21) < read_word(ram, actor + 0x21))
                ram[actor + 0x29] = 1;
            write_word(ram, actor, 0x2c46);
        }
        queue_surface_enemy(ram, main_rom, actor);
        register_surface_enemy(ram, actor);
        surface_enemy_hit(ram, main_rom, actor);
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
            uint16_t kind = read_word(ram, actor + 0x7a);
            uint16_t next = read_word(ram, actor + 0x7e);
            uint16_t next_kind = read_word(main_rom + 0xb0000, next);
            if (kind > 2 || (read_word(ram, actor + 0x7c) == 1 && next_kind != 0xffff && next_kind > 2))
                return 0;
            scroll_actor(ram, actor, 0xb71a);
            if (kind == 2) {
                for (unsigned i = 0; i < 2; ++i) {
                    uint16_t speed = read_word(ram, actor + 0x38 + 2*i) + read_word(ram, actor + 0x78 - 2*i);
                    write_word(ram, actor + 0x38 + 2*i, speed);
                    move_actor_axis(ram, actor + 0x20 + 3*i, speed);
                }
            }
            if (!decrement(ram, actor + 0x7c)) {
                if (next_kind == 0xffff) {
                    world_queue_sound(ram, 0x5d);
                    uint16_t effect = effect_slot(ram, actor, 0x6efe);
                    write_word(ram, effect + 0x4c, read_word(main_rom, 0x6b44));
                    write_word(ram, effect + 0x4a, read_word(main_rom, 0x6b49));
                    ram[effect + 0x2c] = 10;
                    ram[effect + 0x2d] = 0x20;
                    write_word(ram, effect + 0x56, 0x30);
                    write_word(ram, effect + 0x58, 0x80);
                    if (!(effect & 0x300)) write_word(ram, effect + 0x44, 0x5d);
                    finish_animation(ram, actor);
                    break;
                }
                begin_linear_script(ram, main_rom, actor, next);
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
        ram[actor + 0x0d] = 0;
        uint16_t attacker = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            attacker = world_projectile_contact(ram, main_rom, actor);
            /* B96F3 tail: a ring knocks it away (ADF0), unscored. */
            if (attacker && world_knocked_away(ram, main_rom, actor, attacker)) break;
        }
        if (!read_word(ram, actor + 0x26))
            world_kind11_death(ram, main_rom, actor, attacker);
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
        enemy_torpedo_contact(ram, main_rom, actor, world_terrain_mask(ram, tiles + 0x4000, x, y));
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
    case 0x15d98:
        world_stage_spawns(ram, main_rom, actor);
        break;
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
        update_scenery(ram, main_rom, tiles, actor, 1);
        break;
    case 0xda13: {
        if (!tiles) return 0;
        unsigned stage = 2 * read_word(ram, 0x84);
        uint16_t row = read_word(main_rom, 0xfe5f + stage);
        write_word(ram, actor + 0x50, read_word(main_rom, row + (ram[actor + 0x1e] & 6)));
        write_word(ram, actor + 0x52, read_word(main_rom, 0xfe87 + (ram[actor + 0x1f] & 30)));
        write_word(ram, actor + 0x54, read_word(main_rom, 0xdcff + stage));
        write_word(ram, actor, 0xda4f);
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
        break;
    case 0x7f81: {
        uint16_t index = 2 * ram[actor + 0x41];
        uint16_t sprite = read_word(main_rom, 0x781f + index);
        scroll_actor(ram, actor, 0xb71a);
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 0x80);
        if (sprite == 0xffff)
            finish_animation(ram, actor);
        else {
            write_word(ram, actor + 0x2a, sprite);
            queue_sprite(ram, main_rom, actor, 0);
        }
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
        if (read_word(ram, actor + 8) >= read_word(ram, read_word(ram, actor + 4) + 8))
            retire_actor(ram, actor);
        break;
    case 0x1b4b:
    case 0x1b62:
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
