#include "world_menu.h"
#include "world_crt.h"
#include <stdio.h>
#include <string.h>

enum { MENU_WIDTH = 320, MENU_HEIGHT = 240, ITEM_LIMIT = 10 };
enum { ITEM_START, ITEM_RESUME, ITEM_OPTIONS, ITEM_EXIT, ITEM_TITLE,
       ITEM_CONTROLS, ITEM_ACHIEVEMENTS, ITEM_BOSS_RUSH, ITEM_SCORE_ATTACK,
       ITEM_VOLUME, ITEM_TIMING, ITEM_BORDER, ITEM_BRIGHTNESS, ITEM_POPUPS, ITEM_BACK,
       ITEM_DISPLAY, ITEM_FULLSCREEN, ITEM_SCALE, ITEM_SHADER, ITEM_VSYNC };
enum { PAGE_MAIN, PAGE_OPTIONS, PAGE_DISPLAY };
/* locked is the reason a listed row cannot be used, or NULL. */
typedef struct { unsigned id; const char *label, *locked; } MenuItem;

/* Original fixed-width interface lettering, five columns by seven rows. */
static const uint8_t alphabet[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const uint8_t digits[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static unsigned glyph_row(unsigned c, unsigned row)
{
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return alphabet[c-'A'][row];
    if (c >= '0' && c <= '9') return digits[c-'0'][row];
    switch (c) {
    case ' ': return 0;
    case '-': return row == 3 ? 14 : 0;
    case '.': return row == 6 ? 4 : 0;
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '/': return 1u << (row*4/6);
    case '%': {
        static const uint8_t mark[7] = {25,26,2,4,8,11,19};
        return mark[row];
    }
    case '>': return row < 4 ? 1u << (3-row) : 1u << (row-3);
    case '<': return row < 4 ? 1u << row : 1u << (6-row);
    case '!': return row < 4 || row == 6 ? 4 : 0;
    default: {
        static const uint8_t unknown[7] = {14,17,1,2,4,0,4};
        return unknown[row];
    }
    }
}

int world_menu_text_width(const char *text, unsigned scale)
{
    if (!text || !*text) return 0;
    return (int)((strlen(text)*6-1)*scale);
}

void world_menu_draw_text(uint32_t *pixels, unsigned width, unsigned height,
                          int x, int y, unsigned scale, uint32_t color, const char *text)
{
    if (!pixels || !text || !scale) return;
    for (; *text; ++text, x += (int)(6*scale)) {
        for (unsigned row = 0; row < 7; ++row) {
            unsigned bits = glyph_row((unsigned char)*text, row);
            for (unsigned col = 0; col < 5; ++col) {
                if (!(bits & (16u >> col))) continue;
                for (unsigned dy = 0; dy < scale; ++dy)
                    for (unsigned dx = 0; dx < scale; ++dx) {
                        int px = x + (int)(col*scale+dx), py = y + (int)(row*scale+dy);
                        if (px >= 0 && py >= 0 && (unsigned)px < width && (unsigned)py < height)
                            pixels[(unsigned)py*width+(unsigned)px] = color;
                    }
            }
        }
    }
}

static void add_item(MenuItem *items, unsigned *count, unsigned id, const char *label)
{
    if (*count >= ITEM_LIMIT) return;
    items[*count].id = id;
    items[*count].label = label;
    items[*count].locked = NULL;
    ++*count;
}

/* A ranked mode is listed when it can start from this menu, or when the host
   asks for the modes to be shown; without its capability the row is locked. */
static void add_mode(const WorldMenu *menu, MenuItem *items, unsigned *count,
                     unsigned id, const char *label, unsigned capability, const char *reason)
{
    int available = (menu->capabilities & capability) != 0;
    if (!(menu->capabilities & WORLD_MENU_MODES) && (menu->paused || !available)) return;
    if (*count >= ITEM_LIMIT) return;
    add_item(items, count, id, label);
    if (!available) items[*count-1].locked = reason ? reason : "Not available in this build.";
}

static unsigned menu_items(const WorldMenu *menu, const WorldSettings *settings, MenuItem items[ITEM_LIMIT])
{
    unsigned count = 0, caps = menu->capabilities;
    if (menu->options == PAGE_DISPLAY) {
        if (caps & WORLD_MENU_DISPLAY) {
            add_item(items,&count,ITEM_FULLSCREEN,"FULLSCREEN");
            /* The window size only applies to a window. */
            if (!settings || !settings->fullscreen) add_item(items,&count,ITEM_SCALE,"WINDOW SIZE");
        }
        if (caps & WORLD_MENU_SHADERS) add_item(items,&count,ITEM_SHADER,"DISPLAY PROFILE");
        if (caps & WORLD_MENU_VSYNC) add_item(items,&count,ITEM_VSYNC,"VSYNC");
        if (caps & WORLD_MENU_BORDERS) {
            add_item(items,&count,ITEM_BORDER,"CABINET BORDER");
            add_item(items,&count,ITEM_BRIGHTNESS,"BORDER BRIGHTNESS");
        }
        add_item(items,&count,ITEM_BACK,"BACK");
    } else if (menu->options == PAGE_OPTIONS) {
        if (caps & (WORLD_MENU_DISPLAY|WORLD_MENU_SHADERS|WORLD_MENU_VSYNC|WORLD_MENU_BORDERS))
            add_item(items,&count,ITEM_DISPLAY,"DISPLAY");
        if (caps & WORLD_MENU_SOUND) add_item(items,&count,ITEM_VOLUME,"SOUND VOLUME");
        if (caps & WORLD_MENU_TIMING) add_item(items,&count,ITEM_TIMING,"SLOWDOWN");
        if (caps & WORLD_MENU_CONTROLS) add_item(items,&count,ITEM_CONTROLS,"CONTROLS");
        if (caps & WORLD_MENU_ACHIEVEMENTS) add_item(items,&count,ITEM_POPUPS,"ACHIEVEMENT POPUPS");
        add_item(items,&count,ITEM_BACK,"BACK");
    } else {
        /* Launch: the ways to play come first. Paused: resuming and settings do. */
        add_item(items,&count,menu->paused ? ITEM_RESUME : ITEM_START,menu->paused ? "RESUME" : "START");
        for (unsigned pass = 0; pass < 2; ++pass) {
            if (pass == menu->paused) {
                add_mode(menu,items,&count,ITEM_BOSS_RUSH,"BOSS RUSH",WORLD_MENU_BOSS_RUSH,menu->boss_rush_reason);
                add_mode(menu,items,&count,ITEM_SCORE_ATTACK,"SCORE ATTACK",WORLD_MENU_SCORE_ATTACK,menu->score_attack_reason);
            } else {
                add_item(items,&count,ITEM_OPTIONS,"OPTIONS");
                if (caps & WORLD_MENU_ACHIEVEMENTS) add_item(items,&count,ITEM_ACHIEVEMENTS,"ACHIEVEMENTS");
            }
        }
        if (menu->paused && (caps & WORLD_MENU_RETURN_TITLE)) add_item(items,&count,ITEM_TITLE,"RETURN TO TITLE");
        add_item(items,&count,ITEM_EXIT,"EXIT GAME");
    }
    return count;
}

void world_menu_open(WorldMenu *menu, int paused, unsigned capabilities,
                     unsigned held_buttons, uint32_t now_ms)
{
    memset(menu,0,sizeof *menu);
    menu->open = 1;
    menu->paused = !!paused;
    menu->capabilities = capabilities;
    menu->previous_buttons = held_buttons;
    menu->repeat_at = now_ms;
}

void world_menu_close(WorldMenu *menu)
{
    menu->open = 0;
}

void world_menu_set_mode_reasons(WorldMenu *menu, const char *boss_rush, const char *score_attack)
{
    menu->boss_rush_reason = boss_rush;
    menu->score_attack_reason = score_attack;
}

unsigned world_menu_buttons(const WorldInput *input)
{
    enum { STICK = 12000 };
    if (!input || !input->focused || input->capture_phase || input->muted_until_release) return 0;
    unsigned held = 0;
    if (input->keys[0x26]) held |= WORLD_MENU_UP;
    if (input->keys[0x28]) held |= WORLD_MENU_DOWN;
    if (input->keys[0x25]) held |= WORLD_MENU_LEFT;
    if (input->keys[0x27]) held |= WORLD_MENU_RIGHT;
    if (input->keys[0x0d]) held |= WORLD_MENU_ACCEPT;
    if (input->keys[0x1b]) held |= WORLD_MENU_BACK;
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player) {
        const WorldInputController *pad = &input->controllers[player];
        if (!pad->connected) continue;
        if ((pad->buttons & 1) || pad->left_y > STICK) held |= WORLD_MENU_UP;
        if ((pad->buttons & 2) || pad->left_y < -STICK) held |= WORLD_MENU_DOWN;
        if ((pad->buttons & 4) || pad->left_x < -STICK) held |= WORLD_MENU_LEFT;
        if ((pad->buttons & 8) || pad->left_x > STICK) held |= WORLD_MENU_RIGHT;
        if (pad->buttons & 0x1000) held |= WORLD_MENU_ACCEPT;
        if (pad->buttons & 0x2000) held |= WORLD_MENU_BACK;
    }
    return held;
}

static unsigned stepped(unsigned value, int direction)
{
    if (direction > 0) return value >= 95 ? 100 : value + 5;
    return value <= 5 ? 0 : value - 5;
}

static WorldMenuCommand adjust_item(unsigned id, WorldSettings *settings, int direction)
{
    switch (id) {
    case ITEM_VOLUME:
        if (settings->volume == stepped(settings->volume,direction)) return WORLD_MENU_NONE;
        settings->volume = stepped(settings->volume,direction);
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_BRIGHTNESS:
        if (settings->border_brightness == stepped(settings->border_brightness,direction)) return WORLD_MENU_NONE;
        settings->border_brightness = stepped(settings->border_brightness,direction);
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_TIMING:
        settings->timing_mode = settings->timing_mode == WORLD_TIMING_ARCADE ? WORLD_TIMING_REMOVE_SLOWDOWN : WORLD_TIMING_ARCADE;
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_POPUPS:
        settings->achievement_popups = !settings->achievement_popups;
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_FULLSCREEN:
        settings->fullscreen = !settings->fullscreen;
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_VSYNC:
        settings->vsync = !settings->vsync;
        return WORLD_MENU_SETTINGS_CHANGED;
    case ITEM_SCALE: {
        /* The host clamps the request to the desktop and writes it back. */
        unsigned next = settings->window_scale + (direction > 0 ? 1u : ~0u);
        if (next < 1 || next > 12) return WORLD_MENU_NONE;
        settings->window_scale = next;
        return WORLD_MENU_SETTINGS_CHANGED;
    }
    case ITEM_SHADER: {
        WorldCrtProfile profile = WORLD_CRT_PIXELS;
        world_crt_parse(settings->shader,&profile);
        unsigned next = ((unsigned)profile + (direction > 0 ? 1u : WORLD_CRT_COUNT - 1u)) % WORLD_CRT_COUNT;
        snprintf(settings->shader,sizeof settings->shader,"%s",world_crt_id((WorldCrtProfile)next));
        return WORLD_MENU_SETTINGS_CHANGED;
    }
    case ITEM_BORDER:
        if (strcmp(settings->border,"black")) {
            strcpy(settings->border,"black");
            return WORLD_MENU_SETTINGS_CHANGED;
        }
        if (!settings->border_image[0]) return WORLD_MENU_PICK_BACKGROUND;
        strcpy(settings->border,"custom");
        return WORLD_MENU_SETTINGS_CHANGED;
    default: return WORLD_MENU_NONE;
    }
}

/* Edges plus key repeat for the navigation buttons in repeat_mask. */
static unsigned pressed_buttons(unsigned *previous, unsigned *repeat_button, uint32_t *repeat_at,
                                unsigned held, uint32_t now_ms, unsigned repeat_mask)
{
    unsigned pressed = held & ~*previous;
    unsigned navigation = held & (WORLD_MENU_UP|WORLD_MENU_DOWN|WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
    if (navigation != *repeat_button) {
        *repeat_button = navigation;
        *repeat_at = now_ms + 320;
    } else if (navigation && (int32_t)(now_ms-*repeat_at) >= 0) {
        pressed |= navigation & repeat_mask;
        *repeat_at = now_ms + 90;
    }
    *previous = held;
    return pressed;
}

unsigned world_menu_pressed(WorldMenuRepeat *state, unsigned held_buttons, uint32_t now_ms,
                            unsigned repeat_mask)
{
    return pressed_buttons(&state->previous,&state->repeat_button,&state->repeat_at,
                           held_buttons,now_ms,repeat_mask);
}

WorldMenuCommand world_menu_update(WorldMenu *menu, WorldSettings *settings,
                                   unsigned held_buttons, uint32_t now_ms)
{
    if (!menu->open) return WORLD_MENU_NONE;
    MenuItem items[ITEM_LIMIT];
    unsigned count = menu_items(menu,settings,items);
    if (menu->selected >= count) menu->selected = count-1;
    unsigned repeat = WORLD_MENU_UP|WORLD_MENU_DOWN;
    if (items[menu->selected].id == ITEM_VOLUME || items[menu->selected].id == ITEM_BRIGHTNESS)
        repeat |= WORLD_MENU_LEFT|WORLD_MENU_RIGHT;
    unsigned pressed = pressed_buttons(&menu->previous_buttons,&menu->repeat_button,&menu->repeat_at,
                                       held_buttons,now_ms,repeat);
    if (pressed & WORLD_MENU_BACK) {
        if (menu->options == PAGE_DISPLAY) { menu->options = PAGE_OPTIONS; menu->selected = menu->options_selected; return WORLD_MENU_NONE; }
        if (menu->options) { menu->options = PAGE_MAIN; menu->selected = menu->return_selected; return WORLD_MENU_NONE; }
        if (menu->paused) { world_menu_close(menu); return WORLD_MENU_RESUME; }
        return WORLD_MENU_NONE;
    }
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_UP)
        menu->selected = menu->selected ? menu->selected-1 : count-1;
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_DOWN)
        menu->selected = (menu->selected+1)%count;
    unsigned id = items[menu->selected].id;
    if (items[menu->selected].locked) return WORLD_MENU_NONE;
    unsigned horizontal = pressed & (WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
    if (horizontal == WORLD_MENU_LEFT || horizontal == WORLD_MENU_RIGHT)
        return adjust_item(id,settings,horizontal == WORLD_MENU_RIGHT ? 1 : -1);
    if (!(pressed & WORLD_MENU_ACCEPT)) return WORLD_MENU_NONE;
    switch (id) {
    case ITEM_OPTIONS: menu->return_selected = menu->selected; menu->options = PAGE_OPTIONS; menu->selected = 0; return WORLD_MENU_NONE;
    case ITEM_DISPLAY: menu->options_selected = menu->selected; menu->options = PAGE_DISPLAY; menu->selected = 0; return WORLD_MENU_NONE;
    case ITEM_BACK:
        if (menu->options == PAGE_DISPLAY) { menu->options = PAGE_OPTIONS; menu->selected = menu->options_selected; }
        else { menu->options = PAGE_MAIN; menu->selected = menu->return_selected; }
        return WORLD_MENU_NONE;
    case ITEM_START: world_menu_close(menu); return WORLD_MENU_START_GAME;
    case ITEM_RESUME: world_menu_close(menu); return WORLD_MENU_RESUME;
    case ITEM_EXIT: return WORLD_MENU_EXIT;
    case ITEM_TITLE: return WORLD_MENU_TITLE;
    case ITEM_CONTROLS: return WORLD_MENU_OPEN_CONTROLS;
    case ITEM_ACHIEVEMENTS: return WORLD_MENU_OPEN_ACHIEVEMENTS;
    case ITEM_BOSS_RUSH: return WORLD_MENU_START_BOSS_RUSH;
    case ITEM_SCORE_ATTACK: return WORLD_MENU_START_SCORE_ATTACK;
    case ITEM_BORDER: return WORLD_MENU_PICK_BACKGROUND;
    default: return adjust_item(id,settings,1);
    }
}

static void rectangle(uint32_t *canvas, unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color)
{
    for (unsigned row = y; row < y+h && row < MENU_HEIGHT; ++row)
        for (unsigned col = x; col < x+w && col < MENU_WIDTH; ++col) canvas[row*MENU_WIDTH+col] = color;
}

static void draw_text(uint32_t *canvas, int x, int y, unsigned scale, uint32_t color, const char *text)
{
    world_menu_draw_text(canvas,MENU_WIDTH,MENU_HEIGHT,x,y,scale,color,text);
}

static void item_value(unsigned id, const WorldSettings *settings, char value[32])
{
    value[0] = 0;
    switch (id) {
    case ITEM_VOLUME: snprintf(value,32,"%u%%",settings->volume); break;
    case ITEM_BRIGHTNESS: snprintf(value,32,"%u%%",settings->border_brightness); break;
    case ITEM_TIMING: snprintf(value,32,"%s",settings->timing_mode == WORLD_TIMING_ARCADE ? "ARCADE" : "REMOVED"); break;
    case ITEM_BORDER: snprintf(value,32,"%s",!strcmp(settings->border,"black") ? "BLACK" : "ARTWORK"); break;
    case ITEM_POPUPS: snprintf(value,32,"%s",settings->achievement_popups ? "ON" : "OFF"); break;
    case ITEM_FULLSCREEN: snprintf(value,32,"%s",settings->fullscreen ? "ON" : "OFF"); break;
    case ITEM_VSYNC: snprintf(value,32,"%s",settings->vsync ? "ON" : "OFF"); break;
    case ITEM_SCALE: snprintf(value,32,"%uX",settings->window_scale); break;
    case ITEM_SHADER: {
        WorldCrtProfile profile = WORLD_CRT_PIXELS;
        world_crt_parse(settings->shader,&profile);
        snprintf(value,32,"%s",world_crt_name(profile));
        break;
    }
    default: break;
    }
}

/* Dimmed game picture, or deep water when there is none, inside the frame. */
static void canvas_begin(uint32_t *canvas, const uint32_t *game_frame)
{
    for (unsigned y = 0; y < MENU_HEIGHT; ++y)
        for (unsigned x = 0; x < MENU_WIDTH; ++x) {
            uint32_t color = 0xff071a22;
            if (game_frame) {
                uint32_t original = game_frame[y*MENU_WIDTH+x];
                unsigned r = ((original>>16)&255)/5+3;
                unsigned g = ((original>>8)&255)/5+9;
                unsigned b = (original&255)/5+13;
                color = 0xff000000u | r<<16 | g<<8 | b;
            }
            canvas[y*MENU_WIDTH+x] = color;
        }
    rectangle(canvas,14,14,292,2,0xffa7894c);
    rectangle(canvas,14,16,2,206,0xff52675e);
    rectangle(canvas,304,16,2,206,0xff52675e);
    rectangle(canvas,14,222,292,2,0xffa7894c);
}

/* Up to two lines, broken at a space, for text wider than the frame. */
static void draw_note(uint32_t *canvas, int y, uint32_t color, const char *text)
{
    enum { COLUMNS = 44 };
    size_t length = strlen(text);
    if (length <= COLUMNS) { draw_text(canvas,30,y+6,1,color,text); return; }
    size_t split = COLUMNS;
    while (split && text[split] != ' ') --split;
    if (!split) split = COLUMNS;
    char line[COLUMNS+1];
    memcpy(line,text,split);
    line[split] = 0;
    draw_text(canvas,30,y,1,color,line);
    const char *rest = text+split;
    while (*rest == ' ') ++rest;
    snprintf(line,sizeof line,"%s",rest);
    draw_text(canvas,30,y+11,1,color,line);
}

void world_menu_render(const WorldMenu *menu, const WorldSettings *settings,
                       uint32_t *output, unsigned width, unsigned height,
                       const uint32_t *game_frame)
{
    if (!menu->open || !output || width < MENU_WIDTH || height < MENU_HEIGHT) return;
    uint32_t canvas[MENU_WIDTH*MENU_HEIGHT];
    canvas_begin(canvas,game_frame);
    const uint32_t cream = 0xffe5dfbd, brass = 0xffc9a661, muted = 0xff91aba6, locked = 0xff5c7370;
    if (menu->options) {
        draw_text(canvas,30,29,2,cream,menu->options == PAGE_DISPLAY ? "DISPLAY" : "OPTIONS");
        draw_text(canvas,30,51,1,muted,"PICTURE 4:3 / INTEGER SCALING");
    } else if (menu->paused) {
        draw_text(canvas,30,29,2,cream,"PAUSED");
        draw_text(canvas,30,51,1,muted,"IN THE HUNT");
    } else {
        draw_text(canvas,30,28,3,cream,"IN THE HUNT");
        rectangle(canvas,30,55,29,2,0xffd1744a);
    }
    MenuItem items[ITEM_LIMIT];
    unsigned count = menu_items(menu,settings,items);
    unsigned spacing = menu->options ? 18 : count > 6 ? 18 : 21;
    unsigned first_y = menu->options ? 76 : count > 6 ? 72 : 78;
    if (!menu->options && count <= 4) first_y = 89;
    const char *note = NULL;
    for (unsigned i = 0; i < count; ++i) {
        unsigned y = first_y+i*spacing;
        int selected = i == menu->selected;
        if (selected) {
            rectangle(canvas,25,y-5,270,17,0xff193c43);
            rectangle(canvas,25,y-5,3,17,0xffdb8050);
            draw_text(canvas,32,y,1,brass,">");
            note = items[i].locked;
        }
        uint32_t color = items[i].locked ? locked : selected ? cream : muted;
        draw_text(canvas,45,y,1,color,items[i].label);
        char value[32]; item_value(items[i].id,settings,value);
        if (items[i].locked) snprintf(value,sizeof value,"LOCKED");
        if (*value) draw_text(canvas,284-world_menu_text_width(value,1),y,1,items[i].locked ? locked : brass,value);
    }
    if (note) draw_note(canvas,200,brass,note);
    else draw_text(canvas,30,209,1,muted,"ARROWS MOVE  ENTER SELECT  ESC BACK");
    unsigned scale = width/MENU_WIDTH;
    if (height/MENU_HEIGHT < scale) scale = height/MENU_HEIGHT;
    unsigned left = (width-MENU_WIDTH*scale)/2, top = (height-MENU_HEIGHT*scale)/2;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            uint32_t color = 0xff07151a;
            if (x >= left && x < left+MENU_WIDTH*scale && y >= top && y < top+MENU_HEIGHT*scale)
                color = canvas[((y-top)/scale)*MENU_WIDTH+(x-left)/scale];
            output[y*width+x] = color;
        }
}

/* ---- Control remapping page ---- */

enum { CONTROL_DEFAULTS = WORLD_INPUT_ACTIONS, CONTROL_BACK, CONTROL_ROWS };

void world_menu_controls_open(WorldMenuControls *page, unsigned held_buttons, uint32_t now_ms)
{
    memset(page,0,sizeof *page);
    page->open = 1;
    page->source = WORLD_INPUT_KEYBOARD;
    page->previous_buttons = held_buttons;
    page->repeat_at = now_ms;
}

WorldMenuControlsCommand world_menu_controls_update(WorldMenuControls *page,
                                                    WorldInputBindings *bindings,
                                                    unsigned held_buttons, uint32_t now_ms)
{
    if (!page->open) return WORLD_MENU_CONTROLS_NONE;
    unsigned pressed = pressed_buttons(&page->previous_buttons,&page->repeat_button,&page->repeat_at,
                                       held_buttons,now_ms,WORLD_MENU_UP|WORLD_MENU_DOWN);
    if (pressed & WORLD_MENU_BACK) { page->open = 0; return WORLD_MENU_CONTROLS_CLOSE; }
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_UP)
        page->selected = page->selected ? page->selected-1 : CONTROL_ROWS-1;
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_DOWN)
        page->selected = (page->selected+1)%CONTROL_ROWS;
    unsigned horizontal = pressed & (WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
    if (horizontal == WORLD_MENU_LEFT || horizontal == WORLD_MENU_RIGHT) {
        /* Player 1 keyboard, Player 1 controller, Player 2 keyboard, Player 2 controller. */
        unsigned table = page->player*2 + (page->source == WORLD_INPUT_CONTROLLER);
        table = (table + (horizontal == WORLD_MENU_RIGHT ? 1 : 3)) % 4;
        page->player = table/2;
        page->source = table & 1 ? WORLD_INPUT_CONTROLLER : WORLD_INPUT_KEYBOARD;
        return WORLD_MENU_CONTROLS_NONE;
    }
    if (!(pressed & WORLD_MENU_ACCEPT)) return WORLD_MENU_CONTROLS_NONE;
    if (page->selected == CONTROL_BACK) { page->open = 0; return WORLD_MENU_CONTROLS_CLOSE; }
    if (page->selected == CONTROL_DEFAULTS) {
        WorldInputBindings defaults;
        world_input_bindings_defaults(&defaults);
        if (!bindings) return WORLD_MENU_CONTROLS_NONE;
        if (page->source == WORLD_INPUT_CONTROLLER)
            memcpy(bindings->controller[page->player],defaults.controller[page->player],sizeof defaults.controller[0]);
        else memcpy(bindings->keyboard[page->player],defaults.keyboard[page->player],sizeof defaults.keyboard[0]);
        return WORLD_MENU_CONTROLS_CHANGED;
    }
    return WORLD_MENU_CONTROLS_CAPTURE;
}

void world_menu_controls_render(const WorldMenuControls *page, const WorldInputBindings *bindings,
                                int capturing, uint32_t *output, const uint32_t *game_frame)
{
    if (!page->open || !bindings || !output) return;
    canvas_begin(output,game_frame);
    const uint32_t cream = 0xffe5dfbd, brass = 0xffc9a661, muted = 0xff91aba6;
    int controller = page->source == WORLD_INPUT_CONTROLLER;
    char title[48];
    draw_text(output,30,27,2,cream,"CONTROLS");
    snprintf(title,sizeof title,"< PLAYER %u %s >",page->player+1,controller ? "CONTROLLER" : "KEYBOARD");
    draw_text(output,30,49,1,brass,title);
    for (unsigned row = 0; row < CONTROL_ROWS; ++row) {
        unsigned y = 66+row*14;
        int selected = row == page->selected;
        if (selected) {
            rectangle(output,25,y-4,270,14,0xff193c43);
            rectangle(output,25,y-4,3,14,0xffdb8050);
            draw_text(output,32,y,1,brass,">");
        }
        const char *label = row == CONTROL_DEFAULTS ? "RESTORE DEFAULTS" : row == CONTROL_BACK ? "BACK"
                          : world_input_action_name((WorldInputAction)row);
        draw_text(output,45,y,1,selected ? cream : muted,label);
        if (row >= WORLD_INPUT_ACTIONS) continue;
        char value[40];
        if (selected && capturing) snprintf(value,sizeof value,"PRESS...");
        else world_input_binding_name(page->source,controller ? bindings->controller[page->player][row]
                                                              : bindings->keyboard[page->player][row],
                                      value,sizeof value);
        draw_text(output,284-world_menu_text_width(value,1),y,1,brass,value);
    }
    if (capturing)
        draw_text(output,30,209,1,cream,controller ? "PRESS A CONTROLLER INPUT  ESC CANCELS"
                                                   : "PRESS A KEY  ESC CANCELS");
    else draw_text(output,30,209,1,muted,"ENTER REBIND  LEFT/RIGHT TABLE  ESC BACK");
}
