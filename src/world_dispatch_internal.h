#ifndef WORLD_DISPATCH_INTERNAL_H
#define WORLD_DISPATCH_INTERNAL_H
/* Shared by the actor-handler sources. Not a public interface. */
#include "world_dispatch.h"
#include "world_enemy_target.h"
#include "world_stage.h"
#include "world_title.h"
#include "world_torpedo.h"
#include "world_tile_address.h"
#include "world_stage5.h"
#include "world_stale.h"

/* Result of a handler group for one actor call. */
enum {
    WORLD_HANDLER_UNKNOWN = -1, /* Address belongs to another group. */
    WORLD_HANDLER_STOP = 0,     /* Reached a path that is not recovered yet. */
    WORLD_HANDLER_DONE = 2      /* Handled; the dispatcher links to the next actor. */
};
/* actor_io holds BP on entry and the actor to link from on return. */
typedef int WorldHandlerGroup(WorldDispatch *pass, uint8_t *ram, const uint8_t *main_rom,
                              uint8_t *tiles, uint16_t *actor_io, uint32_t address);
WorldHandlerGroup world_actors_1, world_actors_2, world_actors_3, world_actors_4, world_actors_5, world_actors_6;
WorldHandlerGroup world_stage1_handlers, world_stage2_handlers, world_stage3_handlers,
                  world_stage4_handlers, world_stage5_handlers, world_stage6_handlers,
                  world_common_handlers, world_gaps_handlers, world_credits_handlers, world_coop_handlers, world_explore1_handlers, world_explore2_handlers;

/* Recovered routines in other sources. */
uint16_t world_player_contact(uint8_t *, const uint8_t *, uint16_t);
int world_ship_contact(const uint8_t *, const uint8_t *, uint16_t);
uint16_t world_projectile_contact(uint8_t *, const uint8_t *, uint16_t);
int world_shape_overlap(const uint8_t *, const uint8_t *, uint16_t, uint16_t, uint16_t, uint16_t);
void world_kind11_death(uint8_t *, const uint8_t *, uint16_t, uint16_t);
void world_spawn_radial_explosion(uint8_t *, const uint8_t *, uint16_t);
void world_kind11_move(uint8_t *, const uint8_t *, const uint8_t *, uint16_t);
uint16_t world_terrain_mask(const uint8_t *, const uint8_t *, uint16_t, uint16_t);
void world_queue_sound(uint8_t *, uint16_t);
uint16_t world_surface_contact(uint8_t *, const uint8_t *, uint16_t);
void world_award_score(uint8_t *, const uint8_t *, uint16_t, uint16_t);
uint16_t world_spawn_child(uint8_t *, uint16_t, uint16_t);
uint16_t world_append_candidate(uint8_t *, uint16_t);
int world_horizontal_camera(uint8_t *, const uint8_t *, uint16_t, uint16_t, uint16_t);
int world_sub_open_water(uint8_t *, const uint8_t *, uint8_t *, uint16_t);
void world_sub_lives_hud(const uint8_t *, uint8_t *, unsigned, uint16_t);
void world_sub_entry_step(uint8_t *, const uint8_t *, uint8_t *, uint16_t);

/* Shared actor helpers defined in world_dispatch.c. */
uint16_t read_word(const uint8_t *ram, uint16_t offset);
void write_word(uint8_t *ram, uint16_t offset, uint16_t value);
void swap_bytes(uint8_t *ram, uint16_t offset);
void submit_sprite(uint8_t *ram, uint16_t actor, uint16_t sprite, uint16_t segment, uint8_t palette);
int queue_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, int flash);
uint16_t decrement(uint8_t *ram, uint16_t address);
void finish_animation(uint8_t *ram, uint16_t actor);
void scroll_actor(uint8_t *ram, uint16_t actor, uint16_t delta);
void update_attached_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);
void move_actor_axis(uint8_t *ram, uint16_t position, uint16_t velocity);
void update_intro_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);
void animate_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void release_actor_resources(uint8_t *ram, uint16_t actor);
void retire_actor(uint8_t *ram, uint16_t actor);
uint8_t vector_angle(const uint8_t *rom, int16_t dx, int16_t dy);
void apply_radial_impulse(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t player, unsigned hit_bit);
void update_radial_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, int attached);
void update_scenery(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, int near_layer);
int above_cruiser(const uint8_t *ram, uint16_t enemy, uint16_t player);
void fire_cruiser_weapon(uint8_t *ram, const uint8_t *rom, uint16_t actor, int vertical);
void queue_surface_enemy(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void surface_enemy_death_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void register_surface_enemy(uint8_t *ram, uint16_t actor);
void surface_enemy_hit(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void begin_linear_script(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t offset);
int step_flight_script(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_cruiser_fire(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t burst);
void draw_cruiser_and_check_hits(uint8_t *ram, const uint8_t *rom, uint16_t actor);
int update_cruising_enemy(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor);
uint16_t effect_slot(uint8_t *ram, uint16_t parent, uint16_t handler);
uint16_t stage_two_machine_parent(uint8_t *ram, uint16_t actor);
uint16_t bank_b_effect(uint8_t *ram, uint16_t parent, uint16_t handler);
int player_near_turret(const uint8_t *ram, const uint8_t *rom, uint16_t actor);
int turret_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void turret_effect(uint8_t *ram, const uint8_t *bank, uint16_t actor, int opening);
void spawn_stage_pickup(uint8_t *ram, uint16_t actor);
int outside_pickup_area(const uint8_t *ram, uint16_t actor);
int warship_attachment(uint8_t *ram, uint16_t actor);
void aim_warship_turret(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned facing);
void warship_part_explosion(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void boss_chain_move(uint8_t *ram, uint16_t actor);
void boss_limb_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor);
uint16_t spawn_boss_part(uint8_t *ram, uint16_t actor, uint16_t handler);
void boss_blast(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t entry);
int boss_claw_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);
void boss_appendage(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);
void warship_blast_sequence(uint8_t *ram, uint16_t actor, uint16_t table, uint16_t delay);
int spawn_warship_parts(uint8_t *ram, const uint8_t *rom, uint16_t actor);
int advance_timed_sprite(uint8_t *ram, const uint8_t *bank, uint16_t actor, uint16_t table);
void spawn_bomb_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor);
uint16_t mission_child(uint8_t *ram, uint16_t actor, uint16_t handler);
void mission_text(uint8_t *ram, const uint8_t *bank, uint16_t actor);
void start_mission_scroll(uint8_t *ram, const uint8_t *bank, uint16_t actor);
void update_mission_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void init_stage_list(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void init_player_slot(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned player);
void set_projectile_trail_length(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor);
void init_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor);
void cycle_torpedo_sprite(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t table, uint16_t delay);
void spawn_impact(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_standard_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor, int accelerating);
int update_powered_torpedo(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor, uint32_t handler);
void update_sub_display(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void fire_secondary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void fire_primary_launcher(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t mode);
int update_idle_equipment(uint8_t *ram, const uint8_t *rom, uint16_t actor, int secondary);
uint16_t draw_join_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label);
void tally_tile(uint8_t *tiles, uint16_t dest, uint16_t glyph, unsigned attr);
void tally_block(uint8_t *tiles, uint16_t dest, unsigned width, unsigned height, unsigned glyph, unsigned attr);
void tally_stamp(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t shape, unsigned attr);
void tally_large_label(uint8_t *tiles, const uint8_t *rom, uint16_t dest, uint16_t label, unsigned attr);
void tally_score(const uint8_t *ram, const uint8_t *rom, uint8_t *tiles, unsigned side);
void name_glyph(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor);
void finish_name_selection(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor);
void name_input(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor);
void tally_bonus(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor);
int tally_skip_requested(const uint8_t *ram);
void tally_add_count(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, unsigned type);
void tally_groups(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, int large);
void update_join_hud(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor);
void update_rank(uint8_t *ram, const uint8_t *rom);
void load_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void advance_scroll_script(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_camera_limits(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void init_tile_layer(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned layer);
void update_layer_scroll(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_tile_script(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, uint16_t base);
void queue_palette(uint8_t *ram, unsigned slot, uint16_t palette, uint16_t speed);
int digit_product(int value, int factor);
void project_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void queue_continue_sound(uint8_t *ram, uint16_t command);
void shatter_continue_digit(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void digit_fragment_velocity(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void acquire_private_palette(uint8_t *ram, uint16_t actor, unsigned resource);
void refresh_stage_time(uint8_t *ram, uint8_t *tiles, uint16_t actor, unsigned seconds);
void expire_stage_time(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_stage_trigger(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void enemy_torpedo_contact(uint8_t *ram, const uint8_t *rom, uint16_t actor, unsigned mask);
void water_entry_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y);
uint16_t spawn_surface_splash_effect(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y);
void spawn_surface_splash(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t x, uint16_t y);
int update_vertical_weapon(uint8_t *ram, const uint8_t *rom, const uint8_t *terrain, uint16_t actor, uint16_t handler);
void update_vertical_camera(uint8_t *ram, const uint8_t *rom, uint16_t actor);
void update_background_vertical(uint8_t *ram, uint16_t actor);
void update_camera_layers(uint8_t *ram, const uint8_t *rom, uint8_t *tiles, uint16_t actor, int background, int vertical_control);
void surface_enemy_pose(uint8_t *ram, const uint8_t *bank, uint16_t actor, int magnitude);
void surface_enemy_fire(uint8_t *ram, uint16_t actor);
int surface_enemy_swim(uint8_t *ram, const uint8_t *rom, const uint8_t *tiles, uint16_t actor, uint16_t handler);
int boss_segment_step(uint8_t *ram, const uint8_t *rom, uint16_t actor, uint16_t handler);

#endif
