#ifndef WORLD_INPUT_H
#define WORLD_INPUT_H
#include <stddef.h>
#include <stdint.h>

enum { WORLD_INPUT_PLAYERS = 2, WORLD_INPUT_ACTIONS = 8 };
typedef enum {
    WORLD_INPUT_RIGHT, WORLD_INPUT_LEFT, WORLD_INPUT_DOWN, WORLD_INPUT_UP,
    WORLD_INPUT_FIRE1, WORLD_INPUT_FIRE2, WORLD_INPUT_START, WORLD_INPUT_COIN
} WorldInputAction;

typedef enum {
    WORLD_INPUT_KEYBOARD, WORLD_INPUT_CONTROLLER
} WorldInputSource;

/* Codes 1..16 select the corresponding XInput button bit plus one.
 * The two unused XInput bits have no binding codes. */
typedef enum {
    WORLD_PAD_NONE = 0,
    WORLD_PAD_UP = 1, WORLD_PAD_DOWN, WORLD_PAD_LEFT, WORLD_PAD_RIGHT,
    WORLD_PAD_START, WORLD_PAD_BACK, WORLD_PAD_LEFT_THUMB, WORLD_PAD_RIGHT_THUMB,
    WORLD_PAD_LEFT_SHOULDER, WORLD_PAD_RIGHT_SHOULDER,
    WORLD_PAD_A = 13, WORLD_PAD_B, WORLD_PAD_X, WORLD_PAD_Y,
    WORLD_PAD_LEFT_STICK_RIGHT, WORLD_PAD_LEFT_STICK_LEFT,
    WORLD_PAD_LEFT_STICK_DOWN, WORLD_PAD_LEFT_STICK_UP,
    WORLD_PAD_RIGHT_STICK_RIGHT, WORLD_PAD_RIGHT_STICK_LEFT,
    WORLD_PAD_RIGHT_STICK_DOWN, WORLD_PAD_RIGHT_STICK_UP,
    WORLD_PAD_LEFT_TRIGGER, WORLD_PAD_RIGHT_TRIGGER,
    WORLD_PAD_DEFAULT_RIGHT, WORLD_PAD_DEFAULT_LEFT,
    WORLD_PAD_DEFAULT_DOWN, WORLD_PAD_DEFAULT_UP
} WorldInputControllerCode;

typedef struct {
    uint16_t keyboard[WORLD_INPUT_PLAYERS][WORLD_INPUT_ACTIONS];
    uint16_t controller[WORLD_INPUT_PLAYERS][WORLD_INPUT_ACTIONS];
} WorldInputBindings;

typedef struct {
    uint16_t buttons;
    int16_t left_x, left_y, right_x, right_y;
    uint8_t left_trigger, right_trigger, connected;
} WorldInputController;

typedef enum {
    WORLD_INPUT_CAPTURE_NONE, WORLD_INPUT_CAPTURE_WAITING,
    WORLD_INPUT_CAPTURE_BOUND, WORLD_INPUT_CAPTURE_CANCELLED
} WorldInputCaptureResult;

typedef struct {
    WorldInputBindings bindings;
    uint8_t keys[256];
    WorldInputController controllers[WORLD_INPUT_PLAYERS];
    uint8_t focused, muted_until_release;
    uint8_t capture_phase, capture_player, capture_action, capture_source;
} WorldInput;

void world_input_bindings_defaults(WorldInputBindings *bindings);
int world_input_bindings_valid(const WorldInputBindings *bindings);
void world_input_init(WorldInput *input, const WorldInputBindings *bindings);
int world_input_apply_bindings(WorldInput *input, const WorldInputBindings *bindings);
void world_input_set_focus(WorldInput *input, int focused);
void world_input_key(WorldInput *input, unsigned virtual_key, int down);
void world_input_controller(WorldInput *input, unsigned player, const WorldInputController *controller);
/* Raw cabinet words remain active-low. Opposing directions are preserved. */
void world_input_ports(const WorldInput *input, uint16_t *pads, uint16_t *system);
int world_input_action_held(const WorldInput *input, unsigned player, WorldInputAction action);

/* Begin capture after the remapping row is confirmed. Initial held controls
 * must be released before capture arms. Escape cancels either source.
 * Successful capture updates input->bindings for persistence. Game controls
 * remain muted until all controls are released after binding/cancellation. */
int world_input_capture_begin(WorldInput *input, unsigned player, WorldInputAction action, WorldInputSource source);
WorldInputCaptureResult world_input_capture_poll(WorldInput *input, uint16_t *binding);
void world_input_capture_cancel(WorldInput *input);
const char *world_input_action_name(WorldInputAction action);
void world_input_binding_name(WorldInputSource source, uint16_t binding, char *output, size_t capacity);
#endif
