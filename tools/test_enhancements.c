#ifdef NDEBUG
#error Regression tests require assertions; compile with -UNDEBUG.
#endif
/* Native compositor regression: no ROMs or desktop required. */
#include "../src/world_scene.c"
#include "../src/world_settings.h"
#include <assert.h>
static uint8_t test_tiles[65536],test_sprites[2048],test_gfx1[2097152],test_gfx2[4194304];
static uint32_t test_colors[1024],picture[MAXK*MAXK*N],plain[N];
static WorldSceneFrame frame;
static void put16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void pen_tile(int code,int x,int y,int pen){
    for(int b=0;b<4;++b)if(pen&(1<<b))test_gfx1[code*8+y+b*0x80000]|=1<<(7-x);
}
static void pen_sprite(int code,int x,int y,int pen){
    for(int b=0;b<4;++b)if(pen&(1<<b))test_gfx2[code*32+y+((x&8)<<1)+b*0x100000]|=1<<(7-(x&7));
}
static void fixture(void){
    for(int p=0;p<1024;++p)test_colors[p]=0xff000000u|((p*29)&255)<<16|((p*17)&255)<<8|((p*11)&255);
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){pen_tile(1,x,y,1+(x+y)%14);pen_tile(2,x,y,6);}
    for(int i=0;i<2048;++i){put16(test_tiles+i*4,1);put16(test_tiles+i*4+2,(i%8)*16);}
    /* Layer 1 has a solid column, all other cells transparent. */
    for(int row=0;row<64;++row){put16(test_tiles+0x4000+(row*64+15)*4,2);put16(test_tiles+0x4000+(row*64+15)*4+2,0x100);}
    for(int y=0;y<32;++y)for(int x=0;x<16;++x)if(x>1&&x<14&&y>1&&y<30)pen_sprite(0xc1e + y/16,x,y&15,2+(x+y)%14);
    put16(test_sprites,(360-100-16)|0x200);put16(test_sprites+2,0xc1e);
    put16(test_sprites+4,0x81);put16(test_sprites+6,96+96);
    frame=(WorldSceneFrame){.tiles=test_tiles,.sprites=test_sprites,.colors=test_colors,
        .layers={{.master=0x10},{.master=1},{.master=0}},.waterline=60,.scene_id=1,.video_control=0x2000};
    world_scene_init(test_gfx1,test_gfx2);world_scene_reset();world_scene_submit(&frame);
}
static int count_events(void){int n=0;for(int i=0;i<CINEMA_EVENTS;++i)n+=cinema_events[i].life>0;return n;}
int main(void){
    SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS);
    fixture();WorldSceneFx fx={0};
    for(int y=0;y<H;++y){uint16_t indices[W];world_video_scanline(indices,y,test_tiles,test_gfx1,test_gfx2,test_sprites,frame.layers,frame.video_control);
        for(int x=0;x<W;++x)plain[y*W+x]=test_colors[indices[x]&1023];}
    for(int k=1;k<=MAXK;++k){
        assert(world_scene_render(picture,k,1,&fx,8000));
        for(int y=0;y<H*k;++y)for(int x=0;x<W*k;++x)assert(picture[y*W*k+x]==plain[(y/k)*W+x/k]);
    }
    puts("PASS: all effects off exactly matches arcade renderer at 1x/2x/3x/4x");
    fx.pressure=1;world_scene_render(picture,3,1,&fx,8100);
    cinema_blast(110,116,120,&fx);cinema_blast(110,116,120,&fx);assert(count_events()==1);
    world_scene_render(picture,3,1,&fx,9300);assert(field_on);
    int changed=0;
    for(int y=0;y<HUD;++y)for(int x=0;x<W;++x){
        if(sprite_px[y*3*W*3+x*3]!=NONE)assert(picture[y*3*W*3+x*3]==plain[y*W+x]);
        else if(picture[y*3*W*3+x*3]!=plain[y*W+x])++changed;
    }
    assert(changed>0);puts("PASS: pressure change backdrop but preserve sprite positions/colours");

    /* Foreground priority remains at its arcade coordinates under pressure refraction. */
    for(int y=61;y<HUD;++y)for(int x=0;x<W;++x){
        int i=(y+MARGIN)*LW+x+MARGIN;
        if(layer_pri[1][i]&&sprite_px[y*3*W*3+x*3]==NONE)assert(picture[y*3*W*3+x*3]==plain[y*W+x]);
    }
    float old_x=cinema_events[0].x;frame.layers[1].scroll_x+=4;world_scene_submit(&frame);
    assert(cinema_events[0].x==old_x-4);frame.scene_id++;world_scene_submit(&frame);assert(!count_events());
    fx=(WorldSceneFx){.pressure=1,.sediment_light=1,.water_volume=1,.aftermath=1};g.fx=&fx;
    cinema_blast(120,30,120,&fx);assert(!count_events());
    for(int i=0;i<100;++i)cinema_blast(8+(i%10)*31,70+(i/10)*15,120,&fx);
    assert(count_events()<=CINEMA_EVENTS && count_events()>0);
    for(int i=0;i<N;++i)water_map[i]=255;
    cinema_update(.1f);cinema_draw(picture);assert(cinema_samples<=14500);
    for(int i=0;i<45;++i)cinema_update(.1f);assert(!count_events());
    fx=(WorldSceneFx){0};cinema_update(.01f);for(int i=0;i<VW*VH;++i)assert(!water_volume[i]);
    puts("PASS: event dedup, fixed pool, work cap, waterline, expiry, camera follow and scene reset");
    /* Every effect has an independent event path, including tracing disabled. */
    for(int kind=0;kind<4;++kind){
        fx=(WorldSceneFx){0};g.fx=&fx;cinema_reset();
        if(kind==0)fx.pressure=1;if(kind==1)fx.sediment_light=1;if(kind==2)fx.water_volume=1;if(kind==3)fx.aftermath=1;
        cinema_blast(120,100,120,&fx);assert(count_events()==1);
    }
    fx=(WorldSceneFx){.materials=1};g.fx=&fx;material_palette_prepare();material_prepare();
    assert(material_kind(0xc1e,1,15,0)==MAT_GLASS);
    assert(material_kind(0x2de0,51,7,0)==MAT_RUST);
    uint32_t a=material_surface(0xc1e,1,7,6,8,0,0),b=material_surface(0xc1e,1,7,6,8,1,1);
    assert((int8_t)(a>>8)==-(int8_t)(b>>8) && (int8_t)(a>>16)==-(int8_t)(b>>16));
    uint32_t c=0xff607080;uint32_t dark=material_shade(c,MAT_STEEL,100,100);
    material_light[100*W+100].energy=2;material_light[100*W+100].r=1;
    assert(dark!=material_shade(c,MAT_STEEL,100,100));
    for(int kind=1;kind<MAT_COUNT;++kind)assert((material_shade(c,kind,100,100)>>24)==255);
    puts("PASS: authored material roles, flip-correct normals and dynamic lighting response");
    WorldSettings saved,loaded;world_settings_defaults(&saved);
    saved.fx_pressure=0;saved.fx_sediment_light=1;saved.fx_water_volume=0;saved.fx_ocean_depth=1;
    saved.fx_aftermath=0;saved.fx_materials=1;
    assert(world_settings_save("build/light-colour-20261113/settings.ini",&saved)==WORLD_SETTINGS_OK);
    assert(world_settings_load("build/light-colour-20261113/settings.ini",&loaded)==WORLD_SETTINGS_OK);
    assert(!loaded.fx_pressure&&loaded.fx_sediment_light&&!loaded.fx_water_volume&&loaded.fx_ocean_depth);
    assert(!loaded.fx_aftermath&&loaded.fx_materials);
    puts("PASS: six independent settings persist");
    float red=8,green=4,blue=1;light_shoulder(&red,&green,&blue);
    assert(fabsf(red/green-2)<1e-5f && fabsf(red/blue-8)<1e-5f);
    uint32_t warm=light_rgb(510,255,64);assert(((warm>>16)&255)==255 && ((warm>>8)&255)==127 && (warm&255)==32);
    g.waterline=60;assert(submerged_path(20,40,20)==0);assert(submerged_path(40,80,40)==20);
    assert(submerged_path(80,40,40)==20);g.waterline=-1;assert(submerged_path(20,40,20)==20);
    for(int d=1;d<320;++d){assert(light_absorb[0][d]<light_absorb[1][d] && light_absorb[1][d]<light_absorb[2][d]);
        for(int ch=0;ch<3;++ch)assert(light_absorb[ch][d]<=light_absorb[ch][d-1]);}
    FILE *old=fopen("build/light-colour-20261113/settings.ini","ab");assert(old);fputs("\nfx_diorama=1\n",old);fclose(old);
    assert(world_settings_load("build/light-colour-20261113/settings.ini",&loaded)==WORLD_SETTINGS_OK);
    assert(world_settings_save("build/light-colour-20261113/settings.ini",&loaded)==WORLD_SETTINGS_OK);
    old=fopen("build/light-colour-20261113/settings.ini","rb");assert(old);char text[16384];size_t n=fread(text,1,sizeof text-1,old);text[n]=0;fclose(old);
    assert(!strstr(text,"fx_diorama"));
    /* Exercise the actual direct transport, with no geometry or bounce light:
     * neutral light stays neutral in air, but loses red first through water. */
    fx=(WorldSceneFx){.lights=1};transport_fx=&fx;transport_valid=0;
    memset(albedo,0,sizeof albedo);memset(total,0,sizeof total);memset(traced,1,sizeof traced);
    for(int i=0;i<PN;++i)sdf[i]=1e6f;
    light_count=1;lights[0]=(Light){20.5f,50.5f,1,1,1,80};bin_lights();
    g.waterline=220;transport_rows(50,51);int q=50*PW+40;
    assert(direct[0][q]>0 && fabsf(direct[0][q]-direct[2][q])<1e-6f);
    g.waterline=-1;transport_rows(50,51);
    assert(direct[0][q]<direct[1][q] && direct[1][q]<direct[2][q]);
    puts("PASS: actual traced white light stays neutral in air and attenuates spectrally underwater");
    /* Paused preview toggles must invalidate lighting immediately, but repeated
     * identical refreshes must not redo transport or layer work. */
    fixture();fx=(WorldSceneFx){0};world_scene_render(picture,1,1,&fx,16000);
    int before_transport=transport_frame;
    fx.glow=1;world_scene_render(picture,1,1,&fx,16000);
    assert(transport_valid && transport_frame==before_transport+1);
    uint32_t before_layer_clock=layer_clock;
    world_scene_render(picture,1,1,&fx,16000);
    assert(transport_frame==before_transport+1 && layer_clock==before_layer_clock);
    fx.glow=0;world_scene_render(picture,1,1,&fx,16000);assert(!transport_valid);
    /* A cut clears EVERY temporal family, not only the new water effects. */
    rings[0]=(Ring){100,100,0,.5f,1};particles[0]=(Particle){100,100,0,0,0,1,0};
    have_history=1;shake=2;rumble=20000;hot_before[0]=20;
    frame.scene_id++;world_scene_submit(&frame);
    assert(!rings[0].life&&!particles[0].life&&!have_history&&!shake&&!rumble&&!hot_before[0]);
    memset(solid,0,sizeof solid);solid[50*PW+52]=1;
    assert(!wf_rock(100,100)&&!wf_rock(110,100)&&wf_sweep(100,100,110,100));
    assert(!wf_sweep(100,96,110,96));
    fx=(WorldSceneFx){.shock=1,.pressure=1};g.fx=&fx;
    rings[0]=(Ring){100,100,0,.5f,1};build_field(&fx,.01f);assert(!field_on);
    /* Every legacy particle stamp protects sprite-covered subpixels and HUD. */
    g.k=1;g.ow=W;g.oh=H;g.shake_x=g.shake_y=0;
    for(int i=0;i<N;++i){water_map[i]=255;sprite_px[i]=NONE;picture[i]=0xff123456;}
    sprite_px[100*W+100]=1;add_block(picture,100,100,1,200,200,200);assert(picture[100*W+100]==0xff123456);
    add_block(picture,100,HUD,1,200,200,200);assert(picture[HUD*W+100]!=0xff123456);
    puts("PASS: immediate toggles, repeat-refresh cache, scene-cut reset, swept terrain, pressure precedence and particle occlusion");
    fixture();fx=(WorldSceneFx){.materials=1,.vivid=1,.surface=1};
    static uint32_t reference[N*9];
    world_scene_render(picture,3,1,&fx,24000);memcpy(reference,picture,sizeof reference);
    world_scene_render(picture,3,1,&fx,24000);assert(!memcmp(reference,picture,sizeof reference));
    scene_cache_valid=0;world_scene_render(picture,3,1,&fx,24000);assert(!memcmp(reference,picture,sizeof reference));
    puts("PASS: cached refresh and forced full rebuild produce identical material/colour/surface pixels");
    puts("PASS: hue-preserving shoulders/output, water path crossings, spectral attenuation, legacy parallax setting removal");
    /* Opaque HUD glyphs stay exact, but their transparent gaps receive effects
     * through the final row, including post-composite trails and particles. */
    fixture();
    for(int y=0;y<8;++y)for(int x=0;x<4;++x)pen_tile(3,x,y,6);
    frame.layers[0].master=2;
    frame.layers[2].scroll_y=(uint16_t)-136;
    for(int row=45;row<47;++row)put16(test_tiles+0x8000+(row*64+10)*4,3);
    put16(test_sprites,360-224);put16(test_sprites+4,0x8b);
    world_scene_submit(&frame);
    for(int y=0;y<H;++y){uint16_t indices[W];world_video_scanline(indices,y,test_tiles,test_gfx1,test_gfx2,test_sprites,frame.layers,frame.video_control);
        for(int x=0;x<W;++x)plain[y*W+x]=test_colors[indices[x]&1023];}
    for(int k=1;k<=MAXK;++k){
        fx=(WorldSceneFx){0};world_scene_render(picture,k,1,&fx,32000);
        for(int y=0;y<H*k;++y)for(int x=0;x<W*k;++x)assert(picture[y*W*k+x]==plain[(y/k)*W+x/k]);
        fx=(WorldSceneFx){.depth=1,.vivid=1,.caustics=1,.trails=1,.snow=1};
        world_scene_render(picture,k,1,&fx,32133);
        world_scene_render(picture,k,1,&fx,32266);
        for(int y=224*k;y<H*k;++y){
            int changes=0;
            for(int x=0;x<W*k;++x){
                int glyph=(x<4*k), sprite=sprite_px[y*W*k+x]!=NONE;
                if(glyph||sprite)assert(picture[y*W*k+x]==plain[(y/k)*W+x/k]);
                else changes+=picture[y*W*k+x]!=plain[(y/k)*W+x/k];
            }
            assert(changes>W*k/2);
        }
    }
    puts("PASS: effects reach every bottom row, opaque HUD tiles/sprites stay exact, transparent gaps remain enhanced at 1x-4x");
    /* Upgrade a disabled legacy preset, then keep the player's overrides. */
    const char *preset_file="build/light-colour-20261113/settings.ini";
    FILE *legacy=fopen(preset_file,"wb"); assert(legacy);
    fputs("version=1\nfx_enable=0\nfx_vignette=1\nfx_dof=0\nfx_trails=0\n",legacy); fclose(legacy);
    assert(world_settings_load(preset_file,&loaded)==WORLD_SETTINGS_OK);
    loaded.fx_enable=1; world_settings_activate_enhancements(&loaded);
    assert(loaded.fx_dof && loaded.fx_trails && loaded.fx_shake && loaded.fx_vivid);
    loaded.fx_dof=0; loaded.fx_enable=0;
    assert(world_settings_save(preset_file,&loaded)==WORLD_SETTINGS_OK);
    assert(world_settings_load(preset_file,&loaded)==WORLD_SETTINGS_OK);
    loaded.fx_enable=1; world_settings_activate_enhancements(&loaded);
    assert(!loaded.fx_dof && loaded.fx_trails);
    puts("PASS: first-enable upgrade and saved custom enhancement selection");
    return 0;
}
