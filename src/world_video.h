#ifndef WORLD_VIDEO_H
#define WORLD_VIDEO_H

#include <stdint.h>

/* Use the user's imported gfx1 (2 MiB) and gfx2 (4 MiB) planar images. */
uint8_t world_video_tile_pen(const uint8_t *gfx1, unsigned code, unsigned x, unsigned y);
uint8_t world_video_sprite_pen(const uint8_t *gfx2, unsigned code, unsigned x, unsigned y);

typedef struct {
    uint16_t master;       /* I/O 98, 9A or 9C */
    uint16_t scroll_y;     /* layer control word 0 */
    uint16_t scroll_x;     /* layer control word 2 */
} WorldVideoLayer;

typedef struct {
    uint16_t palette_index;
    uint8_t sprite_priority; /* nonzero if any front-half tile pen blocks low-priority sprites */
} WorldVideoPixel;

/* x and y are unflipped picture coordinates. x is signed: a widescreen view
 * samples legacy coordinates below zero on its left wing. Layer 0 is nearest
 * the viewer. Pass the control words active on this line. */
WorldVideoPixel world_video_playfields(const uint8_t *tiles, const uint8_t *gfx1,
                                      const WorldVideoLayer layers[3], uint16_t video_control,
                                      int x, unsigned y);
/* Draw one visible scanline of `width` pixels, including the DMA-buffered
 * 2 KiB sprite list. Raster column 0 of output is legacy picture coordinate
 * `left` (0 for the arcade viewport, -54 for widescreen). Supply active
 * registers/VRAM for this line so raster IRQ changes are retained. */
void world_video_scanline(uint16_t *output, unsigned y, const uint8_t *tiles,
                          const uint8_t *gfx1, const uint8_t *gfx2, const uint8_t *sprite_buffer,
                          const WorldVideoLayer layers[3], uint16_t video_control,
                          unsigned width, int left);

/* Expand a hardware xBGR555 palette word to opaque ARGB8888. */
uint32_t world_video_rgb(uint16_t palette_word);
#endif

