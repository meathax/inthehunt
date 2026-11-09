#ifndef WORLD_WINDOW_H
#define WORLD_WINDOW_H
#include <stddef.h>
#include <stdint.h>
#include "world_input.h"

typedef struct {
    unsigned window_scale; /* Whole-number multiple of 320x240, clamped to the desktop. */
    int fullscreen;        /* Borderless, on the monitor that holds the window. */
    int vsync;             /* Direct3D only: wait for the display on present. */
    const char *shader;    /* world_crt profile id; NULL or unknown selects pixels. */
} WorldWindowOptions;

/* A 320x240, top-down ARGB8888 framebuffer. The caller owns simulation time.
 * The picture is always a centered whole-number multiple of 320x240.
 * Direct3D 11 (world_present) draws it when the device initialises;
 * otherwise the window falls back to GDI, which has no CRT profiles or vsync.
 * Invalid or NULL bindings select the default controls. */
int world_window_open(const WorldWindowOptions *options, const WorldInputBindings *bindings);
/* Pump messages and controllers. Raw cabinet words are active-low. */
int world_window_poll(uint16_t *pads, uint16_t *system);
/* Keyboard and controller state behind the cabinet words, for menus and
 * binding capture. Valid while the window is open. */
WorldInput *world_window_input(void);
/* allow_vsync is zero for a frame that is already late, so a display slower
 * than the board never holds the simulation back. */
/* Nonzero once for each fresh Esc press since the previous call. */
int world_window_take_menu_request(void);
/* Modal file picker for border artwork. Writes a UTF-8 path and returns
 * nonzero when a file was chosen and fits in capacity bytes. */
int world_window_pick_image(char *path, size_t capacity);
void world_window_present(const uint32_t *pixels, int allow_vsync);
/* Nonzero while Direct3D presents; zero on the GDI fallback. */
int world_window_direct3d(void);
/* Id of the world_crt profile in use; "pixels" on GDI. */
const char *world_window_shader(void);
/* Select a world_crt profile by id. Returns zero, keeping the previous
 * profile, on GDI, for an unknown id or when the shader is rejected. */
int world_window_set_shader(const char *id);
void world_window_set_vsync(int enabled);
/* Empty UTF-8 path selects black. Brightness is background-only, 0..100. */
int world_window_set_border(const char *path, unsigned brightness);
/* Resize the windowed picture. Returns the scale that fits the desktop;
 * while fullscreen the value is kept for the return to a window. */
unsigned world_window_set_scale(unsigned scale);
void world_window_set_fullscreen(int fullscreen);
void world_window_close(void);
#endif
