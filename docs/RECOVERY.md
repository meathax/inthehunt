# Recovery workflow (developer reference)

How to recover an untranslated routine and prove it against the original
program. Everything here is a development aid; none of it ships.

## Tools (Linux, from the project root)

| Command | Purpose |
|---|---|
| `tools/ref/run_capture.sh DIR 'opts'` | Run the World set in MAME headless and record a reference into `DIR` (keep it under `.local/ref/`). |
| `make -f tools/ref/build.mk` | Build `.local/bin/check_replay` from the native core. Add `CFLAGS='-std=c99 -O1 -g -Wall -Wextra'` for faster builds. |
| `.local/bin/check_replay DIR [--from P] [--passes N] [--keep-going --quiet] [--trace DIR/trace.bin]` | Run the native handlers over a recording. Exit 0 = every compared pass matches the original byte for byte. |
| `... --keep-going --quiet --verified --trace DIR/trace.bin` | Inside traced passes a missing handler is stepped over using the reference state before the next call, so the handlers after it are still compared one by one. `--verified` lists per handler how many calls were compared and matched. |
| `tools/ref/dasm.sh ADDR LEN` | Disassemble main-CPU code (hex linear address and length) with MAME's NEC V33 disassembler. |
| `python3 tools/ref/ledger.py` | Rebuild `docs/COVERAGE.md` from all recordings under `.local/ref/`. |

Capture options (Lua table body): `frames=N` (stop frame), `start=N` (first
recorded frame, default 600), `stage=N` (start the campaign at stage index N),
`refill=true` (top up P1 lives), `passes=false` (call log only, tiny output),
`trace_pass=P, trace_count=M` or `trace={{P,M},{P,M}}` (dump state before every
handler call of those passes; about 11 MB per pass in a busy stage), `snap_every=N` (PNG every N passes), `bot=false`, `follow=true` (the bot holds
up or down through vertical scroll sections), `steer_from=F, steer_x=X` (from
frame F the bot keeps P1 near actor X instead of pushing right). One pass with full
state costs 135 KB, so keep a recording to a few thousand passes and delete
recordings you no longer need.

## The loop

1. `check_replay REC --keep-going --quiet` lists every missing handler with
   its first pass. Without `--keep-going` it stops at the first problem.
2. Disassemble the handler. Follow its near/far calls. Translate it into a
   `case 0xNNNNN:` in the stage file that owns it (`src/world_stageN.c`, or
   `src/world_common.c` for player, weapon, pickup and effect actors).
3. Rebuild and rerun. A mismatch is reported at the next pass start. To find
   the handler responsible, recapture with `trace_pass=P` and rerun with
   `--from P --passes 1 --trace REC/trace.bin`.
4. A handler counts as recovered only when the passes that execute it match.
   Never weaken the comparison or special-case the checker.
5. When a pass also runs handlers that are still missing (another stage's
   work), capture sparse trace windows over the passes of interest
   (`trace={{P,M},...}`) and check with `--keep-going --verified --trace`.
   A sound reply that arrives inside a stepped-over multi-frame routine is
   already part of the reference state and is then applied a second time at
   the boundary; expect one false mismatch after such a routine.

## Conventions in the recovered code

- `ram` is work RAM at E0000 (64 KiB); `tiles` is video RAM at D0000;
  `main_rom` is the 1 MiB program image. All words are little-endian; use
  `read_word`/`write_word` from `world_dispatch_internal.h`.
- An actor is a 128-byte record in work RAM addressed by BP. `[bp+0]`/`[bp+2]`
  hold the far pointer of its next handler; writing a new offset to `[bp+0]`
  is how a routine advances its state. `[bp+4]` links to the next actor.
- A handler entry is the linear address `segment*16 + offset`. Bank B000
  routines read tables through `main_rom + 0xb0000`.
- `break` from a case means the handler returned normally (the dispatcher
  then links to the next actor). `return WORLD_HANDLER_STOP` marks a path
  that is not recovered; the game reports it instead of guessing.
- Reuse the shared helpers (`queue_sprite`, `move_actor_axis`,
  `world_queue_sound`, `world_spawn_child`, contact and terrain routines)
  whenever the original calls the same subroutine. Check by address, not by
  resemblance.
- Scene controllers (masters, RAM[80]) and the VBlank boundary live in
  `src/world_frame.c`, `world_title_master.c` and `world_gameover.c`.
- Known fixed addresses: master RAM[80], stage index RAM[84], pass counter
  RAM[274], board tick RAM[276], P1 lives RAM[118], active players RAM[264],
  score record pointers RAM[254]/[256], RNG state RAM[B74E]/[B750].

## Rules

- ROM bytes, memory dumps, traces and screenshots stay under `.local/`.
  Tables are read from the user's ROM image at run time, never copied into
  source.
- Timing: `check_replay` verifies state, not cycle timing. Arcade slowdown is
  a separate, approximate feature.
