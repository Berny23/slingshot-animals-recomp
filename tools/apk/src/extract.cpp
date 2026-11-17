// SPDX-License-Identifier: GPL-3.0-or-later
#include "extract.hpp"

#include <fstream>
#include <system_error>

#include "android_res.hpp"
#include "sha256.hpp"
#include "zip.hpp"

namespace apk {
namespace fs = std::filesystem;

std::string library_path(const GameSpec& g) { return "lib/armeabi-v7a/lib" + g.library + ".so"; }

Match identify(ZipReader& apk, const std::vector<GameSpec>& games) {
    Match m;
    for (const auto& g : games) {
        if (!apk.find(library_path(g))) continue;
        const auto lib = apk.read(library_path(g));
        if (!lib) continue;
        m.known_library = true;
        m.sha256 = sha256_hex(lib->data(), lib->size());
        if (m.sha256 == g.sha256) {
            m.game = &g;
            break;
        }
    }
    return m;
}

bool write_file(const fs::path& p, const std::vector<uint8_t>& data) {
    std::error_code ec;
    // An identical file stays untouched (its time too, so builds do not redo what depends on it).
    if (fs::file_size(p, ec) == data.size() && !ec) {
        std::ifstream in(p, std::ios::binary);
        std::vector<uint8_t> old(data.size());
        if (in.read(reinterpret_cast<char*>(old.data()), std::streamsize(old.size())) && old == data) return true;
    }
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

namespace {

// A relative path without "..", "." or absolute parts (no way out of the target).
bool safe_name(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        return false;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = std::min(name.find('/', start), name.size());
        const std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

}  // namespace

bool extract(ZipReader& apk, const GameSpec& game, const fs::path& out, std::string& error,
             const std::function<void(uint64_t, uint64_t)>& progress) {
    std::error_code ec;
    const fs::path lib_out = out / "lib" / ("lib" + game.library + ".so");
    const fs::path stamp = out / ".complete";
    fs::remove(stamp, ec);
    uint64_t total = 0, done = 0;
    for (const auto& e : apk.entries())
        if (e.name.rfind("assets/", 0) == 0) total += e.size;
    for (const auto& e : apk.entries()) {
        if (e.name.rfind("assets/", 0) != 0 || e.name.back() == '/') continue;
        if (!safe_name(e.name)) {
            error = "unsafe file name in the APK: " + e.name;
            return false;
        }
        const auto data = apk.read(e);
        if (!data) {
            error = apk.error();
            return false;
        }
        if (!write_file(out / fs::path(e.name), *data)) {
            error = "cannot write " + (out / fs::path(e.name)).string();
            return false;
        }
        done += e.size;
        if (progress) progress(done, total);
    }
    if (const auto icons = app_icons(apk); !icons.empty()) {
        const auto png = apk.read(icons.front().path);
        if (!png || !write_file(out / "icon.png", *png) || !write_file(out / "icon.ico", png_to_ico(*png))) {
            error = "cannot write " + (out / "icon.png").string();
            return false;
        }
        // Every density for Android's launcher (icons/<dpi>.png).
        for (const auto& i : icons)
            if (const auto d = apk.read(i.path)) write_file(out / "icons" / (std::to_string(i.density ? i.density : 160) + ".png"), *d);
    }
    const auto lib = apk.read(library_path(game));
    if (!lib) {
        error = apk.error();
        return false;
    }
    if (sha256_hex(lib->data(), lib->size()) != game.sha256) {
        error = "unsupported build of lib" + game.library + ".so";
        return false;
    }
    if (!write_file(lib_out, *lib) || !write_file(stamp, {})) {
        error = "cannot write " + lib_out.string();
        return false;
    }
    return true;
}

}  // namespace apk
