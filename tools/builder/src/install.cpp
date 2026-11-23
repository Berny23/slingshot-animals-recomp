// SPDX-License-Identifier: GPL-3.0-or-later
#include "install.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace builder {
namespace {

// Copies `from` (file or folder) to `to`, replacing what is there.
bool copy_tree(const QString& from, const QString& to) {
    const QFileInfo info(from);
    if (info.isFile()) {
        QFile::remove(to);
        QDir().mkpath(QFileInfo(to).path());
        return QFile::copy(from, to);
    }
    QDir(to).removeRecursively();
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + "/" + QDir(from).relativeFilePath(file);
        QDir().mkpath(QFileInfo(target).path());
        if (!QFile::copy(file, target)) return false;
    }
    return true;
}

// The game folder an installed game reads: the executable's game/ (platform/sdl/main.cpp).
bool copy_game_files(const BuiltGame& b, const QString& game_dir) {
    QDir(game_dir).removeRecursively();
    return copy_tree(b.game_dir + "/lib", game_dir + "/lib") && copy_tree(b.game_dir + "/assets", game_dir + "/assets") &&
           copy_tree(b.game_dir + "/icon.png", game_dir + "/icon.png");
}

QString exe_name(const BuiltGame& b) { return QFileInfo(b.executable).fileName(); }

// A folder with the executable and game/ next to it.
bool make_folder(const BuiltGame& b, const QString& dir, QString& error) {
    QDir().mkpath(dir);
    const QString exe = dir + "/" + exe_name(b);
    if (!copy_tree(b.executable, exe) || !copy_game_files(b, dir + "/game")) {
        error = "Cannot copy the game into " + QDir::toNativeSeparators(dir);
        return false;
    }
    QFile::setPermissions(exe, QFile::permissions(exe) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    return true;
}

#ifdef Q_OS_MACOS
bool run(const QString& program, const QStringList& args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(-1) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// <dir>/<title>.app: Contents/MacOS/<exe>, Contents/Resources/{game/, icon.icns}.
QString make_bundle(const BuiltGame& b, const QString& dir, QString& error) {
    const QString app = dir + "/" + b.game.title + ".app";
    QDir(app).removeRecursively();
    const QString contents = app + "/Contents";
    const QString exe = contents + "/MacOS/" + exe_name(b);
    if (!copy_tree(b.executable, exe) || !copy_game_files(b, contents + "/Resources/game")) {
        error = "Cannot copy the game into " + app;
        return {};
    }
    QFile::setPermissions(exe, QFile::permissions(exe) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);

    // icon.icns from the APK's icon (sips scales it, iconutil packs the sizes).
    const QString iconset = QDir::tempPath() + "/slingshot-" + b.game.id + ".iconset";
    QDir(iconset).removeRecursively();
    QDir().mkpath(iconset);
    for (int size : {16, 32, 128, 256, 512}) {
        for (int scale : {1, 2}) {
            const int px = size * scale;
            const QString name = QString("icon_%1x%1%2.png").arg(size).arg(scale == 2 ? "@2x" : "");
            run("sips", {"-z", QString::number(px), QString::number(px), b.game_dir + "/icon.png", "--out", iconset + "/" + name});
        }
    }
    const bool icon = run("iconutil", {"-c", "icns", iconset, "-o", contents + "/Resources/icon.icns"});
    QDir(iconset).removeRecursively();

    QFile plist(contents + "/Info.plist");
    if (!plist.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = "Cannot write " + plist.fileName();
        return {};
    }
    const QString version = b.game.version.section(' ', 0, 0);
    auto esc = [](QString s) { return s.replace("&", "&amp;").replace("<", "&lt;"); };
    QTextStream(&plist) << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                        << "<plist version=\"1.0\"><dict>\n"
                        << "<key>CFBundleName</key><string>" << esc(b.game.title) << "</string>\n"
                        << "<key>CFBundleDisplayName</key><string>" << esc(b.game.title) << "</string>\n"
                        << "<key>CFBundleIdentifier</key><string>io.github.berny23.slingshot-" << b.game.id << "</string>\n"
                        << "<key>CFBundleExecutable</key><string>" << exe_name(b) << "</string>\n"
                        << (icon ? "<key>CFBundleIconFile</key><string>icon</string>\n" : "")
                        << "<key>CFBundlePackageType</key><string>APPL</string>\n"
                        << "<key>CFBundleShortVersionString</key><string>" << esc(version) << "</string>\n"
                        << "<key>CFBundleVersion</key><string>" << esc(version) << "</string>\n"
                        << "<key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>\n"
                        << "<key>LSMinimumSystemVersion</key><string>11.0</string>\n"
                        << "<key>NSHighResolutionCapable</key><true/>\n"
                        << "</dict></plist>\n";
    plist.close();
    // Ad-hoc signature: required to run on Apple silicon.
    run("codesign", {"--force", "--deep", "--sign", "-", app});
    return app;
}
#endif

}  // namespace

QString install(const BuiltGame& b, QString& error) {
#if defined(Q_OS_MACOS)
    const QString apps = QDir::homePath() + "/Applications";
    QDir().mkpath(apps);
    return make_bundle(b, apps, error);
#elif defined(Q_OS_WIN)
    const QString base = qEnvironmentVariable("LOCALAPPDATA") + "/Programs/slingshot-animals-recomp/" + b.game.id;
    if (!make_folder(b, base, error)) return {};
    const QString exe = base + "/" + exe_name(b);
    const QString menu = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir().mkpath(menu);
    const QString link = menu + "/" + b.game.title + ".lnk";
    QFile::remove(link);
    QFile::link(exe, link);
    // "Installed apps" entry; uninstalling removes the folder, shortcut and entry (saves stay).
    const QString key = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\slingshot-" + b.game.id;
    QSettings reg(key, QSettings::NativeFormat);
    const QString native_base = QDir::toNativeSeparators(base);
    reg.setValue("DisplayName", b.game.title);
    reg.setValue("DisplayVersion", b.game.version.section(' ', 0, 0));
    reg.setValue("DisplayIcon", QDir::toNativeSeparators(exe));
    reg.setValue("Publisher", "slingshot-animals-recomp");
    reg.setValue("InstallLocation", native_base);
    reg.setValue("NoModify", 1);
    reg.setValue("NoRepair", 1);
    reg.setValue("UninstallString",
                 QString("cmd.exe /c rmdir /s /q \"%1\" & del /q \"%2\" & reg delete \"%3\" /f")
                     .arg(native_base, QDir::toNativeSeparators(link), QString(key).replace("HKEY_CURRENT_USER", "HKCU")));
    reg.sync();
    return base;
#else
    const QString base = QDir::homePath() + "/.local/lib/slingshot-animals-recomp/" + b.game.id;
    if (!make_folder(b, base, error)) return {};
    // The file name matches the app id the game sets (taskbar icon on Wayland).
    const QString apps = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir().mkpath(apps);
    QFile desktop(apps + "/slingshot-" + b.game.id + ".desktop");
    if (!desktop.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = "Cannot write " + desktop.fileName();
        return {};
    }
    auto quote = [](QString s) { return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""; };
    QTextStream(&desktop) << "[Desktop Entry]\n"
                          << "Type=Application\n"
                          << "Name=" << b.game.title << "\n"
                          << "Comment=" << b.game.title << " " << b.game.version.section(' ', 0, 0) << " (recompiled)\n"
                          << "Exec=" << quote(base + "/" + exe_name(b)) << "\n"
                          << "Icon=" << base << "/game/icon.png\n"
                          << "Categories=Game;ArcadeGame;\n"
                          << "StartupWMClass=slingshot-" << b.game.id << "\n"
                          << "Terminal=false\n";
    desktop.close();
    QProcess::execute("update-desktop-database", {apps});
    return base;
#endif
}

bool export_folder(const BuiltGame& b, const QString& folder, QString& error) { return make_folder(b, folder, error); }

QString export_portable(const BuiltGame& b, const QString& dir, QString& error) {
#ifdef Q_OS_MACOS
    return make_bundle(b, dir, error);
#else
    const QString folder = dir + "/" + b.game.title;
    if (!make_folder(b, folder, error)) return {};
    return folder;
#endif
}

}  // namespace builder
