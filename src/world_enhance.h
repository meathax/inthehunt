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

/* Where the picture is in the game world, so the water effects stay fixed
 * to the water as the camera moves. */
typedef struct {
    uint32_t time;       /* animation clock in 1/8 ms */
    int scroll_x;        /* background layer horizontal scroll */
    int scroll_y;        /* background layer vertical scroll */
    int waterline;       /* screen row where the sea surface ends and the water
                            begins; may be off screen (negative: all water) */
} WorldEnhanceScene;

/* in and out are 320x240 ARGB frames and may not alias. */
void world_enhance_apply(const uint32_t *in, uint32_t *out, const WorldEnhance *fx,
                         const WorldEnhanceScene *scene);
/* Forget the persistence history (after a pause or a scene cut). */
void world_enhance_reset(void);
/* The screen row where water effects began in the last frame, for tests. */
int world_enhance_waterline(void);
#endif
