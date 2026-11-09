#ifndef WORLD_FRAME_H
#define WORLD_FRAME_H

#include <stdint.h>

/* Stage and briefing master routines use the same board resets. */
void world_frame_init_actors(uint8_t *ram, uint16_t first_handler, uint16_t terminal_priority);
void world_frame_init_palette(uint8_t *ram, uint8_t *palette, const uint8_t *rom, uint16_t list);
int world_gameover_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *sound);
void world_frame_reset_sound(uint8_t *ram);
void world_frame_sound_reply(uint8_t *ram, uint16_t reply);
void world_frame_briefing_master(uint8_t *ram, unsigned master);

/* Advance one physical non-busy VBlank and run the next master routine.
 * Supply measured spin-loop iterations for controllers that preserve B892.
 * The two sound outputs correspond to the IRQ and master, in that order.
 * Inspect bit zero at RAM[262] to see if an actor pass follows. */
int world_frame_boundary(uint8_t *ram, uint8_t *tiles, uint8_t *oam, uint8_t *palette,
                         const uint8_t *rom, uint16_t raw_pads, uint16_t raw_aux,
                         uint16_t raw_system, uint16_t raw_dips, unsigned polls,
                         int *sound_command, int *master_command);
/* busy_system is -1 or the raw system port at an IRQ inside master execution. */
int world_frame_boundary_irq(uint8_t *ram, uint8_t *tiles, uint8_t *oam, uint8_t *palette,
                             const uint8_t *rom, uint16_t raw_pads, uint16_t raw_aux,
                             uint16_t raw_system, uint16_t raw_dips, unsigned polls,
                             int busy_system, int *sound_command, int *master_command);

/* Service a measured VBlank inside an active actor pass (IRQ busy branch).
 * Returns zero when pending sound recovery requires guest execution. */
int world_frame_busy_vblank(uint8_t *ram, uint16_t raw_system);

/* Advance the active World encounter from actor-list end to the next pass.
 * RAM is 48 KiB, tiles 64 KiB, OAM 2 KiB and main ROM 1 MiB. Raw ports
 * are active-low. The returned sound command is -1 when the queues are idle.
 * Call palette servicing before this and dispatch_begin after success. */
int world_frame_advance(uint8_t *ram, uint8_t *tiles, uint8_t *oam,
                        const uint8_t *rom, uint16_t raw_pads, uint16_t raw_aux,
                        uint16_t raw_system, uint16_t raw_dips, int *sound_command);
/* Actor passes can span a second VBlank during the stage opening. Supply
 * elapsed board IRQ ticks from the scheduler, or use world_frame_advance at 1. */
int world_frame_advance_vblanks(uint8_t *ram, uint8_t *tiles, uint8_t *oam,
                               const uint8_t *rom, uint16_t raw_pads, uint16_t raw_aux,
                               uint16_t raw_system, uint16_t raw_dips, unsigned vblanks,
                               int *sound_command);
#endif

