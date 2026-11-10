#ifndef WORLD_SCORES_STORE_H
#define WORLD_SCORES_STORE_H
#include <stdint.h>

enum {
    WORLD_SCORES_OK,
    WORLD_SCORES_NOT_FOUND,
    WORLD_SCORES_BUSY,
    WORLD_SCORES_INVALID,
    WORLD_SCORES_UNSUPPORTED,
    WORLD_SCORES_IO_ERROR,
    WORLD_SCORES_BAD_STATE
};

typedef struct {
    uint8_t defaults[12][16];
    uint8_t ranked_defaults[10][16];
    int initialized;
} WorldScoresStore;

/* Capture the ROM-initialized table immediately after world_boot_world. */
int world_scores_store_init(WorldScoresStore *store, const uint8_t *boot_ram);
/* Load once at boot. Only ranked records and their ten pointers are written.
 * The active-player records and pointers at 254/256 remain untouched.
 * Supports this module's version 1 file and legacy 216-byte arcade score files. */
int world_scores_store_load(const WorldScoresStore *store, uint8_t *boot_ram, const char *path);
/* Call after completed name entry, or on shutdown after a completed ranking.
 * Only user records are written. Concurrent saves merge under a file lock.
 * BUSY can be retried. Corrupt files are replaced after a valid ranking.
 * Unknown future versions are preserved. I/O failure leaves the previous file intact. */
int world_scores_store_save(const WorldScoresStore *store, const uint8_t *ram, const char *path);
#endif
