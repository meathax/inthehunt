# Verification record

These are the existing project reports, preserved from the README. They have not been rerun or independently requalified by the release-documentation audit. Bind final release claims to the selected source commit and executable hash.

## Verification

The game's behaviour was tested with scripted headless runs that drive real inputs through the native executable:

- An autopilot playthrough from power-on ran **620,000 frames** (several complete loops of all six stages and bosses) with no stops.
- Five consecutive full loops (2,000,000 frames from a saved state), both endings, and a **two-player co-op** run of 120,000 frames all completed with no stops.
- Regression modes (`steer`, `adv`, `dive`) are rerun after each translation batch.
- The pause menu was driven live: open, change and save volume, resume and quit. The title menu's Start Game and Exit to Menu are covered by an off-screen self-test (`WORLD_TEST_TITLE=1`).
- A crash hunt (`tools/crash_hunt.py`) resumes a snapshot of every stage of two loops under nine input styles the autopilot never produces (nobody playing, never continuing, ramming without firing, random input, a second player idling or dropping out): 208 runs of 30,000 frames, no stops.

Earlier in the project, encounter replays were compared with MAME's reference execution, and in the measured windows RAM, sprite, tile, palette and sound-command state matched. See the developer tools below.

### Reactive-effects boss qualification

`python -X utf8 tools/qualify_waterfx.py --snapshot <snapshot> --script <headless-script>` runs a serial OFF/ON/ON/OFF comparison at native and 3× compositor resolution. It excludes 120 warmup frames, requires a live stage-three boss witness, checks that all four effects emit, and compares RAM/video/OAM/palette snapshot hashes. Inputs must end with an absolute-frame `end` command. Results describe CPU scene rendering (two display refreshes per game frame), not GPU/CRT presentation.

Test hooks: `WORLD_SCENE_RUN=1` or `3`, `WORLD_SCENE_REACTIVE=0` or `1`, and `WORLD_SCENE_STATS=<csv>` select compositor scale, override only the four reactive switches, and record per-refresh timing, boss state, waterline and effect counts.

The November 2026 replay caught stage-three mode 4 being incorrectly treated as all water. Its static sea boundary is now recognised at background row 168, including vertical-scroll adjustment. Regression tests cover this mode and the previous ripple modes. The replay produced natural crossing splashes and matched gameplay state with the effects on and off; timings and capture provenance are retained under `.local/boss-review/`.

Final source-matched boss replay: 1,201 measured boss frames per run after warmup, eight serial runs. All enabled runs emitted 609 wakes, 231 debris fragments, 55 silt clouds and 36 splashes. Above-water spray was confirmed by on/off pixel differences across 74 captured frames; it now clears the native foam while leaving sprites and the HUD protected. At 3×, the mean of the two run p95 CPU scene times was 9.99 ms with the four effects off and 9.99 ms on (two refreshes per game frame). This is one repeatable boss sequence, not a whole-game or GPU timing guarantee.

### Headless release qualification (13 November 2026)

The current source was checked without opening a gameplay window. A cold-boot
620,000-frame run traversed all six stages twice and entered a third loop;
a separate 120,000-frame run exercised two-player inputs. Both reached their
requested final frame without a stop. The normal ending and credits were
captured from a final-boss snapshot. Explicit stage-clear fixtures separately
exercised both ending branches; the true-ending epilogue reached bonus level 3
with both players present. This branch test is not a two-player playthrough
from power-on to that ending.

Off-screen Direct3D 12 tests rendered OFF plus all twelve CRT presets. Menu,
title, scaling and border-layout captures were inspected. Every enhancement
was rendered individually in six stage scenes; disabling enhancements matched
the raw arcade image pixel-for-pixel. The first stage was also inspected during
active gameplay. Native sound synthesis and driver dispatch ran throughout the
headless tests, but no physical audio device was opened.

A freshly extracted Windows ZIP passed ROM-chip hash verification, fresh import,
cache validation, a 3,600-frame gameplay replay and all CRT preset renders using
its packaged runtime. Original game data is excluded from both release ZIPs.
Detailed local receipts and captures are under `build/release-qa-20261113/`.

This is headless release-candidate qualification. Actual monitor pacing,
fullscreen/focus transitions, audible mixing, rumble and controller hot-plug
still need an interactive hardware session. The fidelity limits above remain:
these checks do not establish cycle-exact timing, bit-identical audio or full
gameplay equivalence to MAME. Additional effects are not required for release.

The final crash matrix completed **156 runs of 30,000 frames** across every
stage in two loops, with zero stops and every run reaching its requested end.
One run ended one VBlank beyond the target because a slowdown pass advances
multiple ticks before the script checks its end command. Seventeen runs used
known register estimates (last reported reason: sixteen no-player surface-enemy
facing decisions and one ice test using leftover CX). These remain fidelity
limits, not new translation coverage claims. Replays now use stable random
seeds and retain inputs, logs, snapshot hashes and final-frame receipts.

## Fidelity details

- **Arcade slowdown is a fitted model, not cycle-exact.** The native code runs instantly, so each logic pass is charged an estimate of the original V33 time and the game misses VBlanks when that exceeds a frame, as the PCB does. The estimate comes from 157,503 passes of the original program measured in MAME (see [Slowdown model](DEVELOPMENT.md#slowdown-model)); held-out passes are predicted to within 1.4 % of a frame on average (r² 0.978), and about two thirds of real overruns are caught. Individual heavy frames can still differ. Remove Slowdown is unaffected.
- **No whole-game frame-by-frame MAME comparison.** MAME 0.289 can run the set rebuilt from the imported chips (the six PLD files are not emulated, so placeholders of the right size are enough; MAME only warns about their checksums), and it was used to measure the slowdown model, but a full lockstep comparison of every frame has not been done.
- **Audio** follows the original driver and chip models, but its samples were never shown to be bit-identical to the reference.
- **A few rare branches estimate a register value.** Some original code paths read a CPU register left over from earlier, unrelated code (for example, scoring an enemy destroyed by something other than a shot, or a turn decision when no player is in play). Where the port does not track that value it continues with a neutral one and counts it (`Register estimates` in headless output); `WORLD_STRICT=1` stops there instead, for translation work.
- Only the World set (`inthunt`) is supported. *Kaitei Daisensou* and the US set are not.
