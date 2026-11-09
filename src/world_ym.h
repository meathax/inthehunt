#ifndef WORLD_YM_H
#define WORLD_YM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void world_ym_reset(void);
void world_ym_write(uint8_t reg, uint8_t value);
/* 48 kHz mono with the World board's FM routing gain. */
void world_ym_render(int32_t *output, size_t count);
#ifdef __cplusplus
}
#endif
#endif
