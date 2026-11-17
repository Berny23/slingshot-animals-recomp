// SPDX-License-Identifier: GPL-3.0-or-later
#include "apk_match.hpp"

#include <QStringList>

#include "android_res.hpp"
#include "extract.hpp"
#include "paths.hpp"
#include "zip.hpp"

namespace builder {

std::optional<Game> identify_apk(const QString& path, const QVector<Game>& games, QString& error) {
    if (path.contains(';')) {
        error = "Please rename the file or its folder: the path must not contain “;”.";
        return std::nullopt;
    }
    apk::ZipReader zip;
    if (!zip.open(to_path(path))) {
        error = "This file is not an APK, or it is damaged.";
        return std::nullopt;
    }
    std::vector<apk::GameSpec> specs;
    for (const Game& g : games) specs.push_back({g.id.toStdString(), g.library.toStdString(), g.sha256.toStdString()});
    const apk::Match match = apk::identify(zip, specs);
    if (match.game) {
        for (const Game& g : games)
            if (g.id.toStdString() == match.game->id) return g;
    }
    if (match.known_library) {
        // The library name tells the game, its hash the version.
        for (const Game& g : games) {
            if (zip.find("lib/armeabi-v7a/lib" + g.library.toStdString() + ".so")) {
                error = "This is " + g.title + ", but another version. Supported is version " + g.version + ".";
                return std::nullopt;
            }
        }
    }
    QStringList supported;
    for (const Game& g : games) supported << "• " + g.title + " " + g.version.section(' ', 0, 0);
    error = "This APK is not a supported game. Supported are:\n" + supported.join("\n");
    return std::nullopt;
}

QByteArray apk_icon(const QString& path) {
    apk::ZipReader zip;
    if (!zip.open(to_path(path))) return {};
    const auto icons = apk::app_icons(zip);
    if (icons.empty()) return {};
    const auto png = zip.read(icons.front().path);
    return png ? QByteArray(reinterpret_cast<const char*>(png->data()), qsizetype(png->size())) : QByteArray();
}

}  // namespace builder
