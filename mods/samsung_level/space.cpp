// SPDX-License-Identifier: GPL-3.0-or-later
// The eggsteroid level Bonus279 was a gift for Samsung devices: the game opens
// it at start when deviceManufacturer contains "samsung" and leaves it out
// everywhere else (deviceRestrictions; its button in the eggsteroid level
// selection, BLOCK_MENU_SAMSUNG_BUTTON_1, has supportedManufacturer). Here it is
// open on every device and counts like the other eggsteroid levels.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLevelOrder = R"(
local meta = actions.myLevelsMetadata and actions.myLevelsMetadata.Bonus279
if meta then meta.deviceRestrictions = nil end
)";

// The scenes check blocks with a local function of game.lua; the button is made
// available in blockTable before a scene loads.
constexpr const char* kGame = R"(
local load = loadLevelInternal
function loadLevelInternal(...)
    local button = blockTable and blockTable.blocks and blockTable.blocks.BLOCK_MENU_SAMSUNG_BUTTON_1
    if button then button.supportedManufacturer = nil end
    return load(...)
end
)";

// Unlocked as the game does on Samsung devices (storeEggsteroidLevelUnlocked).
constexpr const char* kSettingsWrapper = R"(
local unlocked = SettingsWrapper.isEggsteroidLevelUnlocked
function SettingsWrapper:isEggsteroidLevelUnlocked(level, ...)
    if level == 'Bonus279' and settings and settings.root and settings.root.unlockedEggsteroids then
        settings.root.unlockedEggsteroids.Bonus279 = true
    end
    return unlocked(self, level, ...)
end
)";

class SamsungLevel final : public slingshot::Mod {
public:
    SamsungLevel() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/levelOrder.lua", kLevelOrder);
        append_lua("data/scripts/game.lua", kGame);
        append_lua("data/scripts/settingsWrapper.lua", kSettingsWrapper);
    }
};

}  // namespace

SLINGSHOT_MOD(SamsungLevel, "samsung-level", "Content once given only to Samsung devices is open on every device");
