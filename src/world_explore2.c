/* Recovered World actor handlers: handlers found by exploratory play of stage indexes 3-5.
 * Add one case per handler entry (linear address). Return WORLD_HANDLER_STOP
 * from a path that is not recovered yet; break when the handler is done. */
#include "world_dispatch_internal.h"
#include <string.h>

/* 37EB: draw CH columns of CL four-by-four tile blocks from a word list in bank A. */
static void tile_grid(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t base,
                      uint16_t dest, uint16_t source, uint16_t size)
{
    unsigned rows = (uint8_t)((size & 255) - 1) + 1, columns = (uint8_t)((size >> 8) - 1) + 1;
    write_word(ram, 0xb89a, size);
    write_word(ram, 0xb89c, dest);
    for (unsigned i = 0; i < columns; i++) {
        uint16_t cell = dest;
        for (unsigned j = 0; j < rows; j++) {
            world_draw_tile_block(tiles, rom, base, cell, read_word(rom + 0xa0000, source));
            source += 2;
            cell = (cell + 0x400) & 0x3fff;
        }
        if (i + 1 < columns) {
            dest = (dest & 0xff00) | ((dest + 16) & 255);
            write_word(ram, 0xb89c, dest);
        }
    }
}

int world_explore2_handlers(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
        uint8_t *tiles, uint16_t *actor_io, uint32_t address)
{
    uint16_t actor = *actor_io;
    (void)pass;
    switch (address) {
    /* Ranking backdrop after a game over in stage index 3: the same block list twice. */
    case 0x0c944:
        if (!tiles) return WORLD_HANDLER_STOP;
        tile_grid(ram, main_rom, tiles, 0x8000, 0x0f60, 0x4b14, 0x0408);
        tile_grid(ram, main_rom, tiles, 0x8000, 0x0fa0, 0x4b14, 0x0408);
        finish_animation(ram, actor);
        break;
    default:
        return WORLD_HANDLER_UNKNOWN;
    }
    *actor_io = actor;
    return WORLD_HANDLER_DONE;
}
