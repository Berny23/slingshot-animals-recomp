// SPDX-License-Identifier: GPL-3.0-or-later
// offline-assets: starts without Rovio's asset service, with the downloadable content
// (sounds, theme images) copied into the game's data folder or built into the game,
// or with stand-ins for it.
//
// The game downloads the archives 16000hz-16bit-mono.7z and images.7z into
// files/assets_service/, unpacks them there and stores a hash per archive in its
// settings (assetData). Offline it starts only with both hashes stored, else it
// shows "downloading the assets failed" and quits; a missing file it then opens
// ends the game. Here both archives count as installed. A file that is not in the
// data folder comes from the game's assets/assets_service/ (added by the build,
// GAME_EXTRA_starwars2), else it is a stand-in: a sound is silence, a theme sheet
// has every sprite of the original in its size, drawn as a smooth grid of the
// original's colours (starwars2_sprites.hpp).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "common/game_scripts.hpp"
#include "mod.hpp"
#include "offline_assets/starwars2_sprites.hpp"
#include "recomp/game.hpp"
#include "recomp/vfs.hpp"

namespace {

using offline_assets::Sheet;
using offline_assets::Sprite;

// Asset keys of the two archives (rovioid/Assets.lua).
constexpr const char* kSettings = R"(
local installed = {sounds_1_8_0 = true, images = true}
local get = SettingsWrapper.getAssetHash
function SettingsWrapper:getAssetHash(key, ...)
    local hash = get(self, key, ...)
    if hash == nil and installed[key] then return 'local' end
    return hash
end
)";

constexpr const char* kAssets = "files/assets_service/";
constexpr const char* kSounds = "16000hz-16bit-mono/";
constexpr const char* kImages = "images/android/";

// Half a second of silence: MPEG-2 Layer III frames, 16 kHz mono, 8 kbit/s, no audio data.
std::string silence() {
    std::string frame(36, '\0');
    const unsigned char header[] = {0xff, 0xf3, 0x18, 0xc0};
    std::memcpy(frame.data(), header, sizeof header);
    std::string out;
    for (int i = 0; i < 14; ++i) out += frame;
    return out;
}

void put16(std::string& s, unsigned v) {
    s += char(v >> 8);
    s += char(v);
}
void put32be(std::string& s, uint32_t v) {
    put16(s, v >> 16);
    put16(s, v & 0xffff);
}
void put32le(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += char(v >> (8 * i));
}

struct Placed {
    unsigned x, y;
};

// Sprites in shelves of a texture at least 1024 pixels wide, 1 pixel apart.
std::vector<Placed> pack(std::span<const Sprite> sprites, unsigned& width, unsigned& height) {
    width = 1024;
    for (const Sprite& s : sprites) width = std::max(width, unsigned(s.width) + 2);
    std::vector<size_t> order(sprites.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return sprites[a].height > sprites[b].height; });
    std::vector<Placed> placed(sprites.size());
    unsigned x = 1, y = 1, shelf = 0;
    for (size_t i : order) {
        const Sprite& s = sprites[i];
        if (x + s.width + 1 > width) {
            x = 1;
            y += shelf + 1;
            shelf = 0;
        }
        placed[i] = {x, y};
        x += s.width + 1u;
        shelf = std::max(shelf, unsigned(s.height));
    }
    height = y + shelf + 1;
    return placed;
}

// The sheet's description (KA3D SPRT: image, then name, rectangle and pivot per sprite).
std::string description(const Sheet& sheet) {
    unsigned width, height;
    const std::vector<Placed> placed = pack(sheet.sprites, width, height);
    const std::string image = std::string(sheet.name) + ".pvr";
    std::string body;
    put16(body, 1);
    put16(body, unsigned(image.size()));
    body += image;
    put16(body, unsigned(sheet.sprites.size()));
    for (size_t i = 0; i < sheet.sprites.size(); ++i) {
        const Sprite& s = sheet.sprites[i];
        put16(body, unsigned(std::strlen(s.name)));
        body += s.name;
        for (unsigned v : {placed[i].x, placed[i].y, unsigned(s.width), unsigned(s.height)}) put16(body, v);
        put16(body, uint16_t(s.pivot_x));
        put16(body, uint16_t(s.pivot_y));
    }
    std::string sprt = "SPRT";
    put32be(sprt, uint32_t(body.size()));
    sprt += body;
    std::string out = "KA3D";
    put32be(out, uint32_t(sprt.size()));
    return out + sprt;
}

// A point's colour, premultiplied.
void premultiplied(uint32_t point, double out[4]) {
    const double a = (point >> 24) / 255.0;
    for (int c = 0; c < 3; ++c) out[c] = ((point >> (16 - 8 * c)) & 0xff) * a;
    out[3] = a * 255;
}

// Position of a pixel's centre on a grid of n points from edge to edge: the first
// point of its interval and the fraction to the next.
std::pair<unsigned, double> on_grid(unsigned pixel, unsigned size, unsigned n) {
    const double at = std::clamp((pixel + 0.5) * (n - 1) / size, 0.0, double(n - 1));
    const unsigned first = std::min(unsigned(at), n - 2);
    return {first, at - first};
}

// The sheet's texture: PVR v2, RGBA8888 with premultiplied alpha like the originals.
// A sprite is its grid blended between the points; a block has a darker rim.
std::string texture(const Sheet& sheet) {
    unsigned width, height;
    const std::vector<Placed> placed = pack(sheet.sprites, width, height);
    std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
    for (size_t i = 0; i < sheet.sprites.size(); ++i) {
        const Sprite& s = sheet.sprites[i];
        if (!s.columns) continue;
        std::vector<std::array<double, 4>> points(size_t(s.columns) * s.rows);
        for (size_t k = 0; k < points.size(); ++k) premultiplied(sheet.points[s.first + k], points[k].data());
        const bool rim = s.block && s.width > 6 && s.height > 6;
        for (unsigned y = 0; y < s.height; ++y) {
            const auto [row, fy] = on_grid(y, s.height, s.rows);
            for (unsigned x = 0; x < s.width; ++x) {
                const auto [column, fx] = on_grid(x, s.width, s.columns);
                const auto& p00 = points[row * s.columns + column];
                const auto& p01 = points[row * s.columns + column + 1];
                const auto& p10 = points[(row + 1) * s.columns + column];
                const auto& p11 = points[(row + 1) * s.columns + column + 1];
                const double inside = std::min({x + 0.5, s.width - x - 0.5, y + 0.5, s.height - y - 0.5});
                const double shade = rim && inside < 3 ? 0.6 : 1.0;
                uint8_t* p = &pixels[(size_t(placed[i].y + y) * width + placed[i].x + x) * 4];
                for (int c = 0; c < 4; ++c) {
                    const double top = p00[c] * (1 - fx) + p01[c] * fx;
                    const double bottom = p10[c] * (1 - fx) + p11[c] * fx;
                    const double value = (top * (1 - fy) + bottom * fy) * (c < 3 ? shade : 1.0);
                    p[c] = uint8_t(std::lround(std::clamp(value, 0.0, 255.0)));
                }
            }
        }
    }
    std::string out;
    for (uint32_t v : {52u, height, width, 0u, 0x8012u, uint32_t(pixels.size()), 32u, 0xffu, 0xff00u, 0xff0000u,
                       0xff000000u, 0x21525650u, 1u})
        put32le(out, v);
    out.append(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    return out;
}

bool ends_with(const std::string& s, const char* end) {
    const size_t n = std::strlen(end);
    return s.size() >= n && !s.compare(s.size() - n, n, end);
}

std::optional<std::string> stand_in(const std::string& path) {
    const size_t at = path.find(kAssets);
    if (at == std::string::npos) return std::nullopt;
    const std::string name = path.substr(at + std::strlen(kAssets));
    if (const auto bundled = recomp::vfs_read_asset("assets_service/" + name))
        return std::string(bundled->begin(), bundled->end());
    if (!name.compare(0, std::strlen(kSounds), kSounds) && ends_with(name, ".mp3")) {
        static const std::string quiet = silence();
        return quiet;
    }
    if (name.compare(0, std::strlen(kImages), kImages)) return std::nullopt;
    const std::string file = name.substr(std::strlen(kImages));
    for (const Sheet& sheet : offline_assets::kSheets) {
        const std::string base = sheet.name;
        const bool dat = file == base + ".dat", pvr = file == base + ".pvr";
        if (!dat && !pvr) continue;
        static std::mutex lock;
        static std::map<std::string, std::string> made;
        std::lock_guard lk(lock);
        auto it = made.find(file);
        if (it == made.end()) it = made.emplace(file, dat ? description(sheet) : texture(sheet)).first;
        return it->second;
    }
    return std::nullopt;
}

class OfflineAssets final : public slingshot::Mod {
public:
    OfflineAssets() {
        slingshot::mods::append_lua("data/scripts/settingsWrapper.lua", kSettings);
        recomp::vfs_add_virtual_fallback(stand_in);
    }
};

}  // namespace

SLINGSHOT_MOD(OfflineAssets, "offline-assets",
              "Starts without the asset service: with the downloaded content in the data folder, or stand-ins");
