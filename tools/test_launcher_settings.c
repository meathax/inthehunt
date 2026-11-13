/* Exercise the shipping settings parser, not a second INI implementation. */
#include "world_settings.h"
#include <string.h>
int main(int argc, char **argv)
{
    WorldSettings settings;
    if (argc != 3) return 2;
    if (!strcmp(argv[1], "defaults")) world_settings_defaults(&settings);
    else if (!strcmp(argv[1], "roundtrip")) {
        if (world_settings_load(argv[2], &settings) != WORLD_SETTINGS_OK) return 3;
    } else return 2;
    return world_settings_save(argv[2], &settings) == WORLD_SETTINGS_OK ? 0 : 4;
}
