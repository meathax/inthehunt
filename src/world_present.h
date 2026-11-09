#ifndef WORLD_PRESENT_H
#define WORLD_PRESENT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned surface_width, surface_height;
    int x, y, width, height, scale;
} WorldPresentViewport;

/* native_window is an HWND. Frames are top-down opaque ARGB8888.
 * d3d11.dll and d3dcompiler_47.dll are loaded from the system directory at
 * the first open; a failed open reports why through world_present_error and
 * leaves the window untouched for another renderer. */
int world_present_open(void *native_window);
int world_present_draw(const uint32_t pixels[320 * 240], int vsync);
void world_present_close(void);
void world_present_viewport(WorldPresentViewport *viewport);
const char *world_present_error(void);

/* t0 is the 320x240 BGRA8 framebuffer, s0 is point/clamp, b0 contains up
 * to 256 bytes. VS output is float2 TEXCOORD0 plus float4 SV_Position.
 * A failed compile preserves the active shader. NULL restores point output. */
int world_present_set_shader(const char *source, const char *entry,
                             const void *constants, size_t size);
int world_present_set_shader_file(const wchar_t *path, const char *entry,
                                  const void *constants, size_t size);
int world_present_set_constants(const void *constants, size_t size);

/* NULL restores black borders. Backdrops cover the window with centered
 * cropping, retaining image aspect. Brightness is in the range 0..100. */
int world_present_set_background(const uint32_t *pixels, unsigned width,
                                 unsigned height, unsigned brightness);
void world_present_set_background_brightness(unsigned brightness);

/* Arm a GPU copy on subsequent draws, then read the most recently drawn
 * surface. Resizing discards the pending screenshot. */
int world_present_request_readback(void);
int world_present_readback(uint32_t *pixels, size_t capacity,
                          unsigned *width, unsigned *height);
#endif
