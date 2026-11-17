// SPDX-License-Identifier: GPL-3.0-or-later
#include "android_input.hpp"

#include <bit>
#include <cstdio>

#include "recomp/game.hpp"
#include "recomp/jni.hpp"
#include "recomp/runtime.hpp"

namespace slingshot {
namespace {

namespace jni = recomp::jni;

uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }

// Keys InputDelegator.a(int) reports as handled. The framework gives the
// others to the next handler, which turns some into fallback keys.
bool consumed(int keycode) {
    using namespace android::key;
    switch (keycode) {
        case kBack: case kMenu: case kButtonA: case kButtonB: case kButtonX: case kButtonY:
        case kButtonThumbL: case kButtonThumbR: case kButtonL1: case kButtonR1:
        case kDpadCenter: case kDpadLeft: case kDpadRight: case kDpadUp: case kDpadDown:
            return true;
        default:
            return false;
    }
}

// Fallback keys of unhandled keys, from AOSP's Generic.kcm.
int fallback(int keycode) {
    using namespace android::key;
    switch (keycode) {
        case kEscape: return kBack;
        case kButtonStart: return kDpadCenter;
        case kButtonSelect: case kButtonMode: return kMenu;
        default: return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------- pointers
// InputDelegator.onTouchEvent reports DOWN/UP for the action pointer only and
// MOVE/CANCEL for every pointer of the MotionEvent.

void AndroidInput::touch_event(int action, float x, float y, int id) {
    x /= screen_scale_;
    y /= screen_scale_;
    queue_.push_back({Event::Touch, action, int(bits(x)), int(bits(y)), id});
}

void AndroidInput::flush_moves() {
    if (!moved_) return;
    moved_ = false;
    for (int id = 0; id < kMaxPointers; ++id)
        if (pointers_[id].active) touch_event(android::kActionMove, pointers_[id].x, pointers_[id].y, id);
}

int AndroidInput::pointer_down(float x, float y) {
    for (int id = 0; id < kMaxPointers; ++id) {
        if (pointers_[id].active) continue;
        flush_moves();
        pointers_[id] = {true, x, y};
        touch_event(android::kActionDown, x, y, id);
        return id;
    }
    return -1;
}

void AndroidInput::pointer_move(int id, float x, float y) {
    if (!pointer_active(id)) return;
    Pointer& p = pointers_[id];
    if (p.x == x && p.y == y) return;
    p.x = x;
    p.y = y;
    moved_ = true;
}

void AndroidInput::pointer_up(int id) {
    if (!pointer_active(id)) return;
    flush_moves();
    pointers_[id].active = false;
    touch_event(android::kActionUp, pointers_[id].x, pointers_[id].y, id);
}

void AndroidInput::cancel_pointers() {
    moved_ = false;
    for (int id = 0; id < kMaxPointers; ++id)
        if (pointers_[id].active) touch_event(android::kActionCancel, pointers_[id].x, pointers_[id].y, id);
    for (Pointer& p : pointers_) p.active = false;
}

bool AndroidInput::pointer_active(int id) const { return id >= 0 && id < kMaxPointers && pointers_[id].active; }

int AndroidInput::active_pointers() const {
    int n = 0;
    for (const Pointer& p : pointers_) n += p.active;
    return n;
}

void AndroidInput::hover(int action, float x, float y) {
    flush_moves();
    touch_event(action, x, y, 0);  // handleEvents passes index 0 for hover events
}

// ---------------------------------------------------------------- keys, axes

void AndroidInput::key(bool down, int keycode, int unicode, int device) {
    queue_.push_back({Event::Key, keycode, down ? 1 : 0, unicode, device});
    if (!consumed(keycode))
        if (const int fb = fallback(keycode)) queue_.push_back({Event::Key, fb, down ? 1 : 0, 0, device});
}

void AndroidInput::gamepad_axes(const AxisValues& values, int device) {
    for (int i = 0; i < android::kAxisCount; ++i)
        queue_.push_back({Event::Axis, i + 1, int(bits(values[i])), device, 0});
}

// ---------------------------------------------------------------- delivery

void AndroidInput::deliver() {
    flush_moves();
    if (queue_.empty()) return;
    static const uint32_t touch = jni::native_method(recomp::game.input_class, "nativeInput");
    static const uint32_t key = jni::native_method(recomp::game.input_class, "nativeKeyInput");
    static const uint32_t axis = jni::native_method(recomp::game.input_class, "nativeInputAxis");
    if (!handler_) handler_ = jni::new_global(jni::new_object(recomp::game.input_class));

    jni::NativeFrame frame;
    const uint32_t env = jni::env();
    for (const Event& e : queue_) {
        switch (e.kind) {
            case Event::Touch:
                if (touch) recomp::guest_call(touch, {env, handler_, uint32_t(e.a), uint32_t(e.b), uint32_t(e.c), uint32_t(e.d)});
                break;
            case Event::Key: {
                // handleEvents passes '\n' for ENTER and '\b' for DEL.
                const int unicode = e.a == android::key::kEnter ? 10 : e.a == android::key::kDel ? 8 : e.c;
                if (key) recomp::guest_call(key, {env, handler_, uint32_t(e.a), uint32_t(e.b), uint32_t(unicode), uint32_t(e.d)});
                break;
            }
            case Event::Axis:
                if (axis) recomp::guest_call(axis, {env, handler_, uint32_t(e.a), uint32_t(e.b), uint32_t(e.c)});
                break;
        }
    }
    if (recomp::trace_enabled("input"))
        for (const Event& e : queue_) {
            if (e.kind == Event::Touch)
                std::fprintf(stderr, "input: touch %d %.1f %.1f id %d\n", e.a, std::bit_cast<float>(e.b),
                             std::bit_cast<float>(e.c), e.d);
            else if (e.kind == Event::Key)
                std::fprintf(stderr, "input: key %d %s unicode %d device %d\n", e.a, e.b ? "down" : "up", e.c, e.d);
            else
                std::fprintf(stderr, "input: axis %d %.3f device %d\n", e.a, std::bit_cast<float>(e.b), e.c);
        }
    queue_.clear();
}

}  // namespace slingshot
