#include "world_tile_address.h"

unsigned short __far tile_lookup_offset(unsigned short x, unsigned short y);
#pragma aux tile_lookup_offset parm [bx] [cx] value [bx] modify exact [bx]
unsigned short __far tile_lookup_offset(unsigned short x, unsigned short y)
{
    const volatile unsigned short *scroll = (const volatile unsigned short *)0xb70a;
    return world_tile_address(x - 0x60 + scroll[0], 0x177 - y + scroll[1]);
}
