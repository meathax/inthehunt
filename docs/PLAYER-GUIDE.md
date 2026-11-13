# Player guide

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
    - Autofire: holding either fire button repeats it
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
| Sun shadows | Rock blocks the sunlight: shafts and caustics stop behind overhangs and pillars, and rock out of the sun is darker |
| Path tracing | Explosions and glowing scenery (lava) light the scene with soft shadows cast by the rock, plus bounced light picked up from the rock they hit (see below) |
| Water glow | Light scattered by the water around explosions: halos that the rock cuts off |
| Absorption | Light loses red first, then green, with depth (per-channel Beer-Lambert); things lit by an explosion show their true colour again |
| Marine snow | Drifting particles on two parallax layers |
| Water shimmer | A gentle refraction wobble (subpixel) |
| Vivid color | Richer saturation and contrast |
| Vignette | Darker corners |
| Motion trails | Phosphor-style persistence behind bright objects |
| Smooth scroll | The camera keeps a fraction of a pixel that the scroll registers drop; layers (and sprites moving with them) are drawn at the true position |
| Motion ×2 | In-between frames on 120 Hz and faster displays: layers and matched sprites are drawn part-way between two game frames |
| Drop shadows | Sprites cast soft shadows on underwater scenery behind them (not on open water or above the waterline) |
| Depth haze | The far layer fades toward water blue |
| Depth of field | The far layer is slightly out of focus |
| Normal maps | Surface normals worked out from the pixel art's shading give per-pixel sunlight, highlights on wet and icy rock, and shading toward each explosion |
| Live surface | Ripples just under the waterline and glints along it |
| Reflections | Just below the waterline, the open water mirrors what is above it, rippling and fading with depth |
| Shockwaves | A refraction ring spreads from each explosion |
| Particles | Embers and bubbles from explosions |
| Shake + rumble | Big explosions shake the picture and the controller |
| Wakes + turbulence | Curling bubble wakes behind moving submarines and thin trails behind fast torpedo-like sprites |
| Destruction debris | Small tumbling fragments from explosions; heavy pieces sink and settle against rock while lighter pieces rise |
| Impact silt | Soft sediment clouds from explosions near scenery, clipped behind sprites and away from the HUD |
| Surface splashes | Waterline crossings produce spray, expanding ripples and bubbles beneath the surface |
| Stereo 3D | Red-cyan anaglyph from the layer depths |

**Path tracing.** Once per game frame the playfield is traced as a 2D scene at half resolution (160×120). The playfield layer's opaque pixels below the surface are rock; a distance field over them lets rays step quickly through open water. Lights are the game's explosion graphics (in any palette) and fire-coloured scenery such as lava. Every probe gets direct light from each light in range through a soft-shadow ray, and three rays in rotating directions collect the light leaving the first rock they hit (its colour times the light it received last frame), so the bounces build up over a few frames. Sunlight visibility is traced the same way toward the surface along the light-shaft angle. It runs on the CPU's worker threads. Shadow rays are skipped when a light contributes less than 1% in every colour channel before occlusion, reducing work during large explosions.

The four reactive water effects have independent saved switches in **Options → Enhancements** and default to on within the master enhancement switch. They use separate fixed pools (128 wake bubbles, 48 debris fragments, 16 silt clouds, 16 splash events), per-frame spawn limits, and bounded drawing work. They require no extra lighting rays or full-screen passes. Terrain interaction also works with path tracing and sun shadows disabled. Scene cuts clear the pools, camera motion carries existing effects, and disabling a switch clears that effect.

These effects are inferred from sprite movement and explosion graphics, not game physics: debris is cosmetic, "torpedoes" are inferred from thin fast sprites, and a surface crossing needs a matching sprite on both sides of the waterline. Large shape changes can miss a crossing. Silt appears only near detected scenery; sparse two-dimensional terrain cannot reproduce true fluid or debris collisions. The checks and saturation benchmark are in `tools/test_waterfx.c`.

The master switch is off by default. Limits worth knowing:
- **Smooth scroll and Motion ×2** need the high-resolution compositor, so they are inactive with a CRT shader, and when the display scale has no divisor of 2, 3 or 4 (×5, ×7). Motion ×2 needs Smooth or Exact frame pacing on a 120 Hz or faster display, and adds half a frame of display lag.
- **Sprites are matched between frames by position, shape and palette.** A sprite that changes shape or jumps is shown at its new place without an in-between step; a wrong match would show as a one-frame slide.
- **Explosions are recognised from the picture**: a large patch of the game's explosion graphics appearing where there was none, not from the game's actor list, so an occasional one is missed.
- **Lights and rock are inferred from the picture.** Rock is whatever the playfield layer draws below the surface, and lights are the explosion graphics and fire-coloured scenery; the far background layer is lit but casts no shadows.
- **Water is recognised by colour** below the sea surface; the surface itself comes from the game's row-scroll programme or the static stage-three sea boundary.
- The camera fraction is known for horizontal scrolling only.
- Cost depends on the scene, compositor scale and enabled effects. Lighting runs once per game frame; composition runs each displayed refresh. Stereo renders two views; trails add a full-frame history pass.

**Enhancement tuning (November 2026).** Every switch was reviewed. The balanced defaults retain lighting and underwater atmosphere; optional blur, trails, shimmer, vivid color and stereo remain off by default. Existing settings are respected.

| Settings | Tuning and recommendation |
|---|---|
| Caustics, light shafts, absorption | Keep on: retain the layered water lighting and depth gradient. |
| Path tracing, water glow, sun shadows | Keep on: three bounce rays and negligible-light culling; skip gathering explosion/lava lights when path tracing is off. |
| Normal maps | Keep on for rock detail and highlights; disable first if lighting still exceeds the frame budget. |
| Drop shadows | Softer contact shading, underwater only. |
| Depth haze, vignette | Reduced strength to preserve distant art and corner readability. |
| Reflections, live surface | Reduced reflection opacity; keep surface ripples and glints. |
| Marine snow, particles | 80 subtler snow flecks; 224-particle cap and 12 particles per explosion instead of 18. |
| Shockwaves, shake + rumble | Weaker distortion and a 2.5-pixel shake cap; reject pixels outside each shock ring before expensive distance calculations. Rumble remains unchanged. |
| Vivid color | Gentler saturation and contrast; optional, especially with a CRT shader. |
| Depth of field | One blur pass instead of two; optional and off by default to retain pixel-art detail. |
| Motion trails | Shorter persistence; optional and off by default to avoid projectile ghosting and a full-frame history pass. |
| Water shimmer | Smaller displacement; optional and off by default for a steadier picture. |
| Smooth scroll, Motion ×2 | Retain subpixel/interpolated motion; use without a CRT shader, with Motion ×2 only on suitable high-refresh displays. |
| Stereo 3D | Keep off unless using red-cyan glasses; intentionally needs two composites. |

### Border art

`play.cmd` downloads one optional undersea background (`tools/get_border_art.ps1`, SHA-256 checked) into `.local/art-sources`. It fills the space beside the 4:3 picture in full screen or a resized window; at the default ×3 window the picture fills the window exactly. If the download fails, the game starts anyway with a black border.

### Command line

```text
InTheHunt.exe <data-dir> [--timing=arcade|remove-slowdown]
.\play.ps1 [-Rom <archive>] [-TimingMode Arcade|RemoveSlowdown]
```

---
