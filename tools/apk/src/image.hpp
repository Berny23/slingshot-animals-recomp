// SPDX-License-Identifier: GPL-3.0-or-later
// PNG decoding and baseline JPEG encoding, enough for app icons.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace apk {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // 8 bits per channel, not premultiplied
};

// Non-interlaced PNG of any colour type and bit depth.
std::optional<Image> decode_png(const std::vector<uint8_t>& data);

// `size` x `size`, over a white background (no alpha in JPEG).
Image icon_square(const Image& src, int size);

// Baseline JPEG, 4:4:4, of the image's RGB.
std::vector<uint8_t> encode_jpeg(const Image& img, int quality);

}  // namespace apk
