# Development

Run commands from the repository root.

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
| `WORLD_BORDER_SHOT=frame` | Compose that frame with each border preset and each scaling mode at 1920×1080 using the real window painter |
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
