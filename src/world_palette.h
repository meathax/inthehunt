#ifndef WORLD_PALETTE_H
#define WORLD_PALETTE_H
#include <stdint.h>

/* Advances the 64 palette jobs against 2 KiB of M92 palette RAM. */
void world_palette_step(uint8_t *work, uint8_t *palette, const uint8_t *rom);
#endif
