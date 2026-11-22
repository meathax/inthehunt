#ifndef WORLD_AUDIO_IMMERSION_H
#define WORLD_AUDIO_IMMERSION_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "world_weapon_light.h"
#include "world_view.h"

/* Presentation-only synthesis at 48 kHz. No allocation, wall clock, or game
 * writes. Actor identities keep repeated dispatches from retriggering sounds. */
enum { IMM_VOICES=48, IMM_ACTORS=4096 };
typedef struct {
    float phase, frequency, gain, pan, wet, decay, noise, low;
    unsigned remaining, delay, ambient;
} ImmVoice;
typedef struct {
    unsigned actor, state, stamp, kind, boss;
    int x,y;
} ImmActor;
typedef struct {
    ImmVoice voices[IMM_VOICES];
    ImmActor actors[IMM_ACTORS];
    uint32_t random;
    unsigned frame, stage, active, ambient_clock;
    float prop_phase[2], prop_speed[2], prop_target[2], player_pan[2];
    int player_x[2], player_y[2], player_present[2];
    float listener, noise_low, pressure, motor_phase;
    float machinery_gain, machinery_pan, machinery_pitch, threat_gain;
} WorldImmersion;
static WorldImmersion immersion;

static float imm_noise(void)
{
    uint32_t n=immersion.random;
    n^=n<<13; n^=n>>17; n^=n<<5;
    immersion.random=n;
    return (float)(n&65535)/32768.f-1.f;
}

static void imm_reset(unsigned stage, unsigned active)
{
    memset(&immersion,0,sizeof immersion);
    immersion.random=0x5137ac29u;
    immersion.stage=stage;
    immersion.active=active;
}

/* Screen-space geometry for emitters follows the session view: the aperture
 * plus a 48 px margin admits sources just off-frame, and pan/distance terms
 * normalize against the aperture half-width (160 px arcade, 213 1/3 wide). */
static float imm_half(void)
{
    return world_view_width()==WORLD_VIEW_WIDE_WIDTH?640.f/3.f:160.f;
}
static int imm_lo(void){return world_view_left()-48;}
static int imm_hi(void){return world_view_left()+(int)world_view_width()+48;}

static ImmVoice *imm_emit(int x,int y,int water,float frequency,float gain,
                     float seconds,float noise,float delay)
{
    if(!immersion.active || x < imm_lo() || x > imm_hi() || y < -48 || y > 288) return NULL;
    unsigned slot=0;
    float quietest=1e9f;
    for(unsigned i=0;i<IMM_VOICES;++i){
        if(!immersion.voices[i].remaining){slot=i;break;}
        if(immersion.voices[i].gain<quietest){quietest=immersion.voices[i].gain;slot=i;}
    }
    float pan=fmaxf(-.8f,fminf(.8f,(x-160)*(.8f/imm_half())));
    float wet=water<0?1.f:fmaxf(0,fminf(1,(y-water+4)/24.f));
    float distance=(float)abs(x-160)/(2.f*imm_half());
    immersion.voices[slot]=(ImmVoice){0,frequency,gain*(1-.35f*distance),pan,wet,
        expf(-7.f/(seconds*48000)),noise,0,(unsigned)(seconds*48000),(unsigned)(delay*48000),0};
    return &immersion.voices[slot];
}

static void imm_frame(void)
{
    float gain=0,pan=0,pitch=0,threat=0;
    for(unsigned i=0;i<IMM_ACTORS;++i){
        ImmActor *a=&immersion.actors[i];
        if(immersion.frame-a->stamp>2 || a->x<world_view_left() ||
           a->x>=world_view_left()+(int)world_view_width() || a->y<0 || a->y>=240)continue;
        float weight=a->boss?1.f:a->kind==TORPEDO_LENS||a->kind==HEAVY_LENS?.3f:
            a->kind==ROCKET_PLUME||a->kind==VERTICAL_EXHAUST?.4f:0;
        gain+=weight;
        pan+=weight*(a->x-160)*(.7f/imm_half());
        pitch+=weight*(a->boss==BOSS_ICE?240:95);
        if(a->kind==CHARGE_FUSE || a->kind==SHELL_FUSE)threat+=.12f;
    }
    immersion.machinery_gain=fminf(120,gain*32);
    immersion.machinery_pan=gain?pan/gain:0;
    immersion.machinery_pitch=gain?pitch/gain:95;
    immersion.threat_gain=fminf(45,threat*30);
    ++immersion.frame;
}

static void imm_actor(unsigned actor,unsigned entry,unsigned state,int x,int y,
                      int water,unsigned boss)
{
    ImmActor *a=&immersion.actors[(actor>>4)&(IMM_ACTORS-1)];
    int known=a->actor==actor && immersion.frame-a->stamp<4;
    unsigned launch=weapon_launch(entry,state),kind=weapon_flight(state);
    if(!kind)kind=weapon_flight(entry);
    int changed=!known || a->state!=state;
    if(launch && changed){
        float pitch=launch==HEAVY_TUBE?62:launch==RACK_PULSE?145:210;
        /* Both player tubes retain a subtly different mechanical resonance. */
        for(unsigned p=0;p<2;++p)
            if(immersion.player_present[p] && abs(x-immersion.player_x[p])<28 &&
               abs(y-immersion.player_y[p])<24) pitch*=p?1.12f:1.f;
        imm_emit(x,y,water,pitch,420,.14f,.55f,0);
        if(launch==RACK_PULSE || launch==MORTAR_FLASH)
            imm_emit(x,y,water,1050,95,.45f,.18f,.025f);
    }
    if(known && kind && a->kind && water>=0 && ((y>=water)!=(a->y>=water))){
        imm_emit(x,water,water,350,650,.24f,.88f,0);
        imm_emit(x,water+8,water,110,320,.55f,.72f,.07f);
    }
    if(changed && (entry==0xa1bf || entry==0x1d2ea || entry==0xad81)){
        float pitch=immersion.stage==4?740:immersion.stage==2?170:280;
        float distance=fabsf((float)x-160)/(2.f*imm_half());
        imm_emit(x,y,water,52,1250,.65f,.65f,0);
        imm_emit(x,y,water,pitch,460,.85f,.35f,.08f+distance*.08f);
        imm_emit(x,y,water,pitch*.57f,240,1.1f,.7f,.26f+distance*.12f);
        immersion.pressure=fminf(1,immersion.pressure+.35f);
    }
    if(changed && kind==ICE_SHARD)
        imm_emit(x,y,water,1200,180,.35f,.75f,0);
    if(boss && changed && known && (abs(x-a->x)+abs(y-a->y)>0))
        imm_emit(x,y,water,boss==BOSS_ICE||boss==BOSS_VIOLET?820:130,190,.18f,.45f,0);
    if(known && a->boss && !boss){
        imm_emit(a->x,a->y,water,85,900,.8f,.6f,0);
        imm_emit(a->x,a->y,water,390,280,1.2f,.25f,.2f);
    }
    *a=(ImmActor){actor,state,immersion.frame,kind,boss,x,y};
}

static void imm_player(unsigned p,int present,int x,int y,int water)
{
    if(present && immersion.player_present[p]){
        int dx=x-immersion.player_x[p],dy=y-immersion.player_y[p];
        float speed=fminf(1,(abs(dx)+abs(dy))*.18f);
        immersion.prop_target[p]=speed;
        if(water>=0 && ((y>=water)!=(immersion.player_y[p]>=water)))
            imm_emit(x,water,water,160,500,.4f,.9f,0);
        if(dx && (dx>0)!=(immersion.player_pan[p]>0))
            imm_emit(x,y,water,78,150,.22f,.85f,0);
        if(dx)immersion.player_pan[p]=dx>0?1:-1;
    }
    immersion.player_present[p]=present;
    immersion.player_x[p]=x; immersion.player_y[p]=y;
    if(!present)immersion.prop_target[p]=0;
}

static void imm_sample(float out[2],float ambience,float effects,int reduced_bass)
{
    out[0]=out[1]=0;
    float noise=imm_noise();
    immersion.noise_low+=.018f*(noise-immersion.noise_low);
    if(immersion.active){
        float floor=immersion.noise_low*(24+18*immersion.listener);
        out[0]=out[1]=floor*ambience;
        immersion.motor_phase+=immersion.machinery_pitch/48000;
        if(immersion.motor_phase>=1)immersion.motor_phase-=1;
        float mechanism=(sinf(6.2831853f*immersion.motor_phase)+.2f*noise)*immersion.machinery_gain*ambience;
        out[0]+=mechanism*(1-immersion.machinery_pan)+noise*immersion.threat_gain*effects;
        out[1]+=mechanism*(1+immersion.machinery_pan)+noise*immersion.threat_gain*effects;
        for(unsigned p=0;p<2;++p){
            immersion.prop_speed[p]+=.00012f*(immersion.prop_target[p]-immersion.prop_speed[p]);
            immersion.prop_phase[p]+=(38+immersion.prop_speed[p]*34+p*3)/48000.f;
            if(immersion.prop_phase[p]>=1)immersion.prop_phase[p]-=1;
            float wave=sinf(6.2831853f*immersion.prop_phase[p]);
            float motor=immersion.player_present[p]?(wave+noise*.15f)*(28+immersion.prop_speed[p]*65):0;
            motor*=ambience*(1-.65f*immersion.pressure)*(reduced_bass?.4f:1.f);
            float pan=fmaxf(-.7f,fminf(.7f,(immersion.player_x[p]-160)*(.7f/imm_half())));
            out[0]+=motor*(1-pan);out[1]+=motor*(1+pan);
        }
        /* Sparse authored stage textures, driven by the sample clock. */
        if(++immersion.ambient_clock>=48000u*(7+immersion.stage)){
            immersion.ambient_clock=0;
            unsigned s=immersion.stage;
            static const float texture[6]={210,320,115,165,950,72};
            ImmVoice *v=imm_emit(world_view_left()+40+(int)(immersion.random%(world_view_width()-80)),180,-1,
                texture[s%6],95,1.4f,s==4?.8f:.3f,0);
            if(v)v->ambient=1;
        }
    }
    for(unsigned i=0;i<IMM_VOICES;++i){
        ImmVoice *v=&immersion.voices[i];
        if(!v->remaining)continue;
        if(v->delay){--v->delay;continue;}
        --v->remaining;
        v->phase+=v->frequency/48000;
        if(v->phase>=1)v->phase-=1;
        float raw=sinf(v->phase*6.2831853f)*(1-v->noise)+noise*v->noise;
        v->low+=.12f*(raw-v->low);
        float signal=(raw*(1-.65f*v->wet)+v->low*.65f*v->wet)*v->gain*(v->ambient?ambience:effects);
        if(reduced_bass && v->frequency<120)signal*=.4f;
        out[0]+=signal*(1-v->pan);out[1]+=signal*(1+v->pan);
        v->gain*=v->decay;
    }
    immersion.pressure*=.99994f;
}
#endif
