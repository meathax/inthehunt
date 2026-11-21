/* Link with the native gameplay sources and compile with -UNDEBUG. */
#ifdef NDEBUG
#error Surface enemy regression requires assertions enabled.
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/world_dispatch.h"

static uint8_t ram[65536], rom[1048576], tiles[65536];
enum { actor = 0x7000, shot = 0x7080 };

static unsigned word(unsigned address)
{
    return ram[address] | (ram[address + 1] << 8);
}

static void put(unsigned address, unsigned value)
{
    ram[address] = value;
    ram[address + 1] = value >> 8;
}

static void run(unsigned handler, uint16_t x, unsigned health, int dies,
                int queues_sprite, int fires, unsigned owner)
{
    memset(ram, 0, sizeof ram);
    world_dispatch_reset();
    put(actor, handler);
    put(actor + 2, 0xb000);
    put(actor + 4, shot);
    put(actor + 0x0e, 9);
    put(actor + 0x21, x - 1);
    put(actor + 0x26, health);
    put(actor + 0x38, 0x120);
    put(actor + 0x46, 0x20);
    put(actor + 0x4e, 2);
    put(actor + 0x50, fires ? 1 : 2);
    put(0xc4, 3);
    put(0x9e04, 0x8c00);
    put(0x9e02, queues_sprite ? 200 : 6);
    ram[actor + 0x2d] = 32;
    ram[0x40 + 0x0b] = 1;
    ram[2 + 0x0b] = 2; /* Sound queue cursor after firing. */
    ram[0x264] = 12;
    put(0x254, 0xe000);
    put(0x256, 0xe010);
    rom[0x63c2 + 4 * 9] = 1;
    put(0xaee6, 2);
    put(0xaeea, shot);
    put(shot + 0x3c, 0xffff);
    ram[shot + 0x0b] = 2;

    WorldDispatch pass = { WORLD_AFTER_PLAYER_TWO, actor };
    assert(world_dispatch_native_video(&pass, ram, rom, tiles));
    assert(word(actor + 0x21) == x);
    assert(word(actor + 0x38) == (handler == 0x2cd3 ? 0x100 : 0x140));
    assert(word(actor + 0x4e) == 1);
    assert((word(actor) == 0x2eee) == dies);
    assert(word(0xc4) == (dies ? 2 : 3));
    assert(word(0xb6ea) == (dies ? 0 : 2));
    assert(word(0xaeea) == shot);
    assert(ram[actor + 0x0b] == owner);
    assert(ram[0xe001] == (owner == 1));
    assert(ram[0xe011] == (owner == 2));
}

static void run_zero_health_blast(void)
{
    memset(ram, 0, sizeof ram);
    world_dispatch_reset();
    put(actor, 0xe48a);
    put(actor + 2, 0x7000);
    put(actor + 4, 0x7100);
    put(actor + 0x0e, 0x1c);
    put(actor + 0x21, 128);
    put(actor + 0x24, 144);
    put(actor + 0x31, 128);
    put(actor + 0x44, 2);
    put(0x90, 0x8480);
    put(0x92, shot);
    put(0x9c, actor);
    put(0x8b04, 1);
    put(0x8900, 0x7180);
    put(0x8480 + 0x3c, 0xffff);
    put(shot + 0x3c, 0xffff);
    ram[shot + 0x0b] = 2;
    put(0x256, 0xe010);
    ram[0x264] = 8;
    rom[0x63c2 + 4 * 0x1c] = 1;

    WorldDispatch pass = { WORLD_AFTER_PLAYER_TWO, actor };
    assert(world_dispatch_native_video(&pass, ram, rom, tiles));
    assert(word(actor) == 0x1b4c);
    assert(word(actor + 0x3c) == 0xffff);
    assert(word(0x7180) == 0xe587);
    assert(word(0x7180 + 2) == 0x7000);
    assert(word(0x7180 + 0x24) == 144 + 0x70);
    assert(ram[actor + 0x0b] == 2);
    assert(ram[0xe011] == 1);
}

int main(void)
{
    run(0x2cd3, -64, 96, 1, 1, 0, 1);
    run(0x2cd3, 576, 96, 1, 1, 1, 2);
    run(0x2cd3, 576, 96, 1, 0, 0, 0);
    run(0x2cd3, -65, 96, 0, 1, 0, 0);
    run(0x2cd3, -63, 96, 0, 1, 0, 0);
    run(0x2cd3, 575, 96, 0, 1, 0, 0);
    run(0x2cd3, 577, 96, 0, 1, 0, 0);
    run(0x2cd3, 128, 0, 1, 1, 0, 2);
    run(0x2c68, 576, 96, 1, 1, 0, 1);
    run_zero_health_blast();
    puts("PASS: surface enemy clipping, score source and zero-health cleanup in both handlers");
    return 0;
}
