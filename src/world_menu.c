#include "world_menu.h"
#include <stdio.h>
#include <string.h>

/* Pause and options overlay for the native port, drawn at the game's own
 * 320x240 resolution with the arcade's 8x8 and 16x16 lettering from gfx1.
 * The game's font uses pen 1 for the face, A..E for a top-to-bottom shade
 * and F for the drop shadow, so each style below supplies those colours. */

enum { W = 320, H = 240 };
enum { PAGE_MAIN, PAGE_OPTIONS, PAGE_CONTROLS, PAGE_QUIT };
enum { TAB_VIDEO, TAB_SOUND, TAB_GAME, TAB_COUNT };
enum {
    ITEM_RESUME, ITEM_OPTIONS, ITEM_CONTROLS, ITEM_QUIT,
    ITEM_DISPLAY, ITEM_SCALE, ITEM_BORDER, ITEM_LIGHT,
    ITEM_VOLUME, ITEM_SLOWDOWN, ITEM_DEFAULTS, ITEM_BACK,
    ITEM_NO, ITEM_YES, ITEM_TABS
};

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

/* ---- items --------------------------------------------------------------- */

typedef struct { unsigned id; const char *label; const char *help; } Item;

static unsigned page_items(const WorldMenu *m, Item *items)
{
    unsigned n = 0;
#define ADD(i, l, h) (items[n].id = (i), items[n].label = (l), items[n].help = (h), ++n)
    switch (m->page) {
    case PAGE_MAIN:
        ADD(ITEM_RESUME, "RESUME", "BACK TO THE HUNT");
        ADD(ITEM_OPTIONS, "OPTIONS", "VIDEO, SOUND AND GAME SETTINGS");
        ADD(ITEM_CONTROLS, "CONTROLS", "KEYBOARD AND GAMEPAD LAYOUT");
        ADD(ITEM_QUIT, "QUIT GAME", "CLOSE IN THE HUNT");
        break;
    case PAGE_OPTIONS:
        ADD(ITEM_TABS, "", "LEFT/RIGHT CHANGES THE PAGE");
        if (m->tab == TAB_VIDEO) {
            ADD(ITEM_DISPLAY, "DISPLAY", "WINDOW OR FULL SCREEN");
            ADD(ITEM_SCALE, "WINDOW SIZE", "SHARP X1 TO X8 PIXEL SCALING");
            ADD(ITEM_BORDER, "BORDER ART", "ARTWORK AROUND THE PICTURE");
            ADD(ITEM_LIGHT, "BORDER LIGHT", "BRIGHTNESS OF THE BORDER ART");
        } else if (m->tab == TAB_SOUND) {
            ADD(ITEM_VOLUME, "VOLUME", "MASTER VOLUME. 0 IS MUTE");
        } else {
            ADD(ITEM_SLOWDOWN, "SLOWDOWN", "ARCADE KEEPS ORIGINAL SLOWDOWN");
            ADD(ITEM_DEFAULTS, "RESET OPTIONS", "RESTORE ALL OPTIONS");
        }
        ADD(ITEM_BACK, "BACK", "RETURN TO THE PAUSE MENU");
        break;
    case PAGE_CONTROLS:
        ADD(ITEM_BACK, "BACK", "");
        break;
    default:
        ADD(ITEM_NO, "NO", "KEEP PLAYING");
        ADD(ITEM_YES, "YES", "CLOSE IN THE HUNT");
        break;
    }
#undef ADD
    return n;
}

static int item_disabled(const WorldMenu *m, const WorldSettings *s, unsigned id)
{
    if (id == ITEM_SCALE) return s->fullscreen != 0;
    if (id == ITEM_LIGHT) return m->border_index == 0;
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
    default: break;
    }
}

static int slider(unsigned id, const WorldSettings *s)
{
    if (id == ITEM_VOLUME) return (int)s->volume;
    if (id == ITEM_LIGHT) return (int)s->border_brightness;
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
    m->opened_at = now_ms;
}

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
    default: return 0;
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

    Item items[16];
    unsigned count = page_items(m, items), result = 0;
    if (m->row >= count) m->row = count - 1;

    if (pressed & WORLD_MENU_BACK) {
        if (m->page == PAGE_MAIN) { m->open = 0; return WORLD_MENU_RESUME | (m->dirty ? WORLD_MENU_SAVE : 0); }
        go(m, PAGE_MAIN, m->page == PAGE_OPTIONS ? 1 : m->page == PAGE_CONTROLS ? 2 : 3);
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
        case ITEM_OPTIONS: go(m, PAGE_OPTIONS, m->rows[PAGE_OPTIONS] ? m->rows[PAGE_OPTIONS] : 1); return 0;
        case ITEM_CONTROLS: go(m, PAGE_CONTROLS, 0); return 0;
        case ITEM_QUIT: go(m, PAGE_QUIT, 0); return 0;
        case ITEM_NO: go(m, PAGE_MAIN, 3); return 0;
        case ITEM_YES: return WORLD_MENU_QUIT | (m->dirty ? WORLD_MENU_SAVE : 0);
        case ITEM_BACK: go(m, PAGE_MAIN, m->page == PAGE_OPTIONS ? 1 : 2); return 0;
        case ITEM_TABS: m->tab = (m->tab + 1) % TAB_COUNT; return 0;
        case ITEM_DEFAULTS: {
            WorldSettings fresh;
            world_settings_defaults(&fresh);
            fresh.bindings = s->bindings;
            *s = fresh;
            m->border_index = 0;
            result = WORLD_MENU_VOLUME | WORLD_MENU_DISPLAY | WORLD_MENU_BORDER | WORLD_MENU_TIMING;
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
    if (m->gamepad)
        return m->page == PAGE_OPTIONS ? "A OK  B BACK  LB RB PAGE" : "A OK  B BACK  L3+R3 CLOSE";
    return m->page == PAGE_OPTIONS ? "ENTER OK  ESC BACK  Q E PAGE" : "ENTER OK  ESC BACK  F1 CLOSE";
}

static void controls_page(uint32_t *c, const uint8_t *gfx1, const Ramp *head, const Ramp *body, const Ramp *value)
{
    static const char *rows[][3] = {
        {"MOVE", "ARROWS", "D-PAD"},
        {"TORPEDO", "Z", "A"},
        {"MISSILE", "X", "B"},
        {"START", "1", "START"},
        {"COIN", "5", "BACK"},
        {"PLAYER 2", "WASD G H", "PAD 2"},
        {"PAUSE MENU", "F1/ESC", "L3+R3"},
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

void world_menu_render(const WorldMenu *m, const WorldSettings *s, uint32_t *c,
                       const uint8_t *gfx1, uint32_t now_ms)
{
    if (!m->open || !c || !gfx1) return;
    Ramp title = gold(), normal = steel(), picked = gold(), value = sea(), faint = dim();
    backdrop(c, now_ms);
    int top = m->page == PAGE_QUIT ? 66 : 18, bottom = m->page == PAGE_QUIT ? 174 : 224;
    panel(c, 20, top, W - 40, bottom - top);

    static const char *titles[] = {"PAUSED", "OPTIONS", "CONTROLS", "QUIT GAME"};
    big_text_center(c, gfx1, top + 10, titles[m->page], &title);
    rule(c, top + 30);

    Item items[16];
    unsigned count = page_items(m, items);
    int bob = (int)((now_ms / 120) % 4);
    bob = bob == 3 ? 1 : bob;

    if (m->page == PAGE_CONTROLS) controls_page(c, gfx1, &title, &normal, &value);

    int y = m->page == PAGE_MAIN ? 74 : m->page == PAGE_OPTIONS ? 58 : m->page == PAGE_QUIT ? top + 46 : 188;
    int step = m->page == PAGE_MAIN ? 22 : 18;
    for (unsigned i = 0; i < count; ++i) {
        unsigned id = items[i].id;
        int here = i == m->row;
        if (id == ITEM_TABS) {
            static const char *names[TAB_COUNT] = {"VIDEO", "SOUND", "GAME"};
            int x = 44;
            for (unsigned t = 0; t < TAB_COUNT; ++t) {
                int w = text_px(names[t]) + 16, active = t == m->tab;
                if (active) {
                    shade(c, x, y - 4, w, 15, here ? 0xff1c4c6c : 0xff12324a, 230);
                    fill(c, x, y + 10, w, 1, 0xffd8a848);
                }
                text(c, gfx1, x + 8, y, names[t], active ? (here ? &picked : &value) : &faint);
                x += w + 12;
            }
            if (here) {
                arrow(c, 32, y, 1, 0xffd8a848);
                arrow(c, W - 36, y, 0, 0xffd8a848);
            }
            y += 26;
            continue;
        }
        if (m->page == PAGE_OPTIONS && id == ITEM_BACK) y += 6;
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
            shade(c, 30, y - 5, W - 60, 17, 0xff1c4c6c, 220);
            fill(c, 30, y - 5, 2, 17, 0xffd8a848);
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
