#ifdef NDEBUG
#error Regression tests require assertions; compile with -UNDEBUG.
#endif
/* Build: zig cc -O2 tools/test_waterfx.c src/world_video.c src/world_settings.c
 * src/world_input.c src/world_timing_options.c -luser32 -o waterfx-test.exe */
#include "../src/world_scene.c"
#include "../src/world_settings.h"
#include "../src/world_surface.h"
#include <assert.h>
static uint32_t picture[N*MAXK*MAXK], clean[N*MAXK*MAXK];
static int active(int kind) { int n=0; for(int i=wf_start[kind];i<wf_start[kind+1];i++) n+=waterfx[i].life>0; return n; }
static void put16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void actor(uint8_t *p,int x,int y,int wide,unsigned bank) {
    put16(p,(unsigned)(360-y)|(wide?0x800:0));put16(p+2,0x100);put16(p+4,bank);put16(p+6,(unsigned)(x+96));
}
static void setup(int k) {
    prepare();world_scene_reset();memset(&cur,0,sizeof cur);memset(&prev,0,sizeof prev);
    cur.waterline=prev.waterline=g.waterline=60;g.k=k;g.ow=W*k;g.oh=H*k;
    memset(solid,0,sizeof solid);memset(layer_on,0,sizeof layer_on);
    memset(g.row_dx,0,sizeof g.row_dx);g.shake_x=g.shake_y=field_on=0;
    memset(water_map,255,sizeof water_map);
    for(int i=0;i<LW*MAXK+8;i++)divide[i]=(uint16_t)(i/k);
    for(int i=0;i<N*k*k;i++){sprite_px[i]=NONE;picture[i]=clean[i]=0xff163949;}
}
int main(void) {
    uint8_t surface_ram[65536]={0};
    surface_ram[0x292]=5;surface_ram[0x298]=4;put16(surface_ram+0x84,2);
    assert(world_surface_row(surface_ram)==168);
    put16(surface_ram+0xb710,24);assert(world_surface_row(surface_ram)==144);
    put16(surface_ram+0x84,1);assert(world_surface_row(surface_ram)==-1);
    surface_ram[0x298]=0;assert(world_surface_row(surface_ram)==88);
    surface_ram[0x298]=2;assert(world_surface_row(surface_ram)==144);
    surface_ram[0x298]=6;assert(world_surface_row(surface_ram)==-1);
    surface_ram[0x298]=8;assert(world_surface_row(surface_ram)==80);
    surface_ram[0x292]=0;assert(world_surface_row(surface_ram)==-1);
    WorldSceneFx fx={0};setup(1);fx.wakes=1;
    /* Captured native sub geometry: three adjacent 16x32 pieces, bank 1. */
    for(int i=0;i<3;i++) {
        actor(prev.sprites+i*8,70+i*16,100,0,1);actor(cur.sprites+i*8,72+i*16,100,0,1);
        put16(prev.sprites+i*8,(360-100-16)|0x200);put16(cur.sprites+i*8,(360-100-16)|0x200);
    }
    waterfx_begin_frame();waterfx_begin_frame();waterfx_motion(&fx);
    assert(active(WF_WAKE)==3 && !active(WF_DEBRIS)&&!active(WF_SILT)&&!active(WF_SPLASH));
    float before_x=waterfx[0].x, before_y=waterfx[0].y;
    waterfx_scroll(5,3);assert(waterfx[0].x==before_x-5 && waterfx[0].y==before_y-3);
    waterfx_reset();memset(cur.sprites,0,sizeof cur.sprites);memset(prev.sprites,0,sizeof prev.sprites);
    fx.wakes=0;fx.splashes=1;
    actor(prev.sprites,100,48,0,3);actor(cur.sprites,100,56,0,3);
    waterfx_begin_frame();waterfx_motion(&fx);assert(active(WF_SPLASH)==1);
    waterfx_reset();cur.waterline=g.waterline=68;cur.layers[1].scroll_y=(uint16_t)-8;
    waterfx_begin_frame();waterfx_motion(&fx);assert(!active(WF_SPLASH)); /* camera-only crossing */
    setup(1);fx=(WorldSceneFx){0};fx.silt=1;
    waterfx_begin_frame();waterfx_impact(120,130);assert(!active(WF_SILT));
    layer_on[1]=1;memset(layer_col[1],0,sizeof layer_col[1]);
    for(int y=145;y<HUD;y++)for(int x=80;x<200;x++)layer_col[1][(y+MARGIN)*LW+x+MARGIN]=0xff507070;
    transport(&fx);assert(!transport_valid && wf_rock(120,150)); /* independent of tracing */
    waterfx_impact(120,135);assert(active(WF_SILT)==1);
    waterfx_impact(121,135);assert(active(WF_SILT)==1);
    fx.debris=1;waterfx_blast(120,130,200,&fx);assert(active(WF_DEBRIS)==10);
    for(int i=0;i<20;i++)waterfx_blast(180,130,200,&fx);
    assert(active(WF_DEBRIS)==16 && active(WF_SILT)<=2); /* frame emission limits */
    fx=(WorldSceneFx){0};waterfx_draw(picture,&fx,0.016f);
    assert(!memcmp(picture,clean,sizeof(uint32_t)*N));for(int k=0;k<4;k++)assert(!active(k));
    setup(1);fx.wakes=1;WaterFx *p=wf_spawn(WF_WAKE,100,100,1);assert(p);p->size=2;
    sprite_px[100*W+100]=1;waterfx_draw(picture,&fx,0);
    assert(picture[100*W+100]==clean[100*W+100]);assert(picture[100*W+101]!=clean[100*W+101]);
    for(int y=0;y<60;y++)assert(!memcmp(picture+y*W,clean+y*W,W*4));
    assert(!memcmp(picture+HUD*W,clean+HUD*W,(H-HUD)*W*4));
    layer_on[0]=1;
    for(int i=0;i<LN;i++)layer_col[0][i]=0xffa07030;
    wf_pixel(picture,120,50,185,230,240,128,0);
    assert(picture[50*W+120]!=clean[50*W+120]); /* airborne spray over city backdrop */
    wf_pixel(picture,120,HUD,185,230,240,128,0);
    assert(picture[HUD*W+120]!=clean[HUD*W+120]);
    layer_on[0]=0;
    for(int i=0;i<12;i++)waterfx_draw(picture,&fx,0.1f);assert(!active(WF_WAKE));
    setup(1);p=wf_spawn(WF_DEBRIS,120,140,3);p->vy=40;p->size=2;
    solid[71*PW+60]=1;fx=(WorldSceneFx){0};fx.debris=1;waterfx_draw(picture,&fx,0.06f);
    assert(p->settled && p->y==140);
    setup(1);fx=(WorldSceneFx){0};fx.debris=1;
    actor(cur.sprites,116,90,0,10);put16(cur.sprites,(360-90-48)|0x400);put16(cur.sprites+2,0x1780);
    waterfx_begin_frame();waterfx_motion(&fx);assert(active(WF_DEBRIS)==6);
    memcpy(prev.sprites,cur.sprites,sizeof cur.sprites);
    waterfx_begin_frame();waterfx_motion(&fx);assert(active(WF_DEBRIS)==6); /* no repeat for same smoke */
    world_scene_reset();for(int k=0;k<4;k++)assert(!active(k));
    WorldSettings saved,loaded;world_settings_defaults(&saved);
    assert(saved.fx_wakes&&saved.fx_debris&&saved.fx_silt&&saved.fx_splashes);
    saved.fx_wakes=saved.fx_silt=0;
    assert(world_settings_save(".local/waterfx-review/settings-test.ini",&saved)==WORLD_SETTINGS_OK);
    assert(world_settings_load(".local/waterfx-review/settings-test.ini",&loaded)==WORLD_SETTINGS_OK);
    assert(!loaded.fx_wakes&&loaded.fx_debris&&!loaded.fx_silt&&loaded.fx_splashes);
    for(int k=1;k<=3;k+=2) {
        setup(k);fx=(WorldSceneFx){0};fx.wakes=fx.debris=fx.silt=fx.splashes=1;
        for(int kind=0;kind<4;kind++)for(int n=0;n<wf_start[kind+1]-wf_start[kind];n++) {
            waterfx_begin_frame();WaterFx *q=wf_spawn(kind,20+(n*17)%280,85+(n*13)%110,10);assert(q);
            q->age=2;q->size=kind==WF_SILT?12:2;
            if(kind==WF_SPLASH){q->life=0.85f;q->age=0.3f;}
        }
        for(int i=0;i<30;i++)waterfx_draw(picture,&fx,0);
        double t=stamp();for(int i=0;i<200;i++)waterfx_draw(picture,&fx,0);
        printf("Saturated original pools K%d: %.3f ms, %u native samples, %d live objects\n",k,(stamp()-t)/200,wf_samples,wf_start[WF_SPARK]);
        assert(wf_samples<=25000);
    }
    setup(3);fx=(WorldSceneFx){0};fx.wakes=fx.debris=fx.silt=fx.splashes=1;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
        uint32_t c=y<60?0xff6b99b0u:y>=190?0xff30444au:0xff103c54u;
        water_map[y*W+x]=(uint8_t)(y<190?255:0);
        for(int yy=0;yy<3;yy++)for(int xx=0;xx<3;xx++)picture[(y*3+yy)*W*3+x*3+xx]=c;
    }
    for(int i=0;i<18;i++){waterfx_begin_frame();WaterFx*q=wf_spawn(WF_WAKE,48+i*3,115+(i%3)-1,1.3f);q->age=(18-i)*0.04f;q->size=1.5f;}
    for(int i=0;i<14;i++){waterfx_begin_frame();WaterFx*q=wf_spawn(WF_DEBRIS,180+(i%5)*5,125+(i/5)*12,3);q->age=0.5f;q->size=2;}
    for(int i=0;i<3;i++){waterfx_begin_frame();WaterFx*q=wf_spawn(WF_SILT,120+i*12,185-i*2,2.8f);q->age=1;q->size=7;}
    waterfx_begin_frame();WaterFx*q=wf_spawn(WF_SPLASH,235,60,0.85f);q->age=0.3f;
    waterfx_draw(picture,&fx,0);
    FILE *preview=fopen(".local/waterfx-review/preview.bmp","wb");assert(preview);
    BITMAPFILEHEADER bh={0};BITMAPINFOHEADER bi={0};bh.bfType=0x4d42;bh.bfOffBits=sizeof bh+sizeof bi;
    bh.bfSize=bh.bfOffBits+N*9*4;bi.biSize=sizeof bi;bi.biWidth=W*3;bi.biHeight=-H*3;bi.biPlanes=1;bi.biBitCount=32;
    fwrite(&bh,sizeof bh,1,preview);fwrite(&bi,sizeof bi,1,preview);fwrite(picture,4,N*9,preview);fclose(preview);
    setup(1);fx=(WorldSceneFx){0};fx.sparks=fx.impact_motes=1;
    waterfx_begin_frame();waterfx_blast(120,120,100,&fx);
    assert(active(WF_SPARK)>0 && active(WF_MOTE)>0);
    int sparks=active(WF_SPARK),motes=active(WF_MOTE);
    waterfx_blast(120,120,100,&fx);assert(active(WF_SPARK)==sparks && active(WF_MOTE)==motes);
    assert(active(WF_SPARK)<=16 && active(WF_MOTE)<=20);
    waterfx_draw(picture,&fx,0.016f);assert(memcmp(picture,clean,sizeof(uint32_t)*N));
    for(int i=0;i<50;i++)waterfx_draw(picture,&fx,0.016f);
    assert(!active(WF_SPARK));
    fx.impact_motes=0;waterfx_draw(picture,&fx,0);assert(!active(WF_MOTE));
    setup(1);fx=(WorldSceneFx){0};fx.sparks=fx.impact_motes=1;
    waterfx_begin_frame();waterfx_blast(120,40,100,&fx);assert(!active(WF_SPARK)&&!active(WF_MOTE));
    waterfx_begin_frame();WaterFx *spark=wf_spawn(WF_SPARK,100,100,0.3f);spark->vx=80;
    float speed=spark->vx;waterfx_draw(picture,&fx,0.03f);assert(spark->vx<speed);
    WaterFx *bubble=wf_spawn(WF_MOTE,140,100,1);bubble->light=1;
    WaterFx *heavy=wf_spawn(WF_MOTE,160,100,1);
    waterfx_draw(picture,&fx,0.03f);assert(bubble->vy<0&&heavy->vy>0);
    bubble->y=59;waterfx_draw(picture,&fx,0.03f);assert(!bubble->life);
    solid[50*PW+80]=1;waterfx_draw(picture,&fx,0.03f);assert(heavy->settled);
    setup(3);fx=(WorldSceneFx){0};fx.sparks=fx.impact_motes=1;
    for(int kind=WF_SPARK;kind<=WF_MOTE;++kind)for(int n=0;n<wf_start[kind+1]-wf_start[kind];++n){
        waterfx_begin_frame();WaterFx *p=wf_spawn(kind,20+n%280,90+n%100,1);assert(p);p->age=0.1f;
    }
    waterfx_draw(picture,&fx,0);assert(wf_samples<=448);
    /* Every visible subpixel is protected when covered by a sprite. */
    for(int i=0;i<N*9;++i){sprite_px[i]=1;picture[i]=clean[i]=0xff123456;}
    waterfx_draw(picture,&fx,0);assert(!memcmp(picture,clean,sizeof(uint32_t)*N*9));
    world_settings_defaults(&saved);saved.fx_sparks=0;saved.fx_impact_motes=1;
    assert(world_settings_save(".local/waterfx-review/settings-test.ini",&saved)==WORLD_SETTINGS_OK);
    assert(world_settings_load(".local/waterfx-review/settings-test.ini",&loaded)==WORLD_SETTINGS_OK);
    assert(!loaded.fx_sparks&&loaded.fx_impact_motes);
    FILE *legacy=fopen(".local/waterfx-review/settings-test.ini","ab");assert(legacy);
    fputs("\nfx_shimmer=1\n",legacy);fclose(legacy);
    assert(world_settings_load(".local/waterfx-review/settings-test.ini",&loaded)==WORLD_SETTINGS_OK);
    assert(!loaded.fx_sparks&&loaded.fx_impact_motes);
    assert(world_settings_save(".local/waterfx-review/settings-test.ini",&loaded)==WORLD_SETTINGS_OK);
    legacy=fopen(".local/waterfx-review/settings-test.ini","rb");assert(legacy);
    char settings_text[16384];size_t settings_size=fread(settings_text,1,sizeof settings_text-1,legacy);
    settings_text[settings_size]=0;fclose(legacy);assert(!strstr(settings_text,"fx_shimmer"));
    /* Cached lighting is the same bilinear lookup, including image borders. */
    for(int i=0;i<PN;++i){total[0][i]=(i%19)*0.03f;total[1][i]=(i%23)*0.02f;
        total[2][i]=(i%29)*0.01f;light_dx[i]=(i%7)-3;light_dy[i]=(i%11)-5;sun_vis[i]=(i%5)*0.25f;}
    cache_transport();
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        PixelLight *p=&pixel_light[y*W+x];float dx=probe_sample(light_dx,x,y),dy=probe_sample(light_dy,x,y);
        float inv=1.0f/sqrtf(dx*dx+dy*dy+0.0001f);
        assert(fabsf(p->r-probe_sample(total[0],x,y))<0.00001f);
        assert(fabsf(p->g-probe_sample(total[1],x,y))<0.00001f);
        assert(fabsf(p->b-probe_sample(total[2],x,y))<0.00001f);
        assert(fabsf(p->sun-probe_sample(sun_vis,x,y))<0.00001f);
        assert(fabsf(p->dx-dx*inv)<0.00001f&&fabsf(p->dy-dy*inv)<0.00001f);
    }
    puts("PASS: cached lighting matches exact bilinear samples over all native pixels");
    puts("PASS: micro sparks/motes emission, dedup, budgets, drag, buoyancy, settling, surface, expiry, occlusion, settings");
    puts("PASS: independent emission, tracing-off terrain, camera crossing, caps, occlusion, HUD/sky, expiry, settling, reset, settings round trip");
    return 0;
}
