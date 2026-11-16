// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: a mod's Lua running every frame and talking to its host part.
//
// kEachFrameLua, put in front of Lua appended to recomp::game.main_script, defines
//   each_frame(fn)        fn runs once per frame, before the game's update. It is
//                         called from update, or from isSafeToQuit where the engine
//                         keeps the original update (Classic's update calls
//                         isSafeToQuit first).
//   send(prefix, text)    a message to the host: on_script_message(prefix) gets text.
//   read(path)            what a virtual file returns (recomp::vfs_add_virtual_file),
//                         or nil.
#pragma once

#include <functional>
#include <string>

namespace slingshot::mods {

constexpr const char* kEachFrameLua = R"(
local G = _G
local function each_frame(fn)
    local env = G.getfenv(2)
    local inside = false
    local function run()
        local ok, err = G.pcall(fn)
        if not ok and G.native and G.native.logError then G.native.logError(G.tostring(err)) end
    end
    local update, safe = env.update, env.isSafeToQuit
    if update then
        env.update = function(...)
            inside = true
            run()
            local results = {G.pcall(update, ...)}
            inside = false
            if not results[1] then G.error(results[2], 0) end
            return G.unpack(results, 2)
        end
    end
    if safe then
        env.isSafeToQuit = function(...)
            if not inside then run() end
            return safe(...)
        end
    end
end
local function send(prefix, text)
    G.loadfile(prefix .. G.tostring(text):gsub('.', function(c) return G.string.format('%02x', c:byte()) end))
end
local function read(path)
    local chunk = G.loadfile(path)
    if not chunk then return nil end
    local ok, value = G.pcall(chunk)
    if ok then return value end
end
)";

// Messages the script sends with send(prefix, text); prefix is a guest path such
// as "cache/slingshot-keyboard/". The handler runs on the game's thread.
void on_script_message(const std::string& prefix, std::function<void(const std::string&)> handler);

}  // namespace slingshot::mods
