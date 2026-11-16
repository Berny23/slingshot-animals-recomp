// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Rio (see mouse_wheel.hpp). The level selection reads
// cursor.wheelTriggered itself; the episode selection and the credits do only
// in the PC version (isPCVersion), and get the same lines here.
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level()
    return sm ~= nil and sm.scenes ~= nil and sm.currentScene ~= nil and sm.currentScene == sm.scenes.game
end
)";

constexpr const char* kEpisodeSelection = R"(
local update = EpisodeSelectionPage.update
function EpisodeSelectionPage:update(...)
    local cursor = gamelua.cursor
    if cursor.wheelTriggered and not gamelua.isPCVersion() and self.transition_target then
        self.transition_target = math.min(self.max_transition,
            math.max(self.min_transition, self.transition_target - cursor.wheel))
        cursor.wheelTriggered = false
    end
    return update(self, ...)
end
)";

constexpr const char* kCredits = R"(
local touch = CreditsPage.HandleTouch
function CreditsPage:HandleTouch(...)
    local results = {touch(self, ...)}
    local cursor = gamelua.cursor
    if cursor.wheelTriggered and not gamelua.isPCVersion() then
        self.dragSpeed = 20 * cursor.wheel
        cursor.wheelTriggered = false
    end
    return unpack(results)
end
)";

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts/menu/EpisodeSelectionPage.lua", kEpisodeSelection},
                                     {"data/scripts/menu/CreditsPage.lua", kCredits}});
