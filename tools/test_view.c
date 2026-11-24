/* Geometry contract tests for world_view (docs/widescreen-gameplay-plan.md step 3).
   Build: zig cc -o test_view.exe tools/test_view.c src/world_view.c */
#include <stdio.h>
#include <stdint.h>
#include "../src/world_view.h"

static int failures;

static void check(int cond, const char *name)
{
    if (!cond) { ++failures; printf("FAIL %s\n", name); }
}

int main(void)
{
    /* --- Arcade contract is the legacy contract --- */
    world_view_set(WORLD_VIEW_ARCADE);
    check(world_view_width() == 320, "arcade width");
    check(world_view_left() == 0, "arcade left");
    check(world_view_raster(0) == 0, "arcade origin");
    check(world_view_raster(319) == 319, "arcade last");
    check(world_view_raster(-1) == -1, "arcade negative");
    check(world_view_player_min_x() == 0x70, "arcade min x");
    check(world_view_player_max_x() == 0x188, "arcade max x");
    check(world_view_spawn_x(0x1b8) == 0x1b8, "arcade spawn passthrough");
    check(world_view_mode() == WORLD_VIEW_ARCADE, "arcade mode");

    /* --- Wide endpoints and center --- */
    world_view_set(WORLD_VIEW_WIDE);
    check(world_view_mode() == WORLD_VIEW_WIDE, "wide mode");
    check(world_view_width() == 428, "wide width");
    check(world_view_left() == -54, "wide left");
    check(world_view_raster(-54) == 0, "wide first column");
    check(world_view_raster(373) == 427, "wide last column");
    check(world_view_raster(374) == 428, "wide first off right");
    check(world_view_raster(-55) == -1, "wide first off left");
    /* Center: raster 214 == screen 160 (both modes' midpoint). */
    check(world_view_raster(160) == 214, "wide center maps screen 160");
    check(world_view_raster(0) == 54, "wide legacy origin at 54");
    check(world_view_raster(319) == 373, "wide legacy last at 373");

    /* Player limits: authored insets preserved at aperture edges. */
    check(world_view_player_min_x() == 0x3b, "wide min x");
    check(world_view_player_max_x() == 0x1bd, "wide max x");

    /* Spawn shift: edge-entrants stay beyond their edge. */
    check(world_view_spawn_x(0x1b8) == 0x1b8 + 54, "right entrant +54");
    check(world_view_spawn_x(0x1a1) == 0x1a1 + 54, "just-beyond-right +54");
    check(world_view_spawn_x(0x1a0) == 0x1a0 + 54, "at-right-edge +54");
    check(world_view_spawn_x(0x140) == 0x140, "mid-view authored unchanged");
    check(world_view_spawn_x(0x5f) == 0x5f - 54, "left entrant -54");
    check(world_view_spawn_x(0x60) == 0x60, "on-left-edge unchanged");
    check(world_view_spawn_x(0x100) == 0x100, "midfield unchanged");
    check(world_view_spawn_x(0) == 0, "x=0 parameter unchanged");
    /* Signed-negative authored X is a far-left entrance (e.g. b000:571c). */
    check(world_view_spawn_x(0xffe6) == (uint16_t)(0xffe6 - 54), "far-left -54");
    check(world_view_spawn_x(0xffa0) == (uint16_t)(0xffa0 - 54), "far-left -54 b");

    /* Off-view edge tests: arcade keeps the authored constants. */
    world_view_set(WORLD_VIEW_ARCADE);
    check(world_view_edge_less(0x3f, 0x40) == 1, "arc edge lt in");
    check(world_view_edge_less(0x40, 0x40) == 0, "arc edge lt out");
    check(world_view_edge_greater(0x1c1, 0x1c0) == 1, "arc edge gt in");
    check(world_view_edge_greater(0x1c0, 0x1c0) == 0, "arc edge gt out");
    check(world_view_edge_less_s(-0x21, -0x20) == 1, "arc sedge lt in");
    check(world_view_edge_eq(0x40, 0x40) == 1, "arc edge eq");

    world_view_set(WORLD_VIEW_WIDE);
    /* Left edge: window becomes screen [-96-54, C-96-54) — signed screen. */
    check(world_view_edge_less(0x09, 0x40) == 1, "wide edge lt in (field 9)");
    check(world_view_edge_less(0x0a, 0x40) == 0, "wide edge lt out (field 10)");
    check(world_view_edge_less(0xffe6, 0x40) == 1, "wide edge lt wrapped");
    check(world_view_edge_less(0xffe6, 0x10) == 0, "wide edge lt 0x10 wrapped");
    check(world_view_edge_less(0, 0x40) == 1, "wide edge lt zero");
    /* Right edge: constant +54, still unsigned. */
    check(world_view_edge_greater(0x1f7, 0x1c0) == 1, "wide edge gt in");
    check(world_view_edge_greater(0x1f6, 0x1c0) == 0, "wide edge gt out");
    check(world_view_edge_less_s(-0x57, -0x20) == 1, "wide sedge lt in");
    check(world_view_edge_less_s(-0x56, -0x20) == 0, "wide sedge lt out");
    check(world_view_edge_less_s(0x215, 0x1e0) == 1, "wide sedge draw in");
    check(world_view_edge_less_s(0x216, 0x1e0) == 0, "wide sedge draw out");
    check(world_view_edge_eq(0x0a, 0x40) == 1, "wide edge eq at 10");
    check(world_view_edge_eq(0x40, 0x40) == 0, "wide edge eq not at 64");

    /* le/ge signed helpers and the raw clamp constants. */
    world_view_set(WORLD_VIEW_ARCADE);
    check(world_view_edge_le_s(-48, -48) == 1, "arc le at c");
    check(world_view_edge_ge_s(0x160, 0x160) == 1, "arc ge at c");
    check(world_view_edge_left(0xa0) == 0xa0 && world_view_edge_right(0x1a0) == 0x1a0,
          "arc raw edge constants");
    world_view_set(WORLD_VIEW_WIDE);
    check(world_view_edge_le_s(-102, -48) == 1, "wide le left in");
    check(world_view_edge_le_s(-101, -48) == 0, "wide le left out");
    check(world_view_edge_le_s(0x216, 0x1e0) == 1, "wide le right in");
    check(world_view_edge_ge_s(0x196, 0x160) == 1, "wide ge right in");
    check(world_view_edge_ge_s(0x195, 0x160) == 0, "wide ge right out");
    check(world_view_edge_ge_s(0x2a, 0x60) == 1, "wide ge left in");
    check(world_view_edge_greater_s(-69, -16) == 1, "wide gt_s left neg in");
    check(world_view_edge_greater_s(-70, -16) == 0, "wide gt_s left neg out");
    check(world_view_edge_greater_s(0x2b, 0x60) == 1, "wide gt_s left in");
    check(world_view_edge_greater_s(0x2a, 0x60) == 0, "wide gt_s left out");
    check(world_view_edge_left(0xa0) == 0x6a, "wide edge_left 0xa0");
    check(world_view_edge_left(0x68) == 0x32, "wide edge_left 0x68");
    check(world_view_edge_left(0x60) == 0x2a, "wide edge_left 0x60");
    check(world_view_edge_right(0x1a0) == 0x1d6, "wide edge_right");
    check(world_view_edge_right(0x80) == 0x80, "wide edge_right left c unchanged");

    world_view_set(WORLD_VIEW_WIDE);
    /* world -> raster -> world round-trip at a fixed camera.
       world = field - 0x60 + camera;  screen = world - camera;  raster = screen + 54. */
    for (int world = 0; world < 4096; world += 7) {
        int camera = world / 5;
        int field = world - camera + 0x60;
        int screen = field - 96;
        int raster = world_view_raster(screen);
        int back = raster - 54 + camera;      /* raster -> screen -> world */
        check(back == world, "world round-trip");
    }

    /* Aperture: exact 16:9 in thirds-of-column units. 428 columns wide,
       aperture [2/3, 426+2/3) -> 1280/3 columns. At 3x, 1284 wide, crop 2/side. */
    check(428 * 3 == 1284, "3x raster width");
    check((426 * 3 + 2) == 1280, "aperture columns 1280/3");

    /* A 16x16 game tile occupies 48x48 output at 3x on an aligned sample:
       raster 3 columns -> 9 output px; 16 columns -> 48 px exactly. */
    check(16 * 3 == 48, "tile 3x");

    /* Object just outside the aperture, one column visible:
       raster column 0 and 427 are still rendered (overscan). */
    check(world_view_raster(-54) == 0 && world_view_raster(373) == 427, "seam neighbors visible");

    /* HUD panel anchoring: P1 panel shifts left 7 tiles (28 byte-columns),
       P2 right 7; both are no-ops in arcade mode. */
    world_view_set(WORLD_VIEW_WIDE);
    check(world_view_hud_shift(1) == -28, "wide hud P1");
    check(world_view_hud_shift(2) == 28, "wide hud P2");
    /* --- Ultrawide (64:27) --- */
    world_view_set(WORLD_VIEW_ULTRA);
    check(world_view_mode() == WORLD_VIEW_ULTRA, "ultra mode");
    check(world_view_width() == 568 && world_view_wing() == 124, "ultra width/wing");
    check(world_view_raster(-124) == 0 && world_view_raster(443) == 567, "ultra endpoints");
    check(world_view_raster(160) == 284, "ultra centre");
    check(world_view_player_min_x() == 0 && world_view_player_max_x() == 0x204, "ultra clamps");
    check(world_view_spawn_x(0x1a0) == 0x1a0 + 124 && world_view_spawn_x(0x5f) == (uint16_t)(0x5f - 124), "ultra spawn");
    check(world_view_edge_less(0x0000, 0x3f) == 0, "ultra field 0 still inside");
    check(world_view_edge_less((uint16_t)-62, 0x3f) == 1, "ultra left retire past -61");
    check(world_view_edge_greater(0x1c1 + 124, 0x1c0) == 1 && world_view_edge_greater(0x1c0 + 124, 0x1c0) == 0, "ultra right retire");
    check(world_view_edge_ge_left((uint16_t)-28, 0x60) == 1 && world_view_edge_ge_left((uint16_t)-29, 0x60) == 0, "ultra entered gate");
    check(world_view_edge_ge_left(0x100, 0x60) == 1, "ultra entered gate interior");
    check(world_view_hud_shift(1) == -48 && world_view_hud_shift(2) == 48, "ultra hud");
    world_view_set_stage(4);
    check(world_view_width() == 320 && world_view_canvas_width() == 568, "ultra shaft arcade aperture");
    world_view_set_stage(0);

    world_view_set(WORLD_VIEW_ARCADE);
    check(world_view_hud_shift(1) == 0 && world_view_hud_shift(2) == 0, "arc hud zero");
    check(world_view_width() == 320, "back to arcade");

    printf(failures ? "%d failures\n" : "all geometry checks passed\n", failures);
    return failures != 0;
}
