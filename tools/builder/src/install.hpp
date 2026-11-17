// SPDX-License-Identifier: GPL-3.0-or-later
// Putting a built game in place: for the user only, as is usual on each system.
//   Linux:   ~/.local/lib/slingshot-animals-recomp/<id>/ and a .desktop entry
//   Windows: %LOCALAPPDATA%\Programs\slingshot-animals-recomp\<id>\, a Start
//            menu shortcut and an entry in "Installed apps"
//   macOS:   ~/Applications/<title>.app
#pragma once

#include <QString>

#include "catalog.hpp"

namespace builder {

struct BuiltGame {
    Game game;
    QString executable;  // bin/slingshot-<id>[.exe]
    QString game_dir;    // extracted lib/, assets/, icon.png, icon.ico
};

// Installs; returns where, or an empty string with `error` set.
QString install(const BuiltGame& built, QString& error);

// A copy that runs from anywhere (folder, or app bundle on macOS) inside `dir`;
// returns its path, or an empty string with `error` set.
QString export_portable(const BuiltGame& built, const QString& dir, QString& error);

}  // namespace builder
