#ifndef WORLD_MODES_H
#define WORLD_MODES_H
#include <stdint.h>
#include "world_timing_options.h"

typedef enum {
    WORLD_MODE_ARCADE,
    WORLD_MODE_BOSS_RUSH,
    WORLD_MODE_SCORE_ATTACK
} WorldGameMode;

typedef enum {
    WORLD_MODE_IDLE,
    WORLD_MODE_STARTING,
    WORLD_MODE_PLAYING,
    WORLD_MODE_BETWEEN_BOSSES,
    WORLD_MODE_FINISHED,
    WORLD_MODE_ABORTED
} WorldModePhase;

enum {
    WORLD_MODE_INVALID_CHEAT = 1u << 0,
    WORLD_MODE_INVALID_STATE_LOAD = 1u << 1,
    WORLD_MODE_INVALID_FRAME_STEP = 1u << 2,
    WORLD_MODE_INVALID_CONTINUE = 1u << 3,
    WORLD_MODE_INVALID_RULES = 1u << 4,
    WORLD_MODE_INVALID_CORE_FAILURE = 1u << 5,
    WORLD_MODE_INVALID_QUIT = 1u << 6,
    WORLD_MODE_INVALID_MULTIPLAYER = 1u << 7
};

typedef struct {
    WorldGameMode mode;
    WorldTimingMode timing;
    unsigned players; /* Score Attack is solo. Boss Rush has separate crew records. */
} WorldModeRules;

typedef struct {
    WorldModeRules rules;
    unsigned boss;
    uint16_t raw_dips; /* Fixed World defaults, 0xFFBF. */
    unsigned credits;
    int preserve_players; /* Keep lives and equipment between Boss Rush encounters. */
} WorldModeLaunch;

typedef int (*WorldModeLaunchFn)(void *context, const WorldModeLaunch *request);

typedef struct {
    int native_campaign_start;
    unsigned boss_count;
    uint64_t native_bosses;
    /* Launch must initialize the original arena/camera/spawns/music together.
       Failure must leave the current game unchanged. No captured states belong
       in this interface or the distributed game. */
    WorldModeLaunchFn launch;
    void *context;
} WorldModeCore;

typedef struct {
    WorldModeRules rules;
    WorldModePhase phase;
    uint32_t invalid_reasons;
    uint32_t score;
    uint64_t active_ticks;
    unsigned boss_count;
    unsigned bosses_cleared;
    unsigned current_boss;
    int victory;
} WorldModeRun;

typedef struct {
    int present;
    uint32_t score;
    unsigned bosses_cleared;
    uint64_t active_ticks;
} WorldModeRecord;

enum { WORLD_MODE_RECORD_SLOTS = 8 };
typedef struct {
    WorldModeRecord entries[WORLD_MODE_RECORD_SLOTS];
} WorldModeRecords;

typedef enum {
    WORLD_MODE_FILE_OK,
    WORLD_MODE_FILE_MISSING,
    WORLD_MODE_FILE_INVALID,
    WORLD_MODE_FILE_BUSY,
    WORLD_MODE_FILE_IO_ERROR
} WorldModeFileStatus;

const char *world_modes_name(WorldGameMode mode);
int world_modes_available(WorldGameMode mode, const WorldModeCore *core);
const char *world_modes_unavailable_reason(WorldGameMode mode, const WorldModeCore *core);
void world_modes_reset(WorldModeRun *run);
/* Called for a fresh game. Arcade leaves ordinary boot/start handling to the
   host. Ranked modes request a fresh default-rule launch through core->launch.
   A failed launch leaves run unchanged. */
int world_modes_begin(WorldModeRun *run, WorldModeRules rules, const WorldModeCore *core);
/* Host confirms that the player can control the initialized encounter. */
int world_modes_started(WorldModeRun *run);
/* Count confirmed physical board ticks while active. Busy VBlanks count,
   paused time and Boss Rush transitions do not. Presentation FPS is unrelated. */
void world_modes_tick(WorldModeRun *run, uint64_t ticks, int paused, int confirmed);
void world_modes_invalidate(WorldModeRun *run, uint32_t reason);
void world_modes_abort(WorldModeRun *run);
/* Call only for the actual enemy's confirmed death. A duplicate or out-of-order
   clear is rejected. Advance is deferred until the host's safe frame boundary. */
int world_modes_boss_cleared(WorldModeRun *run, unsigned boss, int confirmed);
int world_modes_advance(WorldModeRun *run, const WorldModeCore *core);
/* Score Attack ends at game over or ending. Boss Rush ends at game over, or
   automatically after its final confirmed boss. */
int world_modes_finish(WorldModeRun *run, uint32_t score, int victory, int confirmed);
int world_modes_ranked(const WorldModeRun *run);
/* Observe the genuine P1 packed-BCD score. Read-only; never alters arcade RAM. */
int world_modes_observe_world(WorldModeRun *run, const uint8_t *ram, int confirmed);
/* Filter external cabinet inputs. Host-generated startup coin/start pulses are
   applied after this filter. Arcade input is unchanged. */
void world_modes_filter_input(const WorldModeRun *run, uint16_t *pads, uint16_t *system);

void world_modes_records_reset(WorldModeRecords *records);
const WorldModeRecord *world_modes_record(const WorldModeRecords *records, WorldModeRules rules);
int world_modes_record_result(WorldModeRecords *records, const WorldModeRun *run);
/* Dedicated user profile, independent of the arcade's original score table.
   Save merges best results under an exclusive lock, then replaces atomically.
   The containing directory must already exist. Failed loads leave records
   unchanged. A busy save can be retried by the host. */
WorldModeFileStatus world_modes_records_load(WorldModeRecords *records, const char *path);
WorldModeFileStatus world_modes_records_save(const WorldModeRecords *records, const char *path);
#endif
