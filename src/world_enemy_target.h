#ifndef WORLD_ENEMY_TARGET_H
#define WORLD_ENEMY_TARGET_H

#include <stdint.h>

int world_enemy_target_mode_supported(uint8_t mode);
uint8_t world_random_byte(uint8_t *ram);
/* Returns the selected player's work RAM offset for the World 065B1 call. */
uint16_t world_enemy_target(uint8_t *ram, uint16_t actor);

#endif
