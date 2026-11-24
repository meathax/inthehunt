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
    unsigned volume;
    unsigned fullscreen;
    unsigned vsync;
    unsigned window_scale;
    unsigned achievement_popups;
    unsigned scaling;            /* 0 integer, 1 fit 4:3, 2 stretch */
    unsigned fps_limit;
    unsigned pacing;             /* 0 display-locked, 1 exact arcade clock */
    unsigned show_fps;
    unsigned pause_unfocused;
    unsigned fm_volume, pcm_volume, spatial_audio;
    unsigned ambience_volume, reduced_bass;
    unsigned dip_lives;          /* 0..3 = 2, 3, 4, 5 submarines */
    unsigned dip_difficulty;     /* 0..3 = very easy, easy, normal, hard */
    unsigned dip_demo_sound;
    unsigned crt_shader;         /* 0 off, else 1 + index in world_gpu_preset_path */
    unsigned autofire;           /* held fire buttons repeat */
    unsigned gfx_quality;
    unsigned fx_enable;
    unsigned enhance_preset_pending;
    unsigned fx_rays;
    unsigned fx_lights;
    unsigned fx_depth;
    unsigned fx_snow;
    unsigned fx_vivid;
    unsigned fx_trails;
    unsigned fx_haze;
    unsigned fx_sunshadow;
    unsigned fx_glow;
    unsigned fx_reflect;
    unsigned fx_shock;
    unsigned fx_particles;
    unsigned fx_shake;
    unsigned fx_surface;
    unsigned fx_smooth;
    unsigned fx_interp;
    unsigned fx_wakes;
    unsigned fx_debris;
    unsigned fx_silt;
    unsigned fx_splashes;
    unsigned fx_sparks, fx_impact_motes;
    unsigned fx_pressure, fx_sediment_light, fx_water_volume, fx_ocean_depth, fx_aftermath, fx_materials;
    unsigned headlight_level, fx_headlights, fx_rim, fx_bubble_light, fx_contact_shade, fx_blast_reflect;
    unsigned brightness;         /* picture brightness curve, percent (100 = arcade) */
    unsigned auto_quality;       /* lighten High lighting when the GPU falls behind */
    unsigned ambient_borders;    /* fill the bars beside the picture with its blurred colours */
    unsigned fx_caustics;        /* sunlit seabed caustics */
    unsigned fx_radiance;        /* radiance cascades: smooth, long-range bounce light */
    /* Effect strengths in percent (25..200, 100 = as designed). */
    unsigned light_strength, shadow_strength, depth_strength, shaft_strength, rim_strength, caustic_strength;
    char shader[WORLD_SETTINGS_ID_SIZE];
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
enum { WORLD_GFX_LOW=1, WORLD_GFX_MEDIUM, WORLD_GFX_HIGH };
void world_settings_apply_quality(WorldSettings *settings, unsigned quality);
int world_settings_quality_custom(const WorldSettings *settings);
void world_settings_activate_enhancements(WorldSettings *settings);
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
