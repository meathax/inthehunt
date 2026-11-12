#include "world_menu.h"
#include "world_gpu.h"
#include <stdio.h>
#include <string.h>

/* Pause and options overlay for the native port, drawn at the game's own
 * 320x240 resolution with the arcade's 8x8 and 16x16 lettering from gfx1.
 * The game's font uses pen 1 for the face, A..E for a top-to-bottom shade
 * and F for the drop shadow, so each style below supplies those colours. */

enum { W = 320, H = 240 };
enum { PAGE_MAIN, PAGE_OPTIONS, PAGE_CONTROLS, PAGE_QUIT };
enum { TAB_VIDEO, TAB_SOUND, TAB_GAME, TAB_ENHANCE, TAB_COUNT };
enum {
    ITEM_RESUME, ITEM_OPTIONS, ITEM_CONTROLS, ITEM_QUIT,
    ITEM_DISPLAY, ITEM_SCALE, ITEM_BORDER, ITEM_LIGHT,
    ITEM_VOLUME, ITEM_SLOWDOWN, ITEM_DEFAULTS, ITEM_BACK,
    ITEM_NO, ITEM_YES, ITEM_TABS,
    ITEM_SCALING, ITEM_VSYNC, ITEM_FPS, ITEM_SHOWFPS, ITEM_FM, ITEM_PCM,
    ITEM_FOCUS, ITEM_LIVES, ITEM_DIFFICULTY, ITEM_DEMOSOUND,
    ITEM_FX, ITEM_FX_CAUSTICS, ITEM_FX_RAYS, ITEM_FX_LIGHTS, ITEM_FX_BLOOM, ITEM_FX_SHIMMER,
    ITEM_FX_DEPTH, ITEM_FX_SNOW, ITEM_FX_VIVID, ITEM_FX_VIGNETTE, ITEM_FX_TRAILS, ITEM_FX_SCANLINES,
    ITEM_CRT, ITEM_BEAM, ITEM_PACING,
    ITEM_FX_SHADOWS, ITEM_FX_DOF, ITEM_FX_HAZE, ITEM_FX_RELIEF, ITEM_FX_STEREO, ITEM_FX_SHOCK,
    ITEM_FX_PARTICLES, ITEM_FX_SHAKE, ITEM_FX_SURFACE, ITEM_FX_SMOOTH, ITEM_FX_INTERP,
    ITEM_START
};
enum { MAX_ITEMS = 40, VISIBLE_ROWS = 6, ROW_STEP = 15 };

const unsigned world_menu_fps[WORLD_MENU_FPS_CHOICES] = {0, 90, 120, 180, 240, 0xffffffffu};

/* The on/off switch behind an item, or NULL. */
static unsigned *toggle_field(WorldSettings *s, unsigned id)
{
    switch (id) {
    case ITEM_VSYNC: return &s->vsync;
    case ITEM_SHOWFPS: return &s->show_fps;
    case ITEM_FOCUS: return &s->pause_unfocused;
    case ITEM_DEMOSOUND: return &s->dip_demo_sound;
    case ITEM_FX: return &s->fx_enable;
    case ITEM_FX_CAUSTICS: return &s->fx_caustics;
    case ITEM_FX_RAYS: return &s->fx_rays;
    case ITEM_FX_LIGHTS: return &s->fx_lights;
    case ITEM_FX_BLOOM: return &s->fx_bloom;
    case ITEM_FX_SHIMMER: return &s->fx_shimmer;
    case ITEM_FX_DEPTH: return &s->fx_depth;
    case ITEM_FX_SNOW: return &s->fx_snow;
    case ITEM_FX_VIVID: return &s->fx_vivid;
    case ITEM_FX_VIGNETTE: return &s->fx_vignette;
    case ITEM_FX_TRAILS: return &s->fx_trails;
    case ITEM_FX_SCANLINES: return &s->fx_scanlines;
    case ITEM_FX_SHADOWS: return &s->fx_shadows;
    case ITEM_FX_DOF: return &s->fx_dof;
    case ITEM_FX_HAZE: return &s->fx_haze;
    case ITEM_FX_RELIEF: return &s->fx_relief;
    case ITEM_FX_STEREO: return &s->fx_stereo;
    case ITEM_FX_SHOCK: return &s->fx_shock;
    case ITEM_FX_PARTICLES: return &s->fx_particles;
    case ITEM_FX_SHAKE: return &s->fx_shake;
    case ITEM_FX_SURFACE: return &s->fx_surface;
    case ITEM_FX_SMOOTH: return &s->fx_smooth;
    case ITEM_FX_INTERP: return &s->fx_interp;
    default: return NULL;
    }
}

typedef struct { uint32_t pen[16]; } Ramp;

static Ramp make_ramp(uint32_t face, uint32_t top, uint32_t bottom, uint32_t shadow)
{
    Ramp ramp;
    memset(&ramp, 0, sizeof ramp);
    ramp.pen[1] = face;
    for (unsigned i = 0; i < 5; ++i) {
        unsigned r = (((top >> 16) & 255) * (4 - i) + ((bottom >> 16) & 255) * i) / 4;
        unsigned g = (((top >> 8) & 255) * (4 - i) + ((bottom >> 8) & 255) * i) / 4;
        unsigned b = ((top & 255) * (4 - i) + (bottom & 255) * i) / 4;
        ramp.pen[10 + i] = 0xff000000u | r << 16 | g << 8 | b;
    }
    ramp.pen[15] = shadow;
    for (unsigned i = 2; i < 10; ++i) ramp.pen[i] = face;
    return ramp;
}

static Ramp gold(void)  { return make_ramp(0xfffff0b0, 0xffffd860, 0xffa84818, 0xff1c0800); }
static Ramp steel(void) { return make_ramp(0xffe6f4ff, 0xffb4d4ee, 0xff3c6890, 0xff040a14); }
static Ramp sea(void)   { return make_ramp(0xffc8fff4, 0xff78e8dc, 0xff1c7c88, 0xff021014); }
static Ramp dim(void)   { return make_ramp(0xff8aa2b4, 0xff7890a4, 0xff3a4c5e, 0xff02060a); }

/* ---- drawing primitives ------------------------------------------------ */

static void put(uint32_t *c, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < W && y < H) c[y * W + x] = color;
}

static void fill(uint32_t *c, int x, int y, int w, int h, uint32_t color)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) put(c, i, j, color);
}

static uint32_t mix(uint32_t a, uint32_t b, unsigned t /* 0..256 toward b */)
{
    unsigned r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    unsigned g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    unsigned bl = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return 0xff000000u | r << 16 | g << 8 | bl;
}

static void shade(uint32_t *c, int x, int y, int w, int h, uint32_t color, unsigned t)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            if (i >= 0 && j >= 0 && i < W && j < H) c[j * W + i] = mix(c[j * W + i], color, t);
}

static unsigned tile_pen(const uint8_t *gfx1, unsigned code, unsigned x, unsigned y)
{
    const uint8_t *row = gfx1 + code * 8 + (y & 7);
    unsigned shift = 7 - (x & 7), pen = 0;
    for (unsigned plane = 0; plane < 4; ++plane)
        pen |= ((row[plane * 0x80000] >> shift) & 1u) << plane;
    return pen;
}

static void tile(uint32_t *c, const uint8_t *gfx1, int x, int y, unsigned code, const Ramp *ramp)
{
    for (unsigned j = 0; j < 8; ++j)
        for (unsigned i = 0; i < 8; ++i) {
            unsigned pen = tile_pen(gfx1, code, i, j);
            if (pen) put(c, x + (int)i, y + (int)j, ramp->pen[pen]);
        }
}

static unsigned small_code(char ch)
{
    unsigned c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c -= 32;
    return c >= 0x21 && c < 0x60 && c != '=' && c != '>' ? c : 0;
}

static int text_px(const char *text) { return (int)strlen(text) * 8; }

static void text(uint32_t *c, const uint8_t *gfx1, int x, int y, const char *s, const Ramp *ramp)
{
    for (; *s; ++s, x += 8) {
        unsigned code = small_code(*s);
        if (code) tile(c, gfx1, x, y, code, ramp);
    }
}

static void text_center(uint32_t *c, const uint8_t *gfx1, int y, const char *s, const Ramp *ramp)
{
    text(c, gfx1, (W - text_px(s)) / 2, y, s, ramp);
}

/* 16x16 lettering: four tiles at 100h + (char - 30h) * 4. */
static void big_text_center(uint32_t *c, const uint8_t *gfx1, int y, const char *s, const Ramp *ramp)
{
    int x = (W - (int)strlen(s) * 16) / 2;
    for (; *s; ++s, x += 16) {
        unsigned ch = (unsigned char)*s;
        if (ch < '0' || ch > 'Z' || ch == ':' || ch == 'J') continue;
        unsigned base = 0x100 + (ch - 0x30) * 4;
        tile(c, gfx1, x, y, base, ramp);
        tile(c, gfx1, x + 8, y, base + 1, ramp);
        tile(c, gfx1, x, y + 8, base + 2, ramp);
        tile(c, gfx1, x + 8, y + 8, base + 3, ramp);
    }
}

/* A small torpedo pointing right, used as the cursor and value arrows. */
static void torpedo(uint32_t *c, int x, int y, int facing_left)
{
    static const char *shape[5] = {"..aaaa..", "aabbbbbf", "abbbbbbbf", "aabbbbbf", "..aaaa.."};
    for (int j = 0; j < 5; ++j)
        for (int i = 0; shape[j][i]; ++i) {
            char p = shape[j][i];
            if (p == '.') continue;
            uint32_t color = p == 'a' ? 0xffa06020 : p == 'b' ? 0xffffd060 : 0xfffff8d0;
            put(c, facing_left ? x + 8 - i : x + i, y + j, color);
        }
}

static void arrow(uint32_t *c, int x, int y, int left, uint32_t color)
{
    for (int j = 0; j < 7; ++j) {
        int span = j < 4 ? j : 6 - j;
        for (int i = 0; i <= span; ++i) put(c, left ? x + 3 - i : x + i, y + j, color);
    }
}

static void arrow_up(uint32_t *c, int x, int y, uint32_t color)
{
    for (int j = 0; j < 4; ++j)
        for (int i = -j; i <= j; ++i) put(c, x + i, y + j, color);
}

static void arrow_down(uint32_t *c, int x, int y, uint32_t color)
{
    for (int j = 0; j < 4; ++j)
        for (int i = -(3 - j); i <= 3 - j; ++i) put(c, x + i, y + j, color);
}

/* ---- items --------------------------------------------------------------- */

typedef struct { unsigned id; const char *label; const char *help; } Item;

static unsigned page_items(const WorldMenu *m, Item *items)
{
    unsigned n = 0;
#define ADD(i, l, h) (items[n].id = (i), items[n].label = (l), items[n].help = (h), ++n)
    switch (m->page) {
    case PAGE_MAIN:
        if (m->title) {
            ADD(ITEM_START, "START GAME", "INSERT A COIN AND DIVE IN");
            ADD(ITEM_OPTIONS, "OPTIONS", "VIDEO, SOUND, GAME AND CONTROLS");
            ADD(ITEM_QUIT, "EXIT", "CLOSE IN THE HUNT");
        } else {
            ADD(ITEM_RESUME, "RESUME", "BACK TO THE HUNT");
            ADD(ITEM_OPTIONS, "OPTIONS", "VIDEO, SOUND, GAME AND CONTROLS");
            ADD(ITEM_QUIT, "EXIT TO MENU", "END THIS GAME");
        }
        break;
    case PAGE_OPTIONS:
        ADD(ITEM_TABS, "", "LEFT/RIGHT CHANGES THE PAGE");
        if (m->tab == TAB_VIDEO) {
            ADD(ITEM_DISPLAY, "DISPLAY", "WINDOW OR FULL SCREEN");
            ADD(ITEM_SCALE, "WINDOW SIZE", "SHARP X1 TO X8 PIXEL SCALING");
            ADD(ITEM_SCALING, "SCALING", "INTEGER IS SHARPEST");
            ADD(ITEM_CRT, "CRT SHADER", "TUBE SIMULATION. GPU");
            ADD(ITEM_PACING, "FRAME PACING", "SMOOTH LOCKS FRAMES TO THE DISPLAY");
            ADD(ITEM_VSYNC, "V-SYNC", "TIMER PACING ONLY");
            ADD(ITEM_FPS, "FPS LIMIT", "TIMER PACING ONLY. REDRAW RATE");
            ADD(ITEM_SHOWFPS, "SHOW FPS", "FRAME RATE AND GAME SPEED");
            ADD(ITEM_BORDER, "BORDER ART", "ARTWORK AROUND THE PICTURE");
            ADD(ITEM_LIGHT, "BORDER LIGHT", "BRIGHTNESS OF THE BORDER ART");
        } else if (m->tab == TAB_SOUND) {
            ADD(ITEM_VOLUME, "VOLUME", "MASTER VOLUME. 0 IS MUTE");
            ADD(ITEM_FM, "FM SYNTH", "YM2151 MUSIC LEVEL");
            ADD(ITEM_PCM, "PCM SAMPLES", "GA20 SAMPLE LEVEL");
        } else if (m->tab == TAB_GAME) {
            ADD(ITEM_SLOWDOWN, "SLOWDOWN", "ARCADE KEEPS ORIGINAL SLOWDOWN");
            ADD(ITEM_DIFFICULTY, "DIFFICULTY", "CABINET SWITCH. NEXT NEW GAME");
            ADD(ITEM_LIVES, "SUBMARINES", "CABINET SWITCH. NEXT NEW GAME");
            ADD(ITEM_DEMOSOUND, "DEMO SOUND", "CABINET SWITCH. NEXT NEW GAME");
            ADD(ITEM_FOCUS, "AUTO PAUSE", "PAUSE WHEN THE WINDOW LOSES FOCUS");
            ADD(ITEM_CONTROLS, "CONTROLS", "KEYBOARD AND GAMEPAD LAYOUT");
            ADD(ITEM_DEFAULTS, "RESET OPTIONS", "RESTORE ALL OPTIONS");
        } else {
            ADD(ITEM_FX, "ENHANCEMENTS", "MASTER SWITCH. OFF IS PURE ARCADE");
            ADD(ITEM_FX_CAUSTICS, "CAUSTICS", "RIPPLING LIGHT IN OPEN WATER");
            ADD(ITEM_FX_RAYS, "LIGHT SHAFTS", "SUNBEAMS FROM THE SURFACE");
            ADD(ITEM_FX_LIGHTS, "DYNAMIC LIGHTS", "BLASTS LIGHT UP THE SCENE");
            ADD(ITEM_FX_BLOOM, "BLOOM", "SOFT GLOW ON BRIGHT PIXELS");
            ADD(ITEM_FX_DEPTH, "DEPTH SHADING", "DEEPER WATER IS DARKER");
            ADD(ITEM_FX_SNOW, "MARINE SNOW", "DRIFTING PARTICLES IN THE WATER");
            ADD(ITEM_FX_SHIMMER, "WATER SHIMMER", "GENTLE REFRACTION WOBBLE");
            ADD(ITEM_FX_VIVID, "VIVID COLOR", "RICHER COLOUR AND CONTRAST");
            ADD(ITEM_FX_VIGNETTE, "VIGNETTE", "DARKER CORNERS");
            ADD(ITEM_FX_TRAILS, "MOTION TRAILS", "PHOSPHOR STYLE PERSISTENCE");
            ADD(ITEM_FX_SCANLINES, "SCANLINES", "CRT LINES. INTEGER SCALING X2+");
            ADD(ITEM_FX_SMOOTH, "SMOOTH SCROLL", "SUBPIXEL CAMERA. NO CRT SHADER");
            ADD(ITEM_FX_INTERP, "MOTION X2", "IN-BETWEEN FRAMES AT 120 HZ+");
            ADD(ITEM_FX_SHADOWS, "DROP SHADOWS", "SPRITES SHADE THE SCENERY");
            ADD(ITEM_FX_HAZE, "DEPTH HAZE", "FAR SCENERY FADES TO BLUE");
            ADD(ITEM_FX_DOF, "DEPTH OF FIELD", "SOFT FAR BACKGROUND");
            ADD(ITEM_FX_RELIEF, "RELIEF LIGHT", "EMBOSSED ROCK AND ICE");
            ADD(ITEM_FX_SURFACE, "LIVE SURFACE", "RIPPLING, GLINTING WATERLINE");
            ADD(ITEM_FX_SHOCK, "SHOCKWAVES", "BLASTS BEND THE WATER");
            ADD(ITEM_FX_PARTICLES, "PARTICLES", "EMBERS AND BUBBLES FROM BLASTS");
            ADD(ITEM_FX_SHAKE, "SHAKE + RUMBLE", "BLASTS SHAKE SCREEN AND PAD");
            ADD(ITEM_FX_STEREO, "STEREO 3D", "RED-CYAN GLASSES");
        }
        ADD(ITEM_BACK, "BACK", m->title ? "RETURN TO THE TITLE MENU" : "RETURN TO THE PAUSE MENU");
        break;
    case PAGE_CONTROLS:
        ADD(ITEM_BACK, "BACK", "");
        break;
    default:
        ADD(ITEM_NO, "NO", m->title ? "STAY" : "KEEP PLAYING");
        ADD(ITEM_YES, "YES", m->title ? "CLOSE IN THE HUNT" : "END THIS GAME");
        break;
    }
#undef ADD
    return n;
}

static int item_disabled(const WorldMenu *m, const WorldSettings *s, unsigned id)
{
    if (id == ITEM_SCALE) return s->fullscreen != 0;
    if (id == ITEM_LIGHT) return m->border_index == 0;
    if (id == ITEM_VSYNC || id == ITEM_FPS) return s->pacing != 2;
    if ((id > ITEM_FX && id <= ITEM_FX_SCANLINES) || (id >= ITEM_FX_SHADOWS && id <= ITEM_FX_INTERP))
        return !s->fx_enable;
    return 0;
}

static void item_value(const WorldMenu *m, const WorldSettings *s, unsigned id, char out[32])
{
    out[0] = 0;
    switch (id) {
    case ITEM_DISPLAY: strcpy(out, s->fullscreen ? "FULL SCREEN" : "WINDOW"); break;
    case ITEM_SCALE: snprintf(out, 32, "X%u", s->window_scale); break;
    case ITEM_BORDER:
        snprintf(out, 32, "%s", m->border_index < m->border_count ? m->border_name[m->border_index] : "BLACK");
        break;
    case ITEM_SLOWDOWN: strcpy(out, s->timing_mode == WORLD_TIMING_ARCADE ? "ARCADE" : "OFF"); break;
    case ITEM_SCALING: strcpy(out, s->scaling == 0 ? "INTEGER" : s->scaling == 1 ? "FIT 4:3" : "STRETCH"); break;
    case ITEM_PACING: strcpy(out, s->pacing == 0 ? "SMOOTH" : s->pacing == 1 ? "EXACT 60.01" : "TIMER"); break;
    case ITEM_FPS:
        if (!s->fps_limit) strcpy(out, "ARCADE");
        else if (s->fps_limit == WORLD_MENU_FPS_CHOICES - 1) strcpy(out, "UNCAPPED");
        else snprintf(out, 32, "%u", world_menu_fps[s->fps_limit]);
        break;
    case ITEM_LIVES: snprintf(out, 32, "%u", s->dip_lives + 2); break;
    case ITEM_CRT:
        snprintf(out, 32, "%s", s->crt_shader && s->crt_shader <= WORLD_GPU_PRESETS
                                ? world_gpu_preset_name[s->crt_shader - 1] : "OFF");
        break;
    case ITEM_DIFFICULTY: {
        static const char *const names[4] = {"VERY EASY", "EASY", "NORMAL", "HARD"};
        strcpy(out, names[s->dip_difficulty & 3]);
        break;
    }
    default: {
        unsigned *field = toggle_field((WorldSettings *)s, id);
        if (field) strcpy(out, *field ? "ON" : "OFF");
        break;
    }
    }
}

static int slider(unsigned id, const WorldSettings *s)
{
    if (id == ITEM_VOLUME) return (int)s->volume;
    if (id == ITEM_LIGHT) return (int)s->border_brightness;
    if (id == ITEM_FM) return (int)s->fm_volume;
    if (id == ITEM_PCM) return (int)s->pcm_volume;
    return -1;
}

void world_menu_set_borders(WorldMenu *m, unsigned count, const char *const names[],
                            const char *const paths[], const WorldSettings *settings)
{
    if (count > WORLD_MENU_BORDER_LIMIT) count = WORLD_MENU_BORDER_LIMIT;
    m->border_count = count;
    m->border_index = 0;
    for (unsigned i = 0; i < count; ++i) {
        snprintf(m->border_name[i], sizeof m->border_name[i], "%s", names[i]);
        snprintf(m->border_path[i], sizeof m->border_path[i], "%s", paths[i] ? paths[i] : "");
        if (i && strcmp(settings->border, "black") && !strcmp(paths[i], settings->border_image))
            m->border_index = i;
    }
}

void world_menu_open(WorldMenu *m, unsigned held, int gamepad, uint32_t now_ms)
{
    m->open = 1;
    m->page = PAGE_MAIN;
    m->row = 0;
    m->previous = held;
    m->repeat_buttons = 0;
    m->gamepad = !!gamepad;
    m->dirty = 0;
    m->peek = 0;
    m->opened_at = now_ms;
    m->title = 0;
}

void world_menu_open_title(WorldMenu *m, unsigned held, int gamepad, uint32_t now_ms)
{
    world_menu_open(m, held, gamepad, now_ms);
    m->title = 1;
}

/* The row of the CONTROLS entry on the options GAME tab. */
enum { CONTROLS_ROW = 6 };

static unsigned step_percent(unsigned value, int direction)
{
    if (direction > 0) return value >= 95 ? 100 : value + 5;
    return value <= 5 ? 0 : value - 5;
}

static unsigned adjust(WorldMenu *m, WorldSettings *s, unsigned id, int d)
{
    switch (id) {
    case ITEM_DISPLAY: s->fullscreen = !s->fullscreen; return WORLD_MENU_DISPLAY;
    case ITEM_SCALE: {
        unsigned scale = s->window_scale + (d > 0 ? 1 : -1);
        if (s->fullscreen || scale < 1 || scale > 8) return 0;
        s->window_scale = scale;
        return WORLD_MENU_DISPLAY;
    }
    case ITEM_BORDER: {
        if (m->border_count < 2) return 0;
        m->border_index = (m->border_index + m->border_count + (d > 0 ? 1 : -1)) % m->border_count;
        if (!m->border_index) strcpy(s->border, "black");
        else {
            strcpy(s->border, "custom");
            snprintf(s->border_image, sizeof s->border_image, "%s", m->border_path[m->border_index]);
        }
        return WORLD_MENU_BORDER;
    }
    case ITEM_LIGHT: {
        if (!m->border_index) return 0;
        unsigned v = step_percent(s->border_brightness, d);
        if (v == s->border_brightness) return 0;
        s->border_brightness = v;
        return WORLD_MENU_BORDER;
    }
    case ITEM_VOLUME: {
        unsigned v = step_percent(s->volume, d);
        if (v == s->volume) return 0;
        s->volume = v;
        return WORLD_MENU_VOLUME;
    }
    case ITEM_SLOWDOWN:
        s->timing_mode = s->timing_mode == WORLD_TIMING_ARCADE ? WORLD_TIMING_REMOVE_SLOWDOWN : WORLD_TIMING_ARCADE;
        return WORLD_MENU_TIMING;
    case ITEM_FM: case ITEM_PCM: {
        unsigned *level = id == ITEM_FM ? &s->fm_volume : &s->pcm_volume;
        unsigned v = step_percent(*level, d);
        if (v == *level) return 0;
        *level = v;
        return WORLD_MENU_VOLUME;
    }
    case ITEM_SCALING: s->scaling = (s->scaling + 3 + (d > 0 ? 1 : -1)) % 3; return WORLD_MENU_VIEW;
    case ITEM_PACING: s->pacing = (s->pacing + 3 + (d > 0 ? 1 : -1)) % 3; return WORLD_MENU_VIEW;
    case ITEM_FPS:
        if (s->pacing != 2) return 0;
        s->fps_limit = (s->fps_limit + WORLD_MENU_FPS_CHOICES + (d > 0 ? 1 : -1)) % WORLD_MENU_FPS_CHOICES;
        return WORLD_MENU_VIEW;
    case ITEM_LIVES: s->dip_lives = (s->dip_lives + 4 + (d > 0 ? 1 : -1)) % 4; return WORLD_MENU_VIEW;
    case ITEM_CRT:
        s->crt_shader = (s->crt_shader + WORLD_GPU_PRESETS + 1 + (d > 0 ? 1 : -1)) % (WORLD_GPU_PRESETS + 1);
        return WORLD_MENU_VIEW;
    case ITEM_DIFFICULTY: s->dip_difficulty = (s->dip_difficulty + 4 + (d > 0 ? 1 : -1)) % 4; return WORLD_MENU_VIEW;
    default: {
        unsigned *field = toggle_field(s, id);
        if (!field || item_disabled(m, s, id)) return 0;
        *field = !*field;
        return WORLD_MENU_VIEW;
    }
    }
}

static void go(WorldMenu *m, unsigned page, unsigned row)
{
    m->rows[m->page] = m->row;
    m->page = page;
    m->row = row;
}

unsigned world_menu_update(WorldMenu *m, WorldSettings *s, unsigned held, int gamepad, uint32_t now_ms)
{
    if (!m->open) return 0;
    unsigned pressed = held & ~m->previous;
    if (pressed) m->gamepad = !!gamepad;
    unsigned moving = held & (WORLD_MENU_UP | WORLD_MENU_DOWN | WORLD_MENU_LEFT | WORLD_MENU_RIGHT);
    if (moving != m->repeat_buttons) {
        m->repeat_buttons = moving;
        m->repeat_at = now_ms + 300;
    } else if (moving && (int32_t)(now_ms - m->repeat_at) >= 0) {
        pressed |= moving;
        m->repeat_at = now_ms + 70;
    }
    m->previous = held;

    Item items[MAX_ITEMS];
    unsigned count = page_items(m, items), result = 0;
    if (m->row >= count) m->row = count - 1;
    m->peek = m->page == PAGE_OPTIONS && m->tab == TAB_ENHANCE && (held & WORLD_MENU_PEEK);

    if (pressed & WORLD_MENU_BACK) {
        if (m->page == PAGE_MAIN) {
            if (m->title) return 0;
            m->open = 0;
            return WORLD_MENU_RESUME | (m->dirty ? WORLD_MENU_SAVE : 0);
        }
        if (m->page == PAGE_CONTROLS) { m->tab = TAB_GAME; go(m, PAGE_OPTIONS, CONTROLS_ROW); }
        else go(m, PAGE_MAIN, m->page == PAGE_OPTIONS ? 1 : 2);
        return 0;
    }
    if (m->page == PAGE_OPTIONS && (pressed & (WORLD_MENU_TAB_PREV | WORLD_MENU_TAB_NEXT))) {
        int d = (pressed & WORLD_MENU_TAB_NEXT) ? 1 : -1;
        m->tab = (m->tab + TAB_COUNT + (unsigned)d) % TAB_COUNT;
        m->row = 0;
        return 0;
    }
    if (pressed & WORLD_MENU_UP) {
        do m->row = m->row ? m->row - 1 : count - 1;
        while (item_disabled(m, s, items[m->row].id) && count > 1);
    }
    if (pressed & WORLD_MENU_DOWN) {
        do m->row = (m->row + 1) % count;
        while (item_disabled(m, s, items[m->row].id) && count > 1);
    }
    unsigned id = items[m->row].id;
    int horizontal = (pressed & WORLD_MENU_RIGHT) ? 1 : (pressed & WORLD_MENU_LEFT) ? -1 : 0;
    if (horizontal) {
        if (id == ITEM_TABS) {
            m->tab = (m->tab + TAB_COUNT + (unsigned)horizontal) % TAB_COUNT;
            return 0;
        }
        if (m->page == PAGE_QUIT) { m->row ^= 1; return 0; }
        result = adjust(m, s, id, horizontal);
    } else if (pressed & WORLD_MENU_ACCEPT) {
        switch (id) {
        case ITEM_RESUME: m->open = 0; return WORLD_MENU_RESUME | (m->dirty ? WORLD_MENU_SAVE : 0);
        case ITEM_START: m->open = 0; return WORLD_MENU_START | (m->dirty ? WORLD_MENU_SAVE : 0);
        case ITEM_OPTIONS: go(m, PAGE_OPTIONS, m->rows[PAGE_OPTIONS] ? m->rows[PAGE_OPTIONS] : 1); return 0;
        case ITEM_CONTROLS: go(m, PAGE_CONTROLS, 0); return 0;
        case ITEM_QUIT: go(m, PAGE_QUIT, 0); return 0;
        case ITEM_NO: go(m, PAGE_MAIN, 2); return 0;
        case ITEM_YES:
            if (m->title) return WORLD_MENU_QUIT | (m->dirty ? WORLD_MENU_SAVE : 0);
            m->open = 0;
            return WORLD_MENU_TITLE | (m->dirty ? WORLD_MENU_SAVE : 0);
        case ITEM_BACK:
            if (m->page == PAGE_CONTROLS) { m->tab = TAB_GAME; go(m, PAGE_OPTIONS, CONTROLS_ROW); }
            else go(m, PAGE_MAIN, 1);
            return 0;
        case ITEM_TABS: m->tab = (m->tab + 1) % TAB_COUNT; return 0;
        case ITEM_DEFAULTS: {
            WorldSettings fresh;
            world_settings_defaults(&fresh);
            fresh.bindings = s->bindings;
            *s = fresh;
            m->border_index = 0;
            result = WORLD_MENU_VOLUME | WORLD_MENU_DISPLAY | WORLD_MENU_BORDER | WORLD_MENU_TIMING | WORLD_MENU_VIEW;
            break;
        }
        default:
            if (slider(id, s) < 0) result = adjust(m, s, id, 1);
            break;
        }
    }
    if (result) m->dirty = 1;
    return result;
}

/* ---- rendering ----------------------------------------------------------- */

static void backdrop(uint32_t *c, uint32_t now_ms)
{
    /* The paused frame, recoloured into the deep-water blues of the game and
     * crossed by a slow sonar sweep. */
    int sweep = (int)((now_ms / 14) % (H + 60)) - 30;
    for (int y = 0; y < H; ++y) {
        int distance = y - sweep;
        if (distance < 0) distance = -distance;
        unsigned glow = distance < 24 ? (unsigned)(24 - distance) * 3 : 0;
        for (int x = 0; x < W; ++x) {
            uint32_t p = c[y * W + x];
            unsigned l = (((p >> 16) & 255) * 3 + ((p >> 8) & 255) * 5 + (p & 255) * 2) / 10;
            unsigned r = l / 7, g = l / 3 + 10 + glow / 2, b = l / 2 + 26 + glow;
            if (y & 1) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            c[y * W + x] = 0xff000000u | r << 16 | g << 8 | b;
        }
    }
}

static void panel(uint32_t *c, int x, int y, int w, int h)
{
    shade(c, x, y, w, h, 0xff040e1e, 200);
    /* Hull plating: brass rim, dark seam, teal inner line, rivets. */
    fill(c, x, y, w, 1, 0xffd8a848); fill(c, x, y + h - 1, w, 1, 0xff8a6224);
    fill(c, x, y, 1, h, 0xffc89838); fill(c, x + w - 1, y, 1, h, 0xff8a6224);
    fill(c, x + 1, y + 1, w - 2, 1, 0xff4a3010); fill(c, x + 1, y + h - 2, w - 2, 1, 0xff4a3010);
    fill(c, x + 1, y + 1, 1, h - 2, 0xff4a3010); fill(c, x + w - 2, y + 1, 1, h - 2, 0xff4a3010);
    fill(c, x + 3, y + 3, w - 6, 1, 0xff2a7a96); fill(c, x + 3, y + h - 4, w - 6, 1, 0xff1a4a60);
    fill(c, x + 3, y + 3, 1, h - 6, 0xff2a7a96); fill(c, x + w - 4, y + 3, 1, h - 6, 0xff1a4a60);
    static const int rivet[4][2] = {{6, 6}, {-8, 6}, {6, -8}, {-8, -8}};
    for (unsigned i = 0; i < 4; ++i) {
        int rx = rivet[i][0] < 0 ? x + w + rivet[i][0] : x + rivet[i][0];
        int ry = rivet[i][1] < 0 ? y + h + rivet[i][1] : y + rivet[i][1];
        fill(c, rx, ry, 2, 2, 0xffe8c070);
        put(c, rx + 1, ry + 1, 0xff6a4818);
    }
}

static void rule(uint32_t *c, int y)
{
    for (int x = 40; x < W - 40; ++x) {
        unsigned edge = (unsigned)(x < 100 ? x - 40 : x > W - 100 ? W - 40 - x : 60);
        put(c, x, y, mix(0xff04101e, 0xffd8a848, edge * 4 > 256 ? 256 : edge * 4));
        put(c, x, y + 1, mix(0xff04101e, 0xff5a3c14, edge * 4 > 256 ? 256 : edge * 4));
    }
}

static void bar(uint32_t *c, int x, int y, int percent, int enabled)
{
    for (int i = 0; i < 10; ++i) {
        int on = percent >= (i + 1) * 10 || (percent > i * 10 && percent % 10 >= 5);
        uint32_t top = on ? (enabled ? 0xffffe070 : 0xff8a8a70) : 0xff16324a;
        uint32_t bottom = on ? (enabled ? 0xffc06820 : 0xff5a5a48) : 0xff0c1c2c;
        for (int j = 0; j < 7; ++j) fill(c, x + i * 7, y + j, 5, 1, mix(top, bottom, (unsigned)j * 36));
    }
}

static const char *hints(const WorldMenu *m)
{
    if (m->page == PAGE_OPTIONS && m->tab == TAB_ENHANCE)
        return m->gamepad ? "HOLD Y PREVIEW  B BACK" : "HOLD P PREVIEW  ESC BACK";
    if (m->gamepad)
        return m->page == PAGE_OPTIONS ? "A OK  B BACK  LB RB PAGE"
             : m->title ? "A OK  B BACK" : "A OK  B BACK  START RESUME";
    return m->page == PAGE_OPTIONS ? "ENTER OK  ESC BACK  Q E PAGE"
         : m->title ? "ENTER OK  ESC BACK" : "ENTER OK  ESC BACK  F1 RESUME";
}

static void controls_page(uint32_t *c, const uint8_t *gfx1, const Ramp *head, const Ramp *body, const Ramp *value)
{
    static const char *rows[][3] = {
        {"MOVE", "ARROWS", "D-PAD"},
        {"TORPEDO", "Z", "A"},
        {"MISSILE", "X", "B"},
        {"START", "1", "A"},
        {"COIN", "5", "BACK"},
        {"PLAYER 2", "WASD G H", "PAD 2"},
        {"PAUSE MENU", "F1/ESC", "START"},
    };
    text(c, gfx1, 40, 66, "ACTION", head);
    text(c, gfx1, 136, 66, "KEYS", head);
    text(c, gfx1, 216, 66, "PAD", head);
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
        int y = 82 + (int)i * 14;
        if (i & 1) shade(c, 32, y - 3, W - 64, 13, 0xff0e2a40, 120);
        text(c, gfx1, 40, y, rows[i][0], body);
        text(c, gfx1, 136, y, rows[i][1], value);
        text(c, gfx1, 216, y, rows[i][2], value);
    }
}

/* The title menu: the attract sequence (with the game's own logo) keeps
   playing, the choices sit on a panel over its coin prompt. */
static void title_screen(const WorldMenu *m, uint32_t *c, const uint8_t *gfx1, uint32_t now_ms)
{
    Ramp normal = steel(), picked = gold();
    for (int y = 120; y < H; ++y)                       /* darken toward the bottom */
        shade(c, 0, y, W, 1, 0xff020a16, (unsigned)(y - 120));
    fill(c, 96, 140, W - 192, 72, 0xff040e1e);
    panel(c, 96, 140, W - 192, 72);
    Item items[MAX_ITEMS];
    unsigned count = page_items(m, items);
    int bob = (int)((now_ms / 120) % 4);
    bob = bob == 3 ? 1 : bob;
    int y = 153;
    for (unsigned i = 0; i < count; ++i, y += 18) {
        int here = i == m->row;
        if (here) {
            shade(c, 103, y - 4, W - 206, 15, 0xff1c4c6c, 230);
            fill(c, 103, y - 4, 2, 15, 0xffd8a848);
            torpedo(c, 110 + bob, y + 1, 0);
        }
        text(c, gfx1, (W - text_px(items[i].label)) / 2 + 6, y, items[i].label, here ? &picked : &normal);
    }
}

void world_menu_render(const WorldMenu *m, const WorldSettings *s, uint32_t *c,
                       const uint8_t *gfx1, uint32_t now_ms)
{
    if (!m->open || m->peek || !c || !gfx1) return;
    if (m->title && m->page == PAGE_MAIN) { title_screen(m, c, gfx1, now_ms); return; }
    Ramp title = gold(), normal = steel(), picked = gold(), value = sea(), faint = dim();
    backdrop(c, now_ms);
    int top = m->page == PAGE_QUIT ? 66 : 18, bottom = m->page == PAGE_QUIT ? 174 : 224;
    panel(c, 20, top, W - 40, bottom - top);

    static const char *titles[] = {"PAUSED", "OPTIONS", "CONTROLS", "EXIT TO MENU"};
    big_text_center(c, gfx1, top + 10, m->title && m->page == PAGE_QUIT ? "EXIT GAME" : titles[m->page], &title);
    rule(c, top + 30);

    Item items[MAX_ITEMS];
    unsigned count = page_items(m, items);
    /* The options list scrolls: six rows show, centred on the cursor. */
    unsigned listed = count ? count - 1 : 0, first = 0;
    if (m->page == PAGE_OPTIONS && listed > VISIBLE_ROWS) {
        unsigned cursor = m->row ? m->row - 1 : 0;
        first = cursor > 3 ? cursor - 3 : 0;
        if (first > listed - VISIBLE_ROWS) first = listed - VISIBLE_ROWS;
    }
    int bob = (int)((now_ms / 120) % 4);
    bob = bob == 3 ? 1 : bob;

    if (m->page == PAGE_CONTROLS) controls_page(c, gfx1, &title, &normal, &value);

    int y = m->page == PAGE_MAIN ? 74 : m->page == PAGE_OPTIONS ? 58 : m->page == PAGE_QUIT ? top + 46 : 188;
    int step = m->page == PAGE_MAIN ? 22 : m->page == PAGE_OPTIONS ? ROW_STEP : 18;
    for (unsigned i = 0; i < count; ++i) {
        unsigned id = items[i].id;
        int here = i == m->row;
        if (id == ITEM_TABS) {
            static const char *names[TAB_COUNT] = {"VIDEO", "SOUND", "GAME", "ENHANCE"};
            int x = 44;
            for (unsigned t = 0; t < TAB_COUNT; ++t) {
                int w = text_px(names[t]) + 10, active = t == m->tab;
                if (active) {
                    shade(c, x, y - 4, w, 15, here ? 0xff1c4c6c : 0xff12324a, 230);
                    fill(c, x, y + 10, w, 1, 0xffd8a848);
                }
                text(c, gfx1, x + 5, y, names[t], active ? (here ? &picked : &value) : &faint);
                x += w + 6;
            }
            if (here) {
                arrow(c, 32, y, 1, 0xffd8a848);
                arrow(c, W - 36, y, 0, 0xffd8a848);
            }
            y += 24;
            if (first) arrow_up(c, W - 34, y - 9, 0xffd8a848);
            continue;
        }
        if (m->page == PAGE_OPTIONS) {
            if (i - 1 < first || i - 1 >= first + VISIBLE_ROWS) continue;
            if (i - 1 == first + VISIBLE_ROWS - 1 && first + VISIBLE_ROWS < listed)
                arrow_down(c, W - 34, y + 11, 0xffd8a848);
        }
        int disabled = item_disabled(m, s, id);
        if (m->page == PAGE_QUIT) {
            int x = i ? 176 : 112;
            if (here) {
                shade(c, x - 10, y - 5, 48, 17, 0xff1c4c6c, 230);
                torpedo(c, x - 22 + bob, y + 1, 0);
            }
            text(c, gfx1, x + 4, y, items[i].label, here ? &picked : &normal);
            continue;
        }
        if (here) {
            int tall = m->page == PAGE_OPTIONS ? ROW_STEP : 17, lift = m->page == PAGE_OPTIONS ? 4 : 5;
            shade(c, 30, y - lift, W - 60, tall, 0xff1c4c6c, 220);
            fill(c, 30, y - lift, 2, tall, 0xffd8a848);
            torpedo(c, 36 + bob, y + 1, 0);
        }
        int label_x = m->page == PAGE_MAIN ? (W - text_px(items[i].label)) / 2 : 52;
        text(c, gfx1, label_x, y, items[i].label, disabled ? &faint : here ? &picked : &normal);
        int percent = slider(id, s);
        char shown[32];
        item_value(m, s, id, shown);
        if (percent >= 0) {
            bar(c, 172, y, percent, !disabled);
            char digits[8];
            snprintf(digits, sizeof digits, "%d", percent);
            text(c, gfx1, 272 - text_px(digits) + 8, y, digits, disabled ? &faint : &value);
        } else if (*shown) {
            int x = 268 - text_px(shown);
            text(c, gfx1, x, y, shown, disabled ? &faint : here ? &picked : &value);
            if (here && !disabled) {
                arrow(c, x - 10, y, 1, 0xffd8a848);
                arrow(c, 272, y, 0, 0xffd8a848);
            }
        }
        y += step;
    }

    if (m->page != PAGE_CONTROLS && m->row < count && *items[m->row].help) {
        int help_y = m->page == PAGE_QUIT ? bottom - 22 : 182;
        if (m->page != PAGE_QUIT) shade(c, 30, help_y - 4, W - 60, 15, 0xff000000, 90);
        text_center(c, gfx1, help_y, items[m->row].help, &faint);
    }
    if (m->page != PAGE_QUIT) {
        rule(c, 199);
        text_center(c, gfx1, 207, hints(m), &value);
    }
}

/* ---- 5x7 host lettering (achievement view) ------------------------------ */

static const uint8_t alphabet[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const uint8_t numerals[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30}, {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static unsigned glyph_row(unsigned c, unsigned row)
{
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return alphabet[c - 'A'][row];
    if (c >= '0' && c <= '9') return numerals[c - '0'][row];
    switch (c) {
    case ' ': return 0;
    case '-': return row == 3 ? 14 : 0;
    case '.': return row == 6 ? 4 : 0;
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '/': return 1u << (4 - (row * 4 / 6));
    case '!': return row < 4 || row == 6 ? 4 : 0;
    default: return 0;
    }
}

int world_menu_text_width(const char *t, unsigned scale)
{
    if (!t || !*t) return 0;
    return (int)((strlen(t) * 6 - 1) * scale);
}

void world_menu_draw_text(uint32_t *pixels, unsigned width, unsigned height,
                          int x, int y, unsigned scale, uint32_t color, const char *t)
{
    if (!pixels || !t || !scale) return;
    for (; *t; ++t, x += (int)(6 * scale))
        for (unsigned row = 0; row < 7; ++row) {
            unsigned bits = glyph_row((unsigned char)*t, row);
            for (unsigned col = 0; col < 5; ++col) {
                if (!(bits & (16u >> col))) continue;
                for (unsigned dy = 0; dy < scale; ++dy)
                    for (unsigned dx = 0; dx < scale; ++dx) {
                        int px = x + (int)(col * scale + dx), py = y + (int)(row * scale + dy);
                        if (px >= 0 && py >= 0 && (unsigned)px < width && (unsigned)py < height)
                            pixels[(unsigned)py * width + (unsigned)px] = color;
                    }
            }
        }
}
