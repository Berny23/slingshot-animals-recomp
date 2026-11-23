// SPDX-License-Identifier: GPL-3.0-or-later
// Coins (Bank).
#include "unlimited_money.hpp"

namespace {

constexpr const char* kRefill = R"(
if not Bank or not Consumables then return end
local currency = Consumables.currencyId
local v = Bank.getAmount(currency) or 0
if v < MAX then Bank.deposit(currency, MAX - v) end
)";

class UnlimitedMoney final : public slingshot::mods::MoneyRefill {
public:
    UnlimitedMoney() : MoneyRefill(kRefill) {}
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlimitedMoney, "unlimited-money", "Money never runs out (cheat: the save gets no Recomp Achievements)");
