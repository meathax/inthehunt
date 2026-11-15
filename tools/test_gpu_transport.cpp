#include "../src/world_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static WorldLightScene scene={};
static uint32_t frame[320*240], clear_view[320*240], wall_view[320*240];
int main() {
    // At 4x, a quarter-source-texel pan must advance an edge one output
    // pixel per refresh. The two planes move independently of a fixed sprite.
    static uint32_t scaled[1280*960];
    char camera_error[512]={};
    for(int step=-2;step<=2;++step){
        for(int y=0;y<240;++y)for(int x=0;x<320;++x){
            unsigned tag=y<80?251:y<160?252:255;
            frame[y*320+x]=(tag<<24)|(x>=100?0xffffff:0);
        }
        float offsets[4]={step*.25f,0,-step*.25f,0};
        if(!world_gpu_camera_offscreen(offsets,frame,scaled,4,camera_error,sizeof camera_error)){
            puts(camera_error);return 1;
        }
        for(int band=0;band<3;++band){
            const uint32_t *row=scaled+1280*(band*320+80);
            int edge=0;while(edge<1280 && !(row[edge]&0xffffff))++edge;
            if(edge!=400+(band==0?-step:band==1?step:0))return 1;
        }
    }
    puts("PASS: quarter-texel camera steps advance exactly one display pixel in both directions; fixed sprites stay still.");
    scene.scale=1;scene.waterline=-1;
    unsigned center=120*320+120, tile=(120/8)*40+120/8;
    std::fill_n(frame,320*240,0xf7505050u);
    scene.effects.heads[tile]=1;scene.effects.count=2;
    scene.effects.nodes[1]={120,120,0,0,0x8050d050,1,0,2};
    scene.effects.nodes[2]={120,120,1,0,0xff100020,3,0,0};
    char check_error[512]={};
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error)){puts(check_error);return 1;}
    if(clear_view[center]!=0xff609070u || clear_view[center+1]!=0xff505050u)return 1;
    frame[center]=0xff36a2c5;
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error) ||
       clear_view[center]!=frame[center])return 1;
    scene.effects.heads[tile]=0;scene.effects.count=0;
    scene.flags=32; scene.effects.volume_count=1;
    scene.effects.volumes[0][0]=120; scene.effects.volumes[0][1]=120; scene.effects.volumes[0][2]=1;
    std::fill_n(frame,320*240,0xee505050u);
    frame[center+1]=0xfe505050u;
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error) ||
       ((clear_view[center]>>16)&255)<=80 || clear_view[center+1]!=0xff505050u)return 1;
    scene.flags=64; scene.effects.reflect_row=120; scene.effects.reflect_rows=1;
    scene.effects.mirror[0][120]=(128u<<16)|1;
    frame[center]=0xfe202020; frame[center-320]=0x3ee0e0e0;
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error) ||
       (clear_view[center]&0xffffff)!=0x808080)return 1;
    puts("PASS: ordered particles, volumetric water mask, reflection and HUD protection.");
    // Compare water tint to the original integer compositor, including the
    // near-surface rows retained on CPU and opaque/above-water pixels.
    for(unsigned haze : {0u,8192u}){
        scene.flags=4096u|haze;
        std::fill_n(scene.effects.ocean_rows,8,0xaaaaaaaau);
        for(unsigned i=0;i<320*240;i++)
            frame[i]=((i%3==0?0xffu:i%3==1?0xfeu:0xeeu)<<24)|((i*2654435761u)&0xffffffu);
        if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error))return 1;
        for(unsigned i=0;i<320*240;i++){
            int r=(frame[i]>>16)&255,g=(frame[i]>>8)&255,b=frame[i]&255;
            if((i/320)%2 && i%3==2){
                int lum=(3*r+5*g+2*b)/10;
                int w=std::min(255,std::clamp((std::max(g,b)-r)*3,0,255)*(255-lum)/200);
                int f=3+w*(haze?5:21)/255;
                r+=(5-r)*f/256;g+=(28-g)*f/256;b+=(52-b)*f/256;
            }
            if(clear_view[i]!=(0xff000000u|(r<<16)|(g<<8)|b))return 1;
        }
    }
    puts("PASS: GPU ocean tint matches native integer output, HUD and reserved rows.");
    scene.flags=1;scene.count=1;
    float *light=scene.emitters[0]; light[0]=65;light[1]=60;light[2]=55;
    light[4]=3;light[5]=1;light[6]=.2f;
    std::fill_n(frame,320*240,0xfe505050u);
    frame[120*320+125]=0xff36a2c5;
    char error[512]={};
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,error,sizeof error)) {puts(error);return 1;}
    for(int y=0;y<120;y++)scene.cells[y*160+70]=0xff101010;
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error)) {puts(error);return 1;}
    int receiver=120*320+150;
    printf("Lit %08x, occluded %08x, HUD %08x\n",clear_view[receiver],wall_view[receiver],clear_view[120*320+125]);
    if (((clear_view[receiver]>>16)&255)<=((wall_view[receiver]>>16)&255)+6 ||
        clear_view[120*320+125]!=0xff36a2c5 || wall_view[120*320+125]!=0xff36a2c5) return 1;
    for(unsigned budget : {1024u, 1024u|2048u}){
        scene.flags=1|budget;
        if(!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error) ||
           ((wall_view[receiver]>>16)&255)+6>=((clear_view[receiver]>>16)&255) ||
           wall_view[120*320+125]!=0xff36a2c5)return 1;
    }
    scene.flags=1;
    // With point emitters off, a coloured wall can tint the receiver only
    // through the indirect pass sampling skylight on the first hit.
    scene.count=0;
    for (int y=0;y<120;++y) scene.cells[y*160+70]=0xffff0000;
    if (!world_gpu_light_offscreen(&scene,frame,clear_view,error,sizeof error)) return 1;
    for (int y=0;y<120;++y) scene.cells[y*160+70]=0xff0000ff;
    if (!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error)) return 1;
    printf("Red bounce %08x, blue bounce %08x\n",clear_view[receiver],wall_view[receiver]);
    return !(((clear_view[receiver]>>16)&255)>((wall_view[receiver]>>16)&255) &&
             (wall_view[receiver]&255)>(clear_view[receiver]&255));
}
