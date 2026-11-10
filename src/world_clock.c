#include "world_clock.h"
#include <string.h>

void world_clock_reset(WorldClock *clock, WorldTimingMode mode)
{
    memset(clock, 0, sizeof *clock);
    clock->requested_mode = mode == WORLD_TIMING_REMOVE_SLOWDOWN ? mode : WORLD_TIMING_ARCADE;
    clock->pass_mode = clock->requested_mode;
}

int world_clock_request_mode(WorldClock *clock, WorldTimingMode mode)
{
    if (mode != WORLD_TIMING_ARCADE && mode != WORLD_TIMING_REMOVE_SLOWDOWN) return 0;
    clock->requested_mode = mode;
    return 1;
}

void world_clock_begin_pass(WorldClock *clock)
{
    clock->pass_mode = clock->requested_mode;
    ++clock->passes;
}

static uint32_t advance_cycles(WorldClock *clock, uint32_t cycles)
{
    uint64_t elapsed = clock->phase + (uint64_t)cycles * WORLD_CLOCK_PIXEL_HZ;
    uint32_t crossed = (uint32_t)(elapsed / WORLD_CLOCK_FRAME_UNITS);
    clock->phase = elapsed % WORLD_CLOCK_FRAME_UNITS;
    clock->vblanks += crossed;
    return crossed;
}

uint32_t world_clock_advance_busy(WorldClock *clock, uint32_t cycles, WorldClockCostBasis basis)
{
    clock->charged_cycles += cycles;
    switch (basis) {
    case WORLD_CLOCK_COST_MEASURED: ++clock->measured_costs; break;
    case WORLD_CLOCK_COST_REFERENCE: ++clock->reference_costs; break;
    default: ++clock->unrecovered_costs; break;
    }
    if (clock->pass_mode == WORLD_TIMING_REMOVE_SLOWDOWN) return 0;
    uint32_t crossed = advance_cycles(clock, cycles);
    clock->busy_vblanks += crossed;
    return crossed;
}

void world_clock_note_unrecovered(WorldClock *clock)
{
    ++clock->unrecovered_costs;
}

uint32_t world_clock_advance_hardware(WorldClock *clock, uint32_t cycles)
{
    return advance_cycles(clock, cycles);
}

uint32_t world_clock_cycles_until_vblank(const WorldClock *clock)
{
    uint64_t remaining = WORLD_CLOCK_FRAME_UNITS - clock->phase;
    return (uint32_t)((remaining + WORLD_CLOCK_PIXEL_HZ - 1) / WORLD_CLOCK_PIXEL_HZ);
}

uint32_t world_clock_wait_vblank(WorldClock *clock)
{
    uint32_t cycles = world_clock_cycles_until_vblank(clock);
    clock->phase = 0;
    ++clock->vblanks;
    return cycles;
}

void world_clock_raster(const WorldClock *clock, unsigned *line, unsigned *pixel,
                        uint32_t *subpixel)
{
    uint32_t position = (uint32_t)(clock->phase / WORLD_CLOCK_CPU_HZ);
    if (line) *line = (WORLD_CLOCK_VBLANK_LINE + position / WORLD_CLOCK_LINE_PIXELS) % WORLD_CLOCK_FRAME_LINES;
    if (pixel) *pixel = position % WORLD_CLOCK_LINE_PIXELS;
    if (subpixel) *subpixel = (uint32_t)(clock->phase % WORLD_CLOCK_CPU_HZ);
}

int world_clock_has_measured_costs(const WorldClock *clock)
{
    return clock->measured_costs && !clock->reference_costs && !clock->unrecovered_costs;
}
