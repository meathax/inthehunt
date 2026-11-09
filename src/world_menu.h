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
    WORLD_MENU_RETURN_TITLE = 128,
    /* List Boss Rush and Score Attack even while they cannot start. A row
       without its own capability bit is drawn locked, cannot be accepted and
       shows the reason supplied through world_menu_set_mode_reasons. */
    WORLD_MENU_MODES = 256,
    /* Display page rows: fullscreen and window size, CRT profile, vsync. */
    WORLD_MENU_DISPLAY = 512, WORLD_MENU_SHADERS = 1024, WORLD_MENU_VSYNC = 2048
};
typedef enum {
    WORLD_MENU_NONE, WORLD_MENU_START_GAME, WORLD_MENU_RESUME,
    WORLD_MENU_EXIT, WORLD_MENU_TITLE, WORLD_MENU_OPEN_CONTROLS,
    WORLD_MENU_OPEN_ACHIEVEMENTS, WORLD_MENU_PICK_BACKGROUND,
    WORLD_MENU_START_BOSS_RUSH, WORLD_MENU_START_SCORE_ATTACK,
    WORLD_MENU_SETTINGS_CHANGED
} WorldMenuCommand;

/* options selects the page: 0 main, 1 options, 2 display. */
typedef struct {
    unsigned open, paused, options, selected, return_selected, capabilities;
    unsigned previous_buttons, repeat_button;
    uint32_t repeat_at;
    unsigned options_selected;
    const char *boss_rush_reason, *score_attack_reason;
} WorldMenu;

/* Supply buttons currently held to consume the key that opened this menu.
 * Capability bits must describe implemented host features. */
void world_menu_open(WorldMenu *menu, int paused, unsigned capabilities,
                     unsigned held_buttons, uint32_t now_ms);
void world_menu_close(WorldMenu *menu);
/* Text from world_modes_unavailable_reason, borrowed while the menu is open. */
void world_menu_set_mode_reasons(WorldMenu *menu, const char *boss_rush,
                                 const char *score_attack);
/* Fixed menu controls from the live input state: arrow keys, Enter and Esc,
 * plus either controller's D-pad or left stick, A to accept and B to go back.
 * Reports nothing while a binding is being captured or controls are muted. */
unsigned world_menu_buttons(const WorldInput *input);
/* The menu's own press detection for other host screens: fresh presses,
 * plus repeats of held navigation buttons in repeat_mask (320 ms, then 90 ms). */
typedef struct {
    unsigned previous, repeat_button;
    uint32_t repeat_at;
} WorldMenuRepeat;
unsigned world_menu_pressed(WorldMenuRepeat *state, unsigned held_buttons, uint32_t now_ms,
                            unsigned repeat_mask);
WorldMenuCommand world_menu_update(WorldMenu *menu, WorldSettings *settings,
                                   unsigned held_buttons, uint32_t now_ms);
/* Separate host UI canvas. game_frame is optional, read-only 320x240 ARGB.
 * width/height must fit at least 320x240. No game state is modified. */
void world_menu_render(const WorldMenu *menu, const WorldSettings *settings,
                       uint32_t *output, unsigned width, unsigned height,
                       const uint32_t *game_frame);

/* Control remapping page. The host owns the WorldInput capture: on
 * WORLD_MENU_CONTROLS_CAPTURE it calls world_input_capture_begin with the
 * page's player, selected action and source, then polls the capture. */
typedef enum {
    WORLD_MENU_CONTROLS_NONE, WORLD_MENU_CONTROLS_CAPTURE,
    WORLD_MENU_CONTROLS_CHANGED, WORLD_MENU_CONTROLS_CLOSE
} WorldMenuControlsCommand;
typedef struct {
    unsigned open, selected, player;
    WorldInputSource source;
    unsigned previous_buttons, repeat_button;
    uint32_t repeat_at;
} WorldMenuControls;
void world_menu_controls_open(WorldMenuControls *page, unsigned held_buttons, uint32_t now_ms);
/* Left/right switch between both players' keyboard and controller tables.
 * CHANGED reports that Restore defaults rewrote the shown table. */
WorldMenuControlsCommand world_menu_controls_update(WorldMenuControls *page,
                                                    WorldInputBindings *bindings,
                                                    unsigned held_buttons, uint32_t now_ms);
/* output is 320x240. capturing marks the selected row as waiting for input. */
void world_menu_controls_render(const WorldMenuControls *page, const WorldInputBindings *bindings,
                                int capturing, uint32_t *output, const uint32_t *game_frame);

void world_menu_draw_text(uint32_t *pixels, unsigned width, unsigned height,
                          int x, int y, unsigned scale, uint32_t color, const char *text);
int world_menu_text_width(const char *text, unsigned scale);
#endif
