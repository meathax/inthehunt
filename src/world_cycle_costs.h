#ifndef WORLD_CYCLE_COSTS_H
#define WORLD_CYCLE_COSTS_H

#include <stdint.h>

enum {
    WORLD_CYCLES_NEEDS_PREFETCH = 1,
    WORLD_CYCLES_NEEDS_READY = 2,
    WORLD_CYCLES_NEEDS_IRQ_SERVICE = 4
};

typedef enum {
    WORLD_CYCLE_INTERNAL,
    WORLD_CYCLE_ROM_WORD,
    WORLD_CYCLE_TILE_WORD,
    WORLD_CYCLE_TILE_CLEAR
} WorldCycleAccess;

typedef struct {
    uint32_t core_cycles;
    WorldCycleAccess access;
    uint32_t rom_address;
    uint32_t video_address;
    uint16_t attribute_xor;
} WorldCycleCheckpoint;

typedef void (*WorldCycleVisitor)(void *context, const WorldCycleCheckpoint *checkpoint);

typedef struct {
    uint64_t core_cycles;
    uint32_t rom_words;
    uint32_t video_words;
    unsigned missing_timing;
} WorldCycleCost;

typedef struct {
    unsigned available_bytes;
    uint64_t instructions;
    uint64_t fetch_stall_cycles;
} WorldCyclePrefetch;

/* Reference-model instruction timing only. The recovered native routine
 * supplies its source instruction length and execution cycles, including
 * prefixes. This does not read or execute guest opcodes. Pass flush=1 only
 * where the reference invokes CHANGE_PC (calls, returns and most taken
 * branches). LOOP/LOOPZ/LOOPNZ deliberately retain their queue in that core.
 * The pinned V33 model uses six queued bytes and one cycle per fetch byte.
 * This is the reference core's stated approximation, not measured PCB timing. */
uint32_t world_cycles_reference_instruction(WorldCyclePrefetch *prefetch,
                                            unsigned encoded_bytes,
                                            uint32_t execution_cycles,
                                            int flush);

/* Source annotations use the pinned MAME V33 instruction-cycle column.
 * They count executed loop iterations and address parity, and expose each
 * video store in execution order. Instruction fetch/prefetch and external
 * READY waits must be added by the board scheduler. IRQ service is separate.
 * Consequently these results describe core work, not total elapsed time. */
WorldCycleCost world_cycles_tile_block(uint32_t source, uint16_t video_base,
                                      uint16_t destination, unsigned flip,
                                      WorldCycleVisitor visit, void *context);

/* 37EB (modes 0/1), 3847 (mode 2), 38A9 (mode 3), including far return.
 * main_rom is the user's 1 MiB bus-ordered image. A zero dimension byte
 * performs 256 iterations, matching the original eight-bit loop counter.
 * Includes descriptor and blitter jump-table reads, not instruction fetch. */
WorldCycleCost world_cycles_tile_transfer(const uint8_t *main_rom, uint16_t source,
                                         uint16_t video_base, uint16_t destination,
                                         uint16_t dimensions, unsigned mode,
                                         WorldCycleVisitor visit, void *context);

/* 37C7 fills the 64 KiB video bank, including setup and far return. */
WorldCycleCost world_cycles_video_clear(WorldCycleVisitor visit, void *context);

#endif
