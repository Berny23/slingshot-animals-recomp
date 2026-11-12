// SPDX-License-Identifier: GPL-3.0-or-later
// Debug aid: the control command "lua <code>" (platform/sdl/test_script.hpp)
// writes <code> to kConsoleFile in the game's cache directory. The game runs it on
// its next frame with its globals, deletes it and logs "lua: <result>" or
// "lua error: ...". `dump(v, depth)` returns a table as text.
#include <cstdlib>
#include <string>

#include "common/game_scripts.hpp"
#include "lua_console.hpp"

namespace {

std::string console_lua() {
    return std::string(R"(
local path = ')") + slingshot::kConsoleFile + R"('
local G = _G
local function dump(v, depth, indent)
    depth, indent = depth or 1, indent or ''
    if G.type(v) ~= 'table' or depth <= 0 then return G.tostring(v) end
    local keys = {}
    for k in G.pairs(v) do keys[#keys + 1] = k end
    G.table.sort(keys, function(a, b) return G.tostring(a) < G.tostring(b) end)
    local out = {'{'}
    for _, k in G.ipairs(keys) do
        out[#out + 1] = indent .. '  ' .. G.tostring(k) .. ' = ' .. dump(v[k], depth - 1, indent .. '  ')
    end
    out[#out + 1] = indent .. '}'
    return G.table.concat(out, '\n')
end
G.dump = dump
-- The file is `return <number>, function() <code> end`; each number runs once
-- (this Lua has no io library and no os.remove: the host deletes the file).
local env = G.getfenv(1)  -- the game's globals (gamelogic.lua's environment), then _G
local last, last_error
local function poll()
    local chunk, err = G.loadfile(path)
    if not chunk then
        -- No file (most frames), or code that does not compile (reported once).
        if not err:find('cannot open', 1, true) and err ~= last_error then
            native.logError('lua error: ' .. err)
        end
        last_error = err
        return
    end
    last_error = nil
    local ok, number, fn = G.pcall(chunk)
    if not ok then
        last = nil
        native.logError('lua error: ' .. G.tostring(number))
        return
    end
    if number == last then return end
    last = number
    G.setfenv(fn, env)
    local function pack(...) return {n = G.select('#', ...), ...} end
    local results = pack(G.pcall(fn))
    if results[1] then
        local text = {}
        for i = 2, G.math.max(2, results.n) do text[#text + 1] = dump(results[i]) end
        native.logError('lua: ' .. G.table.concat(text, ', '))
    else
        native.logError('lua error: ' .. G.tostring(results[2]))
    end
end
-- Polled from isSafeToQuit, the first thing the game's update (gamelogic.lua)
-- calls on every frame (the engine does not look `update` itself up again).
local is_safe_to_quit = isSafeToQuit
function isSafeToQuit(...)
    local ok, err = G.pcall(poll)
    if not ok then native.logError('lua error: ' .. G.tostring(err)) end
    return is_safe_to_quit(...)
end
)";
}

const bool registered = [] {
    const char* control = std::getenv("SLINGSHOT_CONTROL");
    if (!control) return false;
    slingshot::mods::append_lua("data/scripts/gamelogic.lua", console_lua());
    return true;
}();

}  // namespace
