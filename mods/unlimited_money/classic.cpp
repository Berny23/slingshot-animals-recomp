// SPDX-License-Identifier: GPL-3.0-or-later
// Coins and gems (ingameCurrencySystem).
#include "unlimited_money.hpp"

namespace {

constexpr const char* kRefill = R"(
local c = gamelua and gamelua.ingameCurrencySystem
if not c then return end
local items = {}
for _, name in ipairs({'coins', 'gems'}) do
    local v = c:getIGCValue(name) or 0
    if v < MAX then items[name] = MAX - v end
end
if next(items) then
    c:addIGCItems(items, {
        type_of_event = gamelua.subsystems.IngameCurrencySystem.IGC_EVENT_TYPE_REWARD,
        from = 'unlimited money',
    })
end
)";

class UnlimitedMoney final : public slingshot::mods::MoneyRefill {
public:
    UnlimitedMoney() : MoneyRefill(kRefill) {}
};

}  // namespace

SLINGSHOT_MOD_OFF(UnlimitedMoney, "unlimited-money", "Money never runs out (cheat: the save gets no Recomp Achievements)");
