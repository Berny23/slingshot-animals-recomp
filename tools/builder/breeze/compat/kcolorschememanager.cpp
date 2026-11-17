// SPDX-License-Identifier: GPL-3.0-or-later
// The one KColorSchemeManager function that KColorScheme uses.
#include "kcolorschememanager_p.h"

#include <QGuiApplication>
#include <QStyleHints>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif

KColorSchemeManagerPrivate::ContrastPreference KColorSchemeManagerPrivate::contrastPreference() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    if (qGuiApp->styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast)
        return HighContrast;
#endif
    return NoPreference;
}
