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
        /* Aspect ratio is the first VIDEO option and cycles 4:3, 16:9, 21:9. */
        menu.row = 1;
        world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 6100);
        world_menu_update(&menu, &settings, 0, wide, 6101);
        check(menu.page == 1 && menu.tab == 0, "options open on VIDEO");
        menu.row = 1;
        settings.aspect = 0;
        unsigned changed = world_menu_update(&menu, &settings, WORLD_MENU_RIGHT, wide, 6104);
        check((changed & WORLD_MENU_ASPECT) && settings.aspect == 1, "aspect 16:9");
        world_menu_update(&menu, &settings, 0, wide, 6105);
        world_menu_update(&menu, &settings, WORLD_MENU_RIGHT, wide, 6106);
        world_menu_update(&menu, &settings, 0, wide, 6107);
        world_menu_update(&menu, &settings, WORLD_MENU_RIGHT, wide, 6108);
        check(settings.aspect == 0, "aspect wraps to 4:3");
        world_menu_update(&menu, &settings, 0, wide, 6109);
        world_menu_update(&menu, &settings, WORLD_MENU_LEFT, wide, 6110);
        check(settings.aspect == 2, "aspect 21:9");
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
        check(world_menu_update(&menu, &settings, WORLD_MENU_ACCEPT, wide, 12003) == WORLD_MENU_START, "Play");
    }
    puts("Title menu, aspect option and attract transitions passed.");
    return 0;
}
