// SPDX-License-Identifier: GPL-3.0-or-later
// Debug aid: a script of input events and screenshots, run at given frames.
// Events are pushed into SDL's queue, so they take the same path as real
// input (mods included). Enable with SLINGSHOT_SCRIPT=<file>.
//
// One command per line, "<frame> <command> [args]"; '#' starts a comment.
// Positions are window coordinates.
//
// Live control: with SLINGSHOT_CONTROL=<fifo> (created if missing) the game
// also takes commands while it runs, written to that named pipe, e.g.
// `echo "shot /tmp/a.bmp" > fifo`. A line "<command> [args]" runs on the next
// frame, "+<n> <command> [args]" n frames later (so "+0 mouse down 10 10",
// "+3 mouse up 10 10" is a tap). Writers may come and go.
//   shot <file.bmp>                  save the frame before it is presented
//   mouse move|down|up <x> <y>       left mouse button
//   wheel <x> <y> <notches>          mouse wheel (positive: away from the user)
//   finger down|move|up <n> <x> <y>  touch screen finger n
//   key down|up <scancode name>      e.g. "Escape", "Return", "A"
//   pad attach                       plug in a virtual gamepad
//   pad down|up <button>             SDL gamepad button name, e.g. "a", "dpup", "start"
//   pad axis <axis> <value>          SDL gamepad axis name, value -1..1 (triggers 0..1)
//   window minimize|restore          as reported by the window manager
//   mark <label>                     print the time (for latency measurements)
//   sync <label>                     print "host: sync <label> frame <n>" (all earlier commands have run)
//   lua <code>                       (control pipe only) run Lua code in the game, see
//                                    mods/common/lua_console.cpp; the result is logged
//   quit
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace slingshot {

class TestScript {
public:
    explicit TestScript(SDL_Window* window);
    ~TestScript();

    bool active() const { return !commands_.empty() || live_; }
    // Pushes the input events of this frame. Returns false on "quit".
    bool run_input(long frame);
    // Saves the screenshots of this frame (call before presenting it).
    void run_shots(long frame);

private:
    struct Command {
        long frame;
        std::vector<std::string> args;
        std::string text;  // "lua": the code
    };
    void push_mouse(const std::string& what, float x, float y);
    void push_finger(const std::string& what, int n, float x, float y);
    void push_key(bool down, const std::string& name);
    void pad(const std::vector<std::string>& a);
    void read_control(std::string path);  // thread: the lines of SLINGSHOT_CONTROL into pending_

    SDL_Window* window_;
    std::vector<Command> commands_;
    bool live_ = false;
    std::mutex pending_mutex_;
    std::vector<Command> pending_;  // from the control pipe; frame = offset from now
    int console_number_ = 0;        // "lua": numbers the code (each runs once)
    long console_remove_frame_ = 0;
    SDL_JoystickID pad_id_ = 0;
    SDL_Joystick* pad_ = nullptr;
};

void save_screenshot(SDL_Window* window, const std::string& path);

}  // namespace slingshot
