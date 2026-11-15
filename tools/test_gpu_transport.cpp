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
    // Assembled silhouette with a warm lamp to the left and sun above.
    scene.flags=16384;scene.count=1;
    float *lamp=scene.emitters[0];
    lamp[0]=53;lamp[1]=62;lamp[2]=25;lamp[4]=3;lamp[5]=.4f;lamp[6]=.1f;
    std::fill_n(frame,320*240,0xfe404040u);
    for(int y=116;y<136;++y)for(int x=120;x<152;++x)frame[y*320+x]=0xae404040u;
    frame[0]=0xff123456;
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,check_error,sizeof check_error))return 1;
    int left=124*320+120,top=116*320+136,mid=124*320+136,bottom=135*320+136;
    // A readable outer crest rolls off through one inner hull pixel.
    int inner=top+320;
    if((clear_view[top]&255)<(clear_view[mid]&255)+35 ||
       (clear_view[inner]&255)<(clear_view[mid]&255)+12 ||
       (clear_view[top]&255)<=(clear_view[inner]&255))return 1;
    for(int x=0;x<160;++x)scene.cells[52*160+x]=0xff303030;
    for(int y=0;y<120;++y)scene.cells[y*160+57]=0xff303030;
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,check_error,sizeof check_error))return 1;
    if(((clear_view[left]>>16)&255)<=((wall_view[left]>>16)&255)+5 ||
       (clear_view[top]&255)<=(wall_view[top]&255)+5 ||
       clear_view[mid]!=0xff404040 || clear_view[bottom]!=clear_view[mid] ||
       clear_view[0]!=0xff123456 || wall_view[0]!=clear_view[0])return 1;
    std::fill_n(scene.cells,WORLD_PROBES,0u);
    for(int y=116;y<136;++y)for(int x=120;x<152;++x)frame[y*320+x]=0x6e404040u;
    // Foreground wreckage receives a single edge pixel, with no inner band.
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,check_error,sizeof check_error) ||
       (wall_view[top]&255)<(wall_view[mid]&255)+15 ||
       wall_view[inner]!=wall_view[mid] || wall_view[0]!=frame[0])return 1;
    puts("PASS: strong directional hull and scenery rims, shadow occlusion, inner rolloff and HUD.");
    std::fill_n(scene.cells,WORLD_PROBES,0u);
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
    // Two one-pixel casters above the waterline: follow both shadows across
    // the interface to the very last row, with clear water beside each edge.
    scene.flags=2;scene.count=0;scene.waterline=70;
    std::fill_n(scene.cells,WORLD_PROBES,0u);
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,error,sizeof error))return 1;
    scene.cells[10*160+65]=0x01000000;
    scene.cells[10*160+95]=0x10000000;
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error))return 1;
    for(int y=21;y<240;++y)for(int x:{130,190}){
        int at=y*320+x+(11*20+25)/50-(11*y+25)/50;
        if((wall_view[at]&255)+3>=(clear_view[at]&255) ||
           wall_view[at+1]!=clear_view[at+1])return 1;
    }
    puts("PASS: terrain and sprite sun shadows cross the waterline and reach row 239.");
    scene.waterline=-1;scene.count=1;
    // A right-facing bow lamp illuminates only its cone. A single native
    // pixel at x=140,y=120 blocks the centre ray but leaves its neighbour lit.
    std::fill_n(scene.cells,WORLD_PROBES,0u);
    scene.flags=32768;light[3]=1;light[1]=60.25f;
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,error,sizeof error))return 1;
    scene.cells[60*160+70]=0x10000000;
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error))return 1;
    if((clear_view[receiver]&0xffffff)==(wall_view[receiver]&0xffffff) ||
       (wall_view[receiver+320*4]&0xffffff)!=(clear_view[receiver+320*4]&0xffffff) ||
       (clear_view[120*320+105]&0xffffff)!=0x505050)return 1;
    light[3]=0;
    scene.flags=1;
    // Two embedded lights of different colours project the same ship onto
    // a terrain receiver in opposite directions. The unblocked lamp fills
    // each shadow independently; moving the occluder away restores both.
    scene.count=2;
    std::fill_n(scene.cells,WORLD_PROBES,0u);
    auto occupy=[](int x,int y,unsigned material){
        scene.cells[(y/2)*160+x/2]|=material<<(24+(x&1)+2*(y&1));
    };
    for(int i=0;i<2;++i){
        float *e=scene.emitters[i];
        e[0]=(120.5f+80*i)*.5f;e[1]=40.25f;e[2]=85;e[3]=0;
        e[4]=i?0:6;e[5]=0;e[6]=i?6:0;
        for(int y=77;y<=84;++y)for(int x=117;x<=124;++x)occupy(x+i*80,y,1);
    }
    for(int y=136;y<=145;++y)for(int x=125;x<=190;++x)occupy(x,y,1);
    if(!world_gpu_light_offscreen(&scene,frame,clear_view,error,sizeof error))return 1;
    for(int y=117;y<=123;++y)for(int x=157;x<=163;++x)occupy(x,y,16);
    if(!world_gpu_light_offscreen(&scene,frame,wall_view,error,sizeof error))return 1;
    for(int i=0;i<2;++i){
        unsigned at=136*320+(i?144:176),shift=i?0:16;
        int loss=((clear_view[at]>>shift)&255)-((wall_view[at]>>shift)&255);
        int fill=((wall_view[at]>>(16-shift))&255);
        if(loss<12 || fill<95)return 1;
    }
    puts("PASS: opposing embedded sources cast independent coloured shadows onto terrain.");
    std::fill_n(scene.cells,WORLD_PROBES,0u);
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
