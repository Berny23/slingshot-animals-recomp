// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace builder {

QVector<Game> load_games(const QString& source) {
    QVector<Game> games;
    const QDir dir(source + "/games");
    for (const QString& id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile f(dir.filePath(id + "/game.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        games.push_back({id, o["title"].toString(), o["version"].toString(), o["package"].toString(),
                         o["library"].toString(), o["sha256"].toString()});
    }
    return games;
}

namespace {

// SLINGSHOT_MOD(Type, "name", "description" "continued") in a source file.
QVector<Mod> registrations(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString text = QString::fromUtf8(f.readAll());
    text.replace(QRegularExpression("\\\\\\s*\\n"), " ");  // macro line continuations
    static const QRegularExpression call(R"re(SLINGSHOT_MOD\(\s*\w+\s*,\s*"([^"]+)"\s*,((?:\s*"(?:[^"\\]|\\.)*")+)\s*\))re");
    static const QRegularExpression literal(R"re("((?:[^"\\]|\\.)*)")re");
    QVector<Mod> mods;
    for (auto it = call.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QString description;
        for (auto lit = literal.globalMatch(m.captured(2)); lit.hasNext();) description += lit.next().captured(1);
        description.replace("\\\"", "\"");
        mods.push_back({m.captured(1), description});
    }
    return mods;
}

}  // namespace

QVector<Mod> load_mods(const QString& source, const QString& id) {
    const QDir mods_dir(source + "/mods");
    QStringList game_ids;
    for (const Game& g : load_games(source)) game_ids << g.id;
    QVector<Mod> mods;
    for (const QString& name : mods_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name == "common") continue;
        const QDir dir(mods_dir.filePath(name));
        bool for_games = false, own = false;
        QStringList files;
        for (const QString& file : dir.entryList({"*.cpp"}, QDir::Files)) {
            const QString stem = QFileInfo(file).completeBaseName();
            if (game_ids.contains(stem)) {
                for_games = true;
                if (stem == id) {
                    own = true;
                    files << dir.filePath(file);
                }
            } else {
                files << dir.filePath(file);
            }
        }
        if (for_games && !own) continue;
        for (const QString& header : dir.entryList({"*.hpp"}, QDir::Files)) files << dir.filePath(header);
        for (const QString& file : files) {
            for (const Mod& m : registrations(file)) {
                if (std::none_of(mods.begin(), mods.end(), [&](const Mod& x) { return x.name == m.name; }))
                    mods.push_back(m);
            }
        }
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.name < b.name; });
    return mods;
}

}  // namespace builder
