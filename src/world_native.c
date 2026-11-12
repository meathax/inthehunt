/* Native Windows entry point. The imported program image is read-only lookup data. */
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "world_boot.h"
#include "world_dispatch.h"
#include "world_frame.h"
#include "world_palette.h"
#include "world_video.h"
#include "world_window.h"
#include "world_enhance.h"
#include "world_gpu.h"
#include "world_slowdown_table.h"
#include "world_border.h"
#include "world_audio_driver.h"
#include "world_audio_output.h"
#include "world_ga20.h"
#include "world_ym.h"
#include "world_clock.h"
#include "world_settings.h"
#include "world_menu.h"

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
static WorldMenu menu;

/* Border presets fetched by tools/get_border_art.ps1 into .local/art-sources. */
static void menu_borders(const char *dir)
{
    static char paths[2][4096];
    static const char *const files[1] = {"world-fan-background.jpg"};
    static const char *const labels[1] = {"UNDERSEA"};
    const char *names[3], *list[3];
    unsigned count = 0;
    names[count] = "BLACK";
    list[count++] = "";
    for (unsigned i = 0; i < 1; ++i) {
        snprintf(paths[i], sizeof paths[i], "%s/../art-sources/%s", dir, files[i]);
        if (GetFileAttributesA(paths[i]) == INVALID_FILE_ATTRIBUTES) continue;
        names[count] = labels[i];
        list[count++] = paths[i];
    }
    int known = 0;
    for (unsigned i = 1; i < count; ++i) known |= !strcmp(list[i], settings.border_image);
    if (settings.border_image[0] && !known &&
        GetFileAttributesA(settings.border_image) == INVALID_FILE_ATTRIBUTES) {
        /* A saved border whose file is gone (for example a retired preset) reverts to black. */
        strcpy(settings.border, "black");
        settings.border_image[0] = 0;
    }
    if (settings.border_image[0] && !known) {
        snprintf(paths[1], sizeof paths[1], "%s", settings.border_image);
        names[count] = "CUSTOM";
        list[count++] = paths[1];
    }
    world_menu_set_borders(&menu, count, names, list, &settings);
}

static uint32_t shown[320 * 240];      /* the frame as presented: enhancements and overlays */
static unsigned shown_fps, shown_speed;
static unsigned word(const uint8_t *p);

/* The sea surface on screen. The game ripples the background layer below
 * the surface with row scroll (0:FC1B, world_frame_rowscroll): when that is
 * on, the first rippled tilemap row is the waterline, which lands on screen
 * at that row minus the layer's vertical scroll. Without the ripple there is
 * no surface in view and the whole picture is treated as water. */
static int waterline_row(void)
{
    if (!(ram[0x292] & 4)) return -1;
    int row;
    switch (ram[0x298] & 14) {
    case 0: row = 112; break;
    case 2: row = 168; break;
    case 8: {
        unsigned shift = (word(ram + 0xb8ca) << 3) & 0xffff;
        row = (int)((104 - (((shift >> 8) | (shift << 8)) & 0xffff)) & 0x1ff);
        break;
    }
    default: return -1;                         /* 6: rippled from the top */
    }
    int y = (row - (int)word(ram + 0xb710)) & 0x1ff;   /* the layer is 512 rows */
    return y >= 256 ? y - 512 : y;
}

/* Animation clock for the enhancements, in 1/8 ms. */
static uint32_t effect_clock(void)
{
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint32_t)((unsigned long long)counter.QuadPart * 8000ull / (unsigned long long)frequency.QuadPart);
}

static char notice[96];
static DWORD notice_until;

/* A short message in the corner of the picture, for a few seconds. */
static void show_notice(const char *text)
{
    snprintf(notice, sizeof notice, "%s", text);
    notice_until = GetTickCount() + 5000;
}

static void apply_view(void)
{
    /* 2 = locked to the display: the loop waits for VBlank itself. */
    world_window_set_view((int)settings.scaling, settings.fx_enable && settings.fx_scanlines,
                          settings.pacing == 2 ? (int)settings.vsync : 2);
    char preset[1200] = "", error[256] = "", folder[1024];
    if (settings.crt_shader) {
        if (!world_gpu_find_shaders(data_directory, folder, sizeof folder)) {
            show_notice("CRT SHADERS NOT FOUND");
            settings.crt_shader = 0;
        } else
            snprintf(preset, sizeof preset, "%s/%s", folder, world_gpu_preset_path[settings.crt_shader - 1]);
    }
    /* Smooth and Exact pacing present through a DXGI flip-model swap chain:
       it waits for each refresh precisely and shows every frame on the
       refresh it was meant for. The CPU scanline effect needs the GDI view. */
    int flip = settings.pacing != 2 && !(settings.fx_enable && settings.fx_scanlines);
    if (!world_window_set_crt(preset, (int)settings.crt_beam, flip, error, sizeof error)) {
        fprintf(stderr, "CRT view unavailable: %s\n", error);
        if (preset[0] || settings.crt_beam) show_notice("CRT SHADER UNAVAILABLE");
        settings.crt_shader = 0;
        settings.crt_beam = 0;
        world_window_set_crt("", 0, 0, NULL, 0);
    }
}

static void enhance(const uint32_t *source, uint32_t *target, uint32_t time)
{
    if (!settings.fx_enable) {
        memcpy(target, source, sizeof shown);
        world_enhance_reset();
        return;
    }
    WorldEnhance fx;
    fx.caustics = settings.fx_caustics;
    fx.rays = settings.fx_rays;
    fx.lights = settings.fx_lights;
    fx.bloom = settings.fx_bloom;
    fx.shimmer = settings.fx_shimmer;
    fx.depth = settings.fx_depth;
    fx.snow = settings.fx_snow;
    fx.vivid = settings.fx_vivid;
    fx.vignette = settings.fx_vignette;
    fx.trails = settings.fx_trails;
    WorldEnhanceScene scene;
    scene.time = time;
    scene.scroll_x = word(ram + 0xb70e);           /* background layer x */
    scene.scroll_y = word(ram + 0xb710);           /* background layer y */
    scene.waterline = waterline_row();
    world_enhance_apply(source, target, &fx, &scene);
}

/* Presents a game frame through the enhancement pass, with the optional
 * frame-rate readout in the top right corner. */
/* Corner notices (shader loading, failures), drawn into a 320x240 frame. */
static void overlay_notices(uint32_t *frame)
{
    /* CRT presets compile in the background: say so, and fall back if one fails. */
    char why[256];
    int compiling = world_gpu_active() ? world_gpu_preset_status(why, sizeof why) : 0;
    if (compiling > 0) show_notice("LOADING CRT SHADER");
    else if (compiling < 0) {
        fprintf(stderr, "CRT shader failed: %s%c", why, 10);
        settings.crt_shader = 0;
        apply_view();
        show_notice("CRT SHADER FAILED");
    } else if (notice_until && !strcmp(notice, "LOADING CRT SHADER")) notice_until = GetTickCount();
    if (notice[0] && (int)(notice_until - GetTickCount()) > 0) {
        world_menu_draw_text(frame, 320, 240, 5, 5, 1, 0xff000000, notice);
        world_menu_draw_text(frame, 320, 240, 4, 4, 1, 0xffff8060, notice);
    } else
        notice[0] = 0;
}

/* The beam simulator's clock, in emulated refreshes: it advances on every
 * displayed refresh (by 1/k when locked to the display, by elapsed time
 * otherwise), in play and in the pause menu alike, and never goes back. */
static double beam_time;
static unsigned pacing_k;
static LONGLONG beam_last;

static void beam_step(int new_frame)
{
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    if (pacing_k) {
        /* A new game frame starts on a whole emulated refresh. */
        if (new_frame) beam_time = ceil(beam_time - 1e-6);
        else beam_time += 1.0 / pacing_k;
    } else if (beam_last)
        beam_time += (double)(now.QuadPart - beam_last) * 6666666.0 / ((double)rate.QuadPart * 424.0 * 262.0);
    beam_last = now.QuadPart;
    world_window_set_source_time(beam_time);
}

static void present_game(const uint32_t *source)
{
    enhance(source, shown, effect_clock());
    overlay_notices(shown);
    if (settings.show_fps && shown_fps) {
        char label[32];
        snprintf(label, sizeof label, "%u FPS %u%%", shown_fps, shown_speed);
        int x = 320 - world_menu_text_width(label, 1) - 4;
        world_menu_draw_text(shown, 320, 240, x + 1, 5, 1, 0xff000000, label);
        world_menu_draw_text(shown, 320, 240, x, 4, 1, 0xffffe070, label);
    }
    world_window_present(shown);
}

static void apply_border(void)
{
    world_window_set_border(strcmp(settings.border, "black") ? settings.border_image : "", settings.border_brightness);
}

/* Runs the open pause menu for one frame on the paused picture. Returns 0
 * when the player chose to quit. */
static int menu_frame(const uint32_t *paused, unsigned held, unsigned toggle, unsigned rising, int gamepad)
{
    if (toggle & 2) held |= WORLD_MENU_BACK;           /* Esc steps back inside the menu */
    unsigned result = 0;
    if (rising & 1) {
        menu.open = 0;
        result = WORLD_MENU_RESUME | (menu.dirty ? WORLD_MENU_SAVE : 0);
    } else
        result = world_menu_update(&menu, &settings, held, gamepad, GetTickCount());
    if (result & WORLD_MENU_DISPLAY) world_window_set_display(settings.fullscreen, settings.window_scale);
    if (result & WORLD_MENU_BORDER) apply_border();
    if (result & WORLD_MENU_VIEW) apply_view();
    if (result & WORLD_MENU_TIMING) world_clock_request_mode(&board_clock, settings.timing_mode);
    if ((result & WORLD_MENU_SAVE) && world_settings_save(settings_path, &settings) != WORLD_SETTINGS_OK)
        fprintf(stderr, "Could not save native settings.\n");
    if (result & WORLD_MENU_QUIT) return 0;
    if (menu.open && menu.peek) {
        /* Preview held on the enhancements page: the paused picture alone. */
        present_game(paused);
    } else if (menu.open) {
        memcpy(pixels, paused, sizeof pixels);
        world_menu_render(&menu, &settings, pixels, gfx1, GetTickCount());
        overlay_notices(pixels);
        beam_step(0);
        world_window_present(pixels);
    }
    return 1;
}

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
            int32_t mixed = (2 * (int32_t)output[done + i] * (int32_t)settings.pcm_volume / 100 +
                             fm[i] * (int32_t)settings.fm_volume / 100) * (int32_t)settings.volume / 100;
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
    if (!sound_device) return;
    /* Dynamic rate control: the video clock paces the game, so stretch or
       squeeze this frame's samples by at most 0.5 percent to hold the output
       queue near 50 ms. Linear interpolation; the phase carries over. */
    static double phase;
    static int16_t last;
    const double target = 2400.0;
    double level = (double)world_audio_output_level();
    double error = (level - target) / target;
    if (error > 1.0) error = 1.0;
    if (error < -1.0) error = -1.0;
    double step = 1.0 + 0.005 * error;      /* input samples per output sample */
    int16_t stretched[1100];
    unsigned produced = 0;
    while (produced < sizeof stretched / sizeof stretched[0]) {
        unsigned index = (unsigned)phase;
        if (index >= frames) break;
        double t = phase - index;
        int16_t a0 = index ? output[index - 1] : last, a1 = output[index];
        stretched[produced++] = (int16_t)(a0 + (a1 - a0) * t);
        phase += step;
    }
    phase -= frames;
    if (frames) last = output[frames - 1];
    if (!world_audio_output_submit(stretched, produced)) {
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

/* ---- slowdown model ------------------------------------------------------
 * The board's V33 often needs more than one frame for a logic pass; it then
 * misses a VBlank and the game slows down. The native code runs in no time,
 * so each pass is charged an estimate of the original CPU time, fitted by
 * tools/fit_slowdown.py to MAME measurements of the original program:
 * a base for the master routine, a cost per actor routine dispatched, and
 * terms for sprites, video-RAM writes and palette fading. A pass whose
 * estimate exceeds one board frame services the extra VBlanks. */
enum { SLOWDOWN_FRAME_CYCLES = 149980 };     /* 9 MHz x 424 x 262 / 6.666666 MHz */
static uint8_t slowdown_shadow[65536];
static uint32_t slowdown_estimate;
static unsigned long slowdown_frames, slowdown_passes, slowdown_overruns;
static uint32_t slowdown_by_master[65536];
static unsigned long long slowdown_sum_cycles, slowdown_sum_actors, slowdown_gameplay;
static unsigned slowdown_actors;

static uint32_t slowdown_routine(uint32_t address)
{
    unsigned lo = 0, hi = sizeof slowdown_cost / sizeof slowdown_cost[0];
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (slowdown_cost[mid].address < address) lo = mid + 1;
        else hi = mid;
    }
    if (lo < sizeof slowdown_cost / sizeof slowdown_cost[0] && slowdown_cost[lo].address == address)
        return slowdown_cost[lo].cycles;
    return slowdown_default_routine;
}

static uint32_t slowdown_master(unsigned master)
{
    for (unsigned i = 0; i < sizeof slowdown_base / sizeof slowdown_base[0]; ++i)
        if (slowdown_base[i].master == master) return slowdown_base[i].cycles;
    return slowdown_default_base;
}

/* The colours world_palette_step is about to process, by fade mode, plus
 * one-shot copies; the same test the stepper itself makes. */
static uint32_t slowdown_palette(void)
{
    uint32_t cycles = 0;
    for (unsigned bank = 0; bank < 64; ++bank) {
        const uint8_t *job = ram + 0x9e06 + 8 * bank;
        unsigned mode = job[2] & 3;
        if (job[2] & 0x80) {
            if (mode == 3) continue;
            const uint8_t *speed = rom + 0x121d + 4 * (job[4] & 15);
            if (word(ram + 0x274) & word(speed + 2)) continue;
            for (unsigned colour = 0; colour < 16; ++colour) {
                unsigned target = word(rom + 0xa0000 + ((0xa212 + 32 * word(job) + 2 * colour) & 0xffff));
                if (mode == 0 && word(palette + 32 * bank + 2 * colour) == target) continue;
                cycles += slowdown_per_faded_colour[mode];
            }
        } else if (job[2] & 0x40)
            cycles += 16 * slowdown_per_copied_colour;
    }
    return cycles;
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
            slowdown_estimate += slowdown_routine(addr);
            ++slowdown_actors;
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
    slowdown_estimate += slowdown_palette();
    world_palette_step(ram, palette, rom);
    int sound, master_sound;
    unsigned master = word(ram + 0x80);
    world_clock_note_unrecovered(&board_clock);
    for (unsigned i = 0; i < 3; ++i)
        if (ram[0x29a] & (1u << i)) layer_master[i] = word(ram + 0x29c + 2*i);
    if (master == 0xa0e) layer_master[1] = 1;
    if (master == 0x24e2 || master == 0x344d || master == 0x333d)       /* OUT 98/9A/9C = 0, 1, 2 */
        for (unsigned i = 0; i < 3; ++i) layer_master[i] = (uint16_t)i;
    if (master == 0xa09 || master == 0x1d52) layer_master[1] = 0x41;
    int busy_system = -1;
    if (board_clock.pass_mode == WORLD_TIMING_ARCADE && (master == 0xa0e || master == 0x24e2))
        busy_system = system;
    /* Rest of the estimate: video-RAM words written since the last decision
       (the previous boundary's transfers and this pass's), sprites, master. */
    unsigned written = 0;
    for (unsigned i = 0; i < sizeof slowdown_shadow; i += 2)
        if (memcmp(slowdown_shadow + i, tiles + i, 2)) ++written;
    memcpy(slowdown_shadow, tiles, sizeof slowdown_shadow);
    slowdown_estimate += written * slowdown_per_vram_write + slowdown_master(master) +
                         ((0u - ram[0x8b26]) & 255) * slowdown_per_sprite;
    unsigned vblanks = 1;
    if (board_clock.pass_mode == WORLD_TIMING_ARCADE && busy_system < 0) {
        vblanks = 1 + slowdown_estimate / SLOWDOWN_FRAME_CYCLES;
        if (vblanks > 4) vblanks = 4;
    }
    if (master == 0xb2e) {
        ++slowdown_gameplay;
        slowdown_sum_cycles += slowdown_estimate;
        slowdown_sum_actors += slowdown_actors;
    }
    slowdown_actors = 0;
    slowdown_frames += vblanks - 1;
    ++slowdown_passes;
    slowdown_overruns += vblanks > 1;
    if (vblanks > 1) ++slowdown_by_master[master];
    slowdown_estimate = 0;
    int boundary_ok = vblanks > 1
        ? world_frame_boundary_irq_vblanks(ram, tiles, oam, palette, rom, pads, 0xffff, system,
                                           world_dispatch_port4, vblanks, &sound, &master_sound)
        : world_frame_boundary_irq(ram, tiles, oam, palette, rom, pads, 0xffff,
                                   system, world_dispatch_port4, 0, busy_system, &sound, &master_sound);
    if (!boundary_ok) {
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
static const uint32_t *shot_source = pixels;

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
    fwrite(shot_source, sizeof pixels, 1, f);
    fclose(f);
}

/* Test-only autopilot for scripted runs: it reads work RAM to choose the
   held inputs and changes no game state. While the stage-three end boss body
   (1000:45BA..4C19) is in the actor list, player one moves under it, keeps
   to the surface and alternates both fire buttons; otherwise `fallback`. */
static unsigned headless_autopilot(unsigned fallback)
{
    static const unsigned boss_states[] = {0x45ba, 0x4614, 0x4640, 0x465a, 0x46a5, 0x46e4, 0x47cb,
                                           0x4811, 0x4837, 0x48a4, 0x49ab, 0x49ce, 0x4c19};
    unsigned boss = 0, actor = 0x400;
    for (unsigned steps = 0; steps < 512 && actor; ++steps) {
        if (word(ram + actor + 2) == 0x1000) {
            unsigned state = word(ram + actor);
            for (unsigned i = 0; i < sizeof boss_states / sizeof *boss_states; ++i)
                if (state == boss_states[i]) boss = actor;
        }
        unsigned next = word(ram + actor + 4);
        if (next == actor || next == 0x400) break;
        actor = next;
    }
    unsigned sub = word(ram + 0x90);
    unsigned phase = (unsigned)(ticks % 12);
    unsigned fire = phase < 4 ? 0x40 : (phase >= 6 && phase < 10) ? 0x80 : 0;
    if (!boss) {
        /* Stage four's rising camera (0:3E29) moves only while a player is
           below its word 4C: steer there, mid-screen. */
        unsigned camera = 0;
        actor = 0x400;
        for (unsigned steps = 0; steps < 512 && actor; ++steps) {
            if (word(ram + actor + 2) == 0 && (word(ram + actor) == 0x3e29 || word(ram + actor) == 0x3e6c)) camera = actor;
            unsigned next = word(ram + actor + 4);
            if (next == actor || next == 0x400) break;
            actor = next;
        }
        if (!camera) return fallback;
        /* In the descent, sweep across the screen so an ice ceiling over
           one column cannot hold the sub below word 4C forever. */
        int column = word(ram + camera) == 0x3e6c ? 0x90 + 0x40 * (int)((ticks / 600) % 5) : 0x100;
        int tx = column - (int)word(ram + sub + 0x21);
        /* Stage five's descent (0:3E6C) sinks only while a player is at or
           below word 4C (and none above C8h): steer just below it. */
        int depth = word(ram + camera) == 0x3e6c ? 0x10 : -0x20;
        int ty = (int)word(ram + camera + 0x4c) + depth - (int)word(ram + sub + 0x24);
        /* Breaking the stage-five ice ceiling needs many upward missiles:
           press each fire button on alternate pairs of frames. */
        unsigned held = word(ram + camera) == 0x3e6c ? ((ticks & 2) ? 0x40 : 0x80) : fire;
        if (tx > 6) held |= 0x01;
        else if (tx < -6) held |= 0x02;
        if (ty > 6) held |= 0x08;
        else if (ty < -6) held |= 0x04;
        return held;
    }
    int dx = (int16_t)(word(ram + boss + 0x21) - word(ram + sub + 0x21));
    unsigned held = 0x08;                       /* keep rising to the surface */
    if (dx > 6) held |= 0x01;
    else if (dx < -6) held |= 0x02;
    return held | fire;                         /* both fire buttons in turn */
}

static void headless_run(const char *dir, const char *script_path)
{
    FILE *script = fopen(script_path, "r");
    if (!script) {
        snprintf(failure, sizeof failure, "Cannot open the headless input script.");
        return;
    }
    unsigned held = 0, buttons = 0, autopilot = 0;
    unsigned long at = 0;
    char first[32], second[32], third[32];
    int fields = fscanf(script, "%lu %31s", &at, first);
    char trace_path[4096];
    snprintf(trace_path, sizeof trace_path, "%s/headless-trace.txt", dir);
    FILE *trace = fopen(trace_path, "w");
    /* Debug acceleration only: WORLD_SNAPSHOT_LOAD=path resumes from a
       snapshot; WORLD_SNAPSHOT_SAVE=frame,path writes one. Not an exact
       machine state (YM/PCM and per-pass dispatch caches are not kept). */
    const char *load_path = getenv("WORLD_SNAPSHOT_LOAD"), *save_spec = getenv("WORLD_SNAPSHOT_SAVE");
    unsigned long save_at = 0;
    const char *save_path = NULL;
    if (save_spec) {
        save_at = strtoul(save_spec, NULL, 10);
        save_path = strchr(save_spec, ',');
        if (save_path) ++save_path;
    }
    if (load_path) {
        FILE *f = fopen(load_path, "rb");
        if (!f || fread(ram, sizeof ram, 1, f) != 1 || fread(tiles, sizeof tiles, 1, f) != 1 ||
            fread(oam, sizeof oam, 1, f) != 1 || fread(palette, sizeof palette, 1, f) != 1 ||
            fread(layer_master, sizeof layer_master, 1, f) != 1 || fread(&ticks, sizeof ticks, 1, f) != 1 ||
            fread(&board_clock, sizeof board_clock, 1, f) != 1 || fread(sound_state, sizeof sound_state, 1, f) != 1) {
            snprintf(failure, sizeof failure, "Cannot load the headless snapshot.");
            if (f) fclose(f);
            if (trace) fclose(trace);
            fclose(script);
            return;
        }
        fclose(f);
    }
    for (;;) {
        /* WORLD_MENU_SHOT=frame renders every menu page over that frame. */
        static int menu_shot_done;
        const char *menu_shot = getenv("WORLD_MENU_SHOT");
        if (menu_shot && !menu_shot_done && ticks >= strtoul(menu_shot, NULL, 10)) {
            menu_shot_done = 1;
            static uint32_t still[320*240];
            memcpy(still, pixels, sizeof still);
            menu_borders(dir);
            static const unsigned pages[][4] = {{0,0,0,0},{1,0,0,1},{1,0,1,3},{1,1,1,1},{1,2,1,1},{2,0,0,0},{3,0,0,0},
                                                {1,0,6,0},{1,2,3,0},{1,3,1,0},{1,3,9,0}};
            for (unsigned i = 0; i < sizeof pages / sizeof pages[0]; ++i) {
                world_menu_open(&menu, 0, i & 1, 0);
                menu.page = pages[i][0];
                menu.tab = pages[i][1];
                menu.row = pages[i][2];
                memcpy(pixels, still, sizeof still);
                world_menu_render(&menu, &settings, pixels, gfx1, 1500 + 400 * i);
                char name[32];
                snprintf(name, sizeof name, "menu%u", i);
                headless_shot(dir, name);
            }
            memcpy(pixels, still, sizeof still);
            menu.open = 0;
        }
        /* WORLD_FX_SHOT=frame saves that frame with no enhancement, with each
           one alone, and with all of them (two moments, for the animation). */
        static int fx_shot_done;
        const char *fx_shot = getenv("WORLD_FX_SHOT");
        if (fx_shot && !fx_shot_done && ticks >= strtoul(fx_shot, NULL, 10)) {
            fx_shot_done = 1;
            static const char *const names[12] = {"off", "caustics", "rays", "lights", "bloom", "shimmer",
                                                  "depth", "snow", "vivid", "vignette", "all", "all-later"};
            WorldSettings keep = settings;
            for (unsigned i = 0; i < 12; ++i) {
                unsigned all = i >= 10;
                settings.fx_enable = i != 0;
                settings.fx_caustics = all || i == 1; settings.fx_rays = all || i == 2;
                settings.fx_lights = all || i == 3;   settings.fx_bloom = all || i == 4;
                settings.fx_shimmer = all || i == 5;  settings.fx_depth = all || i == 6;
                settings.fx_snow = all || i == 7;     settings.fx_vivid = all || i == 8;
                settings.fx_vignette = all || i == 9; settings.fx_trails = 0;
                enhance(pixels, shown, (uint32_t)(ticks * 133 + (i == 11 ? 5600 : 0)));
                char name[32];
                snprintf(name, sizeof name, "fx-%s", names[i]);
                shot_source = shown;
                headless_shot(dir, name);
                shot_source = pixels;
            }
            settings = keep;
            world_enhance_reset();
        }
        /* WORLD_CRT_SHOT=frame renders that frame through each CRT preset
           off screen at 1920x1080 (needs Direct3D 12 and the shader package). */
        static int crt_shot_done;
        const char *crt_shot = getenv("WORLD_CRT_SHOT");
        if (crt_shot && !crt_shot_done && ticks >= strtoul(crt_shot, NULL, 10)) {
            crt_shot_done = 1;
            char folder[1024];
            static uint32_t big[1920 * 1080];
            if (!world_gpu_find_shaders(dir, folder, sizeof folder)) {
                snprintf(failure, sizeof failure, "CRT shot: shader package not found");
                break;
            }
            for (unsigned i = 0; i <= WORLD_GPU_PRESETS; ++i) {
                char preset[1200] = "", error[512] = "", path[4096];
                if (i) snprintf(preset, sizeof preset, "%s/%s", folder, world_gpu_preset_path[i - 1]);
                snprintf(path, sizeof path, "%s/headless-crt%02u.bmp", dir, i);
                DWORD started = GetTickCount();
                if (!world_gpu_render_offscreen(pixels, preset, 0, 1920, 1080, big, error, sizeof error)) {
                    fprintf(stderr, "CRT preset %u (%s) failed: %s\n", i, i ? world_gpu_preset_name[i - 1] : "OFF", error);
                    continue;
                }
                FILE *f = fopen(path, "wb");
                if (!f) continue;
                BITMAPFILEHEADER file = {0};
                BITMAPINFOHEADER info = {0};
                file.bfType = 0x4d42;
                file.bfOffBits = sizeof file + sizeof info;
                file.bfSize = file.bfOffBits + sizeof big;
                info.biSize = sizeof info;
                info.biWidth = 1920;
                info.biHeight = -1080;
                info.biPlanes = 1;
                info.biBitCount = 32;
                fwrite(&file, sizeof file, 1, f);
                fwrite(&info, sizeof info, 1, f);
                fwrite(big, sizeof big, 1, f);
                fclose(f);
                fprintf(stderr, "CRT preset %u (%s) rendered in %lu ms%c", i, i ? world_gpu_preset_name[i - 1] : "OFF", (unsigned long)(GetTickCount() - started), 10);
            }
        }
        /* WORLD_BORDER_SHOT=frame composes that frame with every border
           preset at 1920x1080 (and 1280x1024) through the window painter. */
        static int border_shot_done;
        const char *border_shot = getenv("WORLD_BORDER_SHOT");
        if (border_shot && !border_shot_done && ticks >= strtoul(border_shot, NULL, 10)) {
            border_shot_done = 1;
            int border_failed = 0;
            menu_borders(dir);
            static const int sizes[2][2] = {{1920, 1080}, {1280, 1024}};
            for (unsigned i = 0; i < menu.border_count; ++i) {
                world_border_set(menu.border_path[i], 100);
                for (unsigned s = 0; s < 2; ++s) {
                    char path[4096];
                    snprintf(path, sizeof path, "%s/headless-border%u-%dx%d.bmp", dir, i,
                             sizes[s][0], sizes[s][1]);
                    if (!world_window_compose_bmp(pixels, sizes[s][0], sizes[s][1], path)) {
                        snprintf(failure, sizeof failure, "Border composition failed: %s", menu.border_name[i]);
                        border_failed = 1;
                    }
                }
            }
            world_border_set("", 100);
            /* The three scaling modes, and scanlines, on a black border. */
            static const char *const views[4] = {"integer", "fit", "stretch", "scanlines"};
            for (int v = 0; v < 4 && !border_failed; ++v) {
                char path[4096];
                world_window_set_view(v < 3 ? v : 0, v == 3, 0);
                snprintf(path, sizeof path, "%s/headless-view-%s.bmp", dir, views[v]);
                if (!world_window_compose_bmp(pixels, 1920, 1080, path)) {
                    snprintf(failure, sizeof failure, "View composition failed: %s", views[v]);
                    border_failed = 1;
                }
            }
            world_window_set_view(0, 0, 0);
            if (border_failed) break;
        }
        if (save_path && ticks >= save_at) {
            FILE *f = fopen(save_path, "wb");
            if (f) {
                fwrite(ram, sizeof ram, 1, f);
                fwrite(tiles, sizeof tiles, 1, f);
                fwrite(oam, sizeof oam, 1, f);
                fwrite(palette, sizeof palette, 1, f);
                fwrite(layer_master, sizeof layer_master, 1, f);
                fwrite(&ticks, sizeof ticks, 1, f);
                fwrite(&board_clock, sizeof board_clock, 1, f);
                fwrite(sound_state, sizeof sound_state, 1, f);
                fclose(f);
            }
            save_path = NULL;
        }
        int shot = 0;
        while (fields == 2 && ticks >= at) {
            if (!strcmp(first, "end")) goto done;
            if (fscanf(script, "%31s", second) != 1) goto done;
            autopilot = first[0] == 'a';
            held = (unsigned)strtoul(first + autopilot, NULL, 16);
            buttons = (unsigned)strtoul(second, NULL, 16);
            long mark = ftell(script);
            if (fscanf(script, "%31s", third) == 1 && !strcmp(third, "shot")) shot = 1;
            else fseek(script, mark, SEEK_SET);
            fields = fscanf(script, "%lu %31s", &at, first);
        }
        if (fields != 2) break;
        unsigned input = autopilot ? headless_autopilot(held) : held;
        if (!tick((uint16_t)~input, (uint16_t)~buttons)) break;
        unsigned long before = ticks;
        ticks += elapsed_board_frames;
        /* WORLD_SCROLL_LOG=path: the playfield and background scroll at every
           board frame (passes that overran repeat their values). */
        static FILE *scroll_log;
        static int scroll_log_checked;
        if (!scroll_log_checked) {
            scroll_log_checked = 1;
            const char *path = getenv("WORLD_SCROLL_LOG");
            if (path) scroll_log = fopen(path, "w");
        }
        if (scroll_log)
            for (unsigned long f = before; f < ticks; ++f)
                fprintf(scroll_log, "%lu %u %u %u %u %u\n", f, word(ram + 0xb70a), word(ram + 0xb70c),
                        word(ram + 0xb70e), word(ram + 0xb710), (unsigned)(ticks - before));
        if (trace && before / 1000 != ticks / 1000) {
            unsigned sub = word(ram + 0x90);
            unsigned cfg = word(ram + sub + 0x70);
            fprintf(trace, "%lu stage %u B700 %04x 26E %04x 264 %04x sub %04x x %04x y %04x input %02x up %04x mid %04x down %04x\n",
                    ticks, word(ram + 0x84), word(ram + 0xb700), word(ram + 0x26e), word(ram + 0x264),
                    word(ram + sub), word(ram + sub + 0x21), word(ram + sub + 0x24), input & 0xff,
                    word(ram + cfg + 4), word(ram + cfg + 8), word(ram + cfg + 6));
        }
        draw();
        if (shot) {
            char name[32];
            snprintf(name, sizeof name, "%06lu", ticks);
            /* WORLD_FX_SHOTS=1 saves every shot with the water effects on. */
            int with_fx = getenv("WORLD_FX_SHOTS") != NULL;
            if (with_fx) {
                WorldSettings keep = settings;
                settings.fx_enable = settings.fx_caustics = settings.fx_rays = settings.fx_depth = 1;
                settings.fx_lights = settings.fx_bloom = settings.fx_snow = settings.fx_shimmer = 0;
                settings.fx_vivid = settings.fx_vignette = settings.fx_trails = 0;
                enhance(pixels, shown, (uint32_t)(ticks * 133));
                settings = keep;
                shot_source = shown;
            }
            headless_shot(dir, name);
            shot_source = pixels;
            char log_path[4096];
            snprintf(log_path, sizeof log_path, "%s/headless-scroll.txt", dir);
            FILE *log = fopen(log_path, "a");
            if (log) {
                fprintf(log, "%06lu stage %u", ticks, word(ram + 0x84));
                for (unsigned i = 0; i < 3; ++i)
                    fprintf(log, " L%u %04x %04x m%04x", i, word(ram + 0xb706 + 4*i), word(ram + 0xb708 + 4*i), layer_master[i]);
                fprintf(log, " vc %04x water %d\n", word(ram + 0x8b28), with_fx ? world_enhance_waterline() : -1);
                fclose(log);
            }
        }
    }
done:
    if (trace) fclose(trace);
    fclose(script);
    draw();
    headless_shot(dir, "final");
    /* Test-only: the live actor list with its key words, for diagnosing runs. */
    char path[4096];
    snprintf(path, sizeof path, "%s/headless-actors.txt", dir);
    FILE *list = fopen(path, "w");
    if (list) {
        fprintf(list, "frame %lu master %04x stage %u B700 %04x B70A %04x B70C %04x 26E %04x 262 %04x 264 %04x B8C2 %04x 28C %04x P1 %04x P2 %04x 25C %04x\n",
                ticks, word(ram + 0x80), word(ram + 0x84), word(ram + 0xb700), word(ram + 0xb70a),
                word(ram + 0xb70c), word(ram + 0x26e), word(ram + 0x262), word(ram + 0x264),
                word(ram + 0xb8c2), word(ram + 0x28c), word(ram + 0x90), word(ram + 0x92), word(ram + 0x25c));
        unsigned actor = 0x400;
        for (unsigned steps = 0; steps < 512 && actor; ++steps) {
            fprintf(list, "%04x %04x:%04x x %04x y %04x hp %04x 1E %04x 40 %04x 42 %04x 44 %04x 46 %04x 4C %04x 50 %04x 60 %04x 6A %04x\n",
                    actor, word(ram + actor + 2), word(ram + actor), word(ram + actor + 0x21), word(ram + actor + 0x24),
                    word(ram + actor + 0x26), word(ram + actor + 0x1e), word(ram + actor + 0x40), word(ram + actor + 0x42),
                    word(ram + actor + 0x44), word(ram + actor + 0x46), word(ram + actor + 0x4c), word(ram + actor + 0x50),
                    word(ram + actor + 0x60), word(ram + actor + 0x6a));
            unsigned next = word(ram + actor + 4);
            if (next == actor || next == 0x400) break;
            actor = next;
        }
        fclose(list);
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".local/world";
    data_directory = dir;
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
    if (!headless) {
        /* Cabinet switch bank 1 as the board reads it (active low): lives on
           bits 0-1, difficulty on bits 2-3, demo sound on bit 6. Scripted
           runs keep the factory setting. */
        static const uint16_t lives[4] = {2, 3, 1, 0}, difficulty[4] = {0x0, 0x8, 0xc, 0x4};
        world_dispatch_port4 = (uint16_t)((0xffbf & ~0x4f) | lives[settings.dip_lives & 3] |
                                          difficulty[settings.dip_difficulty & 3] |
                                          (settings.dip_demo_sound ? 0 : 0x40));
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
    fprintf(stderr, "Timing policy: %s (pass costs fitted to MAME measurements).\n",
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
    world_window_set_display(settings.fullscreen, settings.window_scale);
    menu_borders(dir);
    apply_border();
    apply_view();
    timeBeginPeriod(1);                 /* Sleep(1) must mean one millisecond for even pacing */
    LARGE_INTEGER rate, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&now);
    double deadline = (double)now.QuadPart;
    const double arcade_period = (double)rate.QuadPart * (424 * 262) / 6666666;
    uint16_t pads, system;
    static uint32_t paused_frame[320*240];
    unsigned previous_toggle = 0, hold_input = 0, counted = 0, presented = 0;
    int was_focused = 0, fresh = 0;
    LONGLONG counted_from = now.QuadPart;
    double present_deadline = (double)now.QuadPart;
    /* Display lock (Smooth and Exact pacing): when the display refresh is a
       whole multiple k of the board's 60.01 Hz (within 0.6 %), the loop runs
       on the display's VBlank. It presents on every refresh and runs a game
       frame every k refreshes, so each game frame is shown for exactly k
       refreshes and scrolling is perfectly even. A pass that overran keeps
       its frame up for k refreshes per board frame. Smooth runs the game at
       the display's pace (60.000 Hz on a 60/120 Hz display, 0.02 % slower than
       the board); Exact keeps the board's 60.0125 Hz by running one extra
       frame whenever the display falls a whole frame behind (about every 80 s
       at 60/120 Hz). Timer pacing, or a display that is not a whole multiple,
       uses the board clock and the timer loop below. */
    unsigned lock_k = 0, lock_wait = 0, lock_phase = 0, lock_span = 1;
    double lock_hz = 0.0, lock_origin = 0.0;
    unsigned long lock_ticks = 0, live_ticks = 0;
    unsigned lock_ready = 0, lock_next_span = 1;
    static uint32_t live[320 * 240];          /* the game frame on show while locked */
    LONGLONG lock_checked = 0;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    /* Test hooks: WORLD_PACING_LOG=path logs every displayed refresh;
       WORLD_RUN_SECONDS=n quits after n seconds. */
    FILE *pacing_log = getenv("WORLD_PACING_LOG") ? fopen(getenv("WORLD_PACING_LOG"), "w") : NULL;
    double run_seconds = getenv("WORLD_RUN_SECONDS") ? atof(getenv("WORLD_RUN_SECONDS")) : 0.0;
    LONGLONG run_start = now.QuadPart;
    while (world_window_poll(&pads, &system)) {
        /* F1, Esc or L3+R3 opens the pause menu; the game stands still. */
        unsigned toggle;
        int gamepad;
        unsigned menu_held = world_window_menu_buttons(&toggle, &gamepad);
        unsigned rising = toggle & ~previous_toggle;
        previous_toggle = toggle;
        /* Losing the window focus pauses, when that option is on. */
        int focused = world_window_focused();
        if (settings.pause_unfocused && was_focused && !focused && !menu.open) rising |= 1;
        was_focused = focused;
        QueryPerformanceCounter(&now);
        if (run_seconds > 0.0 && (double)(now.QuadPart - run_start) > run_seconds * (double)rate.QuadPart) break;
        /* Re-check the display twice a second (mode changes, moving monitors). */
        if (now.QuadPart - lock_checked > rate.QuadPart / 2) {
            lock_checked = now.QuadPart;
            unsigned k = 0;
            double hz = settings.pacing != 2 ? world_window_refresh_hz() : 0.0;
            if (hz > 20.0) {
                k = (unsigned)(hz / 60.0125 + 0.5);
                if (k < 1 || k > 8 || fabs(hz / k - 60.0125) / 60.0125 > 0.006) k = 0;
            }
            if (k != lock_k || fabs(hz - lock_hz) > 0.01) {
                if (pacing_log) fprintf(pacing_log, "lock %.4f %u%c", hz, k, 10);
                lock_k = k;
                lock_hz = hz;
                lock_wait = lock_phase = lock_ready = 0;
                lock_origin = (double)now.QuadPart;
                lock_ticks = 0;
                deadline = (double)now.QuadPart;
            }
        }
        pacing_k = lock_k;
        /* Test hooks: WORLD_TEST_CRT_CYCLE=n switches to the next CRT preset
           (beam simulator on) every n refreshes; WORLD_TEST_MENU=1 holds the
           pause menu open on the Video tab. */
        static unsigned long test_refreshes;
        ++test_refreshes;
        if (getenv("WORLD_TEST_CRT_CYCLE")) {
            unsigned long every = strtoul(getenv("WORLD_TEST_CRT_CYCLE"), NULL, 10);
            if (every && test_refreshes % every == 0) {
                settings.crt_shader = settings.crt_shader % WORLD_GPU_PRESETS + 1;
                settings.crt_beam = 1;
                apply_view();
                if (pacing_log) fprintf(pacing_log, "switch %u%c", settings.crt_shader, 10);
            }
        }
        if (getenv("WORLD_TEST_MENU") && !menu.open && test_refreshes == 180) {
            memcpy(paused_frame, lock_k ? live : pixels, sizeof paused_frame);
            world_menu_open(&menu, 0, 0, GetTickCount());
            menu.page = 1;
        }
        unsigned limit = world_menu_fps[settings.fps_limit];
        double period = arcade_period;
        double display_period = !limit || limit == 0xffffffffu ? 0.0 : (double)rate.QuadPart / limit;
        if (menu.open) {
            if (!menu_frame(paused_frame, menu_held, toggle, rising, gamepad)) break;
            if (!menu.open) {
                hold_input = 1;
                previous_toggle = toggle;
                QueryPerformanceCounter(&now);
                deadline = (double)now.QuadPart;
                lock_wait = lock_phase = lock_ready = 0;
                lock_origin = (double)now.QuadPart;
                lock_ticks = 0;
            }
            if (lock_k) world_window_wait_vblank();
            else Sleep(15);
            if (pacing_log) {
                QueryPerformanceCounter(&now);
                fprintf(pacing_log, "%.4f menu %.3f%c", (double)now.QuadPart * 1000.0 / (double)rate.QuadPart, beam_time, 10);
            }
            continue;
        }
        if (rising) {
            memcpy(paused_frame, lock_k ? live : pixels, sizeof paused_frame);
            world_menu_open(&menu, menu_held | ((toggle & 2) ? WORLD_MENU_BACK : 0), gamepad, GetTickCount());
            menu_frame(paused_frame, menu_held, toggle, 0, gamepad);
            continue;
        }
        /* Keys used to leave the menu are not passed to the game until released. */
        if (hold_input) {
            if ((uint16_t)~pads || ((uint16_t)~system & 0xf) || menu_held) pads = system = 0xffff;
            else hold_input = 0;
        }

        if (lock_k) {
            /* The next game frame is computed during the last refresh of the
               current one (all of it when k is 1), so the refresh where it
               goes live only has to present it. */
            if (!lock_ready && (lock_wait <= 1 || lock_k == 1)) {
                unsigned runs = 1;
                if (settings.pacing == 1) {
                    /* Exact: keep the game frame count on the board clock. */
                    double owed = ((double)now.QuadPart - lock_origin) / period - (double)lock_ticks;
                    if (owed > 1.5) runs = 2;
                    else if (owed < -1.5) runs = 0;
                    if (runs != 1 && pacing_log) fprintf(pacing_log, "correct %u owed %.3f%c", runs, owed, 10);
                }
                /* The hold is set by the first pass alone: an overrun keeps
                   its frame up for every board frame it took, while an Exact
                   catch-up pass only advances the game. */
                unsigned span = 0;
                for (unsigned r = 0; r < runs; ++r) {
                    if (!tick(pads, system)) { runs = 99; break; }
                    ticks += elapsed_board_frames;
                    lock_ticks += elapsed_board_frames;
                    counted += elapsed_board_frames;
                    if (!r) span = elapsed_board_frames;
                }
                if (runs == 99) break;
                if (runs) draw();
                lock_next_span = span ? span : 1;
                lock_ready = 1;
            }
            if (!lock_wait) {
                memcpy(live, pixels, sizeof live);
                live_ticks = ticks;
                lock_span = lock_next_span;
                lock_wait = lock_k * lock_span;
                lock_phase = 0;
                lock_ready = 0;
            }
            beam_step(lock_phase == 0);
            present_game(live);
            ++presented;
            fresh = 0;
            --lock_wait;
            ++lock_phase;
            world_window_wait_vblank();
            if (pacing_log) {
                QueryPerformanceCounter(&now);
                fprintf(pacing_log, "%.4f %lu %u %u %u\n", (double)now.QuadPart * 1000.0 / (double)rate.QuadPart,
                        live_ticks, lock_phase, lock_k, word(ram + 0xb70a));
                unsigned shown_present = 0, shown_refresh = 0, made = 0;
                if (world_gpu_frame_statistics(&shown_present, &shown_refresh, &made))
                    fprintf(pacing_log, "dxgi %u %u %u%c", shown_present, shown_refresh, made, 10);
            }
        } else {
            if (now.QuadPart >= deadline) {
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
                counted += elapsed_board_frames;
                fresh = 1;
            }
            /* Arcade redraws once per game frame; a limit redraws on its own
               clock; uncapped redraws every pass (v-sync, if on, paces it). */
            int show = 0;
            QueryPerformanceCounter(&now);
            if (settings.crt_beam && world_window_gpu()) show = 1;   /* one sub-frame per display refresh */
            else if (!limit) show = fresh;
            else if (limit == 0xffffffffu) show = 1;
            else if (now.QuadPart >= present_deadline) {
                show = 1;
                present_deadline += display_period;
                if (now.QuadPart - present_deadline > rate.QuadPart / 4) present_deadline = (double)now.QuadPart;
            }
            if (show) {
                beam_step(0);
                present_game(pixels);
                ++presented;
                fresh = 0;
                if (pacing_log)
                    fprintf(pacing_log, "%.4f %lu 0 0 %u%c", (double)now.QuadPart * 1000.0 / (double)rate.QuadPart,
                            ticks, word(ram + 0xb70a), 10);
            } else {
                double next = deadline;
                if (limit && limit != 0xffffffffu && present_deadline < next) next = present_deadline;
                if (next - (double)now.QuadPart > (double)rate.QuadPart / 1000.0) Sleep(1);
            }
        }
        world_audio_output_pump();
        QueryPerformanceCounter(&now);
        if (now.QuadPart - counted_from >= rate.QuadPart / 2) {
            double seconds = (double)(now.QuadPart - counted_from) / (double)rate.QuadPart;
            shown_fps = (unsigned)(presented / seconds + 0.5);
            shown_speed = (unsigned)(counted / seconds * arcade_period / (double)rate.QuadPart * 100.0 + 0.5);
            counted = presented = 0;
            counted_from = now.QuadPart;
        }
    }
    if (pacing_log) fclose(pacing_log);
    timeEndPeriod(1);
    world_window_close();
    world_audio_output_close();
finished:
    for (unsigned m = 0; m < 65536; ++m)
        if (slowdown_by_master[m]) fprintf(stderr, "  overruns at master %04X: %u%c", m, slowdown_by_master[m], 10);
    if (slowdown_gameplay)
        fprintf(stderr, "  gameplay passes %llu: mean estimate %llu cycles, mean actors %.1f%c", slowdown_gameplay,
                slowdown_sum_cycles / slowdown_gameplay, (double)slowdown_sum_actors / (double)slowdown_gameplay, 10);
    fprintf(stderr, "Slowdown model: %lu of %lu passes overran, %lu frames lost.\n", slowdown_overruns, slowdown_passes, slowdown_frames);
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

