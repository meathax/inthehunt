#ifndef WORLD_GA20_H
#define WORLD_GA20_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t position;
    uint16_t counter, volume;
    uint8_t rate, playing;
} WorldGa20Voice;

typedef struct {
    const uint8_t *samples;
    size_t sample_count;
    uint32_t sample_mask;
    uint8_t registers[32];
    WorldGa20Voice voices[4];
    uint32_t tick_remaining;
    int32_t tick_sample;
} WorldGa20;

void world_ga20_reset(WorldGa20 *chip, const uint8_t *samples, size_t size);
void world_ga20_write(WorldGa20 *chip, unsigned reg, uint8_t value);
uint8_t world_ga20_read(const WorldGa20 *chip, unsigned reg);
/* Advance one PCM clock (the World oscillator divided by sixteen).
 * Return the four-voice signed sum before the board's quarter gain. */
int32_t world_ga20_clock(WorldGa20 *chip);
/* One chip output, signed 16-bit PCM at 48 kHz. The mono board sums both
 * identical chip outputs. Fractional clock phase survives calls.
 * Writes take effect after the currently sampled chip clock. */
void world_ga20_render(WorldGa20 *chip, int16_t *output, size_t count);
#ifdef __cplusplus
}
#endif
#endif
