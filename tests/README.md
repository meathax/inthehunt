# Tests

Portable checks for the modules that need no game window, GPU or audio device.
Run them from PowerShell after the first `.\play.ps1` or `.\setup.ps1` has
installed the pinned Zig compiler:

```powershell
.\tests\run_tests.ps1
```

Executables are written to `.local/tests`, which stays out of source control.

| Test | Covers | Needs imported ROM |
|---|---|---|
| `achievements-test.c` | Service-record rules, lifetime progress, profile save/load/merge | No |
| `clock-test.c` | Board clock boundaries, drift, mode latching, raster phase | No |
| `controls-remap-test.c` | Default bindings, focus loss, capture and cancellation | No |
| `cycle-cost-test.c` | Tile-transfer cycle annotations against a reference blitter | Yes (`.local/world/maincpu.bin`) |
| `prefetch-test.c` | Reference V33 prefetch timing model | No |
| `timing-option-test.c` | Timing-mode parsing | No |

The trace-comparison harnesses that need MAME captures (`tools/run_encounter.py`,
`tools/render_world.py`, `tools/replay_*.c`) are separate; see the README.
