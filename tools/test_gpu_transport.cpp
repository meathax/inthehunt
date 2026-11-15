#include "../src/world_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static WorldLightScene scene={};
static uint32_t frame[320*240], clear_view[320*240], wall_view[320*240];
int main() {
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
