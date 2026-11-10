/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright Bryan McPhail.
 * Reference prefetch timing adapted from MAME nec.cpp; redistribution terms
 * are in LICENSES/MAME-Timing-BSD-3-Clause.txt.
 *
 * Native timing annotations for recovered World tile-transfer loops.
 * Cycle-column reference: MAME NEC core, revision
 * 73346bee98cd79d605514d14691f40ee5e368915, necinstr.hxx.
 * The software prefetch model in that revision is explicitly approximate.
 */
#include "world_cycle_costs.h"

uint32_t world_cycles_reference_instruction(WorldCyclePrefetch *prefetch,
                                            unsigned encoded_bytes,
                                            uint32_t execution_cycles,
                                            int flush)
{
    unsigned queued = prefetch->available_bytes;
    unsigned missing = encoded_bytes > queued ? encoded_bytes - queued : 0;
    queued = encoded_bytes > queued ? 0 : queued - encoded_bytes;
    /* do_prefetch consumes execution credit while it exceeds one. Once
       only one credit remains, every further missing byte stalls the CPU. */
    uint32_t credit = execution_cycles;
    unsigned absorbed = credit ? credit - 1 : 0;
    if (absorbed > missing) absorbed = missing;
    credit -= absorbed;
    unsigned stalls = missing - absorbed;
    if (flush) queued = 0;
    else {
        uint64_t filled = (uint64_t)queued + credit;
        queued = filled < 6 ? (unsigned)filled : 6;
    }
    prefetch->available_bytes = queued;
    ++prefetch->instructions;
    prefetch->fetch_stall_cycles += stalls;
    return execution_cycles + stalls;
}

typedef struct {
    WorldCycleCost cost;
    WorldCycleVisitor visit;
    void *context;
} CycleStream;

static CycleStream begin_stream(WorldCycleVisitor visit, void *context)
{
    CycleStream stream = {{0}, visit, context};
    stream.cost.missing_timing = WORLD_CYCLES_NEEDS_PREFETCH |
                                WORLD_CYCLES_NEEDS_READY |
                                WORLD_CYCLES_NEEDS_IRQ_SERVICE;
    return stream;
}

static void emit(CycleStream *stream, unsigned cycles, WorldCycleAccess access,
                 uint32_t source, uint32_t destination, unsigned attribute_xor)
{
    WorldCycleCheckpoint checkpoint = {cycles, access, source, destination,
                                       (uint16_t)attribute_xor};
    stream->cost.core_cycles += cycles;
    if (access == WORLD_CYCLE_ROM_WORD) ++stream->cost.rom_words;
    if (access == WORLD_CYCLE_TILE_WORD || access == WORLD_CYCLE_TILE_CLEAR) ++stream->cost.video_words;
    if (stream->visit) stream->visit(stream->context, &checkpoint);
}

static void internal(CycleStream *stream, unsigned cycles)
{
    emit(stream, cycles, WORLD_CYCLE_INTERNAL, 0, 0, 0);
}

static void block(CycleStream *stream, uint32_t source, uint16_t base,
                  uint16_t destination, unsigned flip)
{
    flip &= 3;
    unsigned odd = destination & 1;
    if (!flip) {
        /* XOR IX,IX and MOV AW,IY. Each row loads CW=8 and REP MOVSW.
         * The six-cycle MOVSW includes one ROM read and one video store. */
        internal(stream, 4);
        for (unsigned row = 0; row < 4; ++row) {
            internal(stream, 4);
            for (unsigned word = 0; word < 8; ++word) {
                uint32_t src = (source + row*16 + word*2) & 0xfffff;
                uint32_t dst = 0xd0000u + base + ((destination + row*256) & 0x3fff) + word*2;
                ++stream->cost.rom_words;
                emit(stream, 6 + 2*(source & 1) + 2*odd, WORLD_CYCLE_TILE_WORD, src, dst, 0);
            }
            if (row != 3) internal(stream, 6);
        }
    } else {
        /* Flipped variants use individual loads and ES-prefixed stores.
         * Both-flipped code loads the third tile number but overwrites AW
         * with DS1 instead of storing that number. Its attribute is stored. */
        internal(stream, flip & 2 ? 8 : 4);
        for (unsigned row = 0; row < 4; ++row) {
            unsigned target_row = flip & 2 ? 3 - row : row;
            for (unsigned word = 0; word < 8; ++word) {
                unsigned column = word / 2;
                if (flip & 1) column = 3 - column;
                uint32_t src = (source + row*16 + word*2) & 0xfffff;
                uint32_t dst = 0xd0000u + base + ((destination + target_row*256) & 0x3fff) +
                               column*4 + (word & 1)*2;
                emit(stream, 5 + 2*(source & 1), WORLD_CYCLE_ROM_WORD, src, 0, 0);
                if (word & 1) internal(stream, 2);
                if (flip == 3 && word == 4) {
                    internal(stream, 2);
                } else {
                    emit(stream, 5 + 2*odd, WORLD_CYCLE_TILE_WORD, src, dst,
                         word & 1 ? flip << 9 : 0);
                }
            }
            internal(stream, 6);
            internal(stream, row == 3 ? 3 : 6);
        }
    }
    internal(stream, 10); /* Near RET. */
}

WorldCycleCost world_cycles_tile_block(uint32_t source, uint16_t video_base,
                                      uint16_t destination, unsigned flip,
                                      WorldCycleVisitor visit, void *context)
{
    CycleStream stream = begin_stream(visit, context);
    block(&stream, source, video_base, destination, flip);
    return stream.cost;
}

WorldCycleCost world_cycles_tile_transfer(const uint8_t *rom, uint16_t source,
                                         uint16_t base, uint16_t destination,
                                         uint16_t dimensions, unsigned mode,
                                         WorldCycleVisitor visit, void *context)
{
    CycleStream stream = begin_stream(visit, context);
    unsigned width = (uint8_t)((dimensions >> 8) - 1) + 1u;
    unsigned height = (uint8_t)((dimensions & 255) - 1) + 1u;
    mode &= 3;
    unsigned lines = mode < 2 ? width : height;
    unsigned cells = mode < 2 ? height : width;
    /* PUSH DS0, PUSH DS1, MOV DS1,AW and two SS-prefixed shadow stores. */
    internal(&stream, 18);
    for (unsigned line = 0; line < lines; ++line) {
        uint16_t position = destination;
        for (unsigned cell = 0; cell < cells; ++cell) {
            /* Set descriptor segment and offset, load descriptor, select
             * blitter, save CW/IY, and CALL CS:[IX]. This read accounts for
             * descriptor parity; the blitter table is word-aligned. */
            internal(&stream, 6);
            uint16_t descriptor = rom[0xa0000u + source] |
                                  (uint16_t)rom[0xa0000u + source + 1] << 8;
            emit(&stream, 5 + 2*(source & 1), WORLD_CYCLE_ROM_WORD,
                 0xa0000u + source, 0, 0);
            internal(&stream, 18);
            emit(&stream, 22, WORLD_CYCLE_ROM_WORD, 0x398e + 2*(descriptor & 3), 0, 0);
            block(&stream, (uint32_t)(descriptor & 0xfffc) << 4, base, position, descriptor & 3);
            internal(&stream, 15); /* Two POPs, DEC CL/CH, conditional branch. */
            source += 2;
            if (cell + 1 < cells) {
                internal(&stream, mode < 2 ? 18 : 20);
                position = mode < 2 ? (position + 0x400) & 0x3fff
                                   : (position & 0xff00) | ((position + 16) & 255);
            }
        }
        if (line + 1 < lines) {
            internal(&stream, mode < 2 ? 42 : 51);
            destination = mode < 2 ? (destination & 0xff00) | ((destination + 16) & 255)
                                  : (destination + (mode == 2 ? 0x400 : -0x400)) & 0x3fff;
        }
    }
    internal(&stream, 31); /* Outer DEC/branch, POP segments, far RET. */
    return stream.cost;
}

WorldCycleCost world_cycles_video_clear(WorldCycleVisitor visit, void *context)
{
    CycleStream stream = begin_stream(visit, context);
    /* MOV AW,D000, MOV DS1,AW, zero AW/IY, load CW, REP prefix. */
    internal(&stream, 12);
    for (unsigned word = 0; word < 32768; ++word)
        emit(&stream, 3, WORLD_CYCLE_TILE_CLEAR, 0, 0xd0000u + word*2, 0);
    internal(&stream, 16);
    return stream.cost;
}
