#ifndef WORLD_AUDIO_OUTPUT_H
#define WORLD_AUDIO_OUTPUT_H
#include <stddef.h>
#include <stdint.h>

int world_audio_output_open(void);
/* Queue at most 1024 mono frames at 48 kHz. Returns zero on device failure. */
int world_audio_output_submit(const int16_t *samples, size_t count);
void world_audio_output_close(void);
#endif
