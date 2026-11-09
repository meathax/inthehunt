#define WIN32_LEAN_AND_MEAN
#include "world_achievements.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

enum {
    ACH_FIRST_CONTACT,
    ACH_MIXED_SALVO,
    ACH_ON_THE_BOARD,
    ACH_CAST_OFF,
    ACH_DEPTH_CHARGE,
    ACH_REFITTED,
    ACH_CONTACT_REPORT,
    ACH_SECOND_WATCH,
    ACH_UNDER_PRESSURE,
    ACH_OPENING_BELL,
    ACH_SEA_TRIALS,
    ACH_STEADY_HELM,
    ACH_WOLF_PACK,
    ACH_BACK_TO_PORT,
    ACH_RAISING_THE_BAR,
    ACH_SALVO_DRILL,
    ACH_BATTLE_STATIONS,
    ACH_BROADSIDE,
    ACH_HALF_WATCH,
    ACH_SOUNDINGS,
    ACH_ONE_CREDIT_UNDER,
    ACH_THREE_DOWN,
    ACH_CLEAR_WATERS,
    ACH_IRON_WATCH,
    ACH_HARD_PORT,
    ACH_TUBES_ONLY,
    ACH_SALVAGE_CREW,
    ACH_DOUBLE_HULL,
    ACH_FULL_MANIFEST,
    ACH_RESERVE_FLEET,
    ACH_PRESSURE_HULL,
    ACH_SILENT_RUNNING,
    ACH_ABYSSAL_COMMAND,
    ACH_TORPEDO_SCHOOL,
    ACH_PERFECT_CHARTS,
    ACH_CONVOY_DUTY,
    ACH_DEEP_PATROL,
    ACH_UNBROKEN_LINE,
    ACH_VICTORY_LEDGER,
    ACH_CLEAN_SWEEP,
};

const WorldAchievementInfo world_achievement_catalog[WORLD_ACHIEVEMENT_COUNT] = {
    { "first_contact", "First Contact", "Defeat the first stage boss.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "mixed_salvo", "Mixed Salvo", "Destroy one enemy with a torpedo and another with the secondary weapon during the same life.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "on_the_board", "On the Board", "Defeat the first boss and finish the run with a ranked score.", "Score Attack", WORLD_ACHIEVEMENT_EASY },
    { "cast_off", "Cast Off", "Destroy your first enemy with a torpedo.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "depth_charge", "Depth Charge", "Destroy your first enemy with the secondary weapon.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "refitted", "Dockyard Refit", "Collect your first torpedo upgrade.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "contact_report", "Contact Report", "Destroy 20 enemies across arcade runs.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "second_watch", "Second Watch", "Enter the second stage.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "under_pressure", "Under Pressure", "Defeat a boss after losing a submarine during that encounter.", "Arcade", WORLD_ACHIEVEMENT_EASY },
    { "opening_bell", "Opening Salvo", "Defeat the opening boss in a ranked Boss Rush.", "Boss Rush", WORLD_ACHIEVEMENT_EASY },
    { "sea_trials", "Sea Trials", "Score a kill with each upgraded torpedo type across your runs: Power, Wave and Spread.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "steady_helm", "Steady Helm", "Clear a full stage from entry to exit without losing your submarine.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "wolf_pack", "Wolf Pack", "Play a full stage together and defeat its boss with both players still on their original lives for that stage.", "Arcade co-op", WORLD_ACHIEVEMENT_MEDIUM },
    { "back_to_port", "Back to Port", "Reach an arcade ending. Continues are allowed.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "raising_the_bar", "Raising the Bar", "Beat a previously recorded personal best in a ranked run after defeating at least one boss.", "Score Attack", WORLD_ACHIEVEMENT_MEDIUM },
    { "salvo_drill", "Salvo Drill", "Destroy 25 enemies with the secondary weapon during one life.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "battle_stations", "Battle Stations", "Clear three stages in one run.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "broadside", "Broadside", "Defeat a boss with a secondary-weapon finishing hit.", "Arcade", WORLD_ACHIEVEMENT_MEDIUM },
    { "half_watch", "Half Watch", "Reach the fourth boss of a ranked Boss Rush.", "Boss Rush", WORLD_ACHIEVEMENT_MEDIUM },
    { "soundings", "Deep Soundings", "Finish a ranked run after clearing three stages.", "Score Attack", WORLD_ACHIEVEMENT_MEDIUM },
    { "one_credit_under", "One Credit Under the Sea", "Reach an ending from a fresh solo start without continuing or adding a second player.", "Arcade solo", WORLD_ACHIEVEMENT_DIFFICULT },
    { "three_down", "Three Down", "Defeat three consecutive bosses in one ranked run without losing a life.", "Boss Rush", WORLD_ACHIEVEMENT_DIFFICULT },
    { "clear_waters", "Clear Waters", "Defeat the final boss in a ranked Boss Rush.", "Boss Rush", WORLD_ACHIEVEMENT_DIFFICULT },
    { "iron_watch", "Iron Watch", "Clear three consecutive full stages without losing a submarine.", "Arcade solo", WORLD_ACHIEVEMENT_DIFFICULT },
    { "hard_port", "Hard to Port", "Reach an ending on Hard with default lives and at most one continue.", "Arcade solo", WORLD_ACHIEVEMENT_DIFFICULT },
    { "tubes_only", "Torpedo Discipline", "Clear a full stage using only torpedoes, including the boss, without losing a life.", "Arcade solo", WORLD_ACHIEVEMENT_DIFFICULT },
    { "salvage_crew", "Salvage Crew", "Defeat three distinct bosses with a secondary-weapon finishing hit in one run.", "Arcade solo", WORLD_ACHIEVEMENT_DIFFICULT },
    { "double_hull", "Double Hull", "Start together and clear three consecutive stages with neither player losing a submarine.", "Arcade co-op", WORLD_ACHIEVEMENT_DIFFICULT },
    { "full_manifest", "Full Manifest", "Finish the entire campaign in ranked Score Attack.", "Score Attack", WORLD_ACHIEVEMENT_DIFFICULT },
    { "reserve_fleet", "Fleet in Reserve", "Clear a ranked Boss Rush with at most one life lost.", "Boss Rush", WORLD_ACHIEVEMENT_DIFFICULT },
    { "pressure_hull", "Pressure Hull", "Clear a ranked Boss Rush without losing a life.", "Boss Rush", WORLD_ACHIEVEMENT_EXTREME },
    { "silent_running", "Silent Running", "Clear the campaign on Normal or Hard with default starting lives, without losing a submarine.", "Arcade solo", WORLD_ACHIEVEMENT_EXTREME },
    { "abyssal_command", "Abyssal Command", "Complete 10 solo one-credit clears on Hard with default starting lives.", "Arcade solo", WORLD_ACHIEVEMENT_EXTREME },
    { "torpedo_school", "Torpedo School", "Finish 15 solo one-credit campaigns on Normal or Hard, scoring kills with Power, Wave and Spread torpedoes in each run.", "Arcade solo", WORLD_ACHIEVEMENT_EXTREME },
    { "perfect_charts", "Perfect Charts", "Clear every campaign stage without losing a life on Hard, three times per stage across solo runs.", "Arcade solo", WORLD_ACHIEVEMENT_EXTREME },
    { "convoy_duty", "Convoy Duty", "Start together and finish five campaigns on Normal or Hard with default lives and no continues by either player.", "Arcade co-op", WORLD_ACHIEVEMENT_EXTREME },
    { "deep_patrol", "Deep Patrol", "Complete 50 ranked Boss Rush clears, including five without losing a life.", "Boss Rush", WORLD_ACHIEVEMENT_EXTREME },
    { "unbroken_line", "Unbroken Line", "Clear three consecutive ranked Boss Rush attempts without losing a life. Quitting or restarting breaks the streak.", "Boss Rush", WORLD_ACHIEVEMENT_EXTREME },
    { "victory_ledger", "Victory Ledger", "Complete 25 full campaigns in ranked Score Attack.", "Score Attack", WORLD_ACHIEVEMENT_EXTREME },
    { "clean_sweep", "Clean Sweep", "Complete five ranked Score Attack campaigns without losing a submarine.", "Score Attack", WORLD_ACHIEVEMENT_EXTREME },
};

static uint64_t increment(uint64_t value)
{
    return value == UINT64_MAX ? value : value + 1;
}

static void award(WorldAchievements *state, unsigned index)
{
    if (state->profile.earned[index]) return;
    state->profile.earned[index] = 1;
    state->profile.earned_at[index] = state->last_utc;
    if (state->notify) state->notify(state->notify_context, index);
}

static unsigned bit_count(unsigned value)
{
    unsigned count = 0;
    while (value) { count += value & 1; value >>= 1; }
    return count;
}

static int solo(const WorldAchievementRun *run)
{
    return bit_count(run->config.players) == 1 && bit_count(run->players) == 1;
}

static int arcade(const WorldAchievementRun *run)
{
    return run->config.mode == WORLD_MODE_ARCADE;
}

void world_achievements_init(WorldAchievements *state, WorldAchievementUnlocked notify, void *context)
{
    memset(state, 0, sizeof *state);
    state->notify = notify;
    state->notify_context = context;
}

void world_achievements_begin(WorldAchievements *state, const WorldAchievementRunConfig *config)
{
    if (!state || !config || (state->run.has_run && state->run.config.run_id == config->run_id)) return;
    if (state->run.active && state->run.config.mode == WORLD_MODE_BOSS_RUSH) {
        state->profile.rush_clean_streak = 0;
        state->pending_clean_rush = 0;
        state->reset_rush_streak = 1;
    }
    memset(&state->run, 0, sizeof state->run);
    state->run.config = *config;
    state->run.has_run = state->run.active = 1;
    state->run.eligible = !!config->eligible && config->players >= 1 && config->players <= 3 &&
                          config->campaign_stages <= WORLD_ACHIEVEMENT_STAGES;
    state->run.players = config->players & 3;
    if (config->campaign_stages && config->campaign_stages <= WORLD_ACHIEVEMENT_STAGES)
        state->profile.campaign_stages = config->campaign_stages;
}

static void boss_defeated(WorldAchievements *state, const WorldAchievementEvent *event)
{
    WorldAchievementRun *run = &state->run;
    unsigned bit = 1u << event->stage;
    if (run->boss_mask & bit) return;
    run->boss_mask |= bit;
    ++run->bosses;
    if (arcade(run)) {
        if (!event->stage) award(state, ACH_FIRST_CONTACT);
        if (run->boss_deaths) award(state, ACH_UNDER_PRESSURE);
        if (event->weapon == WORLD_ACHIEVEMENT_SECONDARY) {
            award(state, ACH_BROADSIDE);
            run->secondary_boss_mask |= bit;
            if (solo(run) && bit_count(run->secondary_boss_mask) >= 3) award(state, ACH_SALVAGE_CREW);
        }
    }
    if (run->config.mode == WORLD_MODE_BOSS_RUSH && run->config.ranked) {
        award(state, ACH_OPENING_BELL);
        if (!run->boss_deaths) ++run->rush_clean_bosses;
        else run->rush_clean_bosses = 0;
        if (run->rush_clean_bosses >= 3) award(state, ACH_THREE_DOWN);
    }
    run->boss_active = 0;
}

static void stage_cleared(WorldAchievements *state, const WorldAchievementEvent *event)
{
    WorldAchievementRun *run = &state->run;
    unsigned bit = 1u << event->stage;
    if (run->stage_mask & bit) return;
    run->stage_mask |= bit;
    ++run->clears;
    if (arcade(run) && run->clears >= 3) award(state, ACH_BATTLE_STATIONS);
    int whole = run->stage_started && run->stage == event->stage;
    if (!whole || !arcade(run)) return;
    if (!run->stage_deaths) {
        award(state, ACH_STEADY_HELM);
        ++run->clean_stages;
        if (solo(run)) {
            if (!run->stage_secondary_shots) award(state, ACH_TUBES_ONLY);
            if (run->clean_stages >= 3) award(state, ACH_IRON_WATCH);
            if (run->config.hard) {
                uint64_t *count = &state->profile.hard_clean_stages[event->stage];
                *count = increment(*count);
                unsigned stages = state->profile.campaign_stages;
                unsigned complete = stages != 0;
                for (unsigned i = 0; i < stages; ++i)
                    if (state->profile.hard_clean_stages[i] < 3) complete = 0;
                if (complete) award(state, ACH_PERFECT_CHARTS);
            }
        }
    } else run->clean_stages = 0;
    if (run->stage_players == 3 && run->players == 3 && !run->stage_deaths) {
        award(state, ACH_WOLF_PACK);
        ++run->coop_clean_stages;
        if (run->config.players == 3 && run->coop_clean_stages >= 3) award(state, ACH_DOUBLE_HULL);
    } else run->coop_clean_stages = 0;
    run->stage_started = 0;
}

static void finish_run(WorldAchievements *state, const WorldAchievementEvent *event)
{
    WorldAchievementRun *run = &state->run;
    WorldAchievementProfile *profile = &state->profile;
    if (run->config.mode == WORLD_MODE_BOSS_RUSH) {
        int clear = event->campaign_complete && run->eligible && run->config.ranked;
        if (clear) {
            award(state, ACH_CLEAR_WATERS);
            profile->rush_clears = increment(profile->rush_clears);
            if (run->deaths <= 1) award(state, ACH_RESERVE_FLEET);
            if (!run->deaths) {
                award(state, ACH_PRESSURE_HULL);
                profile->rush_clean_clears = increment(profile->rush_clean_clears);
                profile->rush_clean_streak = increment(profile->rush_clean_streak);
                state->pending_clean_rush = increment(state->pending_clean_rush);
            }
        }
        if (!clear || run->deaths) {
            profile->rush_clean_streak = 0;
            state->pending_clean_rush = 0;
            state->reset_rush_streak = 1;
        }
        if (profile->rush_clears >= 50 && profile->rush_clean_clears >= 5) award(state, ACH_DEEP_PATROL);
    } else if (run->eligible && run->config.mode == WORLD_MODE_SCORE_ATTACK && run->config.ranked) {
        if (run->bosses) {
            award(state, ACH_ON_THE_BOARD);
            if (run->config.previous_best && run->score > run->config.previous_best) award(state, ACH_RAISING_THE_BAR);
        }
        if (run->clears >= 3) award(state, ACH_SOUNDINGS);
        if (event->campaign_complete) {
            award(state, ACH_FULL_MANIFEST);
            profile->score_clears = increment(profile->score_clears);
            if (!run->deaths) profile->score_clean_clears = increment(profile->score_clean_clears);
            if (profile->score_clears >= 25) award(state, ACH_VICTORY_LEDGER);
            if (profile->score_clean_clears >= 5) award(state, ACH_CLEAN_SWEEP);
        }
    } else if (run->eligible && arcade(run) && event->campaign_complete) {
        award(state, ACH_BACK_TO_PORT);
        int defaults = run->config.default_lives && run->config.normal_or_harder;
        int one_credit = solo(run) && run->config.fresh_start && !run->continues;
        if (one_credit) award(state, ACH_ONE_CREDIT_UNDER);
        if (solo(run) && run->config.hard && run->config.default_lives && run->continues <= 1)
            award(state, ACH_HARD_PORT);
        if (one_credit && defaults) {
            if (!run->deaths) award(state, ACH_SILENT_RUNNING);
            if (run->config.hard) profile->hard_solo_clears = increment(profile->hard_solo_clears);
            if (run->torpedo_types == 7) profile->torpedo_school_clears = increment(profile->torpedo_school_clears);
            if (profile->hard_solo_clears >= 10) award(state, ACH_ABYSSAL_COMMAND);
            if (profile->torpedo_school_clears >= 15) award(state, ACH_TORPEDO_SCHOOL);
        }
        if (run->config.players == 3 && run->players == 3 && defaults && !run->continues) {
            profile->coop_clears = increment(profile->coop_clears);
            if (profile->coop_clears >= 5) award(state, ACH_CONVOY_DUTY);
        }
    }
    run->active = 0;
}

int world_achievements_event(WorldAchievements *state, const WorldAchievementEvent *event)
{
    if (!state || !event || !event->confirmed || !state->run.active ||
        !event->serial || event->serial <= state->run.serial) return 0;
    WorldAchievementRun *run = &state->run;
    run->serial = event->serial;
    state->last_utc = event->utc_seconds;
    if (event->type == WORLD_ACHIEVEMENT_INVALIDATE) run->eligible = 0;
    if (event->type == WORLD_ACHIEVEMENT_ABORT || event->type == WORLD_ACHIEVEMENT_INVALIDATE) {
        if (run->config.mode == WORLD_MODE_BOSS_RUSH) {
            state->profile.rush_clean_streak = 0;
            state->pending_clean_rush = 0;
            state->reset_rush_streak = 1;
        }
        if (event->type == WORLD_ACHIEVEMENT_ABORT) run->active = 0;
        return 1;
    }
    if (event->type == WORLD_ACHIEVEMENT_FINISH) { finish_run(state, event); return 1; }
    if (!run->eligible) return 1;
    unsigned player = event->player;
    unsigned player_bit = player >= 1 && player <= 2 ? 1u << (player - 1) : 0;
    switch (event->type) {
    case WORLD_ACHIEVEMENT_SHOT:
        if (player_bit && event->weapon == WORLD_ACHIEVEMENT_SECONDARY) run->stage_secondary_shots |= player_bit;
        break;
    case WORLD_ACHIEVEMENT_KILL:
        if (!player_bit) break;
        if (event->weapon == WORLD_ACHIEVEMENT_TORPEDO && event->torpedo_type >= 1 && event->torpedo_type <= 3)
            run->torpedo_types |= 1u << (event->torpedo_type - 1);
        if (!arcade(run)) break;
        state->profile.arcade_kills = increment(state->profile.arcade_kills);
        if (state->profile.arcade_kills >= 20) award(state, ACH_CONTACT_REPORT);
        run->life_weapons[player - 1] |= 1u << event->weapon;
        if (run->life_weapons[player - 1] == 3) award(state, ACH_MIXED_SALVO);
        if (event->weapon == WORLD_ACHIEVEMENT_TORPEDO) {
            award(state, ACH_CAST_OFF);
            if (event->torpedo_type >= 1 && event->torpedo_type <= 3)
                state->profile.torpedo_types |= 1u << (event->torpedo_type - 1);
            if (state->profile.torpedo_types == 7) award(state, ACH_SEA_TRIALS);
        } else {
            award(state, ACH_DEPTH_CHARGE);
            if (++run->life_kills[player - 1] >= 25) award(state, ACH_SALVO_DRILL);
        }
        break;
    case WORLD_ACHIEVEMENT_UPGRADE:
        if (arcade(run) && player_bit && event->weapon == WORLD_ACHIEVEMENT_TORPEDO) award(state, ACH_REFITTED);
        break;
    case WORLD_ACHIEVEMENT_STAGE_START:
        if (event->stage >= WORLD_ACHIEVEMENT_STAGES) break;
        run->stage = event->stage; run->stage_started = 1;
        run->stage_players = event->players & 3;
        run->stage_deaths = run->stage_secondary_shots = 0;
        if (arcade(run) && event->stage == 1) award(state, ACH_SECOND_WATCH);
        if (run->config.mode == WORLD_MODE_BOSS_RUSH && run->config.ranked && run->bosses >= 3) award(state, ACH_HALF_WATCH);
        break;
    case WORLD_ACHIEVEMENT_STAGE_CLEAR:
        if (event->stage < WORLD_ACHIEVEMENT_STAGES) stage_cleared(state, event);
        break;
    case WORLD_ACHIEVEMENT_BOSS_START:
        if (!run->boss_active) { run->boss_active = 1; run->boss_deaths = 0; }
        break;
    case WORLD_ACHIEVEMENT_BOSS_KILL:
        if (event->stage < WORLD_ACHIEVEMENT_STAGES) boss_defeated(state, event);
        break;
    case WORLD_ACHIEVEMENT_DEATH:
        if (player_bit) {
            ++run->deaths; run->stage_deaths |= player_bit;
            if (run->boss_active) run->boss_deaths |= player_bit;
            run->life_kills[player - 1] = run->life_weapons[player - 1] = 0;
        }
        break;
    case WORLD_ACHIEVEMENT_CONTINUE:
        ++run->continues;
        break;
    case WORLD_ACHIEVEMENT_JOIN:
        run->players |= event->players & 3;
        break;
    case WORLD_ACHIEVEMENT_SCORE:
        run->score = event->score;
        break;
    default: break;
    }
    return 1;
}
