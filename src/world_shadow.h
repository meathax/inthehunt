#ifndef WORLD_SHADOW_H
#define WORLD_SHADOW_H

#include <stdint.h>
#include "world_video.h"

/* Ultrawide tile shadow.
 *
 * Each M92 playfield page is a 64-column (512-pixel) ring, but the
 * ultrawide view is 568 pixels wide. The game keeps addressing its rings;
 * this module maintains a 128-column (1024-pixel) copy of every scrolling
 * page in which each ring column is filed under the world column it
 * represents. Before an actor runs, the ring is rebased so that the 64
 * world columns centred on that actor occupy it; afterwards its changes are
 * filed back. Renderers read the shadow, so both wings show the columns the
 * game drew for them even when the ring has since reused that storage.
 *
 * World column of a picture X on ring row R: floor((x + offset_R) / 8),
 * where offset_R = 80 + scroll - 2 * layer is the playfield sample offset
 * (world_video_playfields). The ring column is world & 63, the shadow slot
 * world & 127. */

enum { WORLD_SHADOW_COLUMNS = 128 };

/* Shadow cells for a page (64 rows x 128 columns x 4 bytes), or NULL when
 * the page is read from its ring. Renderers must check the tag too. */
extern const uint8_t *world_tile_shadow_page[4];
extern const int16_t *world_tile_shadow_tags[4];
/* Per ring row: 0 shadowed, 1 static (ring, wrapping), 2 HUD (ring, clipped). */
extern const uint8_t *world_tile_shadow_rows[4];
/* Nonzero when renderers sample ultrawide pictures: a non-shadowed page
 * then shows nothing beyond its 64 columns instead of wrapping. */
extern int world_tile_shadow_clip;

/* Cell pointer for a playfield sample, or NULL for a transparent sample. */
static inline const uint8_t *world_tile_entry(const uint8_t *tiles, unsigned master, unsigned sx, unsigned sy)
{
    unsigned page = master & 3, row = (sy / 8) & 63;
    unsigned columns = master & 4 ? 128 : 64;
    unsigned mode = !(master & 4) && world_tile_shadow_page[page] ? 1u + world_tile_shadow_rows[page][row] : 0u;
    if (mode == 1) {
        int world = (int)(sx >> 3) & 8191;
        unsigned slot = (unsigned)world & 127;
        if ((world_tile_shadow_tags[page][row * WORLD_SHADOW_COLUMNS + slot] & 8191) != world) return 0;
        return world_tile_shadow_page[page] + (row * WORLD_SHADOW_COLUMNS + slot) * 4;
    }
    /* HUD lines (and unshadowed layers) end at their 64 columns; static
       scenery lines repeat like the hardware ring. */
    if (mode != 2 && world_tile_shadow_clip && ((int)sx < 0 || sx >= columns * 8)) return 0;
    return tiles + ((page * 0x4000 + (row * columns + ((sx / 8) & (columns - 1))) * 4) & 0xffff);
}

/* Start a logic pass. Activates in ultrawide gameplay; layers carry the
 * current scroll registers (scroll_x biased as in capture_video). */
void world_shadow_begin_pass(const uint8_t *ram, uint8_t *tiles, const uint16_t layer_master[3]);
/* Before an actor's handler: rebase the rings around a picture X. */
void world_shadow_focus(uint8_t *ram, uint8_t *tiles, int screen_x);
/* Before a dispatched handler: choose the focus for this actor. */
void world_shadow_actor(uint8_t *ram, uint8_t *tiles, uint16_t actor, uint32_t address);
/* End of pass: file the ring back and restore the default focus. */
void world_shadow_end_pass(uint8_t *ram, uint8_t *tiles);
/* Whether the shadow is active this pass. */
int world_shadow_active(void);
/* Reflect the authored opening map outward into the shadow (stage start).
   base is the page offset, origin the map's first block, width/height in
   4x4 blocks, upward for a mode-3 map. */
void world_shadow_extend_opening(uint8_t *tiles, uint16_t base, uint16_t origin,
                                 unsigned width, unsigned height, int upward);
/* Write a cell for a world column of the fg page (8-pixel column index as in
   world_tile_entry) into both the shadow and, when mapped, the ring. */
void world_shadow_put_column(uint8_t *tiles, unsigned page, int world, unsigned row, const uint8_t cell[4]);
/* The world column of a picture X on a page row (for callers that must
   address a column the ring cannot hold). */
int world_shadow_world_column(unsigned page, unsigned row, int screen_x);
/* A page row the game repaints across its whole ring: render it straight
   from the ring, wrapping (until the shadow resets). */
void world_shadow_ring_band(unsigned page, unsigned row);
/* Copy a world column's cell out of the shadow. Returns 0 when the shadow
   does not hold that column or the row is not shadowed. */
int world_shadow_get_column(unsigned page, int world, unsigned row, uint8_t cell[4]);
void world_shadow_reset(void);
/* World-column offset (96 + layer scroll at the start of this pass) of a
   page shown by a plain scrolling layer, in any mode: world column of a
   picture X = floor((x + offset) / 8). Returns 0 for HUD, line-scrolled,
   128-column or hidden pages. */
int world_shadow_page_offset(unsigned page, int *offset);
/* Store one 16-bit word (byte 0 tile, 2 attribute) of a world column's cell
   in the shadow and, when that column is mapped, in the ring. Columns the
   shadow does not hold yet are left for the game to draw. */
void world_shadow_put_word(uint8_t *tiles, unsigned page, int world, unsigned row, unsigned byte, uint16_t value);

#endif
