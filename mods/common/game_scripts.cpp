// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/game_scripts.hpp"

#include <lzma.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace slingshot::mods {
namespace {

using Bytes = std::vector<uint8_t>;

// In libAngryBirdsClassic.so 8.0.3: the script key getter (returns a static
// vector<char>) and the engine's AES cipher object: init(cipher, key vector, 0, 0),
// decrypt(cipher, const vector<char>& in, vector<char>& out), which returns false
// for data that is not its format, and free(cipher).
constexpr uint32_t kScriptKeyGetter = 0x726d8;
constexpr uint32_t kCipherInit = 0x6dac60, kCipherDecrypt = 0x6dacfc, kCipherFree = 0x6daca8;
constexpr uint32_t kCipherSize = 0x200;  // the engine keeps it in a stack slot of at most this size

constexpr uint8_t kLzmaMagic[9] = {0x89, 'L', 'Z', 'M', 'A', '\r', '\n', 0x1a, '\n'};
constexpr std::string_view kScriptDir = "data/scripts/";

std::mutex g_lock;  // patches()
std::map<std::string, std::string>& patches() {  // script -> Lua to run after it
    static std::map<std::string, std::string> p;
    return p;
}

// `data` decrypted by the game's own cipher, nullopt if it is not its format.
std::optional<Bytes> decrypt(const Bytes& data) {
    const uint32_t key = recomp::guest_call(kScriptKeyGetter, {});
    const uint32_t size = uint32_t(data.size());
    const uint32_t cipher = recomp::mem::alloc_zeroed(kCipherSize), vecs = recomp::mem::alloc_zeroed(24),
                   in = recomp::mem::alloc(size ? size : 1);
    if (!key || !cipher || !vecs || !in) {
        recomp::mem::free(cipher), recomp::mem::free(vecs), recomp::mem::free(in);
        return std::nullopt;
    }
    std::memcpy(recomp::g_mem + in, data.data(), size);
    recomp::st32(vecs, in);  // in: begin, end, capacity; out: empty
    recomp::st32(vecs + 4, in + size);
    recomp::st32(vecs + 8, in + size);
    recomp::guest_call(kCipherInit, {cipher, key, 0, 0});
    const bool ok = recomp::guest_call(kCipherDecrypt, {cipher, vecs, vecs + 12}) != 0;
    recomp::guest_call(kCipherFree, {cipher});
    std::optional<Bytes> out;
    const uint32_t begin = recomp::ld32(vecs + 12), end = recomp::ld32(vecs + 16);
    if (ok) out = Bytes(recomp::g_mem + begin, recomp::g_mem + end);
    static const uint32_t operator_delete = recomp::lib_symbol("_ZdlPv");
    if (begin && operator_delete) recomp::guest_call(operator_delete, {begin});
    recomp::mem::free(cipher), recomp::mem::free(vecs), recomp::mem::free(in);
    return out;
}

// Runs a whole buffer through an LZMA coder set up in `s`.
std::optional<Bytes> run_lzma(lzma_stream& s, const uint8_t* data, size_t size, Bytes out) {
    s.next_in = data;
    s.avail_in = size;
    uint8_t buf[65536];
    lzma_ret r = LZMA_OK;
    do {
        s.next_out = buf;
        s.avail_out = sizeof buf;
        r = lzma_code(&s, LZMA_FINISH);
        out.insert(out.end(), buf, buf + (sizeof buf - s.avail_out));
    } while (r == LZMA_OK);
    lzma_end(&s);
    if (r != LZMA_STREAM_END) return std::nullopt;
    return out;
}

// \x89LZMA\r\n\x1a\n + an .lzma ("LZMA alone") stream; nullopt for anything else
// (a few of the game's scripts are 7z archives instead).
std::optional<Bytes> decompress(const Bytes& data) {
    if (data.size() < sizeof kLzmaMagic || std::memcmp(data.data(), kLzmaMagic, sizeof kLzmaMagic)) return std::nullopt;
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_alone_decoder(&s, UINT64_MAX) != LZMA_OK) return std::nullopt;
    return run_lzma(s, data.data() + sizeof kLzmaMagic, data.size() - sizeof kLzmaMagic, {});
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
    // The game's own file, or a plain one (the loader takes both, see
    // runtime/hle/fusion/script_loader.cpp).
    std::optional<Bytes> chunk;
    if (const auto plain = decrypt(*data)) {
        if (plain->size() >= sizeof kLzmaMagic && !std::memcmp(plain->data(), kLzmaMagic, sizeof kLzmaMagic))
            chunk = decompress(*plain);
    } else {
        chunk = data;
    }
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
