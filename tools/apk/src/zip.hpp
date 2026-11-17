// SPDX-License-Identifier: GPL-3.0-or-later
// Zip archives (APKs): reading stored and deflated entries, and writing them
// with the alignment Android expects.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace apk {

struct ZipEntry {
    std::string name;
    uint16_t method = 0;  // 0 stored, 8 deflated
    uint32_t crc = 0;
    uint64_t compressed = 0, size = 0;
    uint64_t header_offset = 0;  // local file header
    uint16_t unix_mode = 0;      // st_mode if made on Unix, else 0
};

class ZipReader {
public:
    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    // False with error() set if the file is not a readable zip archive.
    bool open(const std::filesystem::path& path);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    const ZipEntry* find(const std::string& name) const;
    // The entry's uncompressed contents; nullopt (and error()) if it is damaged.
    std::optional<std::vector<uint8_t>> read(const ZipEntry& e);
    std::optional<std::vector<uint8_t>> read(const std::string& name);
    const std::string& error() const { return error_; }

private:
    std::FILE* f_ = nullptr;
    std::vector<ZipEntry> entries_;
    std::string error_;
};

class ZipWriter {
public:
    ~ZipWriter();
    bool open(const std::filesystem::path& path);
    // Adds an entry; stored entries start at a multiple of `align` bytes
    // (4 for assets, 4096 for native libraries, as zipalign does).
    bool add(const std::string& name, const std::vector<uint8_t>& data, bool deflate, uint32_t align = 4);
    bool close();  // writes the central directory
    const std::string& error() const { return error_; }

private:
    std::FILE* f_ = nullptr;
    std::vector<ZipEntry> entries_;
    std::string error_;
};

uint32_t crc32(const uint8_t* data, size_t n);

// Unpacks every entry below `dir` with Unix modes and symbolic links (tool
// archives). Names leaving `dir` are refused. `progress` gets (entries done, total).
bool unpack(ZipReader& zip, const std::filesystem::path& dir, std::string& error,
            const std::function<void(size_t, size_t)>& progress = {});

}  // namespace apk
