#include <stdint.h>

/* World 0000:6237. The caller supplies a full work RAM segment, the
 * bus-ordered main ROM, the dying actor and the actor credited for the hit. */
static uint16_t word_at(const uint8_t *memory, uint16_t offset)
{
    return memory[offset] | ((uint16_t)memory[(uint16_t)(offset + 1)] << 8);
}

void world_award_score(uint8_t *ram, const uint8_t *rom,
                       uint16_t enemy, uint16_t attacker)
{
    unsigned owner = ram[(uint16_t)(attacker + 11)] & 3;
    if ((owner != 1 && owner != 2) || !(ram[0x264] & (2u << owner)))
        return;

    uint16_t kind = word_at(ram, enemy + 14);
    if (kind < 0x29) {
        uint16_t category = word_at(rom, 0x650a + 2 * kind);
        if (category != UINT16_MAX) {
            uint16_t address = 0x120 + (owner - 1) * 0x40 + (category & 2);
            uint16_t count = word_at(ram, address) + 1;
            ram[address] = (uint8_t)count;
            ram[address + 1] = count >> 8;
        }
    }

    uint16_t dest = word_at(ram, 0x254 + 2 * (owner - 1)) + 1;
    uint16_t source = 0x63c2 + 4 * kind;
    unsigned carry = 0;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned a = ram[dest], b = rom[source++];
        unsigned sum = 10 * ((a >> 4) + (b >> 4)) + (a & 15) + (b & 15) + carry;
        carry = sum >= 100;
        sum %= 100;
        ram[dest++] = (uint8_t)((sum / 10) * 16 + sum % 10);
    }
}
