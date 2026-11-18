// SPDX-License-Identifier: GPL-3.0-or-later
// The Java input path of the original app, re-implemented on the host:
//
//   MySurfaceView.onTouchEvent/onHoverEvent ─┐
//   App.onKeyDown/onKeyUp/onGenericMotionEvent ─> InputDelegator ─> MyInputEvent queue
//   NativeApplication.doUpdate ─> MyInputHandler.handleEvents ─> native methods
//
// Platform code reports what an Android device would report (pointers with
// MotionEvent semantics, Android keycodes, gamepad axes). This class applies
// the Java side's conversions and queues the events; deliver() hands them to
// the game right before nativeUpdate, as handleEvents() does.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace slingshot {

namespace android {

// MotionEvent actions as nativeInput receives them.
enum Action : int {
    kActionDown = 0,  // ACTION_DOWN and ACTION_POINTER_DOWN
    kActionUp = 1,    // ACTION_UP and ACTION_POINTER_UP
    kActionMove = 2,
    kActionCancel = 3,
    kActionHoverMove = 7,
    kActionHoverEnter = 9,
    kActionHoverExit = 10,
};

// android.view.KeyEvent keycodes used by the platform layers.
namespace key {
constexpr int kBack = 4, kDpadUp = 19, kDpadDown = 20, kDpadLeft = 21, kDpadRight = 22, kDpadCenter = 23;
constexpr int kEnter = 66, kDel = 67, kMenu = 82, kEscape = 111;
constexpr int kButtonA = 96, kButtonB = 97, kButtonX = 99, kButtonY = 100, kButtonL1 = 102, kButtonR1 = 103;
constexpr int kButtonL2 = 104, kButtonR2 = 105, kButtonThumbL = 106, kButtonThumbR = 107;
constexpr int kButtonStart = 108, kButtonSelect = 109, kButtonMode = 110;
}  // namespace key

// Gamepad axes as InputDelegator.onGenericMotionEvent numbers them (1-based).
enum GamepadAxis : int {
    kAxisLeftX = 1, kAxisLeftY, kAxisRightX, kAxisRightY,  // AXIS_X, AXIS_Y, AXIS_Z, AXIS_RZ
    kAxisLeftTrigger, kAxisRightTrigger,                    // AXIS_LTRIGGER, AXIS_RTRIGGER
    kAxisBrake, kAxisGas,                                    // AXIS_BRAKE, AXIS_GAS
    kAxisHatX, kAxisHatY,                                    // AXIS_HAT_X, AXIS_HAT_Y
    kAxisCount = 10,
};

// KeyCharacterMap.BUILT_IN_KEYBOARD
constexpr int kBuiltInKeyboard = 0;

}  // namespace android

class AndroidInput {
public:
    static constexpr int kMaxPointers = 10;

    // Where the game's screen is on the surface (pixels, y from the top) and surface
    // pixels per game pixel (recomp::gles::set_screen_area): touch and hover
    // positions are clamped to the area and mapped into it on the way to the game.
    // Width 0: the whole surface.
    void set_screen_area(float x, float y, float width, float height, float scale) {
        area_ = {x, y, width, height};
        screen_scale_ = scale > 0 ? scale : 1;
    }

    // ---- touch screen (MotionEvent). Positions are surface pixels.
    // A new pointer gets the lowest free id, as on Android; -1 if all are taken.
    int pointer_down(float x, float y);
    void pointer_move(int id, float x, float y);
    void pointer_up(int id);
    void cancel_pointers();  // ACTION_CANCEL, e.g. when the window loses focus
    bool pointer_active(int id) const;
    int active_pointers() const;

    // ---- hovering mouse (MotionEvent from SOURCE_MOUSE without buttons)
    void hover(int action, float x, float y);

    // ---- keys (KeyEvent). Key repeats are not reported by the app.
    void key(bool down, int keycode, int unicode, int device);

    // ---- joystick / gamepad (one generic MotionEvent: all axes at once).
    // Values are -1..1 (sticks, hats) or 0..1 (triggers), indexed by GamepadAxis - 1.
    using AxisValues = std::array<float, android::kAxisCount>;
    void gamepad_axes(const AxisValues& values, int device);

    // MyInputHandler.handleEvents(): calls the game's native input methods.
    void deliver();

private:
    struct Event {
        enum Kind { Touch, Key, Axis } kind;
        int a, b, c, d;  // Touch: action, x, y (float bits), id. Key: keycode, down, unicode, device. Axis: axis, value bits, device
    };
    struct Pointer {
        bool active = false;
        float x = 0, y = 0;
    };

    void flush_moves();
    void touch_event(int action, float x, float y, int id);

    std::array<Pointer, kMaxPointers> pointers_{};
    bool moved_ = false;  // a MOVE is pending (Android batches moves per frame)
    struct {
        float x, y, w, h;
    } area_{};
    float screen_scale_ = 1;
    std::vector<Event> queue_;
    uint32_t handler_ = 0;  // the MyInputHandler object
};

}  // namespace slingshot
