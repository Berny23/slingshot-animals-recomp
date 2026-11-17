// SPDX-License-Identifier: GPL-3.0-or-later
// The part of KDE's KConfig that Breeze and KColorScheme use: reading KDE
// configuration files (kdeglobals, breezerc, colour schemes). Nothing is written.
#pragma once

#include <QColor>
#include <QExplicitlySharedDataPointer>
#include <QHash>
#include <QObject>
#include <QSharedData>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStringList>

class KConfigGroup;

class KConfig {
public:
    enum OpenFlag { IncludeGlobals = 0x01, CascadeConfig = 0x02, SimpleConfig = 0x00, NoCascade = IncludeGlobals,
                    NoGlobals = CascadeConfig, FullConfig = IncludeGlobals | CascadeConfig };
    Q_DECLARE_FLAGS(OpenFlags, OpenFlag)

    explicit KConfig(const QString& file = QString(), OpenFlags flags = FullConfig,
                     QStandardPaths::StandardLocation type = QStandardPaths::GenericConfigLocation);
    virtual ~KConfig() = default;
    QString name() const { return name_; }
    KConfigGroup group(const QString& name) const;
    bool hasGroup(const QString& name) const { return groups_.contains(name); }
    void reparseConfiguration();

    // group -> key -> value; nested groups are joined with '\x1d'
    QHash<QString, QHash<QString, QString>> groups_;

private:
    void parse(const QString& path);
    QString name_;
    OpenFlags flags_;
    QStandardPaths::StandardLocation type_;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(KConfig::OpenFlags)

class KSharedConfig : public KConfig, public QSharedData {
public:
    using Ptr = QExplicitlySharedDataPointer<KSharedConfig>;
    static Ptr openConfig(const QString& file = QString(), OpenFlags flags = FullConfig,
                          QStandardPaths::StandardLocation type = QStandardPaths::GenericConfigLocation);

private:
    using KConfig::KConfig;
};
using KSharedConfigPtr = KSharedConfig::Ptr;

class KConfigGroup {
public:
    KConfigGroup() = default;
    KConfigGroup(const KConfig* config, const QString& name) : config_(config), name_(name) {}
    KConfigGroup(const KSharedConfigPtr& config, const QString& name) : KConfigGroup(config.data(), name) {}
    KConfigGroup(const KConfigGroup* parent, const QString& name)
        : config_(parent->config_), name_(parent->name_ + QChar(0x1d) + name) {}
    bool exists() const { return config_ && config_->groups_.contains(name_); }
    bool hasKey(const char* key) const { return value(key) != nullptr; }
    QString name() const { return name_; }
    KConfigGroup group(const QString& name) const { return KConfigGroup(this, name); }

    QString readEntry(const char* key, const char* def) const { return readEntry(key, QString::fromUtf8(def)); }
    QString readEntry(const char* key, const QString& def) const {
        const QString* v = value(key);
        return v ? *v : def;
    }
    QStringList readEntry(const char* key, const QStringList& def) const {
        const QString* v = value(key);
        return v ? v->split(QLatin1Char(','), Qt::SkipEmptyParts) : def;
    }
    QColor readEntry(const char* key, const QColor& def) const;
    bool readEntry(const char* key, bool def) const;
    int readEntry(const char* key, int def) const {
        const QString* v = value(key);
        bool ok = false;
        const int n = v ? v->toInt(&ok) : 0;
        return ok ? n : def;
    }
    double readEntry(const char* key, double def) const {
        const QString* v = value(key);
        bool ok = false;
        const double n = v ? v->toDouble(&ok) : 0;
        return ok ? n : def;
    }
    float readEntry(const char* key, float def) const { return float(readEntry(key, double(def))); }

private:
    const QString* value(const char* key) const;
    const KConfig* config_ = nullptr;
    QString name_;
};

// Reports changes of a configuration; files are not watched here.
class KConfigWatcher : public QObject {
    Q_OBJECT
public:
    using Ptr = QSharedPointer<KConfigWatcher>;
    static Ptr create(const KSharedConfig::Ptr& config) { return Ptr(new KConfigWatcher(config)); }
    KSharedConfig::Ptr config() const { return config_; }

Q_SIGNALS:
    void configChanged(const KConfigGroup& group, const QByteArrayList& names);

private:
    explicit KConfigWatcher(const KSharedConfig::Ptr& config) : config_(config) {}
    KSharedConfig::Ptr config_;
};
