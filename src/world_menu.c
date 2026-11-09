#include "world_menu.h"
#include <stdio.h>
#include <string.h>

enum { MENU_WIDTH = 320, MENU_HEIGHT = 240, ITEM_LIMIT = 10 };
enum { ITEM_START, ITEM_RESUME, ITEM_OPTIONS, ITEM_EXIT, ITEM_TITLE,
       ITEM_CONTROLS, ITEM_ACHIEVEMENTS, ITEM_BOSS_RUSH, ITEM_SCORE_ATTACK,
       ITEM_VOLUME, ITEM_TIMING, ITEM_BORDER, ITEM_BRIGHTNESS, ITEM_POPUPS, ITEM_BACK };
typedef struct { unsigned id; const char *label; } MenuItem;

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
    case '/': return 1u << (4 - (row*4/6));
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
    ++*count;
}

static unsigned menu_items(const WorldMenu *menu, MenuItem items[ITEM_LIMIT])
{
    unsigned count = 0, caps = menu->capabilities;
    if (menu->options) {
        if (caps & WORLD_MENU_SOUND) add_item(items,&count,ITEM_VOLUME,"SOUND VOLUME");
        if (caps & WORLD_MENU_TIMING) add_item(items,&count,ITEM_TIMING,"SLOWDOWN");
        if (caps & WORLD_MENU_BORDERS) {
            add_item(items,&count,ITEM_BORDER,"CABINET BORDER");
            add_item(items,&count,ITEM_BRIGHTNESS,"BORDER BRIGHTNESS");
        }
        if (caps & WORLD_MENU_CONTROLS) add_item(items,&count,ITEM_CONTROLS,"CONTROLS");
        if (caps & WORLD_MENU_ACHIEVEMENTS) add_item(items,&count,ITEM_POPUPS,"ACHIEVEMENT POPUPS");
        add_item(items,&count,ITEM_BACK,"BACK");
    } else {
        add_item(items,&count,menu->paused ? ITEM_RESUME : ITEM_START,menu->paused ? "RESUME" : "START");
        if (!menu->paused && (caps & WORLD_MENU_BOSS_RUSH)) add_item(items,&count,ITEM_BOSS_RUSH,"BOSS RUSH");
        if (!menu->paused && (caps & WORLD_MENU_SCORE_ATTACK)) add_item(items,&count,ITEM_SCORE_ATTACK,"SCORE ATTACK");
        add_item(items,&count,ITEM_OPTIONS,"OPTIONS");
        if (caps & WORLD_MENU_ACHIEVEMENTS) add_item(items,&count,ITEM_ACHIEVEMENTS,"ACHIEVEMENTS");
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

WorldMenuCommand world_menu_update(WorldMenu *menu, WorldSettings *settings,
                                   unsigned held_buttons, uint32_t now_ms)
{
    if (!menu->open) return WORLD_MENU_NONE;
    unsigned pressed = held_buttons & ~menu->previous_buttons;
    unsigned navigation = held_buttons & (WORLD_MENU_UP|WORLD_MENU_DOWN|WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
    if (navigation != menu->repeat_button) {
        menu->repeat_button = navigation;
        menu->repeat_at = now_ms + 320;
    } else if (navigation && (int32_t)(now_ms-menu->repeat_at) >= 0) {
        pressed |= navigation & (WORLD_MENU_UP|WORLD_MENU_DOWN);
        if (menu->options) {
            MenuItem current[ITEM_LIMIT];
            unsigned count = menu_items(menu,current);
            if (menu->selected < count && (current[menu->selected].id == ITEM_VOLUME ||
                                           current[menu->selected].id == ITEM_BRIGHTNESS))
                pressed |= navigation & (WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
        }
        menu->repeat_at = now_ms + 90;
    }
    menu->previous_buttons = held_buttons;
    if (pressed & WORLD_MENU_BACK) {
        if (menu->options) { menu->options = 0; menu->selected = menu->return_selected; return WORLD_MENU_NONE; }
        if (menu->paused) { world_menu_close(menu); return WORLD_MENU_RESUME; }
        return WORLD_MENU_NONE;
    }
    MenuItem items[ITEM_LIMIT];
    unsigned count = menu_items(menu,items);
    if (menu->selected >= count) menu->selected = count-1;
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_UP)
        menu->selected = menu->selected ? menu->selected-1 : count-1;
    if ((pressed & (WORLD_MENU_UP|WORLD_MENU_DOWN)) == WORLD_MENU_DOWN)
        menu->selected = (menu->selected+1)%count;
    unsigned id = items[menu->selected].id;
    unsigned horizontal = pressed & (WORLD_MENU_LEFT|WORLD_MENU_RIGHT);
    if (horizontal == WORLD_MENU_LEFT || horizontal == WORLD_MENU_RIGHT)
        return adjust_item(id,settings,horizontal == WORLD_MENU_RIGHT ? 1 : -1);
    if (!(pressed & WORLD_MENU_ACCEPT)) return WORLD_MENU_NONE;
    switch (id) {
    case ITEM_OPTIONS: menu->return_selected = menu->selected; menu->options = 1; menu->selected = 0; return WORLD_MENU_NONE;
    case ITEM_BACK: menu->options = 0; menu->selected = menu->return_selected; return WORLD_MENU_NONE;
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
    default: break;
    }
}

void world_menu_render(const WorldMenu *menu, const WorldSettings *settings,
                       uint32_t *output, unsigned width, unsigned height,
                       const uint32_t *game_frame)
{
    if (!menu->open || !output || width < MENU_WIDTH || height < MENU_HEIGHT) return;
    uint32_t canvas[MENU_WIDTH*MENU_HEIGHT];
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
    const uint32_t cream = 0xffe5dfbd, brass = 0xffc9a661, muted = 0xff91aba6;
    if (menu->options) {
        draw_text(canvas,30,29,2,cream,"OPTIONS");
        draw_text(canvas,30,51,1,muted,"PICTURE 4:3 / INTEGER SCALING");
    } else if (menu->paused) {
        draw_text(canvas,30,29,2,cream,"PAUSED");
        draw_text(canvas,30,51,1,muted,"IN THE HUNT");
    } else {
        draw_text(canvas,30,28,3,cream,"IN THE HUNT");
        rectangle(canvas,30,55,29,2,0xffd1744a);
    }
    MenuItem items[ITEM_LIMIT];
    unsigned count = menu_items(menu,items);
    unsigned spacing = menu->options ? 18 : 21;
    unsigned first_y = menu->options ? 76 : 78;
    if (!menu->options && count <= 4) first_y = 89;
    for (unsigned i = 0; i < count; ++i) {
        unsigned y = first_y+i*spacing;
        int selected = i == menu->selected;
        if (selected) {
            rectangle(canvas,25,y-5,270,17,0xff193c43);
            rectangle(canvas,25,y-5,3,17,0xffdb8050);
            draw_text(canvas,32,y,1,brass,">");
        }
        draw_text(canvas,45,y,1,selected ? cream : muted,items[i].label);
        char value[32]; item_value(items[i].id,settings,value);
        if (*value) draw_text(canvas,284-world_menu_text_width(value,1),y,1,brass,value);
    }
    draw_text(canvas,30,209,1,muted,"ARROWS MOVE  ENTER SELECT  ESC BACK");
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
