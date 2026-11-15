#ifndef WORLD_WEAPON_LIGHT_H
#define WORLD_WEAPON_LIGHT_H

/* World ROM actor entry points. Launches and sustained emission are separate:
   a rack or tube produces a brief disturbance, a burning motor stays bright. */
typedef enum {
    WEAPON_DARK, TUBE_PULSE, HEAVY_TUBE, RACK_PULSE, GUN_FLASH, MORTAR_FLASH,
    ROCKET_IGNITION, ARC_FLASH, VERTICAL_PULSE, ROCKET_PLUME, VERTICAL_EXHAUST, BURNING_FUEL, ARC_GLOW,
    TORPEDO_LENS, HEAVY_LENS, CHARGE_FUSE, TRACER, SHELL_FUSE, HOT_FRAGMENT, BEAM_CORE, ICE_SHARD, BOSS_AMBER, BOSS_RED, BOSS_CYAN, BOSS_VIOLET, BOSS_TEAL, BOSS_ICE
} WeaponEmission;

static inline WeaponEmission weapon_launch(unsigned entry, unsigned next)
{
    switch (entry) {
    case 0x11ea7: case 0x9be7: case 0xb97ee: case 0x1362f: case 0x7be50: case 0x7b732:
        return TUBE_PULSE;
    case 0xa804c: case 0xabb7: case 0xaf63: return HEAVY_TUBE;
    case 0x13863: case 0xbe57b: case 0xbfbf9: case 0xa06b: case 0xb40e4: case 0xbe0fb: case 0xb75b6: case 0xb30c6:
        return RACK_PULSE;
    case 0x9fa1: case 0x9e73: return VERTICAL_PULSE;
    case 0xb98e4: case 0xbe393: case 0xb7d77: case 0xb7e2f:
    case 0xb31c8: case 0xb6c6c: case 0xb2349: case 0xbd175:
    case 0xb6a4d: case 0xb696e: case 0x7e21c: case 0xba2ec:
    case 0x13935: case 0xb2f81: case 0x76a77: case 0xbc97b: return GUN_FLASH;
    case 0xb6272: case 0xb665e: case 0xb21ea: case 0xbf70a: case 0xbf0ea:
    case 0x1d0be: case 0x7c018: return MORTAR_FLASH;
    case 0xb4ce: case 0xb6df: case 0xbb40: case 0xbc73:
        return ROCKET_IGNITION;
    /* Motor lights on release, after the launcher holds the round. */
    case 0xb3c64: return next==0xb3d2a ? ROCKET_IGNITION : WEAPON_DARK;
    case 0xb17c7: return next==0xb1867 ? ROCKET_IGNITION : WEAPON_DARK;
    case 0x7862b: case 0xb0c8d: return ARC_FLASH;
    default: return WEAPON_DARK;
    }
}

static inline WeaponEmission weapon_flight(unsigned entry)
{
    switch (entry) {
    case 0x9eb2: case 0x9ec4: case 0x9fcd: case 0x9fda: return VERTICAL_EXHAUST;
    case 0xbb7e: case 0xb553: case 0xb57b: case 0xb5eb:
    case 0xb77c: case 0xbcf3: case 0xbd9a: case 0xb1867: case 0xb3d2a: case 0xb3e0d:
    case 0x7b8bc: return ROCKET_PLUME;
    case 0x1d2ea: case 0xad81: return BURNING_FUEL;
    case 0xbbda2: case 0xbbdb2: case 0xbbe4b: return ARC_GLOW;
    case 0x11edb: case 0x11f70: case 0x11fbf: case 0x12040:
    case 0xb9825: case 0x9c33: case 0x9c82: case 0x13656: case 0x13664: case 0x13716:
    case 0x7beaa: case 0x7bee0: case 0x7b7a8: case 0x7b7f8: return TORPEDO_LENS;
    case 0xa80b5: case 0xa80ea: case 0xac0e: case 0xac65: case 0xafd3: return HEAVY_LENS;
    case 0xbf74a: case 0xbf784: case 0xb41b6: case 0xbe120:
    case 0xbfc37: case 0xbfcda: case 0xbfcf4: case 0x1d1d2: case 0x1d1fe: case 0xb22e7: case 0xb3033:
    case 0xb6b7c: case 0xb6bd8: case 0xbe604: case 0x7e30e:
    case 0xa0a9: case 0x1388f: case 0x1389d: case 0xbe5dd: case 0xb75e9: return CHARGE_FUSE;
    case 0xb6a95: case 0xb6ad6: case 0xb6b25: case 0x7e24c: case 0x7e260:
    case 0xbe3b3: case 0xb2fac: case 0xba30c: case 0xb6998: case 0xb6ca8: case 0xb6cf7:
    case 0xbd1cd: case 0xbd1db: case 0xb237f: case 0xb2412: case 0xb2476:
    case 0xb990c: case 0xb9926: case 0xb31f2: case 0xb3269:
    case 0x139d9: case 0xb7d9c: case 0xb7e4f:
    case 0xbc993: case 0xbc9e5: case 0x76aaa: case 0x76b07: return TRACER;
    case 0xbf10a: case 0xb62dc: case 0xb63b3:
    case 0xb2224: case 0xb2238: case 0xb66bb: case 0xb62cc:
    case 0x7c05b: case 0x1d0eb: return SHELL_FUSE;
    case 0x1f4a9: case 0x1f4e4: case 0x1f5c8:
    case 0x7c0ef: case 0x7c13f: case 0x7c14f: case 0x7c195: case 0x7c1ac:
    case 0xb0c4: case 0xb1da: case 0xb234: case 0xb2ab: return HOT_FRAGMENT;
    case 0x79dd9: case 0x79e1a: return ICE_SHARD;
    case 0x132a3: case 0x132ad: case 0x13308: case 0x13315: case 0x78680:
    case 0xb0caf: case 0xb0d24: return BEAM_CORE;
    default: return WEAPON_DARK;
    }
}

static inline WeaponEmission round_from_launch(WeaponEmission pulse)
{
    switch(pulse){
    case TUBE_PULSE: return TORPEDO_LENS;
    case HEAVY_TUBE: return HEAVY_LENS;
    case RACK_PULSE: return CHARGE_FUSE;
    case GUN_FLASH: return TRACER;
    case MORTAR_FLASH: return SHELL_FUSE;
    case ROCKET_IGNITION: return ROCKET_PLUME;
    case VERTICAL_PULSE: return VERTICAL_EXHAUST;
    case ARC_FLASH: return ARC_GLOW;
    default: return WEAPON_DARK;
    }
}

enum { MAX_WEAPON_EMITTERS=128 };
typedef struct {
    short x,y;
    signed char dx,dy;
    unsigned char kind,age,level,repeat;
} WeaponEmitter;

/* One lighting envelope per nearby, similarly directed volley. Advance only
   on successful board passes, so duplicate presentations cannot pump it. */
static inline void weapon_pulse(WeaponEmitter *list,unsigned *count,WeaponEmitter shot)
{
    unsigned best=*count;
    int distance=19*19;
    for(unsigned i=0;i<*count;++i){
        WeaponEmitter *e=list+i;
        if(e->kind!=shot.kind || e->dx*shot.dx+e->dy*shot.dy<=0)continue;
        int x=e->x-shot.x,y=e->y-shot.y,d=x*x+y*y;
        if(d<distance){best=i;distance=d;}
    }
    if(best==*count){
        if(*count==MAX_WEAPON_EMITTERS)return;
        list[(*count)++]=shot;
    } else {
        WeaponEmitter *e=list+best;
        /* Keep simultaneous spread rounds attached to the first barrel. */
        if(e->age){e->repeat=1;e->x=shot.x;e->y=shot.y;e->dx=shot.dx;e->dy=shot.dy;}
        e->age=0;
    }
}

static inline void weapon_envelopes(WeaponEmitter *list,unsigned count)
{
    for(unsigned i=0;i<count;++i){
        WeaponEmitter *e=list+i;
        if(e->kind>=ROCKET_PLUME)continue;
        if(e->age<=(e->repeat?14:5))e->level+=(96-e->level+5)/6;
        else e->level-=(e->level+7)/8;
    }
}

#endif
