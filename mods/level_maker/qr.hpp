// SPDX-License-Identifier: GPL-3.0-or-later
// QR codes of share codes: modules to draw, PNG files, and reading them from pictures.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace slingshot::level_maker {

// The modules of a QR code for text, row by row (1 = dark); size 0 when it does not fit.
std::vector<uint8_t> qr_modules(const std::string& text, int& size);
// The QR code as a PNG file.
bool save_qr_png(const std::string& text, const std::string& path);
// Text of the first QR code found in a picture file (PNG, JPEG, BMP, GIF).
std::optional<std::string> read_qr_picture(const std::string& path);

}  // namespace slingshot::level_maker
