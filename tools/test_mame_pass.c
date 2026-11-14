/* Compare one actor pass with MAME. Build as world_native.c replacement;
   arguments: ROM directory, private snapshot, output prefix. */
#define wmain static unused_game_entry
#include "../src/world_native.c"
#undef wmain
#include <assert.h>
int wmain(int argc,wchar_t **args) {
 if(argc!=4)return 1;
 char dir[4096];WideCharToMultiByte(CP_UTF8,0,args[1],-1,dir,sizeof dir,NULL,NULL);
 if(!load(dir,"maincpu.bin",rom,sizeof rom))return 1;
 FILE *f=_wfopen(args[2],L"rb");if(!f)return 1;
 if(fread(ram,1,65536,f)!=65536 || fread(tiles,1,65536,f)!=65536)return 1;fclose(f);
 WorldDispatch pass;world_dispatch_begin(&pass,ram);uint32_t addr;unsigned calls=0;
 while(world_dispatch_target(&pass,ram,&addr)) {
  if(!world_dispatch_native_video(&pass,ram,rom,tiles)) {fprintf(stderr,"stop %05x actor %04x %s\n",addr,pass.actor,world_dispatch_detail);return 2;}
  if(++calls>65536)return 3;
 }
 wchar_t path[4096];swprintf(path,4096,L"%ls.work",args[3]);f=_wfopen(path,L"wb");if(!f)return 1;fwrite(ram,1,65536,f);fclose(f);
 swprintf(path,4096,L"%ls.video",args[3]);f=_wfopen(path,L"wb");if(!f)return 1;fwrite(tiles,1,65536,f);fclose(f);
 printf("%u calls; %lu estimated register reads\n",calls,world_dispatch_estimates);return 0;
}
