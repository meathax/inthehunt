#ifndef WORLD_STAGE5_H
#define WORLD_STAGE5_H
#include <stdint.h>
/* Breakable wall cells (terrain material 1) exist only in stage 5 (stage index 4).
 * The player and weapon routines shared by all stages call the wall routine at
 * 7000:9DAF/9D93 from these places; the code lives in world_stage5.c. */

/* 0A3B7: a player weapon struck terrain at its own position. Stage index 4 only. */
void world_stage5_wall_hit(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor);
/* 0A3B7 after a probe away from the actor: `cell` is the probed terrain cell (BW). */
void world_stage5_wall_hit_cell(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor, uint16_t cell);
/* 08683: the entering submarine clears a 6x3 cell block around itself. Stage index 4 only.
 * Returns the pointer the original leaves in IX: the submarine's settings record, or,
 * once a cell was opened, whatever the wall routine left there. The caller reads its
 * medium bytes through that pointer, as the original does. */
uint16_t world_stage5_sub_entry_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub);
/* 08763: every 16th pass, the cells beside the submarine. x is the moved, unresolved X;
 * rows are the three sensor cells at x-16 (Y+7, Y, Y-8) for the Y before this move. */
void world_stage5_sub_side_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub,
                                 uint16_t x, const uint16_t rows[3]);
/* 08886: every 16th pass, five cells above or below. x is resolved, y is the moved Y;
 * upper and lower are the sensor cells at x-16 for Y+7 and Y-8. */
void world_stage5_sub_row_walls(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub,
                                uint16_t x, uint16_t y, uint16_t upper, uint16_t lower);
#endif
