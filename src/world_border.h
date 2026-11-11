#ifndef WORLD_BORDER_H
#define WORLD_BORDER_H
#include <stdint.h>
#include <windows.h>

/* UTF-8 file path, or an empty string for black. A failed load retains the
 * current selection. Brightness affects the background independently. */
int world_border_set(const char *path, unsigned brightness);
/* Opaque ARGB, with source transparency composited over black. */
const uint32_t *world_border_pixels(unsigned *width, unsigned *height, unsigned *brightness);
void world_border_paint(HDC target, int width, int height);
/* The border art scaled to width x height (ARGB, top-down), or NULL when the
 * border is black. Valid until the next border call. */
const uint32_t *world_border_scaled(int width, int height);
void world_border_close(void);
#endif
