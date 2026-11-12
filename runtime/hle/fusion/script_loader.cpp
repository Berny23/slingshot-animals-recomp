// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's asset loaders without the encryption: scripts and configs may be
// plain files (Lua source or bytecode, JSON, zip) as well as the game's own
// encrypted and compressed ones.
//
// The loaders read an asset into a std::vector<char>, decrypt it (AES-256-CBC)
// and usually decompress it (LZMA or 7z); some also check it against a hash list
// first. They are wrapped (abrecomp emit --wrap, addresses in games/<id>/game.json):
// a plain asset is returned as it is, everything else goes through the original
// code, so the game's own files load unchanged. The loaders for files in the
// data directory (downloads, saves) are left alone.
//
//   fusion_load_asset   scripts (loadLuaFile), config.json, ...
//   fusion_load_config  config archives (data/config/*.zip)
//   fusion_load_script  scripts (native.loadLuaScript, i.e. requireFile)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "recomp/cpu.hpp"
#include "recomp/fusion.hpp"
#include "recomp/game.hpp"
#include "recomp/hle.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::hle {
namespace {

uint32_t operator_new() {
    static const uint32_t a = lib_symbol("_Znwj");
    return a;
}

// std::string in guest memory. libstdc++: a pointer to the characters, with the
// length 12 bytes before them. libc++: the short form keeps size << 1 in the first
// byte and the characters inline; the long form is {capacity | 1, size, data}.
std::string read_string(uint32_t s) {
    if (!game.libcxx) {
        const uint32_t chars = ld32(s);
        return std::string(reinterpret_cast<const char*>(g_mem + chars), ld32(chars - 12));
    }
    const uint8_t b = g_mem[s];
    if (!(b & 1)) return std::string(reinterpret_cast<const char*>(g_mem + s + 1), b >> 1);
    return std::string(reinterpret_cast<const char*>(g_mem + ld32(s + 8)), ld32(s + 4));
}

// A new std::string holding `v`: returns its address; free_string releases it
// (but not a COW rep, which copies of the string may still share).
uint32_t make_string(const std::string& v) {
    const uint32_t n = uint32_t(v.size());
    if (!game.libcxx) {  // COW rep: length, capacity, refcount (0: one owner), characters
        const uint32_t rep = mem::alloc(12 + n + 1), s = mem::alloc(4);
        st32(rep, n);
        st32(rep + 4, n);
        st32(rep + 8, 0);
        std::memcpy(g_mem + rep + 12, v.c_str(), n + 1);
        st32(s, rep + 12);
        return s;
    }
    const uint32_t s = mem::alloc(12), data = mem::alloc(n + 1);
    std::memcpy(g_mem + data, v.c_str(), n + 1);
    st32(s, (n + 1) | 1);
    st32(s + 4, n);
    st32(s + 8, data);
    return s;
}
void free_string(uint32_t s) {
    if (game.libcxx) mem::free(ld32(s + 8));
    mem::free(s);
}

// std::vector<char> load(const std::string& name, ...): r0 = result (sret), r1 = name.
void load(CPU* c, const char* hook) {
    const uint32_t out = c->r[0];
    const std::string name = game.asset_prefix + read_string(c->r[1]);
    const auto data = vfs_read_asset(name);
    if (trace_enabled("scripts"))
        std::fprintf(stderr, "scripts: load %s (%s)\n", name.c_str(),
                     !data ? "not an asset" : is_plain_asset(*data) ? "plain" : "encrypted");
    if (!data || !is_plain_asset(*data)) return find_original(hook)(c);
    const uint32_t size = uint32_t(data->size());
    const uint32_t buf = guest_call(c, operator_new(), {size ? size : 1});
    std::memcpy(g_mem + buf, data->data(), size);
    st32(out, buf);
    st32(out + 4, buf + size);
    st32(out + 8, buf + size);
    c->r[0] = out;
}

constexpr char kAsset[] = "fusion_load_asset", kConfig[] = "fusion_load_config", kScript[] = "fusion_load_script";
template <const char* Hook>
void loader(CPU* c) {
    load(c, Hook);
}
RECOMP_REGISTER_IMPORT("hook:fusion_load_asset", loader<kAsset>);
RECOMP_REGISTER_IMPORT("hook:fusion_load_config", loader<kConfig>);
RECOMP_REGISTER_IMPORT("hook:fusion_load_script", loader<kScript>);

bool ends_with(const std::string& s, const char* e) {
    const size_t n = std::strlen(e);
    return s.size() >= n && !s.compare(s.size() - n, n, e);
}

}  // namespace

int dump_encrypted_assets(const std::string& dir) {
    CPU* c = current_cpu();
    const auto list = vfs_read_asset("assets.list");
    const uint32_t operator_delete = lib_symbol("_ZdlPv");
    if (!c || !list || !game.cipher.decrypt || !operator_delete) return -1;
    std::istringstream names(std::string(list->begin(), list->end()));
    int count = 0;
    for (std::string name; std::getline(names, name);) {
        const bool config = name.rfind("data/config/", 0) == 0 && ends_with(name, ".zip");
        if (config ? !game.load_config : !ends_with(name, ".lua") && !ends_with(name, ".json")) continue;
        // The extracted file, without the mods' patches.
        std::ifstream in(std::filesystem::path(vfs_asset_root()) / name, std::ios::binary);
        const std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (data.empty() || is_plain_asset(data)) continue;
        // The game's loaders where wrapped (they also unpack 7z archives), else its
        // cipher; a 7z archive is then written as <name>.7z.
        auto load = [&](uint32_t loader) {
            const std::string_view prefix = game.asset_prefix;
            std::vector<uint8_t> out;
            if (!loader || name.compare(0, prefix.size(), prefix)) return out;
            const uint32_t str = make_string(name.substr(prefix.size())), vec = mem::alloc_zeroed(12);
            if (config) guest_call(c, loader, {vec, str, 1, 1});
            else guest_call(c, loader, {vec, str});
            const uint32_t begin = ld32(vec), end = ld32(vec + 4);
            out.assign(g_mem + begin, g_mem + end);
            if (begin) guest_call(c, operator_delete, {begin});
            mem::free(vec);
            free_string(str);
            return out;
        };
        std::string file = name;
        std::vector<uint8_t> out;
        if (config) {
            out = load(game.load_config);
        } else if (auto decoded = decode_asset(data)) {
            out = std::move(*decoded);
            if (out.size() >= 6 && !std::memcmp(out.data(), "7z\xbc\xaf\x27\x1c", 6)) {
                if (game.load_asset) out = load(game.load_asset);
                else file += ".7z";
            }
        }
        if (out.empty()) continue;
        const std::filesystem::path path = std::filesystem::path(dir) / file;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        ++count;
    }
    return count;
}

}  // namespace recomp::hle
