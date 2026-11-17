// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-256 (FIPS 180-4).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace apk {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t n);
    std::array<uint8_t, 32> finish();
    // Lowercase hex of finish().
    std::string hex();

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

std::string sha256_hex(const void* data, size_t n);

}  // namespace apk
