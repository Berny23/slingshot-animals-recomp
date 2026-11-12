// SPDX-License-Identifier: GPL-3.0-or-later
// Guest file system view.
//
// The game believes it runs as an Android app. Its private directories are
// mapped below the host data directory; everything else is invisible:
//
//   /data/data/com.rovio.angrybirds/...   -> <data>/internal/...
//   /sdcard/..., /storage/emulated/0/...  -> <data>/external/...
//   relative paths                        -> <data>/internal/...
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "libc.hpp"
#include "recomp/vfs.hpp"

namespace recomp {
namespace {

std::string g_root;
std::string g_asset_root;

std::string default_root() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return std::string(x) + "/slingshot-animals-recomp";
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share/slingshot-animals-recomp";
    return "./userdata";
}

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

}  // namespace

void vfs_set_root(const std::string& root) { g_root = root; }
void vfs_set_asset_root(const std::string& dir) { g_asset_root = dir; }
const std::string& vfs_asset_root() { return g_asset_root; }

namespace {
std::vector<AssetPatch>& asset_patches() {
    static std::vector<AssetPatch> p;
    return p;
}
}  // namespace
void vfs_add_asset_patch(AssetPatch patch) { asset_patches().push_back(std::move(patch)); }

std::optional<std::vector<uint8_t>> vfs_read_asset(const std::string& name) {
    std::optional<std::vector<uint8_t>> data;
    if (std::FILE* f = std::fopen((g_asset_root + "/" + name).c_str(), "rb")) {
        std::vector<uint8_t> v;
        uint8_t buf[65536];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) v.insert(v.end(), buf, buf + n);
        std::fclose(f);
        data = std::move(v);
    }
    for (const auto& patch : asset_patches())
        if (auto r = patch(name, data)) data = std::move(r);
    return data;
}
namespace {
std::mutex g_virtual_lock;
std::unordered_map<std::string, VirtualFile>& virtual_files() {
    static std::unordered_map<std::string, VirtualFile> files;
    return files;
}
}  // namespace
void vfs_add_virtual_file(const std::string& guest_path, VirtualFile contents) {
    std::lock_guard lk(g_virtual_lock);
    virtual_files()[guest_path] = std::move(contents);
}
std::optional<std::string> vfs_read_virtual_file(const char* guest_path) {
    VirtualFile contents;
    {
        std::lock_guard lk(g_virtual_lock);
        auto it = virtual_files().find(guest_path ? guest_path : "");
        if (it == virtual_files().end()) return std::nullopt;
        contents = it->second;
    }
    return contents();
}

const std::string& vfs_root() {
    static bool created = false;
    if (g_root.empty()) g_root = default_root();
    if (!created) {
        // What Android creates when the app is installed.
        std::error_code ec;
        for (const char* d : {"internal/files", "internal/cache", "internal/shared_prefs", "external"})
            std::filesystem::create_directories(g_root + "/" + d, ec);
        created = true;
    }
    return g_root;
}

std::string vfs_map(const char* guest) {
    namespace fs = std::filesystem;
    std::string_view p = guest ? guest : "";
    struct Mount {
        std::string_view prefix;
        const char* dir;
    };
    static const Mount mounts[] = {
        {"/data/data/com.rovio.angrybirds", "internal"},
        {"/data/user/0/com.rovio.angrybirds", "internal"},
        {"/sdcard", "external"},
        {"/storage/emulated/0", "external"},
        {"/mnt/sdcard", "external"},
    };
    std::string rel;
    const char* dir = nullptr;
    if (!p.empty() && p[0] != '/') {
        dir = "internal";
        rel = std::string(p);
    } else {
        for (const auto& m : mounts)
            if (starts_with(p, m.prefix) && (p.size() == m.prefix.size() || p[m.prefix.size()] == '/')) {
                dir = m.dir;
                rel = std::string(p.substr(m.prefix.size()));
                break;
            }
    }
    if (!dir) return {};  // not visible to the guest
    // Normalise and refuse to escape the mount.
    fs::path r = fs::path(rel).relative_path().lexically_normal();
    if (!r.empty() && *r.begin() == "..") return {};
    fs::path host = fs::path(vfs_root()) / dir / r;
    std::error_code ec;
    fs::create_directories(fs::path(vfs_root()) / dir, ec);
    return host.string();
}

namespace hle {
std::string host_path(const char* guest) { return vfs_map(guest); }
}  // namespace hle

}  // namespace recomp
