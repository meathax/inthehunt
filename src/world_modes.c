#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "world_modes.h"

static int rules_valid(WorldModeRules rules)
{
    return rules.mode >= WORLD_MODE_ARCADE && rules.mode <= WORLD_MODE_SCORE_ATTACK &&
           (rules.timing == WORLD_TIMING_ARCADE || rules.timing == WORLD_TIMING_REMOVE_SLOWDOWN) &&
           rules.players >= 1 && rules.players <= 2 &&
           (rules.mode != WORLD_MODE_SCORE_ATTACK || rules.players == 1);
}

static unsigned record_slot(WorldModeRules rules)
{
    return (rules.mode - 1) * 4 + rules.timing * 2 + rules.players - 1;
}

static int better(WorldGameMode mode, const WorldModeRecord *candidate, const WorldModeRecord *old)
{
    if (!candidate->present) return 0;
    if (!old->present) return 1;
    if (mode == WORLD_MODE_SCORE_ATTACK) return candidate->score > old->score;
    if (candidate->bosses_cleared != old->bosses_cleared)
        return candidate->bosses_cleared > old->bosses_cleared;
    return candidate->active_ticks < old->active_ticks;
}

const char *world_modes_name(WorldGameMode mode)
{
    switch (mode) {
    case WORLD_MODE_ARCADE: return "Arcade";
    case WORLD_MODE_BOSS_RUSH: return "Boss Rush";
    case WORLD_MODE_SCORE_ATTACK: return "Score Attack";
    default: return "Unknown";
    }
}

int world_modes_available(WorldGameMode mode, const WorldModeCore *core)
{
    if (mode == WORLD_MODE_ARCADE) return 1;
    if (!core || !core->launch) return 0;
    if (mode == WORLD_MODE_SCORE_ATTACK) return !!core->native_campaign_start;
    if (mode != WORLD_MODE_BOSS_RUSH || !core->boss_count || core->boss_count > 64) return 0;
    uint64_t all = core->boss_count == 64 ? UINT64_MAX : (UINT64_C(1) << core->boss_count) - 1;
    return (core->native_bosses & all) == all;
}

const char *world_modes_unavailable_reason(WorldGameMode mode, const WorldModeCore *core)
{
    if (world_modes_available(mode, core)) return NULL;
    if (mode == WORLD_MODE_BOSS_RUSH) return "Boss encounters are still being recovered.";
    if (mode == WORLD_MODE_SCORE_ATTACK) return "Native campaign launch is still being connected.";
    return "Unknown game mode.";
}

void world_modes_reset(WorldModeRun *run)
{
    memset(run, 0, sizeof *run);
    run->rules.players = 1;
}

int world_modes_begin(WorldModeRun *run, WorldModeRules rules, const WorldModeCore *core)
{
    if (!rules_valid(rules) || !world_modes_available(rules.mode, core)) return 0;
    WorldModeRun next = {0};
    next.rules = rules;
    if (rules.mode == WORLD_MODE_ARCADE) next.phase = WORLD_MODE_PLAYING;
    else {
        WorldModeLaunch request = {0};
        request.rules = rules;
        request.raw_dips = 0xffbf;
        request.credits = rules.players;
        if (!core->launch(core->context, &request)) return 0;
        next.phase = WORLD_MODE_STARTING;
        if (rules.mode == WORLD_MODE_BOSS_RUSH) next.boss_count = core->boss_count;
    }
    *run = next;
    return 1;
}

int world_modes_started(WorldModeRun *run)
{
    if (run->phase != WORLD_MODE_STARTING) return 0;
    run->phase = WORLD_MODE_PLAYING;
    return 1;
}

void world_modes_tick(WorldModeRun *run, uint64_t ticks, int paused, int confirmed)
{
    if (!confirmed || paused || run->phase != WORLD_MODE_PLAYING) return;
    if (ticks > UINT64_MAX - run->active_ticks) {
        run->active_ticks = UINT64_MAX;
        world_modes_invalidate(run, WORLD_MODE_INVALID_CORE_FAILURE);
    } else run->active_ticks += ticks;
}

void world_modes_invalidate(WorldModeRun *run, uint32_t reason)
{
    if (run->phase == WORLD_MODE_STARTING || run->phase == WORLD_MODE_PLAYING ||
        run->phase == WORLD_MODE_BETWEEN_BOSSES) run->invalid_reasons |= reason;
}

void world_modes_abort(WorldModeRun *run)
{
    if (run->phase != WORLD_MODE_STARTING && run->phase != WORLD_MODE_PLAYING &&
        run->phase != WORLD_MODE_BETWEEN_BOSSES) return;
    world_modes_invalidate(run, WORLD_MODE_INVALID_QUIT);
    run->phase = WORLD_MODE_ABORTED;
}

int world_modes_boss_cleared(WorldModeRun *run, unsigned boss, int confirmed)
{
    if (!confirmed || run->rules.mode != WORLD_MODE_BOSS_RUSH ||
        run->phase != WORLD_MODE_PLAYING || boss != run->current_boss) return 0;
    ++run->bosses_cleared;
    if (run->bosses_cleared == run->boss_count) {
        run->victory = 1;
        run->phase = WORLD_MODE_FINISHED;
    } else run->phase = WORLD_MODE_BETWEEN_BOSSES;
    return 1;
}

int world_modes_advance(WorldModeRun *run, const WorldModeCore *core)
{
    if (run->rules.mode != WORLD_MODE_BOSS_RUSH || run->phase != WORLD_MODE_BETWEEN_BOSSES ||
        !world_modes_available(WORLD_MODE_BOSS_RUSH, core) || core->boss_count != run->boss_count) return 0;
    WorldModeLaunch request = {0};
    request.rules = run->rules;
    request.boss = run->current_boss + 1;
    request.raw_dips = 0xffbf;
    request.preserve_players = 1;
    if (!core->launch(core->context, &request)) return 0;
    run->current_boss = request.boss;
    run->phase = WORLD_MODE_STARTING;
    return 1;
}

int world_modes_finish(WorldModeRun *run, uint32_t score, int victory, int confirmed)
{
    if (!confirmed || (run->phase != WORLD_MODE_PLAYING && run->phase != WORLD_MODE_BETWEEN_BOSSES))
        return 0;
    if (score > 999999 || (run->rules.mode == WORLD_MODE_BOSS_RUSH && victory)) return 0;
    run->score = score;
    run->victory = !!victory;
    run->phase = WORLD_MODE_FINISHED;
    return 1;
}

int world_modes_ranked(const WorldModeRun *run)
{
    return rules_valid(run->rules) && run->rules.mode != WORLD_MODE_ARCADE &&
           run->phase == WORLD_MODE_FINISHED && !run->invalid_reasons;
}

int world_modes_observe_world(WorldModeRun *run, const uint8_t *ram, int confirmed)
{
    if (!confirmed || run->phase != WORLD_MODE_PLAYING) return 0;
    unsigned players = ram[0x264] & 3;
    if (run->rules.mode != WORLD_MODE_ARCADE && run->rules.players == 1 && (players & 2))
        world_modes_invalidate(run, WORLD_MODE_INVALID_MULTIPLAYER);
    unsigned record = ram[0x254] | ((unsigned)ram[0x255] << 8);
    if (record < 0x180 || record > 0x230 || (record & 15)) return 0;
    uint32_t score = 0, scale = 1;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned packed = ram[record + 1 + i];
        if ((packed & 15) > 9 || (packed >> 4) > 9) return 0;
        score += ((packed >> 4) * 10 + (packed & 15)) * scale;
        scale *= 100;
    }
    run->score = score;
    return 1;
}

void world_modes_filter_input(const WorldModeRun *run, uint16_t *pads, uint16_t *system)
{
    if (run->rules.mode == WORLD_MODE_ARCADE ||
        (run->phase != WORLD_MODE_STARTING && run->phase != WORLD_MODE_PLAYING &&
         run->phase != WORLD_MODE_BETWEEN_BOSSES)) return;
    *system |= 0x000c; /* External credits cannot add another run. */
    if (run->rules.players == 1) {
        *pads |= 0xff00;
        *system |= 2;
    }
    if (run->phase != WORLD_MODE_STARTING) *system |= 3;
}

void world_modes_records_reset(WorldModeRecords *records)
{
    memset(records, 0, sizeof *records);
}

const WorldModeRecord *world_modes_record(const WorldModeRecords *records, WorldModeRules rules)
{
    if (!rules_valid(rules) || rules.mode == WORLD_MODE_ARCADE) return NULL;
    return &records->entries[record_slot(rules)];
}

int world_modes_record_result(WorldModeRecords *records, const WorldModeRun *run)
{
    if (!world_modes_ranked(run)) return 0;
    WorldModeRecord candidate = {1, run->score, run->bosses_cleared, run->active_ticks};
    WorldModeRecord *old = &records->entries[record_slot(run->rules)];
    if (!better(run->rules.mode, &candidate, old)) return 0;
    *old = candidate;
    return 1;
}

WorldModeFileStatus world_modes_records_load(WorldModeRecords *records, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT ? WORLD_MODE_FILE_MISSING : WORLD_MODE_FILE_IO_ERROR;
    char line[160];
    WorldModeRecords loaded = {0};
    WorldModeFileStatus status = WORLD_MODE_FILE_INVALID;
    if (!fgets(line, sizeof line, file) || strcmp(line, "INTHUNT-MODES 1\n")) goto done;
    while (fgets(line, sizeof line, file)) {
        unsigned mode, timing, players, score, clears;
        uint64_t ticks;
        char extra;
        if (sscanf(line, "%u %u %u %u %u %" SCNu64 " %c", &mode, &timing, &players,
                   &score, &clears, &ticks, &extra) != 6) goto done;
        WorldModeRules rules = {(WorldGameMode)mode, (WorldTimingMode)timing, players};
        if (!rules_valid(rules) || mode == WORLD_MODE_ARCADE || score > 999999 ||
            clears > 64 || (mode == WORLD_MODE_SCORE_ATTACK && clears)) goto done;
        WorldModeRecord *entry = &loaded.entries[record_slot(rules)];
        if (entry->present) goto done;
        entry->present = 1;
        entry->score = score;
        entry->bosses_cleared = clears;
        entry->active_ticks = ticks;
    }
    if (ferror(file)) { status = WORLD_MODE_FILE_IO_ERROR; goto done; }
    *records = loaded;
    status = WORLD_MODE_FILE_OK;
done:
    fclose(file);
    return status;
}

static void merge_records(WorldModeRecords *into, const WorldModeRecords *from)
{
    for (unsigned i = 0; i < WORLD_MODE_RECORD_SLOTS; ++i) {
        WorldGameMode mode = i < 4 ? WORLD_MODE_BOSS_RUSH : WORLD_MODE_SCORE_ATTACK;
        if (better(mode, &from->entries[i], &into->entries[i])) into->entries[i] = from->entries[i];
    }
}

WorldModeFileStatus world_modes_records_save(const WorldModeRecords *records, const char *path)
{
    char lock_path[4096], temporary[4096], text[2048];
    int length = snprintf(lock_path, sizeof lock_path, "%s.lock", path);
    if (length < 0 || (size_t)length >= sizeof lock_path) return WORLD_MODE_FILE_IO_ERROR;
    HANDLE lock = CreateFileA(lock_path, GENERIC_WRITE | DELETE, 0, NULL, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (lock == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ?
               WORLD_MODE_FILE_BUSY : WORLD_MODE_FILE_IO_ERROR;
    }
    WorldModeRecords merged = {0};
    WorldModeFileStatus loaded = world_modes_records_load(&merged, path);
    if (loaded == WORLD_MODE_FILE_IO_ERROR) { CloseHandle(lock); return loaded; }
    merge_records(&merged, records);
    size_t used = (size_t)snprintf(text, sizeof text, "INTHUNT-MODES 1\n");
    for (unsigned i = 0; i < WORLD_MODE_RECORD_SLOTS; ++i) {
        const WorldModeRecord *entry = &merged.entries[i];
        if (!entry->present) continue;
        WorldModeRules rules = {(WorldGameMode)(1 + i / 4), (WorldTimingMode)((i % 4) / 2), 1 + i % 2};
        if (!rules_valid(rules) || entry->score > 999999 || entry->bosses_cleared > 64 ||
            (rules.mode == WORLD_MODE_SCORE_ATTACK && entry->bosses_cleared)) {
            CloseHandle(lock);
            return WORLD_MODE_FILE_INVALID;
        }
        int written = snprintf(text + used, sizeof text - used, "%u %u %u %u %u %" PRIu64 "\n",
                               (unsigned)rules.mode, (unsigned)rules.timing, rules.players,
                               entry->score, entry->bosses_cleared, entry->active_ticks);
        if (written < 0 || (size_t)written >= sizeof text - used) {
            CloseHandle(lock);
            return WORLD_MODE_FILE_IO_ERROR;
        }
        used += (size_t)written;
    }
    static LONG serial;
    HANDLE output = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        length = snprintf(temporary, sizeof temporary, "%s.%lu.%lu.tmp", path,
                          (unsigned long)GetCurrentProcessId(), (unsigned long)InterlockedIncrement(&serial));
        if (length < 0 || (size_t)length >= sizeof temporary) break;
        output = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (output != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    WorldModeFileStatus status = WORLD_MODE_FILE_IO_ERROR;
    if (output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        int ok = WriteFile(output, text, (DWORD)used, &written, NULL) && written == used &&
                 FlushFileBuffers(output);
        if (!CloseHandle(output)) ok = 0;
        if (ok && MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            status = WORLD_MODE_FILE_OK;
        else DeleteFileA(temporary);
    }
    CloseHandle(lock);
    return status;
}
