/* Recovered World actor handlers, part 5 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_5(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    case 0x3d1a:
    case 0x3d32:
        if (address == 0x3d1a) {
            write_word(ram, actor + 0x6c, 640);
            write_word(ram, 0xb71a, 0);
            write_word(ram, 0xb72a, 0);
            write_word(ram, 0xa4, 0);
            write_word(ram, 0x26e, 0);
            write_word(ram, actor, 0x3d32);
        }
        if (!decrement(ram, actor + 0x6c)) {
            write_word(ram, 0x26e, read_word(ram, 0x26e) | 0x4000);
            write_word(ram, 0xa4, 128);
            write_word(ram, actor + 0x6c, 33);
            write_word(ram, actor, 0x3d4e);
        }
        break;
    case 0x3b72:
    case 0x3b91:
    case 0x3d4e:
    case 0x3d6e: {
        int layer_one = address == 0x3b72 || address == 0x3b91;
        uint16_t speed = layer_one ? actor + 0x6e : 0xa4;
        if (address == 0x3d4e || address == 0x3b72) {
            uint16_t ticks = decrement(ram, actor + 0x6c);
            if (!(ticks & (layer_one ? 7 : 15))) write_word(ram, speed, read_word(ram, speed) << 1);
            if (!ticks) {
                write_word(ram, 0x26e, read_word(ram, 0x26e) | 0x100);
                write_word(ram, actor, layer_one ? 0x3b91 : 0x3d6e);
            }
        }
        uint16_t old_x = read_word(ram, actor + 0x52);
        uint16_t velocity = read_word(ram, speed);
        unsigned fraction = ram[actor + 0x51] + (velocity & 255);
        int delta = (int8_t)(velocity >> 8) + (int)(fraction >> 8);
        ram[actor + 0x51] = (uint8_t)fraction;
        write_word(ram, actor + 0x5a, old_x);
        write_word(ram, actor + 0x52, old_x + delta);
        if (layer_one) {
            write_word(ram, 0xb706, old_x + delta);
            write_word(ram, 0xb726, -delta);
            write_word(ram, 0xb716, (-delta) >> 3);
        } else {
            write_word(ram, 0xb70a, old_x + delta);
            write_word(ram, 0xb700, read_word(ram, 0xb700) + delta);
            write_word(ram, 0xb72a, -delta);
        }
        break;
    }
    case 0x3ad7:
    case 0x3bfa: case 0x3f11:
        init_tile_layer(ram, main_rom, actor, address == 0x3ad7 ? 0 : address == 0x3bfa ? 1 : 2);
        break;
    case 0x3bb8:
    case 0x3ecf: case 0x40d9: {
        if (!tiles) return 0;
        uint16_t stage = 2 * read_word(ram, 0x84);
        uint16_t table = address == 0x3bb8 ? 0x479f : address == 0x3ecf ? 0x47cb : 0x480d;
        uint16_t row = read_word(main_rom, table + stage);
        write_word(ram, actor + 0x40, row);
        write_word(ram, actor + 0x46, read_word(main_rom, row));
        write_word(ram, actor, (uint16_t)address + 25);
        update_tile_script(ram, main_rom, tiles, actor,
                           address == 0x3bb8 ? 0 : address == 0x3ecf ? 0x4000 : 0x8000);
        break;
    }
    case 0x3bd1:
    case 0x3ee8:
    case 0x40f2:
        if (!tiles)
            return 0;
        update_tile_script(ram, main_rom, tiles, actor,
                           address == 0x3bd1 ? 0 : address == 0x3ee8 ? 0x4000 : 0x8000);
        break;
    case 0xa419:
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor + 0x46, 1);
        write_word(ram, actor + 0x48, 1);
        write_word(ram, actor + 0x50, 16);
        write_word(ram, actor, 0xa432);
        /* fall through */
    case 0xa432:
    case 0xa6b3:
        if (!update_idle_equipment(ram, main_rom, actor, address == 0xa6b3))
            return 0;
        break;
    case 0x803f:
        write_word(ram, actor + 0x40, read_word(ram, ram[actor + 0x0b] == 1 ? 0x90 : 0x92));
        write_word(ram, actor, 0x8053);
        /* fall through */
    case 0x8053:
        update_sub_display(ram, main_rom, actor);
        break;
    case 0x8154:
    case 0x815c:
        write_word(ram, actor + 0x4c, read_word(main_rom, (uint16_t)address + 3));
        world_acquire_resource(ram, actor, read_word(main_rom, 0x7965 + (ram[actor + 0x0b] & 2)));
        ram[actor + 0x2c] |= 0x80;
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x817d));
        write_word(ram, actor + 0x36, 4);
        write_word(ram, actor + 0x58, 1);
        write_word(ram, actor, 0x818f);
        break;
    case 0x818f: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        uint16_t frame = read_word(ram, actor + 0x40);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t sprite = read_word(main_rom, read_word(ram, actor + 0x4c) + ((frame & 0xfffc) >> 1));
            if (sprite == 0xffff) {
                uint16_t player = read_word(ram, (ram[actor + 0x0b] & 1) ? 0x90 : 0x92);
                ram[player + 0x0a] = 0;
                write_word(ram, player + 0x26, 1);
                write_word(ram, player + 0x36, 0x100);
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x58, 4);
        }
        if (!(frame & 1)) queue_sprite(ram, main_rom, actor, 0);
        write_word(ram, actor + 0x40, frame + 1);
        world_append_candidate(ram + 0xa6e2, actor);
        break;
    }
    case 0x8126: {
        int16_t limit = (int16_t)read_word(ram, actor + 0x1e);
        if (limit && (int16_t)read_word(ram, 0xb700) > limit)
            finish_animation(ram, actor);
        else {
            scroll_actor(ram, actor, 0xb71a);
            write_word(ram, 0x25c, read_word(ram, actor + 0x24));
        }
        break;
    }
    case 0x9dab:
        ram[actor + 0x2c] = 3;
        write_word(ram, actor + 0x5c, 1);
        write_word(ram, actor, 0x9db9);
        /* fall through */
    case 0x9db9: {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
        cycle_torpedo_sprite(ram, main_rom, actor, 0x99a3, 5);
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x9e73:
    case 0x9fa1:
    case 0xa06b: {
        uint16_t fields;
        uint16_t resource = read_word(main_rom, 0x9bb1 + (ram[actor + 0x0b] & 2));
        if (address == 0x9fa1)
            resource = read_word(main_rom, 0x9fa2);
        world_acquire_resource(ram, actor, resource);
        if (address == 0xa06b) {
            fields = 0xa07f;
            ram[actor + 0x2d] = main_rom[0xa093];
            write_word(ram, actor + 0x0e, read_word(main_rom, 0xa097));
            write_word(ram, actor + 0x42, read_word(main_rom, 0xa09c));
            write_word(ram, actor + 0x58, read_word(main_rom, 0xa0a1));
            write_word(ram, actor, 0xa0a9);
        } else {
            fields = address == 0x9e73 ? 0x9e87 : 0x9fac;
            write_word(ram, actor + 0x0e, read_word(main_rom, fields + 20));
            write_word(ram, actor + 0x42, read_word(main_rom, fields + 25));
            if (address == 0x9e73) {
                write_word(ram, actor + 0x58, read_word(main_rom, 0x9ea5));
                write_word(ram, actor + 0x5a, read_word(main_rom, 0x9eaa));
                write_word(ram, actor, 0x9eb2);
            } else {
                write_word(ram, actor, 0x9fcd);
            }
        }
        write_word(ram, actor + 0x3c, read_word(main_rom, fields));
        write_word(ram, actor + 0x3a, read_word(main_rom, fields + 5));
        write_word(ram, actor + 0x26, read_word(main_rom, fields + 10));
        write_word(ram, actor + 0x36, read_word(main_rom, fields + 15));
        break;
    }
    case 0xa804c:
    case 0xa80b5:
    case 0xa80ea:
        if (!tiles || !update_powered_torpedo(ram, main_rom, tiles + 0x4000, actor, address)) return 0;
        break;
    case 0x9be7:
        if (!tiles)
            return 0;
        init_standard_torpedo(ram, main_rom, tiles + 0x4000, actor);
        break;
    case 0x9c33:
    case 0x9c82:
        if (!tiles || (ram[actor + 0x0b] & 0x80) || read_word(ram, 0x84) == 4 ||
            (address == 0x9c33 && !read_word(ram, actor + 0x42)))
            return 0;
        update_standard_torpedo(ram, main_rom, tiles + 0x4000, actor, address == 0x9c33);
        break;
    case 0xa933f:
        write_word(ram, actor + 0x3c, read_word(main_rom + 0xa0000, 0x9342));
        ram[actor + 0x2c] = 5;
        ram[actor + 0x2d] = 17;
        write_word(ram, actor + 0x42, 160 - 4 * read_word(ram, actor + 0x56));
        write_word(ram, actor, 0x935f);
        /* fall through */
    case 0xa935f:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) finish_animation(ram, actor);
        break;
    case 0xa937d: {
        unsigned column = read_word(ram, actor + 0x56);
        unsigned side = read_word(ram, actor + 0x1e);
        uint16_t glyph = read_word(ram, 0xb9f8 + 2 * column + (side ? 0 : 40));
        if (!glyph) {
            finish_animation(ram, actor);
            break;
        }
        if (glyph != 1) {
            write_word(ram, actor + 0x21, 104 + side + 16 * column);
            uint16_t child = effect_slot(ram, actor, 0x933f);
            if (child != 0x8480) write_word(ram, child + 2, 0xa000);
            write_word(ram, child + 0x2a, glyph);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x24, read_word(ram, actor + 0x24));
            write_word(ram, child + 0x56, column);
        }
        write_word(ram, actor + 0x58, glyph == 1 ? 0x9134 : 0x9122);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xa0000, glyph == 1 ? 0x93b8 : 0x93fb));
        write_word(ram, actor + 0x44, glyph == 1 ? 4 : 8);
        ram[actor + 0x2c] = 6;
        ram[actor + 0x2d] = 18;
        write_word(ram, actor, 0x940f);
    }
        /* fall through */
    case 0xa940f:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t index = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xa0000, read_word(ram, actor + 0x58) + index);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x912c + index));
            write_word(ram, actor + 0x40, index + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa9496:
    case 0xa94f8:
        write_word(ram, actor + 0x40, 0xfffe);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, address == 0xa9496 ? 0x94a5 : 0x9507);
        /* fall through */
    case 0xa94a5:
    case 0xa9507:
    case 0xa944b: {
        if (read_word(ram, 0x262) & 0x8000) {
            finish_animation(ram, actor);
            break;
        }
        if (decrement(ram, actor + 0x42)) break;
        uint16_t phase = read_word(ram, actor + 0x40) + 2;
        write_word(ram, actor + 0x40, phase);
        int fixed_slot = address == 0xa944b;
        unsigned span = fixed_slot ? 12 : 8;
        if (phase >= span) {
            finish_animation(ram, actor);
            break;
        }
        int high_slot = address == 0xa94f8 || address == 0xa9507;
        unsigned base = fixed_slot ? 0x9140 : high_slot ? 0x8f20 : 0x8f08;
        uint16_t slot = fixed_slot ? 5 : read_word(ram, actor + 0x50);
        if (!fixed_slot)
            slot = (slot & 0xff00) | ((slot + (high_slot ? 32 : 11)) & 255);
        const uint8_t *row = main_rom + 0xa0000 + base + phase;
        queue_palette(ram, slot & 63, read_word(row, 0), read_word(row, span));
        if (!fixed_slot) {
            unsigned dest = read_word(ram, 0x262) & 0x8800 ? slot : 0x9e06 + 8 * (slot & 63);
            ram[(uint16_t)(dest + 7)] = 5;
        }
        write_word(ram, actor + 0x42, read_word(row, 2 * span));
        break;
    }
    case 0xa955a:
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x9564);
        /* fall through */
    case 0xa9564:
        if (!decrement(ram, actor + 0x44)) {
            uint16_t step = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xa0000, 0x8efc + (step & 6)));
            write_word(ram, 0xb70c, read_word(main_rom + 0xa0000, 0x8f04 + (step & 2)));
            write_word(ram, actor + 0x40, step + 2);
        }
        break;
    case 0xa9164: {
        const uint8_t *bank = main_rom + 0xa0000;
        write_word(ram, actor + 0x2c, 0x0808);
        write_word(ram, actor + 0x21, 0x70);
        write_word(ram, actor + 0x24, 0x170);
        write_word(ram, actor + 0x2a, read_word(bank, 0x9179));
        uint16_t middle = mission_child(ram, actor, 0x91c8);
        write_word(ram, middle + 0x2c, 0x0808);
        write_word(ram, middle + 0x21, 0x70);
        write_word(ram, middle + 0x24, 0x100);
        write_word(ram, middle + 0x2a, read_word(bank, 0x919b));
        uint16_t bottom = mission_child(ram, actor, 0x91c8);
        write_word(ram, bottom + 0x2c, 0x0808);
        ram[bottom + 0x29] = 2;
        write_word(ram, bottom + 0x21, 0x70);
        write_word(ram, bottom + 0x24, 0x90);
        write_word(ram, bottom + 0x2a, read_word(bank, 0x91c1));
        write_word(ram, actor, 0x91c8);
        /* fall through */
    }
    case 0xa91c8:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0xa91ce:
        ram[actor + 0x2c] = 4;
        ram[actor + 0x2d] = 16;
        write_word(ram, actor + 0x38, 0x400);
        write_word(ram, actor + 0x46, 2);
        /* fall through */
    case 0xa91e5:
        start_mission_scroll(ram, main_rom + 0xa0000, actor);
        /* fall through */
    case 0xa923c:
        update_mission_scroll(ram, main_rom, actor);
        break;
    case 0xa9589:
        if (!tiles) return 0;
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x9593);
        /* fall through */
    case 0xa9593: {
        if (!tiles) return 0;
        if (decrement(ram, actor + 0x42)) break;
        uint16_t row = read_word(ram, actor + 0x40);
        uint16_t dest = 0x1648 + 2 * (uint16_t)((row >> 8) | (row << 8));
        draw_join_label(tiles, main_rom, dest, read_word(main_rom, 0x2109 + row));
        row += 2;
        write_word(ram, actor + 0x40, row);
        write_word(ram, actor + 0x42, 20);
        if (row >= 10) {
            write_word(ram, actor + 0x40, 0);
            write_word(ram, actor + 0x42, 324);
            write_word(ram, actor, 0x95d4);
        }
        break;
    }
    case 0xa95d4: {
        if (!tiles) return 0;
        if (decrement(ram, actor + 0x42)) break;
        uint16_t row = read_word(ram, actor + 0x40);
        do {
            draw_join_label(tiles, main_rom, 0x1648 + row, read_word(main_rom, 0x2113));
            row += 0x200;
        } while (row < 0x1400);
        write_word(ram, actor + 0x40, row);
        finish_animation(ram, actor);
        break;
    }
    case 0xafc1b:
    case 0xafce1: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned flags = ram[actor + 0x1e];
        uint16_t resource = address == 0xafc1b ? read_word(bank, 0xfc1c) : read_word(bank, 0xf160 + 2 * (flags & 3));
        world_acquire_resource(ram, actor, resource);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x26, 128);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xfd05));
        if (address == 0xafc1b) {
            ram[actor + 0x29] = flags & 1;
            write_word(ram, actor + 0x38, read_word(bank, 0xf140 + 2 * (flags & 7)));
            write_word(ram, actor + 0x42, read_word(bank, 0xf150 + (flags & 6)));
            write_word(ram, actor, 0xfc67);
        } else {
            write_word(ram, actor + 0x38, read_word(bank, 0xf178 + (flags & 12)));
            write_word(ram, actor + 0x3a, read_word(bank, 0xf17a + (flags & 12)));
            write_word(ram, actor, 0xfd29);
        }
        write_word(ram, actor + 0x44, 1);
        break;
    }
    case 0xba6d6: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned route = ram[actor + 0x1f] & 14;
        uint16_t script = read_word(bank, 0xa5f8 + route);
        if (read_word(bank, script) != 2) return 0;
        world_acquire_resource(ram, actor, read_word(bank, 0xa6d7));
        write_word(ram, actor + 0x0e, 19);
        ram[actor + 0x2d] = 32;
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xa6f4));
        write_word(ram, actor + 0x2a, read_word(bank, 0xa6f9));
        begin_linear_script(ram, main_rom, actor, script);
        if ((int16_t)read_word(ram, actor + 0x38) >= 0) ram[actor + 0x29] = 1;
        uint16_t child = bank_b_effect(ram, actor, 0xa7a6);
        write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        ram[child + 0x29] = ram[actor + 0x29];
        write_word(ram, actor, 0xa734);
        break;
    }
    case 0xba734: {
        int step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0 || read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        unsigned pose = ((ram[actor + 0x28] + 8) & 0xf0) >> 3;
        write_word(ram, actor + 0x40, pose);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0xa52e + pose));
        queue_sprite(ram, main_rom, actor, 1);
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_surface_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) register_surface_enemy(ram, actor);
        else {
            world_award_score(ram, main_rom, actor, hit);
            uint16_t effect = effect_slot(ram, actor, 0x6efe);
            write_word(ram, effect + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, effect + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, effect + 0x44, 0x5d);
            ram[effect + 0x2d] = 32;
            write_word(ram, effect + 0x56, 48);
            write_word(ram, effect + 0x58, 128);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xba7a6: {
        const uint8_t *bank = main_rom + 0xb0000;
        world_acquire_resource(ram, actor, read_word(bank, 0xa7a7));
        write_word(ram, actor + 0x0e, 19);
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        write_word(ram, actor + 0x3c, read_word(bank, 0xa7c0));
        write_word(ram, actor + 0x2a, read_word(bank, 0xa7c5));
        uint16_t config = 0xa59e + read_word(bank, 0xa55e + (ram[actor + 0x1e] & 32) + read_word(ram, 0x8a));
        write_word(ram, actor + 0x52, read_word(bank, config + 2));
        write_word(ram, actor + 0x54, read_word(bank, config + 4));
        write_word(ram, actor + 0x50, read_word(bank, 0xa5f0 + (ram[actor + 0x1f] & 14)));
        write_word(ram, actor, 0xa7fd);
    }
        /* fall through */
    case 0xba7fd: {
        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
        if (ram[parent + 0x0a]) {
            finish_animation(ram, actor);
            break;
        }
        write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, parent + 0x24) - 6);
        ram[actor + 0x2d] = main_rom[0xba6c6 + (read_word(ram, parent + 0x40) >> 1)];
        queue_sprite(ram, main_rom, actor, 1);
        if (!decrement(ram, actor + 0x50)) {
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor + 0x3a, -256);
            world_queue_sound(ram, 0x70);
            write_word(ram, actor, 0xa863);
        }
        break;
    }
    case 0xba863:
    case 0xba8e0:
    case 0xba968:
    case 0xba9ee:
    case 0xbaa8f: {
        if (!tiles) return 0;
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (address == 0xbaa8f) {
            if (read_word(ram, actor + 0x54)) {
                write_word(ram, actor + 0x38, read_word(ram, actor + 0x38) - read_word(ram, actor + 0x52));
                decrement(ram, actor + 0x54);
            }
            move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
            if (read_word(ram, actor + 0x42)) {
                decrement(ram, actor + 0x42);
                move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
            }
        } else move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        if (x < 64) {
            finish_animation(ram, actor);
            break;
        }
        if ((address == 0xba863 || address == 0xba9ee) && !decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0xa550 + frame);
            if (sprite != 0xffff) {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x44, 4);
                write_word(ram, actor + 0x40, frame + (address == 0xba863 ? 2 : -2));
            } else if (address == 0xba863) {
                write_word(ram, actor + 0x3a, -512);
                write_word(ram, actor, 0xa8e0);
            } else {
                write_word(ram, actor + 0x40, 0);
                write_word(ram, actor + 0x50, 1);
                write_word(ram, actor + 0x3a, read_word(bank, 0xa5d0 + (ram[actor + 0x1e] & 6)));
                write_word(ram, actor + 0x42, 16);
                write_word(ram, actor, 0xaa8f);
            }
        } else if (address == 0xbaa8f) {
            uint16_t frame = read_word(ram, actor + 0x40) + 64;
            if (frame >= 768) frame = 0;
            write_word(ram, actor + 0x40, frame);
            write_word(ram, actor + 0x2a, read_word(bank, 0xa558 + 2*(frame >> 8)));
        }
        queue_sprite(ram, main_rom, actor, 1);
        int impact = 0;
        if (address == 0xba863 || address == 0xba8e0) {
            ram[actor + 0x0d] = 0;
            world_surface_contact(ram, main_rom, actor);
            impact = !read_word(ram, actor + 0x26);
            if (!impact && address == 0xba8e0) {
                uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
                unsigned material = tiles[0x4000 + cell + 3] >> 3;
                if (material == 4) {
                    write_word(ram, actor + 0x3a, -256);
                    write_word(ram, actor + 0x56, read_word(bank, 0xa5c8 + (ram[actor + 0x1e] & 6)));
                    ram[actor + 0x2d] = 4;
                    write_word(ram, actor, 0xa968);
                } else impact = (1u << (material & 15)) & 0xff02;
            }
        } else {
            if (address == 0xbaa8f)
                impact = world_terrain_mask(ram, tiles + 0x4000, x - 16, y) & 0xff02;
            if (!impact) {
                ram[actor + 0x0d] = 2;
                impact = world_player_contact(ram, main_rom, actor) != 0;
                if (!impact) {
                    ram[actor + 0x0d] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    impact = !read_word(ram, actor + 0x26);
                }
            }
            if (!impact && address == 0xba968) {
                impact = world_terrain_mask(ram, tiles + 0x4000, x, y - 2) & 0xff02;
                if (!impact && !decrement(ram, actor + 0x56)) {
                    write_word(ram, actor + 0x44, 1);
                    write_word(ram, actor + 0x40, 4);
                    write_word(ram, actor, 0xa9ee);
                }
            }
            if (!impact && address == 0xbaa8f && !decrement(ram, actor + 0x50)) {
                write_word(ram, actor + 0x50, 3);
                uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0xbe41);
                write_word(ram, trail + 0x21, x - 4);
                write_word(ram, trail + 0x24, y);
                ram[trail + 0x2c] = 3;
            }
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xbab67: {
        const uint8_t *bank = main_rom + 0xb0000;
        int16_t limit = read_word(ram, actor + 0x1e) & 0xfff0;
        if ((int16_t)read_word(ram, 0xb700) >= limit) {
            finish_animation(ram, actor);
            break;
        }
        int16_t delay = read_word(ram, actor + 0x42) - 4;
        if (delay > 0) {
            uint16_t scroll = -read_word(ram, 0xb71a);
            delay -= scroll >> 4;
        }
        if (delay <= 0) {
            for (int offset = 6; offset >= 0; offset -= 2) {
                uint16_t child = bank_b_effect(ram, actor, 0xa6d6);
                if (child == 0x8480) continue;
                write_word(ram, child + 0x21, read_word(bank, 0xa5d8 + offset));
                write_word(ram, child + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xa5e0 + offset));
                write_word(ram, child + 0x1e, read_word(bank, 0xa5e8 + offset));
            }
            delay = 1024;
        }
        write_word(ram, actor + 0x42, delay);
        break;
    }
    case 0x2681: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1, stage = read_word(ram, 0x84);
        if (!stage || stage > 16) return 0;
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t child = effect_slot(ram, actor, 0x2b18);
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e) + 2*i);
        }
        unsigned earned = read_word(ram, 0x124 + 64*side);
        write_word(ram, actor + 0x48, earned);
        write_word(ram, actor + 0x42, 0);
        write_word(ram, actor + 0x40, 2*(stage - 1));
        for (unsigned i = 0; i + 1 < stage; ++i) {
            if (earned & 1) {
                uint16_t child = effect_slot(ram, actor, 0x2e60);
                write_word(ram, child + 0x1e, 2*i + read_word(ram, actor + 0x1e));
            }
            earned >>= 1;
        }
        write_word(ram, actor + 0x48, earned);
        if (!(ram[0x264] & (1u << side))) {
            write_word(ram, actor, 0x2abb);
            address = 0x2abb;
        } else {
            tally_score(ram, main_rom, tiles, side);
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) | (1u << side));
            unsigned small = read_word(ram, 0x120 + 64*side), large = read_word(ram, 0x122 + 64*side);
            write_word(ram, actor + 0x50, small >= 190 ? 189 : small);
            write_word(ram, actor + 0x52, large >= 30 ? 29 : large);
            write_word(ram, actor + 0x48, read_word(ram, 0x124 + 64*side));
            write_word(ram, actor + 0x42, 64);
            write_word(ram, actor + 0x4e, 10);
            write_word(ram, actor, 0x2756);
            address = 0x2756;
        }
    }
        /* fall through */
    case 0x2756:
    case 0x2abb: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1;
        if (address == 0x2abb) {
            uint16_t clock = read_word(ram, 0x274);
            if (clock & 31) break;
            uint16_t label = 0xc2f6;
            if (clock & 32) {
                unsigned credits = read_word(ram, 0x2b8);
                if (ram[0x264] & (1u << side)) label = read_word(main_rom, 0xc253 + 2*side);
                else if (ram[0x2c4] & 1) label = 0xc2e5;
                else if (!credits || ((ram[0x289] & 128) && credits < 2)) label = 0xc290;
                else label = read_word(main_rom, 0xc24f + 2*side);
            }
            tally_large_label(tiles, main_rom, read_word(main_rom, 0xc25b + 2*side), label, 0x106);
        } else {
            if (!decrement(ram, actor + 0x42)) {
                write_word(ram, actor + 0x42, 1);
                write_word(ram, actor, 0x2769);
            }
            if (tally_skip_requested(ram))
                tally_bonus(ram, main_rom, tiles, actor);
        }
        break;
    }
    case 0x2769:
    case 0x2811:
    case 0x28d3:
    case 0x290d:
    case 0x2942:
    case 0x29b8: {
        if (!tiles) return 0;
        unsigned side = ram[actor + 0x1e] & 1;
        unsigned stage = (read_word(ram, 0x84) - 1) & 15;
        int completed = 0;
        if (address == 0x2769 || address == 0x2811) {
            if (!decrement(ram, actor + 0x42)) {
                int large = address == 0x2811;
                uint16_t count = actor + (large ? 0x52 : 0x50);
                write_word(ram, actor + 0x42, 2);
                if (!read_word(ram, count)) {
                    write_word(ram, actor, large ? 0x28d3 : 0x2811);
                    if (large) {
                        uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
                        write_word(ram, parent + 0x46, read_word(ram, parent + 0x46) & ~(1u << side));
                    }
                } else {
                    decrement(ram, count);
                    tally_add_count(ram, main_rom, tiles, actor, large);
                    uint16_t partial = actor + (large ? 0x56 : 0x54);
                    unsigned item = read_word(ram, partial);
                    uint16_t dest = read_word(main_rom, (large ? 0x22d7 : 0x22bf) + 2*item) + side*0x1000;
                    if (large) tally_stamp(tiles, main_rom, dest, 0x2478, 0x107);
                    else for (uint16_t text = 0x2461; main_rom[text] != 255; ++text) {
                        tally_tile(tiles, dest, main_rom[text], 0x107);
                        dest = (dest & 0xff00) | ((dest + 4) & 255);
                    }
                    write_word(ram, partial, item + 1);
                    if (item + 1 == (large ? 5 : 10)) {
                        dest = read_word(main_rom, large ? 0x22d7 : 0x22bf) + side*0x1000;
                        draw_join_label(tiles, main_rom, dest, large ? 0x2494 : 0x2499);
                        draw_join_label(tiles, main_rom, dest + (large ? 256 : 512), large ? 0x2494 : 0x2499);
                        write_word(ram, partial, 0);
                        uint16_t groups = actor + (large ? 0x5a : 0x58);
                        write_word(ram, groups, read_word(ram, groups) + 1);
                        tally_groups(ram, main_rom, tiles, actor, large);
                    }
                }
            }
        } else if (address == 0x28d3) {
            uint16_t parent = read_word(ram, 0x84fe + read_word(ram, actor + 8));
            if (!read_word(ram, parent + 0x46)) {
                tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side), 0x248c, 0x110);
                write_word(ram, actor + 0x42, 32);
                write_word(ram, actor, 0x290d);
            }
        } else if (address == 0x290d) {
            if (!decrement(ram, actor + 0x42)) {
                int both = (ram[0x264] & 3) == 3;
                write_word(ram, actor + 0x42, both ? 16 : 2);
                write_word(ram, actor, both ? 0x2942 : 0x29b8);
                if (both) {
                    write_word(ram, actor + 0x40, 4);
                    write_word(ram, actor + 0x4a, 8);
                }
            }
        } else if (address == 0x2942) {
            if (!decrement(ram, actor + 0x42)) {
                unsigned frame = read_word(ram, actor + 0x40);
                uint16_t shape = read_word(main_rom, 0x2488 + 2*(frame & 1));
                tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side), shape, 0x110);
                if (!decrement(ram, actor + 0x40)) {
                    write_word(ram, actor + 0x40, 4);
                    unsigned delay = read_word(ram, actor + 0x4a) >> 1;
                    write_word(ram, actor + 0x4a, delay);
                    if (!delay) {
                        unsigned medal = (read_word(ram, actor + 0x48) >> stage) & 1;
                        tally_stamp(tiles, main_rom, read_word(main_rom, 0x22e5 + 2*side),
                                    read_word(main_rom, 0x2488 + 2*medal), 0x110);
                        write_word(ram, actor + 0x42, 2);
                        write_word(ram, actor, 0x29b8);
                    } else write_word(ram, actor + 0x42, delay);
                } else write_word(ram, actor + 0x42, read_word(ram, actor + 0x4a));
            }
        } else if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 2);
            if (read_word(ram, actor + 0x48) & (1u << stage))
                tally_add_count(ram, main_rom, tiles, actor, 2);
            if (!decrement(ram, actor + 0x4e)) {
                write_word(ram, actor + 0x42, 64);
                write_word(ram, 0xd2, 1);
                write_word(ram, actor, 0x2a10);
                completed = 1;
            }
        }
        if (!completed && tally_skip_requested(ram)) tally_bonus(ram, main_rom, tiles, actor);
        break;
    }
    case 0x2a10:
        if (!tiles) return 0;
        if (!decrement(ram, actor + 0x42)) {
            unsigned side = ram[actor + 0x1e] & 1;
            memset(tiles + read_word(main_rom, 0x22e9 + 2*side), 0, 0xe00);
            tally_score(ram, main_rom, tiles, side);
            write_word(ram, actor + 0x42, 32);
            write_word(ram, actor, 0x2a3d);
        }
        break;
    case 0x2a3d:
        if (!decrement(ram, actor + 0x42)) {
            unsigned stage = read_word(ram, 0x84) - 1;
            if (read_word(ram, actor + 0x48) & (1u << (stage & 15))) {
                uint16_t child = effect_slot(ram, actor, 0x2c04);
                write_word(ram, child + 0x1e, read_word(main_rom, 0x22ed + 2*(2*stage + read_word(ram, actor + 0x1e))));
                write_word(ram, actor + 0x2e, 0x600);
                write_word(ram, actor + 0x38, 0x1000);
            }
            write_word(ram, actor, 0x2a7c);
        }
        break;
    case 0x2a7c: {
        uint16_t amplitude = read_word(ram, actor + 0x2e);
        if (amplitude) {
            uint16_t x = read_word(ram, actor + 0x21) + 2, speed = read_word(ram, actor + 0x38);
            write_word(ram, actor + 0x21, x);
            unsigned angle = ((uint32_t)x * speed >> 8) & 255;
            int16_t unit = read_word(main_rom + 0xa0000, 0x9d92 + 2*angle);
            uint32_t y = ram[actor + 0x33] + 256u*read_word(ram, actor + 0x34) + unit*(amplitude >> 8);
            ram[actor + 0x23] = y;
            write_word(ram, actor + 0x24, y >> 8);
            write_word(ram, actor + 0x38, speed - 9);
            if ((int16_t)amplitude > 0x300) write_word(ram, actor + 0x2e, amplitude - 32);
        }
        write_word(ram, ram[actor + 0x1e] & 1 ? 0xb710 : 0xb70c, read_word(ram, actor + 0x24));
        break;
    }
    case 0x2b18: {
        unsigned side = ram[actor + 0x1e] & 3;
        ram[actor + 0x2d] = 63;
        ram[actor + 0x2c] = main_rom[0x2417 + side];
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x241b + 2*side));
        write_word(ram, actor + 0x21, read_word(main_rom, 0x2423 + 4*side));
        write_word(ram, actor + 0x34, read_word(main_rom, 0x2425 + 4*side));
        write_word(ram, actor, 0x2b4c);
    }
        /* fall through */
    case 0x2b4c:
        write_word(ram, actor + 0x24, read_word(ram, (ram[actor + 0x1e] & 1) ? 0xb710 : 0xb70c) + read_word(ram, actor + 0x34));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x2b68:
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x2b6b));
        ram[actor + 0x2c] = 0x84;
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x2301 + 2*read_word(ram, actor + 0x1e)));
        write_word(ram, actor + 0x21, 256);
        write_word(ram, actor + 0x24, 256);
        write_word(ram, actor + 0x42, 16);
        write_word(ram, actor, 0x2b96);
        /* fall through */
    case 0x2b96:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            write_word(ram, actor + 0x42, 1);
            write_word(ram, actor, 0x2bab);
        }
        break;
    case 0x2bab:
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) {
            unsigned index = 2*read_word(ram, actor + 0x1e);
            write_word(ram, actor + 0x31, 256);
            write_word(ram, actor + 0x34, 256);
            write_word(ram, actor + 0x1a, read_word(main_rom, 0x2329 + index));
            write_word(ram, actor + 0x1c, read_word(main_rom, 0x233d + index));
            write_word(ram, actor + 0x42, 64);
            write_word(ram, actor, 0x2bdf);
        }
        break;
    case 0x2bdf: {
        uint16_t phase = read_word(ram, actor + 0x1a) - 256;
        write_word(ram, actor + 0x1a, phase);
        for (unsigned i = 0; i < 2; ++i) {
            uint16_t pos = actor + 0x20 + 3*i, center = pos + 16;
            int16_t unit = read_word(main_rom + 0xa0000, (i ? 0x9d92 : 0x9e12) + 2*(phase >> 8));
            uint32_t coord = ram[center] + 256u*read_word(ram, center + 1) + unit*(int16_t)read_word(ram, actor + 0x1c);
            ram[pos] = coord;
            write_word(ram, pos + 1, coord >> 8);
        }
        write_word(ram, actor + 0x24, 256);
        queue_sprite(ram, main_rom, actor, 0);
        if (!decrement(ram, actor + 0x42)) write_word(ram, actor, 0x2bfe);
        break;
    }
    case 0x2c04: {
        world_acquire_resource(ram, actor, 0x99);
        world_acquire_resource(ram, actor, 0);
        unsigned index=4*(read_word(ram,actor+0x1e)&15);
        uint16_t x=read_word(main_rom,0x23ef+index), y=read_word(main_rom,0x23f1+index);
        write_word(ram,actor+0x21,x);
        write_word(ram,actor+0x24,y);
        for (unsigned layer=0;layer<2;++layer) {
            uint16_t child=effect_slot(ram,actor,layer ? 0x2de7 : 0x2d6e);
            write_word(ram,child+0x21,x);
            write_word(ram,child+0x34,y);
            write_word(ram,child+0x1e,read_word(ram,actor+0x1e));
            if (!layer) world_queue_sound(ram,0x67);
        }
        for (int i=4;i>=0;--i) {
            uint16_t part=effect_slot(ram,actor,0x2d0d);
            ram[part+0x2c]=ram[actor+0x14];
            ram[part+0x29]=main_rom[0x23a9+i];
            ram[part+0x28]=main_rom[0x23ae + i];
            write_word(ram,part+0x21,x+read_word(main_rom,0x23b3+4*i));
            write_word(ram,part+0x24,y+read_word(main_rom,0x23b5+4*i));
        }
        write_word(ram,actor+0x42,0);
        ram[actor+0x2d]=15;
        write_word(ram,actor+0x44,1);
        write_word(ram,actor,0x2cc7);
        break;
    }
    case 0x2d0d:
        ram[actor+0x2d]=15;
        write_word(ram,actor+0x2e,0x300);
        write_word(ram,actor+0x44,1);
        digit_fragment_velocity(ram,main_rom,actor);
        write_word(ram,actor,0x2d25);
        /* fall through */
    case 0x2d25:
    case 0x2cc7: {
        int particle=address!=0x2cc7;
        if (particle) {
            move_actor_axis(ram,actor+0x20,read_word(ram,actor+0x38));
            move_actor_axis(ram,actor+0x23,read_word(ram,actor+0x3a));
            write_word(ram,actor+0x3a,read_word(ram,actor+0x3a)-32);
        }
        if (!decrement(ram,actor+0x44)) {
            uint16_t frame=read_word(ram,actor+0x40);
            uint16_t sprite=read_word(main_rom,(particle?0x237f:0x2351)+frame);
            if(sprite==0xffff) {finish_animation(ram,actor);break;}
            write_word(ram,actor+0x2a,sprite);
            write_word(ram,actor+0x44,read_word(main_rom,(particle?0x2395:0x2369)+frame));
            write_word(ram,actor+0x40,frame+2);
            if (!particle && frame==2) ram[actor+0x2c]=ram[actor+0x14];
        }
        queue_sprite(ram,main_rom,actor,0);
        break;
    }
    case 0x2d6e:
    case 0x2de7: {
        int back=address==0x2de7;
        write_word(ram,actor+0x3c,0xfb4b);
        acquire_private_palette(ram,actor,back?0x99:1);
        ram[actor+0x2d]=back?17:16;
        unsigned index=2*(read_word(ram,actor+0x1e)&15);
        write_word(ram,actor+0x2a,read_word(main_rom,(back?0x23db:0x23c7)+index));
        write_word(ram,actor+0x42,1);
        write_word(ram,actor,back?0x2e14:0x2d9b);
        break;
    }
    case 0x2d9b:
    case 0x2e14: {
        unsigned side=ram[actor+0x1e]&1;
        write_word(ram,actor+0x24,read_word(ram,actor+0x34)+read_word(ram,side?0xb710:0xb70c));
        queue_sprite(ram,main_rom,actor,0);
        if (!decrement(ram,actor+0x42)) {
            uint16_t frame=read_word(ram,actor+0x40);
            unsigned table=address==0x2d9b?0x2433+12*side:0x244b+6*side;
            write_word(ram,actor+0x42,read_word(main_rom,table+frame+4));
            queue_palette(ram,ram[actor+0x2c]&63,read_word(main_rom,table+frame),read_word(main_rom,table+frame+2));
            write_word(ram,actor+0x40,frame+6);
        }
        break;
    }
    case 0x2e60:
    case 0x2ee1: {
        unsigned index = read_word(ram, actor + 0x1e) & 15;
        unsigned side = index & 1;
        int back = address == 0x2ee1;
        write_word(ram, actor + 0x3c, 0xfb4b);
        ram[actor + 0x2c] = main_rom[(back ? 0x24aa : 0x24a8) + side];
        ram[actor + 0x2d] = back ? 17 : 16;
        write_word(ram, actor + 0x2a, read_word(main_rom, (back ? 0x23db : 0x23c7) + 2*index));
        if (!back) {
            write_word(ram, actor + 0x21, read_word(main_rom, 0x23ef + 4*index));
            write_word(ram, actor + 0x34, read_word(main_rom, 0x23f1 + 4*index));
            uint16_t child = effect_slot(ram, actor, 0x2ee1);
            write_word(ram, child + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, child + 0x34, read_word(ram, actor + 0x34));
            write_word(ram, child + 0x1e, read_word(ram, actor + 0x1e));
        }
        write_word(ram, actor, 0x2ec5);
        /* fall through */
    }
    case 0x2ec5:
        write_word(ram, actor + 0x24,
                   read_word(ram, (ram[actor + 0x1e] & 1) ? 0xb710 : 0xb70c) +
                   read_word(ram, actor + 0x34));
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x2bfe:
        queue_sprite(ram, main_rom, actor, 0);
        break;
    case 0x621c:
        write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + read_word(main_rom, 0x6214 + (ram[actor + 0x1e] & 6)));
        write_word(ram, actor, 0x1b4c);
        write_word(ram, actor + 2, 0);
        break;
    case 0xaf95d: {
        const uint8_t *bank = main_rom + 0xa0000;
        unsigned variant = ram[actor + 0x1e] & 6;
        uint16_t init = read_word(bank, 0xf93d + variant);
        world_acquire_resource(ram, actor, read_word(bank, 0xf95e));
        world_acquire_resource(ram, actor, read_word(bank, 0xf966));
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x38;
        write_word(ram, actor + 0x36, 0x200);
        write_word(ram, actor + 0x38, 0xffe0);
        write_word(ram, actor + 0x3c, read_word(bank, init + 3));
        write_word(ram, actor + 0x2a, read_word(bank, init + 8));
        write_word(ram, actor + 0x26, read_word(bank, init + 13));
        if (ram[actor + 0x1e] & 1) {
            uint16_t child = bank_b_effect(ram, actor, 0x29e2);
            write_word(ram, child + 0x1e, 3);
            write_word(ram, child + 0x34, read_word(bank, 0xf955 + variant));
        }
        write_word(ram, actor, 0xf9bb);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
