// SPDX-License-Identifier: GPL-3.0-or-later
// Unlimited money: about twice a second, each currency below MAX is filled up to it
// through the game's own function for rewards. The save is marked as cheated
// (common/cheats.hpp), so Recomp Achievements gets no achievements from it.
#pragma once

#include <string>

#include "common/cheats.hpp"
#include "common/game_scripts.hpp"
#include "common/script_link.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"

namespace slingshot::mods {

// `refill` is Lua run in the main script's environment, with MAX set.
class MoneyRefill : public slingshot::Mod {
public:
    explicit MoneyRefill(const char* refill) {
        append_lua(recomp::game.main_script, std::string(kEachFrameLua) + R"(
local MAX = 999999
local function refill()
)" + refill + R"(
end
local ticks = 0
each_frame(function()
    ticks = ticks + 1
    if ticks % 30 == 1 then refill() end
end)
)");
    }
    void on_frame(slingshot::ModContext&) override { mark_save_cheated(); }
};

}  // namespace slingshot::mods
