#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/world_torpedo.h"
#include <stdlib.h>

void world_award_score(uint8_t *, const uint8_t *, uint16_t, uint16_t);

uint16_t world_projectile_contact(uint8_t *, const uint8_t *, uint16_t);
void world_kind11_death(uint8_t *, const uint8_t *, uint16_t, uint16_t);
void world_latch_cabinet(uint8_t *, uint16_t);
void world_sub_velocity(uint8_t *, const uint8_t *, uint16_t, unsigned);
void world_sub_pose(uint8_t *, const uint8_t *, uint16_t);
void world_sub_wake(uint8_t *, const uint8_t *, uint16_t);
void world_sub_free_motion(uint8_t *, uint16_t);
int world_horizontal_camera(uint8_t *, const uint8_t *, uint16_t, uint16_t, uint16_t);
#include "../src/world_stage.h"
void world_kind11_move(uint8_t *, const uint8_t *, const uint8_t *, uint16_t);
static int read_segment(const char *path, uint8_t *segment, size_t size)
{
    FILE *stream = fopen(path, "rb");
    if (!stream) return 0;
    size_t bytes = fread(segment, 1, size, stream);
    fclose(stream);
    return bytes == size;
}

int main(int argc, char **argv)
{
    static uint8_t ram[65536], rom[0xc0000];
    if ((argc < 6 || argc > 8) || !read_segment(argv[1], rom, sizeof rom) || !read_segment(argv[2], ram, sizeof ram))
        return 1;
    uint16_t enemy = strtoul(argv[3], NULL, 0);
    uint16_t attacker;
    int cabinet = strcmp(argv[4], "cabinet-encounter") == 0;
    int steering = cabinet || strcmp(argv[4], "steering-encounter") == 0;
    int camera = steering || strcmp(argv[4], "camera-encounter") == 0;
    int encounter = camera || strcmp(argv[4], "encounter") == 0;
    if ((argc == 8) != encounter) return 1;
    int motion = strcmp(argv[4], "enemy-motion") == 0;
    if ((argc == 7) != motion) return 1;
    if (motion) {
        static uint8_t terrain[16384];
        if (!read_segment(argv[6], terrain, sizeof terrain)) return 1;
        world_kind11_move(ram, rom, terrain, enemy);
    }
    int flight = encounter || strcmp(argv[4], "flight") == 0;
    int spawn = strcmp(argv[4], "stage-spawn") == 0;
    int init = strcmp(argv[4], "enemy-init") == 0;
    int weapons = strcmp(argv[4], "player-weapons") == 0;
    int display = strcmp(argv[4], "player-display-fire") == 0;
    if (display) world_sub_pose(ram, rom, enemy);
    if (display || weapons) world_player_weapons(ram, rom, enemy);
    if (display) world_sub_wake(ram, rom, enemy);
    if (spawn) world_stage_spawns(ram, rom, enemy);
    if (init) world_kind11_init(ram, rom, enemy);
    if (strcmp(argv[4], "fire") == 0) {
        Torpedo shot;
        printf("allocated=%04x\n", world_fire_standard(ram, rom, enemy, &shot));
    }
    if (flight) {
        unsigned slot, x, y, facing, pose, dx, dy;
        Torpedo shot;
        static uint8_t launch_ram[65536];
        unsigned controller = 0, first_pad = 0;
        if (encounter) {
            if (!read_segment(argv[6], launch_ram, sizeof launch_ram)) return 1;
            if (camera && (scanf("%x", &controller) != 1 || controller > 0xff80)) return 1;
            if (cabinet) {
                if (scanf("%x", &first_pad) != 1 || first_pad > 0xffff) return 1;
                uint16_t sub = strtoul(argv[7], NULL, 0);
                world_latch_cabinet(launch_ram, first_pad);
                world_sub_velocity(launch_ram, rom, sub, launch_ram[0xb73e]);
                world_sub_free_motion(launch_ram, sub);
            }
            slot = world_fire_standard(launch_ram, rom, strtoul(argv[7], NULL, 0), &shot);
            if (!slot) return 1;
            ram[slot + 0x0b] = launch_ram[slot + 0x0b];
            ram[slot + 0x29] = shot.facing;
            /* Fresh torpedo's collision shape, health and damage immediates. */
            memcpy(ram + slot + 0x3c, rom + 0x9bfb, 2);
            memcpy(ram + slot + 0x26, rom + 0x9c0f, 2);
            memcpy(ram + slot + 0x36, rom + 0x9c14, 2);
            printf("spawn actor=%04x\n", slot);
        } else {
            if (scanf("%x %u %u %u %u", &slot, &x, &y, &facing, &pose) != 5 ||
                slot > 0xff80) return 1;
            world_launch_torpedo(&shot, rom, x, y, facing, pose);
        }
        int fields;
        unsigned tick = 0;
        for (;;) {
            if (cabinet) {
                if (tick == 0) dx = first_pad;
                else if ((fields = scanf("%x", &dx)) != 1) break;
                if (dx > 0xffff) return 1;
                if (tick) world_latch_cabinet(launch_ram, dx);
                dx = launch_ram[0xb73e];
            } else if (steering) {
                if ((fields = scanf("%u", &dx)) != 1) break;
            } else if ((fields = scanf("%u %u", &dx, &dy)) != 2) break;
            uint16_t player = launch_ram[0x90] | ((uint16_t)launch_ram[0x91] << 8);
            if (steering) {
                if ((launch_ram[0x264] & 12) != 4) return 1;
                if (tick++) {
                    world_sub_velocity(launch_ram, rom, player, dx);
                    world_sub_free_motion(launch_ram, player);
                }
                dx = launch_ram[player+0x21] | ((unsigned)launch_ram[player+0x22] << 8);
                dy = 0;
                printf("player x=%u\n", dx);
            }
            if (camera) {
                if (!world_horizontal_camera(launch_ram, rom, controller, dx, dy)) {
                    fprintf(stderr, "Camera stop path still requires recovery\n");
                    return 1;
                }
                dx = launch_ram[0xb71a] | ((unsigned)launch_ram[0xb71b] << 8);
                dy = launch_ram[0xb71c] | ((unsigned)launch_ram[0xb71d] << 8);
                printf("camera x=%u fraction=%u speed=%u\n",
                       launch_ram[0xb70a] | ((unsigned)launch_ram[0xb70b] << 8),
                       launch_ram[controller+0x51], launch_ram[0xa4] | ((unsigned)launch_ram[0xa5] << 8));
            }
            if (steering) {
                for (unsigned axis = 0; axis < 2; ++axis) {
                    uint8_t *position = launch_ram + player + 0x21 + axis * 3;
                    uint16_t value = position[0] | ((uint16_t)position[1] << 8);
                    value += axis ? dy : dx;
                    position[0] = (uint8_t)value;
                    position[1] = value >> 8;
                }
            }
            world_fly_torpedo(&shot, rom, dx, dy);
        }
        if (fields != EOF) return 1;
        ram[slot+0x20] = shot.fraction;
        ram[slot+0x21] = (uint8_t)shot.x;
        ram[slot+0x22] = shot.x >> 8;
        ram[slot+0x24] = (uint8_t)shot.y;
        ram[slot+0x25] = shot.y >> 8;
    }
    if (flight || strcmp(argv[4], "contact") == 0) {
        attacker = world_projectile_contact(ram, rom, enemy);
        printf("contact actor=%04x enemy=%04x\n", attacker, enemy);
        if (attacker && !ram[(uint16_t)(enemy+0x26)] && !ram[(uint16_t)(enemy+0x27)]) {
            if (ram[enemy+14] == 0x11 && ram[enemy+15] == 0)
                world_kind11_death(ram, rom, enemy, attacker);
            else
            world_award_score(ram, rom, enemy, attacker);
        }
    } else if (!motion && !spawn && !init && !weapons && !display && strcmp(argv[4], "fire") != 0) {
        attacker = strtoul(argv[4], NULL, 0);
        world_award_score(ram, rom, enemy, attacker);
    }
    FILE *result = fopen(argv[5], "wb");
    if (!result) return 1;
    int written = fwrite(ram, 1, sizeof ram, result) == sizeof ram;
    return fclose(result) || !written;
}
