#ifndef WORLD_GPU_LIGHT_H
#define WORLD_GPU_LIGHT_H
#include <stdint.h>
/* Native arcade coordinates, two source pixels per transport cell. */
enum { WORLD_PROBES=160*120, WORLD_EMITTERS=140 };
typedef struct {
    uint32_t count, flags;
    int waterline, scale;
    float emitters[WORLD_EMITTERS][8];
    uint32_t cells[WORLD_PROBES]; /* occupancy in alpha, diffuse RGB */
    uint32_t revision;
} WorldLightScene;
#endif
