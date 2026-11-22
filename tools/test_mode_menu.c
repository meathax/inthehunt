/* Exercise both menu layers with the same held-button API used by the native host. */
#include "../src/world_menu.h"
#include "../src/world_gpu.h"
#include <stdio.h>
#include <stdlib.h>

const char *const world_gpu_preset_name[WORLD_GPU_PRESETS] = {0};

static void check(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", message);
        exit(1);
    }
}

int main(void)
{
    WorldSettings settings;
    WorldMenu menu = {0};
    world_settings_defaults(&settings);
    for (int wide = 0; wide <= 1; ++wide) {
        world_menu_open_title(&menu, 0, wide, 0);
        menu.selector = 1;
        world_menu_title_activity(&menu, 0, 0, wide, 6000);
        check(!menu.title_hidden, "selector waits indefinitely");
        /* Enter Options and Exit, then return to the originating row. */
        for (unsigned row = 2; row < 4; ++row) {
            while (menu.row < row) {
                world_menu_update(&menu, &settings, WORLD_MENU_DOWN, wide, 6000);
                world_menu_update(&menu, &settings, 0, wide, 6000);
            }
            world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6000);
            check(menu.page != 0, "selector submenu opened");
            world_menu_update(&menu, &settings, WORLD_MENU_BACK, wide, 6000);
            check(menu.page == 0 && menu.row == row, "submenu restores selector focus");
        }
        world_menu_update(&menu, &settings, WORLD_MENU_DOWN, wide, 6000);
        world_menu_update(&menu, &settings, 0, wide, 6000);
        if (wide) world_menu_update(&menu, &settings, WORLD_MENU_DOWN, wide, 6001);
        unsigned action = world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6002);
        check(action == (wide ? WORLD_MENU_SELECT_WIDE : WORLD_MENU_SELECT_ARCADE), "mode selection");

        world_menu_open_title(&menu, WORLD_MENU_ACCEPT, wide, 7000);
        check(!world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 7001), "held selection");
        world_menu_update(&menu, &settings, 0, wide, 7002);
        world_menu_title_activity(&menu, 0, 0, wide, 11999);
        check(!menu.title_hidden, "title visible before timeout");
        world_menu_title_activity(&menu, 0, 0, wide, 12000);
        check(menu.title_hidden, "five-second timeout");
        world_menu_title_activity(&menu, WORLD_MENU_ACCEPT, 1, wide, 12001);
        check(!world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 12001), "wake consumes accept");
        world_menu_update(&menu, &settings, 0, wide, 12002);
        check(world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 12003) == WORLD_MENU_START, "Play");
    }
    puts("Both mode selections and attract menu transitions passed.");
    return 0;
}
