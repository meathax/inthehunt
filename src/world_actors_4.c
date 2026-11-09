/* Recovered World actor handlers, part 4 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_4(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    case 0xb5f0d:
        warship_attachment(ram, actor);
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            for (unsigned i = 0; i < 2; ++i) {
                uint16_t child = effect_slot(ram, actor, 0x5f64);
                if (child != 0x8480) write_word(ram, child + 2, 0xb000);
                ram[child + 0x2c] = ram[actor + 0x2c];
                write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
                ram[child + 0x28] = i ? 0x20 : 0x60;
                write_word(ram, child + 0x60, i*4);
            }
            ram[actor + 0xa] = 0;
            write_word(ram, actor, 0x6084);
        }
        break;
    case 0xb5f64: {
        const uint8_t *bank = main_rom + 0xb0000;
        write_word(ram, actor + 0xe, 0x2a);
        ram[actor + 0xc] = 4;
        ram[actor + 0x2d] = 0x29;
        write_word(ram, actor + 0x3c, read_word(bank, 0x5f74));
        write_word(ram, actor + 0x26, 0x600);
        write_word(ram, actor + 0x36, 0x300);
        write_word(ram, actor + 0x42, 1);
        unsigned side = read_word(ram, actor + 0x60);
        write_word(ram, actor + 0x31, read_word(bank, 0x5050 + side));
        write_word(ram, actor + 0x34, read_word(bank, 0x5052 + side));
        uint16_t config = 0x55d2 + read_word(bank, 0x5512 + (ram[actor + 0x1e] & 32) + read_word(ram, 0x8c));
        uint16_t delay = read_word(bank, config + 4);
        write_word(ram, actor + 0x52, delay);
        write_word(ram, actor + 0x50, delay == 0xffff ? delay : 200);
        write_word(ram, actor + 0x2e, read_word(bank, config + 6));
        write_word(ram, actor, 0x5fca);
    }
        /* fall through */
    case 0xb5fca: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) {
            warship_part_explosion(ram, main_rom, actor);
            break;
        }
        aim_warship_turret(ram, main_rom, actor, 0);
        uint16_t barrel = read_word(ram, actor + 0x40);
        write_word(ram, actor + 0x2a, read_word(bank, 0x503e + barrel));
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        uint16_t x = read_word(ram, actor + 0x21);
        if (x >= 0x40 && x <= 0x1c0 && !decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            write_word(ram, actor + 0x42, 96);
            uint16_t child = world_spawn_child(ram, actor, 0x6150);
            if (child) {
                write_word(ram, child + 2, 0xb000);
                write_word(ram, child + 0x46, barrel);
                ram[child + 0x29] = bank[0x56e0 + (barrel >> 1)];
                write_word(ram, child + 0x2e, read_word(ram, actor + 0x2e));
                write_word(ram, child + 0x60, 0x12);
            }
        }
        break;
    }
    case 0xb6084:
        ram[actor + 0x2c] = ram[actor + 0x16];
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0x6099);
        /* fall through */
    case 0xb6099: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) {
            unsigned slot = ram[actor + 0x14];
            uint16_t cursor = read_word(ram, 0xa488);
            ram[0xa406 + cursor] = slot;
            write_word(ram, 0xa488, (cursor + 1) & 63);
            write_word(ram, 0xa48a, read_word(ram, 0xa48a) + 1);
            write_word(ram, 0x9e06 + 8*slot, 0);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            uint16_t step = read_word(ram, actor + 0x40);
            queue_palette(ram, ram[actor + 0x2c], read_word(bank, 0x56dc + (step & 2)), 6);
            write_word(ram, actor + 0x42, 80);
            write_word(ram, actor + 0x40, step + 2);
        }
        ram[actor + 0xd] = 1;
        world_player_contact(ram, main_rom, actor);
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x3c, read_word(bank, 0x60ed));
        ram[actor + 0xd] = 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        uint16_t hp = read_word(ram, actor + 0x26);
        if (!hp) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, parent + 0x26, 0);
            write_word(ram, parent + 0x74, hit);
        }
        write_word(ram, actor + 0x3c, read_word(bank, 0x6135));
        write_word(ram, actor + 0x48, hp);
        if (world_projectile_contact(ram, main_rom, actor)) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        write_word(ram, actor + 0x26, hp);
        break;
    }
    case 0xb59ea:
        world_acquire_resource(ram, actor, 0x5b);
        world_acquire_resource(ram, actor, 0x62);
        write_word(ram, actor + 0xe, 0x2a);
        write_word(ram, actor, 0x5a04);
        /* fall through */
    case 0xb5a04: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        uint16_t slot = read_word(ram, actor + 0x60);
        ram[actor + 0x29] = ram[actor + 0x5e];
        write_word(ram, actor + 0x2a, read_word(bank, 0x4db6 + slot));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        uint8_t angle = ram[actor + 0x28];
        if (!(ram[0x274] & 7) && (ram[0x264] & 3)) {
            uint16_t target = world_enemy_target(ram, actor);
            uint8_t desired = (vector_angle(main_rom, read_word(ram, target + 0x21) - x,
                                             read_word(ram, target + 0x24) - y) + 4) & 0xf8;
            if (desired != angle) {
                if ((uint8_t)(desired - angle) < (uint8_t)(angle - desired)) {
                    if ((int8_t)angle < -32) angle += 8;
                } else if ((int8_t)angle > -96) angle -= 8;
            }
            ram[actor + 0x28] = angle;
        }
        uint16_t dir = ((uint16_t)((angle & 0xf8) - 0xa0)) >> 3;
        ram[actor + 0x29] = bank[0x56e0 + dir];
        uint16_t barrel = read_word(bank, 0x4dd0 + 2 * dir);
        write_word(ram, actor + 0x62, barrel);
        write_word(ram, actor + 0x2a, read_word(bank, 0x4dc6 + barrel));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (x >= 0x40 && x <= 0x1c0 && !read_word(ram, parent + 0x50) && read_word(ram, parent + 0x64) == slot) {
            uint16_t flash = world_spawn_child(ram, actor, 0xa2c0);
            if (flash) {
                write_word(ram, flash + 0x31, read_word(ram, actor + 0x38));
                write_word(ram, flash + 0x34, read_word(ram, actor + 0x3a));
                ram[flash + 0x29] = ram[actor + 0x29];
                ram[flash + 0x2d] = 4;
                write_word(ram, flash + 0x60, 3);
                uint16_t shot = world_spawn_child(ram, actor, 0x6a4d);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    ram[shot + 0x2c] = ram[actor + 0x2c];
                    ram[shot + 0x14] = ram[actor + 0x14];
                    ram[shot + 0x29] = ram[actor + 0x29];
                    write_word(ram, shot + 0x62, barrel);
                    uint16_t dx = read_word(bank, 0x4df2 + 2*barrel), dy = read_word(bank, 0x4df4 + 2*barrel);
                    if (ram[actor + 0x29] & 1) dx = -dx;
                    write_word(ram, actor + 0x38, dx);
                    write_word(ram, actor + 0x3a, dy);
                    write_word(ram, shot + 0x21, x + dx);
                    write_word(ram, shot + 0x24, y + dy);
                    write_word(ram, shot + 0x58, read_word(ram, actor + 0x58));
                    world_queue_sound(ram, 0x5c);
                }
            }
        }
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            write_word(ram, actor + 0x2a, read_word(bank, 0x4db8 + slot));
            ram[actor + 0x29] = ram[actor + 0x5e];
            write_word(ram, actor + 0x3c, 0xffff);
            ram[actor + 0xa] = 1;
            write_word(ram, actor, 0x5cc6);
        }
        register_surface_enemy(ram, actor);
        break;
    }
    case 0xb571c: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t variant = read_word(ram, actor + 0x1e);
        uint16_t script = read_word(bank, 0x5662 + (variant & 6));
        if (read_word(bank, script) > 2) return 0;
        world_acquire_resource(ram, actor, 0x63);
        world_acquire_resource(ram, actor, 0x62);
        write_word(ram, actor + 0xe, 12);
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x3c, read_word(bank, 0x5738));
        write_word(ram, actor + 0x2a, read_word(bank, 0x573d));
        uint16_t base = 0x5512 + (variant & 0x20);
        uint16_t cfg = 0x5552 + read_word(bank, base + read_word(ram, 0x8a));
        write_word(ram, actor + 0x26, read_word(bank, cfg));
        write_word(ram, actor + 0x36, read_word(bank, cfg));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x70, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x4c, 0);
        write_word(ram, actor + 0x58, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x54, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x56, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x4e, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x72, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x5a, read_word(bank, cfg + 12));
        write_word(ram, actor + 0x4a, read_word(bank, 0x5560 + read_word(bank, base + read_word(ram, 0x88))));
        write_word(ram, actor + 0x48, read_word(bank, 0x55e0 + read_word(bank, base + read_word(ram, 0x8c))));
        ram[actor + 0x29] = variant & 1;
        write_word(ram, actor + 0x38, read_word(bank, 0x5652 + 2*(variant & 1)));
        write_word(ram, actor + 0x6c, read_word(bank, 0x56d4 + (variant & 6)));
        begin_linear_script(ram, main_rom, actor, script);
        if (!spawn_warship_parts(ram, main_rom, actor)) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor, 0x5824);
        break;
    }
    case 0xb5824: {
        const uint8_t *bank = main_rom + 0xb0000;
        int moving = !read_word(ram, actor + 0x6c);
        uint16_t kind = read_word(ram, actor + 0x7a);
        uint16_t next = read_word(ram, actor + 0x7e);
        uint16_t next_kind = read_word(bank, next);
        if (moving && (kind > 2 || (read_word(ram, actor + 0x7c) == 1 && next_kind > 2 && next_kind != 0xffff))) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (moving) {
            if (kind == 2) {
                for (unsigned i = 0; i < 2; ++i) {
                    uint16_t speed = read_word(ram, actor + 0x38 + 2*i) + read_word(ram, actor + 0x78 - 2*i);
                    write_word(ram, actor + 0x38 + 2*i, speed);
                    move_actor_axis(ram, actor + 0x20 + 3*i, speed);
                }
            }
            if (!decrement(ram, actor + 0x7c)) {
                if (next_kind == 0xffff) {
                    next = 0x56ca;
                    write_word(ram, actor + 0x42, 0xffff);
                }
                begin_linear_script(ram, main_rom, actor, next);
            }
        }
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        if ((int16_t)read_word(ram, actor + 0x21) <= -48) { finish_animation(ram, actor); break; }
        if (ram[actor + 0x19]) --ram[actor + 0x19];
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + (ram[actor + 0x19] & 2));
        queue_sprite(ram, main_rom, actor, 0);
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, read_word(ram, actor + 0x74));
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 20);
            world_queue_sound(ram, 0x5f);
            warship_blast_sequence(ram, actor, 0x54b6, 8);
            write_word(ram, actor, 0x595d);
            break;
        }
        for (unsigned group = 0; group < 2; ++group) {
            uint16_t timer = group ? 0x50 : 0x54;
            uint16_t active = group ? 0x4c : 0x4e;
            unsigned mask = group ? 12 : 24;
            unsigned chosen = group ? 0x64 : 0x66;
            if (!read_word(ram, actor + timer)) write_word(ram, actor + timer, read_word(ram, actor + timer + 2));
            if (!read_word(ram, actor + active)) continue;
            if (!decrement(ram, actor + active))
                write_word(ram, actor + (group ? 0x4e : 0x4c), read_word(ram, actor + (group ? 0x72 : 0x70)));
            if (!decrement(ram, actor + timer)) {
                unsigned pick = world_random_byte(ram) & mask;
                if (pick == read_word(ram, actor + chosen)) pick = (pick + 8) & mask;
                write_word(ram, actor + chosen, pick);
            }
        }
        write_word(ram, actor + 0x26, 0x7f00);
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x42) && read_word(ram, actor + 0x21) <
            read_word(bank, 0x565a + (read_word(ram, actor + 0x1e) & 6))) {
            write_word(ram, actor + 0x6c, 1);
            write_word(ram, actor + 0x42, 0);
        }
        break;
    }
    case 0xb595d:
        ram[actor + 0xa] = 2;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x3a, -32);
        write_word(ram, actor, 0x5975);
        /* fall through */
    case 0xb5975:
    case 0xb59b3:
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (address == 0xb59b3) {
            if ((int16_t)read_word(ram, actor + 0x21) <= -32 || !decrement(ram, actor + 0x42))
                finish_animation(ram, actor);
            else if (ram[actor + 0x42] & 1) queue_sprite(ram, main_rom, actor, 0);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            const uint8_t *bank = main_rom + 0xb0000;
            uint16_t sprite = read_word(bank, 0x4d90 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x42, 64);
                write_word(ram, actor, 0x59b3);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, 0x4da2 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xbf986: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 8);
        write_word(ram, actor + 0x3c, read_word(bank, 0xf991));
        write_word(ram, actor + 0x36, 128);
        ram[actor + 0x2d] = 0x28;
        ram[actor + 0xe] = 0x27;
        uint16_t variant = read_word(ram, actor + 0x1e);
        uint16_t cfg = 0xf876 + read_word(bank, 0xf836 + (variant & 32) + read_word(ram, 0x8a));
        write_word(ram, actor + 0x26, read_word(bank, cfg));
        write_word(ram, actor + 0x46, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x48, read_word(bank, cfg + 2));
        write_word(ram, actor + 0x4e, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 4));
        write_word(ram, actor + 0x4a, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x4c, read_word(bank, cfg + 6));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x5c, 160);
        uint16_t speed = read_word(bank, 0xf8c6 + (variant & 2));
        write_word(ram, actor + 0x38, speed);
        ram[actor + 0x5e] = speed >> 15;
        write_word(ram, actor + 0x56, 0x1e0);
        write_word(ram, actor + 0x44, read_word(bank, 0xf8ca));
        write_word(ram, actor + 0x2a, read_word(bank, 0xf8cc));
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x60, 44);
        write_word(ram, actor, 0xfa25);
        break;
    }
    case 0xbfa25: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t orientation = read_word(ram, actor + 0x29);
        ram[actor + 0x29] = ram[actor + 0x5e];
        queue_sprite(ram, main_rom, actor, 1);
        write_word(ram, actor + 0x29, orientation);
        advance_timed_sprite(ram, bank, actor, 0xf8ca);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (!read_word(ram, actor + 0x54)) {
            if (!decrement(ram, actor + 0x48)) {
                write_word(ram, actor + 0x48, read_word(ram, actor + 0x46));
                write_word(ram, actor + 0x54, 0xffff);
            }
        } else if (!decrement(ram, actor + 0x4c)) {
            write_word(ram, actor + 0x4c, read_word(ram, actor + 0x4a));
            if (y < 0x180) {
                uint16_t shot = world_spawn_child(ram, actor, 0xfbf9);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x21, x);
                    write_word(ram, shot + 0x24, y - 8);
                    write_word(ram, shot + 0x3a, read_word(ram, actor + 0x52));
                    world_queue_sound(ram, 0x7d);
                }
            }
            if (!decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x4e));
                write_word(ram, actor + 0x54, 0);
            }
        }
        if (!decrement(ram, actor + 0x5c)) {
            write_word(ram, actor + 0x5c, 160);
            ram[actor + 0x29] ^= 1;
            if (y < 0x180) {
                uint16_t child = effect_slot(ram, actor, 0x30c6);
                if (child != 0x8480) write_word(ram, child + 2, 0xb000);
                write_word(ram, child + 0x58, 4);
            }
        }
        int16_t vx = read_word(ram, actor + 0x38);
        if (vx >= 0 && (int16_t)decrement(ram, actor + 0x56) < 0) write_word(ram, actor + 0x3a, 128);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        uint16_t bottom = ram[0x26e] & 2 ? 0x198 : read_word(ram, 0xb732) + 0x1c8;
        if ((int16_t)read_word(ram, actor + 0x26) <= 0) {
            ram[actor + 0xb] = ram[hit + 0xb];
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, actor, 0xfac6);
        } else if (x < 0x28 || x > 0x1d8 || y > bottom) finish_animation(ram, actor);
        else {
            if (vx >= 0 && x >= 0x80) write_word(ram, actor + 0x38, 256);
            register_surface_enemy(ram, actor);
        }
        break;
    }
    case 0xbfbf9:
    case 0xbfd45: {
        const uint8_t *bank = main_rom + 0xb0000;
        int drop = address == 0xbfbf9;
        world_acquire_resource(ram, actor, 9);
        ram[actor + 0x2d] = drop ? 0x29 : 4;
        ram[actor + 0xe] = 0x27;
        uint16_t table = drop ? 0xf918 : 0xf960;
        write_word(ram, actor + 0x44, read_word(bank, table));
        write_word(ram, actor + 0x2a, read_word(bank, table + 2));
        write_word(ram, actor + 0x40, 0);
        if (drop) {
            write_word(ram, actor + 0x3c, read_word(bank, 0xfc04));
            write_word(ram, actor + 0x36, 0);
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x42, 8);
        }
        write_word(ram, actor, drop ? 0xfc37 : 0xfd6f);
        break;
    }
    case 0xbfc37:
    case 0xbfcda:
    case 0xbfcf4:
    case 0xbfd6f: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!tiles && address != 0xbfd6f) return 0;
        if (address == 0xbfcda) {
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            if (tiles[0x4000 + cell + 3] >> 3 == 4) {
                write_word(ram, actor, 0xfcf4);
                spawn_bomb_splash(ram, main_rom, actor);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (address == 0xbfd6f) {
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            if (!advance_timed_sprite(ram, bank, actor, 0xf960) || (ram[parent + 0xa] & 1)) finish_animation(ram, actor);
            break;
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (address != 0xbfc37) {
            if ((int16_t)decrement(ram, actor + 0x42) < 0) {
                queue_sprite(ram, main_rom, actor, 0);
                advance_timed_sprite(ram, bank, actor, 0xf952);
            }
            if (x < 0x40) { finish_animation(ram, actor); break; }
            ram[actor + 0xd] = 0;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        advance_timed_sprite(ram, bank, actor, 0xf918);
        ram[actor + 0xd] = 3;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (hit || material == 4) {
            write_word(ram, actor, hit ? 0xfcda : 0xfcf4);
            if (!hit) spawn_bomb_splash(ram, main_rom, actor);
            write_word(ram, actor + 0x44, read_word(bank, 0xf952));
            write_word(ram, actor + 0x2a, read_word(bank, 0xf954));
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x3a, -384);
            uint16_t child = effect_slot(ram, actor, 0xfd45);
            if (child != 0x8480) write_word(ram, child + 2, 0xb000);
            write_word(ram, child + 0x21, x);
            write_word(ram, child + 0x24, y);
            write_word(ram, child + 0x3a, -384);
            write_word(ram, actor + 0x42, 4);
            write_word(ram, actor + 0x36, 128);
        } else if (material == 8) finish_animation(ram, actor);
        else {
            if (!decrement(ram, actor + 0x42)) ram[actor + 0x2d] = 4;
            ram[actor + 0xd] = 0;
            if (world_player_contact(ram, main_rom, actor)) finish_animation(ram, actor);
        }
        break;
    }
    case 0xb73c0: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, 0xa4);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0xe, 13);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0x73de));
        write_word(ram, actor + 0x2a, read_word(bank, 0x73e3));
        unsigned facing = (int16_t)read_word(ram, actor + 0x21) <= 0x100;
        write_word(ram, actor + 0x38, facing ? 128 : -128);
        ram[actor + 0x29] = facing;
        uint16_t kind = (read_word(ram, actor + 0x1e) >> 8) & 14;
        write_word(ram, actor + 0x50, kind);
        uint16_t child = effect_slot(ram, actor, 0x74a0);
        if (child != 0x8480) write_word(ram, child + 2, 0xb000);
        ram[child + 0x29] = facing;
        child = effect_slot(ram, actor, 0x7511);
        if (child != 0x8480) write_word(ram, child + 2, 0xb000);
        ram[child + 0x29] = facing;
        ram[child + 0x2c] = ram[actor + 0x2c];
        write_word(ram, child + 0x50, kind);
        write_word(ram, actor, 0x743b);
        break;
    }
    case 0xb743b: {
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        else {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
            ram[actor + 0xa] |= 2;
        }
        break;
    }
    case 0xb74a0:
        world_acquire_resource(ram, actor, 4);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x74b3);
        break;
    case 0xb7511:
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 64);
        write_word(ram, actor + 0x36, 0);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x7522));
        write_word(ram, actor + 0x34, -12);
        write_word(ram, actor + 0x31, ram[actor + 0x29] & 1 ? -18 : 18);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x7542);
        /* fall through */
    case 0xb7542:
    case 0xb74b3: {
        int offset = address != 0xb74b3;
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (!offset && ram[parent + 0xa]) { finish_animation(ram, actor); break; }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + (offset ? read_word(ram, actor + 0x31) : 0));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + (offset ? read_word(ram, actor + 0x34) : 0));
        if (offset && (ram[parent + 0xa] & 2)) write_word(ram, actor, 0x75b6);
        else if (offset && (ram[parent + 0xa] & 1)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, (offset ? 0x7284 : 0x73b6) + frame));
            write_word(ram, actor + 0x44, 4);
            write_word(ram, actor + 0x40, frame + 2 < 6 ? frame + 2 : 0);
        }
        queue_sprite(ram, main_rom, actor, !offset);
        break;
    }
    case 0xb76a6: {
        const uint8_t *bank = main_rom + 0xb0000;
        ram[actor + 0x2c] = 8;
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(bank, 0x76b1));
        ram[actor + 0x29] = 0;
        write_word(ram, actor + 0x3a, 0x40);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        uint16_t kind = read_word(ram, actor + 0x50);
        if (!kind) {
            kind = read_word(bank, 0x73bc + (world_random_byte(ram) & 2));
            write_word(ram, actor + 0x50, kind);
        }
        uint16_t sequence = read_word(bank, 0x7296 + kind);
        write_word(ram, actor + 0x52, sequence);
        write_word(ram, actor + 0x4c, read_word(bank, 0x72ce + read_word(bank, sequence)));
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x56, 4);
        write_word(ram, actor, 0x7702);
    }
        /* fall through */
    case 0xb7702:
    case 0xb7846:
    case 0xb786a: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t state = read_word(ram, actor);
        if (state == 0x7702 && !tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (state == 0x7702) {
            if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                               0x177 - y - 8 + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0xff02) write_word(ram, actor, 0x786a);
            else if (material == 3) {
                write_word(ram, actor + 0x34, y);
                write_word(ram, actor, 0x7846);
            }
        } else if (state == 0x7846) {
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
            uint16_t target = read_word(ram, actor + 0x34) + read_word(ram, 0xb71c);
            write_word(ram, actor + 0x34, target);
            uint16_t speed = read_word(ram, actor + 0x3a);
            move_actor_axis(ram, actor + 0x23, speed);
            int16_t error = 8 * (target - read_word(ram, actor + 0x24)) - speed;
            write_word(ram, actor + 0x3a, speed + (error >> 5));
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x4c) + frame));
            write_word(ram, actor + 0x44, read_word(bank, 0x736e + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame >= 24 ? 0 : frame);
            if (frame >= 24 && !decrement(ram, actor + 0x56)) {
                write_word(ram, actor + 0x56, 4);
                uint16_t step = read_word(ram, actor + 0x54) + 2;
                uint16_t sequence = read_word(ram, actor + 0x52);
                uint16_t kind = read_word(bank, sequence + step);
                if (kind == 0xffff) { step = 0; kind = read_word(bank, sequence); }
                write_word(ram, actor + 0x54, step);
                write_word(ram, actor + 0x50, kind);
                write_word(ram, actor + 0x4c, read_word(bank, 0x72ce + kind));
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0xd] = 1;
        uint16_t player = world_player_contact(ram, main_rom, actor);
        if (player) {
            uint16_t kind = read_word(ram, actor + 0x50);
            uint16_t weapon = read_word(bank, 0x7386 + kind * 2);
            if (weapon) {
                world_queue_sound(ram, read_word(bank, 0x73a6 + kind));
                uint16_t stats = read_word(ram, player + 0x70);
                uint16_t slot = stats + read_word(bank, 0x7388 + kind * 2);
                write_word(ram, slot + 0x10, weapon);
                uint16_t level = read_word(ram, slot + 0x12) + 1;
                if ((int16_t)level > 3) {
                    level = 3;
                    write_word(ram, stats + 0x1a, read_word(ram, stats + 0x1a) + 1);
                    write_word(ram, stats + 0x1c, read_word(ram, stats + 0x1c) + 1);
                }
                write_word(ram, slot + 0x12, level);
            }
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7ed34:
        break;
    case 0x7dc27:
    case 0x7dc68:
        if (read_word(ram, actor + 0x48) < read_word(ram, 0xb700)) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 0x46)) {
            if (address == 0x7dc68)
                write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
            uint16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
            write_word(ram, actor + 0x21, x);
            if (x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (address == 0x7dc27) {
            if (!decrement(ram, actor + 0x42))
                write_word(ram, actor, 0xdc68);
        } else if (!read_word(ram, 0x286)) {
            if (read_word(ram, actor + 0x50)) {
                if (decrement(ram, actor + 0x50)) break;
            } else {
                if (decrement(ram, actor + 0x58)) break;
                write_word(ram, actor + 0x58, read_word(ram, actor + 0x5a));
            }
            if (!decrement(ram, actor + 0x54)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
            }
            uint16_t child = effect_slot(ram, actor, read_word(ram, actor + 0x60));
            if (child != 0x8480)
                write_word(ram, child + 2, read_word(ram, actor + 0x62));
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x44));
        }
        break;
    case 0xb8a38:
        ram[actor + 0x2c] = 0x13;
        write_word(ram, actor + 0x42, 256);
        write_word(ram, actor, 0x8a46);
        /* fall through */
    case 0xb8a46: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!decrement(ram, actor + 0x42)) {
            unsigned frame = read_word(ram, actor + 0x40);
            queue_palette(ram, ram[actor + 0x2c] & 63, read_word(bank, 0x8108 + frame), 0);
            write_word(ram, actor + 0x42, read_word(bank, 0x811c + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame < 20 ? frame : 0);
        }
        if (ram[0x263] & 0x80) {
            queue_palette(ram, ram[actor + 0x2c] & 63, read_word(bank, 0x810a), 0);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7e7b6:
        write_word(ram, actor + 0x42, 32);
        write_word(ram, actor, 0xe7c0);
        /* fall through */
    case 0x7e7c0:
        if (read_word(ram, 0x262) & 0x8000)
            finish_animation(ram, actor);
        else if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            queue_palette(ram, 7, read_word(main_rom + 0x70000, 0xe7ae + (frame & 2)), 5);
            queue_palette(ram, 8, read_word(main_rom + 0x70000, 0xe7b2 + (frame & 2)), 5);
            write_word(ram, actor + 0x42, 33);
            write_word(ram, actor + 0x40, frame + 2);
        }
        break;
    case 0xc309:
        write_word(ram, actor + 0x50, effect_slot(ram, actor, 0xdecd));
        write_word(ram, 0x286, 0);
        /* fall through */
    case 0xc322:
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor, 0xc32c);
        /* fall through */
    case 0xc32c:
        if (!(ram[0x264] & 12)) {
            write_word(ram, actor + 0x42, 0x501);
            write_word(ram, actor + 0x5a, 0x24);
            write_word(ram, 0x286, 1);
            write_word(ram, actor, 0xc35e);
        }
        break;
    case 0xc35e: {
        uint16_t display = read_word(ram, actor + 0x50);
        if (ram[0x264] & 12) {
            write_word(ram, display + 0x40, 0xffff);
            write_word(ram, 0x286, 0);
            write_word(ram, actor, 0xc322);
            break;
        }
        if (!decrement(ram, actor + 0x5a)) {
            write_word(ram, actor + 0x5a, 36);
            world_queue_sound(ram, 0x50);
        }
        unsigned waiting = ram[0x266] & 3;
        for (unsigned i = 0; i < 2; ++i) {
            if (!(waiting & (1u << i))) continue;
            uint16_t player = read_word(ram, 0x90 + 2*i);
            if (read_word(ram, player + 0x42)) write_word(ram, player + 0x42, read_word(ram, actor + 0x42) + 256);
        }
        uint16_t count = decrement(ram, actor + 0x42);
        if (!count) {
            ram[0x262] |= 2;
            write_word(ram, display + 0x40, 1);
            write_word(ram, actor + 0x42, 238);
            write_word(ram, actor, 0xc419);
        } else if (read_word(ram, actor + 0x58)) decrement(ram, actor + 0x58);
        else if (!(count & 127)) {
            write_word(ram, display + 0x40, 1);
            write_word(ram, actor + 0x58, 9);
        } else if (count < 0x380 && (((waiting & 1) && (ram[0xb743] & 0xc0)) ||
                                      ((waiting & 2) && (ram[0xb742] & 0xc0)))) {
            write_word(ram, actor + 0x42, (count & 0x780) + 1);
        }
        break;
    }
    case 0xc419:
        if (!decrement(ram, actor + 0x42)) {
            ram[0x263] |= 8;
            finish_animation(ram, actor);
        }
        break;
    case 0x19e2:
        for (unsigned entry = 0x19e2; entry <= 0x19f8; entry += 11) {
            uint16_t child = effect_slot(ram, actor, read_word(main_rom, entry + 4));
            if (child != 0x8480) write_word(ram, child + 2, read_word(main_rom, entry + 1));
        }
        write_word(ram, actor, 0x1a0d);
        write_word(ram, actor + 2, 0);
        break;
    case 0x19a6: {
        effect_slot(ram,actor,0xca44);
        uint16_t player=effect_slot(ram,actor,0xca9c);
        ram[player+0x0b]=2;write_word(ram,0x92,player);
        player=effect_slot(ram,actor,0xca96);
        ram[player+0x0b]=1;write_word(ram,0x90,player);
        write_word(ram,actor,0x19e1);
        break;
    }
    case 0x19e1:
    case 0xca63:
        break;
    case 0xca44:
        do {
            uint16_t child=effect_slot(ram,actor,0xca64);
            write_word(ram,child+0x1e,read_word(ram,actor+0x42));
            write_word(ram,actor+0x42,read_word(ram,actor+0x42)+1);
        } while(read_word(ram,actor+0x42)<9);
        write_word(ram,actor,0xca63);
        break;
    case 0xca64: {
        ram[actor+0x2c]=0x84;ram[actor+0x2d]=0;
        write_word(ram,actor+0x3c,0xfb4b);
        unsigned offset=2*read_word(ram,actor+0x1e);
        write_word(ram,actor+0x2a,read_word(main_rom,0xce7d+offset));
        write_word(ram,actor+0x21,read_word(main_rom,0xce8f+offset));
        write_word(ram,actor+0x24,0x100);
        write_word(ram,actor,0xca90);
        /* fall through */
    }
    case 0xca90:
        queue_sprite(ram,main_rom,actor,0);
        break;
    case 0xca96:
    case 0xca9c: {
        unsigned side=address==0xca9c;
        if(side)write_word(ram,actor+0x1e,1);
        write_word(ram,actor+0x48,read_word(ram,0x124+64*side));
        write_word(ram,actor+0x42,read_word(ram,0x84)+1);
        while(decrement(ram,actor+0x42)) {
            unsigned medals=read_word(ram,actor+0x48);
            write_word(ram,actor+0x48,medals>>1);
            if(medals&1) {
                uint16_t child=effect_slot(ram,actor,0x2e60);
                write_word(ram,child+0x1e,read_word(ram,actor+0x40)+read_word(ram,actor+0x1e));
            }
            write_word(ram,actor+0x40,read_word(ram,actor+0x40)+2);
        }
        for(unsigned i=0;i<2;++i) {
            uint16_t child=effect_slot(ram,actor,0x2b18);
            write_word(ram,child+0x1e,read_word(ram,actor+0x1e)+2*i);
        }
        unsigned place=read_word(ram,0x11e + 64*side);
        if(place>=11) {
            decrement(ram,0xd2);
            finish_animation(ram,actor);
            break;
        }
        write_word(ram,actor+0x50,(place-1)*32);
        tally_score(ram,main_rom,tiles,side);
        write_word(ram,actor+0x42,1);
        write_word(ram,actor+0x40,0);
        write_word(ram,actor+0x46,0);
        write_word(ram,actor+0x54,640);
        write_word(ram,actor+0x52,read_word(main_rom,0xcea1+2*side));
        name_glyph(ram,main_rom,tiles,actor);
        write_word(ram,actor,0xcb44);
        /* The initial entry performs its first input step immediately. */
        /* fall through */
    }
    case 0xcb44:
        name_input(ram,main_rom,tiles,actor);
        break;
    case 0xcc89:
        if((read_word(ram,0xb742)&15) || !decrement(ram,actor+0x42)) {
            decrement(ram,0xd2);
            finish_animation(ram,actor);
        }
        break;
    case 0xccb7:
        finish_animation(ram,actor);
        break;
    case 0x1a3a:
        effect_slot(ram, actor, 0xcccb);
        write_word(ram, actor, 0x1a4f);
        write_word(ram, actor+2, 0);
        break;
    case 0x1a4f:
        break;
    case 0xcccb: {
        if (read_word(ram, 0xd2)) {
            for (unsigned p=0; p<2; ++p) {
                unsigned rank=read_word(ram, 0x11e + 64*p);
                if(rank<11) {
                    uint16_t child=effect_slot(ram, actor, 0xcd9c);
                    write_word(ram,child+0x1e,p); write_word(ram,child+0x4e,rank);
                }
            }
        }
        unsigned stage=read_word(ram,0x84);
        uint16_t child=effect_slot(ram,actor,read_word(main_rom,0xd181+4*stage));
        if(child!=0x8480) write_word(ram,child+2,read_word(main_rom,0xd183+4*stage));
        ram[actor+0x42]=main_rom[0xd0c6+stage];
        write_word(ram,actor,0xcd34);
        /* fall through */
    }
    case 0xcd34:
        if (!--ram[actor+0x42]) {
            unsigned row=read_word(main_rom,0xd060+2*read_word(ram,0x84));
            unsigned slot=0;
            do {write_word(ram,0x9e08+8*slot++,0);row+=2;} while(read_word(main_rom+0xa0000,row)!=0xffff);
            finish_animation(ram, actor);
        }
        break;
    case 0xc930:
        finish_animation(ram, actor);
        break;
    case 0x1a0e: {
        uint16_t child = effect_slot(ram, actor, read_word(main_rom, 0x1a12));
        if (child != 0x8480) write_word(ram, child + 2, read_word(main_rom, 0x1a0f));
        write_word(ram, actor, 0x1a23);
        write_word(ram, actor + 2, 0);
        break;
    }
    case 0x1a0d:
    case 0x1a23:
        break;
    case 0x18a9:
        init_stage_list(ram, main_rom, actor);
        /* fall through */
    case 0x1950:
        update_rank(ram, main_rom);
        break;
    case 0x3b26:
        update_layer_scroll(ram, main_rom, actor);
        break;
    case 0x4177:
        update_background_vertical(ram, actor);
        /* fall through */
    case 0x417a:
        write_word(ram, 0xb71e, 0);
        write_word(ram, 0xb720, 0);
        break;
    case 0x3fcd:
        update_background_vertical(ram, actor);
        /* fall through */
    case 0x3fd0: {
        if (!(read_word(ram, 0x26e) & 0x4000)) {
            write_word(ram, 0xb71e, 0);
            break;
        }
        int speed = (int16_t)read_word(ram, 0xa4) >> 1;
        uint16_t old_x = read_word(ram, actor + 0x52);
        unsigned frac = ram[actor + 0x51] + (speed & 255);
        int delta = (speed >> 8) + (int)(frac >> 8);
        ram[actor + 0x51] = frac;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        write_word(ram, 0xb70e, old_x + delta);
        write_word(ram, 0xb71e, -delta);
        if (read_word(ram, actor + 0x46)) advance_scroll_script(ram, main_rom, actor);
        break;
    }
    case 0x3b45:
    case 0x3b5d:
        if (address == 0x3b45) {
            write_word(ram, 0xb716, 0);
            write_word(ram, 0xb718, 0);
            write_word(ram, 0xb726, 0);
            write_word(ram, 0xb728, 0);
            write_word(ram, actor + 0x6c, 640);
            write_word(ram, actor, 0x3b5d);
        }
        if (!decrement(ram, actor + 0x6c)) {
            write_word(ram, actor + 0x6e, 0xff80);
            write_word(ram, actor + 0x6c, 25);
            write_word(ram, actor, 0x3b72);
        }
        break;
    case 0x3d93:
        write_word(ram, 0xb71a, 0);
        write_word(ram, 0xb72a, 0);
        write_word(ram, 0xb726, 0);
        write_word(ram, 0x26e, 0x4000);
        write_word(ram, actor + 0x6c, 1);
        write_word(ram, actor, 0x3db1);
        /* fall through */
    case 0x3db1:
        if (read_word(ram, 0xa4) >= 0x400) {
            write_word(ram, 0xa4, 0x400);
            write_word(ram, actor, 0x3dd6);
        } else if (!(decrement(ram, actor + 0x6c) & 15)) {
            write_word(ram, 0xa4, read_word(ram, 0xa4) + 64);
        }
        /* fall through */
    case 0x3dd6: {
        uint16_t old_x = read_word(ram, actor + 0x52);
        uint16_t velocity = read_word(ram, 0xa4);
        unsigned fraction = ram[actor + 0x51] + (velocity & 255);
        int delta = (int8_t)(velocity >> 8) + (int)(fraction >> 8);
        ram[actor + 0x51] = (uint8_t)fraction;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        write_word(ram, 0xb70a, old_x + delta);
        write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
        write_word(ram, 0xb72a, -delta);
        write_word(ram, 0xb726, -delta);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
