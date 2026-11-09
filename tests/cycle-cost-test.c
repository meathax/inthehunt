
#include "../src/world_cycle_costs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char rom[1048576],actual[65536],expected[65536];
static uint16_t read_word(const uint8_t*p,uint16_t o){return p[o]|((uint16_t)p[(uint16_t)(o+1)]<<8);}
static void write_word(uint8_t*p,uint16_t o,uint16_t v){p[o]=v;p[(uint16_t)(o+1)]=v>>8;}
void world_draw_tile_block(uint8_t *tiles, const uint8_t *rom, uint16_t base,
                            uint16_t dest, uint16_t descriptor)
{
    const uint8_t *block = rom + ((uint32_t)(descriptor & 0xfffc) << 4);
    unsigned flip = descriptor & 3;
    for (unsigned y = 0; y < 4; y++) {
        unsigned row = flip & 2 ? 3 - y : y;
        unsigned line = (dest + row * 0x100) & 0x3fff;
        for (unsigned x = 0; x < 4; x++) {
            unsigned column = flip & 1 ? 3 - x : x;
            uint16_t cell = base + line + 4 * column;
            unsigned src = 16 * y + 4 * x;
            /* The World double-flip routine skips its third tile-number store. */
            if (flip != 3 || x != 2)
                write_word(tiles, cell, read_word(block, src));
            write_word(tiles, cell + 2, read_word(block, src + 2) ^ (flip << 9));
        }
    }
}

static uint64_t elapsed;
static unsigned stores,clears,loads;
static void event(void*ctx,const WorldCycleCheckpoint*p){
 (void)ctx;elapsed+=p->core_cycles;
 if(p->access==WORLD_CYCLE_ROM_WORD)loads++;
 if(p->access==WORLD_CYCLE_TILE_WORD){unsigned offset=p->video_address-0xd0000;uint16_t v=rom[p->rom_address]|((uint16_t)rom[p->rom_address+1]<<8);write_word(actual,offset,v^p->attribute_xor);stores++;}
 if(p->access==WORLD_CYCLE_TILE_CLEAR){write_word(actual,p->video_address-0xd0000,0);clears++;}
}
int main(void){
 for(unsigned i=0;i<sizeof rom;i++)rom[i]=(uint8_t)(i*17+(i>>8));
 const unsigned cycles[4]={240,411,415,403},words[4]={32,32,32,28};
 for(unsigned flip=0;flip<4;flip++){
  memset(actual,0xa5,sizeof actual);memcpy(expected,actual,sizeof actual);elapsed=stores=clears=loads=0;
  world_draw_tile_block(expected,rom,0x4000,0x3ff0,0x4010|flip);
  WorldCycleCost c=world_cycles_tile_block(0x40100,0x4000,0x3ff0,flip,event,0);
  if(memcmp(actual,expected,sizeof actual)||c.core_cycles!=cycles[flip]||c.video_words!=words[flip]||elapsed!=c.core_cycles||stores!=c.video_words||c.rom_words!=32)return 1;
 }
 for(unsigned mode=0;mode<4;mode++){
  memset(actual,0xa5,sizeof actual);memcpy(expected,actual,sizeof actual);elapsed=stores=clears=loads=0;
  for(unsigned i=0;i<4;i++)write_word(rom+0xa0000,0x100+2*i,0x4010+i);
  unsigned src=0x100,origin=0x1130;
  for(unsigned i=0;i<2;i++){unsigned pos=origin;for(unsigned j=0;j<2;j++){world_draw_tile_block(expected,rom,0x8000,pos,read_word(rom+0xa0000,src));src+=2;pos=mode<2?(pos+0x400)&0x3fff:(pos&0xff00)|((pos+16)&255);}origin=mode<2?(origin&0xff00)|((origin+16)&255):(origin+(mode==2?0x400:-0x400))&0x3fff;}
  WorldCycleCost c=world_cycles_tile_transfer(rom,0x100,0x8000,0x1130,0x202,mode,event,0);
  if(memcmp(actual,expected,sizeof actual)||elapsed!=c.core_cycles||stores!=124||c.video_words!=124||c.rom_words!=136)return 2;
 }
 memset(actual,0xa5,sizeof actual);elapsed=stores=clears=loads=0;
 WorldCycleCost c=world_cycles_video_clear(event,0);
 if(c.core_cycles!=98332||c.video_words!=32768||clears!=32768||elapsed!=c.core_cycles)return 3;
 for(unsigned i=0;i<sizeof actual;i++)if(actual[i])return 4;
 FILE*f=fopen(".local/world/maincpu.bin","rb");if(!f)return 5;fread(rom,1,sizeof rom,f);fclose(f);
 for(unsigned row=0x4909;row<=0x4987;row+=0x7e){
  c=world_cycles_tile_transfer(rom,read_word(rom,row+2),0x4000,read_word(rom,row+4),read_word(rom,row+6),read_word(rom,row+8),0,0);
  printf("opening row %04x nominal %llu clocks, video %u words, rom %u words, missing %u\n",row,(unsigned long long)c.core_cycles,c.video_words,c.rom_words,c.missing_timing);
 }
 puts("block flips, descriptor traversal modes, clear sequence and nominal counts pass");
}
