/* World D3D0 and D3E4 share this append after the shape != FFFF test.
 * Each table starts with a byte cursor and a reserved word. */
#ifdef __WATCOMC__
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
#else
#include <stdint.h>
#endif

uint16_t world_append_candidate(uint8_t *list, uint16_t actor);
#ifdef __WATCOMC__
#pragma aux world_append_candidate parm [bx] [si] value [bx] modify exact [bx]
#endif
uint16_t world_append_candidate(uint8_t *list, uint16_t actor)
{
    uint16_t cursor = list[0] | ((uint16_t)list[1] << 8);
    uint16_t next = cursor + 2;
    list[(uint16_t)(4 + cursor)] = (uint8_t)actor;
    list[(uint16_t)(5 + cursor)] = (uint8_t)(actor >> 8);

    list[0] = (uint8_t)next;
    list[1] = (uint8_t)(next >> 8);
    return cursor;
}
