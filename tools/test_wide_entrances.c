/* Sweep the actual warship ROM pieces through both viewport edges.
   Build with -UNDEBUG and pass the imported maincpu.bin. */
#include "../src/world_frame.c"
#include "../src/world_stage.h"
#include <assert.h>
#ifdef NDEBUG
#error Enable assertions for this regression.
#endif

static uint8_t memory[65536], program[1 << 20], object_ram[2048];
static int allocated;

uint16_t world_spawn_child(uint8_t *ram, uint16_t parent, uint16_t kind)
{
    (void)parent;
    if (allocated) return 0;
    allocated = 1;
    put(ram + 0x500, kind);
    return 0x500;
}

/* Drain each actual ROM row once, including gated rows and late retries.
   Only the pool allocation is stubbed. Coordinates come from the game. */
static unsigned sweep_rows(unsigned stage, unsigned mode)
{
    const uint8_t *bank = program + 0x10000;
    unsigned start = word(bank + 0x5e42 + 2 * stage), index = 0;
    world_view_set(mode);
    world_view_set_stage(stage);
    for (; word(bank + start + index) != 0xffff; index += 12) {
        const uint8_t *row = bank + start + index;
        unsigned trigger = word(row), kind = word(row + 2), segment = word(row + 4);
        unsigned x = word(row + 6), flags = word(row + 10);
        for (int late = 0; late <= 512; late += 512) {
            allocated = 0;
            put(memory + 0x84, stage);
            put(memory + 0xb700, trigger + late);
            put(memory + 0x440, index);
            memory[0x264] = (flags >> 14) == 1 ? 4 : 12;
            world_stage_spawns(memory, program, 0x400);
            assert(allocated && word(memory + 0x500) == kind && word(memory + 0x502) == segment);
            int actual = (int16_t)word(memory + 0x521);
            if (!mode || stage == 4)
                assert((uint16_t)actual == (uint16_t)(x - late));
            else if (segment == 0xb000 && kind == 0x571c)
                assert((flags & 1) ? actual + 128 <= 42 : actual - 128 >= 470);
            else if (segment == 0xb000 && kind == 0x909f && x >= 0x198 && x <= 0x1a0)
                assert(actual - 24 >= 470);
            else if (segment == 0xb000 && kind == 0xd457 && (flags & 3) < 2)
                assert((flags & 1) ? actual + 48 <= 42 : actual - 48 >= 470);
            else if (segment == 0xb000 && kind == 0xa6d6 && x <= 96)
                assert(actual + 26 <= 42);
            else if (segment == 0xa000 && kind == 0xec7e)
                assert(actual - 24 >= 470);
            else if (segment == 0xb000 && kind == 0x1c16 && (flags & 0x0e00) == 0x0600 &&
                     x >= 96 && x < 416)
                assert(actual - 16 >= 470);
            else if ((int16_t)x >= 416 && kind != 0xda13 && kind != 0xdb86 &&
                     kind != 0x8309 && kind != 0xec04)
                assert(actual >= 470);
        }
    }
    return index / 12;
}

static void sweep_ship(unsigned stage, unsigned mode)
{
    const uint8_t *ship = program + 0x80000 + word(program + 0xb573d);
    unsigned count = word(ship);
    assert(count > 1 && count < 210);
    world_view_set(mode);
    world_view_set_stage(stage);
    int wide = mode == WORLD_VIEW_WIDE && stage != 4;
    for (int flip = 0; flip <= 1; ++flip) {
        memory[0x8c09] = flip;
        for (int anchor = -160; anchor <= 640; ++anchor) {
            put(memory + 0x8c04, anchor);
            build_sprites(memory, object_ram, program);
            unsigned out = 0;
            for (unsigned i = 0; i < count; ++i) {
                const uint8_t *piece = ship + 2 + 8 * i;
                int offset = (int16_t)word(piece + 2);
                int x = anchor + (flip ? -offset - 16 : offset);
                int width = 16 << ((word(piece + 6) >> 3) & 3);
                int visible = wide ? x < 470 && x + width > 42 : x >= 78 && x <= 433;
                if (!visible) continue;
                assert(word(object_ram + out + 6) == (uint16_t)x);
                assert(word(object_ram + out + 2) == word(piece + 4));
                out += 8;
            }
            assert((uint8_t)(0 - memory[0x8b26]) == out / 8);
        }
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f && fread(program, 1, sizeof program, f) == sizeof program);
    fclose(f);
    put(memory + 0x80, 0xb2e);
    put(memory + 0x9c00, 0x8c00);
    put(memory + 0x8c00, 0xffff);
    put(memory + 0x8c02, word(program + 0xb573d));
    put(memory + 0x8c06, 256);
    put(memory + 0x8c0a, 0x8000);
    for (unsigned stage = 0; stage < 6; ++stage) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            sweep_ship(stage, mode);
            printf("Round %u, mode %u: %u spawn rows\n", stage + 1, mode, sweep_rows(stage, mode));
        }
    }
    assert(world_view_edge_less((uint16_t)-6, 0x1a8));
    assert(!world_view_edge_greater((uint16_t)-6, 0x1d8));
    puts("Six stages, both modes and facings, 801 hull positions passed.");
    return 0;
}
