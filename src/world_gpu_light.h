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
    uint32_t volume_count;
    int playfield_scroll;   /* playfield scroll x | y << 16, anchors seabed caustics */
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
    uint32_t cells[WORLD_PROBES]; /* alpha bits 0..3 terrain, 4..7 sprites in 2x2 raster order; diffuse RGB */
    uint32_t revision;
    uint32_t light_bins[10*8][5]; /* 16x16 probes per tile, one bit per emitter */
    float water_grade[4];
    uint8_t ice_coverage[WORLD_PROBES]; /* four distant ice bits and four seabed interior bits */
    uint8_t sky_stop[376]; /* first roof hit + 1 per diagonal sun ray, zero for sky */
    uint8_t point_coverage[WORLD_PROBES]; /* continuous material for local-light rays */
    /* Three float4s followed by source-texel material normals at native resolution. */
    float stage_light[12];
    uint32_t surface[320*240];
    /* Light, shadow, depth, shaft, rim and caustic strengths (1 = as designed;
       zero reads as 1), then brightness unused, padding. */
    float strengths[8];
    WorldEffectBatch effects;
} WorldLightScene;
#endif
