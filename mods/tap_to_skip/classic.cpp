// SPDX-License-Identifier: GPL-3.0-or-later
// tap-to-skip: a tap skips the wait at the end of a level.
//
// The game's own skip button shows only after 5 s (levelFailSkipButtonCountDown).
// A tap on no HUD button does what it does: a won level adds the remaining birds'
// bonus at once (skipToLevelEnd); with the birds used up the level ends (endLevel,
// after the extra-bird offer). Then a tap only counts AFTER_LAST_BIRD seconds after
// the last shot, so the last bird's ability still works.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local AFTER_LAST_BIRD = 1.5  -- seconds

local function can_skip(hud)
    local g = gamelua
    if g.g_skip_to_level_end or g.skipInput then return false end
    if g.flyingBird and g.birdSpecialtyAvailable then return false end  -- that tap is the ability
    if g.levelCompleted then return true end
    if hud.show_skip_button then return true end
    local waited = hud.levelFailSkipButtonCountDown
    return waited ~= nil and waited >= AFTER_LAST_BIRD
end

-- What the skip button does (GameHud:onPointerEvent, SKIP_TO_LEVEL_END).
local function skip(hud)
    local g = gamelua
    if not g.isLevelSucceeding() and not g.isLeagueMode() and not g.g_extra_bird_shown then
        g.g_extra_bird_shown = true
        league.displayModernPopup(ui.ExtraBirdIngamePopup:new())
        g.levelCompleteTimer = 0.01
    elseif g.isLevelSucceeding() then
        g.skipToLevelEnd()
    else
        g.endLevel()
    end
    local button = hud:getChild('skip_button')
    if button then button.state = 'CLICKED' end
end

local pointer = GameHud.onPointerEvent
function GameHud:onPointerEvent(event, x, y, ...)
    local results = {pointer(self, event, x, y, ...)}
    if event == 'LRELEASE' and results[1] == nil and not (self:getChild('pausePage') or {}).visible
        and can_skip(self) then
        skip(self)
    end
    return unpack(results)
end
)";

class TapToSkip final : public slingshot::Mod {
public:
    TapToSkip() { slingshot::mods::append_lua("data/scripts/menus/GameHud.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(TapToSkip, "tap-to-skip", "Tap anywhere to skip the wait at the end of a level");
