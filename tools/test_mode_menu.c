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
        /* Options and Exit return to their own rows on the title menu. */
        for (unsigned row = 1; row < 3; ++row) {
            while (menu.row < row) {
                world_menu_update(&menu, &settings, WORLD_MENU_DOWN, wide, 6000);
                world_menu_update(&menu, &settings, 0, wide, 6000);
            }
            world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6000);
            check(menu.page != 0, "title submenu opened");
            world_menu_update(&menu, &settings, WORLD_MENU_BACK, wide, 6000);
            world_menu_update(&menu, &settings, 0, wide, 6000);
            check(menu.page == 0 && menu.row == row, "submenu restores title focus");
        }
        /* The VIDEO tab no longer carries an aspect option. */
        menu.row = 1;
        world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6100);
        world_menu_update(&menu, &settings, 0, wide, 6101);
        check(menu.page == 1 && menu.tab == 0, "options open on VIDEO");
        menu.row = 1;
        settings.aspect = 0;
        world_menu_update(&menu, &settings, WORLD_MENU_RIGHT, wide, 6104);
        check(settings.aspect == 0, "VIDEO tab leaves aspect alone");
        world_menu_update(&menu, &settings, 0, wide, 6105);

        /* Play opens the screen-mode chooser; each choice starts in its mode. */
        for (unsigned pick = 0; pick < 2; ++pick) {
            for (unsigned before = 0; before < 2; ++before) {
                settings.aspect = before;
                world_menu_open_title(&menu, 0, wide, 6200);
                world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6201);
                world_menu_update(&menu, &settings, 0, wide, 6202);
                check(menu.open && menu.page == 4, "Play opens the mode chooser");
                check(menu.row == before, "chooser focuses the last mode");
                if (menu.row != pick) {
                    world_menu_update(&menu, &settings, pick ? WORLD_MENU_DOWN : WORLD_MENU_UP, wide, 6203);
                    world_menu_update(&menu, &settings, 0, wide, 6204);
                }
                unsigned r = world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6205);
                world_menu_update(&menu, &settings, 0, wide, 6206);
                check((r & WORLD_MENU_START) && !menu.open, "mode choice starts the game");
                check(settings.aspect == pick, "mode choice sets the aspect");
                check(!!(r & WORLD_MENU_ASPECT) == (pick != before), "mode change flagged");
                check(pick == before || (r & WORLD_MENU_SAVE), "changed mode is saved");
            }
        }
        /* Back from the chooser returns to Play. */
        world_menu_open_title(&menu, 0, wide, 6300);
        world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6301);
        world_menu_update(&menu, &settings, 0, wide, 6302);
        world_menu_update(&menu, &settings, WORLD_MENU_BACK, wide, 6303);
        world_menu_update(&menu, &settings, 0, wide, 6304);
        check(menu.open && menu.page == 0 && menu.row == 0, "chooser back returns to Play");
        settings.aspect = 0;

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
        check(world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 12003) == 0 && menu.page == 4, "Play");
    }
    puts("Title menu, screen-mode chooser and attract transitions passed.");
    return 0;
}
