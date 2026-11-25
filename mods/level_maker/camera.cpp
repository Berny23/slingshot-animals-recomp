// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdlib>

#include "qr.hpp"

namespace slingshot::level_maker {

#ifdef __SWITCH__
// The Switch has no camera API (SDL2 under SDL3's names).
QrCamera::~QrCamera() = default;
bool QrCamera::available() { return false; }
void QrCamera::open() { state_ = State::None; }
void QrCamera::close() { state_ = State::Closed; }
void QrCamera::update() {}
#else

namespace {

bool init() {
    static const bool ok = SDL_WasInit(SDL_INIT_CAMERA) || SDL_InitSubSystem(SDL_INIT_CAMERA);
    return ok;
}

// The back camera, else the first.
SDL_CameraID pick() {
    int count = 0;
    SDL_CameraID* ids = init() ? SDL_GetCameras(&count) : nullptr;
    SDL_CameraID id = 0;
    for (int i = 0; i < count && !id; ++i)
        if (SDL_GetCameraPosition(ids[i]) == SDL_CAMERA_POSITION_BACK_FACING) id = ids[i];
    if (!id && count > 0) id = ids[0];
    SDL_free(ids);
    return id;
}

// A picture size near 1280 x 720: enough for a dense QR code, little work a frame.
bool choose(SDL_CameraID id, SDL_CameraSpec& out) {
    int count = 0;
    SDL_CameraSpec** specs = SDL_GetCameraSupportedFormats(id, &count);
    long best = -1;
    for (int i = 0; i < count; ++i) {
        const SDL_CameraSpec& s = *specs[i];
        if (s.width < 320 || s.height < 240) continue;
        const long cost = std::labs(long(s.width) * s.height - 1280L * 720);
        if (best < 0 || cost < best) {
            best = cost;
            out = s;
        }
    }
    SDL_free(specs);
    return best >= 0;
}

}  // namespace

QrCamera::~QrCamera() { close(); }

bool QrCamera::available() {
    if (!init()) return false;
    int count = 0;
    SDL_CameraID* ids = SDL_GetCameras(&count);
    SDL_free(ids);
    return count > 0;
}

void QrCamera::open() {
    close();
    found_.reset();
    const SDL_CameraID id = pick();
    if (!id) {
        state_ = State::None;
        return;
    }
    SDL_CameraSpec spec{};
    const bool chosen = choose(id, spec);
    // RGBA for the preview; SDL converts from what the camera gives.
    spec.format = SDL_PIXELFORMAT_RGBA32;
    camera_ = SDL_OpenCamera(id, chosen ? &spec : nullptr);
    state_ = camera_ ? State::Waiting : State::None;
}

void QrCamera::close() {
    if (search_.valid()) search_.wait();
    search_ = {};
    if (camera_) SDL_CloseCamera(camera_);
    camera_ = nullptr;
    state_ = State::Closed;
    rgba_.clear();
    width_ = height_ = 0;
}

void QrCamera::update() {
    if (!camera_) return;
    const SDL_CameraPermissionState permission = SDL_GetCameraPermissionState(camera_);
    if (permission == SDL_CAMERA_PERMISSION_STATE_DENIED) {
        state_ = State::Denied;
        return;
    }
    if (permission != SDL_CAMERA_PERMISSION_STATE_APPROVED) return;
    state_ = State::Running;

    // the newest frame only
    SDL_Surface* frame = nullptr;
    while (SDL_Surface* next = SDL_AcquireCameraFrame(camera_, nullptr)) {
        if (frame) SDL_ReleaseCameraFrame(camera_, frame);
        frame = next;
    }
    if (frame) {
        SDL_Surface* rgba = frame->format == SDL_PIXELFORMAT_RGBA32 ? frame : SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGBA32);
        if (rgba) {
            width_ = rgba->w;
            height_ = rgba->h;
            rgba_.resize(size_t(width_) * height_ * 4);
            for (int y = 0; y < height_; ++y)
                SDL_memcpy(&rgba_[size_t(y) * width_ * 4], static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * rgba->pitch,
                           size_t(width_) * 4);
            const float degrees = SDL_GetFloatProperty(SDL_GetSurfaceProperties(frame), SDL_PROP_SURFACE_ROTATION_FLOAT, 0);
            turns_ = ((int(std::lround(degrees / 90)) % 4) + 4) % 4;
            ++generation_;
            if (rgba != frame) SDL_DestroySurface(rgba);
        }
        SDL_ReleaseCameraFrame(camera_, frame);
    }

    // the search: one frame at a time on a worker thread
    if (search_.valid() && search_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto text = search_.get();
        if (text) found_ = text;
    }
    if (!found_ && !search_.valid() && !rgba_.empty()) {
        std::vector<uint8_t> grey(size_t(width_) * height_);
        for (size_t i = 0; i < grey.size(); ++i) {
            const uint8_t* p = &rgba_[i * 4];
            grey[i] = uint8_t((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        }
        const int w = width_, h = height_;
        search_ = std::async(std::launch::async, [grey = std::move(grey), w, h] { return read_qr_grey(grey.data(), w, h, w); });
    }
}

#endif

}  // namespace slingshot::level_maker
