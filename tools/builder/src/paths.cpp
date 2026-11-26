// SPDX-License-Identifier: GPL-3.0-or-later
#include "paths.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
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
QString g_data_root, g_cache_dir;
}  // namespace

void set_data_root(const QString& dir) { g_data_root = QDir::cleanPath(QDir(dir).absolutePath()); }

QString data_root() { return g_data_root; }

void set_cache_dir(const QString& dir) { g_cache_dir = dir; }

namespace {
// Where the Windows launcher unpacks the builder.
QStringList unpacked_apps_dirs() {
    QStringList dirs;
    if (!g_data_root.isEmpty()) dirs << g_data_root + "/app";
    dirs << QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA") + "/slingshot/app");
    return dirs;
}
}  // namespace

QString unpacked_app_dir() {
#ifdef Q_OS_WIN
    const QString app = QDir::cleanPath(QCoreApplication::applicationDirPath());
    for (const QString& apps : unpacked_apps_dirs())
        if (app.startsWith(apps + "/", Qt::CaseInsensitive))
            return apps + "/" + app.mid(apps.size() + 1).section('/', 0, 0);  // the version folder
    return {};
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

QString home_dir() {
    const QString launcher = qEnvironmentVariable("SLINGSHOT_BUILDER_HOME");
    if (!launcher.isEmpty()) return QDir::cleanPath(launcher);
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (!appimage.isEmpty()) return QFileInfo(appimage).absolutePath();
    QString app = QDir::cleanPath(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    if (app.endsWith(".app/Contents/MacOS")) return QFileInfo(app.chopped(int(sizeof("/Contents/MacOS") - 1))).path();
#endif
    return app;
}

namespace {

// The places without a data root.
QString system_cache_dir() {
#ifdef Q_OS_WIN
    return QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA", QDir::homePath()) + "/slingshot/c");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/slingshot-builder";
#endif
}

QString system_data_dir() {
#ifdef Q_OS_WIN
    return QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA", QDir::homePath()) + "/slingshot/data");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/slingshot-builder";
#endif
}

bool writable_dir(const QString& dir) {
    if (!QDir().mkpath(dir)) return false;
    QFile probe(dir + "/.write-test");
    const bool ok = probe.open(QIODevice::WriteOnly);
    probe.close();
    probe.remove();
    return ok;
}

}  // namespace

void use_portable_data() {
    if (!g_data_root.isEmpty()) return;
    const QString dir = QDir::cleanPath(QDir(home_dir()).filePath("slingshot-builder-data"));
    if (!writable_dir(dir)) return;
    set_data_root(dir);
    // The cache from before is moved where that is a rename (the same drive); else
    // Clear cache deletes it. Build directories start again (Job::reset_if_moved).
    const QString cache = g_data_root + "/cache";
    if (!QFileInfo::exists(cache) && QFileInfo::exists(system_cache_dir())) QDir().rename(system_cache_dir(), cache);
    const QString key = "/android.keystore";
    if (!QFileInfo::exists(data_dir() + key) && QFileInfo::exists(system_data_dir() + key)) {
        QDir().mkpath(data_dir());
        QFile::copy(system_data_dir() + key, data_dir() + key);
    }
    shorten_cache();
}

void shorten_cache() {
#ifdef Q_OS_WIN
    // Windows limits most programs to paths of 260 characters, and the build's
    // deepest files (the dependencies' sources) are about 190 below the cache. A
    // cache in a long path is reached through a short junction instead:
    // %LOCALAPPDATA%\slingshot\l\<hash>. Junctions whose folder is gone are removed.
    constexpr int kLongCache = 60;
    if (g_data_root.isEmpty()) return;
    const QString real = QDir::cleanPath(g_data_root + "/cache");
    const QString links = QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA", QDir::homePath()) + "/slingshot/l");
    for (const QFileInfo& f : QDir(links).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System))
        if (f.isJunction() && !QFileInfo::exists(f.junctionTarget())) QDir().rmdir(f.absoluteFilePath());
    if (real.size() <= kLongCache) return;
    QDir().mkpath(real);
    const QString hash = QCryptographicHash::hash(real.toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(8);
    const QString link = links + "/" + hash;
    if (!QFileInfo(link).isJunction()) {
        QDir().mkpath(links);
        QProcess p;
        p.setProgram("cmd.exe");
        p.setNativeArguments(QString("/d /c mklink /J \"%1\" \"%2\"").arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(real)));
        p.start();
        p.waitForFinished(-1);
    }
    if (QFileInfo(link).isJunction()) set_cache_dir(link);
#endif
}

QStringList legacy_cache_dirs() {
    QStringList dirs;
    if (g_data_root.isEmpty()) return dirs;
    QStringList candidates{system_cache_dir()};
#ifdef Q_OS_WIN
    candidates << QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA") + "/slingshot/app");
#endif
    const QString own_app = unpacked_app_dir();
    for (const QString& d : candidates)
        if (QFileInfo::exists(d) && (own_app.isEmpty() || !own_app.startsWith(d + "/", Qt::CaseInsensitive)))
            dirs << d;
    return dirs;
}

QString cache_dir() {
    if (!g_cache_dir.isEmpty()) return g_cache_dir;
    if (!g_data_root.isEmpty()) return g_data_root + "/cache";
    return system_cache_dir();
}

QString log_dir() { return g_data_root.isEmpty() ? cache_dir() : g_data_root + "/logs"; }

QString data_dir() {
    if (!g_data_root.isEmpty()) return g_data_root + "/builder";
    return system_data_dir();
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
