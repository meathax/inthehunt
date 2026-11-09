#include "world_enemy_target.h"

static uint16_t word(const uint8_t *ram, uint16_t address)
{
    return (uint16_t)(ram[address] | (ram[(uint16_t)(address + 1)] << 8));
}

static uint16_t axis_distance(const uint8_t *ram, uint16_t actor, uint16_t player, unsigned axis)
{
    uint16_t delta = word(ram, player + axis) - word(ram, actor + axis);
    return (delta & 0x8000) ? (uint16_t)-delta : delta;
}

static uint16_t distance(const uint8_t *ram, uint16_t actor, uint16_t player, uint8_t mode)
{
    if (mode >= 12) return axis_distance(ram, actor, player, 0x24);
    uint16_t dx = axis_distance(ram, actor, player, 0x21);
    if (mode >= 8) return dx;
    return (uint16_t)(dx + axis_distance(ram, actor, player, 0x24));
}

uint8_t world_random_byte(uint8_t *ram)
{
    uint8_t *state = ram + 0xb74e;
    if (!(state[0] | state[1] | state[2] | state[3])) {
        state[0] = 0x34;
        state[1] = 0x12;
        state[2] = 0x78;
        state[3] = 0x56;
    }
    for (unsigned round = 0; round < 8; ++round) {
        unsigned feedback = ((state[0] >> 3) ^ (state[0] >> 6)) & 1;
        for (int i = 3; i >= 0; --i) {
            unsigned next = state[i] >> 7;
            state[i] = (uint8_t)((state[i] << 1) | feedback);
            feedback = next;
        }
    }
    return state[3];
}

int world_enemy_target_mode_supported(uint8_t mode)
{
    return mode <= 40 && !(mode & 1);
}

uint16_t world_enemy_target(uint8_t *ram, uint16_t actor)
{
    unsigned active = word(ram, 0x264) & 3;
    if (!active) return 0x8480;
    uint16_t first = word(ram, 0x90);
    uint16_t second = word(ram, 0x92);
    if (active != 3) return active == 1 ? first : second;
    uint8_t mode = ram[(uint16_t)(actor + 0x0c)];
    if (mode == 40) return (world_random_byte(ram) & 1) ? second : first;
    if (mode < 4 || mode >= 16) return mode == 2 ? second : first;
    uint16_t a = distance(ram, actor, first, mode);
    uint16_t b = distance(ram, actor, second, mode);
    return ((a < b) != !!(mode & 2)) ? first : second;
}
