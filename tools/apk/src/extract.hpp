// SPDX-License-Identifier: GPL-3.0-or-later
// What the build takes from a game's APK: the armeabi-v7a library (input of
// the recompiler), assets/ (read at run time) and the app icon.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace apk {

class ZipReader;

struct GameSpec {
    std::string id;       // games/<id>
    std::string library;  // lib<library>.so
    std::string sha256;   // of the supported build's library
};

struct Match {
    const GameSpec* game = nullptr;  // nullptr: no supported game
    bool known_library = false;      // a game's library, but another build (sha256 differs)
    std::string sha256;              // of the APK's library
};

std::string library_path(const GameSpec& g);  // lib/armeabi-v7a/lib<library>.so in the APK
Match identify(ZipReader& apk, const std::vector<GameSpec>& games);

// Writes <out>/assets/..., <out>/icon.png (the largest icon), icon.ico,
// icons/<dpi>.png (each density),
// <out>/lib/lib<library>.so and, last, the mark of a complete extraction
// <out>/.complete. Files already there with the same contents are kept as
// they are. `progress` gets (bytes done, bytes total). False with `error` set
// on failure.
bool extract(ZipReader& apk, const GameSpec& game, const std::filesystem::path& out, std::string& error,
             const std::function<void(uint64_t, uint64_t)>& progress = {});

bool write_file(const std::filesystem::path& p, const std::vector<uint8_t>& data);

// Star Wars II's downloaded content (files/assets_service/ of an installation)
// from a zip archive or a folder into `out`, which is emptied first: everything
// in the folder that holds 16000hz-16bit-mono/ or images/android/, at any depth
// (the archive or folder may hold files/, assets_service/ or its contents), but
// the downloaded .7z archives. False
// with `error` set if there is no such folder.
bool extract_assets_service(const std::filesystem::path& from, const std::filesystem::path& out, std::string& error);

}  // namespace apk
