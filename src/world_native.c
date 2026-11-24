/* Native Windows entry point. The imported program image is read-only lookup data. */
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <xmmintrin.h>
#include <stdio.h>
#include <string.h>
#include "world_boss_light.h"
#include "world_boot.h"
#include "world_dispatch.h"
#include "world_frame.h"
#include "world_stage.h"
#include "world_palette.h"
#include "world_video.h"
#include "world_window.h"
#include "world_scene.h"
#include "world_surface.h"
#include "world_gpu.h"
#include "world_slowdown_table.h"
#include "world_stage6_timing.h"
#include "world_audio_driver.h"
#include "world_audio_output.h"
#include "world_audio_immersion.h"
#include "world_ga20.h"
#include "world_ym.h"
#include "world_clock.h"
#include "world_settings.h"
#include "world_file.h"
#include "world_setup.h"
#include "world_menu.h"
#include "world_shadow.h"
#include "world_view.h"
extern int world_terrain_mirror, world_terrain_cam;

static uint8_t rom[1<<20], gfx1[2<<20], gfx2[4<<20], ram[65536], tiles[65536];
static uint8_t oam[2048], palette[2048];
extern double world_scene_stage_ms[6];
static uint32_t pixels[WORLD_VIEW_MAX_WIDTH * WORLD_VIEW_HEIGHT];
/* Columns in the last drawn frame: 320 outside a widescreen gameplay
   session (the attract stays 4:3), 428 while 0:B2E runs under Widescreen. */
static unsigned frame_w = 320;
static unsigned shot_w = 320;          /* unscaled width of the last captured frame */
static int frame_left(void)
{
    return -world_view_wing_of(frame_w);
}
/* Title entry at 0:1D52 programs all three playfield controls. */
static const uint16_t title_master_controls[3] = {0, 0x41, 2};
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

/* The frame as presented: enhancements and overlays, shown_k times frame_w x 240. */
static uint32_t shown[WORLD_VIEW_MAX_WIDTH * 240 * WORLD_SCENE_MAX_K * WORLD_SCENE_MAX_K];
static int shown_k = 1;
static double scene_alpha = 1.0;       /* current pose plus fractional camera advance */
/* What draw() last drew, for the scene renderer. */
static WorldVideoLayer drawn_layers[3];
static uint8_t drawn_sprites[2048];
static uint32_t drawn_colors[1024];
static unsigned shown_fps, shown_speed;
static unsigned word(const uint8_t *p);

/* The sea boundary also covers static, non-rippled stage-three water. */
static int waterline_row(void)
{
    return world_surface_row(ram);
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
    world_window_set_view((int)settings.scaling, 2);
    world_gpu_set_ambient_borders((int)settings.ambient_borders);
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
       refresh it was meant for. */
    int flip = 1;
    if (!world_window_set_crt(preset, 0, flip, error, sizeof error)) {
        fprintf(stderr, "CRT view unavailable: %s\n", error);
        if (preset[0]) show_notice("CRT SHADER UNAVAILABLE");
        settings.crt_shader = 0;
        world_window_set_crt("", 0, 1, NULL, 0);
    }
}

/* Hand the frame draw() just made to the scene renderer. */
static void scene_submit(void)
{
    WorldSceneFrame frame;
    memset(&frame, 0, sizeof frame);
    if (word(ram+0x80)==0xb2e) for (int player=0; player<2; ++player) {
        unsigned actor=word(ram+0x90+2*player);
        if (actor<0x400 || actor>0xff00) continue;
        int x=(int16_t)word(ram+actor+0x21)-96, y=375-(int16_t)word(ram+actor+0x24);
        if (x<frame_left() || x>=frame_left()+(int)frame_w || y<0 || y>=240) continue;
        int facing=(ram[actor+0x29]&1)?-1:1;
        frame.lamp[player][0]=x+17*facing;
        frame.lamp[player][1]=y;
        frame.lamp[player][2]=facing;
    }
    frame.tiles = tiles;
    frame.sprites = drawn_sprites;
    frame.sprite_owners = world_sprite_owners;
    frame.colors = drawn_colors;
    memcpy(frame.layers, drawn_layers, sizeof frame.layers);
    frame.video_control = (uint16_t)word(ram + 0x8b28);
    frame.waterline = waterline_row();
    frame.scene_id = word(ram + 0x80) | word(ram + 0x84) << 16;
    frame.width = word(ram + 0x80) == 0xb2e ? world_view_canvas_width() : WORLD_VIEW_ARCADE_WIDTH;
    frame.left = -world_view_wing_of(frame.width);
    frame.wing_mirror = world_view_wing_mirror();
    /* The camera keeps a fraction of a pixel (byte 51h of its actor) that
       the whole-pixel scroll registers drop. The background scrolls at half
       the playfield's rate, so its fraction follows from the same value. */
    if (word(ram + 0x80) != 0xb2e) world_dispatch_camera_actor = 0;
    if (world_dispatch_camera_actor) {
        unsigned fraction = ram[world_dispatch_camera_actor + 0x51], x = word(ram + 0xb70a);
        frame.frac[1] = (int)fraction;
        if (word(ram + 0xb70e) == x >> 1) frame.frac[2] = (int)(((x & 1) * 256 + fraction) / 2);
    }
    world_scene_submit(&frame);
}

static WorldSceneFx scene_effects(void)
{
    WorldSceneFx fx = {0};
    fx.rays = settings.fx_rays; fx.lights = settings.fx_lights;
    fx.sunshadow = settings.fx_sunshadow; fx.glow = settings.fx_glow; fx.reflect = settings.fx_reflect;
    fx.depth = settings.fx_depth;
    fx.snow = settings.fx_snow; fx.vivid = settings.fx_vivid;
    fx.trails = settings.fx_trails;
    fx.haze = settings.fx_haze;
    fx.shock = settings.fx_shock; fx.particles = settings.fx_particles; fx.shake = settings.fx_shake;
    fx.surface = settings.fx_surface; fx.smooth = settings.fx_smooth; fx.interp = settings.fx_interp;
    fx.wakes = settings.fx_wakes; fx.debris = settings.fx_debris;
    fx.silt = settings.fx_silt; fx.splashes = settings.fx_splashes;
    fx.sparks = settings.fx_sparks; fx.impact_motes = settings.fx_impact_motes;
    fx.pressure = settings.fx_pressure;
    fx.sediment_light = settings.fx_sediment_light;
    fx.water_volume = settings.fx_water_volume;
    fx.ocean_depth = settings.fx_ocean_depth;
    fx.aftermath = settings.fx_aftermath;
    fx.materials = settings.fx_materials;
    fx.headlight_level = settings.headlight_level;
    fx.headlights = settings.fx_headlights;
    fx.rim = settings.fx_rim;
    fx.bubble_light = settings.fx_bubble_light;
    fx.contact_shade = settings.fx_contact_shade;
    fx.blast_reflect = settings.fx_blast_reflect;
    fx.caustics = settings.fx_caustics;
    fx.radiance = settings.fx_radiance;
    fx.auto_quality = settings.auto_quality;
    fx.light_strength = settings.light_strength;
    fx.shadow_strength = settings.shadow_strength;
    fx.depth_strength = settings.depth_strength;
    fx.shaft_strength = settings.shaft_strength;
    fx.rim_strength = settings.rim_strength;
    fx.caustic_strength = settings.caustic_strength;
    return fx;
}

/* The scene is composited at K times 320x240 so that layers and sprites can
 * sit between pixels. K divides the display scale (so every game pixel
 * stays the same size); a CRT shader needs the plain
 * 240-line picture. */
static int scene_scale(void)
{
    if (settings.crt_shader || (settings.fx_enable && (settings.gfx_quality==WORLD_GFX_LOW || settings.fx_sunshadow || settings.fx_headlights))) return 1;
    int scale = world_window_scale();
    /* Two subpixel phases per arcade pixel match the two refreshes per
       board tick on 120 Hz displays. Avoid rasterizing a 4x picture when
       2x tiles divide the output exactly. Odd scales stay pixel-aligned. */
    double hz = world_window_refresh_hz();
    if (scale>=2 && scale%2==0 && hz>0 && hz<=121.0) return 2;
    int cap=settings.fx_enable && settings.gfx_quality==WORLD_GFX_MEDIUM ? 2 : WORLD_SCENE_MAX_K;
    for (int k = cap; k > 1; --k)
        if (scale % k == 0) return k;
    return 1;
}

/* Render the enhanced picture, or copy the plain one. Returns K. */
static int enhance(const uint32_t *source, uint32_t *target, uint32_t time, int k, double alpha)
{
    WorldSceneFx fx = settings.fx_enable ? scene_effects() : (WorldSceneFx){0};
    fx.smooth = 1;
    fx.interp = 0;
    if (world_scene_render(target, (unsigned)k, alpha, &fx, time)) {
        unsigned strength = world_scene_take_rumble();
        if (strength && settings.fx_shake) world_window_rumble(strength, 160);
        return k;
    }
    memcpy(target, source, sizeof(uint32_t) * frame_w * 240);
    return 1;
}

/* Presents a game frame through the enhancement pass, with the optional
 * frame-rate readout in the top right corner. */
/* Corner notices (shader loading, failures), drawn into a 320x240 frame. */
static void overlay_notices(uint32_t *frame, int k)
{
    /* CRT presets compile in the background: say so, and fall back if one fails. */
    char why[256];
    int compiling = world_gpu_visible() ? world_gpu_preset_status(why, sizeof why) : 0;
    if (compiling > 0) show_notice("LOADING CRT SHADER");
    else if (compiling < 0) {
        fprintf(stderr, "CRT shader failed: %s%c", why, 10);
        settings.crt_shader = 0;
        apply_view();
        show_notice("CRT SHADER FAILED");
    } else if (notice_until && !strcmp(notice, "LOADING CRT SHADER")) notice_until = GetTickCount();
    int inset = frame_w > world_view_width() ? (int)(frame_w - world_view_width()) / 2 : 0;
    if (notice[0] && (int)(notice_until - GetTickCount()) > 0) {
        world_menu_draw_text(frame, (unsigned)frame_w * k, 240u * k, (inset + 5) * k, 5 * k, (unsigned)k, 0xff000000, notice);
        world_menu_draw_text(frame, (unsigned)frame_w * k, 240u * k, (inset + 4) * k, 4 * k, (unsigned)k, 0xffff8060, notice);
    } else
        notice[0] = 0;
}

/* The beam simulator's clock, in emulated refreshes: it advances on every
 * displayed refresh (by 1/k when locked to the display, by elapsed time
 * otherwise), in play and in the pause menu alike, and never goes back. */
static double beam_time;
static LONGLONG beam_last;

static void beam_step(void)
{
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    if (beam_last)
        beam_time += (double)(now.QuadPart - beam_last) * WORLD_CLOCK_PIXEL_HZ /
                     ((double)rate.QuadPart * WORLD_CLOCK_FRAME_PIXELS);
    beam_last = now.QuadPart;
    world_window_set_source_time(beam_time);
}

static float displayed_camera_offsets[4];

/* Brightness: a power curve on each channel that lifts (or deepens) dark and
   middle tones while black and white stay fixed. Plane tags in the top byte
   pass through for the GPU lighting. */
static void apply_brightness(uint32_t *picture, size_t count)
{
    static uint32_t curve[3][256];
    static unsigned built;
    if (settings.brightness == 100) return;
    if (built != settings.brightness) {
        double exponent = 100.0 / settings.brightness;
        for (unsigned i = 0; i < 256; ++i) {
            uint32_t v = (uint32_t)lround(255.0 * pow(i / 255.0, exponent));
            curve[0][i] = v << 16; curve[1][i] = v << 8; curve[2][i] = v;
        }
        built = settings.brightness;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t c = picture[i];
        picture[i] = (c & 0xff000000u) | curve[0][(c >> 16) & 255] | curve[1][(c >> 8) & 255] | curve[2][c & 255];
    }
}

/* Developer timing for the pacing log: enhance, overlays, window present. */
static double present_profile_ms[3];
static int present_profile_k;
extern double world_gpu_present_ms[5];

static void present_game(const uint32_t *source)
{
    /* The title menu sits over the running attract sequence, at 1x. */
    int title = menu.open && menu.title && !menu.title_hidden && !menu.peek;
    static WorldLightScene lights;
    int trace=(!title || world_menu_main_title(&menu)) && settings.fx_enable
        && world_gpu_transport_available();
    float offsets[4]={0};
    int pixel_camera=!title && !settings.crt_shader && world_gpu_visible() &&
                     !(frame_w > WORLD_VIEW_ARCADE_WIDTH && world_view_width() == WORLD_VIEW_ARCADE_WIDTH);
    world_scene_camera_output(pixel_camera ? offsets : NULL);
    world_scene_light_ready=trace && !title && settings.gfx_quality==WORLD_GFX_HIGH ? world_gpu_prepare_lights : NULL;
    world_scene_light_target(trace ? &lights : NULL);
    LARGE_INTEGER prof0, prof1, prof2, prof3;
    QueryPerformanceCounter(&prof0);
    int k = shown_k = enhance(source, shown, effect_clock(), title ? 1 : scene_scale(), scene_alpha);
    QueryPerformanceCounter(&prof1);
    world_scene_light_ready=NULL;
    /* Some stages keep their 4:3 aperture inside the widescreen canvas. */
    if (frame_w > WORLD_VIEW_ARCADE_WIDTH && world_view_width() == WORLD_VIEW_ARCADE_WIDTH && !world_view_wing_mirror()) {
        int inset = world_view_wing_of(frame_w)*k, stride = (int)frame_w*k;
        for (int y=0; y<240*k; ++y) {
            uint32_t *row = shown + y*stride;
            for (int x=0; x<inset; ++x) row[x] = row[stride-1-x] = 0xff000000u;
        }
    }
    apply_brightness(shown, (size_t)frame_w * 240 * k * k);
    if (trace && settings.gfx_quality<WORLD_GFX_HIGH) lights.flags |= 1024u;
    if (title && trace && lights.scale == k) {
        lights.flags |= 256u;
        world_menu_gpu_title(&menu, shown, gfx1, GetTickCount(), frame_w);
    } else if (title) world_menu_render(&menu, &settings, shown, gfx1, GetTickCount(), frame_w);
    overlay_notices(shown, k);
    if (settings.show_fps && shown_fps) {
        char label[32];
        snprintf(label, sizeof label, "%u FPS %u%%", shown_fps, shown_speed);
        int inset = frame_w > world_view_width() ? (int)(frame_w - world_view_width()) / 2 : 0;
        int x = (int)frame_w - inset - world_menu_text_width(label, 1) - 4;
        world_menu_draw_text(shown, (unsigned)frame_w * k, 240u * k, (x + 1) * k, 5 * k, (unsigned)k, 0xff000000, label);
        world_menu_draw_text(shown, (unsigned)frame_w * k, 240u * k, x * k, 4 * k, (unsigned)k, 0xffffe070, label);
    }
    world_gpu_light_frame(trace && lights.scale==k ? &lights : NULL);
    memcpy(displayed_camera_offsets,offsets,sizeof offsets);
    world_gpu_camera_offsets(pixel_camera ? offsets : NULL);
    QueryPerformanceCounter(&prof2);
    world_window_present(shown, k, frame_w);
    QueryPerformanceCounter(&prof3);
    {
        LARGE_INTEGER hz; QueryPerformanceFrequency(&hz);
        double ms = 1000.0 / (double)hz.QuadPart;
        present_profile_ms[0] = (double)(prof1.QuadPart - prof0.QuadPart) * ms;
        present_profile_ms[1] = (double)(prof2.QuadPart - prof1.QuadPart) * ms;
        present_profile_ms[2] = (double)(prof3.QuadPart - prof2.QuadPart) * ms;
        present_profile_k = k;
    }
    world_gpu_camera_offsets(NULL);
    world_scene_camera_output(NULL);
    world_gpu_light_frame(NULL);
}

static unsigned launch_fullscreen_override, desktop_fullscreen;
static WorldSettingsStatus save_runtime_settings(void)
{
    WorldSettings saved = settings;
    if (launch_fullscreen_override) saved.fullscreen = desktop_fullscreen;
    return world_settings_save(settings_path, &saved);
}

static unsigned menu_frame(const uint32_t *paused, unsigned held, unsigned toggle, unsigned rising, int gamepad)
{
    if (toggle & 2) held |= WORLD_MENU_BACK;           /* Esc steps back inside the menu */
    unsigned result = 0;
    if ((rising & 1) && !menu.title) {
        menu.open = 0;
        result = WORLD_MENU_RESUME | (menu.dirty ? WORLD_MENU_SAVE : 0);
    } else
        result = world_menu_update(&menu, &settings, held, gamepad, GetTickCount());
    if (result & WORLD_MENU_DISPLAY) {
        launch_fullscreen_override = 0; /* An explicit player choice supersedes launch mode. */
        world_window_set_display(settings.fullscreen, settings.window_scale, world_view_width());
        world_gpu_retry_vrr();          /* a new display mode may support VRR */
    }
    if (result & WORLD_MENU_VIEW) {
        apply_view();
        scene_submit(); /* Refresh immediately for a paused-menu preview. */
    }
    if (result & WORLD_MENU_TIMING) world_clock_request_mode(&board_clock, settings.timing_mode);
    if ((result & WORLD_MENU_SAVE) && save_runtime_settings() != WORLD_SETTINGS_OK)
        fprintf(stderr, "Could not save native settings.\n");
    if ((result & WORLD_MENU_QUIT) || menu.title) return result;
    if (menu.open && menu.peek) {
        scene_alpha = 1.0;
        /* Preview held on the enhancements page: the paused picture alone. */
        present_game(paused);
    } else if (menu.open) {
        memcpy(pixels, paused, sizeof pixels);
        world_menu_render(&menu, &settings, pixels, gfx1, GetTickCount(), frame_w);
        overlay_notices(pixels, 1);
        beam_step();
        world_window_present(pixels, 1, frame_w);
    }
    return result;
}

static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }

static int load(const char *dir, const char *name, void *data, size_t size)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) return 0;
    FILE *f = world_fopen(path, "rb");
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

/* Match positions to the actual sound queue entry, sequencer task and PCM
   key-on. A repeated command from the other player gets its own position. */
typedef struct { float pan, submerged; } SoundOrigin;
static SoundOrigin board_origins[8],command_origins[16],effect_origins[WORLD_AUDIO_STATE_SIZE];
static SoundOrigin selected_origin,voice_origins[4];
static struct {
    float bass[4], tone[4], echo[2048][2];
    unsigned cursor;
} acoustics;
static double stereo_phase;
static int16_t stereo_last[2];

static void sound_created(unsigned task,unsigned slot){effect_origins[task]=command_origins[slot];}
static void sound_selected(unsigned task){selected_origin=effect_origins[task];}

static void locate_sounds(unsigned actor,unsigned tail)
{
    unsigned end=word(ram+0xa6d4)&14;
    if(tail==end)return;
    SoundOrigin origin={0};
    if(word(ram+0x80)==0xb2e){
        int x=(int16_t)word(ram+actor+0x21)-96,y=375-(int16_t)word(ram+actor+0x24);
        /* Listener window: the aperture plus 48 px of margin each side;
           pan normalizes to the aperture half-width, clamped at output. */
        int lo=frame_left()-48, hi=frame_left()+(int)frame_w+48;
        float half=frame_w==WORLD_VIEW_ULTRA_WIDTH?2560.f/9.f:frame_w==WORLD_VIEW_WIDE_WIDTH?640.f/3.f:160.f;
        if(x>=lo && x<hi && y>=-48 && y<288){
            origin.pan=fmaxf(-.72f,fminf(.72f,(x-159.5f)*(.72f/half)));
            int wl=waterline_row();
            origin.submerged=wl==-1?.82f:fmaxf(0,fminf(1,(y-wl+4)/32.f));
        }
    }
    while(tail!=end){tail=(tail+2)&14;board_origins[tail/2]=origin;}
}

static float limit_audio(float x)
{
    float peak=fabsf(x),knee=25500.f;
    if(peak<=knee)return x;
    float excess=peak-knee;
    return copysignf(knee+7000.f*excess/(excess+7000.f),x);
}
static void sound_write(void *context, uint8_t reg, uint8_t value)
{
    if((reg&7)==6 && (value&2))voice_origins[(reg>>3)&3]=selected_origin;
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
    FILE *f = world_fopen(path, "a");
    if (!f) return;
    fprintf(f, "%lu\t%04x\t%02x\t%04x\n", ticks, task, state, cursor);
    fclose(f);
}

static void sound_fm_write(void *context, uint8_t reg, uint8_t value)
{
    (void)context;
    world_ym_write(reg, value);
}

static WorldAudioBus sound_bus = { &pcm, sound_busy, sound_write, sound_reply, sound_unrecovered, sound_fm_write, sound_created, sound_selected };

static void sound_frame_body(void);
/* The mixer's decaying filters and echo tails reach denormal floats, which
   the FPU processes about a hundred times more slowly. Flush them to zero
   (far below 16-bit output precision) for the mixer only. */
static void sound_frame(void)
{
    unsigned csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040u);          /* FTZ | DAZ */
    sound_frame_body();
    _mm_setcsr(csr);
}

static void sound_frame_body(void)
{
    int16_t output[1024][2],voices[1024][4],mono[1024];
    int32_t fm[2048];
    float master=settings.volume*.01f,pcm_gain=settings.pcm_volume*.02f,fm_gain=settings.fm_volume*.01f;
    static unsigned previous_acoustic_scene=~0u;
    unsigned acoustic_scene=(word(ram+0x80)|(word(ram+0x84)<<16))^(settings.spatial_audio<<30);
    if(acoustic_scene!=previous_acoustic_scene){
        memset(&acoustics,0,sizeof acoustics);
        imm_reset(word(ram+0x84),settings.spatial_audio && word(ram+0x80)==0xb2e);
        previous_acoustic_scene=acoustic_scene;
    }
    float listener=0;
    if(word(ram+0x80)==0xb2e){
        int wl=waterline_row();
        unsigned player=word(ram+0x90);
        if(player>=0x400 && player<=0xff00){
            int y=375-(int16_t)word(ram+player+0x24);
            listener=wl==-1?1:fmaxf(0,fminf(1,(y-wl+6)/45.f));
        }
    }
    immersion.listener=listener;
    for(unsigned p=0;p<2;++p){
        unsigned actor=word(ram+0x90+2*p);
        int present=immersion.active && actor>=0x400 && actor<=0xff00;
        int x=present?(int16_t)word(ram+actor+0x21)-96:0;
        int y=present?375-(int16_t)word(ram+actor+0x24):0;
        present=present && x>=frame_left() && x<frame_left()+(int)frame_w && y>=0 && y<240;
        imm_player(p,present,x,y,waterline_row());
    }
    imm_frame();
    /* One board frame has 424*262 pixel clocks at 6,666,666 Hz. */
    sound_frame_phase += (uint64_t)48000 * 424 * 262;
    unsigned frames = (unsigned)(sound_frame_phase / 6666666);
    sound_frame_phase %= 6666666;
    unsigned done = 0;
    const uint64_t timer_period = (uint64_t)48000 * 57344;
    while (done < frames) {
        unsigned count = (unsigned)((timer_period - sound_timer_phase + 14318180) / 14318181);
        if (count > frames - done) count = frames - done;
        if(settings.spatial_audio)world_ga20_render_voices(&pcm,voices,count);
        else world_ga20_render(&pcm,mono,count);
        if(settings.spatial_audio)world_ym_render_stereo(fm,count);
        else world_ym_render(fm,count);
        for(unsigned i=0;i<count;++i){
            if(!settings.spatial_audio){
                float sum=pcm_gain*mono[i]+fm_gain*fm[i];
                output[done+i][0]=output[done+i][1]=(int16_t)fmaxf(-32768,fminf(32767,master*sum));
                continue;
            }
            float direct[2]={0},send[2]={0};
            for(int v=0;v<4;++v){
                float x=voices[i][v],wet=voice_origins[v].submerged;
                float pan=voice_origins[v].pan;
                /* Parallel 110 Hz shelf, gentle HF absorption, dry transient.
                   Bass stays near centre for speakers and mono downmix. */
                acoustics.bass[v]+=.0143f*(x-acoustics.bass[v]);
                acoustics.tone[v]+=.38f*(x-acoustics.tone[v]);
                float body=acoustics.bass[v]*wet*(.32f+.38f*listener)*(settings.reduced_bass?.25f:1.f);
                float absorption=wet*(.18f+.20f*listener)+(1-wet)*listener*.65f;
                float tone=x+absorption*(acoustics.tone[v]-x);
                direct[0]+=tone*(1-pan)+body;
                direct[1]+=tone*(1+pan)+body;
                float reflection=acoustics.tone[v]*wet*listener;
                send[0]+=reflection*(1-pan);
                send[1]+=reflection*(1+pan);
            }
            float added[2];
            imm_sample(added,settings.ambience_volume*.01f,settings.pcm_volume*.01f,settings.reduced_bass);
            unsigned pos=acoustics.cursor;
            for(int ch=0;ch<2;++ch){
                unsigned stage=word(ram+0x84);
                float space=stage==4?1.35f:stage==2?1.15f:.65f;
                float reflection=space*(.075f*acoustics.echo[(pos-733)&2047][ch]+.043f*acoustics.echo[(pos-1301)&2047][1-ch]);
                float music_duck=1-.10f*immersion.pressure;
                float mixed=fm_gain*fm[2*i+ch]*music_duck+pcm_gain*(direct[ch]+reflection)+added[ch];
                output[done+i][ch]=(int16_t)limit_audio(master*mixed);
                acoustics.echo[pos][ch]=send[ch];
            }
            acoustics.cursor=(pos+1)&2047;
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

    const double target = 2400.0;
    /* After a stall the 0.5 percent controller would take about a minute to
       drain a full FIFO; drop the backlog instead of carrying the latency. */
    if (world_audio_output_level() > 3 * (unsigned)target) world_audio_output_trim((unsigned)target);
    double level = (double)world_audio_output_level();
    double error = (level - target) / target;
    if (error > 1.0) error = 1.0;
    if (error < -1.0) error = -1.0;
    double step = 1.0 + 0.005 * error;      /* input samples per output sample */
    int16_t stretched[1100][2];
    unsigned produced = 0;
    while (produced < sizeof stretched / sizeof stretched[0]) {
        unsigned index = (unsigned)stereo_phase;
        if (index >= frames) break;
        double t = stereo_phase - index;
        for(int ch=0;ch<2;++ch){
            int a=index?output[index-1][ch]:stereo_last[ch],b=output[index][ch];
            stretched[produced][ch]=(int16_t)(a+(b-a)*t);
        }
        ++produced;
        stereo_phase+=step;
    }
    stereo_phase-=frames;
    if(frames)memcpy(stereo_last,output[frames-1],sizeof stereo_last);
    if (!world_audio_output_submit(&stretched[0][0], produced)) {
        fprintf(stderr, "Windows audio output stopped.\n");
        world_audio_output_close();
        sound_device = 0;
    }
}

static void capture_video(void)
{
    world_shadow_end_pass(ram, tiles);
    { extern void world_shadow_debug(const uint8_t *, unsigned long); world_shadow_debug(ram, ticks); }
    frame_w = word(ram + 0x80) == 0xb2e ? world_view_canvas_width() : WORLD_VIEW_ARCADE_WIDTH;
    shot_w = frame_w;
    /* Wings with no authored picture: the arcade-width shaft (stage 5) always,
       and a locked stage-4 arena once the camera has held still. */
    {
        static unsigned held_b700, held_frames;
        unsigned b700 = word(ram + 0xb700);
        held_frames = b700 == held_b700 ? held_frames + 1 : 0;
        held_b700 = b700;
        int mirror = 0;
        if (word(ram + 0x80) == 0xb2e && frame_w > WORLD_VIEW_ARCADE_WIDTH) {
            if (word(ram + 0x84) == 4) mirror = 2;
            else if (word(ram + 0x84) == 3 && held_frames >= 20 && b700 >= 0x500) mirror = 1; /* the final arena, not earlier holds */
            /* Stage-one boss cavern: as the opening's sea-scenery edge scrolls out
               through the left wing, the arcade never shows that edge; reflect the
               picture's own cavern wall over it until the real cavern fills the wing. */
            else if (word(ram + 0x84) == 0 && b700 >= 0x1600 && word(ram + 0xb70a) >= 0x7fc && word(ram + 0xb70a) <= 0x834) mirror = 3; /* left wing only */
        }
        world_view_set_wing_mirror(mirror);
        world_tile_shadow_static = held_frames >= 20;
    }
    WorldVideoLayer layers[3];
    for (unsigned i = 0; i < 3; ++i) {
        unsigned pos = 0xb706 + 4*i;
        layers[i].master = layer_master[i];
        layers[i].scroll_y = word(ram + pos + 2);
        layers[i].scroll_x = word(ram + pos) + 16 + 2*i;
    }
    uint8_t *sprites = drawn_sprites;
    memset(drawn_sprites, 0, sizeof drawn_sprites);
    for (unsigned i = 0, count = (0u - ram[0x8b26]) & 255; i < count; ) {
        memcpy(sprites + i*8, oam + i*8, 8);
        i += 1u << ((oam[i*8+1] >> 3) & 3);
    }
    uint32_t *colors = drawn_colors;
    for (unsigned i = 0; i < 1024; ++i) colors[i] = world_video_rgb(word(palette + 2*i));
    memcpy(drawn_layers, layers, sizeof drawn_layers);
}

static void draw_captured_pixels(void)
{
    unsigned width = frame_w < world_view_width() || world_view_wing_mirror() ? frame_w : world_view_width();
    unsigned inset = (frame_w - width) / 2;
    int left = -world_view_wing_of(width);
    for (unsigned y = 0; y < 240; ++y) {
        uint16_t line[WORLD_VIEW_MAX_WIDTH];
        world_video_scanline(line, y, tiles, gfx1, gfx2, drawn_sprites, drawn_layers,
                             word(ram + 0x8b28), width, left);
        uint32_t *row = pixels + y * frame_w;
        for (unsigned x = 0; x < inset; ++x)
            row[x] = row[frame_w - 1 - x] = 0xff000000u;
        for (unsigned x = 0; x < width; ++x) row[inset + x] = drawn_colors[line[x] & 1023];
    }
}

static void draw(void)
{
    capture_video();
    draw_captured_pixels();
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
static Stage6Timing stage6_timing;

/* Extra held refreshes per 50,000 estimated cycles above the quiet-scene
 * floor, in thousandths. See docs/pcb-slowdown.md for the capture and fit. */
static unsigned pcb_gameplay_hold(unsigned stage, unsigned scroll, uint32_t cycles,
                                  unsigned muzzle_bombs, unsigned volley, uint64_t pass)
{
    /* The final stage repeats its scenery through several attack phases.
     * Its identifiable barrages below predict holds better than scroll. */
    static const unsigned slope[] = {65, 185, 65, 70, 85, 0};
    if (stage >= sizeof slope / sizeof slope[0]) return 0;
    unsigned gain = slope[stage];
    if (stage == 2 && scroll >= 2048 && scroll < 3072) gain = 245;
    if (stage == 4 && scroll >= 512 && scroll < 1024) gain = 335;

    unsigned rate = cycles > 25000 ? (cycles - 25000) * gain / 50000 : 0;
    if (stage == 5 && rate < muzzle_bombs * 125) rate = muzzle_bombs * 125;
    if (stage == 5 && volley > 2) {
        unsigned volley_rate = volley > 10 ? 850 : (volley - 2) * 100;
        if (rate < volley_rate) rate = volley_rate;
    }
    /* Spread fractional holds across logic passes. Taking phase from the
     * saved board clock also makes a restored pass choose the same hold. */
    uint32_t phase = (uint32_t)pass * UINT32_C(2654435769);
    return (phase >> 16) * 1000u < (uint64_t)rate * 65536u;
}

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

/* Observe the same successful actor pass that submitted the arcade sprites.
   Hold muzzle pulses at their launch point as the camera moves. Rebuild
   moving sources every pass so destroyed rounds leave no light behind. */
static WeaponEmitter weapon_lights[MAX_WEAPON_EMITTERS];
static unsigned weapon_lights_count, weapon_scene;
static int weapon_scroll_x,weapon_scroll_y;
/* Key flight provenance by the board actor address and expected dispatch.
   A recycled slot cannot inherit the light of the previous round. */
static struct { unsigned expected,pass; WeaponEmission kind; } flight[65536];
static unsigned weapon_pass;
static void begin_weapon_pass(void)
{
    unsigned scene=word(ram+0x80) | ((unsigned)word(ram+0x84)<<16);
    int x=word(ram+0xb70a),y=word(ram+0xb70c);
    int dx=(int16_t)(x-weapon_scroll_x),dy=(int16_t)(y-weapon_scroll_y);
    unsigned keep=0;
    ++weapon_pass;
    if(scene!=weapon_scene)memset(flight,0,sizeof flight);
    if(scene==weapon_scene && abs(dx)<=32 && abs(dy)<=32){
        for(unsigned i=0;i<weapon_lights_count;++i){
            WeaponEmitter e=weapon_lights[i];
            if(e.kind>=ROCKET_PLUME || !e.level)continue;
            ++e.age;e.x-=dx;e.y-=dy;
            weapon_lights[keep++]=e;
        }
    }
    weapon_lights_count=keep;weapon_scene=scene;weapon_scroll_x=x;weapon_scroll_y=y;
}

static int weapon_position(unsigned address)
{
    return (word(ram+address+1)<<8)|ram[address];
}

/* The renderer can temporarily move a part to draw it. Anchor to the
   submitted sprite and check the owner's final state after all actors run. */
static struct { uint16_t actor; WeaponEmitter light; } boss_lamps[MAX_WEAPON_EMITTERS];
static unsigned boss_lamp_count;

static void observe_boss(unsigned actor,unsigned sprite)
{
    if(sprite==word(ram+0x9e04) || boss_lamp_count==MAX_WEAPON_EMITTERS)return;
    unsigned state=(word(ram+actor)+16u*word(ram+actor+2))&0xfffff;
    BossLamp lamp=boss_lamp(state);
    if(!lamp.kind)return;
    int x=(int16_t)word(ram+sprite+4)-96+((ram[sprite+9]&1)?-lamp.x:lamp.x);
    int y=375-(int16_t)word(ram+sprite+6)+((ram[sprite+9]&2)?-lamp.y:lamp.y);
    if(x < frame_left()-64 || x >= frame_left()+(int)frame_w+64 || y < -64 || y >= 304)return;
    boss_lamps[boss_lamp_count].actor=actor;
    boss_lamps[boss_lamp_count++].light=(WeaponEmitter){x,y,0,0,lamp.kind,0,0,0};
}

static void observe_weapon(unsigned entry,unsigned actor,int old_x,int old_y)
{
    unsigned next=(word(ram+actor)+16u*word(ram+actor+2))&0xfffff;
    if(next==0x1b4c){flight[actor].kind=WEAPON_DARK;return;}
    WeaponEmission pulse=weapon_launch(entry,next);
    WeaponEmission moving=weapon_flight(entry);
    if(!moving)moving=round_from_launch(pulse);
    if(!moving && flight[actor].expected==entry && flight[actor].pass==weapon_pass-1)
        moving=flight[actor].kind;
    if(!moving && !pulse)return;
    flight[actor].kind=moving;flight[actor].expected=next;flight[actor].pass=weapon_pass;
    WeaponEmission kind=pulse?pulse:moving;
    int x=(int16_t)word(ram+actor+0x21)-96,y=375-(int16_t)word(ram+actor+0x24);
    if(x<frame_left() || x>=frame_left()+(int)frame_w || y<0 || y>=240)return;
    int vx=(int16_t)word(ram+actor+0x38),vy=-(int16_t)word(ram+actor+0x3a);
    int moved_x=weapon_position(actor+0x20)-old_x;
    int moved_y=old_y-weapon_position(actor+0x23);
    if(!pulse && (moved_x || moved_y) && abs(moved_x)<32*256 && abs(moved_y)<32*256){
        vx=moved_x;vy=moved_y;
    }
    if(!vx && !vy){
        if(kind==RACK_PULSE)vy=1;
        else if(entry==0x9fa1 || entry==0x9e73 || entry==0xbb40 || entry==0x1d0be)vy=-1;
        else vx=(ram[actor+0x29]&1)?-1:1;
    }
    float mag=fmaxf(abs(vx),abs(vy));
    int dx=(int)lroundf(64*vx/mag),dy=(int)lroundf(64*vy/mag);
    WeaponEmitter shot={x,y,dx,dy,kind,0,0,0};
    if(pulse)weapon_pulse(weapon_lights,&weapon_lights_count,shot);
    else if(weapon_lights_count<MAX_WEAPON_EMITTERS)weapon_lights[weapon_lights_count++]=shot;
}

static int tick(uint16_t pads, uint16_t system)
{
    unsigned muzzle_bombs = 0, volley = 0;
    unsigned approach = 0, approach_enemies = 0;
    unsigned board_tick_before = word(ram + 0x276);
    world_clock_begin_pass(&board_clock);
    if (ram[0x262] & 1) {
        begin_weapon_pass();
        boss_lamp_count=0;
        WorldDispatch pass;
        uint32_t addr;
        world_dispatch_begin(&pass, ram);
        world_terrain_mirror = world_view_wing_mirror() == 1 || world_view_wing_mirror() == 2;
        world_terrain_cam = word(ram + 0xb70a);
        world_shadow_begin_pass(ram, tiles, layer_master);
        unsigned calls = 0;
        while (world_dispatch_target(&pass, ram, &addr)) {
            world_clock_note_unrecovered(&board_clock);
            slowdown_estimate += slowdown_routine(addr);
            if (addr == 0x15d98 && word(ram + 0x84) == 5) {
                unsigned table = word(rom + 0x15e4c);
                unsigned row = (uint16_t)(table + word(ram + pass.actor + 0x40));
                approach = word(rom + 0x10000 + row) != 0xffff;
            }
            /* The surface barrage continues while the hull explodes and sinks. */
            if (addr == 0xb5824 || addr == 0xb595d || addr == 0xb5975 || addr == 0xb59b3)
                approach_enemies |= APPROACH_WARSHIP;
            if (addr == 0xb1399 || addr == 0xb14bd) approach_enemies |= APPROACH_GUNSHIP;
            /* These consecutive cases in final_boss cover one torpedo
             * from launch through its turn. Count the ones on screen. */
            if (addr >= 0x7be50 && addr <= 0x7bfd2) {
                int x = (int)word(ram + pass.actor + 0x21), y = word(ram + pass.actor + 0x24);
                if (x >= 96 + frame_left() && x < 96 + frame_left() + (int)frame_w && y >= 136 && y <= 375) ++volley;
            }
            if (addr == 0x7c018 || addr == 0x7c05b || addr == 0x7c0c2) ++muzzle_bombs;
            ++slowdown_actors;
            if (++calls > 65536) {
                snprintf(failure, sizeof failure, "Actor list did not terminate at master %04X.", word(ram + 0x80));
                return 0;
            }
            uint16_t rendered_actor=pass.actor,sprite=word(ram+0x9e04);
            unsigned sound_tail=word(ram+0xa6d4)&14;
            int old_x=weapon_position(rendered_actor+0x20),old_y=weapon_position(rendered_actor+0x23);
            world_shadow_actor(ram, tiles, pass.actor, addr);
            { extern void world_shadow_before_handler(const uint8_t *); world_shadow_before_handler(tiles); }
            if (!world_dispatch_native_video(&pass, ram, rom, tiles)) {
                snprintf(failure, sizeof failure, "Unrecovered World handler %05lX, actor %04X, master %04X.%s%s",
                         (unsigned long)addr, pass.actor, word(ram + 0x80),
                         *world_dispatch_detail ? " " : "", world_dispatch_detail);
                return 0;
            }
            { extern void world_shadow_after_handler(const uint8_t *, const uint8_t *, uint16_t, uint32_t); world_shadow_after_handler(ram, tiles, rendered_actor, addr); }
            unsigned sprite_end=word(ram+0x9e04);
            if(sprite_end!=sprite){
                int radial=addr==0x6f84 || addr==0x6fb1;
                int detonation=addr==0xa1bf || addr==0x1d2ea || addr==0xad81;
                world_frame_tag_blast(sprite,sprite_end,detonation?2:radial);
            }
            locate_sounds(rendered_actor,sound_tail);
            observe_weapon(addr,rendered_actor,old_x,old_y);
            observe_boss(rendered_actor,sprite);
            if(settings.spatial_audio){
                unsigned state=(word(ram+rendered_actor)+16u*word(ram+rendered_actor+2))&0xfffff;
                imm_actor(rendered_actor,addr,state,
                    (int16_t)word(ram+rendered_actor+0x21)-96,
                    375-(int16_t)word(ram+rendered_actor+0x24),waterline_row(),boss_lamp(state).kind);
            }
        }
        world_shadow_end_pass(ram, tiles);
        world_stage_one_far_mountains(ram, tiles);
        weapon_envelopes(weapon_lights,weapon_lights_count);
        for(unsigned i=0;i<boss_lamp_count && weapon_lights_count<MAX_WEAPON_EMITTERS;++i){
            unsigned actor=boss_lamps[i].actor;
            unsigned state=(word(ram+actor)+16u*word(ram+actor+2))&0xfffff;
            if(boss_lamp(state).kind==boss_lamps[i].light.kind)
                weapon_lights[weapon_lights_count++]=boss_lamps[i].light;
        }
    }
    world_scene_weapons(weapon_lights,weapon_lights_count);
    slowdown_estimate += slowdown_palette();
    world_palette_step(ram, palette, rom);
    int sound, master_sound;
    unsigned master = word(ram + 0x80);
    world_clock_note_unrecovered(&board_clock);
    for (unsigned i = 0; i < 3; ++i)
        if (ram[0x29a] & (1u << i)) layer_master[i] = word(ram + 0x29c + 2*i);
    if (master == 0xa0e || master == 0x24e2 || master == 0x344d || master == 0x333d ||
        master == 0xc466 || master == 0xc66e || (master == 0xc3e && word(ram + 0x86) == 1))       /* OUT 98/9A/9C = 0, 1, 2 */
        for (unsigned i = 0; i < 3; ++i) layer_master[i] = (uint16_t)i;
    if (master == 0xa09 || master == 0x1d52)
        memcpy(layer_master, title_master_controls, sizeof layer_master);
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
    int approach_hold = -1;
    if (master == 0xb2e && approach && word(ram + 0xb700) < 4064) {
        if (word(ram + 0xb8ac)) approach_enemies |= APPROACH_MISSILES;
        approach_hold = stage6_approach_step(&stage6_timing, word(ram + 0xb700), approach_enemies, board_tick_before);
    }
    if (board_clock.pass_mode == WORLD_TIMING_ARCADE && busy_system < 0) {
        vblanks = 1 + slowdown_estimate / SLOWDOWN_FRAME_CYCLES;
        if (approach_hold >= 0)
            vblanks = 1u + (unsigned)approach_hold;
        else if (vblanks == 1 && master == 0xb2e && slowdown_actors)
            vblanks += pcb_gameplay_hold(word(ram + 0x84), word(ram + 0xb700),
                                        slowdown_estimate, muzzle_bombs, volley, board_clock.passes);
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
    unsigned sound_head=word(ram+0xa6d2)&14;
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
    if(sound>=0){
        unsigned head=word(ram+0xa6d2)&14;
        command_origins[sound_state[0x8d0]&15]=head!=sound_head?board_origins[head/2]:(SoundOrigin){0};
        if(head!=sound_head)board_origins[head/2]=(SoundOrigin){0};
        world_audio_receive(sound_state,(uint8_t)sound);
    }
    if(master_sound>=0){
        command_origins[sound_state[0x8d0]&15]=(SoundOrigin){0};
        world_audio_receive(sound_state,(uint8_t)master_sound);
    }
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
    FILE *f = world_fopen(path, "a");
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
static int shot_k = 1;                 /* shot_source is shot_k times shot_w x 240 */

static void headless_shot(const char *dir, const char *name)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/headless-%s.bmp", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = world_fopen(path, "wb");
    if (!f) return;
    BITMAPFILEHEADER file;
    BITMAPINFOHEADER info;
    memset(&file, 0, sizeof file);
    memset(&info, 0, sizeof info);
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof file + sizeof info;
    size_t shot_bytes = (size_t)shot_w * 240 * sizeof(uint32_t) * (size_t)shot_k * (size_t)shot_k;
    file.bfSize = (DWORD)(file.bfOffBits + shot_bytes);
    info.biSize = sizeof info;
    info.biWidth = (LONG)(shot_w * shot_k);
    info.biHeight = -240 * shot_k;
    info.biPlanes = 1;
    info.biBitCount = 32;
    fwrite(&file, sizeof file, 1, f);
    fwrite(&info, sizeof info, 1, f);
    fwrite(shot_source, shot_bytes, 1, f);
    fclose(f);
}

/* Test-only autopilot for scripted runs: it reads work RAM to choose the
   held inputs and changes no game state. While the stage-three end boss body
   (1000:45BA..4C19) is in the actor list, player one moves under it, keeps
   to the surface and alternates both fire buttons; otherwise `fallback`. */
static unsigned headless_boss_state; /* diagnostic witness, never game state */

static unsigned headless_autopilot(unsigned fallback, unsigned *edges)
{
    static const unsigned boss_states[] = {0x45ba, 0x4614, 0x4640, 0x465a, 0x46a5, 0x46e4, 0x47cb,
                                           0x4811, 0x4837, 0x48a4, 0x49ab, 0x49ce, 0x4c19};
    /* Outside the 0xb2e gameplay master emit single-frame coin and start
       edges so scripted runs survive continue, game over and title on their
       own. nonplay resets the same tick the scene returns to gameplay, so a
       start edge can never land on a 0xb2e frame and latch a pause. */
    static unsigned nonplay;
    /* WORLD_AUTOPILOT_P2=1 (test-only): drive a player-two-only session â€”
       start edge 0x02 and sub tracking via ram[0x92] instead of 0x90. */
    static int p2_only = -1;
    if (p2_only < 0) p2_only = getenv("WORLD_AUTOPILOT_P2") ? 1 : 0;
    /* Truly dead sub states (blast + respawn wait) still need edges for the
       continue prompt; 7C3B is a live entry hold that wants movement. */
    unsigned p1 = word(ram + 0x90);
    unsigned p1_state = p1 ? word(ram + p1) : 0;
    int parked = p1_state == 0x7eb1 || p1_state == 0x7ee5;
    if (word(ram + 0x80) != 0xb2e || (ram[0x28c] & 1) || parked) {
        ++nonplay;
        /* Coin chutes debounce over vblanks: service (system bit 4) needs
           two consecutive held samples ((0x2bb&15)==3), chutes the 0101
           pattern â€” single-frame edges can never produce either, so hold
           service for two ticks, then issue a start edge. */
        unsigned m = nonplay % 120;
        *edges = (m == 60 || m == 61) ? 0x10 : m == 90 ? (p2_only ? 0x02 : 0x01) : 0;
        return m == 96 ? 0x40 : 0;   /* pad fire edge: 0x2218 briefing needs
                                        B742&0xF0, not a system bit */
    }
    nonplay = 0;
    *edges = 0;
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
    headless_boss_state = boss ? word(ram + boss) : 0;
    unsigned sub = word(ram + (p2_only ? 0x92 : 0x90));
    unsigned phase = (unsigned)(ticks % 12);
    unsigned fire = phase < 4 ? 0x40 : (phase >= 6 && phase < 10) ? 0x80 : 0;
    if (!boss) {
        /* Test-only: steer toward live pickup actors (7000:EA17 init and its
           7EA62/7EB1C update states) so empirical collection events get
           captured; keeps firing while chasing. */
        unsigned pickup = 0;
        actor = 0x400;
        for (unsigned steps = 0; steps < 512 && actor; ++steps) {
            unsigned state = word(ram + actor);
            if (state == 0xea17 || state == 0xea62 || state == 0xeb1c) pickup = actor;
            unsigned next = word(ram + actor + 4);
            if (next == actor || next == 0x400) break;
            actor = next;
        }
        if (pickup) {
            int px = (int16_t)(word(ram + pickup + 0x21) - word(ram + sub + 0x21));
            int py = (int16_t)(word(ram + pickup + 0x24) - word(ram + sub + 0x24));
            unsigned held = fire;
            if (px > 6) held |= 0x01;
            else if (px < -6) held |= 0x02;
            if (py > 6) held |= 0x08;
            else if (py < -6) held |= 0x04;
            return held;
        }
        /* WORLD_AUTOPILOT_DODGE=1 (test-only): evade any actor with a nonzero
           +0x0d contact flag inside a danger box around the sub â€” covers both
           enemy projectiles and collision damage without classifying types. */
        static int dodge = -1;
        if (dodge < 0) dodge = getenv("WORLD_AUTOPILOT_DODGE") ? 1 : 0;
        if (dodge) {
            int ex = 0, ey = 0, threats = 0;
            actor = 0x400;
            for (unsigned steps = 0; steps < 512 && actor; ++steps) {
                if (actor != sub && actor != word(ram + 0x92) && ram[actor + 0x0d]) {
                    int dx = (int16_t)(word(ram + actor + 0x21) - word(ram + sub + 0x21));
                    int dy = (int16_t)(word(ram + actor + 0x24) - word(ram + sub + 0x24));
                    if (dx > -0x30 && dx < 0x30 && dy > -0x30 && dy < 0x30) { ex += dx; ey += dy; ++threats; }
                }
                unsigned next = word(ram + actor + 4);
                if (next == actor || next == 0x400) break;
                actor = next;
            }
            if (threats) {
                unsigned held = fire;
                if (ex > 2) held |= 0x02;       /* threats right: move left */
                else if (ex < -2) held |= 0x01;
                else held |= 0x01;
                if (ey > 2) held |= 0x04;
                else if (ey < -2) held |= 0x08;
                return held;
            }
        }
        /* Test-only: when the stage-one boss's B5C73 kill-gate parts are up,
           park so the up-missile spawn (sub_x+8 facing left) lands on the
           part's line and hold the 0x40 weapon group. */
        unsigned gate = 0;
        actor = 0x400;
        for (unsigned steps = 0; steps < 512 && actor; ++steps) {
            if (word(ram + actor + 2) == 0xb000 && word(ram + actor) == 0x5c73) gate = actor;
            unsigned next = word(ram + actor + 4);
            if (next == actor || next == 0x400) break;
            actor = next;
        }
        if (gate) {
            int aim = (int)word(ram + gate + 0x21) - 8 - (int)word(ram + sub + 0x21);
            unsigned held = 0x40;
            if (aim > 6) held |= 0x01;
            else if (aim < -6) held |= 0x02;
            else held |= 0x02;                  /* hold facing-left spawn line */
            return held;
        }
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
        /* Unguided play (stage intros, camera-locked arenas): hold right so
           the sub clears entry gates that park a drifting sub against solid
           terrain, keep firing into the scroll path, and weave vertically so
           arena fights don't eat every hit at one depth. */
        if (!camera) return fallback | 0x01 | fire | ((ticks / 70) & 1 ? 0x04 : 0x08);
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

static int restore_snapshot(const char *load_path)
{
        world_stage_reset_prespawn();
        FILE *f = world_fopen(load_path, "rb");
        int failed = !f || fread(ram, sizeof ram, 1, f) != 1 || fread(tiles, sizeof tiles, 1, f) != 1 ||
            fread(oam, sizeof oam, 1, f) != 1 || fread(palette, sizeof palette, 1, f) != 1 ||
            fread(layer_master, sizeof layer_master, 1, f) != 1 || fread(&ticks, sizeof ticks, 1, f) != 1 ||
            fread(&board_clock, sizeof board_clock, 1, f) != 1 || fread(sound_state, sizeof sound_state, 1, f) != 1;
        imm_reset(word(ram+0x84),settings.spatial_audio && word(ram+0x80)==0xb2e);
        world_shadow_reset();
        memset(&acoustics,0,sizeof acoustics);
        memset(&stage6_timing, 0, sizeof stage6_timing);
        if (!failed) {
            /* Earlier diagnostic saves ended immediately after sound_state. */
            int next = fgetc(f);
            if (next == EOF && !ferror(f)) {
                stage6_legacy_restore(&stage6_timing, word(ram + 0xb700));
            } else {
                ungetc(next, f);
                failed = fread(&stage6_timing, sizeof stage6_timing, 1, f) != 1;
            }
            world_view_set_stage(word(ram + 0x84));
            world_stage_three_boss_wall(ram, tiles);
            memcpy(slowdown_shadow, tiles, sizeof slowdown_shadow);
        }
        if(f)fclose(f);
        return !failed;
}

static void headless_run(const char *dir, const char *script_path, int timing_override)
{
    /* Binary mode keeps ftell/fseek byte-exact on any line ending. */
    FILE *script = world_fopen(script_path, "rb");
    if (!script) {
        snprintf(failure, sizeof failure, "Cannot open the headless input script.");
        return;
    }
    FILE *scene_stats = NULL;
    unsigned held = 0, buttons = 0, autopilot = 0;
    unsigned long at = 0;
    char first[32], second[32], third[32];
    int fields = fscanf(script, "%lu %31s", &at, first);
    char trace_path[4096];
    snprintf(trace_path, sizeof trace_path, "%s/headless-trace.txt", dir);
    FILE *trace = world_fopen(trace_path, "w");
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
        if(!restore_snapshot(load_path)){
            snprintf(failure,sizeof failure,"Cannot load snapshot.");
            if(trace)fclose(trace);
            fclose(script);
            return;
        }
    }
    if (load_path && timing_override) world_clock_request_mode(&board_clock, settings.timing_mode);
    /* WORLD_SCRIPT_RELATIVE=1: script frames count from the loaded snapshot. */
    unsigned long script_base = getenv("WORLD_SCRIPT_RELATIVE") ? ticks : 0;
    at += script_base;
    /* WORLD_SNAPSHOT_STAGES=dir: a snapshot each time the stage changes. */
    const char *stage_dir = getenv("WORLD_SNAPSHOT_STAGES");
    unsigned last_stage = 0xffff, stage_count = 0;
    for (;;) {
        /* WORLD_SNAPSHOT_EVERY=n,dir (test-only): a snapshot every n ticks of
           live play, named by tick. */
        static unsigned long every_n, every_next;
        static const char *every_dir;
        if (!every_dir && getenv("WORLD_SNAPSHOT_EVERY")) {
            const char *e = getenv("WORLD_SNAPSHOT_EVERY");
            every_n = strtoul(e, NULL, 10);
            every_dir = strchr(e, ',') ? strchr(e, ',') + 1 : "";
        }
        int every_due = every_n && *every_dir && word(ram + 0x80) == 0xb2e && ticks >= every_next;
        if (every_due) every_next = ticks + every_n;
        if ((stage_dir && word(ram + 0x80) == 0xb2e && word(ram + 0x84) != last_stage) || every_due) {
            char path[4096];
            if (every_due) snprintf(path, sizeof path, "%s/t%06lu.snap", every_dir, ticks);
            else {
                last_stage = word(ram + 0x84);
                snprintf(path, sizeof path, "%s/stage%02u-%u.snap", stage_dir, stage_count++, last_stage);
            }
            FILE *f = world_fopen(path, "wb");
            if (f) {
                fwrite(ram, sizeof ram, 1, f);
                fwrite(tiles, sizeof tiles, 1, f);
                fwrite(oam, sizeof oam, 1, f);
                fwrite(palette, sizeof palette, 1, f);
                fwrite(layer_master, sizeof layer_master, 1, f);
                fwrite(&ticks, sizeof ticks, 1, f);
                fwrite(&board_clock, sizeof board_clock, 1, f);
                fwrite(sound_state, sizeof sound_state, 1, f);
                fwrite(&stage6_timing, sizeof stage6_timing, 1, f);
                fclose(f);
            }
        }
        /* WORLD_MENU_SHOT=frame renders every menu page over that frame. */
        static int menu_shot_done;
        const char *menu_shot = getenv("WORLD_MENU_SHOT");
        if (menu_shot && !menu_shot_done && ticks >= strtoul(menu_shot, NULL, 10)) {
            menu_shot_done = 1;
            static uint32_t still[WORLD_VIEW_MAX_WIDTH * 240];
            memcpy(still, pixels, sizeof still);
            static const unsigned pages[][4] = {{0,0,0,0},{1,0,0,1},{1,0,1,3},{1,1,1,1},{1,2,1,1},{2,0,0,0},{3,0,0,0},
                                                {1,0,6,0},{1,2,3,0},{1,3,1,0},{1,3,9,0},{1,0,8,0},{1,3,4,0}};
            for (unsigned i = 0; i < sizeof pages / sizeof pages[0]; ++i) {
                world_menu_open(&menu, 0, i & 1, 0);
                menu.page = pages[i][0];
                menu.tab = pages[i][1];
                menu.row = pages[i][2];
                memcpy(pixels, still, sizeof still);
                world_menu_render(&menu, &settings, pixels, gfx1, 1500 + 400 * i, frame_w);
                char name[32];
                snprintf(name, sizeof name, "menu%u", i);
                headless_shot(dir, name);
            }
            static const unsigned title_pages[][2] = {{0, 0}, {0, 2}, {3, 1}};
            for (unsigned i = 0; i < sizeof title_pages / sizeof title_pages[0]; ++i) {
                world_menu_open_title(&menu, 0, i & 1, 0);
                menu.page = title_pages[i][0];
                menu.row = title_pages[i][1];
                memcpy(pixels, still, sizeof still);
                world_menu_render(&menu, &settings, pixels, gfx1, 1500 + 400 * i, frame_w);
                char name[32];
                snprintf(name, sizeof name, "title%u", i);
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
            static const struct { const char *name; size_t offset; } effects[] = {
                {"rays", offsetof(WorldSettings, fx_rays)},
                {"lights", offsetof(WorldSettings, fx_lights)},
                {"depth", offsetof(WorldSettings, fx_depth)},
                {"snow", offsetof(WorldSettings, fx_snow)}, {"vivid", offsetof(WorldSettings, fx_vivid)},
                {"haze", offsetof(WorldSettings, fx_haze)},
                {"shock", offsetof(WorldSettings, fx_shock)}, {"particles", offsetof(WorldSettings, fx_particles)},
                {"surface", offsetof(WorldSettings, fx_surface)}, {"sunshadow", offsetof(WorldSettings, fx_sunshadow)},
                {"glow", offsetof(WorldSettings, fx_glow)}, {"reflect", offsetof(WorldSettings, fx_reflect)},
                {"wakes", offsetof(WorldSettings, fx_wakes)},
                {"debris", offsetof(WorldSettings, fx_debris)},
                {"silt", offsetof(WorldSettings, fx_silt)},
                {"splashes", offsetof(WorldSettings, fx_splashes)},
                {"sparks", offsetof(WorldSettings, fx_sparks)},
                {"impact-motes", offsetof(WorldSettings, fx_impact_motes)},
                {"pressure", offsetof(WorldSettings, fx_pressure)},
                {"sediment_light", offsetof(WorldSettings, fx_sediment_light)},
                {"water_volume", offsetof(WorldSettings, fx_water_volume)},
                {"ocean_depth", offsetof(WorldSettings, fx_ocean_depth)},
                {"aftermath", offsetof(WorldSettings, fx_aftermath)},
                {"materials", offsetof(WorldSettings, fx_materials)},
                {"bubble_light", offsetof(WorldSettings, fx_bubble_light)},
                {"contact_shade", offsetof(WorldSettings, fx_contact_shade)},
                {"blast_reflect", offsetof(WorldSettings, fx_blast_reflect)},

            };
            enum { EFFECTS = sizeof effects / sizeof effects[0] };
            WorldSettings keep = settings;
            scene_submit();
            /* Every effect alone, the plain picture, then the default set. */
            for (unsigned i = 0; i <= EFFECTS + 1; ++i) {
                WorldSettings one = keep;
                for (unsigned e = 0; e < EFFECTS; ++e)
                    memcpy((char *)&one + effects[e].offset, &(unsigned){i == e + 1}, sizeof(unsigned));
                one.fx_trails = one.fx_shake = one.fx_smooth = one.fx_interp = 0;
                if (i == EFFECTS + 1) {
                    WorldSettings fresh;
                    world_settings_defaults(&fresh);
                    fresh.bindings = keep.bindings;
                    one = fresh;
                }
                one.fx_enable = 1;                 /* "off" is the scene renderer with no effect on */
                if (i == 0) headless_shot(dir, "fx-raw");
                settings = one;
                /* Run a few game frames so bounced light settles. */
                for (unsigned pass = 0; pass < 8; ++pass) {
                    scene_submit();
                    enhance(pixels, shown, (uint32_t)(ticks * 133 + pass * 67), 1, 1.0);
                }
                char name[32];
                snprintf(name, sizeof name, "fx-%s", i == 0 ? "off" : i == EFFECTS + 1 ? "all" : effects[i - 1].name);
                shot_source = shown;
                headless_shot(dir, name);
                shot_source = pixels;
            }
            /* The light transport's view: rock, sun visibility, light. */
            world_scene_debug(shown);
            shot_source = shown;
            headless_shot(dir, "fx-transport");
            shot_source = pixels;
            settings = keep;
            world_scene_reset();
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
                if (!world_gpu_render_offscreen(pixels, frame_w, preset, 0, 1920, 1080, big, error, sizeof error)) {
                    fprintf(stderr, "CRT preset %u (%s) failed: %s\n", i, i ? world_gpu_preset_name[i - 1] : "OFF", error);
                    continue;
                }
                FILE *f = world_fopen(path, "wb");
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
        static int view_shot_done;
        const char *view_shot = getenv("WORLD_VIEW_SHOT");
        if (view_shot && !view_shot_done && ticks >= strtoul(view_shot, NULL, 10)) {
            view_shot_done = 1;
            char path[4096];
            snprintf(path, sizeof path, "%s/headless-view-fit.bmp", dir);
            world_window_set_view(1, 0);
            int ok = world_window_compose_bmp(pixels, frame_w, 1920, 1080, path);
            world_window_set_view(0, 0);
            if (!ok) { snprintf(failure, sizeof failure, "View composition failed"); break; }
        }
        if (save_path && ticks >= save_at) {
            FILE *f = world_fopen(save_path, "wb");
            if (f) {
                fwrite(ram, sizeof ram, 1, f);
                fwrite(tiles, sizeof tiles, 1, f);
                fwrite(oam, sizeof oam, 1, f);
                fwrite(palette, sizeof palette, 1, f);
                fwrite(layer_master, sizeof layer_master, 1, f);
                fwrite(&ticks, sizeof ticks, 1, f);
                fwrite(&board_clock, sizeof board_clock, 1, f);
                fwrite(sound_state, sizeof sound_state, 1, f);
                fwrite(&stage6_timing, sizeof stage6_timing, 1, f);
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
            at += script_base;
        }
        if (fields != 2) break;
        /* WORLD_STAGE_SEED=n (test-only): pins the level counter so a fresh
           coin+start boots straight into stage n. Evidence runs only. */
        static int stage_seed = -2;
        if (stage_seed == -2) {
            const char *e = getenv("WORLD_STAGE_SEED");
            stage_seed = e ? (int)strtol(e, NULL, 0) : -1;
        }
        /* The frame validator requires word 84 to stay zero through the title
           masters, so the seed only applies along the briefing chain that
           hands it to start_world_stage at A81. */
        if (stage_seed >= 0 && (word(ram + 0x80) == 0x2218 || word(ram + 0x80) == 0x2282
                              || word(ram + 0x80) == 0xa81)) {
            ram[0x84] = (uint8_t)stage_seed; ram[0x85] = 0;
        }
        unsigned input = held, edges = buttons;
        if (autopilot) input = headless_autopilot(held, &edges);
        if (!tick((uint16_t)~input, (uint16_t)~edges)) break;
        unsigned long before = ticks;
        ticks += elapsed_board_frames;
        /* WORLD_SCROLL_LOG=path: the playfield and background scroll at every
           board frame (passes that overran repeat their values). */
        static FILE *scroll_log;
        static int scroll_log_checked;
        if (!scroll_log_checked) {
            scroll_log_checked = 1;
            const char *path = getenv("WORLD_SCROLL_LOG");
            if (path) scroll_log = world_fopen(path, "w");
        }
        if (scroll_log)
            for (unsigned long f = before; f < ticks; ++f)
                fprintf(scroll_log, "%lu %u %u %u %u %u\n", f, word(ram + 0xb70a), word(ram + 0xb70c),
                        word(ram + 0xb70e), word(ram + 0xb710), (unsigned)(ticks - before));
        /* WORLD_TRACE_EVERY=n (test-only) overrides the 1000-tick trace
           interval so transient events (deaths, pickups) are captured. */
        static unsigned trace_every;
        if (!trace_every) {
            const char *e = getenv("WORLD_TRACE_EVERY");
            trace_every = e ? (unsigned)strtoul(e, NULL, 10) : 1000;
            if (!trace_every) trace_every = 1000;
        }
        if (trace && before / trace_every != ticks / trace_every) {
            unsigned sub = word(ram + 0x90);
            unsigned cfg = word(ram + sub + 0x70);
            fprintf(trace, "%lu stage %u B700 %04x 26E %04x 264 %04x 28C %04x 2B8 %04x 2AA %04x 262 %04x 80 %04x 2C4 %04x ba80 %04x ba84 %04x 282 %04x sub %04x x %04x y %04x input %02x up %04x mid %04x down %04x b742 %04x s44 %04x s46 %04x s48 %04x\n",
                    ticks, word(ram + 0x84), word(ram + 0xb700), word(ram + 0x26e), word(ram + 0x264), word(ram + 0x28c),
                    word(ram + 0x2b8), word(ram + 0x2aa), word(ram + 0x262), word(ram + 0x80), word(ram + 0x2c4),
                    word(ram + 0xba80), word(ram + 0xba84), word(ram + 0x282),
                    word(ram + sub), word(ram + sub + 0x21), word(ram + sub + 0x24), input & 0xff,
                    word(ram + cfg + 4), word(ram + cfg + 8), word(ram + cfg + 6),
                    word(ram + 0xb742), word(ram + sub + 0x44), word(ram + sub + 0x46), word(ram + sub + 0x48));
            /* WORLD_ACTOR_TRACE=1 (test-only): append the actor handler list to
               the trace so boss phase transitions (wreck/destroyed states) can
               be located by handler-column changes between samples. */
            static int actor_trace = -1;
            if (actor_trace < 0) actor_trace = getenv("WORLD_ACTOR_TRACE") ? 1 : 0;
            if (actor_trace) {
                unsigned actor = 0x400;
                for (unsigned steps = 0; steps < 512 && actor; ++steps) {
                    fprintf(trace, "    %04x %04x:%04x x %04x y %04x hp %04x\n",
                            actor, word(ram + actor + 2), word(ram + actor),
                            word(ram + actor + 0x21), word(ram + actor + 0x24),
                            word(ram + actor + 0x26));
                    unsigned next = word(ram + actor + 4);
                    if (next == actor || next == 0x400) break;
                    actor = next;
                }
                for (unsigned i = 0, end = word(ram + 0xaee6); i < end && i < 64; i += 2) {
                    unsigned p = word(ram + 0xaee6 + 4 + i);
                    if (!p) continue;
                    fprintf(trace, "    P%04x %04x:%04x x %04x y %04x hp %04x\n",
                            p, word(ram + p + 2), word(ram + p),
                            word(ram + p + 0x21), word(ram + p + 0x24),
                            word(ram + p + 0x26));
                }
                {
                    unsigned p = word(ram + 0xba90);
                    if (p) fprintf(trace, "    W%04x %04x:%04x x %04x y %04x hp %04x f4a %04x f60 %04x f66 %04x f68 %04x\n",
                            p, word(ram + p + 2), word(ram + p),
                            word(ram + p + 0x21), word(ram + p + 0x24),
                            word(ram + p + 0x26), word(ram + p + 0x4a),
                            word(ram + p + 0x60), word(ram + p + 0x66),
                            word(ram + p + 0x68));
                }
                for (unsigned i = 0; i < 16; i += 2) {
                    unsigned p = word(ram + 0x84fe + i);
                    if (!p) continue;
                    fprintf(trace, "    A%02x=%04x %04x:%04x x %04x y %04x hp %04x f60 %04x f66 %04x f68 %04x\n",
                            i, p, word(ram + p + 2), word(ram + p),
                            word(ram + p + 0x21), word(ram + p + 0x24),
                            word(ram + p + 0x26), word(ram + p + 0x60),
                            word(ram + p + 0x66), word(ram + p + 0x68));
                }
            }
        }
        draw();
        if(getenv("WORLD_LIGHT_CAPTURE")){
            static WorldLightScene capture;
            static uint32_t readback[WORLD_VIEW_MAX_WIDTH * 240];
            char err[256]={0};
            scene_submit();
            world_scene_light_target(&capture);
            enhance(pixels,shown,(uint32_t)(ticks*133),1,1);
            if(shot){
                if(!world_gpu_light_offscreen(&capture,shown,readback,frame_w,err,sizeof err)){
                    snprintf(failure,sizeof failure,"GPU capture: %.200s",err);
                    break;
                }
                char name[40];
                snprintf(name,sizeof name,"lighting-%06lu",ticks);
                shot_source=readback;shot_w=frame_w;headless_shot(dir,name);shot_source=pixels;shot_w=frame_w;
                /* WORLD_LIGHT_DUMP=1: keep the exact GPU inputs for offline benchmarks. */
                if(getenv("WORLD_LIGHT_DUMP")){
                    char path[4096];
                    snprintf(path,sizeof path,"%s/light-%06lu.bin",dir,ticks);
                    FILE *f=world_fopen(path,"wb");
                    if(f){fwrite(&capture,sizeof capture,1,f);fwrite(shown,sizeof(uint32_t),frame_w*240,f);fclose(f);}
                }
            }
            world_scene_light_target(NULL);
        }
        /* WORLD_SCENE_RUN=K: render every frame through the scene renderer at
           K times the size, halfway to the frame and on it, with the default
           effects; time it, and save both pictures on shot frames. */
        static int scene_run = -1;
        static double scene_ms, scene_worst;
        static unsigned long scene_count;
        if (scene_run < 0) scene_run = getenv("WORLD_SCENE_RUN") ? atoi(getenv("WORLD_SCENE_RUN")) : 0;
        if (scene_run > 0) {
            WorldSettings keep = settings;
            world_settings_defaults(&settings);
            settings.bindings = keep.bindings;
            settings.fx_enable = 1;
            /* Diagnostic A/B override: all other enhancements stay identical. */
            const char *reactive = getenv("WORLD_SCENE_REACTIVE");
            if (reactive) settings.fx_wakes = settings.fx_debris = settings.fx_silt = settings.fx_splashes = atoi(reactive) != 0;
            const char *cinema = getenv("WORLD_SCENE_CINEMA");
            if (cinema) { unsigned mask=(unsigned)strtoul(cinema,NULL,0);
                settings.fx_pressure = (mask >> 0) & 1;
                settings.fx_sediment_light = (mask >> 1) & 1;
                settings.fx_water_volume = (mask >> 2) & 1;
                settings.fx_ocean_depth = (mask >> 3) & 1;
                settings.fx_aftermath = (mask >> 4) & 1;
                settings.fx_materials = (mask >> 5) & 1;
            }
            const char *micro = getenv("WORLD_SCENE_MICRO");
            if (micro) settings.fx_sparks = settings.fx_impact_motes = atoi(micro) != 0;
            if (!scene_stats && getenv("WORLD_SCENE_STATS")) {
                scene_stats = world_fopen(getenv("WORLD_SCENE_STATS"), "w");
                if (scene_stats) fputs("frame,half,render_ms,waterfx_draw_ms,stage,boss,waterline,wakes,debris,silt,splashes,sparks,impact_motes\n", scene_stats);
            }
            scene_submit();
            for (unsigned half = 0; half < 2; ++half) {
                LARGE_INTEGER t0, t1, hz;
                QueryPerformanceFrequency(&hz);
                QueryPerformanceCounter(&t0);
                enhance(pixels, shown, (uint32_t)(ticks * 133 + half * 67), scene_run, half ? 1.0 : 0.5);
                QueryPerformanceCounter(&t1);
                double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart;
                if (scene_stats) {
                    extern double world_scene_waterfx_draw_ms;
                    extern unsigned world_scene_waterfx_emitted[6];
                    fprintf(scene_stats, "%lu,%u,%.6f,%.6f,%u,%u,%d,%u,%u,%u,%u,%u,%u\n",
                            ticks, half, ms, world_scene_waterfx_draw_ms, word(ram + 0x84), headless_boss_state,
                            waterline_row(), world_scene_waterfx_emitted[0], world_scene_waterfx_emitted[1],
                            world_scene_waterfx_emitted[2], world_scene_waterfx_emitted[3],
                            world_scene_waterfx_emitted[4], world_scene_waterfx_emitted[5]);
                }
                scene_ms += ms;
                if (ms > scene_worst) scene_worst = ms;
                ++scene_count;
                if (shot) {
                    char name[40];
                    snprintf(name, sizeof name, "scene-%06lu-%s", ticks, half ? "b" : "a");
                    shot_source = shown; shot_k = scene_run;
                    headless_shot(dir, name);
                    shot_source = pixels; shot_k = 1;
                }
            }
            settings = keep;
            if (ticks % 2000 < elapsed_board_frames)
                fprintf(stderr, "scene K=%d: mean %.2f ms, worst %.2f ms over %lu renders%c", scene_run,
                        scene_ms / (double)scene_count, scene_worst, scene_count, 10);
            if (ticks % 2000 < elapsed_board_frames) {

                double n = world_scene_stage_ms[5];
                extern unsigned world_scene_explosions;
                extern unsigned world_scene_waterfx_emitted[6];
                fprintf(stderr, "  explosions detected: %u%c", world_scene_explosions, 10);
                fprintf(stderr, "  water effects: wakes %u, debris %u, silt %u, splashes %u%c",
                        world_scene_waterfx_emitted[0], world_scene_waterfx_emitted[1],
                        world_scene_waterfx_emitted[2], world_scene_waterfx_emitted[3], 10);
                fprintf(stderr, "  colours %.2f, sprites + light transport %.2f, layer fx %.2f, compose %.2f, post %.2f ms%c",
                        world_scene_stage_ms[0] / n, world_scene_stage_ms[1] / n, world_scene_stage_ms[2] / n,
                        world_scene_stage_ms[3] / n, world_scene_stage_ms[4] / n, 10);
            }
        }
        if (shot) {
            char name[32];
            snprintf(name, sizeof name, "%06lu", ticks);
            /* WORLD_FX_SHOTS=1 saves every shot with the water effects on. */
            int with_fx = getenv("WORLD_FX_SHOTS") != NULL;
            if (with_fx) {
                WorldSettings keep = settings;
                world_settings_defaults(&settings);
                settings.bindings = keep.bindings;
                settings.fx_enable = 1;
                settings.fx_smooth = settings.fx_interp = 0;
                scene_submit();
                for (unsigned pass = 0; pass < 2; ++pass)
                    enhance(pixels, shown, (uint32_t)(ticks * 133 + pass * 67), 1, 1.0);
                settings = keep;
                shot_source = shown;
            }
            headless_shot(dir, name);
            shot_source = pixels;
            char log_path[4096];
            snprintf(log_path, sizeof log_path, "%s/headless-scroll.txt", dir);
            FILE *log = world_fopen(log_path, "a");
            if (log) {
                fprintf(log, "%06lu stage %u master %04x", ticks, word(ram + 0x84), word(ram + 0x80));
                for (unsigned i = 0; i < 3; ++i)
                    fprintf(log, " L%u %04x %04x m%04x", i, word(ram + 0xb706 + 4*i), word(ram + 0xb708 + 4*i), layer_master[i]);
                fprintf(log, " vc %04x water %d\n", word(ram + 0x8b28), with_fx ? world_scene_waterline() : -1);
                fclose(log);
            }
        }
    }
done:
    if (scene_stats) fclose(scene_stats);
    if (trace) fclose(trace);
    fclose(script);
    draw();
    headless_shot(dir, "final");
    /* Test-only: the live actor list with its key words, for diagnosing runs. */
    char path[4096];
    snprintf(path, sizeof path, "%s/headless-actors.txt", dir);
    FILE *list = world_fopen(path, "w");
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

/* Cabinet switch bank 1 as the board reads it (active low): lives on bits
   0-1, difficulty on bits 2-3, demo sound on bit 6. */
static uint16_t cabinet_switches(void)
{
    static const uint16_t lives[4] = {2, 3, 1, 0}, difficulty[4] = {0x0, 0x8, 0xc, 0x4};
    return (uint16_t)((0xffbf & ~0x4f) | lives[settings.dip_lives & 3] |
                      difficulty[settings.dip_difficulty & 3] | (settings.dip_demo_sound ? 0 : 0x40));
}

static void set_cabinet_switches(void) { world_dispatch_port4 = cabinet_switches(); }

/* Sound board power-on: the command gate and the title's reset command. */
static void sound_power_on(void)
{
    imm_reset(0,0);
    world_ym_reset();
    world_audio_reset_ym(sound_rom, sound_fm_write, NULL);
    world_audio_reset(sound_state, sound_rom, 0);
    world_audio_receive(sound_state, 0); /* Power-on command gate. */
    world_audio_receive(sound_state, 0x20); /* Title initialization's reset command. */
    world_audio_tick(sound_state, sound_rom, &sound_bus);
}

/* Exit to menu: the whole board back to power-on, with the cabinet switches
   as they are set now. */
/* The board ranks ten 16-byte records through a rotating pointer table.
 * Store rows in display order. The two live player slots stay board-owned. */
enum { SCORE_BYTES = 10 * 16 };
static unsigned char saved_scores[SCORE_BYTES];
static wchar_t score_path[4096];
static int scores_active;

static void load_scores(void)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data_directory, -1, score_path, 4040);
    scores_active = n > 0;
    if (!scores_active) return;
    wcscat(score_path, world_view_mode() == WORLD_VIEW_ULTRA ? L"/scores-ultrawide.dat"
                     : world_view_mode() == WORLD_VIEW_WIDE ? L"/scores-wide.dat" : L"/scores-arcade.dat");
    FILE *f = _wfopen(score_path, L"rb");
    unsigned char rows[SCORE_BYTES];
    if (f) {
        int ok = fread(rows, 1, sizeof rows, f) == sizeof rows && fgetc(f) == EOF && !ferror(f);
        fclose(f);
        for (unsigned i = 0; ok && i < 10; ++i) {
            unsigned stage = word(rows + 16*i + 6);
            if ((stage & 1) || stage > 12) ok = 0;
        }
        if (ok) memcpy(ram + 0x180, rows, sizeof rows);
    }
    memcpy(saved_scores, ram + 0x180, SCORE_BYTES);
}

static void save_scores(void)
{
    if (!scores_active) return;
    unsigned char rows[SCORE_BYTES];
    for (unsigned i = 0; i < 10; ++i) {
        unsigned slot = word(ram + 0x240 + i*2);
        if (slot < 0x180 || slot > 0x230 || (slot & 15)) return;
        memcpy(rows + 16*i, ram + slot, 16);
    }
    if (!memcmp(rows, saved_scores, sizeof rows)) return;
    wchar_t temp[4160];
    swprintf(temp, sizeof temp / sizeof *temp, L"%ls.%lu.tmp", score_path, GetCurrentProcessId());
    FILE *f = _wfopen(temp, L"wb");
    if (f) {
        int ok = fwrite(rows, 1, sizeof rows, f) == sizeof rows;
        if (fclose(f) == 0 && ok && MoveFileExW(temp, score_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            memcpy(saved_scores, rows, sizeof rows);
            return;
        }
        DeleteFileW(temp);
    }
    fprintf(stderr, "Could not save the selected mode's scores.\n");
}

static void machine_reset(void)
{
    set_cabinet_switches();
    world_dispatch_reset();
    world_shadow_reset();
    world_stage_reset_prespawn();
    world_boot_world(ram, tiles, oam, palette, rom, world_dispatch_port4);
    memcpy(layer_master, title_master_controls, sizeof layer_master);
    weapon_lights_count=0;memset(flight,0,sizeof flight);
    ticks = 0;
    world_clock_reset(&board_clock, settings.timing_mode);
    world_ga20_reset(&pcm, samples, sizeof samples);
    memset(&acoustics,0,sizeof acoustics);
    memset(board_origins,0,sizeof board_origins);
    memset(command_origins,0,sizeof command_origins);
    memset(effect_origins,0,sizeof effect_origins);
    memset(voice_origins,0,sizeof voice_origins);
    selected_origin=(SoundOrigin){0};
    stereo_phase=0;memset(stereo_last,0,sizeof stereo_last);
    sound_frame_phase = sound_timer_phase = 0;
    sound_power_on();
    memset(slowdown_shadow, 0, sizeof slowdown_shadow);
    memset(&stage6_timing, 0, sizeof stage6_timing);
    world_scene_reset();
}

/* START GAME on the title menu drops a coin and presses 1P start, as a
   player at the cabinet would; the title menu itself keeps the game's
   controls idle. start_from is the board frame the sequence began on. */
static unsigned long start_from;
static int starting;

static int play_tick(uint16_t pads, uint16_t system)
{
    if (menu.open && menu.title) pads = system = 0xffff;
    if (settings.autofire && (ticks & 4)) pads |= 0xc0c0;     /* active low: release on the off phase */
    if (starting) {
        unsigned long d = ticks - start_from;
        if (d < 6) system &= (uint16_t)~4;                                /* coin 1 */
        else if (d % 60 >= 50 && d % 60 < 56) system &= (uint16_t)~1;     /* 1P start */
        if ((d > 60 && word(ram + 0x80) == 0xb2e) || d > 400) starting = 0;
    }
    return tick(pads, system);
}

/* Camera samples belong to display refreshes. CPU wake-up jitter must
 * not change their spacing. Only predict the display time, never inputs
 * or the simulation. Discard stale statistics after a pause/mode change. */
static double camera_display_time(LONGLONG now,LONGLONG frequency)
{
    static int64_t anchor, last_sync;
    static unsigned anchor_refresh;
    static double period;
    int64_t sync;
    unsigned refresh;
    if (!world_gpu_sync_sample(&sync,&refresh) || sync>now || now-sync>frequency/4) {
        anchor=last_sync=0; period=0;
        return (double)now;
    }
    if (!anchor || sync<last_sync) {
        anchor=sync; anchor_refresh=refresh; period=0;
    }
    unsigned elapsed=refresh-anchor_refresh;
    if (elapsed>=8 && sync>anchor) {
        double sample=(double)(sync-anchor)/elapsed;
        if (sample>=frequency/1000.0 && sample<=frequency/20.0) period=sample;
        anchor=sync; anchor_refresh=refresh;
    }
    last_sync=sync;
    if (!period) return (double)now;
    /* Associate the wake with its nearest scan boundary. Leading by a whole
       refresh would saturate the one-tick prediction on 120 Hz displays. */
    return sync+floor((now-sync)/period+.5)*period;
}

/* Restart the attract sequence in the configured aspect ratio. Sessions
   keep their mode for a whole game, so the option applies only from the
   title menu (or on returning to it). The caller saves the scores. */
static void apply_session_aspect(void)
{
    world_view_set(settings.aspect);
    machine_reset();
    load_scores();
    if (!settings.fullscreen) world_window_set_display(0, settings.window_scale, world_view_width());
}

static int run_game(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : world_default_directory();
    if (!dir) return 1;
    data_directory = dir;
    int path_length = snprintf(settings_path, sizeof settings_path, "%s/native-settings.ini", dir);
    if (path_length < 0 || (size_t)path_length >= sizeof settings_path) return 1;
    WorldSettingsStatus settings_status = world_settings_load(settings_path, &settings);
    int timing_override = 0;
    const char *headless = NULL;
    int mode_option = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strncmp(argv[i], "--headless=", 11)) {
            headless = argv[i] + 11;
            continue;
        }
        if (!strncmp(argv[i], "--mode=", 7)) {
            mode_option = 1;
            if (!strcmp(argv[i] + 7, "wide")) world_view_set(WORLD_VIEW_WIDE);
            else if (strcmp(argv[i] + 7, "arcade")) {
                fprintf(stderr, "Unknown --mode=%s. Use arcade or wide.\n", argv[i] + 7);
                return 1;
            }
            continue;
        }
        if (strncmp(argv[i], "--timing=", 9) ||
            !world_timing_mode_parse(argv[i] + 9, &settings.timing_mode)) {
            fprintf(stderr, "Unknown native option %s. Use --timing=arcade or --timing=remove-slowdown.\n", argv[i]);
            return 1;
        }
        timing_override = 1;
    }
    /* Scripted runs stay Arcade unless --mode says otherwise. */
    if (!mode_option && !headless) world_view_set(settings.aspect);
    const char *launcher_mode = getenv("RECOMPLAUNCHER_MODE");
    if (!headless && launcher_mode && !strcmp(launcher_mode, "bigpicture")) {
        desktop_fullscreen = settings.fullscreen;
        launch_fullscreen_override = 1;
        settings.fullscreen = 1;
    }
    world_clock_reset(&board_clock, settings.timing_mode);
load_game_data:
    if (!load(dir, "maincpu.bin", rom, sizeof rom) || !load(dir, "gfx1.bin", gfx1, sizeof gfx1)
        || !load(dir, "gfx2.bin", gfx2, sizeof gfx2)
        || !load(dir, "soundcpu.bin", sound_rom, sizeof sound_rom)
        || !load(dir, "irem.bin", samples, sizeof samples)) {
        if (!headless && world_setup_screen(dir)) goto load_game_data;
        return 1;
    }
    if (!headless) set_cabinet_switches();      /* scripted runs keep the factory setting */
    else if (getenv("WORLD_DIP")) {             /* test-only: "lives,difficulty" headless dip override */
        unsigned dl = 1, dd = 2;
        sscanf(getenv("WORLD_DIP"), "%u,%u", &dl, &dd);
        settings.dip_lives = dl & 3; settings.dip_difficulty = dd & 3;
        set_cabinet_switches();
    }
    if (!world_boot_world(ram, tiles, oam, palette, rom, world_dispatch_port4)) {
        if (!headless) MessageBoxA(NULL, "World startup checks failed. Re-import the World ROM archive.",
                                   "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    world_ga20_reset(&pcm, samples, sizeof samples);
    world_scene_init(gfx1, gfx2);
    world_scene_init_impact_art(rom);
    /* A game, not a background task: keep Windows from parking it on the
       efficiency cores or slowing its clock when the window is not in front. */
    {
        PROCESS_POWER_THROTTLING_STATE power;
        memset(&power, 0, sizeof power);
        power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        power.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &power, sizeof power);
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }
    if ((timing_override || settings_status == WORLD_SETTINGS_MISSING) &&
        settings_status != WORLD_SETTINGS_NEWER_VERSION &&
        save_runtime_settings() != WORLD_SETTINGS_OK)
        fprintf(stderr, "Could not save native settings.\n");
    fprintf(stderr, "Timing policy: %s (pass costs fitted to MAME measurements).\n",
            world_timing_mode_label(settings.timing_mode));
    sound_power_on();
    if (headless) {
        headless_run(dir, headless, timing_override);
        goto finished;
    }
    const char *boss_snapshot=getenv("WORLD_BOSS_BENCH_SNAPSHOT");
    if(boss_snapshot && !restore_snapshot(boss_snapshot)){snprintf(failure,sizeof failure,"Cannot load boss replay.");goto finished;}
    sound_device = world_audio_output_open();
    if (!world_window_open()) {
        MessageBoxA(NULL, "Windows could not create the game window.", "In The Hunt", MB_ICONERROR);
        world_audio_output_close();
        return 1;
    }
    world_window_set_display(settings.fullscreen, settings.window_scale, world_view_width());
    apply_view();
    timeBeginPeriod(1);                 /* Sleep(1) must mean one millisecond for even pacing */
    LARGE_INTEGER rate, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&now);
    double deadline = (double)now.QuadPart;
    const double arcade_period = (double)rate.QuadPart * WORLD_CLOCK_FRAME_PIXELS / WORLD_CLOCK_PIXEL_HZ;
    uint16_t pads, system;
    static uint32_t paused_frame[WORLD_VIEW_MAX_WIDTH * 240];
    unsigned previous_toggle = 0, hold_input = 0, counted = 0, presented = 0;
    int was_focused = 0;
    LONGLONG counted_from = now.QuadPart;
    double last_tick = deadline;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    world_menu_open_title(&menu, 0, 0, GetTickCount());
    if (!boss_snapshot) load_scores();
    if(boss_snapshot)menu.open=0;
    /* Test hooks: WORLD_PACING_LOG=path logs every displayed refresh;
       WORLD_RUN_SECONDS=n quits after n seconds. */
    FILE *pacing_log = getenv("WORLD_PACING_LOG") ? world_fopen(getenv("WORLD_PACING_LOG"), "w") : NULL;
    /* Keep diagnostic file writes off the presentation deadline. */
    static char pacing_buffer[4*1024*1024];
    if (pacing_log) setvbuf(pacing_log,pacing_buffer,_IOFBF,sizeof pacing_buffer);
    double run_seconds = getenv("WORLD_RUN_SECONDS") ? atof(getenv("WORLD_RUN_SECONDS")) : 0.0;
    LONGLONG run_start = now.QuadPart;
    while (world_window_poll(&pads, &system)) {
        /* F1, Esc, a pad's Start or L3+R3 opens the pause menu; the game
           stands still. The title menu runs over the live attract sequence. */
        unsigned toggle;
        int gamepad;
        unsigned menu_held = world_window_menu_buttons(&toggle, &gamepad);
        unsigned rising = toggle & ~previous_toggle;
        previous_toggle = toggle;
        /* Losing the window focus pauses, when that option is on. */
        int focused = world_window_focused();
        if (settings.pause_unfocused && was_focused && !focused && !menu.open && !starting) rising |= 1;
        was_focused = focused;
        QueryPerformanceCounter(&now);
        if (run_seconds > 0.0 && (double)(now.QuadPart - run_start) > run_seconds * (double)rate.QuadPart) break;
        static unsigned long test_refreshes;
        ++test_refreshes;
        if (getenv("WORLD_TEST_MENU") && !menu.open && test_refreshes == 180) {
            draw_captured_pixels();
            memcpy(paused_frame, pixels, sizeof paused_frame);
            world_menu_open(&menu, 0, 0, GetTickCount());
            menu.page = 1;
        }
        /* WORLD_TEST_TITLE=1: START GAME, play, EXIT TO MENU, START GAME again,
           logging the game's master state; quits when done. */
        if (getenv("WORLD_TEST_TITLE")) {
            static unsigned phase;
            static unsigned long logged = 1;
            if (ticks / 60 != logged) {
                logged = ticks / 60;
                fprintf(stderr, "title-test phase %u tick %lu master %04X menu %u/%u starting %d%c",
                        phase, ticks, word(ram + 0x80), menu.open, menu.title, starting, 10);
            }
            if ((phase == 0 || phase == 2) && ticks >= 120 && menu.open && menu.title) {
                menu.open = 0;
                starting = 1;
                start_from = ticks;
                ++phase;
            } else if (phase == 1 && ticks >= 900) {
                machine_reset();
                starting = 0;
                world_menu_open_title(&menu, 0, 0, GetTickCount());
                ++phase;
            } else if (phase == 3 && ticks >= 900) break;
        }
        if (menu.open && !menu.title) world_gpu_request_vrr(0);
        if (menu.open && menu.title) {
            unsigned title_held = menu_held | ((toggle & 2) ? WORLD_MENU_BACK : 0);
            world_menu_title_activity(&menu, title_held, world_window_button_activity() || toggle, gamepad, GetTickCount());
            unsigned result = menu_frame(paused_frame, menu_held, toggle, rising, gamepad);
            if (result & WORLD_MENU_QUIT) break;
            if ((result & WORLD_MENU_ASPECT) && settings.aspect != world_view_mode()) {
                save_scores();
                apply_session_aspect();
                QueryPerformanceCounter(&now);
                deadline = last_tick = (double)now.QuadPart;
            } else if (result & WORLD_MENU_START) {
                if (cabinet_switches() != world_dispatch_port4) {
                    save_scores();
                    machine_reset();
                    load_scores();
                }
                starting = 1;
                start_from = ticks;
                hold_input = 1;
            }
        } else if (menu.open) {
            unsigned result = menu_frame(paused_frame, menu_held, toggle, rising, gamepad);
            if (result & WORLD_MENU_QUIT) break;
            if (result & WORLD_MENU_TITLE) {
                save_scores();
                scores_active = 0;
                starting = 0;
                apply_session_aspect();
                world_menu_open_title(&menu, menu_held, gamepad, GetTickCount());
            }
            if (!menu.open || menu.title) {
                hold_input = 1;
                previous_toggle = toggle;
                QueryPerformanceCounter(&now);
                deadline = (double)now.QuadPart;
                last_tick = deadline;
            }
            world_window_wait_vblank();
            if (pacing_log) {
                QueryPerformanceCounter(&now);
                fprintf(pacing_log, "%.4f menu %.3f%c", (double)now.QuadPart * 1000.0 / (double)rate.QuadPart, beam_time, 10);
            }
            continue;
        }
        if (rising && !menu.open) {
            world_gpu_request_vrr(0);
            draw_captured_pixels();
            memcpy(paused_frame, pixels, sizeof paused_frame);
            world_menu_open(&menu, menu_held | ((toggle & 2) ? WORLD_MENU_BACK : 0), gamepad, GetTickCount());
            menu_frame(paused_frame, menu_held, toggle, 0, gamepad);
            continue;
        }
        /* Board deadlines advance only by elapsed arcade vblanks. Display
           refreshes may repeat a state, but never stage the next input frame. */
        QueryPerformanceCounter(&now);
        /* WORLD_TEST_VRR=1 (developer): request VRR in a window, to exercise
           the fixed-refresh detection without taking over the display. */
        static int test_vrr = -1;
        if (test_vrr < 0) test_vrr = getenv("WORLD_TEST_VRR") != NULL;
        int vrr = world_gpu_request_vrr(settings.pacing && ((settings.fullscreen && focused) || test_vrr));
        static int told_vrr;
        if (!told_vrr && world_gpu_vrr_failed()) { show_notice("NO VRR: VSYNC PACING"); told_vrr = 1; }
        double period = arcade_period;
        if (!settings.pacing) {
            double hz = world_window_refresh_hz();
            double multiple = floor(hz * arcade_period / rate.QuadPart + 0.5);
            if (multiple >= 1 && multiple <= 8 &&
                fabs(hz / multiple * arcade_period / rate.QuadPart - 1.0) <= 0.006)
                period = rate.QuadPart * multiple / hz;
        }
        /* A stall (window drag, sleep, debugger) must not fast-forward the
           board to catch up: beyond a few frames, rejoin the clock instead. */
        if ((double)now.QuadPart - deadline > 4.0 * period) deadline = (double)now.QuadPart;
        /* Immediate presents do not throttle the loop. Wait before polling
           gameplay input and keep the deadline on the absolute board clock. */
        if (vrr) {
            while ((double)now.QuadPart < deadline) {
                double ms = (deadline - now.QuadPart) * 1000.0 / rate.QuadPart;
                if (ms >= 2.0) Sleep((DWORD)(ms - 1.0));
                else YieldProcessor();
                QueryPerformanceCounter(&now);
            }
        }
        int advanced = 0;
        unsigned passes = 0;            /* logged: two in one refresh would be judder */
        while ((double)now.QuadPart >= deadline) {
            if (!world_window_poll(&pads, &system)) goto end_loop;
            if (hold_input) {
                if (pads != 0xffff || ((uint16_t)~system & 15) || menu_held) pads = system = 0xffff;
                else hold_input = 0;
            }
            if(boss_snapshot){unsigned e;pads=(uint16_t)~headless_autopilot(0xc1,&e);}
            if (!play_tick(pads, system)) goto end_loop;
            last_tick = deadline;
            deadline += period * elapsed_board_frames;
            ticks += elapsed_board_frames;
            counted += elapsed_board_frames;
            advanced = 1;
            ++passes;
        }
        LARGE_INTEGER after_tick, after_submit, after_present;
        if (pacing_log) QueryPerformanceCounter(&after_tick);
        if (advanced) {
            capture_video();
            if (frame_w > WORLD_VIEW_ARCADE_WIDTH) draw_captured_pixels();
            scene_submit();
        }
        /* Hold original multi-vblank slowdown frames. On extra display
           refreshes only the camera can advance beyond the newest pose. */
        double phase = vrr ? 0.0 : (camera_display_time(now.QuadPart,rate.QuadPart) - last_tick) / period;
        scene_alpha = 1.0 + (elapsed_board_frames == 1 ? fmin(1.0,fmax(0.0,phase)) : 0.0);
        if (pacing_log) QueryPerformanceCounter(&after_submit);
        beam_step();
        present_game(pixels);
        ++presented;
        if (pacing_log) {
            QueryPerformanceCounter(&after_present);
            fprintf(pacing_log,"cost %d %.3f %.3f %.3f",advanced,
                    (after_tick.QuadPart-now.QuadPart)*1000.0/rate.QuadPart,
                    (after_submit.QuadPart-after_tick.QuadPart)*1000.0/rate.QuadPart,
                    (after_present.QuadPart-after_submit.QuadPart)*1000.0/rate.QuadPart);
            for(int j=0;j<5;++j) fprintf(pacing_log," %.3f",world_scene_stage_ms[j]);
            memset(world_scene_stage_ms,0,sizeof(double)*6);
            fputc(10,pacing_log);
            fprintf(pacing_log,"prof %d %.3f %.3f %.3f",present_profile_k,present_profile_ms[0],present_profile_ms[1],present_profile_ms[2]);
            for(int j=0;j<5;++j) fprintf(pacing_log," %.3f",world_gpu_present_ms[j]);
            fputc(10,pacing_log);
            fprintf(pacing_log, "%.4f %lu %.5f %u\n",
                    (double)now.QuadPart * 1000.0 / rate.QuadPart,ticks,scene_alpha,word(ram + 0xb70a));
            fprintf(pacing_log,"audio %u\n",world_audio_output_level());
            fprintf(pacing_log,"passes %u %u\n",passes,elapsed_board_frames);
            fprintf(pacing_log,"camera %u %u %.5f %.5f %d\n",drawn_layers[1].scroll_x,
                    drawn_layers[2].scroll_x,displayed_camera_offsets[0],displayed_camera_offsets[2],focused);
            unsigned shown_present, shown_refresh, made;
            if (world_gpu_frame_statistics(&shown_present,&shown_refresh,&made))
                fprintf(pacing_log,"dxgi %u %u %u\n",shown_present,shown_refresh,made);
        }
        if (word(ram + 0x80) == 0xc7f8) save_scores();
        world_audio_output_pump();
        world_window_wait_vblank();
        QueryPerformanceCounter(&now);
        if (now.QuadPart - counted_from >= rate.QuadPart / 2) {
            double seconds = (double)(now.QuadPart - counted_from) / (double)rate.QuadPart;
            shown_fps = (unsigned)(presented / seconds + 0.5);
            shown_speed = (unsigned)(counted / seconds * arcade_period / (double)rate.QuadPart * 100.0 + 0.5);
            counted = presented = 0;
            counted_from = now.QuadPart;
        }
    }
end_loop:
    save_scores();
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
    if (world_dispatch_estimates)
        fprintf(stderr, "Register estimates: %lu (last: %s)%c", world_dispatch_estimates, world_dispatch_estimate_note, 10);
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


/* Decode arguments once, losslessly, before settings, ROM or shader access. */
int wmain(int argc, wchar_t **wide_argv)
{
    if (argc == 3 && !wcscmp(wide_argv[1], L"--import-rom")) return world_setup_import(wide_argv[2]);
    if (argc == 1) {
        DWORD process;
        if (GetConsoleProcessList(&process, 1) == 1) ShowWindow(GetConsoleWindow(), SW_HIDE);
    }
    char **argv = calloc((size_t)argc + 1, sizeof *argv);
    if (!argv) return 1;
    int result = 1, converted = 0;
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0, NULL, NULL);
        if (!n || !(argv[i] = malloc((size_t)n))) goto cleanup;
        ++converted;
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, argv[i], n, NULL, NULL)) goto cleanup;
    }
    result = run_game(argc, argv);
cleanup:
    for (int i = 0; i < converted; ++i) free(argv[i]);
    free(argv);
    return result;
}
