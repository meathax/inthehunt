#ifndef WORLD_BOOT_H
#define WORLD_BOOT_H
#include <stdint.h>

/* Cold boot the World board to its first actor dispatch. The caller supplies
 * 64 KiB work RAM, 64 KiB tile RAM, 2 KiB OAM, 2 KiB palette and 1 MiB ROM.
 * Returns zero for service mode, a failed diagnostic or an invalid ROM map. */
int world_boot_world(uint8_t *ram, uint8_t *tiles, uint8_t *oam, uint8_t *palette,
                     const uint8_t *rom, uint16_t raw_dips);
#endif
