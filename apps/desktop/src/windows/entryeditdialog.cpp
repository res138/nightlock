#include "entryeditdialog.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCursor>
#include <QEasingCurve>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <functional>
#include <tuple>

#include "appearancesettings.hpp"
#include "expirationui.hpp"
#include "generalsettings.hpp"
#include "qsecure.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/entrycolorpicker.hpp"
#include "widgets/iconpicker.hpp"
#include "widgets/nlmenu.hpp"
#include "widgets/overlayscrollbar.hpp"
#include "widgets/patternpicker.hpp"

namespace {

using nightlock::EntryPreset;

bool isExpirationLabel(const QString& label) {
    const QByteArray utf8 = label.trimmed().toUtf8();
    return nightlock::expiration::isLabel(
        std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

// An expiration custom field is edited as clean DD / MM / YYYY segments
// with separators inserted as the user types. Unlike QLineEdit's native
// input mask, this leaves no technical underscore placeholders behind.
// It switches to a localized long date as soon as focus leaves it.
// The vault always receives DD/MM/YYYY, independently of the UI locale.
class ExpirationLineEdit : public QLineEdit {
public:
    explicit ExpirationLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        connect(this, &QLineEdit::textEdited, this, [this](const QString& value) {
            if (!expirationMode_)
                return;
            const QString formatted = numericText(value);
            if (formatted != value) {
                setText(formatted);
                setCursorPosition(formatted.size());
            }
        });
    }

    void setExpirationMode(bool enabled) {
        if (expirationMode_ == enabled)
            return;
        expirationMode_ = enabled;
        if (!enabled) {
            setPlaceholderText(ordinaryPlaceholder_);
            setMaxLength(32767);
            return;
        }

        ordinaryPlaceholder_ = placeholderText();
        setPlaceholderText(tr("DD / MM / YYYY"));
        const QDate parsed = parsedDate();
        if (parsed.isValid()) {
            setText(expirationui::displayText(parsed));
        } else {
            beginNumericEditing();
        }
    }

    QString valueForStorage() const {
        if (!expirationMode_)
            return text();
        if (blank())
            return {};
        const QDate parsed = parsedDate();
        return parsed.isValid() ? expirationui::storedText(parsed) : text().trimmed();
    }

    bool validOrEmpty() const {
        if (!expirationMode_)
            return true;
        return blank() || parsedDate().isValid();
    }

protected:
    void focusInEvent(QFocusEvent* event) override {
        if (expirationMode_)
            beginNumericEditing();
        QLineEdit::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if (!expirationMode_)
            return;
        const QDate parsed = parsedDate();
        if (!parsed.isValid())
            return;
        setMaxLength(32767);
        setText(expirationui::displayText(parsed));
    }

private:
    bool blank() const {
        return digits(text()).isEmpty();
    }

    static QString digits(const QString& value) {
        QString result;
        result.reserve(8);
        for (const QChar character : value) {
            if (character.isDigit() && result.size() < 8)
                result.append(character);
        }
        return result;
    }

    static QString numericText(const QString& value) {
        const QString raw = digits(value);
        if (raw.size() <= 2)
            return raw;
        QString result = raw.left(2) + QStringLiteral(" / ") + raw.mid(2, 2);
        if (raw.size() > 4)
            result += QStringLiteral(" / ") + raw.mid(4, 4);
        return result;
    }

    QDate parsedDate() const {
        if (const QDate displayed = expirationui::parseText(text()); displayed.isValid())
            return displayed;
        const QString raw = digits(text());
        return raw.size() == 8
                   ? expirationui::parseText(raw.left(2) + QLatin1Char('/') +
                                             raw.mid(2, 2) + QLatin1Char('/') +
                                             raw.mid(4, 4))
                   : QDate{};
    }

    void beginNumericEditing() {
        const QDate parsed = parsedDate();
        setMaxLength(14);
        setText(parsed.isValid()
                    ? parsed.toString(QStringLiteral("dd / MM / yyyy"))
                    : numericText(text()));
    }

    bool expirationMode_ = false;
    QString ordinaryPlaceholder_;
};

QString presetTitle(EntryPreset preset) {
    switch (preset) {
        case EntryPreset::Classic: return EntryEditDialog::tr("Classic");
        case EntryPreset::Wifi: return EntryEditDialog::tr("Wi-Fi");
        case EntryPreset::BankCard: return EntryEditDialog::tr("Bank Card");
        case EntryPreset::BrowserBookmark: return EntryEditDialog::tr("Browser Bookmark");
        case EntryPreset::CryptoWallet: return EntryEditDialog::tr("Cryptowallet");
    }
    return EntryEditDialog::tr("Classic");
}

constexpr EntryPreset kPresets[] = {
    EntryPreset::Classic,
    EntryPreset::Wifi,
    EntryPreset::BankCard,
    EntryPreset::BrowserBookmark,
    EntryPreset::CryptoWallet,
};

struct CryptoAsset {
    const char* id;
    const char* title;
    int seedWords;
};

constexpr CryptoAsset kCryptoAssets[] = {
    {"btc", QT_TRANSLATE_NOOP("EntryEditDialog", "Bitcoin (BTC)"), 12},
    {"eth", QT_TRANSLATE_NOOP("EntryEditDialog", "Ethereum (ETH)"), 12},
    {"sol", QT_TRANSLATE_NOOP("EntryEditDialog", "Solana (SOL)"), 12},
    {"bnb", QT_TRANSLATE_NOOP("EntryEditDialog", "BNB (BNB Smart Chain)"), 12},
    {"usdt-erc20", QT_TRANSLATE_NOOP("EntryEditDialog", "USDT (Ethereum ERC-20)"), 12},
    {"usdt-trc20", QT_TRANSLATE_NOOP("EntryEditDialog", "USDT (Tron TRC-20)"), 12},
    {"ada", QT_TRANSLATE_NOOP("EntryEditDialog", "Cardano (ADA)"), 24},
    {"xmr", QT_TRANSLATE_NOOP("EntryEditDialog", "Monero (XMR)"), 25},
};

int cryptoAssetIndex(const QString& id) {
    for (int i = 0; i < static_cast<int>(std::size(kCryptoAssets)); ++i)
        if (id == QLatin1String(kCryptoAssets[i].id))
            return i;
    return 0;
}

QStringList cryptoAssetTitles() {
    QStringList result;
    for (const CryptoAsset& asset : kCryptoAssets)
        result.append(EntryEditDialog::tr(asset.title));
    return result;
}

// Dialog dropdown with the same flipping chevron language as Pattern.
// It owns an NlMenu rather than relying on the platform combo-box look.
class EntryDropdownButton : public QAbstractButton {
public:
    EntryDropdownButton(QStringList options, int current, QWidget* parent = nullptr)
        : QAbstractButton(parent), options_(std::move(options)), current_(current) {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(34);
        QFont f = font();
        f.setPixelSize(13);
        setFont(f);
        connect(this, &QAbstractButton::clicked, this, [this] { openMenu(); });
    }

    void setOnSelected(std::function<void(int)> callback) {
        onSelected_ = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& palette = appearancesettings::palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette.border, 1));
        painter.drawLine(QPointF(0, height() - 0.5),
                         QPointF(width(), height() - 0.5));
        painter.setPen(palette.ink);
        painter.drawText(rect().adjusted(2, 0, -30, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, options_.value(current_));

        painter.translate(width() - 15, height() / 2.0);
        painter.rotate(180.0 * chevronTurn_);
        QPen chevronPen(palette.muted, 1.6);
        chevronPen.setCapStyle(Qt::RoundCap);
        chevronPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(chevronPen);
        painter.setBrush(Qt::NoBrush);
        QPainterPath chevron;
        chevron.moveTo(-3.5, -1.5);
        chevron.lineTo(0.0, 2.0);
        chevron.lineTo(3.5, -1.5);
        painter.drawPath(chevron);
    }

private:
    void openMenu() {
        if (menuOpen_)
            return;
        auto* menu = new NlMenu(this);
        for (int i = 0; i < options_.size(); ++i) {
            QAction* action = menu->addAction(options_[i], this, [this, i] {
                current_ = i;
                update();
                if (onSelected_)
                    onSelected_(i);
            });
            action->setCheckable(true);
            action->setChecked(i == current_);
        }
        connect(menu, &QMenu::aboutToHide, this, [this, menu] {
            menuOpen_ = false;
            animateChevron(false);
            menu->deleteLater();
        });
        menuOpen_ = true;
        animateChevron(true);
        menu->popupAt(mapToGlobal(QPoint(0, height() + 5)));
    }

    void animateChevron(bool open) {
        if (animation_) {
            animation_->stop();
            animation_->deleteLater();
        }
        animation_ = new QVariantAnimation(this);
        animation_->setDuration(130);
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        animation_->setStartValue(chevronTurn_);
        animation_->setEndValue(open ? 1.0 : 0.0);
        connect(animation_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    chevronTurn_ = value.toReal();
                    update();
                });
        animation_->start();
    }

    QStringList options_;
    int current_ = 0;
    bool menuOpen_ = false;
    qreal chevronTurn_ = 0.0;
    QVariantAnimation* animation_ = nullptr;
    std::function<void(int)> onSelected_;
};

// Compact theme-aware check control for custom secret fields. The
// tick is painted in the accent's contrast color, so it remains clear
// for black as well as colored accents.
class SecretCheckBox : public QCheckBox {
public:
    explicit SecretCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);
    }

    QSize sizeHint() const override {
        return {18 + 7 + fontMetrics().horizontalAdvance(text()), 24};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& palette = appearancesettings::palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        const QRectF box(1, 4, 16, 16);
        painter.setPen(QPen(isChecked() ? appearancesettings::accentColor()
                                       : palette.borderStrong,
                            1));
        painter.setBrush(isChecked() ? appearancesettings::accentColor()
                                     : palette.input);
        painter.drawRoundedRect(box, 5, 5);
        if (isChecked()) {
            QPen tick(appearancesettings::accentTextColor(), 1.8);
            tick.setCapStyle(Qt::RoundCap);
            tick.setJoinStyle(Qt::RoundJoin);
            painter.setPen(tick);
            QPainterPath path;
            path.moveTo(5, 12);
            path.lineTo(8, 15);
            path.lineTo(14, 8.5);
            painter.drawPath(path);
        }
        painter.setPen(palette.ink);
        painter.drawText(rect().adjusted(24, 0, 0, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

}  // namespace

EntryEditDialog::EntryEditDialog(Mode mode, QWidget* parent)
    : QDialog(parent),
      mode_(mode),
      presetsEnabled_(generalsettings::presetsEnabled()),
      allowCustomFields_(generalsettings::allowCustomFields()) {
    setObjectName(QStringLiteral("entryEditDialog"));
    const QString title = mode == Mode::Add ? tr("New Entry") : tr("Edit Entry");
    setWindowTitle(title);

    auto* form = new QWidget;
    auto* layout = new QVBoxLayout(form);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    // Content may grow for a preset, but the window itself is frozen
    // to Classic's size below; this constraint keeps the scroll range
    // synchronized with rows being added and removed.
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    scroll_ = new QScrollArea;
    scroll_->setObjectName(QStringLiteral("entryDialogScroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setWidget(form);
    new OverlayScrollBar(scroll_);

    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    shell->addWidget(scroll_);

    // Preset choice belongs only to creation. Edit reconstructs the
    // saved preset's labels without offering a type-changing control.
    if (mode == Mode::Add && presetsEnabled_) {
        QStringList titles;
        for (EntryPreset preset : kPresets)
            titles.append(presetTitle(preset));
        auto* dropdown = new EntryDropdownButton(titles, 0);
        presetButton_ = dropdown;
        presetButton_->setObjectName(QStringLiteral("presetPicker"));
        dropdown->setOnSelected([this](int index) { setPreset(kPresets[index], true); });
        layout->addWidget(makeField(tr("Preset"), presetButton_));
    }

    iconPicker_ = new IconPicker;
    connect(iconPicker_, &IconPicker::addIconRequested, this, [this] {
        auto* gallery = new IconGalleryPopup(this);
        connect(gallery, &IconGalleryPopup::iconSelected, iconPicker_,
                &IconPicker::setCustomIcon);
        gallery->popupAt(QCursor::pos());
    });
    layout->addWidget(makeField(tr("Icon"), iconPicker_));

    // Detail-view background pattern; a fresh entry starts on a
    // random look instead of None.
    patternPicker_ = new PatternPicker;
    if (mode == Mode::Add)
        patternPicker_->setValue(PatternPicker::randomOption());
    layout->addWidget(makeField(tr("Pattern"), patternPicker_));

    colorPicker_ = new EntryColorPicker;
    colorField_ = makeField(tr("Color"), colorPicker_);
    const bool showEntryColors = generalsettings::entryColorsEnabled();
    // Keep the immutable Classic baseline independent of optional
    // features; the field is revealed only after the viewport freezes.
    colorField_->hide();
    layout->addWidget(colorField_);

    nameEdit_ = new QLineEdit;
    nameEdit_->setMinimumWidth(384);  // keeps the fixed dialog ~440px wide
    layout->addWidget(makeField(tr("Name"), nameEdit_, true));

    loginEdit_ = new QLineEdit;
    passwordEdit_ = new QLineEdit;
    credentialsField_ = new QWidget;
    auto* credentialsLayout = new QHBoxLayout(credentialsField_);
    credentialsLayout->setContentsMargins(0, 0, 0, 0);
    credentialsLayout->setSpacing(12);
    loginField_ = makeField(tr("Login"), loginEdit_, true, &loginCaption_);
    passwordField_ = makeField(tr("Password"), passwordEdit_, false, &passwordCaption_);
    credentialsLayout->addWidget(loginField_, 1);
    credentialsLayout->addWidget(passwordField_, 1);
    layout->addWidget(credentialsField_);

    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("https://"));
    urlField_ = makeField(tr("URL"), urlEdit_, false, &urlCaption_);
    layout->addWidget(urlField_);

    extraFieldsContainer_ = new QWidget;
    extraFieldsLayout_ = new QVBoxLayout(extraFieldsContainer_);
    extraFieldsLayout_->setContentsMargins(0, 0, 0, 0);
    extraFieldsLayout_->setSpacing(14);
    layout->addWidget(extraFieldsContainer_);

    if (allowCustomFields_) {
        addFieldButton_ = new QPushButton(tr("+ Add custom field"));
        addFieldButton_->setObjectName(QStringLiteral("customFieldAdd"));
        addFieldButton_->setCursor(Qt::PointingHandCursor);
        connect(addFieldButton_, &QPushButton::clicked, this,
                [this] { addExtraField({}, false, true); });
        layout->addWidget(addFieldButton_, 0, Qt::AlignLeft);
    }

    noteEdit_ = new QPlainTextEdit;
    noteEdit_->setFixedHeight(34);
    noteEdit_->setTabChangesFocus(true);
    noteField_ = makeField(tr("Note"), noteEdit_);
    layout->addWidget(noteField_);

    layout->addSpacing(6);
    saveButton_ = new QPushButton(mode == Mode::Add ? tr("Add") : tr("Save"));
    saveButton_->setObjectName(QStringLiteral("primaryButton"));
    saveButton_->setDefault(true);
    saveButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(saveButton_);

    connect(saveButton_, &QPushButton::clicked, this, &QDialog::accept);

    // Name and the preset's visible login-equivalent field are required.
    saveButton_->setEnabled(false);
    connect(nameEdit_, &QLineEdit::textChanged, this,
            [this] { refreshSaveAvailability(); });
    connect(loginEdit_, &QLineEdit::textChanged, this,
            [this] { refreshSaveAvailability(); });

    setPreset(EntryPreset::Classic, false);

    // Classic is the immutable window geometry. Longer presets and
    // any number of custom fields scroll inside this exact viewport.
    layout->activate();
    form->adjustSize();
    const QSize classicSize = form->sizeHint();
    // Preset-specific labels and controls must never change the
    // horizontal geometry established by Classic.
    form->setFixedWidth(classicSize.width());
    scroll_->setFixedSize(classicSize);
    setFixedSize(classicSize);
    colorField_->setVisible(showEntryColors);
}

void EntryEditDialog::setEntry(const nightlock::Entry& entry) {
    setPreset(entry.preset, true);
    if (entry.preset == EntryPreset::CryptoWallet) {
        QString assetId = QStringLiteral("btc");
        for (const nightlock::EntryField& field : entry.fields) {
            if (!field.custom && field.label == "Coin") {
                assetId = toQString(field.value);
                break;
            }
        }
        setCryptoAsset(assetId);
    }
    nameEdit_->setText(QString::fromStdString(entry.name));
    loginEdit_->setText(QString::fromStdString(entry.login));
    passwordEdit_->setText(toQString(entry.password));
    urlEdit_->setText(QString::fromStdString(entry.url));
    noteEdit_->setPlainText(QString::fromStdString(entry.note));
    iconPicker_->setSelectedIconValue(QString::fromStdString(entry.icon));
    patternPicker_->setValue(entry.pattern);
    colorPicker_->setValue(entry.color);

    // Preset fields are matched onto the current built-in schema so a
    // later app version may add a field without losing older values.
    for (const nightlock::EntryField& field : entry.fields) {
        const QString label = QString::fromStdString(field.label);
        if (entry.preset == EntryPreset::CryptoWallet && !field.custom &&
            label != QLatin1String("Coin")) {
            bool isWord = false;
            label.toInt(&isWord);
            if (!isWord)
                continue;  // retired pre-seedphrase crypto fields
        }
        auto existing = std::find_if(extraFields_.begin(), extraFields_.end(),
                                     [&field, &label](const ExtraFieldEditor& editor) {
                                         return !field.custom && !editor.custom &&
                                                editor.fixedLabel == label;
                                     });
        if (existing != extraFields_.end()) {
            existing->valueEdit->setText(toQString(field.value));
            continue;
        }
        addExtraField(label, field.secret, field.custom, toQString(field.value));
    }
}

void EntryEditDialog::applyTo(nightlock::Entry& entry) const {
    entry.name = nameEdit_->text().trimmed().toStdString();
    entry.login = loginEdit_->text().trimmed().toStdString();
    assignSecret(entry.password, passwordEdit_->text());
    entry.url = urlEdit_->text().trimmed().toStdString();
    entry.note = noteEdit_->toPlainText().trimmed().toStdString();
    entry.icon = iconPicker_->selectedIconValue().toStdString();
    entry.pattern = patternPicker_->value();
    entry.color = colorPicker_->value();
    entry.preset = preset_;
    entry.fields.clear();
    for (const ExtraFieldEditor& editor : extraFields_) {
        const QString label = editor.custom ? editor.labelEdit->text().trimmed()
                                            : editor.fixedLabel;
        if (label.isEmpty())
            continue;
        nightlock::EntryField field;
        field.label = label.toStdString();
        QString value = editor.valueEdit->text();
        if (const auto* expirationEdit =
                dynamic_cast<const ExpirationLineEdit*>(editor.valueEdit)) {
            value = expirationEdit->valueForStorage();
        }
        assignSecret(field.value, value);
        field.secret = editor.secretToggle ? editor.secretToggle->isChecked()
                                           : editor.fixedSecret;
        field.custom = editor.custom;
        entry.fields.push_back(std::move(field));
    }
}

QMenu* EntryEditDialog::openPatternMenuForScreenshot() {
    return patternPicker_->openMenu();
}

void EntryEditDialog::setPreset(EntryPreset preset, bool resetValues) {
    std::vector<std::tuple<QString, QString, bool>> customValues;
    if (mode_ == Mode::Add && resetValues && allowCustomFields_) {
        for (const ExtraFieldEditor& editor : extraFields_) {
            if (editor.custom)
                customValues.emplace_back(editor.labelEdit->text(), editor.valueEdit->text(),
                                          editor.secretToggle->isChecked());
        }
    }

    preset_ = preset;

    bool showLogin = true;
    bool showPassword = true;
    bool showUrl = true;
    QString login = tr("Login");
    QString password = tr("Password");
    QString url = tr("URL");
    switch (preset) {
        case EntryPreset::Classic:
            break;
        case EntryPreset::Wifi:
            login = tr("SSID");
            showUrl = false;
            break;
        case EntryPreset::BankCard:
            login = tr("Card Number");
            password = tr("PIN");
            break;
        case EntryPreset::BrowserBookmark:
            showLogin = false;
            showPassword = false;
            break;
        case EntryPreset::CryptoWallet:
            showLogin = false;
            showPassword = false;
            showUrl = false;
            break;
    }
    loginCaption_->setText(
        login + QStringLiteral(" <span style=\"color:#FF3B30;\">*</span>"));
    passwordCaption_->setText(password);
    urlCaption_->setText(url);
    loginField_->setVisible(showLogin);
    passwordField_->setVisible(showPassword);
    credentialsField_->setVisible(showLogin || showPassword);
    urlField_->setVisible(showUrl);
    noteField_->show();

    if (resetValues) {
        loginEdit_->clear();
        passwordEdit_->clear();
        urlEdit_->clear();
    }
    clearExtraFields();
    addPresetFields(preset);
    for (const auto& [label, value, secret] : customValues)
        addExtraField(label, secret, true, value);
    refreshSaveAvailability();

    // A long preset can leave the overlay viewport scrolled down.
    // Rebuilding another preset must always restore the same top and
    // side margins as Classic.
    QTimer::singleShot(0, scroll_, [this] {
        scroll_->verticalScrollBar()->setValue(0);
        scroll_->horizontalScrollBar()->setValue(0);
    });
}

void EntryEditDialog::refreshSaveAvailability() {
    const bool nameReady = !nameEdit_->text().trimmed().isEmpty();
    const bool loginReady = !loginField_->isVisible() ||
                            !loginEdit_->text().trimmed().isEmpty();
    const bool expirationReady = std::all_of(
        extraFields_.begin(), extraFields_.end(), [](const ExtraFieldEditor& editor) {
            const auto* expirationEdit =
                dynamic_cast<const ExpirationLineEdit*>(editor.valueEdit);
            return !expirationEdit || expirationEdit->validOrEmpty();
        });
    saveButton_->setEnabled(nameReady && loginReady && expirationReady);
}

void EntryEditDialog::addPresetFields(EntryPreset preset) {
    switch (preset) {
        case EntryPreset::Classic:
            break;
        case EntryPreset::Wifi:
            addExtraField(QStringLiteral("BSSID"), false, false);
            break;
        case EntryPreset::BankCard:
            addExtraField(QStringLiteral("Cardholder Name"), false, false);
            addExtraField(QStringLiteral("CVV"), true, false);
            break;
        case EntryPreset::BrowserBookmark:
            break;
        case EntryPreset::CryptoWallet:
            addCryptoFields(QStringLiteral("btc"));
            break;
    }
}

void EntryEditDialog::addCryptoFields(const QString& assetId) {
    const int assetIndex = cryptoAssetIndex(assetId);
    const CryptoAsset& asset = kCryptoAssets[assetIndex];

    auto* assetDropdown = new EntryDropdownButton(cryptoAssetTitles(), assetIndex);
    assetDropdown->setOnSelected([this](int index) {
        const QString id = QLatin1String(kCryptoAssets[index].id);
        // The dropdown owns the signal currently being delivered; do
        // the form rebuild after the menu/action stack has unwound.
        QTimer::singleShot(0, this, [this, id] { setCryptoAsset(id); });
    });

    ExtraFieldEditor coin;
    coin.fixedLabel = QStringLiteral("Coin");
    coin.valueEdit = new QLineEdit;
    coin.valueEdit->setText(QLatin1String(asset.id));
    coin.valueEdit->hide();
    coin.row = makeField(tr("Coin"), assetDropdown);
    coin.valueEdit->setParent(coin.row);
    extraFields_.push_back(coin);
    pendingPresetRow_ = nullptr;
    extraFieldsLayout_->addWidget(coin.row);
    extraLayoutRows_.push_back(coin.row);
    extraFieldsContainer_->show();

    auto* seedHeader = new QLabel(tr("Seedphrase"));
    seedHeader->setObjectName(QStringLiteral("dialogFieldLabel"));
    extraFieldsLayout_->addWidget(seedHeader);
    extraLayoutRows_.push_back(seedHeader);

    for (int word = 1; word <= asset.seedWords; ++word)
        addExtraField(QString::number(word), true, false);
}

void EntryEditDialog::setCryptoAsset(const QString& assetId) {
    std::vector<std::tuple<QString, QString, bool>> customValues;
    for (const ExtraFieldEditor& editor : extraFields_) {
        if (editor.custom)
            customValues.emplace_back(editor.labelEdit->text(), editor.valueEdit->text(),
                                      editor.secretToggle->isChecked());
    }
    clearExtraFields();
    addCryptoFields(QLatin1String(kCryptoAssets[cryptoAssetIndex(assetId)].id));
    for (const auto& [label, value, secret] : customValues)
        addExtraField(label, secret, true, value);
}

void EntryEditDialog::addExtraField(const QString& label, bool secret, bool custom,
                                    const QString& value) {
    ExtraFieldEditor editor;
    editor.custom = custom;
    editor.fixedLabel = label;
    editor.fixedSecret = secret;
    editor.valueEdit = custom ? static_cast<QLineEdit*>(new ExpirationLineEdit)
                              : new QLineEdit;
    editor.valueEdit->setText(value);

    if (!custom) {
        editor.row = makeField(label, editor.valueEdit);
    } else {
        editor.row = new QWidget;
        auto* rowLayout = new QHBoxLayout(editor.row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        editor.labelEdit = new QLineEdit;
        editor.labelEdit->setPlaceholderText(tr("Field name"));
        editor.labelEdit->setText(label);
        editor.valueEdit->setPlaceholderText(tr("Value"));
        editor.secretToggle = new SecretCheckBox(tr("Secret"));
        editor.secretToggle->setChecked(secret);
        editor.removeButton = new QPushButton(QStringLiteral("×"));
        editor.removeButton->setObjectName(QStringLiteral("customFieldRemove"));
        editor.removeButton->setToolTip(tr("Remove custom field"));
        editor.removeButton->setCursor(Qt::PointingHandCursor);
        editor.removeButton->setVisible(allowCustomFields_);
        editor.labelEdit->setReadOnly(!allowCustomFields_);
        editor.valueEdit->setReadOnly(!allowCustomFields_);
        editor.secretToggle->setEnabled(allowCustomFields_);
        rowLayout->addWidget(editor.labelEdit, 1);
        rowLayout->addWidget(editor.valueEdit, 1);
        rowLayout->addWidget(editor.secretToggle);
        rowLayout->addWidget(editor.removeButton);

        auto* expirationEdit = static_cast<ExpirationLineEdit*>(editor.valueEdit);
        const auto syncExpirationMode = [expirationEdit](const QString& fieldLabel) {
            expirationEdit->setExpirationMode(isExpirationLabel(fieldLabel));
        };
        connect(editor.labelEdit, &QLineEdit::textChanged, this,
                [this, syncExpirationMode](const QString& fieldLabel) {
                    syncExpirationMode(fieldLabel);
                    refreshSaveAvailability();
                });
        connect(editor.valueEdit, &QLineEdit::textChanged, this,
                [this] { refreshSaveAvailability(); });
        syncExpirationMode(editor.labelEdit->text());
    }

    QWidget* row = editor.row;
    if (editor.removeButton)
        connect(editor.removeButton, &QPushButton::clicked, this,
                [this, row] { removeExtraField(row); });
    extraFields_.push_back(editor);
    if (!custom) {
        if (!pendingPresetRow_) {
            auto* pair = new QWidget;
            pendingPresetRow_ = new QHBoxLayout(pair);
            pendingPresetRow_->setContentsMargins(0, 0, 0, 0);
            pendingPresetRow_->setSpacing(12);
            extraFieldsLayout_->addWidget(pair);
            extraLayoutRows_.push_back(pair);
        }
        pendingPresetRow_->addWidget(row, 1);
        if (pendingPresetRow_->count() == 2)
            pendingPresetRow_ = nullptr;
    } else {
        pendingPresetRow_ = nullptr;
        extraFieldsLayout_->addWidget(row);
        extraLayoutRows_.push_back(row);
    }
    extraFieldsContainer_->setVisible(true);
}

void EntryEditDialog::clearExtraFields() {
    for (QWidget* row : extraLayoutRows_)
        delete row;
    extraFields_.clear();
    extraLayoutRows_.clear();
    pendingPresetRow_ = nullptr;
    extraFieldsContainer_->hide();
}

void EntryEditDialog::removeExtraField(QWidget* row) {
    const auto it = std::find_if(extraFields_.begin(), extraFields_.end(),
                                 [row](const ExtraFieldEditor& editor) {
                                     return editor.row == row;
                                 });
    if (it == extraFields_.end() || !it->custom || !allowCustomFields_)
        return;
    delete it->row;
    extraLayoutRows_.erase(
        std::remove(extraLayoutRows_.begin(), extraLayoutRows_.end(), row),
        extraLayoutRows_.end());
    extraFields_.erase(it);
    extraFieldsContainer_->setVisible(!extraFields_.empty());
    refreshSaveAvailability();
}

QWidget* EntryEditDialog::makeField(const QString& label, QWidget* editor, bool required,
                                    QLabel** captionOut) {
    auto* field = new QWidget;
    auto* fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(5);

    auto* caption = new QLabel(
        required ? label + QStringLiteral(" <span style=\"color:#FF3B30;\">*</span>") : label);
    caption->setTextFormat(required ? Qt::RichText : Qt::PlainText);
    caption->setObjectName(QStringLiteral("dialogFieldLabel"));
    if (captionOut)
        *captionOut = caption;
    fieldLayout->addWidget(caption);
    fieldLayout->addWidget(editor);
    return field;
}
