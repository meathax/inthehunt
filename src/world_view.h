#ifndef WORLD_VIEW_H
#define WORLD_VIEW_H

#include <stdint.h>

/* Session-owned view contract for docs/widescreen-gameplay-plan.md.
 *
 * The game logic keeps its original coordinate spaces: actor +0x21 is the
 * hardware sprite-X field (screen + 96) and screen = world - camera. The
 * mode only widens the window into that screen space: Arcade sees legacy X
 * in [0,320); Widescreen sees [-54,374) in a 428-wide raster where
 * raster X = screen X + 54. The visible 16:9 aperture is the middle
 * 426 2/3 columns; 2/3 column of overscan stays on each side. */
enum { WORLD_VIEW_ARCADE = 0, WORLD_VIEW_WIDE = 1, WORLD_VIEW_ULTRA = 2 };

/* Ultrawide (64:27, sold as 21:9) sees legacy X in [-124,444) in a 568-wide
   raster; its 568 8/9-column aperture overscans by 4/9 column per side. */
enum {
    WORLD_VIEW_HEIGHT = 240,
    WORLD_VIEW_ARCADE_WIDTH = 320,
    WORLD_VIEW_WIDE_WIDTH = 428,
    WORLD_VIEW_ULTRA_WIDTH = 568,
    WORLD_VIEW_MAX_WIDTH = 568,
    WORLD_VIEW_WING = 54,             /* wide raster X = screen X + 54 */
    WORLD_VIEW_ULTRA_WING = 124       /* ultrawide raster X = screen X + 124 */
};

/* Wing of a raster width: (width - 320) / 2. */
static inline int world_view_wing_of(unsigned width)
{
    return width > WORLD_VIEW_ARCADE_WIDTH ? (int)(width - WORLD_VIEW_ARCADE_WIDTH) / 2 : 0;
}
/* Session width of a mode. */
static inline unsigned world_view_mode_width(unsigned mode)
{
    return mode == WORLD_VIEW_ULTRA ? WORLD_VIEW_ULTRA_WIDTH
         : mode == WORLD_VIEW_WIDE ? WORLD_VIEW_WIDE_WIDTH : WORLD_VIEW_ARCADE_WIDTH;
}

/* Set once per session before any mode-dependent camera, buffers, actors or
 * first frame initialize. Arcade is the power-on default. */
void world_view_set(unsigned mode);
unsigned world_view_mode(void);
void world_view_set_stage(unsigned stage);
unsigned world_view_canvas_width(void);
/* Wings of a picture that has no authored content outside the arcade 320
 * columns (locked arenas, the arcade-width shaft): the picture's edges are
 * reflected into the wings, tile-aligned, for tile layers 1 and 2. 0 off;
 * 1 reflect; 2 reflect and hide layer 0 in the wings (its HUD stays put). */
int world_view_wing_mirror(void);
void world_view_set_wing_mirror(int mode);
/* The picture-x range reflected for a layer whose picture x 0 samples
 * playfield column K: [-inf,*lo) and [*hi,inf) take the reflection, and
 * the return is the reflected x of `x` (or x unchanged inside). */
int world_view_mirror_x(int x, int k);

/* Raster columns of the working frame: 320, 428 or 568. */
unsigned world_view_width(void);
/* Columns added on each side of the arcade picture: 0, 54 or 124. */
int world_view_wing(void);
/* Legacy screen X at raster column 0: 0 or -54. */
int world_view_left(void);
/* Raster column of a signed legacy screen X (may be < 0 or >= width). */
int world_view_raster(int screen_x);

/* Player hull clamp bounds in field units (screen + 96). Authored insets are
 * +16 inside the left edge and -24 inside the right edge; in Wide they are
 * applied to the aperture edges [-53 1/3, 373 1/3) rounded inward. */
uint16_t world_view_player_min_x(void);
uint16_t world_view_player_max_x(void);

/* A spawn record's authored X (field units) adjusted so an entrant authored
 * beyond a legacy picture edge enters beyond the corresponding wide edge.
 * Call only for records classified as edge-entrant (see docs). */
uint16_t world_view_spawn_x(uint16_t x);

/* Off-view position tests. Actor +0x21 (screen X + 96) is compared in the
 * decompiled handlers against constants that encode a position relative to
 * a picture edge — despawn margins (< 0x40, > 0x1c0), draw culls, and "has
 * reached the edge" triggers. Constants in [0x10,0x9f] are left-edge, those
 * >= 0xa0 right-edge; Widescreen shifts them by the 54-px wing so the same
 * tests fire at the same distance outside the new aperture. Interior
 * constants (screen positions, patrol bounds) must NOT use these. */
int world_view_edge_less(uint16_t x, unsigned c);      /* x < c  (unsigned read) */
int world_view_edge_less_s(int16_t x, int c);          /* x < c  ((int16_t) read) */
int world_view_edge_greater(uint16_t x, unsigned c);   /* x > c  (unsigned read) */
int world_view_edge_greater_s(int16_t x, int c);       /* x > c  ((int16_t) read) */
int world_view_edge_ge_s(int16_t x, int c);            /* x >= c ((int16_t) read) */
int world_view_edge_le_s(int16_t x, int c);            /* x <= c ((int16_t) read) */
int world_view_edge_eq(uint16_t x, unsigned c);        /* x == c */

/* The shifted edge constant itself, for position clamps that pin a sprite
 * field X at the edge and for unsigned >= gates whose operand domain keeps
 * wrapped negative fields (x >= c admits wrapped fields). edge_left covers
 * left-margin constants [WING,0xa0]; edge_right covers [0xa0,...) — the 0xa0
 * boundary is ambiguous in the source, so the caller picks by semantics. */
uint16_t world_view_edge_right(unsigned c);
uint16_t world_view_edge_left(unsigned c);
/* x >= edge_left(c) for a left-margin "has entered the picture" gate. The
   ROM compares unsigned; ultrawide edges lie at negative fields, so that
   mode compares signed. */
int world_view_edge_ge_left(uint16_t x, unsigned c);

/* Tilemap byte-column shift applied to per-player HUD panel destinations.
 * The HUD plane's rows 45/46 hold P1's left panel and P2's right panel;
 * Widescreen anchors them 7 tiles (28 bytes, 56 px) toward the new aperture
 * edges so they sit at safe insets. Centered prompts (timer, HI-SCORE,
 * tally rows) keep their authored positions. */
int world_view_hud_shift(unsigned owner);

#endif
