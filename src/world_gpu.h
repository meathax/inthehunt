#ifndef WORLD_GPU_H
#define WORLD_GPU_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Direct3D 12 presentation for the CRT shaders: the finished 320x240 frame
 * goes through the post chain (librashader .slangp presets and the Blur
 * Busters beam simulator) into a swap chain on a child window that covers
 * the game window. Used only while a CRT shader or the beam simulator is
 * selected; the GDI presenter remains the default. */

enum { WORLD_GPU_PRESETS = 12 };
/* Menu name and path inside the shader folder (tools/make_shaders.py). */
extern const char *const world_gpu_preset_name[WORLD_GPU_PRESETS];
extern const char *const world_gpu_preset_path[WORLD_GPU_PRESETS];

/* Folder holding the shader package; returns 0 if none was found. */
int world_gpu_find_shaders(const char *data_dir, char *out, size_t size);

/* parent: the game window. Returns 0 (with a reason) if Direct3D 12 or the
 * swap chain is unavailable. */
int world_gpu_open(void *parent, char *error, size_t error_size);
void world_gpu_close(void);
int world_gpu_active(void);
/* Full path of a .slangp, or "" for pass-through. Keeps the old preset on failure. */
int world_gpu_set_preset(const char *path, char *error, size_t error_size);
/* scaling: 0 integer, 1 fit 4:3, 2 stretch. */
void world_gpu_set_options(int scaling, int beam, int vsync);
/* Client-area sized ARGB border art (or NULL for black), shown outside the picture. */
void world_gpu_set_background(const uint32_t *pixels, unsigned width, unsigned height);
/* One display refresh. frame is the 320x240 ARGB picture when it changed
 * since the last call (else NULL). source_time counts emulated refreshes
 * (game frames plus the fraction elapsed), for the beam simulator. */
void world_gpu_present(const uint32_t *frame, double source_time);
/* Measured display refresh rate (0 until known). */
double world_gpu_refresh_hz(void);
/* Wait until the swap chain can take another frame (one per refresh with v-sync). */
void world_gpu_wait_frame(void);
/* DXGI statistics: the latest present shown and the refresh it appeared on,
 * and the number of presents made. Returns 0 if unavailable. */
int world_gpu_frame_statistics(unsigned *present_count, unsigned *present_refresh, unsigned *last_present);

/* Headless: render frame through preset (path or "") at width x height into
 * out (ARGB). Creates and releases its own device. */
int world_gpu_render_offscreen(const uint32_t *frame, const char *preset, int scaling,
                               unsigned width, unsigned height, uint32_t *out,
                               char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
