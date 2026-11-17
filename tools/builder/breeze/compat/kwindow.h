// SPDX-License-Identifier: GPL-3.0-or-later
// The window-system parts of KDE Frameworks that Breeze calls. Without KWin
// they have no effect: no blur behind menus, no compositor-drawn shadows.
#pragma once

#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMargins>
#include <QPalette>
#include <QRegion>
#include <QSharedPointer>
#include <QWindow>

namespace KWindowSystem {
inline bool isPlatformX11() { return QGuiApplication::platformName() == QLatin1String("xcb"); }
inline bool isPlatformWayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }
}

namespace KWindowEffects {
inline void enableBlurBehind(QWindow*, bool = true, const QRegion& = QRegion()) {}
}

class KWindowShadowTile {
public:
    using Ptr = QSharedPointer<KWindowShadowTile>;
    void setImage(const QImage& image) { image_ = image; }
    QImage image() const { return image_; }

private:
    QImage image_;
};

class KWindowShadow : public QObject {
public:
    explicit KWindowShadow(QObject* parent = nullptr) : QObject(parent) {}
    void setTopTile(const KWindowShadowTile::Ptr&) {}
    void setTopRightTile(const KWindowShadowTile::Ptr&) {}
    void setRightTile(const KWindowShadowTile::Ptr&) {}
    void setBottomRightTile(const KWindowShadowTile::Ptr&) {}
    void setBottomTile(const KWindowShadowTile::Ptr&) {}
    void setBottomLeftTile(const KWindowShadowTile::Ptr&) {}
    void setLeftTile(const KWindowShadowTile::Ptr&) {}
    void setTopLeftTile(const KWindowShadowTile::Ptr&) {}
    void setPadding(const QMargins&) {}
    void setWindow(QWindow*) {}
    bool isCreated() const { return false; }
    bool create() { return false; }
    void destroy() {}
};

// Icons are not recoloured to the palette (no KIconLoader).
class KIconLoader {
public:
    static KIconLoader* global() {
        static KIconLoader loader;
        return &loader;
    }
    QPalette customPalette() const { return palette_; }
    void setCustomPalette(const QPalette& palette) { palette_ = palette; }
    void resetPalette() { palette_ = QPalette(); }

private:
    QPalette palette_;
};
