#ifndef WORLD_STAGE_H
#define WORLD_STAGE_H
#include <stdint.h>

void world_acquire_resource(uint8_t *ram, uint16_t actor, uint16_t id);
void world_stage_opening_hud(uint8_t *ram, uint8_t *tiles, const uint8_t *rom);
void world_timer_digits(uint8_t *tiles, const uint8_t *digits);
void world_stage_spawns(uint8_t *ram, const uint8_t *rom, uint16_t parent);
void world_kind11_init(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void world_kind25_init(uint8_t *ram, const uint8_t *rom, uint16_t actor);
#endif

