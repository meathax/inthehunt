# Widescreen coordinate contract (step 3 evidence)

Coordinate spaces, all measured in the same square unit (one game pixel):

| Space | Definition |
|---|---|
| field | hardware sprite-X domain: `field = screen + 96` (actor +0x21, OAM X word). 9-bit, wraps at 512. |
| screen | signed picture coordinate: `screen = field - 96 = world - camera`. Arcade visible [0,320); wide raster [-54,374). |
| world | physical object position on the map: `world = screen + camera` (camera = ram 0xb70a). |
| raster | output column in the frame buffer: `raster = screen - view.left = screen + 54` in wide; `raster = screen` in arcade. |
| progress | script progress ram 0xb700 (camera advance total; spawn triggers compare signed). |
| map | tilemap cell address `row<<8 + col`, col = `((x&0xfff8)>>1 + 0x30) & 255` (world_tile_address.h); map base = field - 0x60 + 0xb70a = world. |
| y-field | actor +0x24: `screenY = 375 - field`. HUD/audio code uses 0x8b.. regs and 375-f. |

Verified transforms (one tick):
- Submarine: `field_x = actor+0x21`; `screen = field - 96`; `world = field - 0x60 + 0xb70a`.
  Probe: `world_tile_address(x - 0x60 + b70a, 0x177 - y + b70c)` (world_player.c:197).
- Tile (layer i): `sx = raster_or_screen_x + 80 + scroll_reg - 2*i` where scroll_reg = `word(0xb706+4i) + 16 + 2*i`
  (capture_video adds the bias) => `sx = screen + word(0xb706+4i) + 96 - 2*i + (wide_layer ? 256 : 0)`.
  cell = `(master&3)*0x4000 + ((sy/8)*columns + ((sx/8)&(columns-1)))*4` (world_video.c).
- Sprite: `left = ((X - 16) & 511) - 80 = X - 96` -> picture X of first 16px column (world_video.c:106).
- Camera: 0xb70a = fg scroll X (camera); deltas published at 0xb71a..0xb72e; actors scroll-compensate +0x21/+0x24 via scroll_actor.

## Mode geometry

| | Arcade | Wide |
|---|---|---|
| raster W | 320 | 428 (aperture 426 2/3, 2/3+2/3 overscan) |
| view.left | 0 | -54 |
| player X limits (field) | [0x70, 0x188] (insets +16 / -24) | [0x3B, 0x1BD] vs aperture edges [-53.33, 373.33), rounded inward |
| camera | unchanged (works on field = world-camera+96 already) | unchanged |
| edge entrants | authored beyond [0x60,0x1A0) field = beyond screen [0,320) | shifted +-54 to stay beyond new edges; a drain-time clamp in world_stage_spawns pins late-drained right entrants to field 0x1D6 so a camera overshoot can never land one inside the aperture |

Player-limit derivation: authored insets are +16 (left edge field 0x60 -> limit 0x70)
and -24 (right edge 0x1A0 -> limit 0x188). Wide aperture edges are legacy X in
[-53 1/3, 373 1/3): left = ceil(-53.33 + 16 + 96) = 59 = 0x3B; right =
floor(373.33 - 24 + 96) = 445 = 0x1BD.

## Geometry dependency inventory (src)

Rendering:
- world_video.c: scanline output[320]; playfields `unsigned x` param (needs signed);
  sprite clip x>=0&&x<320; wrap alias xpos>=497 -> xpos-512; columns*16 extents.
- world_native.c: pixels[320*240]; shown[320*240*K*K]; paused_frame; draw_captured_pixels
  iterates 320; headless_shot 320x240 BMP; scene_submit lamp `x=field-96, x<320`;
  locate_sounds x in [-48,368) pan /160; observe_weapon/imm_actor x<320; menu canvas.
- world_menu.c: enum W=320 canvas stride everywhere; title panel 96..224 (needs +1 row).
- world_window.c: enum WIDTH=320; frame[] buffer; paint_area 4:3 fit; compose_bmp; min window.
- world_gpu.cpp / post_chain.cpp: W=320,H=240, 4:3 aspect fields, textures, viewport/scissor.
- world_scene.c: re-renders tiles/sprites at K*320x240 (its own clip assumptions; must accept 428 source).

Gameplay (screen-space, all in field units = screen+96):
- world_player.c: x clamps 0x70/0x188 (world_sub_open_water L346,353; world_sub_entry_step L250).
- world_camera.c: pos<0xa0 blocks scroll; origin actor+0x4c; cap 0xbf; world-relative already.
- world_stage.c: child+0x21 = trigger - camera + authoredX (spawn = screen at spawn time).
- world_dispatch.c: ~100 scroll_actor(0xb71a/0xb71c/0xb726/...) sites; edge retire checks
  (e.g. L395 `x < 0x3f` despawn, others throughout); world_draw_tile_block producer
  (11x 4x4 blocks per +32 fg / +64 bg trigger); update_camera_limits (0xb734.. bounds);
  enemy fire ranges measured vs player +0x21 (screen domain - widen only screen-edge checks).
- HUD: world_stage_opening_hud / world_sub_hud write tiles at fixed map cells (layer 0, non-scrolling);
  lands at legacy positions = raster +54 (functional for slice; semantic anchoring = step 9).
- Audio pan/listeners: world_native.c locate_sounds x-96, /160, bounds [-48,368).

## What does NOT change

- Camera algorithm, scroll speed table, script progress 0xb700, trigger order,
  vertical geometry (y untouched), weapon speeds/damage/lifetimes, difficulty,
  authored stage order, Arcade code path (mode == ARCADE keeps every constant).
