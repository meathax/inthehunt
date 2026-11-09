#include "world_palette.h"

static unsigned word(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static unsigned fade(unsigned current, unsigned target, unsigned step,
                     unsigned threshold, unsigned mode)
{
    if (mode == 2)
        return current > step ? current - step : 0;
    if (mode == 0 && current > target)
        return current - target > step ? current - step : target;
    if (target < threshold)
        return current;
    return current + step < target ? current + step : target;
}

void world_palette_step(uint8_t *work, uint8_t *palette, const uint8_t *rom)
{
    for (unsigned bank = 0; bank < 64; ++bank) {
        uint8_t *job = work + 0x9e06 + 8 * bank;
        unsigned mode = job[2] & 3;
        unsigned step = 0;
        unsigned threshold = 0;
        int animate = job[2] & 0x80;
        if (animate) {
            if (mode == 3)
                continue;
            const uint8_t *speed = rom + 0x121d + 4 * (job[4] & 15);
            if (word(work + 0x274) & word(speed + 2))
                continue;
            step = speed[0];
            if (job[6] < step)
                job[2] &= 0x7f;
            threshold = job[6] > step ? job[6] - step : 0;
            job[6] = (uint8_t)threshold;
        } else {
            if (!(job[2] & 0x40))
                continue;
            job[2] &= 0xbf;
        }

        for (unsigned color = 0; color < 16; ++color) {
            unsigned offset = (0xa212 + 32 * word(job) + 2 * color) & 0xffff;
            unsigned target = word(rom + 0xa0000 + offset);
            uint8_t *entry = palette + 32 * bank + 2 * color;
            unsigned current = word(entry);
            unsigned result = target;
            if (animate) {
                if (mode == 0 && current == target)
                    continue;
                result = current & 0x8000;
                for (unsigned shift = 0; shift <= 10; shift += 5) {
                    unsigned from = (current >> shift) & 31;
                    unsigned to = (target >> shift) & 31;
                    if (mode == 0 || (shift && ((mode == 1 && to >= threshold) ||
                                                (mode == 2 && from))))
                        result &= 0x7fff;
                    result |= fade(from, to, step, threshold, mode) << shift;
                }
            }
            entry[0] = (uint8_t)result;
            entry[1] = (uint8_t)(result >> 8);
        }
    }
}
