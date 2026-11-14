/* Compile with -UNDEBUG: assertions perform the regression checks. */
#ifdef NDEBUG
#error Runtime audit requires assertions; compile with -UNDEBUG.
#endif
#define wmain static unused_game_entry
#include "../src/world_native.c"
#undef wmain
#include <assert.h>

uint16_t world_dispatch_camera_actor;

int main(int argc, char **argv)
{
    assert(argc == 2);
    snprintf(settings_path, sizeof settings_path, "%s/settings.ini", argv[1]);
    remove(settings_path); /* Dedicated disposable fixture, never a player's settings. */
    world_settings_defaults(&settings);
    desktop_fullscreen = 0;
    launch_fullscreen_override = 1;
    settings.fullscreen = 1;
    settings.volume = 37;
    assert(save_runtime_settings() == WORLD_SETTINGS_OK);
    WorldSettings loaded;
    assert(world_settings_load(settings_path, &loaded) == WORLD_SETTINGS_OK);
    assert(loaded.fullscreen == 0 && loaded.volume == 37 && settings.fullscreen == 1);
    launch_fullscreen_override = 0;
    assert(save_runtime_settings() == WORLD_SETTINGS_OK);
    assert(world_settings_load(settings_path, &loaded) == WORLD_SETTINGS_OK && loaded.fullscreen == 1);
    FILE *f = world_fopen(settings_path, "wb");
    assert(f);
    const char future[] = "version=999\nfuture-option=preserve-me\n";
    assert(fwrite(future, 1, sizeof future - 1, f) == sizeof future - 1);
    assert(fclose(f) == 0);
    assert(save_runtime_settings() == WORLD_SETTINGS_NEWER_VERSION);
    char contents[sizeof future] = {0};
    f = world_fopen(settings_path, "rb");
    assert(f && fread(contents, 1, sizeof contents, f) == sizeof future - 1);
    fclose(f);
    assert(!strcmp(contents, future));

    /* Measure the layer preparation currently paid even with effects off. */
    world_scene_init(gfx1, gfx2);
    LARGE_INTEGER a, b, hz;
    QueryPerformanceFrequency(&hz);
    scene_submit();
    QueryPerformanceCounter(&a);
    for (unsigned i = 0; i < 1000; ++i) scene_submit();
    QueryPerformanceCounter(&b);
    printf("scene_submit_ms=%.6f\n", (double)(b.QuadPart-a.QuadPart)*1000/hz.QuadPart/1000);
    settings.fx_enable = 0;
    world_scene_reset();
    QueryPerformanceCounter(&a);
    for (unsigned i = 0; i < 1000; ++i) scene_submit_live();
    QueryPerformanceCounter(&b);
    printf("disabled_scene_submit_ms=%.6f\n", (double)(b.QuadPart-a.QuadPart)*1000/hz.QuadPart/1000);
    WorldSceneFx fx = {0};
    assert(world_scene_render(shown, 1, 1.0, &fx, 133) == 0);
    settings.fx_enable = 1;
    scene_submit_live();
    assert(world_scene_render(shown, 1, 1.0, &fx, 266) == 1);
    uint32_t reference[320 * 240];
    memcpy(reference, shown, sizeof reference);
    settings.fx_enable = 0;
    scene_submit_live();
    settings.fx_enable = 1;
    scene_submit_live();
    assert(world_scene_render(shown, 1, 0.5, &fx, 399) == 1);
    assert(!memcmp(reference, shown, sizeof reference));
    puts("PASS: disabled preparation, immediate enable and re-enable image");
    puts("PASS: Big Picture preference and future settings preservation");
    return 0;
}
