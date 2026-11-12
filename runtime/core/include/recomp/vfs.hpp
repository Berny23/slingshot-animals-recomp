// SPDX-License-Identifier: GPL-3.0-or-later
// Guest file system mapping (see runtime/hle/libc/vfs.cpp).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace recomp {

// Host directory that holds the guest's private data (saves, settings).
void vfs_set_root(const std::string& root);
const std::string& vfs_root();
// Host path for a guest path, or an empty string if the path is not visible.
std::string vfs_map(const char* guest_path);

// Host directory with the APK's assets/ contents (read through AAssetManager).
void vfs_set_asset_root(const std::string& dir);
const std::string& vfs_asset_root();

// Asset patches: called in registration order when the game opens an asset, with
// its name and current contents (the APK's file, or what an earlier patch made of
// it; nullopt when the APK has no such asset). A patch may return replacement bytes.
using AssetPatch = std::function<std::optional<std::vector<uint8_t>>(const std::string& name,
                                                                     const std::optional<std::vector<uint8_t>>& data)>;
void vfs_add_asset_patch(AssetPatch patch);
// The asset as the game sees it, patches applied.
std::optional<std::vector<uint8_t>> vfs_read_asset(const std::string& name);

// Virtual files: read-only guest files that exist only in memory. Each time the
// game opens one for reading (fopen), `contents` makes what it reads. A way for
// the host to pass state to game scripts, which read it with loadfile.
using VirtualFile = std::function<std::string()>;
void vfs_add_virtual_file(const std::string& guest_path, VirtualFile contents);
// Stand-ins for missing files: when the game opens a visible guest path for reading
// and no file is there, each fallback in registration order may make what it reads.
using VirtualFallback = std::function<std::optional<std::string>(const std::string& guest_path)>;
void vfs_add_virtual_fallback(VirtualFallback fallback);
// The contents of the virtual file or fallback for `guest_path`, or nullopt if there is none.
std::optional<std::string> vfs_read_virtual_file(const char* guest_path);

}  // namespace recomp
