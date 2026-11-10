#include "world_input.h"
#include <stdio.h>
#include <string.h>

enum { CAPTURE_IDLE, CAPTURE_RELEASE, CAPTURE_ARMED, CAPTURE_CANCEL };
enum { KEY_ESCAPE = 0x1b, STICK_THRESHOLD = 12000, TRIGGER_THRESHOLD = 30 };

static int controller_code_valid(unsigned code)
{
    return code <= WORLD_PAD_DEFAULT_UP && code != 11 && code != 12;
}

static int keyboard_code_valid(unsigned code)
{
    return code <= 255 && code != KEY_ESCAPE;
}

void world_input_bindings_defaults(WorldInputBindings *bindings)
{
    static const uint16_t keyboard[2][8] = {
        {0x27, 0x25, 0x28, 0x26, 'Z', 'X', '1', '5'},
        {'D', 'A', 'S', 'W', 'G', 'H', '2', '6'}
    };
    static const uint16_t controller[8] = {
        WORLD_PAD_DEFAULT_RIGHT, WORLD_PAD_DEFAULT_LEFT,
        WORLD_PAD_DEFAULT_DOWN, WORLD_PAD_DEFAULT_UP,
        WORLD_PAD_A, WORLD_PAD_B, WORLD_PAD_START, WORLD_PAD_BACK
    };
    memcpy(bindings->keyboard, keyboard, sizeof keyboard);
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player)
        memcpy(bindings->controller[player], controller, sizeof controller);
}

int world_input_bindings_valid(const WorldInputBindings *bindings)
{
    if (!bindings) return 0;
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player)
        for (unsigned action = 0; action < WORLD_INPUT_ACTIONS; ++action)
            if (!keyboard_code_valid(bindings->keyboard[player][action]) ||
                !controller_code_valid(bindings->controller[player][action])) return 0;
    return 1;
}

void world_input_init(WorldInput *input, const WorldInputBindings *bindings)
{
    memset(input, 0, sizeof *input);
    input->focused = 1;
    world_input_bindings_defaults(&input->bindings);
    if (world_input_bindings_valid(bindings)) input->bindings = *bindings;
}

int world_input_apply_bindings(WorldInput *input, const WorldInputBindings *bindings)
{
    if (!world_input_bindings_valid(bindings)) return 0;
    input->bindings = *bindings;
    return 1;
}

static int controller_down(const WorldInputController *pad, unsigned code)
{
    if (!pad->connected || !code) return 0;
    if (code <= 16) return !!(pad->buttons & (1u << (code - 1)));
    switch (code) {
    case WORLD_PAD_LEFT_STICK_RIGHT: return pad->left_x > STICK_THRESHOLD;
    case WORLD_PAD_LEFT_STICK_LEFT: return pad->left_x < -STICK_THRESHOLD;
    case WORLD_PAD_LEFT_STICK_DOWN: return pad->left_y < -STICK_THRESHOLD;
    case WORLD_PAD_LEFT_STICK_UP: return pad->left_y > STICK_THRESHOLD;
    case WORLD_PAD_RIGHT_STICK_RIGHT: return pad->right_x > STICK_THRESHOLD;
    case WORLD_PAD_RIGHT_STICK_LEFT: return pad->right_x < -STICK_THRESHOLD;
    case WORLD_PAD_RIGHT_STICK_DOWN: return pad->right_y < -STICK_THRESHOLD;
    case WORLD_PAD_RIGHT_STICK_UP: return pad->right_y > STICK_THRESHOLD;
    case WORLD_PAD_LEFT_TRIGGER: return pad->left_trigger > TRIGGER_THRESHOLD;
    case WORLD_PAD_RIGHT_TRIGGER: return pad->right_trigger > TRIGGER_THRESHOLD;
    case WORLD_PAD_DEFAULT_RIGHT: return (pad->buttons & 8) || pad->left_x > STICK_THRESHOLD;
    case WORLD_PAD_DEFAULT_LEFT: return (pad->buttons & 4) || pad->left_x < -STICK_THRESHOLD;
    case WORLD_PAD_DEFAULT_DOWN: return (pad->buttons & 2) || pad->left_y < -STICK_THRESHOLD;
    case WORLD_PAD_DEFAULT_UP: return (pad->buttons & 1) || pad->left_y > STICK_THRESHOLD;
    default: return 0;
    }
}

static int controls_released(const WorldInput *input)
{
    for (unsigned key = 1; key < 256; ++key) if (input->keys[key]) return 0;
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player)
        for (unsigned code = 1; code <= WORLD_PAD_RIGHT_TRIGGER; ++code)
            if (controller_code_valid(code) && controller_down(&input->controllers[player], code)) return 0;
    return 1;
}

static void release_mute(WorldInput *input)
{
    if (input->muted_until_release && controls_released(input)) input->muted_until_release = 0;
}

void world_input_set_focus(WorldInput *input, int focused)
{
    input->focused = !!focused;
    if (focused) return;
    memset(input->keys, 0, sizeof input->keys);
    memset(input->controllers, 0, sizeof input->controllers);
    input->muted_until_release = 0;
    if (input->capture_phase != CAPTURE_IDLE) input->capture_phase = CAPTURE_CANCEL;
}

void world_input_key(WorldInput *input, unsigned virtual_key, int down)
{
    if (!input->focused || virtual_key > 255) return;
    input->keys[virtual_key] = !!down;
    release_mute(input);
}

void world_input_controller(WorldInput *input, unsigned player, const WorldInputController *controller)
{
    if (player >= WORLD_INPUT_PLAYERS) return;
    memset(&input->controllers[player], 0, sizeof input->controllers[player]);
    if (input->focused && controller && controller->connected) input->controllers[player] = *controller;
    release_mute(input);
}

int world_input_action_held(const WorldInput *input, unsigned player, WorldInputAction action)
{
    if (!input->focused || input->muted_until_release || input->capture_phase != CAPTURE_IDLE ||
        player >= WORLD_INPUT_PLAYERS || (unsigned)action >= WORLD_INPUT_ACTIONS) return 0;
    unsigned key = input->bindings.keyboard[player][action];
    return (key && input->keys[key]) ||
           controller_down(&input->controllers[player], input->bindings.controller[player][action]);
}

void world_input_ports(const WorldInput *input, uint16_t *pads, uint16_t *system)
{
    static const uint8_t pad_bits[6] = {1, 2, 4, 8, 0x80, 0x40};
    uint16_t pressed = 0, buttons = 0;
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player) {
        for (unsigned action = 0; action < 6; ++action)
            if (world_input_action_held(input, player, (WorldInputAction)action))
                pressed |= (uint16_t)pad_bits[action] << (8 * player);
        if (world_input_action_held(input, player, WORLD_INPUT_START)) buttons |= 1u << player;
        if (world_input_action_held(input, player, WORLD_INPUT_COIN)) buttons |= 4u << player;
    }
    *pads = (uint16_t)~pressed;
    *system = (uint16_t)~buttons;
}

int world_input_capture_begin(WorldInput *input, unsigned player, WorldInputAction action, WorldInputSource source)
{
    if (!input->focused || player >= WORLD_INPUT_PLAYERS || (unsigned)action >= WORLD_INPUT_ACTIONS ||
        (source != WORLD_INPUT_KEYBOARD && source != WORLD_INPUT_CONTROLLER)) return 0;
    input->capture_player = (uint8_t)player;
    input->capture_action = (uint8_t)action;
    input->capture_source = (uint8_t)source;
    input->capture_phase = CAPTURE_RELEASE;
    input->muted_until_release = 1;
    return 1;
}

void world_input_capture_cancel(WorldInput *input)
{
    if (input->capture_phase == CAPTURE_IDLE) return;
    input->capture_phase = CAPTURE_CANCEL;
    input->muted_until_release = 1;
}

WorldInputCaptureResult world_input_capture_poll(WorldInput *input, uint16_t *binding)
{
    if (input->capture_phase == CAPTURE_IDLE) return WORLD_INPUT_CAPTURE_NONE;
    if (input->keys[KEY_ESCAPE]) world_input_capture_cancel(input);
    if (input->capture_phase == CAPTURE_CANCEL) {
        input->capture_phase = CAPTURE_IDLE;
        release_mute(input);
        return WORLD_INPUT_CAPTURE_CANCELLED;
    }
    if (input->capture_phase == CAPTURE_RELEASE) {
        if (controls_released(input)) input->capture_phase = CAPTURE_ARMED;
        return WORLD_INPUT_CAPTURE_WAITING;
    }
    unsigned code = 0;
    if (input->capture_source == WORLD_INPUT_KEYBOARD) {
        for (unsigned key = 1; key < 256; ++key)
            if (input->keys[key] && keyboard_code_valid(key)) { code = key; break; }
    } else {
        for (unsigned key = 1; key <= WORLD_PAD_RIGHT_TRIGGER; ++key)
            if (controller_code_valid(key) && controller_down(&input->controllers[input->capture_player], key)) {
                code = key;
                break;
            }
    }
    if (!code) return WORLD_INPUT_CAPTURE_WAITING;
    if (input->capture_source == WORLD_INPUT_KEYBOARD)
        input->bindings.keyboard[input->capture_player][input->capture_action] = (uint16_t)code;
    else input->bindings.controller[input->capture_player][input->capture_action] = (uint16_t)code;
    input->capture_phase = CAPTURE_IDLE;
    input->muted_until_release = 1;
    if (binding) *binding = (uint16_t)code;
    return WORLD_INPUT_CAPTURE_BOUND;
}

const char *world_input_action_name(WorldInputAction action)
{
    static const char *const names[8] = {"Right", "Left", "Down", "Up", "Torpedoes", "Secondary", "Start", "Insert coin"};
    return (unsigned)action < WORLD_INPUT_ACTIONS ? names[action] : "";
}

void world_input_binding_name(WorldInputSource source, uint16_t binding, char *output, size_t capacity)
{
    static const char *const controller[31] = {
        "Unbound", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back",
        "Left stick press", "Right stick press", "Left shoulder", "Right shoulder", "", "",
        "A", "B", "X", "Y", "Left stick right", "Left stick left", "Left stick down", "Left stick up",
        "Right stick right", "Right stick left", "Right stick down", "Right stick up", "Left trigger", "Right trigger",
        "D-pad / stick right", "D-pad / stick left", "D-pad / stick down", "D-pad / stick up"
    };
    if (!output || !capacity) return;
    const char *name = NULL;
    if (!binding) name = "Unbound";
    else if (source == WORLD_INPUT_CONTROLLER) {
        name = controller_code_valid(binding) ? controller[binding] : "Invalid";
    } else {
        switch (binding) {
        case 8: name = "Backspace"; break;
        case 9: name = "Tab"; break;
        case 13: name = "Enter"; break;
        case 16: name = "Shift"; break;
        case 17: name = "Ctrl"; break;
        case 18: name = "Alt"; break;
        case 32: name = "Space"; break;
        case 33: name = "Page up"; break;
        case 34: name = "Page down"; break;
        case 35: name = "End"; break;
        case 36: name = "Home"; break;
        case 37: name = "Left arrow"; break;
        case 38: name = "Up arrow"; break;
        case 39: name = "Right arrow"; break;
        case 40: name = "Down arrow"; break;
        case 45: name = "Insert"; break;
        case 46: name = "Delete"; break;
        }
    }
    if (name) snprintf(output, capacity, "%s", name);
    else if ((binding >= '0' && binding <= '9') || (binding >= 'A' && binding <= 'Z'))
        snprintf(output, capacity, "%c", (char)binding);
    else if (binding >= 112 && binding <= 135) snprintf(output, capacity, "F%u", binding - 111);
    else if (binding >= 96 && binding <= 105) snprintf(output, capacity, "Numpad %u", binding - 96);
    else snprintf(output, capacity, "Key %u", binding);
}
