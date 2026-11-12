// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's asset loaders without the encryption: scripts and configs may be
// plain files (Lua source or bytecode, JSON, zip) as well as the game's own
// encrypted and compressed ones.
//
// The loaders (libAngryBirdsClassic.so 8.0.3) read an asset into a
// std::vector<char>, decrypt it (AES-256-CBC) and usually decompress it (LZMA or
// 7z); some also check it against a hash list first. They are wrapped (abrecomp
// emit --wrap, see game/CMakeLists.txt): a plain asset is returned as it is,
// everything else goes through the original code, so the game's own files load
// exactly as before. The loaders for files in the data directory (downloads,
// saves) are left alone.
//
//   0x1313e4  scripts (loadLuaFile), config.json, ...
//   0x1319b8  config archives (data/config/*.zip)
//   0x5ae4a4  scripts (native.loadLuaScript, i.e. requireFile)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "recomp/cpu.hpp"
#include "recomp/fusion.hpp"
#include "recomp/hle.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::hle {
namespace {

constexpr uint32_t kOperatorNew = 0x859271;     // operator new(size_t), Thumb
constexpr uint32_t kOperatorDelete = 0x857b1d;  // operator delete(void*), Thumb
constexpr uint32_t kStringFromChars = 0x88cee9;  // std::string(const char*, const allocator&), Thumb
constexpr uint32_t kLoadAsset = 0x1313e4, kLoadConfig = 0x1319b8;  // the wrapped loaders

// Encrypted assets are whole AES blocks of random-looking bytes. A plain one is
// Lua bytecode, a zip archive or text: the first 256 bytes of a random file
// contain no control characters other than tab/newline/CR with a chance of
// about 1e-12.
bool is_plain(const std::vector<uint8_t>& d) {
    if (d.size() % 16) return true;
    if (d.size() >= 4 && (!std::memcmp(d.data(), "\x1bLua", 4) || !std::memcmp(d.data(), "PK\x03\x04", 4))) return true;
    for (size_t i = 0; i < d.size() && i < 256; ++i) {
        const uint8_t c = d[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

// std::vector<char> load(const std::string& name, ...): r0 = result (sret), r1 = name.
void load(CPU* c, const char* hook) {
    const uint32_t out = c->r[0];
    const uint32_t chars = ld32(c->r[1]);  // COW std::string: length 12 bytes before the characters
    const std::string name(reinterpret_cast<const char*>(g_mem + chars), ld32(chars - 12));
    const auto data = vfs_read_asset(name);
    if (trace_enabled("scripts"))
        std::fprintf(stderr, "scripts: load %s (%s)\n", name.c_str(),
                     !data ? "not an asset" : is_plain(*data) ? "plain" : "encrypted");
    if (!data || !is_plain(*data)) return find_original(hook)(c);
    const uint32_t size = uint32_t(data->size());
    const uint32_t buf = guest_call(c, kOperatorNew, {size ? size : 1});
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
    if (!c || !list) return -1;
    std::istringstream names(std::string(list->begin(), list->end()));
    int count = 0;
    for (std::string name; std::getline(names, name);) {
        const bool config = name.rfind("data/config/", 0) == 0 && ends_with(name, ".zip");
        if (!config && !ends_with(name, ".lua") && !ends_with(name, ".json")) continue;
        const auto data = vfs_read_asset(name);
        if (!data || is_plain(*data)) continue;
        // The game's own loader decrypts and decompresses it (as the scripts load it).
        const uint32_t chars = mem::alloc(uint32_t(name.size() + 1));
        std::memcpy(g_mem + chars, name.c_str(), name.size() + 1);
        const uint32_t str = mem::alloc(8), vec = mem::alloc(12);
        guest_call(c, kStringFromChars, {str, chars, str + 4});
        if (config) guest_call(c, kLoadConfig, {vec, str, 1, 1});
        else guest_call(c, kLoadAsset, {vec, str});
        const uint32_t begin = ld32(vec), end = ld32(vec + 4);
        const std::filesystem::path out = std::filesystem::path(dir) / name;
        std::filesystem::create_directories(out.parent_path());
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(g_mem + begin), end - begin);
        if (begin) guest_call(c, kOperatorDelete, {begin});
        mem::free(vec);
        mem::free(chars);  // the string itself is left alone (its buffer belongs to the guest)
        ++count;
    }
    return count;
}

}  // namespace recomp::hle
