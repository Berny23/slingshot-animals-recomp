// SPDX-License-Identifier: GPL-3.0-or-later
// The game an executable is built for, from games/<id>/game.json
// (generated game_info.cpp, see games/CMakeLists.txt).
#pragma once

#include <cstdint>
#include <string>

namespace recomp {

// The engine's AES cipher object: init(cipher, key vector, 0, 0), free(cipher),
// encrypt/decrypt(cipher, const vector<char>& in, vector<char>& out); decrypt
// returns false for data that is not its format. 0 if unknown.
struct GameCipher {
    uint32_t init, free, encrypt, decrypt;
};

struct GameInfo {
    const char* id;          // games/<id>
    const char* title;
    const char* version;     // the APK's versionName
    const char* package;     // Android package name
    const char* library;     // lib<library>.so
    const char* game_dir;    // directory of the extracted APK when there is no game/ next to the executable ("" if none)
    uint32_t script_key;     // script key getter (returns a static vector<char>), 0 if unknown
    GameCipher cipher;
    uint32_t load_asset;     // asset loader wrapped as fusion_load_asset, 0 if none
    uint32_t load_config;    // config archive loader wrapped as fusion_load_config, 0 if none
    const char* asset_prefix;  // what the loaders leave out of asset names ("data/" or "")
    bool libcxx;             // C++ library: LLVM libc++ (else GNU libstdc++)
    bool scripts_7z;         // scripts are 7z archives (else \x89LZMA streams)
    const char* main_script;  // asset of the script run with the game's globals (gamelogic.lua)
    const char* app_class;    // Java class with the lifecycle natives (nativeInit, nativeUpdate, ...)
    const char* input_class;  // Java class with the input natives (nativeInput, nativeKeyInput, ...)
    const char* default_mods;  // mods enabled without --mod or SLINGSHOT_MODS: "name,name", "all" or "default"
};

extern const GameInfo game;

// /data/data/<package>
inline std::string game_data_dir() { return std::string("/data/data/") + game.package; }

}  // namespace recomp
