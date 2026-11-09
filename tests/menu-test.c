#include "../src/world_menu.h"
#include "../src/world_crt.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){printf("FAIL line%d %s\n",__LINE__,#c);return 1;}}while(0)

static uint32_t clock_ms;
static WorldSettings settings;
static uint32_t canvas[320*240], game[320*240];

/* One fresh press and release. */
static WorldMenuCommand press(WorldMenu *menu, unsigned button)
{
    WorldMenuCommand command = world_menu_update(menu, &settings, button, clock_ms += 16);
    world_menu_update(menu, &settings, 0, clock_ms += 16);
    return command;
}

/* Rows on the current page, counted by walking down until the cursor wraps. */
static unsigned rows(WorldMenu *menu)
{
    unsigned count = 0, start = menu->selected;
    do { press(menu, WORLD_MENU_DOWN); ++count; } while (menu->selected != start && count < 32);
    return count;
}

static WorldMenuControlsCommand control(WorldMenuControls *page, unsigned button)
{
    WorldMenuControlsCommand command = world_menu_controls_update(page, &settings.bindings, button, clock_ms += 16);
    world_menu_controls_update(page, &settings.bindings, 0, clock_ms += 16);
    return command;
}

int main(void)
{
    memset(&settings, 0, sizeof settings);
    settings.volume = 100; settings.border_brightness = 100; settings.window_scale = 3;
    strcpy(settings.shader, "pixels"); strcpy(settings.border, "black");
    world_input_bindings_defaults(&settings.bindings);
    const unsigned base = WORLD_MENU_SOUND | WORLD_MENU_TIMING | WORLD_MENU_BORDERS | WORLD_MENU_CONTROLS |
                          WORLD_MENU_DISPLAY;
    WorldMenu menu;

    /* The button that opened the menu is consumed; Back resumes a paused game. */
    world_menu_open(&menu, 1, base, WORLD_MENU_BACK, clock_ms);
    CHECK(world_menu_update(&menu, &settings, WORLD_MENU_BACK, clock_ms += 16) == WORLD_MENU_NONE);
    world_menu_update(&menu, &settings, 0, clock_ms += 16);
    CHECK(rows(&menu) == 3); /* Resume, Options, Exit game. */
    CHECK(press(&menu, WORLD_MENU_BACK) == WORLD_MENU_RESUME && !menu.open);

    /* Locked ranked modes are listed, cannot be accepted and keep their reason. */
    world_menu_open(&menu, 1, base | WORLD_MENU_MODES | WORLD_MENU_ACHIEVEMENTS, 0, clock_ms);
    world_menu_set_mode_reasons(&menu, "Boss encounters are still being recovered.",
                                "Native campaign launch is still being connected.");
    CHECK(rows(&menu) == 6); /* Resume, Options, Achievements, Boss Rush, Score Attack, Exit game. */
    press(&menu, WORLD_MENU_DOWN); press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_OPEN_ACHIEVEMENTS);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_NONE && menu.open);
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_NONE);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_NONE && menu.open);
    memset(canvas, 0, sizeof canvas);
    world_menu_render(&menu, &settings, canvas, 320, 240, game);
    CHECK(canvas[14*320+14] == 0xffa7894c);
    /* With a launch function the same row starts the mode. */
    menu.capabilities |= WORLD_MENU_SCORE_ATTACK;
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_START_SCORE_ATTACK);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_EXIT);

    /* A launch menu without the listing shows only modes that can start. */
    world_menu_open(&menu, 0, base, 0, clock_ms);
    CHECK(rows(&menu) == 3);
    world_menu_open(&menu, 0, base | WORLD_MENU_BOSS_RUSH, 0, clock_ms);
    CHECK(rows(&menu) == 4);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_START_BOSS_RUSH);

    /* Options and the display page offer only rows the host declared. */
    world_menu_open(&menu, 1, base, 0, clock_ms);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_NONE && menu.options == 1);
    CHECK(rows(&menu) == 5); /* Display, Volume, Slowdown, Controls, Back. */
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_NONE && menu.options == 2);
    CHECK(rows(&menu) == 5); /* Fullscreen, Window size, Border, Brightness, Back. */
    menu.capabilities |= WORLD_MENU_SHADERS | WORLD_MENU_VSYNC;
    CHECK(rows(&menu) == 7);

    /* Fullscreen hides the window size without moving the cursor. */
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_SETTINGS_CHANGED && settings.fullscreen == 1);
    CHECK(rows(&menu) == 6 && menu.selected == 0);
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_SETTINGS_CHANGED && settings.fullscreen == 0);

    /* Window size stays within 1..12. */
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_SETTINGS_CHANGED && settings.window_scale == 4);
    settings.window_scale = 12;
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_NONE && settings.window_scale == 12);
    settings.window_scale = 1;
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_NONE && settings.window_scale == 1);

    /* The display profile cycles through every world_crt id in both directions. */
    press(&menu, WORLD_MENU_DOWN);
    for (unsigned i = 1; i <= WORLD_CRT_COUNT; ++i) {
        CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_SETTINGS_CHANGED);
        CHECK(!strcmp(settings.shader, world_crt_id((WorldCrtProfile)(i % WORLD_CRT_COUNT))));
    }
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_SETTINGS_CHANGED);
    CHECK(!strcmp(settings.shader, world_crt_id((WorldCrtProfile)(WORLD_CRT_COUNT - 1))));
    strcpy(settings.shader, "not-a-profile");
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_SETTINGS_CHANGED && !strcmp(settings.shader, "lottes-flat"));

    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_SETTINGS_CHANGED && settings.vsync == 1);

    /* Border: no artwork yet asks the host for a file; artwork toggles. */
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_PICK_BACKGROUND && !strcmp(settings.border, "black"));
    strcpy(settings.border_image, "C:/art/hull.png");
    CHECK(press(&menu, WORLD_MENU_RIGHT) == WORLD_MENU_SETTINGS_CHANGED && !strcmp(settings.border, "custom"));
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_SETTINGS_CHANGED && !strcmp(settings.border, "black"));
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_SETTINGS_CHANGED && settings.border_brightness == 95);
    world_menu_render(&menu, &settings, canvas, 320, 240, NULL);

    /* Back steps out one page at a time and restores each cursor. */
    CHECK(press(&menu, WORLD_MENU_BACK) == WORLD_MENU_NONE && menu.options == 1 && menu.selected == 0);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_LEFT) == WORLD_MENU_SETTINGS_CHANGED && settings.volume == 95);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_SETTINGS_CHANGED &&
          settings.timing_mode == WORLD_TIMING_REMOVE_SLOWDOWN);
    press(&menu, WORLD_MENU_DOWN);
    CHECK(press(&menu, WORLD_MENU_ACCEPT) == WORLD_MENU_OPEN_CONTROLS);
    CHECK(press(&menu, WORLD_MENU_BACK) == WORLD_MENU_NONE && menu.options == 0 && menu.selected == 1);

    /* Fixed menu controls from keyboard and either controller. */
    WorldInput input;
    world_input_init(&input, &settings.bindings);
    CHECK(world_menu_buttons(&input) == 0);
    world_input_key(&input, 0x26, 1); world_input_key(&input, 0x0d, 1);
    CHECK(world_menu_buttons(&input) == (WORLD_MENU_UP | WORLD_MENU_ACCEPT));
    world_input_key(&input, 0x26, 0); world_input_key(&input, 0x0d, 0);
    world_input_key(&input, 0x1b, 1);
    CHECK(world_menu_buttons(&input) == WORLD_MENU_BACK);
    world_input_key(&input, 0x1b, 0);
    WorldInputController pad = {0};
    pad.connected = 1; pad.buttons = 0x2000 | 2; pad.left_x = 20000;
    world_input_controller(&input, 1, &pad);
    CHECK(world_menu_buttons(&input) == (WORLD_MENU_BACK | WORLD_MENU_DOWN | WORLD_MENU_RIGHT));
    /* Nothing leaks into the menu while a binding is captured or muted. */
    CHECK(world_input_capture_begin(&input, 0, WORLD_INPUT_FIRE1, WORLD_INPUT_KEYBOARD));
    CHECK(world_menu_buttons(&input) == 0);
    world_input_controller(&input, 1, NULL);
    CHECK(world_input_capture_poll(&input, NULL) == WORLD_INPUT_CAPTURE_WAITING);
    world_input_key(&input, 'Q', 1);
    CHECK(world_input_capture_poll(&input, NULL) == WORLD_INPUT_CAPTURE_BOUND);
    CHECK(world_menu_buttons(&input) == 0);
    world_input_key(&input, 'Q', 0);
    world_input_set_focus(&input, 0);
    CHECK(world_menu_buttons(&input) == 0);

    /* Controls page: four tables, capture request, defaults, close. */
    WorldMenuControls page;
    world_menu_controls_open(&page, WORLD_MENU_ACCEPT, clock_ms);
    CHECK(world_menu_controls_update(&page, &settings.bindings, WORLD_MENU_ACCEPT, clock_ms += 16) ==
          WORLD_MENU_CONTROLS_NONE);
    world_menu_controls_update(&page, &settings.bindings, 0, clock_ms += 16);
    CHECK(page.player == 0 && page.source == WORLD_INPUT_KEYBOARD && page.selected == WORLD_INPUT_RIGHT);
    CHECK(control(&page, WORLD_MENU_ACCEPT) == WORLD_MENU_CONTROLS_CAPTURE);
    control(&page, WORLD_MENU_RIGHT);
    CHECK(page.player == 0 && page.source == WORLD_INPUT_CONTROLLER);
    control(&page, WORLD_MENU_RIGHT);
    CHECK(page.player == 1 && page.source == WORLD_INPUT_KEYBOARD);
    control(&page, WORLD_MENU_RIGHT); control(&page, WORLD_MENU_RIGHT);
    CHECK(page.player == 0 && page.source == WORLD_INPUT_KEYBOARD);
    control(&page, WORLD_MENU_LEFT);
    CHECK(page.player == 1 && page.source == WORLD_INPUT_CONTROLLER);
    for (unsigned i = 0; i < WORLD_INPUT_FIRE2; ++i) control(&page, WORLD_MENU_DOWN);
    CHECK(page.selected == WORLD_INPUT_FIRE2);
    CHECK(control(&page, WORLD_MENU_ACCEPT) == WORLD_MENU_CONTROLS_CAPTURE);
    world_menu_controls_render(&page, &settings.bindings, 1, canvas, game);
    CHECK(canvas[14*320+14] == 0xffa7894c);
    /* Restore defaults rewrites only the table on screen. */
    settings.bindings.controller[1][WORLD_INPUT_FIRE2] = WORLD_PAD_Y;
    settings.bindings.controller[0][WORLD_INPUT_FIRE2] = WORLD_PAD_X;
    settings.bindings.keyboard[1][WORLD_INPUT_FIRE2] = 'Q';
    while (page.selected != WORLD_INPUT_ACTIONS) control(&page, WORLD_MENU_DOWN);
    CHECK(control(&page, WORLD_MENU_ACCEPT) == WORLD_MENU_CONTROLS_CHANGED);
    CHECK(settings.bindings.controller[1][WORLD_INPUT_FIRE2] == WORLD_PAD_B);
    CHECK(settings.bindings.controller[0][WORLD_INPUT_FIRE2] == WORLD_PAD_X);
    CHECK(settings.bindings.keyboard[1][WORLD_INPUT_FIRE2] == 'Q');
    CHECK(world_input_bindings_valid(&settings.bindings));
    control(&page, WORLD_MENU_DOWN);
    CHECK(control(&page, WORLD_MENU_ACCEPT) == WORLD_MENU_CONTROLS_CLOSE && !page.open);
    world_menu_controls_open(&page, 0, clock_ms);
    CHECK(control(&page, WORLD_MENU_BACK) == WORLD_MENU_CONTROLS_CLOSE);

    puts("menu pages, capability gating, locked modes, display rows, menu controls and remapping page pass");
    return 0;
}
