/* Bounded presentation-only water volumes.
 * No reads/writes of gameplay actors. Native tile/sprite priorities always win. */
enum { CINEMA_EVENTS=24, VW=W/4, VH=H/4 };
typedef struct {float x,y,age,life,power; int seabed; unsigned tint; float light,hold;} CinemaEvent;
static CinemaEvent cinema_events[CINEMA_EVENTS];
static uint32_t volume_probes[VW*VH], water_volume[N];
static unsigned cinema_emitted,cinema_samples;
static int cinema_volume_dirty=1;
typedef struct {float hx,hy,hz,r,g,b,energy;} MaterialLight;
static MaterialLight material_light[N];
static void material_rows(int from,int to)
{
    for(int y=from;y<to;++y)for(int x=0;x<W;++x){
        PixelLight *p=&pixel_light[y*W+x];MaterialLight *l=&material_light[y*W+x];
        float energy=transport_valid && g.fx->lights ? p->r+p->g+p->b : 0;
        float blend=energy/(1+energy);
        l->hx=.24f*(1-blend)+(transport_valid?p->dx:0)*blend*.65f;
        l->hy=-.40f*(1-blend)+(transport_valid?p->dy:0)*blend*.65f;
        l->hz=.88f;float inv=1/sqrtf(l->hx*l->hx+l->hy*l->hy+l->hz*l->hz);
        l->hx*=inv;l->hy*=inv;l->hz*=inv;
        l->r=energy>0?p->r:0;l->g=energy>0?p->g:0;l->b=energy>0?p->b:0;l->energy=energy;
    }
}
static void material_prepare(void)
{
    if(g.fx->materials && !gpu_light) parallel(material_rows,H);
}
static uint32_t material_shade(uint32_t c,uint32_t surface,int x,int y)
{
    unsigned kind=surface&255;if(kind==0 || kind>=MAT_COUNT)return c;
    int wet=g.waterline<0 || y>=g.waterline;
    if (wet && g.fx->contact_shade) {
        int r=(c>>16)&255, gr=(c>>8)&255, b=c&255;
        int keep=256-3*((surface>>24)&15);
        r=r*keep>>8; gr=gr*keep>>8; b=b*keep>>8;
        c=light_rgb(r,gr,b);
    }
    if (!g.fx->materials) return c;
    const Material *m=&materials[kind];
    /* GPU transport applies position-dependent radiance after compositing.
       The unlit sprite pass only needs the fixed incident half-vector. */
    static const MaterialLight ambient={.24f/.99599197f,-.40f/.99599197f,.88f/.99599197f,0,0,0,0};
    const MaterialLight *l=gpu_light?&ambient:&material_light[y*W+x];
    float nx=(int8_t)(surface>>8)/96.0f*m->relief,ny=(int8_t)(surface>>16)/96.0f*m->relief;
    float nz=1-.45f*(nx*nx+ny*ny);
    float h=nx*l->hx+ny*l->hy+nz*l->hz;if(h<0)h=0;if(h>1)h=1;
    float h2=h*h,h4=h2*h2,h8=h4*h4,h16=h8*h8;
    float spec=m->rough>.6f?h4*.35f:m->rough>.2f?h8:h16*h16;
    float grain=((surface>>28)&15)/15.0f;
    /* Stable machining marks / corrosion, tied to ROM texels, never screen noise. */
    float finish=kind==MAT_RUST?.65f+grain*.3f:.86f+grain*.14f;
    float highlight=spec*m->spec*finish;
    float peak=fmaxf(l->r,fmaxf(l->g,l->b));
    float dynamic=peak>0?fminf(l->energy,2)*28/peak:0;
    float sunlight = wet && g.fx->sunshadow && transport_valid ? pixel_light[y*W+x].sun : 1.0f;
    int sky=sky_reaches(x,y);
    if(!sky)sunlight=0;
    float hr=highlight*((wet?13:18)*sunlight+l->r*dynamic);
    float hg=highlight*((wet?17:18)*sunlight+l->g*dynamic);
    float hb=highlight*(19*sunlight+l->b*dynamic);
    float relief=1+clampi((int)((nx*.25f-ny*.40f)*30),-12,16)/100.0f;
    int r=(c>>16)&255,gr=(c>>8)&255,b=c&255;
    if (sky && wet && g.fx->lights && g.fx->sunshadow) relief *= 0.74f + 0.26f * sunlight;
    float dark=kind==MAT_RUST?(grain-.5f)*8:0;
    r=(int)(r*relief+hr*((m->tint>>16)&255)/255.0f+(r+12)*l->r*.24f+dark);
    gr=(int)(gr*relief+hg*((m->tint>>8)&255)/255.0f+(gr+12)*l->g*.24f+dark);
    b=(int)(b*relief+hb*(m->tint&255)/255.0f+(b+12)*l->b*.24f+dark);
    return light_rgb(r,gr,b);
}

static void cinema_reset(void)
{
    memset(cinema_events,0,sizeof cinema_events);
    memset(water_volume,0,sizeof water_volume);cinema_volume_dirty=1;
}
static void cinema_scroll(int dx,int dy)
{
    for(int i=0;i<CINEMA_EVENTS;++i){cinema_events[i].x-=dx;cinema_events[i].y-=dy;}
}
static void cinema_blast(float x,float y,int size,const WorldSceneFx *fx)
{
    if (!(fx->pressure || fx->sediment_light || fx->water_volume || fx->aftermath || fx->blast_reflect) ||
        y<0 || y>=H || (g.waterline>=0 && y<g.waterline)) return;
    int slot=-1;
    for(int i=0;i<CINEMA_EVENTS;++i){
        CinemaEvent *p=&cinema_events[i];
        if(p->life>0 && (p->hold>0 || p->light>.04f) && fabsf(p->x-x)<26 && fabsf(p->y-y)<26){
            p->hold=.14f;
            p->life=fmaxf(p->life,p->age+.8f);
            return;
        }
        if(p->life<=0 && slot<0) slot=i;
    }
    if(slot<0)return;
    int seabed=0;
    for(int d=2;d<=22;d+=4)if(wf_rock(x,y+d)||wf_rock(x-8,y+d)||wf_rock(x+8,y+d)){seabed=1;break;}
    unsigned tint=0xff8e2b;
    float nearest=48*48;
    for(unsigned i=0;i<MAX_SPRITES;++i){
        const BlastRadiance *f=&blast_radiance[i];
        if(!f->weight)continue;
        float dx=(float)f->x/f->weight-x,dy=(float)f->y/f->weight-y;
        if(dx*dx+dy*dy>=nearest)continue;
        nearest=dx*dx+dy*dy;
        tint=((f->r/f->weight)<<16)|((f->g/f->weight)<<8)|(f->b/f->weight);
    }
    cinema_events[slot]=(CinemaEvent){x,y,0,3.8f, size>=110?1.0f:.65f,seabed,tint,0,.14f};
    ++cinema_emitted;
}

static void volume_rows(int from,int to)
{
    /* Reconstruct at native pixel centres once, shared by every output scale. */
    for(int y=from;y<to;++y){
        int py=clampi(y-2,0,(VH-1)*4), fy=py%4;
        const uint32_t *top=volume_probes+(py/4)*VW;
        const uint32_t *bottom=py/4<VH-1?top+VW:top;
        for(int x=0;x<W;++x){
            int px=clampi(x-2,0,(VW-1)*4), a=px/4, b=clampi(a+1,0,VW-1), fx=px%4;
            unsigned out=0;
            for(int shift=0;shift<=16;shift+=8){
                int u=((top[a]>>shift)&255)*(4-fx)+((top[b]>>shift)&255)*fx;
                int v=((bottom[a]>>shift)&255)*(4-fx)+((bottom[b]>>shift)&255)*fx;
                out|=((u*(4-fy)+v*fy+8)/16)<<shift;
            }
            water_volume[y*W+x]=out;
        }
    }
}

static void cinema_update(float dt)
{
    const WorldSceneFx *fx=g.fx;
    for(int i=0;i<CINEMA_EVENTS;++i){
        CinemaEvent *p=&cinema_events[i];
        if(p->life<=0)continue;
        p->age+=dt;
        float attack=fminf(dt,p->hold);
        p->hold-=attack;
        p->light+=(p->power-p->light)*(1-expf(-12*attack));
        p->light*=expf(-7*(dt-attack));
        if(p->age>=p->life || p->x<-80 || p->x>W+80 || p->y>H+40)p->life=0;
    }
    if(!fx->pressure && !fx->sediment_light && !fx->water_volume && !fx->aftermath && !fx->blast_reflect)
        memset(cinema_events,0,sizeof cinema_events);
    if(fx->pressure){
        if(!field_on){memset(field_x,0,sizeof field_x);memset(field_y,0,sizeof field_y);}
        for(int i=0;i<CINEMA_EVENTS;++i){
            CinemaEvent *p=&cinema_events[i];
            if(p->life<=0 || p->age>.65f)continue;
            float radius=5+p->age*120,band=5+p->age*5,amp=(1-p->age/.65f)*7*p->power;
            int x0=clampi((int)(p->x-radius-band),0,W-1),x1=clampi((int)(p->x+radius+band),0,W-1);
            int y0=clampi((int)(p->y-radius-band),g.waterline<0?0:clampi(g.waterline,0,H-1),H-1);
            int y1=clampi((int)(p->y+radius+band),0,H-1);
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
                float dx=x-p->x,dy=y-p->y,d2=dx*dx+dy*dy;
                if(d2<(radius-band)*(radius-band)||d2>(radius+band)*(radius+band))continue;
                float d=sqrtf(d2);if(d<1)continue;
                float a=amp*(1-fabsf(d-radius)/band);
                int at=y*W+x;field_x[at]=(int8_t)clampi(field_x[at]+(int)(dx/d*a),-16,16);
                field_y[at]=(int8_t)clampi(field_y[at]+(int)(dy/d*a),-16,16);
            }
            field_on=1;
        }
    }
    /* A quarter-resolution scattering field. Uses cached sunlight occlusion,
     * plus bounded explosion volumes; no additional path tracing. */
    if(gpu_light){
        WorldEffectBatch *batch=&gpu_light->effects;
        batch->volume_scroll=g.scroll_x[2];batch->volume_time=g.T;batch->volume_count=0;
        if(fx->water_volume)for(int i=0;i<CINEMA_EVENTS;++i){
            const CinemaEvent *p=cinema_events+i;
            if(p->life<=0 || p->light<.005f)continue;
            float *v=batch->volumes[batch->volume_count++];
            v[0]=p->x;v[1]=p->y;v[2]=p->light;v[3]=(float)p->tint;
        }
        return;
    }
    if(!fx->water_volume){memset(water_volume,0,sizeof water_volume);return;}
    if(!cinema_volume_dirty)return;
    int flashes[CINEMA_EVENTS],flash_count=0;
    for(int i=0;i<CINEMA_EVENTS;++i)if(cinema_events[i].life>0 && cinema_events[i].light>=.005f)flashes[flash_count++]=i;
    for(int yy=0;yy<VH;++yy)for(int xx=0;xx<VW;++xx){
        int x=xx*4+2,y=yy*4+2,at=yy*VW+xx;
        if(g.waterline>=0 && y<g.waterline){volume_probes[at]=0;continue;}
        float sun=transport_valid?pixel_light[y*W+x].sun:1;
        if(!transport_valid){ /* cheap occlusion even with tracing disabled */
            for(int j=1;j<=4;++j){int sx=clampi(x+j*5,0,W-1),sy=y-j*10;
                if(sy>=0 && solid[(sy/2)*PW+sx/2])sun*=.3f;}
        }
        int beam=S((x+g.scroll_x[2]-y/3)*9+g.T/3)+S(x*5-y*3+g.T/5);
        float shaft=sky_reaches(x,y)*(beam>210?(beam-210)/302.0f:0)*sun;
        int depth=g.waterline<0?y:y-g.waterline;
        float fade=1-clampi(depth,0,240)/300.0f;
        float r=shaft*3*fade,gr=shaft*11*fade,b=shaft*16*fade;
        if(transport_valid){PixelLight *l=&pixel_light[y*W+x];float gain=fx->glow?.65f:1; r+=l->r*9*gain;gr+=l->g*10*gain;b+=l->b*12*gain;}
        for(int i=0;i<flash_count;++i){
            CinemaEvent *p=&cinema_events[flashes[i]];
            float dx=x-p->x,dy=y-p->y,d=dx*dx+dy*dy;
            float f=p->light*900/(900+d);
            int wet=submerged_path(y,p->y,sqrtf(d));
            f*=18.f/255;
            r+=f*(p->tint>>16)*light_absorb[0][wet];
            gr+=f*((p->tint>>8)&255)*light_absorb[1][wet];
            b+=f*(p->tint&255)*light_absorb[2][wet];
        }
        volume_probes[at]=clamp255((int)r)<<16|clamp255((int)gr)<<8|clamp255((int)b);
    }
    parallel(volume_rows,H);
}

static uint32_t cinema_background(uint32_t c,int layer,int x,int y)
{
    if(layer<0 || layer>2 || (layer==0 && y>=HUD) || y>=H || (g.waterline>=0 && y<g.waterline))return c;
    int r=(c>>16)&255,gr=(c>>8)&255,b=c&255;
    if(g.fx->ocean_depth){int f=3 + water_amount(c)*(g.fx->haze?5:21)/255;r+=(5-r)*f/256;gr+=(28-gr)*f/256;b+=(52-b)*f/256;}
    if(g.fx->water_volume && !gpu_light){unsigned v=water_volume[y*W+x];r+=(v>>16)&255;gr+=(v>>8)&255;b+=v&255;}
    return light_rgb(r,gr,b);
}

static void cinema_draw(uint32_t *out)
{
    const WorldSceneFx *fx=g.fx;int budget=14000;cinema_samples=0;
    for(int i=0;i<CINEMA_EVENTS;++i){
        CinemaEvent *p=&cinema_events[i];if(p->life<=0)continue;
        float fade=1-p->age/p->life;
        if(fx->pressure && p->age<.7f){
            float radius=6+p->age*110;
            for(int j=0;j<14;++j){int a=j*1024/14+i*73;
                int x=(int)(p->x+S(a)*radius/256),y=(int)(p->y+S(a+256)*radius/256);
                wf_pixel(out,x,y,115,185,205,(int)((1-p->age/.7f)*80),1);++cinema_samples;}
        }
        if(fx->sediment_light && (p->seabed || fx->aftermath)){
            int radius=(int)(6+p->age*5),alpha=(int)(fade*(p->age<.2f?p->age*5:1)*(p->seabed?48:25));
            if(fx->silt && p->seabed)alpha=alpha*2/3;
            int cx=(int)(p->x+S(i*71+(int)(p->age*70))*p->age*.012f),cy=(int)(p->y-p->age*3);
            float hot=p->light;
            if(budget>0 && gpu_stamp(cx,cy,radius,(int)(p->age*140)+i*33,
                 stamp_colour(70+(int)(hot*120),96+(int)(hot*40),104,alpha),6,budget,
                 cx-radius,cy-radius/2,cx+radius,cy+radius/2)){
                budget-=(radius*2+1)*(2*(radius/2)+1);
            }else for(int dy=-radius/2;dy<=radius/2 && budget>0;++dy)
                for(int dx=-radius;dx<=radius && budget>0;++dx,--budget){
                    int d=dx*dx+dy*dy*4;if(d>=radius*radius)continue;
                    int curl=S(dx*27+dy*49+(int)(p->age*140)+i*33);
                    int a=alpha*(radius*radius-d)/(radius*radius)*(210+curl/6)/256;
                    int x=cx+dx,y=cy+dy;if(x<0||x>=W||y<0||y>=H)continue;
                    float hot=p->light;
                    PixelLight *l=&pixel_light[y*W+x];
                    int r=70+(int)(hot*120)+(transport_valid?(int)(l->r*45):0);
                    int gr=96+(int)(hot*40)+(transport_valid?(int)(l->g*35):0);
                    int b=104+(transport_valid?(int)(l->b*25):0);
                    wf_pixel(out,x,y,clamp255(r),clamp255(gr),clamp255(b),a,1);++cinema_samples;
                }
        }
        if(fx->aftermath && p->age>.22f){
            for(int j=0;j<5;++j){
                float age=p->age-j*.18f;if(age<0)continue;
                int x=(int)(p->x+(j-2)*3+S((int)(age*100)+j*200)*age*.015f);
                int y=(int)(p->y-age*(6+j*2));
                wf_pixel(out,x,y,135,194,204,(int)(fade*115),1);
                if(j==2)wf_pixel(out,x+1,y+1,55,112,125,(int)(fade*55),1);
                ++cinema_samples;
            }
            /* Cooling flecks fall slowly; no persistent underwater flames. */
            for(int j=0;j<3;++j){int x=(int)(p->x+(j-1)*p->age*4),y=(int)(p->y+p->age*3);
                if(!wf_rock(x,y))wf_pixel(out,x,y,108,123,120,(int)(fade*110),1);}
        }
    }

}
