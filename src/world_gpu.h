#ifndef WORLD_GPU_H
#define WORLD_GPU_H
#include <stddef.h>
#include <stdint.h>
#include "world_gpu_light.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The Direct3D 12 flip-model swap chain presents to the game window. */

enum { WORLD_GPU_PRESETS = 0
#define WORLD_CRT_PRESET(name, path) + 1
#include "world_crt_presets.inc"
#undef WORLD_CRT_PRESET
};
/* Menu name and path inside the shader folder. */
extern const char *const world_gpu_preset_name[WORLD_GPU_PRESETS];
extern const char *const world_gpu_preset_path[WORLD_GPU_PRESETS];

/* Folder holding the shader package; returns 0 if none was found. */
int world_gpu_find_shaders(const char *data_dir, char *out, size_t size);

/* parent: the game window. Returns 0 (with a reason) if Direct3D 12 or the
 * swap chain is unavailable. */
int world_gpu_open(void *parent, char *error, size_t error_size);
void world_gpu_close(void);
int world_gpu_active(void);
int world_gpu_transport_available(void);
void world_gpu_prepare_lights(const WorldLightScene *scene);
void world_gpu_camera_offsets(const float *offsets);
void world_gpu_light_frame(const WorldLightScene *frame);
/* Hide or show the view; while hidden the window draws itself with GDI. */
void world_gpu_show(int show);
int world_gpu_visible(void);
/* Full path of a .slangp, or "" for pass-through. Keeps the old preset on failure. */
int world_gpu_set_preset(const char *path, char *error, size_t error_size);
/* Preset compilation status: 1 compiling (or waiting to be installed),
 * -1 failed (message set), 0 idle. world_gpu_set_preset only queues it. */
int world_gpu_preset_status(char *message, size_t size);
/* scaling: 0 integer, 1 fit 4:3, 2 stretch. */
void world_gpu_set_options(int scaling, int beam, int vsync);
/* One display refresh. frame is the ARGB picture, k times source_w x 240
 * (source_w 320 arcade, 428 widescreen), when it changed since the last call
 * (else NULL). source_time counts emulated refreshes (game frames plus the
 * fraction elapsed), for the beam simulator. */
void world_gpu_present(const uint32_t *frame, int k, double source_time, unsigned source_w);
/* Measured display refresh rate (0 until known). */
int world_gpu_request_vrr(int enable);
double world_gpu_refresh_hz(void);
/* QPC timestamp of a display sync and its refresh counter. */
int world_gpu_sync_sample(int64_t *qpc, unsigned *refresh);
/* Wait until the swap chain can take another frame (one per refresh with v-sync). */
void world_gpu_wait_frame(void);
/* DXGI statistics: the latest present shown and the refresh it appeared on,
 * and the number of presents made. Returns 0 if unavailable. */
int world_gpu_frame_statistics(unsigned *present_count, unsigned *present_refresh, unsigned *last_present);

/* Headless: render frame through preset (path or "") at width x height into
 * out (ARGB). Creates and releases its own device. */
int world_gpu_camera_offscreen(const float offsets[4],const uint32_t *pixels,uint32_t *out,
                               unsigned source_w,unsigned scale,char *error,size_t size);
/* VRR was requested but the display kept its fixed refresh; vsync is used. */
int world_gpu_vrr_failed(void);
/* Probe VRR again (after a display or mode change). */
void world_gpu_retry_vrr(void);
/* Fill the bars beside the picture with a dim blur of its edges (else black). */
void world_gpu_set_ambient_borders(int on);
int world_gpu_light_offscreen(const WorldLightScene *scene,const uint32_t *pixels,uint32_t *out,
                              unsigned source_w,char *error,size_t size);
/* Developer benchmark: on one device, rebuild and shade each captured k=1
   scene `runs` times with each shader variant (NULL source = built-in),
   interleaved after a warm-up. pass_ms and shaded are indexed
   [variant * count + scene]; pass order is sun, direct, indirect, resolve,
   upload, volume, shade, copy. Shaded pictures keep their plane tags. */
int world_gpu_light_benchmark(const WorldLightScene *const *scenes,const uint32_t *const *pixels,
                              unsigned count,const char *const *sources,unsigned variants,unsigned runs,
                              double (*pass_ms)[8],uint32_t *const *shaded,char *error,size_t size);
int world_gpu_render_offscreen(const uint32_t *frame, unsigned source_w, const char *preset, int scaling,
                               unsigned width, unsigned height, uint32_t *out,
                               char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
