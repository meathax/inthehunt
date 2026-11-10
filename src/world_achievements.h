#ifndef WORLD_ACHIEVEMENTS_H
#define WORLD_ACHIEVEMENTS_H
#include <stddef.h>
#include <stdint.h>
#include "world_modes.h"

enum { WORLD_ACHIEVEMENT_COUNT = 40, WORLD_ACHIEVEMENT_STAGES = 16 };
typedef enum {
    WORLD_ACHIEVEMENT_EASY, WORLD_ACHIEVEMENT_MEDIUM,
    WORLD_ACHIEVEMENT_DIFFICULT, WORLD_ACHIEVEMENT_EXTREME
} WorldAchievementTier;
typedef struct {
    const char *id, *name, *description, *mode;
    WorldAchievementTier tier;
} WorldAchievementInfo;
extern const WorldAchievementInfo world_achievement_catalog[WORLD_ACHIEVEMENT_COUNT];

typedef enum { WORLD_ACHIEVEMENT_TORPEDO, WORLD_ACHIEVEMENT_SECONDARY } WorldAchievementWeapon;
typedef enum {
    WORLD_ACHIEVEMENT_SHOT, WORLD_ACHIEVEMENT_KILL, WORLD_ACHIEVEMENT_UPGRADE,
    WORLD_ACHIEVEMENT_STAGE_START, WORLD_ACHIEVEMENT_STAGE_CLEAR,
    WORLD_ACHIEVEMENT_BOSS_START, WORLD_ACHIEVEMENT_BOSS_KILL,
    WORLD_ACHIEVEMENT_DEATH, WORLD_ACHIEVEMENT_CONTINUE, WORLD_ACHIEVEMENT_JOIN,
    WORLD_ACHIEVEMENT_SCORE, WORLD_ACHIEVEMENT_FINISH,
    WORLD_ACHIEVEMENT_ABORT, WORLD_ACHIEVEMENT_INVALIDATE
} WorldAchievementEventType;
typedef struct {
    uint64_t serial, utc_seconds;
    WorldAchievementEventType type;
    unsigned player, stage, players, score;
    WorldAchievementWeapon weapon;
    unsigned torpedo_type; /* 0 standard, 1 Power, 2 Wave, 3 Spread */
    unsigned campaign_complete;
    unsigned confirmed; /* Only committed live gameplay events are accepted. */
} WorldAchievementEvent;
typedef struct {
    uint64_t run_id;
    WorldGameMode mode;
    unsigned eligible, ranked, fresh_start, default_lives;
    unsigned normal_or_harder, hard, players, campaign_stages;
    unsigned previous_best;
} WorldAchievementRunConfig;
typedef struct {
    uint8_t earned[WORLD_ACHIEVEMENT_COUNT];
    uint64_t earned_at[WORLD_ACHIEVEMENT_COUNT];
    uint64_t arcade_kills, hard_solo_clears, torpedo_school_clears, coop_clears;
    uint64_t rush_clears, rush_clean_clears, rush_clean_streak;
    uint64_t score_clears, score_clean_clears;
    uint64_t hard_clean_stages[WORLD_ACHIEVEMENT_STAGES];
    unsigned torpedo_types, campaign_stages;
} WorldAchievementProfile;
typedef struct {
    WorldAchievementRunConfig config;
    uint64_t serial;
    unsigned has_run, active, eligible, players, deaths, continues, score, bosses, clears;
    unsigned stage, stage_started, stage_players, stage_deaths, stage_secondary_shots;
    unsigned boss_active, boss_deaths, clean_stages, coop_clean_stages;
    unsigned stage_mask, boss_mask, secondary_boss_mask, torpedo_types;
    unsigned life_kills[2], life_weapons[2], rush_clean_bosses;
} WorldAchievementRun;
typedef void (*WorldAchievementUnlocked)(void *context, unsigned index);
typedef struct {
    WorldAchievementProfile profile, saved;
    WorldAchievementRun run;
    WorldAchievementUnlocked notify;
    void *notify_context;
    uint64_t last_utc, pending_clean_rush;
    unsigned reset_rush_streak;
} WorldAchievements;

void world_achievements_init(WorldAchievements *state, WorldAchievementUnlocked notify, void *context);
void world_achievements_begin(WorldAchievements *state, const WorldAchievementRunConfig *config);
/* Event serials increase within a run. Repeated/old serials are ignored. */
int world_achievements_event(WorldAchievements *state, const WorldAchievementEvent *event);
void world_achievements_progress(const WorldAchievements *state, unsigned index, uint64_t *current, uint64_t *target);
/* Load returns 1 on success, 0 when absent, -1 on invalid data or I/O error.
 * Failed loads preserve state. Save merges concurrent earned IDs/counter
 * increments under a file lock and replaces the profile atomically.
 * The consecutive Boss Rush award is committed by save after run completion. */
int world_achievements_load(WorldAchievements *state, const char *path);
int world_achievements_save(WorldAchievements *state, const char *path);
#endif
