#ifndef WORLD_SERVICE_H
#define WORLD_SERVICE_H
#include <stdint.h>
#include "world_achievements.h"

/* Turns documented World work RAM into service-record events. Only facts
 * that follow with certainty from this state are reported; docs/HOST.md
 * lists them and the events the game core does not expose yet. */

enum {
    WORLD_SERVICE_CAMPAIGN_STAGES = 6, /* Master 0CED leaves for the ending at stage index 6. */
    WORLD_SERVICE_TITLE_MASTER = 0x0a09
};

/* One completed, live board frame. */
typedef struct {
    unsigned master;   /* RAM[0x80], word: master routine. */
    unsigned stage;    /* RAM[0x84], word: stage index, 0 for the first stage. */
    unsigned p1_lives; /* RAM[0x118], word: Player 1 stock. */
    unsigned players;  /* RAM[0x264] bits 0-1: submarines in the water. */
    int demo;          /* RAM[0x28C] bit 0: attract demo is replaying ROM inputs. */
    uint32_t score;    /* Player 1 score from world_modes_observe_world. */
    int score_valid;
    uint64_t utc_seconds;
} WorldServiceObservation;

typedef struct {
    WorldAchievements *achievements;
    uint64_t run_id, serial;
    unsigned active;      /* A credited run is in progress. */
    unsigned members;     /* Players who have taken part in it, bits 0-1. */
    unsigned p1_stocked;  /* Player 1 has received a stock in this run. */
    unsigned primed, previous_players, previous_lives, previous_stage;
    uint32_t previous_score;
} WorldService;

/* first_run_id seeds run ids, which must differ between runs of a session. */
void world_service_init(WorldService *service, WorldAchievements *achievements, uint64_t first_run_id);
/* Call once after each successful board frame, never for a paused frame. */
void world_service_observe(WorldService *service, const WorldServiceObservation *frame);
/* The host is closing with a run in progress. */
void world_service_abort(WorldService *service, uint64_t utc_seconds);
#endif
