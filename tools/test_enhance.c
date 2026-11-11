/* Build: zig cc -Isrc tools/test_enhance.c src/world_enhance.c -o test_enhance.exe */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "world_enhance.h"
/* Caustics on flat water must move with the background scroll: the frame at
 * scroll (x+dx, y+dy) equals the frame at (x, y) shifted by (-dx, -dy). */
static uint32_t in[320 * 240], a[320 * 240], b[320 * 240];
int main(void)
{
    for (int i = 0; i < 320 * 240; ++i) in[i] = 0xff103070u;
    WorldEnhance fx;
    memset(&fx, 0, sizeof fx);
    fx.caustics = 1;
    WorldEnhanceScene s = {5000, 100, 40, -1};
    world_enhance_apply(in, a, &fx, &s);
    int dx = 7, dy = 5, bad = 0, lit = 0;
    s.scroll_x += dx; s.scroll_y += dy; s.waterline -= dy;
    world_enhance_apply(in, b, &fx, &s);
    for (int y = 20; y < 200; ++y)
        for (int x = 0; x < 300; ++x) {
            if (a[y * 320 + x] != in[0]) ++lit;
            if (b[(y - dy) * 320 + (x - dx < 0 ? 0 : x - dx)] != a[y * 320 + x] && x >= dx) ++bad;
        }
    printf("scroll lock: %d mismatched of %d compared, %d lit pixels\n", bad, 180 * 300, lit);
    /* Above the waterline nothing changes. */
    s.waterline = 100;
    world_enhance_apply(in, b, &fx, &s);
    int above = 0;
    for (int i = 0; i < 100 * 320; ++i) above += b[i] != in[i];
    printf("changed pixels above waterline: %d\n", above);
    return bad || above || !lit;
}
