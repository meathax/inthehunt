# In the Hunt — native Windows port

Irem's 1993 submarine shooter *In the Hunt* (World set `inthunt`), rebuilt as a native Windows x64 game. The original V33 main program and V35 sound program were translated routine by routine into portable C. The arcade CPUs are not emulated. All six stages, every boss, both endings and two-player co-op run from power-on on translated code. Original music and effects are kept, along with an approximation of the arcade slowdown.

You supply the game data from your own copy of the World ROM set. This repository contains no Irem code, graphics or audio.

---

## Quick start

1. Download the Windows x64 release ZIP and extract **all** of it to a writable folder.
2. Double-click **`play.cmd`**.
3. On first launch, pick your `inthunt.7z` (MAME World parent set). The launcher also looks in `Downloads` and next to `play.cmd`. The chips are SHA-1 checked and imported into `.local/world`; later launches no longer need the archive.

From a source checkout, `.\play.ps1` does the same and builds the executable first if the sources are newer (the pinned Zig C compiler is fetched on first build). To import a different archive: `.\setup.ps1 -Rom 'D:\Games\inthunt.7z'`.

## Controls

| Action | Player 1 keyboard | Player 2 keyboard | Xbox-style controller |
|---|---|---|---|
| Move | Arrow keys | W A S D | D-pad or left stick |
| Torpedo / fire | Z | G | A |
| Secondary weapon | X | H | B |
| Insert coin | 5 | 6 | Back |
| Start / join / continue | 1 | 2 | A |
| **Pause menu** | **F1** or Esc | **F1** or Esc | **Start** (or L3 + R3) |

Each controller drives its own player. Key and button assignments are stored in `.local/world/native-settings.ini`.

## Title, pause and options menus

The game opens on a **title menu** over the running attract sequence: **Start Game** (drops a coin and presses start for you), **Options** and **Exit**. During play, the controller's **Start** (or **F1**/Esc) freezes the game and opens the **pause menu**: **Resume**, **Options** and **Exit to Menu** (confirmed; resets the board to the title). On a controller, A is the cabinet's start button whenever the game asks for one (second player join, continue). The menus use the game's own lettering and colours. Up and down select, left and right change a value, Enter/A confirms, Esc/B goes back. Each line has a short description at the bottom of the screen.

- **Options**: four tabs (Q/E or the shoulder buttons switch tabs; long lists scroll)
  - **Video**
    - Display: window or full screen
    - Window size: sharp ×1 to ×8
    - Scaling: Integer (sharpest), Fit 4:3 (largest picture that keeps the shape) or Stretch (fill the window)
    - CRT shader: Off or one of twelve CRT looks (see below)
    - Frame pacing: Smooth (default), Exact 60.01 or Timer (see below)
    - V-Sync and FPS limit: Timer pacing only (FPS limit: Arcade, 90, 120, 180, 240 or Uncapped)
    - Show FPS: frame rate and game speed in the corner
    - Border art (Black or Undersea) and border brightness
  - **Sound**: master volume, plus separate levels for the FM synth (YM2151) and the PCM samples (GA20)
  - **Game**
    - Slowdown: Arcade keeps the original slowdown; Off runs busy scenes at full speed
    - Difficulty (Very Easy, Easy, Normal, Hard), Submarines (2–5) and Demo Sound: the cabinet's own DIP switches, read by the game at power-on; a changed switch resets the board at the next Start Game or Exit to Menu
    - Auto pause: open the pause menu when the window loses focus
    - Controls: the keyboard and controller layout
    - Reset options
  - **Enhance**: optional picture enhancements (see below)

Settings are saved when the menu closes. Esc no longer quits the game outright.

**Frame pacing.** The board refreshes at 60.0125 Hz. With **Smooth** or **Exact**, the game locks to your display when its refresh is a whole multiple of that (60, 120, 180, 240 Hz, within 0.6 %): it presents on every refresh through a DXGI flip-model swap chain and runs one game frame every *k* refreshes, so every frame is on screen for exactly the same time and scrolling is perfectly even. Measured on a 120 Hz display over 130 s: every present landed on the next refresh (DXGI statistics) and every game frame was shown for exactly 2 refreshes, apart from the original's own slowdown frames (4 refreshes each).
- **Smooth** runs the game at the display's pace: 60.000 Hz on a 60/120 Hz display, 0.02 % slower than the board.
- **Exact 60.01** keeps the board's 60.0125 Hz by running one extra game frame whenever a whole frame of drift has built up (about once every 80 s at 60/120 Hz).
- **Timer** is the free-running board clock for variable-refresh (G-Sync/FreeSync) displays or refresh rates that are not a multiple of 60; there V-Sync and the FPS limit apply. The FPS limit only sets the redraw rate; the game speed is fixed.

Sound never blocks the game: it is queued and stretched or squeezed by at most 0.5 % to stay in step with whichever clock the video runs on.

**What is not a pacing fault:** the original scrolls in whole pixels from a fractional camera, so a pan of 1.25 pixels a frame moves 1, 1, 1, 2 pixels (measured in MAME), and the stage-opening palette fade makes the original take two frames per pass for about a second while the submarine enters. Both are reproduced; Remove Slowdown removes the second.

### CRT shaders

**CRT SHADER** runs the picture through real RetroArch shader presets, using [librashader](https://github.com/SnowflakePowered/librashader) on Direct3D 12:

| Menu | Preset | Character |
|---|---|---|
| CRT-ROYALE | `crt/crt-royale` | TroggleMonkey's physically modelled tube: aperture grille, halation, bloom |
| ROYALE PVM | `presets/crt-royale-kurozumi` | Royale tuned as a Sony PVM broadcast monitor |
| GUEST ADVANCED | `crt/crt-guest-advanced` | guest(r)'s highly configurable modern CRT |
| GUEST NTSC | `crt/crt-guest-advanced-ntsc` | Guest with composite NTSC signal artefacts |
| CRT-GEOM | `crt/crt-geom` | cgwg's classic curved tube |
| CRT-LOTTES | `crt/crt-lottes` | Timothy Lottes's shadow-mask CRT |
| CRT-HYLLIAN | `crt/crt-hyllian` | Hyllian's sharp scanline CRT |
| EASYMODE | `crt/crt-easymode` | EasyMode's balanced flat CRT |
| APERTURE | `crt/crt-aperture` | Sharp aperture grille with glow |
| CONSUMER TV | `crt/crt-consumer` | A domestic television |
| CALIGARI | `crt/crt-caligari` | Soft phosphor glow |
| ZFAST | `crt/zfast-crt` | Lightweight, for slower GPUs |

Every preset is shown **flat**: the presets' own screen curvature, rounded corners and vignettes (darkened edges) are switched off when they load, so the whole picture stays square and evenly lit.

Presets compile in the background (CRT-Royale takes about 5 s), so the game and menu keep running and the previous look stays up, with LOADING CRT SHADER in the corner, until the new one is ready. The Direct3D view is used for the CRT shaders and for Smooth/Exact pacing; otherwise the window draws itself as before. Border art, scaling and the enhancements carry through. If Direct3D 12 or the shader files are missing, the game says so in the corner and falls back to the normal picture. The presets ship in `shaders/` as data files (mostly GPL; `shaders/SOURCES.md` lists every file and its licence).

### Enhancements

The **Enhance** tab adds effects the arcade board never had. Nothing feeds back into the game, and with the master switch off the picture is the untouched arcade output. Every effect has its own switch. Hold **P** (or **Y** on a controller) on this tab to hide the menu and see the result on the paused picture.

They come from a layered renderer (`src/world_scene.c`): the three background layers and the sprites are drawn separately, so effects know what is in front of what, and the picture is composited at up to 4× the resolution so layers and sprites can sit between pixels. With every effect off it is pixel-identical to the plain picture.

| Effect | What it does |
|---|---|
| Caustics | A rippling net of light on open water, behind the sprites, fixed to each layer |
| Light shafts | Slanted sunbeams from the surface that fade with depth |
| Dynamic lights | Fire, flashes and tracer on sprites light the scenery around them |
| Bloom | Soft glow around the brightest pixels |
| Depth shading | Water loses red, then green, toward the sea bed |
| Marine snow | Drifting particles on two parallax layers |
| Water shimmer | A gentle refraction wobble (subpixel) |
| Vivid color | Richer saturation and contrast |
| Vignette | Darker corners |
| Motion trails | Phosphor-style persistence behind bright objects |
| Scanlines | CRT beam lines (integer scaling, ×2 and up) |
| Smooth scroll | The camera keeps a fraction of a pixel that the scroll registers drop; layers (and sprites moving with them) are drawn at the true position |
| Motion ×2 | In-between frames on 120 Hz and faster displays: layers and matched sprites are drawn part-way between two game frames |
| Drop shadows | Sprites cast soft shadows on the scenery behind them (not on open water) |
| Depth haze | The far layer fades toward water blue |
| Depth of field | The far layer is slightly out of focus |
| Relief light | Embossed lighting on rock and ice |
| Live surface | Ripples just under the waterline and glints along it |
| Shockwaves | A refraction ring spreads from each explosion |
| Particles | Embers and bubbles from explosions |
| Shake + rumble | Big explosions shake the picture and the controller |
| Stereo 3D | Red-cyan anaglyph from the layer depths |

The master switch is off by default. Limits worth knowing:
- **Smooth scroll and Motion ×2** need the high-resolution compositor, so they are inactive with a CRT shader or the Scanlines effect, and when the display scale has no divisor of 2, 3 or 4 (×5, ×7). Motion ×2 needs Smooth or Exact frame pacing on a 120 Hz or faster display, and adds half a frame of display lag.
- **Sprites are matched between frames by position, shape and palette.** A sprite that changes shape or jumps is shown at its new place without an in-between step; a wrong match would show as a one-frame slide.
- **Explosions are recognised from the picture** (a large patch of fire appearing where there was none), not from the game's actor list, so an occasional one is missed or a big muzzle flash counts.
- **Water is recognised by colour** below the sea surface; the surface itself comes from the game's own row-scroll programme for the background layer.
- The camera fraction is known for horizontal scrolling only.
- Cost with the default set: about 3.3 ms per refresh at ×3 (a 4K display) and 4.6 ms at ×4, on eight threads.

### Border art

`play.cmd` downloads one optional undersea background (`tools/get_border_art.ps1`, SHA-256 checked) into `.local/art-sources`. It fills the space beside the 4:3 picture in full screen or a resized window; at the default ×3 window the picture fills the window exactly. If the download fails, the game starts anyway with a black border.

### Command line

```text
InTheHunt.exe <data-dir> [--timing=arcade|remove-slowdown]
.\play.ps1 [-Rom <archive>] [-TimingMode Arcade|RemoveSlowdown]
```

---

## What is in the box

| Area | State |
|---|---|
| Boot, attract, title, coin/start, briefing | Native C |
| Stages 1–6, mid-bosses, bosses, stage transitions | Native C |
| Normal ending, true ending and epilogue, staff roll | Native C |
| Two-player co-op, continues, game over | Native C |
| Sound: V35 sequencer, YM2151 (ymfm), GA20 PCM | Native C/C++ |
| Video: M92 tilemaps, sprites, palette, raster effects | Native C framebuffer, 320×240 |
| Pause/options menu, border art, controller support, optional enhancements | New, native |

If play ever reaches a routine that was not translated, the game stops instead of guessing. It names the routine's address on screen and logs it to `.local/world/native-stops.tsv`. Recent long runs have not hit this (see Verification).

## Verification

The game's behaviour was tested with scripted headless runs that drive real inputs through the native executable:

- An autopilot playthrough from power-on ran **620,000 frames** (several complete loops of all six stages and bosses) with no stops.
- Five consecutive full loops (2,000,000 frames from a saved state), both endings, and a **two-player co-op** run of 120,000 frames all completed with no stops.
- Regression modes (`steer`, `adv`, `dive`) are rerun after each translation batch.
- The pause menu was driven live: open, change and save volume, resume and quit. The title menu's Start Game and Exit to Menu are covered by an off-screen self-test (`WORLD_TEST_TITLE=1`).
- A crash hunt (`tools/crash_hunt.py`) resumes a snapshot of every stage of two loops under nine input styles the autopilot never produces (nobody playing, never continuing, ramming without firing, random input, a second player idling or dropping out): 208 runs of 30,000 frames, no stops.

Earlier in the project, encounter replays were compared with MAME's reference execution, and in the measured windows RAM, sprite, tile, palette and sound-command state matched. See the developer tools below.

## Known limitations

- **Arcade slowdown is a fitted model, not cycle-exact.** The native code runs instantly, so each logic pass is charged an estimate of the original V33 time and the game misses VBlanks when that exceeds a frame, as the PCB does. The estimate comes from 157,503 passes of the original program measured in MAME (see [Slowdown model](#slowdown-model)); held-out passes are predicted to within 1.4 % of a frame on average (r² 0.978), and about two thirds of real overruns are caught. Individual heavy frames can still differ. Remove Slowdown is unaffected.
- **No whole-game frame-by-frame MAME comparison.** MAME 0.289 can run the set rebuilt from the imported chips (the six PLD files are not emulated, so placeholders of the right size are enough; MAME only warns about their checksums), and it was used to measure the slowdown model, but a full lockstep comparison of every frame has not been done.
- **Audio** follows the original driver and chip models, but its samples were never shown to be bit-identical to the reference.
- **A few rare branches estimate a register value.** Some original code paths read a CPU register left over from earlier, unrelated code (for example, scoring an enemy destroyed by something other than a shot, or a turn decision when no player is in play). Where the port does not track that value it continues with a neutral one and counts it (`Register estimates` in headless output); `WORLD_STRICT=1` stops there instead, for translation work.
- Only the World set (`inthunt`) is supported. *Kaitei Daisensou* and the US set are not.

---

## Building from source

Requirements: Windows 10/11 x64, PowerShell 5+, Python 3 (packaging only). For the CRT shaders, put `librashader.dll` and `dxcompiler.dll` in `third_party/librashader/` (pinned builds, see `tools/shader_pins.md`). The build downloads Zig 0.16.0 (SHA-256 pinned) into `.local/` and compiles with `zig cc`. No Visual Studio is needed.

```powershell
. .\tools\world_native.ps1; Build-WorldNative -Root (Get-Location).Path   # → .local/native/InTheHunt.exe
python -I tools/make_shaders.py --slang D:/path/to/slang-shaders   # → .local/shaders (CRT presets)
python tools/package_native.py     # → .local/release/InTheHunt-Windows-x64.zip
python tools/package_source.py     # → .local/release/inthehunt-source.zip (every tracked file)
```

### Source map

| File | Contents |
|---|---|
| `src/world_native.c` | Main loop, board frame and VBlank scheduling, menu integration, headless test runner |
| `src/world_dispatch.c` | The game itself: every actor, enemy, boss, effect and camera handler, translated from the V33 program |
| `src/world_frame.c`, `world_title_master.c`, `world_title.c`, `world_gameover.c` | Scene masters: attract, title, stage clear, endings, game over |
| `src/world_player.c`, `world_fire.c`, `world_torpedo.c` | Player submarine, weapons, stage-end and sub exit |
| `src/world_contact.c`, `world_candidates.c`, `world_death.c`, `world_score.c` | Collision lists, damage, death effects, scoring |
| `src/world_stage.c`, `world_camera.c`, `world_terrain.c`, `world_enemy_target.c` | Stage scripts, scrolling, terrain response, targeting |
| `src/world_video.c`, `world_frame_video.c`, `world_palette.c` | M92 video: tilemaps, sprites, priorities, palette |
| `src/world_audio_driver.c`, `world_audio_notes.c`, `world_ga20.c`, `world_ym.cpp`, `world_audio_output.c` | V35 sound driver, chip synthesis, Windows output |
| `src/world_clock.c`, `world_cycle_costs.c`, `world_timing_options.c` | Board clock and the slowdown model |
| `src/world_window.c`, `world_input.c`, `world_border.c`, `world_menu.c`, `world_settings.c` | Window, XInput/keyboard, border art, pause menu, settings |
| `src/world_scene.c` | Layered scene renderer for the optional enhancements |
| `src/world_gpu.cpp`, `src/post/` | Direct3D 12 presenter and post chain: frame pacing and librashader CRT presets |
| `src/world_boot.c` | ROM import checks and cold-boot state |
| `src/world_io.c`, `world_motion.c`, `world_sound.c`, `world_tile_v33.c` | Early V33 reference routines used by the MAME hybrid tools |
| `src/third_party/ymfm` | Aaron Giles's YM2151 emulator (BSD-3-Clause) |

### Headless testing

`tools/headless_test.ps1` runs the executable with no window using scripted inputs and reports any stop:

```powershell
.\tools\headless_test.ps1 -Root (Get-Location).Path -Modes @('dive coin z x boss') -Frames 620000 -ShotEvery 0
```

Input modes combine: `steer`, `adv`, `sweep`, `dive` (route styles), `z`/`x` (fire), `coin` (keep continuing), `boss` (an autopilot that reads game RAM to position for bosses), `duo` (two players; P2 mirrors P1). Screenshots go to `.local/world/headless/`. Environment hooks:

| Variable | Effect |
|---|---|
| `WORLD_SNAPSHOT_SAVE=frame,path` / `WORLD_SNAPSHOT_LOAD=path` | Save or resume full machine state |
| `WORLD_SNAPSHOT_STAGES=dir` | Save a snapshot at every stage start (input for `tools/crash_hunt.py`) |
| `WORLD_SCRIPT_RELATIVE=1` | Headless script frame numbers count from the loaded snapshot |
| `WORLD_STRICT=1` | Stop on untracked-register branches instead of estimating |
| `WORLD_MENU_SHOT=frame` | Render every pause-menu and title-menu page over that frame |
| `WORLD_TEST_TITLE=1` | Windowed self-test: Start Game, play, Exit to Menu, Start Game again (logs the game state) |
| `WORLD_BORDER_SHOT=frame` | Compose that frame with each border preset, each scaling mode and scanlines at 1920×1080 using the real window painter |
| `WORLD_FX_SHOT=frame` | Save that frame raw, through the scene renderer with no effect, with each effect alone, and with the default set |
| `WORLD_SCENE_RUN=K` | Render every frame through the scene renderer at K× (halfway to the frame and on it), report timing and detected explosions; shot frames are saved at full size |
| `WORLD_FX_SHOTS=1` | Save every scripted shot with the water effects on, and log the waterline |
| `WORLD_CRT_SHOT=frame` | Render that frame through every CRT preset off screen at 1920×1080 (Direct3D 12) |
| `WORLD_SCROLL_LOG=path` | Log the layer scroll registers at every board frame |
| `WORLD_OFFSCREEN=1`, `WORLD_PACING_LOG=path`, `WORLD_RUN_SECONDS=n` | Windowed pacing test on the real display: window off the desktop, never focused; logs every refresh with DXGI present statistics, quits after *n* s |
| `WORLD_TEST_CRT_CYCLE=n`, `WORLD_TEST_PACING_CYCLE=n`, `WORLD_TEST_MENU=1` | With the pacing test: switch to the next CRT preset / frame-pacing mode every *n* refreshes; hold the pause menu open |

### Slowdown model

The original game slows down when its main CPU needs more than one frame (149,980 V33 cycles) for a logic pass. To reproduce that:

1. `tools/mame_measure_passes.lua` runs the original program in MAME with scripted one- and two-player play and records, for every pass, its busy time (from the pass-counter increment at `0:086C` to the `0:084D` VBlank wait loop), the master routine, the actor routines in the list, sprites, video-RAM writes and palette colours stepped per fade mode.
2. `tools/fit_slowdown.py` fits a non-negative linear model (a base per master, a cost per actor routine, and per-sprite, per-VRAM-write and per-faded-colour terms) and writes `src/world_slowdown_table.h`.
3. In Arcade timing the native game sums the same terms for each pass and services the extra VBlanks when the estimate overruns.

Measured findings: an ordinary gameplay pass takes 45–130k cycles depending on the number of actors (≈650 each); the stage-opening palette fade (≈300 cycles per faded colour) makes passes take two frames; a full tilemap redraw (≈16k video writes) takes 2–3 frames. With matched play, the native estimate per gameplay pass agrees with MAME's measured time to within about 5 %.

### Developer reference tools (MAME)

`tools/setup_reference.ps1` installs pinned MAME 0.289 and Open Watcom. `tools/play_reference.ps1` runs the original game or a hybrid with translated V33 routines patched in. `tools/run_encounter.py`, `tools/render_world.py` and `tools/replay_*.c` replay captured encounters on the native code and compare them against the reference. These tools were used for verification during translation; they need the complete original set (including the PLD dumps) and are not needed to play.

## Credits and licences

*In the Hunt* © 1993 Irem. This project is an unofficial, non-commercial preservation and porting effort and ships no original game data. Third-party code and references are listed in [CREDITS.md](CREDITS.md). Their licences are in [LICENSES/](LICENSES): ymfm and the GA20 model (BSD-3-Clause), MAME timing constants (BSD-3-Clause), librashader (MPL-2.0), the DirectX Shader Compiler (LLVM/MIT) and the Blur Busters beam simulator (MIT). The CRT presets keep their own licences (mostly GPL) inside `shaders/`.
