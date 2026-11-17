// SPDX-License-Identifier: GPL-3.0-or-later
#include "kconfig.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTextStream>
#include <map>
#include <mutex>

#include "breeze_logging.h"
#include "kcolorscheme_debug.h"

Q_LOGGING_CATEGORY(BREEZE, "breeze", QtWarningMsg)
Q_LOGGING_CATEGORY(KCOLORSCHEME, "kcolorscheme", QtWarningMsg)

KConfig::KConfig(const QString& file, OpenFlags flags, QStandardPaths::StandardLocation type)
    : name_(file), flags_(flags), type_(type) {
    reparseConfiguration();
}

void KConfig::reparseConfiguration() {
    groups_.clear();
    const QString file = name_.isEmpty() ? QStringLiteral("kdeglobals") : name_;
    QStringList paths;
    if (file.startsWith(QLatin1Char(':')) || QFileInfo(file).isAbsolute()) {
        paths << file;
    } else {
        if ((flags_ & IncludeGlobals) && file != QLatin1String("kdeglobals")) {
            const QStringList globals = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
            for (auto it = globals.rbegin(); it != globals.rend(); ++it) paths << *it;
        }
        const QStringList found = QStandardPaths::locateAll(type_, file);
        if (flags_ & CascadeConfig) {
            for (auto it = found.rbegin(); it != found.rend(); ++it) paths << *it;  // the user's file last: it wins
        } else if (!found.isEmpty()) {
            paths << found.first();
        }
    }
    for (const QString& p : paths) parse(p);
}

void KConfig::parse(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    QString group = QStringLiteral("<default>");
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('['))) {
            // [Group][Subgroup]
            QStringList parts;
            int pos = 0;
            while (pos < line.size() && line[pos] == QLatin1Char('[')) {
                const int end = line.indexOf(QLatin1Char(']'), pos);
                if (end < 0) break;
                parts << line.mid(pos + 1, end - pos - 1);
                pos = end + 1;
            }
            group = parts.join(QChar(0x1d));
            groups_[group];
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString key = line.left(eq).trimmed();
        if (const int loc = key.indexOf(QLatin1Char('[')); loc >= 0) {
            if (!key.endsWith(QLatin1String("[$e]"))) continue;  // localised values are not used
            key = key.left(loc);
        }
        groups_[group][key] = line.mid(eq + 1).trimmed();
    }
}

KConfigGroup KConfig::group(const QString& name) const { return KConfigGroup(this, name); }

KSharedConfig::Ptr KSharedConfig::openConfig(const QString& file, OpenFlags flags, QStandardPaths::StandardLocation type) {
    static std::mutex lock;
    static std::map<std::pair<QString, int>, Ptr> cache;
    std::lock_guard lk(lock);
    Ptr& p = cache[{file, int(flags) | int(type) << 8}];
    if (!p) p = Ptr(new KSharedConfig(file, flags, type));
    return p;
}

const QString* KConfigGroup::value(const char* key) const {
    if (!config_) return nullptr;
    const auto g = config_->groups_.constFind(name_);
    if (g == config_->groups_.constEnd()) return nullptr;
    const auto v = g->constFind(QString::fromUtf8(key));
    return v == g->constEnd() ? nullptr : &*v;
}

QColor KConfigGroup::readEntry(const char* key, const QColor& def) const {
    const QString* v = value(key);
    if (!v || v->isEmpty()) return def;
    if (v->startsWith(QLatin1Char('#'))) return QColor(*v);
    const QStringList parts = v->split(QLatin1Char(','));
    if (parts.size() < 3) return def;
    QColor c(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt(),
             parts.size() > 3 ? parts[3].trimmed().toInt() : 255);
    return c.isValid() ? c : def;
}

bool KConfigGroup::readEntry(const char* key, bool def) const {
    const QString* v = value(key);
    if (!v) return def;
    const QString s = v->toLower();
    if (s == QLatin1String("true") || s == QLatin1String("on") || s == QLatin1String("yes") || s == QLatin1String("1")) return true;
    if (s == QLatin1String("false") || s == QLatin1String("off") || s == QLatin1String("no") || s == QLatin1String("0")) return false;
    return def;
}
