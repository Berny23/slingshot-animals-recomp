// SPDX-License-Identifier: GPL-3.0-or-later
// The supported games (games/*/game.json) and their mods (mods/), read from
// the sources the builder ships.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace builder {

struct Game {
    QString id, title, version, package, library, sha256;
};

struct Mod {
    QString name, description;
    bool on_by_default = true;  // SLINGSHOT_MOD, not SLINGSHOT_MOD_OFF
};

QVector<Game> load_games(const QString& source);

// The mods built into game `id` (the rules of mods/CMakeLists.txt).
QVector<Mod> load_mods(const QString& source, const QString& id);

}  // namespace builder
