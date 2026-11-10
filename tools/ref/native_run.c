/* Developer check: run the native game from cold boot on Linux with scripted
 * cabinet inputs, no window and no audio device. Mirrors the frame loop of
 * src/world_native.c. Reports scene changes and the first unrecovered routine,
 * and can write PPM screenshots.
 *
 *   native_run DATA_DIR TICKS [--stage N] [--shots DIR EVERY] [--refill]
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

static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }

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

static int tick(uint16_t pads, uint16_t system)
{
    unsigned before = word(ram + 0x276);
    if (ram[0x262] & 1) {
        WorldDispatch pass;
        uint32_t addr;
        world_dispatch_begin(&pass, ram);
        unsigned calls = 0;
        while (world_dispatch_target(&pass, ram, &addr)) {
            if (++calls > 65536) { snprintf(failure, sizeof failure, "actor list did not terminate"); return 0; }
            if (!world_dispatch_native_video(&pass, ram, rom, tiles)) {
                snprintf(failure, sizeof failure, "unrecovered handler %05lX, actor %04X, master %04X, stage %u",
                         (unsigned long)addr, pass.actor, word(ram + 0x80), word(ram + 0x84));
                return 0;
            }
        }
    }
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
    if (argc < 3) { fprintf(stderr, "usage: native_run DATA_DIR TICKS [--stage N] [--shots DIR EVERY] [--refill]\n"); return 1; }
    const char *dir = argv[1], *shots = NULL;
    unsigned ticks = (unsigned)strtoul(argv[2], NULL, 10), every = 0, refill = 0;
    int stage = -1;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--stage") && i + 1 < argc) stage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots") && i + 2 < argc) { shots = argv[++i]; every = (unsigned)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--refill")) refill = 1;
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
    unsigned old_master = 0xffff, old_stage = 0xffff, staged = 0, i;
    for (i = 0; i < ticks; ++i) {
        unsigned master = word(ram + 0x80), now = word(ram + 0x84);
        if (master != old_master || now != old_stage) {
            printf("tick %u: master %04X stage %u\n", i, master, now);
            old_master = master; old_stage = now;
        }
        /* Same development pokes as the reference recorder. */
        if (stage >= 0 && !staged && master == 0x2218) { ram[0x84] = (uint8_t)stage; ram[0x85] = 0; staged = 1; }
        if (refill && (ram[0x264] & 1) && ram[0x118] < 2) ram[0x118] = 3;
        unsigned held = 0, buttons = 0, phase = (i / 90) % 4;
        if (i >= 600 && i < 606) buttons |= 4;          /* coin 1 */
        if (i >= 700 && i < 706) buttons |= 1;          /* start 1 */
        if (i >= 800) {
            if (phase != 3) held |= 1;                  /* right */
            if (phase == 1) held |= 8;                  /* up */
            if (phase == 3) held |= 4;                  /* down */
            if (i % 12 < 6) held |= 0x80;
            if (i % 16 < 8) held |= 0x40;
        }
        if (!tick((uint16_t)~held, (uint16_t)~buttons)) break;
        if (shots && every && i % every == 0) screenshot(shots, i);
    }
    if (shots) screenshot(shots, i);
    printf("ran %u of %u ticks; final master %04X stage %u; %u sound callbacks unrecovered\n",
           i, ticks, word(ram + 0x80), word(ram + 0x84), sound_gaps);
    if (*failure) { printf("STOP at tick %u: %s\n", i, failure); return 2; }
    return 0;
}
