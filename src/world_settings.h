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
    unsigned scaling;            /* 0 integer, 1 fit 4:3, 2 stretch */
    unsigned fps_limit;
    unsigned pacing;             /* 0 smooth (locked to the display), 1 exact 60.01 Hz, 2 timer */          /* index into the frame-rate table; 0 is the arcade rate */
    unsigned show_fps;
    unsigned pause_unfocused;
    unsigned fm_volume, pcm_volume;
    unsigned dip_lives;          /* 0..3 = 2, 3, 4, 5 submarines */
    unsigned dip_difficulty;     /* 0..3 = very easy, easy, normal, hard */
    unsigned dip_demo_sound;
    unsigned crt_shader;         /* 0 off, else 1 + index in world_gpu_preset_path */
    unsigned crt_beam;           /* Blur Busters beam simulator */
    unsigned fx_enable;
    unsigned fx_caustics;
    unsigned fx_rays;
    unsigned fx_lights;
    unsigned fx_bloom;
    unsigned fx_shimmer;
    unsigned fx_depth;
    unsigned fx_snow;
    unsigned fx_vivid;
    unsigned fx_vignette;
    unsigned fx_trails;
    unsigned fx_scanlines;
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
