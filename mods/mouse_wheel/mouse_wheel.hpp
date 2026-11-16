// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel: zooms in levels with an emulated pinch around the cursor, and
// scrolls in menus.
//
// The games of the Classic line still have the PC version's wheel handling:
// MenuManager sends a 'WHEEL' pointer event when cursor.wheelTriggered is set,
// with the notches in cursor.wheel, and ui.ScrollFrame steps through its anchors
// on it. The Android engine never sets them; the script part reads the notches
// from kWheelFile each frame and does. Scrollers without that handling get it
// from the game's file (`extra`). In a level, the script reads kLevelFile
// instead, which tells the host to zoom.
//
// A game's file passes its Lua: WH.in_level() is true while a level is played
// (the wheel zooms); `extra` is appended to scripts with append_lua.
#pragma once

#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "common/game_scripts.hpp"
#include "common/pinch.hpp"
#include "common/script_link.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"
#include "recomp/vfs.hpp"

namespace slingshot::mods::mouse_wheel {

constexpr const char* kWheelFile = "cache/slingshot-wheel.lua";
constexpr const char* kLevelFile = "cache/slingshot-wheel-level.lua";

inline std::string wheel_lua(const std::string& adapter) {
    return std::string(kEachFrameLua) + "local WH = {}\n" + adapter + R"(
local WHEEL_FILE = ')" + kWheelFile + R"('
local LEVEL_FILE = ')" + kLevelFile + R"('
each_frame(function()
    if WH.in_level() then  -- the host zooms
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
        cursor.wheelId = (cursor.wheelId or 0) + 1  -- one id per wheel event
    elseif cursor.wheelTriggered then
        cursor.wheelTriggered, cursor.wheel = false, 0
    end
end)
)";
}

// For ui.ScrollFrame (ScrollFrame2.lua in Classic, Seasons and Star Wars,
// ScrollFrame.lua in Space and Stella): its WHEEL handling steps one anchor from
// the current one, so notches that come while it moves are lost (Stella's adds
// them up, but without their direction). Here the target moves by the notches, once per wheel event
// (MenuManager can deliver the same event to a frame twice). As for a drag, the
// cursor must be inside the frame's clip, where it has one.
constexpr const char* kScrollFrameLua = R"(
local function in_clip(clip, cursor)
    return not clip or (cursor.x >= clip.x and cursor.y >= clip.y
                        and cursor.x < clip.x + clip.w and cursor.y < clip.y + clip.h)
end
local pointer = ui.ScrollFrame.onPointerEvent
function ui.ScrollFrame:onPointerEvent(event, x, y, ...)
    if event ~= 'WHEEL' then return pointer(self, event, x, y, ...) end
    local cursor = gamelua and gamelua.cursor or cursor
    local notches = cursor.wheel or 0
    cursor.wheel = 0  -- the original's next/previous step is replaced below
    local results = {pointer(self, event, x, y, ...)}
    cursor.wheel = notches
    local count = #(self.anchors or {})
    if notches ~= 0 and cursor.wheelId ~= self.wheelId and count > 0 and in_clip(self.clip, cursor) then
        self.wheelId = cursor.wheelId
        local target = (self.target_anchor or self.current_anchor or 1) - notches
        self:setTargetAnchor(math.max(1, math.min(count, target)))
        self.velocity = 8  -- as next/previous
    end
    return unpack(results)
end
)";

// For maps drawn by the game scene (GameScene, gamescene.lua), whose pages the
// PC version's wheel turns with updatePCCameraPanningToDirection: a turn slides
// the camera (gameCamera in Star Wars II, globals before) to the next page, and
// turns asked for while it slides are lost.
// Here they wait for the slide to end. `in_menu` is a Lua expression, true where
// the wheel turns pages. A frame over the map (the shop, a popup) is a shown
// child of the scene that is no button, no map entity (BLOCK_...) and not in
// `map_frames` (a Lua list of names); then the wheel turns nothing.
inline std::string camera_pages_lua(const std::string& in_menu, const std::string& map_frames = "{}") {
    return R"(
local MAX_QUEUED = 3
local MAP_FRAMES = {}
for _, name in ipairs()" + map_frames + R"() do MAP_FRAMES[name] = true end
local function covered(scene)
    for _, child in ipairs(scene.children or {}) do
        local name = tostring(child.name)
        if child.visible ~= false and not child.returnValue and not MAP_FRAMES[name] and not name:find('^BLOCK_') then
            return true
        end
    end
    return false
end
local function in_menu() return )" + in_menu + R"( end
local function sliding()
    local camera = gameCamera or {isSwipingCamera = isSwipingCamera, cameraAnimationSlider = cameraAnimationSlider,
                                  cameraAnimationSliderTarget = cameraAnimationSliderTarget}
    return camera.isSwipingCamera
        or math.abs((camera.cameraAnimationSlider or 0) - (camera.cameraAnimationSliderTarget or 0)) > 0.01
end
local update = GameScene.update
function GameScene:update(...)
    local queued = self.wheelQueue or 0
    if queued ~= 0 and not sliding() then
        if in_menu() then
            updatePCCameraPanningToDirection(queued < 0)
            self.wheelQueue = queued - (queued < 0 and -1 or 1)
        else
            self.wheelQueue = 0
        end
    end
    return update(self, ...)
end
local pointer = GameScene.onPointerEvent
function GameScene:onPointerEvent(event, x, y, ...)
    if event == 'LPRESS' then self.wheelQueue = 0 end  -- a drag takes over
    if event ~= 'WHEEL' or not in_menu() then return pointer(self, event, x, y, ...) end
    local notches = cursor.wheel or 0
    if notches ~= 0 and cursor.wheelId ~= self.wheelId and not covered(self) then
        self.wheelId = cursor.wheelId
        self.wheelQueue = math.max(-MAX_QUEUED, math.min(MAX_QUEUED, (self.wheelQueue or 0) + notches))
    end
    local turn = updatePCCameraPanningToDirection
    updatePCCameraPanningToDirection = function() end  -- the turns are made above
    local results = {pointer(self, event, x, y, ...)}
    updatePCCameraPanningToDirection = turn
    return unpack(results)
end
)";
}

class MouseWheel : public slingshot::Mod {
public:
    // `extra`: {script, lua} pairs for append_lua.
    MouseWheel(const std::string& adapter, const std::vector<std::pair<const char*, const char*>>& extra) {
        slingshot::mods::append_lua(recomp::game.main_script, wheel_lua(adapter));
        for (const auto& [script, lua] : extra) slingshot::mods::append_lua(script, lua);
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
    static constexpr float kNotchFactor = 1.15f;  // finger distance change per wheel notch
    static constexpr uint64_t kIdleNs = 150'000'000;

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

}  // namespace slingshot::mods::mouse_wheel

// A game's mouse-wheel mod: SLINGSHOT_MOUSE_WHEEL_MOD(adapter, {{script, lua}, ...}).
#define SLINGSHOT_MOUSE_WHEEL_MOD(adapter, ...)                                                    \
    namespace {                                                                                    \
    struct GameMouseWheel final : slingshot::mods::mouse_wheel::MouseWheel {                       \
        GameMouseWheel() : MouseWheel(adapter, __VA_ARGS__) {}                                     \
    };                                                                                             \
    }                                                                                              \
    SLINGSHOT_MOD(GameMouseWheel, "mouse-wheel",                                                   \
                  "Mouse wheel: zoom in levels (emulated two-finger pinch), scroll in menus")
