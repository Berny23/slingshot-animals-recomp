// SPDX-License-Identifier: GPL-3.0-or-later
// slingshot-builder                 the window
// slingshot-builder --build <apk> [--android | --switch] [--only <folder|file.apk|file.nro>] [--mods <a,b>|none]
//                                   the same build in the terminal
// slingshot-builder --prepare ... | --play=<id> ...
//                                   for RecompLauncher (recomplauncher.hpp)
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apk_match.hpp"
#ifdef SLINGSHOT_BREEZE
#include "breeze_style.hpp"
#endif
#include "job.hpp"
#include "paths.hpp"
#include "recomplauncher.hpp"
#include "window.hpp"

namespace {

int run_cli(QCoreApplication& app) {
    using namespace builder;
    const QStringList args = app.arguments();
    QTextStream err(stderr);
    Request r;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == "--build" && i + 1 < args.size()) r.apk = QFileInfo(args[++i]).absoluteFilePath();
        else if (a == "--android") r.android = true;
        else if (a == "--switch") r.nintendo_switch = true;
        else if (a == "--only" && i + 1 < args.size()) {
            r.install = false;
            r.output = args[++i];
            r.output = QFileInfo(r.output).absoluteFilePath();
        } else if (a == "--mods" && i + 1 < args.size()) {
            r.all_mods = false;
            const QString list = args[++i];
            if (list != "none") r.mods = list.split(',', Qt::SkipEmptyParts);
        } else {
            err << "usage: slingshot-builder --build <apk> [--android | --switch] [--only <folder|file.apk|file.nro>] "
                   "[--mods <a,b>|none]\n";
            return 2;
        }
    }
    QString error;
    const auto game = identify_apk(r.apk, load_games(source_dir()), error);
    if (!game) {
        err << error << "\n";
        return 1;
    }
    r.game = *game;
    if (r.nintendo_switch) {
        r.install = false;
        if (r.output.isEmpty()) r.output = QDir::currentPath() + "/slingshot-" + r.game.id + ".nro";
    }
    Job job(r);
    int last = -1;
    QObject::connect(&job, &Job::progress, [&](double f, const QString& status, qint64 eta) {
        const int pct = int(f * 100);
        if (pct == last) return;
        last = pct;
        std::printf("%3d %%  %s%s\n", pct, qPrintable(status), eta >= 0 ? qPrintable(QString("  (~%1 s left)").arg(eta)) : "");
        std::fflush(stdout);
    });
    int code = 1;
    QObject::connect(&job, &Job::finished, [&](bool ok, const QString& message, const QString&) {
        std::printf("%s\n", qPrintable(message));
        std::printf("log: %s\n", qPrintable(job.log_path()));
        code = ok ? 0 : 1;
        app.quit();
    });
    job.start();
    app.exec();
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--build") {
            QCoreApplication app(argc, argv);
            app.setApplicationName("slingshot-builder");
            return run_cli(app);
        }
    }
    if (builder::is_recomplauncher_mode(argc, argv)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("slingshot-builder");
        return builder::run_recomplauncher_mode();
    }
#if defined(__linux__)
    // The AppImage carries its own Qt: the desktop portal gives it the
    // system's file dialogs and light/dark setting.
    if (qEnvironmentVariableIsSet("APPIMAGE") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif
#ifdef SLINGSHOT_BREEZE
    // Breeze unless a style is asked for (-style, and QT_STYLE_OVERRIDE except
    // on Windows, where it is often set system-wide for other apps).
#ifdef _WIN32
    bool own_style = false;
#else
    bool own_style = qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE");
#endif
    for (int i = 1; i < argc; ++i)
        own_style |= !std::strcmp(argv[i], "-style") || !std::strncmp(argv[i], "-style=", 7) ||
                     !std::strcmp(argv[i], "--style") || !std::strncmp(argv[i], "--style=", 8);
#endif
    QApplication app(argc, argv);
#ifdef SLINGSHOT_BREEZE
    if (!own_style) builder::use_breeze(app);
#endif
    app.setApplicationName("slingshot-builder");
    app.setApplicationDisplayName("Slingshot Builder");
    app.setDesktopFileName("slingshot-builder");
    app.setWindowIcon(QIcon(":/builder.svg"));
    builder::Window window;
    window.show();
    // An APK given on the command line (options such as RecompLauncher's --data= are not one)
    if (app.arguments().size() > 1 && !app.arguments().at(1).startsWith("--")) window.open_apk(app.arguments().at(1));
    const int code = app.exec();
    builder::finish_unpacked_app_removal();
    return code;
}
