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
/* Pause-menu buttons (see world_window.c); *toggle bit 0 = F1 or L3+R3, bit 1 = Esc. */
unsigned world_window_menu_buttons(unsigned *toggle, int *gamepad);
void world_window_set_display(int fullscreen, unsigned scale);
/* scaling: 0 whole-number, 1 largest 4:3 fit, 2 fill the window. Scanlines
 * need whole-number scaling of at least x2. vsync waits for the compositor. */
void world_window_set_view(int scaling, int scanlines, int vsync);
int world_window_focused(void);
/* Headless: paint border plus frame into a width x height BMP file. */
int world_window_compose_bmp(const uint32_t *pixels, int width, int height, const char *path);
#endif
