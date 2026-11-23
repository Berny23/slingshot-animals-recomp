// SPDX-License-Identifier: GPL-3.0-or-later
#include "job.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <thread>

#include "devkitpro.hpp"
#include "downloads.hpp"
#include "install.hpp"
#include "paths.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif

namespace builder {
namespace {

constexpr int kTools = 0, kHostTools = 1, kConfigure = 2, kBuild = 3, kFinish = 4;

#ifdef Q_OS_WIN
const QString kExe = ".exe";
const QChar kPathSep = ';';
#else
const QString kExe = "";
const QChar kPathSep = ':';
#endif

// Memory free for the build, in MiB.
qint64 available_mib() {
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) return qint64(m.ullAvailPhys / (1024 * 1024));
#elif defined(Q_OS_MACOS)
    uint64_t total = 0;
    size_t len = sizeof total;
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) == 0) return qint64(total / (1024 * 1024) / 2);
#else
    QFile f("/proc/meminfo");
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : f.readAll().split('\n'))
            if (line.startsWith("MemAvailable:")) return line.simplified().split(' ').value(1).toLongLong() / 1024;
    }
#endif
    return 4096;
}

// Estimated seconds of a Ninja step on one core, by its description; `serial`
// for steps that run alone (job pool, links).
double step_cost(const QString& desc, bool& serial) {
    serial = true;
    if (desc.startsWith("Recompiling")) return 120;
    if (desc.startsWith("Extracting")) return 20;
    if (desc.startsWith("Packaging")) return 40;
    if (desc.startsWith("Linking") && desc.contains("slingshot-")) return 15;
    if (desc.startsWith("Linking") && desc.contains("libmain")) return 15;
    serial = false;
    if (desc.contains("/generated/funcs_")) return 14;  // recompiled code: ~0.5 GB and ~15 s each
    return 1.5;
}

// Windows: the 8.3 form of a path, which has no spaces (devkitPro's linker
// settings cannot hold them).
QString short_path(const QString& path) {
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH];
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD n = GetShortPathNameW(native.c_str(), buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return QDir::fromNativeSeparators(QString::fromWCharArray(buf, int(n)));
#endif
    return path;
}

QString find_program(const QString& name, const QStringList& extra = {}) {
    return QStandardPaths::findExecutable(name, extra).isEmpty() ? QStandardPaths::findExecutable(name)
                                                                  : QStandardPaths::findExecutable(name, extra);
}

}  // namespace

int job_count() {
    const int cores = std::max(1u, std::thread::hardware_concurrency());
    // The largest recompiled files take ~0.5 GB to compile; keep 1.5 GB for the rest.
    const qint64 by_memory = (available_mib() - 1536) / 600;
    return int(std::clamp<qint64>(by_memory, 1, cores));
}

Job::Job(Request request, QObject* parent) : QObject(parent), req_(std::move(request)) {}

QString Job::log_path() const { return log_dir() + "/last-build.log"; }

void Job::start() {
    QThread* thread = QThread::create([this] { run(); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Job::log(const QString& line) {
    if (log_file_.isOpen()) {
        log_file_.write(line.toUtf8() + '\n');
        log_file_.flush();
    }
    emit log_line(line);
}

void Job::set_phase(int index, double fraction) {
    current_ = index;
    for (int i = 0; i < phases_.size(); ++i) {
        if (i < index) phases_[i].done = phases_[i].estimate;
        else if (i == index) phases_[i].done = phases_[i].estimate * std::clamp(fraction, 0.0, 1.0);
        else phases_[i].done = 0;
    }
}

void Job::report(const QString& status) {
    if (!status.isEmpty()) status_ = status;
    double total = 0, done = 0;
    for (const Phase& p : phases_) {
        total += p.estimate;
        done += p.done;
    }
    const double fraction = total > 0 ? done / total : 0;
    // The estimate corrected by how fast this computer has been so far.
    const double elapsed = clock_.elapsed() / 1000.0;
    qint64 eta = -1;
    if (done > 0 && elapsed > 5 && fraction > 0.02) {
        const double speed = std::clamp(elapsed / done, 0.25, 6.0);
        eta = qint64((total - done) * speed);
    } else if (total > 0) {
        eta = qint64(total - done);
    }
    emit progress(fraction, status_, eta);
}

int Job::run_process(const QString& program, const QStringList& args, const std::function<void(const QString&)>& line) {
    log("$ " + program + " " + args.join(' '));
    QProcess p;
    p.setProcessEnvironment(env_);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(-1)) {
        log("cannot start " + program + ": " + p.errorString());
        return -1;
    }
    QByteArray pending;
    auto drain = [&] {
        pending += p.readAll();
        for (qsizetype nl; (nl = pending.indexOf('\n')) >= 0;) {
            const QString text = QString::fromUtf8(pending.left(nl)).trimmed();
            pending.remove(0, nl + 1);
            log(text);
            if (line) line(text);
        }
    };
    while (!p.waitForFinished(200)) {
        drain();
        if (cancel_) {
            p.kill();
            p.waitForFinished(-1);
            return -1;
        }
    }
    drain();
    if (!pending.isEmpty()) {
        log(QString::fromUtf8(pending));
        if (line) line(QString::fromUtf8(pending));
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

bool Job::prepare_tools(QString& error) {
    QStringList path_dirs;
    QVector<const Download*> needed;
    auto want = [&](Tool t) {
        const Download* d = download_for(t);
        if (d && cached(*d).isEmpty()) needed.push_back(d);
        return d;
    };

    // CMake and Ninja: the system's if new enough (Linux), else downloaded.
    QString system_cmake = find_program("cmake");
    if (!system_cmake.isEmpty()) {
        QProcess v;
        v.start(system_cmake, {"--version"});
        v.waitForFinished(-1);
        const auto m = QRegularExpression("version (\\d+)\\.(\\d+)").match(QString::fromUtf8(v.readAll()));
        if (!m.hasMatch() || m.captured(1).toInt() * 100 + m.captured(2).toInt() < 328) system_cmake.clear();
    }
    QString system_ninja = find_program("ninja");
    if (system_ninja.isEmpty()) system_ninja = find_program("ninja-build");
#if !defined(Q_OS_LINUX)
    system_cmake.clear();  // pinned versions where nothing is preinstalled
    system_ninja.clear();
#endif
    const Download* cmake_dl = system_cmake.isEmpty() ? want(Tool::CMake) : nullptr;
    const Download* ninja_dl = system_ninja.isEmpty() ? want(Tool::Ninja) : nullptr;
    if ((system_cmake.isEmpty() && !cmake_dl) || (system_ninja.isEmpty() && !ninja_dl)) {
        error = "CMake 3.28 or newer and Ninja are needed; please install them.";
        return false;
    }
    const Download* mingw_dl = want(Tool::LlvmMingw);
    const Download *ndk_dl = nullptr, *bt_dl = nullptr, *platform_dl = nullptr, *pt_dl = nullptr, *jdk_dl = nullptr;
    if (req_.android) {
        ndk_dl = want(Tool::Ndk);
        bt_dl = want(Tool::BuildTools);
        platform_dl = want(Tool::Platform);
        pt_dl = want(Tool::PlatformTools);
        jdk_dl = want(Tool::Jdk);
        if (!ndk_dl || !jdk_dl) {
            error = "Android builds are not available on this computer.";
            return false;
        }
    }
    if (req_.nintendo_switch && !devkitpro_available()) {
        error = "Nintendo Switch builds are not available on this computer (devkitPro has no tools for it).";
        return false;
    }

    // A C++ compiler: llvm-mingw (Windows, downloaded), the Command Line Tools
    // (macOS) or the system's (Linux).
#if defined(Q_OS_MACOS)
    {
        QProcess x;
        x.start("xcrun", {"--find", "clang++"});
        if (!x.waitForFinished(-1) || x.exitCode() != 0) {
            QProcess::startDetached("xcode-select", {"--install"});
            error = "The build needs Apple's Command Line Tools. Install them in the window that opened, then try again.";
            return false;
        }
    }
#elif defined(Q_OS_LINUX)
    // The system's compiler, else Zig (clang with glibc headers and libc++).
    const bool system_cxx =
        !find_program("c++").isEmpty() || !find_program("g++").isEmpty() || !find_program("clang++").isEmpty();
    const Download* zig_dl = system_cxx ? nullptr : want(Tool::Zig);
    if (!system_cxx && !zig_dl) {
        error = "A C++ compiler is needed (GCC or Clang); please install it, e.g. the package g++ or gcc-c++.";
        return false;
    }
    if (!req_.android && !req_.nintendo_switch) {
        // The game's libraries: the system's development packages, else built
        // from the pinned sources with the prebuilt SDL3 this app carries.
        bool system_libs = false;
        if (system_cxx) {
            QProcess pc;
            pc.start("pkg-config", {"--exists", "sdl3", "freetype2", "harfbuzz", "liblzma", "zlib", "capstone"});
            system_libs = pc.waitForFinished(-1) && pc.exitStatus() == QProcess::NormalExit && pc.exitCode() == 0;
        }
        if (!system_libs) sdl3_kit_ = sdl3_kit_dir();
        if (!system_libs && sdl3_kit_.isEmpty()) {
            QFile os_release("/etc/os-release");
            QString ids;
            if (os_release.open(QIODevice::ReadOnly)) ids = QString::fromUtf8(os_release.readAll());
            QString command;
            if (ids.contains(QRegularExpression("ID(_LIKE)?=.*(fedora|rhel)")))
                command = "sudo dnf install cmake ninja-build gcc-c++ pkgconf SDL3-devel freetype-devel harfbuzz-devel "
                          "xz-devel zlib-ng-compat-devel capstone-devel";
            else if (ids.contains(QRegularExpression("ID(_LIKE)?=.*(debian|ubuntu)")))
                command = "sudo apt install cmake ninja-build g++ pkg-config libsdl3-dev libfreetype-dev libharfbuzz-dev "
                          "liblzma-dev zlib1g-dev libcapstone-dev";
            else if (ids.contains(QRegularExpression("ID(_LIKE)?=.*arch")))
                command = "sudo pacman -S --needed cmake ninja gcc pkgconf sdl3 freetype2 harfbuzz xz zlib capstone";
            error = "Some development packages are missing (a C++ compiler, SDL 3, FreeType, HarfBuzz, liblzma, zlib, "
                    "capstone and pkg-config).";
            if (!command.isEmpty()) error += "\n\nInstall them with:\n" + command;
            return false;
        }
    }
#endif

    // Downloads, weighted by size.
    qint64 total = 0;
    for (const Download* d : needed) total += d->size;
    qint64 finished_bytes = 0;
    for (const Download* d : needed) {
        FetchContext ctx;
        ctx.cancel = &cancel_;
        ctx.log = [this](const QString& s) { log(s); };
        ctx.progress = [&](qint64 done, qint64) {
            set_phase(kTools, double(finished_bytes + std::min(done, d->size)) / double(std::max<qint64>(total, 1)));
            report("Downloading " + d->name + " (" + format_size(finished_bytes + done) + " of " + format_size(total) + ")");
        };
        report("Downloading " + d->name);
        if (fetch(*d, ctx, error).isEmpty()) return false;
        finished_bytes += d->size;
    }

    auto dir_of = [](const Download* d) { return d ? cached(*d) : QString(); };
    cmake_ = cmake_dl ? dir_of(cmake_dl) + (QFileInfo::exists(dir_of(cmake_dl) + "/CMake.app")
                                                ? "/CMake.app/Contents/bin/cmake"
                                                : "/bin/cmake" + kExe)
                      : system_cmake;
    if (req_.nintendo_switch) {
        // devkitPro: the newest packages on every build (unpacked by CMake, which reads .zst).
        FetchContext ctx;
        ctx.cancel = &cancel_;
        ctx.log = [this](const QString& s) { log(s); };
        ctx.progress = [&](qint64 done, qint64 all) {
            report("Downloading devkitPro (" + format_size(done) + " of " + format_size(all) + ")");
        };
        report("Checking devkitPro");
        devkitpro_ = fetch_devkitpro(cmake_, ctx, error);
        if (devkitpro_.isEmpty()) return false;
    }
    ninja_ = ninja_dl ? dir_of(ninja_dl) + "/ninja" + kExe : system_ninja;
    if (ninja_dl)  // zips made elsewhere carry no Unix permissions
        QFile::setPermissions(ninja_, QFile::permissions(ninja_) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    llvm_mingw_ = dir_of(mingw_dl);
#if defined(Q_OS_LINUX)
    if (zig_dl) {
        // CMake wants one program per tool: small scripts that run zig.
        const QString zig = dir_of(zig_dl) + "/zig";
        const QString bin = cache_dir() + "/tools/zig-bin";
        QDir().mkpath(bin);
        for (const QString& tool : {"cc", "c++", "ar", "ranlib"}) {
            QFile f(bin + "/" + tool);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                error = "Cannot write " + f.fileName();
                return false;
            }
            QString quoted = zig;
            quoted.replace("'", "'\\''");
            f.write(QString("#!/bin/sh\nexec '%1' %2 \"$@\"\n").arg(quoted, tool).toUtf8());
            f.close();
            f.setPermissions(f.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        }
        compiler_args_ = {"-DCMAKE_C_COMPILER=" + bin + "/cc", "-DCMAKE_CXX_COMPILER=" + bin + "/c++",
                          "-DCMAKE_AR=" + bin + "/ar", "-DCMAKE_RANLIB=" + bin + "/ranlib"};
        flavor_ = "-zig";
    }
    if (!sdl3_kit_.isEmpty()) flavor_ += "-bundled";
#endif
    if (req_.android) {
        ndk_ = dir_of(ndk_dl);
        sdk_ = cache_dir() + "/tools/android-sdk";
        jdk_ = dir_of(jdk_dl);
        if (QFileInfo::exists(jdk_ + "/Contents/Home")) jdk_ += "/Contents/Home";  // macOS layout
        (void)bt_dl;
        (void)platform_dl;
        (void)pt_dl;
    }

    env_ = QProcessEnvironment::systemEnvironment();
    env_.insert("NINJA_STATUS", "[%f/%t] ");
    if (!compiler_args_.isEmpty()) {
        env_.insert("ZIG_GLOBAL_CACHE_DIR", cache_dir() + "/zig-cache");
        env_.insert("ZIG_LOCAL_CACHE_DIR", cache_dir() + "/zig-cache");
    }
    path_dirs << QFileInfo(cmake_).path() << QFileInfo(ninja_).path();
    if (!llvm_mingw_.isEmpty()) path_dirs << llvm_mingw_ + "/bin";
    if (!jdk_.isEmpty()) {
        env_.insert("JAVA_HOME", jdk_);
        path_dirs << jdk_ + "/bin";
    }
    QStringList native;
    for (const QString& d : path_dirs) native << QDir::toNativeSeparators(d);
    env_.insert("PATH", native.join(kPathSep) + kPathSep + env_.value("PATH"));
    return true;
}

void Job::reset_if_moved(const QString& build_dir) {
    // A build directory made from other sources is started again (CMake refuses it).
    QFile cache(build_dir + "/CMakeCache.txt");
    if (cache.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(cache.readAll());
        cache.close();
        const auto m = QRegularExpression("^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", QRegularExpression::MultilineOption)
                           .match(text);
        if (m.hasMatch() && QDir::cleanPath(m.captured(1).trimmed()) != QDir::cleanPath(source_)) {
            log("Sources moved: starting " + build_dir + " again");
            QDir(build_dir).removeRecursively();
        }
    }
}

bool Job::configure(const QString& build_dir, const QStringList& args, QString& error) {
    reset_if_moved(build_dir);
    QStringList all = {"-S", source_, "-B", build_dir, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja_,
                       "-DCMAKE_BUILD_TYPE=Release"};
    all += args;
    if (run_process(cmake_, all) != 0) {
        error = cancel_ ? "Cancelled" : "Configuring the build failed.";
        return false;
    }
    return true;
}

Job::BuildEstimate Job::estimate_build(const QString& build_dir) const {
    // The heavy steps still to do, from what the build directory holds: the
    // recompile and each recompiled file not compiled yet. The light steps are
    // guessed until Ninja's first status line tells their number.
    BuildEstimate e;
    const QString id = req_.game.id;
    e.recompile = !QFileInfo::exists(build_dir + "/games/" + id + "/generated/funcs.hpp");
    const QString objects = build_dir + "/games/CMakeFiles/recomp_game_code_" + id + ".dir/" + id + "/generated/";
    e.funcs_missing = 0;
    for (int i = 0; i < 128; ++i)
        if (!QFileInfo::exists(objects + QString("funcs_%1.cpp.o").arg(i, 3, 10, QChar('0')))) ++e.funcs_missing;
    e.serial = (e.recompile ? 120 : 0) + 15 + (req_.android || req_.nintendo_switch ? 40 : 0);
    e.parallel = e.funcs_missing * 14.0;
    e.light_guess = QFileInfo::exists(build_dir + "/build.ninja") ? 20 : 500;  // a new directory builds the libraries too
    return e;
}

bool Job::build(const QString& build_dir, const QStringList& targets, QString& error) {
    QStringList base = {"--build", build_dir, "--target"};
    base += targets;

    const BuildEstimate e = estimate_build(build_dir);
    const int funcs_missing = e.funcs_missing;
    const bool recompile = e.recompile;
    const double serial = e.serial;
    double parallel = e.parallel;
    phases_[kBuild].estimate = std::max(5.0, serial + (parallel + e.light_guess * 1.5) / jobs_);
    double total = serial + parallel + e.light_guess * 1.5;
    bool counted = false;

    double done = 0;
    report("Building " + req_.game.title);
    const int code = run_process(cmake_, base + QStringList{"-j", QString::number(jobs_)}, [&](const QString& line) {
        static const QRegularExpression status(R"(^\[(\d+)/(\d+)\] (.*)$)");
        const auto m = status.match(line);
        if (!m.hasMatch()) return;
        bool alone = false;
        const QString desc = m.captured(3);
        if (!counted) {  // the remaining light steps (all but the heavy ones counted above)
            counted = true;
            const int light = std::max(0, m.captured(2).toInt() - funcs_missing - (recompile ? 1 : 0) - 1);
            parallel += light * 1.5;
            total = serial + parallel;  // now with the actual number of light steps
            phases_[kBuild].estimate = std::max(5.0, serial + parallel / jobs_);
        }
        done += step_cost(desc, alone);
        set_phase(kBuild, total > 0 ? done / total : 0);
        QString what = "Building " + req_.game.title;
        if (desc.startsWith("Recompiling") || desc.startsWith("Extracting") || desc.startsWith("Packaging"))
            what = desc.section(" (", 0, 0);
        else if (desc.contains("/generated/funcs_"))
            what = "Compiling the recompiled code";
        report(what);
    });
    if (code != 0) {
        error = cancel_ ? "Cancelled" : "The build failed.";
        return false;
    }
    return true;
}

bool Job::build_host_tools(QString& error) {
    // abrecomp and slingshot-apk for this computer (Android and Switch cross builds).
    const QString dir = cache_dir() + "/build/host" + QString(compiler_args_.isEmpty() ? "" : "-zig");
    QStringList args = {"-DSLINGSHOT_TOOLS_ONLY=ON", "-DSLINGSHOT_SYSTEM_DEPS=OFF"};
    args << compiler_args_;
#ifdef Q_OS_WIN
    args << "-DCMAKE_TOOLCHAIN_FILE=" + source_ + "/cmake/toolchains/llvm-mingw.cmake" << "-DLLVM_MINGW=" + llvm_mingw_;
#endif
    reset_if_moved(dir);
    if (!QFileInfo::exists(dir + "/host-tools/abrecomp" + kExe) || !QFileInfo::exists(dir + "/build.ninja")) {
        report("Preparing the build tools");
        if (!configure(dir, args, error)) return false;
    }
    if (run_process(cmake_, {"--build", dir, "-j", QString::number(jobs_)}) != 0) {
        error = cancel_ ? "Cancelled" : "Building the build tools failed.";
        return false;
    }
    host_tools_ = dir + "/host-tools";
    return true;
}

bool Job::finish(QString& message, QString& path, QString& error) {
    report(req_.android ? "Finishing the Android package" : (req_.install ? "Installing" : "Saving"));
    const QString id = req_.game.id;
    if (req_.nintendo_switch) {
        const QString nro = build_dir_ + "/bin/slingshot-" + id + ".nro";
        QFile::remove(req_.output);
        if (!QFile::copy(nro, req_.output)) {
            error = "Cannot write " + QDir::toNativeSeparators(req_.output);
            return false;
        }
        path = req_.output;
        message = "The Nintendo Switch homebrew was saved:\n" + QDir::toNativeSeparators(path) +
                  "\n\nCopy it into the folder switch on the SD card and start it from the Homebrew Menu.";
        return true;
    }
    if (req_.android) {
        const QString apk = build_dir_ + "/bin/slingshot-" + id + ".apk";
        if (req_.install) {
            // A connected device (USB debugging allowed) gets the game directly.
            const QString adb = sdk_ + "/platform-tools/adb" + kExe;
            QStringList devices;
            run_process(adb, {"devices"}, [&](const QString& line) {
                const QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2 && parts[1] == "device") devices << parts[0];
            });
            if (!devices.isEmpty()) {
                report("Installing on " + devices.first());
                if (run_process(adb, {"-s", devices.first(), "install", "-r", apk}) != 0) {
                    error = "Installing on the device failed (see the log). An older version signed with "
                            "another key has to be uninstalled first.";
                    return false;
                }
                message = req_.game.title + " is installed on the Android device.";
                return true;
            }
        }
        QString out = req_.output;
        if (out.isEmpty())
            out = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + req_.game.title + ".apk";
        QFile::remove(out);
        if (!QFile::copy(apk, out)) {
            error = "Cannot write " + QDir::toNativeSeparators(out);
            return false;
        }
        path = out;
        message = req_.install ? "No Android device is connected, so the APK was saved:\n" + QDir::toNativeSeparators(out)
                               : "The APK was saved:\n" + QDir::toNativeSeparators(out);
        return true;
    }
    BuiltGame built{req_.game, build_dir_ + "/bin/slingshot-" + id + kExe, build_dir_ + "/games/" + id};
    if (req_.exact_output) path = export_folder(built, req_.output, error) ? req_.output : QString();
    else path = req_.install ? install(built, error) : export_portable(built, req_.output, error);
    if (path.isEmpty()) return false;
    if (req_.install) {
#if defined(Q_OS_MACOS)
        message = req_.game.title + " is installed in your Applications folder.";
#elif defined(Q_OS_WIN)
        message = req_.game.title + " is installed: find it in the Start menu.";
#else
        message = req_.game.title + " is installed: find it in your application menu.";
#endif
    } else {
        message = "The game was saved:\n" + QDir::toNativeSeparators(path);
    }
    return true;
}

void Job::run() {
    clock_.start();
    QDir().mkpath(cache_dir());
    QDir().mkpath(log_dir());
    log_file_.setFileName(log_path());
    if (!log_file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) emit log_line("cannot write " + log_path());
    jobs_ = job_count();
    log(QString("%1: %2 (%3), %4, %5 parallel jobs")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), req_.game.title, req_.game.version,
                 req_.android ? "Android" : req_.nintendo_switch ? "Nintendo Switch" : "this computer")
            .arg(jobs_));

    const bool first = !QFileInfo::exists(cache_dir() + "/build");
    const bool cross = req_.android || req_.nintendo_switch;
    phases_ = {{"Tools", 1}, {"Build tools", cross ? 60.0 : 0.0}, {"Configure", first ? 150.0 : 25.0},
               {"Build", 900}, {"Finish", req_.android ? 15.0 : 5.0}};
    // devkitPro: about 120 MB the first time.
    if (req_.nintendo_switch && !QFileInfo::exists(cache_dir() + "/tools/devkitpro")) phases_[kTools].estimate += 24;
    // Downloads: their size at ~5 MB/s, unpacking included.
    for (Tool t : {Tool::CMake, Tool::Ninja, Tool::LlvmMingw, Tool::Ndk, Tool::BuildTools, Tool::Platform,
                   Tool::PlatformTools, Tool::Jdk}) {
        const bool android_tool = t == Tool::Ndk || t == Tool::BuildTools || t == Tool::Platform ||
                                  t == Tool::PlatformTools || t == Tool::Jdk;
        if (android_tool && !req_.android) continue;
        if (const Download* d = download_for(t); d && cached(*d).isEmpty()) phases_[kTools].estimate += d->size / 5e6;
    }
    set_phase(kTools, 0);
    report("Preparing");

    QString error, message, path;
    source_ = sync_sources(error);
    bool ok = !source_.isEmpty() && prepare_tools(error);
    if (ok && cross) {
        set_phase(kHostTools, 0);
        report("Preparing the build tools");
        ok = build_host_tools(error);
    }
    if (ok) {
        set_phase(kConfigure, 0);
        report("Preparing the build");
        QStringList args = {"-DGAME_APKS=" + req_.apk, "-DSLINGSHOT_DEFAULT_MODS=" + (req_.all_mods ? QString("all")
                                                                                     : req_.mods.isEmpty() ? QString("none")
                                                                                     : req_.mods.join(';'))};
        args << "-DGAME_EXTRA_starwars2=" + (req_.game.id == "starwars2" ? req_.extra : QString());
        QString target_dir;
        QStringList targets;
        if (req_.android) {
            target_dir = "android-arm64";
            args << "-DCMAKE_TOOLCHAIN_FILE=" + ndk_ + "/build/cmake/android.toolchain.cmake"
                 << "-DANDROID_ABI=arm64-v8a" << "-DANDROID_PLATFORM=android-24"
                 << "-DSLINGSHOT_HOST_TOOLS=" + host_tools_ << "-DSLINGSHOT_ANDROID_SDK=" + sdk_
                 << "-DSLINGSHOT_JDK=" + jdk_ << "-DSLINGSHOT_ANDROID_KEYSTORE=" + data_dir() + "/android.keystore";
            targets << "slingshot-" + req_.game.id + "-apk";
        } else if (req_.nintendo_switch) {
            target_dir = "switch";
            args << "-DCMAKE_TOOLCHAIN_FILE=" + source_ + "/cmake/toolchains/switch.cmake"
                 << "-DDEVKITPRO=" + short_path(devkitpro_) << "-DSLINGSHOT_HOST_TOOLS=" + host_tools_;
            targets << "slingshot-" + req_.game.id + "-nro";
        } else {
#if defined(Q_OS_WIN)
            target_dir = "windows";
            args << "-DCMAKE_TOOLCHAIN_FILE=" + source_ + "/cmake/toolchains/llvm-mingw.cmake"
                 << "-DLLVM_MINGW=" + llvm_mingw_;
#elif defined(Q_OS_MACOS)
            target_dir = "macos";
#else
            target_dir = "linux" + flavor_;
            args << compiler_args_;
            if (!sdl3_kit_.isEmpty()) args << "-DSLINGSHOT_SYSTEM_DEPS=OFF" << "-DSLINGSHOT_SDL3_PREFIX=" + sdl3_kit_;
#endif
            targets << "slingshot-" + req_.game.id;
        }
        build_dir_ = cache_dir() + "/build/" + target_dir;
        reset_if_moved(build_dir_);
        const BuildEstimate e = estimate_build(build_dir_);
        log(QString("To do: %1recompiled files to compile: %2").arg(e.recompile ? "recompile, " : "").arg(e.funcs_missing));
        phases_[kBuild].estimate = std::max(5.0, e.serial + (e.parallel + e.light_guess * 1.5) / jobs_);
        if (QFileInfo::exists(build_dir_ + "/build.ninja")) phases_[kConfigure].estimate = 15;
        report({});
        ok = configure(build_dir_, args, error);
        if (ok) {
            set_phase(kBuild, 0);
            ok = build(build_dir_, targets, error);
        }
    }
    if (ok) {
        set_phase(kFinish, 0);
        ok = finish(message, path, error);
    }
    if (ok) {
        set_phase(kFinish, 1);
        report("Done");
    }
    log(ok ? message : "error: " + error);
    log_file_.close();
    emit finished(ok, ok ? message : error, path);
}

}  // namespace builder
