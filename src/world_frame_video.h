#ifndef WORLD_FRAME_VIDEO_H
#define WORLD_FRAME_VIDEO_H

#include <stdint.h>

/* Render enabled row-scroll planes using the terminal-pass camera and frame tick. */
void world_frame_rowscroll(uint8_t *work, const uint8_t *rom,
                           uint8_t *tiles, uint16_t port4);
#endif

