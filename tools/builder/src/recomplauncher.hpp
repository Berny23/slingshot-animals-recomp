// SPDX-License-Identifier: GPL-3.0-or-later
// The builder as RecompLauncher runs it (recomplauncher.json):
//   slingshot-builder --prepare --data=<dir> --apk=<apk>... [--starwars2-assets=<zip or folder>]
//       builds each game into <dir>/games/<id>/, without a window, printing
//       "progress <0..1> <text>" lines; a game already built from the same
//       inputs by this version is kept
//   slingshot-builder --data=<dir> --play=<id> [game options...]
//       starts <dir>/games/<id>/slingshot-<id> with the other options, its saves
//       in <dir>/saves/<id> (copied once from the system's place) and its output
//       in <dir>/logs/<id>.log
#pragma once

namespace builder {

// True if the command line asks for one of the modes above.
bool is_recomplauncher_mode(int argc, char** argv);

// Runs it with QCoreApplication::arguments(); returns the exit code.
int run_recomplauncher_mode();

}  // namespace builder
