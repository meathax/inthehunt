#ifndef WORLD_STAGE6_TIMING_H
#define WORLD_STAGE6_TIMING_H

#include <stdint.h>

enum { APPROACH_WARSHIP = 1, APPROACH_GUNSHIP = 2, APPROACH_MISSILES = 4 };

#include "world_stage6_profile.h"

typedef struct {
    struct {
        uint16_t entry_vblank;
        uint16_t next_pass; /* zero outside the window, one while waiting */
    } burst[sizeof stage6_bursts / sizeof stage6_bursts[0]];
} Stage6Timing;

/* A negative result leaves the ordinary workload estimate in charge. Zero
 * inside a matched attack is a measured update on the next refresh. */
static int stage6_approach_step(Stage6Timing *timing, unsigned scroll, unsigned enemies, uint16_t vblank)
{
    int holds = -1;
    for (unsigned i = 0; i < sizeof timing->burst / sizeof timing->burst[0]; ++i) {
        if (scroll < stage6_bursts[i].entry || scroll > stage6_bursts[i].exit) {
            timing->burst[i].next_pass = 0;
            continue;
        }
        if (!timing->burst[i].next_pass) {
            timing->burst[i].entry_vblank = vblank;
            timing->burst[i].next_pass = 1;
        }
        unsigned step = timing->burst[i].next_pass - 1;
        unsigned length = stage6_bursts[i].length;
        if (step >= length) continue;
        /* A faster route can pass the spot where the PCB player waited. */
        uint16_t elapsed = (uint16_t)(vblank - timing->burst[i].entry_vblank);
        if (!step && elapsed < stage6_bursts[i].delay && scroll <= stage6_bursts[i].onset + 6u) continue;
        ++timing->burst[i].next_pass;
        if (stage6_bursts[i].enemy && !(enemies & stage6_bursts[i].enemy)) continue;
        unsigned packed = stage6_profile[stage6_bursts[i].profile_offset + step / 4];
        int extra = (packed >> (2 * (step % 4))) & 3;
        if (holds < extra) holds = extra;
    }
    return holds;
}
static void stage6_legacy_restore(Stage6Timing *timing, unsigned scroll)
{
    for (unsigned i = 0; i < sizeof timing->burst / sizeof timing->burst[0]; ++i)
        if (scroll > stage6_bursts[i].entry && scroll <= stage6_bursts[i].exit)
            timing->burst[i].next_pass = stage6_bursts[i].length + 1;
}
#endif
