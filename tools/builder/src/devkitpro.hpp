// SPDX-License-Identifier: GPL-3.0-or-later
// devkitPro for Nintendo Switch builds: the newest packages from devkitPro's
// pacman repositories, each checked against the SHA-256 the repository lists.
#pragma once

#include <QString>

#include "downloads.hpp"

namespace builder {

// <cache>/tools/devkitpro, brought up to the repositories' newest packages
// (unpacked with `cmake`). Offline, the packages there already are used.
// Returns an empty string with `error` set on failure.
QString fetch_devkitpro(const QString& cmake, const FetchContext& ctx, QString& error);

// Whether this computer has devkitPro packages at all.
bool devkitpro_available();

}  // namespace builder
