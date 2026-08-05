#include "entrydetailview.hpp"

#include <QDateTime>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "appearancesettings.hpp"
#include "graphsettings.hpp"

#include <nightlock/entry.hpp>

#include "copylabel.hpp"
#include "overlayscrollbar.hpp"
#include "qsecure.hpp"
#include "patternbackdrop.hpp"
#include "spoilerlabel.hpp"
#include "totpring.hpp"

namespace {

constexpr int kIconSize = 58;
constexpr int kDetachThreshold = 12;  // px of grip travel before undocking
constexpr int kGripHeight = 22;
constexpr int kGripGap = 12;          // equal gap above and below the grip
constexpr int kSectionGap = 14;       // one vertical rhythm between all sections
constexpr qreal kFloatingRadius = 10;  // matches the main window corners

QString formatDate(std::chrono::system_clock::time_point tp) {
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("dd.MM.yyyy"));
}

// The six-dot grip (two rows of three) that tears the panel off.
class DragHandle : public QWidget {
public:
    explicit DragHandle(EntryDetailView* view) : QWidget(view), view_(view) {
        setFixedSize(46, kGripHeight);
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        // Same shade as the header icon strokes.
        painter.setBrush(appearancesettings::palette().ink);
        constexpr qreal kDotRadius = 2.6;
        constexpr int kStep = 12;
        const qreal left = width() / 2.0 - kStep;
        const qreal top = height() / 2.0 - kStep / 2.0;
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 3; ++column)
                painter.drawEllipse(QPointF(left + column * kStep, top + row * kStep),
                                    kDotRadius, kDotRadius);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = true;
        setCursor(Qt::ClosedHandCursor);
        view_->gripPressed(event->globalPosition().toPoint());
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed_)
            view_->gripDragged(event->globalPosition().toPoint());
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!pressed_)
            return;
        pressed_ = false;
        setCursor(Qt::OpenHandCursor);
        view_->gripReleased(event->globalPosition().toPoint());
    }

private:
    EntryDetailView* view_;
    bool pressed_ = false;
};

// Bottom-most layer of the floating window: the rounded white panel
// (the scroll area itself cannot paint outside its viewport).
class FloatingBackdrop : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(appearancesettings::palette().border);
        painter.setBrush(appearancesettings::palette().window);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                kFloatingRadius, kFloatingRadius);
    }
};

// Traffic lights of the floating window: red docks the panel back into
// the main window, yellow and green are decorative for now.
class FloatingControls : public QWidget {
public:
    explicit FloatingControls(EntryDetailView* view) : QWidget(view), view_(view) {
        setFixedSize(3 * kDot + 2 * kGap, kDot);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const QColor colors[] = {QColor(0xFF, 0x5F, 0x57), QColor(0xFE, 0xBC, 0x2E),
                                 QColor(0x28, 0xC8, 0x40)};
        for (int i = 0; i < 3; ++i) {
            painter.setBrush(colors[i]);
            painter.drawEllipse(QRectF(i * (kDot + kGap), 0, kDot, kDot));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        if (event->position().x() < kDot)  // the red one
            emit view_->dockRequested();
    }

private:
    static constexpr int kDot = 12;
    static constexpr int kGap = 8;
    EntryDetailView* view_;
};

}  // namespace

EntryDetailView::EntryDetailView(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    new OverlayScrollBar(this);

    content_ = new QWidget;
    auto* layout = new QVBoxLayout(content_);
    // Top room for the grip strip: the same gap above and below it.
    layout->setContentsMargins(30, kGripGap * 2 + kGripHeight, 30, 28);
    layout->setSpacing(0);

    iconLabel_ = new QLabel;
    iconLabel_->setAlignment(Qt::AlignCenter);
    // Multi-size icons may resolve below 58px (e.g. 48px .ico packs);
    // a fixed height keeps the header from shifting.
    iconLabel_->setFixedHeight(kIconSize);
    layout->addWidget(iconLabel_);
    layout->addSpacing(kSectionGap);

    titleLabel_ = new QLabel;
    titleLabel_->setObjectName(QStringLiteral("detailTitle"));
    titleLabel_->setAlignment(Qt::AlignHCenter);
    layout->addWidget(titleLabel_);
    layout->addSpacing(8);

    noteLabel_ = new QLabel;
    noteLabel_->setObjectName(QStringLiteral("detailNote"));
    noteLabel_->setAlignment(Qt::AlignHCenter);
    noteLabel_->setWordWrap(true);
    layout->addWidget(noteLabel_);
    layout->addSpacing(kSectionGap);

    fieldsCard_ = new QFrame;
    fieldsCard_->setObjectName(QStringLiteral("card"));
    fieldsLayout_ = new QVBoxLayout(fieldsCard_);
    fieldsLayout_->setContentsMargins(16, 2, 16, 2);
    fieldsLayout_->setSpacing(0);
    loginRow_ = makeRow(fieldsLayout_, tr("Login"));
    // The login copies on click with the same "Copied" flash as the
    // password (just without the spoiler).
    loginRow_.value->hide();
    loginCopy_ = new CopyLabel;
    loginRow_.layout->addWidget(loginCopy_);
    passwordRow_ = makeRow(fieldsLayout_, tr("Password"));
    // The password hides behind a Telegram-style particle spoiler
    // instead of asterisks.
    passwordRow_.value->hide();
    passwordSpoiler_ = new SpoilerLabel;
    passwordRow_.layout->addWidget(passwordSpoiler_);
    urlRow_ = makeRow(fieldsLayout_, tr("URL"));
    urlRow_.value->setTextFormat(Qt::RichText);
    urlRow_.value->setTextInteractionFlags(Qt::TextBrowserInteraction);
    urlRow_.value->setOpenExternalLinks(true);
    codeRow_ = makeRow(fieldsLayout_, tr("Code"));
    codeRow_.layout->insertWidget(codeRow_.layout->count() - 1, new TotpRing);
    layout->addWidget(fieldsCard_);
    layout->addSpacing(kSectionGap);

    seedSection_ = new QWidget;
    auto* seedSectionLayout = new QVBoxLayout(seedSection_);
    seedSectionLayout->setContentsMargins(0, 0, 0, 0);
    seedSectionLayout->setSpacing(12);
    seedHeader_ = new QLabel(tr("Seedphrase"));
    seedHeader_->setObjectName(QStringLiteral("metaHeader"));
    seedSectionLayout->addWidget(seedHeader_);
    seedCard_ = new QFrame;
    seedCard_->setObjectName(QStringLiteral("seedStack"));
    auto* seedCardLayout = new QVBoxLayout(seedCard_);
    seedCardLayout->setContentsMargins(0, 0, 0, 0);
    seedCardLayout->setSpacing(0);
    auto* seedCopyInset = new QFrame;
    seedCopyInset->setObjectName(QStringLiteral("seedCopyInset"));
    auto* seedCopyInsetLayout = new QHBoxLayout(seedCopyInset);
    seedCopyInsetLayout->setContentsMargins(18, 0, 18, 0);
    seedCopyInsetLayout->setSpacing(0);
    auto* seedCopyBar = new QFrame;
    seedCopyBar->setObjectName(QStringLiteral("seedCopyBar"));
    auto* seedCopyLayout = new QHBoxLayout(seedCopyBar);
    seedCopyLayout->setContentsMargins(14, 7, 14, 7);
    seedCopyLayout->setSpacing(0);
    seedCopy_ = new CopyLabel;
    seedCopy_->setText(tr("Copy phrase"));
    seedCopy_->setLeadingIconVisible(true);
    seedCopy_->setContentAlignment(Qt::AlignCenter);
    seedCopy_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    seedCopyLayout->addWidget(seedCopy_);
    seedCopyInsetLayout->addWidget(seedCopyBar);
    seedCardLayout->addWidget(seedCopyInset);
    auto* seedRows = new QFrame;
    seedRows->setObjectName(QStringLiteral("seedWordsCard"));
    seedFieldsLayout_ = new QVBoxLayout(seedRows);
    seedFieldsLayout_->setContentsMargins(16, 2, 16, 2);
    seedFieldsLayout_->setSpacing(0);
    seedCardLayout->addWidget(seedRows);
    seedSectionLayout->addWidget(seedCard_);
    seedSectionLayout->addSpacing(kSectionGap);
    layout->addWidget(seedSection_);
    seedSection_->hide();

    const auto makeSection = [layout](const QString& title, QWidget*& section,
                                      QLabel*& header, QVBoxLayout*& rows) {
        section = new QWidget;
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(12);
        header = new QLabel(title);
        header->setObjectName(QStringLiteral("metaHeader"));
        sectionLayout->addWidget(header);
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        rows = new QVBoxLayout(card);
        rows->setContentsMargins(16, 2, 16, 2);
        rows->setSpacing(0);
        sectionLayout->addWidget(card);
        sectionLayout->addSpacing(kSectionGap);
        layout->addWidget(section);
        section->hide();
    };
    QLabel* customHeader = nullptr;
    makeSection(tr("Custom"), customSection_, customHeader, customFieldsLayout_);

    metaHeader_ = new QLabel(tr("Meta"));
    metaHeader_->setObjectName(QStringLiteral("metaHeader"));
    layout->addWidget(metaHeader_);
    layout->addSpacing(12);

    auto* metaCard = new QFrame;
    metaCard->setObjectName(QStringLiteral("card"));
    auto* metaLayout = new QVBoxLayout(metaCard);
    metaLayout->setContentsMargins(16, 2, 16, 2);
    metaLayout->setSpacing(0);
    createdRow_ = makeRow(metaLayout, tr("Created"));
    modifiedRow_ = makeRow(metaLayout, tr("Modified"), true);
    layout->addWidget(metaCard);
    layout->addSpacing(kSectionGap);

    // Full-width black pill jumping to this entry's node in the graph.
    graphButton_ = new QPushButton(tr("Show in NetGraph"));
    graphButton_->setObjectName(QStringLiteral("showInGraph"));
    graphButton_->setCursor(Qt::PointingHandCursor);
    // The toolbar's graph glyph, recolored to the accent's text color
    // for the accent pill; re-tinted when the theme or accent changes.
    const auto tintGraphGlyph = [this] {
        QPixmap glyph =
            QIcon(QStringLiteral(":/icons/menu/graph.svg")).pixmap(QSize(34, 34));
        QPainter tint(&glyph);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(glyph.rect(), appearancesettings::accentTextColor());
        tint.end();
        glyph.setDevicePixelRatio(2.0);
        graphButton_->setIcon(QIcon(glyph));
    };
    tintGraphGlyph();
    connect(appearancesettings::notifier(), &appearancesettings::Notifier::changed, this,
            [this, tintGraphGlyph] {
                tintGraphGlyph();
                refreshUrlText();
            });
    graphButton_->setIconSize(QSize(17, 17));
    connect(graphButton_, &QPushButton::clicked, this, &EntryDetailView::graphRequested);
    const auto syncGraphButton = [this] {
        graphButton_->setVisible(!graphsettings::hideButton());
    };
    connect(graphsettings::notifier(), &graphsettings::Notifier::changed, this,
            syncGraphButton);
    syncGraphButton();
    layout->addWidget(graphButton_);

    layout->addStretch(1);

    // Child of the content (not the scroll area): in docked mode the
    // content paints the white panel, so a sibling below the viewport
    // would never show through. Lowered under every field widget, the
    // pattern sits behind the icon and scrolls with the entry.
    patternBackdrop_ = new PatternBackdrop(content_);
    patternBackdrop_->lower();

    setWidget(content_);

    // Overlay child of the scroll area itself (not the content), so it
    // stays put while scrolling and remains visible with no entry.
    grip_ = new DragHandle(this);
    grip_->raise();

    // Pencil in the top-right corner, level with the grip; hidden
    // together with the content when no entry is shown.
    const auto makeCornerButton = [this](const QString& icon, const QString& toolTip) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("headerIconButton"));
        button->setIcon(appearancesettings::themedMenuIcon(icon));
        button->setIconSize(QSize(17, 17));
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(toolTip);
        button->raise();
        return button;
    };
    editButton_ = makeCornerButton(QStringLiteral("edit"), tr("Edit entry"));
    connect(editButton_, &QToolButton::clicked, this, &EntryDetailView::editRequested);

    floatingControls_ = new FloatingControls(this);
    floatingControls_->move(12, 12);
    floatingControls_->raise();
    floatingControls_->hide();

    floatingBackdrop_ = new FloatingBackdrop(this);
    floatingBackdrop_->lower();
    floatingBackdrop_->hide();

    setEntry(nullptr);
}

void EntryDetailView::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    grip_->move((width() - grip_->width()) / 2, kGripGap);
    // Centered on the grip row, mirroring the floating traffic lights.
    const int buttonY = kGripGap + (kGripHeight - editButton_->height()) / 2;
    editButton_->move(width() - editButton_->width() - 14, buttonY);
    floatingBackdrop_->setGeometry(rect());
    updatePatternGeometry();
}

// The pattern zone runs from the very top of the panel (the grip strip)
// down to the fields card; the card position depends on the optional
// note, so this is re-run after every setEntry as well as on resize.
void EntryDetailView::updatePatternGeometry() {
    if (auto* layout = content_->layout())
        layout->activate();  // the card must sit at its final position
    // A fieldless entry hides the whole card; its stale geometry can't
    // anchor the zone, so the Meta header takes over.
    const QWidget* below = !fieldsCard_->isHidden()
                               ? static_cast<const QWidget*>(fieldsCard_)
                           : !seedSection_->isHidden()
                               ? seedSection_
                           : !customSection_->isHidden()
                               ? customSection_
                               : static_cast<const QWidget*>(metaHeader_);
    patternBackdrop_->setGeometry(0, 0, content_->width(), below->geometry().top());
    patternBackdrop_->setIconCenterY(iconLabel_->geometry().center().y());
}

void EntryDetailView::setFloatingMode(bool floating) {
    setAttribute(Qt::WA_TranslucentBackground, floating);
    setProperty("floatingWindow", floating);
    floatingControls_->setVisible(floating);
    floatingBackdrop_->setGeometry(rect());
    floatingBackdrop_->setVisible(floating);
    floatingBackdrop_->lower();
    // Re-evaluate the [floatingWindow] stylesheet selectors.
    style()->unpolish(this);
    style()->polish(this);
    content_->style()->unpolish(content_);
    content_->style()->polish(content_);
}

void EntryDetailView::gripPressed(const QPoint& globalPos) {
    pressGlobal_ = globalPos;
    if (isWindow())
        grabOffset_ = globalPos - pos();
}

void EntryDetailView::gripDragged(const QPoint& globalPos) {
    if (isWindow()) {
        move(globalPos - grabOffset_);
        return;
    }
    if ((globalPos - pressGlobal_).manhattanLength() > kDetachThreshold)
        emit detachRequested(globalPos);
}

void EntryDetailView::gripReleased(const QPoint& globalPos) {
    if (isWindow())
        emit dropped(globalPos);
}

void EntryDetailView::beginFloatingDrag(const QPoint& globalPos) {
    grabOffset_ = globalPos - pos();
}

void EntryDetailView::debugSpoiler(const QString& state) {
    passwordSpoiler_->reveal();
    if (state == QLatin1String("copied"))
        passwordSpoiler_->copyAndFlash();
    if (state == QLatin1String("cycle")) {
        // Repeated transitions with completed animations in between —
        // the exact path that used to hit a dangling animation pointer.
        QTimer::singleShot(250, passwordSpoiler_, [this] { passwordSpoiler_->conceal(); });
        QTimer::singleShot(500, passwordSpoiler_, [this] { passwordSpoiler_->reveal(); });
    }
    if (state == QLatin1String("login-copied"))
        loginCopy_->copyAndFlash();
}

// Ink from the palette, not a hardcoded near-black: the link must
// stay readable on the dark theme too, and it re-colors on a switch.
void EntryDetailView::refreshUrlText() {
    if (url_.isEmpty())
        return;
    urlRow_.value->setText(QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%1</a> ↗")
                               .arg(url_, appearancesettings::palette().ink.name()));
}

void EntryDetailView::setEntry(const nightlock::Entry* entry) {
    content_->setVisible(entry != nullptr);
    editButton_->setVisible(entry != nullptr);
    if (!entry)
        return;

    const QString iconPath = entry->icon.empty() ? QStringLiteral(":/icons/entry.png")
                                                 : QString::fromStdString(entry->icon);
    // Select the variant through QIcon, exactly like the entry list
    // does: pack .ico files hold several sizes and color depths, and
    // QPixmap would load only the first sub-image — often a different
    // rendition than the one the list shows.
    iconLabel_->setPixmap(QIcon(iconPath).pixmap(QSize(kIconSize, kIconSize),
                                                 iconLabel_->devicePixelRatioF()));

    titleLabel_->setText(QString::fromStdString(entry->name));
    applyPresetLabels(static_cast<int>(entry->preset));

    noteLabel_->setVisible(!entry->note.empty());
    noteLabel_->setText(QString::fromStdString(entry->note));

    // Only filled fields get a row — a name-only entry shows no card
    // at all instead of a stack of blanks.
    loginRow_.frame->setVisible(!entry->login.empty());
    loginCopy_->setText(QString::fromStdString(entry->login));

    passwordRow_.frame->setVisible(!entry->password.empty());
    passwordSpoiler_->setSecret(toQString(entry->password));

    urlRow_.frame->setVisible(!entry->url.empty());
    url_ = QString::fromStdString(entry->url);
    refreshUrlText();

    codeRow_.frame->setVisible(!entry->code.empty());
    codeRow_.value->setText(toQString(entry->code));

    clearAdditionalFields();
    QString cryptoAsset;
    QStringList seedWords;
    const auto populateRow = [](FieldRow& row, const nightlock::EntryField& field) {
        if (field.secret) {
            row.value->hide();
            auto* spoiler = new SpoilerLabel;
            spoiler->setSecret(toQString(field.value));
            row.layout->addWidget(spoiler);
        } else {
            row.value->setText(toQString(field.value));
        }
    };
    for (const nightlock::EntryField& field : entry->fields) {
        if (field.value.empty())
            continue;
        if (entry->preset == nightlock::EntryPreset::CryptoWallet && !field.custom &&
            field.label == "Coin") {
            cryptoAsset = toQString(field.value);
            continue;
        }
        if (entry->preset == nightlock::EntryPreset::CryptoWallet && !field.custom) {
            bool isWord = false;
            QString::fromStdString(field.label).toInt(&isWord);
            if (!isWord)
                continue;
        }
        if (field.custom) {
            FieldRow row = makeRow(customFieldsLayout_, QString::fromStdString(field.label));
            populateRow(row, field);
            customRows_.append(row);
        } else if (entry->preset == nightlock::EntryPreset::CryptoWallet) {
            FieldRow row = makeRow(seedFieldsLayout_, QString::fromStdString(field.label));
            row.value->hide();
            auto* spoiler = new SpoilerLabel;
            spoiler->setSecret(toQString(field.value));
            spoiler->setCoordinatedReveal(true);
            connect(spoiler, &SpoilerLabel::revealRequested, this, [this] {
                for (SpoilerLabel* seedSpoiler : seedSpoilers_)
                    seedSpoiler->reveal();
            });
            row.layout->addWidget(spoiler);
            seedSpoilers_.append(spoiler);
            seedWords.append(toQString(field.value));
            seedRows_.append(row);
        } else {
            FieldRow row = makeRow(fieldsLayout_, QString::fromStdString(field.label));
            populateRow(row, field);
            additionalRows_.append(row);
        }
    }

    const auto cryptoTitle = [](const QString& id) {
        if (id == QLatin1String("eth")) return QStringLiteral("Ethereum");
        if (id == QLatin1String("sol")) return QStringLiteral("Solana");
        if (id == QLatin1String("bnb")) return QStringLiteral("BNB (BNB Smart Chain)");
        if (id == QLatin1String("usdt-erc20")) return QStringLiteral("USDT (Ethereum ERC-20)");
        if (id == QLatin1String("usdt-trc20")) return QStringLiteral("USDT (Tron TRC-20)");
        if (id == QLatin1String("ada")) return QStringLiteral("Cardano");
        if (id == QLatin1String("xmr")) return QStringLiteral("Monero");
        return QStringLiteral("Bitcoin");
    };
    if (entry->preset == nightlock::EntryPreset::CryptoWallet && !cryptoAsset.isEmpty()) {
        FieldRow row = makeRow(fieldsLayout_, tr("Currency"));
        row.value->setText(cryptoTitle(cryptoAsset));
        additionalRows_.append(row);
    }
    seedHeader_->setText(tr("Seedphrase"));
    seedCopy_->setText(tr("Copy phrase"));
    seedCopy_->setClipboardText(seedWords.join(QLatin1Char(' ')));
    seedSection_->setVisible(!seedRows_.isEmpty());
    customSection_->setVisible(!customRows_.isEmpty());

    fieldsCard_->setVisible(!entry->login.empty() || !entry->password.empty() ||
                            !entry->url.empty() || !entry->code.empty() ||
                            !additionalRows_.isEmpty());

    createdRow_.value->setText(formatDate(entry->created));
    modifiedRow_.value->setText(formatDate(entry->modified));

    refreshLastVisibleRow();

    patternBackdrop_->setEntry(entry);
    updatePatternGeometry();
}

// The last visible row of the fields card must not draw its bottom
// border; which row that is depends on the entry's optional fields.
void EntryDetailView::refreshLastVisibleRow() {
    QList<FieldRow> rows = {loginRow_, passwordRow_, urlRow_, codeRow_};
    for (const FieldRow& row : additionalRows_)
        rows.append(row);

    const auto updateSeparators = [](const QList<FieldRow>& cardRows) {
        const QFrame* lastVisible = nullptr;
        for (const FieldRow& row : cardRows)
            if (!row.frame->isHidden())
                lastVisible = row.frame;
        for (const FieldRow& row : cardRows) {
            const bool show = !row.frame->isHidden() && row.frame != lastVisible;
            row.separator->setVisible(show);
        }
    };
    updateSeparators(rows);

    QList<FieldRow> seedFrames;
    for (const FieldRow& row : seedRows_)
        seedFrames.append(row);
    updateSeparators(seedFrames);

    QList<FieldRow> customFrames;
    for (const FieldRow& row : customRows_)
        customFrames.append(row);
    updateSeparators(customFrames);
}

void EntryDetailView::applyPresetLabels(int presetValue) {
    const auto preset = static_cast<nightlock::EntryPreset>(presetValue);
    QString login = tr("Login");
    QString password = tr("Password");
    QString url = tr("URL");
    switch (preset) {
        case nightlock::EntryPreset::Classic:
            break;
        case nightlock::EntryPreset::Wifi:
            login = tr("SSID");
            break;
        case nightlock::EntryPreset::BankCard:
            login = tr("Card Number");
            password = tr("PIN");
            break;
        case nightlock::EntryPreset::BrowserBookmark:
            break;
        case nightlock::EntryPreset::CryptoWallet:
            break;
    }
    loginRow_.name->setText(login);
    passwordRow_.name->setText(password);
    urlRow_.name->setText(url);
}

void EntryDetailView::clearAdditionalFields() {
    for (const FieldRow& row : additionalRows_)
        delete row.frame;
    for (const FieldRow& row : seedRows_)
        delete row.frame;
    for (const FieldRow& row : customRows_)
        delete row.frame;
    additionalRows_.clear();
    seedRows_.clear();
    seedSpoilers_.clear();
    customRows_.clear();
    seedSection_->hide();
    customSection_->hide();
}

EntryDetailView::FieldRow EntryDetailView::makeRow(QVBoxLayout* cardLayout,
                                                   const QString& label, bool last) {
    FieldRow row;
    row.frame = new QFrame;
    row.frame->setObjectName(QStringLiteral("fieldRow"));
    auto* rowLayout = new QVBoxLayout(row.frame);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto* content = new QWidget;
    row.layout = new QHBoxLayout(content);
    row.layout->setContentsMargins(2, 13, 2, 13);
    row.layout->setSpacing(8);

    row.name = new QLabel(label);
    row.name->setObjectName(QStringLiteral("fieldLabel"));
    row.name->setTextFormat(Qt::PlainText);
    row.value = new QLabel;
    row.value->setObjectName(QStringLiteral("fieldValue"));
    row.value->setTextFormat(Qt::PlainText);
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.value->setTextInteractionFlags(Qt::TextSelectableByMouse);

    row.layout->addWidget(row.name);
    row.layout->addStretch(1);
    row.layout->addWidget(row.value);

    rowLayout->addWidget(content);
    row.separator = new QFrame;
    row.separator->setObjectName(QStringLiteral("fieldSeparator"));
    row.separator->setFixedHeight(1);
    row.separator->setVisible(!last);
    rowLayout->addWidget(row.separator);

    cardLayout->addWidget(row.frame);
    return row;
}
