#include "world_audio_internal.h"
#include <string.h>

static unsigned word(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void put(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* 06C1, 05A0, 06F9 and the command-enable write at 083B. */
void world_audio_reset(uint8_t *s, const uint8_t *rom, int warm)
{
    memset(s, 0, WORLD_AUDIO_STATE_SIZE);
    for (unsigned i = 0; i < 32; ++i) put(s + 0x8d2 + 2*i, 0x80 + 0x40*i);
    s[0x953] = 32;
    s[0] = 2;
    s[0x40] = 1;
    put(s + 0x44, 0x40);
    put(s + 0x956, 0xfff);
    put(s + 0x962, 0x101);
    put(s + 0x964, 0x8080);
    s[0x9ed] = 1;
    put(s + 0x958, word(rom + 0x45c));
    s[0x9ea] = warm ? 0xff : 0;
}

/* 07AE reads the hardware latch, then 04EC writes the ring before ack. */
void world_audio_receive(uint8_t *s, uint8_t command)
{
    unsigned pos = s[0x8d0];
    s[0x8c0 + pos] = command;
    s[0x8d0] = (uint8_t)((pos + 1) & 15);
}

/* 0505 skips masked commands. Zero opens the gate for subsequent commands. */
static int next_command(uint8_t *s, unsigned *command)
{
    while (s[0x8d1] != s[0x8d0]) {
        unsigned pos = s[0x8d1];
        *command = s[0x8c0 + pos];
        s[0x8d1] = (uint8_t)((pos + 1) & 15);
        if (s[0x9ea] & *command) return 1;
        if (!*command) s[0x9ea] = 0xff;
    }
    return 0;
}

/* 0576 and 05E9 take one free task and insert it after higher priorities. */
static unsigned allocate_task(uint8_t *s, unsigned parent)
{
    unsigned fetch = s[0x952];
    if (fetch == s[0x953]) return 0;
    unsigned child = word(s + 0x8d2 + fetch*2);
    unsigned predecessor = parent, priority = s[parent + 3];
    s[0x952] = (uint8_t)((fetch + 1) & 63);
    unsigned next = word(s + predecessor + 4);
    while (priority < s[next + 3]) {
        predecessor = next;
        next = word(s + predecessor + 4);
    }
    put(s + predecessor + 4, child);
    put(s + child + 6, parent);
    put(s + child + 4, next);
    s[child + 3] = (uint8_t)(priority + 1);
    s[child + 2] = 0;
    s[child] = 0;
    return child;
}

/* 0DE2 dispatches one eligible command from the scheduler's command task. */
void world_audio_dispatch(uint8_t *s, const uint8_t *rom, uint16_t task)
{
    unsigned command;
    if (!next_command(s, &command)) return;
    if (command < 0x20) {
        s[task + 9] = (uint8_t)command;
        ++s[task];
        return;
    }
    unsigned index = command - 0x20;
    unsigned table = word(rom + 0x444) << 4;
    unsigned sequence = word(rom + table + 2*index);
    if (!sequence) return;
    unsigned child = allocate_task(s, task);
    if (child) {
        s[child] = 4;
        s[child + 8] = (uint8_t)index;
        put(s + child + 0xa, sequence);
    }
}

static void ramp(uint8_t *t, unsigned interval, unsigned value)
{
    if (!t[interval] || --t[interval + 1]) return;
    t[interval + 1] = t[interval];
    if (t[value] == t[interval + 2]) t[interval] = 0;
    else if (t[value] < t[interval + 2]) ++t[value];
    else --t[value];
}

/* The two independent task lists are rooted at 0000 (effects/commands)
 * and 0040 (music). Each tick the driver dispatches on the low state nibble. */
int world_audio_music_step(uint8_t *s, const uint8_t *rom, uint16_t task)
{
    uint8_t *t = s + task;
    switch (*t & 15) {
    case 3: /* 0E1B: a low command selects a song for the music root. */
        if (!t[10]) {
            unsigned song = t[9];
            t[9] = 0;
            if (song & 0x80) { put(s + 0x958, word(rom + 0x45c)); return 1; }
            if (song) {
                t[10] = (uint8_t)song;
                s[0x48] = (uint8_t)(song - 1);
                s[0x40] = 8;
                put(s + 0x954, 0x40);
            }
            --*t;
        }
        return 1;
    case 8: { /* 0E4B: open the song header and allocate each enabled track. */
        const uint8_t *music = rom + (word(rom + 0x440) << 4);
        uint16_t row = (uint16_t)word(music + 2*t[8]);
        t[12] = music[row];
        t[13] = music[(uint16_t)(row + 1)];
        unsigned tracks = music[(uint16_t)(row + 2)];
        row += 3;
        while (tracks) {
            if (tracks & 0x80) {
                unsigned child = allocate_task(s, task);
                if (child) {
                    s[child] = 12;
                    put(s + child + 10, word(music + row));
                }
            }
            tracks = (tracks << 1) & 255;
            row += 2;
        }
        t[0x19] = t[0x1c] = 0;
        t[2] |= 1;
        ++*t;
        return 1;
    }
    case 9: { /* 0E99: tick volume/tempo ramps and the fractional song clock. */
        t[9] = s[word(t + 6) + 9];
        t[0x18] = (t[0x18] + 1) & 7;
        if (!t[0x18]) {
            ramp(t, 0x19, 12);
            ramp(t, 0x1c, 13);
        }
        unsigned ticks = s[0x9ec] ? s[0x9ec] : 256;
        unsigned scale = rom[0x459] ? rom[0x459] : 256;
        uint16_t total = (uint16_t)(word(t + 0x16) + ticks*t[13]);
        uint16_t threshold = (uint16_t)(0x138*scale);
        if (total >= threshold) {
            total -= threshold;
            if (t[3] < s[word(t + 4) + 3]) t[2] &= (uint8_t)~1u;
            else { ++*t; t[2] |= 1; }
        } else t[2] |= 1;
        put(t + 0x16, total);
        return 1;
    }
    case 10: /* 0EFD: allow the command task to start the next song. */
        if (t[9] == 0x83 && ((s[0x960] | s[0x95e] | s[0x95c] | s[0x95a]) ||
            (s[0x961] | s[0x95f] | s[0x95d] | s[0x95b]))) return 0;
        s[word(t + 6) + 10] = 0;
        *t = 1;
        return 1;
    case 12: /* 0FBF: initialize one track before decoding its first event. */
        t[0x18] = t[0x1f] = t[0x1c] = t[0x1d] = t[0x1e] = 0;
        put(t + 0x12, 0);
        t[0x20] = t[0x0f] = 0;
        t[8] = 0xf0;
        ++*t;
        return 1;
    case 14: { /* 107D: count note duration until a gate or event comes due. */
        if (s[word(t + 6) + 9] || t[0x0e] == 1 || t[0x0f] == 1) return 0;
        if (t[0x1e] & (uint8_t)~t[0x1d] & s[0x95c + word(t + 0x12)]) t[0x1e] = 0;
        if (t[0x0f]) --t[0x0f];
        --t[0x0e];
        return 1;
    }
    default: return 0;
    }
}

/* 0733 keys off all eight channels and initializes the YM2151 timer. */
void world_audio_reset_ym(const uint8_t *rom, WorldAudioYmWrite write, void *context)
{
    for (unsigned channel = 0; channel < 8; ++channel) {
        write(context, rom[0x73b], (uint8_t)channel);
        for (unsigned op = 0; op < 4; ++op)
            write(context, (uint8_t)(rom[0x742] + channel + 8*op), rom[0x746]);
    }
    for (unsigned address = 0x75e; address <= 0x76a; address += 6)
        write(context, rom[address + 1], rom[address]);
}

typedef WorldAudioWrites Ga20Events;

static int ga_write(Ga20Events *events, unsigned channel, unsigned reg, unsigned data)
{
    if (channel >= 4 || events->count >= 256) return 0;
    events->pcm[events->count] = 1;
    events->reg[events->count] = (uint8_t)(channel*8 + reg);
    events->data[events->count++] = (uint8_t)data;
    if (reg == 6) {
        if (data) events->busy |= (uint8_t)(1u << channel);
        else events->busy &= (uint8_t)~(1u << channel);
    }
    return 1;
}

/* 0C1B. Sample boundaries are four bytes in the sound program's address table. */
static int ga_sample(Ga20Events *out, const uint8_t *rom, const uint8_t *t)
{
    unsigned offset = (word(rom + 0x448) << 4) + (uint8_t)(2*t[0x18]);
    for (unsigned i = 0; i < 4; ++i)
        if (!ga_write(out, t[0x1f], i, rom[offset + i])) return 0;
    return 1;
}

static void release_voice(uint8_t *s, uint8_t *t)
{
    unsigned bank = word(t + 0x12);
    s[0x95c + bank] &= (uint8_t)~t[0x1d];
    s[0x95a + bank] &= (uint8_t)~t[0x1e];
    s[0x960 + bank] &= (uint8_t)~t[0x1e];
    t[0x1d] = t[0x1e] = 0;
}

static int effect_voice(uint8_t *s, const uint8_t *rom, uint8_t *t, Ga20Events *out)
{
    if (t[0x1e]) return 1;
    unsigned bank = word(t + 0x12);
    unsigned available = s[0x956 + bank] & ~s[0x958 + bank] & ~(s[0x95c + bank] | s[0x960 + bank]);
    if (!available) {
        /* 11AF chooses the highest eligible effect ID, taking the last tie. */
        uint8_t *victim = NULL;
        unsigned next = word(s + 4), threshold = t[8];
        for (unsigned visited = 0; visited < 34 && s[next + 3] > s[3]; ++visited) {
            uint8_t *other = s + next;
            if (other != t && other[0x1e] && !((other[0x18] ^ t[0x18]) & 0x80) && other[8] >= threshold) {
                victim = other;
                threshold = other[8];
            }
            next = word(other + 4);
        }
        if (!victim) return 0;
        if (!bank) world_audio_quiet_fm(s, rom, victim, out);
        else if ((out->busy & (1u << victim[0x1f])) && !ga_write(out, victim[0x1f], 6, 0)) return 0;
        release_voice(s, victim);
        victim[0] = 0;
        available = s[0x956 + bank] & ~s[0x958 + bank] & ~(s[0x95c + bank] | s[0x960 + bank]);
        if (!available) return 0;
    }
    unsigned preferred = available & ~s[0x95a + bank];
    if (preferred) available = preferred;
    uint8_t mask = s[0x964 + bank];
    if (!mask) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t selected = mask & available;
        mask = (uint8_t)((mask >> 1) | (mask << 7));
        if (!selected) continue;
        s[0x964 + bank] = mask;
        if (s[0x95a + bank] & selected) {
            s[0x95c + bank] |= selected;
            t[0x1d] = selected;
        }
        s[0x95a + bank] |= selected;
        s[0x960 + bank] |= selected;
        t[0x1e] = selected;
        unsigned channel = 0;
        while ((selected >>= 1) != 0) ++channel;
        t[0x1f] = (uint8_t)channel;
        if (!bank) { unsigned shadow = 0x966 + 16*channel; put(t + 0x14, shadow); s[shadow] = (uint8_t)channel; }
        return 1;
    }
    return 0;
}

static int retire_matching_effects(uint8_t *s, const uint8_t *rom, uint8_t *t, unsigned id, Ga20Events *out)
{
    unsigned root = 0;
    for (unsigned i = 0; i < 34; ++i) {
        root = word(s + root + 4);
        if (root > 0x840) return 0;
        uint8_t *old = s + root;
        if (old[3] <= s[3]) return 1;
        if (old == t || (id < 256 && old[8] != id)) continue;
        if (old[0x1e]) {
            if (old[0x18] < 0x80) world_audio_quiet_fm(s,rom,old,out);
            else if ((out->busy & (1u << old[0x1f])) && !ga_write(out, old[0x1f], 6, 0)) return 0;
        }
        release_voice(s, old);
        *old = 0;
    }
    return 0;
}

static int step_effect(uint8_t *s, const uint8_t *rom, uint16_t task, Ga20Events *out)
{
    uint8_t *t = s + task;
    switch (*t & 15) {
    case 4:
        t[0x20] = t[0x18] = t[0x1f] = 0;
        t[0x1c] = t[0x1d] = t[0x1e] = 0;
        put(t + 0x12, 0);
        ++*t;
        break;
    case 5: break;
    case 6:
        if (--t[14]) return 1;
        --*t;
        break;
    case 7:
        if (out->busy & (1u << t[0x1f])) return 1;
        release_voice(s, t);
        *t = 0;
        return 1;
    default: return 0;
    }
    const uint8_t *seq = rom + (word(rom + 0x444) << 4);
    uint16_t cursor = (uint16_t)word(t + 10);
    for (unsigned count = 0; count < 256; ++count) {
        unsigned op = seq[cursor], value = seq[(uint16_t)(cursor+1)], delay = seq[(uint16_t)(cursor+2)];
        unsigned channel = t[0x1f];
        cursor += 3;
        if (op == 0xfd) {
            if (!retire_matching_effects(s, rom, t, value, out)) return 0;
            put(t + 10, cursor);
            return 1;
        }
        if (op == 0xe5) {
            unsigned depth = t[0x20];
            if (depth < 4) {
                uint8_t *loop = t + 0x21 + 5*depth;
                loop[0] = (uint8_t)value;
                put(loop + 1, (uint16_t)(cursor - 3));
                ++t[0x20];
            }
            continue;
        }
        if (op == 0xe6) {
            unsigned depth = t[0x20];
            if (depth) {
                uint8_t *loop = t + 0x21 + 5*(depth - 1);
                put(loop + 3, (uint16_t)(cursor - 3));
                if (!loop[0] || --loop[0]) cursor = (uint16_t)(word(loop + 1) + 3);
                else --t[0x20];
            }
            continue;
        }
        if (op == 0xf7 || op == 0xf8) {
            uint8_t *song = s + word(s + 0x954);
            unsigned target = op == 0xf7 ? 0x0c : 0x0d;
            unsigned interval = op == 0xf7 ? 0x19 : 0x1c;
            if (!value) song[target] = (uint8_t)delay;
            else {
                if (op == 0xf7 && (value & 128)) value = (uint8_t)-value;
                song[interval] = song[interval + 1] = (uint8_t)value;
                song[interval + 2] = (uint8_t)delay;
            }
            put(t + 10, cursor);
            return 1;
        }
        if (op == 0xfa || op == 0xfb || op == 0xf6) {
            if (op == 0xfa) {
                s[0x9ed] ^= 1;
                if (!retire_matching_effects(s, rom, t, 256, out)) return 0;
            }
            if (op == 0xf6) put(s + 0x958, value | ((unsigned)rom[0x45d] << 8));
            else s[word(t + 6) + 9] = op == 0xfa ? 0x83 : 0x81;
            op = 0xff;
        }
        if (op == 0xff) {
            if (t[0x1e] && t[0x18] < 0x80) {
                world_audio_quiet_fm(s,rom,t,out);
                release_voice(s,t);
                *t = 0;
                return 1;
            }
            if (t[0x1e]) *t = 7;
            else { release_voice(s, t); *t = 0; }
            return 1;
        }
        if (op == 0xe4) {
            if (t[0x1e] && ((value ^ t[0x18]) & 128)) return 0;
            t[0x18] = (uint8_t)value;
            put(t + 0x12, value >> 7);
            if (!effect_voice(s, rom, t, out)) { release_voice(s, t); *t = 0; return 1; }
            if (value < 128) world_audio_effect_fm(s,rom,t,op,value,out);
            else if (!ga_sample(out, rom, t)) return 0;
            continue;
        }
        if (t[0x18] < 128) {
            if (!world_audio_effect_fm(s,rom,t,op,value,out)) return 0;
            if (op == 0xe7) continue;
        } else if (op == 0xe7) {
            t[12] = (uint8_t)value;
            unsigned attenuation = (uint8_t)(0x7f - value);
            if (attenuation >= 0x2f) attenuation = 0x2f;
            if (!ga_write(out, channel, 5, rom[0xd1e + attenuation])) return 0;
            continue;
        } else if (op < 0x80) {
            t[13] = (uint8_t)op;
            t[0x1a] = 0;
            unsigned note = (uint8_t)(op - 0x2b);
            if (note >= 0x33) note = 0x1a;
            if (!ga_write(out, channel, 4, rom[0xceb + note])) return 0;
        } else if (op == 0xe0) {
            if (!ga_sample(out, rom, t) || !ga_write(out, channel, 6, 2)) return 0;
        } else if (op == 0xe1) {
            if ((out->busy & (1u << channel)) && !ga_write(out, channel, 6, 0)) return 0;
        } else if (op != 0xe2 && op != 0xef) return 0;
        if (delay) {
            t[14] = (uint8_t)delay;
            put(t + 10, cursor);
            ++*t;
            return 1;
        }
    }
    return 0;
}

static int effect_io(uint8_t *s, const uint8_t *rom, uint16_t task, uint8_t busy, WorldAudioYmWrite fm,
                            WorldAudioGa20Write write, void *context)
{
    uint8_t next[WORLD_AUDIO_STATE_SIZE];
    Ga20Events events = {0};
    events.busy = busy;
    memcpy(next, s, sizeof next);
    if (!step_effect(next, rom, task, &events) || events.count == 256) return 0;
    memcpy(s, next, sizeof next);
    for (unsigned i = 0; i < events.count; ++i) {
        if (events.pcm[i]) { if (write) write(context,events.reg[i],events.data[i]); }
        else if (fm) fm(context,events.reg[i],events.data[i]);
    }
    return 1;
}

int world_audio_effect_step(uint8_t *s, const uint8_t *rom, uint16_t task, uint8_t busy,
                            WorldAudioGa20Write write, void *context)
{
    return effect_io(s,rom,task,busy,NULL,write,context);
}

/* 0672 walks live task links. Children can be allocated or freed while their
 * parent is executing, so the successor must be fetched after each callback. */
static unsigned run_task_list(uint8_t *s, const uint8_t *rom, unsigned root, const WorldAudioBus *bus)
{
    unsigned current = root, previous = root, skipped = 0;
    for (unsigned calls = 0; calls < 128; ++calls) {
        uint8_t *t = s + current;
        unsigned phase = t[0] & 15;
        unsigned reply_phase = s[0x9ed];
        int ok = 1;
        switch (phase) {
        case 0: case 11:
            if (t[3] && t[3] >= s[word(t + 4) + 3]) {
                put(s + previous + 4, word(t + 4));
                unsigned pos = s[0x953];
                put(s + 0x8d2 + 2*pos, current);
                s[0x953] = (uint8_t)((pos + 1) & 63);
                current = previous;
            }
            break;
        case 1: break;
        case 2:
            if (t[9]) ++t[0];
            else world_audio_dispatch(s, rom, (uint16_t)current);
            break;
        case 4: case 5: case 6: case 7: {
            uint8_t busy = bus && bus->ga20_busy ? bus->ga20_busy(bus->context) : 0;
            ok = effect_io(s, rom, (uint16_t)current, busy, bus ? bus->ym_write : NULL,
                    bus ? bus->ga20_write : NULL, bus ? bus->context : NULL);
            break;
        }
        case 13: case 14:
            ok = world_audio_note_io(s, rom, (uint16_t)current, bus);
            break;
        default: ok = world_audio_music_step(s, rom, (uint16_t)current); break;
        }
        if (!ok) {
            if (bus && bus->unrecovered)
                bus->unrecovered(bus->context, (uint16_t)current, (uint8_t)phase, (uint16_t)word(t + 10));
            ++skipped;
            if (current == root) {
                s[word(t + 6) + 10] = 0;
                t[0] = root == 0 ? 2 : 1;
            } else {
                release_voice(s, t);
                t[0] = 0;
            }
        }
        if (reply_phase != s[0x9ed] && bus && bus->reply)
            bus->reply(bus->context, s[0x9ed] ? rom[0x1634] : 0);
        if (s[current + 2] & 1) {
            unsigned priority = s[current + 3];
            unsigned next = word(s + current + 4);
            while (priority < s[next + 3]) {
                current = next;
                next = word(s + current + 4);
            }
        }
        previous = current;
        current = word(s + current + 4);
        if (!s[current + 3]) break;
    }
    return skipped;
}

unsigned world_audio_tick(uint8_t *s, const uint8_t *rom, const WorldAudioBus *bus)
{
    ++s[0x9eb];
    if (s[0x9ec]) return 0;
    s[0x9ec] = s[0x9eb];
    s[0x9eb] = 0;
    unsigned skipped = run_task_list(s, rom, 0, bus);
    skipped += run_task_list(s, rom, 0x40, bus);
    s[0x9ec] = 0;
    return skipped;
}
