#ifndef WORLD_IMPORT_H
#define WORLD_IMPORT_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* (offset, length) pairs that rebuild a World chip from a verified regional chip. */
typedef struct { uint32_t offset, length; } WorldCopyRange;
typedef struct {
    const char *set;
    uint8_t sha1[20];
    const WorldCopyRange *ranges;
    uint32_t range_count;
} WorldAlternate;
typedef struct {
    const char *file;
    uint32_t offset, size, stride;
    uint8_t sha1[20];
    const WorldAlternate *alternates;
    uint32_t alternate_count;
} WorldChip;
typedef struct {
    const char *name;
    uint32_t size;
    const WorldChip *chips;
    uint32_t chip_count;
} WorldRegion;
typedef struct {
    const WorldRegion *regions;
    uint32_t region_count;
    const char *metadata_sha256;   /* identifies the table in native-import.ok */
} WorldRegionTable;

extern const WorldRegionTable world_regions;

/* Import a MAME ZIP or 7z archive into the World directory, then validate the
 * installed cache. Returns 1 on success; on failure, error holds a message. */
int world_import_rom(const WorldRegionTable *table, const wchar_t *archive, const wchar_t *world,
                     wchar_t *error, size_t error_count);

#endif
