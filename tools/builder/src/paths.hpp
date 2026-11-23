// SPDX-License-Identifier: GPL-3.0-or-later
// Where the builder keeps things.
#pragma once

#include <QString>
#include <filesystem>

namespace builder {

// The repository sources the games are built from: source/ in the app
// (next to the executable, or Contents/Resources in the macOS bundle), else
// the checkout the builder was built in.
QString source_dir();

// The prebuilt SDL3 the Linux app carries (platform/linux/sdl3), or empty.
QString sdl3_kit_dir();

// <cache>/source: a copy of source_dir() at a fixed path, which the builds use
// (the app's own folder can move: AppImage mounts, macOS app translocation).
// Only files that differ are rewritten, so builds stay incremental. Returns
// the folder, or an empty string with `error` set.
QString sync_sources(QString& error);

// --data=<dir>: everything below goes into <dir> (cache/, logs/, builder/).
void set_data_root(const QString& dir);
QString data_root();

// Downloaded tools and build directories; "Clear cache" deletes it. Short on
// Windows (path length limit of some tools).
QString cache_dir();

// Where the build log goes: the cache, or <data root>/logs.
QString log_dir();

// The Windows single-file release unpacks the builder into
// %LOCALAPPDATA%\slingshot\app\<version>: that folder when this builder runs
// from it, else empty. Clear cache deletes it too.
QString unpacked_app_dir();
// Deletes the other unpacked versions now and this one once the app has ended
// (Windows keeps running programs' files).
void remove_unpacked_app();
// Call when the app ends: starts the deletion remove_unpacked_app() asked for.
void finish_unpacked_app_removal();

// Kept when the cache is cleared: the key signing Android packages (an
// installed game updates only with the same key).
QString data_dir();

// Bytes used by a directory tree.
qint64 tree_size(const QString& dir);

// A path for std::filesystem (UTF-16 on Windows, UTF-8 elsewhere).
inline std::filesystem::path to_path(const QString& s) {
#ifdef _WIN32
    return std::filesystem::path(s.toStdWString());
#else
    return std::filesystem::path(s.toStdString());
#endif
}

// "1.4 GiB"
QString format_size(qint64 bytes);

}  // namespace builder
