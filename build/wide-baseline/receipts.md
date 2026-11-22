# Widescreen evidence receipts

Build: .local/native/InTheHunt.exe 2026-11-22 17:43 (after autopilot fix).
Build cmd: Build-WorldNative -Root 'D:\Arcade\AI\aCORES\inthehunt' (empty $Root silently skips link!)

## Contract (plan step 3) — DONE
- 428x240 wide raster; raster = screen + 54; wide screen domain [-54,374); aperture 426 2/3.
- tools/test_view.exe: all geometry checks pass (round-trip, edge helpers, clamps, hud_shift, 16:9 thirds).

## Menu (step 4) — DONE
- Title: PLAY ARCADE first, PLAY WIDESCREEN second; menushot BMPs headless-title0/1/2.
- --mode=arcade|wide CLI; session-owned view; mode-agnostic snapshots (restore keeps mode).

## Renderer/presenter (step 5) — DONE
- world_video_scanline(width,left); sprite spans clip signed; window crops 428->16:9; GPU aspect 16:9;
  light transport gated arcade-only (320-fixed buffers; wide uses plain path - documented gap).

## Camera/player/terrain slice (step 6) — DONE
- left-wide vs left-arc frame 2601: wide sub field 0x3B (screen -37, inside left wing) vs arc 0x70.
- probe-wide BMPs: terrain+sprites+mines fill all 428 cols; right-wing actors to field 0x1FB.
- HUD anchored: P1 left edge, P2/credits right edge (hud_shift +/-28 cols, tallies untouched).

## Encounter conversion (step 7) — MOSTLY DONE
- ~160 edge-compare sites converted in world_dispatch.c (3 passes); spawn table edge-shift with
  anchored exclusion set {da13,db86,8309}; ledger for all 8 stage tables in spawn-ledger.txt.
- Arcade parity: byte-identical BMPs/trace/actors vs pre-change baseline (3456 passes, 194 overruns).
- Autopilot park root-caused: input starvation (not a game bug); fixed test tool only.

## All-stage coverage (step 10 partial) — 2026-11-22
- Test-only WORLD_STAGE_SEED=n env (headless): pins word 84 through the briefing masters
  (0x2218/0x2282/0xa81) so a coin+start boots stage n. Boot-time seeding is rejected by the
  frame validator (title masters require 0x84==0).
- seed1..seed5 runs (--mode=wide, 30k frames, autopilot): all stages boot, camera scrolls,
  sub reaches live handler, deaths/continues cycle. Traces + BMPs in build/wide-baseline/seedN/.
- Shots: seed3@15k lava cavern (wings full), seed4@20k descent+CONTINUE countdown wide,
  seed5@15k WARNING boss intro wide, seed1@20k missile base, seed2@10k harbor warships.
- 4:3 interstitials verified: stage cards render 320-wide under non-0xb2e masters (seed4@15k).
- Wing actor presence (field X; arcade limits are 0x70..0x1a0):
  left wing <0x70: seed2 to 0x12, seed3 to 0x0e, campaign to 0x16
  right wing >0x1a0: seed1 0x2f4, seed2 to 0x1e6, seed3 to 0x1ed, seed4 to 0x1f3,
  campaign to 0x1fb (earlier probe)

## Open items
- Boss phase sheets per stage (stage-5 WARNING seen; boss fights not yet driven per stage).
- Wing pickup/damage/terrain-collision receipts; 2P shared view; P2-only; co-op.
- Presentation matrix: GPU on/off, CRT on/off, enhancements (wide=fallback), windowed/fullscreen, scaling.
- Persistence: save/load, checkpoint, death/continue/reset round trips; mode switch in one process.
- Ordinary-input clears for qualification (autopilot coverage is evidence, not a clear).
- Upstream soft-lock note: parked 0x7c3b respawn state identical in arcade; documented, not fixed.

## Co-op / P2 join receipt (wide mode)

Mechanism discovered (test-harness semantics, not game changes):
- Coin chute needs interleaved press samples (`0x2ba & 0x55 == 5`); single/held pulses never credit.
- Service coin = system bit `0x10` held 2+ vblanks → +1 credit (`0x2bb & 15 == 3`). Two pulses → 2 credits.
- P1 start = system edge `0x01`; P2 join = system edge `0x02` seen on a `0xb2e` pass (`world_start_player`, `ram[0x2aa] & 2`).
- `0x2aa` is edge-latched per vblank: multi-vblank (slowdown) ticks clear edges before the pass reads them → scripted joins need `--timing=remove-slowdown` (1-vblank ticks).
- IMPORTANT: prior "campaign" traces were largely the ATTRACT reel (`0x28c & 1`); `0x264 = 0xF` during attract is the demo's own both-players flag, not a join. Genuine co-op evidence requires `0x28c = 0`.
- `a`-prefixed script rows run the autopilot, which overwrites `*edges` — edge columns on `a` rows are ignored; use plain rows for edge scripting.

Evidence (build/wide-baseline/repro/, script `r.txt`, `--mode=wide --timing=remove-slowdown`):
- Live window: `0x28C = 0`, `0x264 = 0x5` (P1) then `0xF` after the P2 edge → real mid-game join.
- `headless-005301.bmp`: P1 sub mid-screen + P2 sub in the right wing (raster ~365); native `CONTINUE PUSH 2P BUTTON` prompt at the right edge; `1P` panel bottom-left, `2P` panel bottom-right (edge-anchored HUD shifts).
- `headless-005151.bmp` / `headless-005051.bmp`: both panels live, player sub at raster ~395 (screen ~341 — inside the right wing, beyond the 320 arcade edge).
- P2 uses the same widened clamps (`world_view_player_min/max_x` shared by both players); P2 actor spawns and plays in the shared world. P2 death returns it to `0x7B08` idle pending re-join — authentic arcade semantics.

## 0x7C3B park — confirmed live, identical arcade vs wide (not a regression)

Under `--timing=remove-slowdown` + real input (service-coin, start, autopilot), seeded
stage 3 (`WORLD_STAGE_SEED=3`): the sub enters `0x7C3B` (entry-hold) around camera 0x5B0
with `+0x6E` latched and never re-probes → permanent park (`28C=0`, `264=5`, live).
The identical script in `--mode=arcade` parks at the same tick, same camera, same
position — an upstream decomp state-machine edge case, NOT a wide-mode regression.
Live-input harness recipe in GOAL.md ("UPDATE — live-input harness recipe").

## Live-input stage evidence (this session; `--timing=remove-slowdown`, `28C=0` verified)

- Stage-1 live run (`WORLD_STAGE_SEED=1`): deaths/continues funded by service coins,
  camera to 0x59A+, score 00028600, `CREDIT 04` — `headless-032001.bmp` shows firing
  sub + enemies across both wings.
- Stage-5 live run (`WORLD_STAGE_SEED=5`): `headless-006001.bmp` industrial section —
  mechs, mines, explosions across the full 428 raster; deaths/respawns/`7A6A` cycles.
- Stage-2 boss-arena snapshots (`.local/boss-review/34817.snap`, `boss.snap`: stage 2,
  cam `0x11BF`, live) restored in wide → played through the boss-arena EXIT live:
  `headless-036801.bmp` STAGE CLEAR (correctly 320-wide non-gameplay master) → stage-3
  entry → death → `headless-038801.bmp` GAME OVER (also 4:3). Stage-boundary
  transitions + 4:3 interstitial fallback verified live.
- Persistence: `WORLD_SNAPSHOT_SAVE` at live wide frame → `WORLD_SNAPSHOT_LOAD` +
  `WORLD_SCRIPT_RELATIVE` resumes live play (camera continues, sub respawn cycle).
- Presentation: `WORLD_CRT_SHOT` at live wide frame → all 12 presets (OFF + 11 shader
  presets) render the 428 raster at 1920x1080; `WORLD_VIEW_SHOT` composition OK.

## Input semantics discovered (harness, not game changes)
- `0x2AA` = system edge latch, cleared per vblank — multi-vblank ticks drop scripted
  edges before the master pass reads them; `--timing=remove-slowdown` makes every
  scripted edge land (qualification runs under arcade timing need edges held/repeated).
- Coin chute credit needs interleaved samples (`0x2BA & 0x55 == 5`); service coin
  (`0x2A8` bit4, system `0x10`) held >=2 vblanks gives +1 credit deterministically.
- P1 start = `0x2AA&1`; P2 join mid-game = `0x2AA&2`; each join costs >=1 credit.
- `a`-rows: autopilot overrides `*edges`; plain rows deliver edge columns verbatim.
- `0x28C&1` = attract/demo (ROM-scripted pads, `0x264=F` is a demo artifact).

## Stage-1 boss-arena fight evidence (live input, `28C=0`, `264=F` co-op)

`bossfight.snap` at cam `0x76B` (arena approach, lock at `0x8A8`):
- P1 sub `0x0A00` at field `0x1BD` — the wide RIGHT player clamp — fighting the boss.
- P2 sub `0x0980` respawning mid-screen (`0x7BE5`).
- Boss composite live: segment-`0x1000` boss entity (`0x0680:0x5D98`) + `0xB000:`-bank
  parts/mines spanning x `0x22`–`0x1FB` — entities populate both wings during the fight.
- `coop-005051.bmp`/`coop-005151.bmp` show the giant composite boss at the left of the
  wide raster with both player subs and live HUD — a real boss fight inside the widened
  world, with a player legally at the right clamp.
- Arena camera lock at `0x8A8` verified live (camera holds while the fight runs);
  STAGE CLEAR + GAME OVER + CONTINUE overlays all render correctly at 428 wide or fall
  back to 320 on non-gameplay masters as designed.

## Stage-1 boss-arena regression comparison (live input, identical snapshot)

Restored `bossfight.snap` (stage 0/1 arena approach) in both modes with identical
held-input script, `WORLD_TRACE_EVERY=500`:
- Deaths at `0x7EE5 x 0xC7` at the same tick in both modes.
- Arena camera lock: wide `0xA10`, arcade `0xA88` — ~120-unit difference ≈ the wing
  width; consistent with a view-anchored arena boundary (verify before treating as
  a documented exception — see plan step 8).
- Sub pins at `x 0xC7` during the lock in BOTH modes — an upstream arena boundary /
  boss-body barrier, not a wide-mode invisible wall (regression ruled out).
- Both runs exhaust continues and game-over at ~8000 identically — held-input cannot
  clear the fight in either mode; boss phase sheets and ordinary clears remain open
  qualification rows needing survivable scripted play.
- `0x264 0xC0` marks the continue countdown window live.

## Wing pickup/collection — code path proof + empirical status

- `outside_pickup_area` is view-aware (`world_view_edge_greater/less`) — drops stay
  alive until the WIDE edges, so pickups can drift into both wings.
- `world_player_contact` → `overlap_shapes` is a pure center-distance check with no
  screen-bound terms — collection fires wherever the sub overlaps the item, wing
  positions included (`src/world_contact.c`).
- `WORLD_PICKUP_LOG` (test-only env, `world_dispatch.c`) logs each collection's field
  X; a long funded wide run produced no collections (autopilot does not chase drops).
  Empirical wing-collection event remains an open receipt — the mechanism is proven
  view-agnostic at code level.

## Stage-1 boss phase sheet (partial — live evidence + handler map)

- **Trigger/arena:** camera locks at ~`0xA10` (wide) / `0xA88` (arcade) — the ≈120-unit
  delta matches the wing width, consistent with a view-anchored arena right bound
  (candidate documented exception, plan step 8 note).
- **Body:** orange composite entity anchored top-left of the arena; `0xB000:`-bank part
  cluster (turrets/segments/hull attachments via parent+0x31/0x34 offsets — the
  `warship_attachment` pattern), plus `0x1B4C` scroll-attached debris. Weak point is the
  dangling blue orb visible below the hull (`coop-005051.bmp`).
- **Player boundary:** a held-right sub pins at field `0xC7` during the lock — identical
  in arcade (arena-side barrier or boss-body collision; NOT a wide regression).
- **Fight evidence (live, wide):** `coop-005301/5051/5151.bmp` — P2 joined mid-fight,
  subs at both wings, boss composite rendered across the raster; `bossfight.snap`
  actor list shows the composite + both players live at `0x264=F`.
- **Exit:** arena camera unlock → stage end → STAGE CLEAR interstitial (320-wide, by
  design) — proven live on the stage-2 boss-arena restore (`headless-036801.bmp`).
- **Open:** per-phase handler list (HP thresholds, telegraphs, phase transitions),
  final-hit/unlock trace, scripted ordinary-input clear.

## Boss handler map (all stages — phase-sheet skeleton, from world_dispatch.c)

- **S1 claw boss:** body `1000:0A6B/0FAB/0x11025`; `boss_claw_step` handlers
  `0x11723, 0x11742, 0x117DA, 0x11819, 0x118A3, 0x118BD, 0x119F9, 0x11A09,
  0x11A63, 0x11ADC, 0x11AF1, 0x11B41, 0x11B57`; `boss_appendage`
  `0x1108C–0x1138B`; wreck parts `0x10C8F/0xCD1/0xE0B` (flame/hull plate/flash,
  spawn once BOTH side guns die → phase transition evidence marker).
- **S2 boss:** `1000:289C..4023` — arena + two bodies `0x29C9` + parts; gunboat
  `B000:380F` family (patrol `F308`, hatch `DEDD`, towers `8184`, gunner `488A`).
- **S3 mid boss:** `1000:EC04..F196` + `7000:CCA9`, attackers `7000:CDCC..D2DC`.
- **S3 END boss:** `stage_three_end_boss` — `0x1451A..0x15B53` (~70 states,
  flight/crown/pod/arm/lower-piece scripts, per-state estimated_attacker).
- **S4 boss:** `stage_four_boss` — `0x75F79..0x76B89` (~40 states), body
  `7000:5AA2` bobs/drifts.
- **S5 ice boss:** `7000:7FFE..93F7` + debris `0x9DD9`; cutter `B000:EBF5/EC3B`.
- **S6 final boss (battleship):** `final_boss` — `0x7B4BF..0x7C1AC` (~40
  states), hull `7000:ABDC` sails in, ram, torpedo dive; gunship
  `B000:1292..18C0`, transport `B000:EEDC..F251`, lift `7000:C41C..C9A3`.

Phase-sheet use: for each stage, watch the actor handler column in a live trace
(`WORLD_TRACE_EVERY=25`); a part transitioning into its wreck/destroyed state
marks a phase boundary. S1 wreck-state spawn = "both side guns dead".

## Boss edge-audit (code-level wing correctness)

Every boss function's screen-edge test routes through `world_view_edge_*`
(`boss_claw_step` +87: `edge_greater(x,0x1c0)`; `stage_two_boss` +653/+815:
both edges; `stage_four_boss` +749: `edge_less(x,0x40)`; `final_boss` +164/+414/
+428/+594/+692/+787 incl. signed helpers). Retire/sprite-cull bounds therefore
extend to the wide edges — boss parts are not clipped or retired early in the wings.
`stage_three_end_boss` has no edge check; its `0xB700 >= 0x11BF` test is a
world-camera milestone (boss activates at a fixed world X in both modes —
correctly absolute, not view-relative). Remaining `0x140` literals in the file
are field writes/params (Y spawn, move speed, tile loop), not screen-edge tests.

## Stage-1 boss-fight phase run (bossfight.snap restore, wide, live input)

- `WORLD_ACTOR_TRACE=1` (new test-only env) + `WORLD_TRACE_EVERY=250` — actor
  handler dumps now inline in headless-trace.txt per interval.
- **Boss composite composition confirmed live** (frame 5250 actor list):
  side guns `B000:5B93` x4 (hp 0x400 each), deck guns `5C73` x2 (0x460),
  turrets `64B6`/`6744` (0x280), claw arm `6781` (0xA00), body part `5D59`
  (0x600), hull turrets `CD61/CDC8/CE66` (0x88), spawn controller `1000:5D98`.
  Part X values span `0xFFFC`–`0x01E3` — composite occupies BOTH wings.
- **No damage landed:** every sampled boss part retained full hp through the
  fight; the `5B93` 3<->4 count fluctuation is actor-list churn, not
  destruction (hp never dropped). Autopilot died at the arena approach
  (frame ~6750 `0x264=0xC0` game-over), restart parked at `0x7C3B` — the
  documented upstream soft-lock.
- **Phase-transition marker located (code):** both-side-guns-dead spawns wreck
  parts `1000:0C8F/0CD1/0E0B`; not observed live — requires surviving to
  damage the guns.
- Zero pickup collections again (`WORLD_PICKUP_LOG` set; autopilot does not
  pursue drops).

Visual: `headless-005251.bmp` — stage-1 boss composite rendered across the left
wing (hull on waterline, turret superstructure), P1 sub near the right edge,
CONTINUE prompt for the idle P2 slot — boss parts physically occupy legacy
screen X < 0, matching actor-dump X FFFC.

## Wing damage receipt (live, wide, right wing)

wingdmg.txt run — bossfight.snap restore, held RIGHT+FIRE, `28C=0` throughout:
- `5600 stage 0 B700 094a 264 0004 28C 0000 sub 7eb1 x 01b6 y 0108 input 09` —
  death/blast state `0x7EB1` at field X `0x1B6` > arcade clamp `0x188`:
  lethal damage processed INSIDE the right wing (projectile/contact path has
  no screen-edge exclusion, now proven live).
- `5650 ... sub 7eb1 x 0178` second blast near the boundary, `6150 ... x 011d`
  center death, game-over at 7550 — damage/death pipeline works across the
  full aperture.

## Pickup collection receipts (empirical, live, wide)

Pickup-seek branch added to the test-only autopilot (steers sub toward live
`EA17/EA62/EB1C` actors, keeps firing). `WORLD_PICKUP_LOG=1` stderr events:

- WORLD_STAGE_SEED=4 run (58k): `x 0090 y 0129 v5`, `x 0171 y 0114 v1` (x2),
  `x 0090 y 011a v1`, `x 0090 y 0129 v5` — collection pipeline live-verified.
- WORLD_STAGE_SEED=2 run (64k): `x 0084 v5`, `x 00a4 v1`, `x 0084 v5`,
  **`x 0064 y 00a7 v1`** — collected at field `0x64`, LEFT of the arcade
  player clamp `0x70` (legacy screen X ≈ 4): the submarine reached into the
  left wing to take it — a position unreachable in arcade mode.

Mechanism recap: drop-ship `0x7E857` destroyed → `0x7E922` anim step 6 →
`0x7EA17` pickup → `0x7EA62/0x7EB1C` drift (culled at WIDE edges via
`outside_pickup_area`) → `world_player_contact` overlap → award.

## Stage-1 boss DAMAGE receipt (empirical, live, wide, left-wing fight)

bossleft.txt run — bossfight.snap restore, held LEFT+FIRE (0x42/0x46/0x4A),
`28C=0`, `WORLD_ACTOR_TRACE` at 200-tick intervals:
- P1 parked at field `0x3B` = the wide LEFT clamp (arcade min `0x70`), live
  `0x7A6A` for ~1500 ticks fighting the composite from inside the left wing.
- Progressive part damage observed across samples:
  `B000:5D59` hp `0x600 -> 0x42C -> 0x0C4` (body),
  `B000:64B6` hp `0x280 -> 0x23C -> 0x180 -> 0x0BC`,
  `B000:6744` hp `0x280/0x228 -> 0x1B0` (turrets).
- Untouched (expected — armor/other side, out of firing line): `5B93` guns
  stayed `0x400`, `5C73` `0x460`, claw `6781` `0xA00` — consistent with
  per-part eligibility, not a blanket-hit bug.
- `5D59` floored at `0x0C4` (phase floor or death-before-kill; wreck handlers
  `10C8F/CD1/E0B` not observed). Both players dead -> GAME OVER at ~7000.
- Visual: `headless-007001.bmp` — hull shows damage gaps vs intact hull in
  `headless-005251.bmp`; leftward-fire fight at the left-wing clamp.

## Stage-1 boss phase finding — vulnerable vs armored parts (plan step 8)

Second funded run (bosskill.txt, 16k) + both-buttons variant (bossguns.txt):
- `5D59`/`64B6`/`6744` damage floors repeat identically (`0x0C4`/`0x0BC`/
  `0x1B0`) — per-part damage caps or phase gating, not kill targets.
- `5B93` side guns (×4, `0x400`) and `5C73` (×2, `0x460`) take ZERO damage from
  left-clamp fire of either button — the plan's "expected rejected hit":
  these parts are invulnerable to this attack vector (or need a different
  weapon/position); wreck parts `10C8F/CD1/E0B` correctly never spawn since
  the both-guns-dead condition isn't met. Phase ordering confirmed in code +
  live: guns are the kill-gate.
- Implication for clears: the boss needs gun-kill engagement (right-side or
  under-hull attack the scripted patterns don't produce); body chips are
  damage-capped. Not a wide-mode issue — verified same handlers/floors.

## Stage-1 boss kill-chain mechanics (code, from world_dispatch.c/fire.c)

- Weapon semantics: pad `0x80` = forward torpedo (fires in `ram[sub+0x29]`
  facing); pad `0x40` = up-missile (`fire_upward`, spawn offset by facing at
  `rom[0x9BAD+2*(facing&1)]`) + down depth-charge (`fire_downward`).
- `B5B93` side guns are NOT directly damageable (handler runs no contact
  check); they detonate when `parent+0x68` bits set.
- `B5C73` (the kill-gate) dies via `world_surface_contact` — the up-missile
  candidate list `0xAEE6` — setting `parent+0x68 |= 1<<(index&15)`. Both dead
  → wreck parts spawn. Phase order: surface-kill `5C73` first.
- Scripted attempts (left-clamp park, strafe sweep, left-hold+right-tap) all
  damaged body parts but never broke a `5C73`/`5B93` — the surface-missile
  line needs aim the scripted patterns can't hold. Boss kill requires real
  play — same survivor/aim blocker as ordinary clears, not wide code.

## Weapon-isolation result (S1 boss)

- Torpedo-only (`0x82`, leftward into the wing): zero part damage incl. claw
  `6781` at `0xFFFC` — claw is armored to torpedoes / off the torpedo line.
- Up/down weapons (`0x4A`/`0xCA` variants): `5D59`/`64B6`/`6744` damaged —
  the damaging weapon is `0x40` (up-missile / depth-charge family), matching
  the `world_surface_contact` gate on `5C73`.
- Up-missile spawn offsets `rom[0x9BAD+2*(facing&1)]`: facing-left `sub+8`,
  facing-right `sub-8` — left-clamped missiles land at `0x43`, between the
  `5C73` guns (`0x3C`/`0x5C`) without breaking either. Confirmed aim-limited.

## Autopilot dodge + 5C73 aim experiments (test-only)

- `WORLD_AUTOPILOT_DODGE=1` added (world_native.c): evades any `+0x0d`-flagged
  actor inside a ±0x30 box. Measured: fresh stage-0 replay survived ~5000
  ticks with 2 deaths vs repeated instant deaths without it — real but
  insufficient for clears; still dies at arena lock phases identically.
- 5C73 aim mode: parked the sub at `x 0x77` (gun_x-8) holding `0x42` for
  ~1200 live ticks — up-missiles spawned at `0x7F` on the gun line, gun held
  `0x460` throughout. `projectile_contact_list(0xAEE6)` is a plain shape
  overlap: the surface weapon's actual trajectory does not intersect the
  `5C73` shape from sub-level positions. Kill needs the surface path at the
  gun's own line — beyond scripted positioning. Documented, not a wide issue.

## Dip/lives extremes (headless, wide, live input)

`WORLD_DIP=lives,difficulty` env (test-only) overrides the headless cabinet
switches (set_cabinet_switches is normally !headless-gated). 8k-tick live
runs: dip `0,0` game-overed at ~6000 (0 extra lives); dip `3,3` survived to
the run end with deaths/respawns — wide mode plays at both dip extremes.

## Co-op lifecycle (headless, wide, live input, 28C=0)

Script `repro/p2solo.txt` (level semantics: rows are absolute ticks; `edges`
column holds the system word until the next row, so each `0x10`/`0x0x` pulse
needs an explicit idle row to re-arm the rising-edge latch):

- Credits: `0x10` pulses of ~6 ticks separated by >=6 idle ticks each add one
  credit (`2B8`: 3 -> 2 -> 1 -> 0 across joins/continues).
- P1 start `0x01` edge, P2 join `0x02` edges during live `0x0B2E`/briefing
  `0x2218` masters with a spare credit -> `264` went `5 -> F` (P2 joined live).
- P2 death: `264` `F -> D` (P1-only window while P2's active bit cleared),
  respawn back to `F`.
- Co-op game over: `264 = C0`, master `0x0C04`; GAME OVER screen renders at
  4:3 (headless-016001.png) in wide mode.
- Continues: spare credits bought two solo restarts (`2218` -> `0B2E`),
  credits exhausted -> back to title/attract (`28C=4` -> `28C=1`).

Note: script rows hold levels, not per-tick pulses; a join/start/coin edge
latches once per asserted row. Boot (28C=4 phase) does not run `irq_tick`
(`0x2AA` never latches) — all input rows must land after the title master.
`WORLD_DIP`/`WORLD_FREE_PLAY` unset here: factory dips, no free play.
Open: `264=7` (P2 alone, P1 fully out) not yet captured — the dodge
autopilot drives P1 only, so P1 outlives P2 in these runs.

- P2-only window captured: `264 = A` at ~4500 (28C=0, master 0x0B2E,
  cam 0x62C) — P1 fully out at its `7A6A` continue wait while P2 held
  `0x41` right+up-missile at x 0xD7. One sample in a 500-tick trace;
  the window is nondeterministic (whoever exhausts lives first).
- coop-continue.png: both players out (`264=0`) — CONTINUE banner with
  PUSH 1P BUTTON (left edge) and PUSH 2P BUTTON (right edge) prompts
  simultaneously, edge-anchored, over a chained-mine field spanning
  the full 428 raster.

## Difficulty x slowdown matrix (headless, wide, live input, 28C=0)

Script `repro/matrix.txt`; all four cells reached live `0x0B2E` play:

| dips (lives,diff) | timing      | live | overrun passes | mean actors |
|-------------------|-------------|------|----------------|-------------|
| 0,0               | arcade      | yes  | 813/11184      | 90.1        |
| 0,0               | remove-slow | yes  | 0/12000        | 88.9        |
| 3,3               | arcade      | yes  | 1062/10936     | 103.1       |
| 3,3               | remove-slow | yes  | 0/12000        | 104.0       |

Arcade timing loses ~7-10% of frames under combat load (the slowdown
model engages); remove-slowdown is frame-perfect. Hardest dips raise
mean actor count ~14% (103 vs 90) — the difficulty scaling is live.
Start edges needed periodic re-fire under arcade timing because
multi-vblank ticks discard rising edges (expected, same as a real
cabinet's edge behavior).

## Weapon-stock inspection (bossfight.snap RAM read)

Sub weapon stocks are per-sub counters `+0x44` (torpedo), `+0x46` (up/
surface missile — the 5C73 kill-gate weapon), `+0x48` (depth-charge
in-flight). Snapshot reads: P1 `+0x44=2, +0x46=0, +0x48=2` (no up-missile
ammo — explains zero 7D-spawns in its scripted `0x40` runs); P2
`+0x44=2, +0x46=2, +0x48=0` — P2 IS armed for the kill gate. Up-shot
fires from `fire_upward` when `0x40` pressed, `+0x46>0`, `config+0x14==0`;
the shot arcs up then runs along the waterline in `0x7DA36` state
registered on the `0xAEE6` surface-contact list its whole flight —
the overlap that `5C73` tests. Direction follows facing (spawn at
`sub_x ± 8`). Remaining scripted-kill path: an armed sub (`+0x46>0`)
must reach `7A6A` beside a `5C73` gun and fire so the surface-run
crosses the part's shape.

## Up-missile (surface weapon) verified live in wide mode

Trace `b742`+stock instrumentation (test-only fields added to
headless-trace.txt; geometry tests still pass):

- `sub+0x46` = up-missile in-flight counter; spawn seeds it to 2
  (`world_dispatch.c:19415`), `fire_upward` decrements on `0x40` edge
  (`b742` is an edge word — holding 0x40 yields one edge only), despawn
  refunds it. bossfight.snap P1 had `+0x46=0` (both missiles still out);
  pickup-fed subs reached `+0x46=0x1a` in a long run.
- Armed-sub restore (patched `+0x46=5`, `x=0x1bd`): `0x40` edges spawn
  `9e73` -> `9eb2` flight actors (plus `a0a9` companions); `1d00
  0000:9eb2 x 01b5 y 013a..014a` lived ~50 ticks crossing the field.
  Right-clamp spawns (`0x1c5`) cull instantly past `view_edge 0x1c0` —
  correct edge behavior, and `+0x46` refunds.
- `0xAEE6` is a flat array (count + pointers), not a chain; actor trace
  now dumps it. No `7da36`/`aee6` registration observed yet — the `9eb2`
  flight expired before reaching the surface-run state. The kill-gate
  still needs a `7da36` surface run crossing a `5c73` shape; weapon
  mechanism itself is proven functional in the wide field.
- Also corrected: `7a6a`/`7b08` are join-label actors (dead player's
  "PUSH nP" prompt shadowing the survivor), not live-sub states.

## Surface-kill chain fully decoded (code + live flight evidence)

`5C73` dies only to `world_surface_contact` = the `0xAEE6` list (flat
array: count + actor pointers; actor trace now dumps it). Three weapons
register on `0xAEE6`, and every one of their edge/cull tests already
routes through `world_view_edge_*`:

1. `9FA1 -> 9FCD -> 9FDA` surface runner (fire_upward when sub medium
   `+0x60 == 3` = tile material 3 at `y+16`, i.e. genuinely floating on
   the waterline). Appends at update_vertical_weapon surface branch.
   Underwater variant `9E73 -> 9EB2/9EC4` registers on `0xA6E2` instead —
   cannot hit `5C73`. `9EB2` flight verified live in wide (`1d00
   0000:9eb2 x 01b5` ~50 ticks); wing-clamp spawns cull at `view_edge
   >0x1C0`/`<0x40` and refund `+0x46` — correct edge behavior.
2. `B4CE -> ... -> B5EB` homing missile (B482 launcher, equipment
   `+0x60==3` upgrade): targets `0xB6EE` surface-enemy list — would seek
   `5C73` itself. Registers on `0xAEE6` each tick.
3. `BB40 -> BB7E` salvo missile (BAA5/BAAF level-3 launcher).

Also confirmed: `+0x44`/`+0x46`/`+0x48` are per-weapon in-flight counters
(spawn seeds 2/2/0, refunded on despawn — `+0x46` observed at 0x1A after
pickups). `0xB742` is an edge word — fire bits need pulses, not holds.
`medium==3` requires the material-3 surface tile right beneath the sub —
not merely the `0xA0` y-clamp, so scripted surface fire needs an actual
waterline position.

## 0xAEE6 surface-kill list populated LIVE in wide mode

Stage-1 live run (28C=0, remove-slowdown): sub at the surface (y 0xA0)
fired the `9FA1 -> 9FCD/9FDA` surface-runner variant; four missiles in
flight at once (actors 0x6380/0x3800/0x2700/0x5280) and ALL registered
on the `0xAEE6` contact list — the `P` trace lines:
`P6380 0000:9fcd`, `P3800 0000:9fda`, `P2700 0000:9fcd`,
`P5280 0000:9fcd` at x 0xBF running the surface path.
This closes the 5C73 kill path mechanically: spawn (medium==3 at the
waterline) -> surface run -> 0xAEE6 registration -> shape overlap ->
world_surface_contact -> parent+0x68 -> 5B93 detonation -> wreck. The
only unobserved step is the literal overlap with a 5C73 shape, which is
ordinary aim — identical code and geometry in arcade mode.

## 5C73 aim window characterized

`9FDA` surface runners descend vertically from spawn (`sub_x±8`,
constant x, y -> ~0x1A0). To overlap a `5C73` gun (x 0x65/0x85, y
0x1B8) the surfaced sub parks ~8px left/right of the gun and fires —
the runner descends onto the deck. In the arena the sub dies at
y~0xBF (the gun's own shape reaches up to it) just above the
`medium==3` waterline band — the last step is again survival/aim under
fire, identical in arcade. Mechanism proof is complete: `0xAEE6`
registration live-verified; overlap is ordinary play.

## Homing missile intercept verified live (spawn-assisted)

A `0xB4CE` homing missile injected into an expendable effect actor ran
the real targeting path: `b52f` -> `b553` (picked a `0xB6EE` surface
target) -> `b57b` homing flight curving down-left onto the composite ->
registered on `0xAEE6` every tick (`P1d80` lines) -> `1b4c` blast on
impact at `0xCD,0x160`. The `0xB6EE` surface-target list was dumped:
both `5C73` deck guns are entries 0 and 2 (plus four `5A04` parts) —
homing salvos pick targets round-robin via `0xB6EC`, so a real volley
covers `5C73`. The injected pair's live missile struck a `5A04` part
instead; `5C73` hp unchanged. Every link of the `5C73` kill chain is
now live-verified except the literal overlap + `parent+0x68` detonation,
which is the same code path in arcade.

## 5C73 deck-gun KILL confirmed live (spawn-assisted)

Injecting a B57B homing missile targeted at 5C73#1 (0x1B00) produced the
full surface-kill sequence in live wide mode (28C=0): missile flew to
the gun, world_surface_contact consumed it, 5C73 hp 0x460 -> 0x3C0
(−0xA0 missile damage) on the first run, and on the one-hit run the gun
went 5c73 -> 5cc6 -> 5cd0 (death sequence, hp 0). This is the first
confirmed kill of the S1 gate part via its actual mechanic — the deck
guns take damage ONLY from the 0xAEE6 surface-contact list, and the
homing missile reaches them.

The B57B homing path also exercised: b553 target pick over the 0xB6EE
list (both 5C73 guns + four 5A04 parts are registered surface targets),
b57b intercept steering, 0xAEE6 registration every tick, blast on hit.
One injected missile's round-robin pick was eaten by a nearer 5A04 part
— surface enemies consume 0xAEE6 shots first-overlap, so volleys are the
real weapon.

Still unobserved (same code path as arcade, needs both guns dead): the
second 5C73 kill, parent+0x68 bit accumulation, the 5B93 side-gun
detonations, and wreck handlers 10C8F/10CD1/10E0B. An injected B57B
also spawned an unhandled B6D91 child (likely a smoke trail with fields
my patch left inconsistent) — test artifact, not game code.

## FULL deck-gun kill + side-gun detonation observed live (spawn-assisted)

Restored bossfight.snap with both 5C73 guns at 1 hp and three B57B homing
missiles targeted onto them. Result, live wide mode (28C=0):
- BOTH deck guns died: 5c73 -> 5cc6 -> 5cd0 (hp 0), via
  world_surface_contact consuming the homing missiles.
- The four 5B93 side guns detonated immediately (absent from the first
  post-death actor dump; present every prior sample) — the
  parent+0x68 kill-bit chain fired.
- Composite then slid left through the left wing (parts at x 0xFF4A..
  0xFFCD, i.e. off the left edge) — boss retreat/despawn behavior.
- New coverage gap found + fixed: dead deck guns spawn effect
  b000:6d91 (husk smoke) every 16 ticks; it was unimplemented and
  aborted the run. Added a stub case 0xb6d91 (finish_animation) —
  cosmetic gap only, documented pending ROM decode.
- Wreck handlers 10C8F/10CD1/10E0B not yet reached; the composite
  exits first — further phase tracing would need the wreck trigger
  conditions (262 did not set 0x82 in the 1200-tick window).

## Wreck sequencing decoded (source + trace)

10C8F/CD1/D10/D8C/E0B are wreck-child effects pinned to the composite
parent (explosion ring, debris, falling hull parts with terrain masks).
They spawn only when the parent transitions to wreck. In the
double-gun-kill trace the parent did NOT wreck: after the deck guns died
and 5B93s detonated, the warship advanced left (parts to x 0xFF4A..)
with its hull parts at full hp — i.e. losing the deck guns is a phase
change, not the kill. The wreck requires the remaining hull parts
(5D59/6744/6781/5A04, hp 0x290..0xA00) destroyed — torpedo targets that
need sustained survival under boss fire, the same competence gate as
arcade.

## COMPLETE S1 BOSS WRECK observed live (spawn-assisted)

The full kill chain now runs end-to-end in live wide mode (28C=0):

- Parent warship identified: actor 3680, handler b000:5824 (the
  composite root; +0x4A saved-hp 0x600, +0x66 flags, script-driven
  movement). All parts attach via the 0x84fe ancestor table
  (warship_attachment).
- Damage model decoded (src/world_dispatch.c 23770-23789): hull parts
  (63B3/64F3/653E/6928 family) reset +0x26=0x7f00 each tick — immune to
  accumulation — but every 0xA6E2 hit deducts its damage from the
  shared pool parent+0x48. When the pool empties, parent+0x26=0.
  Separately, 6CA8 is a one-shot weak point: a single 0xA6E2 kill ->
  parent+0x26=0 directly (lines 24061-66). It patrols the hull
  (positions change per tick).
- Parent+0x26==0 -> 5824 case 24208 -> 0x595D (award + blast sequence +
  +0x0A=2) -> 0x5975 sinking animation -> 0x59B3 drift-off -> despawn.
  Observed: 5824 -> 595D -> 5975 (y drifting upward/sinking past
  0x176->0x166) -> 59B3 -> gone. Every attached part (5D59, 6744x2,
  6781, 5A04x4, 5C73x2, 5B93/5BFD, 64B6x2, 63B3, 6CA8) despawned via
  warship_attachment seeing parent+0x0A=2.
- Visual: headless-005081.bmp shows the warship mid-explosion sinking
  at x~0x75 with debris — the wreck rendered in the wide raster inside
  the arcade field region.
- 0x262 stayed 0x0001 in the 1200-tick window — the 0x82 boss-kill
  cinematic bit is set by a different path (probably the 0x10xxx
  variant master or later stage-clear logic), not by hull death alone.

So the S1 composite is killed by: sustained 0xA6E2 fire on the 6xxx
hull parts draining parent+0x48 (or the moving 6CA8 weak point), while
the 5C73 deck guns + 5A04 turrets are 0xAEE6 surface targets and 5B93
side guns chain-detonate. All mechanics verified in wide mode.

## Interactive GPU presenter — windowed wide, REAL HARDWARE

.local/native/InTheHunt.exe .local/world --mode=wide with
WORLD_TEST_TITLE=1 WORLD_PACING_LOG on the user's display:

- Real window "In the Hunt" 3072x1728 (16:9 client) on NVIDIA RTX 4090
  via the D3D12 effects path ("GPU path transport ready",
  DXGI hw-composition flags 3). GPU effects samples 0.2-1.7 ms.
- The --mode=wide session view persisted through the scripted
  title->start->machine_reset->title->start cycle (phases 0-3),
  reaching live-play master 0x0B2E — wide gameplay presented on the
  interactive presenter, not only headless.
- 21582-line pacing log: 3595 DXGI presents counted (dxgi
  shown/refresh/made counters), all 1800 gameplay passes at exactly
  passes=1 per refresh — no judder, 0 frames lost; ~1797 present-only
  refreshes during attract/intro (passes 0 1 = correct hold behavior).
- Captures (PrintWindow, window was backgrounded):
  presenter/wide-live.png = story intro text fill 16:9 edge-to-edge;
  wide-live2.png = radar "09 APR 0093 CLOUDY" at full 16:9.
- Windowed + GPU + wide verified interactively. Fullscreen/VRR uses
  the same swapchain path (world_gpu_request_vrr); fullscreen takeover
  not exercised to avoid disrupting the user's session — flagged as
  the remaining manual check.
- NOTE for repeaters: argv[1] is the data directory — pass .local/world
  before flags or the exe opens the "Choose your ROM" welcome screen.

## S1 CLAW BOSS — spawn, fight, kill, and stage-2 transition all live

The stage-1 end boss is the claw giant-sub, a separate actor family
from the mid-stage warship composite (5824):

- Spawn: stage-0 (player stage 1) row 469 of the spawn table decoded
  from maincpu.bin: trigger B700=0x16A0, kind 1000:0917, x 0x1e8
  y 0xa0. Verified live by cursor-forwarding the stage spawner
  (1000:5d98, +0x40 row cursor) to row 469 and B700=0x16A0 in a
  snapshot — the boss spawned and ran its real phase script
  (test-assisted skip of stage rows, labeled as such).
- Fight composition: master 0x2180 = 1000:0A6B (main phase handler;
  +0x60 arm mask 0x0F = all four arms), ~50 arm-segment actors
  1000:14F1, appendages 1000:108C, scripted anchors 1000:1742
  (boss_claw_step), plus 1000:1E36/20B3/2069/1EDB/2197/1FBF support
  handlers — all observed live in wide mode.
- Kill gate: master hp +0x26 drains via 0xA6E2 contact during calm
  (+0x66 bit0 clear; +0x4A is the per-tick restore). Kill requires
  ram[0x264]&3 (a live player). Death: 0x0A6B -> 0xEA3 (drift to
  terrain anchor) -> 0xF16 (jitter left-drift + boss_blast 0x7B8
  explosion train) -> x<=0 exit -> ram[0x263]|=0xC0 -> despawn.
- Observed live: +0x26/+0x4A=0 patch (the calm check precedes the
  restore) -> 0x262: 0001 -> 0081 (boss-kill bit) immediately,
  master 0xEA3 -> 0xF16 drifting left through the wide field
  (x 0x19C -> 0x0003), all ~50 arm parts retreating with it
  (x to 0xFFD0), explosion frames headless-005411.bmp.
- STAGE TRANSITION: at wreck exit, 0x263|=0xC0 fired -> stage 0 ->
  stage 1 at tick 6780, B700 reset, clean 26E/282 state — then live
  stage-2 opening (green water, sunken ship, sharks; score 0x2500;
  headless-final.bmp). The full "boss kill -> unlock -> transition
  to 2" stage card row is now verified in wide mode.
- Note: the second continue screen shot rendered 4:3 during the
  continue countdown — consistent with the earlier game-over
  composition observation (0x0C04 correctly 4:3).

## S2 BOSS — spawn, bodies, kill, and stage-3 transition all live

Boss = twin-hull vessel, 1000:289C family (the 1000:289C..4023 map).

- Spawn: stage-1 (player stage 2) spawn-table row 215: trigger
  B700=0x1020, kind 1000:289C — cursor-forwarded on the 1000:5D98
  spawner like S1 (test-assisted, labeled).
- Composition: master 1000:298C (+0x40 body count = 2), two bodies
  1000:2AF6 (hp 0x3C00 each — one at 0x3580, one at 0x6B00; both
  share handler 0x2AF6 so unique-offset listing misses one),
  plus riding parts 0x2EED/0x31FA/0x32AD/0x3315/0x3392/0x34A0/
  0x3BEB/0x3C40/0x305A/0x2E09 claw/armor/cannon appendages.
- Kill: each body's +0x26 drains via s5_contacts hitboxes; at 0
  (with 0x264&3) -> 0x2D79 dying -> ancestor +0x40 decrement ->
  0x2DBD terrain fall -> 0x2DEC/0x2DFE wreck. Both bodies dead ->
  +0x40=0 -> master 0x298C -> 0x262|=0x80 -> 0x29A4 ->
  0x262|=0xC000 -> despawn.
- Observed live: both bodies 0x2AF6->0x2D79->0x2DBD falling, master
  0x298C->0x29A4, 0x262: 0001->0081, then STAGE 1->2 transition
  (stage-3 entry) at ~tick 4740 with clean state and the live sub
  at 0x81. Explosion frame headless-002681.bmp (score 0x66301).
- Method note: boss-kill verification needs only the kill target's
  +0x26=0 (each state's tail re-checks it) + 0x264|3 gate bits — no
  projectile needed when the tail checks hp directly rather than
  consuming a contact.

## S3 end boss: spawn -> fight -> kill -> wreck -> STAGE-4 ENTRY (all live)

- Spawn: row 240 of stage-idx-2 table (`0x7E90`), trigger `B700=0x10DF`,
  kind `1000:451A`, via `1000:5D98` spawner cursor `+0x40` assist.
- Fight chain live: `451A` -> `45BA` scroll-in (x `0x1D5`->`0x101`) -> `4614`
  -> `4640` arm/link setup -> `465A` surface fight, hp `0x21C0`, y `0x138`.
  Damage model: `world_surface_contact` (`0xAEE6` list) only in `465A`.
- Boss is two-phase: `465A` kill -> `47CB` countdown -> `4811`/`4837` flight
  restores `+0x26` via `s3b_boss_health` -> `48A4`/`49AB`/`49CE` phase-2
  (accepts `world_projectile_contact` + `world_surface_contact`, `0x264&3`
  gate, resets `+0x26=0x100` if no live player) -> at `+0x26==0`:
  `0x262|=0x80`, `world_award_score`, `blast_sequence(0x41bc,4)`,
  `begin_linear_script(0x4506)`, `+0x42=0x28` -> `0x4C19`.
- `0x4C19` wreck: `+0x42` countdown + `step_flight_script` — wreck visibly
  ascends `y 0x138 -> 0xF8` (frames `headless-002991/003111.bmp`) -> `step<0`
  -> `0x262|=0xC000` (stage-defeated).
- Transition chain observed live: `0x0B2E` -> `0x0BB3`/`0x0BED` stage-clear
  masters -> `0x2576`/`0x25B5`/`0x25FF`/`0x0A0E` score sequence -> `0x2218`
  briefing (`world_start_player` consumed a credit, `0x264=5`) -> fire edge
  (`0xB742&0xF0`) advances -> `0x2282` -> `0x0B2E` -> **stage index 2->3**
  (player stage 4), fresh `B700=0`.
- Assists (labeled): snap handler `465A->49CE` + `+0x26=0` (enters the real
  phase-2 kill check; the `47CB`->`4811` health-restore flight between is
  cosmetic to the kill evidence); `+0x42=1` countdown; `0x263&=~8` +
  `0x28E=0x0BFF` (clears the premature stage-clear latched by the camera
  jump); `0x28C:1->0` + `0x264:F->5` + `0x2B8=9` demo->live conversion.
- KEY DIAGNOSTIC: `0x28C&1` = attract-demo flag; demo signature is
  `0x264=15`,`0x266=3`,`0x284=0`,`0x28E` counting vs live `0x284=1`,`0x28E=0`.
  All earlier S3 snaps (`s3mid`/`s3end`/`s3fight`) were demo-reel captures —
  the recurring `0xC04`->`0xC7F8`->attract "game over" was the demo reel's
  `0x28E>=0xC00` expiry wiping actors, NOT player death. `0x28C=0` halts the
  `0x28E` counter and that wipe path entirely. S1/S2 snaps verified live
  (`28c=0`,`284=1`) — unaffected.

## S4 boss: spawn -> fight -> kill -> wreck -> STAGE-5 ENTRY (live stage-4 snap)

- Spawn: row 333 of stage-idx-3 table (`0xA24C`), trigger `B700=0x153E`,
  kind `7000:5AA2` (boss body), via `1000:5D98` spawner cursor `+0x40`
  (BYTE OFFSET from table base, not absolute) + `B700` assist.
- Boss composite live: body `0x5BD5`/`0x5C0B` hp `0x2400`, face parts
  `0x5F08`/`0x5F79`/`0x5FA0`, claw roots `0x6099` -> `0x62EF`/`0x63BB` links
  -> `0x64CA`/`0x6550` claws, all riding `s5_parent` (`0x84FE` ring —
  STALE in snapshots; use live trace).
- Damage model (s4b_wound): `world_projectile_contact` (`0xA6E2`) hit on
  `0x75F79`/`0x75FA0` face parts -> `parent+0x26 -= hit+0x36`; killing blow
  needs `0x264&3` -> `+0x26=0` + score record `|8` -> caller `0x262|=0x80`,
  part -> `0x5EB7` wreck visual.
- Kill assist: `0x1780+0x26=1` + injected `0xAC0E` blast torpedo (seg `0000`,
  `+0x38=0` stationary, `+0x26/+0x6e=0x7fff`, `+0x3c=0xABA3`) at `0x1A6,0xDA`
  — `5FA0`'s `+0x3c=0xF85B` shape is `cx=-0x40` — hitbox center is NOT the
  `+0x21` anchor. `0x262: 0x0001 -> 0x0081` observed live; all parts -> `5EB7`.
- Wreck chain: `0x5D89` (+0x42 countdown, `0x263|=0x40`, `0x26F&=0x7F`) ->
  `0x5DA7` rise (`+0x3A=0xFF00` until `y<=0xC0`) -> `0x5DBD` (`+0x42=0x80`)
  -> `0x263|=0x80` (stage-end).
- Transition: `0x0BB3`/`0x0BED` -> `0x2576`/`0x25B5`/`0x25FF`/`0x0A0E` ->
  `0x2218`/`0x2282` -> `0x0B2E` -> **stage index 3->4 (player stage 5)**
  live, `0x28C=0`, `0x264=7`. Visual: `show/s4-final.png`.
- Gotcha: `0xAC0E`/`0xA804C` torpedoes terrain-probe `x+0x10` each tick and
  retire on solid cells; `world_append_candidate` writes `0xA6E2` per tick
  (`world_dispatch_begin` zeroes the list every frame — snap-injected list
  entries do NOT persist; the actor must self-append via its handler).

## S5 ice boss: live stage-5 snap -> core kill -> hull wreck -> STAGE-6 ENTRY

- Spawn: `7000:7FFE` at row 6 trig `0x0000` (stage start, present in
  `s5live.snap`: `stage=4 28c=0 264=7 80=0x0b2e` — live, not demo).
- Composite: hull `0x80C9` (root, `0xB8C4/0xB8C6/0xB8C8` = `0x8337`/`0x8A19`
  children), core `0x1580=0x84A3` cycling `0x84A3->0x8442->0x84FA->0x8534`
  (EXPOSED: `s5_core_exposed` + `!R(0x26) -> 0x85E2`), spikes `0x8E51`,
  shards `0x8BAC`, icicles `0xDC68`, cutters `0x9194`.
- Kill assist: `0x1580` handler `0x84A3 -> 0x8534` + `+0x26=0` + `0x264|3`.
  `0x8534` -> `0x85E2` (blast_sequence 0x7f56) -> `0x85FA` (+0x42=0x28) ->
  `parent+0x0a |= 2` -> `finish_animation` — all observed live.
- Hull: `0x80C9` end-of-case `B(0x0a)&2` -> `0x262|=0x80` + `0x826A` chain
  -> `0x8304` destruction states -> `0x263|=0x80` -> `0x0BB3`/`0x0BED` ->
  `0x2576` score -> `0x2218`/`0x2282` -> `0x0B2E` -> **stage index 4->5
  (player stage 6)** at ~tick 7350, `0x28C=0`, `0x264=7`.
- Evidence: `s5post.snap` shows `0x110a=0x06`, `0x1100=0x8304`,
  `0x262=0x0081`, `0x263=0x0700` mid-sequence; `s6live.snap` confirms
  stage-6 entry live.
- Note: `s5_parent` = `0x84FE[actor+8]` — `+8` is a depth index written at
  spawn (`parent+8+2`); the `0x84FE/0x8500` table is LIVE-written during
  dispatch so snap-dump parents are stale; `world_spawn_child` splices the
  child directly after the parent in the `+4` chain.

## S6 final boss + true ending: live stage-6 boot -> boss kill -> ENDING sequence

- Spawn: row 213 (last row) @ `0xC3FA`: trig `0x0FE0`, kind `7000:ABDC`,
  flags `0x0000` (stage-idx-5 table ptr `0xB9FE`).
- Boot: `s6play.snap` (stage=5, `28C=0`, `80=0x0B2E`, `264=7`, live sub
  `0x0A00=0x7C3B`) + `B700=0x0FE0` + spawner `+0x40=213*12` -> `0xABDC`
  init spawns 4 `0xB37E`/`0xB912` arm parts + `0xBDEF`/`0xAF1C`.
- Composite: master `0x1300=0xACB5` (hp `0x5800`), arms `0xB3B7`/`0xB99E`
  (hp `0x0C00`), parts `0xBDFD`/`0xAF56`/`0xBEE0`/`0xBC3C`/`0xBC89`/`0xBBFD`;
  lift `0xC42C` and scroll lamps `0xC3C9` also live.
- Kill path (final_boss `hull_death`, checked every tick in `0xADD9`/
  `0xAE4E`/`0xAEBC`/`0xAE7B`/hull_cruise): `+0x26=0` -> `blast_sequence`
  -> `0x262|=0x82` -> `0xAED8` leftward drift -> `0x262|=0x40` ->
  `finish_animation` -> `0x0BB3`/`0x0BED` -> stage index 6 (ending
  "stage") -> `0x33A9` ending master -> `0x8F82` TRUE-ENDING actor
  (`0x264==3` gate, `0x272>=2` true-ending path, `0x905F` surface).
- Kill assists (diagnostic): handler `0xACB5->0xADD9` + `+0x26=0` +
  `x=0x2A0`; sub `+0x3C=0xFFFF` (no hitbox -> `world_player_contact`
  skips) so the parked sub survives the wreck drift. `+0x0A`/`+0x26`/
  teleport alone do NOT save it — contact zeroes `+0x26` then `damage()`.
- Ending visuals: `s6-headless-016901.png`, `s6-headless-017101.png` —
  escape scene, red sub left edge + white sub right edge, countdown
  27->25 bottom-center, purple depth-charge field.
- End state: `stage=6`, `80=0x33A9`, `26E=0x8000`, `28C=0`, `264=3` —
  ending sequence running live (not attract, not game-over).

## Harness fix: cold-boot autopilot coin/start (test-only)

- The `a`-row nonplay edge generator emitted single-frame `0x05`/`0x04`
  pulses that could never satisfy the coin debounce (service `(0x2BB&15)==3`
  needs two consecutive held samples; chutes need the `0101` pattern), so a
  cold-boot `a0` run sat in `0x1DE6` attract forever.
- Now emits `0x10` service for two consecutive nonplay ticks (credit+1 per
  120-tick cycle), `0x01` start at +29, and a one-tick pad `0x40` at +36 so
  `0x2218`-family briefings (which need `B742&0xF0`, not a system bit) also
  advance on their own.
- Verified: cold boot -> `0x1DE6` -> `2B8=1` -> `0x1EA5` -> `0x1F19` ->
  `264=0x0005` -> `0x2218` -> `0x2282` -> `0x0B2E` live play, no manual
  edges. NOTE: script `a`-rows only take effect at their frame — a
  cold-boot run needs `1 a0 0` (or similar) as the first row.
- Play depth under WORLD_AUTOPILOT_DODGE=1: reaches `B700=0x070D` in stage
  1 before the arena fight kills the sub; death -> continue/attract ->
  re-coin -> start cycles autonomously. Identical behavior observed in
  arcade mode (competence gap, not a wide regression).

## Both endings verified at handler level in wide mode

- TRUE ending (`0x264==3`, `0xB8A0!=0`): `0x0BB3`/`0x0BED` -> `0x33A9`
  hold -> `0x8F82` true-ending actor family (`true_ending_bar`, `0x905F`
  surface path). Escape-scene captures `s6-headless-016901/017101.png`.
- NORMAL ending (`0xB8A0=0x0051`, `0x264=0x0005` solo): `0x0BB3`/`0x0BED`
  -> `0x33E4` -> `0x341E` -> `0x344D` first scene (clear picture, `0x126`
  record slots, `0x1A50` actors) -> `0x3513` -> `0x3522` -> `0x355B` ->
  `0x356E` second scene (`0x1AF7` actors, `0x272`-indexed sound) ->
  `0x35BE` -> `0x35F9` fade -> `0xC466`. Captures:
  `s6n-headless-012701.png` (surfacing sub + story text),
  `s6n-headless-014701.png` (credits scroll).
- QUICK route (`0xB8A0==0`): `0x342A` -> `0x35BE` — also observed
  incidentally when the boss was killed before its phase-3 counter.
- All three routes run under `--mode=wide` with no wide-specific handler;
  the endings are mode-agnostic masters/actor families.

## P2-only survival (WORLD_AUTOPILOT_P2=1, test-only)

- New test env `WORLD_AUTOPILOT_P2=1`: nonplay start edge becomes `0x02`
  (P2 start) and the autopilot's sub tracking reads `ram[0x92]` (the P2
  pointer) instead of `0x90`.
- Cold boot -> coin -> title -> `world_start_player` takes P2 (`0x264=0x0A`)
  -> `0x2218` -> `0x2282` -> `0x0B2E` live solo-P2 play in wide mode.
- P2 sub moved to field `0x00C7`, camera scrolled to `B700=0x01F7`, fired
  and dodged identically to P1. No P1 actor ever joined.
- Same code path as P1 — verifies the P2 pointer/index plumbing is live
  in wide mode, not just the join overlay.

## Wide-mode audit — every stage and boss (2026-11-22)

Visual audit captures: `audit-s0..s5-001101/001201.png` — live wide frames
for all six stages (seed boots, live `0x0B2E` play). All six render
correctly: background continuous across both wings, sprites/enemies/mines/
torpedoes active in the wings, HUD anchored at the wide edges, no seams,
no misplaced fixtures, no black regions, no stretching.

### Camera/scroll audit
- `world_horizontal_camera` `+0x4c` origin and `update_camera_limits`/
  `advance_scroll_script` limits all come from ROM scroll-script rows
  keyed by world `B700` milestones — world-anchored by design. The S1
  arena camera-lock delta (0xA10 wide vs 0xA88 arcade) is the wider view
  edge resting against the same world boundary — identical trajectories,
  not a regression.

### Spawn-table audit
- ~800 authored rows sit in edge shift zones across the six tables.
  Classification: through-scrollers and edge-enterers shift correctly;
  tile-painting scenery (`0xDB86`/`0xDAA9`/`0xDACD`) and burst-scheduled
  emitters keep world semantics. `world_anchored_kind()` (0xDA13/0xDB86/
  0x8309) covers the tile painters — no additions needed: static screen-
  pinned emitters (e.g. `0x7C3AA` lamps) are view-edge anchored and shift
  correctly; world fixtures paint via tile/camera paths, not `+0x21`.

### Bugs found and fixed — ROM-loaded left-edge despawn limits
Constants were view-translated in the original conversion, but despawn
limits loaded from ROM tables were not. In wide, actors hit their raw
limit ~54 px before reaching the left view edge -> visible pop-out.

- `src/world_dispatch.c` `0x7C33D` (stage-6 scenery sprite, `0xC312` rows
  — ~10 rows at x 0x1A0-0x1D0 in the stage-6 table): `(int16_t)+0x21 <
  +0x46` where `+0x46` is a ROM limit from `0xA686` ({0x60,0x28,0x38,0x0,
  0x40}). -> `world_view_edge_less_s(...)`.
- `src/world_dispatch.c` `0xBBBE6` (b000 launcher cleanup): `+0x21 <=
  ROM 0xB06A+type` ({0x40,0x0,0xffa0,0xfff0,...}). -> `world_view_edge_le_s(...)`.
- Cleared as correct-by-design: `0x1F7..` family `+0x31` limits (-0x58..
  -0xB8) already deeper than the wide edge; `0x565A`/`+0x6c`/`+0x46`
  (0xBCD61 turret) ROM thresholds are world-position transitions, not
  edge despawns — raw values are correct.

### Verification
- `test_view`: all geometry checks pass.
- Cold boots in BOTH modes reach live `0x0B2E` play (arcade B700=0x27E,
  wide 0x24F at tick 1000 — expected view dynamics).
- Stage-6 wide run: `0xC3AA` lamps (`0xC3C9`), `0xDD**` emitters
  (`0xDC27`), `0xCD61`/`0xCDC8` turrets all live. Arcade path of the two
  fixes is a provable no-op (helpers return raw compares when not wide).

### Prior evidence standing
All six boss chains (spawn -> composite -> damage -> kill -> wreck ->
stage transition), both endings, P2-only play, wing clamps/pickup/terrain
receipts remain verified as documented above.

### Live differential check for the despawn fix (0x7C33D)

Harness note: snapshots load via `WORLD_SNAPSHOT_LOAD=path` +
`WORLD_SCRIPT_RELATIVE=1` (there is no `--from` option); mid-run saves via
`WORLD_SNAPSHOT_SAVE` fire at absolute frame numbers, not script-relative.

- Hijacked stage-6 actor 0xE80 (chain-linked, cs=0x7000): handler `0xC33D`,
  `+0x21=0x40`, `+0x46=0x60` (ROM limit kind 0 — exactly the pop-out case).
- WIDE run: actor survived ~200+ dispatched ticks while scrolling left
  (old code `x < 0x60` would retire it on the first dispatched tick since
  `0x40 < 0x60` — impossible without the fix) and then retired to `0x1B4C`
  at the wide left edge — despawn fired at `x < 0x2A` = `0x60 - 54`,
  preserving the arcade raster despawn point.
- ARCADE run: same actor dispatched and retired via `0x1B4C` as expected —
  helpers are no-ops off-wide.
- Both runs: 300/300 gameplay passes, 0 frames lost, no unrecovered
  handlers.

## UPDATE 33 — enemy spawn audit (all six stages, field-space corrected)

Field convention restated: actor +0x21 = field = screen + 96. Arcade
visible field [0x60,0x1A0); wide visible [0x2A,0x1D6). An earlier
working assumption treated +0x21 as screen space — corrected here; all
conclusions below use field space.

### Findings and fixes

- `world_view_spawn_x` threshold `x > 0x1A0` left a one-value hole:
  authored field X exactly 0x1A0 (screen 320, first off-screen column)
  spawned at wide field 0x1A0 = raster 374 — inside the right wing.
  12 non-anchored rows sit at 0x1A0 (s1 b000:909f; s2 1000:e9d2 x5;
  s4 b000:921a, b000:0084; s5 7000:c312 x4). Threshold is now
  `x >= 0x1A0`.
- Drain-time overshoot: fx = trigger - camera + converted X. When a row
  drains after the camera already passed its trigger (delta > 0), a
  right-shifted entrant could land inside the wide aperture. A clamp in
  world_stage_spawns now pins any right-shifted spawn landing in
  [0x2A,0x1D6) to field 0x1D6 (just off-screen). Live proof: a forced
  B700=0x08E0 drain on stage 2 shows 1000:e9d2 rows — ax=0x1A0 lands at
  fx=0x1D6, and five late-drained rows clamp to 0x1D6 rather than
  materializing mid-view.
- Two constant child spawns (0x7CCEE effect at field 0x1B8, b000:380F
  wave child at field 0x1E0) now go through world_view_spawn_x — they
  stayed off-screen in wide but entered ~54px early relative to the new
  right edge; the shift preserves authored entry distance. No-op in
  arcade.
- Classification sweep: all [0x140,0x1A0) band rows rechecked in field
  space — they are authored in-view spawns (screen 224-319), correctly
  left unshifted. Anchored kinds (da13/db86/8309 tile painters and
  scroll-locked decoration) keep world positions by design.

### Verification

- Static full-table audit: all 1909 spawn rows across six stages —
  zero rows where a non-anchored authored-off-screen X converts to a
  wide-visible field position.
- Live WORLD_SPAWN_TRACE runs, all six stages wide mode: 1126 spawn
  events, 0 pop-ins (no authored-off-screen actor landed in
  [0x2A,0x1D6)).
- Arcade control: 342 spawn events, every fx = trigger - camera + raw
  authored X (conversion a complete no-op off-wide).
- tools/test_view.c updated for the >=0x1A0 threshold; all checks pass.
- WORLD_SPAWN_TRACE=1 is a permanent env-gated diagnostic in
  world_stage_spawns (logs trg/cam/kind/ax/fx/y/flags per drained row).

## UPDATE 34 — non-table spawn paths audited (exhaustive +0x21 sweep)

Every `+0x21` write site reviewed beyond world_stage_spawns:
- Constants: child 0x1B8 (0x7CCEE effect), child 0x1E0 (b000:380F wave),
  actor 0x1E0 (0x1451A stage-3 end-boss init) — all now
  world_view_spawn_x'd so authored entry distance past the right edge
  is preserved in wide.
- ROM-table loads: 1000:40F4 (0x147CB), 1000:4024 (0x14CBA side pod,
  values [0x60,0x1A0]), 1000:4028 (0x155AD arm piece, [0x2B,0x1D5] —
  0x1D5 landed at wide raster 427), B000:A5D8 effect children
  ([0x1B8..0x1F8]) — all view-translated.
- Verified untouched (correct semantics): a000:ED8E/a9702, rom:D1B9/
  2423/23EF in-view formation coords; DE72's 0xDE37 sprite-queue
  positions; parent-relative child writes; per-tick scroll updates
  (B71A/B70A); begin_linear_script anchors (relative); the
  true_ending_steer arena clamp [0x78,0x188] (authored cutscene bounds);
  ending level table (in-view).
- world_spawn_child sets no position — all spawn sites covered.

Verification: s0/s3/s5 re-traced (727 spawns, 0 pop-ins); forced drain
still lands fx=0x1D6; both modes cold-boot to live play; arcade path of
all new call sites is a no-op (spawn_x returns x unchanged off-wide).

## November 23 live selector and presentation verification

Both keyboard paths reached the first playable stage through the normal Windows executable on the RTX 4090. The 2x client was 854x480 for Widescreen and 640x480 for Arcade. Each showed the Play overlay, ran attract after five seconds idle, consumed the waking Enter press, then started on the next press. Direct window captures are under build/mode-selector-qa/live; mode-select.png and widescreen-play.png in artwork/screenshots are the wide live captures.

The live check caught two issues missed by headless geometry checks. The live loop must draw the captured wide scanlines once per advanced frame. The D3D12 post chain must receive the new aspect when the source texture changes width. Window sizing rounds the exact aperture up after multiplying by the requested scale.

tools/test_mode_menu.c covers both choices, Options and Exit focus restoration, held selection, 4999/5000 ms and wake/Play. tools/test_view.c passed. An isolated native persistence harness rotated the board rank pointers, saved different initials in both modes, reset the table and restored each file; both leaders matched. This run covered the first stage and synthetic score persistence. No full-campaign or physical gamepad qualification was performed in this run.
