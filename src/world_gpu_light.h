#ifndef WORLD_GPU_LIGHT_H
#define WORLD_GPU_LIGHT_H
#include <stdint.h>
/* Native arcade coordinates, two source pixels per transport cell. */
enum { WORLD_PROBES=160*120, WORLD_EMITTERS=140 };
/* Ordered effects binned into 8x8 native-pixel tiles. Index zero ends a list.
   Nodes use native coordinates except additive subpixel blocks (kind 3). */
enum { WORLD_EFFECT_TILES=40*30, WORLD_EFFECT_NODES=65536 };
typedef struct { int x,y,radius,phase; uint32_t rgba,kind,limit,next; } WorldEffectNode;
typedef struct {
    uint32_t heads[WORLD_EFFECT_TILES], count, history_reset;
    int volume_scroll,volume_time;
    uint32_t volume_count,padding;
    float volumes[24][4];
    int reflect_row,reflect_rows,ripple[30];
    uint32_t mirror[30][320];
    uint32_t ocean_rows[8]; /* underwater terrain eligible for deferred depth colour */
    WorldEffectNode nodes[WORLD_EFFECT_NODES];
} WorldEffectBatch;
typedef struct {
    uint32_t count, flags;
    int waterline, scale;
    float emitters[WORLD_EMITTERS][8];
    uint32_t cells[WORLD_PROBES]; /* occupancy in alpha, diffuse RGB */
    uint32_t revision;
    uint32_t light_bins[10*8][5]; /* 16x16 probes per tile, one bit per emitter */
    WorldEffectBatch effects;
} WorldLightScene;
#endif
