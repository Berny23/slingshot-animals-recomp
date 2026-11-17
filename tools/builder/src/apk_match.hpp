// SPDX-License-Identifier: GPL-3.0-or-later
// Which supported game an APK is.
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

#include "catalog.hpp"

namespace builder {

// The game of the APK at `path`; nullopt with a message for the user in `error`.
std::optional<Game> identify_apk(const QString& path, const QVector<Game>& games, QString& error);

// The APK's largest app icon (PNG data), empty if it has none.
QByteArray apk_icon(const QString& path);

}  // namespace builder
