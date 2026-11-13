#ifndef WORLD_SCENE_H
#define WORLD_SCENE_H
#include <stdint.h>
#include "world_video.h"

/* Layered scene renderer for the optional enhancements.
 *
 * The plain picture (world_video_scanline) flattens the three tile layers
 * and the sprites into one 320x240 frame. Here each layer and the sprites
 * are kept apart, so effects know what is in front of what, and the picture
 * is composited at K times the resolution with subpixel layer and sprite
 * offsets (smooth camera, in-between frames). Everything is presentation
 * only: nothing feeds back into the game. */

typedef struct {
    unsigned caustics, rays, shimmer, depth, snow, vivid, vignette, trails;
    unsigned lights;     /* path-traced light from fire: soft shadows and bounce light */
    unsigned shadows;    /* sprites cast soft shadows on the scenery behind them */
    unsigned dof;        /* far layer slightly out of focus */
    unsigned haze;       /* far layer fades toward water blue */
    unsigned normals;    /* per-pixel lighting from normals derived from the pixel art */
    unsigned sunshadow;  /* rock blocks the sunlight: shafts, caustics and shading */
    unsigned glow;       /* light scattered by the water around lights */
    unsigned reflect;    /* the surface mirrors what is above it */
    unsigned stereo;     /* red-cyan anaglyph from layer depth */
    unsigned shock;      /* refraction rings from explosions */
    unsigned particles;  /* embers and bubbles from explosions */
    unsigned shake;      /* screen shake (and rumble) on explosions */
    unsigned surface;    /* rippling waterline with glints */
    unsigned smooth;     /* subpixel camera position */
    unsigned interp;     /* in-between frames */
    unsigned wakes;
    unsigned debris;
    unsigned silt;
    unsigned splashes;
} WorldSceneFx;

typedef struct {
    const uint8_t *tiles;        /* 64 KiB video RAM */
    const uint8_t *sprites;      /* 2 KiB sprite list as drawn */
    const uint32_t *colors;      /* 1024 ARGB palette entries */
    WorldVideoLayer layers[3];
    uint16_t video_control;
    int waterline;               /* screen row of the sea surface, negative = all water */
    int frac[3];                 /* true scroll beyond the whole pixel, 1/256 px, per layer */
    unsigned scene_id;           /* changes on a scene cut (no in-between frames across it) */
} WorldSceneFrame;

void world_scene_init(const uint8_t *gfx1, const uint8_t *gfx2);
/* A new game frame went on show. */
void world_scene_submit(const WorldSceneFrame *frame);
/* Largest supported K. */
enum { WORLD_SCENE_MAX_K = 4 };
/* Render the scene into out (320*K x 240*K ARGB). alpha is the position
 * between the previous game frame (0) and the current one (1); clock is in
 * 1/8 ms and animates the effects. Returns 0 until a frame was submitted. */
int world_scene_render(uint32_t *out, unsigned k, double alpha, const WorldSceneFx *fx, uint32_t clock);
/* Rumble requested by explosions since the last call, 0..65535. */
unsigned world_scene_take_rumble(void);
/* Screen row where water effects began in the last render, for tests. */
int world_scene_waterline(void);
void world_scene_reset(void);
/* Test view of the light transport (320x240): rock in its own colour,
   open water grey by sun visibility, plus the traced light. */
void world_scene_debug(uint32_t *out);
#endif
