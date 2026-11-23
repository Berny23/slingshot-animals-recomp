// SPDX-License-Identifier: GPL-3.0-or-later
// One build, run on its own thread: tools (downloaded or the system's), CMake
// configure, Ninja build of the game, then install, export or adb install.
#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <atomic>
#include <functional>

#include "catalog.hpp"

namespace builder {

struct Request {
    QString apk;
    Game game;
    QStringList mods;       // enabled mods
    bool all_mods = true;   // every mod (also ones added later)
    bool android = false;
    bool nintendo_switch = false;  // a homebrew .nro, saved to `output`
    bool install = true;    // false: build only, into `output`
    QString output;         // build only: a folder (desktop), the .apk path (Android) or the .nro path
    bool exact_output = false;  // desktop: `output` is the game's folder itself, on every system
    QString extra;          // Star Wars II: files/assets_service/ as zip or folder, or empty
};

// Parallel compile jobs for this computer: cores, limited by free memory.
int job_count();

class Job : public QObject {
    Q_OBJECT
public:
    explicit Job(Request request, QObject* parent = nullptr);
    void start();
    void cancel() { cancel_ = true; }
    QString log_path() const;

signals:
    // fraction 0..1; eta in seconds, -1 while unknown
    void progress(double fraction, const QString& status, qint64 eta);
    void log_line(const QString& line);
    void finished(bool ok, const QString& message, const QString& result_path);

private:
    struct Phase {
        QString name;
        double estimate = 1;  // seconds
        double done = 0;      // seconds of `estimate` reached
    };

    struct BuildEstimate {
        bool recompile = true;
        int funcs_missing = 128;
        double serial = 0, parallel = 0;  // seconds on one core
        int light_guess = 0;              // other steps
    };
    BuildEstimate estimate_build(const QString& build_dir) const;

    void run();
    bool prepare_tools(QString& error);
    bool build_host_tools(QString& error);
    void reset_if_moved(const QString& build_dir);
    bool configure(const QString& build_dir, const QStringList& args, QString& error);
    bool build(const QString& build_dir, const QStringList& targets, QString& error);
    bool finish(QString& message, QString& path, QString& error);
    int run_process(const QString& program, const QStringList& args, const std::function<void(const QString&)>& line = {});
    void report(const QString& status);
    void log(const QString& line);
    void set_phase(int index, double fraction);

    Request req_;
    std::atomic<bool> cancel_{false};
    QFile log_file_;
    QElapsedTimer clock_;
    QVector<Phase> phases_;
    int current_ = 0;
    QString status_;
    int jobs_ = 1;

    QProcessEnvironment env_;
    QString cmake_, ninja_, llvm_mingw_, ndk_, sdk_, jdk_, devkitpro_;
    QString source_;  // <cache>/source
    QString build_dir_, host_tools_;
    // Linux: the compiler (none: the system's) and the libraries (none: the
    // system's; else downloaded and built, with the app's prebuilt SDL3)
    QStringList compiler_args_;
    QString sdl3_kit_;
    QString flavor_;  // build directory suffix for the above
};

}  // namespace builder
