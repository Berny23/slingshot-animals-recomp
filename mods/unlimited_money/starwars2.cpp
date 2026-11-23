// SPDX-License-Identifier: GPL-3.0-or-later
// Credits (the coins of SettingsWrapper).
#include "unlimited_money.hpp"

namespace {

constexpr const char* kRefill = R"(
if not SettingsWrapper or not addCoins then return end
local v = SettingsWrapper:getAmount('coins') or 0
if v < MAX then addCoins(MAX - v, true) end
)";

class UnlimitedMoney final : public slingshot::mods::MoneyRefill {
public:
    UnlimitedMoney() : MoneyRefill(kRefill) {}
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlimitedMoney, "unlimited-money", "Money never runs out (cheat: the save gets no Recomp Achievements)");
