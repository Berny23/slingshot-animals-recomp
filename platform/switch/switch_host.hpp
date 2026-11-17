// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch parts of the host (libnx).
#pragma once

#include <string>

namespace slingshot::nx {

// romfs (the game files inside the NRO), stderr to nxlink when started by it,
// and the console's time zone for localtime().
void init();
void exit();
// The console's shared fonts as files (written once), for ScanFontProvider.
std::string font_dir();

}  // namespace slingshot::nx
