// SPDX-License-Identifier: GPL-3.0-or-later
// Debug aid: the control command "lua <code>" (platform/sdl/test_script.hpp)
// writes <code> to kConsoleFile in the game's cache directory. The game runs it on
// its next frame with its globals, deletes it and logs "lua: <result>" or
// "lua error: ...". `dump(v, depth)` returns a table as text. The script logs by
// opening kLogPrefix + the text in hex, which the host prints (release builds of
// the engine have an empty print).
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "common/game_scripts.hpp"
#include "lua_console.hpp"
#include "recomp/game.hpp"
#include "recomp/vfs.hpp"

namespace {

constexpr std::string_view kLogPrefix = "cache/slingshot-console-log/";

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
local function log(text)
    G.loadfile(')" + std::string(kLogPrefix) + R"(' .. text:gsub('.', function(c) return G.string.format('%02x', c:byte()) end))
end
-- The file is `return <number>, function() <code> end`; each number runs once
-- (this Lua has no io library and no os.remove: the host deletes the file).
local env = G.getfenv(1)  -- the game's globals (gamelogic.lua's environment), then _G
local last, last_error
local function poll()
    local chunk, err = G.loadfile(path)
    if not chunk then
        -- No file (most frames), or code that does not compile (reported once).
        if not err:find('cannot open', 1, true) and err ~= last_error then
            log('lua error: ' .. err)
        end
        last_error = err
        return
    end
    last_error = nil
    local ok, number, fn = G.pcall(chunk)
    if not ok then
        last = nil
        log('lua error: ' .. G.tostring(number))
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
        log('lua: ' .. G.table.concat(text, ', '))
    else
        log('lua error: ' .. G.tostring(results[2]))
    end
end
local function safe_poll()
    local ok, err = G.pcall(poll)
    if not ok then log('lua error: ' .. G.tostring(err)) end
end
-- Polled from update and from isSafeToQuit, which Classic's update (gamelogic.lua)
-- calls first on every frame (that engine does not look `update` itself up again).
for _, name in G.ipairs({'update', 'isSafeToQuit'}) do
    local original = env[name]
    if original then
        env[name] = function(...)
            safe_poll()
            return original(...)
        end
    end
end
)";
}

const bool registered = [] {
    const char* control = std::getenv("SLINGSHOT_CONTROL");
    if (!control) return false;
    recomp::vfs_add_virtual_fallback([](const std::string& path) -> std::optional<std::string> {
        if (path.compare(0, kLogPrefix.size(), kLogPrefix)) return std::nullopt;
        std::string text;
        for (size_t i = kLogPrefix.size(); i + 1 < path.size(); i += 2)
            text += char(std::strtol(path.substr(i, 2).c_str(), nullptr, 16));
        std::fprintf(stderr, "%s\n", text.c_str());
        return std::string();
    });
    slingshot::mods::append_lua(recomp::game.main_script, console_lua());
    return true;
}();

}  // namespace
