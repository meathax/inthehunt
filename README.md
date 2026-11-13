# In the Hunt — native Windows port

Irem's 1993 submarine shooter *In the Hunt* (World set `inthunt`), rebuilt as a native Windows x64 game. The original V33 main program and V35 sound program were translated routine by routine into portable C. The arcade CPUs are not emulated. All six stages, every boss, both endings and two-player co-op run from power-on on translated code. Original music and effects are kept, along with an approximation of the arcade slowdown.

You supply the game data from your own copy of the World ROM set. Original ROM images, graphics and audio are not supplied; the translated program is derived from the original game.

**Release preparation:** no public download URL is configured in this checkout. See the [release checklist](docs/RELEASING.md) for outstanding publication requirements.

## Requirements

Windows 10/11 x64, PowerShell 5 or later, and your own World `inthunt.7z` ROM set. The importer uses Windows `tar.exe` and verifies the chip SHA-1 hashes. Direct3D 12 is required for CRT shaders. A packaged release does not need Python or a compiler.

## Quick start

1. From the GitHub release assets, download **`InTheHunt-Windows-x64.zip`** (not GitHub's automatic source-code ZIP) and extract **all** of it to a writable folder.
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

## Features

- Six stages, bosses, both endings and local two-player co-op.
- Native music and sample playback, keyboard and Xbox-style controllers.
- Pause/options menus, integer scaling, CRT presets and optional visual enhancements.
- Arcade slowdown model or Remove Slowdown; enhancements are off by default.

See the [player guide](docs/PLAYER-GUIDE.md) for options, shaders, frame pacing and enhancement limits.

## Known limitations

- **Arcade slowdown is a fitted model, not cycle-exact.** The native code runs instantly, so each logic pass is charged an estimate of the original V33 time and the game misses VBlanks when that exceeds a frame, as the PCB does. The estimate comes from 157,503 passes of the original program measured in MAME (see [Slowdown model](docs/DEVELOPMENT.md#slowdown-model)); held-out passes are predicted to within 1.4 % of a frame on average (r² 0.978), and about two thirds of real overruns are caught. Individual heavy frames can still differ. Remove Slowdown is unaffected.
- **No whole-game frame-by-frame MAME comparison.** MAME 0.289 can run the set rebuilt from the imported chips (the six PLD files are not emulated, so placeholders of the right size are enough; MAME only warns about their checksums), and it was used to measure the slowdown model, but a full lockstep comparison of every frame has not been done.
- **Audio** follows the original driver and chip models, but its samples were never shown to be bit-identical to the reference.
- **A few rare branches estimate a register value.** Some original code paths read a CPU register left over from earlier, unrelated code (for example, scoring an enemy destroyed by something other than a shot, or a turn decision when no player is in play). Where the port does not track that value it continues with a neutral one and counts it (`Register estimates` in headless output); `WORLD_STRICT=1` stops there instead, for translation work.
- Only the World set (`inthunt`) is supported. *Kaitei Daisensou* and the US set are not.

## Documentation and source layout

| Path | Purpose |
|---|---|
| `src/` | Translated game, board models and native Windows presentation |
| `src/third_party/` | Vendored source with upstream notices |
| `tools/` | Build, import, packaging and verification tools |
| `docs/` | Player guide, development, verification and release procedure |
| `LICENSES/`, `CREDITS.md` | Dependency terms and attribution |
| `artwork/` | Tracked layout and vector artwork |
| `.local/` | Ignored imported data, tools, captures and release output |

[Build and source map](docs/DEVELOPMENT.md) · [Verification record](docs/VERIFICATION.md) · [Release audit](docs/RELEASE-AUDIT.md)

## Reporting problems

Include the release version, Windows version, GPU, controller, stage and reproduction steps. Say whether CRT shaders and enhancements were enabled. For an untranslated-routine stop, include the address from `.local/world/native-stops.tsv`. Do not attach ROMs, imported data or machine-state snapshots.

## Credits and licences

*In the Hunt* © 1993 Irem. This project is an unofficial, non-commercial preservation and porting effort and ships no original game data. Third-party code and references are listed in [CREDITS.md](CREDITS.md). Their licences are in [LICENSES/](LICENSES): ymfm and the GA20 model (BSD-3-Clause), MAME timing constants (BSD-3-Clause), librashader (MPL-2.0), the DirectX Shader Compiler (LLVM/MIT) and the Blur Busters beam simulator (MIT). The CRT presets keep their own licences (mostly GPL) inside `shaders/`.

A top-level licence for project-authored code has not yet been selected. The dependency notices do not establish one; this must be resolved before the public release.

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
