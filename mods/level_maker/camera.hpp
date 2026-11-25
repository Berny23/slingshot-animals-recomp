// SPDX-License-Identifier: GPL-3.0-or-later
// A camera that reads QR codes: SDL's camera (on Android its back camera), its newest
// frame kept as RGBA for the preview, frames searched for a QR code on a worker thread.
#pragma once

#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

struct SDL_Camera;

namespace slingshot::level_maker {

class QrCamera {
public:
    enum class State { Closed, None, Waiting, Denied, Running };

    ~QrCamera();
    // Whether the device has a camera at all.
    static bool available();
    // Opens the camera (the permission prompt, where there is one, comes up).
    void open();
    void close();
    // Each frame: takes the newest picture and the result of the search.
    void update();
    State state() const { return state_; }
    // The text of a QR code once one was read (then the camera stops searching).
    std::optional<std::string> found() const { return found_; }

    // The preview: RGBA pixels, its size, quarter turns clockwise to stand upright, and
    // a number that changes with each new picture.
    const std::vector<uint8_t>& pixels() const { return rgba_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int turns() const { return turns_; }
    uint64_t generation() const { return generation_; }

private:
    SDL_Camera* camera_ = nullptr;
    State state_ = State::Closed;
    std::vector<uint8_t> rgba_;
    int width_ = 0, height_ = 0, turns_ = 0;
    uint64_t generation_ = 0;
    std::future<std::optional<std::string>> search_;
    std::optional<std::string> found_;
};

}  // namespace slingshot::level_maker
