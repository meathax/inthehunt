/* Service-record events derived from documented World work RAM: what is
 * reported, and which awards stay locked because the core does not expose
 * the facts they need. */
#include "../src/world_service.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){printf("FAIL line%d %s\n",__LINE__,#c);return 1;}}while(0)

enum { TITLE = 0x0a09, PRESS_START = 0x1ea5, PLAY = 0x0b2e, STAGE_CLEAR = 0x0ced, GAME_OVER = 0xc04 };
static WorldAchievements record;
static WorldService service;
static WorldServiceObservation frame;
static unsigned unlocks;

static void unlocked(void *context, unsigned index) { (void)context; (void)index; ++unlocks; }
static unsigned find(const char *id)
{
    for (unsigned i = 0; i < WORLD_ACHIEVEMENT_COUNT; ++i)
        if (!strcmp(world_achievement_catalog[i].id, id)) return i;
    return 0;
}
static int earned(const char *id) { return record.profile.earned[find(id)]; }
static void see(unsigned master, unsigned stage, unsigned lives, unsigned players)
{
    frame.master = master; frame.stage = stage; frame.p1_lives = lives; frame.players = players;
    ++frame.utc_seconds;
    world_service_observe(&service, &frame);
}
static void fresh(void)
{
    world_achievements_init(&record, unlocked, NULL);
    world_service_init(&service, &record, 100);
    memset(&frame, 0, sizeof frame);
    frame.score_valid = 1;
    unlocks = 0;
    see(TITLE, 0, 0, 0);
}
/* Player 1 presses Start on the title and the first stage begins. */
static void start_p1(void)
{
    see(PRESS_START, 0, 0, 1);
    see(PLAY, 0, 3, 1);
}

int main(void)
{
    /* Attract play sets the player bits and its own stage; none of it counts. */
    fresh();
    frame.demo = 1;
    see(PLAY, 2, 0, 3); see(PLAY, 3, 0, 3);
    frame.demo = 0;
    see(TITLE, 0, 0, 0);
    CHECK(!service.active && !record.run.has_run && !unlocks);
    /* Player bits that rise outside the title do not start a run. */
    see(PLAY, 0, 3, 1);
    CHECK(!service.active);

    /* A solo run: entering stage two, clean stages, a loss, a continue, the ending. */
    fresh();
    start_p1();
    CHECK(service.active && record.run.active && record.run.config.fresh_start && record.run.continues == 0);
    frame.score = 2500;
    see(PLAY, 0, 3, 1);
    CHECK(record.run.score == 2500);
    see(PLAY, 0, 3, 0); see(PLAY, 0, 3, 1);       /* The bit alone dropping is not a loss. */
    CHECK(record.run.deaths == 0);
    see(PLAY, 0, 4, 1);                           /* An extra submarine. */
    CHECK(record.run.deaths == 0 && record.run.continues == 0);
    see(STAGE_CLEAR, 1, 4, 1);
    CHECK(earned("second_watch") && earned("steady_helm"));
    CHECK(!earned("tubes_only"));                 /* Shots are not reported. */
    see(PLAY, 1, 3, 0);                           /* A submarine is lost. */
    CHECK(record.run.deaths == 1);
    see(PLAY, 1, 3, 1);
    see(STAGE_CLEAR, 2, 3, 1);
    CHECK(record.run.clean_stages == 0 && record.run.clears == 2);
    see(PLAY, 2, 2, 0); see(PLAY, 2, 1, 0); see(PLAY, 2, 0, 0);
    CHECK(record.run.deaths == 4 && service.active);
    see(GAME_OVER, 2, 0, 0);
    see(PLAY, 2, 3, 1);                           /* Stock returns from zero: a continue. */
    CHECK(record.run.continues == 1);
    see(STAGE_CLEAR, 3, 3, 1);
    CHECK(earned("battle_stations") && !earned("iron_watch"));
    see(STAGE_CLEAR, 4, 3, 1); see(STAGE_CLEAR, 5, 3, 1);
    CHECK(!earned("back_to_port"));
    see(STAGE_CLEAR, 6, 3, 1);
    CHECK(earned("back_to_port") && !earned("one_credit_under") && !service.active && !record.run.active);
    /* Nothing that needs kills, bosses, upgrades or board settings unlocked. */
    CHECK(!earned("first_contact") && !earned("cast_off") && !earned("refitted") && !earned("contact_report") &&
          !earned("under_pressure") && !earned("broadside") && !earned("silent_running") && !earned("hard_port"));
    see(TITLE, 0, 0, 0);
    CHECK(!service.active);

    /* A clean one-credit solo campaign. */
    unsigned first_run = (unsigned)record.run.config.run_id;
    start_p1();
    CHECK(record.run.config.run_id != first_run && record.run.active);
    for (unsigned stage = 1; stage <= 6; ++stage) see(STAGE_CLEAR, stage, 3, 1);
    CHECK(earned("one_credit_under") && earned("iron_watch"));
    CHECK(!earned("silent_running") && !earned("tubes_only") && !earned("abyssal_command"));
    CHECK(record.profile.hard_solo_clears == 0 && record.profile.torpedo_school_clears == 0);

    /* Game over: the run ends when the board returns to the title. */
    fresh();
    start_p1();
    see(PLAY, 0, 0, 0); see(GAME_OVER, 0, 0, 0);
    CHECK(service.active);
    see(TITLE, 0, 0, 0);
    CHECK(!service.active && !record.run.active && !unlocks);

    /* Player 2 joins: stage clears are withheld because only Player 1's stock is seen. */
    fresh();
    start_p1();
    see(PLAY, 0, 3, 3);
    CHECK(record.run.players == 3);
    see(STAGE_CLEAR, 1, 3, 3);
    CHECK(earned("second_watch") && !earned("steady_helm") && !earned("wolf_pack") && record.run.clears == 0);
    for (unsigned stage = 2; stage <= 6; ++stage) see(STAGE_CLEAR, stage, 3, 3);
    CHECK(earned("back_to_port") && !earned("one_credit_under") && !earned("battle_stations") &&
          !earned("double_hull") && record.profile.coop_clears == 0);

    /* A Player 2 start is never vouched for as one credit: its continues are unseen. */
    fresh();
    see(PRESS_START, 0, 0, 2); see(PLAY, 0, 0, 2);
    CHECK(service.active && !record.run.config.fresh_start);
    for (unsigned stage = 1; stage <= 6; ++stage) see(STAGE_CLEAR, stage, 0, 2);
    CHECK(earned("back_to_port") && !earned("one_credit_under") && !earned("steady_helm"));

    /* Closing the game mid-run aborts it. */
    fresh();
    start_p1();
    world_service_abort(&service, 5000);
    CHECK(!service.active && !record.run.active);
    world_service_abort(&service, 5001);

    puts("run detection, attract rejection, stage, loss, continue, join and ending events pass; unwired awards stay locked");
    return 0;
}
