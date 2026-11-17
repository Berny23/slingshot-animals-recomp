// SPDX-License-Identifier: GPL-3.0-or-later
#include "devkitpro.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSysInfo>

#include "paths.hpp"

namespace builder {
namespace {

const QString kServer = "https://pkg.devkitpro.org/packages/";
const QString kStamp = ".slingshot-packages";

// The compiler, libnx, the NRO tools and SDL2 with its OpenGL ES (mesa).
const QStringList kPackages = {"devkita64-binutils", "devkita64-gcc", "devkita64-newlib", "libnx", "switch-tools",
                               "switch-sdl2", "switch-mesa", "switch-libdrm_nouveau"};

// The repository of programs for this computer (the libraries are in dkp-libs).
QString host_repository() {
#if defined(Q_OS_LINUX)
    if (QSysInfo::currentCpuArchitecture() == "x86_64") return "linux/x86_64/dkp-linux.db";
    if (QSysInfo::currentCpuArchitecture() == "arm64") return "linux/aarch64/dkp-linux.db";
#elif defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture() == "arm64" ? "osx/arm64/dkp-osx.db" : "osx/x86_64/dkp-osx.db";
#elif defined(Q_OS_WIN)
    if (QSysInfo::currentCpuArchitecture() == "x86_64") return "windows/x86_64/dkp-windows.db";
#endif
    return {};
}

struct Package {
    QString file, sha256, url;
    qint64 size = 0;
};

int unpack(const QString& cmake, const QString& archive, const QString& dir) {
    QDir().mkpath(dir);
    QProcess tar;
    tar.setWorkingDirectory(dir);
    tar.start(cmake, {"-E", "tar", "xf", archive});
    if (!tar.waitForFinished(-1)) return -1;
    return tar.exitStatus() == QProcess::NormalExit ? tar.exitCode() : -1;
}

// A repository's package list (a tar of <name>-<version>/desc files): name -> package.
bool read_repository(const QString& cmake, const QString& db, const QString& base_url, QHash<QString, Package>& out,
                     QString& error) {
    const QString dir = db + ".d";
    QDir(dir).removeRecursively();
    if (unpack(cmake, db, dir) != 0) {
        error = "Cannot read devkitPro's package list " + QFileInfo(db).fileName();
        return false;
    }
    for (QDirIterator it(dir, {"desc"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QHash<QString, QString> fields;
        QString key;
        for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
            if (line.startsWith('%') && line.endsWith('%')) key = line.mid(1, line.size() - 2);
            else if (!line.isEmpty() && !key.isEmpty() && !fields.contains(key)) fields[key] = line.trimmed();
        }
        Package p{fields.value("FILENAME"), fields.value("SHA256SUM"), base_url + fields.value("FILENAME"),
                  fields.value("CSIZE").toLongLong()};
        if (!p.file.isEmpty() && !p.sha256.isEmpty()) out.insert(fields.value("NAME"), p);
    }
    QDir(dir).removeRecursively();
    return true;
}

}  // namespace

bool devkitpro_available() { return !host_repository().isEmpty(); }

QString fetch_devkitpro(const QString& cmake, const FetchContext& ctx, QString& error) {
    const QString dest = cache_dir() + "/tools/devkitpro";
    QFile stamp_file(dest + "/" + kStamp);
    const QString installed = stamp_file.open(QIODevice::ReadOnly) ? QString::fromUtf8(stamp_file.readAll()) : QString();
    stamp_file.close();
    const QString host = host_repository();
    if (host.isEmpty()) {
        error = "Nintendo Switch builds are not available on this computer (devkitPro has no tools for it).";
        return {};
    }

    // The newest versions: the package lists, downloaded on every build.
    const QString dl_dir = cache_dir() + "/downloads";
    QDir().mkpath(dl_dir);
    QHash<QString, Package> host_packages, lib_packages;
    QString list_error;
    FetchContext quiet{ctx.cancel, {}, ctx.log};
    const QString host_db = dl_dir + "/" + QFileInfo(host).fileName(), lib_db = dl_dir + "/dkp-libs.db";
    const bool lists = download_checked(kServer + host, {}, 30'000, "devkitPro's package list", host_db, quiet, list_error) &&
                       download_checked(kServer + "dkp-libs.db", {}, 250'000, "devkitPro's package list", lib_db, quiet,
                                        list_error) &&
                       read_repository(cmake, host_db, kServer + QFileInfo(host).path() + "/", host_packages, list_error) &&
                       read_repository(cmake, lib_db, kServer, lib_packages, list_error);
    QFile::remove(host_db);
    QFile::remove(lib_db);
    if (!lists) {
        if (ctx.cancel && *ctx.cancel) {
            error = "Cancelled";
            return {};
        }
        if (!installed.isEmpty()) {
            if (ctx.log) ctx.log(list_error + "; using the devkitPro packages already downloaded");
            return dest;
        }
        error = list_error;
        return {};
    }
    QVector<Package> wanted;
    QStringList files;
    qint64 total = 0;
    for (const QString& name : kPackages) {
        const Package* p = host_packages.contains(name) ? &host_packages[name]
                           : lib_packages.contains(name) ? &lib_packages[name]
                                                         : nullptr;
        if (!p) {
            error = "devkitPro's package lists have no " + name + ".";
            return {};
        }
        wanted.push_back(*p);
        files << p->file;
        total += p->size;
    }
    const QString stamp = files.join('\n');
    if (stamp == installed.trimmed()) return dest;

    // Download and check every package, then unpack them together and replace the old ones.
    const QString tmp = dest + ".tmp";
    QDir(tmp).removeRecursively();
    qint64 finished = 0;
    for (const Package& p : wanted) {
        FetchContext each = ctx;
        each.progress = [&](qint64 done, qint64) {
            if (ctx.progress) ctx.progress(finished + std::min(done, p.size), total);
        };
        const QString archive = dl_dir + "/" + p.file;
        if (!download_checked(p.url, p.sha256, p.size, p.file, archive, each, error)) return {};
        if (ctx.log) ctx.log("Unpacking " + p.file);
        const int code = unpack(cmake, archive, tmp);
        QFile::remove(archive);
        if (code != 0) {
            error = "Cannot unpack " + p.file;
            return {};
        }
        finished += p.size;
    }
    QDir(dest).removeRecursively();
    if (!QDir().rename(tmp + "/opt/devkitpro", dest)) {
        error = "Cannot move devkitPro into " + dest;
        return {};
    }
    QDir(tmp).removeRecursively();
    if (stamp_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) stamp_file.write(stamp.toUtf8());
    return dest;
}

}  // namespace builder
