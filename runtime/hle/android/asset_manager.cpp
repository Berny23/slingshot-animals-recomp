// SPDX-License-Identifier: GPL-3.0-or-later
// <android/asset_manager.h>: assets come from the extracted APK assets/ directory.
//
// An asset is read into guest memory when it is opened, which makes
// AAsset_getBuffer trivial; the game's assets are small enough for that.
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp/hle.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::hle {
namespace {

using u32 = uint32_t;

struct Asset {
    u32 data = 0;
    u32 size = 0;
    u32 pos = 0;
};
std::mutex g_lock;
std::unordered_map<u32, Asset> g_assets;  // keyed by the guest handle

u32 manager() {
    static u32 m = mem::static_alloc(16, 4);  // opaque, never dereferenced by the game
    return m;
}

bool safe_name(const std::string& n) {
    return !n.empty() && n[0] != '/' && n.find("..") == std::string::npos;
}

u32 h_AAssetManager_fromJava(u32, u32) { return manager(); }

u32 h_AAssetManager_open(u32, u32 name, int) {
    const std::string n = mem::str(name);
    if (!safe_name(n)) return 0;
    const auto bytes = vfs_read_asset(n);
    if (!bytes) {
        if (trace_enabled("assets")) std::fprintf(stderr, "assets: missing %s\n", n.c_str());
        return 0;
    }
    Asset a;
    a.size = u32(bytes->size());
    a.data = mem::alloc(a.size ? a.size : 1, 16);
    if (!a.data) return 0;
    std::memcpy(g_mem + a.data, bytes->data(), a.size);
    if (trace_enabled("assets")) std::fprintf(stderr, "assets: open %s (%u bytes)\n", n.c_str(), a.size);
    const u32 h = mem::alloc(16, 4);
    std::lock_guard lk(g_lock);
    g_assets[h] = a;
    return h;
}

Asset* find(u32 h) {
    auto it = g_assets.find(h);
    return it == g_assets.end() ? nullptr : &it->second;
}

void h_AAsset_close(u32 h) {
    std::lock_guard lk(g_lock);
    if (Asset* a = find(h)) {
        mem::free(a->data);
        g_assets.erase(h);
        mem::free(h);
    }
}
u32 h_AAsset_getBuffer(u32 h) {
    std::lock_guard lk(g_lock);
    Asset* a = find(h);
    return a ? a->data : 0;
}
int64_t h_AAsset_getLength64(u32 h) {
    std::lock_guard lk(g_lock);
    Asset* a = find(h);
    return a ? a->size : 0;
}
int32_t h_AAsset_getLength(u32 h) { return int32_t(h_AAsset_getLength64(h)); }
int64_t h_AAsset_getRemainingLength64(u32 h) {
    std::lock_guard lk(g_lock);
    Asset* a = find(h);
    return a ? a->size - a->pos : 0;
}
int h_AAsset_read(u32 h, u32 buf, u32 n) {
    std::lock_guard lk(g_lock);
    Asset* a = find(h);
    if (!a) return -1;
    if (n > a->size - a->pos) n = a->size - a->pos;
    std::memcpy(g_mem + buf, g_mem + a->data + a->pos, n);
    a->pos += n;
    return int(n);
}
int64_t seek(u32 h, int64_t off, int whence) {
    std::lock_guard lk(g_lock);
    Asset* a = find(h);
    if (!a) return -1;
    const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? a->pos : a->size;
    const int64_t p = base + off;
    if (p < 0 || p > a->size) return -1;
    a->pos = u32(p);
    return p;
}
int32_t h_AAsset_seek(u32 h, int32_t off, int whence) { return int32_t(seek(h, off, whence)); }
int64_t h_AAsset_seek64(u32 h, int64_t off, int whence) { return seek(h, off, whence); }

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(AAssetManager_fromJava); REG(AAssetManager_open); REG(AAsset_close); REG(AAsset_getBuffer);
REG(AAsset_getLength64); REG(AAsset_getLength); REG(AAsset_getRemainingLength64); REG(AAsset_read);
REG(AAsset_seek); REG(AAsset_seek64);

}  // namespace
}  // namespace recomp::hle
