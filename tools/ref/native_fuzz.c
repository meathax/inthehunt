/* Developer check: fuzz the native game from cold boot with the pass-keyed
 * bot that tools/ref/ref_capture.lua runs under `pk=true`. Both bots read
 * only a count of actor passes and game state, so the same options give the
 * same play natively and in MAME (verified by comparing --calls output with a
 * scout's calls.txt). A STOP found here at counter key K is recorded in MAME
 * with the same options and a window around frame K (frame = key + lag).
 * Derived from native_run.c; build (after `make -f tools/ref/build.mk`):
 *   gcc -std=c99 -O1 -g -Wall -Wextra -c tools/ref/native_fuzz.c -o /tmp/native_fuzz.o
 *   then link /tmp/native_fuzz.o with g++ against every object in .local/bin/obj except native_run.o
 *
 *   native_fuzz DATA_DIR TICKS [--stage N] [--refill] [--random SEED] [--random-from K]
 *       [--random-until K] [--still-from K] [--arm K MODE LEVEL KIND LEVEL] [--follow] [--climb PERIOD] [--steer FROM X] [--aim5]
 *       [--continue] [--skip-stops] [--calls FILE] [--shots DIR EVERY]
 */
#include "../../src/world_boot.h"
#include "../../src/world_dispatch.h"
#include "../../src/world_frame.h"
#include "../../src/world_palette.h"
#include "../../src/world_video.h"
#include "../../src/world_audio_driver.h"
#include "../../src/world_ga20.h"
#include "../../src/world_ym.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t rom[1 << 20], gfx1[2 << 20], gfx2[4 << 20], ram[65536], tiles[65536];
static uint8_t oam[2048], palette[2048];
static uint16_t layer_master[3] = {0, 0x41, 2};
static uint8_t sound_rom[WORLD_AUDIO_ROM_SIZE], samples[524288], sound_state[WORLD_AUDIO_STATE_SIZE];
static WorldGa20 pcm;
static uint64_t sound_frame_phase, sound_timer_phase;
static unsigned sound_gaps;
static char failure[256];
static FILE *calls_file;
static unsigned game_passes, opt_skip, stops;
static uint32_t stop_address[64];
static int opt_stage = -1, opt_staged, opt_refill, opt_follow, opt_climb, opt_aim5, opt_continue;
static long opt_random, opt_random_from, opt_random_until = 1L << 30, opt_steer_from = -1, opt_steer_x, opt_still_from = -1, opt_arm_from = -1;
static int opt_arm[4] = {-1, -1, -1, -1};



static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static const struct { uint32_t handler; int pri; } aim5[] = {
    {0x7c690, 1}, {0x7b572, 1}, {0x7b61e, 1}, {0x7b64e, 1}, {0x7b696, 1}, {0x7b6ef, 1}, {0x7bb61, 1},
    {0x7b050, 3}, {0x7b2a4, 3}, {0x7b1ff, 3}, {0x7af56, 5} };

/* Must stay identical to the pk=true branch of the bot in ref_capture.lua. */
static void bot(unsigned *held_out, unsigned *buttons_out)
{
    /* Key: the pass counter until the first stage pass, then 1454 plus the
     * number of actor passes since (MAME lag frames advance RAM[274] too). */
    unsigned k = game_passes ? 1454 + game_passes : word(ram + 0x274) + 580, master = word(ram + 0x80);
    if (!game_passes && k >= 1400) k = 1399;
    unsigned held = 0, buttons = 0;
    if (k >= 1200 && k < 1206) buttons |= 4;
    if (k >= 1280 && k < 1286) buttons |= 1;
    int play = k >= 1400, phase = (k / 90) % 4;
    int right = phase != 3, left = 0, up = phase == 1, down = phase == 3;
    unsigned sub = word(ram + 0x90);
    if (play && opt_steer_from >= 0 && (long)k >= opt_steer_from) {
        long x = word(ram + ((sub + 0x21) & 0xffff));
        right = x < opt_steer_x - 8; left = x > opt_steer_x + 8;
    }
    if (opt_follow && master == 0xb2e) {
        unsigned v = word(ram + 0x270);
        up = v == 4 || (up && v != 2);
        down = v == 2 || (down && v != 4);
    }
    if (opt_climb) { int sweep = (k / (unsigned)opt_climb) % 2; right = sweep == 0; left = sweep == 1; up = 1; down = 0; }
    if (opt_aim5 && play) {
        unsigned a = 0x400, best = 0; int best_pri = 0;
        for (unsigned n = 0; n < 300; ++n) {
            uint32_t target = ((word(ram + a + 2) << 4) + word(ram + a)) & 0xfffff;
            if (target == 0x96a) break;
            for (unsigned i = 0; i < sizeof aim5 / sizeof *aim5; ++i)
                if (aim5[i].handler == target && (!best || aim5[i].pri < best_pri)) { best = a; best_pri = aim5[i].pri; }
            a = word(ram + a + 4);
        }
        if (best) {
            int ex = (int16_t)(word(ram + best + 0x21) - 70 - word(ram + ((sub + 0x21) & 0xffff)));
            int ey = (int16_t)(word(ram + best + 0x24) - word(ram + ((sub + 0x24) & 0xffff)));
            int facing_left = ram[(sub + 0x29) & 0xffff] & 1, inside = ex >= -20 && ex <= 20;
            right = ex > 20; left = ex < -20;
            if (inside) right = facing_left;
            up = ey > 3; down = ey < -3;
        }
    }
    int fire1 = k % 12 < 6, fire2 = k % 16 < 8;
    if (opt_random && (long)k >= opt_random_from && (long)k < opt_random_until) {
        uint32_t h = (uint32_t)opt_random * 0x9E3779B1u + (k / 24) * 0x85EBCA6Bu;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        unsigned style = (unsigned)opt_random % 3, d = h % 8, v = (h >> 3) % 4;
        if (style == 0) { right = d < 5; left = d == 5; }
        else if (style == 1) { right = d < 3; left = d >= 3 && d < 6; }
        else { right = d < 4; left = d == 4; }
        up = v == 0 || (style == 2 && v == 2); down = v == 1;
        fire1 = (h >> 5) % 4 != 0 && k % 12 < 6;
        fire2 = (h >> 7) % 4 != 0 && k % 16 < 8;
    }
    if (opt_still_from >= 0 && (long)k >= opt_still_from) right = left = up = down = 0;
    if (play) held = (right ? 1 : 0) | (left ? 2 : 0) | (down ? 4 : 0) | (up ? 8 : 0) | (fire1 ? 0x80 : 0) | (fire2 ? 0x40 : 0);
    if (opt_continue && play && !(ram[0x264] & 1)) {
        if (k % 300 < 6) buttons |= 4;
        if (k % 300 >= 100 && k % 300 < 106) buttons |= 1;
    }
    if (getenv("FUZZ_DEBUG") && game_passes && game_passes < 20) fprintf(stderr, "gp %u k %u pad %04x\n", game_passes, k, (uint16_t)~held);
    *held_out = held; *buttons_out = buttons;
}

static int load(const char *dir, const char *name, void *data, size_t size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static uint8_t sound_busy(void *context)
{
    uint8_t active = 0;
    for (unsigned i = 0; i < 4; ++i) active |= world_ga20_read(context, i * 8 + 7) << i;
    return active;
}
static void sound_write(void *context, uint8_t reg, uint8_t value) { world_ga20_write(context, reg, value); }
static void sound_reply(void *context, uint8_t value) { (void)context; world_frame_sound_reply(ram, value); }
static void sound_unrecovered(void *context, uint16_t task, uint8_t state, uint16_t cursor)
{
    (void)context; (void)task; (void)state; (void)cursor;
    ++sound_gaps;
}
static void sound_fm_write(void *context, uint8_t reg, uint8_t value) { (void)context; world_ym_write(reg, value); }
static WorldAudioBus sound_bus = { &pcm, sound_busy, sound_write, sound_reply, sound_unrecovered, sound_fm_write };

static void sound_frame(void)
{
    int16_t output[1024];
    int32_t fm[1024];
    sound_frame_phase += (uint64_t)48000 * 424 * 262;
    unsigned frames = (unsigned)(sound_frame_phase / 6666666), done = 0;
    sound_frame_phase %= 6666666;
    const uint64_t timer_period = (uint64_t)48000 * 57344;
    while (done < frames) {
        unsigned count = (unsigned)((timer_period - sound_timer_phase + 14318180) / 14318181);
        if (count > frames - done) count = frames - done;
        world_ga20_render(&pcm, output + done, count);
        world_ym_render(fm, count);
        done += count;
        sound_timer_phase += (uint64_t)count * 14318181;
        if (sound_timer_phase >= timer_period) {
            sound_timer_phase -= timer_period;
            world_audio_tick(sound_state, sound_rom, &sound_bus);
        }
    }
}

static void screenshot(const char *dir, unsigned tick)
{
    WorldVideoLayer layers[3];
    for (unsigned i = 0; i < 3; ++i) {
        unsigned pos = 0xb706 + 4 * i;
        layers[i].master = layer_master[i];
        layers[i].scroll_y = (uint16_t)word(ram + pos + 2);
        layers[i].scroll_x = (uint16_t)(word(ram + pos) + 16 + 2 * i);
    }
    uint8_t sprites[2048] = {0};
    for (unsigned i = 0, count = (0u - ram[0x8b26]) & 255; i < count; ) {
        memcpy(sprites + i * 8, oam + i * 8, 8);
        i += 1u << ((oam[i * 8 + 1] >> 3) & 3);
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/native-%06u.ppm", dir, tick);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (unsigned y = 0; y < 240; ++y) {
        uint16_t line[320];
        world_video_scanline(line, y, tiles, gfx1, gfx2, sprites, layers, (uint16_t)word(ram + 0x8b28));
        for (unsigned x = 0; x < 320; ++x) {
            uint32_t c = world_video_rgb((uint16_t)word(palette + 2 * (line[x] & 1023)));
            fputc((int)(c >> 16 & 255), f); fputc((int)(c >> 8 & 255), f); fputc((int)(c & 255), f);
        }
    }
    fclose(f);
}

static void bot(unsigned *held, unsigned *buttons);
static int tick(void)
{
    unsigned held = 0, buttons = 0;
    /* Same development pokes as the reference recorder, at the pass start. */
    if (opt_stage >= 0 && !opt_staged && word(ram + 0x80) == 0x2218) { ram[0x84] = (uint8_t)opt_stage; ram[0x85] = 0; opt_staged = 1; }
    if (opt_refill && (ram[0x264] & 1) && ram[0x118] < 2) ram[0x118] = 3;
    if (opt_arm_from >= 0 && (long)(1454 + game_passes) >= opt_arm_from && word(ram + 0x80) == 0xb2e && (ram[0x264] & 1)) {
        unsigned stats = word(ram + ((word(ram + 0x90) + 0x70) & 0xffff));
        for (unsigned i = 0; i < 4; ++i)
            if (opt_arm[i] >= 0 && stats) { ram[stats + 0x10 + 2 * i] = (uint8_t)opt_arm[i]; ram[stats + 0x11 + 2 * i] = 0; }
    }
    unsigned before = word(ram + 0x276);
    if (ram[0x262] & 1) {
        WorldDispatch pass;
        uint32_t addr;
        world_dispatch_begin(&pass, ram);
        if (game_passes || (word(ram + 0x80) == 0xb2e && word(ram + 0x274) + 580 >= 1286)) ++game_passes;
        unsigned calls = 0;
        if (calls_file) fprintf(calls_file, "%u %04x %u", word(ram + 0x274), word(ram + 0x80), word(ram + 0x84));
        while (world_dispatch_target(&pass, ram, &addr)) {
            if (calls_file) fprintf(calls_file, " %05lx", (unsigned long)addr);
            if (++calls > 65536) { snprintf(failure, sizeof failure, "actor list did not terminate"); return 0; }
            if (world_dispatch_native_video(&pass, ram, rom, tiles)) continue;
            if (!opt_skip) {
                snprintf(failure, sizeof failure, "unrecovered handler %05lX, actor %04X, master %04X, stage %u",
                         (unsigned long)addr, pass.actor, word(ram + 0x80), word(ram + 0x84));
                return 0;
            }
            /* Hunting mode: note the stop, retire the actor and carry on. */
            unsigned n = 0, a = pass.actor;
            while (n < stops && stop_address[n] != addr) ++n;
            if (n == stops && stops < 64) {
                stop_address[stops++] = addr;
                printf("stop %05lX key %u actor %04X stage %u players %u\n", (unsigned long)addr,
                       1454 + game_passes, a, word(ram + 0x84), ram[0x264] & 3);
            }
            ram[a] = 0x4c; ram[a + 1] = 0x1b; ram[a + 2] = ram[a + 3] = 0; ram[a + 0x0a] = 1;
            ram[a + 0x3c] = ram[a + 0x3d] = 0xff;
            world_dispatch_return(&pass, ram, (uint16_t)a);
        }
        if (calls_file) fputc('\n', calls_file);
    }
    bot(&held, &buttons);
    uint16_t pads = (uint16_t)~held, system = (uint16_t)~buttons;
    world_palette_step(ram, palette, rom);
    int sound, master_sound;
    unsigned master = word(ram + 0x80);
    for (unsigned i = 0; i < 3; ++i)
        if (ram[0x29a] & (1u << i)) layer_master[i] = (uint16_t)word(ram + 0x29c + 2 * i);
    if (master == 0xa0e) layer_master[1] = 1;
    if (master == 0x24e2) for (unsigned i = 0; i < 3; ++i) layer_master[i] = (uint16_t)i;
    if (master == 0xa09 || master == 0x1d52) layer_master[1] = 0x41;
    int busy = (master == 0xa0e || master == 0x24e2) ? system : -1;
    if (!world_frame_boundary_irq(ram, tiles, oam, palette, rom, pads, 0xffff, system, 0xffbf, 0, busy,
                                  &sound, &master_sound)) {
        snprintf(failure, sizeof failure, "unrecovered frame transition at master %04X, stage %u", master, word(ram + 0x84));
        return 0;
    }
    unsigned elapsed = (uint16_t)(word(ram + 0x276) - before);
    for (unsigned i = 1; i < elapsed; ++i) sound_frame();
    if (sound >= 0) world_audio_receive(sound_state, (uint8_t)sound);
    if (master_sound >= 0) world_audio_receive(sound_state, (uint8_t)master_sound);
    sound_frame();
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: native_fuzz DATA_DIR TICKS [options]\n"); return 1; }
    const char *dir = argv[1], *shots = NULL;
    unsigned ticks = (unsigned)strtoul(argv[2], NULL, 10), every = 0;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--stage") && i + 1 < argc) opt_stage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots") && i + 2 < argc) { shots = argv[++i]; every = (unsigned)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--refill")) opt_refill = 1;
        else if (!strcmp(argv[i], "--follow")) opt_follow = 1;
        else if (!strcmp(argv[i], "--skip-stops")) opt_skip = 1;
        else if (!strcmp(argv[i], "--aim5")) opt_aim5 = 1;
        else if (!strcmp(argv[i], "--continue")) opt_continue = 1;
        else if (!strcmp(argv[i], "--climb") && i + 1 < argc) opt_climb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--random") && i + 1 < argc) opt_random = atol(argv[++i]);
        else if (!strcmp(argv[i], "--random-from") && i + 1 < argc) opt_random_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--still-from") && i + 1 < argc) opt_still_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--arm") && i + 5 < argc) {
            opt_arm_from = atol(argv[++i]);
            for (unsigned j = 0; j < 4; ++j) opt_arm[j] = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--random-until") && i + 1 < argc) opt_random_until = atol(argv[++i]);
        else if (!strcmp(argv[i], "--steer") && i + 2 < argc) { opt_steer_from = atol(argv[++i]); opt_steer_x = strtol(argv[++i], NULL, 0); }
        else if (!strcmp(argv[i], "--calls") && i + 1 < argc) calls_file = fopen(argv[++i], "w");
    }
    if (!load(dir, "maincpu.bin", rom, sizeof rom) || !load(dir, "gfx1.bin", gfx1, sizeof gfx1) ||
        !load(dir, "gfx2.bin", gfx2, sizeof gfx2) || !load(dir, "soundcpu.bin", sound_rom, sizeof sound_rom) ||
        !load(dir, "irem.bin", samples, sizeof samples)) { fprintf(stderr, "missing imported images in %s\n", dir); return 1; }
    if (!world_boot_world(ram, tiles, oam, palette, rom, 0xffbf)) { fprintf(stderr, "boot checks failed\n"); return 1; }
    world_ga20_reset(&pcm, samples, sizeof samples);
    world_ym_reset();
    world_audio_reset_ym(sound_rom, sound_fm_write, NULL);
    world_audio_reset(sound_state, sound_rom, 0);
    world_audio_receive(sound_state, 0);
    world_audio_receive(sound_state, 0x20);
    world_audio_tick(sound_state, sound_rom, &sound_bus);
    unsigned old_master = 0xffff, old_stage = 0xffff, i;
    for (i = 0; i < ticks; ++i) {
        unsigned master = word(ram + 0x80), now = word(ram + 0x84);
        if (master != old_master || now != old_stage) {
            printf("tick %u: master %04X stage %u\n", i, master, now);
            old_master = master; old_stage = now;
        }
        if (!tick()) break;
        if (shots && every && i % every == 0) screenshot(shots, i);
    }
    if (shots) screenshot(shots, i);
    printf("ran %u of %u ticks; final master %04X stage %u; camera %04X,%04X; %u sound callbacks unrecovered\n",
           i, ticks, word(ram + 0x80), word(ram + 0x84), word(ram + 0xb700), word(ram + 0xb702), sound_gaps);
    if (*failure && getenv("FUZZ_DUMP")) {
        FILE *f = fopen(getenv("FUZZ_DUMP"), "wb");
        if (f) { fwrite(ram, 1, sizeof ram, f); fwrite(tiles, 1, sizeof tiles, f); fclose(f); }
    }
    if (*failure) { printf("STOP at tick %u key %u: %s\n", i, 1454 + game_passes, failure); return 2; }
    return 0;
}
