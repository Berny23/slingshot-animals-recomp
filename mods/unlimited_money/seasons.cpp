// SPDX-License-Identifier: GPL-3.0-or-later
// Coins (IngameCurrencyState).
#include "unlimited_money.hpp"

namespace {

constexpr const char* kRefill = R"(
local s = IngameCurrencyState
if not (s and s.currencyAmount and s.virtualCurrencyAmount) then return end
local v = s:getCurrentAmount()
if v < MAX then s:addCurrency(MAX - v) end
)";

class UnlimitedMoney final : public slingshot::mods::MoneyRefill {
public:
    UnlimitedMoney() : MoneyRefill(kRefill) {}
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlimitedMoney, "unlimited-money", "Money never runs out (cheat: the save gets no Recomp Achievements)");
