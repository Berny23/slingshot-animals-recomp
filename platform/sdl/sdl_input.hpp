// SPDX-License-Identifier: GPL-3.0-or-later
// Translates SDL input into what an Android device reports to the app:
//   mouse      -> touch pointer while the left button is held, hover otherwise
//                 (Android delivers mouse clicks as touches); the "back" side
//                 button is KEYCODE_BACK
//   touch      -> touch pointers
//   keyboard   -> KeyEvents with Android keycodes
//   gamepads   -> Android gamepad keycodes and joystick axes
#pragma once

#include <SDL3/SDL.h>

#include <unordered_map>

#include "android_input.hpp"

namespace slingshot {

class SdlInput {
public:
    SdlInput(SDL_Window* window, AndroidInput& input) : window_(window), input_(input) {}
    ~SdlInput();

    void handle(const SDL_Event& e);

private:
    struct Gamepad {
        SDL_Gamepad* pad = nullptr;
        AndroidInput::AxisValues axes{};
    };

    float density() const;
    void mouse_button(const SDL_MouseButtonEvent& b, bool down);
    void finger(const SDL_TouchFingerEvent& f);
    void keyboard(const SDL_KeyboardEvent& k);
    void gamepad_button(const SDL_GamepadButtonEvent& b, bool down);
    void gamepad_axis(const SDL_GamepadAxisEvent& a);

    SDL_Window* window_;
    AndroidInput& input_;
    int mouse_pointer_ = -1;  // touch pointer of the left button
    bool hovering_ = false;
    std::unordered_map<SDL_FingerID, int> fingers_;  // SDL finger -> pointer id
    std::unordered_map<SDL_JoystickID, Gamepad> gamepads_;
};

}  // namespace slingshot
