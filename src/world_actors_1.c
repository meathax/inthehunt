/* Recovered World actor handlers, part 1 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_1(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    case 0xb7960: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned variant = read_word(ram, actor + 0x1e);
        world_acquire_resource(ram, actor, read_word(bank, 0x7958 + (variant & 6)));
        write_word(ram, actor + 0x0e, 15);
        ram[actor + 0x2d] = 0x20;
        write_word(ram, actor + 0x26, 0x80);
        write_word(ram, actor + 0x36, 0x20);
        write_word(ram, actor + 0x3c, 0xf4db);
        ram[actor + 0x0c] = 10;
        write_word(ram, actor + 0x38, (variant & 1) ? 0x100 : -0x100);
        unsigned profile = 0x7944 + read_word(bank, 0x7904 + (variant & 32) + read_word(ram, 0x88));
        write_word(ram, actor + 0x50, read_word(bank, profile));
        write_word(ram, actor + 0x52, read_word(bank, profile));
        write_word(ram, actor + 0x2e, read_word(bank, profile + 2));
        write_word(ram, actor, 0x7aa9);
        break;
    }
    case 0xb79c5:
    case 0xb79d4:
    case 0xb7aa9:
    case 0xb7acd: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        int jumping = address == 0xb7aa9 || address == 0xb7acd;
        if (address == 0xb79c5) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0x79d4);
        }
        if (address == 0xb7aa9) {
            world_queue_sound(ram, 0x8d);
            write_word(ram, actor + 0x2a, 0x3e56);
            unsigned index = ((read_word(ram, actor + 0x50) - 1) & 3) * 2;
            write_word(ram, actor + 0x3a, read_word(bank, 0x78e4 + index));
            write_word(ram, actor, 0x7acd);
        }
        scroll_actor(ram, actor, 0xb71a);
        if (jumping) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
            int16_t lower = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            unsigned x = read_word(ram, actor + 0x21);
            if (x > 0x1c0 || x < 0x40 || (int16_t)read_word(ram, actor + 0x24) < lower) {
                finish_animation(ram, actor);
                break;
            }
        }
        unsigned phase = (uint16_t)(read_word(ram, actor + 0x46) + 64);
        if (phase >= 0xa00) phase = 0;
        write_word(ram, actor + 0x46, phase);
        write_word(ram, actor + 0x2a, read_word(bank, 0x7872 + 2*(phase >> 8)));
        queue_sprite(ram, main_rom, actor, 1);
        ++ram[actor + 0x19];
        if (!jumping) {
            if (!decrement(ram, actor + 0x44)) {
                unsigned frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x7886 + frame);
                if (sprite == 0xffff) write_word(ram, actor, 0x7aa9);
                else {
                    write_word(ram, actor + 0x54, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x7892 + frame));
                    write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0x789c + frame));
                    if (frame == 4 && read_word(ram, actor + 0x21) >= 0xa0 &&
                        (ram[actor + 0x1e] & 4) && !decrement(ram, actor + 0x50)) {
                        if (ram[actor + 0x1e] & 2) {
                            uint16_t shot = bank_b_effect(ram, actor, 0x7cda);
                            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                            uint16_t speed = read_word(ram, actor + 0x2e);
                            write_word(ram, shot + 0x2e, speed + (speed >> 1));
                        }
                        write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
                    }
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            write_word(ram, actor + 0x2a, read_word(ram, actor + 0x54));
            queue_sprite(ram, main_rom, actor, 1);
        } else {
            write_word(ram, actor + 0x2a, 0x3e56);
            queue_sprite(ram, main_rom, actor, 1);
            if (read_word(ram, actor + 0x21) >= 0xa0 && !(ram[actor + 0x1e] & 4) &&
                !read_word(ram, actor + 0x3a) && !decrement(ram, actor + 0x50)) {
                if (ram[actor + 0x1e] & 2) {
                    uint16_t shot = bank_b_effect(ram, actor, 0x7cda);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                } else if (ram[0x264] & 3) {
                    uint16_t player = world_enemy_target(ram, actor);
                    uint16_t shot = bank_b_effect(ram, actor, 0x7c2e);
                    ram[shot + 0x29] = read_word(ram, player + 0x21) >= read_word(ram, actor + 0x21);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
                    write_word(ram, shot + 0x2e, read_word(ram, actor + 0x2e));
                } else {
                    write_word(ram, actor + 0x50, read_word(ram, actor + 0x50) + 1);
                    goto stage2_jumper_terrain;
                }
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
            }
stage2_jumper_terrain:
            if ((int16_t)read_word(ram, actor + 0x3a) < 0 &&
                (world_terrain_mask(ram, tiles + 0x4000, read_word(ram, actor + 0x21),
                                     read_word(ram, actor + 0x24) - 8) & 0xff02)) {
                unsigned scroll = read_word(ram, 0xb70c) & 7;
                uint16_t y = read_word(ram, actor + 0x24);
                ram[actor + 0x23] = 0;
                write_word(ram, actor + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
                write_word(ram, actor, 0x79c5);
            } else {
                uint16_t x = read_word(ram, actor + 0x21) + ((int16_t)read_word(ram, actor + 0x38) >> 5);
                uint16_t y = read_word(ram, actor + 0x24) + 8;
                unsigned cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
                for (unsigned row = 0; row < 3; ++row) {
                    unsigned material = tiles[0x4000 + ((cell + 256*row) & 0x3fff) + 3] >> 3;
                    unsigned mask = 1u << (material & 15);
                    if (!(mask & 0xff02)) continue;
                    write_word(ram, actor + 0x60, mask);
                    write_word(ram, actor + 0x38, -read_word(ram, actor + 0x38));
                    break;
                }
            }
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (read_word(ram, actor + 0x26) && !world_ship_contact(ram, main_rom, actor)) break;
        world_award_score(ram, main_rom, actor, hit);
        world_queue_sound(ram, 0x5d);
        world_spawn_radial_explosion(ram, main_rom, actor);
        finish_animation(ram, actor);
        break;
    }
    case 0xbf70a:
    case 0xbf74a:
    case 0xbf784: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xbf70a) {
            write_word(ram, actor + 0x0e, 0x123);
            write_word(ram, actor + 0x26, 8);
            write_word(ram, actor + 0x36, 0x80);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3c, 0xf703);
            write_word(ram, actor + 0x38, read_word(bank, 0xf300 + 2*(ram[actor + 0x29] & 1)));
            write_word(ram, actor + 0x3a, 0x200);
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xf74a);
        } else scroll_actor(ram, actor, 0xb71a);
        if (address != 0xbf784) {
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
        } else {
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)read_word(ram, actor + 0x24) < limit) {
                finish_animation(ram, actor);
                break;
            }
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (address == 0xbf784) {
            if ((1u << (material & 15)) & 0xff02) {
                spawn_impact(ram, main_rom, actor);
                finish_animation(ram, actor);
                break;
            }
        } else if (material == 4) {
            spawn_bomb_splash(ram, main_rom, actor);
            write_word(ram, actor + 0x3a, 0xfe40);
            write_word(ram, actor + 0x62, 16);
            write_word(ram, actor, 0xf784);
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = (read_word(ram, actor + 0x40) & 14) + (read_word(ram, actor + 0x62) & 16);
            write_word(ram, actor + 0x2a, read_word(bank, 0xf2bc + frame));
            write_word(ram, actor + 0x44, read_word(bank, 0xf2dc + frame));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 2;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (!hit) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
            if (!hit) {
                ram[actor + 0x0d] = 0;
                hit = world_surface_contact(ram, main_rom, actor);
            }
        }
        if (hit) finish_animation(ram, actor);
        break;
    }
    case 0xa8309:
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x2a, 0x8375);
        write_word(ram, actor + 0x3c, 0xfb4b);
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor, 0x832a);
        break;
    case 0xa832a:
    case 0xa836b:
    case 0xa83e6:
    case 0xa843f: {
        const uint8_t *bank = main_rom + 0xa0000;
        scroll_actor(ram, actor, 0xb72a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xa832a) {
            queue_sprite(ram, main_rom, actor, 0);
            if (read_word(ram, 0xb8aa) & (1u << (ram[actor + 0x1e] & 7))) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x836b);
            }
            break;
        }
        if (address == 0xa836b) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x81ec + frame);
                if (sprite == 0xffff) {
                    write_word(ram, actor + 0x40, 0);
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x42, 0xb7);
                    uint16_t child = effect_slot(ram, actor, 0x848d);
                    if (child != 0x8480) write_word(ram, child + 2, 0xa000);
                    write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, child + 0x24, read_word(ram, actor + 0x24) - 0x40);
                    write_word(ram, actor, 0x83e6);
                    address = 0xa83e6;
                } else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x8216 + (frame & 2)));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            if (address == 0xa836b) {
                queue_sprite(ram, main_rom, actor, 0);
                break;
            }
        }
        if (address == 0xa83e6) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(bank, 0x821a + (frame & 6)));
                write_word(ram, actor + 0x44, read_word(bank, 0x8222 + (frame & 6)));
                write_word(ram, actor + 0x40, frame + 2);
            }
            queue_sprite(ram, main_rom, actor, 0);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x843f);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                uint16_t sprite = read_word(bank, 0x822a + frame);
                if (sprite == 0xffff) write_word(ram, actor, 0x832a);
                else {
                    write_word(ram, actor + 0x2a, sprite);
                    write_word(ram, actor + 0x44, read_word(bank, 0x823e + frame));
                    write_word(ram, actor + 0x40, frame + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    }
    case 0xa848d: {
        write_word(ram, 0xb8ac, read_word(ram, 0xb8ac) + 1);
        world_acquire_resource(ram, actor, 0xe5);
        write_word(ram, actor + 0x0e, 5);
        write_word(ram, actor + 0x3c, 0xf303);
        ram[actor + 0x2d] = 5;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x3a, 0xc0);
        write_word(ram, actor + 0x40, 0xfffe);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, 5);
        write_word(ram, actor + 0x4a, 0xb7);
        write_word(ram, actor + 0x4c, 16);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t child = effect_slot(ram, actor, 0x85ff);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x3c, i ? 0xf313 : 0xf30b);
            write_word(ram, child + 0x46, i ? 4 : -4);
            write_word(ram, child + 0x3a, i ? -64 : 64);
            if (i) {
                write_word(ram, child + 0x1e, 32);
                ram[child + 0x29] = 2;
            }
        }
        uint16_t child = effect_slot(ram, actor, 0x87ed);
        if (child != 0x8480) write_word(ram, child + 2, 0xa000);
        write_word(ram, actor, 0x8518);
        break;
    }
    case 0xa8518: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb72a);
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if ((int16_t)x <= 0x50 || (int16_t)y > 0x1b8) {
            write_word(ram, actor + 0x48, 3);
            decrement(ram, 0xb8ac);
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40) + 2;
            write_word(ram, actor + 0x40, frame);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x8250 + (frame & 14)));
            write_word(ram, actor + 0x44, read_word(ram, actor + 0x46));
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 2;
        world_player_contact(ram, main_rom, actor);
        ram[actor + 0x0d] = 1;
        world_projectile_contact(ram, main_rom, actor);
        unsigned flags = read_word(ram, actor + 0x48);
        uint16_t sample_y = flags & 1 ? y - 0x40 : y + 0x38;
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - sample_y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (!(flags & 1) && material == 3) {
            uint16_t splash = spawn_surface_splash_effect(ram, main_rom, actor, x, y);
            write_word(ram, splash + 0x24, read_word(ram, splash + 0x24) + 0x38);
            ram[splash + 0x2d] = 4;
            uint16_t child = effect_slot(ram, actor, 0x8715);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x21, x);
            write_word(ram, child + 0x24, y + 0x38);
            ram[actor + 0x48] |= 1;
        } else if ((flags & 1) && !(flags & 2) && material == 3) {
            uint16_t child = effect_slot(ram, actor, 0x86d4);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, actor + 0x3a, 0x300);
            write_word(ram, actor + 0x4c, 4);
            ram[actor + 0x48] |= 2;
        }
        break;
    }
    case 0xa85ff:
        world_acquire_resource(ram, actor, 0xe4);
        ram[actor + 0x2d] = 5;
        write_word(ram, actor + 0x36, 0x800);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x861a);
        break;
    case 0xa861a:
    case 0xa8671: {
        uint16_t parent = stage_two_machine_parent(ram, actor);
        if (!parent) break;
        if (address == 0xa861a) {
            int detach = !(ram[actor + 0x29] & 2) && (ram[parent + 0x48] & 2);
            if (detach) write_word(ram, actor + 0x42, 0);
            else {
                ram[actor + 0x0d] = 2;
                world_player_contact(ram, main_rom, actor);
                ram[actor + 0x0d] = 1;
                if (!world_projectile_contact(ram, main_rom, actor)) break;
                write_word(ram, actor + 0x42, 32);
                write_word(ram, parent + 0x3a,
                           read_word(ram, parent + 0x3a) + read_word(ram, actor + 0x3a));
            }
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) + read_word(ram, actor + 0x46));
            write_word(ram, actor, 0x8671);
        } else {
            unsigned frame = (read_word(ram, actor + 0x40) & 2) + read_word(ram, actor + 0x1e) +
                             2 * (read_word(ram, parent + 0x40) & 14);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x8260 + frame));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
            queue_sprite(ram, main_rom, actor, 0);
            ram[actor + 0x0d] = 2;
            world_player_contact(ram, main_rom, actor);
            ram[actor + 0x0d] = 1;
            world_projectile_contact(ram, main_rom, actor);
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, parent + 0x46,
                           read_word(ram, parent + 0x46) - read_word(ram, actor + 0x46));
                write_word(ram, parent + 0x3a,
                           read_word(ram, parent + 0x3a) - read_word(ram, actor + 0x3a));
                write_word(ram, actor, 0x861a);
            }
        }
        break;
    }
    case 0xa86d4:
        world_acquire_resource(ram, actor, 0xe7);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x86ea);
        break;
    case 0xa86ea:
        if (stage_two_machine_parent(ram, actor) && !decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82a0 + (frame & 2)));
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x82a4 + (frame & 2)));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa8715:
        world_acquire_resource(ram, actor, 0x57);
        write_word(ram, actor + 0x42, 64);
        write_word(ram, actor, 0x8727);
        break;
    case 0xa8727:
        scroll_actor(ram, actor, 0xb72a);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x8737);
        break;
    case 0xa8737: {
        scroll_actor(ram, actor, 0xb72a);
        unsigned frame = read_word(ram, actor + 0x40) & 14;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (!frame && (ram[parent + 0x48] & 2)) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82a8 + frame));
        write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xa87ed:
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x34, -44);
        write_word(ram, actor, 0x87ff);
        break;
    case 0xa87ff:
        if (stage_two_machine_parent(ram, actor)) {
            write_word(ram, actor + 0x24, read_word(ram, actor + 0x24) - 44);
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, 0x82b8 + (frame & 6)));
            write_word(ram, actor + 0x40, frame + 2);
            queue_sprite(ram, main_rom, actor, 0);
        }
        break;
    case 0xa8780:
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x44, 32);
        write_word(ram, actor + 0x4e, 1);
        write_word(ram, actor, 0x8795);
        break;
    case 0xa8795:
        write_word(ram, 0xb8aa, 0);
        if (read_word(ram, 0xb700) >= read_word(ram, actor + 0x1e)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, read_word(ram, actor + 0x44));
            write_word(ram, 0xb8aa, read_word(ram, 0xb8aa) | (1u << (read_word(ram, actor + 0x40) & 7)));
            write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 1);
        }
        if (read_word(ram, 0xb8ac) && !decrement(ram, actor + 0x4e)) {
            world_queue_sound(ram, 0x53);
            write_word(ram, actor + 0x4e, 60);
        }
        break;
    case 0x11496:
    case 0x114f1: case 0x11545: case 0x1156f:
    case 0x115bd:
    case 0x115fb: case 0x116b8: case 0x116c8:
        if (!boss_segment_step(ram, main_rom, actor, (uint16_t)address)) return 0;
        break;
    case 0xb30c6:
        world_acquire_resource(ram, actor, read_word(main_rom + 0xb0000, 0x30c7));
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x0e, 14);
        write_word(ram, actor + 0x50, 1);
        write_word(ram, actor, 0x30e1);
        /* fall through */
    case 0xb30e1: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a] & 1) {
            finish_animation(ram, actor);
            break;
        }
        unsigned pose = read_word(ram, parent + 0x60);
        ram[actor + 0x29] = ram[parent + 0x29];
        unsigned facing = ram[actor + 0x29] & 1;
        uint16_t dx = read_word(bank, 0x28e2 + pose);
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21) + (facing ? -dx : dx));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) + read_word(bank, 0x28e4 + pose));
        uint16_t frame = read_word(ram, actor + 0x40);
        int16_t next = frame + 2;
        write_word(ram, actor + 0x40, next >= 6 ? 0 : next);
        write_word(ram, actor + 0x2a, read_word(bank, 0x27d2 + frame + (pose ? 0 : 6)));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x50, 4);
            world_queue_sound(ram, 0x96);
            uint16_t shot = bank_b_effect(ram, actor, 0x31c8);
            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24));
            ram[shot + 0x2c] = ram[actor + 0x2c];
            ram[shot + 0x28] = bank[(uint16_t)(0x2912 + (pose >> 2) + 12*facing)];
            if (!decrement(ram, actor + 0x58)) finish_animation(ram, actor);
        }
        break;
    }
    case 0xb31c8:
    case 0xb31f2:
    case 0xb3269: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb31c8) {
            write_word(ram, actor + 0x3c, read_word(bank, 0x31cb));
            write_word(ram, actor + 0x26, 96);
            write_word(ram, actor + 0x36, 128);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x0e, 14);
            write_word(ram, actor + 0x2e, 768);
            for (unsigned axis = 0; axis < 2; ++axis) {
                uint16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2 * ram[actor + 0x28]);
                write_word(ram, actor + 0x38 + 2*axis, 3 * unit);
            }
            write_word(ram, actor, 0x31f2);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (address == 0xb3269) {
            ram[actor + 0x0d] = 2;
            if (world_player_contact(ram, main_rom, actor) || !decrement(ram, actor + 0x42)) {
                finish_animation(ram, actor);
                break;
            }
        } else {
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0x5502) {
                finish_animation(ram, actor);
                break;
            }
            if (material == 4) {
                spawn_bomb_splash(ram, main_rom, actor);
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor, 0x3269);
            } else {
                int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
                if (x > 0x1c0 || x < 64 || (int16_t)y < limit) {
                    finish_animation(ram, actor);
                    break;
                }
            }
        }
        uint16_t frame = read_word(ram, actor + 0x40) + 128;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, 0x27de + 2*((frame >> 8) & 1)));
        queue_sprite(ram, main_rom, actor, 1);
        break;
    }
    case 0xb2eee:
        if (read_word(ram, actor + 0x0e) != 14) {
            scroll_actor(ram, actor, 0xb71a);
            surface_enemy_death_blast(ram, main_rom, actor);
            finish_animation(ram, actor);
            break;
        }
        if (!tiles) return 0;
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x36, 0x280);
        ram[actor + 0x0a] = 1;
        write_word(ram, actor, 0x2f25);
        /* fall through */
    case 0xb2f25: {
        if (!tiles) return 0;
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        uint16_t speed = read_word(ram, actor + 0x3a) - 32;
        write_word(ram, actor + 0x3a, speed);
        move_actor_axis(ram, actor + 0x23, speed);
        write_word(ram, actor + 0x26, 96);
        if (read_word(ram, actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
        queue_surface_enemy(ram, main_rom, actor);
        if (!decrement(ram, actor + 0x42)) {
            surface_enemy_death_blast(ram, main_rom, actor);
            write_word(ram, actor + 0x42, 8);
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x187 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if ((1u << (material & 15)) & 0x551a) {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6b01));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6b06));
            ram[blast + 0x2c] = main_rom[0x6b0b];
            ram[blast + 0x2d] = main_rom[0x6b0f];
            write_word(ram, blast + 0x56, read_word(main_rom, 0x6b13));
            write_word(ram, blast + 0x58, read_word(main_rom, 0x6b18));
            world_queue_sound(ram, read_word(main_rom, 0x6b1b));
            if (!(blast & 0x300)) write_word(ram, blast + 0x44, read_word(main_rom, 0x6b2b));
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb2f81:
        world_acquire_resource(ram, actor, read_word(main_rom + 0xb0000, 0x2f82));
        write_word(ram, actor + 0x0e, 0x109);
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xb0000, 0x2f91));
        write_word(ram, actor + 0x26, 32);
        write_word(ram, actor + 0x36, 128);
        ram[actor + 0x2d] = 33;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x2fac);
        break;
    case 0xb2fac:
    case 0xb3033: {
        if (!tiles) return 0;
        int airborne = address == 0xb2fac;
        scroll_actor(ram, actor, 0xb71a);
        if (airborne) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (airborne) {
            if (!decrement(ram, actor + 0x44)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x27b0 + frame));
                write_word(ram, actor + 0x44, 2);
                write_word(ram, actor + 0x40, frame + 2);
                if ((uint16_t)(frame + 2) >= 18) {
                    write_word(ram, actor + 0x44, 0xffff);
                    write_word(ram, actor + 0x38, 0);
                    ram[actor + 0x2d] = 4;
                }
            }
        } else {
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)y < limit) {
                finish_animation(ram, actor);
                break;
            }
        }
        if (!queue_sprite(ram, main_rom, actor, 1) && !airborne) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int impact = (1u << (material & 15)) & (airborne ? 0x5502 : 0xff02);
        if (airborne && material == 4) {
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x5e));
            write_word(ram, actor + 0x40, 0);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0x300f));
            water_entry_splash(ram, main_rom, actor, x, y);
            write_word(ram, actor, 0x3033);
        }
        if (!impact) {
            ram[actor + 0x0d] = 0;
            if (airborne) world_surface_contact(ram, main_rom, actor);
            else {
                uint16_t hit = world_projectile_contact(ram, main_rom, actor);
                if (hit && read_word(ram, hit + 0x0e) == 0x121) {
                    int16_t dx = read_word(ram, hit + 0x21) - x, dy = read_word(ram, hit + 0x24) - y;
                    unsigned ax = abs((int)dx), ay = abs((int)dy);
                    unsigned direction = dx < 0 ? 128 : 0;
                    if (ax || ay) {
                        unsigned octant = ay > ax ? main_rom[0xd58e + 256*ax/ay] : main_rom[0xd48d + 256*ay/ax];
                        direction += (dx < 0) != (dy < 0) ? -octant : octant;
                    }
                    ram[actor + 0x28] = (uint8_t)direction;
                    write_word(ram, actor + 0x2e, 0x400);
                    for (unsigned axis = 0; axis < 2; ++axis) {
                        uint16_t unit = read_word(main_rom + 0xa0000, (axis ? 0x9d92 : 0x9e12) + 2*(uint8_t)direction);
                        write_word(ram, actor + 0x38 + 2*axis, unit * 4);
                    }
                    write_word(ram, actor + 0x42, 7);
                    write_word(ram, actor, 0xae2a);
                    write_word(ram, actor + 2, 0);
                    break;
                }
            }
            impact = !read_word(ram, actor + 0x26);
            if (!impact) {
                ram[actor + 0x0d] = 2;
                impact = world_player_contact(ram, main_rom, actor) || world_ship_contact(ram, main_rom, actor);
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb6c6c:
    case 0xb6ca8:
    case 0xb6cf7: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb6c6c) {
            world_acquire_resource(ram, actor, read_word(bank, 0x6c6d));
            write_word(ram, actor + 0x3c, read_word(bank, 0x6c77));
            write_word(ram, actor + 0x26, 1);
            write_word(ram, actor + 0x36, 0x800);
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3a, 0x200);
            unsigned side = (ram[actor + 0x29] & 1) * 2;
            unsigned index = read_word(ram, actor + 0x60) >> 1;
            write_word(ram, actor + 0x38, read_word(bank, 0x4fc8 + index + side));
            write_word(ram, actor, 0x6ca8);
        }
        int underwater = address == 0xb6cf7;
        scroll_actor(ram, actor, 0xb71a);
        if (!underwater) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (underwater) {
            int16_t limit = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
            if ((int16_t)y < limit) {
                finish_animation(ram, actor);
                break;
            }
        } else {
            write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a) - 32);
        }
        uint16_t frame = read_word(ram, actor + 0x40) + 128;
        write_word(ram, actor + 0x40, frame);
        write_word(ram, actor + 0x2a, read_word(bank, (underwater ? 0xf2cc : 0xf2bc) + 2*((frame >> 8) & 7)));
        queue_sprite(ram, main_rom, actor, 1);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        if (!underwater) {
            if (material == 4) {
                write_word(ram, actor + 0x3a, read_word(ram, actor + 0x5a));
                water_entry_splash(ram, main_rom, actor, x, y);
                write_word(ram, actor, 0x6cf7);
            }
            break;
        }
        int impact = (1u << (material & 15)) & 0xff02;
        if (!impact) {
            ram[actor + 0x0d] = 2;
            impact = world_player_contact(ram, main_rom, actor) != 0;
            if (!impact) {
                ram[actor + 0x0d] = 0;
                world_projectile_contact(ram, main_rom, actor);
                impact = !read_word(ram, actor + 0x26);
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb5b93:
    case 0xb5bfd: {
        if (!warship_attachment(ram, actor)) break;
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        unsigned index = read_word(ram, actor + 0x60);
        if (address == 0xb5b93) {
            write_word(ram, actor + 0x2a, read_word(bank, 0x4f8c + index));
            queue_sprite(ram, main_rom, actor, 0);
            if (!read_word(ram, parent + 0x54) && read_word(ram, parent + 0x66) == index) {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x44, 1);
                write_word(ram, actor, 0x5bfd);
            }
        } else {
            if (!decrement(ram, actor + 0x44)) {
                unsigned frame = read_word(ram, actor + 0x40);
                if (frame == 4) {
                    uint16_t shot = effect_slot(ram, actor, 0x6c6c);
                    if (shot != 0x8480) write_word(ram, shot + 2, 0xb000);
                    write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
                    write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + 8);
                    write_word(ram, shot + 0x60, index);
                    ram[shot + 0x29] = ram[actor + 0x29];
                    write_word(ram, shot + 0x5a, read_word(ram, actor + 0x5a));
                    /* 364B leaves BX at the attempted sound-ring slot. */
                    frame = (read_word(ram, 0xa6d4) + 2) & 14;
                    world_queue_sound(ram, 0x76);
                }
                if (frame >= 10) write_word(ram, actor, 0x5b93);
                else {
                    write_word(ram, actor + 0x44, read_word(bank, 0x4fac + frame));
                    unsigned sprite = index + read_word(bank, 0x4fb6 + frame);
                    write_word(ram, actor + 0x2a, read_word(bank, 0x4f8c + sprite));
                    write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
                }
            }
            queue_sprite(ram, main_rom, actor, 0);
        }
        if (address == 0xb5b93 && (read_word(ram, parent + 0x68) & (1u << ((index >> 3) & 2)))) {
            warship_part_explosion(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb5c73:
    case 0xb5cc6:
    case 0xb5cd0: {
        const uint8_t *bank = main_rom + 0xb0000;
        if (address == 0xb5cc6) {
            write_word(ram, actor + 0x42, 16);
            write_word(ram, actor, 0x5cd0);
        }
        int active = warship_attachment(ram, actor);
        if (address == 0xb5c73 && !active) break;
        queue_sprite(ram, main_rom, actor, 0);
        if (address == 0xb5c73) {
            ram[actor + 0x0d] = 0;
            uint16_t hit = world_surface_contact(ram, main_rom, actor);
            if (!read_word(ram, actor + 0x26)) {
                world_award_score(ram, main_rom, actor, hit);
                warship_part_explosion(ram, main_rom, actor);
                unsigned index = read_word(ram, actor + 0x60);
                write_word(ram, actor + 0x2a, read_word(bank, 0x4db2 + index));
                uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                write_word(ram, parent + 0x68, read_word(ram, parent + 0x68) | (1u << (index & 15)));
                write_word(ram, actor + 0x3c, 0xffff);
                write_word(ram, actor, 0x5cc6);
            }
            register_surface_enemy(ram, actor);
        } else if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 16);
            uint16_t effect = effect_slot(ram, actor, 0x6d91);
            if (effect != 0x8480) write_word(ram, effect + 2, 0xb000);
            write_word(ram, effect + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, effect + 0x24, read_word(ram, actor + 0x24));
        }
        break;
    }
    case 0xb46a:
        ram[actor + 0x2c] = 3;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x42, 0);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor, 0xb482);
        /* fall through */
    case 0xb482: {
        unsigned level = read_word(ram, actor + 0x7a);
        int homing = read_word(ram, actor + 0x60) == 3;
        unsigned index = read_word(ram, actor + 0x42);
        unsigned count = homing ? (level >= index ? level - index + 1 : 1) : level + 1;
        for (unsigned i = 0; i < count; ++i) {
            uint16_t child = world_spawn_child(ram, actor, homing ? 0xb4ce : 0xb6df);
            if (child) {
                write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + (homing ? 0 : 4));
                ram[child + 0x0b] = ram[actor + 0x0b];
                ram[child + 0x2d] = ram[actor + 0x2d];
                if (homing) {
                    write_word(ram, child + 0x78, read_word(ram, actor + 0x42));
                    ram[child + 0x29] = ram[actor + 0x29];
                    ram[child + 0x28] = ram[actor + 0x28];
                    write_word(ram, child + 0x44, read_word(ram, actor + 0x44));
                } else {
                    write_word(ram, child + 0x52, count - i - 1);
                    write_word(ram, child + 0x7a, (level - 1) * 2);
                }
                write_word(ram, actor + 0x44, read_word(ram, actor + 0x44) + 1);
            }
            if (homing) write_word(ram, actor + 0x42, read_word(ram, actor + 0x42) + 1);
        }
        world_queue_sound(ram, 0x35);
        write_word(ram, actor, 0xb4b0);
        break;
    }
    case 0xb6df:
    case 0xb730:
    case 0xb77c: {
        if (!tiles) return 0;
        if (address == 0xb6df) {
            world_acquire_resource(ram, actor, read_word(main_rom, 0x9bb1 + (ram[actor + 0x0b] & 2)));
            write_word(ram, actor + 0x3c, 0xb3cb);
            write_word(ram, actor + 0x26, 8);
            write_word(ram, actor + 0x36, 0xa0);
            write_word(ram, actor + 0x42, 5);
            write_word(ram, actor + 0x5a, 5);
            write_word(ram, actor + 0x58, 1);
            unsigned sequence = read_word(main_rom, 0xb3db + read_word(ram, actor + 0x7a));
            ram[actor + 0x28] = main_rom[(uint16_t)(0xb461 + sequence + read_word(ram, actor + 0x52))];
            write_word(ram, actor + 0x2e, 0x200);
            unsigned angle = 2 * ram[actor + 0x28];
            write_word(ram, actor + 0x38, ((uint32_t)read_word(main_rom + 0xa0000, 0x9e12 + angle) * 0x200) >> 8);
            write_word(ram, actor + 0x3a, ((uint32_t)read_word(main_rom + 0xa0000, 0x9d92 + angle) * 0x200) >> 8);
            write_word(ram, actor, 0xb730);
        }
        if (address != 0xb77c && !decrement(ram, actor + 0x58)) {
            unsigned frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x58, read_word(main_rom, 0xb3ab + frame));
            if (read_word(ram, actor + 0x52) & 1) frame += 26;
            uint16_t sprite = read_word(main_rom, 0xb377 + frame);
            if (sprite != 0xffff) {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x40, read_word(ram, actor + 0x40) + 2);
            } else {
                write_word(ram, actor + 0x2a, 0x2332);
                write_word(ram, actor + 0x38, 0);
                write_word(ram, actor + 0x3a, 0x600);
                write_word(ram, actor + 0x5a, 3);
                write_word(ram, actor + 0x42, 2);
                write_word(ram, actor, 0xb77c);
            }
        }
        scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, read_word(ram, actor + 0x5a));
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0x9e17);
            write_word(ram, trail + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, trail + 0x24, read_word(ram, actor + 0x24) - 4);
            ram[trail + 0x2c] = 3;
            write_word(ram, trail + 0x56, read_word(ram, actor + 0x58) ? 0x20 : 0x40);
            write_word(ram, trail + 0x58, 0x11);
        }
        unsigned cell = world_tile_address(read_word(ram, actor + 0x21) - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - read_word(ram, actor + 0x24) + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int outside = outside_pickup_area(ram, actor);
        int impact = (0x5502 & (1u << (material & 15))) ||
                     (material != 3 && !outside && !read_word(ram, actor + 0x26));
        if (impact && read_word(ram, 0x84) == 4) return 0;
        if (impact || material == 3 || outside) {
            if (impact) spawn_impact(ram, main_rom, actor);
            else if (material == 3)
                spawn_surface_splash(ram, main_rom, actor, read_word(ram, actor + 0x21), read_word(ram, actor + 0x24));
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            decrement(ram, parent + 0x44);
            finish_animation(ram, actor);
        } else if (read_word(ram, actor + 0x3c) != 0xffff) {
            world_append_candidate(ram + 0xa6e2, actor);
        }
        break;
    }
    case 0xb4b0:
        if (!read_word(ram, actor + 0x44)) {
            uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
            decrement(ram, parent + 0x44);
            retire_actor(ram, actor);
        }
        break;
    case 0xde69:
        ram[actor + 0x2c] = 0x85;
        write_word(ram, actor, 0xde72);
        /* fall through */
    case 0xde72: {
        uint16_t parent = read_word(ram, actor + 0x54);
        if (!ram[parent + 0x58] || !read_word(ram, 0x286)) {
            finish_animation(ram, actor);
            break;
        }
        for (uint16_t row = 0xde37; read_word(main_rom, row) != 0xffff; row += 6) {
            write_word(ram, actor + 0x21, read_word(main_rom, row));
            write_word(ram, actor + 0x24, read_word(main_rom, row + 2));
            write_word(ram, actor + 0x2a, read_word(main_rom, row + 4));
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    }
    case 0xe1da:
        if (!--ram[actor + 0x4e]) {
            for (unsigned i = 0; i < 8; ++i) {
                uint16_t child = effect_slot(ram, actor, 0xf1b5);
                static const unsigned dest[] = {0x4e, 0x2a, 0x21, 0x24, 0x31, 0x34};
                for (unsigned n = 0; n < 6; ++n)
                    write_word(ram, child + dest[n], read_word(main_rom, 0xf101 + 12*i + 2*n));
                ram[child + 0x2c] = 0x85;
            }
            queue_continue_sound(ram, 0x14);
            write_word(ram, actor, 0xe1ef);
            ram[actor + 0x4e] = 99;
        }
        break;
    case 0xe1ef:
        if (!--ram[actor + 0x4e]) finish_animation(ram, actor);
        break;
    case 0xe209:
        if (!--ram[actor + 0x43]) {
            ram[actor + 0x43] = 28;
            if (--ram[actor + 0x60]) queue_continue_sound(ram, 0x6f);
            else finish_animation(ram, actor);
        }
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
