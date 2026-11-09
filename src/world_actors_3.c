/* Recovered World actor handlers, part 3 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_3(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    case 0x10a22: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0) write_word(ram, actor, 0xa46);
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0xd] = 1;
        world_projectile_contact(ram, main_rom, actor);
        break;
    }
    case 0x10a46:
    case 0x10a6b:
    case 0x10f69:
    case 0x10fab:
    case 0x10fce:
    case 0x10fe5:
    case 0x11009:
    case 0x11025:
    case 0x11bcf: {
        const uint8_t *bank = main_rom + 0x10000;
        if (address == 0x10a46 || address == 0x10f69) {
            ram[actor + 0xc] = 4;
            uint16_t target = world_enemy_target(ram, actor);
            unsigned choice = 0;
            if (address == 0x10a46) {
                if (ram[0x264] & 3) {
                    choice = read_word(ram, target + 0x24) < read_word(ram, actor + 0x24) ? 2 : 0;
                    write_word(ram, actor + 0x56, choice);
                }
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x694 + choice));
                address = 0x10a6b;
            } else {
                choice = 2;
                if (ram[0x264] & 3) {
                    uint16_t y = read_word(ram, target + 0x24);
                    choice = y < 0xa8 ? 4 : y >= 0x160 ? 0 : 2;
                } else target = 0;
                write_word(ram, actor + 0x4e, target);
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x6c4 + choice));
                address = 0x10fab;
            }
            write_word(ram, actor, (uint16_t)address);
        }
        if (address == 0x10fe5 || address == 0x11009) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0 && address == 0x10fe5) {
                ram[actor + 0x63] &= (uint8_t)~1u;
                ram[actor + 0x63] |= 2;
                begin_linear_script(ram, main_rom, actor, 0x758);
                write_word(ram, actor, 0x1009);
            } else if (step < 0) {
                ram[actor + 0x63] &= (uint8_t)~2u;
                write_word(ram, actor + 0x42, 80);
                write_word(ram, actor, 0x1025);
            }
        } else if (address == 0x11025 && !decrement(ram, actor + 0x42)) {
            write_word(ram, actor, 0xa46);
        }
        int calm = !(ram[actor + 0x66] & 1);
        if (calm && (address == 0x10a6b || address == 0x10fab)) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0 && address == 0x10fab) {
                write_word(ram, actor + 0x42, 96);
                ram[actor + 0x62] &= ~16u;
                write_word(ram, actor, 0xfce);
            } else if (step < 0) {
                uint16_t arms = read_word(ram, actor + 0x60), sequence = read_word(ram, actor + 0x58);
                if (arms == 15 && read_word(bank, 0x18 + (sequence & 6))) {
                    write_word(ram, actor, 0xfe5);
                    ram[actor + 0x63] |= 1;
                    begin_linear_script(ram, main_rom, actor, 0x746);
                } else if (arms & 3) {
                    write_word(ram, actor, 0xf69);
                    ram[actor + 0x62] |= 16;
                } else {
                    uint16_t side = read_word(ram, actor + 0x56) + 2;
                    write_word(ram, actor + 0x56, side);
                    begin_linear_script(ram, main_rom, actor, read_word(bank, 0x694 + (side & 2)));
                }
                if ((arms == 15 && read_word(bank, 0x18 + (sequence & 6))) || (arms & 3)) {
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                    write_word(ram, actor + 0x58, sequence + 2);
                }
            }
        } else if (calm && address == 0x10fce && !decrement(ram, actor + 0x42)) write_word(ram, actor, 0xa46);
        if (ram[actor + 0x19]) {
            --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x14 + (ram[actor + 0x19] & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0x11bcf && !(ram[actor + 0x62] & 1)) write_word(ram, actor, 0xa6b);
        if (calm && address == 0x10a6b && !decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            ram[actor + 0x62] |= 1;
            uint16_t child = spawn_boss_part(ram, actor, 0x1be1);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            ram[child + 0x14] = ram[actor + 0x14];
            write_word(ram, child + 0x54, read_word(ram, actor + 0x54));
            ram[child + 0x2d] = 0x2e;
            write_word(ram, child + 0x44, 1);
            world_queue_sound(ram, 0x66);
            write_word(ram, actor, 0x1bcf);
        }
        if (calm) write_word(ram, actor + 0x26, read_word(ram, actor + 0x4a));
        ram[actor + 0xd] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0xd] = calm ? 0 : 1;
        uint16_t attacker = world_projectile_contact(ram, main_rom, actor);
        if (calm && !read_word(ram, actor + 0x26)) {
            if (ram[0x264] & 3) {
                ram[0x124 + 32*(ram[attacker + 0xb] & 2)] |= 1;
                world_award_score(ram, main_rom, actor, attacker);
                ram[actor + 0xa] |= 2;
                write_word(ram, actor + 0x3a, -128);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x42, 1);
                ram[actor + 0x2c] = ram[actor + 0x14];
                ram[0x262] |= 128;
                write_word(ram, actor, 0xea3);
                break;
            }
            write_word(ram, actor + 0x26, 256);
        }
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        if (!calm) {
            if (!decrement(ram, actor + 0x68)) {
                ram[actor + 0x66] = (ram[actor + 0x66] & ~1u) | 4;
                write_word(ram, actor + 0x68, 64);
            }
            break;
        }
        write_word(ram, actor + 0x4a, read_word(ram, actor + 0x26));
        if (!(ram[actor + 0x66] & 2) && !(ram[actor + 0x60] & 3)) {
            write_word(ram, actor + 0x66, 3);
            write_word(ram, actor + 0x2a, read_word(bank, 0xc0f));
            write_word(ram, actor + 0x68, 256);
            write_word(ram, actor + 0x50, 8);
            write_word(ram, actor + 0x52, 32);
            uint16_t part = spawn_boss_part(ram, actor, 0xcd1);
            ram[part + 0x2c] = ram[actor + 0x14];
            spawn_boss_part(ram, actor, 0xe0b);
            part = spawn_boss_part(ram, actor, 0xc8f);
            ram[part + 0x2d] = ram[actor + 0x2d];
        } else if ((ram[actor + 0x66] & 4) && !decrement(ram, actor + 0x68)) {
            write_word(ram, actor + 0x68, 32);
            uint16_t step = read_word(ram, actor + 0x40);
            boss_blast(ram, main_rom, actor, 0x8a2 + step);
            write_word(ram, actor + 0x40, step + 6 < 36 ? step + 6 : 0);
        }
        break;
    }
    case 0x10ea3:
    case 0x10f16: {
        if (address == 0x10ea3) {
            if (!tiles) return 0;
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            queue_sprite(ram, main_rom, actor, 0);
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            if (world_terrain_mask(ram, tiles + 0x4000, x, y - 48) & 0xff02) {
                write_word(ram, actor + 0x38, -512);
                write_word(ram, actor + 0x31, x);
                write_word(ram, actor + 0x34, y);
                write_word(ram, actor, 0xf16);
            }
        } else {
            move_actor_axis(ram, actor + 0x30, read_word(ram, actor + 0x38));
            uint16_t x = read_word(ram, actor + 0x31) + (world_random_byte(ram) & 2) - 1;
            write_word(ram, actor + 0x21, x);
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x34) + (world_random_byte(ram) & 2) - 1);
            if ((int16_t)x <= 0) {
                ram[0x263] |= 0xc0;
                finish_animation(ram, actor);
                break;
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 3);
            uint16_t step = read_word(ram, actor + 0x40);
            boss_blast(ram, main_rom, actor, 0x7b8 + step);
            step += 6;
            write_word(ram, actor + 0x40, step < 60 ? step : 0);
        }
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0x7df0e:
    case 0x7dffe: {
        const uint8_t *bank = main_rom + 0x70000;
        int base = address == 0x7df0e;
        if (base) world_acquire_resource(ram, actor, 0x2a);
        write_word(ram, actor + 0xe, 0x24);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, base ? 0xffff : read_word(bank, 0xe00a));
        write_word(ram, actor + 0x2a, read_word(bank, base ? 0xdf27 : 0xe013));
        uint16_t cfg = read_word(bank, 0xdeba + (read_word(ram, actor + 0x1e) & 32) + read_word(ram, 0x8a));
        uint16_t hp = read_word(bank, 0xdefa + cfg);
        write_word(ram, actor + 0x26, hp);
        write_word(ram, actor + 0x36, hp);
        if (base) {
            uint16_t child = effect_slot(ram, actor, 0xdffe);
            if (child != 0x8480) write_word(ram, child + 2, 0x7000);
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
            ram[child + 0x2c] = ram[actor + 0x2c];
            write_word(ram, actor, 0xdf66);
            break;
        }
        ram[actor + 0xc] = 8;
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor + 0x52, read_word(bank, 0xdefc + cfg));
        write_word(ram, actor, 0xe042);
    }
        /* fall through */
    case 0x7e042:
    case 0x7e106: {
        const uint8_t *bank = main_rom + 0x70000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0xa]) { finish_animation(ram, actor); break; }
        uint16_t x = read_word(ram, parent + 0x21), y = read_word(ram, parent + 0x24);
        write_word(ram, actor + 0x21, x);
        write_word(ram, actor + 0x24, y);
        if (address != 0x7e106) {
            queue_sprite(ram, main_rom, actor, 1);
            if ((ram[0x264] & 3) && !decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                uint16_t target = world_enemy_target(ram, actor);
                unsigned side = read_word(ram, target + 0x21) < x ? 2 : 0;
                write_word(ram, actor + 0x54, side);
                write_word(ram, actor + 0x48, read_word(bank, 0xde02 + side));
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xe106);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t step = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, read_word(ram, actor + 0x48) + step);
                if (sprite == 0xffff) write_word(ram, actor, 0xe042);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0xde32 + step));
                    write_word(ram, actor + 0x40, step + 2);
                    if (step == 4 || step == 10) {
                        uint16_t side = read_word(ram, actor + 0x54) + (step == 10);
                        write_word(ram, actor + 0x54, side);
                        uint16_t dx = read_word(bank, 0xde4e + (side & 2));
                        uint16_t flash = effect_slot(ram, actor, 0xa2c0);
                        write_word(ram, flash + 0x31, dx);
                        write_word(ram, flash + 0x34, 29);
                        ram[flash + 0x2d] = 3;
                        write_word(ram, flash + 0x60, 3);
                        uint16_t shot = effect_slot(ram, actor, 0xe21c);
                        if (shot != 0x8480) write_word(ram, shot + 2, 0x7000);
                        write_word(ram, shot + 0x54, side);
                        write_word(ram, shot + 0x21, x + dx);
                        write_word(ram, shot + 0x24, y + 29);
                        world_queue_sound(ram, 0x63);
                    }
                }
            }
            queue_sprite(ram, main_rom, actor, 1);
        }
        ram[actor + 0xd] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26) && read_word(ram, parent + 0x26)) register_surface_enemy(ram, actor);
        else {
            world_award_score(ram, main_rom, actor, hit);
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            write_word(ram, parent + 0x46, 1);
            write_word(ram, actor + 0x2a, read_word(bank, 0xe0f8));
            write_word(ram, actor, 0xe1d6);
        }
        break;
    }
    case 0x7e1d6: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        uint16_t speed = read_word(ram, actor + 0x3a) - 16;
        write_word(ram, actor + 0x3a, speed);
        move_actor_axis(ram, actor + 0x23, speed);
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t y = read_word(ram, actor + 0x24);
        uint16_t cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                           0x177 - y + read_word(ram, 0xb70c));
        if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
            write_word(ram, actor + 0x3a, -256);
            unsigned scroll = read_word(ram, 0xb70c) & 7;
            write_word(ram, actor + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
            ram[actor + 0x23] = 0;
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c03));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c08));
            write_word(ram, blast + 0x44, 0x57);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 64);
            write_word(ram, blast + 0x58, 0);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7df66:
    case 0x7dfb4: {
        scroll_actor(ram, actor, 0xb71a);
        uint16_t x = read_word(ram, actor + 0x21);
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        if (address == 0x7dfb4 && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xde46 + (frame & 6)));
            write_word(ram, actor + 0x44, 9);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (address == 0x7df66) {
            ram[actor + 0xd] = 0;
            world_projectile_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26) || read_word(ram, actor + 0x46)) {
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0xdfb4);
            }
        }
        break;
    }
    case 0x7e41e: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, read_word(bank, 0xe41f));
        world_acquire_resource(ram, actor, read_word(bank, 0xe41a + 2 * (variant & 1)));
        world_acquire_resource(ram, actor, read_word(bank, 0xe43a));
        ram[actor + 0x2c] = ram[actor + 0x15];
        write_word(ram, actor + 0xe, 0x1c);
        write_word(ram, actor + 0x26, 0x120);
        write_word(ram, actor + 0x36, 0x120);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x3c, read_word(bank, 0xe45d));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        unsigned height = variant & 6;
        if (!height) height = world_random_byte(ram) & 6;
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xe3d0 + height));
        write_word(ram, actor, 0xe48a);
        break;
    }
    case 0x7e48a: {
        const uint8_t *bank = main_rom + 0x70000;
        if (!read_word(ram, actor + 0x26)) return 0;
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
        write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(ram, 0xb71c));
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t phase = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(bank, 0xe3a0 + phase));
            write_word(ram, actor + 0x46, read_word(bank, 0xe3b8 + phase));
            write_word(ram, actor + 0x44, 8);
            phase += 2;
            write_word(ram, actor + 0x40, phase < 24 ? phase : 0);
        }
        uint16_t x = read_word(ram, actor + 0x31) + read_word(ram, actor + 0x46);
        write_word(ram, actor + 0x21, x);
        if (ram[actor + 0x19]) {
            unsigned flash = --ram[actor + 0x19];
            ram[actor + 0x2c] = ram[actor + 0x15 + ((flash >> 1) & 1)];
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) ram[actor + 0x19] = (ram[actor + 0x19] & 1) + 20;
            if (read_word(ram, actor + 0x26)) break;
        }
        ram[actor + 0xb] = ram[hit + 0xb];
        world_award_score(ram, main_rom, actor, hit);
        uint16_t y = read_word(ram, actor + 0x24) + 0x70;
        if (ram[actor + 0x1e] & 1) {
            for (int direction = 7; direction >= 0; --direction) {
                uint16_t child = effect_slot(ram, actor, 0xe5d8);
                if (child != 0x8480) write_word(ram, child + 2, 0x7000);
                write_word(ram, child + 0x21, x);
                write_word(ram, child + 0x24, y);
                ram[child + 0x28] = direction * 32;
            }
        }
        uint16_t blast = effect_slot(ram, read_word(ram, 0x9c), 0xe587);
        if (blast != 0x8480) write_word(ram, blast + 2, 0x7000);
        write_word(ram, blast + 0x21, x);
        write_word(ram, blast + 0x24, y);
        ram[blast + 0x2c] = ram[actor + 0x14];
        ram[blast + 0x2d] = 0x20;
        write_word(ram, blast + 0x3c, read_word(bank, 0xe55f));
        write_word(ram, blast + 0x36, 0x120);
        write_word(ram, blast + 0x44, 1);
        world_queue_sound(ram, 0x5e);
        finish_animation(ram, actor);
        break;
    }
    case 0x7e587: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        uint16_t frame = read_word(ram, actor + 0x40);
        if (!decrement(ram, actor + 0x44)) {
            uint16_t sprite = read_word(bank, 0xe3d8 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0xe3fa + frame));
            frame += 2;
            write_word(ram, actor + 0x40, frame);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (frame >= 4 && frame < 8 && read_word(ram, actor + 0x3c) != 0xffff)
            world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0xb2e05:
    case 0xb2e4a:
    case 0xb2e5c: {
        uint16_t speed = read_word(ram, actor + 0x38);
        int16_t x = read_word(ram, actor + 0x21) + read_word(ram, 0xb71a);
        int magnitude = abs((int16_t)speed);
        if (address == 0xb2e05) {
            if (magnitude <= 512) speed -= read_word(ram, actor + 0x46);
            unsigned fraction = ram[actor + 0x20] + (speed & 255);
            x += (int8_t)(speed >> 8) + (fraction >> 8);
        }
        /* Offscreen kills also consume the collision routine's scratch BX. */
        if (x <= -64 || x >= 576) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xb2e05) {
            write_word(ram, actor + 0x38, speed);
            if (speed || magnitude > 512) {
                surface_enemy_pose(ram, main_rom + 0xb0000, actor, abs((int16_t)speed));
                move_actor_axis(ram, actor + 0x20, speed);
            } else {
                write_word(ram, actor, 0x2e4a);
                write_word(ram, actor + 0x4c, 0x172a);
                write_word(ram, actor + 0x4a, 0);
                write_word(ram, actor + 0x3a, 0);
                write_word(ram, actor + 0x42, 16);
                address = 0xb2e4a;
            }
        }
        if (address == 0xb2e4a && !decrement(ram, actor + 0x42)) write_word(ram, actor, 0x2e5c);
        if (address == 0xb2e5c) {
            uint16_t dy = read_word(ram, actor + 0x3a) + 32;
            write_word(ram, actor + 0x3a, dy);
            move_actor_axis(ram, actor + 0x23, dy);
            uint16_t limit = (ram[0x26e] & 2) ? 0x198 : 0x1c8 + read_word(ram, 0xb732);
            if (read_word(ram, actor + 0x24) > limit) {
                unsigned bit = 1u << (ram[actor + 0x1e] & 15);
                write_word(ram, 0xc4, read_word(ram, 0xc4) & ~bit);
                finish_animation(ram, actor);
                break;
            }
        }
        ram[actor + 0x29] = speed >> 15;
        queue_surface_enemy(ram, main_rom, actor);
        surface_enemy_hit(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        break;
    }
    case 0x7e5d8: {
        if (!tiles) return 0;
        world_acquire_resource(ram, actor, 10);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(main_rom + 0x70000, 0xe5e7));
        write_word(ram, actor + 0x2e, 0x200);
        unsigned angle = ram[actor + 0x28] * 2;
        write_word(ram, actor + 0x38, 2 * read_word(main_rom + 0xa0000, 0x9e12 + angle));
        write_word(ram, actor + 0x3a, 2 * read_word(main_rom + 0xa0000, 0x9d92 + angle));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe5fd);
    }
        /* fall through */
    case 0x7e5fd: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xa787 + (frame & 14)));
            write_word(ram, actor + 0x44, 2);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (!(world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                                  read_word(ram, actor + 0x24)) & 0xff0a)) {
            ram[actor + 0xd] = 0;
            if (!world_player_contact(ram, main_rom, actor)) break;
        }
        spawn_impact(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0x7e817: {
        const uint8_t *bank = main_rom + 0x70000;
        unsigned variant = read_word(ram, actor + 0x1f) & 2;
        ram[actor + 0x2c] = 7;
        write_word(ram, actor + 0xe, 7);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 64);
        write_word(ram, actor + 0x36, 16);
        write_word(ram, actor + 0x3c, read_word(bank, 0xe6e2 + variant));
        write_word(ram, actor + 0x48, read_word(bank, 0xe6ea + variant));
        write_word(ram, actor + 0x46, read_word(bank, 0xe6f2 + variant));
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe857);
    }
        /* fall through */
    case 0x7e857: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        if (outside_pickup_area(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        if (!(ram[actor + 0x1e] & 1)) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t step = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x48) + (step & 6)));
                write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
                write_word(ram, actor + 0x40, step + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        ram[actor + 0xd] = 1;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0xd] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (read_word(ram, actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        unsigned variant = read_word(ram, actor + 0x1f) & 2;
        write_word(ram, actor + 0x48, read_word(bank, 0xe6ee + variant));
        write_word(ram, actor + 0x4a, read_word(bank, 0xe6f6 + variant));
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xe922);
        break;
    }
    case 0x7e922:
        if (ram[actor + 0x1e] & 1) {
            spawn_stage_pickup(ram, actor);
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor, 0xe95f);
        /* fall through */
    case 0x7e95f: {
        const uint8_t *bank = main_rom + 0x70000;
        scroll_actor(ram, actor, 0xb71a);
        if (outside_pickup_area(ram, actor)) { finish_animation(ram, actor); break; }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, read_word(ram, actor + 0x48) + step);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, read_word(ram, actor + 0x4a) + step));
            write_word(ram, actor + 0x40, step + 2);
            if (step == 6) spawn_stage_pickup(ram, actor);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb0357:
    case 0xb03f1:
    case 0xb042b: {
        scroll_actor(ram, actor, 0xb71a);
        if (address != 0xb03f1) {
            uint16_t speed;
            if (address == 0xb0357) {
                uint16_t time = read_word(ram, actor + 0x46);
                if ((int16_t)time > 16) time = 16;
                speed = (uint16_t)((time << 8) | (time >> 8)) >> 2;
            } else {
                uint16_t acceleration = read_word(ram, actor + 0x2e) + 32;
                write_word(ram, actor + 0x2e, acceleration);
                speed = (int16_t)acceleration > 512 ? 512 : acceleration;
            }
            if (!(ram[actor + 0x29] & 1)) speed = (uint16_t)-speed;
            write_word(ram, actor + 0x38, speed);
            move_actor_axis(ram, actor + 0x20, speed);
            uint16_t x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (address == 0xb03f1 && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xb0000, 0x2b0 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x2e, 0);
                write_word(ram, actor, 0x42b);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0x2c0 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (address == 0xb0357 && !decrement(ram, actor + 0x46)) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x3f1);
        }
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (hit && (ram[actor + 0x0d] == 2 || !read_word(ram, actor + 0x26))) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        } else if (!read_word(ram, actor + 0x26)) {
            /* The no-contact death branch consumes an inherited BX. */
            return 0;
        }
        break;
    }
    case 0xb4652: {
        const uint8_t *bank = main_rom + 0xb0000;
        if ((int16_t)read_word(ram, 0xb700) >= (int16_t)read_word(ram, actor + 0x6e)) {
            finish_animation(ram, actor);
            break;
        }
        int16_t time = decrement(ram, actor + 0x42);
        if (time > 0) {
            uint16_t scroll = (uint16_t)-read_word(ram, 0xb71a);
            time = (uint16_t)time - (uint16_t)(scroll << 2);
            write_word(ram, actor + 0x42, (uint16_t)time);
            if (time > 0) break;
        }
        uint16_t child = world_spawn_child(ram, actor, 0x380f);
        unsigned frame = read_word(ram, actor + 0x40);
        if (child) {
            write_word(ram, child + 2, 0xb000);
            write_word(ram, child + 0x21, 0x1e0);
            write_word(ram, child + 0x24, read_word(bank, 0x37b0 + frame) + read_word(ram, 0xb732));
            write_word(ram, child + 0x1e, read_word(bank, 0x37c2 + frame) | ram[actor + 0x1e]);
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x44) + 1);
        }
        write_word(ram, actor + 0x42, read_word(bank, 0x379e + frame) << 2);
        frame += 2;
        write_word(ram, actor + 0x40, frame < 18 ? frame : 0);
        break;
    }
    case 0xb8244:
        if (!read_word(ram, actor + 0x48)) finish_animation(ram, actor);
        break;
    case 0xb876c:
        world_acquire_resource(ram, actor, 0x28);
        ram[actor + 0x2d] = 1;
        write_word(ram, actor + 0x26, 0x180);
        write_word(ram, actor + 0x36, 0x180);
        {
            unsigned kind = 2 * (ram[actor + 0x1e] & 15);
            write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x7fec + kind));
            write_word(ram, actor + 0x50, read_word(main_rom + 0xb0000, 0x8002 + kind));
        }
        write_word(ram, actor, 0x87a1);
        break;
    case 0xb87a1: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned kind = ram[actor + 0x1e] & 15;
        unsigned paint = read_word(bank, 0x80ee + 2*kind);
        if (paint != 0x8c85 && paint != 0x8ca8 && paint != 0x8cb0) return 0;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        unsigned damage = (uint16_t)(0x180 - read_word(ram, actor + 0x26));
        unsigned frame = 2 * ((damage >> 8) & 15);
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, read_word(ram, actor + 0x50) + frame));
        queue_sprite(ram, main_rom, actor, 0);
        if (read_word(ram, actor + 0x42)) decrement(ram, actor + 0x42);
        ram[actor + 0x0d] = (ram[actor + 0x1f] & 1) ? 1 : 0;
        uint16_t hit = world_projectile_contact(ram, main_rom, actor);
        if (ram[actor + 0x1f] & 1) break;
        if (hit && !read_word(ram, actor + 0x42)) {
            for (unsigned i = 0; i < 2; ++i) {
                uint16_t burst = effect_slot(ram, actor, 0x6efe);
                write_word(ram, burst + 0x4c, 0x75df);
                write_word(ram, burst + 0x4a, 0x76f6);
                write_word(ram, burst + 0x2c, 0x200a);
                write_word(ram, burst + 0x56, 0);
                write_word(ram, burst + 0x58, 0);
                ram[burst + 0x2d] = 1;
                ram[burst + 0x4e] = 0x80;
                write_word(ram, burst + 0x31, i ? 24 : 8);
                write_word(ram, burst + 0x34, i ? -16 : -8);
            }
            write_word(ram, actor + 0x42, 16);
        }
        if (!read_word(ram, actor + 0x26)) {
            write_word(ram, actor + 0x44, 1);
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, 0x74e5);
            write_word(ram, blast + 0x4a, 0x7675);
            write_word(ram, blast + 0x2c, 0x200a);
            write_word(ram, blast + 0x56, 64);
            write_word(ram, blast + 0x58, 0);
            world_queue_sound(ram, 0x3e);
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, 0x5d);
            write_word(ram, blast + 0x31, 16);
            write_word(ram, blast + 0x34, -16);
            uint16_t cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                              0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
            write_word(ram, actor + 0x52, cell);
            unsigned width = paint == 0x8c85 ? 4 : 5, height = paint == 0x8cb0 ? 4 : 6;
            for (unsigned row = 0; row < height; ++row)
                for (unsigned col = 0; col < width; ++col) {
                    unsigned dest = 0x4000 + ((cell + row*256) & 0x3f00) + ((cell + col*4) & 255);
                    write_word(tiles, dest + 2, 0x3016);
                }
            world_queue_sound(ram, 0x6d);
            write_word(ram, actor, 0x886d);
        }
        break;
    }
    case 0xb886d:
    case 0xb88bc: {
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xb886d && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            const uint8_t *bank = main_rom + 0xb0000;
            uint16_t sprite = read_word(bank, read_word(ram, actor + 0x50) + frame);
            if (sprite == 0xffff) write_word(ram, actor, 0x88bc);
            else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, read_word(bank, 0x80de + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xb0084: {
        uint16_t variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, 2);
        write_word(ram, actor + 0xe, 1);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x80);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x9e));
        ram[actor + 0x2d] = 0x20;
        ram[actor + 0x29] = variant & 1;
        write_word(ram, actor + 0x2e, 0xc0);
        write_word(ram, actor + 0x38, variant & 1 ? 0xc0 : -0xc0);
        write_word(ram, actor + 0x50, (variant >> 8) & 14);
        write_word(ram, actor + 0x44, 16);
        write_word(ram, actor + 0x46, 0x100);
        write_word(ram, actor + 0x48, 8);
        write_word(ram, actor, 0xde);
        break;
    }
    case 0xb00de:
        write_word(ram, actor + 0x31, read_word(ram, actor + 0x21));
        write_word(ram, actor + 0x34, read_word(ram, actor + 0x24));
        write_word(ram, actor, 0xef);
        /* fall through */
    case 0xb00ef:
    case 0xb01f6: {
        const uint8_t *bank = main_rom + 0xb0000;
        int turning = read_word(ram, actor) == 0x1f6;
        if (!turning && !tiles) return 0;
        if (turning) {
            if (!decrement(ram, actor + 0x42)) {
                uint16_t step = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x20 + step);
                write_word(ram, actor + 0x42, 6);
                if (sprite == 0xffff) write_word(ram, actor, 0xde);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x3c, read_word(bank, 0x56 + step));
                    if (step == 14) ram[actor + 0x29] ^= 1;
                    uint16_t speed = read_word(bank, 0x3c + step);
                    write_word(ram, actor + 0x38, ram[actor + 0x29] & 1 ? speed : -speed);
                    write_word(ram, actor + 0x40, step + 2);
                }
            }
            scroll_actor(ram, actor, 0xb71a);
        } else {
            write_word(ram, actor + 0x31, read_word(ram, actor + 0x31) + read_word(ram, 0xb71a));
            write_word(ram, actor + 0x34, read_word(ram, actor + 0x34) + read_word(ram, 0xb71c));
            write_word(ram, actor + 0x21, read_word(ram, actor + 0x21) + read_word(ram, 0xb71a));
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t x = read_word(ram, actor + 0x21);
        uint16_t dx = x - read_word(ram, actor + 0x31);
        if (!turning) {
            uint16_t phase = (uint32_t)dx * read_word(ram, actor + 0x46);
            int16_t wave = read_word(main_rom + 0xa0000, 0x9d92 + 2 * (phase >> 8));
            uint32_t y = ram[actor + 0x33] + 256u * read_word(ram, actor + 0x34);
            y += wave * (int16_t)read_word(ram, actor + 0x44);
            ram[actor + 0x23] = y;
            write_word(ram, actor + 0x24, y >> 8);
        }
        if (x < 0x40 || x > 0x1c0) { finish_animation(ram, actor); break; }
        uint16_t y = read_word(ram, actor + 0x24);
        if (!turning) {
            uint16_t table = read_word(bank, 0x74 + (((dx + 0x40) >> 5) & 4));
            uint16_t pose = ((y - read_word(ram, actor + 0x34) + 0x12) >> 1) & 14;
            write_word(ram, actor + 0x2a, read_word(bank, table + pose));
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (!turning) {
            uint8_t facing = ram[actor + 0x29];
            if (!decrement(ram, actor + 0x48)) {
                write_word(ram, actor + 0x48, 8);
                uint16_t wake = effect_slot(ram, read_word(ram, 0x9a), 0x7f73);
                ram[wake + 0x29] = facing ^ 1;
                write_word(ram, wake + 0x21, x + read_word(bank, 0x80 + 2 * ((facing ^ 1) & 1)));
                write_word(ram, wake + 0x24, y - 2);
            }
            if (world_terrain_mask(ram, tiles + 0x4000, x + read_word(bank, 0x7c + 2 * (facing & 1)), y) & 0xff02) {
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor, 0x1f6);
            }
        }
        ram[actor + 0xd] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0xd] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (read_word(ram, actor + 0x26)) break;
        }
        world_award_score(ram, main_rom, actor, hit);
        world_queue_sound(ram, 0x5d);
        uint16_t impact = effect_slot(ram, actor, 0x6efe);
        write_word(ram, impact + 0x4c, read_word(main_rom, 0x6b44));
        write_word(ram, impact + 0x4a, read_word(main_rom, 0x6b49));
        ram[impact + 0x2c] = 10;
        ram[impact + 0x2d] = 0x20;
        write_word(ram, impact + 0x56, 0x30);
        write_word(ram, impact + 0x58, 0x80);
        if (!(impact & 0x300)) write_word(ram, impact + 0x44, 0x5d);
        release_actor_resources(ram, actor);
        if (x >= 0x1a0) write_word(ram, actor + 0x21, 0x1a0);
        write_word(ram, actor, 0x76a6);
        break;
    }
    case 0xb6744:
    case 0xb6781:
    case 0xb64b6: {
        if (!warship_attachment(ram, actor)) break;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (address == 0xb6781 && !read_word(ram, parent + 0x6c) && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40), phase = frame & 2;
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x5410 + phase));
            write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0x5414 + phase));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            if (address == 0xb6781) write_word(ram, parent + 0x6c, 1);
            else {
                world_queue_sound(ram, 0x5d);
                world_spawn_radial_explosion(ram, main_rom, actor);
            }
            write_word(ram, actor, address == 0xb64b6 ? 0x64f3 : 0x67f3);
        }
        break;
    }
    case 0xb5ce6: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned resource = 0x64;
        acquire_private_palette(ram, actor, resource);
        ram[actor + 0x2c] = ram[actor + 0x15];
        write_word(ram, actor + 0xe, 0x2a);
        ram[actor + 0xc] = 4;
        ram[actor + 0x2d] = 0x28;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x31, 0);
        write_word(ram, actor + 0x34, -32);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x42, 32);
        ram[actor + 0x28] = 0x40;
        uint16_t cfg = 0x55d2 + read_word(bank, 0x5512 + (read_word(ram, actor + 0x1e) & 32) + read_word(ram, 0x8c));
        write_word(ram, actor + 0x52, read_word(bank, cfg + 8));
        write_word(ram, actor + 0x50, read_word(bank, cfg + 8) >> 1);
        write_word(ram, actor + 0x54, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x56, read_word(bank, cfg + 10));
        write_word(ram, actor + 0x2e, read_word(bank, cfg + 12));
        write_word(ram, actor + 0x4c, 40);
        write_word(ram, actor, 0x5d59);
    }
        /* fall through */
    case 0xb5d59: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        uint16_t x = read_word(ram, actor + 0x21);
        aim_warship_turret(ram, main_rom, actor, ram[actor + 0x29] & 1);
        write_word(ram, actor + 0x2a, read_word(bank, 0x4fd8 + read_word(ram, actor + 0x40)));
        queue_sprite(ram, main_rom, actor, 0);
        if (x >= 0x40 && x <= 0x1c0 && !read_word(ram, 0x286)) {
            int fire = 0;
            if (read_word(ram, actor + 0x50)) fire = !decrement(ram, actor + 0x50);
            else if (!decrement(ram, actor + 0x4c)) {
                fire = 1;
                write_word(ram, actor + 0x4c, 40);
                if (!decrement(ram, actor + 0x54)) {
                    write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                }
            }
            if (fire) {
                write_word(ram, actor + 0x42, 96);
                uint16_t barrel = read_word(bank, 0x4fea + ((ram[actor + 0x28] & 0xf0) >> 3));
                write_word(ram, actor + 0x46, barrel);
                uint16_t shot = world_spawn_child(ram, actor, 0x6150);
                if (shot) {
                    write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x46, barrel);
                    ram[shot + 0x29] = bank[0x56e0 + (barrel >> 1)];
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                }
            }
        }
        uint16_t hit = 0;
        for (unsigned i = 0; i < 2; ++i) {
            write_word(ram, actor + 0x3c, read_word(bank, i ? 0x5e3e : 0x5e16));
            ram[actor + 0xd] = 0;
            hit = world_player_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26)) break;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (!i && hit) {
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
            }
            if (!read_word(ram, actor + 0x26)) break;
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            warship_part_explosion(ram, main_rom, actor);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x26, 0x200);
            write_word(ram, actor + 0x3c, read_word(bank, 0x5e7c));
            ram[actor + 0xa] |= 2;
            write_word(ram, actor, 0x5e8b);
        }
        break;
    }
    case 0xb5e8b: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (!warship_attachment(ram, actor)) break;
        if (!decrement(ram, actor + 0x44)) {
            uint16_t tick = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x44, read_word(bank, 0x502e + (tick & 14)));
            write_word(ram, actor + 0x2a, read_word(bank, 0x502a + (tick & 2)));
            ram[actor + 0x2c] = ram[actor + 0x14 + ((tick >> 1) & 1)];
            write_word(ram, actor + 0x40, tick + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0xd] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            hit = world_projectile_contact(ram, main_rom, actor);
            if (hit) {
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                ram[parent + 0x19] = (ram[parent + 0x19] & 1) + 32;
            }
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, actor + 0x26, read_word(ram, actor + 0x48));
            write_word(ram, actor + 0x2a, read_word(bank, 0x5ef2));
            write_word(ram, actor + 0x42, 32);
            warship_blast_sequence(ram, actor, 0x5490, 4);
            write_word(ram, actor, 0x5f0d);
        }
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
