#ifndef WORLD_WEAPON_LIGHT_H
#define WORLD_WEAPON_LIGHT_H

/* World ROM actor entry points. Launches and sustained emission are separate:
   a rack or tube produces a brief disturbance, a burning motor stays bright. */
typedef enum {
    WEAPON_DARK, TUBE_PULSE, HEAVY_TUBE, RACK_PULSE, GUN_FLASH, MORTAR_FLASH,
    ROCKET_IGNITION, ARC_FLASH, ROCKET_PLUME, BURNING_FUEL, ARC_GLOW
} WeaponEmission;

static inline WeaponEmission weapon_launch(unsigned entry, unsigned next)
{
    switch (entry) {
    case 0x11ea7: case 0x9be7: case 0xb97ee: case 0x1362f: case 0x7be50: case 0x7b732:
        return TUBE_PULSE;
    case 0xa804c: case 0xabb7: case 0xaf63: return HEAVY_TUBE;
    case 0x13863: case 0xbe57b: case 0xbfbf9: case 0xa06b: case 0xb40e4: case 0xbe0fb: case 0xb75b6: case 0xb30c6:
        return RACK_PULSE;
    case 0x9fa1: case 0xb98e4: case 0xbe393: case 0xb7d77: case 0xb7e2f:
    case 0xb31c8: case 0xb6c6c: case 0xb2349: case 0xbd175:
    case 0xb6a4d: case 0xb696e: case 0x7e21c: case 0xba2ec:
    case 0x13935: case 0xb2f81: case 0x76a77: case 0xbc97b: return GUN_FLASH;
    case 0xb6272: case 0xb665e: case 0xb21ea: case 0xbf70a: case 0xbf0ea:
    case 0x1d0be: case 0x7c018: return MORTAR_FLASH;
    case 0x9e73: case 0xb4ce: case 0xb6df: case 0xbb40: case 0xbc73:
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
    case 0x9ec4: case 0xbb7e: case 0xb553: case 0xb57b: case 0xb5eb:
    case 0xb77c: case 0xbcf3: case 0xbd9a: case 0xb1867: case 0xb3d2a: case 0xb3e0d:
    case 0x7b8bc: return ROCKET_PLUME;
    case 0x1d2ea: case 0xad81: return BURNING_FUEL;
    case 0xbbda2: case 0xbbdb2: case 0xbbe4b: return ARC_GLOW;
    default: return WEAPON_DARK;
    }
}

enum { MAX_WEAPON_EMITTERS=128 };
typedef struct {
    short x,y;
    signed char dx,dy;
    unsigned char kind,age;
} WeaponEmitter;

#endif
