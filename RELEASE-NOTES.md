# In the Hunt — RecompLauncher integration preview

**Pre-release candidate.** Adds a developer-maintained RecompLauncher profile, noninteractive validated ROM import, launcher settings, Big Picture fullscreen startup and a new submarine application icon. Catalogue listing and interactive launcher testing remain pending. The top-level project licence decision also remains open; dependency notices are included.

An unofficial native Windows port of Irem's 1993 submarine shooter. The translated V33/V35 programs run natively, with a native M92 renderer and sound-chip models.

## Download and play

Choose **InTheHunt-Windows-x64.zip**, not the automatic GitHub source archive. Requires Windows 10/11 x64 and PowerShell 5 or later. No compiler or Python is needed for the player package. CRT shaders require Direct3D 12.

1. Extract the entire ZIP to a writable folder.
2. Open `play.cmd` and select your own World `inthunt.7z` archive.
3. Use the title menu to start. Arrow keys move; Z/X fire. F1 or Esc opens the pause menu. On an Xbox-style controller use the D-pad/stick, A/B and Start.

Original game data is not included. Only the World set is supported; Japanese and US sets are not interchangeable. Imported data and settings are kept under `.local/world`. Back up that directory before upgrading and extract updates to a new folder rather than overwriting a running installation.

## Included features

- Six stages, bosses, both endings and local two-player co-op.
- Native music and sound, keyboard/controller input and pause/options menus.
- Integer scaling, CRT presets and optional visual enhancements, disabled by default.
- Arcade slowdown model and a Remove Slowdown option.

## Known limitations

Slowdown is an estimate of arcade CPU cost, not cycle-exact timing. Whole-game frame-by-frame equivalence and bit-identical audio have not been established. Rare translated branches estimate leftover register values. Some enhancement combinations are unavailable with CRT shaders; see the player guide.

## Candidate verification

The integration candidate passed upstream schema validation, 59 native settings roundtrips, embedded-icon inspection, fresh/repeated ROM import, invalid-ROM rejection with existing data preserved, update preservation, and a 3,600-frame replay from the extracted Windows ZIP. Unicode ROM archive filenames and installation paths containing spaces and an ampersand were exercised. Source and asset hashes are recorded in release-manifest.json and SHA256SUMS.txt.

No interactive RecompLauncher, Big Picture display, physical controller or audible playback check was performed for this candidate. Existing long gameplay reports are historical evidence, not fresh full-game qualification of this executable.
