#ifndef WORLD_DISPATCH_H
#define WORLD_DISPATCH_H

#include <stdint.h>

typedef enum {
    WORLD_FRAME_HEAD, WORLD_PLAYER_ONE, WORLD_BEFORE_PLAYER_TWO,
    WORLD_PLAYER_TWO, WORLD_AFTER_PLAYER_TWO
} WorldDispatchPhase;

typedef struct {
    WorldDispatchPhase phase;
    uint16_t actor;
} WorldDispatch;

void world_draw_tile_block(uint8_t *tiles, const uint8_t *rom, uint16_t base, uint16_t dest, uint16_t descriptor);
void world_dispatch_begin(WorldDispatch *, uint8_t *ram);
/* Returns zero for the terminal handler. Writes the physical address in both cases. */
int world_dispatch_target(const WorldDispatch *, const uint8_t *ram, uint32_t *address);
/* Executes a recovered handler and advances the pass. Returns zero at fallback or end. */
int world_dispatch_native(WorldDispatch *, uint8_t *ram, const uint8_t *main_rom);
/* tiles points to 64 KiB of video RAM beginning at D0000. */
int world_dispatch_native_video(WorldDispatch *, uint8_t *ram, const uint8_t *main_rom, uint8_t *tiles);
/* Supply BP as returned by the actor handler, including after a list mutation. */
void world_dispatch_return(WorldDispatch *, uint8_t *ram, uint16_t actor);

/* Names the untranslated branch when a recovered handler stops. */
extern const char *world_dispatch_detail;
extern uint16_t world_dispatch_port4;
/* 0:867B: stage-five ice broken by the surfacing submarine. */
void world_stage_five_entry_ice(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t sub);

#endif



