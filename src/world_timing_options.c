#include "world_timing_options.h"
#include <string.h>

int world_timing_mode_parse(const char *text, WorldTimingMode *mode)
{
    if (!text || !mode) return 0;
    if (!strcmp(text, "arcade") || !strcmp(text, "Arcade")) {
        *mode = WORLD_TIMING_ARCADE;
        return 1;
    }
    if (!strcmp(text, "remove-slowdown") || !strcmp(text, "RemoveSlowdown")) {
        *mode = WORLD_TIMING_REMOVE_SLOWDOWN;
        return 1;
    }
    return 0;
}

const char *world_timing_mode_name(WorldTimingMode mode)
{
    return mode == WORLD_TIMING_REMOVE_SLOWDOWN ? "remove-slowdown" : "arcade";
}

const char *world_timing_mode_label(WorldTimingMode mode)
{
    return mode == WORLD_TIMING_REMOVE_SLOWDOWN ? "Remove slowdown" : "Arcade slowdown";
}
