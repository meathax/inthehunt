#ifndef WORLD_WINDOW_H
#define WORLD_WINDOW_H
#include <stdint.h>

/* A 320x240, top-down ARGB8888 framebuffer. The caller owns simulation time. */
int world_window_open(void);
int world_window_poll(uint16_t *pads, uint16_t *system);
void world_window_present(const uint32_t *pixels);
/* Empty UTF-8 path selects black. Brightness is background-only, 0..100. */
int world_window_set_border(const char *path, unsigned brightness);
void world_window_close(void);
#endif
