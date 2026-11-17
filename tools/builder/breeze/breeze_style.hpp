// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QApplication;

namespace builder {

// Uses KDE's Breeze style with Breeze colours. Plasma's own colour scheme
// (kdeglobals) is used when there is one, otherwise Breeze Light or Dark,
// following the system's light/dark setting.
void use_breeze(QApplication& app);

}
