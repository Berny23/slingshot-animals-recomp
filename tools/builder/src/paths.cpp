// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace builder {

QString source_dir() {
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& candidate : {app + "/source", app + "/../Resources/source", app + "/../share/slingshot-builder/source"})
        if (QFileInfo::exists(candidate + "/games") && QFileInfo::exists(candidate + "/CMakeLists.txt"))
            return QDir::cleanPath(candidate);
    return QStringLiteral(SLINGSHOT_SOURCE_DIR);
}

QString sdl3_kit_dir() {
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& candidate : {app + "/sdl3", app + "/../share/slingshot-builder/sdl3"})
        if (QFileInfo::exists(candidate + "/lib/cmake/SDL3/SDL3Config.cmake")) return QDir::cleanPath(candidate);
    return {};
}

namespace {
bool g_remove_unpacked = false;
QString g_data_root;
}  // namespace

void set_data_root(const QString& dir) { g_data_root = QDir::cleanPath(QDir(dir).absolutePath()); }

QString data_root() { return g_data_root; }

QString unpacked_app_dir() {
#ifdef Q_OS_WIN
    const QString apps = QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA") + "/slingshot/app");
    const QString app = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (!app.startsWith(apps + "/", Qt::CaseInsensitive)) return {};
    return apps + "/" + app.mid(apps.size() + 1).section('/', 0, 0);  // the version folder
#else
    return {};
#endif
}

void remove_unpacked_app() {
    const QString own = unpacked_app_dir();
    if (own.isEmpty()) return;
    const QDir apps(QFileInfo(own).path());
    for (const QFileInfo& d : apps.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (d.absoluteFilePath().compare(own, Qt::CaseInsensitive) != 0) QDir(d.absoluteFilePath()).removeRecursively();
    g_remove_unpacked = true;
}

void finish_unpacked_app_removal() {
#ifdef Q_OS_WIN
    const QString own = unpacked_app_dir();
    if (!g_remove_unpacked || own.isEmpty()) return;
    // A hidden cmd waits until this process has ended, then deletes the folder.
    QProcess p;
    p.setProgram("cmd.exe");
    p.setNativeArguments(QString("/d /c ping -n 4 127.0.0.1 >nul & rmdir /s /q \"%1\"").arg(QDir::toNativeSeparators(own)));
    p.setWorkingDirectory(QFileInfo(QFileInfo(own).path()).path());
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
    p.startDetached();
#endif
}

QString cache_dir() {
    if (!g_data_root.isEmpty()) return g_data_root + "/cache";
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath());
    return QDir::cleanPath(base + "/slingshot/c");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/slingshot-builder";
#endif
}

QString log_dir() { return g_data_root.isEmpty() ? cache_dir() : g_data_root + "/logs"; }

QString data_dir() {
    if (!g_data_root.isEmpty()) return g_data_root + "/builder";
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath());
    return QDir::cleanPath(base + "/slingshot/data");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/slingshot-builder";
#endif
}

namespace {

bool same_contents(const QString& a, const QString& b) {
    QFile fa(a), fb(b);
    if (fa.size() != fb.size() || !fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    while (!fa.atEnd())
        if (fa.read(1 << 20) != fb.read(1 << 20)) return false;
    return true;
}

}  // namespace

QString sync_sources(QString& error) {
    const QString from = source_dir();
    const QString to = cache_dir() + "/source";
    QSet<QString> wanted;
    for (const QString& top : {"CMakeLists.txt", "LICENSE", "cmake", "games", "mods", "platform", "runtime", "tools"}) {
        const QString root = from + "/" + top;
        QStringList files;
        if (QFileInfo(root).isFile()) {
            files << root;
        } else {
            QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) files << it.next();
        }
        for (const QString& file : files) {
            const QString rel = QDir(from).relativeFilePath(file);
            wanted.insert(rel);
            const QString target = to + "/" + rel;
            if (same_contents(file, target)) continue;
            QDir().mkpath(QFileInfo(target).path());
            QFile::remove(target);
            if (!QFile::copy(file, target)) {
                error = "Cannot copy the sources to " + QDir::toNativeSeparators(to);
                return {};
            }
            QFile::setPermissions(target, QFile::permissions(target) | QFile::WriteOwner);
        }
    }
    // Files no longer in the sources
    QDirIterator it(to, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (!wanted.contains(QDir(to).relativeFilePath(file))) QFile::remove(file);
    }
    return to;
}

qint64 tree_size(const QString& dir) {
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) total += it.nextFileInfo().size();
    return total;
}

QString format_size(qint64 bytes) {
    const double gib = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 0.1) return QString::number(gib, 'f', 1) + " GiB";
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 0) + " MiB";
}

}  // namespace builder
