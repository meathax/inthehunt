#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "world_scores_store.h"

enum { RECORD_BYTES = 16, RANKS = 10, POOL = 12, HEADER_BYTES = 16, LEGACY_BYTES = 216 };
typedef struct { unsigned count; uint8_t records[RANKS][RECORD_BYTES]; } UserScores;

static unsigned word(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static void put_word(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static uint32_t payload_hash(const uint8_t *bytes, unsigned size)
{
    uint32_t hash = hash_bytes(2166136261u, bytes, 8);
    return hash_bytes(hash, bytes + HEADER_BYTES, size - HEADER_BYTES);
}

static int name_glyph(unsigned glyph)
{
    return !glyph || glyph == ' ' || glyph == '!' || glyph == '-' || glyph == '.' ||
           glyph == '<' || glyph == '>' || (glyph >= '0' && glyph <= '9') ||
           (glyph >= 'A' && glyph <= 'Z');
}

static int valid_record(const uint8_t *record)
{
    for (unsigned i = 0; i < 4; ++i)
        if ((record[i] & 15) > 9 || (record[i] >> 4) > 9) return 0;
    unsigned category = word(record + 6);
    if ((category & 1) || category > 20) return 0;
    for (unsigned i = 8; i < RECORD_BYTES; ++i)
        if (!name_glyph(record[i])) return 0;
    return 1;
}

static int pool_slot(unsigned pointer)
{
    return pointer >= 0x180 && pointer < 0x240 && !(pointer & 15);
}

static int default_record(const WorldScoresStore *store, const uint8_t *record)
{
    for (unsigned i = 0; i < POOL; ++i)
        if (!memcmp(store->defaults[i], record, RECORD_BYTES)) return 1;
    return 0;
}

static int extract_ranked(const WorldScoresStore *store, const uint8_t *records,
                          const uint8_t *pointers, UserScores *scores)
{
    unsigned used = 0;
    scores->count = 0;
    for (unsigned i = 0; i < RANKS; ++i) {
        unsigned pointer = word(pointers + 2*i);
        if (!pool_slot(pointer)) return 0;
        unsigned slot = (pointer - 0x180) / RECORD_BYTES;
        if (used & (1u << slot)) return 0;
        used |= 1u << slot;
        const uint8_t *record = records + RECORD_BYTES * slot;
        if (!valid_record(record)) return 0;
        if (!default_record(store, record))
            memcpy(scores->records[scores->count++], record, RECORD_BYTES);
    }
    return 1;
}

static int compare_record(const uint8_t *a, const uint8_t *b)
{
    for (int i = 3; i >= 0; --i)
        if (a[i] != b[i]) return (int)a[i] - b[i];
    return (int)word(a + 4) - (int)word(b + 4);
}

static void sort_records(uint8_t records[20][RECORD_BYTES], unsigned count)
{
    for (unsigned i = 1; i < count; ++i) {
        uint8_t entry[RECORD_BYTES];
        memcpy(entry, records[i], sizeof entry);
        unsigned position = i;
        while (position && compare_record(entry, records[position-1]) > 0) {
            memcpy(records[position], records[position-1], RECORD_BYTES);
            --position;
        }
        memcpy(records[position], entry, RECORD_BYTES);
    }
}

int world_scores_store_init(WorldScoresStore *store, const uint8_t *ram)
{
    if (!store || !ram) return WORLD_SCORES_BAD_STATE;
    memset(store, 0, sizeof *store);
    unsigned used = 0;
    for (unsigned i = 0; i < POOL; ++i) {
        unsigned pointer = word(ram + 0x240 + 2*i);
        if (!pool_slot(pointer)) return WORLD_SCORES_BAD_STATE;
        unsigned slot = (pointer - 0x180) / RECORD_BYTES;
        if (used & (1u << slot)) return WORLD_SCORES_BAD_STATE;
        used |= 1u << slot;
        if (i < RANKS) memcpy(store->ranked_defaults[i], ram + pointer, RECORD_BYTES);
    }
    memcpy(store->defaults, ram + 0x180, sizeof store->defaults);
    store->initialized = 1;
    return WORLD_SCORES_OK;
}

static int read_scores(const WorldScoresStore *store, const char *path, UserScores *scores)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? WORLD_SCORES_NOT_FOUND : WORLD_SCORES_IO_ERROR;
    LARGE_INTEGER length;
    uint8_t bytes[LEGACY_BYTES];
    DWORD received = 0;
    int ok = GetFileSizeEx(file, &length) && length.QuadPart >= HEADER_BYTES &&
             length.QuadPart <= (LONGLONG)sizeof bytes &&
             ReadFile(file, bytes, (DWORD)length.QuadPart, &received, NULL) &&
             received == length.QuadPart;
    CloseHandle(file);
    if (!ok) return WORLD_SCORES_INVALID;
    unsigned size = (unsigned)length.QuadPart;
    if (!memcmp(bytes, "IHS", 3) && (memcmp(bytes, "IHS1", 4) || word(bytes + 4) != 1))
        return WORLD_SCORES_UNSUPPORTED;
    if (size == LEGACY_BYTES && memcmp(bytes, "IHS1", 4)) {
        unsigned used = 0;
        for (unsigned i = 0; i < POOL; ++i) {
            unsigned pointer = word(bytes + POOL*RECORD_BYTES + 2*i);
            if (!pool_slot(pointer)) return WORLD_SCORES_INVALID;
            unsigned slot = (pointer - 0x180) / RECORD_BYTES;
            if (used & (1u << slot)) return WORLD_SCORES_INVALID;
            used |= 1u << slot;
        }
        return extract_ranked(store, bytes, bytes + POOL*RECORD_BYTES, scores) ?
               WORLD_SCORES_OK : WORLD_SCORES_INVALID;
    }
    unsigned count = word(bytes + 6);
    uint32_t checksum = (uint32_t)word(bytes + 8) | (uint32_t)word(bytes + 10) << 16;
    if (memcmp(bytes, "IHS1", 4) || word(bytes + 4) != 1 || count > RANKS ||
        size != HEADER_BYTES + count*RECORD_BYTES || word(bytes + 12) || word(bytes + 14) ||
        checksum != payload_hash(bytes, size)) return WORLD_SCORES_INVALID;
    scores->count = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *record = bytes + HEADER_BYTES + i*RECORD_BYTES;
        if (!valid_record(record)) return WORLD_SCORES_INVALID;
        if (!default_record(store, record))
            memcpy(scores->records[scores->count++], record, RECORD_BYTES);
    }
    return WORLD_SCORES_OK;
}

int world_scores_store_load(const WorldScoresStore *store, uint8_t *ram, const char *path)
{
    if (!store || !store->initialized || !ram || !path || (ram[0x264] & 3) ||
        word(ram + 0x254) != 0x220 || word(ram + 0x256) != 0x230)
        return WORLD_SCORES_BAD_STATE;
    UserScores user;
    int status = read_scores(store, path, &user);
    if (status != WORLD_SCORES_OK) return status;
    uint8_t ranked[20][RECORD_BYTES];
    memcpy(ranked, store->ranked_defaults, sizeof store->ranked_defaults);
    memcpy(ranked + RANKS, user.records, user.count*RECORD_BYTES);
    sort_records(ranked, RANKS + user.count);
    for (unsigned i = 0; i < RANKS; ++i) {
        memcpy(ram + 0x180 + RECORD_BYTES*i, ranked[i], RECORD_BYTES);
        put_word(ram + 0x240 + 2*i, 0x180 + RECORD_BYTES*i);
    }
    return WORLD_SCORES_OK;
}

/* Merge by maximum multiplicity, so repeated saves retain real tied entries
 * without multiplying records already present in both snapshots. */
static void merge_users(UserScores *saved, const UserScores *current)
{
    uint8_t merged[20][RECORD_BYTES];
    unsigned count = saved->count;
    memcpy(merged, saved->records, count*RECORD_BYTES);
    for (unsigned i = 0; i < current->count; ++i) {
        unsigned previous = 0, seen = 0;
        for (unsigned j = 0; j < saved->count; ++j)
            previous += !memcmp(current->records[i], saved->records[j], RECORD_BYTES);
        for (unsigned j = 0; j <= i; ++j)
            seen += !memcmp(current->records[i], current->records[j], RECORD_BYTES);
        if (seen > previous) memcpy(merged[count++], current->records[i], RECORD_BYTES);
    }
    sort_records(merged, count);
    saved->count = count < RANKS ? count : RANKS;
    memcpy(saved->records, merged, saved->count*RECORD_BYTES);
}

static int write_scores(const char *path, const UserScores *scores)
{
    static LONG counter;
    char temporary[4096];
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        int size = snprintf(temporary, sizeof temporary, "%s.tmp-%lu-%lu", path,
                            (unsigned long)GetCurrentProcessId(), (unsigned long)InterlockedIncrement(&counter));
        if (size < 0 || (size_t)size >= sizeof temporary) return WORLD_SCORES_IO_ERROR;
        file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS) return WORLD_SCORES_IO_ERROR;
    }
    if (file == INVALID_HANDLE_VALUE) return WORLD_SCORES_IO_ERROR;
    uint8_t bytes[HEADER_BYTES + RANKS*RECORD_BYTES] = {0};
    memcpy(bytes, "IHS1", 4);
    put_word(bytes + 4, 1);
    put_word(bytes + 6, scores->count);
    memcpy(bytes + HEADER_BYTES, scores->records, scores->count*RECORD_BYTES);
    unsigned size = HEADER_BYTES + scores->count*RECORD_BYTES;
    uint32_t checksum = payload_hash(bytes, size);
    put_word(bytes + 8, checksum);
    put_word(bytes + 10, checksum >> 16);
    DWORD written = 0;
    int ok = WriteFile(file, bytes, size, &written, NULL) && written == size && FlushFileBuffers(file);
    if (!CloseHandle(file)) ok = 0;
    if (ok) ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileA(temporary);
    return ok ? WORLD_SCORES_OK : WORLD_SCORES_IO_ERROR;
}

int world_scores_store_save(const WorldScoresStore *store, const uint8_t *ram, const char *path)
{
    if (!store || !store->initialized || !ram || !path) return WORLD_SCORES_BAD_STATE;
    UserScores current, saved = {0};
    if (!extract_ranked(store, ram + 0x180, ram + 0x240, &current)) return WORLD_SCORES_BAD_STATE;
    char lock_path[4096];
    int size = snprintf(lock_path, sizeof lock_path, "%s.lock", path);
    if (size < 0 || (size_t)size >= sizeof lock_path) return WORLD_SCORES_IO_ERROR;
    HANDLE lock = CreateFileA(lock_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (lock == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION ? WORLD_SCORES_BUSY : WORLD_SCORES_IO_ERROR;
    int status = read_scores(store, path, &saved);
    if (status == WORLD_SCORES_INVALID) {
        memset(&saved, 0, sizeof saved);
        status = WORLD_SCORES_NOT_FOUND;
    }
    if (status == WORLD_SCORES_OK || status == WORLD_SCORES_NOT_FOUND) {
        merge_users(&saved, &current);
        status = write_scores(path, &saved);
    }
    CloseHandle(lock);
    return status;
}
