// SPDX-License-Identifier: GPL-3.0-or-later
// Stella: fixes for window resizes, always on (not a mod).
//
// On a new screen size the game lays out its frames and recalculates the
// cameras (screenDimensionsChanges), but the island map's level limits, which
// decide its zoom, are set only when the map opens (IslandMap:onEntry). The map
// then keeps the zoom of the old size: on a narrower window it is cut off at
// the sides. They are set again here, as onEntry does.
#include "common/game_scripts.hpp"
#include "recomp/game.hpp"

namespace {

constexpr const char* kResizeLua = R"(
local changed = screenDimensionsChanges
if changed then
    function screenDimensionsChanges(...)
        local results = {changed(...)}
        local root = menuManager and menuManager.currentRoot
        local map = root and root:getChild('IslandMap')
        if map and map.entered and gameCamera and recalculateLevelLimits then
            recalculateLevelLimits(gameCamera:getEdgeCameras())
            gameCamera:updateActiveCameras()
        end
        return unpack(results)
    end
end
)";

const bool registered = [] {
    slingshot::mods::append_lua(recomp::game.main_script, kResizeLua);
    return true;
}();

}  // namespace
