# In the Hunt arcade decompilation

**First playable target:** the World parent set, `inthunt`, on Windows. **Finished target:** readable native source for the arcade game's main and sound programs, all stages, both players, endings and operator paths, with an independently selectable enhanced presentation. The donor search found hardware implementations for this exact game and a suitable native shooter frontend. It did not turn up a public gameplay decompilation under either regional title.

## Shortest route to playable

**Current native build.** The end-user launcher runs a Windows x64 executable. Cold boot, live input, the title, briefing, Arctic opening and framebuffer are implemented in C. The input-only cold-boot run now continues through player death and respawn to 2,500 points. Upgraded secondary weapons, enemy turn states and additional pickups are recovered. The local reference harness remains available to developers. The executable stops and identifies the missing routine when play reaches an untranslated path.

**Timing priority.** Original arcade slowdown is the default target, with a persistent Remove Slowdown option. Recover original per-block execution time, prefetch and board waits, then service every busy VBlank at its actual checkpoint. Validate the resulting frame/pass sequence against the World reference with live inputs. Keep audio and authored timers on the physical board clock. The current clock primitives and scene-transfer waits cover only part of that work.

**Next native milestones**

1. Extend the Arctic encounter across alternate player routes, surface enemies, enemy fire and player death. Compare newly translated handlers against local traces.
2. Finish unresolved sound sequences and verify the full-game sound output. Native V35 task scheduling, FM notes, PCM effects, YM2151/GA20 synthesis and Windows playback are now connected.
3. Finish the first boss and stage transition, then reuse the actor, script and audio implementations through subsequent stages, endings, attract and service modes.
4. Wire the presentation and progression features below into the native build. Keep the game at the original aspect ratio and keep ROM assets in the private import directory.

**Same-board comparison.** Undercover Cops and its Alpha Renewal editions share the M92-E-B (05C04238B1) ROM board with In the Hunt. Both use the standard M92 machine configuration. Each game uses its own sound-CPU opcode mapping. [Irem M92 hardware driver](https://github.com/mamedev/mame/blob/master/src/mame/irem/m92.cpp)

## Recovery workflow

1. **Import World once.** Read the supplied merged 7z locally. Use MAME's `ROM_START(inthunt)` for member names, CRCs and region sizes. Interleave L0/H0 at even/odd addresses from 0, L1/H1 from 0x80000, and SL0/SH0 in a separate sound image. Keep original sound data reads and V35 opcode decryption distinct. Decode tile, sprite and GA20 sample regions once. Leave ROMs and generated asset caches outside source control.
2. **Developer reference only.** Start with the pinned MAME NEC cores and M92 maps, IRQs, sprite buffering, playfields, palette, sound latch, YM2151 and GA20. Expose frame step, two-player input, framebuffer, audio and state serialization. Reach title → insert coin → start → steer and fire with sound. Use the same interface throughout native recovery.
3. **Recover gameplay in playable slices.** Trace reset, interrupts and the frame dispatcher first. Replace routines at known entries, keeping address, inputs, outputs and side effects beside each recovered function. Follow player movement and both weapons through collision, enemy spawning, scrolling, bosses, scoring, death and continue. Use the World MiSTer MRA's stage index (0xE0084), timer (0xEB8A0) and P1 lives (0xE0118) as starting points for watchpoints. Recover stage scripts and tables as data; reuse common handlers across stages. Replace the V35 command, music and effects handlers behind the existing audio interface.
4. **Wire the frontend as soon as one stage plays.** Keep the donor's shared menu renderer, settings and HUD panels. Adapt its action callbacks to the M92 interface. Feed optional overlay panels from named game state; render the original score, lives and timer through the game's own layers. One settings registry drives the launcher, pause menu, presets and command line.
5. **Close the remaining program paths.** Track each executable region as recovered, data, padding or unresolved, including indirect dispatch and service routines. Finish attract, two-player join, every stage, last battle, endings and sound commands. Completion requires native execution of both game programs with the temporary CPU fallback disabled. Then map the US, Japanese and provisional Korean variants from the archive by program differences against World.

## Menu and enhancement parity

Use the pinned CPS2 collection for common options and its Giga Wing module for shooter modes. Preserve the donor's page order, choice lists, defaults, hotkeys, session-only flags and persistence. The following local files define the implementation scope; use the registrations themselves when wiring each option.

| Surface | Source under `donors/cps2recomp/` | In the Hunt implementation |
|---|---|---|
| Pause menu and HUD | `recomp-common/menu.cpp`, `port/menu.cpp`, `port/hud.cpp` | Online, Display, Game, mode pages, save/load/slot, coin, P2 join, test, reset options, reset game, quit and resume. Game contains speed, mute, Board, Cheats and Sound. Keep keyboard and pad navigation. |
| Shared enhancements | `port/main.cpp::RegisterPortFeatures`, `launcher/`, `docs/FEATURES.md` | Pure/Enhanced presets, run-ahead, arcade slowdown timing, rapid fire, aspect-preserving scaling, shaders, rendering backend, VSync, window size, custom music, online hosting, ping, names and delay. Carry through bindings, screenshot, pause/frame advance, replay and rollback co-op. Serialize once for states, rollback, run-ahead and replay. |
| Shooter options | `shmup/modes.cpp`, `games/gigawing/src/{game,board,modes}.cpp` | Free play, skip intro, Boss Rush, Score Attack, timed caravan, automatic stage viewer, attract selection and records. Bind stock, weapons, invincibility and timer to actual submarine state. Board exposes the original DIP fields, including lives, difficulty and any-button start. |
| Title-specific settings | `games/*/src/`, `docs/FEATURES.md` | Track donor-only rows separately when assigning equivalents. For example, map player labels to submarine crews. Giga Wing's hidden fighter and fighting-game roster selectors have no established In the Hunt counterpart. Keep such rows explicitly marked for a design decision; never count an inert toggle as implemented. |

## Modes and achievements

**Boss Rush** starts at the first boss and advances automatically through arcade encounter order. Carry lives and loadout between fights. Stop on game over or final victory. Rank local runs by bosses defeated, then by shortest active time to defeat that many bosses. Use the recovered boss entry routines to initialize the arena, camera, spawns and music together.

**Score Attack** starts a solo run at the beginning of the campaign with one credit, World default difficulty and stock. Record the game's score at game over or the ending. Keep the best local score and show the previous best on the results screen. Both ranked modes use fixed rules. Cheats, state loads and frame stepping invalidate a record. Exclude practice, hitbox overlays and player-selectable stage starts across the launcher, menus, cheats and CLI.

**Achievements** use the names and unlock conditions in [achievements.json](achievements.json). Add an Achievements page and a dismissible unlock notification. Evaluate live, confirmed gameplay events. Attribute weapon kills to the firing player and crew awards to both participants. Keep run counters with game state and earned IDs in the local profile. Ignore attract, replay and speculative frames. Loading a state or enabling a gameplay cheat makes the rest of that run ineligible. Free play, remapping and display settings remain eligible. A fresh run resets eligibility.

### Service record and unlock presentation

Ship 40 commendations, ten per tier. Easy rewards the first sorties. Medium introduces weapon and crew challenges. Difficult demands full runs or sustained survival. Extreme combines single-run feats with persistent campaigns measured in dozens of successful clears. Persist counters for the long awards and show current/target counts on their detail pages. For multi-part awards such as Deep Patrol, show both counters. Roll confirmed events into lifetime totals exactly once, including runs that end in game over.

Use [the offline visual prototype](achievement-preview.html) as the art and interaction reference. Open Service Record from the pause menu or launcher. Use oxidized steel frames, warm ivory lettering, a pixel-art submarine plate and stamped rank shields. Mint I, blue II, brass III and coral IV identify the four tiers in both text and shape markings. On the cabinet screen, present one scrollable list with a detail page for the selected medal; the wider launcher can show list and details together. Support D-pad, confirm and back. Show locked conditions, earned dates and progress.

Queue unlocks in earned order and show one compact medal plate at a time for 4.5 seconds. Use a brief upward entrance and an optional soft sonar ping; respect mute and reduced motion. Anchor the plate in a free display margin when available. At native aspect, defer the banner until a stage transition or results screen if it would obscure play. Put the full condition on the menu detail page. The browser preview demonstrates medal selection and the popup; the native game supplies events, progress and input.

**Acceptance with minimal overhead.** Reuse one recorded World input sequence covering boot, movement, both weapons and a P2 join. At each native substitution compare the affected state or frame to the reference run at that boundary. Reuse stage-start states for boss, ending and sound regressions. At release, run through in Pure with fallback disabled and round-trip the implemented options against the pinned donor registry.

## Cabinet framing

Keep the full 320 × 240 picture at 4:3 in every mode. Center and fit it within the window or fullscreen display. Exclude widescreen, stretch-to-fill and cropping from launcher, settings and shortcuts. Replace the donor camera zoom option with aspect-preserving window or integer scaling.

Keep black borders as the default. Optional treatments for unused side space are an original illustrated submarine porthole bezel, or a subdued underwater light effect with slow caustics and drifting particles. Clip decoration to the margins, dim it independently, offer a still frame for reduced motion, and collapse it entirely on a 4:3 display. Generate or author decorations separately from the game ROM.

In Display settings, label the choices **Black**, **Pressure hull** and **Deep water**. For Pressure hull, use shadowed steel ribs and a few recessed rivets, with the inner edge fading to black. For Deep water, let faint shafts of blue-green light drift near the outer edges. On resize, fit the game picture first and crop the border artwork into the remaining space. Narrow margins can show just a sliver of steel or light. Keep the HUD and achievement popups inside the game viewport so both remain readable at 4:3.

## Display, timing, audio and controls

- Default to centered uniform integer scaling of the 320 × 240 playfield. Apply the CRT pass inside the viewport, and compose black or original selectable cabinet borders outside it. Keep curvature, overscan and tilt fixed at neutral.
- Ship flat CRT-Geom and CRT-Geom Deluxe in the Windows hybrid. For the standalone renderer, evaluate CRT-Royale for high-resolution phosphor masks, Guest Advanced for adjustable scanline and bloom response, and CRT Aperture for an aperture-grille look. Verify flat geometry and GPU cost on actual target displays before selecting native defaults. [Libretro CRT catalog](https://docs.libretro.com/shader/crt/) and [CRT-Royale settings](https://docs.libretro.com/shader/crt_royale/)
- Drive simulation at 6,666,666 / (424 × 262) ticks per second. Schedule inputs, collision, score, sound commands and achievements on simulation ticks. Present the most recently completed state independently as fast as the display permits. Preserve arcade sprite positions for the integer-pixel profile.
- Preserve the original YM2151 and GA20 mix and mono routing. Drive audio from the board clock, accumulate fractional output samples across ticks, and maintain pitch when presentation rate changes.
- Provide Start, Options and Exit on launch. Keep settings reachable during play, expose both players' bindings, and retain settings between sessions. Persist arcade, Boss Rush and Score Attack records separately once the additional modes are playable.

## Current native coverage

The reproducible encounter starts at frame 1812 and runs through frame 3699. Continue through later stage-one enemies and recover stage transitions, then replace the playable hybrid dispatcher with native execution and connect native video and audio output. For each progress report, include screenshots from the current build and label the execution path shown. Keep all gameplay screenshots outside the source package.
