// SPDX-License-Identifier: GPL-3.0-or-later
// Coins (Coins, the game's consumable).
#include "unlimited_money.hpp"

namespace {

constexpr const char* kRefill = R"(
if not Coins then return end
local v = Coins:getAmount() or 0
if v < MAX then Coins:gained(MAX - v, nil, 'Reward') end
)";

class UnlimitedMoney final : public slingshot::mods::MoneyRefill {
public:
    UnlimitedMoney() : MoneyRefill(kRefill) {}
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlimitedMoney, "unlimited-money", "Money never runs out (cheat: the save gets no Recomp Achievements)");
