// SPDX-License-Identifier: GPL-3.0-or-later
#include "extract.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
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

namespace {

// Where the content starts in `path`: the position of its first 16000hz-16bit-mono/
// or images/android/, or npos.
size_t assets_service_root(const std::string& path) {
    for (size_t start = 0; start < path.size();) {
        if (path.compare(start, 19, "16000hz-16bit-mono/") == 0 || path.compare(start, 15, "images/android/") == 0)
            return start;
        const size_t slash = path.find('/', start);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return std::string::npos;
}

}  // namespace

bool extract_assets_service(const fs::path& from, const fs::path& out, std::string& error) {
    std::error_code ec;
    fs::remove_all(out, ec);
    // Files below the content's root, as (path, read).
    std::vector<std::pair<std::string, std::function<std::optional<std::vector<uint8_t>>()>>> files;
    if (fs::is_directory(from, ec)) {
        // Names from the folder's parent on: the folder may be 16000hz-16bit-mono/ itself.
        fs::path dir = fs::absolute(from, ec).lexically_normal();
        if (!dir.has_filename()) dir = dir.parent_path();
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path file = it->path();
            files.emplace_back(file.lexically_relative(dir.parent_path()).generic_string(),
                               [file]() -> std::optional<std::vector<uint8_t>> {
                                   std::ifstream in(file, std::ios::binary);
                                   std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                                             std::istreambuf_iterator<char>());
                                   if (in.bad()) return std::nullopt;
                                   return data;
                               });
        }
        if (ec) {
            error = "cannot read " + from.string() + ": " + ec.message();
            return false;
        }
    }
    ZipReader zip;
    if (!fs::is_directory(from, ec)) {
        if (!zip.open(from)) {
            error = zip.error();
            return false;
        }
        for (const auto& e : zip.entries())
            if (e.name.back() != '/') files.emplace_back(e.name, [&zip, &e] { return zip.read(e); });
    }
    std::string root;
    bool found = false;
    for (const auto& [path, read] : files)
        if (const size_t at = assets_service_root(path); at != std::string::npos) {
            root = path.substr(0, at);
            found = true;
            break;
        }
    if (!found) {
        error = "no 16000hz-16bit-mono/ or images/android/ in " + from.string();
        return false;
    }
    for (const auto& [path, read] : files) {
        if (path.compare(0, root.size(), root) != 0) continue;
        const std::string name = path.substr(root.size());
        // The downloaded archives themselves: the game reads only what it unpacked.
        if (name.find('/') == std::string::npos && name.size() > 3 && !name.compare(name.size() - 3, 3, ".7z")) continue;
        if (!safe_name(name)) {
            error = "unsafe file name: " + path;
            return false;
        }
        const auto data = read();
        if (!data) {
            error = "cannot read " + path + (zip.error().empty() ? "" : ": " + zip.error());
            return false;
        }
        if (!write_file(out / fs::path(name), *data)) {
            error = "cannot write " + (out / fs::path(name)).string();
            return false;
        }
    }
    return true;
}

}  // namespace apk
