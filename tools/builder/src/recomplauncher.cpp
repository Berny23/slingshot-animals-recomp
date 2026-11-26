// SPDX-License-Identifier: GPL-3.0-or-later
#include "recomplauncher.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "apk_match.hpp"
#include "job.hpp"
#include "paths.hpp"

namespace builder {
namespace {

#ifdef Q_OS_WIN
const QString kExe = ".exe";
#else
const QString kExe = "";
#endif

// Exit codes (exit_codes in recomplauncher.json)
constexpr int kFailed = 1, kUsage = 2, kNotBuilt = 3;

// Game logs kept: <id>.log and the ones before it, <id>-1.log ...
constexpr int kLogsKept = 5;

void print_progress(double fraction, const QString& text) {
    // QString::number: a decimal point whatever the locale (printf follows LC_NUMERIC)
    std::printf("progress %s %s\n", qPrintable(QString::number(std::clamp(fraction, 0.0, 1.0), 'f', 3)), qPrintable(text));
    std::fflush(stdout);
}

void print_error(const QString& text) {
    std::printf("error: %s\n", qPrintable(text));
    std::fflush(stdout);
}

QString game_folder(const QString& id) { return data_root() + "/games/" + id; }
QString save_folder(const QString& id) { return data_root() + "/saves/" + id; }

// Where the game kept its saves without --data before saves/ next to it (platform/sdl/main.cpp).
QString system_save_folder(const QString& id) {
#if defined(Q_OS_WIN)
    return qEnvironmentVariable("APPDATA") + "/slingshot-animals-recomp/" + id;
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + "/Library/Application Support/slingshot-animals-recomp/" + id;
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    return (xdg.isEmpty() ? QDir::homePath() + "/.local/share" : xdg) + "/slingshot-animals-recomp/" + id;
#endif
}

// The saves of a game the builder installed (saves/ next to it, install.cpp).
QString installed_save_folder(const QString& id) {
#if defined(Q_OS_WIN)
    return qEnvironmentVariable("LOCALAPPDATA") + "/Programs/slingshot-animals-recomp/" + id + "/saves";
#elif defined(Q_OS_MACOS)
    return {};
#else
    return QDir::homePath() + "/.local/lib/slingshot-animals-recomp/" + id + "/saves";
#endif
}

bool copy_folder(const QString& from, const QString& to) {
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + "/" + QDir(from).relativeFilePath(file);
        QDir().mkpath(QFileInfo(target).path());
        if (!QFile::copy(file, target)) return false;
    }
    return QDir().mkpath(to);
}

// What a built game was made from: the builder's version, the game and its extra files.
QByteArray stamp(const Game& game, const QString& extra) {
    return QString("%1\n%2\n%3\n").arg(SLINGSHOT_VERSION, game.sha256, extra).toUtf8();
}

bool build_game(const Request& request, double base, double share, QString& error) {
    Job job(request);
    QEventLoop loop;
    bool ok = false;
    int last = -1;
    QObject::connect(&job, &Job::progress, &loop, [&](double f, const QString& status, qint64) {
        const int permille = int((base + share * f) * 1000);
        if (permille == last) return;
        last = permille;
        print_progress(permille / 1000.0, status);
    });
    QObject::connect(&job, &Job::finished, &loop, [&](bool done, const QString& message, const QString&) {
        ok = done;
        if (!done) error = message + "\nLog: " + QDir::toNativeSeparators(job.log_path());
        loop.quit();
    });
    job.start();
    loop.exec();
    return ok;
}

int prepare(const QStringList& apks, const QString& extra) {
    if (apks.isEmpty()) {
        print_error("No APK was given (--apk=<file>).");
        return kUsage;
    }
    const QVector<Game> games = load_games(source_dir());
    QVector<Game> found;
    for (const QString& apk : apks) {
        QString error;
        const auto game = identify_apk(apk, games, error);
        if (!game) {
            print_error(QDir::toNativeSeparators(apk) + ": " + error);
            return kFailed;
        }
        found.push_back(*game);
    }
    shorten_cache();
    // The saves' folder exists from the setup on (the launcher looks for it); each game's own
    // folder only once it is played, which copies saves from before (play).
    QDir().mkpath(data_root() + "/saves");
    QSet<QString> done;
    for (int i = 0; i < found.size(); ++i) {
        const Game& game = found[i];
        if (done.contains(game.id)) continue;
        done.insert(game.id);
        const double base = double(i) / found.size(), share = 1.0 / found.size();
        const QString out = game_folder(game.id);
        const QString game_extra = game.id == "starwars2" ? extra : QString();
        const QString stamp_path = out + "/build.txt";
        QFile old(stamp_path);
        if (QFileInfo::exists(out + "/slingshot-" + game.id + kExe) && old.open(QIODevice::ReadOnly) &&
            old.readAll() == stamp(game, game_extra)) {
            print_progress(base + share, game.title + " is already built");
            continue;
        }
        old.close();
        QFile::remove(stamp_path);  // written again only once the build has finished
        Request r;
        r.apk = QFileInfo(apks[i]).absoluteFilePath();
        r.game = game;
        r.install = false;
        r.output = out;
        r.exact_output = true;
        r.extra = game_extra.isEmpty() ? QString() : QFileInfo(game_extra).absoluteFilePath();
        QString error;
        if (!build_game(r, base, share, error)) {
            print_error(error);
            return kFailed;
        }
        QFile f(stamp_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(stamp(game, game_extra)) < 0) {
            print_error("Cannot write " + QDir::toNativeSeparators(stamp_path));
            return kFailed;
        }
    }
    // prepare.done: written after every finished run, with every game built so far.
    QStringList built;
    for (const QFileInfo& d : QDir(data_root() + "/games").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFileInfo::exists(d.absoluteFilePath() + "/build.txt")) built << d.fileName();
    QFile prepared(data_root() + "/games/prepared.txt");
    if (!prepared.open(QIODevice::WriteOnly | QIODevice::Truncate) || prepared.write(built.join('\n').toUtf8() + '\n') < 0) {
        print_error("Cannot write " + QDir::toNativeSeparators(prepared.fileName()));
        return kFailed;
    }
    print_progress(1, "Done");
    return 0;
}

// <data>/logs/<id>.log for this run; the earlier ones move down to <id>-1.log ...
QString new_log(const QString& id) {
    const QString dir = data_root() + "/logs";
    QDir().mkpath(dir);
    auto name = [&](int n) { return dir + "/" + id + (n ? QString("-%1").arg(n) : QString()) + ".log"; };
    QFile::remove(name(kLogsKept - 1));
    for (int n = kLogsKept - 2; n >= 0; --n) QFile::rename(name(n), name(n + 1));
    return name(0);
}

int play(const QString& id, QStringList game_args) {
    if (!QRegularExpression("^[a-z0-9]+$").match(id).hasMatch()) {
        print_error("Unknown game '" + id + "'.");
        return kUsage;
    }
    const QString exe = game_folder(id) + "/slingshot-" + id + kExe;
    if (!QFileInfo::exists(exe)) {
        print_error(QDir::toNativeSeparators(exe) + " is not built yet.");
        return kNotBuilt;
    }
    // The saves go with the launcher's folder; saves the game had before are copied there once.
    const QString saves = save_folder(id);
    if (!QFileInfo::exists(saves)) {
        QString before = installed_save_folder(id);
        if (!QFileInfo(before).isDir()) before = system_save_folder(id);
        if (QFileInfo(before).isDir() && !copy_folder(before, saves))
            print_error("Cannot copy the saves from " + QDir::toNativeSeparators(before));
    }
    game_args.prepend(QDir::toNativeSeparators(saves));
    game_args.prepend("--data");
    const QString log = new_log(id);
#ifdef Q_OS_WIN
    // The game ends with this process (a job object closed with it).
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardOutputFile(log, QIODevice::Truncate);
    p.setInputChannelMode(QProcess::ForwardedInputChannel);
    p.start(exe, game_args);
    if (!p.waitForStarted(-1)) {
        print_error("Cannot start " + QDir::toNativeSeparators(exe) + ": " + p.errorString());
        return kFailed;
    }
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        if (HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(p.processId()))) {
            AssignProcessToJobObject(job, child);
            CloseHandle(child);
        }
    }
    p.waitForFinished(-1);
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : kFailed;
#else
    // The game replaces this process: the launcher sees it, its signals and its exit code.
    const int fd = ::open(QFile::encodeName(log).constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        std::fflush(nullptr);
        ::dup2(fd, STDOUT_FILENO);
        ::dup2(fd, STDERR_FILENO);
        ::close(fd);
    }
    QList<QByteArray> native{QFile::encodeName(exe)};
    for (const QString& a : game_args) native << a.toLocal8Bit();
    std::vector<char*> argv;
    for (QByteArray& a : native) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::printf("%s: %s\n", qPrintable(QDateTime::currentDateTime().toString(Qt::ISODate)), qPrintable(QString::fromLocal8Bit(native.join(' '))));
    std::fflush(stdout);
    execv(argv[0], argv.data());
    print_error("Cannot start " + QDir::toNativeSeparators(exe) + ": " + QString::fromLocal8Bit(std::strerror(errno)));
    return kFailed;
#endif
}

}  // namespace

bool is_recomplauncher_mode(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--prepare") || !std::strncmp(argv[i], "--play=", 7)) return true;
    return false;
}

int run_recomplauncher_mode() {
    const QStringList args = QCoreApplication::arguments();
    bool prepare_mode = false;
    QString data, game, extra;
    QStringList apks, game_args;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == "--prepare") prepare_mode = true;
        else if (a.startsWith("--data=")) data = a.mid(7);
        else if (a.startsWith("--play=")) game = a.mid(7);
        else if (a.startsWith("--apk=")) apks << a.mid(6);
        else if (a.startsWith("--starwars2-assets=")) extra = a.mid(19);
        else game_args << a;  // --play: the game's options
    }
    if (data.isEmpty() || (prepare_mode && !game.isEmpty()) || (prepare_mode && !game_args.isEmpty())) {
        print_error("usage: slingshot-builder --prepare --data=<dir> --apk=<apk>... [--starwars2-assets=<zip or folder>]\n"
                    "       slingshot-builder --data=<dir> --play=<id> [game options...]");
        return kUsage;
    }
    set_data_root(data);
    return prepare_mode ? prepare(apks, extra) : play(game, game_args);
}

}  // namespace builder
