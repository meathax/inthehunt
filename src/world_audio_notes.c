#include "world_audio_internal.h"
#include <string.h>

typedef WorldAudioWrites YmEvents;

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void ym(YmEvents *out, unsigned reg, unsigned value)
{
    if (out->count < 256) {
        out->reg[out->count] = (uint8_t)reg;
        out->data[out->count++] = (uint8_t)value;
    }
}

static void relinquish(uint8_t *s, uint8_t *t, unsigned reserved)
{
    unsigned bank = word(t + 0x12);
    if (reserved & s[0x95e + bank]) {
        s[0x95e + bank] &= (uint8_t)~reserved;
        t[0x1c] = 0;
    }
    s[0x95c + bank] &= (uint8_t)~t[0x1d];
    s[0x95a + bank] &= (uint8_t)~t[0x1e];
    s[0x960 + bank] &= (uint8_t)~t[0x1e];
    t[0x1d] = t[0x1e] = 0;
}

static void reserve(uint8_t *s, uint8_t *t)
{
    unsigned bank = word(t + 0x12);
    unsigned free = s[0x956 + bank] & ~s[0x95e + bank];
    if (t[0x1c] || !free) return;
    uint8_t mask = (uint8_t)(free & (0u-free));
    s[0x95e + bank] |= mask;
    t[0x1c] = mask;
}

static int acquire(uint8_t *s, uint8_t *t)
{
    unsigned bank = word(t + 0x12);
    unsigned allowed = t[0x1c] ? t[0x1c] : (uint8_t)~s[0x95e + bank];
    unsigned mask = allowed & s[0x956 + bank] & ~(s[0x95c + bank] | s[0x960 + bank]);
    if (mask & ~s[0x95a + bank]) mask &= ~s[0x95a + bank];
    if (!mask) return 0;
    uint8_t bit = s[0x962 + bank];
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t selected = bit & mask;
        bit = (uint8_t)((bit << 1) | (bit >> 7));
        if (!selected) continue;
        s[0x962 + bank] = bit;
        if (selected & s[0x95a + bank]) {
            s[0x95c + bank] |= selected;
            t[0x1d] = selected;
        }
        s[0x95a + bank] |= selected;
        t[0x1e] = selected;
        unsigned channel = 0;
        while ((selected >>= 1) != 0) ++channel;
        t[0x1f] = (uint8_t)channel;
        if (t[0x18] >= 128) return 1;
        unsigned shadow = 0x966 + 16*channel;
        put(t + 0x14, shadow);
        s[shadow] = (uint8_t)channel;
        return 1;
    }
    return 0;
}

static void ga(YmEvents *out, unsigned channel, unsigned reg, unsigned value)
{
    if (out->count < 256) {
        out->pcm[out->count] = 1;
        ym(out, 8*channel + reg, value);
        if (reg == 6) {
            if (value) out->busy |= (uint8_t)(1u << channel);
            else out->busy &= (uint8_t)~(1u << channel);
        }
    }
}

static void key_off(uint8_t *s, const uint8_t *t, YmEvents *out)
{
    if (t[0x18] >= 128) {
        if (out->busy & (1u << t[0x1f])) ga(out, t[0x1f], 6, 0);
        return;
    }
    uint8_t *voice = s + word(t + 0x14);
    voice[0] &= (uint8_t)~0x78;
    ym(out, 8, voice[0] & 0x7f);
}

static void silence(uint8_t *s, const uint8_t *rom, const uint8_t *t, YmEvents *out)
{
    if (t[0x18] >= 128) return;
    uint8_t *voice = s + word(t + 0x14);
    voice[0] &= 0x7f;
    unsigned channel = voice[0] & 7;
    const uint8_t *inst = rom + (word(rom + 0x442) << 4) + word(voice + 2);
    for (unsigned i = 0; i < 4; ++i) ym(out, rom[0xc17+i] | channel, inst[0x1a+i] | 15);
}

static void instrument(uint8_t *s, const uint8_t *rom, uint8_t *t, YmEvents *out)
{
    unsigned channel = t[0x1f] & 7;
    if (t[0x18] >= 128) {
        unsigned sample = (word(rom + 0x448) << 4) + (uint8_t)(2*t[0x18]);
        for (unsigned i = 0; i < 4; ++i) ga(out, channel, i, rom[sample+i]);
        return;
    }
    uint8_t *voice = s + word(t + 0x14);
    const uint8_t *bank = rom + (word(rom + 0x442) << 4);
    unsigned offset = word(bank + (uint8_t)(2*t[0x18]));
    const uint8_t *inst = bank + offset;
    put(t + 0x16, offset);
    voice[0] = (uint8_t)channel;
    voice[1] = t[0x18];
    put(voice + 2, offset);
    if (inst[0]) for (unsigned i = 0; i < 4; ++i) ym(out, rom[0xbfd+i], inst[i]);
    ym(out, rom[0xc01] | channel, inst[4] & 63);
    ym(out, rom[0xc02] | channel, inst[5]);
    for (unsigned i = 0; i < 24; ++i) {
        ym(out, rom[0xc03+i] | channel, inst[6+i]);
        if (i < 8) voice[4+i] = inst[6+i];
        if (i >= 16 && i < 20) voice[12+i-16] = inst[6+i];
    }
}

static void fm_level(uint8_t *s, const uint8_t *rom, uint8_t *t, YmEvents *out)
{
    uint8_t *voice = s + word(t + 0x14);
    unsigned channel = voice[0] & 7;
    const uint8_t *inst = rom + (word(rom + 0x442) << 4) + word(t+0x16);
    unsigned levels = inst[0x1e];
    for (unsigned i = 0; levels; ++i, levels = (levels >> 1) & 0x77) {
        if (!(levels & 0x11)) continue;
        unsigned value = (uint8_t)(127 - t[12] + inst[10+i]);
        if (value >= 128) value = 127;
        voice[8+i] = (uint8_t)value;
        ym(out, rom[0xc07+i] | channel, value);
    }
}
static void fm_pitch(uint8_t *t, YmEvents *out)
{
    unsigned channel = t[0x1f];
    unsigned note = (uint8_t)(t[13]-1);
    unsigned key = ((note / 3)*4 + note % 3) & 255;
    if (key < 128) { ym(out, 0x28 | channel, key); ym(out, 0x30 | channel, t[0x1a]); }
}
static void play_note(uint8_t *s, const uint8_t *rom, uint8_t *t, YmEvents *out)
{
    if (t[0x18] >= 128) {
        unsigned attenuation = (uint8_t)(127-t[12]);
        unsigned rate = (uint8_t)(t[13]-0x2b);
        if (attenuation >= 0x2f) attenuation = 0x2f;
        if (rate >= 0x33) rate = 0x1a;
        ga(out,t[0x1f],5,rom[0xd1e + attenuation]);
        ga(out,t[0x1f],4,rom[0xceb+rate]);
        ga(out,t[0x1f],6,2);
        return;
    }
    uint8_t *voice = s + word(t+0x14);
    unsigned channel = voice[0] & 7;
    const uint8_t *inst = rom + (word(rom+0x442) << 4) + word(t+0x16);
    fm_level(s, rom, t, out);
    fm_pitch(t, out);
    ym(out, 0x20 | channel, inst[4]);
    voice[0] |= inst[0x24] & 0x78;
    ym(out, 8, voice[0] & 0x7f);
    ym(out, 1, inst[0x23]);
    ym(out, 1, 0);
}

static int notes(uint8_t *s, const uint8_t *rom, uint16_t task, YmEvents *out)
{
    uint8_t *t = s + task;
    unsigned phase = *t & 15;
    if (phase != 13 && phase != 14) return 0;

    if (t[0x1e] & (uint8_t)~t[0x1d] & s[0x95c + word(t+0x12)]) t[0x1e] = 0;
    unsigned stop = s[word(t+6) + 9];
    if (!stop && phase == 14) {
        if (t[15] && !--t[15]) {
            if (t[0x1e]) key_off(s, t, out);
            relinquish(s, t, 0);
        }
        if (--t[14]) return 1;
        --*t;
    }
    const uint8_t *music = rom + (word(rom + 0x440) << 4);
    uint16_t cursor = (uint16_t)word(t+10);
    for (unsigned i = 0; i < 256 && !stop; ++i) {
        unsigned op = music[cursor], arg = music[(uint16_t)(cursor+1)];
        if (op < 128) {
            unsigned old_gate = t[15];
            t[13] = (uint8_t)op;
            t[0x1a] = 0;
            t[14] = (uint8_t)arg;
            t[15] = music[(uint16_t)(cursor+2)];
            uint8_t volume = (uint8_t)(music[(uint16_t)(cursor+3)] + s[word(t+6)+12] - 127);
            t[12] = volume & 128 ? 0 : volume;
            put(t+10, (uint16_t)(cursor+4));
            if (old_gate) {
                if (t[0x1e]) play_note(s,rom,t,out);
            } else if (t[15] && (t[0x1e] || acquire(s,t))) {
                silence(s,rom,t,out);
                instrument(s,rom,t,out);
                play_note(s,rom,t,out);
            }
            ++*t;
            return 1;
        }
        if (op == 0xff) { stop = 1; break; }
        unsigned group = (op >> 4) & 7;
        unsigned depth = t[0x20];
        uint8_t *loop = t + 0x21 + (depth ? 5*(depth-1) : 0);
        switch (group) {
        case 0:
            if (depth && loop[0] == 1) { cursor = (uint16_t)word(loop+3); --t[0x20]; }
            break;
        case 2:
            if (op == 0xa8 && depth) {
                put(loop+3, cursor);
                if (!loop[0] || --loop[0]) cursor = (uint16_t)word(loop+1);
                else --t[0x20];
            } else if (op != 0xa8 && depth < 4) {
                loop = t + 0x21 + 5*depth;
                loop[0] = (uint8_t)arg;
                put(loop+1,cursor);
                ++t[0x20];
            }
            break;
        case 3:
            relinquish(s,t,t[0x1c]);
            if (op != 0xb8) reserve(s,t);
            break;
        case 4: {
            unsigned saved = t[0x1c];
            if ((arg ^ t[0x18]) & 128) relinquish(s,t,saved);
            t[0x18] = (uint8_t)arg;
            put(t+0x12,arg >> 7);
            if (saved) reserve(s,t);
            break;
        }
        default: break;
        }
        cursor += 4;
    }
    if (!stop) return 0;
    if (t[0x1e]) { key_off(s,t,out); silence(s,rom,t,out); }
    relinquish(s,t,t[0x1c]);
    *t = 0;
    return 1;
}

int world_audio_note_io(uint8_t *s, const uint8_t *rom, uint16_t task, const WorldAudioBus *bus)
{
    uint8_t next[WORLD_AUDIO_STATE_SIZE];
    YmEvents out = {0};
    if (bus && bus->ga20_busy) out.busy = bus->ga20_busy(bus->context);
    memcpy(next,s,sizeof next);
    if (!notes(next,rom,task,&out) || out.count == 256) return 0;
    memcpy(s,next,sizeof next);
    if (bus) for (unsigned i = 0; i < out.count; ++i) {
        if (out.pcm[i]) {
            if (bus->ga20_write) bus->ga20_write(bus->context,out.reg[i],out.data[i]);
        } else if (bus->ym_write) bus->ym_write(bus->context,out.reg[i],out.data[i]);
    }
    return 1;
}

void world_audio_quiet_fm(uint8_t *s, const uint8_t *rom, uint8_t *t, WorldAudioWrites *out)
{
    key_off(s,t,out);
    silence(s,rom,t,out);
}

/* The effect stream controls the four operators individually. Its note,
 * level and key events use the same voices and shadows as the music tracks. */
int world_audio_effect_fm(uint8_t *s, const uint8_t *rom, uint8_t *t, unsigned op,
                          unsigned value, WorldAudioWrites *out)
{
    uint8_t *voice = s + word(t + 0x14);
    if (op < 0x80) {
        t[13] = (uint8_t)op;
        t[0x1a] = 0;
        fm_pitch(t,out);
        return 1;
    }
    if (op < 0xc0) { /* 127F: operator parameter slides. */
        unsigned group = op >> 4;
        unsigned offset = group == 9 ? 12 : group == 11 ? 8 : 4;
        unsigned mask = group == 8 ? 0x70 : group == 9 ? 0xc0 : group == 10 ? 15 : 255;
        unsigned regs = group == 9 ? 0xc13 : group == 11 ? 0xc07 : 0xc03;
        voice[0] &= 0x7f;
        for (unsigned i = 0; i < 4; ++i) {
            if (!(op & (1u << i))) continue;
            unsigned old = voice[offset+i] & mask;
            if ((value & 0x80) ? !old : old >= mask) continue;
            voice[offset+i] = (uint8_t)((old+value) | (voice[offset+i] & ~mask));
            ym(out,rom[regs+i] | t[0x1f],voice[offset+i]);
        }
        return 1;
    }
    switch (op) {
    case 0xe4:
        silence(s,rom,t,out);
        instrument(s,rom,t,out);
        return 1;
    case 0xe7:
        t[12] = (uint8_t)value;
        fm_level(s,rom,t,out);
        return 1;
    case 0xe0: {
        const uint8_t *bank = rom + (word(rom+0x442) << 4);
        ym(out,0x20 | (voice[0] & 7),bank[word(t+0x16)+4]);
        voice[0] |= (uint8_t)(value & 0x78);
        ym(out,8,voice[0] & 0x7f);
        return 1;
    }
    case 0xe1:
        voice[0] &= (uint8_t)~(value & 0x78);
        ym(out,8,voice[0] & 0x7f);
        return 1;
    case 0xe2: {
        unsigned delta = (uint8_t)(value << 6);
        if (value & 0x80) delta |= 0xff00;
        unsigned pitch = (uint16_t)((t[13] << 8) + t[0x1a] + delta);
        if (pitch < 0x8000) {
            t[13] = (uint8_t)(pitch >> 8);
            t[0x1a] = (uint8_t)pitch;
            fm_pitch(t,out);
        }
        return 1;
    }
    case 0xef: return 1;
    default: return 0;
    }
}
