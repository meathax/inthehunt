#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "world_settings.h"

typedef struct {
    const char *key;
    size_t offset;
    unsigned minimum, maximum;
} NumberSetting;

static const NumberSetting numbers[] = {
    {"border_brightness", offsetof(WorldSettings, border_brightness), 0, 100},
    {"volume", offsetof(WorldSettings, volume), 0, 100},
    {"fullscreen", offsetof(WorldSettings, fullscreen), 0, 1},
    {"vsync", offsetof(WorldSettings, vsync), 0, 1},
    {"window_scale", offsetof(WorldSettings, window_scale), 1, 12},
    {"achievement_popups", offsetof(WorldSettings, achievement_popups), 0, 1},
    {"scaling", offsetof(WorldSettings, scaling), 0, 2},
    {"fps_limit", offsetof(WorldSettings, fps_limit), 0, 5},
    {"pacing", offsetof(WorldSettings, pacing), 0, 2},
    {"show_fps", offsetof(WorldSettings, show_fps), 0, 1},
    {"pause_unfocused", offsetof(WorldSettings, pause_unfocused), 0, 1},
    {"fm_volume", offsetof(WorldSettings, fm_volume), 0, 100},
    {"pcm_volume", offsetof(WorldSettings, pcm_volume), 0, 100},
    {"dip_lives", offsetof(WorldSettings, dip_lives), 0, 3},
    {"dip_difficulty", offsetof(WorldSettings, dip_difficulty), 0, 3},
    {"dip_demo_sound", offsetof(WorldSettings, dip_demo_sound), 0, 1},
    {"crt_shader", offsetof(WorldSettings, crt_shader), 0, 12},
    {"autofire", offsetof(WorldSettings, autofire), 0, 1},
    {"enhance_preset_pending", offsetof(WorldSettings, enhance_preset_pending), 0, 1},
    {"fx_enable", offsetof(WorldSettings, fx_enable), 0, 1},
    {"fx_caustics", offsetof(WorldSettings, fx_caustics), 0, 1},
    {"fx_rays", offsetof(WorldSettings, fx_rays), 0, 1},
    {"fx_lights", offsetof(WorldSettings, fx_lights), 0, 1},
    {"fx_depth", offsetof(WorldSettings, fx_depth), 0, 1},
    {"fx_snow", offsetof(WorldSettings, fx_snow), 0, 1},
    {"fx_vivid", offsetof(WorldSettings, fx_vivid), 0, 1},
    {"fx_trails", offsetof(WorldSettings, fx_trails), 0, 1},
    {"fx_shadows", offsetof(WorldSettings, fx_shadows), 0, 1},
    {"fx_dof", offsetof(WorldSettings, fx_dof), 0, 1},
    {"fx_haze", offsetof(WorldSettings, fx_haze), 0, 1},
    {"fx_normals", offsetof(WorldSettings, fx_normals), 0, 1},
    {"fx_sunshadow", offsetof(WorldSettings, fx_sunshadow), 0, 1},
    {"fx_glow", offsetof(WorldSettings, fx_glow), 0, 1},
    {"fx_reflect", offsetof(WorldSettings, fx_reflect), 0, 1},
    {"fx_shock", offsetof(WorldSettings, fx_shock), 0, 1},
    {"fx_particles", offsetof(WorldSettings, fx_particles), 0, 1},
    {"fx_shake", offsetof(WorldSettings, fx_shake), 0, 1},
    {"fx_surface", offsetof(WorldSettings, fx_surface), 0, 1},
    {"fx_smooth", offsetof(WorldSettings, fx_smooth), 0, 1},
    {"fx_interp", offsetof(WorldSettings, fx_interp), 0, 1},
    {"fx_wakes", offsetof(WorldSettings, fx_wakes), 0, 1},
    {"fx_debris", offsetof(WorldSettings, fx_debris), 0, 1},
    {"fx_silt", offsetof(WorldSettings, fx_silt), 0, 1},
    {"fx_splashes", offsetof(WorldSettings, fx_splashes), 0, 1},
    {"fx_sparks", offsetof(WorldSettings, fx_sparks), 0, 1},
    {"fx_impact_motes", offsetof(WorldSettings, fx_impact_motes), 0, 1},
    {"fx_pressure", offsetof(WorldSettings, fx_pressure), 0, 1},
    {"fx_sediment_light", offsetof(WorldSettings, fx_sediment_light), 0, 1},
    {"fx_water_volume", offsetof(WorldSettings, fx_water_volume), 0, 1},
    {"fx_ocean_depth", offsetof(WorldSettings, fx_ocean_depth), 0, 1},
    {"fx_aftermath", offsetof(WorldSettings, fx_aftermath), 0, 1},
    {"fx_materials", offsetof(WorldSettings, fx_materials), 0, 1}
};
static const char *const actions[8] = {
    "right", "left", "down", "up", "fire1", "fire2", "start", "coin"
};
static volatile LONG file_sequence;

static int valid_utf8(const char *text)
{
    return !*text || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) > 0;
}

static int bounded_string(const char *text, size_t size, int identifier)
{
    const char *end = memchr(text, 0, size);
    if (!end || (identifier && end == text)) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < 32 || *p == 127) return 0;
        if (identifier && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return 0;
    }
    return valid_utf8(text);
}

static void all_enhancements_on(WorldSettings *settings)
{
    settings->fx_caustics = settings->fx_rays = settings->fx_lights = 1;
    settings->fx_depth = settings->fx_snow = settings->fx_vivid = 1;
    settings->fx_normals = settings->fx_sunshadow = settings->fx_glow = settings->fx_reflect = 1;
    settings->fx_shadows = settings->fx_haze = settings->fx_shock = settings->fx_particles = 1;
    settings->fx_shake = settings->fx_surface = settings->fx_smooth = settings->fx_interp = 1;
    settings->fx_wakes = settings->fx_debris = settings->fx_silt = settings->fx_splashes = 1;
    settings->fx_sparks = settings->fx_impact_motes = 1;
    settings->fx_pressure = 1;
    settings->fx_sediment_light = 1;
    settings->fx_water_volume = 1;
    settings->fx_ocean_depth = 1;
    settings->fx_aftermath = 1;
    settings->fx_materials = 1;
    settings->fx_dof = settings->fx_trails = 1;
}

void world_settings_activate_enhancements(WorldSettings *settings)
{
    if (settings->fx_enable && settings->enhance_preset_pending) {
        all_enhancements_on(settings);
        settings->enhance_preset_pending = 0;
    }
}

void world_settings_defaults(WorldSettings *settings)
{
    if (!settings) return;
    memset(settings, 0, sizeof *settings);
    settings->timing_mode = WORLD_TIMING_ARCADE;
    settings->border_brightness = 100;
    settings->volume = 100;
    settings->window_scale = 3;
    settings->achievement_popups = 1;
    settings->pause_unfocused = 1;
    settings->fm_volume = 100;
    settings->pcm_volume = 100;
    settings->dip_lives = 1;         /* factory: 3 submarines */
    settings->dip_difficulty = 2;    /* factory: normal */
    settings->dip_demo_sound = 1;
    all_enhancements_on(settings);
    strcpy(settings->shader, "pixels");
    strcpy(settings->border, "black");
    world_input_bindings_defaults(&settings->bindings);
}

int world_settings_valid(const WorldSettings *settings)
{
    if (!settings || (settings->timing_mode != WORLD_TIMING_ARCADE &&
                      settings->timing_mode != WORLD_TIMING_REMOVE_SLOWDOWN)) return 0;
    for (unsigned i = 0; i < sizeof numbers / sizeof numbers[0]; ++i) {
        unsigned value;
        memcpy(&value, (const char *)settings + numbers[i].offset, sizeof value);
        if (value < numbers[i].minimum || value > numbers[i].maximum) return 0;
    }
    return bounded_string(settings->shader, sizeof settings->shader, 1) &&
           bounded_string(settings->border, sizeof settings->border, 1) &&
           bounded_string(settings->border_image, sizeof settings->border_image, 0) &&
           world_input_bindings_valid(&settings->bindings);
}

static wchar_t *wide_path(const char *path)
{
    if (!path || !*path) return NULL;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!length) return NULL;
    wchar_t *wide = malloc((size_t)length * sizeof *wide);
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *trim(char *text)
{
    text += strspn(text, " \t\r");
    size_t length = strlen(text);
    while (length && strchr(" \t\r", text[length - 1])) text[--length] = 0;
    return text;
}

static int number(const char *text, unsigned *value)
{
    if (!*text || *text == '-') return 0;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > UINT_MAX) return 0;
    *value = (unsigned)parsed;
    return 1;
}

/* Returns -1 for an invalid recognized value, zero for an unknown key. */
static int apply_setting(WorldSettings *settings, const char *key, const char *value)
{
    if (!strcmp(key, "timing_mode"))
        return world_timing_mode_parse(value, &settings->timing_mode) ? 1 : -1;
    for (unsigned i = 0; i < sizeof numbers / sizeof numbers[0]; ++i) {
        if (strcmp(key, numbers[i].key)) continue;
        unsigned parsed;
        if (!number(value, &parsed) || parsed < numbers[i].minimum || parsed > numbers[i].maximum) return -1;
        memcpy((char *)settings + numbers[i].offset, &parsed, sizeof parsed);
        return 1;
    }
    char *target = NULL;
    size_t capacity = 0;
    if (!strcmp(key, "shader")) { target = settings->shader; capacity = sizeof settings->shader; }
    else if (!strcmp(key, "border")) { target = settings->border; capacity = sizeof settings->border; }
    else if (!strcmp(key, "border_image")) { target = settings->border_image; capacity = sizeof settings->border_image; }
    if (target) {
        size_t length = strlen(value);
        if (length >= capacity) return -1;
        memcpy(target, value, length + 1);
        return 1;
    }
    for (unsigned player = 0; player < 2; ++player)
        for (unsigned device = 0; device < 2; ++device)
            for (unsigned action = 0; action < 8; ++action) {
                char binding_key[64];
                snprintf(binding_key, sizeof binding_key, "p%u.%s.%s", player + 1,
                         device ? "controller" : "keyboard", actions[action]);
                if (strcmp(key, binding_key)) continue;
                unsigned parsed;
                if (!number(value, &parsed) || parsed > UINT16_MAX) return -1;
                uint16_t (*bindings)[8] = device ? settings->bindings.controller : settings->bindings.keyboard;
                bindings[player][action] = (uint16_t)parsed;
                return 1;
            }
    return 0;
}

WorldSettingsStatus world_settings_load(const char *path, WorldSettings *settings)
{
    if (!settings) return WORLD_SETTINGS_INVALID;
    world_settings_defaults(settings);
    wchar_t *wide = wide_path(path);
    if (!wide) return WORLD_SETTINGS_INVALID;
    FILE *file = _wfopen(wide, L"rb");
    free(wide);
    if (!file) return errno == ENOENT ? WORLD_SETTINGS_MISSING : WORLD_SETTINGS_IO_ERROR;
    char data[65537];
    size_t length = fread(data, 1, sizeof data - 1, file);
    int extra = fgetc(file), failed = ferror(file);
    fclose(file);
    if (failed) return WORLD_SETTINGS_IO_ERROR;
    if (extra != EOF || memchr(data, 0, length)) return WORLD_SETTINGS_INVALID;
    data[length] = 0;
    if (!valid_utf8(data)) return WORLD_SETTINGS_INVALID;
    char *line = data;
    if (length >= 3 && !memcmp(data, "\xef\xbb\xbf", 3)) line += 3;
    WorldSettings candidate = *settings;
    unsigned version = 0, saw_version = 0;
    int invalid = 0, legacy_enhancements = 0, preset_state_saved = 0;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *key = trim(line);
        if (*key && *key != '#' && *key != ';') {
            char *value = strchr(key, '=');
            if (!value) invalid = 1;
            else {
                *value++ = 0;
                key = trim(key);
                value = trim(value);
                /* Older full saves contain the retired switches and a mixed preset. */
                if (!strcmp(key, "fx_vignette") || !strcmp(key, "fx_stereo")) legacy_enhancements = 1;
                if (!strcmp(key, "enhance_preset_pending")) preset_state_saved = 1;
                if (!strcmp(key, "version")) {
                    if (saw_version++ || !number(value, &version)) invalid = 1;
                } else if (apply_setting(&candidate, key, value) < 0) invalid = 1;
            }
        }
        if (!next) break;
        line = next;
    }
    if (version > WORLD_SETTINGS_VERSION) return WORLD_SETTINGS_NEWER_VERSION;
    if (invalid || !world_settings_valid(&candidate)) return WORLD_SETTINGS_INVALID;
    if (legacy_enhancements && !preset_state_saved) candidate.enhance_preset_pending = 1;
    world_settings_activate_enhancements(&candidate);
    *settings = candidate;
    return WORLD_SETTINGS_OK;
}

static int write_settings(FILE *file, const WorldSettings *settings)
{
    fprintf(file, "# In the Hunt native settings\nversion=%u\ntiming_mode=%s\n",
            WORLD_SETTINGS_VERSION, world_timing_mode_name(settings->timing_mode));
    for (unsigned i = 0; i < sizeof numbers / sizeof numbers[0]; ++i) {
        unsigned value;
        memcpy(&value, (const char *)settings + numbers[i].offset, sizeof value);
        fprintf(file, "%s=%u\n", numbers[i].key, value);
    }
    fprintf(file, "shader=%s\nborder=%s\nborder_image=%s\n", settings->shader, settings->border, settings->border_image);
    for (unsigned player = 0; player < 2; ++player)
        for (unsigned device = 0; device < 2; ++device)
            for (unsigned action = 0; action < 8; ++action) {
                const uint16_t (*bindings)[8] = device ? settings->bindings.controller : settings->bindings.keyboard;
                fprintf(file, "p%u.%s.%s=%u\n", player + 1, device ? "controller" : "keyboard",
                        actions[action], bindings[player][action]);
            }
    return !ferror(file) && fflush(file) == 0 && _commit(_fileno(file)) == 0;
}

WorldSettingsStatus world_settings_save(const char *path, const WorldSettings *settings)
{
    if (!world_settings_valid(settings)) return WORLD_SETTINGS_INVALID;
    /* All save paths, including menu changes/reset, preserve newer formats. */
    WorldSettings existing;
    if (world_settings_load(path, &existing) == WORLD_SETTINGS_NEWER_VERSION)
        return WORLD_SETTINGS_NEWER_VERSION;
    wchar_t *destination = wide_path(path);
    if (!destination) return WORLD_SETTINGS_INVALID;
    size_t capacity = wcslen(destination) + 80;
    wchar_t *temporary = malloc(capacity * sizeof *temporary);
    if (!temporary) { free(destination); return WORLD_SETTINGS_IO_ERROR; }
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        unsigned long sequence = (unsigned long)InterlockedIncrement(&file_sequence);
        int length = swprintf(temporary, capacity, L"%ls.tmp.%lu.%lu", destination,
                              (unsigned long)GetCurrentProcessId(), sequence);
        if (length < 0 || (size_t)length >= capacity) break;
        handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    int ok = 0;
    if (handle != INVALID_HANDLE_VALUE) {
        int descriptor = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_WRONLY);
        if (descriptor < 0) CloseHandle(handle);
        else {
            FILE *file = _fdopen(descriptor, "wb");
            if (!file) _close(descriptor);
            else {
                ok = write_settings(file, settings);
                if (fclose(file)) ok = 0;
            }
        }
        if (ok) ok = MoveFileExW(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) DeleteFileW(temporary);
    }
    free(temporary);
    free(destination);
    return ok ? WORLD_SETTINGS_OK : WORLD_SETTINGS_IO_ERROR;
}
