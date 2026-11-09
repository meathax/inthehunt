/* Native Windows entry point. The imported program image is read-only lookup data. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "world_boot.h"
#include "world_dispatch.h"
#include "world_frame.h"
#include "world_palette.h"
#include "world_video.h"
#include "world_window.h"
#include "world_audio_driver.h"
#include "world_audio_output.h"
#include "world_ga20.h"
#include "world_ym.h"
#include "world_clock.h"
#include "world_settings.h"
#include "world_crt.h"
#include "world_menu.h"
#include "world_scores_store.h"
#include "world_achievements.h"
#include "world_achievement_view.h"
#include "world_modes.h"
#include "world_service.h"

static uint8_t rom[1<<20], gfx1[2<<20], gfx2[4<<20], ram[65536], tiles[65536];
static uint8_t oam[2048], palette[2048];
static uint32_t pixels[320*240];
/* Cold boot programs 98/9A/9C, then the title disables the middle plane. */
static uint16_t layer_master[3] = {0, 0x41, 2};
static char failure[256];
static unsigned long ticks;
static uint8_t sound_rom[WORLD_AUDIO_ROM_SIZE], samples[524288], sound_state[WORLD_AUDIO_STATE_SIZE];
static WorldGa20 pcm;
static uint64_t sound_frame_phase, sound_timer_phase;
static int sound_device;
static const char *data_directory;
static unsigned sound_gaps;
static uint8_t reported_sound_gaps[8192];
static WorldClock board_clock;
static WorldSettings settings;
static unsigned elapsed_board_frames = 1;
static char settings_path[4096];
static int settings_writable;
/* Host screens shown over the paused game. */
typedef enum { UI_PLAY, UI_MENU, UI_CONTROLS, UI_RECORD } UiMode;
static UiMode ui;
static WorldMenu menu;
static WorldMenuControls controls_page;
static int capturing, quit_requested;
static uint32_t ui_pixels[320*240];

static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }

static int load(const char *dir, const char *name, void *data, size_t size)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(data, 1, size, f) == size && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    return ok;
}

static uint8_t sound_busy(void *context)
{
    WorldGa20 *chip = context;
    uint8_t active = 0;
    for (unsigned i = 0; i < 4; ++i) active |= world_ga20_read(chip, i*8+7) << i;
    return active;
}

static void sound_write(void *context, uint8_t reg, uint8_t value)
{
    world_ga20_write(context, reg, value);
}

static void sound_reply(void *context, uint8_t value)
{
    (void)context;
    world_frame_sound_reply(ram, value);
}

static void sound_unrecovered(void *context, uint16_t task, uint8_t state, uint16_t cursor)
{
    (void)context;
    ++sound_gaps;
    if (reported_sound_gaps[cursor >> 3] & (1u << (cursor & 7))) return;
    reported_sound_gaps[cursor >> 3] |= 1u << (cursor & 7);
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/native-audio-stops.tsv", data_directory);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%lu\t%04x\t%02x\t%04x\n", ticks, task, state, cursor);
    fclose(f);
}

static void sound_fm_write(void *context, uint8_t reg, uint8_t value)
{
    (void)context;
    world_ym_write(reg, value);
}

static WorldAudioBus sound_bus = { &pcm, sound_busy, sound_write, sound_reply, sound_unrecovered, sound_fm_write };

static void sound_frame(void)
{
    int16_t output[1024];
    int32_t fm[1024];
    /* One board frame has 424*262 pixel clocks at 6,666,666 Hz. */
    sound_frame_phase += (uint64_t)48000 * 424 * 262;
    unsigned frames = (unsigned)(sound_frame_phase / 6666666);
    sound_frame_phase %= 6666666;
    unsigned done = 0;
    const uint64_t timer_period = (uint64_t)48000 * 57344;
    while (done < frames) {
        unsigned count = (unsigned)((timer_period - sound_timer_phase + 14318180) / 14318181);
        if (count > frames - done) count = frames - done;
        world_ga20_render(&pcm, output + done, count);
        world_ym_render(fm, count);
        for (unsigned i = 0; i < count; ++i) {
            /* Both GA20 outputs route at unity to the mono speaker. */
            int32_t mixed = 2 * (int32_t)output[done + i] + fm[i];
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            /* The saved volume scales the finished mono mix; 100 is unity. */
            output[done + i] = (int16_t)(mixed * (int32_t)settings.volume / 100);
        }
        done += count;
        sound_timer_phase += (uint64_t)count * 14318181;
        if (sound_timer_phase >= timer_period) {
            sound_timer_phase -= timer_period;
            world_audio_tick(sound_state, sound_rom, &sound_bus);
        }
    }
    if (sound_device && !world_audio_output_submit(output, frames)) {
        fprintf(stderr, "Windows audio output stopped.\n");
        world_audio_output_close();
        sound_device = 0;
    }
}

/* Black unless the saved border names artwork that still loads. Returns
   zero, and records the black border that is shown, when artwork was wanted
   but could not be used. */
static int apply_border(void)
{
    int artwork = strcmp(settings.border, "black") != 0;
    const char *path = artwork ? settings.border_image : "";
    if ((!artwork || *path) && world_window_set_border(path, settings.border_brightness)) return 1;
    fprintf(stderr, "Could not load border artwork \"%s\". Using black borders.\n", path);
    world_window_set_border("", settings.border_brightness);
    strcpy(settings.border, "black");
    return 0;
}

/* Service record: lifetime profile, the list and popup view, and the
   events derived from board RAM. docs/HOST.md lists what is reported. */
static WorldAchievements record;
static WorldAchievementView *record_view;
static WorldService service;
static char record_path[4096];
static int record_dirty, record_warned;
static unsigned long record_retry_at;
static unsigned pending_unlocks[WORLD_ACHIEVEMENT_COUNT], pending_unlock_count;
static WorldMenuRepeat record_repeat;
static unsigned record_page_keys;
static uint64_t popup_phase;
/* No launch function: Boss Rush and Score Attack stay unavailable until the
   game core can start an encounter or a fixed-rule campaign itself. */
static WorldModeCore mode_core;
static WorldModeRun mode_run;

static void record_unlocked(void *context, unsigned index)
{
    (void)context;
    record_dirty = 1;
    for (unsigned i = 0; i < pending_unlock_count; ++i)
        if (pending_unlocks[i] == index) return;
    if (pending_unlock_count < WORLD_ACHIEVEMENT_COUNT) pending_unlocks[pending_unlock_count++] = index;
}

static WorldAchievementViewStatus record_status(void *context, size_t index)
{
    (void)context;
    WorldAchievementViewStatus status = {0, 0, 0};
    if (index >= WORLD_ACHIEVEMENT_COUNT) return status;
    status.unlocked = record.profile.earned[index];
    world_achievements_progress(&record, (unsigned)index, &status.current, &status.target);
    return status;
}

static void save_record(void)
{
    int saved = world_achievements_save(&record, record_path);
    if (saved == 1) { record_dirty = 0; return; }
    /* Busy means another copy holds the lock. Either way, try again later. */
    record_retry_at = ticks + 60;
    if (saved < 0 && !record_warned) {
        record_warned = 1;
        fprintf(stderr, "Could not save the service record.\n");
    }
}

static void open_record(const char *dir)
{
    world_achievements_init(&record, record_unlocked, NULL);
    world_service_init(&service, &record, (uint64_t)time(NULL));
    int length = snprintf(record_path, sizeof record_path, "%s/native-service-record.txt", dir);
    if (length < 0 || (size_t)length >= sizeof record_path) record_path[0] = 0;
    else if (world_achievements_load(&record, record_path) < 0)
        fprintf(stderr, "The saved service record could not be read. New progress will replace it.\n");
    pending_unlock_count = 0;
    record_dirty = 0;
    record_view = world_achievement_view_create(world_achievement_catalog, WORLD_ACHIEVEMENT_COUNT,
                                                record_status, NULL);
    WorldModeRules arcade = { WORLD_MODE_ARCADE, settings.timing_mode, 1 };
    world_modes_reset(&mode_run);
    world_modes_begin(&mode_run, arcade, &mode_core);
}

/* After each completed board frame: report what the documented RAM shows. */
static void observe_record(void)
{
    int live = !(ram[0x28c] & 1); /* Bit 0 is set while the attract demo replays ROM inputs. */
    WorldServiceObservation frame;
    memset(&frame, 0, sizeof frame);
    frame.master = word(ram + 0x80);
    frame.stage = word(ram + 0x84);
    frame.p1_lives = word(ram + 0x118);
    frame.players = ram[0x264] & 3;
    frame.demo = !live;
    world_modes_tick(&mode_run, elapsed_board_frames, 0, live);
    frame.score_valid = world_modes_observe_world(&mode_run, ram, live);
    frame.score = mode_run.score;
    frame.utc_seconds = (uint64_t)time(NULL);
    unsigned active = service.active;
    world_service_observe(&service, &frame);
    if (active != service.active) record_dirty = 1;
    /* Master 0B2E is a stage in play. Hold unlock plates until the board is
       between stages so they never cover the action. */
    if (pending_unlock_count && frame.master != 0xb2e) {
        for (unsigned i = 0; i < pending_unlock_count; ++i)
            if (settings.achievement_popups && record_view)
                world_achievement_view_notify(record_view, pending_unlocks[i]);
        pending_unlock_count = 0;
    }
    if (record_dirty && record_path[0] && ticks >= record_retry_at) save_record();
}

/* Blend the current unlock plate over the bottom of the finished frame, so
   it stays inside the 4:3 picture at every window size. */
static void composite_popup(void)
{
    if (!record_view) return;
    popup_phase += (uint64_t)elapsed_board_frames * 1000 * 424 * 262;
    world_achievement_view_advance(record_view, (uint32_t)(popup_phase / 6666666));
    popup_phase %= 6666666;
    WorldAchievementViewSurface plate = world_achievement_view_popup(record_view);
    if (!plate.pixels || !plate.opacity) return;
    int top = 240 - (int)plate.height - 4 + plate.offset_y;
    for (unsigned row = 0; row < plate.height; ++row) {
        int y = top + (int)row;
        if (y < 0 || y >= 240) continue;
        for (unsigned x = 0; x < plate.width && x < 320; ++x) {
            uint32_t over = plate.pixels[row*plate.width + x], under = pixels[y*320 + (int)x];
            unsigned a = plate.opacity > 255 ? 255 : plate.opacity;
            unsigned r = (((over >> 16) & 255) * a + ((under >> 16) & 255) * (255 - a)) / 255;
            unsigned g = (((over >> 8) & 255) * a + ((under >> 8) & 255) * (255 - a)) / 255;
            unsigned b = ((over & 255) * a + (under & 255) * (255 - a)) / 255;
            pixels[y*320 + (int)x] = 0xff000000u | r << 16 | g << 8 | b;
        }
    }
}

static WorldScoresStore scores;
static char scores_path[4096];
static int scores_ready, scores_pending;

static const char *scores_status(int status)
{
    switch (status) {
    case WORLD_SCORES_OK: return "saved";
    case WORLD_SCORES_NOT_FOUND: return "no file";
    case WORLD_SCORES_BUSY: return "file is in use by another copy of the game";
    case WORLD_SCORES_INVALID: return "file is damaged";
    case WORLD_SCORES_UNSUPPORTED: return "file was written by a newer build";
    case WORLD_SCORES_IO_ERROR: return "file could not be read or written";
    default: return "ranking table is not in a state that can be stored";
    }
}

/* Read the saved ranking into the freshly booted table. */
static void load_scores(const char *dir)
{
    int length = snprintf(scores_path, sizeof scores_path, "%s/native-highscores.dat", dir);
    if (length < 0 || (size_t)length >= sizeof scores_path) return;
    int status = world_scores_store_init(&scores, ram);
    if (status == WORLD_SCORES_OK) {
        scores_ready = 1;
        status = world_scores_store_load(&scores, ram, scores_path);
    }
    if (status != WORLD_SCORES_OK && status != WORLD_SCORES_NOT_FOUND)
        fprintf(stderr, "High scores were not loaded: %s.\n", scores_status(status));
}

/* Called once a ranking is complete. A busy or failed write is kept
   pending and tried again at the next ranking and at shutdown. */
static void save_scores(void)
{
    if (!scores_ready) return;
    int status = world_scores_store_save(&scores, ram, scores_path);
    scores_pending = status == WORLD_SCORES_BUSY || status == WORLD_SCORES_IO_ERROR;
    if (status != WORLD_SCORES_OK) fprintf(stderr, "High scores were not saved: %s.\n", scores_status(status));
}

static void save_settings(void)
{
    /* A file written by a newer build is left exactly as it was found. */
    if (!settings_writable) return;
    if (world_settings_save(settings_path, &settings) != WORLD_SETTINGS_OK)
        fprintf(stderr, "Could not save native settings.\n");
}

/* Only rows the running host can act on are offered. */
static unsigned menu_capabilities(void)
{
    unsigned capabilities = WORLD_MENU_TIMING | WORLD_MENU_BORDERS | WORLD_MENU_CONTROLS | WORLD_MENU_DISPLAY;
    if (sound_device) capabilities |= WORLD_MENU_SOUND;
    if (world_window_direct3d()) capabilities |= WORLD_MENU_SHADERS | WORLD_MENU_VSYNC;
    if (record_view) capabilities |= WORLD_MENU_ACHIEVEMENTS;
    /* The ranked modes are listed, and locked until world_modes says otherwise. */
    capabilities |= WORLD_MENU_MODES;
    if (world_modes_available(WORLD_MODE_BOSS_RUSH, &mode_core)) capabilities |= WORLD_MENU_BOSS_RUSH;
    if (world_modes_available(WORLD_MODE_SCORE_ATTACK, &mode_core)) capabilities |= WORLD_MENU_SCORE_ATTACK;
    return capabilities;
}

static void open_menu(void)
{
    world_audio_output_pause(1);
    world_menu_open(&menu, 1, menu_capabilities(), world_menu_buttons(world_window_input()) | WORLD_MENU_BACK,
                    GetTickCount());
    world_menu_set_mode_reasons(&menu, world_modes_unavailable_reason(WORLD_MODE_BOSS_RUSH, &mode_core),
                                world_modes_unavailable_reason(WORLD_MODE_SCORE_ATTACK, &mode_core));
    ui = UI_MENU;
}

static void resume_game(void)
{
    world_menu_close(&menu);
    ui = UI_PLAY;
    world_window_take_menu_request();
    /* The button that closed the menu must not reach the cabinet. */
    world_window_input()->muted_until_release = 1;
    world_audio_output_pause(0);
}

/* Bring the window, clock and mix in line with a setting the menu changed.
   Anything the host could not apply is written back so the row stays true. */
static void apply_changed_settings(const WorldSettings *before)
{
    if (settings.timing_mode != before->timing_mode)
        world_clock_request_mode(&board_clock, settings.timing_mode);
    if (settings.fullscreen != before->fullscreen) world_window_set_fullscreen((int)settings.fullscreen);
    if (settings.window_scale != before->window_scale)
        settings.window_scale = world_window_set_scale(settings.window_scale);
    if (settings.vsync != before->vsync) world_window_set_vsync((int)settings.vsync);
    if (strcmp(settings.shader, before->shader) && !world_window_set_shader(settings.shader)) {
        fprintf(stderr, "Display profile %s is unavailable.\n", settings.shader);
        strcpy(settings.shader, world_window_shader());
    }
    if (strcmp(settings.border, before->border) || strcmp(settings.border_image, before->border_image) ||
        settings.border_brightness != before->border_brightness)
        apply_border();
    save_settings();
}

static void pick_border(void)
{
    WorldSettings before = settings;
    char path[WORLD_SETTINGS_PATH_SIZE];
    if (!world_window_pick_image(path, sizeof path)) return;
    strcpy(settings.border_image, path);
    strcpy(settings.border, "custom");
    if (!world_settings_valid(&settings) ||
        !world_window_set_border(settings.border_image, settings.border_brightness)) {
        fprintf(stderr, "Could not load border artwork %s.\n", path);
        settings = before;
        apply_border();
        return;
    }
    save_settings();
}

/* One step of the paused screens. Draws into ui_pixels. */
static void ui_frame(void)
{
    WorldInput *input = world_window_input();
    unsigned buttons = world_menu_buttons(input);
    uint32_t now_ms = GetTickCount();
    world_window_take_menu_request();
    if (ui == UI_CONTROLS) {
        if (capturing) {
            WorldInputCaptureResult result = world_input_capture_poll(input, NULL);
            if (result == WORLD_INPUT_CAPTURE_BOUND) {
                settings.bindings = input->bindings;
                save_settings();
            }
            if (result != WORLD_INPUT_CAPTURE_WAITING) capturing = 0;
        }
        WorldMenuControlsCommand command =
            world_menu_controls_update(&controls_page, &settings.bindings, capturing ? 0 : buttons, now_ms);
        if (command == WORLD_MENU_CONTROLS_CAPTURE)
            capturing = world_input_capture_begin(input, controls_page.player,
                                                  (WorldInputAction)controls_page.selected, controls_page.source);
        else if (command == WORLD_MENU_CONTROLS_CHANGED) {
            world_input_apply_bindings(input, &settings.bindings);
            save_settings();
        } else if (command == WORLD_MENU_CONTROLS_CLOSE) {
            ui = UI_MENU;
            menu.previous_buttons = buttons | WORLD_MENU_BACK | WORLD_MENU_ACCEPT;
        }
        if (ui == UI_CONTROLS) {
            world_menu_controls_render(&controls_page, &settings.bindings, capturing, ui_pixels, pixels);
            return;
        }
    }
    if (ui == UI_RECORD) {
        unsigned pressed = world_menu_pressed(&record_repeat, buttons, now_ms, WORLD_MENU_UP | WORLD_MENU_DOWN);
        unsigned page_keys = (input->keys[0x21] ? 1u : 0) | (input->keys[0x22] ? 2u : 0);
        unsigned paged = page_keys & ~record_page_keys;
        record_page_keys = page_keys;
        /* The view takes one edge action at a time. */
        unsigned action = pressed & WORLD_MENU_BACK ? WORLD_ACHIEVEMENT_VIEW_BACK
                        : pressed & WORLD_MENU_ACCEPT ? WORLD_ACHIEVEMENT_VIEW_ACCEPT
                        : pressed & WORLD_MENU_UP ? WORLD_ACHIEVEMENT_VIEW_UP
                        : pressed & WORLD_MENU_DOWN ? WORLD_ACHIEVEMENT_VIEW_DOWN
                        : pressed & WORLD_MENU_LEFT ? WORLD_ACHIEVEMENT_VIEW_LEFT
                        : pressed & WORLD_MENU_RIGHT ? WORLD_ACHIEVEMENT_VIEW_RIGHT
                        : paged & 1 ? WORLD_ACHIEVEMENT_VIEW_PAGE_UP
                        : paged & 2 ? WORLD_ACHIEVEMENT_VIEW_PAGE_DOWN : 0;
        if (action && !world_achievement_view_navigate(record_view, action) &&
            action == WORLD_ACHIEVEMENT_VIEW_BACK) {
            /* Back on the list page returns to the pause menu. */
            ui = UI_MENU;
            menu.previous_buttons = buttons | WORLD_MENU_BACK | WORLD_MENU_ACCEPT;
        }
        if (ui == UI_RECORD) {
            WorldAchievementViewSurface page = world_achievement_view_menu(record_view);
            if (page.pixels) memcpy(ui_pixels, page.pixels, sizeof ui_pixels);
            return;
        }
    }
    WorldSettings before = settings;
    switch (world_menu_update(&menu, &settings, buttons, now_ms)) {
    case WORLD_MENU_RESUME: resume_game(); return;
    case WORLD_MENU_EXIT: quit_requested = 1; return;
    case WORLD_MENU_SETTINGS_CHANGED: apply_changed_settings(&before); break;
    case WORLD_MENU_PICK_BACKGROUND: pick_border(); break;
    case WORLD_MENU_OPEN_CONTROLS:
        world_menu_controls_open(&controls_page, buttons | WORLD_MENU_ACCEPT, now_ms);
        capturing = 0;
        ui = UI_CONTROLS;
        world_menu_controls_render(&controls_page, &settings.bindings, 0, ui_pixels, pixels);
        return;
    case WORLD_MENU_OPEN_ACHIEVEMENTS:
        if (!record_view) break;
        memset(&record_repeat, 0, sizeof record_repeat);
        record_repeat.previous = buttons | WORLD_MENU_ACCEPT;
        record_page_keys = 3;
        ui = UI_RECORD;
        break;
    /* START_BOSS_RUSH and START_SCORE_ATTACK cannot arrive: their rows are
       locked while world_modes reports the modes unavailable. */
    default: break;
    }
    world_menu_render(&menu, &settings, ui_pixels, 320, 240, pixels);
}

static void draw(void)
{
    WorldVideoLayer layers[3];
    for (unsigned i = 0; i < 3; ++i) {
        unsigned pos = 0xb706 + 4*i;
        layers[i].master = layer_master[i];
        layers[i].scroll_y = word(ram + pos + 2);
        layers[i].scroll_x = word(ram + pos) + 16 + 2*i;
    }
    uint8_t sprites[2048] = {0};
    for (unsigned i = 0, count = (0u - ram[0x8b26]) & 255; i < count; ) {
        memcpy(sprites + i*8, oam + i*8, 8);
        i += 1u << ((oam[i*8+1] >> 3) & 3);
    }
    uint32_t colors[1024];
    for (unsigned i = 0; i < 1024; ++i) colors[i] = world_video_rgb(word(palette + 2*i));
    for (unsigned y = 0; y < 240; ++y) {
        uint16_t line[320];
        world_video_scanline(line, y, tiles, gfx1, gfx2, sprites, layers, word(ram + 0x8b28));
        for (unsigned x = 0; x < 320; ++x) pixels[y*320+x] = colors[line[x] & 1023];
    }
}

static int tick(uint16_t pads, uint16_t system)
{
    unsigned board_tick_before = word(ram + 0x276);
    world_clock_begin_pass(&board_clock);
    if (ram[0x262] & 1) {
        WorldDispatch pass;
        uint32_t addr;
        world_dispatch_begin(&pass, ram);
        unsigned calls = 0;
        while (world_dispatch_target(&pass, ram, &addr)) {
            world_clock_note_unrecovered(&board_clock);
            if (++calls > 65536) {
                snprintf(failure, sizeof failure, "Actor list did not terminate at master %04X.", word(ram + 0x80));
                return 0;
            }
            if (!world_dispatch_native_video(&pass, ram, rom, tiles)) {
                snprintf(failure, sizeof failure, "Unrecovered World handler %05lX, actor %04X, master %04X.",
                         (unsigned long)addr, pass.actor, word(ram + 0x80));
                return 0;
            }
        }
    }
    world_palette_step(ram, palette, rom);
    int sound, master_sound;
    unsigned master = word(ram + 0x80);
    world_clock_note_unrecovered(&board_clock);
    for (unsigned i = 0; i < 3; ++i)
        if (ram[0x29a] & (1u << i)) layer_master[i] = word(ram + 0x29c + 2*i);
    if (master == 0xa0e) layer_master[1] = 1;
    if (master == 0x24e2)
        for (unsigned i = 0; i < 3; ++i) layer_master[i] = (uint16_t)i;
    if (master == 0xa09 || master == 0x1d52) layer_master[1] = 0x41;
    int busy_system = -1;
    if (board_clock.pass_mode == WORLD_TIMING_ARCADE && (master == 0xa0e || master == 0x24e2))
        busy_system = system;
    if (!world_frame_boundary_irq(ram, tiles, oam, palette, rom, pads, 0xffff,
                                  system, 0xffbf, 0, busy_system,
                                  &sound, &master_sound)) {
        snprintf(failure, sizeof failure, "Unrecovered World frame transition at master %04X.", master);
        return 0;
    }
    elapsed_board_frames = (uint16_t)(word(ram + 0x276) - board_tick_before);
    /* Master C642 fades out the name-entry screen and hands over to the
       ranking table at C66E; the entered initials are final from here. */
    if (master == 0xc642 && word(ram + 0x80) == 0xc66e) save_scores();
    /* Preserve the physical interval of the recovered mid-transfer VBlank
       before these scene controllers send their sound-reset command. */
    for (unsigned i = 1; i < elapsed_board_frames; ++i) sound_frame();
    if (sound >= 0) world_audio_receive(sound_state, (uint8_t)sound);
    if (master_sound >= 0) world_audio_receive(sound_state, (uint8_t)master_sound);
    sound_frame();
    for (unsigned i = 0; i < elapsed_board_frames; ++i) world_clock_wait_vblank(&board_clock);
    if (elapsed_board_frames > 1) board_clock.busy_vblanks += elapsed_board_frames - 1;
    return 1;
}

static void record_stop(const char *dir)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/native-stops.tsv", dir);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%lu\tstage=%u\t%s\n", ticks, word(ram + 0x84), failure);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".local/world";
    data_directory = dir;
    int path_length = snprintf(settings_path, sizeof settings_path, "%s/native-settings.ini", dir);
    if (path_length < 0 || (size_t)path_length >= sizeof settings_path) return 1;
    WorldSettingsStatus settings_status = world_settings_load(settings_path, &settings);
    int timing_override = 0;
    for (int i = 2; i < argc; ++i) {
        if (strncmp(argv[i], "--timing=", 9) ||
            !world_timing_mode_parse(argv[i] + 9, &settings.timing_mode)) {
            fprintf(stderr, "Unknown native option %s. Use --timing=arcade or --timing=remove-slowdown.\n", argv[i]);
            return 1;
        }
        timing_override = 1;
    }
    settings_writable = settings_status != WORLD_SETTINGS_NEWER_VERSION;
    world_clock_reset(&board_clock, settings.timing_mode);
    if (!load(dir, "maincpu.bin", rom, sizeof rom) || !load(dir, "gfx1.bin", gfx1, sizeof gfx1)
        || !load(dir, "gfx2.bin", gfx2, sizeof gfx2)
        || !load(dir, "soundcpu.bin", sound_rom, sizeof sound_rom)
        || !load(dir, "irem.bin", samples, sizeof samples)) {
        MessageBoxA(NULL, "Open play.cmd to import your World ROM archive.", "In The Hunt", MB_ICONERROR);
        return 1;
    }
    if (!world_boot_world(ram, tiles, oam, palette, rom, 0xffbf)) {
        MessageBoxA(NULL, "World startup checks failed. Re-import the World ROM archive.",
                    "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    load_scores(dir);
    open_record(dir);
    world_ga20_reset(&pcm, samples, sizeof samples);
    if (timing_override || settings_status == WORLD_SETTINGS_MISSING) save_settings();
    fprintf(stderr, "Timing policy: %s. Original workload cost recovery is incomplete.\n",
            world_timing_mode_label(settings.timing_mode));
    world_ym_reset();
    world_audio_reset_ym(sound_rom, sound_fm_write, NULL);
    world_audio_reset(sound_state, sound_rom, 0);
    world_audio_receive(sound_state, 0); /* Power-on command gate. */
    world_audio_receive(sound_state, 0x20); /* Title initialization's reset command. */
    world_audio_tick(sound_state, sound_rom, &sound_bus);
    sound_device = world_audio_output_open();
    WorldWindowOptions window_options = { settings.window_scale, (int)settings.fullscreen,
                                          (int)settings.vsync, settings.shader };
    if (!world_window_open(&window_options, &settings.bindings)) {
        MessageBoxA(NULL, "Windows could not create the game window.", "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    apply_border();
    fprintf(stderr, "Renderer: %s.\n", world_window_direct3d() ? "Direct3D 11" : "GDI");
    if (world_window_direct3d() && strcmp(world_window_shader(), settings.shader)) {
        /* Unknown id or a rejected shader: the window shows plain pixels. */
        fprintf(stderr, "Display profile %s is unavailable. Using %s.\n", settings.shader, world_window_shader());
        strcpy(settings.shader, world_window_shader());
    }
    LARGE_INTEGER rate, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&now);
    double deadline = (double)now.QuadPart;
    double period = (double)rate.QuadPart * (424 * 262) / 6666666;
    uint16_t pads, system;
    while (!quit_requested && world_window_poll(&pads, &system)) {
        if (ui == UI_PLAY && world_window_take_menu_request()) open_menu();
        if (ui != UI_PLAY) {
            /* The board clock stands still; only the host screen is redrawn. */
            ui_frame();
            if (ui != UI_PLAY) world_window_present(ui_pixels, 1);
            Sleep(10);
            QueryPerformanceCounter(&now);
            deadline = (double)now.QuadPart;
            continue;
        }
        QueryPerformanceCounter(&now);
        if (now.QuadPart < deadline) { Sleep(1); continue; }
        if (now.QuadPart - deadline > rate.QuadPart / 4) deadline = (double)now.QuadPart;
        if (!tick(pads, system)) break;
        observe_record();
        double present_at = deadline + period * (elapsed_board_frames - 1);
        int window_open = 1;
        QueryPerformanceCounter(&now);
        while (now.QuadPart < present_at && window_open) {
            window_open = world_window_poll(&pads, &system);
            if (window_open) Sleep(1);
            QueryPerformanceCounter(&now);
        }
        if (!window_open) break;
        deadline += period * elapsed_board_frames;
        ticks += elapsed_board_frames;
        draw();
        composite_popup();
        /* Wait for the display only when the next tick is not already due. */
        QueryPerformanceCounter(&now);
        world_window_present(pixels, now.QuadPart < deadline);
    }
    world_window_close();
    world_audio_output_close();
    if (scores_pending) save_scores();
    if (service.active) {
        world_service_abort(&service, (uint64_t)time(NULL));
        record_dirty = 1;
    }
    if (record_dirty && record_path[0]) {
        record_retry_at = 0;
        save_record();
    }
    world_achievement_view_destroy(record_view);
    record_view = NULL;
    if (sound_gaps) fprintf(stderr, "%u sound callbacks need recovery.\n", sound_gaps);
    fprintf(stderr, "%llu executed blocks have unrecovered original timing in this run.\n",
            (unsigned long long)board_clock.unrecovered_costs);
    if (*failure) {
        record_stop(dir);
        fprintf(stderr, "%s\n", failure);
        MessageBoxA(NULL, failure, "In The Hunt - native coverage", MB_ICONINFORMATION);
        return 2;
    }
    return 0;
}

