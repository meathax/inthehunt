#include "world_ym.h"
#include "third_party/ymfm/ymfm_opm.h"

class WorldYmInterface : public ymfm::ymfm_interface {
public:
    int64_t timers[2] = {-1, -1};

    void ymfm_set_timer(uint32_t timer, int32_t clocks) override
    {
        timers[timer] = clocks;
    }

    void advance()
    {
        for (unsigned i = 0; i < 2; ++i) {
            if (timers[i] < 0) continue;
            timers[i] -= 64;
            if (timers[i] <= 0) {
                timers[i] = -1;
                m_engine->engine_timer_expired(i);
            }
        }
    }
};

static WorldYmInterface interface;
static ymfm::ym2151 chip(interface);
static uint32_t tick_remaining;
static int32_t tick_sample;

void world_ym_reset(void)
{
    interface.timers[0] = interface.timers[1] = -1;
    chip.reset();
    tick_remaining = 0;
    tick_sample = 0;
}

void world_ym_write(uint8_t reg, uint8_t value)
{
    chip.write_address(reg);
    chip.write_data(value);
}

void world_ym_render(int32_t *output, size_t count)
{
    for (size_t frame = 0; frame < count; ++frame) {
        uint32_t remaining = 14318181;
        int64_t integral = 0;
        while (remaining) {
            if (!tick_remaining) {
                ymfm::ym2151::output_data sample;
                chip.generate(&sample);
                interface.advance();
                tick_sample = sample.data[0] + sample.data[1];
                tick_remaining = 256 * 48000;
            }
            uint32_t duration = remaining < tick_remaining ? remaining : tick_remaining;
            integral += int64_t(tick_sample) * duration;
            remaining -= duration;
            tick_remaining -= duration;
        }
        output[frame] = int32_t(integral * 2 / (int64_t(14318181) * 5));
    }
}
