#include "world_view.h"

static unsigned session_mode, mode;

void world_view_set(unsigned m)
{
    session_mode = mode = m == WORLD_VIEW_WIDE || m == WORLD_VIEW_ULTRA ? m : WORLD_VIEW_ARCADE;
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
    return world_view_mode_width(session_mode);
}

unsigned world_view_width(void)
{
    return world_view_mode_width(mode);
}

int world_view_wing(void)
{
    return mode == WORLD_VIEW_ULTRA ? WORLD_VIEW_ULTRA_WING : mode == WORLD_VIEW_WIDE ? WORLD_VIEW_WING : 0;
}

int world_view_left(void)
{
    return -world_view_wing();
}

int world_view_raster(int screen_x)
{
    return screen_x - world_view_left();
}

uint16_t world_view_player_min_x(void)
{
    /* left edge +96 field bias, authored +16 inset. Wide aperture edge is
       -53 1/3, so +16 -> 58.67 rounds inward to 59 (0x3B). The ultrawide
       edge (-124 4/9) + 16 gives field -12 (0xFFF4); callers compare it
       as a signed 16-bit value. */
    return mode == WORLD_VIEW_ULTRA ? 0xfff4 : mode == WORLD_VIEW_WIDE ? 0x3b : 0x70;
}

uint16_t world_view_player_max_x(void)
{
    /* right edge +96, authored -24 inset; wide aperture edge 373 1/3 ->
       445.33 rounds inward to 445 (0x1BD); ultrawide 444 4/9 -> 516. */
    return mode == WORLD_VIEW_ULTRA ? 0x204 : mode == WORLD_VIEW_WIDE ? 0x1bd : 0x188;
}

uint16_t world_view_spawn_x(uint16_t x)
{
    int wing = world_view_wing();
    if (!wing || !x) return x;
    int16_t sx = (int16_t)x;
    if (sx < 0x60)   return (uint16_t)(sx - wing); /* left edge incl. signed negative */
    if (x >= 0x1a0)  return (uint16_t)(x + wing);  /* beyond arcade right edge */
    return x;
}

/* Left-edge constants are compared unsigned in the decompiled code, where
 * "past the left edge" is a small window [0, C) of the field. In the wide
 * modes the same window sits at screen [-96-wing, C-96-wing), which
 * straddles the field's wrap: a signed screen comparison reproduces it. */
int world_view_edge_less(uint16_t x, unsigned c)
{
    int wing = world_view_wing();
    if (!wing) return x < c;
    return c < 0xa0 ? (int16_t)(x - 0x60) < (int)c - 0x60 - wing
                    : (int16_t)x < (int)c + wing;
}

int world_view_edge_less_s(int16_t x, int c)
{
    int wing = world_view_wing();
    if (!wing) return x < c;
    return c < 0xa0 ? x < c - wing : x < c + wing;
}

int world_view_edge_greater(uint16_t x, unsigned c)
{
    int wing = world_view_wing();
    if (!wing) return x > c;
    return c >= 0xa0 ? (int16_t)x > (int)c + wing : x > c;
}

int world_view_edge_greater_s(int16_t x, int c)
{
    int wing = world_view_wing();
    if (!wing) return x > c;
    return c >= 0xa0 ? x > c + wing : x > c - wing;
}

int world_view_edge_ge_s(int16_t x, int c)
{
    int wing = world_view_wing();
    if (!wing) return x >= c;
    return c >= 0xa0 ? x >= c + wing : x >= c - wing;
}

int world_view_edge_le_s(int16_t x, int c)
{
    int wing = world_view_wing();
    if (!wing) return x <= c;
    return c >= 0xa0 ? x <= c + wing : x <= c - wing;
}

int world_view_edge_eq(uint16_t x, unsigned c)
{
    int wing = world_view_wing();
    if (!wing) return x == c;
    return c < 0xa0 ? (int16_t)(x - 0x60) == (int)c - 0x60 - wing
                    : x == c + wing;
}

uint16_t world_view_edge_right(unsigned c)
{
    int wing = world_view_wing();
    return (uint16_t)(wing && c >= 0xa0 ? c + wing : c);
}

uint16_t world_view_edge_left(unsigned c)
{
    int wing = world_view_wing();
    return (uint16_t)(wing && c <= 0xa0 && (int)c >= WORLD_VIEW_WING ? c - wing : c);
}

int world_view_edge_ge_left(uint16_t x, unsigned c)
{
    if (mode == WORLD_VIEW_ULTRA) return (int16_t)x >= (int)c - WORLD_VIEW_ULTRA_WING;
    return x >= world_view_edge_left(c);
}

int world_view_hud_shift(unsigned owner)
{
    /* Wide: 7 tiles (56 px). Ultrawide: 12 tiles (96 px) puts P1's panel at
       tile column 0 and P2's last cell at column 63, the widest positions
       the 64-column HUD plane holds without wrapping (28-pixel insets). */
    int shift = mode == WORLD_VIEW_ULTRA ? 48 : mode == WORLD_VIEW_WIDE ? 28 : 0;
    return owner == 1 ? -shift : owner == 2 ? shift : 0;
}
