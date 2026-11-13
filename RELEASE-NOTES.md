# In the Hunt — v0.1.0-rc.2: underwater enhancements

Updated 14 November 2026. **Pre-release candidate.** Native Windows x64 build;
original game data is not included. The top-level project licence decision and
RecompLauncher catalogue listing remain open. Dependency notices are included.

## What changed

- Added independently selectable underwater sparks, impact particles, sprite
  materials, pressure waves, illuminated sediment, volumetric water, ocean depth
  and battle aftermath.
- Rebalanced dynamic lighting: colour-preserving highlights, underwater light
  attenuation and material highlights that inherit the light colour.
- Audited all 34 Enhance switches. Shared caches reduce repeated work; Water Glow
  works independently; overlapping haze, shafts, glow and pressure effects are
  balanced. Scene changes clear old effects, and fast fragments respect thin terrain.
- Removed Water Shimmer and Diorama Parallax completely. Legacy saved keys are
  ignored and omitted when settings are saved.
- Retained the submarine application icon and RecompLauncher profile/import support.

The short boss comparison measured 11% less CPU render time for the combined
preset, 27% less for materials and 34% less for vivid colour. Not every switch
became faster. See [the complete audit](docs/ENHANCEMENT-AUDIT.md) for timings,
tradeoffs and scope. Enhancements remain **off by default**.

## Download and play

Download **InTheHunt-Windows-x64.zip**, extract the entire ZIP to a writable folder,
and open **play.cmd**. Supply your own World `inthunt.7z` archive when prompted.
Requires Windows 10/11 x64 and PowerShell 5 or later; no compiler or Python is
needed. CRT presets require Direct3D 12.

Arrow keys move, Z/X fire, and F1 or Esc opens the pause menu. Xbox-style
controllers use the D-pad/stick, A/B and Start. Only the World ROM set is supported.

Extract updates to a new folder rather than overwriting a running installation.
Back up your existing `.local/world` directory before carrying imported data and
settings forward. No ROMs, save data or local settings are included in the release.

## Verification

The enhancement build passed native compositor, lighting, particle and settings
regressions; all-effects-off matches the reference renderer at 1x–4x in the
synthetic fixture. All 34 options were profiled on a retained boss replay, and
before/after captures were inspected. The 1,201-frame combined replay at native
and 3x scale retained the same gameplay-state hash. Packaging checks bind the
Windows executable and source archive to this release's commit and checksums.

Earlier rc.1 launcher/import verification is historical evidence; it is not a
fresh interactive launcher qualification of rc.2. No new desktop, physical
controller, audible playback or full-six-stage visual qualification is claimed.

## Known limitations

Arcade slowdown remains a CPU-cost estimate, not cycle-exact timing. Whole-game
frame-by-frame equivalence and bit-identical audio have not been established.
Material masks are authored for inspected player/boss families; other large
sprites use conservative fallback materials, not bespoke high-resolution maps.
Some enhancement combinations are unavailable with CRT presets. See the player
guide and enhancement audit for details.
