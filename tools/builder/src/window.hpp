// SPDX-License-Identifier: GPL-3.0-or-later
// The builder's window: drop an APK, press Build & Install.
#pragma once

#include <QPointer>
#include <QWidget>

#include "catalog.hpp"
#include "job.hpp"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace builder {

class DropArea;

class Window : public QWidget {
    Q_OBJECT
public:
    Window();
    void open_apk(const QString& path);

protected:
    bool event(QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void choose_apk();
    void start(bool android, bool install);
    void on_finished(bool ok, const QString& message, const QString& path);
    void set_building(bool building);
    void fit();
    void pin_size();
    void update_cache_size();
    void clear_cache();
    void show_about();

    QVector<Game> games_;
    QString apk_;
    int game_ = -1;  // index in games_, -1: none
    QPointer<Job> job_;
    QString result_path_;
    bool result_installed_ = false;
    bool close_when_stopped_ = false;  // closed during a build: close once it stopped

    QStackedWidget* drop_pages_;
    DropArea* drop_;
    QLabel *game_icon_, *game_title_, *game_detail_, *message_;
    QToolButton* advanced_;
    QWidget* advanced_panel_;
    QVBoxLayout* mods_layout_;
    QScrollArea* mods_scroll_;
    QVector<QCheckBox*> mod_boxes_;
    QWidget* progress_panel_;
    QProgressBar* progress_;
    QLabel *status_, *eta_;
    QPushButton *cancel_, *play_, *open_folder_, *clear_cache_, *details_;
    QToolButton* build_;  // Build & Install, the other modes in its menu
    QPlainTextEdit* log_;
};

}  // namespace builder
