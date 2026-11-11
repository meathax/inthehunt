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
| Start / join | 1 | 2 | Start |
| **Pause menu** | **F1** | **F1** | **L3 + R3 together** |

Each controller drives its own player. Key and button assignments are stored in `.local/world/native-settings.ini`.

## Pause and options menu

Press **F1** (or **L3 + R3**) at any time. The game freezes and the menu appears in the game's own lettering and colours. Up and down select, left and right change a value, Enter/A confirms, Esc/B goes back. Each line has a short description at the bottom of the screen.

- **Resume**
- **Options**: three tabs (Q/E or the shoulder buttons switch tabs)
  - **Video**: window or full screen; window size (sharp ×1 to ×8 integer scaling); border art (Black or Undersea); border brightness
  - **Sound**: master volume, 0–100 %
  - **Game**: slowdown mode (Arcade keeps the original slowdown; Remove Slowdown runs busy scenes at full speed); reset all options
- **Controls**: the keyboard and controller layout
- **Quit game**: asks for confirmation

Settings are saved when the menu closes. Esc no longer quits the game outright.

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
| Pause/options menu, border art, controller support | New, native |

If play ever reaches a routine that was not translated, the game stops instead of guessing. It names the routine's address on screen and logs it to `.local/world/native-stops.tsv`. Recent long runs have not hit this (see Verification).

## Verification

The game's behaviour was tested with scripted headless runs that drive real inputs through the native executable:

- An autopilot playthrough from power-on ran **620,000 frames** (several complete loops of all six stages and bosses) with no stops.
- Five consecutive full loops (2,000,000 frames from a saved state), both endings, and a **two-player co-op** run of 120,000 frames all completed with no stops.
- Regression modes (`steer`, `adv`, `dive`) are rerun after each translation batch.
- The pause menu was driven live: open, change and save volume, resume and quit.

Earlier in the project, encounter replays were compared with MAME's reference execution, and in the measured windows RAM, sprite, tile, palette and sound-command state matched. See the developer tools below.

## Known limitations

- **Arcade slowdown is approximate.** Original per-block CPU costs are only partly recovered, so busy scenes slow down close to, but not exactly like, the PCB. Remove Slowdown is unaffected.
- **No final frame-by-frame MAME comparison.** MAME 0.289 also needs the set's four PLD dumps (`m92_a-9j-.ic51`, `m92_e-3k-.ic20`, `m92_e-3p-.ic21`, `m92_e-4k-.ic29`). The importer does not keep them, so whole-game equivalence against MAME has not been rechecked.
- **Audio** follows the original driver and chip models, but its samples were never shown to be bit-identical to the reference.
- Only the World set (`inthunt`) is supported. *Kaitei Daisensou* and the US set are not.

---

## Building from source

Requirements: Windows 10/11 x64, PowerShell 5+, Python 3 (packaging only). The build downloads Zig 0.16.0 (SHA-256 pinned) into `.local/` and compiles with `zig cc`. No Visual Studio is needed.

```powershell
. .\tools\world_native.ps1; Build-WorldNative -Root (Get-Location).Path   # → .local/native/InTheHunt.exe
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
| `WORLD_MENU_SHOT=frame` | Render every pause-menu page over that frame |
| `WORLD_BORDER_SHOT=frame` | Compose that frame with each border preset at 1920×1080 and 1280×1024 using the real window painter |

### Developer reference tools (MAME)

`tools/setup_reference.ps1` installs pinned MAME 0.289 and Open Watcom. `tools/play_reference.ps1` runs the original game or a hybrid with translated V33 routines patched in. `tools/run_encounter.py`, `tools/render_world.py` and `tools/replay_*.c` replay captured encounters on the native code and compare them against the reference. These tools were used for verification during translation; they need the complete original set (including the PLD dumps) and are not needed to play.

## Credits and licences

*In the Hunt* © 1993 Irem. This project is an unofficial, non-commercial preservation and porting effort and ships no original game data. Third-party code and references are listed in [CREDITS.md](CREDITS.md). Their licences are in [LICENSES/](LICENSES): ymfm and the GA20 model (BSD-3-Clause), and MAME timing constants (BSD-3-Clause).
