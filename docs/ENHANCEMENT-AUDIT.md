# Enhance option audit — 13 November 2026

All 34 effect switches were profiled separately at 3x on the same retained boss replay, plus the combined preset and all-effects-off renderer. Each number below is the median **CPU render milliseconds for two refreshes per game frame**, after 40 warm-up frames, across 141 measured frames. It includes the base renderer, not just the named effect. Timings vary with machine load; these are not GPU FPS measurements or a claim that every switch became faster.

The combined preset changed from **7.999 ms to 7.111 ms** (11.1% less CPU render time in this short comparison). Vivid Color changed from 4.926 to 3.253 ms, and Sprite Materials from 5.307 to 3.867 ms.

## Shared improvements

- Cache layer colour, relief, blur and slow water lighting at game-frame cadence. Sprite interpolation and event motion still run at display cadence. Setting changes invalidate caches immediately, including paused previews.
- Memoize repeated material and colour calculations exactly across scaled native pixels. Tests compare cached output against a forced full rebuild.
- Clear all temporal families on scene changes and large camera discontinuities: old rings, particles, trails, hot-cell history, clouds, shake and rumble no longer cross scene boundaries.
- Keep the HUD outside scene grading and camera shake; keep background refraction and surface glints off gameplay sprites. Preserve the recent coloured-light correction and removal of diorama parallax.

## Every switch

| Option | Before ms | After ms | Audit decision / improvement |
| --- | ---: | ---: | --- |
| Caustics | 3.631 | 3.072 | Cache slow-changing layer lighting at game rate; keep the moving caustic pattern and sunlight occlusion. |
| Light Shafts | 3.775 | 3.025 | Reuse the layer pass; reduce additive shaft brightness when Volumetric Water is also enabled. |
| Absorption | 3.967 | 2.950 | Reuse the layer pass without changing the validated per-channel depth response. |
| Marine Snow | 3.483 | 2.856 | Cool blue-green motes with gentle brightness variation; protect sprites and terrain at every drawn subpixel. |
| Vivid Color | 4.926 | 3.253 | Exact subpixel colour memoization; exclude HUD rows from colour processing. |
| Vignette | 3.941 | 3.216 | Memoize repeated colour work; keep the HUD outside the vignette. |
| Motion Trails | 4.631 | 4.484 | Clear history on scene changes/large camera discontinuities; retain the optional phosphor look and off-by-default choice. |
| Path Tracing | 4.533 | 4.631 | Keep current ray counts, coloured attenuation and hue-preserving compression; invalidate immediately when preview settings change. |
| Drop Shadows | 3.359 | 3.014 | Keep underwater-only shadows and coherent camera offsets; shared layer/sprite caches reduce repeated work. |
| Depth of Field | 4.005 | 3.364 | Run the existing background blur once per layer refresh, rather than every display refresh. |
| Depth Haze | 3.190 | 2.966 | Avoid stacking full-strength haze with Ocean Depth; retain readable near scenery. |
| Normal Maps | 3.488 | 3.076 | Reuse the layer relief pass between refreshes; retain source-derived normals and shadow response. |
| Sun Shadows | 3.627 | 3.564 | Keep occlusion accuracy and shared transport; immediate option changes rebuild the cached light state. |
| Water Glow | 3.526 | 4.386 | Works independently of Path Tracing now; share radiance work when both are on and reduce overlap with Volumetric Water. |
| Reflections | 3.562 | 3.330 | Protect sprite-covered subpixels at the waterline; retain the restrained depth fade and ripple. |
| Stereo 3D | 4.834 | 5.490 | Keep the separate red/cyan views and reuse scene preparation; still a two-view cost and an opt-in glasses mode. |
| Shockwaves | 3.281 | 2.952 | Restrict refraction to underwater background; Pressure Waves takes precedence instead of doubling displacement. |
| Particles | 3.440 | 2.920 | Underwater hot flecks quench sooner with stronger drag; prevent stamps over gameplay/HUD and clear them when disabled. |
| Shake + Rumble | 3.378 | 3.060 | Coherent camera shake now works with Pressure Waves; reset shake/rumble at cuts and keep HUD anchored. |
| Live Surface | 3.364 | 3.218 | Ripple and glint belong to the background water, not submarine silhouettes; protect HUD and sprite colours. |
| Smooth Scroll | 3.407 | 3.088 | Reuse unchanged sprite buffers; retain source camera fractions and subpixel placement. |
| Motion X2 | 3.367 | 3.876 | Keep pose interpolation at display rate while slow layer lighting updates at game rate; never cache a changed pose. |
| Wakes + Turbulence | 3.399 | 3.052 | Retain independent emission and drag; improve subpixel occlusion and clear all related history at scene boundaries. |
| Destruction Debris | 3.474 | 3.013 | Sweep the movement segment against terrain so fragments settle without crossing thin walls. |
| Impact Silt | 3.513 | 3.071 | Keep ground-dependent emission and work caps; reduce overlapping Lit Sediment clouds. |
| Surface Splashes | 3.502 | 2.956 | Retain crossing detection, spray and underwater bubbles; share the corrected coverage/camera handling. |
| Underwater Sparks | 3.613 | 3.229 | Swept collision quenches fast flecks against thin terrain; retain short lifetimes and fixed pools. |
| Impact Particles | 3.410 | 3.349 | Swept collision improves settling/bubble termination; preserve separate buoyant and sinking motion. |
| Pressure Waves | 3.733 | 3.555 | Single authoritative refraction field when paired with Shockwaves; preserve gameplay positions and coherent optional shake. |
| Lit Sediment | 3.563 | 3.412 | Reduce density when Impact Silt already covers the same seabed event; preserve warm-to-cool light response. |
| Volumetric Water | 3.600 | 3.293 | Reuse the quarter-resolution field between game frames; scan only active flash events; balance overlapping shafts/glow. |
| Ocean Depth | 3.827 | 3.403 | Use a smaller additional far-water tint when Depth Haze is on; no parallax or scene reprojection. |
| Battle Aftermath | 3.859 | 3.309 | Clear leaks/clouds/fragments at scene cuts and discontinuities; retain bounded lifetimes and cooling. |
| Sprite Materials | 5.307 | 3.867 | Exact thread-local shading memoization avoids repeating native-texel work at 3x/4x; preserve colour, normal and roughness detail. |

Water Glow alone is intentionally more expensive than its old ineffective standalone mode: it now computes the radiance it needs. Stereo and interpolation still pay for two views or changing sprite poses; they showed higher times in this run, so no speedup is claimed for them. Tiny differences between inexpensive modes are within run-to-run variation.

## Verification and limits

The longer 1,201-frame combined replay produced the same gameplay-state hash as the pre-change build. All-off output remains identical to the arcade renderer at 1x–4x. Tests cover immediate toggles, refresh cache equivalence, scene-cut cleanup, thin-wall particle collision, overlapping pressure modes, sprite/HUD protection, light colour, settings and fixed effect budgets.
Individual replay captures and a scrolling underwater comparison were inspected. Event-dependent modes also rely on the focused emitter/collision tests: a single screenshot cannot demonstrate every splash, aftermath phase or camera event. This is not a complete visual qualification of every stage.
Local evidence: `build/enhance-audit-20261113/` (`before`, `final-audit`, `final-replay`, `captures`, `tests.txt`, `waterfx-tests.txt`). Reproduce with `tools/audit_enhancements.py` and `tools/qualify_scene_perf.py`, using your local ROMs and retained snapshots.
