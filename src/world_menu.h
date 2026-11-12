#ifndef WORLD_MENU_H
#define WORLD_MENU_H
#include <stdint.h>
#include "world_settings.h"

/* Menu buttons, sampled as held levels each frame. */
enum {
    WORLD_MENU_UP = 1, WORLD_MENU_DOWN = 2, WORLD_MENU_LEFT = 4, WORLD_MENU_RIGHT = 8,
    WORLD_MENU_ACCEPT = 16, WORLD_MENU_BACK = 32, WORLD_MENU_TAB_PREV = 64, WORLD_MENU_TAB_NEXT = 128,
    WORLD_MENU_PEEK = 256           /* hold on the enhancements page to see the picture */
};

/* Result bits from world_menu_update. */
enum {
    WORLD_MENU_RESUME = 1,          /* close and continue the game */
    WORLD_MENU_QUIT = 2,            /* leave the program */
    WORLD_MENU_VOLUME = 4,          /* settings->volume changed */
    WORLD_MENU_DISPLAY = 8,         /* fullscreen or window_scale changed */
    WORLD_MENU_BORDER = 16,         /* border, border_image or brightness changed */
    WORLD_MENU_TIMING = 32,         /* timing_mode changed */
    WORLD_MENU_SAVE = 64,           /* persist settings (on close) */
    WORLD_MENU_VIEW = 128,          /* scaling, v-sync, frame rate, enhancements or cabinet switches changed */
    WORLD_MENU_START = 256,         /* title menu: start a game */
    WORLD_MENU_TITLE = 512          /* pause menu: end this game and return to the title menu */
};

enum { WORLD_MENU_BORDER_LIMIT = 8, WORLD_MENU_FPS_CHOICES = 6 };
/* Frames per second for settings->fps_limit; 0 is the arcade rate and the
 * last entry (UINT_MAX) is uncapped. */
extern const unsigned world_menu_fps[WORLD_MENU_FPS_CHOICES];

typedef struct {
    unsigned open, page, tab, row, rows[4];
    unsigned title;                 /* the title menu (start, options, exit) rather than the pause menu */
    unsigned previous, repeat_buttons, gamepad, dirty;
    unsigned peek;                  /* the menu is hidden while the preview button is held */
    uint32_t repeat_at, opened_at;
    unsigned border_count, border_index;
    char border_name[WORLD_MENU_BORDER_LIMIT][24];
    char border_path[WORLD_MENU_BORDER_LIMIT][WORLD_SETTINGS_PATH_SIZE];
} WorldMenu;

/* Border choices; index 0 must be black (empty path). */
void world_menu_set_borders(WorldMenu *menu, unsigned count, const char *const names[],
                            const char *const paths[], const WorldSettings *settings);
/* held is the button level when opened so that key is not acted on twice. */
void world_menu_open(WorldMenu *menu, unsigned held, int gamepad, uint32_t now_ms);
/* The title menu, shown over the running attract sequence. */
void world_menu_open_title(WorldMenu *menu, unsigned held, int gamepad, uint32_t now_ms);
unsigned world_menu_update(WorldMenu *menu, WorldSettings *settings, unsigned held,
                           int gamepad, uint32_t now_ms);
/* canvas: the 320x240 ARGB game frame, replaced by the menu over it.
 * gfx1 supplies the game's own 8x8 and 16x16 lettering. */
void world_menu_render(const WorldMenu *menu, const WorldSettings *settings,
                       uint32_t *canvas, const uint8_t *gfx1, uint32_t now_ms);

/* Small 5x7 host lettering kept for the achievement view. */
void world_menu_draw_text(uint32_t *pixels, unsigned width, unsigned height,
                          int x, int y, unsigned scale, uint32_t color, const char *text);
int world_menu_text_width(const char *text, unsigned scale);
#endif
