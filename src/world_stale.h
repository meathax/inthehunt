#ifndef WORLD_STALE_H
#define WORLD_STALE_H
/* Native-only record of a register value that original code leaves behind
 * and a later routine reads without loading it. It is not part of the
 * emulated machine state. Defined in world_gaps.c.
 *
 * DX: the tally actor 2ABB draws its blank label (C234 -> 5FC6) with the tile
 * attribute still in DX. Between passes the sprite builder 0E74 and the
 * palette fader 125D load DX; inside the pass only a few tally handlers do.
 * Every code path that is not known to keep or load DX forgets the value, and
 * a reader that finds it forgotten must stop instead of guessing. */
#include <stdint.h>

void world_stale_dx_set(uint16_t value);
void world_stale_dx_forget(void);
/* Returns zero when the value is not known. */
int world_stale_dx(uint16_t *value);
/* 125D faded a palette bank: DX holds the bank's last colour. The native
 * build runs the fader before the boundary, the original after 0E74. */
void world_stale_dx_fade(uint16_t value);
/* 0E74 finished. `pieces` is nonzero when it walked at least one sprite
 * piece, which leaves `dx`. `master` is the scene controller about to run. */
void world_stale_dx_boundary(int pieces, uint16_t dx, unsigned master);
/* Around every actor handler call: done = 0 before, 1 after it returned. */
void world_stale_dx_call(uint32_t handler, int done);

#endif
