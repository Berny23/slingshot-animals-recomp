// SPDX-License-Identifier: GPL-3.0-or-later
// The app icon of an APK: AndroidManifest.xml (binary XML) names the icon
// resource, resources.arsc maps it to one file per screen density.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apk {

class ZipReader;

struct IconFile {
    std::string path;  // in the APK, e.g. res/drawable-xxxhdpi-v4/icon.png
    uint16_t density;  // dpi (0 = default, 160 dpi)
    uint32_t width, height;
};

// Every PNG file of the manifest's application icon, the largest first.
std::vector<IconFile> app_icons(ZipReader& apk);

// A Windows icon (.ico) holding the PNG as is.
std::vector<uint8_t> png_to_ico(const std::vector<uint8_t>& png);

// Width and height of PNG data, nullopt if it is not a PNG.
std::optional<std::pair<uint32_t, uint32_t>> png_size(const std::vector<uint8_t>& data);

}  // namespace apk
