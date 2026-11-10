/* Developer check: replay the native World code against a MAME reference
 * recording (tools/ref/ref_capture.lua) and report the first divergence.
 * Never part of the shipped game.
 *
 *   check_replay REFDIR [--from PASS] [--passes N] [--rom maincpu.bin]
 *                [--dump-on-stop DIR] [--keep-going] [--quiet] [--trace trace.bin]
 *                [--verified]
 *
 * --keep-going resynchronises from the reference after a mismatch or a missing
 * handler and prints a tally of every missing handler in the recording.
 * --trace compares before every handler call of the traced passes, naming the
 * handler whose result first differs (capture with trace_pass=N). Together
 * with --keep-going, a missing handler inside a traced pass is stepped over
 * using the reference state before the next call, so the handlers after it
 * are still compared; such calls are tallied separately.
 *
 * --verified lists, per handler, how many of its calls had their result
 * compared and found equal: calls inside passes that ran completely and matched
 * at the next pass start, and calls followed by a matching trace record.
 *
 * State is compared at each pass start (after world_dispatch_begin): work RAM
 * 0000-BFFF, video RAM, OAM and palette. --from loads that pass's reference
 * state and skips earlier board events. Exit 0 = all compared passes match,
 * 2 = native stop (unrecovered routine), 3 = mismatch, 1 = usage/IO error. */
#include "../../src/world_dispatch.h"
#include "../../src/world_frame.h"
#include "../../src/world_palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORK = 65536, VIDEO = 65536, OAM = 2048, PAL = 2048, HEADER = 16,
       RECORD = HEADER + WORK + VIDEO + OAM + PAL, COMPARED_WORK = 0xc000 };

static uint8_t rom[1 << 20], ram[WORK], tiles[VIDEO], oam[OAM], palette[PAL];
static uint8_t ref[RECORD];
static FILE *passes, *events, *irqs;
static unsigned irq_pass, irq_call, irq_system;
static int quiet;
static FILE *trace;
enum { MISSING_MAX = 4096 };
static struct { uint32_t address; unsigned count, first_pass; } missing[MISSING_MAX];
static unsigned missing_count;
static int resync_calls;
/* --verified: per handler, calls whose result was compared and matched. */
enum { TALLY_MAX = 8192, PASS_CALLS_MAX = 4096 };
static struct { uint32_t address; unsigned traced, whole; } tally[TALLY_MAX];
static unsigned tally_count;
static uint32_t pass_calls[PASS_CALLS_MAX];
static unsigned pass_call_count;
static int pass_whole;

static void credit(uint32_t address, int whole)
{
    unsigned i;
    for (i = 0; i < tally_count; ++i) if (tally[i].address == address) break;
    if (i == TALLY_MAX) return;
    if (i == tally_count) { tally[i].address = address; tally[i].traced = tally[i].whole = 0; ++tally_count; }
    if (whole) ++tally[i].whole; else ++tally[i].traced;
}
static unsigned trace_skipped_passes;

static void note_missing(uint32_t address, unsigned pass)
{
    for (unsigned i = 0; i < missing_count; ++i)
        if (missing[i].address == address) { ++missing[i].count; return; }
    if (missing_count < MISSING_MAX) {
        missing[missing_count].address = address;
        missing[missing_count].count = 1;
        missing[missing_count++].first_pass = pass;
    }
}

/* Trace records are written in (pass, call) order; index their headers once. */
enum { TRACE_RECORD = 8 + 0xc000 + 65536 };
static struct { unsigned pass, call; } *trace_index;
static size_t trace_records, trace_cursor;
static uint8_t trace_record[TRACE_RECORD];

static void index_trace(void)
{
    uint8_t header[8];
    size_t capacity = 0;
    for (long at = 0; !fseek(trace, at, SEEK_SET) && fread(header, 1, 8, trace) == 8; at += TRACE_RECORD) {
        if (trace_records == capacity) {
            capacity = capacity ? capacity * 2 : 1024;
            trace_index = realloc(trace_index, capacity * sizeof *trace_index);
            if (!trace_index) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        trace_index[trace_records].pass = header[0] | header[1] << 8 | header[2] << 16 | (unsigned)header[3] << 24;
        trace_index[trace_records++].call = header[4] | header[5] << 8 | header[6] << 16 | (unsigned)header[7] << 24;
    }
}

/* Load the trace record (pass, call) into trace_record. Returns 0 when absent. */
static int trace_load(unsigned pass, unsigned call)
{
    if (!trace) return 0;
    if (!trace_index) index_trace();
    size_t i = trace_cursor;
    if (i >= trace_records || trace_index[i].pass > pass || (trace_index[i].pass == pass && trace_index[i].call > call)) i = 0;
    for (; i < trace_records; ++i)
        if (trace_index[i].pass == pass && trace_index[i].call == call) break;
    if (i == trace_records) return 0;
    trace_cursor = i;
    return !fseek(trace, (long)i * TRACE_RECORD, SEEK_SET) && fread(trace_record, 1, TRACE_RECORD, trace) == TRACE_RECORD;
}

/* Compare against the per-call trace record (pass, call). 1 match, 0 differ, -1 no record. */
static int trace_check(unsigned pass, unsigned call, uint32_t previous)
{
    const uint8_t *record = trace_record;
    if (!trace_load(pass, call)) return -1;
    unsigned shown = 0;
    for (unsigned a = 0; a < 0xc000; ++a)
        if (ram[a] != record[8 + a]) {
            if (!shown) printf("pass %u: state differs before call %u (after handler %05lX)\n", pass, call + 1, (unsigned long)previous);
            if (shown++ < 24) printf("  work[%04x] native %02x reference %02x\n", a, ram[a], record[8 + a]);
        }
    for (unsigned a = 0; a < 65536; ++a)
        if (tiles[a] != record[8 + 0xc000 + a]) {
            if (!shown) printf("pass %u: state differs before call %u (after handler %05lX)\n", pass, call + 1, (unsigned long)previous);
            if (shown++ < 24) printf("  video[%04x] native %02x reference %02x\n", a, tiles[a], record[8 + 0xc000 + a]);
        }
    return !shown;
}

/* --keep-going with a trace: step over a handler the native build cannot run
 * by taking the reference state before the next call. The next actor is the
 * skipped actor's link in that state; a wrong guess shows up as a mismatch. */
static unsigned trace_skips, trace_mismatches, trace_calls_checked;
static int trace_resync(WorldDispatch *d, unsigned pass, unsigned call)
{
    if (!trace_load(pass, call)) return 0;
    uint16_t actor = d->actor;
    memcpy(ram, trace_record + 8, 0xc000);
    memcpy(tiles, trace_record + 8 + 0xc000, 65536);
    d->actor = (uint16_t)(ram[actor + 4] | ram[(uint16_t)(actor + 5)] << 8);
    switch (d->phase) {
    case WORLD_FRAME_HEAD: d->phase = WORLD_PLAYER_ONE; break;
    case WORLD_PLAYER_ONE: d->phase = WORLD_BEFORE_PLAYER_TWO; break;
    case WORLD_BEFORE_PLAYER_TWO:
        if (d->actor == (uint16_t)(ram[0x92] | ram[0x93] << 8)) d->phase = WORLD_PLAYER_TWO;
        break;
    case WORLD_PLAYER_TWO: d->phase = WORLD_AFTER_PLAYER_TWO; break;
    case WORLD_AFTER_PLAYER_TWO: break;
    }
    return 1;
}

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

static int read_file(const char *path, void *data, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static int write_file(const char *dir, const char *name, const void *data, size_t size)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    return !fclose(f) && ok;
}

static int load_record(unsigned pass)
{
    if (fseek(passes, (long)(pass - 1) * RECORD, SEEK_SET)) return 0;
    if (fread(ref, 1, RECORD, passes) != RECORD) return 0;
    return word(ref) == (pass & 0xffff);
}

static void next_irq(void)
{
    char line[96];
    irq_pass = 0;
    if (irqs && fgets(line, sizeof line, irqs) &&
        sscanf(line, "%u %u %x", &irq_pass, &irq_call, &irq_system) != 3) irq_pass = 0;
}

typedef struct { const char *name; const uint8_t *native, *expected; size_t size; } Region;

/* Returns the number of differing bytes and prints the first few. */
static size_t compare(unsigned pass, int verbose)
{
    const uint8_t *r = ref + HEADER;
    Region regions[] = {
        {"work", ram, r, COMPARED_WORK},
        {"video", tiles, r + WORK, VIDEO},
        {"oam", oam, r + WORK + VIDEO, OAM},
        {"palette", palette, r + WORK + VIDEO + OAM, PAL},
    };
    size_t total = 0;
    for (unsigned i = 0; i < 4; ++i) {
        size_t count = 0, first = 0;
        for (size_t a = 0; a < regions[i].size; ++a)
            if (regions[i].native[a] != regions[i].expected[a]) { if (!count++) first = a; }
        if (count && verbose) {
            printf("pass %u: %s differs in %zu bytes, first at %04zx\n", pass, regions[i].name, count, first);
            unsigned shown = 0;
            for (size_t a = first; a < regions[i].size && shown < 16; ++a)
                if (regions[i].native[a] != regions[i].expected[a]) {
                    printf("  %s[%04zx] native %02x reference %02x\n", regions[i].name, a,
                           regions[i].native[a], regions[i].expected[a]);
                    ++shown;
                }
        }
        total += count;
    }
    return total;
}

static int run_pass(unsigned pass, uint32_t *stop_address, uint16_t *stop_actor, unsigned *calls_out)
{
    WorldDispatch d;
    world_dispatch_begin(&d, ram);
    unsigned calls = 0, skipped_here = 0;
    pass_call_count = 0;
    pass_whole = 0;
    uint32_t address;
    uint32_t previous = 0;
    while (world_dispatch_target(&d, ram, &address)) {
        int traced = trace_check(pass, calls, previous);
        if (traced > 0) { ++trace_calls_checked; if (previous) credit(previous, 0); }
        if (!traced) {
            if (!resync_calls) { *stop_address = previous; *calls_out = calls; return 5; }
            ++trace_mismatches;
            memcpy(ram, trace_record + 8, 0xc000);
            memcpy(tiles, trace_record + 8 + 0xc000, 65536);
        }
        previous = address;
        if (pass_call_count < PASS_CALLS_MAX) pass_calls[pass_call_count++] = address;
        WorldDispatch before = d;
        if (!world_dispatch_native_video(&d, ram, rom, tiles)) {
            d = before;
            if (resync_calls && trace_resync(&d, pass, calls + 1)) {
                note_missing(address, pass);
                ++trace_skips;
                ++skipped_here;
                ++calls;
                previous = 0;
                /* A busy VBlank inside the skipped call is already in the reference state. */
                while (irq_pass == pass && irq_call == calls) next_irq();
                continue;
            }
            *stop_address = address;
            *stop_actor = d.actor;
            *calls_out = calls;
            return 2;
        }
        ++calls;
        while (irq_pass == pass && irq_call == calls) {
            if (!world_frame_busy_vblank(ram, (uint16_t)irq_system)) { *stop_address = 0; *calls_out = calls; return 4; }
            next_irq();
        }
        if (calls > 65536) return 1;
    }
    while (irq_pass && irq_pass <= pass) next_irq();
    *calls_out = calls;
    if (skipped_here) ++trace_skipped_passes;
    else pass_whole = 1;
    return 0;
}

/* Board events between passes; returns 0 ok, 2 unsupported boundary, 1 eof/error. */
static int run_events(unsigned after_pass, int apply)
{
    char line[160];
    while (fgets(line, sizeof line, events)) {
        unsigned pads, aux, system, dips, polls, value, address;
        int busy = -1;
        if (line[0] == 'P') return 0;
        if (!apply) continue;
        if (sscanf(line, "R %x", &value) == 1) world_frame_sound_reply(ram, (uint16_t)value);
        else if (sscanf(line, "I %x", &value) == 1) {
            if (!world_frame_busy_vblank(ram, (uint16_t)value)) {
                printf("pass %u: unsupported busy VBlank between passes\n", after_pass);
                return 2;
            }
        } else if (sscanf(line, "W %x %x", &address, &value) == 2) {
            ram[address & 0xffff] = (uint8_t)value;
            if (strlen(line) > 9 && line[9] != '\n' && line[9] != '\r') ram[(address + 1) & 0xffff] = (uint8_t)(value >> 8);
        } else if (sscanf(line, "B %x %x %x %x %u %d", &pads, &aux, &system, &dips, &polls, &busy) >= 5) {
            int irq_sound, master_sound;
            unsigned master = word(ram + 0x80);
            world_palette_step(ram, palette, rom);
            if (!world_frame_boundary_irq(ram, tiles, oam, palette, rom, (uint16_t)pads, (uint16_t)aux,
                                          (uint16_t)system, (uint16_t)dips, polls, busy, &irq_sound, &master_sound)) {
                printf("pass %u: unrecovered board boundary at master %04X\n", after_pass, master);
                return 2;
            }
        } else {
            printf("pass %u: bad event line %s", after_pass, line);
            return 1;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *rom_path = NULL, *dump_dir = NULL;
    unsigned from = 1, limit = 0;
    int keep_going = 0, verified = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--from") && i + 1 < argc) from = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--passes") && i + 1 < argc) limit = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_path = argv[++i];
        else if (!strcmp(argv[i], "--dump-on-stop") && i + 1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--keep-going")) keep_going = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) { trace = fopen(argv[++i], "rb"); if (!trace) { fprintf(stderr, "cannot open trace\n"); return 1; } }
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--verified")) verified = 1;
        else if (!dir && argv[i][0] != '-') dir = argv[i];
        else { fprintf(stderr, "usage: check_replay REFDIR [--from PASS] [--passes N] [--rom FILE] [--dump-on-stop DIR] [--keep-going]\n"); return 1; }
    }
    if (!dir || from < 1) return 1;
    char path[4096];
    if (!rom_path) rom_path = ".local/world/maincpu.bin";
    if (!read_file(rom_path, rom, sizeof rom)) { fprintf(stderr, "cannot read %s\n", rom_path); return 1; }
    snprintf(path, sizeof path, "%s/passes.bin", dir); passes = fopen(path, "rb");
    snprintf(path, sizeof path, "%s/hardware.txt", dir); events = fopen(path, "r");
    snprintf(path, sizeof path, "%s/irqs.txt", dir); irqs = fopen(path, "r");
    if (!passes || !events) { fprintf(stderr, "missing passes.bin or hardware.txt in %s\n", dir); return 1; }
    char magic[16];
    if (!fgets(magic, sizeof magic, events) || strncmp(magic, "WH1", 3)) return 1;
    if (!load_record(from)) { fprintf(stderr, "pass %u not in recording\n", from); return 1; }
    memcpy(ram, ref + HEADER, WORK);
    memcpy(tiles, ref + HEADER + WORK, VIDEO);
    memcpy(oam, ref + HEADER + WORK + VIDEO, OAM);
    memcpy(palette, ref + HEADER + WORK + VIDEO + OAM, PAL);
    for (unsigned p = 1; p < from; ++p) if (run_events(p, 0)) { fprintf(stderr, "recording ends before pass %u\n", from); return 1; }
    next_irq();
    while (irq_pass && irq_pass < from) next_irq();
    resync_calls = keep_going && trace;
    unsigned pass = from, matched = 0, mismatches = 0, boundary_stops = 0;
    int status = 0;
    for (;;) {
        if (limit && pass >= from + limit) break;
        if (!load_record(pass)) break;
        WorldDispatch d;
        world_dispatch_begin(&d, ram);
        size_t diff = compare(pass, !quiet || !keep_going);
        if (!diff && pass_whole)
            for (unsigned i = 0; i < pass_call_count; ++i) credit(pass_calls[i], 1);
        pass_whole = 0;
        if (diff) {
            ++mismatches;
            if (!keep_going) { status = 3; break; }
            memcpy(ram, ref + HEADER, WORK);   /* resynchronise and continue */
            memcpy(tiles, ref + HEADER + WORK, VIDEO);
            memcpy(oam, ref + HEADER + WORK + VIDEO, OAM);
            memcpy(palette, ref + HEADER + WORK + VIDEO + OAM, PAL);
        } else ++matched;
        uint32_t stop = 0; uint16_t actor = 0; unsigned calls = 0;
        unsigned master = word(ram + 0x80), stage = word(ram + 0x84);
        int r = run_pass(pass, &stop, &actor, &calls);
        if (r == 5) { status = 3; break; }
        if (r == 2 && keep_going) {
            /* Tally the gap, skip this pass and resynchronise at the next one. */
            note_missing(stop, pass);
            if (run_events(pass, 0)) break;
            ++pass;
            if (!load_record(pass)) break;
            memcpy(ram, ref + HEADER, WORK);
            memcpy(tiles, ref + HEADER + WORK, VIDEO);
            memcpy(oam, ref + HEADER + WORK + VIDEO, OAM);
            memcpy(palette, ref + HEADER + WORK + VIDEO + OAM, PAL);
            while (irq_pass && irq_pass < pass) next_irq();
            continue;
        }
        if (r == 2) {
            printf("pass %u (frame %u, master %04X, stage %u): unrecovered handler %05lX for actor %04X after %u calls\n",
                   pass, word(ref + 4) | (unsigned)word(ref + 6) << 16, master, stage, (unsigned long)stop, actor, calls);
            if (dump_dir) {
                write_file(dump_dir, "stop-work", ram, WORK);
                write_file(dump_dir, "stop-video", tiles, VIDEO);
                write_file(dump_dir, "stop-oam", oam, OAM);
                write_file(dump_dir, "stop-palette", palette, PAL);
            }
            status = 2; break;
        }
        if (r) { printf("pass %u: dispatcher failure %d\n", pass, r); status = 2; break; }
        int e = run_events(pass, 1);
        if (e == 2 && keep_going) {
            /* The rest of this interval was not consumed: skip to its P line. */
            ++boundary_stops;
            if (run_events(pass, 0)) break;
            ++pass;
            if (!load_record(pass)) break;
            memcpy(ram, ref + HEADER, WORK);
            memcpy(tiles, ref + HEADER + WORK, VIDEO);
            memcpy(oam, ref + HEADER + WORK + VIDEO, OAM);
            memcpy(palette, ref + HEADER + WORK + VIDEO + OAM, PAL);
            continue;
        }
        if (e == 2) { status = 2; break; }
        if (e) break;
        ++pass;
    }
    unsigned skipped = 0;
    for (unsigned i = 0; i < missing_count; ++i) {
        printf("missing handler %05lX: %u passes, first pass %u\n", (unsigned long)missing[i].address,
               missing[i].count, missing[i].first_pass);
        skipped += missing[i].count;
    }
    if (verified)
        for (unsigned i = 0; i < tally_count; ++i)
            printf("verified %05lX: %u calls in matching whole passes, %u by trace\n",
                   (unsigned long)tally[i].address, tally[i].whole, tally[i].traced);
    if (resync_calls)
        printf("trace: %u handler results compared, %u differed, %u missing handler calls stepped over in %u passes\n",
               trace_calls_checked, trace_mismatches, trace_skips, trace_skipped_passes);
    printf("%s: compared passes %u..%u, %u matched, %u mismatched, %u stopped at missing handlers, %u at boundaries\n",
           dir, from, pass, matched, mismatches, skipped - trace_skips, boundary_stops);
    skipped -= trace_skips;
    return status ? status : (mismatches || trace_mismatches ? 3 : skipped || boundary_stops ? 2 : 0);
}
