/* Private fixture test: ROM directory + a native warship encounter snapshot.
   Compile with -UNDEBUG and the same sources as the game, replacing world_native.c. */
#ifdef NDEBUG
#error Sprite regression requires assertions enabled.
#endif
#define wmain static unused_game_entry
#include "../src/world_native.c"
#undef wmain
#include <assert.h>
static unsigned char saved_ram[65536];
static void put16(unsigned at, unsigned value) { ram[at]=value;ram[at+1]=value>>8; }
int wmain(int argc,wchar_t **args)
{
    assert(argc==3);
    char directory[4096];
    assert(WideCharToMultiByte(CP_UTF8,0,args[1],-1,directory,sizeof directory,NULL,NULL));
    assert(load(directory,"maincpu.bin",rom,sizeof rom));
    FILE *f=_wfopen(args[2],L"rb");assert(f);
    assert(fread(saved_ram,1,sizeof saved_ram,f)==sizeof saved_ram);fclose(f);
    unsigned tested=0,animated=0;
    for(unsigned actor=0x400;actor<0x8400;actor+=0x80) {
        memcpy(ram,saved_ram,sizeof ram);
        unsigned state=word(ram+actor);
        if(word(ram+actor+2)!=0xb000 || (state!=0x6744 && state!=0x6781 && state!=0x64b6))continue;
        unsigned parent=word(ram+0x84fe + word(ram+actor+8));assert(parent>=0x400 && parent<0x8400);
        ram[parent+0xa]=0;put16(parent+0x21,250);put16(parent+0x24,280);
        put16(parent+0x6c,0);put16(actor+0x44,1);put16(actor+0x40,0);
        put16(actor+0x26,0x400);put16(actor+0x21,0);put16(actor+0x24,0);
        put16(0x264,0);put16(0xa6e2,0);put16(0xaee6,0);
        put16(0x9e02,210);put16(0x9e04,0x8c00);
        for(unsigned i=0;i<128;i+=2)put16(0x9c00+i,0xffff);
        unsigned x=(250+word(ram+actor+0x31))&65535,y=(280+word(ram+actor+0x34))&65535;
        WorldDispatch pass={WORLD_AFTER_PLAYER_TWO,(uint16_t)actor};
        assert(world_dispatch_native_video(&pass,ram,rom,tiles));
        assert(word(ram+actor+0x21)==x && word(ram+actor+0x24)==y);
        assert(word(ram+0x9e04)==0x8c10);
        assert(word(ram+0x8c04)==x && word(ram+0x8c06)==y);
        if(state==0x6781) {
            assert(word(ram+actor+0x2a)==word(rom+0xb5410));
            assert(word(ram+actor+0x40)==2);++animated;
        }
        ++tested;
    }
    assert(tested>=5 && animated);
    printf("PASS: %u hull parts follow their parent and submit sprites; underside animation advances\n",tested);
    return 0;
}
