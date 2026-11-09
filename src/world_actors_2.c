/* Recovered World actor handlers, part 2 of 6 (split from world_dispatch.c).
 * Each case is the linear address of an actor handler entry. */
#include "world_dispatch_internal.h"
#include <stdlib.h>
#include <string.h>

int world_actors_2(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    case 0xe234:
        if (read_word(ram, actor + 0x3c) != 0xffff)
            world_append_candidate(ram + 0xa6e2, actor);
        write_word(ram, actor + 0x2e, 0x450);
        ram[actor + 0x4e] = 1;
        write_word(ram, actor + 0x62, 1);
        write_word(ram, actor + 0x60, 0);
        write_word(ram, actor + 0x4c, 0xdd8f);
        write_word(ram, actor, 0xe256);
        /* fall through */
    case 0xe256:
    case 0xe2e5: {
        int flying = address != 0xe2e5;
        if (flying && !--ram[actor + 0x4e]) {
            ram[actor + 0x4e] = 3;
            digit_fragment_velocity(ram, main_rom, actor);
            unsigned speed = read_word(ram, actor + 0x2e);
            write_word(ram, actor + 0x4c, read_word(main_rom, 0xf0ef + 2*(speed/157)));
            write_word(ram, actor + 0x2e, speed - 48);
            if (speed < 48) { finish_animation(ram, actor); break; }
        }
        if (!decrement(ram, actor + 0x62)) {
            write_word(ram, actor + 0x62, flying ? 2 : 3);
            unsigned index = read_word(ram, actor + 0x60) + 2;
            write_word(ram, actor + 0x60, index >= 24 ? 0 : index);
        }
        uint16_t table = flying ? read_word(ram, actor + 0x4c) : 0xdd8f;
        write_word(ram, actor + 0x2a, read_word(main_rom, table + read_word(ram, actor + 0x60)));
        if (!flying) write_word(ram, actor + 0x3a, read_word(ram, actor + 0x3a)-64);
        queue_sprite(ram, main_rom, actor, 1);
        if (flying) move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        int16_t top = ram[0x26e]&2 ? 0x68 : read_word(ram, 0xb732)+16;
        if (!flying && (int16_t)read_word(ram, actor + 0x24)<top) finish_animation(ram, actor);
        break;
    }
    case 0xe2c9:
        queue_sprite(ram, main_rom, actor, 1);
        if (!--ram[actor + 0x4e]) {
            write_word(ram, actor + 0x62, 1);
            write_word(ram, actor + 0x60, 0);
            write_word(ram, actor, 0xe2e5);
        }
        break;
    case 0xf1b5:
        if (!--ram[actor + 0x4e]) {
            ram[actor + 0x28] = vector_angle(main_rom,
                read_word(ram, actor+0x31) - read_word(ram, actor+0x21),
                read_word(ram, actor+0x34) - read_word(ram, actor+0x24));
            write_word(ram, actor + 0x2e, 0xc00);
            digit_fragment_velocity(ram, main_rom, actor);
            write_word(ram, actor, 0xf1df);
        }
        break;
    case 0xf1df:
        move_actor_axis(ram, actor+0x20, read_word(ram, actor+0x38));
        move_actor_axis(ram, actor+0x23, read_word(ram, actor+0x3a));
        if (((read_word(ram, actor+0x31)-read_word(ram, actor+0x21)) ^ read_word(ram, actor+0x38)) &
            ((read_word(ram, actor+0x34)-read_word(ram, actor+0x24)) ^ read_word(ram, actor+0x3a)) & 0x8000) {
            write_word(ram, actor+0x21, read_word(ram, actor+0x31));
            write_word(ram, actor+0x24, read_word(ram, actor+0x34));
            write_word(ram, actor, 0xf210);
        }
        /* fall through */
    case 0xf210:
        queue_sprite(ram, main_rom, actor, 1);
        break;
    case 0xe06b:
    case 0xe09d: {
        uint16_t parent = read_word(ram, actor + 0x54);
        if (address == 0xe06b) {
            world_acquire_resource(ram, actor, 0xfe);
            world_acquire_resource(ram, actor, 0xfd);
            write_word(ram, actor + 0x14, read_word(ram, actor + 0x14) | 0x8080);
            write_word(ram, actor + 0x3c, 0xfb4b);
            write_word(ram, actor + 0x60, 8);
            write_word(ram, actor + 0x62, 1);
            write_word(ram, actor + 0x64, 136);
            write_word(ram, actor, 0xe09d);
        }
        if (read_word(ram, parent) == 0xe00e) {
            write_word(ram, actor + 0x62, 0);
            write_word(ram, actor + 0x60, 0);
        } else if (read_word(ram, parent) == 0xe039 && !read_word(ram, actor+0x60)) {
            write_word(ram, actor + 0x62, 0);
        }
        if (ram[actor+10] == 1) { shatter_continue_digit(ram, main_rom, actor); break; }
        if (ram[actor+10] || !read_word(ram, 0x286)) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x64)) {
            if (read_word(ram, actor + 0x4c) == 0x7e) { shatter_continue_digit(ram, main_rom, actor); break; }
            finish_animation(ram, actor);
            break;
        }
        memcpy(ram + actor + 0x42, ram + parent + 0x42, 3);
        write_word(ram, actor + 0x46, read_word(ram, parent + 0x46));
        project_continue_digit(ram, main_rom, actor);
        break;
    }
    case 0xdecd:
        write_word(ram, actor, 0xded2);
        /* fall through */
    case 0xded2:
    case 0xdf93:
    case 0xe00e:
    case 0xe039: {
        uint16_t state = read_word(ram, actor + 0x40);
        uint16_t child = read_word(ram, actor + 0x56);
        int active = read_word(ram, 0x286) != 0;
        if (address == 0xdf93 && !state) {
            for (int i = 0; i < 3; ++i) ram[actor + 0x42 + i] += ram[actor + 0x50 + i];
            uint16_t distance = read_word(ram, actor + 0x46) - 12;
            write_word(ram, actor + 0x46, distance);
            if (distance >= 210) break;
            write_word(ram, actor, 0xe039);
            write_word(ram, actor + 0x46, 210);
            memset(ram + actor + 0x42, 0, 3);
            write_word(ram, actor + 0x40, 0);
            break;
        }
        if (address == 0xe039 && !state) break;
        if (address == 0xe00e && active && --ram[actor + 0x4e]) break;
        if (address == 0xded2 || address == 0xdecd) {
            ram[actor + 0x58] = 0;
            if (!state || state == 0xffff) break;
            ram[actor + 0x58] = 0xff;
            uint16_t banner = effect_slot(ram, actor, 0xde69);
            write_word(ram, banner + 0x54, actor);
            write_word(ram, actor + 0x4c, 0);
        } else if (address == 0xdf93 && active) {
            write_word(ram, actor, 0xe00e);
            ram[actor + 0x4e] = 6;
            write_word(ram, actor + 0x46, 210);
            memset(ram + actor + 0x42, 0, 3);
            write_word(ram, actor + 0x40, 0);
            break;
        } else ram[child + 0x0a] = 0xff;
        write_word(ram, actor + 0x40, 0);
        if (!active) {
            write_word(ram, actor, 0xded2);
            ram[child + 0x0a] = 0xff;
            break;
        }
        uint16_t row = read_word(ram, actor + 0x4c);
        if ((int16_t)row >= 140) {
            write_word(ram, actor, 0xded2);
            ram[child + 0x0a] = 1;
            break;
        }
        write_word(ram, actor, 0xdf93);
        child = effect_slot(ram, actor, 0xe06b);
        write_word(ram, actor + 0x56, child);
        write_word(ram, child + 0x54, actor);
        write_word(ram, child + 0x3c, read_word(main_rom, 0xdf49));
        write_word(ram, child + 0x4c, row);
        write_word(ram, child + 0x48, read_word(main_rom, 0xe54b + row));
        for (int i = 0; i < 3; ++i) {
            ram[actor + 0x42 + i] = main_rom[0xe553 + row + 2*i];
            ram[actor + 0x50 + i] = main_rom[0xe54d + row + 2*i];
        }
        write_word(ram, actor + 0x4c, row + 14);
        write_word(ram, actor + 0x46, 1000);
        break;
    }
    case 0xfeee:
        write_word(ram, actor + 0x42, 128);
        write_word(ram, actor + 0x44, 64);
        write_word(ram, actor, 0x60ce);
        /* fall through */
    case 0x60ce:
        if (!read_word(ram, 0x286) && !(ram[0x262] & 0x80)) {
            if (read_word(ram, actor + 0x42) == 1) {
                unsigned seconds = read_word(ram, 0xb8a0);
                /* Entering the refresh with zero would fault at the guest DIV. */
                if (!tiles || !seconds)
                    return 0;
                write_word(ram, actor + 0x42, 128);
                seconds = (seconds >= 100 ? 99 : seconds) - 1;
                write_word(ram, 0xb8a0, seconds);
                if (!seconds)
                    expire_stage_time(ram, main_rom, actor);
                refresh_stage_time(ram, tiles, actor, seconds);
            } else {
                decrement(ram, actor + 0x42);
            }
        }
        if (!decrement(ram, actor + 0x44)) {
            write_word(ram, actor + 0x44, 64);
            if (!(ram[0x262] & 2)) {
                uint16_t frame = read_word(ram, actor + 0x40);
                unsigned index = frame & 2;
                unsigned speed = 6;
                if (read_word(ram, 0xb8a0) < 10) {
                    index += 4;
                    speed = 5;
                    write_word(ram, actor + 0x44, 32);
                }
                queue_palette(ram, 6, read_word(main_rom, 0x60c6 + index), speed);
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        break;
    case 0x7b3c:
    case 0x7b6f: {
        unsigned owner = ram[actor + 0x0b] & 3;
        if (!tiles || !owner || owner > 2 || read_word(ram, 0x84) == 4 ||
            (ram[0x262] & 0x40) || (ram[0xb8b2] & (1u << (owner & 2)))) return 0;
        uint16_t config = read_word(ram, actor + 0x70);
        unsigned side = 2 * (owner & 2);
        int16_t y = (int16_t)read_word(ram, 0xb736 + side);
        int16_t ceiling = (int16_t)read_word(ram, 0x25c);
        if (address == 0x7b3c) {
            write_word(ram, config + 0x26, read_word(ram, 0x84));
            write_word(ram, actor + 0x21, read_word(ram, 0xb734 + side));
            write_word(ram, actor + 0x24, y <= ceiling ? y : ceiling);
            write_word(ram, actor + 0x42, 127);
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 30);
        }
        if (!read_word(ram, config + 0x18))
            write_word(ram, config + 0x18, read_word(main_rom, 0x78e3 + 2 * (ram[0x278] & 3)));
        uint16_t score = read_word(ram, 0x254 + 2 * (owner - 1));
        if (read_word(ram, actor + 0x6e)) {
            unsigned a = ram[score], b = main_rom[0xc307];
            unsigned sum = (a >> 4) * 10 + (a & 15) + (b >> 4) * 10 + (b & 15);
            ram[score] = (uint8_t)((sum / 10 % 10) * 16 + sum % 10);
        } else if (ram[0x264] & (1u << (owner + 5))) {
            ram[0x264] &= ~(1u << (owner + 5));
            for (unsigned i = 0; i < 8; ++i) ram[(uint16_t)(score + i)] = 0;
            write_word(ram, config + 0x1a, 0);
            write_word(ram, config + 0x1c, 0);
            write_word(ram, config + 0x24, 0);
            write_word(ram, config + 0x20, 0);
            write_word(ram, config + 0x22, 0);
        }
        draw_join_label(tiles, main_rom, owner == 1 ? 0x2d30 : 0x2d90, owner == 1 ? 0xc26b : 0xc27c);
        world_sub_lives_hud(main_rom, tiles, owner, read_word(ram, config + 0x18));
        ram[actor + 0x28] = 255;
        write_word(ram, actor + 0x2e, read_word(ram, 0xa2));
        ram[actor + 0x29] = 0;
        write_word(ram, actor + 0x40, 0);
        for (unsigned i = 0x58; i <= 0x5e; i += 2) write_word(ram, actor + i, 0);
        write_word(ram, actor + 0x44, 2);
        write_word(ram, actor + 0x46, 2);
        write_word(ram, actor + 0x48, 0);
        write_word(ram, actor + 0x56, 4);
        write_word(ram, actor + 0x54, 0);
        write_word(ram, actor + 0x64, 6);
        write_word(ram, actor + 0x66, 24);
        write_word(ram, actor + 0x2a, read_word(main_rom, 0x7bd9));
        write_word(ram, actor + 0x6e, 1);
        write_word(ram, actor, 0x7be5);
    }
        /* fall through */
    case 0x7be5:
        if (!tiles) return 0;
        if ((int16_t)read_word(ram, actor + 0x42) > 96) {
            ram[0xb73e] = (uint8_t)(1u << (ram[0x26e] & 3));
            ram[0xb742] = 0;
        }
        if (ram[actor + 0xa]) {
            if (read_word(ram, actor + 0x6e)) {
                memset(ram + actor + 0x58, 0, 8);
                world_sub_entry_step(ram, main_rom, tiles, actor);
            } else if (!world_sub_open_water(ram, main_rom, tiles, actor)) {
                return 0;
            }
            break;
        }
        write_word(ram, actor + 0x3c, read_word(main_rom, 0x7c1e));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 256);
        if (read_word(ram, actor + 0x6e)) write_word(ram, actor + 0x42, 1);
        write_word(ram, actor, 0x7c3b);
        if (read_word(ram, actor + 0x6e))
            world_sub_entry_step(ram, main_rom, tiles, actor);
        else if (!world_sub_open_water(ram, main_rom, tiles, actor))
            return 0;
        break;
    case 0x7c3b:
        if (read_word(ram, 0x2a4) || (read_word(ram, 0x262) & 0x8080)
            || ram[actor + 0xa] || read_word(ram, actor + 0x26)
            || (read_word(ram, 0xb8b2) & (1u << (ram[actor + 0xb] & 2)))) {
            if (!tiles || !world_sub_open_water(ram, main_rom, tiles, actor)) return 0;
            break;
        }
        /* fall through */
    case 0x7e5e: {
        ram[actor + 0xa] = 1;
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x42, 1);
        write_word(ram, actor + 0x60, 0);
        write_word(ram, actor + 0x6c, 1);
        ram[actor + 0x2c] = ram[actor + 0x15];
        unsigned player = ((ram[actor + 0xb] & 3) - 1) & 15;
        write_word(ram, 0x264, read_word(ram, 0x264) & ~(1u << player));
        uint16_t stats = read_word(ram, actor + 0x70);
        memset(ram + stats + 0x10, 0, 8);
        uint16_t blast = effect_slot(ram, actor, 0x6efe);
        write_word(ram, blast + 0x4c, read_word(main_rom, 0x6b77));
        write_word(ram, blast + 0x4a, read_word(main_rom, 0x6b7c));
        ram[blast + 0x2c] = 10;
        ram[blast + 0x2d] = 0x20;
        write_word(ram, blast + 0x56, 0x40);
        write_word(ram, blast + 0x58, 0);
        world_queue_sound(ram, 0x48);
        write_word(ram, actor, 0x7eb1);
    }
        /* fall through */
    case 0x7eb1:
        if (!decrement(ram, actor + 0x42)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom, 0x77e5 + frame);
            if (sprite == 0xffff) {
                write_word(ram, actor + 0x42, 16);
                write_word(ram, actor + 0x6c, 0);
                write_word(ram, actor, 0x7ee5);
            } else {
                write_word(ram, actor + 0x2a, sprite);
                write_word(ram, actor + 0x42, read_word(main_rom, 0x7803 + frame));
                write_word(ram, actor + 0x40, frame + 2);
            }
        }
        break;
    case 0x7ee5: {
        uint16_t stats = read_word(ram, actor + 0x70);
        if (!tiles && read_word(ram, actor + 0x42) == 1 && read_word(ram, stats + 0x18) == 1)
            return 0;
        if (decrement(ram, actor + 0x42)) break;
        unsigned owner = ram[actor + 0xb] & 3;
        if (decrement(ram, stats + 0x18)) {
            ram[0x264] |= 1u << ((owner - 1) & 7);
            write_word(ram, actor + 0x7a, 1);
            write_word(ram, actor + 0x3c, read_word(main_rom, 0x7f08));
            write_word(ram, actor + 0x36, 4);
            ram[actor + 0xa] = 1;
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 30);
            world_queue_sound(ram, 0x5f);
            write_word(ram, actor, 0x7b6f);
        } else {
            write_word(ram, actor + 0x6e, 1);
            ram[0x288] |= 1u << (owner >> 1);
            ram[0x264] &= ~(1u << (2 + (owner >> 1)));
            uint16_t label = read_word(main_rom, 0xc257 + 2 * (owner - 1));
            draw_join_label(tiles, main_rom, label, 0xc2f6);
            write_word(ram, actor, owner & 1 ? 0x7a6a : 0x7b08);
            write_word(ram, actor + 0x42, 0x601);
            draw_join_label(tiles, main_rom, label, 0x78ef);
        }
        break;
    }
    case 0x79fb:
    case 0x7a9e: {
        unsigned player = address == 0x79fb ? 1 : 2;
        int active = ram[0x264] & (2u << player);
        if (!active && !tiles) return 0;
        init_player_slot(ram, main_rom, actor, player);
        if (active) {
            ram[actor + 0x0a] = 0;
            write_word(ram, actor + 0x3c, 0x78d3);
            if (player == 1) write_word(ram, actor + 0x36, 0x100);
            write_word(ram, actor, 0x7b3c);
            break;
        }
        address = player == 1 ? 0x7a6a : 0x7b08;
        write_word(ram, actor, (uint16_t)address);
    }
        /* fall through */
    case 0x7a6a:
    case 0x7b08: {
        if (!tiles)
            return 0;
        uint16_t sub = read_word(ram, address == 0x7a6a ? 0x92 : 0x90);
        write_word(ram, actor + 0x21, read_word(ram, sub + 0x21));
        write_word(ram, actor + 0x24, read_word(ram, sub + 0x24));
        update_join_hud(ram, main_rom, tiles, actor);
        if (ram[0x264] & (address == 0x7a6a ? 4 : 8)) {
            write_word(ram, actor + 0x7a, 2);
            write_word(ram, actor + 0x3c, 0x79f1);
            write_word(ram, actor + 0x36, 4);
            ram[actor + 0x0a] = 1;
            write_word(ram, actor, 0x7b3c);
        }
        break;
    }
    case 0x3c68:
    case 0x3c88:
        if (!tiles || (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
        if (address == 0x3c68) {
            for (unsigned pos = 0xb716; pos <= 0xb720; pos += 2) write_word(ram, pos, 0);
            for (unsigned pos = 0xb726; pos <= 0xb72c; pos += 2) write_word(ram, pos, 0);
        }
        write_word(ram, 0x270, 0);
        write_word(ram, actor, 0x3ca4);
        update_camera_layers(ram, main_rom, tiles, actor, 0, 0);
        break;
    case 0x3c96:
        if (!tiles || (read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
        write_word(ram, 0x270, 0);
        write_word(ram, actor, 0x3ca1);
        update_camera_layers(ram, main_rom, tiles, actor, 0, 1);
        break;
    case 0x3ca4:
    case 0x3ca1:
    case 0x3f68:
        if (!tiles || ((address == 0x3ca1 || address == 0x3ca4) &&
                       read_word(ram, 0x2a2) && (read_word(ram, 0x2a8) & 0x100)))
            return 0;
        update_camera_layers(ram, main_rom, tiles, actor, address == 0x3f68, address == 0x3ca1);
        break;
    case 0x57e3:
    case 0x57fc: {
        int first = address == 0x57e3;
        uint16_t row = first ? read_word(main_rom, 0x56af + 2 * read_word(ram, 0x84))
                             : read_word(ram, actor + 0x40);
        int due = first || read_word(ram, actor + 0x42) <= read_word(ram, 0xb700);
        uint16_t callback = due ? read_word(main_rom, row + 16) : read_word(ram, actor + 0x44);
        if (callback != 0xffff)
            return 0;
        if (due) {
            write_word(ram, 0x29a, read_word(ram, 0x29a) | 7);
            for (unsigned i = 0; i < 3; i++)
                write_word(ram, 0x29c + 2 * i, read_word(main_rom, row + 2 + 2 * i));
            for (unsigned i = 0; i < 4; i++)
                write_word(ram, 0x292 + 2 * i, read_word(main_rom, row + 8 + 2 * i));
            write_word(ram, actor + 0x44, callback);
            write_word(ram, actor + 0x40, row + 18);
            uint16_t next = read_word(main_rom, row + 18);
            if (next == 0xffff)
                finish_animation(ram, actor);
            write_word(ram, actor + 0x42, next);
            if (first) write_word(ram, actor, 0x57fc);
        }
        break;
    }
    case 0x9eb2:
    case 0x9ec4:
    case 0x9fcd:
    case 0x9fda:
    case 0xa0a9:
        if (!tiles || !update_vertical_weapon(ram, main_rom, tiles + 0x4000, actor, address))
            return 0;
        break;
    case 0xba0f4: {
        const uint8_t *bank = main_rom + 0xb0000;
        for (unsigned i = 0; i < 3; i++)
            world_acquire_resource(ram, actor, read_word(bank, 0xa0f5 + 8 * i));
        write_word(ram, actor + 0x0e, 0x12);
        write_word(ram, actor + 0x2a, read_word(bank, 0xa114));
        ram[actor + 0x2d] = bank[0xa119];
        write_word(ram, actor + 0x3c, read_word(bank, 0xa11d));
        uint16_t flags = read_word(ram, actor + 0x1e);
        ram[actor + 0x29] = flags & 2;
        uint16_t index = read_word(bank, 0xa0a4 + (flags & 0x20) + read_word(ram, 0x88));
        uint16_t speed = read_word(bank, 0xa0e4 + index);
        uint16_t delay = read_word(bank, 0xa0e6 + index);
        write_word(ram, actor + 0x26, speed);
        write_word(ram, actor + 0x36, speed);
        write_word(ram, actor + 0x50, delay);
        write_word(ram, actor + 0x52, delay);
        write_word(ram, actor, 0xa152);
        break;
    }
    case 0xba152: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 1);
        int fire;
        if (read_word(ram, actor + 0x50)) {
            fire = !decrement(ram, actor + 0x50);
            if (fire) write_word(ram, actor + 0x56, 4);
        } else fire = !decrement(ram, actor + 0x54);
        if (fire) {
            write_word(ram, actor + 0x54, 16);
            uint16_t flash = bank_b_effect(ram, read_word(ram, 0x9a), 0xa4bd);
            write_word(ram, flash + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, flash + 0x24, read_word(ram, actor + 0x24));
            ram[flash + 0x29] = ram[actor + 0x29];
            ram[flash + 0x2c] = ram[actor + 0x15];
            uint16_t shot = bank_b_effect(ram, actor, 0xa2ec);
            unsigned direction = ram[actor + 0x29] & 2;
            write_word(ram, shot + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, shot + 0x24, read_word(ram, actor + 0x24) + read_word(bank, 0xa084 + direction));
            write_word(ram, shot + 0x3a, read_word(bank, 0xa088 + direction));
            write_word(ram, shot + 0x38, read_word(bank, 0xa08c + 2 * (read_word(ram, actor + 0x56) - 1)));
            ram[shot + 0x2c] = ram[actor + 0x14];
            world_queue_sound(ram, 0x7b);
            if (!decrement(ram, actor + 0x56))
                write_word(ram, actor + 0x50, read_word(ram, actor + 0x52));
        }
        ram[actor + 0x0d] = 0;
        uint16_t hit = world_player_contact(ram, main_rom, actor);
        if (read_word(ram, actor + 0x26)) {
            ram[actor + 0x0d] = 0;
            hit = world_projectile_contact(ram, main_rom, actor);
        }
        if (!read_word(ram, actor + 0x26)) {
            world_award_score(ram, main_rom, actor, hit);
            write_word(ram, 0xb8a0, read_word(ram, 0xb8a0) + 2);
            world_queue_sound(ram, 0x5f);
            world_queue_sound(ram, 0x5a);
            uint16_t blast = bank_b_effect(ram, read_word(ram, 0x9c), 0xa3ba);
            write_word(ram, blast + 0x21, read_word(ram, actor + 0x21));
            write_word(ram, blast + 0x24, read_word(ram, actor + 0x24));
            ram[blast + 0x29] = ram[actor + 0x29];
            ram[blast + 0x2c] = ram[actor + 0x15];
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xa279);
        }
        break;
    }
    case 0xba4bd:
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0xa4cc);
        break;
    case 0xba4cc: {
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 0x40) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(main_rom + 0xb0000, 0xa012 + frame);
            if (sprite == 0xffff) {
                finish_animation(ram, actor);
                break;
            }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(main_rom + 0xb0000, 0xa02c + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        if (!(ram[0x274] & 1)) queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xba2ec:
    case 0xba30c: {
        if (!tiles) return 0;
        if (address == 0xba2ec) {
            ram[actor + 0x2d] = 4;
            write_word(ram, actor + 0x3c, 0xf53b);
            write_word(ram, actor + 0x26, 128);
            write_word(ram, actor + 0x36, 128);
            write_word(ram, actor + 0x44, 1);
            write_word(ram, actor, 0xa30c);
        } else {
            scroll_actor(ram, actor, 0xb71a);
        }
        move_actor_axis(ram, actor + 0x20, read_word(ram, actor + 0x38));
        int16_t velocity = read_word(ram, actor + 0x3a);
        if (velocity > -512) velocity -= 16;
        write_word(ram, actor + 0x3a, velocity);
        move_actor_axis(ram, actor + 0x23, (uint16_t)velocity);
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        uint16_t minimum = (ram[0x26e] & 2) ? 0x68 : 0x10 + read_word(ram, 0xb732);
        if (x > 0x1c0 || x < 0x40 || (int16_t)y < (int16_t)minimum) {
            finish_animation(ram, actor);
            break;
        }
        if (!decrement(ram, actor + 0x44)) {
            uint16_t frame = read_word(ram, actor + 0x40);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, 0xa094 + (frame & 14)));
            write_word(ram, actor + 0x44, 2);
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        unsigned cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a),
                                          0x177 - y + read_word(ram, 0xb70c));
        unsigned material = tiles[0x4000 + cell + 3] >> 3;
        int impact = !!(0xff02 & (1u << (material & 15)));
        if (!impact) {
            ram[actor + 0x0d] = 2;
            impact = world_player_contact(ram, main_rom, actor) != 0;
        }
        if (impact) {
            spawn_impact(ram, main_rom, actor);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xba279:
    case 0xba2c7: {
        const uint8_t *bank = main_rom + 0xb0000;
        scroll_actor(ram, actor, 0xb71a);
        if (read_word(ram, actor + 0x21) < 64) {
            finish_animation(ram, actor);
            break;
        }
        if (address == 0xba279 && !decrement(ram, actor + 0x44)) {
            uint16_t index = read_word(ram, actor + 0x40);
            uint16_t frame = read_word(bank, 0x9fbe + index);
            if (frame == 0xffff) write_word(ram, actor, 0xa2c7);
            else {
                write_word(ram, actor + 0x2a, frame);
                write_word(ram, actor + 0x44, read_word(bank, 0x9ff2 + index));
                write_word(ram, actor + 0x40, index + 2);
            }
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0xaeda6:
        ram[actor + 0x2c] = main_rom[0xaeda9];
        ram[actor + 0x2d] = 0;
        write_word(ram, actor + 0x42, 600);
        write_word(ram, actor + 0x50, read_word(ram, 0xb700) + 16);
        write_word(ram, actor + 0x52, read_word(main_rom + 0xa0000, 0xed9a + 2 * read_word(ram, 0x84)));
        write_word(ram, actor, 0xedd0);
        break;
    case 0xaedd0:
    case 0xaee4f:
    case 0xaeebf:
        update_stage_trigger(ram, main_rom, actor);
        break;
    case 0x7ea17: {
        const uint8_t *bank = main_rom + 0x70000;
        world_acquire_resource(ram, actor, read_word(bank, 0xea18));
        ram[actor + 0x2d] = 32;
        unsigned variant = ram[actor + 0x1e] & 6;
        if (variant == 4) variant = world_random_byte(ram) & 2;
        write_word(ram, actor + 0x50, read_word(bank, 0xe74e + variant));
        write_word(ram, actor + 0x2a, read_word(bank, 0xe74a + (variant & 2)));
        write_word(ram, actor + 0x3c, read_word(bank, 0xe6e6 + (variant & 2)));
        begin_linear_script(ram, main_rom, actor, 0xe756);
        write_word(ram, actor, 0xea62);
        break;
    }
    case 0x7ea62:
    case 0x7eb1c: {
        if (!tiles) return 0;
        int step = -1;
        if (address == 0x7ea62) {
            uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            unsigned material = tiles[0x4000 + cell + 3] >> 3;
            if ((1u << (material & 15)) & 0xff02) scroll_actor(ram, actor, 0xb71a);
            else if (material != 3) step = step_flight_script(ram, main_rom, actor);
        } else step = step_flight_script(ram, main_rom, actor);
        if (!step) return 0;
        if (step < 0) {
            begin_linear_script(ram, main_rom, actor, 0xe774);
            write_word(ram, actor, 0xeb1c);
        }
        if (outside_pickup_area(ram, actor)) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        ram[actor + 0x0d] = 1;
        uint16_t player = world_player_contact(ram, main_rom, actor);
        if (player) {
            uint16_t stats = read_word(ram, player + 0x70), value = read_word(ram, actor + 0x50);
            write_word(ram, stats + 0x1a, read_word(ram, stats + 0x1a) + value);
            write_word(ram, stats + 0x1c, read_word(ram, stats + 0x1c) + value);
            world_queue_sound(ram, 0x31);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0x7ece5:
        ram[actor + 0x2c] = main_rom[0x7ece8];
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0xffff);
        write_word(ram, 0x844c, actor);
        write_word(ram, actor, 0xecfe);
        /* fall through */
    case 0x7ecfe: {
        uint16_t parent = read_word(ram, actor + 0x40);
        if (parent != 0xffff && !(ram[parent + 0x0a] & 1)) {
            write_word(ram, actor + 0x21, read_word(ram, parent + 0x21));
            write_word(ram, actor + 0x24, read_word(ram, parent + 0x24));
            uint16_t phase = read_word(ram, actor + 0x46) + 0x20;
            write_word(ram, actor + 0x46, phase);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0x70000, 0xef9a + 2 * ((phase >> 8) & 3)));
            queue_sprite(ram, main_rom, actor, 1);
        }
        break;
    }
    case 0xb2c46:
    case 0xb2c68: case 0xb2ca0: case 0xb2cd3: case 0xb2d80:
        if (!surface_enemy_swim(ram, main_rom, tiles, actor, (uint16_t)address)) return 0;
        break;
    case 0x7e21c: {
        const uint8_t *bank = main_rom + 0x70000;
        uint16_t script = read_word(bank, 0xde52 + 2*read_word(ram, actor + 0x54));
        if (read_word(bank, script) != 2) return 0;
        world_acquire_resource(ram, actor, 0xff);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x3c, read_word(bank, 0xe22b));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 128);
        begin_linear_script(ram, main_rom, actor, script);
        write_word(ram, actor, 0xe24c);
        break;
    }
    case 0x7e24c:
    case 0x7e260:
    case 0x7e30e: {
        if (!tiles) return 0;
        if (address == 0x7e24c) {
            int step = step_flight_script(ram, main_rom, actor);
            if (!step) return 0;
            if (step < 0) {
                write_word(ram, actor + 0x3a, -512);
                write_word(ram, actor, 0xe260);
            }
        } else {
            scroll_actor(ram, actor, 0xb71a);
            move_actor_axis(ram, actor + 0x23, read_word(ram, actor + 0x3a));
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t top = ram[0x26e] & 2 ? 0x68 : read_word(ram, 0xb732) + 0x10;
        if (x > 0x1c0 || x < 0x40 || (int16_t)y < top) { finish_animation(ram, actor); break; }
        if (address == 0x7e30e) {
            queue_sprite(ram, main_rom, actor, 0);
            int hit = world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02;
            if (!hit) {
                ram[actor + 0xd] = 2;
                hit = world_player_contact(ram, main_rom, actor) != 0;
                if (!hit) {
                    ram[actor + 0xd] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    hit = !read_word(ram, actor + 0x26);
                }
            }
            if (hit) { warship_part_explosion(ram, main_rom, actor); finish_animation(ram, actor); }
            break;
        }
        unsigned direction = ram[actor + 0x28] >> 3;
        ram[actor + 0x29] = main_rom[0xb1b38 + direction];
        uint16_t table = 0x1af8 + 2*(direction & 15);
        write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, table + read_word(ram, actor + 0x40)));
        queue_sprite(ram, main_rom, actor, 0);
        uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - (y - 8) + read_word(ram, 0xb70c));
        if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
            write_word(ram, actor + 0x3a, -320);
            uint16_t splash = effect_slot(ram, actor, 0x6f54);
            unsigned scroll = read_word(ram, 0xb70c) & 7;
            write_word(ram, splash + 0x21, x);
            write_word(ram, splash + 0x24, ((y - 8 - scroll - 1) & 0xfff8) + scroll + 8);
            ram[splash + 0x23] = 0;
            write_word(ram, splash + 0x4c, read_word(main_rom, 0x6d2a));
            write_word(ram, splash + 0x4a, read_word(main_rom, 0x6d2f));
            write_word(ram, splash + 0x2c, 0x200a);
            write_word(ram, splash + 0x56, 48);
            write_word(ram, splash + 0x58, 128);
            write_word(ram, actor + 0x40, 32);
            write_word(ram, actor + 0x2a, read_word(main_rom + 0xb0000, table + 32));
            write_word(ram, actor, 0xe30e);
        }
        ram[actor + 0xd] = 0;
        world_surface_contact(ram, main_rom, actor);
        if (!read_word(ram, actor + 0x26)) {
            uint16_t blast = effect_slot(ram, actor, 0x6efe);
            write_word(ram, blast + 0x4c, read_word(main_rom, 0x6c86));
            write_word(ram, blast + 0x4a, read_word(main_rom, 0x6c8b));
            write_word(ram, blast + 0x44, 0x5d);
            ram[blast + 0x2d] = 0x20;
            write_word(ram, blast + 0x56, 0x30);
            write_word(ram, blast + 0x58, 0x80);
            world_queue_sound(ram, 0x3f);
            finish_animation(ram, actor);
        }
        break;
    }
    case 0xb6a4d: {
        const uint8_t *bank = main_rom + 0xb0000;
        unsigned choice = 2*(ram[actor + 0x29] & 1) + 2*read_word(ram, actor + 0x62);
        uint16_t script = read_word(bank, 0x4e16 + choice);
        if (read_word(bank, script) != 2) return 0;
        begin_linear_script(ram, main_rom, actor, script);
        ram[actor + 0x29] = 0;
        ram[actor + 0xc] = 12;
        write_word(ram, actor + 0x3c, read_word(bank, 0x6a71));
        write_word(ram, actor + 0x26, 1);
        write_word(ram, actor + 0x36, 0x800);
        ram[actor + 0x2d] = 4;
        write_word(ram, actor + 0x6e, 1);
        write_word(ram, actor + 0x54, 3);
        write_word(ram, actor + 0x56, 6);
        write_word(ram, actor, 0x6a95);
    }
        /* fall through */
    case 0xb6a95:
    case 0xb6ad6:
    case 0xb6b25:
    case 0xb6b7c:
    case 0xb6bd8: {
        const uint8_t *bank = main_rom + 0xb0000;
        uint16_t state = read_word(ram, actor);
        int descending = state == 0x6b7c || state == 0x6bd8;
        if (descending && !tiles) return 0;
        if (state == 0x6a95 || state == 0x6b25) {
            int result = step_flight_script(ram, main_rom, actor);
            if (!result) return 0;
            if (result < 0) {
                if (state == 0x6a95 && ram[actor + 0x28] != 0xc0) {
                    write_word(ram, actor + 0x38, ram[actor + 0x28] & 128 ? -512 : 512);
                    write_word(ram, actor, 0x6ad6);
                } else {
                    write_word(ram, actor + 0x3a, -512);
                    write_word(ram, actor, 0x6b7c);
                }
            }
            unsigned angle = (ram[actor + 0x28] + 8) & 0xf0;
            write_word(ram, actor + 0x2a, read_word(bank, 0x4f2c + (angle >> 3)));
        } else {
            scroll_actor(ram, actor, 0xb71a);
            unsigned axis = descending ? 1 : 0;
            move_actor_axis(ram, actor + 0x20 + 3*axis, read_word(ram, actor + 0x38 + 2*axis));
        }
        uint16_t x = read_word(ram, actor + 0x21), y = read_word(ram, actor + 0x24);
        int16_t floor = ram[0x26e] & 2 ? 0x68 : read_word(ram, 0xb732) + 0x10;
        if (descending && (x < 0x40 || x > 0x1c0 || (state == 0x6bd8 && (int16_t)y < floor))) {
            finish_animation(ram, actor);
            break;
        }
        queue_sprite(ram, main_rom, actor, 0);
        if (state == 0x6ad6) {
            int turn = !(ram[0x264] & 3);
            if (!turn) {
                uint16_t target = world_enemy_target(ram, actor);
                int16_t tx = read_word(ram, target + 0x21), threshold = x - 32;
                turn = ram[actor + 0x28] & 128 ? threshold <= tx : threshold >= tx;
            }
            if (turn) {
                begin_linear_script(ram, main_rom, actor, read_word(bank, 0x4f08 + ((ram[actor + 0x28] >> 6) & 2)));
                write_word(ram, actor, 0x6b25);
            }
        }
        if (state == 0x6b7c) {
            uint16_t cell = world_tile_address(x - 0x60 + read_word(ram, 0xb70a), 0x177 - y + read_word(ram, 0xb70c));
            if ((tiles[0x4000 + cell + 3] >> 3) == 4) {
                uint16_t splash = effect_slot(ram, actor, 0x6f54);
                unsigned scroll = read_word(ram, 0xb70c) & 7;
                write_word(ram, splash + 0x21, x);
                write_word(ram, splash + 0x24, ((y - scroll - 1) & 0xfff8) + scroll + 8);
                ram[splash + 0x23] = 0;
                write_word(ram, splash + 0x4c, read_word(main_rom, 0x6d2a));
                write_word(ram, splash + 0x4a, read_word(main_rom, 0x6d2f));
                write_word(ram, splash + 0x2c, 0x200a);
                write_word(ram, splash + 0x56, 48);
                write_word(ram, splash + 0x58, 128);
                write_word(ram, actor + 0x6e, 1);
                ram[actor + 0x14] = 3;
                write_word(ram, actor + 0x3a, -384);
                write_word(ram, actor, 0x6bd8);
            }
        }
        if (!decrement(ram, actor + 0x54)) {
            write_word(ram, actor + 0x54, read_word(ram, actor + 0x56));
            uint16_t trail = effect_slot(ram, read_word(ram, 0x9a), 0xbe45);
            write_word(ram, trail + 0x21, x);
            write_word(ram, trail + 0x24, y);
            ram[trail + 0x2c] = ram[actor + 0x14];
            ram[trail + 0x2d] = 0x28;
            write_word(ram, trail + 0x1e, read_word(ram, actor + 0x6e));
        }
        int hit = 0;
        if (state == 0x6bd8) {
            hit = world_terrain_mask(ram, tiles + 0x4000, x, y) & 0xff02;
            if (!hit) {
                ram[actor + 0xd] = 2;
                hit = world_player_contact(ram, main_rom, actor) != 0;
                if (!hit) {
                    ram[actor + 0xd] = 0;
                    world_projectile_contact(ram, main_rom, actor);
                    hit = !read_word(ram, actor + 0x26);
                }
            }
        } else {
            ram[actor + 0xd] = 0;
            world_surface_contact(ram, main_rom, actor);
            hit = !read_word(ram, actor + 0x26);
        }
        if (hit) { spawn_impact(ram, main_rom, actor); finish_animation(ram, actor); }
        break;
    }
    case 0xbe45:
        write_word(ram, actor + 0x3c, 0xffff);
        write_word(ram, actor + 0x40, 0);
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor + 0x46, read_word(main_rom, 0xbe3d + 2*(ram[actor + 0x1e] & 1)));
        write_word(ram, actor, 0xbe6b);
        break;
    case 0x11723:
    case 0x11742: case 0x117da: case 0x11819: case 0x118a3: case 0x118bd:
    case 0x119f9:
    case 0x11a09: case 0x11a63: case 0x11adc: case 0x11af1:
    case 0x11b41:
    case 0x11b57:
        if (!boss_claw_step(ram, main_rom, actor, (uint16_t)address)) return 0;
        break;
    case 0x1108c:
    case 0x110f8: case 0x11122: case 0x11155: case 0x111f6:
    case 0x11213:
    case 0x11241: case 0x11258: case 0x11290: case 0x112ce:
    case 0x1133a:
    case 0x1138b:
        boss_appendage(ram, main_rom, actor, (uint16_t)address);
        break;
    case 0x11436:
        world_queue_sound(ram, 0x46);
        ram[actor + 0x2c] = 3;
        ram[actor + 0x2d] = 0x2d;
        write_word(ram, actor + 0x44, 1);
        write_word(ram, actor, 0x1450);
        /* fall through */
    case 0x11450: {
        const uint8_t *bank = main_rom + 0x10000;
        if (read_word(ram, actor + 0x40) >= 10) scroll_actor(ram, actor, 0xb716);
        if (!decrement(ram, actor + 0x44)) {
            unsigned frame = read_word(ram, actor + 0x40);
            uint16_t sprite = read_word(bank, 0xf2 + frame);
            if (sprite == 0xffff) { finish_animation(ram, actor); break; }
            write_word(ram, actor + 0x2a, sprite);
            write_word(ram, actor + 0x44, read_word(bank, 0x112 + frame));
            write_word(ram, actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 0);
        break;
    }
    case 0x11035: {
        const uint8_t *bank = main_rom + 0x10000;
        world_acquire_resource(ram, actor, 0x69);
        world_acquire_resource(ram, actor, 0xea);
        ram[actor + 0x2c] = ram[actor + 0x14];
        ram[actor + 0x2d] = 0x2f;
        ram[actor + 0x0c] = 4;
        uint16_t index = read_word(ram, actor + 0x52);
        write_word(ram, actor + 0x2e, read_word(bank, 0x58c + 2 * (index & 1)));
        uint16_t child = spawn_boss_part(ram, actor, 0x1496);
        write_word(ram, child + 0x52, index);
        write_word(ram, child + 0x42, read_word(bank, 0x428 + 2 * (index & 3)));
        write_word(ram, child + 0x50, actor);
        write_word(ram, actor, 0x108c);
        break;
    }
    case 0x12069: {
        if (read_word(ram, 0x262) & 0x8800) {
            finish_animation(ram, actor);
            break;
        }
        uint16_t phase = read_word(ram, actor + 0x40);
        uint16_t job = 0x9e06 + 0x128;
        write_word(ram, job, read_word(main_rom + 0x10000, 0x8cc + (phase & 2)));
        write_word(ram, job + 2, 0x40);
        write_word(ram, job + 4, 0);
        write_word(ram, job + 6, 0);
        write_word(ram, actor + 0x40, phase + 2);
        break;
    }
    case 0x120b3: {
        uint16_t parent = read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
        if (ram[parent + 0x0a] & 1) {
            finish_animation(ram, actor);
        } else if (!decrement(ram, actor + 0x42)) {
            uint16_t phase = read_word(ram, actor + 0x40);
            uint16_t palette = read_word(main_rom + 0x10000, 0x8c8 + (phase & 2));
            queue_palette(ram, ram[actor + 0x2c] & 63, palette, 5);
            write_word(ram, actor + 0x42, 33);
            write_word(ram, actor + 0x40, phase + 2);
        }
        break;
    }
    case 0x10917: {
        const uint8_t *bank = main_rom + 0x10000;
        world_acquire_resource(ram, actor, 0x67);
        world_acquire_resource(ram, actor, 0xea);
        ram[actor + 0x2c] = ram[actor + 0x14];
        write_word(ram, actor + 0xe, 0x40);
        ram[actor + 0x2d] = 0x30;
        write_word(ram, actor + 0x3c, read_word(bank, 0x939));
        write_word(ram, actor + 0x2a, read_word(bank, 0x93e));
        uint16_t cfg = read_word(bank, 0x766 + read_word(ram, 0x88));
        uint16_t hp = read_word(bank, 0x786 + cfg);
        write_word(ram, actor + 0x26, hp);
        write_word(ram, actor + 0x36, hp);
        write_word(ram, actor + 0x4a, hp);
        write_word(ram, actor + 0x50, read_word(bank, 0x788 + cfg));
        write_word(ram, actor + 0x52, read_word(bank, 0x788 + cfg));
        write_word(ram, actor + 0x54, read_word(bank, 0x78a + cfg));
        write_word(ram, actor + 0x60, 15);
        spawn_boss_part(ram, actor, 0x2069);
        uint16_t child = spawn_boss_part(ram, actor, 0x20b3);
        ram[child + 0x2c] = ram[actor + 0x2c];
        write_word(ram, child + 0x42, 1);
        for (unsigned i = 0; i < 4; ++i) {
            child = spawn_boss_part(ram, actor, 0x1035);
            write_word(ram, child + 0x52, i);
            write_word(ram, child + 0x56, read_word(bank, 0x9a3 + 29*i));
            write_word(ram, child + 0x50, actor);
            write_word(ram, child + 0xe, 0x100 + i);
        }
        begin_linear_script(ram, main_rom, actor, 0x666);
        world_queue_sound(ram, 0x8c);
        uint16_t tail = (read_word(ram, 0xa6d8) + 2) & 14;
        if (tail != read_word(ram, 0xa6d6)) {
            write_word(ram, 0xa6d8, tail);
            write_word(ram, 0xa6a2 + tail, 0x11);
        }
        write_word(ram, actor, 0xa22);
        break;
    }
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
