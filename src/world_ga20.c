/* SPDX-License-Identifier: BSD-3-Clause
 * GA20 register and PCM behavior adapted from MAME iremga20.cpp.
 * Copyright Acho A. Tang, R. Belmont, Valley Bell.
 * The redistribution terms are in LICENSES/GA20-BSD-3-Clause.txt.
 */
#include "world_ga20.h"
#include <string.h>

void world_ga20_reset(WorldGa20 *chip, const uint8_t *samples, size_t size)
{
    memset(chip, 0, sizeof *chip);
    chip->samples = samples;
    chip->sample_count = samples ? size : 0;
    uint32_t mask = size >= 0x100000 ? 0xfffff : size ? (uint32_t)(size - 1) : 0;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
    chip->sample_mask = mask & 0xfffff;
}

void world_ga20_write(WorldGa20 *chip, unsigned reg, uint8_t value)
{
    reg &= 31;
    chip->registers[reg] = value;
    WorldGa20Voice *voice = &chip->voices[reg >> 3];
    switch (reg & 7) {
    case 4:
        voice->rate = value;
        break;
    case 5:
        voice->volume = (unsigned)value * 256 / (value + 10);
        break;
    case 6:
        voice->playing = !!(value & 2);
        if (voice->playing) {
            unsigned base = reg & ~7u;
            voice->position = ((unsigned)chip->registers[base] |
                               (unsigned)chip->registers[base + 1] << 8) << 4;
            voice->counter = 256;
        }
        break;
    }
}

uint8_t world_ga20_read(const WorldGa20 *chip, unsigned reg)
{
    reg &= 31;
    return (reg & 7) == 7 ? chip->voices[reg >> 3].playing : 0;
}

int32_t world_ga20_clock(WorldGa20 *chip)
{
    int32_t mixed = 0;
    for (unsigned i = 0; i < 4; ++i) {
        WorldGa20Voice *voice = &chip->voices[i];
        if (!voice->playing) continue;
        unsigned position = voice->position & 0xfffff;
        /* The World 512 KiB ROM mirrors across the 20-bit sample bus. */
        unsigned address = position & chip->sample_mask;
        uint8_t sample = address < chip->sample_count ? chip->samples[address] : 0;
        if (!sample) {
            voice->playing = 0;
            continue;
        }
        mixed += ((int)sample - 128) * voice->volume;
        if (--voice->counter <= voice->rate) {
            voice->position = (position + 1) & 0xfffff;
            voice->counter = 256;
        }
    }
    return mixed;
}

void world_ga20_render(WorldGa20 *chip, int16_t *output, size_t count)
{
    /* Exact rational 14318181 / 16 Hz -> 48000 Hz. Integrate each
       piecewise constant chip sample over the output sample interval. */
    for (size_t frame = 0; frame < count; ++frame) {
        unsigned remaining = 14318181;
        int64_t integral = 0;
        while (remaining) {
            if (!chip->tick_remaining) {
                chip->tick_sample = world_ga20_clock(chip);
                chip->tick_remaining = 16 * 48000;
            }
            unsigned duration = remaining < chip->tick_remaining ? remaining : chip->tick_remaining;
            integral += (int64_t)chip->tick_sample * duration;
            chip->tick_remaining -= duration;
            remaining -= duration;
        }
        output[frame] = (int16_t)(integral / (4 * 14318181));
    }
}
