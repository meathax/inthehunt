#ifndef WORLD_TITLE_H
#define WORLD_TITLE_H
#include <stdint.h>

enum { WORLD_TITLE_DRAW = 1, WORLD_TITLE_FINISH = 2, WORLD_TITLE_SKIP_MOTION = 4 };
/* Returns the pending sprite/retire actions, or -1 for an unknown handler. */
int world_title_actor(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);
void world_credit_step(uint8_t *ram);
int world_start_player(uint8_t *ram);
int world_title_boundary(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom);
#endif
