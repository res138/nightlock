#include "settingswindow.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <iterator>
#include <utility>

#include "appearancesettings.hpp"
#include "fonts.hpp"
#include "generalsettings.hpp"
#include "graphsettings.hpp"
#include "hotkeys.hpp"
#include "iconpackmanager.hpp"
#include "touchid.hpp"
#include "updatemanager.hpp"
#include "vaultservice.hpp"
#include "widgets/applicationiconpicker.hpp"
#include "widgets/nlmenu.hpp"

namespace {

constexpr int kNavWidth = 200;

// Theme-following menu icon: light theme as authored, dark theme
// lightens the glyph, and Selected renders white for the accent row.
QIcon navIcon(const QString& name) {
    return appearancesettings::themedMenuIcon(name);
}

// Pill switch for boolean settings: black when on, with the knob
// gliding between the ends.
class ToggleSwitch : public QAbstractButton {
public:
    explicit ToggleSwitch(bool on, QWidget* parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setChecked(on);
        pos_ = on ? 1.0 : 0.0;
        setCursor(Qt::PointingHandCursor);
        setFixedSize(40, 22);

        auto* animation = new QVariantAnimation(this);
        animation->setDuration(140);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            pos_ = value.toReal();
            update();
        });
        connect(this, &QAbstractButton::toggled, this, [this, animation](bool checked) {
            animation->stop();
            animation->setStartValue(pos_);
            animation->setEndValue(checked ? 1.0 : 0.0);
            animation->start();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        const QColor off = appearancesettings::palette().toggleOff;
        const QColor on = appearancesettings::accentColor();
        const auto lerp = [this](int a, int b) { return a + qRound((b - a) * pos_); };
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(lerp(off.red(), on.red()), lerp(off.green(), on.green()),
                                lerp(off.blue(), on.blue())));
        painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
        // The knob keeps contrast with the track on either end: white
        // over the gray off-track, accent-text over the accent.
        const QColor knobOff(Qt::white);
        const QColor knobOn = appearancesettings::accentTextColor();
        painter.setBrush(QColor(lerp(knobOff.red(), knobOn.red()),
                                lerp(knobOff.green(), knobOn.green()),
                                lerp(knobOff.blue(), knobOn.blue())));
        painter.drawEllipse(QRectF(3 + pos_ * (width() - 22), 3, 16, 16));
    }

private:
    qreal pos_;  // knob position, 0 = off .. 1 = on
};

// Closed dropdown drawn like the app's input fields; opening it pops
// the frosted NlMenu, so the picker matches every other menu in the
// app. Options may carry color dots (the accent picker); dotless
// menus mark the current option with a check instead.
class DropdownButton : public QAbstractButton {
public:
    explicit DropdownButton(QStringList options, int current, QWidget* parent = nullptr)
        : QAbstractButton(parent), options_(std::move(options)), current_(current) {
        setObjectName(QStringLiteral("settingsDropdown"));  // NIGHTLOCK_SETTINGS_DROPDOWN hook
        setCursor(Qt::PointingHandCursor);
        QFont dropdownFont = font();
        dropdownFont.setPixelSize(13);
        setFont(dropdownFont);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(this, &QAbstractButton::clicked, this, &DropdownButton::openMenu);
    }

    void setDots(QVector<QColor> dots) {
        dots_ = std::move(dots);
        updateGeometry();
        update();
    }

    // Per-option availability: disabled entries stay visible in the
    // menu (grayed out) but cannot be chosen.
    void setOptionsEnabled(QVector<bool> enabled) { enabled_ = std::move(enabled); }

    void setOnSelected(std::function<void(int)> onSelected) {
        onSelected_ = std::move(onSelected);
    }

    QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        int widest = 0;
        for (const QString& option : options_)
            widest = std::max(widest, metrics.horizontalAdvance(option));
        return QSize(textLeft() + widest + 8 + 12 + 10, 30);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& palette = appearancesettings::palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette.border, 1));
        painter.setBrush(underMouse() ? palette.inputHover : palette.input);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        if (!dots_.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(dots_[current_]);
            painter.drawEllipse(QRectF(12, height() / 2.0 - 4, 8, 8));
        }
        painter.setPen(palette.ink);
        painter.drawText(rect().adjusted(textLeft(), 0, -22, 0),
                         Qt::AlignVCenter | Qt::AlignLeft, options_[current_]);
        navIcon(QStringLiteral("chevron-down"))
            .paint(&painter, QRect(width() - 22, (height() - 12) / 2, 12, 12));
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    int textLeft() const { return dots_.isEmpty() ? 12 : 27; }

    void openMenu() {
        auto* menu = new NlMenu(this);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        for (int i = 0; i < options_.size(); ++i) {
            QAction* action = menu->addAction(options_[i], this, [this, i] {
                current_ = i;
                update();
                if (onSelected_)
                    onSelected_(i);
            });
            if (!dots_.isEmpty())
                action->setIcon(
                    appearancesettings::colorSwatchIcon(dots_[i]));
            else if (i == current_)
                action->setIcon(navIcon(QStringLiteral("check")));
            action->setEnabled(enabled_.value(i, true));
        }
        menu->popupAt(mapToGlobal(QPoint(0, height() + 4)));
    }

    QStringList options_;
    int current_;
    QVector<QColor> dots_;
    QVector<bool> enabled_;
    std::function<void(int)> onSelected_;
};

// The keycap on a Hotkeys row. Click arms recording: the next chord
// pressed becomes the binding, applied to the live shortcut through
// the hotkeys registry. Esc or losing focus cancels.
class HotkeyButton : public QPushButton {
public:
    explicit HotkeyButton(QString id, const QKeySequence& sequence, QWidget* parent = nullptr)
        : QPushButton(parent), id_(std::move(id)), sequence_(sequence) {
        setObjectName(QStringLiteral("settingsKeycap"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::ClickFocus);
        setText(sequence_.toString(QKeySequence::NativeText));
        connect(this, &QPushButton::clicked, this, [this] { setRecording(true); });
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (!recording_) {
            QPushButton::keyPressEvent(event);
            return;
        }
        const int key = event->key();
        if (key == Qt::Key_Escape) {
            setRecording(false);
            return;
        }
        // A lone modifier is half a chord — hold on for the real key.
        if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta)
            return;
        sequence_ = QKeySequence(event->keyCombination().toCombined());
        hotkeys::setSequence(id_, sequence_);
        setRecording(false);
    }

    void focusOutEvent(QFocusEvent* event) override {
        setRecording(false);
        QPushButton::focusOutEvent(event);
    }

private:
    void setRecording(bool recording) {
        if (recording_ == recording)
            return;
        recording_ = recording;
        setText(recording_ ? tr("Press keys…")
                           : sequence_.toString(QKeySequence::NativeText));
        setProperty("recording", recording_);
        style()->unpolish(this);
        style()->polish(this);
    }

    QString id_;
    QKeySequence sequence_;
    bool recording_ = false;
};

// Small numeric field bound to one NetGraph simulation knob; commits
// on editing finished, and the running graph re-layouts right away.
QLineEdit* numberField(const char* key) {
    auto* field = new QLineEdit(QString::number(graphsettings::value(QLatin1String(key))));
    field->setObjectName(QStringLiteral("settingsNumberField"));
    field->setFixedWidth(72);
    field->setAlignment(Qt::AlignCenter);
    auto* validator = new QDoubleValidator(0.0, 100000.0, 3, field);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(QLocale::c());  // dot decimals, matching toDouble()
    field->setValidator(validator);
    QObject::connect(field, &QLineEdit::editingFinished, field, [field, key] {
        graphsettings::setValue(QLatin1String(key), field->text().toDouble());
    });
    return field;
}

// A settings page is one column of cards with a stretch below; rows
// go into a card and read title + description left, control right.
QWidget* makePage(QVBoxLayout*& column) {
    auto* page = new QWidget;
    column = new QVBoxLayout(page);
    column->setContentsMargins(28, 24, 28, 28);
    column->setSpacing(14);
    return page;
}

QFrame* makeCard(QVBoxLayout*& rows) {
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    rows = new QVBoxLayout(card);
    rows->setContentsMargins(14, 2, 14, 2);
    rows->setSpacing(0);
    return card;
}

// Returns the description label (or nullptr) so pages with live text
// — the Database paths — can retarget it later.
QLabel* addRow(QVBoxLayout* rows, const QString& title, const QString& description,
               QWidget* control) {
    auto* row = new QFrame;
    row->setObjectName(QStringLiteral("settingsRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 11, 2, 11);
    layout->setSpacing(16);

    auto* text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(3);
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("settingsRowTitle"));
    text->addWidget(titleLabel);
    QLabel* descriptionLabel = nullptr;
    if (!description.isEmpty()) {
        descriptionLabel = new QLabel(description);
        descriptionLabel->setObjectName(QStringLiteral("settingsRowDesc"));
        descriptionLabel->setWordWrap(true);
        text->addWidget(descriptionLabel);
    }
    layout->addLayout(text, 1);
    if (control)
        layout->addWidget(control, 0, Qt::AlignVCenter);
    rows->addWidget(row);
    return descriptionLabel;
}

// The bare text button used on action rows ("Check for updates" and
// the Database page's file pickers).
QPushButton* inlineButton(const QString& title) {
    auto* button = new QPushButton(title);
    button->setObjectName(QStringLiteral("settingsInlineButton"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// The last row of a card drops its separator line.
void finishCard(QVBoxLayout* rows) {
    if (rows->count() == 0)
        return;
    if (auto* row = rows->itemAt(rows->count() - 1)->widget())
        row->setProperty("last", true);
}

QString platformTitle(const QString& platform) {
    if (platform == QLatin1String("windows"))
        return SettingsWindow::tr("Windows");
    if (platform == QLatin1String("macos"))
        return SettingsWindow::tr("macOS");
    if (platform == QLatin1String("linux"))
        return SettingsWindow::tr("Linux");
    if (platform == QLatin1String("cross-platform"))
        return SettingsWindow::tr("All platforms");
    return platform;
}

QString byteCount(qint64 bytes) {
    if (bytes <= 0)
        return {};
    return QLocale().formattedDataSize(bytes);
}

class IconPackCard final : public QFrame {
public:
    explicit IconPackCard(iconpacks::IconPackManager* manager,
                          const iconpacks::Pack& pack, QWidget* parent = nullptr)
        : QFrame(parent), manager_(manager), pack_(pack) {
        setObjectName(QStringLiteral("iconPackCard"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(16, 14, 16, 14);
        outer->setSpacing(10);

        auto* header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(16);

        auto* copy = new QVBoxLayout;
        copy->setContentsMargins(0, 0, 0, 0);
        copy->setSpacing(4);
        title_ = new QLabel;
        title_->setObjectName(QStringLiteral("iconPackTitle"));
        title_->setTextFormat(Qt::PlainText);
        metadata_ = new QLabel;
        metadata_->setObjectName(QStringLiteral("iconPackMetadata"));
        metadata_->setTextFormat(Qt::PlainText);
        metadata_->setWordWrap(true);
        description_ = new QLabel;
        description_->setObjectName(QStringLiteral("iconPackDescription"));
        description_->setTextFormat(Qt::PlainText);
        description_->setWordWrap(true);
        copy->addWidget(title_);
        copy->addWidget(metadata_);
        copy->addWidget(description_);
        header->addLayout(copy, 1);

        auto* actions = new QVBoxLayout;
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(7);
        actions->setAlignment(Qt::AlignRight | Qt::AlignTop);
        state_ = new QLabel;
        state_->setObjectName(QStringLiteral("iconPackStateBadge"));
        state_->setAlignment(Qt::AlignCenter);
        action_ = new QPushButton;
        action_->setObjectName(QStringLiteral("iconPackActionButton"));
        action_->setCursor(Qt::PointingHandCursor);
        action_->setMinimumWidth(88);
        actions->addWidget(state_, 0, Qt::AlignRight);
        actions->addWidget(action_, 0, Qt::AlignRight);
        header->addLayout(actions);
        outer->addLayout(header);

        progress_ = new QProgressBar;
        progress_->setObjectName(QStringLiteral("iconPackDownloadProgress"));
        progress_->setTextVisible(false);
        progress_->setFixedHeight(7);
        outer->addWidget(progress_);

        error_ = new QLabel;
        error_->setObjectName(QStringLiteral("iconPackError"));
        error_->setTextFormat(Qt::PlainText);
        error_->setWordWrap(true);
        outer->addWidget(error_);

        connect(action_, &QPushButton::clicked, this, [this] {
            switch (pack_.state) {
            case iconpacks::State::Available:
            case iconpacks::State::Failed:
                manager_->install(pack_.id);
                break;
            case iconpacks::State::Installed: {
                QMessageBox confirmation(
                    QMessageBox::Question, tr("Remove icon pack?"),
                    tr("Remove %1? Entries and folders that use this pack will "
                       "temporarily use their default icon until the pack is installed "
                       "again.")
                        .arg(pack_.title),
                    QMessageBox::NoButton, this);
                confirmation.setTextFormat(Qt::PlainText);
                auto* remove = confirmation.addButton(tr("Remove"),
                                                       QMessageBox::DestructiveRole);
                confirmation.addButton(QMessageBox::Cancel);
                confirmation.setDefaultButton(QMessageBox::Cancel);
                confirmation.exec();
                if (confirmation.clickedButton() == remove &&
                    !manager_->remove(pack_.id)) {
                    QMessageBox warning(
                        QMessageBox::Warning, tr("Could not remove icon pack"),
                        tr("%1 is still installed because its files could not be deleted. "
                           "Check file permissions and try again.")
                            .arg(pack_.title),
                        QMessageBox::Ok, this);
                    warning.setTextFormat(Qt::PlainText);
                    warning.exec();
                }
                break;
            }
            case iconpacks::State::Downloading:
            case iconpacks::State::BuiltIn:
            case iconpacks::State::Preview:
                break;
            }
        });

        updatePack(pack);
    }

    QString packId() const { return pack_.id; }

    void updatePack(const iconpacks::Pack& pack) {
        pack_ = pack;
        setProperty("packId", pack.id);
        setAccessibleName(tr("%1 icon pack").arg(pack.title));

        title_->setText(pack.title);

        QStringList platforms;
        for (const QString& platform : pack.platforms)
            platforms.append(platformTitle(platform));

        QStringList metadata;
        if (!platforms.isEmpty())
            metadata.append(platforms.join(QStringLiteral(" / ")));
        if (!pack.version.isEmpty())
            metadata.append(tr("Version %1").arg(pack.version));
        if (!pack.license.isEmpty())
            metadata.append(tr("License: %1").arg(pack.license));
        if (!pack.author.isEmpty())
            metadata.append(tr("By %1").arg(pack.author));
        if (pack.iconCount > 0) {
            metadata.append(pack.iconCount == 1
                                ? tr("1 icon")
                                : tr("%1 icons").arg(pack.iconCount));
        }
        if (pack.totalBytes > 0)
            metadata.append(byteCount(pack.totalBytes));
        if (!pack.categories.isEmpty()) {
            metadata.append(pack.categories.size() == 1
                                ? tr("1 category")
                                : tr("%1 categories").arg(pack.categories.size()));
        }
        metadata_->setText(metadata.join(QStringLiteral("  ·  ")));

        description_->setText(pack.description.isEmpty()
                                  ? tr("No description is available for this pack.")
                                  : pack.description);

        QString stateKey;
        QString stateText;
        QString actionText;
        QString actionKey = QStringLiteral("primary");
        bool actionEnabled = true;
        switch (pack.state) {
        case iconpacks::State::BuiltIn:
            stateKey = QStringLiteral("built-in");
            stateText = tr("Built in");
            actionText = tr("Included");
            actionEnabled = false;
            break;
        case iconpacks::State::Preview:
            stateKey = QStringLiteral("preview");
            stateText = tr("Source preview");
            actionText = tr("Development");
            actionEnabled = false;
            break;
        case iconpacks::State::Installed:
            stateKey = QStringLiteral("installed");
            stateText = tr("Installed");
            actionText = tr("Remove");
            actionKey = QStringLiteral("remove");
            break;
        case iconpacks::State::Available:
            stateKey = QStringLiteral("available");
            stateText = tr("Available");
            actionText = tr("Download");
            break;
        case iconpacks::State::Downloading:
            stateKey = QStringLiteral("downloading");
            stateText = tr("Downloading");
            actionText = tr("Downloading…");
            actionEnabled = false;
            break;
        case iconpacks::State::Failed:
            stateKey = QStringLiteral("failed");
            stateText = tr("Failed");
            actionText = tr("Retry");
            break;
        }

        setProperty("state", stateKey);
        state_->setProperty("state", stateKey);
        action_->setProperty("action", actionKey);
        state_->setText(stateText);
        state_->setAccessibleName(tr("Status: %1").arg(stateText));
        action_->setText(actionText);
        actionEnabled = actionEnabled && !libraryBusy_;
        action_->setEnabled(actionEnabled);
        action_->setCursor(actionEnabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        action_->setAccessibleName(tr("%1: %2").arg(actionText, pack.title));

        const bool downloading = pack.state == iconpacks::State::Downloading;
        progress_->setVisible(downloading);
        updateProgress(pack.receivedBytes, pack.totalBytes);

        const bool failed = pack.state == iconpacks::State::Failed;
        error_->setVisible(failed);
        error_->setText(pack.error.isEmpty()
                            ? tr("The icon pack could not be downloaded. Try again.")
                            : pack.error);

        const QList<QWidget*> restyled = {this, state_, action_};
        for (QWidget* widget : restyled) {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
        }
    }

    void updateProgress(qint64 received, qint64 total) {
        if (pack_.state != iconpacks::State::Downloading)
            return;
        pack_.receivedBytes = received;
        pack_.totalBytes = total;
        if (total <= 0) {
            progress_->setRange(0, 0);
            progress_->setAccessibleDescription(tr("Download size is not yet known."));
            return;
        }
        progress_->setRange(0, 1000);
        progress_->setValue(static_cast<int>(qBound<qint64>(qint64{0},
                                                            received * 1000 / total,
                                                            qint64{1000})));
        progress_->setFormat(QStringLiteral("%p%"));
        progress_->setAccessibleName(tr("Download progress for %1").arg(pack_.title));
        progress_->setAccessibleDescription(
            tr("%1 of %2").arg(byteCount(received), byteCount(total)));
    }

    void setLibraryBusy(bool busy) {
        if (libraryBusy_ == busy)
            return;
        libraryBusy_ = busy;
        updatePack(pack_);
    }

private:
    iconpacks::IconPackManager* manager_;
    iconpacks::Pack pack_;
    QLabel* title_;
    QLabel* metadata_;
    QLabel* description_;
    QLabel* state_;
    QPushButton* action_;
    QProgressBar* progress_;
    QLabel* error_;
    bool libraryBusy_ = false;
};

class IconLibraryPage final : public QWidget {
public:
    explicit IconLibraryPage(QWidget* parent = nullptr)
        : QWidget(parent), manager_(iconpacks::IconPackManager::instance()) {
        setObjectName(QStringLiteral("iconLibraryPage"));
        setAccessibleName(tr("Icons Library"));
        setAccessibleDescription(
            tr("Download and remove optional icon packs from the official Nightlock library."));

        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(28, 24, 28, 28);
        column->setSpacing(14);

        auto* intro = new QFrame;
        intro->setObjectName(QStringLiteral("iconLibraryHeader"));
        intro->setAttribute(Qt::WA_StyledBackground, true);
        auto* introLayout = new QHBoxLayout(intro);
        introLayout->setContentsMargins(16, 15, 16, 15);
        introLayout->setSpacing(18);
        auto* introCopy = new QVBoxLayout;
        introCopy->setContentsMargins(0, 0, 0, 0);
        introCopy->setSpacing(5);
        auto* title = new QLabel(tr("Icons Library"));
        title->setObjectName(QStringLiteral("iconLibraryTitle"));
        auto* description = new QLabel(
            tr("Nightlock includes one lightweight icon pack. Download only the optional "
               "Linux, macOS, or Windows collections you want from the official repository."));
        description->setObjectName(QStringLiteral("iconLibraryIntro"));
        description->setWordWrap(true);
        status_ = new QLabel;
        status_->setObjectName(QStringLiteral("iconLibraryStatus"));
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        introCopy->addWidget(title);
        introCopy->addWidget(description);
        introCopy->addWidget(status_);
        introLayout->addLayout(introCopy, 1);
        refresh_ = new QPushButton(tr("Refresh"));
        refresh_->setObjectName(QStringLiteral("iconLibraryRefreshButton"));
        refresh_->setCursor(Qt::PointingHandCursor);
        refresh_->setAccessibleName(tr("Refresh icon pack catalog"));
        introLayout->addWidget(refresh_, 0, Qt::AlignTop);
        column->addWidget(intro);

        auto* sectionTitle = new QLabel(tr("ICON PACKS"));
        sectionTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
        sectionTitle->setContentsMargins(6, 0, 0, 0);
        column->addWidget(sectionTitle);

        list_ = new QVBoxLayout;
        list_->setContentsMargins(0, 0, 0, 0);
        list_->setSpacing(10);
        column->addLayout(list_);
        column->addStretch(1);

        connect(refresh_, &QPushButton::clicked, this, [this] { requestRefresh(); });
        // Installing/removing emits from the button's click handler. Queue the
        // structural rebuild so the card that owns that button is not deleted
        // while Qt is still dispatching its mouse event.
        connect(manager_, &iconpacks::IconPackManager::catalogChanged, this,
                [this] { rebuild(); }, Qt::QueuedConnection);
        connect(manager_, &iconpacks::IconPackManager::packChanged, this,
                [this](const QString& id) { updatePack(id); });
        connect(manager_, &iconpacks::IconPackManager::progressChanged, this,
                [this](const QString& id, qint64 received, qint64 total) {
                    if (IconPackCard* card = cards_.value(id, nullptr))
                        card->updateProgress(received, total);
                });
        connect(manager_, &iconpacks::IconPackManager::refreshingChanged, this,
                [this](bool) { refreshStatus(); });
        connect(manager_, &iconpacks::IconPackManager::catalogErrorChanged, this,
                [this](const QString&) { refreshStatus(); });

        rebuild();
    }

    void refreshOnce() {
        if (firstVisitHandled_)
            return;
        firstVisitHandled_ = true;
        // Demo screenshots must stay deterministic and offline. The manual
        // Refresh button remains available when someone wants to exercise it.
        if (!qEnvironmentVariableIsSet("NIGHTLOCK_DEMO"))
            requestRefresh();
    }

private:
    void requestRefresh() {
        if (manager_->isRefreshing())
            return;
        manager_->refreshCatalog();
        refreshStatus();
    }

    void rebuild() {
        while (QLayoutItem* item = list_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        cards_.clear();

        const QVector<iconpacks::Pack> packs = manager_->packs();
        if (packs.isEmpty()) {
            auto* empty = new QFrame;
            empty->setObjectName(QStringLiteral("iconLibraryEmptyState"));
            empty->setAttribute(Qt::WA_StyledBackground, true);
            auto* emptyLayout = new QVBoxLayout(empty);
            emptyLayout->setContentsMargins(18, 22, 18, 22);
            emptyLayout->setSpacing(5);
            auto* emptyTitle = new QLabel(tr("No icon packs found"));
            emptyTitle->setObjectName(QStringLiteral("iconLibraryEmptyTitle"));
            emptyTitle->setAlignment(Qt::AlignCenter);
            auto* emptyText = new QLabel(
                tr("Check your connection, then refresh the official Nightlock catalog."));
            emptyText->setObjectName(QStringLiteral("iconLibraryEmptyText"));
            emptyText->setWordWrap(true);
            emptyText->setAlignment(Qt::AlignCenter);
            emptyLayout->addWidget(emptyTitle);
            emptyLayout->addWidget(emptyText);
            list_->addWidget(empty);
        } else {
            for (const iconpacks::Pack& pack : packs) {
                auto* card = new IconPackCard(manager_, pack);
                cards_.insert(pack.id, card);
                list_->addWidget(card);
            }
        }
        refreshStatus();
    }

    void updatePack(const QString& id) {
        const std::optional<iconpacks::Pack> pack = manager_->pack(id);
        if (!pack) {
            rebuild();
            return;
        }
        if (IconPackCard* card = cards_.value(id, nullptr))
            card->updatePack(*pack);
        else
            rebuild();
        refreshStatus();
    }

    void refreshStatus() {
        const bool refreshing = manager_->isRefreshing();
        const QVector<iconpacks::Pack> packs = manager_->packs();
        const bool downloading = std::any_of(
            packs.cbegin(), packs.cend(), [](const iconpacks::Pack& pack) {
                return pack.state == iconpacks::State::Downloading;
            });
        const bool busy = refreshing || downloading;
        refresh_->setEnabled(!busy);
        refresh_->setText(refreshing ? tr("Refreshing…")
                                     : downloading ? tr("Downloading…")
                                                   : tr("Refresh"));
        refresh_->setCursor(busy ? Qt::ArrowCursor : Qt::PointingHandCursor);
        for (IconPackCard* card : std::as_const(cards_))
            card->setLibraryBusy(busy);

        QString state;
        QString text;
        if (refreshing) {
            state = QStringLiteral("refreshing");
            text = tr("Checking github.com/res138/nightlock for icon packs…");
        } else if (!manager_->catalogError().isEmpty()) {
            state = QStringLiteral("error");
            text = tr("Online library unavailable: %1 Installed packs remain ready to use.")
                       .arg(manager_->catalogError());
        } else {
            int ready = 0;
            int downloadable = 0;
            for (const iconpacks::Pack& pack : packs) {
                if (pack.state == iconpacks::State::BuiltIn ||
                    pack.state == iconpacks::State::Installed ||
                    pack.state == iconpacks::State::Preview)
                    ++ready;
                if (pack.state == iconpacks::State::Available ||
                    pack.state == iconpacks::State::Failed)
                    ++downloadable;
            }
            state = packs.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready");
            if (packs.isEmpty())
                text = tr("The online catalog is empty. Refresh to try again.");
            else if (downloadable == 0)
                text = ready == 1
                           ? tr("1 icon pack ready to use.")
                           : tr("%1 icon packs ready to use.").arg(ready);
            else
                text = tr("%1 icon packs · %2 ready to use")
                           .arg(packs.size())
                           .arg(ready);
        }
        status_->setProperty("state", state);
        status_->setText(text);
        status_->setAccessibleName(tr("Icon library status: %1").arg(text));
        status_->style()->unpolish(status_);
        status_->style()->polish(status_);
    }

    iconpacks::IconPackManager* manager_;
    QLabel* status_;
    QPushButton* refresh_;
    QVBoxLayout* list_;
    QHash<QString, IconPackCard*> cards_;
    bool firstVisitHandled_ = false;
};

}  // namespace

SettingsWindow::SettingsWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("settingsWindow"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(860, 560);
    setMinimumSize(640, 420);

    nav_ = new QListWidget;
    nav_->setObjectName(QStringLiteral("settingsNav"));
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setIconSize(QSize(16, 16));
    nav_->setUniformItemSizes(true);
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* navPane = new QFrame;
    navPane->setObjectName(QStringLiteral("settingsNavPane"));
    navPane->setFixedWidth(kNavWidth);
    auto* navLayout = new QVBoxLayout(navPane);
    navLayout->setContentsMargins(0, 16, 0, 10);
    navLayout->setSpacing(0);
    navLayout->addWidget(nav_, 1);

    pages_ = new QStackedWidget;

    addCategory(QStringLiteral("settings"), tr("General"), buildGeneralPage());
    addCategory(QStringLiteral("database"), tr("Database"), buildDatabasePage());
    addCategory(QStringLiteral("palette"), tr("Appearance"), buildAppearancePage());
    addCategory(QStringLiteral("command"), tr("Hotkeys"), buildHotkeysPage());
    addCategory(QStringLiteral("graph"), tr("NetGraph"), buildGraphPage());

    // Keep every established category index stable. Icons Library is appended
    // and starts its first catalog refresh lazily, only when the user opens it.
    const int iconsLibraryIndex = nav_->count();
    auto* iconsLibraryPage = static_cast<IconLibraryPage*>(buildIconsLibraryPage());
    addCategory(QStringLiteral("image"), tr("Icons Library"), iconsLibraryPage);
    nav_->item(iconsLibraryIndex)
        ->setData(Qt::UserRole, QStringLiteral("icons-library"));
    nav_->item(iconsLibraryIndex)
        ->setToolTip(tr("Download and manage optional icon packs"));

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(nav_, &QListWidget::currentRowChanged, iconsLibraryPage,
            [iconsLibraryPage, iconsLibraryIndex](int row) {
                if (row == iconsLibraryIndex)
                    iconsLibraryPage->refreshOnce();
            });
    nav_->setCurrentRow(0);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(navPane);
    layout->addWidget(pages_, 1);
}

void SettingsWindow::selectCategory(int index) {
    nav_->setCurrentRow(index);
}

void SettingsWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SettingsWindow::addCategory(const QString& iconName, const QString& title, QWidget* page) {
    nav_->addItem(new QListWidgetItem(navIcon(iconName), title));

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    pages_->addWidget(scroll);
}

QWidget* SettingsWindow::buildGeneralPage() {
    QVBoxLayout* column = nullptr;
    QWidget* page = makePage(column);
    QVBoxLayout* rows = nullptr;
    QFrame* card = makeCard(rows);

    auto* checkUpdates = inlineButton(tr("Check for updates"));
    checkUpdates->setObjectName(QStringLiteral("checkForUpdatesButton"));
    auto* updateManager = updates::UpdateManager::instance();
    checkUpdates->setEnabled(!updateManager->isChecking());
    QLabel* updateStatus = addRow(
        rows, tr("Version %1").arg(QStringLiteral(NIGHTLOCK_VERSION)),
        updateManager->isChecking() ? tr("Checking for a stable GitHub release…")
                                    : tr("Nightlock password manager."),
        checkUpdates);
    connect(updateManager, &updates::UpdateManager::checkingChanged, checkUpdates,
            [this, checkUpdates, updateStatus](bool checking) {
                checkUpdates->setEnabled(!checking);
                updateStatus->setText(
                    checking ? tr("Checking for a stable GitHub release…")
                             : tr("Nightlock password manager."));
            });
    connect(checkUpdates, &QPushButton::clicked, this, [this, updateManager] {
        updateManager->checkForUpdates(
            this, updates::UpdateManager::CheckMode::Manual);
    });

    auto* startupUpdates =
        new ToggleSwitch(updates::checkOnStartupEnabled());
    startupUpdates->setObjectName(QStringLiteral("checkUpdatesOnStartupToggle"));
    connect(startupUpdates, &QAbstractButton::toggled, startupUpdates,
            [](bool enabled) { updates::setCheckOnStartupEnabled(enabled); });
    addRow(rows, tr("Check for updates at startup"),
           tr("Look for a newer stable GitHub release when Nightlock starts."),
           startupUpdates);

    addRow(rows, tr("Language"), tr("Changes the interface language."),
           new DropdownButton({tr("English")}, 0));

    auto* presets = new ToggleSwitch(generalsettings::presetsEnabled());
    connect(presets, &QAbstractButton::toggled, presets,
            [](bool enabled) { generalsettings::setPresetsEnabled(enabled); });
    addRow(rows, tr("Enable Presets"),
           tr("Show preset selection when creating an entry."), presets);

    auto* customFields = new ToggleSwitch(generalsettings::allowCustomFields());
    connect(customFields, &QAbstractButton::toggled, customFields,
            [](bool allowed) { generalsettings::setAllowCustomFields(allowed); });
    addRow(rows, tr("Allow custom fields"),
           tr("Allow entry forms to add and remove user-defined fields."), customFields);

    auto* entryColors = new ToggleSwitch(generalsettings::entryColorsEnabled());
    connect(entryColors, &QAbstractButton::toggled, entryColors,
            [](bool enabled) { generalsettings::setEntryColorsEnabled(enabled); });
    addRow(rows, tr("Enable Entry Colors"),
           tr("Allow subtle per-entry colors in the entry list."), entryColors);

    auto* hideSearch = new ToggleSwitch(generalsettings::hideSearchIcon());
    connect(hideSearch, &QAbstractButton::toggled, hideSearch,
            [](bool hidden) { generalsettings::setHideSearchIcon(hidden); });
    addRow(rows, tr("Hide Search Icon"),
           tr("Hide the Search icon from the main toolbar."), hideSearch);

    auto* hideLock = new ToggleSwitch(generalsettings::hideLockButton());
    connect(hideLock, &QAbstractButton::toggled, hideLock,
            [](bool hidden) { generalsettings::setHideLockButton(hidden); });
    addRow(rows, tr("Hide Lock Button"),
           tr("Hide the Lock button from the main toolbar."), hideLock);

    auto* hideNewFolder = new ToggleSwitch(generalsettings::hideNewFolderButton());
    connect(hideNewFolder, &QAbstractButton::toggled, hideNewFolder,
            [](bool hidden) { generalsettings::setHideNewFolderButton(hidden); });
    addRow(rows, tr("Hide New Folder Button"),
           tr("Hide the New Folder button from the main toolbar."), hideNewFolder);

    auto* hideGenerator = new ToggleSwitch(generalsettings::hideGeneratorIcon());
    connect(hideGenerator, &QAbstractButton::toggled, hideGenerator,
            [](bool hidden) { generalsettings::setHideGeneratorIcon(hidden); });
    addRow(rows, tr("Hide Generator Icon"),
           tr("Hide the password generator icon from Entry View."), hideGenerator);
    finishCard(rows);

    column->addWidget(card);
    column->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildDatabasePage() {
    QVBoxLayout* column = nullptr;
    QWidget* page = makePage(column);
    auto* service = VaultService::instance();

    QVBoxLayout* rows = nullptr;
    QFrame* card = makeCard(rows);

    // The vault this session points at; follows switches live.
    QLabel* pathLabel = addRow(rows, tr("Current database"),
                               QDir::toNativeSeparators(service->vaultPath()), nullptr);
    connect(service, &VaultService::vaultPathChanged, pathLabel,
            [pathLabel](const QString& path) {
                pathLabel->setText(QDir::toNativeSeparators(path));
            });

    ToggleSwitch* touchIdToggle = nullptr;
#ifndef Q_OS_WIN
    QString touchIdAvailability;
    const bool touchIdAvailable = touchid::isAvailable(&touchIdAvailability);
    touchIdToggle = new ToggleSwitch(touchid::isEnabledForVault(service->vaultPath()));
    // An existing opt-in can always be turned off, even when Touch ID
    // has since become unavailable or the vault is currently locked.
    touchIdToggle->setEnabled(
        touchIdToggle->isChecked() ||
        (touchIdAvailable && service->isUnlocked() && !service->demoMode()));
    addRow(rows, tr("Unlock with Touch ID"),
           touchIdAvailable
               ? tr("Store this database's master password in the macOS Keychain, "
                    "then require Touch ID before it can be used.")
               : tr("Unavailable: %1").arg(touchIdAvailability),
           touchIdToggle);
    connect(touchIdToggle, &QAbstractButton::toggled, this,
            [this, service, touchIdToggle](bool enabled) {
                const auto restore = [touchIdToggle](bool checked) {
                    const QSignalBlocker blocker(touchIdToggle);
                    touchIdToggle->setChecked(checked);
                };

                const QString path = service->vaultPath();
                QString error;
                if (!enabled) {
                    if (!touchid::disableForVault(path, &error)) {
                        restore(true);
                        QMessageBox::warning(
                            this, tr("Touch ID"),
                            tr("Touch ID could not be disabled: %1").arg(error));
                    }
                    return;
                }

                if (service->demoMode() || !service->isUnlocked()) {
                    restore(false);
                    QMessageBox::warning(this, tr("Touch ID"),
                                         tr("Unlock the database before enabling Touch ID."));
                    return;
                }
                if (!touchid::isAvailable(&error)) {
                    restore(false);
                    QMessageBox::warning(this, tr("Touch ID"), error);
                    return;
                }

                bool accepted = false;
                const QString password = QInputDialog::getText(
                    this, tr("Enable Touch ID"),
                    tr("Enter the current master password:"), QLineEdit::Password,
                    {}, &accepted);
                if (!accepted) {
                    restore(false);
                    return;
                }
                if (!service->verifyPassword(password)) {
                    restore(false);
                    QMessageBox::warning(this, tr("Touch ID"),
                                         tr("The master password is incorrect."));
                    return;
                }
                if (!touchid::enableForVault(path, password, &error)) {
                    restore(false);
                    QMessageBox::warning(
                        this, tr("Touch ID"),
                        tr("Touch ID could not be enabled: %1").arg(error));
                }
            });
#endif

    auto* switchButton = inlineButton(tr("Choose File…"));
    connect(switchButton, &QPushButton::clicked, this, [this, service] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Database"),
            QFileInfo(service->vaultPath()).absolutePath(),
            tr("Nightlock Vault (*.nlck)"));
        if (!path.isEmpty())
            emit switchDatabaseRequested(path);
    });
    addRow(rows, tr("Switch database"),
           tr("Open another vault file and make it the default."), switchButton);

    auto* createButton = inlineButton(tr("Create…"));
    connect(createButton, &QPushButton::clicked, this, [this, service] {
        QString path = QFileDialog::getSaveFileName(
            this, tr("Create Database"),
            QFileInfo(service->vaultPath()).dir().filePath(QStringLiteral("Vault.nlck")),
            tr("Nightlock Vault (*.nlck)"), nullptr,
            // The existence check below refuses instead — a vault is
            // never silently replaced.
            QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty())
            return;
        if (!path.endsWith(QLatin1String(".nlck"), Qt::CaseInsensitive))
            path += QLatin1String(".nlck");
        if (QFileInfo::exists(path)) {
            QMessageBox::warning(this, tr("Create Database"),
                                 tr("A vault already exists there. Pick another name."));
            return;
        }
        emit createDatabaseRequested(path);
    });
    addRow(rows, tr("New database"),
           tr("Create a fresh vault and make it the default."), createButton);
    finishCard(rows);
    column->addWidget(card);

    // Master-password change: three fields, verified and applied by
    // the button row at the bottom. State checks happen at click time
    // — the page can sit open across a lock or an unlock.
    QVBoxLayout* passwordRows = nullptr;
    QFrame* passwordCard = makeCard(passwordRows);

    const auto passwordField = [](const QString& placeholder) {
        auto* field = new QLineEdit;
        field->setObjectName(QStringLiteral("settingsPasswordField"));
        field->setEchoMode(QLineEdit::Password);
        field->setPlaceholderText(placeholder);
        field->setFixedWidth(200);
        field->setAttribute(Qt::WA_MacShowFocusRect, false);
        return field;
    };
    QLineEdit* currentPassword = passwordField(tr("Required"));
    QLineEdit* newPassword = passwordField(tr("At least 1 character"));
    QLineEdit* repeatPassword = passwordField(tr("Same as above"));
    addRow(passwordRows, tr("Current password"), {}, currentPassword);
    addRow(passwordRows, tr("New password"), {}, newPassword);
    addRow(passwordRows, tr("Repeat new password"), {}, repeatPassword);

    auto* applyPassword = inlineButton(tr("Change…"));
    QLabel* passwordStatus =
        addRow(passwordRows, tr("Change password"),
               tr("Verifies the current password, then re-encrypts the vault."),
               applyPassword);
    const auto passwordVerdict = [passwordStatus](const QString& message, bool failed) {
        passwordStatus->setStyleSheet(failed ? QStringLiteral("color:#D2605E;")
                                             : QString());
        passwordStatus->setText(message);
    };
    connect(applyPassword, &QPushButton::clicked, this,
            [service, currentPassword, newPassword, repeatPassword, passwordVerdict,
             touchIdToggle] {
                if (service->demoMode()) {
                    passwordVerdict(tr("Demo mode has no real vault."), true);
                    return;
                }
                if (!service->isUnlocked()) {
                    passwordVerdict(tr("Unlock the vault first."), true);
                    return;
                }
                if (newPassword->text().isEmpty()) {
                    passwordVerdict(tr("Enter a new password."), true);
                    newPassword->setFocus();
                    return;
                }
                if (newPassword->text() != repeatPassword->text()) {
                    passwordVerdict(tr("New passwords do not match."), true);
                    repeatPassword->selectAll();
                    repeatPassword->setFocus();
                    return;
                }
                const bool refreshTouchId =
                    touchIdToggle && touchid::isEnabledForVault(service->vaultPath());
                const nightlock::VaultError error = service->changePassword(
                    currentPassword->text(), newPassword->text());
                if (error == nightlock::VaultError::WrongPassword) {
                    passwordVerdict(tr("Current password is incorrect."), true);
                    currentPassword->selectAll();
                    currentPassword->setFocus();
                    return;
                }
                if (error != nightlock::VaultError::None) {
                    passwordVerdict(
                        QString::fromUtf8(nightlock::errorMessage(error)), true);
                    return;
                }
                QString touchIdError;
                if (refreshTouchId && !touchid::enableForVault(
                                          service->vaultPath(), newPassword->text(),
                                          &touchIdError)) {
                    QString disableError;
                    if (!touchid::disableForVault(service->vaultPath(), &disableError) &&
                        !disableError.isEmpty())
                        touchIdError += tr("; cleanup failed: %1").arg(disableError);
                    const QSignalBlocker blocker(touchIdToggle);
                    touchIdToggle->setChecked(false);
                    currentPassword->clear();
                    newPassword->clear();
                    repeatPassword->clear();
                    passwordVerdict(
                        tr("Password changed, but Touch ID was disabled: %1")
                            .arg(touchIdError),
                        true);
                    return;
                }
                currentPassword->clear();
                newPassword->clear();
                repeatPassword->clear();
                passwordVerdict(tr("Password changed."), false);
            });
    // Enter in the last field applies, like a small form.
    connect(repeatPassword, &QLineEdit::returnPressed, applyPassword,
            &QPushButton::click);
    finishCard(passwordRows);
    column->addWidget(passwordCard);

    QVBoxLayout* startupRows = nullptr;
    QFrame* startupCard = makeCard(startupRows);

    // The startup default: the one vault that opens on the next
    // launch. Clearing it makes Nightlock start knowing nothing.
    auto* clearButton = inlineButton(tr("Clear"));
    QLabel* defaultLabel =
        addRow(startupRows, tr("Open at startup"), QStringLiteral(" "), clearButton);
    const auto refreshDefault = [defaultLabel, clearButton] {
        const QString remembered = VaultService::rememberedVaultPath();
        defaultLabel->setText(remembered.isEmpty()
                                  ? tr("None — Nightlock starts without a database.")
                                  : QDir::toNativeSeparators(remembered));
        clearButton->setEnabled(!remembered.isEmpty());
    };
    refreshDefault();
    connect(service, &VaultService::rememberedVaultChanged, defaultLabel,
            [refreshDefault](const QString&) { refreshDefault(); });
    connect(clearButton, &QPushButton::clicked, service,
            [service] { service->clearRememberedVaultPath(); });

    auto* signOutButton = inlineButton(tr("Sign Out"));
    connect(signOutButton, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, tr("Sign Out"),
            tr("Close this database and forget it? The vault file stays on disk."));
        if (answer == QMessageBox::Yes)
            emit signOutRequested();
    });
    addRow(startupRows, tr("Sign out"),
           tr("Lock the current database and forget it completely."), signOutButton);
    finishCard(startupRows);
    column->addWidget(startupCard);

    column->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildAppearancePage() {
    QVBoxLayout* column = nullptr;
    QWidget* page = makePage(column);
    QVBoxLayout* rows = nullptr;
    QFrame* card = makeCard(rows);

    const auto indexIn = [](const auto& ids, const QString& value) {
        for (int i = 0; i < static_cast<int>(std::size(ids)); ++i)
            if (value == QLatin1String(ids[i]))
                return i;
        return 0;
    };

    auto* scheme = new DropdownButton(
        {tr("Light"), tr("Dark"), tr("System")},
        indexIn(appearancesettings::kThemes, appearancesettings::theme()));
    scheme->setOnSelected([](int index) {
        appearancesettings::setTheme(QLatin1String(appearancesettings::kThemes[index]));
    });
    addRow(rows, tr("Base color scheme"), tr("Choose the interface color scheme."), scheme);

    auto* accent = new DropdownButton(
        {tr("Black"), tr("Blue"), tr("Green")},
        indexIn(appearancesettings::kAccents, appearancesettings::accent()));
    QVector<QColor> dots;
    for (const char* name : appearancesettings::kAccents)
        dots.append(appearancesettings::accentColorFor(QLatin1String(name)));
    accent->setDots(dots);
    accent->setOnSelected([](int index) {
        appearancesettings::setAccent(QLatin1String(appearancesettings::kAccents[index]));
    });
    addRow(rows, tr("Accent color"), tr("Highlight color used across the app."), accent);

    // Font pickers: the dropdown shows the effective choice (a stored
    // font missing on this system resolves to the first available
    // option), and unavailable fonts stay listed but grayed out.
    const auto fontDropdown = [](fonts::Role role) {
        const QList<fonts::Option> catalog = fonts::options(role);
        QStringList titles;
        QVector<bool> available;
        for (const fonts::Option& option : catalog) {
            titles.append(option.title);
            available.append(option.available);
        }
        auto* dropdown = new DropdownButton(titles, fonts::selectedIndex(role));
        dropdown->setOptionsEnabled(available);
        dropdown->setOnSelected(
            [role, catalog](int index) { fonts::setSelected(role, catalog[index].id); });
        return dropdown;
    };
    addRow(rows, tr("Primary font"), tr("The interface font."),
           fontDropdown(fonts::Role::Primary));
    addRow(rows, tr("Secondary font"), tr("Serif font of the tree, titles and entry names."),
           fontDropdown(fonts::Role::Secondary));

    auto* sidebarItemSize = new DropdownButton(
        {tr("Small"), tr("Default"), tr("Large")},
        indexIn(appearancesettings::kSidebarItemSizes,
                appearancesettings::sidebarItemSize()));
    sidebarItemSize->setOnSelected([](int index) {
        appearancesettings::setSidebarItemSize(
            QLatin1String(appearancesettings::kSidebarItemSizes[index]));
    });
    addRow(rows, tr("Sidebar item size"),
           tr("Size of folder names and the icons next to them."), sidebarItemSize);

    auto* entryListItemSize = new DropdownButton(
        {tr("Default"), tr("Small"), tr("Ultra Compact")},
        indexIn(appearancesettings::kEntryListItemSizes,
                appearancesettings::entryListItemSize()));
    entryListItemSize->setOnSelected([](int index) {
        appearancesettings::setEntryListItemSize(
            QLatin1String(appearancesettings::kEntryListItemSizes[index]));
    });
    addRow(rows, tr("Entry list item size"),
           tr("Row density; Ultra Compact hides the login."), entryListItemSize);

    auto* folderIcons = new ToggleSwitch(appearancesettings::folderIcons());
    connect(folderIcons, &QAbstractButton::toggled, folderIcons,
            [](bool shown) { appearancesettings::setFolderIcons(shown); });
    addRow(rows, tr("Folder icons"),
           tr("Show custom icons next to folders in the tree."), folderIcons);
    finishCard(rows);

    column->addWidget(card);

    auto* iconSection = new QVBoxLayout;
    iconSection->setContentsMargins(0, 0, 0, 0);
    iconSection->setSpacing(8);
    auto* iconHeading = new QLabel(tr("APPLICATION ICON"));
    iconHeading->setObjectName(QStringLiteral("settingsSectionTitle"));
    iconHeading->setContentsMargins(6, 0, 0, 0);
    iconSection->addWidget(iconHeading);

    QVBoxLayout* iconRows = nullptr;
    QFrame* iconCard = makeCard(iconRows);
    iconRows->setContentsMargins(18, 18, 18, 16);
    auto* applicationIcon =
        new ApplicationIconPicker(appearancesettings::applicationIcon());
    connect(applicationIcon, &ApplicationIconPicker::iconSelected,
            applicationIcon, [](const QString& id) {
                appearancesettings::setApplicationIcon(id);
            });
    iconRows->addWidget(applicationIcon);
    iconSection->addWidget(iconCard);
    column->addLayout(iconSection);

    column->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildHotkeysPage() {
    QVBoxLayout* column = nullptr;
    QWidget* page = makePage(column);
    QVBoxLayout* rows = nullptr;
    QFrame* card = makeCard(rows);

    for (const hotkeys::Action& action : hotkeys::actions())
        addRow(rows, action.title, {}, new HotkeyButton(action.id, action.sequence));
    finishCard(rows);

    column->addWidget(card);
    column->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildGraphPage() {
    QVBoxLayout* column = nullptr;
    QWidget* page = makePage(column);

    // Master availability controls sit first and separately from the
    // graph's simulation/edge configuration. The master switch forces
    // both entry points hidden and locks their switches until NetGraph
    // is enabled again.
    QVBoxLayout* availability = nullptr;
    QFrame* availabilityCard = makeCard(availability);
    auto* disableGraph = new ToggleSwitch(graphsettings::disabled());
    auto* hideIcon = new ToggleSwitch(graphsettings::hideIcon());
    auto* hideButton = new ToggleSwitch(graphsettings::hideButton());

    const auto syncAvailability = [hideIcon, hideButton](bool disabled) {
        if (disabled) {
            hideIcon->setChecked(true);
            hideButton->setChecked(true);
        }
        hideIcon->setEnabled(!disabled);
        hideButton->setEnabled(!disabled);
        hideIcon->setCursor(disabled ? Qt::ArrowCursor : Qt::PointingHandCursor);
        hideButton->setCursor(disabled ? Qt::ArrowCursor : Qt::PointingHandCursor);
    };

    connect(disableGraph, &QAbstractButton::toggled, disableGraph,
            [syncAvailability](bool disabled) {
                graphsettings::setDisabled(disabled);
                syncAvailability(disabled);
            });
    connect(hideIcon, &QAbstractButton::toggled, hideIcon,
            [](bool hidden) { graphsettings::setHideIcon(hidden); });
    connect(hideButton, &QAbstractButton::toggled, hideButton,
            [](bool hidden) { graphsettings::setHideButton(hidden); });

    addRow(availability, tr("Disable NetGraph"),
           tr("Turn off NetGraph completely, including its hotkey."), disableGraph);
    addRow(availability, tr("Hide NetGraph Icon"),
           tr("Hide the NetGraph icon above the directories."), hideIcon);
    addRow(availability, tr("Hide NetGraph Button"),
           tr("Hide Show in NetGraph in the entry viewer."), hideButton);
    finishCard(availability);
    syncAvailability(disableGraph->isChecked());
    column->addWidget(availabilityCard);

    // The force-simulation knobs, applied to an open graph live.
    QVBoxLayout* forces = nullptr;
    QFrame* forcesCard = makeCard(forces);
    addRow(forces, tr("Center force"), tr("Pull toward the canvas center."),
           numberField(graphsettings::kCenterForce));
    addRow(forces, tr("Repel force"), tr("How strongly nodes push each other apart."),
           numberField(graphsettings::kRepelForce));
    addRow(forces, tr("Link force"), tr("Spring strength of every link."),
           numberField(graphsettings::kLinkForce));
    addRow(forces, tr("Link distance"), tr("Resting length of a link."),
           numberField(graphsettings::kLinkDistance));
    finishCard(forces);
    column->addWidget(forcesCard);

    QVBoxLayout* rows = nullptr;
    QFrame* card = makeCard(rows);

    addRow(rows, tr("Password reuse links"),
           tr("Connect entries that share a password."), new ToggleSwitch(true));
    addRow(rows, tr("Shared login links"),
           tr("Connect entries that share a login."), new ToggleSwitch(true));
    addRow(rows, tr("Same-domain links"),
           tr("Connect entries whose URLs share a base domain."), new ToggleSwitch(true));
    addRow(rows, tr("Directory links"),
           tr("Spokes from every folder hub to the entries inside it."), new ToggleSwitch(true));
    finishCard(rows);

    column->addWidget(card);
    column->addStretch(1);
    return page;
}

QWidget* SettingsWindow::buildIconsLibraryPage() {
    return new IconLibraryPage;
}
