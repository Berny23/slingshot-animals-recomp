// SPDX-License-Identifier: GPL-3.0-or-later
// Build tools the builder downloads: pinned versions, each archive checked
// against its SHA-256 before it is unpacked into the cache.
#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <atomic>

namespace builder {

struct Download {
    QString name;  // shown while downloading
    QString url;
    QString sha256;
    qint64 size;   // bytes, for progress before the server says
    QString dest;  // below <cache>/tools; the archive's single top folder becomes this
};

enum class Tool { CMake, Ninja, LlvmMingw, Zig, Ndk, BuildTools, Platform, PlatformTools, Jdk };

// The pinned download of a tool for this computer; nullptr if there is none
// (the system's tool is used, e.g. the compiler on macOS).
const Download* download_for(Tool tool);

struct FetchContext {
    std::atomic<bool>* cancel = nullptr;
    std::function<void(qint64 done, qint64 total)> progress;  // bytes of this download
    std::function<void(const QString&)> log;
};

// Downloads, checks and unpacks `d` unless it is in the cache already.
// Returns the unpacked folder, or an empty string with `error` set.
QString fetch(const Download& d, const FetchContext& ctx, QString& error);

// The unpacked folder of `d` if it is already in the cache.
QString cached(const Download& d);

}  // namespace builder
