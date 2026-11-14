#include "../src/world_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static WorldLightScene scene={};
static uint32_t frame[320*240], clear_view[320*240], wall_view[320*240];
int main() {
    scene.scale=1;scene.waterline=-1;scene.flags=1;scene.count=1;
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
