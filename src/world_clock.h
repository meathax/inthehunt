#ifndef WORLD_CLOCK_H
#define WORLD_CLOCK_H

#include <stdint.h>
#include "world_timing_options.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    WORLD_CLOCK_CPU_HZ = 9000000,
    WORLD_CLOCK_PIXEL_HZ = 6666666,
    WORLD_CLOCK_LINE_PIXELS = 424,
    WORLD_CLOCK_FRAME_LINES = 262,
    WORLD_CLOCK_FRAME_PIXELS = 424 * 262,
    WORLD_CLOCK_VBLANK_LINE = 248
};

#define WORLD_CLOCK_FRAME_UNITS (UINT64_C(9000000) * WORLD_CLOCK_FRAME_PIXELS)

typedef enum {
    WORLD_CLOCK_COST_UNRECOVERED,
    WORLD_CLOCK_COST_REFERENCE,
    WORLD_CLOCK_COST_MEASURED
} WorldClockCostBasis;

typedef struct {
    /* Phase since the latest VBlank edge, in units of 1/(CPU_HZ*PIXEL_HZ)
       seconds. One CPU cycle adds PIXEL_HZ units. */
    uint64_t phase;
    uint64_t vblanks;
    uint64_t busy_vblanks;
    uint64_t passes;
    uint64_t charged_cycles;
    uint64_t reference_costs;
    uint64_t measured_costs;
    uint64_t unrecovered_costs;
    WorldTimingMode requested_mode;
    WorldTimingMode pass_mode;
} WorldClock;

/* The origin is a VBlank rising edge. A bootstrap with a measured phase may
 * initialize phase to pixel_offset*CPU_HZ plus a fractional pixel numerator.
 * No workload or ROM data is stored in this scheduler. */
void world_clock_reset(WorldClock *clock, WorldTimingMode mode);
int world_clock_request_mode(WorldClock *clock, WorldTimingMode mode);
/* Apply a requested policy at the next complete logic pass. */
void world_clock_begin_pass(WorldClock *clock);

/* Charge original workload cycles at an interruptible source checkpoint.
 * Return the physical VBlank edges crossed. The caller services those edges
 * through the busy IRQ branch before executing the next game-state mutation.
 * Whole-handler totals cannot establish intra-handler interrupt placement.
 * In Remove Slowdown mode these workload cycles consume no physical time. */
uint32_t world_clock_advance_busy(WorldClock *clock, uint32_t cycles, WorldClockCostBasis basis);

/* Account for an original block whose execution cost has not been recovered.
 * This records missing coverage and introduces no invented elapsed time. */
void world_clock_note_unrecovered(WorldClock *clock);

/* Hardware waits remain real time under either policy. The returned edges
 * still require the appropriate IRQ service by the caller. */
uint32_t world_clock_advance_hardware(WorldClock *clock, uint32_t cycles);

/* Wait until the next physical VBlank, returning its elapsed CPU-cycle
 * ceiling. This creates one ordinary frame boundary, never a busy boundary.
 * B892 spin-loop iterations need the separately recovered wait-loop cost. */
uint32_t world_clock_wait_vblank(WorldClock *clock);
uint32_t world_clock_cycles_until_vblank(const WorldClock *clock);

/* Raster position at the current physical phase. subpixel is a numerator
 * over CPU_HZ. It permits phase-sensitive VRAM READY accounting. */
void world_clock_raster(const WorldClock *clock, unsigned *line, unsigned *pixel,
                        uint32_t *subpixel);
/* True only after at least one cost has been charged and every charged
 * workload cost was supplied as measured. It describes coverage supplied
 * by the caller, not an independent PCB-accuracy certification. */
int world_clock_has_measured_costs(const WorldClock *clock);

#ifdef __cplusplus
}
#endif
#endif
