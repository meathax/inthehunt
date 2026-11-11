#ifndef WORLD_ENHANCE_H
#define WORLD_ENHANCE_H
#include <stdint.h>

/* Optional picture enhancements for the native port. They are a pure
 * post-process of the finished 320x240 arcade frame: nothing here feeds
 * back into the game, and with every switch off the frame is untouched. */
typedef struct {
    unsigned caustics;   /* moving light network on open water */
    unsigned rays;       /* slanted light shafts from the surface */
    unsigned lights;     /* explosions and shots light their surroundings */
    unsigned bloom;      /* soft glow around bright pixels */
    unsigned shimmer;    /* gentle one-pixel refraction wobble */
    unsigned depth;      /* water darkens and cools toward the sea bed */
    unsigned snow;       /* drifting marine snow particles */
    unsigned vivid;      /* richer colour and contrast */
    unsigned vignette;   /* darker corners */
    unsigned trails;     /* phosphor-style persistence behind bright objects */
} WorldEnhance;

/* in and out are 320x240 ARGB frames and may not alias. time_ms animates
 * the water effects. */
void world_enhance_apply(const uint32_t *in, uint32_t *out, const WorldEnhance *fx, uint32_t time_ms);
/* Forget the persistence history (after a pause or a scene cut). */
void world_enhance_reset(void);
#endif
