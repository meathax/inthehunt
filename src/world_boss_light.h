#ifndef WORLD_BOSS_LIGHT_H
#define WORLD_BOSS_LIGHT_H
#include "world_weapon_light.h"

typedef struct { WeaponEmission kind; int x,y; } BossLamp;

/* Only powered states of the actual part. A transition to a wreck, a
   detached limb or a freed slot removes its lamp on that board pass. */
static inline BossLamp boss_lamp(unsigned handler)
{
    switch(handler){
    /* First boss: brass claw pivot and the amber port of its launcher bay. */
    case 0x11742: case 0x117da: case 0x11819: case 0x118a3: case 0x118bd:
        return (BossLamp){BOSS_RED,0,0};
    case 0x11c78: case 0x11c9e: case 0x11d41: case 0x11d8a:
        return (BossLamp){BOSS_AMBER,0,-2};
    /* Twin hulls: bridge windows and the blue emitter below each hull. */
    case 0x12af6: case 0x12c3e: case 0x12c52:
        return (BossLamp){BOSS_AMBER,2,-5};
    case 0x12fd1: case 0x12ff6: case 0x1305a: case 0x130aa: case 0x130f7:
        return (BossLamp){BOSS_CYAN,0,1};
    /* Collapsing fortress: the vulnerable central core. */
    case 0x1ee85: case 0x1ee97: return (BossLamp){BOSS_RED,0,0};
    /* Crown and individual conducting hooks of the articulated end boss. */
    case 0x14b24: case 0x14b4e: return (BossLamp){BOSS_VIOLET,0,-3};
    case 0x15329: return (BossLamp){BOSS_CYAN,0,0};
    /* Deep-water creature: green facial core and warmer claw tips. */
    case 0x75f55: case 0x75f79: case 0x75fa0: return (BossLamp){BOSS_TEAL,0,0};
    case 0x764f3: case 0x76550: case 0x7668d: case 0x766cf:
    case 0x766ee: case 0x76736: case 0x767a3: return (BossLamp){BOSS_AMBER,0,0};
    /* Ice boss: a cold central core and distinct blue-violet arm joints. */
    case 0x78373: case 0x7839e: case 0x78442: case 0x784a3:
    case 0x784fa: case 0x78534: case 0x7855e: return (BossLamp){BOSS_ICE,0,-2};
    case 0x78a92: case 0x78ad5: case 0x78ae4: case 0x78bac: case 0x78c0c: case 0x78c20:
        return (BossLamp){BOSS_VIOLET,0,0};
    /* Battleship: red bow running light, amber turret lamps, hot reactor. */
    case 0x7af56: return (BossLamp){BOSS_RED,0,-2};
    case 0x7b3b7: case 0x7b425: case 0x7ba24: return (BossLamp){BOSS_AMBER,0,-3};
    case 0x7b1ff: case 0x7b2a4: return (BossLamp){BOSS_CYAN,0,0};
    default: return (BossLamp){0};
    }
}
#endif
