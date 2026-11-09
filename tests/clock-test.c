#include "../src/world_clock.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void)
{
    WorldClock whole, split;
    world_clock_reset(&whole, WORLD_TIMING_ARCADE);
    world_clock_begin_pass(&whole);
    assert(world_clock_advance_busy(&whole, 149968, WORLD_CLOCK_COST_REFERENCE) == 0);
    assert(world_clock_advance_busy(&whole, 1, WORLD_CLOCK_COST_REFERENCE) == 1);
    assert(whole.vblanks == 1 && whole.busy_vblanks == 1);
    assert(whole.phase == UINT64_C(149969)*6666666-UINT64_C(999792000000));
    assert(!world_clock_has_measured_costs(&whole));

    world_clock_reset(&whole, WORLD_TIMING_ARCADE);
    world_clock_begin_pass(&whole);
    assert(world_clock_advance_busy(&whole, 9000000, WORLD_CLOCK_COST_MEASURED) == 60);
    assert(whole.phase == UINT64_C(1386)*9000000);
    unsigned line, pixel;
    uint32_t fraction;
    world_clock_raster(&whole, &line, &pixel, &fraction);
    assert(line == 251 && pixel == 114 && fraction == 0);
    assert(world_clock_has_measured_costs(&whole));

    world_clock_reset(&split, WORLD_TIMING_ARCADE);
    world_clock_begin_pass(&split);
    unsigned remaining = 9000000, seed = 39, crossed = 0;
    while (remaining) {
        seed = seed * 1664525u + 1013904223u;
        unsigned count = 1 + seed % 300000;
        if (count > remaining) count = remaining;
        crossed += world_clock_advance_busy(&split, count, WORLD_CLOCK_COST_MEASURED);
        remaining -= count;
    }
    assert(crossed == whole.vblanks && split.phase == whole.phase);
    assert(split.charged_cycles == whole.charged_cycles);
    assert(world_clock_request_mode(&split, WORLD_TIMING_REMOVE_SLOWDOWN));
    uint64_t before = split.vblanks;
    assert(world_clock_advance_busy(&split, 9000000, WORLD_CLOCK_COST_REFERENCE) == 60);
    assert(split.vblanks == before + 60);
    world_clock_begin_pass(&split);
    uint64_t phase = split.phase;
    before = split.vblanks;
    assert(world_clock_advance_busy(&split, UINT32_MAX, WORLD_CLOCK_COST_UNRECOVERED) == 0);
    assert(split.phase == phase && split.vblanks == before);
    assert(world_clock_advance_hardware(&split, 9000000) == 60);
    assert(split.phase == UINT64_C(4158)*9000000);
    assert(!world_clock_request_mode(&split, (WorldTimingMode)99));
    assert(split.requested_mode == WORLD_TIMING_REMOVE_SLOWDOWN);

    world_clock_reset(&whole, WORLD_TIMING_ARCADE);
    uint32_t max_edges = world_clock_advance_busy(&whole, UINT32_MAX, WORLD_CLOCK_COST_REFERENCE);
    assert(max_edges == (UINT64_C(4294967295)*6666666)/UINT64_C(999792000000));
    assert(whole.phase == (UINT64_C(4294967295)*6666666)%UINT64_C(999792000000));
    assert(world_clock_cycles_until_vblank(&whole) > 0);
    before = whole.vblanks;
    for (unsigned i = 0; i < 1000000; ++i) {
        unsigned cycles = world_clock_wait_vblank(&whole);
        assert(cycles > 0 && cycles <= 149969);
    }
    assert(whole.vblanks == before + 1000000 && whole.phase == 0);
    whole.phase = UINT64_C(5936)*9000000+12345;
    world_clock_raster(&whole, &line, &pixel, &fraction);
    assert(line == 0 && pixel == 0 && fraction == 12345);
    puts("clock boundaries, drift, mode latching, hardware time, large charges and raster phase passed");
    return 0;
}

