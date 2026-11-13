// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers specific to the game's engine (see runtime/hle/fusion/).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace recomp::hle {

// Whether an asset is a plain file (Lua source or bytecode, JSON, zip) rather than
// the engine's encrypted format: encrypted files are whole AES blocks whose first
// 256 bytes have control characters other than tab and line breaks (a random file
// has none with a chance of about 1e-12).
bool is_plain_asset(const std::vector<uint8_t>& data);

// An asset of the engine's format, decrypted by the game's cipher and
// decompressed; nullopt if it is not that format. Runs guest code: call it on a
// game thread.
std::optional<std::vector<uint8_t>> decode_asset(const std::vector<uint8_t>& data);

// The script asset `name` in the engine's format, encrypted by the game's cipher;
// nullopt if the game's encryption is unknown. Runs guest code.
std::optional<std::vector<uint8_t>> encode_script(const std::vector<uint8_t>& data, const std::string& name);

// Writes the game's encrypted scripts and JSON files, and with a wrapped config
// loader its configs, decrypted and decompressed below `dir` with their asset
// names (Lua 5.1 bytecode with 4-byte numbers, JSON, zip). Needs the engine set up
// (after nativeConfig). Returns the number of files, or -1 if it cannot run.
int dump_encrypted_assets(const std::string& dir);

}  // namespace recomp::hle
