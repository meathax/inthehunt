#ifndef WORLD_TIMING_OPTIONS_H
#define WORLD_TIMING_OPTIONS_H

typedef enum {
    WORLD_TIMING_ARCADE,
    WORLD_TIMING_REMOVE_SLOWDOWN
} WorldTimingMode;

/* Arcade is the default. A successful parse writes a supported value. */
int world_timing_mode_parse(const char *text, WorldTimingMode *mode);
const char *world_timing_mode_name(WorldTimingMode mode);
const char *world_timing_mode_label(WorldTimingMode mode);

#endif
