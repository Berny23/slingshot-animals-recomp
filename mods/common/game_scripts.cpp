// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/game_scripts.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "recomp/fusion.hpp"
#include "recomp/game.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace slingshot::mods {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr std::string_view kScriptDir = "data/";

std::mutex g_lock;  // patches()
std::map<std::string, std::string>& patches() {  // script -> Lua to run after it
    static std::map<std::string, std::string> p;
    return p;
}

// A Lua string literal holding `data` as it is.
std::string lua_string(const Bytes& data) {
    std::string out = "\"";
    char esc[5];
    for (uint8_t c : data) {
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
            out += char(c);
        } else {
            std::snprintf(esc, sizeof esc, "\\%03u", c);  // always 3 digits: a digit may follow
            out += esc;
        }
    }
    return out + "\"";
}

std::optional<Bytes> patch_asset(const std::string& name, const std::optional<Bytes>& data) {
    std::string lua;
    {
        std::lock_guard lk(g_lock);
        const auto it = patches().find(name);
        if (it == patches().end()) return std::nullopt;
        lua = it->second;
    }
    if (!data) return std::nullopt;
    // The game's own file, decrypted by the game, or a plain one.
    const std::optional<Bytes> chunk = recomp::hle::is_plain_asset(*data) ? data : recomp::hle::decode_asset(*data);
    if (!chunk) {
        std::fprintf(stderr, "mods: cannot patch %s: cannot read the script\n", name.c_str());
        return std::nullopt;
    }
    // Scripts run in different environments (the globals, or a table of their own
    // with _G and a few more in it, depending on who loads them), so the original
    // chunk is loaded from a string and given this chunk's environment. The mods'
    // code looks names up in that environment first and in _G after it; what it
    // defines goes into the environment.
    const std::string chunk_name = name.substr(std::string_view("data/").size());  // as the engine names it
    const std::string source =
        "local __G = _G\n"
        "local __env = __G.getfenv(1)\n"
        "local __original, __err = __G.loadstring(" + lua_string(*chunk) + ", \"" + chunk_name + "\")\n"
        "if not __original then __G.error(__err, 0) end\n"  // the game's assert() returns nothing
        "__G.setfenv(__original, __env)\n"
        "local __results = {__original(...)}\n"
        "local function __mods()\n" + lua + "end\n"
        "__G.setfenv(__mods, __G.setmetatable({}, {\n"
        "    __index = function(_, k) local v = __env[k] if v == nil then v = __G[k] end return v end,\n"
        "    __newindex = __env,\n"
        "}))\n"
        "__mods()\n"
        "return __G.unpack(__results)\n";
    if (recomp::trace_enabled("mods")) std::fprintf(stderr, "mods: patched %s\n", name.c_str());
    // Plain: the game opens it in its own format (runtime/hle/android/asset_manager.cpp).
    return Bytes(source.begin(), source.end());
}

}  // namespace

void append_lua(const std::string& script, const std::string& lua) {
    if (script.compare(0, kScriptDir.size(), kScriptDir) || script.size() < kScriptDir.size() + 5 ||
        script.compare(script.size() - 4, 4, ".lua")) {
        std::fprintf(stderr, "mods: %s is not a script under %.*s\n", script.c_str(), int(kScriptDir.size()),
                     kScriptDir.data());
        return;
    }
    static std::once_flag registered;
    std::call_once(registered, [] { recomp::vfs_add_asset_patch(patch_asset); });
    std::lock_guard lk(g_lock);
    patches()[script] += "do\n" + lua + "\nend\n";
}

}  // namespace slingshot::mods
