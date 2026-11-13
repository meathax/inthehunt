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
