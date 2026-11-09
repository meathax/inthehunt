#ifndef WORLD_TITLE_MASTER_H
#define WORLD_TITLE_MASTER_H
#include <stdint.h>

int world_title_master(uint8_t *ram, uint8_t *tiles, uint8_t *oam,
                       uint8_t *palette, const uint8_t *rom, int busy_system);
#endif
