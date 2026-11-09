#include "world_frame.h"
#include "world_dispatch.h"
#include <string.h>
void world_queue_sound(uint8_t *ram, uint16_t command);

static uint16_t word(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static void put(uint8_t *p, uint16_t value) { p[0] = value; p[1] = value >> 8; }

static uint16_t text(uint8_t *tiles, const uint8_t *rom, uint16_t pos, uint16_t str, uint16_t color)
{
    while (rom[str] != 255) {
        put(tiles + pos, rom[str++]);
        put(tiles + pos + 2, color);
        pos = (pos & 0xff00) | (uint8_t)(pos + 4);
    }
    return pos;
}

static void rank_table(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom)
{
    unsigned stage = word(ram+0x84);
    world_frame_init_palette(ram, palette, rom, word(rom + 0xd060 + 2*stage));
    for (unsigned i = 0; i < 2; ++i) {
        unsigned list = i ? 0xd08c : word(rom + 0xd076 + 2*stage);
        if (list) for (; word(rom+list) != 0xffff; list += 4) {
            uint8_t *job = ram + 0x9e06 + 8 * word(rom+list);
            put(job, word(rom+list+2)); put(job+2, 0x80); put(job+4, 5); put(job+6, 0x1020);
        }
    }
    memset(tiles, 0, 65536);
    for (unsigned layer = 0; layer < 2; ++layer) {
        const uint8_t *desc = rom + 0xd0d1 + stage*16 + layer*8;
        uint16_t src = word(desc), origin = word(desc+2), size = word(desc+4);
        unsigned mode = word(desc+6) & 3;
        unsigned width = size >> 8, height = size & 255;
        put(ram+0xb89a, size);
        for (unsigned i = 0; i < (mode<2 ? width : height); ++i) {
            uint16_t pos = origin;
            put(ram+0xb89c, origin);
            for (unsigned j = 0; j < (mode<2 ? height : width); ++j) {
                world_draw_tile_block(tiles, rom, 0x4000*(1+layer), pos, word(rom + 0xa0000 + src));
                src += 2;
                pos = mode<2 ? (pos+0x400)&0x3fff : (pos&0xff00)|(uint8_t)(pos+16);
            }
            origin = mode<2 ? (origin&0xff00)|(uint8_t)(origin+16) : (origin+(mode==2?0x400:-0x400))&0x3fff;
        }
    }
    for (unsigned p = 0x1348, s = 0xcf75; rom[s] != 255; p += 8, ++s) {
        uint16_t t = (rom[s]-48)*4+256;
        for (unsigned n = 0; n < 4; ++n) {
            unsigned dest = p + (n&1)*4+(n>>1)*256;
            put(tiles+dest, t+n); put(tiles+dest+2, 0x130);
        }
    }
    text(tiles, rom, 0x1744, 0xcf84, 0x131);
    for (unsigned row = 0; row < 10; ++row) {
        unsigned color = 0x132+row, entry = word(ram+0x240+2*row);
        unsigned pos = text(tiles,rom,0x1a44+row*512,word(rom+0xcfe5+2*row),color)+4;
        unsigned started = 0;
        for (int k = 7; k >= 0; --k, pos += 4) {
            unsigned digit = (ram[entry+k/2] >> (4*(k&1)))&15;
            if (digit || started) { started=1; put(tiles+pos,48+digit); put(tiles+pos+2,color); }
        }
        pos += 8;
        int distance = word(ram+entry+4);
        started=0;
        for (int d=100; d; d/=10, pos+=4) {
            unsigned digit = distance/d%10;
            if (digit || started || d==1) {
                if (d == 100) started=1;
                put(tiles+pos,48+digit); put(tiles+pos+2,color);
            }
        }
        put(ram+0xca,started);
        pos=text(tiles,rom,pos+8,word(rom+0xcfa3+word(ram+entry+6)),color);
        for (unsigned n=0;n<8;++n,pos+=4) {
            put(tiles+pos+8,ram[entry+8+n]);put(tiles+pos+10,color);
        }
    }
    put(ram+0xcc,0x13c);
    world_frame_init_actors(ram,0x1a3a,0x8000);
    put(ram+0x262,1);
    put(ram+0x86,128);
    put(ram+0x80,0xc7f8);
}

static unsigned insert_score(uint8_t *ram, unsigned player)
{
    unsigned score = word(ram+0x254+2*player), p = 0x100+64*player;
    put(ram+0x258+2*player,score);put(ram+score+4,word(ram+p+0x1c));
    put(ram+score+6,word(ram+p+0x26)*2);
    unsigned place=10;
    while (place) {
        unsigned prev=word(ram+0x240+2*(place-1));
        int order = (int)word(ram+score+2)-word(ram+prev+2);
        if (!order) order=(int)word(ram+score)-word(ram+prev);
        if (!order) order=(int)word(ram+score+4)-word(ram+prev+4);
        if (order<=0) break;
        --place;
    }
    put(ram+p+0x1e,place+1);
    if (place<10) {
        ram[0x266] |= 1<<player;
        unsigned offset=0x240+2*place;
        put(ram+0x254+2*player,word(ram+0x252));
        memmove(ram+offset+2,ram+offset,(9-place)*2);
        put(ram+offset,score);
    }
    return place+1;
}

int world_gameover_master(uint8_t *ram, uint8_t *tiles, uint8_t *palette, const uint8_t *rom, int *sound)
{
    unsigned master=word(ram+0x80);
    if (master==0xc466 || master==0xc66e) {
        memset(ram+0xb700,0,46);
        put(ram+0x292,0);put(ram+0x266,0);
        if(master==0xc466) {
            unsigned first=insert_score(ram,0),second=insert_score(ram,1);
            if(first>=second) put(ram+0x11e,++first);
            if(first < 11 || second < 11) {
                memset(tiles,0,65536);
                put(ram+0x262,1);put(ram+0xd2,2);
                world_frame_init_actors(ram,0x19a6,0x8000);
                put(ram+0x80,0xc59b);
                return 1;
            }
            put(ram+0xd2,0);put(ram+0x80,0xc66e);
            return 1;
        }
        rank_table(ram,tiles,palette,rom);
        master=0xc7f8;
    }
    if(master==0xc59b) {
        unsigned mask=word(ram+0x266)&3;
        world_frame_init_palette(ram,palette,rom,word(rom+0xce75+2*mask));
        for(unsigned layer=0;layer<2;++layer) {
            uint16_t src=0x4dd4+80*layer,origin=layer?0x2030:0x1030;
            put(ram+0xb89a,0x0a04);
            for(unsigned x=0;x<10;++x) {
                put(ram+0xb89c,origin);
                for(unsigned y=0;y<4;++y,src+=2)
                    world_draw_tile_block(tiles,rom,0x4000*(layer+1),
                                         (origin+0x400*y)&0x3fff,word(rom+0xa0000+src));
                origin=(origin&0xff00)|(uint8_t)(origin+16);
            }
        }
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<40;++x) {
            unsigned pos=0xa032+256*y+4*x;
            put(tiles+pos,word(tiles+pos)+2);
        }
        for(unsigned row=0;row<2;++row)for(unsigned x=0;x<40;++x) {
            unsigned pos=0x1f30+256*row+4*x;
            put(tiles+pos,1);put(tiles+pos+2,0x111);
        }
        world_queue_sound(ram,0x0d);
        put(ram+0x86,0xf40);put(ram+0x80,0xc61a);
        master=0xc61a;
    }
    if(master==0xc61a) {
        int done=!word(ram+0xd2);
        if(!done) {
            put(ram+0x86,word(ram+0x86)-1);
            done=!word(ram+0x86) || (ram[0x2aa]&3);
        }
        if(done) {
            for(unsigned p=0x9e06;p<0xa006;p+=8) {
                put(ram+p+2,0x82);put(ram+p+4,5);put(ram+p+6,32);
            }
            put(ram+0x86,48);put(ram+0x80,0xc642);
        }
        return 1;
    }
    if(master==0xc642) {
        put(ram+0x86,word(ram+0x86)-1);
        if(!word(ram+0x86)) {
            ram[0x262]&=(uint8_t)~1u;put(ram+0xd2,1);
            uint16_t dest=word(ram+0x100);
            for(unsigned n=0;n<32;++n)put(ram+(uint16_t)(dest+2*n),0);
            put(ram+0x80,0xc66e);
        }
        return 1;
    }
    if ((master==0xc7f8 && !word(ram+0xd2)) || master==0xc82b) {
        if(word(ram+0x2b8) || (word(ram+0x2c4) && (ram[0x2aa]&3))) goto fade;
    }
    put(ram+0x86,word(ram+0x86)-1);
    if(word(ram+0x86)) return 1;
    if(master==0xc04 || master==0xc7d) {
        for(unsigned p=0x9e0e;p<0xa006;p+=8) {put(ram+p+2,0x82);put(ram+p+4,master==0xc7d ? 4 : 6);put(ram+p+6,0x1020);}
        put(ram+0x86,master==0xc7d ? 24 : 80); put(ram+0x80,master==0xc7d ? 0xcb7 : 0xc3e);
    } else if(master==0xc3e || master==0xcb7) {
        ram[0x262]&=(uint8_t)~1u;
        put(ram+0x284,0);put(ram+0x28c,0);
        world_frame_reset_sound(ram); *sound=0x20;
        for(unsigned p=0x9e06;p<0xa006;p+=8) {memset(ram+p,0,8);put(ram+p+2,0x40);}
        if (master==0xc3e) {put(ram+0x292,0);put(ram+0x80,0xc466);}
        else {put(ram+0x264,0);put(ram+0x84,0);put(ram+0x80,0xa09);}
    } else if(master==0xc7f8) {
        put(ram+0x86,128);put(ram+0x80,0xc82b);
    } else if(master==0xc82b) {
fade:
        for(unsigned p=0x9e06;p<0xa006;p+=8){put(ram+p+2,0x82);put(ram+p+4,5);put(ram+p+6,32);}
        put(ram+0x86,48);ram[0x263]|=128;put(ram+0x80,0xc861);
    } else if(master==0xc861) {
        put(ram+0xd2,0);put(ram+0x84,0);put(ram+0x264,0);ram[0x262]&=(uint8_t)~1u;put(ram+0x80,0xa09);
    } else return 0;
    return 1;
}
