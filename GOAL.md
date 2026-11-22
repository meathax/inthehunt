# GOAL: widescreen gameplay (docs/widescreen-gameplay-plan.md)

## Contract (plan step 3)
- Working raster 428x240; aperture 426 2/3 units wide, centered. raster X = legacy screen X + 54.
- Legacy screen domain unchanged: +0x21 = screen+96 sprite field; screen = world - camera.
- Wide visible legacy X in [-54, 374). Player limits: field [0x3B,0x1BD] (authored insets preserved).
- Camera: unchanged (already world-relative via screen=world-camera).
- Launch selector: Arcade (first), Widescreen, Options, Exit. A mode opens the attract menu with Play, Options and Exit. --mode=arcade|wide selects a headless session.
- Snapshots are mode-agnostic; session mode survives restore (restore_snapshot does not touch world_view).

## world_view.h API (session contract)
- world_view_set/mode/width/left/raster, player_min/max_x, spawn_x
- edge tests: edge_less(x,c) unsigned, edge_less_s/greater/greater_s/ge_s/le_s signed, edge_eq
- edge_less/edge_less_s LEFT consts (<0xa0) map to signed-screen (c-96-WING margin); RIGHT (>=0xa0) shift +54
- edge_greater/_s LEFT consts shift -54 (>=0xa0 +54); edge_eq same mapping as less
- edge_right(c)/edge_left(c): raw shifted constants for position clamps/unsigned >= gates (left incl. c=0xa0 domain; needs c>=WING)
- hud_shift(owner): -28 (P1) / +28 (P2) byte-columns, wide only. Applied at all 0xc25f/0xc257/0xc267/0x78eb dest reads + literal 0x2d30/0x2d90. Tally rows + centered prompts untouched.

## Converted in world_dispatch.c (~160 sites)
- Pass 1: ~84 direct field compares (< 0x40/0x3f/0x20/0x10, > 0x1c0..0x1e0, -0x20 signed, eq 0x40)
- Pass 2: 58 var-based x/shot.x same patterns; x>=0x1a0/0x198 clamps -> edge_right; 0x1b0 joined right set
- Pass 3: >= 0x60/0xa0 unsigned gates -> >= edge_left (preserves wrapped-field union); signed <=0/<=-32/<=-48 -> le_s; >-0x10 -> greater_s; >0x80&&<0x1a0 cannon -> greater_s+less_s; <0x180/>=0x160 edge triggers; <1 terminus; decimal 64/16 -> less; (int16_t)x>0x60 -> greater_s
- KEPT literal: interior consts (0x100/0x120/0x160-volley/0x180-patrol? verify, 0x2a0), y-bounds, angle/speed/order/counters, var-vs-var, +0x31 world-scroll field at 27069 (x<32 = world boundary, not sprite X)
- world_player.c clamps -> player_min/max_x. world_stage.c spawns: edge entrants +-54 via spawn_x; anchored kinds {da13,db86,8309} excluded (world_anchored_kind); x==0 untouched.

## Verified
- tools/test_view.exe: all geometry checks pass (incl. edge helpers, hud_shift not yet in test)
- Arcade parity byte-identical after ALL conversions (3456 passes, BMPs+trace+actors same vs pre-change baseline)
- Wide: 428x240; wings have terrain+sprites; sub clamps at field 0x3B (screen -37, unreachable arcade); actors to x=0x1fb right wing
- Title menu: PLAY ARCADE first + PLAY WIDESCREEN via menushot BMPs (headless-title0/1)
- HUD anchored: P1 score left edge, P2 credits right edge in wide shots
- Autopilot (headless 'a' script rows): survives continue/gameover/title via coin+start edges in non-0xb2e or attract(0x28c&1) scenes; reached stage 2 in test

## Presentation
- world_video_scanline(width,left); sprite span clip vs signed left/right; layer sx formula already handles negative x
- world_window_present(src_w); paint_area crops 428->16:9 aperture ((2k+1)/3 cols/side); set_display client = 1280/3*scale for wide
- world_gpu.cpp: 16:9 aspect for 428 frames; light transport gated to arcade only (frame_w==320) - 320-fixed buffers
- world_audio_immersion.h: imm_half (160 vs 640/3), imm_lo/hi bounds, emitter x gate, ambient spread - all view-aware
- Scene/enhancement path (world_scene.c): 320-hardcoded everywhere; wide falls back to plain path (documented limitation, NOT converted)

## Stage ledger (build/wide-baseline/spawn-ledger.txt)
- 8 tables decoded: st0 471r(R329 L4 A67), st1 216r(R129), st2 245r(R147 L25), st3 334r(R229), st4 429r(R19 L4), st5 214r(R155 L2), st6 empty, st7 75r (attract?)
- Same edge-shift rule applies to all; anchored scenery excluded

## Autopilot park diagnosis (RESOLVED 2026-11-22)
- Symptom: campaign runs froze ~frame 3000, sub parked in 0x7c3b, camera 0x1f7, input 00, BOTH modes.
- Cause: autopilot returned `fallback`(=0) when no boss/stage45-camera actor found. During stage
  entry the sub descends under 0x7be5; with held=0 it drifts to x=0xc7 where probe masks stay
  solid -> reaches 0x7c3b with +0x6e=1 -> open_water returns 0 forever. Not a game bug: scripted
  input 00 starves the entry descent (live runs hold RIGHT and release normally).
- Also: InTheHunt.exe was STALE (empty $Root powershell invocation silently skipped rebuild);
  always build via `Build-WorldNative -Root 'D:\Arcade\AI\aCORES\inthehunt'`.
- Fix (test-only, world_native.c headless_autopilot): no-camera path returns `fallback|0x01|fire`;
  edge pulse gate also covers dead states 0x7eb1/0x7ee5 for continue handling.
- Verified: repro from stage00-2.snap now scrolls camera, sub cycles 7c3b->7be5->7a6a(live)->death->respawn.

## Campaign result (120k frames, both modes)
- Autopilot survives ~10-40k-frame cycles: play->die->continue->respawn, all within stage 0;
  never cleared the stage-0 boss (flies to boss X + fires but dies first). Stage snaps are live
  gameplay states (28c=0), usable for restore-based evidence.
- Arcade parity ALSO verified to run end (both traces healthy, no park).
- NEXT: stage-2+ coverage needs either a smarter autopilot (dodge, not just chase) or
  checkpoint/snapshot-seeded runs per stage; boss sheets pending same.
- WORLD_STAGE_SEED=n test hook added (briefing masters only; title validator needs 0x84==0).
  seed1-5 all boot+play in wide; wing actors proven to field 0x1f3 right / 0x0e left.
- P2 join attempt: edge 0x02 at 4100 didn't join (P2 idle 0x7b08); retry with later timing /
  confirm P2-start system bit. P1/P2 share the same world_view clamps by construction.
- TODO: stage reach audit from trace (which stage indexes appear); boss phase sheets; wing collision/item
  evidence; 2P; CRT/GPU matrix; evidence receipts in build/wide-baseline/receipts.md
- KNOWN GAP: effects/scene path in wide = plain render fallback (document, maybe accept)
- UPSTREAM NOTE: parked-respawn state (0x7c3b+6e under solid terrain) is a real soft-lock reachable
  only via already-parked snapshots; identical in arcade - documented, NOT fixed (parity).

## UPDATE — live-input harness recipe + attract discovery (this session)

**Attract discovery:** runs may sit in the attract/demo reel (`0x28C & 1`) instead of live
play; the demo sets `0x264 = 0xF` itself (not a join) and pads come from the ROM script
(`advance_boundary` overrides `raw_pads` when `0x28C & 1`). Trace now prints `28C` —
live play = `0x28C = 0`.

**How to get real live play headless (proven):**
- Coin needs interleaved samples: service coin = system bit `0x10` held 2+ vblanks →
  +1 credit (`0x2bb & 15 == 3`); two pulses = 2 credits (joins cost ≥1 each).
- P1 start = system edge `0x01`; mid-game P2 join = system edge `0x02` on a `0xb2e` pass.
- Edges die on multi-vblank ticks (`0x2aa` re-latched per `irq_tick`) → script joins with
  `--timing=remove-slowdown` (1-vblank ticks).
- `a`-rows run the autopilot which OVERWRITES `*edges` — edge columns on `a` rows are
  ignored; put edge rows as plain `frame held edges` rows.

**Co-op receipt (wide):** `build/wide-baseline/repro/headless-005301.bmp` — P1 mid-screen,
P2 in the right wing, native `PUSH 2P BUTTON` prompt, both HUD panels edge-anchored.
`0x264: 5 → F` live. P2 shares the widened clamps. P2 death → `0x7B08` idle (authentic).

**Re-qualification of earlier evidence:** clamp/spawn/edge receipts still valid — attract
runs real stage logic and the same view-aware code; the `7c3b` park was also reproduced
under live input (held-left during descent → sub pins at wide left clamp `0x3b`, `28C=0`,
`264=5`). "Autopilot campaign" coverage claims should be treated as demo-reel coverage;
live-input scripted runs are now the standard for qualification receipts.

**Script verbs:** `frame held edges [shot]`; `a` prefix = autopilot held-input; `shot`
saves BMP; `end x 0` ends + dumps actors; `snap`/`save` rows exist for snapshots.

## UPDATE 2 — arena parity + remaining open rows

- Stage-1 arena: identical trajectories wide vs arcade from the same snapshot
  (same death tick/position, pinned `7A6A@0xC7`, same game-over). Arena lock differs
  (`0xA10` wide vs `0xA88` arcade, ~wing-width) — consistent with view-anchored
  arena bounds; listed as a candidate documented exception in receipts.
- `WORLD_TRACE_EVERY=n` added (test-only) for transient-event tracing.
- Open: boss phase sheets (fight footage captured at stage-1 arena + stage-2 exit;
  per-phase handler detail pending), wing pickup itemization, ordinary-input clears
  (held-input dies at the arena identically in both modes — needs real play, not a
  wide fix), interactive GPU presenter.

## UPDATE 3 — session wrap state

- Weaving autopilot (test-only) pushed stage-1 progress to cam `0xC4E` (past the
  `0xA10` arena lock transit) — still no scripted clear; ordinary-input clears remain
  the deepest open row.
- `WORLD_PICKUP_LOG=1` + `WORLD_TRACE_EVERY=n` added as test-only envs. 42k-tick funded
  run collected zero pickups (autopilot doesn't chase drops); collection path proven
  position-agnostic at code level (`world_contact.c` overlap has no screen bounds).
- Geometry tests still pass (`all geometry checks passed`); today\'s edits are
  env-gated diagnostics or test-harness only — arcade parity unaffected.
- Plan doc has a Status block (docs/widescreen-gameplay-plan.md top) — implementation
  + verified evidence + open rows recorded honestly. receipts.md is the evidence log.

NEXT SESSION: for boss phase sheets use WORLD_STAGE_SEED + live-input recipe + funded
continues; stage-2 arena exit already proven live (STAGE CLEAR). For a full clear,
either a dodge-capable autopilot or windowed manual play is needed.

## UPDATE 4 — boss handler map + edge audit complete

- Extracted the full per-stage boss handler map from world_dispatch.c into
  receipts.md (S1 claw: boss_claw_step 0x11723..0x11B57 + boss_appendage
  0x1108C..0x1138B + wreck 0x10C8F/CD1/E0B; S2 1000:289C..4023; S3-mid
  1000:EC04..F196 + 7000:CCA9; S3-end stage_three_end_boss 0x1451A..0x15B53;
  S4 stage_four_boss 0x75F79..0x76B89; S5 7000:7FFE..93F7; S6 final_boss
  0x7B4BF..0x7C1AC battleship). Use handler-column transitions in
  WORLD_TRACE_EVERY traces to mark phase boundaries; S1 wreck-part spawn =
  "both side guns dead".
- Edge audit: every boss function's screen-edge test already routes through
  world_view_edge_* (claw +87, S2 +653/+815, S4 +749, final +164/+414/+428/
  +594/+692/+787). Boss parts retire/cull at the WIDE edges — no early wing
  clipping. stage_three_end_boss B700>=0x11BF is a world-camera milestone —
  correctly absolute. Remaining 0x140 literals are field writes, not edges.
- Stage-1 boss phase sheet stub written to receipts.md (trigger/lock, body
  composite, 0xC7 pin, fight footage, exit proof).
- Lessons DB updated (live-input recipe; attract/0x7C3B/arena/boss-map).

OPEN (unchanged priority): live boss phase-transition traces per stage
(handler-column watch during funded runs), empirical wing pickup/damage events,
ordinary-input full clears, interactive GPU presenter.

## UPDATE 5 — actor-trace hook + stage-1 composite confirmed

- `WORLD_ACTOR_TRACE=1` appends actor handler dumps to each trace interval
  (world_native.c ~1421). Rebuilt .local/native/InTheHunt.exe 18:38 (remember:
  dot-source the ps1 then call `Build-WorldNative -Root <abs>` — running the
  script file alone only defines functions).
- bossfight.snap restore run (44k, funded, weaving): S1 composite verified —
  5B93 side guns x4 (0x400), 5C73 x2 (0x460), 6781 claw (0xA00), 5D59 (0x600),
  parts spanning x FFFC..01E3 (both wings). No damage landed; player died at
  approach; restart hit the upstream 0x7C3B park. Wreck handlers (10C8F/CD1/
  E0B = both-guns-dead) located in code, not observed live.

## UPDATE 6 — wing pickup + wing damage empirical receipts

- Test-only autopilot gained pickup-seek (steers toward EA17/EA62/EB1C actors).
- WORLD_PICKUP_LOG captured 9 live collections: stage-4-seed run x 0090/0171;
  stage-2-seed run incl. **`x 0064` — left of arcade clamp 0x70 = LEFT-WING
  collection** (screen X ~4, unreachable in arcade). All by P1 (0a00).
- Wing damage proven: `sub 7eb1` deaths at field x `0x1B6` (> arcade clamp
  `0x188`) — twice, in two separate bossfight.snap restore runs, `28C=0`.
- Boss damage remains unobserved: composite (5B93/5C73/6781/64B6) is present
  at restore but withdraws during respawns; sub got ~5s live before game-over.
  Same survival blocker as ordinary clears — NOT a wide-code issue.

## UPDATE 7 — BOSS DAMAGE PROVEN (left-wing fight)

- bossfight.snap + held LEFT+FIRE: sub parked at field `0x3B` (wide left clamp)
  in `0x7A6A` ~1500 ticks; leftward fire damaged 3 composite parts —
  `5D59` 0x600->0x0C4, `64B6` 0x280->0x0BC, `6744` ->0x1B0. Untouched:
  5B93/5C73/6781 (armor/other-side parts — per-part eligibility confirmed).
- Visual `headless-007001.bmp` shows hull damage gaps. This is the first
  empirical boss-damage event and it's INSIDE the left wing.

## UPDATE 8 — S1 kill-chain mapped; gun kill needs real aim

- Weapons: 0x80 fwd torpedo (facing); 0x40 up-missile (spawn offset by facing)
  + depth charge. Kill-gate: B5C73 dies only via world_surface_contact
  (up-missile list 0xAEE6) -> parent+0x68 bit -> detonates B5B93 guns ->
  both-dead -> wreck 10C8F/CD1/E0B. 5B93 has NO contact check (detonation
  only). 5D59/64B6/6744 damageable via torpedo with hp floors (~0x0C4-0x1B0).
- bossleft recipe (hold LEFT+FIRE = parks at field 0x3B left-wing clamp)
  reliably damages body parts; scripted surface-aim variants never broke a gun.
  Gun kill needs real play.

## UPDATE 9 — dodge bot + surface-kill aim limits

- WORLD_AUTOPILOT_DODGE=1: proximity evasion of +0x0d actors; fresh stage-0
  play ~5000 ticks/2 deaths (measurable improvement, still dies at arenas).
- 5C73 aim: parked at gun_x-8 firing up-missiles 1200 ticks, zero break.
  projectile_contact_list is shape-overlap only; the surface weapon path
  misses the gun's shape from parked positions. Boss kill needs real play.

## UPDATE 10 — dip extremes + remaining

- WORLD_DIP=l,d (test-only) applies cabinet switches headless; wide plays at
  both extremes (0,0 fast game-over vs 3,3 longer run).
- All scripted-harness evidence rows now covered. Remaining genuinely open:
  ordinary-input full clears + boss kills (need human-grade aim/survival —
  scripted bots die at arenas in BOTH modes identically) and interactive GPU
  presenter (headless GPU/CRT paths already proven).

## UPDATE 11 — co-op join recipe solved deterministically

Script semantics were misunderstood: headless rows are ABSOLUTE ticks with
LEVEL values (`at held edges`), not durations/pulses. Each asserted `edges`
value latches `0x2AA` once per row — re-firing needs an idle row first.
Working recipe: `0x10` pulses (~6 ticks high, >=6 idle) each credit once;
spare credit + `0x02` edge during live `0x0B2E`/`0x2218` masters joins P2
(`264: 5->F`, `28C=0`, `2B8` decrements). Boot phase (28C=4, master 1DE6)
latches nothing — `irq_tick`/`0x2AA` start once the title master runs.
Join gate (world_title.c:284): `0x2AA&bit`, `!(0x262&2)` (boss-kill
cinematic blocks joins), credits >= cost (1 + 2-coin pricing flag), or
free-play dip.

Captured this run: join, P2 death window (`264: F->D`), respawn, co-op game
over (`264=C0` + master `0x0C04` at 4:3), funded continues, attract fallback.
Open: `264=7` (P2 alone with P1 fully out) — parked subs sit in a safe
pocket while the camera pin holds, so scripted P1 never dies first; the
wait/respawn handlers are player-symmetric, coverage is substantial but the
mirrored window is unobserved.

Trace gained columns: 2B8 (credits), 2AA (edge latch), 262, 80 (master),
2C4 (free-play dip), BA80/BA84/282 (irq gate) — all test-only.

## UPDATE 12 — matrix + P2-only

- 264=A (P2-only live, P1 out at 7A6A continue wait) captured once at
  28C=0/0x0B2E — nondeterministic (whoever dies out first). co-op continue
  frame: PUSH 1P/2P prompts edge-anchored over the mine field.
- Difficulty x slowdown matrix: all four cells (0,0 & 3,3 dips x arcade &
  remove-slowdown) reach live wide play; arcade timing overruns ~7-10% of
  passes under load; hardest dips add ~14% mean actors.

## UPDATE 13 — weapon stocks + next lever

- Sub ammo counters: +0x44 torpedo, +0x46 up/surface-missile (the 5C73
  kill weapon), +0x48 depth-charge. bossfight.snap: P1 has +0x46=0 (its
  scripted 0x40 runs could never spawn 7D missiles); P2 has +0x46=2.
- The 0x7DA36 surface missile registers on the 0xAEE6 contact list for
  its entire waterline run — kill only needs the run to cross a 5C73
  shape; aim = park armed sub beside gun, face so the run sweeps it.
- Co-op lifecycle + P2-only (264=A) + difficulty x slowdown matrix all
  receipted this session. Remaining open: ordinary clears, live boss
  kill, GPU presenter — all need human play or an armed-sub boss run.

## UPDATE 14 — surface missile traced live

- Weapon chain verified: 0x40 edge -> fire_upward -> 9e73 -> 9eb2
  flight (~50 ticks, crosses the field). +0x46 is an in-flight counter
  (seed 2, refunded on despawn), NOT fixed ammo — every live sub can
  fire it once spawned fresh.
- Edge-word input: b742 latches rising edges only; scripts must pulse
  0x40 (e.g. 4 ticks on / 8 off) not hold it.
- 0xAEE6 surface-contact list = flat array (count+ptrs); actor trace
  dumps it now. Missiles cull at view edges (<0x40/>0x1c0) — wing-clamp
  spawns die instantly (correct, refunds stock).
- Still open: getting 9eb2 -> 7da36 surface-run onto 5c73 (aim/surface
  proximity), live boss kill, ordinary clears, GPU presenter.

## UPDATE 15 — kill-chain closure at code level

- 5C73 killable by three 0xAEE6 weapons (all view-edge-aware): 9FA1
  surface runner (needs sub floated on the waterline, medium==3), B4CE
  homing missile (equipment upgrade, self-seeks surface targets), BB7E
  salvo (level-3). Underwater 9EB2 variant verified live in wide mode.
- The live 5C73 kill now needs a surfaced sub at the waterline or the
  homing/salvo upgrade — ordinary play beyond scripted patterns, same
  blocker as clears. Mechanism is wide-correct end to end.
- Plan status block + receipts updated; geometry tests pass.

## UPDATE 16 — homing missile proves the intercept path

- Injected 0xB4CE ran real homing: b553 picked a 0xB6EE target, b57b
  curved onto the composite, 0xAEE6-registered every tick, blast on
  impact. 0xB6EE list contains both 5C73 guns (entries 0/2) + 4x 5A04.
- Boss-part +0x21 positions recompute per frame (snap patches revert) —
  can't teleport guns; aim must be real.
- Live 5C73 kill still needs the volley to land on a gun specifically —
  the homing targeting is round-robin over 6 entries, so ordinary play
  covers it. All mechanism links verified; overlap is the last step.

## UPDATE 17 — 5C73 surface-kill chain proven to the death step

- Spawn-assisted homing missile: 5C73#1 took -0xA0 damage (0x460->0x3C0)
  then died 5c73->5cc6->5cd0, live wide mode. The whole gate is verified:
  0xAEE6 registration -> overlap -> world_surface_contact -> damage ->
  death handler.
- Second gun's kill, 5B93 detonation and wreck handlers remain
  unobserved — needs both-gun kill (missile aim was eaten by a nearer
  5A04 part; real volleys cover all targets round-robin).

## UPDATE 18 — full 5C73 kill chain + 5B93 detonation + handler gap fix

- Both deck guns killed live (5c73->5cc6->5cd0 x2) via B57B homing
  missiles; the 5B93 side guns detonated via parent+0x68 as designed.
- Composite slid left off-edge afterward (parts to x 0xFF4A..).
- Found and stubbed missing handler b000:6d91 (dead-gun husk smoke);
  the run previously aborted on it. Cosmetic-only stub pending decode.

## UPDATE 19 — wreck confirmed as phase-gated, needs hull-part kills

- 10C8F/CD1/D10/D8C/E0B read: parent-pinned wreck children (explosion
  ring, debris, terrain-bounced hull parts). They fire only on the
  composite parent's wreck transition.
- With all hull parts patched to 1 hp, nothing hit them in 2000 ticks —
  they die only to 0xA6E2-list projectile contact, same as arcade. The
  composite meanwhile slides left through the wing (x down to 0xFF4A),
  correctly using the wide field as it repositions.
- Remaining unobserved = wreck transition itself + deeper phases; all
  mechanically testable links (gun damage, gun death, side-gun
  detonation, composite reposition) are verified live.

## UPDATE 20 — complete S1 wreck observed end-to-end

- Parent warship = actor 3680 (b000:5824), resolved live via a new
  test-only trace field (warship_attachment now stashes its parent to
  0xba90; trace prints W lines).
- Damage model: hull parts are 0x7f00-regenerating shields; real
  damage drains the shared parent+0x48 pool via 0xA6E2 hits.
  6CA8 = moving one-hit weak point -> parent+0x26=0 instantly.
- Wreck observed: 5824 -> 595D -> 5975 -> 59B3 -> despawn with ALL
  parts (parent+0x0A=2 cascades). Screenshot headless-005081.bmp.
- The earlier "composite slid left" was its scripted approach path
  (view_edge_le_s(x,-48) despawn), not a wreck.

## UPDATE 21 — interactive GPU presenter verified live (wide)

- Real window run on user's RTX 4090 D3D12 path: 3072x1728 16:9 client,
  title-test phases through live master 0x0B2E, 3595 DXGI presents,
  1800/1800 passes 1:1 with refreshes (no judder), 0 frames lost.
- Windowed+GPU+wide closes the presenter row except fullscreen/VRR
  (same swapchain path; not exercised to avoid display takeover).

## UPDATE 22 — S1 claw boss: spawn -> fight -> kill -> STAGE-2 ENTRY, all live

- Boss = claw giant-sub (1000: family), separate from the mid-stage
  warship composite (5824 wrecked earlier). Spawn row 469 of the
  stage-0 table: B700=0x16A0, kind 1000:0917.
- ~50 arm segments 1000:14F1 + appendages 108C + claw steps
  1742/1819/1B57 + master 0A6B->1BCF phases all live.
- Kill via master +0x26=0 (calm-gated): 262 |= 0x80, master
  0EA3->0F16 jitter-drift exits x<=0 -> 263|=0xC0 -> STAGE 0->1
  transition, live stage-2 opening verified (screenshot).
- Interactive GPU presenter also verified this turn: real 3072x1728
  D3D12 window, 3595 DXGI presents, 1:1 pass:refresh, wide title->
  game->reset->game cycle to live master 0B2E.

## UPDATE 23 — S2 boss: spawn -> bodies -> kill -> STAGE-3 ENTRY, all live

- Master 1000:298C +0x40 counts 2 bodies; bodies 1000:2AF6 x2 (hp
  0x3C00) die via +0x26=0 -> 0x2D79->0x2DBD->0x2DEC wreck;
  master 0x298C->0x29A4, 262|=0x80 -> 0x263|=0xC000-equivalent ->
  stage 1->2 transition at ~4740. Screenshot headless-002681.bmp.
- S1+S2 boss kills now proven end-to-end; same recipe (spawn-table
  cursor forward + master/body +0x26=0 + 0x264|3) applies to S3-S6.

## UPDATE 24 — S3 end boss: kill -> wreck -> STAGE-4 ENTRY, all live

- Two-phase boss fully decoded: 465A surface fight (0xAEE6 only) -> 47CB ->
  4811 restores hp -> 48A4/49AB/49CE phase-2 kill check -> 4C19 wreck
  ascends via 0x4506 flight script -> 0x262|=0xC000 -> 0BB3/0BED ->
  2576/25B5/25FF/0A0E -> 2218 briefing -> 2282 -> stage 3->4 (index).
- Wreck frames headless-002991/003111.bmp (explosion ascent).
- Assists: handler 465A->49CE + hp 0 (real kill code path), 0x42=1,
  263&=~8, 28e=0x0bff, 28c->0 demo->live conversion, 264/266/2b8 fixups.
- Demo-vs-live discovered: 0x28C&1 demo flag + 0x28E replay cursor >=0xC00
  = reel end (wipes actors; looked like game over in all S3 traces).
  s3mid/s3end/s3fight snaps were demo-reel; logic identical, inputs differ.
- S1 warship + claw, S2 twin-hull, S3 mid-boss damage (ordinary autopilot
  fire) and S3 end-boss kill->transition all proven. S4-S6 remain: same
  recipe (stage-table decode -> spawner cursor -> kill target +0x26=0).

## UPDATE 25 — S4 boss: kill -> 5D89/5DA7/5DBD wreck -> STAGE-5 ENTRY live

- Row 333 (trig 0x153E, 7000:5AA2) via spawner +0x40 BYTE-OFFSET cursor.
- s4b_wound: 5F79/5FA0 hit -> parent+0x26-=dmg -> +0x26=0 -> 262|=0x80 -> 5EB7.
- Body: 5D89 -> 5DA7 rise -> 5DBD -> 0x263|=0x80 -> stage 3->4 live.
- Kill assist: injected 0xAC0E blast torpedo (seg 0000, +0x38=0 hover) at the
  5FA0 hitbox center (cx=-0x40 off +0x21 anchor) + body +0x26=1 + 0x264|3.
- 0xA6E2/0xAEE6 lists rebuild every frame; snapshot 0x84FE parent table is
  stale — resolve parents via live trace + s5_parent semantics.
- S1 warship+claw, S2 twin, S3 mid+end, S4 done. S5 (ice boss) + S6 (final)
  remain — same recipe. Ordinary-input clears + fullscreen/VRR still open.

## UPDATE 26 — S5 ice boss: core kill -> hull wreck -> STAGE-6 ENTRY live

- Core 0x1580 (0x8337 child of hull 0x1100): 8534 exposed + +0x26=0 ->
  85E2 -> 85FA -> parent+0x0a|=2 -> hull 80C9 B(0x0a)&2 -> 262|=0x80 ->
  826A/8304 destruction -> 263|=0x80 -> 0BB3/0BED -> 2576 -> 2218/2282 ->
  0B2E -> stage 4->5 live at ~7350.
- S1,S2,S3(mid+end),S4,S5 all verified end-to-end. S6 final boss remains:
  final_boss 0x7ABDC init spawns 4 arm parts + BDEF/AF1C; no table row —
  spawns via driver (0xC458 lift platform moves at B700>=0x570).
- Ordinary-input clears + fullscreen/VRR + both endings still open.

## UPDATE 27 — S6 final boss + TRUE ENDING: campaign end verified live

- Row 213 @ C3FA trig 0x0FE0 kind 7000:ABDC. Master 0x1300=ACB5
  (hp 0x5800) + B3B7/B99E arms + BDFD/AF56/BEE0/BC3C/BC89/BBFD parts.
- Kill: ADD9 + +0x26=0 -> hull_death -> 262|=0x82 -> AED8 drift ->
  262|=0x40 -> 0BB3/0BED -> stage index 6 ENDING -> 0x33A9 -> 0x8F82
  true-ending actor (264==3 gate -> 0x905F). Sub +0x3C=FFFF survives.
- ALL SIX STAGE BOSSES now verified end-to-end live in wide mode.
  Ending escape scene screenshotted (subs + countdown).
- Remaining: ordinary-input clears, both-endings matrix, fullscreen/VRR.

## UPDATE 28 — Cold-boot autopilot now self-starting (harness fix)

- Nonplay edges were single-frame pulses that never satisfied the 2-vblank
  service-coin debounce — a0 runs sat in 0x1DE6 forever. Fixed: 0x10 held
  2 ticks, 0x01 start, 0x40 pad-fire for 0x2218 briefings.
- Verified cold boot -> coin -> title -> start -> 0x2218 -> 0x2282 ->
  0x0B2E live play. First a-row must be early (rows apply at their frame).
- DODGE bot reaches B700=0x070D stage 1 then dies at the arena — same as
  arcade. Ordinary clears still need human play; evidence documented.

## UPDATE 29 — Both endings verified (true + normal + quick route)

- True ending: 0x33A9 hold + 0x8F82 actor (264==3, B8A0!=0).
- Normal ending: 0x344D..0x35F9 full two-scene chain observed live;
  surfacing-sub story screen + credits captured as PNGs.
- Quick route 0x342A->0x35BE also observed (B8A0==0).
- All ending sequences run in wide mode unchanged.

## UPDATE 30 — P2-only survival + both endings + cold-boot harness fix

- WORLD_AUTOPILOT_P2=1: P2-only coin->start->play verified (264=0x0A).
- Both endings verified: true (0x33A9+0x8F82, 264==3), normal
  (0x344D..0x35F9 two-scene chain, captured), quick (0x342A).
- Autopilot nonplay edges fixed: service-coin 2-vblank hold + start +
  pad-fire; cold boot now self-starting in both modes.
- Still open: uninterrupted ordinary-input clears (human play), and the
  hardware-manual checks (fullscreen/VRR, gamepad, GPU co-op stress).

## UPDATE 31 — full wide-mode audit: every stage + every boss

Goal: audit widescreen mode across all stages/bosses; fix issues; done when
all verified correct.

- Visual audit: `audit-s0..s5` PNGs — all six stages render correctly wide:
  continuous scenery across wings, wing-active sprites/enemies/mines, HUD
  anchored at wide edges, no seams/stretch/misplacement.
- Camera audit: scroll origin/limits are ROM scroll-script rows on world
  B700 milestones — world-anchored; S1 arena lock delta is the wider view
  edge at the same world boundary (not a regression).
- Spawn audit: ~800 edge-zone rows classified; `world_anchored_kind`
  (tile painters) sufficient; screen-pinned emitters shift correctly.
- TWO REAL BUGS FIXED — ROM-loaded left-edge despawn limits were never
  view-translated (constants were; table values weren't): wide actors
  despawned ~54px early -> left-wing pop-out.
  - 0x7C33D stage-6 scenery (0xC312, ~10 rows): -> world_view_edge_less_s
  - 0xBBBE6 b000 launcher: -> world_view_edge_le_s
- Verified: test_view passes; both modes cold-boot to live play; arcade
  path of both fixes is a no-op (helpers return raw compares off-wide).
- Combined with prior evidence (all 6 boss chains end-to-end, both
  endings, wing clamps/pickup/terrain/co-op), every stage and every boss
  is verified correct in wide mode.
- Remaining (outside this audit's scope): ordinary-input clears + manual
  hardware checks — human-gated, as before.

## UPDATE 32 — despawn-fix differential verified live

- WORLD_SNAPSHOT_LOAD harness recipe documented (no --from option).
- Injected 0xC33D actor (x=0x40, ROM limit 0x60) survived ~200+ ticks in
  wide before retiring at the wide edge — the exact pop-out case, now
  firing at limit-54. Under pre-fix code it retires on tick 1. Arcade
  mode unchanged.

## UPDATE 33 — spawn audit complete (all six stages)

- Corrected audit to field space (+0x21 = screen + 96). Real hole found:
  authored X == 0x1A0 spawned 1px inside the wide right wing; threshold
  is now >= 0x1A0 (was > 0x1A0). 12 non-anchored rows affected.
- Added drain-time clamp: late-drained right-edge spawns pin to field
  0x1D6 instead of landing in the aperture on camera overshoot —
  proven live on a forced stage-2 drain (e9d2 rows all at fx=0x1D6).
- Two constant child spawns (0x1B8 / 0x1E0) now view-translated for
  authored entry distance.
- Evidence: static audit of all 1909 table rows — zero conversion
  holes; live traces — 1126 wide spawns, 0 pop-ins; arcade — 342
  spawns, conversion provably no-op. test_view passes.

## UPDATE 34 — non-table spawn paths closed

- Exhaustive +0x21 sweep: constants 0x1B8/0x1E0 (incl. S3 end-boss init)
  and ROM-table loads (1000:40F4/4024/4028, B000:A5D8) now
  view-translated; in-view formation coords, parent-relative children,
  linear-script anchors and the ending arena clamp verified untouched.
- Re-verified: 727 wide spawns, 0 pop-ins; drain clamp holds; both
  modes boot live.
