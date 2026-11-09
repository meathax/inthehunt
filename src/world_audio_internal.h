#ifndef WORLD_AUDIO_INTERNAL_H
#define WORLD_AUDIO_INTERNAL_H
#include "world_audio_driver.h"

typedef struct {
    uint8_t reg[256], data[256], pcm[256], busy;
    unsigned count;
} WorldAudioWrites;

int world_audio_effect_fm(uint8_t *s, const uint8_t *rom, uint8_t *task, unsigned op,
                          unsigned value, WorldAudioWrites *out);
void world_audio_quiet_fm(uint8_t *s, const uint8_t *rom, uint8_t *task, WorldAudioWrites *out);
#endif
