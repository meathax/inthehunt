# IN THE HUNT

An unofficial native Windows port of Irem's 1993 submarine shooter. Play all six stages, both endings and local two-player co-op.

[**Download for Windows x64**](https://github.com/meathax/inthehunt/releases/download/v0.1.0-rc.2/InTheHunt-Windows-x64.zip) · [All releases](https://github.com/meathax/inthehunt/releases) · [Release notes](RELEASE-NOTES.md)

## Highlights

- Keyboard and Xbox-style controllers, with pause menus and saved settings.
- Integer scaling, fullscreen and 12 CRT presets.
- Optional underwater lighting, reflections, particles and smoother motion.
- Choose arcade-style slowdown or Remove Slowdown.
- RecompLauncher setup and settings support; catalogue listing and live launcher testing remain pending.

## Install and play

**Requires:** Windows 10/11 x64 and your own World `inthunt.zip` or `inthunt.7z` ROM archive. Japanese/US sets are unsupported. **No game data is included.** CRT presets need Direct3D 12; no Python or compiler is needed for the Windows package.

1. Download **InTheHunt-Windows-x64.zip** from [Releases](https://github.com/meathax/inthehunt/releases), not the source-code ZIP.
2. Extract everything to a writable folder.
3. Open **play.cmd**, select your ROM archive and choose **Start Game**.

ROM import uses PowerShell 5+ and Windows `tar.exe`. After importing, you no longer need the archive.

## Controls

| Action | Player 1 | Player 2 | Xbox-style controller |
|---|---|---|---|
| Move | Arrows | W A S D | D-pad / left stick |
| Fire | Z / X | G / H | A / B |
| Insert coin | 5 | 6 | Back |
| Start / join | 1 | 2 | A |
| Pause / options | F1 / Esc | F1 / Esc | Start |

Open **Options** for video, sound, difficulty and enhancements. Q/E or shoulder buttons change tabs. Settings save when the menu closes. Other controllers need an XInput-compatible mapping.

## Enhancements

**Off by default.** Turn on **Options → Enhance → Enhancements**, then choose the effects you like. CRT presets are under **Video**.

<details>
<summary>Each enhancement at a glance</summary>

| Option | Effect |
|---|---|
| **Enhancements** | Master switch; off keeps the arcade picture. |
| **Caustics** | Rippling underwater light. |
| **Light Shafts** | Sunbeams from the surface. |
| **Sun Shadows** | Scenery blocks sunlight. |
| **Path Tracing** | Coloured lighting, soft shadows and light bounces; no RTX card required. |
| **Water Glow** | Scattered light around explosions. |
| **Absorption** | Red light fades with depth. |
| **Marine Snow** | Drifting underwater particles. |
| **Vivid Color** | Stronger colour and contrast. |
| **Vignette** | Darker corners. |
| **Motion Trails** | Phosphor-style afterimages. |
| **Smooth Scroll** | Smoother camera movement between pixels. |
| **Motion X2** | Intermediate motion frames on supported 120 Hz+ displays. |
| **Drop Shadows** | Sprites cast shadows on scenery. |
| **Depth Haze** | Distant scenery fades toward blue. |
| **Depth of Field** | Softens the far background. |
| **Normal Maps** | Surface-relief lighting on rock and ice. |
| **Live Surface** | Waterline ripples and glints. |
| **Reflections** | Sky reflections on the water. |
| **Shockwaves** | Spreading distortion around blasts. |
| **Particles** | Explosion embers and bubbles. |
| **Shake + Rumble** | Screen shake and controller vibration. |
| **Wakes + Turbulence** | Curling submarine and torpedo trails. |
| **Destruction Debris** | Tumbling, settling wreckage. |
| **Impact Silt** | Sediment clouds near impacts. |
| **Surface Splashes** | Spray and ripples at waterline crossings. |
| **Underwater Sparks** | Brief hot flecks. |
| **Impact Particles** | Sinking debris and rising bubbles. |
| **Pressure Waves** | Background refraction and bubble rings. |
| **Lit Sediment** | Sediment catches explosion light. |
| **Volumetric Water** | Light beams within the water. |
| **Ocean Depth** | Distant scenery blends into the ocean. |
| **Battle Aftermath** | Lingering bubbles and cooling flecks. |
| **Sprite Materials** | Distinct lighting for paint, metal, rust and glass. |
| **Stereo 3D** | Red-cyan picture for anaglyph glasses. |

</details>

Smooth Scroll and Motion X2 require compatible scaling and cannot run with CRT presets. Motion X2 needs a supported 120 Hz+ display. Effects can reduce performance; enable them gradually.

## Updates and support

Back up **`.local/world`** (imported data and settings). Extract updates into a new folder, then copy that directory across. Keep the old installation until the update works.

**Preview build:** arcade slowdown is approximate; live launcher, audio and controller testing is still incomplete. [Report a problem](https://github.com/meathax/inthehunt/issues) with your version, hardware, stage and enabled effects. Do not upload ROMs.

## Building and credits

[Build instructions](tools/BUILDING.md) · [Dependency licences](LICENSES)

In the Hunt © 1993 Irem. Unofficial and unaffiliated; a top-level project licence is not yet settled. Thanks to MAME, ymfm, CPS-2 Recomp, FinalBurn Neo, the MiSTer Irem M92 project, librashader, Libretro shader authors, DirectX Shader Compiler and Blur Busters. Dependency notices remain with the source and packaged libraries.
