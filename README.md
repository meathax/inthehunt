<h1 align="center">IN THE HUNT</h1>
An unofficial native Windows port of Irem's **In the Hunt** (1993). Take the submarine through six stages of underwater combat, battle enormous bosses, and play solo or with a friend in local two-player co-op.

The original V33 game program and V35 sound program have been translated into C and compiled for Windows. The arcade CPUs are not emulated; the port supplies native video, input and sound-chip models.

> **Bring your own game.** You need the World arcade ROM set, `inthunt`. Original ROM images and standalone game assets are not included. The Japanese and US sets are not supported.

## Latest release — v0.1.0-rc.2

[**Download for Windows x64**](https://github.com/meathax/inthehunt/releases/download/v0.1.0-rc.2/InTheHunt-Windows-x64.zip) · [Release notes](RELEASE-NOTES.md)

This preview adds underwater sparks and impact particles, sprite materials,
pressure waves, illuminated sediment and battle aftermath. Dynamic light keeps
its colour, attenuates through water and produces coloured metal highlights.
Water Shimmer and Diorama Parallax have been removed.

All **34 Enhance options** were audited. The short boss comparison measured
**11% less CPU rendering time** for the combined preset, **27% less** for sprite
materials and **34% less** for vivid colour. These are workload-specific CPU
measurements, not guaranteed FPS gains.

## Features

- **The full arcade route:** all six stages, bosses, both endings, continues and local two-player play.
- **Native controls and menus:** keyboard or Xbox-style controllers, a title menu, pause menu and saved options.
- **Your choice of picture:** sharp integer scaling, fit-to-screen 4:3, fullscreen and twelve CRT shader presets.
- **Optional underwater effects:** caustics, light shafts, dynamic lighting, shadows, reflections, wakes, debris, surface splashes and tiny underwater sparks and impact particles, with separate Enhance switches.
- **Enhanced presentation:** sprite materials, background pressure waves, illuminated sediment and lingering battle aftermath.
- **Flexible presentation:** display-synchronised frame pacing, optional smooth scrolling and motion interpolation on supported high-refresh displays.
- **Arcade slowdown or full speed:** retain the port's approximation of the original slowdown, or switch it off.

Visual enhancements are **off by default**. Enable effects individually to balance appearance and performance. CRT presets cannot be combined with some enhancements.

## What you need

| Requirement | Details |
|---|---|
| System | Windows 10 or 11, 64-bit |
| Launcher | PowerShell 5 or later; Windows `tar.exe` for ROM import |
| Game data | Your own World `inthunt.7z` archive; chip contents are SHA-1 checked |
| CRT shaders | A Direct3D 12-capable GPU |
| Input | Keyboard or Xbox-style controller; a second controller for player two |

The packaged Windows build needs **no Python, Visual Studio or compiler**. Source builds download the pinned Zig compiler on first use.

## Install and play

**RecompLauncher:** the Windows ZIP includes a developer-maintained setup profile. It imports your World ROM archive, exposes settings in the launcher and starts the game directly. Catalogue listing and interactive launcher testing are still pending.

1. Open [Releases](https://github.com/meathax/inthehunt/releases) and select **`InTheHunt-Windows-x64.zip`**, when available. GitHub's automatic source-code ZIP is not the playable build.
2. Extract **everything** to a writable folder, such as `D:\Games\InTheHunt`.
3. Double-click **`play.cmd`**.
4. On first launch, choose your `inthunt.7z`. The launcher also checks Downloads and the folder beside `play.cmd`.
5. Choose **Start Game** on the title menu.

Imported data stays in `.local/world`; subsequent launches do not need the archive. Settings are stored in `.local/world/native-settings.ini`. The launcher may download optional border art; if the download fails, play continues with a black border.

**Building instead?** Follow the [source build instructions](#building-from-source). From a prepared source checkout, `.\play.ps1` imports the data and builds the executable when needed.

## Settings

Press **F1**, **Esc**, or **Start** on a controller to open the pause menu. Choose **Options**, then use Q/E or the shoulder buttons to change tabs.

| Tab | Options |
|---|---|
| Video | Window/fullscreen, scaling, CRT shaders, frame pacing, FPS display and border art |
| Sound | Master volume and separate FM/PCM levels |
| Game | Slowdown, difficulty, submarines, demo sound, autofire and auto-pause |
| Enhance | Optional lighting, water effects, smooth scrolling and other presentation effects |

Settings save when the menu closes.

## Enhancement guide

Open **Options → Enhance** to adjust these effects. **Enhancements** is the
master switch: it starts **off**, preserving the original arcade picture.
Turning it on activates the selected effects; individual switches let you keep
only the ones you like. The names below match the in-game menu.

| Option | What it does |
|---|---|
| **Enhancements** | Enables or bypasses the entire enhancement pass without clearing your individual effect selections. |
| **Caustics** | Adds moving patterns of refracted light to open water, like sunlight rippling through a pool. |
| **Light Shafts** | Adds beams of sunlight descending from the surface. |
| **Sun Shadows** | Lets rocks and scenery block sunlight, creating darker areas beneath them. |
| **Path Tracing** | Adds coloured lighting from fire and explosions, with soft shadows and bounced light in the enhanced scene. This is the game's lighting effect, not a requirement for an RTX graphics card. |
| **Water Glow** | Spreads light around explosions to suggest scattering through the surrounding water. |
| **Absorption** | Reduces red light with depth, giving deeper water a cooler appearance. |
| **Marine Snow** | Adds small drifting particles suspended in the water. |
| **Vivid Color** | Boosts colour and contrast for a stronger, less subdued picture. |
| **Vignette** | Darkens the corners to draw attention toward the centre. |
| **Motion Trails** | Leaves a short-lived afterimage behind motion, resembling phosphor persistence. |
| **Smooth Scroll** | Allows the camera to move between the original pixel positions, reducing visible stepping during scrolling. Requires compatible display scaling and no CRT shader. |
| **Motion X2** | Draws intermediate motion frames on supported displays running at 120 Hz or above. Requires compatible scaling and no CRT shader; it does not double the game's simulation speed. |
| **Drop Shadows** | Adds shadows from sprites onto the scenery to help objects stand out from their surroundings. |
| **Depth Haze** | Fades distant scenery toward blue to suggest water between it and the viewer. |
| **Depth of Field** | Softens the far background while keeping nearer action more distinct. |
| **Normal Maps** | Adds surface-relief lighting to rock and ice, making light react to apparent bumps and edges. |
| **Live Surface** | Adds ripples and glints to the waterline. |
| **Reflections** | Reflects the sky at the water surface. |
| **Shockwaves** | Distorts the water around blasts with a spreading shockwave effect. |
| **Particles** | Adds embers and bubbles to explosions. |
| **Shake + Rumble** | Adds screen shake and vibration on supported controllers when explosions occur. |
| **Wakes + Turbulence** | Adds curling water trails behind submarines and torpedoes. |
| **Destruction Debris** | Adds tumbling fragments of wreckage that settle after destruction. |
| **Impact Silt** | Lifts clouds of sediment where impacts disturb nearby rock. |
| **Surface Splashes** | Adds spray and ripples when objects cross the waterline. |
| **Underwater Sparks** | Adds small hot flecks that quickly fade in the water. |
| **Impact Particles** | Adds fine impact debris that sinks and bubbles that rise. |
| **Pressure Waves** | Adds background refraction and expanding bubble rings to convey pressure moving through water. |
| **Lit Sediment** | Lets curling sediment clouds catch the light from explosions. |
| **Volumetric Water** | Adds light within the water itself, including beams interrupted by scenery. |
| **Ocean Depth** | Blends distant scenery into the surrounding ocean for a stronger sense of depth. |
| **Battle Aftermath** | Leaves cooling flecks and lingering bubbles after combat effects subside. |
| **Sprite Materials** | Adds lighting and surface relief that distinguish painted surfaces, metal, rust and glass on sprites. |
| **Stereo 3D** | Produces a red-cyan stereo picture for anaglyph glasses; leave it off for normal viewing. |

**Compatibility and performance:** Smooth Scroll and Motion X2 use a higher-resolution
composition path when the display scale permits it. CRT presets use the original
240-line picture, so those motion features cannot be combined with a CRT shader.
Other effects can add rendering cost; switch them on gradually to find a combination
that stays smooth on your PC. CRT presets live under **Video**, separately from
these Enhance options.

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

## Status and known limitations

The native port has completed multi-loop headless playthroughs, two-player input tests, both ending-branch tests and a 156-run crash matrix. A freshly extracted Windows package has also passed import and replay checks.

- **Slowdown is approximate**, not cycle-exact arcade timing.
- Whole-game frame-by-frame equivalence and bit-identical audio have not been established.
- A few rare translated branches estimate leftover CPU register values.
- CRT shaders and some enhancements cannot be combined. Motion interpolation has display and scaling requirements.
- Monitor pacing, focus/fullscreen transitions, audible mixing, controller hot-plug and rumble still need interactive release qualification.

The RecompLauncher integration is a release candidate, not a fully qualified stable release.

## Updating

Back up `.local/world` first. Extract the new release into a new writable folder, copy your existing `.local/world` into it, then start the new `play.cmd`. Keep the previous installation until you have confirmed the update works. Do not overwrite an installation while the game is running.

## Reporting a problem

[Open an issue](https://github.com/meathax/inthehunt/issues) with what happened, the stage and steps to reproduce it. Include your build or commit, Windows version, GPU and controller, and whether CRT shaders or enhancements were enabled. A screenshot helps.

If the port stops at an untranslated routine, include the address from `.local/world/native-stops.tsv`. Do not upload ROM archives, imported game data or machine-state snapshots.

## Building from source

Requires Windows 10/11 x64, PowerShell 5+ and Windows `tar.exe`. The build downloads and verifies Zig 0.16.0; Visual Studio is not required.

```powershell
. .\tools\world_native.ps1
Build-WorldNative -Root (Get-Location).Path
```

The executable is written to `.local/native/InTheHunt.exe`. Run `play.cmd` to import your World ROM archive and play, or use `setup.ps1 -Rom C:\path\to\inthunt.7z` to import and build together.

For optional CRT presets, place `librashader.dll` v0.12.0 and `dxcompiler.dll` v1.9.2609 in `third_party/librashader/` before building. The published Windows ZIP includes these runtimes. Generate the shader data with Python 3 from the pinned libretro checkout:

```powershell
python -I tools/make_shaders.py --slang C:/path/to/slang-shaders
```

Use slang-shaders revision `2345ee91602a7404d3a1a6a5df6108d7305fdad4`. The script copies preset dependencies and their licence notices to `.local/shaders`. The release uses unmodified librashader revision `87e8a97b50516d997defeaa168173dcd185d4022`, built with the Direct3D 12 runtime and static C runtime. Runtime SHA-256 values:

| Runtime | SHA-256 |
|---|---|
| librashader.dll | `8e5912fc7432ff39e935c61674823f37b96a824bb6c727f1319cc31fc561a74a` |
| dxcompiler.dll | `0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59` |

Game and host source live in `src/`; `tools/` contains build, import and runtime support. The application icon is a build input. Generated files, imported ROMs and local settings are ignored. RecompLauncher configuration and its setup script are retained for launcher support.

## RecompLauncher

The Windows ZIP includes `recomplauncher.json` and an import-only setup helper.
Select your own World `inthunt.zip` or `inthunt.7z` in Easy Setup; the importer
checks the original chip sizes and SHA-1 hashes. Play starts `InTheHunt.exe`
directly. Settings include every enhancement, audio, display, arcade timing,
lives and difficulty. Keyboard/controller rebinding remains in the game menu.

First-run defaults use arcade slowdown, integer scaling at 3x, display-synchronised
Smooth pacing, normal difficulty and three lives. Enhancements, CRT shaders and
autofire are off; borders are black. Pause on focus loss is on. Big Picture
starts fullscreen. Controllers use XInput; other pads need an XInput-compatible
mapping. Quit from the game menu to return to the launcher.

Only `native-settings.ini` is included in launcher backups; ROM data is excluded.
Imports and package updates preserve existing settings and game data. External
launcher Looks are disabled because the game supplies its own CRT presets.

To test an unpublished build, enable Settings → Developers → Developer tools in
RecompLauncher and use **Add a build** on the extracted Windows ZIP. Check the
Developer tab, run Easy Setup, change a setting, then test Play, Quit and Big
Picture. Catalogue inclusion is separate: submit the repository through
[Submit a Game](https://recomplauncher.com/submit?platform=pc) after publishing a
tested Windows release. A local manifest does not create a catalogue listing.

Build the native executables first, then run `python tools/package_native.py`.
`tools/test_launcher_settings.c` and `tools/test_recomplauncher.py` verify the
shipping settings parser, current upstream schema, manifest commands, imports,
update preservation and a headless gameplay run. Live launcher UI and physical
controller testing are separate checks; automated checks do not prove them.

## Credits and licences

**In the Hunt © 1993 Irem.** This unofficial project is not affiliated with Irem. The translated program derives from the original game; original ROM data is not distributed. A top-level project licence has not yet been settled. Required dependency notices remain in [LICENSES](LICENSES) and the source headers.

- [MAME](https://github.com/mamedev/mame/tree/73346bee98cd79d605514d14691f40ee5e368915): Bryan McPhail's M92/NEC work supplies ROM layout, video and timing references. GA20 synthesis adapts work by Acho A. Tang, R. Belmont and Valley Bell (BSD-3-Clause).
- [ymfm](https://github.com/aaronsgiles/ymfm): Aaron Giles's YM2151 synthesis (BSD-3-Clause), vendored through [CPS-2 Recomp](https://github.com/tmcleroy/cps2recomp/tree/d3bf22f277d7c610a8acbc8bc164620401f79a38). CPS-2 Recomp, FinalBurn Neo and the MiSTer Irem M92 core also served as engineering references.
- [librashader](https://github.com/SnowflakePowered/librashader): SnowflakePowered and contributors, MPL-2.0 runtime and MIT C headers. [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) provides shader compilation (LLVM/MIT).
- [Libretro shaders](https://github.com/libretro/slang-shaders): CRT-Royale by TroggleMonkey, Kurozumi tuning, Guest Advanced by guest(r)/Dr. Venom, CRT-Geom by cgwg and contributors, CRT-Lottes by Timothy Lottes, CRT-Hyllian by Hyllian, Easymode/Aperture by EasyMode, and zfast by Greg Hogan, alongside Consumer and Caligari. Presets retain their individual notices.
- [Blur Busters CRT Beam Simulator](https://github.com/blurbusters/crt-beam-simulator): Mark Rejhon and Timothy Lottes (MIT). The post chain comes from the author's Gauntlet Dark Legacy port; sharpening follows AMD FidelityFX CAS (MIT).
- Optional cabinet art: Irem originals catalogued by [LaunchBox](https://gamesdb.launchbox-app.com/games/images/29540-in-the-hunt); portrait composition by ArsInvictus, bezel scan by ShaunDBurch, control-panel reference by Briball and rendering by PapaShine. The optional downloader stores these locally; redistribution permission has not been established.
