# Enhancement performance

Measured on 13 November 2026 using the retained boss replay, 120 warm-up
frames and 1,201 measured game frames, with two render refreshes per game frame.
These are CPU rendering measurements, not GPU presentation or end-to-end FPS.

| Workload | Before | After | Change |
| --- | ---: | ---: | ---: |
| Busy synthetic lighting, 140 lights | 5.181 ms | 2.033 ms | 61% less time |
| Boss replay, native scale, median | 4.982 ms | 3.601 ms | 28% less time |
| Boss replay, 3x scale, median | 7.530 ms | 5.421 ms | 28% less time |
| Boss replay, 3x scale, 95th percentile | 8.907 ms | 6.357 ms | 29% less time |

Lighting now rejects irrelevant lights through conservative spatial bins,
traces sparse probes with denser sampling at terrain boundaries, and caches
the interpolated lighting once per game frame for all layers and refreshes.
Lighting away from geometry edges can be slightly softer. The cached lookup
is regression-tested against the original bilinear samples at every native pixel.

The independent Underwater Sparks and Impact Particles options use bounded
pools, water drag, short spark lifetimes, buoyant bubbles and settling flecks.
They infer events from the existing explosion/impact sprite detections and do
not alter game logic. They respect the waterline, terrain and foreground sprites.
Water Shimmer has been removed; obsolete saved keys are ignored.

The same replay with both new effects enabled emitted 258 sparks and 270
particles. Median render times were 3.648 ms at native scale and 5.303 ms at
3x; differences from the effects-disabled run include measurement noise.
All before/after runs produced the same gameplay-state hash (RAM, video RAM,
sprite RAM and palette):
`2bd0432e508cfcc56a26fb6dcbc744b56384de9032fa23df2c2dd45ddcaa9933`.

Local evidence is retained under `build/fx-performance-20261113/`, including
the baseline executable, synthetic results, replay CSVs, summaries and captures.
`tools/qualify_scene_perf.py` reruns the replay when its local snapshot and ROM
inputs are available. ROMs and replay inputs are not distributed.

## Diorama suite

Historical measurement: the projection effect described in this section was
subsequently removed at the user's request. See the lighting correction below
and the current [enhancement guide](ENHANCEMENTS.md).

The subsequent seven-effect suite was measured with sparks and impact particles
enabled, using the same 1,201-frame boss workload and two refreshes per frame.

| Suite configuration | Native median | 3x median | 3x 95th percentile |
| --- | ---: | ---: | ---: |
| Seven new switches off | 3.995 ms | 6.245 ms | 7.197 ms |
| Seven new switches on, qualified build | 5.246 ms | 7.536 ms | 8.544 ms |

The complete suite adds approximately 1.25–1.29 ms per game frame in these runs;
timing varies between runs. These numbers include the CPU compositor only, not
game simulation, GPU presentation or a guaranteed monitor refresh rate.
The initial uncached version cost 7.901/10.189 ms at native/3x; cached material
maps, shared light preparation and once-per-game-frame distant-plane rendering
removed most of that overhead.

All suite-on/off replay state hashes match the hash above. Synthetic tests also
verify exact arcade output with every effect off at 1x, 2x, 3x and 4x; protected
sprite positions, terrain priority and HUD rows; bounded event lifetime/work;
material-map flips and lighting; and settings round trips. Native underwater
captures confirm that the material and scenery passes produce visible changes
while the HUD remains byte-identical.

Evidence: `build/diorama-20261113/{qualified-all,final-off,captures,tests.txt}`.
The `qualified-all/summary.json` binds the measured executable by SHA-256.

## Underwater light colour correction and parallax removal

The parallax projection, associated camera/buffers, extra foreground depth
motes, settings and launcher entry were removed. Original scenery scrolling
is restored; the other enhancements remain independently selectable.

The colour audit found independent-channel light compression, per-channel
highlight clipping, absent wavelength-dependent path attenuation and neutral
material highlights driven only by light intensity. The correction preserves
RGB ratios through shared brightness compression, attenuates direct and bounced
light along their submerged path, and colours material highlights with incident
light. Source emission uses a squared display-colour approximation; attenuation
is an artistic pixel-distance calibration, not a physical measurement.

`tools/test_enhancements.c` verifies the actual transport for white light in
air versus water, surface-crossing path lengths, hue preservation, original
renderer identity, sprite/HUD protection and removal of legacy saved options.
Before/after captures, replay timings and exact executable hashes are retained
under `build/light-colour-20261113/`. Both comparisons use the same six remaining
effects, with the old executable's parallax disabled, to isolate lighting changes.
