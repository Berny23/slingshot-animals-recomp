// SPDX-License-Identifier: GPL-3.0-or-later
// Timed content runs on the device's clock while there is no server time.
//
// getServerTime() and getServerTimeInLocalTimeZone() (game.lua) return nothing
// unless ServerTime has synchronized with Rovio's time server. Without it,
// levels that open on a timer (unlocksOnTimer) stay locked, frozen characters
// never thaw out of their carbonite, and the reward for coming back after a
// while never comes. ServerTime still has the device's time then, in UTC and
// in the local time zone; these return it.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kGame = R"(
-- (ServerTime is registered after this script runs.)
local env = getfenv(1)  -- reads game.lua's globals, writes them
for name, native in pairs({getServerTime = 'getServerTimeInUTC',
                           getServerTimeInLocalTimeZone = 'getServerTimeInLocalTimeZone'}) do
    local original = env[name]
    if original then
        env[name] = function(...)
            local t = original(...)
            local time = ServerTime
            if t == nil and time and time[native] then t = time[native]() end
            return t
        end
    end
end
)";

class OfflineClock final : public slingshot::Mod {
public:
    OfflineClock() { slingshot::mods::append_lua("data/scripts/game.lua", kGame); }
};

}  // namespace

SLINGSHOT_MOD(OfflineClock, "offline-clock",
              "Timed levels, carbonite and comeback rewards run on the device's clock without Rovio's time server");
