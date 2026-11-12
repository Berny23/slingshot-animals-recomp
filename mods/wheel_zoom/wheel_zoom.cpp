// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse wheel: zooms in levels with an emulated pinch around the cursor, and
// scrolls lists in menus.
//
// The scripts still have the PC version's wheel handling: MenuManager sends a
// 'WHEEL' pointer event when cursor.wheelTriggered is set, with the notches in
// cursor.wheel. The Android engine never sets them; the script part reads the
// notches from kWheelFile each frame and does. Reading kLevelFile instead tells
// the host a level is running, so it zooms. SmartScrollFrame moves one anchor per
// event; here it moves by the notches, once per wheel event (MenuManager can
// deliver the same event to a frame twice).
#include <cmath>
#include <mutex>
#include <string>

#include "common/game_scripts.hpp"
#include "common/pinch.hpp"
#include "mod.hpp"
#include "recomp/vfs.hpp"

namespace {

using slingshot::ModContext;

constexpr float kNotchFactor = 1.15f;  // finger distance change per wheel notch
constexpr uint64_t kIdleNs = 150'000'000;
constexpr const char* kWheelFile = "/data/data/com.rovio.angrybirds/cache/slingshot-wheel.lua";
constexpr const char* kLevelFile = "/data/data/com.rovio.angrybirds/cache/slingshot-wheel-level.lua";

// Runs before each update: isSafeToQuit is the first call of gamelogic.lua's update.
constexpr const char* kLua = R"(
local WHEEL_FILE = '/data/data/com.rovio.angrybirds/cache/slingshot-wheel.lua'
local LEVEL_FILE = '/data/data/com.rovio.angrybirds/cache/slingshot-wheel-level.lua'
local G = _G
local function wheel()
    local root = menuManager and menuManager.menus and menuManager.menus.root
    if root and root.name == 'gameHud' then  -- a level: the host zooms
        G.loadfile(LEVEL_FILE)
        if cursor.wheelTriggered then cursor.wheelTriggered, cursor.wheel = false, 0 end
        return
    end
    local chunk = G.loadfile(WHEEL_FILE)
    local ok, notches, x, y = false, 0, nil, nil
    if chunk then ok, notches, x, y = G.pcall(chunk) end
    if ok and G.type(notches) == 'number' and notches ~= 0 then
        cursor.x, cursor.y = x, y
        cursor.wheel, cursor.wheelTriggered = notches, true
        cursor.wheelId = (cursor.wheelId or 0) + 1  -- for kScrollFrame
    elseif cursor.wheelTriggered then
        cursor.wheelTriggered, cursor.wheel = false, 0
    end
end

local is_safe_to_quit = isSafeToQuit
function isSafeToQuit(...)
    local ok, err = G.pcall(wheel)
    if not ok then native.logError('wheel-zoom: ' .. G.tostring(err)) end
    return is_safe_to_quit(...)
end
)";

constexpr const char* kScrollFrame = R"(
local pointer = SmartScrollFrame.onPointerEvent
function SmartScrollFrame:onPointerEvent(event, x, y, ...)
    if event ~= 'WHEEL' then return pointer(self, event, x, y, ...) end
    local notches = gamelua.cursor.wheel or 0
    gamelua.cursor.wheel = 0  -- the original's next/previous step is replaced below
    local results = {pointer(self, event, x, y, ...)}
    gamelua.cursor.wheel = notches
    local id = gamelua.cursor.wheelId
    if notches ~= 0 and id ~= self.wheelId and #(self.anchors or {}) > 0 and self:isWithinFrame(x, y) then
        self.wheelId = id
        self:setTargetAnchor((self.targetAnchor or self.currentAnchor or 1) - notches)
        self.velocity = 8  -- as next/previous
    end
    return unpack(results)
end
)";

class WheelZoom final : public slingshot::Mod {
public:
    WheelZoom() {
        slingshot::mods::append_lua("data/scripts/gamelogic.lua", kLua);
        slingshot::mods::append_lua("data/scripts/ui_components/SmartScrollFrame.lua", kScrollFrame);
        recomp::vfs_add_virtual_file(kWheelFile, [this] { return take_scroll(); });
        recomp::vfs_add_virtual_file(kLevelFile, [this] {
            std::lock_guard lk(mutex_);
            in_level_ = true;
            return std::string();
        });
    }

    bool on_event(const SDL_Event& e, ModContext& ctx) override {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && pinch_.active()) pinch_.release(ctx);  // a click ends the pinch
        if (e.type != SDL_EVENT_MOUSE_WHEEL) return false;
        float notches = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) notches = -notches;
        if (notches == 0) return true;
        const float x = e.wheel.mouse_x * ctx.pixel_density, y = e.wheel.mouse_y * ctx.pixel_density;
        std::lock_guard lk(mutex_);
        if (in_level_) {
            pinch_.zoom(std::pow(kNotchFactor, notches), x, y, ctx.now_ns);
        } else {
            scroll_ += notches;
            x_ = x;
            y_ = y;
        }
        return true;
    }

    void on_frame(ModContext& ctx) override { pinch_.update(ctx, kIdleNs); }

private:
    // `return notches, x, y`: whole notches; fractions of smooth wheels add up.
    std::string take_scroll() {
        std::lock_guard lk(mutex_);
        in_level_ = false;
        const float whole = std::trunc(scroll_);
        scroll_ -= whole;
        return "return " + std::to_string(int(whole)) + ", " + std::to_string(x_) + ", " + std::to_string(y_);
    }

    slingshot::mods::EmulatedPinch pinch_;
    std::mutex mutex_;
    bool in_level_ = false;  // as of the script's last frame
    float scroll_ = 0, x_ = 0, y_ = 0;
};

}  // namespace

SLINGSHOT_MOD(WheelZoom, "wheel-zoom", "Mouse wheel: zoom in levels (emulated two-finger pinch), scroll lists in menus");
