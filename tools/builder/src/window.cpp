// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <thread>

#include "apk_match.hpp"
#include "paths.hpp"

namespace builder {

// The empty drop target: a dashed frame; a click opens the file dialog.
class DropArea : public QWidget {
public:
    explicit DropArea(QWidget* parent) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(150);
        setAccessibleName("Choose an APK");
        setFocusPolicy(Qt::StrongFocus);
    }
    std::function<void()> clicked;
    void set_highlight(bool on) {
        highlight_ = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette& pal = palette();
        QColor line = highlight_ ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid);
        QColor fill = pal.color(QPalette::Base);
        if (highlight_) fill = pal.color(QPalette::Highlight).lighter(190);
        p.setPen(QPen(line, 2, Qt::DashLine));
        p.setBrush(fill);
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 10, 10);
        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 1.25);
        big.setBold(true);
        p.setPen(pal.color(QPalette::Text));
        p.setFont(big);
        const QRect r = rect();
        p.drawText(QRect(r.x(), r.y(), r.width(), r.height() / 2 + 4), Qt::AlignHCenter | Qt::AlignBottom,
                   "Drop a game's APK here");
        p.setFont(font());
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRect(r.x(), r.y() + r.height() / 2 + 10, r.width(), r.height() / 2 - 10),
                   Qt::AlignHCenter | Qt::AlignTop, "or click to choose a file");
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (clicked) clicked();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Space || e->key() == Qt::Key_Return) && clicked) clicked();
        else QWidget::keyPressEvent(e);
    }

private:
    bool highlight_ = false;
};

namespace {

QString eta_text(qint64 s) {
    if (s < 0) return {};
    if (s < 60) return "less than a minute left";
    if (s < 90 * 60) return QString("about %1 min left").arg((s + 30) / 60);
    return QString("about %1 h %2 min left").arg(s / 3600).arg((s % 3600 + 30) / 60);
}

QLabel* small_label(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    l->setFont(f);
    l->setWordWrap(true);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

}  // namespace

Window::Window() : games_(load_games(source_dir())) {
    setWindowTitle("Slingshot Builder");
    setAcceptDrops(true);
    setMinimumWidth(520);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    // Header
    auto* header = new QHBoxLayout;
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon(":/builder.svg").pixmap(40, 40));
    header->addWidget(logo);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel("Slingshot Builder", this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.4);
    tf.setBold(true);
    title->setFont(tf);
    titles->addWidget(title);
    titles->addWidget(small_label("Turns an Angry Birds APK into a game for this computer.", this));
    header->addLayout(titles, 1);
    root->addLayout(header);

    // Drop area / detected game
    drop_pages_ = new QStackedWidget(this);
    drop_ = new DropArea(drop_pages_);
    drop_->clicked = [this] { choose_apk(); };
    drop_pages_->addWidget(drop_);
    auto* card = new QWidget(drop_pages_);
    auto* card_layout = new QHBoxLayout(card);
    card_layout->setContentsMargins(12, 12, 12, 12);
    card_layout->setSpacing(14);
    game_icon_ = new QLabel(card);
    game_icon_->setFixedSize(72, 72);
    game_icon_->setScaledContents(true);
    card_layout->addWidget(game_icon_);
    auto* card_text = new QVBoxLayout;
    card_text->setSpacing(2);
    game_title_ = new QLabel(card);
    QFont gf = game_title_->font();
    gf.setPointSizeF(gf.pointSizeF() * 1.25);
    gf.setBold(true);
    game_title_->setFont(gf);
    card_text->addStretch();
    card_text->addWidget(game_title_);
    game_detail_ = small_label("", card);
    card_text->addWidget(game_detail_);
    auto* other = new QPushButton("Choose another APK…", card);
    other->setFlat(true);
    other->setCursor(Qt::PointingHandCursor);
    other->setStyleSheet("QPushButton { text-align: left; padding: 0; color: palette(link); }");
    connect(other, &QPushButton::clicked, this, &Window::choose_apk);
    card_text->addWidget(other, 0, Qt::AlignLeft);
    card_text->addStretch();
    card_layout->addLayout(card_text, 1);
    card->setMinimumHeight(150);
    card->setAutoFillBackground(true);
    card->setBackgroundRole(QPalette::Base);
    drop_pages_->addWidget(card);
    root->addWidget(drop_pages_);

    message_ = new QLabel(this);
    message_->setWordWrap(true);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_->hide();
    root->addWidget(message_);

    // Advanced: mods and the Android note
    advanced_ = new QToolButton(this);
    advanced_->setText("Advanced");
    advanced_->setCheckable(true);
    advanced_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced_->setArrowType(Qt::RightArrow);
    advanced_->setAutoRaise(true);
    advanced_->hide();
    root->addWidget(advanced_, 0, Qt::AlignLeft);
    advanced_panel_ = new QWidget(this);
    auto* adv = new QVBoxLayout(advanced_panel_);
    adv->setContentsMargins(8, 0, 0, 0);
    auto* mods_title = new QLabel("<b>Mods</b>", advanced_panel_);
    adv->addWidget(mods_title);
    auto* mods_widget = new QWidget;
    mods_layout_ = new QVBoxLayout(mods_widget);
    mods_layout_->setContentsMargins(0, 0, 0, 0);
    mods_layout_->setSpacing(6);
    mods_scroll_ = new QScrollArea(advanced_panel_);
    mods_scroll_->setWidget(mods_widget);
    mods_scroll_->setWidgetResizable(true);
    mods_scroll_->setFrameShape(QFrame::NoFrame);
    mods_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    adv->addWidget(mods_scroll_);
    adv->addWidget(small_label("<b>Android:</b> “Build for Android” installs the game with adb on a phone or tablet "
                               "connected by USB with USB debugging turned on. Without a device, the APK is saved "
                               "to your Downloads folder.",
                               advanced_panel_));
    adv->addWidget(small_label("<b>Nintendo Switch:</b> needs a console that runs homebrew. Copy the .nro file "
                               "into the folder switch on the SD card and start it from the Homebrew Menu.",
                               advanced_panel_));
    advanced_panel_->hide();
    root->addWidget(advanced_panel_);
    connect(advanced_, &QToolButton::toggled, this, [this](bool on) {
        advanced_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        advanced_panel_->setVisible(on);
        fit();
    });

    // Progress
    progress_panel_ = new QWidget(this);
    auto* prog = new QVBoxLayout(progress_panel_);
    prog->setContentsMargins(0, 0, 0, 0);
    auto* bar_row = new QHBoxLayout;
    progress_ = new QProgressBar(progress_panel_);
    progress_->setRange(0, 1000);
    progress_->setFormat("%p %");
    progress_->setTextVisible(true);
    bar_row->addWidget(progress_, 1);
    cancel_ = new QPushButton("Cancel", progress_panel_);
    bar_row->addWidget(cancel_);
    prog->addLayout(bar_row);
    auto* status_row = new QHBoxLayout;
    status_ = small_label("", progress_panel_);
    eta_ = small_label("", progress_panel_);
    eta_->setAlignment(Qt::AlignRight);
    status_row->addWidget(status_, 1);
    status_row->addWidget(eta_);
    prog->addLayout(status_row);
    progress_panel_->hide();
    root->addWidget(progress_panel_);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (job_) job_->cancel();
        cancel_->setEnabled(false);
        status_->setText("Cancelling…");
    });

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMinimumHeight(160);
    log_->hide();
    root->addWidget(log_, 1);

    // Bottom row
    auto* bottom = new QHBoxLayout;
    auto* about = new QPushButton("About", this);
    about->setFlat(true);
    connect(about, &QPushButton::clicked, this, &Window::show_about);
    bottom->addWidget(about);
    clear_cache_ = new QPushButton("Clear cache", this);
    clear_cache_->setFlat(true);
    connect(clear_cache_, &QPushButton::clicked, this, &Window::clear_cache);
    bottom->addWidget(clear_cache_);
    details_ = new QPushButton("Show details", this);
    details_->setFlat(true);
    details_->hide();
    connect(details_, &QPushButton::clicked, this, [this] {
        log_->setVisible(!log_->isVisible());
        details_->setText(log_->isVisible() ? "Hide details" : "Show details");
        fit();
    });
    bottom->addWidget(details_);
    bottom->addStretch();
    open_folder_ = new QPushButton("Show in folder", this);
    open_folder_->hide();
    connect(open_folder_, &QPushButton::clicked, this, [this] {
        const QFileInfo info(result_path_);
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() && !result_path_.endsWith(".app")
                                                          ? result_path_
                                                          : info.path()));
    });
    bottom->addWidget(open_folder_);
    play_ = new QPushButton("Play", this);
    play_->hide();
    connect(play_, &QPushButton::clicked, this, [this] {
        if (game_ < 0) return;
#ifdef Q_OS_MACOS
        QProcess::startDetached("open", {result_path_});
#elif defined(Q_OS_WIN)
        QProcess::startDetached(result_path_ + "/slingshot-" + games_[game_].id + ".exe", {}, result_path_);
#else
        QProcess::startDetached(result_path_ + "/slingshot-" + games_[game_].id, {}, result_path_);
#endif
    });
    bottom->addWidget(play_);

    // Build & Install: a split button with the other modes in its menu.
    build_ = new QToolButton(this);
    build_->setText("Build && Install");
    build_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    build_->setPopupMode(QToolButton::MenuButtonPopup);
    build_->setEnabled(false);
    build_->setMinimumWidth(150);
    build_->setMinimumHeight(play_->sizeHint().height());
    connect(build_, &QToolButton::clicked, this, [this] { start(false, true); });
    auto* menu = new QMenu(build_);
    menu->addAction("Build only (save the game to a folder)…", this, [this] { start(false, false); });
    menu->addSeparator();
    menu->addAction("Build for Android (install on a connected device)", this, [this] { start(true, true); });
    menu->addAction("Build Android APK only…", this, [this] { start(true, false); });
    menu->addSeparator();
    menu->addAction("Build for Nintendo Switch (homebrew)…", this, [this] { start(false, false, true); });
    build_->setMenu(menu);
    bottom->addWidget(build_);
    root->addLayout(bottom);

    update_cache_size();
    if (games_.isEmpty()) {
        message_->setText("The game sources are missing next to the builder: " +
                          QDir::toNativeSeparators(source_dir()));
        message_->show();
    }
}

void Window::dragEnterEvent(QDragEnterEvent* e) {
    if (job_ || !e->mimeData()->hasUrls()) return;
    for (const QUrl& url : e->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(".apk", Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            drop_->set_highlight(true);
            return;
        }
    }
}

void Window::dropEvent(QDropEvent* e) {
    drop_->set_highlight(false);
    for (const QUrl& url : e->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(".apk", Qt::CaseInsensitive)) {
            open_apk(url.toLocalFile());
            return;
        }
    }
}

void Window::closeEvent(QCloseEvent* e) {
    if (job_) {
        if (QMessageBox::question(this, "Cancel the build?", "A build is running. Cancel it and quit?") ==
            QMessageBox::Yes) {
            close_when_stopped_ = true;
            job_->cancel();
            status_->setText("Cancelling…");
        }
        e->ignore();  // the build's thread stops first
        return;
    }
    e->accept();
}

void Window::choose_apk() {
    if (job_) return;
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getOpenFileName(this, "Choose a game's APK", start, "Android packages (*.apk)");
    if (!path.isEmpty()) open_apk(path);
}

void Window::open_apk(const QString& path) {
    game_ = -1;
    apk_.clear();
    extra_.clear();
    extra_asked_ = false;
    build_->setEnabled(false);
    advanced_->hide();
    advanced_panel_->hide();
    advanced_->setChecked(false);
    play_->hide();
    open_folder_->hide();
    progress_panel_->hide();

    auto fail = [&](const QString& text) {
        drop_pages_->setCurrentIndex(0);
        message_->setText(text);
        message_->show();
        fit();
    };
    QString error;
    const auto game = identify_apk(path, games_, error);
    if (!game) return fail(error);
    for (int i = 0; i < games_.size(); ++i)
        if (games_[i].id == game->id) game_ = i;
    const Game& g = games_[game_];
    apk_ = path;

    QPixmap icon;
    icon.loadFromData(apk_icon(path), "PNG");
    game_icon_->setPixmap(icon);
    game_title_->setText(g.title);
    game_detail_->setText("Version " + g.version);
    game_detail_->setToolTip({});
    game_title_->setToolTip(QDir::toNativeSeparators(path));
    drop_pages_->setCurrentIndex(1);
    message_->hide();

    // This game's mods
    qDeleteAll(mod_boxes_);
    mod_boxes_.clear();
    while (QLayoutItem* item = mods_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const Mod& m : load_mods(source_dir(), g.id)) {
        auto* box = new QCheckBox(m.name, mods_layout_->parentWidget());
        box->setChecked(m.on_by_default);
        box->setProperty("default", m.on_by_default);
        box->setToolTip(m.description);
        box->setProperty("mod", m.name);
        mods_layout_->addWidget(box);
        auto* desc = small_label(m.description, mods_layout_->parentWidget());
        desc->setContentsMargins(24, 0, 0, 4);
        mods_layout_->addWidget(desc);
        mod_boxes_.push_back(box);
    }
    mods_scroll_->widget()->adjustSize();
    mods_scroll_->setFixedHeight(std::min(mods_scroll_->widget()->sizeHint().height(), 260));
    advanced_->show();
    build_->setEnabled(true);
    build_->setFocus();
    fit();
}

// Star Wars II: the content it once downloaded, optional. False: cancelled.
bool Window::ask_extra() {
    if (games_[game_].id != "starwars2" || extra_asked_) return true;
    QMessageBox box(QMessageBox::Question, "Add extra data?",
                    "Do you have the game's extra data (the <i>assets_service</i> folder)? "
                    "Add it as a ZIP file or folder.<br><br>"
                    "Without it, there is no music and some backgrounds are simplified.",
                    QMessageBox::NoButton, this);
    box.setTextFormat(Qt::RichText);
    auto* zip = box.addButton("Choose ZIP…", QMessageBox::AcceptRole);
    auto* folder = box.addButton("Choose folder…", QMessageBox::AcceptRole);
    auto* without = box.addButton("Build without", QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(zip);
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    for (;;) {
        box.exec();
        QString path;
        if (box.clickedButton() == zip)
            path = QFileDialog::getOpenFileName(this, "Choose the extra data", start, "ZIP archives (*.zip)");
        else if (box.clickedButton() == folder)
            path = QFileDialog::getExistingDirectory(this, "Choose the extra data folder", start);
        else if (box.clickedButton() != without)
            return false;
        if (box.clickedButton() != without && path.isEmpty()) continue;  // back to the question
        extra_ = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
        extra_asked_ = true;
        const Game& g = games_[game_];
        game_detail_->setText("Version " + g.version +
                              (extra_.isEmpty() ? QString(", without extra data")
                                                : ", extra data: " + QFileInfo(extra_).fileName()));
        game_detail_->setToolTip(QDir::toNativeSeparators(extra_));
        return true;
    }
}

void Window::start(bool android, bool install, bool nintendo_switch) {
    if (game_ < 0 || job_) return;
    if (!ask_extra()) return;
    Request r;
    r.apk = QFileInfo(apk_).absoluteFilePath();
    r.game = games_[game_];
    r.extra = extra_;
    r.android = android;
    r.nintendo_switch = nintendo_switch;
    r.install = install;
    r.default_mods = true;
    for (QCheckBox* box : mod_boxes_) {
        if (box->isChecked()) r.mods << box->property("mod").toString();
        if (box->isChecked() != box->property("default").toBool()) r.default_mods = false;
    }
    if (!install) {
        if (nintendo_switch) {
            const QString suggestion = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) +
                                       "/slingshot-" + r.game.id + ".nro";
            r.output = QFileDialog::getSaveFileName(this, "Save the Nintendo Switch homebrew", suggestion,
                                                    "Switch homebrew (*.nro)");
        } else if (android) {
            const QString suggestion =
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + r.game.title + ".apk";
            r.output = QFileDialog::getSaveFileName(this, "Save the Android package", suggestion,
                                                    "Android packages (*.apk)");
        } else {
            r.output = QFileDialog::getExistingDirectory(this, "Choose where to save the game",
                                                         QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
        }
        if (r.output.isEmpty()) return;
    }
    result_installed_ = install && !android;
    job_ = new Job(r, this);
    connect(job_, &Job::progress, this, [this](double fraction, const QString& status, qint64 eta) {
        progress_->setValue(int(fraction * 1000));
        status_->setText(status);
        eta_->setText(eta_text(eta));
    });
    connect(job_, &Job::log_line, log_, &QPlainTextEdit::appendPlainText);
    connect(job_, &Job::finished, this, &Window::on_finished);
    log_->clear();
    message_->hide();
    play_->hide();
    open_folder_->hide();
    progress_->setValue(0);
    status_->setText("Starting");
    eta_->clear();
    set_building(true);
    job_->start();
}

void Window::on_finished(bool ok, const QString& message, const QString& path) {
    if (job_) job_->deleteLater();
    job_ = nullptr;
    if (close_when_stopped_) {
        close();
        return;
    }
    set_building(false);
    result_path_ = path;
    progress_panel_->setVisible(ok);
    if (ok) {
        progress_->setValue(1000);
        status_->setText("Done");
        eta_->clear();
    }
    message_->setText(message);
    message_->show();
    play_->setVisible(ok && result_installed_);
    open_folder_->setVisible(ok && !path.isEmpty());
    if (!ok) details_->show();
    update_cache_size();
    QApplication::alert(this);
    fit();
}

void Window::set_building(bool building) {
    progress_panel_->setVisible(building || progress_panel_->isVisible());
    cancel_->setVisible(building);
    cancel_->setEnabled(building);
    build_->setEnabled(!building && game_ >= 0);
    drop_pages_->setEnabled(!building);
    clear_cache_->setEnabled(!building);
    advanced_panel_->setEnabled(!building);
    details_->setVisible(true);
    fit();
}

// Resizes the window to its content and pins the size with limits: the window manager may
// restore an older size (e.g. when a file dialog closes), and Qt sets no minimum height itself
// for layouts with word-wrapped labels. With the log shown, the window can grow.
void Window::fit() {
    layout()->activate();
    adjustSize();
    pin_size();
}

void Window::pin_size() {
    QLayout* l = layout();
    l->activate();
    setMinimumWidth(std::max(520, l->totalMinimumSize().width()));
    const int w = std::max(width(), minimumWidth());
    const int h = l->hasHeightForWidth() ? l->totalHeightForWidth(w) : l->totalSizeHint().height();
    const int min = log_->isVisible() ? h - log_->sizeHint().height() + log_->minimumHeight() : h;
    const int max = log_->isVisible() ? QWIDGETSIZE_MAX : h;
    if (minimumHeight() == min && maximumHeight() == max) return;
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(min);
    setMaximumHeight(max);
}

bool Window::event(QEvent* e) {
    const bool handled = QWidget::event(e);
    if (e->type() == QEvent::LayoutRequest) pin_size();  // e.g. a button's text changed
    return handled;
}

void Window::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (e->size().width() != e->oldSize().width()) pin_size();  // wrapped labels change their height
    // A size from the window manager is applied even outside the limits: correct it.
    const QSize min = minimumSize(), max = maximumSize();
    if (width() < min.width() || height() < min.height() || height() > max.height()) {
        QTimer::singleShot(0, this, [this] {
            resize(std::max(width(), minimumWidth()), std::clamp(height(), minimumHeight(), maximumHeight()));
        });
    }
}

void Window::update_cache_size() {
    clear_cache_->setText("Clear cache");
    QPointer<Window> self(this);
    std::thread([self] {
        const QString unpacked = unpacked_app_dir();
        const qint64 size = tree_size(cache_dir()) + (unpacked.isEmpty() ? 0 : tree_size(QFileInfo(unpacked).path()));
        QMetaObject::invokeMethod(qApp, [self, size] {
            if (!self) return;
            self->clear_cache_->setText("Clear cache (" + format_size(size) + ")");
            self->clear_cache_->setProperty("bytes", size);
        });
    }).detach();
}

void Window::clear_cache() {
    if (job_) return;
    const qint64 size = clear_cache_->property("bytes").toLongLong();
    const auto answer = QMessageBox::question(
        this, "Clear the cache?",
        "Delete the downloaded build tools and build files (" + format_size(size) + ")?\n\n" +
            (unpacked_app_dir().isEmpty() ? QString()
                                          : QString("The unpacked builder is deleted too once it is closed; "
                                                    "its next start unpacks it again.\n\n")) +
            "Installed games stay. The next build downloads and compiles everything again, so it takes longer.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QDir(cache_dir()).removeRecursively();
    remove_unpacked_app();
    QApplication::restoreOverrideCursor();
    update_cache_size();
}

void Window::show_about() {
    QDialog dialog(this);
    dialog.setWindowTitle("About Slingshot Builder");
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QLabel(&dialog);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->setText(
        "<h3>slingshot-animals-recomp</h3>"
        "<p>Unofficial static recompilation of Rovio's Android <i>Angry Birds</i> games, "
        "running natively without an emulator.</p>"
        "<p><a href='https://github.com/Berny23/slingshot-animals-recomp'>"
        "github.com/Berny23/slingshot-animals-recomp</a></p>"
        "<p>Author: Berny23<br>Version: " SLINGSHOT_VERSION "<br>"
        "License: GNU General Public License 3.0 or later</p>"
        "<p>The games' files come from the APK you provide; nothing of the original games is included.</p>"
        "<p><b>Libraries and tools</b></p>"
        "<table cellspacing='0' cellpadding='1'>"
        "<tr><td>Qt 6</td><td>&nbsp;LGPL 3.0</td></tr>"
        "<tr><td>KDE Breeze style</td><td>&nbsp;GPL 2.0 or later</td></tr>"
        "<tr><td>KDE KColorScheme, KGuiAddons</td><td>&nbsp;LGPL 2.0 or later</td></tr>"
        "<tr><td>SDL 3</td><td>&nbsp;zlib License</td></tr>"
        "<tr><td>FreeType</td><td>&nbsp;FreeType License</td></tr>"
        "<tr><td>HarfBuzz</td><td>&nbsp;MIT (Old MIT)</td></tr>"
        "<tr><td>XZ Utils (liblzma)</td><td>&nbsp;0BSD</td></tr>"
        "<tr><td>zlib</td><td>&nbsp;zlib License</td></tr>"
        "<tr><td>Capstone</td><td>&nbsp;BSD 3-Clause</td></tr>"
        "<tr><td>fontconfig (Linux)</td><td>&nbsp;MIT-style</td></tr>"
        "<tr><td>Zig (Linux, if no compiler is installed)</td><td>&nbsp;MIT</td></tr>"
        "<tr><td>LLVM libc++ (with Zig)</td><td>&nbsp;Apache 2.0 with LLVM exception</td></tr>"
        "<tr><td>CMake</td><td>&nbsp;BSD 3-Clause</td></tr>"
        "<tr><td>Ninja</td><td>&nbsp;Apache 2.0</td></tr>"
        "<tr><td>llvm-mingw (Windows)</td><td>&nbsp;Apache 2.0 with LLVM exception, mingw-w64 licenses</td></tr>"
        "<tr><td>Android NDK, SDK tools</td><td>&nbsp;Android Software Development Kit License</td></tr>"
        "<tr><td>Eclipse Temurin (Java)</td><td>&nbsp;GPL 2.0 with Classpath Exception</td></tr>"
        "<tr><td>devkitA64 (GCC, binutils, newlib)</td><td>&nbsp;GPL 3.0 with runtime exceptions, newlib licenses</td></tr>"
        "<tr><td>libnx, switch-tools</td><td>&nbsp;ISC</td></tr>"
        "<tr><td>SDL 2 (Switch)</td><td>&nbsp;zlib License</td></tr>"
        "<tr><td>Mesa, libdrm (Switch)</td><td>&nbsp;MIT</td></tr>"
        "</table>"
        "<p>Build tools are downloaded from their official sources when needed and checked "
        "against pinned SHA-256 hashes; devkitPro's packages against the hashes in devkitPro's "
        "package lists.</p>");
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(460);
    dialog.exec();
}

}  // namespace builder
