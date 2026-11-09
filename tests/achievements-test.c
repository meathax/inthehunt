/* Service-record rules, lifetime progress and the saved profile: merge of two
 * game instances, the ordered Boss Rush streak, and rejection of bad files. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/world_achievements.h"
static unsigned notified[WORLD_ACHIEVEMENT_COUNT];
static void note(void *c, unsigned i) { (void)c; ++notified[i]; }
static uint64_t serial;
static void ev(WorldAchievements *s, WorldAchievementEventType t, unsigned player, WorldAchievementWeapon w, unsigned tt, unsigned complete)
{
    WorldAchievementEvent e = {0};
    e.serial = ++serial; e.utc_seconds = 1000 + serial; e.type = t; e.player = player; e.weapon = w;
    e.torpedo_type = tt; e.campaign_complete = complete; e.confirmed = 1; e.players = 1;
    assert(world_achievements_event(s, &e));
}
static unsigned find(const char *id){for(unsigned i=0;i<WORLD_ACHIEVEMENT_COUNT;++i)if(!strcmp(world_achievement_catalog[i].id,id))return i;assert(0);return 0;}
static void begin(WorldAchievements *s, uint64_t id, WorldGameMode mode)
{
    WorldAchievementRunConfig c = {0};
    c.run_id = id; c.mode = mode; c.eligible = 1; c.ranked = mode != WORLD_MODE_ARCADE; c.fresh_start = 1;
    c.default_lives = 1; c.normal_or_harder = 1; c.players = 1; c.campaign_stages = 6;
    world_achievements_begin(s, &c);
}
static int exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* Optional argument: a writable scratch directory. */
int main(int argc, char **argv)
{
    char path[4096], lock[4112];
    snprintf(path, sizeof path, "%s/achievements-test-profile.txt", argc > 1 ? argv[1] : ".");
    snprintf(lock, sizeof lock, "%s.lock", path);
    remove(path);
    WorldAchievements a, b;
    uint64_t cur, tgt;
    world_achievements_init(&a, note, NULL);
    assert(world_achievements_load(&a, path) == 0);
    begin(&a, 1, WORLD_MODE_ARCADE);
    for (unsigned i = 0; i < 12; ++i) ev(&a, WORLD_ACHIEVEMENT_KILL, 1, WORLD_ACHIEVEMENT_TORPEDO, 1, 0);
    world_achievements_progress(&a, find("contact_report"), &cur, &tgt);
    assert(cur == 12 && tgt == 20);
    world_achievements_progress(&a, find("first_contact"), &cur, &tgt);
    assert(cur == 0 && tgt == 0);
    assert(world_achievements_save(&a, path) == 1);
    /* A second instance loads the same file and adds its own kills. */
    world_achievements_init(&b, note, NULL);
    assert(world_achievements_load(&b, path) == 1);
    assert(b.profile.arcade_kills == 12 && b.profile.earned[find("cast_off")]);
    begin(&b, 2, WORLD_MODE_ARCADE);
    for (unsigned i = 0; i < 5; ++i) ev(&b, WORLD_ACHIEVEMENT_KILL, 1, WORLD_ACHIEVEMENT_SECONDARY, 0, 0);
    for (unsigned i = 0; i < 5; ++i) ev(&a, WORLD_ACHIEVEMENT_KILL, 1, WORLD_ACHIEVEMENT_TORPEDO, 2, 0);
    assert(world_achievements_save(&b, path) == 1 && b.profile.arcade_kills == 17);
    assert(!notified[find("contact_report")]);
    /* Neither instance saw 20 kills alone; the merged total earns the award once. */
    assert(world_achievements_save(&a, path) == 1 && a.profile.arcade_kills == 22);
    assert(a.profile.earned[find("contact_report")] && notified[find("contact_report")] == 1);
    assert(a.profile.earned[find("depth_charge")]);
    assert(world_achievements_save(&a, path) == 1 && a.profile.arcade_kills == 22);
    assert(notified[find("contact_report")] == 1);
    /* Boss Rush streak: two clean clears, save, one more -> Unbroken Line at save. */
    for (unsigned run = 0; run < 3; ++run) {
        begin(&a, 10 + run, WORLD_MODE_BOSS_RUSH);
        ev(&a, WORLD_ACHIEVEMENT_FINISH, 1, 0, 0, 1);
        if (run == 1) { assert(world_achievements_save(&a, path) == 1); assert(!a.profile.earned[find("unbroken_line")]); }
    }
    world_achievements_progress(&a, find("unbroken_line"), &cur, &tgt);
    assert(cur == 3 && tgt == 3 && !a.profile.earned[find("unbroken_line")]);
    assert(world_achievements_save(&a, path) == 1 && a.profile.earned[find("unbroken_line")]);
    assert(a.profile.rush_clears == 3 && a.profile.rush_clean_streak == 3);
    /* A broken streak discards the stored run before new clean clears count. */
    begin(&a, 20, WORLD_MODE_BOSS_RUSH);
    ev(&a, WORLD_ACHIEVEMENT_ABORT, 1, 0, 0, 0);
    begin(&a, 21, WORLD_MODE_BOSS_RUSH);
    ev(&a, WORLD_ACHIEVEMENT_FINISH, 1, 0, 0, 1);
    assert(world_achievements_save(&a, path) == 1 && a.profile.rush_clean_streak == 1 && a.profile.rush_clears == 4);
    /* Round trip. */
    world_achievements_init(&b, NULL, NULL);
    assert(world_achievements_load(&b, path) == 1);
    assert(!memcmp(&a.profile, &b.profile, sizeof a.profile));
    /* Corrupt and oversized files are rejected and leave state alone, then replaced by save. */
    FILE *f = fopen(path, "w"); fputs("INTHUNT-SERVICE 1\ncount arcade_kills 5\ncount arcade_kills 6\n", f); fclose(f);
    assert(world_achievements_load(&b, path) == -1 && b.profile.arcade_kills == 22);
    f = fopen(path, "w"); fputs("INTHUNT-SERVICE 2\n", f); fclose(f);
    assert(world_achievements_load(&b, path) == -1);
    f = fopen(path, "w"); fputs("INTHUNT-SERVICE 1\r\nearned cast_off 7\r\nstage 2 3\r\nstate torpedo_types 9\r\n", f); fclose(f);
    assert(world_achievements_load(&b, path) == -1);
    f = fopen(path, "w"); fputs("INTHUNT-SERVICE 1\r\nearned cast_off 7\r\nstage 2 3\r\n", f); fclose(f);
    assert(world_achievements_load(&b, path) == 1 && b.profile.hard_clean_stages[2] == 3 && b.profile.earned_at[find("cast_off")] == 7);
    assert(world_achievements_save(&a, path) == 1);
    assert(!exists(lock));
    remove(path);
    puts("achievement progress, persistence, merge, streak and rejection cases passed");
    return 0;
}
