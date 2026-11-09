#ifndef WORLD_AUDIO_DRIVER_H
#define WORLD_AUDIO_DRIVER_H
#include <stdint.h>

enum { WORLD_AUDIO_STATE_SIZE = 0x9ee, WORLD_AUDIO_ROM_SIZE = 0x20000 };
/* Runtime state is the sequencer's low work RAM. The caller owns its clock. */
void world_audio_reset(uint8_t *state, const uint8_t *sound_rom, int warm);
void world_audio_receive(uint8_t *state, uint8_t command);
void world_audio_dispatch(uint8_t *state, const uint8_t *sound_rom, uint16_t task);
/* Execute one recovered music scheduler callback. Zero means the pending
 * event needs a voice or chip operation outside this callback. */
int world_audio_music_step(uint8_t *state, const uint8_t *sound_rom, uint16_t task);
typedef void (*WorldAudioGa20Write)(void *context, uint8_t reg, uint8_t data);
typedef void (*WorldAudioYmWrite)(void *context, uint8_t reg, uint8_t data);
/* busy is the GA20 playback status, one bit per channel. Unsupported effect
 * commands leave state and chip output untouched for coverage reporting. */
int world_audio_effect_step(uint8_t *state, const uint8_t *sound_rom, uint16_t task,
                            uint8_t busy, WorldAudioGa20Write write, void *context);
typedef struct {
    void *context;
    uint8_t (*ga20_busy)(void *context);
    WorldAudioGa20Write ga20_write;
    void (*reply)(void *context, uint8_t value);
    void (*unrecovered)(void *context, uint16_t task, uint8_t state, uint16_t cursor);
    WorldAudioYmWrite ym_write;
} WorldAudioBus;
/* Run one YM timer A interrupt. Unrecovered tracks are reported and retired
 * individually, so the other voices continue. Return the number skipped. */
unsigned world_audio_tick(uint8_t *state, const uint8_t *sound_rom, const WorldAudioBus *bus);
void world_audio_reset_ym(const uint8_t *sound_rom, WorldAudioYmWrite write, void *context);
int world_audio_note_io(uint8_t *state, const uint8_t *sound_rom, uint16_t task, const WorldAudioBus *bus);
#endif
