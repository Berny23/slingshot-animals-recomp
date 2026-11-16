// SPDX-License-Identifier: GPL-3.0-or-later
// The quest "Samsung VIP" (pass a level, 1,000 credits) was offered only on the
// Samsung models of the game's device list (isValidQuestDevice). Here it is
// offered on every device.
//
// The level end counts the passed level in the menu scripts, which have no
// isValidQuestDevice, so it is set there too.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kQuests = R"(
function isValidQuestDevice() return true end
)";

constexpr const char* kLevelEnd = R"(
isValidQuestDevice = gamelua.isValidQuestDevice
)";

class SamsungLevel final : public slingshot::Mod {
public:
    SamsungLevel() {
        using slingshot::mods::append_lua;
        append_lua("data/scripts/QuestsLogic.lua", kQuests);
        append_lua("data/scripts/menus/level_end.lua", kLevelEnd);
    }
};

}  // namespace

SLINGSHOT_MOD(SamsungLevel, "samsung-level", "Content once given only to Samsung devices is open on every device");
