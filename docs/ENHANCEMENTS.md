# Water and material enhancements

Enable **Options → Enhance → Enhancements**, then select the effects individually.
The master switch still defaults to off. The enhanced preset enables the new
effects; shake is off in fresh settings and remains separately selectable.

See the [complete 34-option audit](ENHANCEMENT-AUDIT.md) for measured costs and
the treatment of every Enhance switch. Water Glow now works independently of
Path Tracing. Combined glow/shafts/volume and haze/depth effects are balanced
to avoid excessive brightness or fog. Pressure Waves takes precedence over
the older Shockwaves displacement when both are enabled.

| Option | Appearance |
| --- | --- |
| Pressure Waves | Short background refraction pulses and expanding bubble rings |
| Lit Sediment | Curling seabed clouds catch warm explosion flashes and cool into the water |
| Volumetric Water | Occluded sunlight shafts and explosion light scattered through water |
| Ocean Depth | Distant scenery fades toward blue-green without changing its scroll |
| Battle Aftermath | Lingering bubble leaks and slowly cooling, sinking fragments |
| Sprite Materials | Different reflectance and relief for paint, steel, chrome, brass, copper, rust, glass, rubber and stone |

## Original scrolling

Diorama Parallax has been removed, including its projection and foreground depth
particles. Old `fx_diorama` settings are ignored and disappear when saved.
Scenery uses the original scrolling again. Pressure waves affect only the
backdrop, and clouds/particles avoid gameplay sprites and the HUD.

## Balanced light colour

Dynamic emission uses an approximate linear conversion of the artwork colours.
Direct and bounced light lose red faster than green and blue along underwater
paths, including rays crossing the surface. Bright RGB is compressed together
instead of whitening each channel separately. Metal highlights now inherit the
incident light colour; ambient underwater glints remain restrained and cool.
Original explosion sprites keep their authored colours. The attenuation values
are artistic coefficients in pixel units, not a calibrated physical ocean model.

## Materials

The renderer preserves the sprite's original colour and transparency. Source
texels provide relief normals; authored palette-role masks distinguish finishes
on the observed player hull/turning families and industrial boss machinery.
Roughness, coloured metal highlights, glass glints and restrained surface grain
are fixed to source texels and follow sprite flips and animation.

These are **not hand-painted high-resolution maps for every enemy**. Unmapped
large sprites receive conservative paint/steel/rubber responses. Small projectiles,
known explosion/smoke families and HUD graphics are excluded. This prevents a
blanket metallic filter from turning fire, bullets or lettering into chrome.
The profiles and coefficients live in `src/world_scene_materials.h` and can be
extended as additional sprite families are inspected. Explosion lighting uses
the existing light cache when Path Tracing is enabled; ambient material response
also works without it.

## Cost and limits

No additional shadow rays are traced for the new effects. Material maps are
cached by sprite tile and palette; the scattering field is quarter resolution.
There are at most 24 cinematic events, with a 14,000-sample cloud budget per
refresh. All are presentation-only, inferred from explosion sprite events;
aftermath does not represent persistent damage in the arcade simulation.

The implementation uses the shared native compositor, so these effects also
work with software presentation and CRT presets. It does not require a new
GPU feature level. CPU render measurements, not GPU frame-rate promises, are
recorded in `ENHANCEMENT-PERFORMANCE.md`.

`tools/test_enhancements.c` checks arcade identity at 1x–4x, protected sprite positions,
priority geometry, event limits/reset/expiry, material roles and flips, lighting
response, and all six persisted switches. Local headless replays and comparison
captures are retained in `build/light-colour-20261113/`.
