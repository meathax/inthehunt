#ifndef WORLD_MENU_H
#define WORLD_MENU_H
#include <stdint.h>
#include "world_settings.h"

enum {
    WORLD_MENU_UP = 1, WORLD_MENU_DOWN = 2, WORLD_MENU_LEFT = 4,
    WORLD_MENU_RIGHT = 8, WORLD_MENU_ACCEPT = 16, WORLD_MENU_BACK = 32
};
enum {
    WORLD_MENU_SOUND = 1, WORLD_MENU_TIMING = 2, WORLD_MENU_BORDERS = 4,
    WORLD_MENU_CONTROLS = 8, WORLD_MENU_ACHIEVEMENTS = 16,
    WORLD_MENU_BOSS_RUSH = 32, WORLD_MENU_SCORE_ATTACK = 64,
    WORLD_MENU_RETURN_TITLE = 128
};
typedef enum {
    WORLD_MENU_NONE, WORLD_MENU_START_GAME, WORLD_MENU_RESUME,
    WORLD_MENU_EXIT, WORLD_MENU_TITLE, WORLD_MENU_OPEN_CONTROLS,
    WORLD_MENU_OPEN_ACHIEVEMENTS, WORLD_MENU_PICK_BACKGROUND,
    WORLD_MENU_START_BOSS_RUSH, WORLD_MENU_START_SCORE_ATTACK,
    WORLD_MENU_SETTINGS_CHANGED
} WorldMenuCommand;

typedef struct {
    unsigned open, paused, options, selected, return_selected, capabilities;
    unsigned previous_buttons, repeat_button;
    uint32_t repeat_at;
} WorldMenu;

/* Supply buttons currently held to consume the key that opened this menu.
 * Capability bits must describe implemented host features. */
void world_menu_open(WorldMenu *menu, int paused, unsigned capabilities,
                     unsigned held_buttons, uint32_t now_ms);
void world_menu_close(WorldMenu *menu);
WorldMenuCommand world_menu_update(WorldMenu *menu, WorldSettings *settings,
                                   unsigned held_buttons, uint32_t now_ms);
/* Separate host UI canvas. game_frame is optional, read-only 320x240 ARGB.
 * width/height must fit at least 320x240. No game state is modified. */
void world_menu_render(const WorldMenu *menu, const WorldSettings *settings,
                       uint32_t *output, unsigned width, unsigned height,
                       const uint32_t *game_frame);
void world_menu_draw_text(uint32_t *pixels, unsigned width, unsigned height,
                          int x, int y, unsigned scale, uint32_t color, const char *text);
int world_menu_text_width(const char *text, unsigned scale);
#endif
