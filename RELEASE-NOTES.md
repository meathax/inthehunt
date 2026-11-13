# In the Hunt — Windows x64 release draft

**Unpublished draft.** Version, source commit, checksums and final candidate testing remain to be filled in after the release checklist is complete.

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

## Candidate verification — complete before publishing

Record the selected tag, full source commit, binary hash, clean-extraction test, tested Windows/GPU/controller configuration and actual regression results. Historical development runs are not proof for a new binary. Attach SHA256SUMS.txt and release-manifest.json with the final archives.
