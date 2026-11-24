#ifndef WORLD_STAGE_H
#define WORLD_STAGE_H
#include <stdint.h>

void world_acquire_resource(uint8_t *ram, uint16_t actor, uint16_t id);
void world_stage_opening_hud(uint8_t *ram, uint8_t *tiles, const uint8_t *rom);
void world_timer_digits(uint8_t *tiles, const uint8_t *digits);
void world_stage_spawns(uint8_t *ram, const uint8_t *rom, uint16_t parent);
/* Forget records spawned ahead of their trigger (machine reset, snapshot load). */
void world_stage_reset_prespawn(void);
void world_stage_three_boss_wall(const uint8_t *ram, uint8_t *tiles);
/* Wide views, after each logic pass: continue the stage-one far mountains
   over the plain sky the island hides (see world_stage.c). */
void world_stage_one_far_mountains(const uint8_t *ram, uint8_t *tiles);
void world_kind11_init(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void world_kind25_init(uint8_t *ram, const uint8_t *rom, uint16_t actor);
#endif

