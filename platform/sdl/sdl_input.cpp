// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdl_input.hpp"

#include <algorithm>
#include <cmath>

namespace slingshot {
namespace {

namespace key = android::key;

// SDL scancode -> android.view.KeyEvent keycode (a USB keyboard on Android).
int android_keycode(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 29 + (s - SDL_SCANCODE_A);   // KEYCODE_A..Z
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return 8 + (s - SDL_SCANCODE_1);    // KEYCODE_1..9
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 131 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return 145 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
        case SDL_SCANCODE_0: return 7;
        case SDL_SCANCODE_RETURN: return key::kEnter;
        case SDL_SCANCODE_ESCAPE: return key::kEscape;
        case SDL_SCANCODE_BACKSPACE: return key::kDel;
        case SDL_SCANCODE_TAB: return 61;
        case SDL_SCANCODE_SPACE: return 62;
        case SDL_SCANCODE_MINUS: return 69;
        case SDL_SCANCODE_EQUALS: return 70;
        case SDL_SCANCODE_LEFTBRACKET: return 71;
        case SDL_SCANCODE_RIGHTBRACKET: return 72;
        case SDL_SCANCODE_BACKSLASH: return 73;
        case SDL_SCANCODE_SEMICOLON: return 74;
        case SDL_SCANCODE_APOSTROPHE: return 75;
        case SDL_SCANCODE_GRAVE: return 68;
        case SDL_SCANCODE_COMMA: return 55;
        case SDL_SCANCODE_PERIOD: return 56;
        case SDL_SCANCODE_SLASH: return 76;
        case SDL_SCANCODE_CAPSLOCK: return 115;
        case SDL_SCANCODE_PRINTSCREEN: return 120;
        case SDL_SCANCODE_SCROLLLOCK: return 116;
        case SDL_SCANCODE_PAUSE: return 121;
        case SDL_SCANCODE_INSERT: return 124;
        case SDL_SCANCODE_HOME: return 122;
        case SDL_SCANCODE_PAGEUP: return 92;
        case SDL_SCANCODE_DELETE: return 112;  // FORWARD_DEL
        case SDL_SCANCODE_END: return 123;
        case SDL_SCANCODE_PAGEDOWN: return 93;
        case SDL_SCANCODE_RIGHT: return key::kDpadRight;
        case SDL_SCANCODE_LEFT: return key::kDpadLeft;
        case SDL_SCANCODE_DOWN: return key::kDpadDown;
        case SDL_SCANCODE_UP: return key::kDpadUp;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 143;
        case SDL_SCANCODE_KP_DIVIDE: return 154;
        case SDL_SCANCODE_KP_MULTIPLY: return 155;
        case SDL_SCANCODE_KP_MINUS: return 156;
        case SDL_SCANCODE_KP_PLUS: return 157;
        case SDL_SCANCODE_KP_ENTER: return 160;
        case SDL_SCANCODE_KP_0: return 144;
        case SDL_SCANCODE_KP_PERIOD: return 158;
        case SDL_SCANCODE_APPLICATION: return key::kMenu;
        case SDL_SCANCODE_MENU: return key::kMenu;
        case SDL_SCANCODE_AC_BACK: return key::kBack;
        case SDL_SCANCODE_LCTRL: return 113;
        case SDL_SCANCODE_RCTRL: return 114;
        case SDL_SCANCODE_LSHIFT: return 59;
        case SDL_SCANCODE_RSHIFT: return 60;
        case SDL_SCANCODE_LALT: return 57;
        case SDL_SCANCODE_RALT: return 58;
        case SDL_SCANCODE_LGUI: return 117;
        case SDL_SCANCODE_RGUI: return 118;
        default: return 0;
    }
}

// SDL gamepad button -> Android keycode (as Android's generic gamepad layouts
// report them). The d-pad is reported as hat axes, like Xbox-style pads do.
int android_button(Uint8 b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return key::kButtonA;
        case SDL_GAMEPAD_BUTTON_EAST: return key::kButtonB;
        case SDL_GAMEPAD_BUTTON_WEST: return key::kButtonX;
        case SDL_GAMEPAD_BUTTON_NORTH: return key::kButtonY;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return key::kButtonL1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return key::kButtonR1;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return key::kButtonThumbL;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return key::kButtonThumbR;
        case SDL_GAMEPAD_BUTTON_START: return key::kButtonStart;
        case SDL_GAMEPAD_BUTTON_BACK: return key::kButtonSelect;
        case SDL_GAMEPAD_BUTTON_GUIDE: return key::kButtonMode;
        default: return 0;
    }
}

// Android zeroes axis values inside the device's flat range (InputDelegator.a).
constexpr float kStickFlat = 0.08f;

float stick(Sint16 v) {
    const float f = std::clamp(v / 32767.0f, -1.0f, 1.0f);
    return std::fabs(f) < kStickFlat ? 0.0f : f;
}

}  // namespace

SdlInput::~SdlInput() {
    for (auto& [id, g] : gamepads_) SDL_CloseGamepad(g.pad);
}

float SdlInput::density() const { return SDL_GetWindowPixelDensity(window_); }

void SdlInput::handle(const SDL_Event& e) {
    switch (e.type) {
        // ---- mouse
        case SDL_EVENT_MOUSE_MOTION: {
            if (e.motion.which == SDL_TOUCH_MOUSEID) break;
            const float x = e.motion.x * density(), y = e.motion.y * density();
            if (mouse_pointer_ >= 0) {
                input_.pointer_move(mouse_pointer_, x, y);
            } else if (!input_.active_pointers()) {
                input_.hover(hovering_ ? android::kActionHoverMove : android::kActionHoverEnter, x, y);
                hovering_ = true;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.which != SDL_TOUCH_MOUSEID) mouse_button(e.button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (hovering_) {
                float x, y;
                SDL_GetMouseState(&x, &y);
                input_.hover(android::kActionHoverExit, x * density(), y * density());
                hovering_ = false;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // The system takes the touch stream away (e.g. a dialog): ACTION_CANCEL.
            if (input_.active_pointers()) input_.cancel_pointers();
            mouse_pointer_ = -1;
            fingers_.clear();
            break;

        // ---- touch screen
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            finger(e.tfinger);
            break;

        // ---- keyboard
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            keyboard(e.key);
            break;

        // ---- gamepads
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepads_.count(e.gdevice.which))
                if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) gamepads_[e.gdevice.which].pad = pad;
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (auto it = gamepads_.find(e.gdevice.which); it != gamepads_.end()) {
                SDL_CloseGamepad(it->second.pad);
                gamepads_.erase(it);
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            gamepad_button(e.gbutton, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            gamepad_axis(e.gaxis);
            break;
        default:
            break;
    }
}

void SdlInput::mouse_button(const SDL_MouseButtonEvent& b, bool down) {
    const float x = b.x * density(), y = b.y * density();
    if (b.button == SDL_BUTTON_X1) {  // Android maps the mouse "back" button to KEYCODE_BACK
        input_.key(down, key::kBack, 0, android::kBuiltInKeyboard);
        return;
    }
    if (b.button != SDL_BUTTON_LEFT) return;
    if (down && mouse_pointer_ < 0) {
        if (hovering_) {
            input_.hover(android::kActionHoverExit, x, y);
            hovering_ = false;
        }
        mouse_pointer_ = input_.pointer_down(x, y);
    } else if (!down && mouse_pointer_ >= 0) {
        input_.pointer_move(mouse_pointer_, x, y);
        input_.pointer_up(mouse_pointer_);
        mouse_pointer_ = -1;
        if (!input_.active_pointers()) {
            input_.hover(android::kActionHoverEnter, x, y);
            hovering_ = true;
        }
    }
}

void SdlInput::finger(const SDL_TouchFingerEvent& f) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    const float x = f.x * w, y = f.y * h;
    auto it = fingers_.find(f.fingerID);
    switch (f.type) {
        case SDL_EVENT_FINGER_DOWN:
            if (it == fingers_.end())
                if (const int id = input_.pointer_down(x, y); id >= 0) fingers_[f.fingerID] = id;
            break;
        case SDL_EVENT_FINGER_MOTION:
            if (it != fingers_.end()) input_.pointer_move(it->second, x, y);
            break;
        case SDL_EVENT_FINGER_UP:
            if (it != fingers_.end()) {
                input_.pointer_move(it->second, x, y);
                input_.pointer_up(it->second);
                fingers_.erase(it);
            }
            break;
        case SDL_EVENT_FINGER_CANCELED:
            if (it != fingers_.end()) {
                // Android cancels the whole gesture.
                input_.cancel_pointers();
                fingers_.clear();
                mouse_pointer_ = -1;
            }
            break;
        default:
            break;
    }
}

void SdlInput::keyboard(const SDL_KeyboardEvent& k) {
    if (k.repeat) return;  // InputDelegator drops repeats
    const int code = android_keycode(k.scancode);
    if (!code) return;
    // KeyEvent.getUnicodeChar(): the character the key produces with the current modifiers.
    int unicode = 0;
    const SDL_Keycode sym = SDL_GetKeyFromScancode(k.scancode, k.mod, false);
    if (!(sym & SDLK_SCANCODE_MASK) && sym >= 0x20 && sym != 0x7f && sym < 0x110000) unicode = int(sym);
    input_.key(k.down, code, unicode, android::kBuiltInKeyboard);
}

void SdlInput::gamepad_button(const SDL_GamepadButtonEvent& b, bool down) {
    // Pressed state comes from the device, so the event's own flag is only used for keys.
    auto it = gamepads_.find(b.which);
    if (it == gamepads_.end()) return;
    switch (b.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        case SDL_GAMEPAD_BUTTON_DPAD_UP: case SDL_GAMEPAD_BUTTON_DPAD_DOWN: {
            SDL_Gamepad* p = it->second.pad;
            auto held = [&](SDL_GamepadButton btn) { return SDL_GetGamepadButton(p, btn) ? 1.0f : 0.0f; };
            it->second.axes[android::kAxisHatX - 1] = held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) - held(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            it->second.axes[android::kAxisHatY - 1] = held(SDL_GAMEPAD_BUTTON_DPAD_DOWN) - held(SDL_GAMEPAD_BUTTON_DPAD_UP);
            input_.gamepad_axes(it->second.axes, int(b.which));
            return;
        }
        default:
            break;
    }
    if (const int code = android_button(b.button)) input_.key(down, code, 0, int(b.which));
}

void SdlInput::gamepad_axis(const SDL_GamepadAxisEvent& a) {
    auto it = gamepads_.find(a.which);
    if (it == gamepads_.end()) return;
    AndroidInput::AxisValues& v = it->second.axes;
    const AndroidInput::AxisValues before = v;
    switch (a.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: v[android::kAxisLeftX - 1] = stick(a.value); break;
        case SDL_GAMEPAD_AXIS_LEFTY: v[android::kAxisLeftY - 1] = stick(a.value); break;
        case SDL_GAMEPAD_AXIS_RIGHTX: v[android::kAxisRightX - 1] = stick(a.value); break;
        case SDL_GAMEPAD_AXIS_RIGHTY: v[android::kAxisRightY - 1] = stick(a.value); break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            // Android reports analog triggers on both the trigger and the brake/gas axes.
            v[android::kAxisLeftTrigger - 1] = v[android::kAxisBrake - 1] = std::max(0.0f, a.value / 32767.0f);
            break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            v[android::kAxisRightTrigger - 1] = v[android::kAxisGas - 1] = std::max(0.0f, a.value / 32767.0f);
            break;
        default:
            return;
    }
    if (v != before) input_.gamepad_axes(v, int(a.which));
}

}  // namespace slingshot
