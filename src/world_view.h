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
enum { WORLD_VIEW_ARCADE = 0, WORLD_VIEW_WIDE = 1 };

enum {
    WORLD_VIEW_HEIGHT = 240,
    WORLD_VIEW_ARCADE_WIDTH = 320,
    WORLD_VIEW_WIDE_WIDTH = 428,
    WORLD_VIEW_MAX_WIDTH = 428,
    WORLD_VIEW_WING = 54              /* wide raster X = screen X + 54 */
};

/* Set once per session before any mode-dependent camera, buffers, actors or
 * first frame initialize. Arcade is the power-on default. */
void world_view_set(unsigned mode);
unsigned world_view_mode(void);

/* Raster columns of the working frame: 320 or 428. */
unsigned world_view_width(void);
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

/* Tilemap byte-column shift applied to per-player HUD panel destinations.
 * The HUD plane's rows 45/46 hold P1's left panel and P2's right panel;
 * Widescreen anchors them 7 tiles (28 bytes, 56 px) toward the new aperture
 * edges so they sit at safe insets. Centered prompts (timer, HI-SCORE,
 * tally rows) keep their authored positions. */
int world_view_hud_shift(unsigned owner);

#endif
