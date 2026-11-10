/* Recovered World actor handlers: paths missed so far: surface weapon variants, stale-register cases, rarely reached states.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include "world_stale.h"
#include <string.h>

/* See world_stale.h. */
static struct { int known, loaded, fade; uint16_t dx, fade_dx; } stale;

void world_stale_dx_set(uint16_t value) { stale.dx = value; stale.known = stale.loaded = 1; }
void world_stale_dx_forget(void) { stale.known = 0; stale.loaded = 1; }
int world_stale_dx(uint16_t *value) { *value = stale.dx; return stale.known; }
void world_stale_dx_fade(uint16_t value) { stale.fade_dx = value; stale.fade = 1; }

void world_stale_dx_boundary(int pieces, uint16_t dx, unsigned master)
{
    if (pieces) world_stale_dx_set(dx);
    if (stale.fade) world_stale_dx_set(stale.fade_dx);
    stale.fade = 0;
    /* Only the tally controllers are known to keep DX (25B5 loads it when it
     * starts the fade-out; world_title_master.c records that). */
    if (master != 0x25b5 && master != 0x25ff) world_stale_dx_forget();
}

void world_stale_dx_call(uint32_t handler, int done)
{
    if (!done) { stale.loaded = 0; return; }
    if (stale.loaded) return;
    switch (handler) {
    /* Tally handlers that never load DX (0DA4 and 1067 keep it). */
    case 0x1a39: case 0x2680: case 0x2b18: case 0x2b4c: case 0x2b68:
    case 0x2b96: case 0x2bab: case 0x2bfe: case 0x2abb:
        break;
    default:
        stale.known = 0;
    }
}

#define W(a) read_word(ram, (uint16_t)(a))
#define SW(a, v) write_word(ram, (uint16_t)(a), (uint16_t)(v))
#define RW(a) read_word(main_rom, (uint16_t)(a))

/* The actor one slot before this one in the per-pass slot table. */
static uint16_t slot_parent(const uint8_t *ram, uint16_t actor)
{
    return read_word(ram, 0x8500 + read_word(ram, actor + 8) - 2);
}

/* 68DF: terrain material under a screen point (mask is 1 << (material & 15)). */
static unsigned terrain_material(const uint8_t *ram, const uint8_t *tiles, uint16_t x, uint16_t y)
{
    uint16_t map_x = x - 0x60 + read_word(ram, 0xb70a);
    uint16_t map_y = 0x177 - y + read_word(ram, 0xb70c);
    return tiles[0x4000 + world_tile_address(map_x, map_y) + 3] >> 3;
}

/* 657D/6580: velocity from an angle and the speed at [bp+2E] (signed product). */
static void angle_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned angle)
{
    const uint8_t *bank = rom + 0xa0000;
    int16_t speed = (int16_t)read_word(ram, actor + 0x2e);
    angle = 2 * (angle & 255);
    write_word(ram, actor + 0x38, (uint16_t)(((int16_t)read_word(bank, 0x9e12 + angle) * (int32_t)speed) >> 8));
    write_word(ram, actor + 0x3a, (uint16_t)(((int16_t)read_word(bank, 0x9d92 + angle) * (int32_t)speed) >> 8));
}

int world_gaps_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    /* Surface variant of the secondary weapon: a burst spawner that releases
     * one rising shot every fourth pass. */
    case 0x0baa5:
        SW(actor + 0x42, 1);
        SW(actor, 0xbaaf);
        /* fall through */
    case 0x0baaf: {
        if (decrement(ram, actor + 0x42)) break;
        uint16_t shot = world_spawn_child(ram, actor, 0xbb40);
        if (shot) {
            uint16_t launcher = slot_parent(ram, actor);
            world_queue_sound(ram, 0x3b);
            ram[shot + 0x29] = ram[launcher + 0x29];
            SW(shot + 0x21, W(launcher + 0x21) + RW(0xba95 + 2 * (ram[launcher + 0x29] & 1)));
            SW(shot + 0x24, W(launcher + 0x24) + 4);
            ram[shot + 0x28] = (ram[launcher + 0x28] + 4) & 0xf8;
            ram[shot + 0x2d] = ram[actor + 0x2d];
            ram[shot + 0x0b] = ram[actor + 0x0b];
            SW(shot + 0x7a, W(actor + 0x58));
            SW(shot + 0x54, W(actor + 0x54));
            SW(actor + 0x58, W(actor + 0x58) + 1);
            SW(actor + 0x54, W(actor + 0x54) + 1);
        }
        if (!decrement(ram, actor + 0x7a)) finish_animation(ram, actor);
        else SW(actor + 0x42, 4);
        break;
    }
    /* Rising shot of the surface burst. */
    case 0x0bb40:
    case 0x0bb7e:
        if (address == 0x0bb40) {
            world_acquire_resource(ram, actor, 0x5a);
            SW(actor + 0x3c, 0xba7d);
            SW(actor + 0x26, 1);
            SW(actor + 0x36, 0x60);
            SW(actor + 0x2e, 0x600);
            angle_velocity(ram, main_rom, actor, ram[actor + 0x28]);
            SW(actor + 0x4e, (uint8_t)(ram[actor + 0x28] - 0x28) & 0xf8);
            SW(actor + 0x58, 1);
            SW(actor + 0x42, 0x0c);
            SW(actor, 0xbb7e);
        } else scroll_actor(ram, actor, 0xb71a);
        move_actor_axis(ram, actor + 0x20, W(actor + 0x38));
        move_actor_axis(ram, actor + 0x23, W(actor + 0x3a));
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40) & 6;
            SW(actor + 0x58, RW(0xb9cb + frame));
            SW(actor + 0x2a, RW(0xb993 + frame + W(actor + 0x4e)));
            SW(actor + 0x40, W(actor + 0x40) + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        {
            int burst = !W(actor + 0x26);
            if (!burst) {
                if (W(actor + 0x3c) != 0xffff) world_append_candidate(ram + 0xaee6, actor);
                if (W(actor + 0x42)) decrement(ram, actor + 0x42);
                else if ((int16_t)W(actor + 0x24) >= (int16_t)RW(0xba99 + 2 * (W(actor + 0x7a) & 3))) burst = 1;
                if (!burst && ((1u << (terrain_material(ram, tiles, W(actor + 0x21), W(actor + 0x24)) & 15)) & 0x5502))
                    burst = 1;
            }
            if (burst) {
                SW(actor + 0x36, 0x0c);
                SW(actor + 0x3c, 0xba8d);
                SW(actor + 0x40, 0);
                SW(actor + 0x58, 1);
                SW(actor + 0x42, 0x29);
                SW(actor, 0xbc14);
            }
        }
        break;
    /* The shot's blast. */
    case 0x0bc14:
        scroll_actor(ram, actor, 0xb71a);
        if (!decrement(ram, actor + 0x58)) {
            uint16_t frame = W(actor + 0x40);
            if (RW(0xb9d3 + frame) == 0xffff) { finish_animation(ram, actor); break; }
            SW(actor + 0x2a, RW(0xb9d3 + frame));
            SW(actor + 0x58, RW(0xb9ff + frame));
            SW(actor + 0x40, frame + 2);
        }
        queue_sprite(ram, main_rom, actor, 1);
        if (W(actor + 0x42)) {
            decrement(ram, actor + 0x42);
            SW(actor + 0x26, 0x7f00);
            if (!((W(0x274) & 3) ^ W(actor + 0x54)) && W(actor + 0x3c) != 0xffff)
                world_append_candidate(ram + 0xaee6, actor);
        }
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
