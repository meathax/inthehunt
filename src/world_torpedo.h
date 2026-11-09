#ifndef WORLD_TORPEDO_H
#define WORLD_TORPEDO_H
#include <stdint.h>

typedef struct {
    uint16_t x, y, velocity, timer;
    uint8_t fraction, facing;
} Torpedo;

void world_launch_torpedo(Torpedo *, const uint8_t *, uint16_t, uint16_t, uint8_t, uint16_t);
void world_fly_torpedo(Torpedo *, const uint8_t *, uint16_t, uint16_t);
uint16_t world_fire_standard(uint8_t *, const uint8_t *, uint16_t, Torpedo *);
void world_player_weapons(uint8_t *, const uint8_t *, uint16_t);
#endif

