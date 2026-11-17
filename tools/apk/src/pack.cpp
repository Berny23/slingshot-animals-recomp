// SPDX-License-Identifier: GPL-3.0-or-later
#include "pack.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "zip.hpp"

namespace apk {
namespace fs = std::filesystem;

namespace {

std::optional<std::vector<uint8_t>> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// Formats that do not shrink: stored, which also makes them faster to read.
bool compressed_format(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char* e : {".png", ".jpg", ".ogg", ".mp3", ".m4a", ".zip", ".7z", ".gz", ".mp4"})
        if (ext == e) return true;
    return false;
}

}  // namespace

bool pack(const PackInput& in, const fs::path& out, std::string& error) {
    ZipReader base;
    if (!base.open(in.base)) {
        error = base.error();
        return false;
    }
    ZipWriter z;
    if (!z.open(out)) {
        error = z.error();
        return false;
    }
    auto fail = [&](const std::string& e) {
        error = e;
        return false;
    };
    // Manifest and resources first; resources.arsc stays uncompressed (Android 11+ requires it).
    for (const auto& e : base.entries()) {
        const auto data = base.read(e);
        if (!data) return fail(base.error());
        if (!z.add(e.name, *data, e.name != "resources.arsc" && e.method == 8)) return fail(z.error());
    }
    const auto dex = read_file(in.dex);
    if (!dex || !z.add("classes.dex", *dex, true)) return fail("cannot add " + in.dex.string());
    // Native libraries uncompressed on 16 KiB boundaries: loaded straight from the APK.
    for (const auto& lib : in.libs) {
        const auto data = read_file(lib);
        if (!data || !z.add("lib/" + in.abi + "/" + lib.filename().string(), *data, false, 16384))
            return fail("cannot add " + lib.string());
    }
    std::vector<std::string> files;
    std::error_code ec;
    for (const char* part : {"lib", "assets", "icon.png"}) {
        const fs::path root = in.game / part;
        if (fs::is_regular_file(root, ec)) {
            files.push_back(part);
            continue;
        }
        for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
            if (it->is_regular_file()) files.push_back(fs::relative(it->path(), in.game).generic_string());
    }
    std::sort(files.begin(), files.end());
    std::string list;
    for (const auto& f : files) {
        const auto data = read_file(in.game / f);
        if (!data || !z.add("assets/game/" + f, *data, !compressed_format(f))) return fail("cannot add " + f);
        list += f + "\n";
    }
    if (!z.add("assets/game/files.txt", std::vector<uint8_t>(list.begin(), list.end()), true)) return fail(z.error());
    if (!z.close()) return fail(z.error());
    return true;
}

}  // namespace apk
