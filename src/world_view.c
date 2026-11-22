#include "world_view.h"

static unsigned session_mode, mode;

void world_view_set(unsigned m)
{
    session_mode = mode = m == WORLD_VIEW_WIDE ? WORLD_VIEW_WIDE : WORLD_VIEW_ARCADE;
}

unsigned world_view_mode(void)
{
    return session_mode;
}

/* The vertical cave and its rising boss occupy one arcade-width shaft. */
void world_view_set_stage(unsigned stage)
{
    mode = stage == 4 ? WORLD_VIEW_ARCADE : session_mode;
}

unsigned world_view_canvas_width(void)
{
    return session_mode == WORLD_VIEW_WIDE ? WORLD_VIEW_WIDE_WIDTH : WORLD_VIEW_ARCADE_WIDTH;
}

unsigned world_view_width(void)
{
    return mode == WORLD_VIEW_WIDE ? WORLD_VIEW_WIDE_WIDTH : WORLD_VIEW_ARCADE_WIDTH;
}

int world_view_left(void)
{
    return mode == WORLD_VIEW_WIDE ? -WORLD_VIEW_WING : 0;
}

int world_view_raster(int screen_x)
{
    return screen_x - world_view_left();
}

uint16_t world_view_player_min_x(void)
{
    /* left edge +96 field bias, authored +16 inset; wide aperture edge is
       -53 1/3, so +16 -> 58.67 rounds inward to 59 (0x3B). */
    return mode == WORLD_VIEW_WIDE ? 0x3b : 0x70;
}

uint16_t world_view_player_max_x(void)
{
    /* right edge +96, authored -24 inset; aperture edge 373 1/3 -> 445.33
       rounds inward to 445 (0x1BD). */
    return mode == WORLD_VIEW_WIDE ? 0x1bd : 0x188;
}

uint16_t world_view_spawn_x(uint16_t x)
{
    if (mode != WORLD_VIEW_WIDE || !x) return x;
    int16_t sx = (int16_t)x;
    if (sx < 0x60)   return (uint16_t)(sx - WORLD_VIEW_WING); /* left edge incl. signed negative */
    if (x >= 0x1a0)  return (uint16_t)(x + WORLD_VIEW_WING);  /* beyond arcade right edge */
    return x;
}

/* Left-edge constants are compared unsigned in the decompiled code, where
 * "past the left edge" is a small window [0, C) of the field. In Wide the
 * same window sits at screen [-96-WING, C-96-WING), which straddles the
 * field's wrap: a signed screen comparison reproduces it exactly. */
int world_view_edge_less(uint16_t x, unsigned c)
{
    if (mode != WORLD_VIEW_WIDE) return x < c;
    return c < 0xa0 ? (int16_t)(x - 0x60) < (int)c - 0x96
                    : x < c + WORLD_VIEW_WING;
}

int world_view_edge_less_s(int16_t x, int c)
{
    if (mode != WORLD_VIEW_WIDE) return x < c;
    return c < 0xa0 ? x < c - WORLD_VIEW_WING : x < c + WORLD_VIEW_WING;
}

int world_view_edge_greater(uint16_t x, unsigned c)
{
    if (mode != WORLD_VIEW_WIDE) return x > c;
    return c >= 0xa0 ? x > c + WORLD_VIEW_WING : x > c;
}

int world_view_edge_greater_s(int16_t x, int c)
{
    if (mode != WORLD_VIEW_WIDE) return x > c;
    return c >= 0xa0 ? x > c + WORLD_VIEW_WING : x > c - WORLD_VIEW_WING;
}

int world_view_edge_ge_s(int16_t x, int c)
{
    if (mode != WORLD_VIEW_WIDE) return x >= c;
    return c >= 0xa0 ? x >= c + WORLD_VIEW_WING : x >= c - WORLD_VIEW_WING;
}

int world_view_edge_le_s(int16_t x, int c)
{
    if (mode != WORLD_VIEW_WIDE) return x <= c;
    return c >= 0xa0 ? x <= c + WORLD_VIEW_WING : x <= c - WORLD_VIEW_WING;
}

int world_view_edge_eq(uint16_t x, unsigned c)
{
    if (mode != WORLD_VIEW_WIDE) return x == c;
    return c < 0xa0 ? (int16_t)(x - 0x60) == (int)c - 0x96
                    : x == c + WORLD_VIEW_WING;
}

uint16_t world_view_edge_right(unsigned c)
{
    return (uint16_t)(mode == WORLD_VIEW_WIDE && c >= 0xa0 ? c + WORLD_VIEW_WING : c);
}

uint16_t world_view_edge_left(unsigned c)
{
    return (uint16_t)(mode == WORLD_VIEW_WIDE && c <= 0xa0 && c >= WORLD_VIEW_WING
                          ? c - WORLD_VIEW_WING : c);
}

int world_view_hud_shift(unsigned owner)
{
    if (mode != WORLD_VIEW_WIDE) return 0;
    return owner == 1 ? -28 : owner == 2 ? 28 : 0;
}
