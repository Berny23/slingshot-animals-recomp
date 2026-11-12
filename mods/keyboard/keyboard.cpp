// SPDX-License-Identifier: GPL-3.0-or-later
// keyboard: play with the keyboard alone.
//
//   Space (hold)          grab the bird; W A S D or arrows aim (Shift: slowly);
//                         release to shoot. A new grab starts at the last aim.
//   Space                 while a bird flies: its ability (a tap)
//   arrow keys            move the camera (when not aiming)
//   + / -                 zoom in / out (also = and the keypad's + and -)
//   Z                     switch between the close and the far view
//   R                     restart the level (as in Rovio's PC version)
//   P                     pause (the same as Esc, the Android back key)
//   Enter                 confirm a popup (the PC version's key handling); level
//                         end: next level, or retry
//
// The engine passes only BACK and MENU to the scripts, so the host serves the key
// state as a virtual file that the script part reads each frame. It acts through
// the game's input: a touch where a finger would be, the keys the menus check, and
// camera moves within the level's limits.
#include <SDL3/SDL.h>

#include <array>
#include <mutex>
#include <string>

#include "common/game_scripts.hpp"
#include "mod.hpp"
#include "recomp/vfs.hpp"

namespace {

using slingshot::ModContext;

constexpr const char* kKeysFile = "/data/data/com.rovio.angrybirds/cache/slingshot-keys.lua";

// The keys the script part knows, by the name it uses.
enum Key { kSpace, kW, kA, kS, kD, kUp, kDown, kLeft, kRight, kShift, kPlus, kMinus, kZ, kR, kP, kEnter, kKeyCount };
constexpr std::array<const char*, kKeyCount> kNames = {
    "SPACE", "W", "A", "S", "D", "UP", "DOWN", "LEFT", "RIGHT", "SHIFT", "PLUS", "MINUS", "Z", "R", "P", "RETURN",
};

int key_of(const SDL_KeyboardEvent& k) {
    // W A S D by position (any layout), the others by what the key shows.
    switch (k.scancode) {
        case SDL_SCANCODE_W: return kW;
        case SDL_SCANCODE_A: return kA;
        case SDL_SCANCODE_S: return kS;
        case SDL_SCANCODE_D: return kD;
        case SDL_SCANCODE_SPACE: return kSpace;
        case SDL_SCANCODE_UP: return kUp;
        case SDL_SCANCODE_DOWN: return kDown;
        case SDL_SCANCODE_LEFT: return kLeft;
        case SDL_SCANCODE_RIGHT: return kRight;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return kShift;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return kEnter;
        case SDL_SCANCODE_KP_PLUS: return kPlus;
        case SDL_SCANCODE_KP_MINUS: return kMinus;
        default: break;
    }
    switch (k.key) {
        case SDLK_PLUS:
        case SDLK_EQUALS: return kPlus;
        case SDLK_MINUS: return kMinus;
        case SDLK_Z: return kZ;
        case SDLK_R: return kR;
        case SDLK_P: return kP;
        default: return -1;
    }
}

// The script part runs before each update: isSafeToQuit is the first call of
// gamelogic.lua's update (the engine does not look `update` up again).
constexpr const char* kLua = R"(
local KEYS_FILE = '/data/data/com.rovio.angrybirds/cache/slingshot-keys.lua'
local AIM_SPEED = 0.8    -- the rubber band's reach per second
local FINE_AIM_SPEED = 0.1
local PAN_SPEED = 0.8    -- screen widths per second
local ZOOM_SPEED = 0.9   -- natural log of the zoom change per second

local G = _G
local down, presses = {}, {}
local pressed, released = {}, {}
local function read_keys()
    local chunk = G.loadfile(KEYS_FILE)
    local ok, keys = false, nil
    if chunk then ok, keys = G.pcall(chunk) end
    if not ok or G.type(keys) ~= 'table' then keys = {} end
    for name in G.pairs(down) do
        if not keys[name] then keys[name] = {0, presses[name] or 0} end
    end
    for name, v in G.pairs(keys) do
        local was = down[name]
        down[name] = v[1] == 1
        pressed[name] = v[2] ~= (presses[name] or 0)
        released[name] = (was or pressed[name]) and not down[name]
        presses[name] = v[2]
    end
end

-- One frame of a touch, as the engine sets it each frame.
local function touch(x, y, press, hold, release)
    cursor.x, cursor.y = x, y
    keyPressed.LBUTTON = press or false
    keyHold.LBUTTON = hold or false
    keyReleased.LBUTTON = release or false
    touchcount = (press or hold) and 1 or 0
end
-- One frame of a key the game's scripts check (keyPressed/keyHold/keyReleased).
local function game_key(name, key)
    if pressed[key] then keyPressed[name] = true end
    if down[key] then keyHold[name] = true end
    if released[key] then keyReleased[name] = true end
end

local function popup_open()
    for _, child in G.ipairs(notificationsFrame and notificationsFrame.children or {}) do
        if child.visible ~= false and child.name ~= 'IngameCurrencyFrame' and child.name ~= 'loading' then
            return true
        end
    end
    return false
end
local function root_name()
    local root = menuManager and menuManager.menus and menuManager.menus.root
    return root and root.name
end
-- Playing a level, nothing over it.
local function playing()
    return root_name() == 'gameHud' and not skipInput and not levelCompleted and not popup_open()
end

-- Aim: the bird's offset from its rest position in physics units, so camera moves don't change it.
local aim, last_aim, aim_level
local tap_release
local function screen_of(x, y) return physicsToScreenTransform(x, y) end
local function slingshot_system() return SlingshotSystem and SlingshotSystem:instance() end
local function bird_in_slingshot()
    local name = currentBirdName
    local bird = name and objects and objects.world and objects.world[name]
    local slingshot = slingshot_system()
    if not bird or not slingshot or slingshot:isBirdDragged() then return nil end
    if slingshot.isSlingshotAvailable and not slingshot:isSlingshotAvailable() then return nil end
    return bird
end
local function reach()
    local slingshot = slingshot_system()
    return slingshot and slingshot.getRubberBandMaxLength and slingshot:getRubberBandMaxLength() or 5
end
local function axis(neg, neg2, pos, pos2)
    local v = 0
    if down[neg] or down[neg2] then v = v - 1 end
    if down[pos] or down[pos2] then v = v + 1 end
    return v
end

local function start_aim()
    local bird = bird_in_slingshot()
    if not bird then return end
    if aim_level ~= levelName then last_aim, aim_level = nil, levelName end
    aim = {x0 = bird.x, y0 = bird.y, dx = last_aim and last_aim.dx or 0, dy = last_aim and last_aim.dy or 0}
    local x, y = screen_of(bird.x, bird.y)
    touch(x, y, true, true, false)
end
local function update_aim(dt)
    local speed = (down.SHIFT and FINE_AIM_SPEED or AIM_SPEED) * reach() * dt
    aim.dx = aim.dx + axis('A', 'LEFT', 'D', 'RIGHT') * speed
    aim.dy = aim.dy + axis('W', 'UP', 'S', 'DOWN') * speed
    local limit = reach() * 1.2
    local length = G.math.sqrt(aim.dx * aim.dx + aim.dy * aim.dy)
    if length > limit then aim.dx, aim.dy = aim.dx * limit / length, aim.dy * limit / length end
    local x, y = screen_of(aim.x0 + aim.dx, aim.y0 + aim.dy)
    if down.SPACE then
        touch(x, y, false, true, false)
    else
        touch(x, y, false, false, true)
        last_aim, aim = {dx = aim.dx, dy = aim.dy}, nil
    end
end

local function move_camera(dt)
    local camera = cameraSystem
    if not camera then return end
    local dx, dy = axis(nil, 'LEFT', nil, 'RIGHT'), axis(nil, 'UP', nil, 'DOWN')
    local zoom = axis('MINUS', nil, 'PLUS', nil)
    if dx == 0 and dy == 0 and zoom == 0 then return end
    local step = PAN_SPEED * camera:getScreenWidth() * dt
    local values = camera:getClosestValidCameraValues({
        x = camera:getScreenX() + dx * step,
        y = camera:getScreenY() + dy * step,
        zoom = camera:getZoomScale() * G.math.exp(zoom * ZOOM_SPEED * dt),
    })
    camera:gotoTargetPosition(values.x, values.y, values.zoom, 0)  -- (a tween would restart every frame)
end

-- A tap on the shown button that returns `value` (pause menu, level end screen).
local function find_button(frame, value)
    for _, child in G.ipairs(frame and frame.children or {}) do
        if child.visible ~= false then
            if child.returnValue == value and child.enabled ~= false and child.getPositionOnScreen then
                local x, y = child:getPositionOnScreen()
                if x >= 0 and y >= 0 and x < screenWidth and y < screenHeight then return x, y end
            end
            local x, y = find_button(child, value)
            if x then return x, y end
        end
    end
end
local function tap_button(value)
    local x, y = find_button(menuManager.menus.root, value)
    if not x then return false end
    touch(x, y, true, true, false)
    tap_release = {x = x, y = y}
    return true
end
local function on_level_end() return root_name() == 'LevelEndRoot' end

local function frame()
    read_keys()
    local dt = G.math.min(g_dt or 0.016, 0.1)
    if tap_release then
        touch(tap_release.x, tap_release.y, false, false, true)
        tap_release = nil
        return
    end
    if aim then
        if playing() or slingshot_system():isBirdDragged() then return update_aim(dt) end
        aim = nil  -- the level ended or a popup opened: the game cancels the drag
        touch(cursor.x, cursor.y, false, false, true)
        return
    end
    game_key('RETURN', 'RETURN')
    if pressed.P then keyPressed.KEY_BACK, keyHold.KEY_BACK = true, true end
    if released.P then keyReleased.KEY_BACK = true end
    if playing() then
        if pressed.SPACE then
            if flyingBird and birdSpecialtyAvailable then
                touch(screenWidth * 0.5, screenHeight * 0.5, true, true, false)
                tap_release = {x = screenWidth * 0.5, y = screenHeight * 0.5}
                return
            end
            return start_aim()
        end
        if pressed.Z and cameraSystem then cameraSystem:toggleBetweenCloseAndFarZoom() end
        move_camera(dt)
    end
    if pressed.R and (on_level_end() or root_name() == 'gameHud') and not popup_open() then
        -- The pause menu's or level end's button, else the PC version's R (LevelSelectionGameModeHud).
        if not tap_button('RESTART_LEVEL') and playing() then restartLevelIngame(false, 'ingame') end
    elseif pressed.RETURN and on_level_end() then
        if not tap_button('NEXT_LEVEL') then tap_button('RESTART_LEVEL') end
    end
end

local is_safe_to_quit = isSafeToQuit
function isSafeToQuit(...)
    local ok, err = G.pcall(frame)
    if not ok then native.logError('keyboard: ' .. G.tostring(err)) end
    return is_safe_to_quit(...)
end
)";

class Keyboard final : public slingshot::Mod {
public:
    Keyboard() {
        slingshot::mods::append_lua("data/scripts/gamelogic.lua", kLua);
        recomp::vfs_add_virtual_file(kKeysFile, [this] { return contents(); });
    }

    bool on_event(const SDL_Event& e, ModContext&) override {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            if (e.key.repeat) return false;
            const int k = key_of(e.key);
            if (k < 0) return false;
            std::lock_guard lk(mutex_);
            if (e.type == SDL_EVENT_KEY_DOWN && !down_[k]) ++presses_[k];
            down_[k] = e.type == SDL_EVENT_KEY_DOWN;
        } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            std::lock_guard lk(mutex_);
            down_.fill(false);
        }
        return false;  // the keys still reach the game
    }

private:
    // `return {NAME = {down, presses}, ...}` for the keys used so far.
    std::string contents() {
        std::lock_guard lk(mutex_);
        std::string s = "return {";
        for (int k = 0; k < kKeyCount; ++k)
            if (presses_[k]) s += std::string(kNames[k]) + "={" + (down_[k] ? "1," : "0,") + std::to_string(presses_[k]) + "},";
        return s + "}";
    }

    std::mutex mutex_;
    std::array<bool, kKeyCount> down_{};
    std::array<unsigned, kKeyCount> presses_{};
};

}  // namespace

SLINGSHOT_MOD(Keyboard, "keyboard",
              "Play with the keyboard: Space + W A S D to aim and shoot, Space for abilities, arrow keys and +/- "
              "for the camera, R to restart, Enter for the next level");
