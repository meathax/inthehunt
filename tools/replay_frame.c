/* Replay one World actor pass or a continuous encounter from local board captures. */
#include "../src/world_dispatch.h"
#include "../src/world_palette.h"
#include "../src/world_frame.h"
#include <stdio.h>
#include <string.h>

static int read_image(const char *path, uint8_t *buffer, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fread(buffer, 1, size, file) == size && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok;
}

static int write_image(const char *path, const uint8_t *buffer, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = fwrite(buffer, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

typedef struct {
    FILE *file;
    unsigned pass, call, system;
} IrqTrace;

static int next_irq(IrqTrace *irq)
{
    char line[96], extra;
    if (!fgets(line, sizeof line, irq->file)) {
        irq->pass = 0;
        return !ferror(irq->file);
    }
    return sscanf(line, "%u %u %x %c", &irq->pass, &irq->call, &irq->system, &extra) == 3 &&
           irq->pass && irq->call && irq->system <= 0xffff;
}

static int run_pass(uint8_t *ram, uint8_t *tiles, const uint8_t *rom, unsigned *total,
                    unsigned frame, IrqTrace *irq)
{
    WorldDispatch pass;
    world_dispatch_begin(&pass, ram);
    unsigned calls = 0;
    uint32_t address;
    while (world_dispatch_target(&pass, ram, &address)) {
        if (!world_dispatch_native_video(&pass, ram, rom, tiles)) {
            fprintf(stderr, "Guest handler required at %05lx for actor %04x after %u calls\n",
                    (unsigned long)address, pass.actor, calls);
            return 2;
        }
        ++calls;
        while (irq->pass == frame && irq->call == calls) {
            if (!world_frame_busy_vblank(ram, (uint16_t)irq->system)) {
                fprintf(stderr, "Unsupported busy VBlank on pass %u after call %u\n", frame, calls);
                return 2;
            }
            if (!next_irq(irq)) return 1;
        }
        if (calls >= 65536) {
            fprintf(stderr, "Actor pass did not reach its terminal handler\n");
            return 1;
        }
    }
    if (irq->pass && irq->pass <= frame) return 1;
    *total += calls;
    return 0;
}

int main(int argc, char **argv)
{
    static uint8_t rom[1 << 20], ram[65536], tiles[65536], oam[2048], palette[2048];
    IrqTrace irq = {0};
    if (argc >= 3 && strcmp(argv[argc - 2], "--irqs") == 0) {
        irq.file = fopen(argv[argc - 1], "r");
        if (!irq.file || !next_irq(&irq)) return 1;
        argc -= 2;
    }
    int palette_held = argc > 1 && strcmp(argv[argc - 1], "--hold-palette") == 0;
    if (palette_held) --argc;
    int video_state = argc == 13 && strcmp(argv[1], "--encounter") == 0;
    int encounter = (argc == 12 || video_state) && strcmp(argv[1], "--encounter") == 0;
    if (!encounter && argc != 6) {
        fprintf(stderr, "Usage: replay_frame maincpu.bin work-in video-in work-out video-out\n"
                "       replay_frame --encounter maincpu.bin work-in video-in oam-in palette-in inputs.txt work-out video-out oam-out palette-out [scanout-out]\n"
                "Each input line gives four hex ports (pads aux system dips) and optional decimal VBlank count.\n");
        return 1;
    }
    char **args = argv + 1 + encounter;
    if (!read_image(args[0], rom, sizeof rom) || !read_image(args[1], ram, sizeof ram)
        || !read_image(args[2], tiles, sizeof tiles)
        || (encounter && (!read_image(args[3], oam, sizeof oam)
                       || !read_image(args[4], palette, sizeof palette)))) {
        fprintf(stderr, "Could not read complete World memory images\n");
        return 1;
    }
    FILE *inputs = encounter ? fopen(args[5], "r") : NULL;
    if (encounter && !inputs) return 1;
    int hardware = 0;
    if (inputs) {
        char magic[16];
        hardware = fgets(magic, sizeof magic, inputs) && !strncmp(magic, "WH1", 3) && strspn(magic + 3, "\r\n") == strlen(magic + 3);
        if (!hardware) rewind(inputs);
    }
    unsigned frames = 0, total = 0;
    uint8_t controls[20];
    unsigned dips_for_pass = 0xffff;
    for (;;) {
        if (video_state) {
            for (unsigned layer = 0; layer < 3; ++layer) {
                unsigned offset = 0xb706 + 4 * layer;
                unsigned scroll = ram[offset] | ((unsigned)ram[offset + 1] << 8);
                int bias = (dips_for_pass & 0x100) ? 16 + 2 * (int)layer : -9 - 2 * (int)layer;
                scroll += bias;
                memcpy(controls + 6 * layer, ram + 0x29c + 2 * layer, 2);
                /* The briefing master programs these registers directly at
                   0A0E. The gameplay master uses the IRQ shadow registers. */
                unsigned master = ram[0x80] | ((unsigned)ram[0x81] << 8);
                if (master == 0x2218 || master == 0x2282) {
                    controls[6 * layer] = (uint8_t)layer;
                    controls[6 * layer + 1] = 0;
                }
                memcpy(controls + 6 * layer + 2, ram + offset + 2, 2);
                controls[6 * layer + 4] = (uint8_t)scroll;
                controls[6 * layer + 5] = (uint8_t)(scroll >> 8);
            }
            memcpy(controls + 18, ram + 0x8b28, 2);
        }
        int result = run_pass(ram, tiles, rom, &total, frames + 1, &irq);
        if (result) {
            fprintf(stderr, "Stopped on encounter frame %u\n", frames);
            if (inputs) fclose(inputs);
            return result;
        }
        ++frames;
        char line[128], extra;
        if (!inputs || !fgets(line, sizeof line, inputs)) break;
        unsigned pads, aux, system, dips;
        if (hardware) {
            for (;;) {
                unsigned polls, value;
                int busy = -1;
                if (line[0] == 'P' && line[1 + strspn(line + 1, "\r\n")] == 0) break;
                if (sscanf(line, "R %x %c", &value, &extra) == 1 && value <= 65535) {
                    world_frame_sound_reply(ram, (uint16_t)value);
                } else if (sscanf(line, "I %x %c", &value, &extra) == 1 && value <= 65535) {
                    if (!world_frame_busy_vblank(ram, (uint16_t)value)) return 2;
                } else if ((sscanf(line, "B %x %x %x %x %u %d %c", &pads, &aux, &system,
                                   &dips, &polls, &busy, &extra) == 6 ||
                            sscanf(line, "B %x %x %x %x %u %c", &pads, &aux, &system,
                                   &dips, &polls, &extra) == 5) && busy >= -1 && busy <= 65535 && (pads | aux | system | dips) <= 65535) {
                    int irq_sound, master_sound;
                    dips_for_pass = dips;
                    world_palette_step(ram, palette, rom);
                    if (!world_frame_boundary_irq(ram, tiles, oam, palette, rom, pads, aux, system,
                                              dips, polls, busy, &irq_sound, &master_sound)) {
                        fprintf(stderr, "Unsupported board boundary after actor pass %u\n", frames);
                        return 2;
                    }
                    if (irq_sound >= 0) printf("transition %u sound %04x\n", frames, irq_sound);
                    if (master_sound >= 0) printf("transition %u sound %04x\n", frames, master_sound);
                } else {
                    fprintf(stderr, "Invalid board event after actor pass %u: %s", frames, line);
                    return 1;
                }
                if (!fgets(line, sizeof line, inputs)) return 1;
            }
            continue;
        }
        unsigned vblanks = 1;
        int fields = sscanf(line, "%x %x %x %x %u %c", &pads, &aux, &system, &dips, &vblanks, &extra);
        if ((fields != 5 && sscanf(line, "%x %x %x %x %c", &pads, &aux, &system, &dips, &extra) != 4) ||
            (pads | aux | system | dips) > 0xffff) {
            fprintf(stderr, "Invalid input on line %u\n", frames);
            fclose(inputs);
            return 1;
        }
        dips_for_pass = dips;
        int command;
        world_palette_step(ram, palette, rom);
        if (!world_frame_advance_vblanks(ram, tiles, oam, rom, pads, aux, system, dips, vblanks, &command)) {
            fprintf(stderr, "Unsupported frame transition on input line %u\n", frames);
            fclose(inputs);
            return 2;
        }
        if (command >= 0) printf("transition %u sound %04x\n", frames, command);
    }
    if (inputs) {
        int failed = ferror(inputs);
        fclose(inputs);
        if (failed) return 1;
    }
    if (irq.file) fclose(irq.file);
    char **out = args + (encounter ? 6 : 3);
    if (!write_image(out[0], ram, sizeof ram) || !write_image(out[1], tiles, sizeof tiles)
        || (encounter && (!write_image(out[2], oam, sizeof oam)
                       || !write_image(out[3], palette, sizeof palette)))) return 1;
    if (video_state) {
        /* The visible scanout uses the previous sprite transfer and tile state.
           The encounter uses unsorted DMA with one descriptor per column group. */
        unsigned count = (0u - ram[0x8b26]) & 255;
        uint8_t buffered[2048] = {0};
        for (unsigned i = 0; i < count; ) {
            memcpy(buffered + i * 8, oam + i * 8, 8);
            i += 1u << ((oam[i * 8 + 1] >> 3) & 3);
        }
        if (!palette_held) world_palette_step(ram, palette, rom);
        FILE *state = fopen(argv[12], "wb");
        if (!state) return 1;
        int ok = fwrite(controls, 1, sizeof controls, state) == sizeof controls &&
                 fwrite(tiles, 1, sizeof tiles, state) == sizeof tiles &&
                 fwrite(buffered, 1, sizeof buffered, state) == sizeof buffered &&
                 fwrite(palette, 1, sizeof palette, state) == sizeof palette;
        if (fclose(state) || !ok) return 1;
    }
    printf("%u native calls across %u actor passes\n", total, frames);
    return 0;
}

