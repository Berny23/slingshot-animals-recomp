// SPDX-License-Identifier: GPL-3.0-or-later
#include "breeze_style.hpp"

#include <KColorScheme>
#include <KConfigGroup>
#include <QApplication>
#include <QFont>
#include <QStyleHints>

#include "breezestyle.h"

// Q_INIT_RESOURCE must be used outside a namespace.
static void init_breeze_resources() { Q_INIT_RESOURCE(breeze); }

namespace builder {
namespace {

QString scheme_path() {
    // Plasma writes the active scheme's colours into kdeglobals.
    if (KSharedConfig::openConfig()->hasGroup(QStringLiteral("Colors:Window"))) return QString();
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    return dark ? QStringLiteral(":/breeze/BreezeDark.colors") : QStringLiteral(":/breeze/BreezeLight.colors");
}

void apply_palette(QApplication& app) {
    const QString path = scheme_path();
    app.setProperty("KDE_COLOR_SCHEME_PATH", path);
    KSharedConfigPtr config = KSharedConfig::openConfig(path);
    app.setPalette(KColorScheme::createApplicationPalette(config));
}

// Plasma's fonts, when the platform theme does not set them.
void apply_fonts(QApplication& app) {
    const KConfigGroup general(KSharedConfig::openConfig(), QStringLiteral("General"));
    QFont font;
    if (const QString f = general.readEntry("font", QString()); !f.isEmpty() && font.fromString(f))
        app.setFont(font);
    if (const QString f = general.readEntry("menuFont", QString()); !f.isEmpty() && font.fromString(f))
        app.setFont(font, "QMenu");
}

}

void use_breeze(QApplication& app) {
    init_breeze_resources();
    apply_fonts(app);
    apply_palette(app);
    app.setStyle(new Breeze::Style);
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
        apply_palette(app);
    });
}

}
