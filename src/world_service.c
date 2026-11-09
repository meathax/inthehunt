#include "world_service.h"
#include <string.h>

/* Masters that draw the title and take the first Start press (world_title.c
   and the title list in world_frame.c). A run can only begin from these. */
static int title_master(unsigned master)
{
    return master == 0x0a09 || master == 0x1d52 || master == 0x1de6 || master == 0x1e6c ||
           master == 0x1ea5 || master == 0x1f52 || master == 0x1f85 || master == 0x1f19;
}

static void send(WorldService *service, const WorldServiceObservation *frame,
                 WorldAchievementEventType type, unsigned player, unsigned stage, int complete)
{
    WorldAchievementEvent event;
    memset(&event, 0, sizeof event);
    event.serial = ++service->serial;
    event.utc_seconds = frame->utc_seconds;
    event.type = type;
    event.player = player;
    event.stage = stage;
    event.players = service->members;
    event.score = frame->score;
    event.campaign_complete = complete ? 1 : 0;
    event.confirmed = 1;
    world_achievements_event(service->achievements, &event);
}

void world_service_init(WorldService *service, WorldAchievements *achievements, uint64_t first_run_id)
{
    memset(service, 0, sizeof *service);
    service->achievements = achievements;
    service->run_id = first_run_id;
}

static void begin(WorldService *service, const WorldServiceObservation *frame)
{
    WorldAchievementRunConfig config;
    memset(&config, 0, sizeof config);
    config.run_id = ++service->run_id;
    config.mode = WORLD_MODE_ARCADE;
    config.eligible = 1;
    config.players = frame->players;
    config.campaign_stages = WORLD_SERVICE_CAMPAIGN_STAGES;
    /* Only Player 1's stock is observed, so only a Player 1 start can be
       vouched for as a one-credit run. Difficulty and starting lives are not
       read back from the board: those flags, and shot reporting, stay clear,
       which keeps the awards that need them locked. */
    config.fresh_start = frame->players == 1;
    world_achievements_begin(service->achievements, &config);
    service->active = 1;
    service->serial = 0;
    service->members = frame->players;
    service->p1_stocked = frame->p1_lives != 0;
    service->previous_lives = frame->p1_lives;
    service->previous_stage = frame->stage;
    service->previous_score = frame->score;
    /* The title masters require stage index 0, so this is the entry to it. */
    if (frame->stage < WORLD_SERVICE_CAMPAIGN_STAGES)
        send(service, frame, WORLD_ACHIEVEMENT_STAGE_START, 0, frame->stage, 0);
}

static void advance(WorldService *service, const WorldServiceObservation *frame)
{
    unsigned joined = frame->players & ~service->members;
    if (joined) {
        service->members |= joined;
        send(service, frame, WORLD_ACHIEVEMENT_JOIN, 0, frame->stage, 0);
    }
    /* Every lost submarine takes one from the stock, the last included. A
       stock that returns from zero after the first issue is a continue. */
    if (frame->p1_lives < service->previous_lives)
        send(service, frame, WORLD_ACHIEVEMENT_DEATH, 1, service->previous_stage, 0);
    else if (!service->previous_lives && frame->p1_lives) {
        if (service->p1_stocked) send(service, frame, WORLD_ACHIEVEMENT_CONTINUE, 1, frame->stage, 0);
        service->p1_stocked = 1;
    }
    service->previous_lives = frame->p1_lives;

    if (frame->score_valid && frame->score != service->previous_score) {
        service->previous_score = frame->score;
        send(service, frame, WORLD_ACHIEVEMENT_SCORE, 1, frame->stage, 0);
    }

    /* Master 0CED adds one to the stage index after a stage is cleared. */
    if (frame->stage == service->previous_stage + 1) {
        /* Player 2's losses are not observed, so a stage that Player 2 took
           part in is never reported as cleared: its awards depend on who
           lost a submarine. */
        if (!(service->members & 2))
            send(service, frame, WORLD_ACHIEVEMENT_STAGE_CLEAR, 0, service->previous_stage, 0);
        if (frame->stage >= WORLD_SERVICE_CAMPAIGN_STAGES) {
            send(service, frame, WORLD_ACHIEVEMENT_FINISH, 0, service->previous_stage, 1);
            service->active = 0;
        } else send(service, frame, WORLD_ACHIEVEMENT_STAGE_START, 0, frame->stage, 0);
    }
    service->previous_stage = frame->stage;

    /* Back at the title: the run ended without reaching the ending. */
    if (service->active && frame->master == WORLD_SERVICE_TITLE_MASTER) {
        send(service, frame, WORLD_ACHIEVEMENT_FINISH, 0, frame->stage, 0);
        service->active = 0;
    }
}

void world_service_observe(WorldService *service, const WorldServiceObservation *frame)
{
    if (!service || !service->achievements || !frame) return;
    unsigned players = frame->players & 3;
    /* Attract play sets the same player bits; nothing in it counts. */
    if (frame->demo) {
        service->previous_players = 0;
        service->primed = 1;
        return;
    }
    if (service->active) advance(service, frame);
    else if (service->primed && players && !service->previous_players && title_master(frame->master)) {
        WorldServiceObservation start = *frame;
        start.players = players;
        begin(service, &start);
    }
    service->previous_players = players;
    service->primed = 1;
}

void world_service_abort(WorldService *service, uint64_t utc_seconds)
{
    if (!service || !service->active) return;
    WorldServiceObservation frame;
    memset(&frame, 0, sizeof frame);
    frame.utc_seconds = utc_seconds;
    frame.score = service->previous_score;
    send(service, &frame, WORLD_ACHIEVEMENT_ABORT, 0, service->previous_stage, 0);
    service->active = 0;
}
