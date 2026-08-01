#include "settingswindow.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <iterator>

#include "appearancesettings.hpp"
#include "fonts.hpp"
#include "graphsettings.hpp"
#include "hotkeys.hpp"
#include "vaultservice.hpp"
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

// Round color swatch for the accent picker's menu entries.
QIcon dotIcon(const QColor& color) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(4, 4, 16, 16);
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}

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
                action->setIcon(dotIcon(dots_[i]));
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

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
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
    addRow(rows, tr("Version %1").arg(QStringLiteral(NIGHTLOCK_VERSION)),
           tr("Nightlock password manager."), checkUpdates);

    addRow(rows, tr("Automatic updates"),
           tr("Check for and download updates in the background."), new ToggleSwitch(true));

    addRow(rows, tr("Language"), tr("Changes the interface language."),
           new DropdownButton({tr("English")}, 0));
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
            [service, currentPassword, newPassword, repeatPassword, passwordVerdict] {
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

    auto* folderIcons = new ToggleSwitch(appearancesettings::folderIcons());
    connect(folderIcons, &QAbstractButton::toggled, folderIcons,
            [](bool shown) { appearancesettings::setFolderIcons(shown); });
    addRow(rows, tr("Folder icons"),
           tr("Show custom icons next to folders in the tree."), folderIcons);
    finishCard(rows);

    column->addWidget(card);
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
