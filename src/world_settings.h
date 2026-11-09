#ifndef WORLD_SETTINGS_H
#define WORLD_SETTINGS_H

#include "world_input.h"
#include "world_timing_options.h"

enum {
    WORLD_SETTINGS_VERSION = 1,
    WORLD_SETTINGS_ID_SIZE = 64,
    WORLD_SETTINGS_PATH_SIZE = 1024
};

typedef struct {
    WorldTimingMode timing_mode;
    unsigned border_brightness;
    unsigned volume;
    unsigned fullscreen;
    unsigned vsync;
    unsigned window_scale;
    unsigned achievement_popups;
    char shader[WORLD_SETTINGS_ID_SIZE];
    char border[WORLD_SETTINGS_ID_SIZE];
    char border_image[WORLD_SETTINGS_PATH_SIZE];
    WorldInputBindings bindings;
} WorldSettings;

typedef enum {
    WORLD_SETTINGS_OK,
    WORLD_SETTINGS_MISSING,
    WORLD_SETTINGS_INVALID,
    WORLD_SETTINGS_NEWER_VERSION,
    WORLD_SETTINGS_IO_ERROR
} WorldSettingsStatus;

/* Integer scaling and the original 4:3 viewport are renderer invariants.
 * Arcade timing is the default selection. Its fidelity depends on the clock
 * and recovered routine costs supplied by the native game. */
void world_settings_defaults(WorldSettings *settings);
int world_settings_valid(const WorldSettings *settings);

/* Paths are UTF-8. Loading always initializes settings, including on failure.
 * A newer version is reported so callers can preserve that file.
 * Version-one files use one key=value per line and decimal binding codes.
 * Unknown keys are ignored. Invalid recognized values reject the load. */
WorldSettingsStatus world_settings_load(const char *path, WorldSettings *settings);

/* Save replaces the destination atomically using a temporary file in the
 * same directory. On failure the previous settings file stays intact. */
WorldSettingsStatus world_settings_save(const char *path, const WorldSettings *settings);

#endif
