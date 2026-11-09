#include "world_achievement_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared original UI font, also used by the native menu. */
void world_menu_draw_text(uint32_t *, unsigned, unsigned, int, int, unsigned, uint32_t, const char *);

enum { QUEUE_SIZE = 64, POPUP_MS = 5000, ROWS_VISIBLE = 6 };
static const uint32_t ink = 0xff0c191d, paper = 0xffe6e9d7, muted = 0xff9caeaa;
static const uint32_t tiers[] = {0xffa4d5bb, 0xff8ac9d8, 0xffeeb873, 0xffffa195};
static const char *tier_names[] = {"EASY", "MEDIUM", "DIFFICULT", "EXTREME"};

struct WorldAchievementView {
    const WorldAchievementViewRow *rows;
    size_t count, selected, top;
    WorldAchievementViewQuery query;
    void *context;
    int filter, reduced_motion, details;
    size_t queue[QUEUE_SIZE];
    unsigned queue_head, queue_count, age;
    uint32_t menu[320 * 240], popup[320 * 60];
};

typedef struct { uint32_t *pixels; unsigned width, height; } Canvas;

static void rect(Canvas c, int x, int y, int width, int height, uint32_t color)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > (int)c.width) right = (int)c.width;
    if (bottom > (int)c.height) bottom = (int)c.height;
    for (int row = y; row < bottom; ++row)
        for (int col = x; col < right; ++col) c.pixels[row*c.width+col] = color;
}

static void text(Canvas c, int x, int y, uint32_t color, const char *value)
{
    world_menu_draw_text(c.pixels, c.width, c.height, x, y, 1, color, value ? value : "");
}

static void plate(Canvas c, int x, int y, int w, int h)
{
    rect(c, x, y, w, h, 0xff172a30);
    rect(c, x, y, w, 1, 0xff7b918d);
    rect(c, x, y+1, 1, h-1, 0xff526e70);
    rect(c, x+w-1, y+1, 1, h-1, 0xff09171b);
    rect(c, x, y+h-1, w, 1, 0xff09171b);
}

static void rivet(Canvas c, int x, int y)
{
    rect(c, x, y, 3, 3, 0xff071519);
    rect(c, x, y, 2, 1, 0xff9aaba0);
    rect(c, x, y+1, 2, 1, 0xff465d60);
}

static void medal(Canvas c, int x, int y, unsigned tier, int unlocked)
{
    uint32_t color = unlocked ? tiers[tier] : 0xff667c7c;
    rect(c, x+4, y, 12, 10, color);
    rect(c, x+20, y, 12, 10, color);
    rect(c, x+8, y, 4, 10, 0xff29454b);
    rect(c, x+24, y, 4, 10, 0xff29454b);
    rect(c, x+1, y+10, 34, 24, ink);
    rect(c, x+2, y+11, 32, 22, color);
    for (unsigned i = 0; i < 14; ++i)
        rect(c, x+2+(int)i, y+33+(int)i, 32-2*(int)i, 1, color);
    rect(c, x+5, y+14, 26, 2, 0xffe7ead0);
    for (unsigned i = 0; i <= tier; ++i)
        rect(c, x+9, y+21+4*(int)i, 18, 2, ink);
}

static unsigned tier_of(const WorldAchievementViewRow *row)
{
    unsigned tier = (unsigned)row->tier;
    return tier < 4 ? tier : 0;
}

static WorldAchievementViewStatus status_of(const WorldAchievementView *v, size_t index)
{
    WorldAchievementViewStatus status = {0, 0, 0};
    if (v->query) status = v->query(v->context, index);
    return status;
}

static int matches(const WorldAchievementView *v, size_t index)
{
    return v->filter < 0 || tier_of(v->rows + index) == (unsigned)v->filter;
}

static size_t filtered_count(const WorldAchievementView *v)
{
    size_t count = 0;
    for (size_t i = 0; i < v->count; ++i) count += matches(v, i);
    return count;
}

static size_t filtered_index(const WorldAchievementView *v, size_t position)
{
    for (size_t i = 0; i < v->count; ++i)
        if (matches(v, i) && position-- == 0) return i;
    return SIZE_MAX;
}

static size_t selected_position(const WorldAchievementView *v)
{
    size_t position = 0;
    for (size_t i = 0; i < v->selected; ++i) position += matches(v, i);
    return position;
}

static void follow_selection(WorldAchievementView *v)
{
    size_t position = selected_position(v);
    if (position < v->top) v->top = position;
    if (position >= v->top + ROWS_VISIBLE) v->top = position - ROWS_VISIBLE + 1;
}

/* Wrap only at word boundaries, preserving as much readable text as fits. */
static int paragraph(Canvas c, int x, int y, unsigned columns, unsigned lines,
                     uint32_t color, const char *value)
{
    const char *next = value ? value : "";
    for (unsigned row = 0; row < lines && *next; ++row) {
        while (*next == ' ') ++next;
        size_t length = strlen(next);
        if (length > columns) length = columns;
        if (next[length] && next[length] != ' ') {
            size_t boundary = length;
            while (boundary && next[boundary] != ' ') --boundary;
            if (boundary) length = boundary;
        }
        char line[64];
        if (row+1 == lines && next[length] && length >= 3) {
            memcpy(line, next, length-3);
            memcpy(line+length-3, "...", 3);
        } else memcpy(line, next, length);
        line[length] = 0;
        text(c, x, y, color, line);
        next += length;
        y += 10;
    }
    return y;
}

WorldAchievementView *world_achievement_view_create(const WorldAchievementViewRow *rows, size_t count,
                                                   WorldAchievementViewQuery query, void *context)
{
    if (!rows && count) return NULL;
    WorldAchievementView *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->rows = rows; v->count = count; v->query = query; v->context = context;
    v->filter = -1;
    v->selected = count ? 0 : SIZE_MAX;
    return v;
}

void world_achievement_view_destroy(WorldAchievementView *v) { free(v); }
void world_achievement_view_reduced_motion(WorldAchievementView *v, int enabled)
{
    if (v) v->reduced_motion = !!enabled;
}

int world_achievement_view_notify(WorldAchievementView *v, size_t index)
{
    if (!v || index >= v->count || v->queue_count == QUEUE_SIZE) return 0;
    for (unsigned i = 0; i < v->queue_count; ++i)
        if (v->queue[(v->queue_head+i) % QUEUE_SIZE] == index) return 0;
    v->queue[(v->queue_head+v->queue_count) % QUEUE_SIZE] = index;
    if (!v->queue_count) v->age = 0;
    ++v->queue_count;
    return 1;
}

void world_achievement_view_advance(WorldAchievementView *v, uint32_t elapsed)
{
    if (!v) return;
    while (v->queue_count && elapsed >= POPUP_MS - v->age) {
        elapsed -= POPUP_MS - v->age;
        v->queue_head = (v->queue_head+1) % QUEUE_SIZE;
        --v->queue_count;
        v->age = 0;
    }
    if (v->queue_count) v->age += elapsed;
}

int world_achievement_view_navigate(WorldAchievementView *v, unsigned actions)
{
    if (!v) return 0;
    if (actions & WORLD_ACHIEVEMENT_VIEW_BACK) {
        if (!v->details) return 0;
        v->details = 0;
        return 1;
    }
    if (!v->count) return 0;
    if (actions & WORLD_ACHIEVEMENT_VIEW_ACCEPT) {
        v->details = v->selected != SIZE_MAX;
        return v->details;
    }
    if (actions & (WORLD_ACHIEVEMENT_VIEW_LEFT | WORLD_ACHIEVEMENT_VIEW_RIGHT)) {
        v->filter += (actions & WORLD_ACHIEVEMENT_VIEW_RIGHT) ? 1 : -1;
        if (v->filter > 3) v->filter = -1;
        if (v->filter < -1) v->filter = 3;
        v->selected = filtered_index(v, 0);
        v->top = 0;
    }
    size_t count = filtered_count(v);
    if (!count) return 0;
    size_t position = selected_position(v), step = 1;
    if (actions & (WORLD_ACHIEVEMENT_VIEW_PAGE_UP | WORLD_ACHIEVEMENT_VIEW_PAGE_DOWN)) step = ROWS_VISIBLE;
    if (actions & (WORLD_ACHIEVEMENT_VIEW_UP | WORLD_ACHIEVEMENT_VIEW_PAGE_UP))
        position = position >= step ? position-step : count-1;
    else if (actions & (WORLD_ACHIEVEMENT_VIEW_DOWN | WORLD_ACHIEVEMENT_VIEW_PAGE_DOWN))
        position = (position+step) % count;
    v->selected = filtered_index(v, position);
    follow_selection(v);
    return 1;
}

size_t world_achievement_view_selected(const WorldAchievementView *v)
{
    return v ? v->selected : SIZE_MAX;
}

WorldAchievementViewSurface world_achievement_view_menu(WorldAchievementView *v)
{
    WorldAchievementViewSurface surface = {NULL, 320, 240, 255, 0};
    if (!v) return surface;
    Canvas c = {v->menu, 320, 240};
    rect(c, 0, 0, 320, 240, ink);
    plate(c, 6, 6, 308, 228);
    rivet(c, 11, 11); rivet(c, 306, 11); rivet(c, 11, 226); rivet(c, 306, 226);
    text(c, 18, 16, paper, "ACHIEVEMENTS");
    size_t unlocked = 0;
    for (size_t i = 0; i < v->count; ++i) unlocked += !!status_of(v, i).unlocked;
    char counter[40];
    snprintf(counter, sizeof counter, "%u / %u", (unsigned)unlocked, (unsigned)v->count);
    text(c, 238, 16, tiers[2], counter);
    if (v->details && v->selected != SIZE_MAX) {
        const WorldAchievementViewRow *item = v->rows + v->selected;
        WorldAchievementViewStatus status = status_of(v, v->selected);
        unsigned tier = tier_of(item);
        medal(c, 22, 55, tier, status.unlocked);
        text(c, 76, 55, tiers[tier], tier_names[tier]);
        paragraph(c, 76, 69, 36, 3, paper, item->name);
        text(c, 76, 101, muted, item->mode);
        paragraph(c, 22, 121, 46, 7, paper, item->description);
        text(c, 22, 202, status.unlocked ? tiers[tier] : muted,
             status.unlocked ? "UNLOCKED" : "LOCKED");
        if (!status.unlocked && status.target) {
            uint64_t current = status.current > status.target ? status.target : status.current;
            unsigned width = (unsigned)((double)current / (double)status.target * 140);
            rect(c, 150, 201, 144, 5, ink);
            rect(c, 152, 202, (int)width, 3, tiers[tier]);
            snprintf(counter, sizeof counter, "%llu / %llu", (unsigned long long)current,
                     (unsigned long long)status.target);
            text(c, 150, 211, muted, counter);
        }
        text(c, 22, 222, muted, "UP/DOWN SELECT");
        text(c, 226, 222, paper, "BACK LIST");
        surface.pixels = v->menu;
        return surface;
    }
    static const char *filters[] = {"ALL", "EASY", "MEDIUM", "DIFFICULT", "EXTREME"};
    static const int x[] = {17, 51, 91, 143, 213}, width[] = {27, 33, 45, 63, 63};
    for (int i = 0; i < 5; ++i) {
        if (v->filter == i-1) rect(c, x[i]-3, 32, width[i], 15, tiers[2]);
        text(c, x[i], 36, v->filter == i-1 ? ink : paper, filters[i]);
    }
    size_t count = filtered_count(v);
    for (unsigned row = 0; row < ROWS_VISIBLE; ++row) {
        size_t index = filtered_index(v, v->top+row);
        if (index == SIZE_MAX) break;
        const WorldAchievementViewRow *item = v->rows + index;
        WorldAchievementViewStatus status = status_of(v, index);
        int y = 55+24*(int)row, selected = index == v->selected;
        rect(c, 15, y, 140, 23, selected ? 0xff355454 : 0xff12252a);
        if (selected) rect(c, 15, y, 3, 23, tiers[tier_of(item)]);
        char name[22];
        snprintf(name, sizeof name, "%.20s", item->name ? item->name : "");
        if (item->name && strlen(item->name) > 20) memcpy(name+17, "...", 4);
        text(c, 23, y+4, status.unlocked ? paper : muted, name);
        text(c, 23, y+14, status.unlocked ? tiers[0] : muted, status.unlocked ? "UNLOCKED" : "LOCKED");
    }
    if (count > ROWS_VISIBLE) {
        unsigned thumb = 144*ROWS_VISIBLE/(unsigned)count;
        unsigned offset = (144-thumb)*(unsigned)v->top/(unsigned)(count-ROWS_VISIBLE);
        rect(c, 156, 55, 2, 144, 0xff07171c);
        rect(c, 156, 55+(int)offset, 2, (int)thumb, 0xff8b9e93);
    }
    plate(c, 165, 55, 140, 144);
    if (v->selected != SIZE_MAX) {
        const WorldAchievementViewRow *item = v->rows + v->selected;
        WorldAchievementViewStatus status = status_of(v, v->selected);
        unsigned tier = tier_of(item);
        text(c, 173, 64, tiers[tier], tier_names[tier]);
        int y = paragraph(c, 173, 78, 20, 3, paper, item->name);
        y = paragraph(c, 173, y+3, 20, 1, muted, item->mode);
        unsigned bottom = !status.unlocked && status.target ? 176 : 195;
        unsigned lines = y+8 < (int)bottom ? (bottom-(unsigned)(y+8))/10 : 0;
        paragraph(c, 173, y+8, 20, lines, paper, item->description);
        if (!status.unlocked && status.target) {
            uint64_t current = status.current > status.target ? status.target : status.current;
            unsigned progress = (unsigned)((double)current / (double)status.target * 120);
            rect(c, 173, 180, 124, 4, ink);
            rect(c, 175, 181, (int)progress, 2, tiers[tier]);
            snprintf(counter, sizeof counter, "%llu / %llu", (unsigned long long)current,
                     (unsigned long long)status.target);
            text(c, 173, 189, muted, counter);
        }
    } else text(c, 173, 70, muted, "NO ACHIEVEMENTS");
    text(c, 18, 211, paper, "UP/DOWN SELECT");
    text(c, 208, 211, paper, "ENTER DETAILS");
    text(c, 18, 222, muted, "LEFT/RIGHT FILTER");
    text(c, 226, 222, paper, "BACK RETURN");
    surface.pixels = v->menu;
    return surface;
}

WorldAchievementViewSurface world_achievement_view_popup(WorldAchievementView *v)
{
    WorldAchievementViewSurface surface = {NULL, 320, 60, 0, 0};
    if (!v || !v->queue_count) return surface;
    const WorldAchievementViewRow *item = v->rows + v->queue[v->queue_head];
    unsigned tier = tier_of(item);
    Canvas c = {v->popup, 320, 60};
    rect(c, 0, 0, 320, 60, ink);
    plate(c, 1, 1, 318, 58);
    rect(c, 2, 2, 3, 56, tiers[tier]);
    rivet(c, 309, 7); rivet(c, 309, 50);
    medal(c, 12, 7, tier, 1);
    text(c, 57, 10, tiers[tier], "ACHIEVEMENT UNLOCKED");
    paragraph(c, 57, 24, 40, 2, paper, item->name);
    text(c, 57, 47, muted, tier_names[tier]);
    unsigned progress = (POPUP_MS-v->age)*244/POPUP_MS;
    rect(c, 57, 56, (int)progress, 1, tiers[tier]);
    unsigned fade = v->age < 200 ? v->age : POPUP_MS-v->age < 300 ? (POPUP_MS-v->age)*2/3 : 200;
    surface.opacity = v->reduced_motion ? 255 : fade*255/200;
    surface.offset_y = v->reduced_motion ? 0 : (int)((255-surface.opacity)*12/255);
    surface.pixels = v->popup;
    return surface;
}
