/* Native Windows entry point. The imported program image is read-only lookup data. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
            output[done + i] = (int16_t)mixed;
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
                snprintf(failure, sizeof failure, "Unrecovered World handler %05lX, actor %04X, master %04X.%s%s",
                         (unsigned long)addr, pass.actor, word(ram + 0x80),
                         *world_dispatch_detail ? " " : "", world_dispatch_detail);
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
                                  system, world_dispatch_port4, 0, busy_system,
                                  &sound, &master_sound)) {
        snprintf(failure, sizeof failure, "Unrecovered World frame transition at master %04X.", master);
        return 0;
    }
    elapsed_board_frames = (uint16_t)(word(ram + 0x276) - board_tick_before);
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

/* Developer test mode: no window, no sound device, no pacing and no dialogs.
   Each script line is "FRAME HELD BUTTONS [shot]" with active-high hex masks
   in the world_window_poll layout, applied from that board frame onward;
   "FRAME end" stops the run. "shot" saves that frame as a BMP in the data
   directory, as does the last frame of the run. */
static void headless_shot(const char *dir, const char *name)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/headless-%s.bmp", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    BITMAPFILEHEADER file;
    BITMAPINFOHEADER info;
    memset(&file, 0, sizeof file);
    memset(&info, 0, sizeof info);
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof file + sizeof info;
    file.bfSize = file.bfOffBits + sizeof pixels;
    info.biSize = sizeof info;
    info.biWidth = 320;
    info.biHeight = -240;
    info.biPlanes = 1;
    info.biBitCount = 32;
    fwrite(&file, sizeof file, 1, f);
    fwrite(&info, sizeof info, 1, f);
    fwrite(pixels, sizeof pixels, 1, f);
    fclose(f);
}

static void headless_run(const char *dir, const char *script_path)
{
    FILE *script = fopen(script_path, "r");
    if (!script) {
        snprintf(failure, sizeof failure, "Cannot open the headless input script.");
        return;
    }
    unsigned held = 0, buttons = 0;
    unsigned long at = 0;
    char first[32], second[32], third[32];
    int fields = fscanf(script, "%lu %31s", &at, first);
    for (;;) {
        int shot = 0;
        while (fields == 2 && ticks >= at) {
            if (!strcmp(first, "end")) goto done;
            if (fscanf(script, "%31s", second) != 1) goto done;
            held = (unsigned)strtoul(first, NULL, 16);
            buttons = (unsigned)strtoul(second, NULL, 16);
            long mark = ftell(script);
            if (fscanf(script, "%31s", third) == 1 && !strcmp(third, "shot")) shot = 1;
            else fseek(script, mark, SEEK_SET);
            fields = fscanf(script, "%lu %31s", &at, first);
        }
        if (fields != 2) break;
        if (!tick((uint16_t)~held, (uint16_t)~buttons)) break;
        ticks += elapsed_board_frames;
        draw();
        if (shot) {
            char name[32];
            snprintf(name, sizeof name, "%06lu", ticks);
            headless_shot(dir, name);
        }
    }
done:
    fclose(script);
    draw();
    headless_shot(dir, "final");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".local/world";
    data_directory = dir;
    char settings_path[4096];
    int path_length = snprintf(settings_path, sizeof settings_path, "%s/native-settings.ini", dir);
    if (path_length < 0 || (size_t)path_length >= sizeof settings_path) return 1;
    WorldSettingsStatus settings_status = world_settings_load(settings_path, &settings);
    int timing_override = 0;
    const char *headless = NULL;
    for (int i = 2; i < argc; ++i) {
        if (!strncmp(argv[i], "--headless=", 11)) {
            headless = argv[i] + 11;
            continue;
        }
        if (strncmp(argv[i], "--timing=", 9) ||
            !world_timing_mode_parse(argv[i] + 9, &settings.timing_mode)) {
            fprintf(stderr, "Unknown native option %s. Use --timing=arcade or --timing=remove-slowdown.\n", argv[i]);
            return 1;
        }
        timing_override = 1;
    }
    world_clock_reset(&board_clock, settings.timing_mode);
    if (!load(dir, "maincpu.bin", rom, sizeof rom) || !load(dir, "gfx1.bin", gfx1, sizeof gfx1)
        || !load(dir, "gfx2.bin", gfx2, sizeof gfx2)
        || !load(dir, "soundcpu.bin", sound_rom, sizeof sound_rom)
        || !load(dir, "irem.bin", samples, sizeof samples)) {
        if (!headless) MessageBoxA(NULL, "Open play.cmd to import your World ROM archive.", "In The Hunt", MB_ICONERROR);
        return 1;
    }
    if (!world_boot_world(ram, tiles, oam, palette, rom, world_dispatch_port4)) {
        if (!headless) MessageBoxA(NULL, "World startup checks failed. Re-import the World ROM archive.",
                                   "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    world_ga20_reset(&pcm, samples, sizeof samples);
    if ((timing_override || settings_status == WORLD_SETTINGS_MISSING) &&
        settings_status != WORLD_SETTINGS_NEWER_VERSION &&
        world_settings_save(settings_path, &settings) != WORLD_SETTINGS_OK)
        fprintf(stderr, "Could not save native settings.\n");
    fprintf(stderr, "Timing policy: %s. Original workload cost recovery is incomplete.\n",
            world_timing_mode_label(settings.timing_mode));
    world_ym_reset();
    world_audio_reset_ym(sound_rom, sound_fm_write, NULL);
    world_audio_reset(sound_state, sound_rom, 0);
    world_audio_receive(sound_state, 0); /* Power-on command gate. */
    world_audio_receive(sound_state, 0x20); /* Title initialization's reset command. */
    world_audio_tick(sound_state, sound_rom, &sound_bus);
    if (headless) {
        headless_run(dir, headless);
        goto finished;
    }
    sound_device = world_audio_output_open();
    if (!world_window_open()) {
        MessageBoxA(NULL, "Windows could not create the game window.", "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    LARGE_INTEGER rate, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&now);
    double deadline = (double)now.QuadPart;
    double period = (double)rate.QuadPart * (424 * 262) / 6666666;
    uint16_t pads, system;
    while (world_window_poll(&pads, &system)) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart < deadline) { Sleep(1); continue; }
        if (now.QuadPart - deadline > rate.QuadPart / 4) deadline = (double)now.QuadPart;
        if (!tick(pads, system)) break;
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
        world_window_present(pixels);
    }
    world_window_close();
    world_audio_output_close();
finished:
    if (sound_gaps) fprintf(stderr, "%u sound callbacks need recovery.\n", sound_gaps);
    fprintf(stderr, "%llu executed blocks have unrecovered original timing in this run.\n",
            (unsigned long long)board_clock.unrecovered_costs);
    if (*failure) {
        record_stop(dir);
        fprintf(stderr, "%s\n", failure);
        if (!headless) MessageBoxA(NULL, failure, "In The Hunt - native coverage", MB_ICONINFORMATION);
        return 2;
    }
    return 0;
}

