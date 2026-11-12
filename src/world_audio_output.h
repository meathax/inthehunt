#ifndef WORLD_AUDIO_OUTPUT_H
#define WORLD_AUDIO_OUTPUT_H
#include <stddef.h>
#include <stdint.h>

int world_audio_output_open(void);
/* Queue mono 48 kHz samples. Never blocks; the oldest samples are dropped
 * if the FIFO overflows. Returns zero only without a device. */
int world_audio_output_submit(const int16_t *samples, size_t count);
/* Move queued samples to the device; call regularly. */
void world_audio_output_pump(void);
/* Samples queued but not yet played (FIFO plus device blocks). */
unsigned world_audio_output_level(void);
void world_audio_output_close(void);
#endif
