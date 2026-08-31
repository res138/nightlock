#include "entrydetailview.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QGraphicsEffect>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include "appearancesettings.hpp"
#include "entrycolors.hpp"
#include "expirationui.hpp"
#include "generalsettings.hpp"
#include "graphsettings.hpp"
#include "iconreferences.hpp"
#include "iconpackmanager.hpp"

#include <nightlock/entry.hpp>

#include "copylabel.hpp"
#include "overlayscrollbar.hpp"
#include "overflowfade.hpp"
#include "qsecure.hpp"
#include "patternbackdrop.hpp"
#include "spoilerlabel.hpp"
#include "totpring.hpp"

#include <functional>
#include <utility>

namespace {

constexpr int kIconSize = 58;
constexpr int kDetachThreshold = 12;  // px of grip travel before undocking
constexpr int kGripHeight = 22;
constexpr int kGripGap = 12;          // equal gap above and below the grip
constexpr int kHeaderFadeHeight = 68;
constexpr int kSectionGap = 14;       // one vertical rhythm between all sections
constexpr qreal kFloatingRadius = 10;  // matches the main window corners

// Keep the reference design's 360 px card width whenever the pane has
// enough room.  Surplus width becomes symmetric breathing room, so a
// narrowing pane spends those insets first; only below the breakpoint
// does the card itself contract.  The final 180 px body still leaves
// room for a field name and a useful value.
constexpr int kPreferredBodyWidth = 360;
constexpr int kPreferredHorizontalInset = 30;
constexpr int kMinimumBodyWidth = 180;
constexpr int kMinimumHorizontalInset = 12;
constexpr int kMinimumPaneWidth = kMinimumBodyWidth + 2 * kMinimumHorizontalInset;

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

// Keeps the fixed grip/edit strip legible while the entry contents
// scroll underneath it. The solid top dissolves before it reaches the
// entry header, avoiding a hard toolbar edge.
class ScrollHeaderFade : public QWidget {
public:
    explicit ScrollHeaderFade(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(appearancesettings::notifier(), &appearancesettings::Notifier::changed,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QColor solid = appearancesettings::palette().window;
        QColor clear = solid;
        clear.setAlpha(0);
        QLinearGradient gradient(0, 0, 0, height());
        gradient.setColorAt(0.0, solid);
        gradient.setColorAt(0.52, solid);
        gradient.setColorAt(1.0, clear);
        QPainter(this).fillRect(rect(), gradient);
    }
};

// Alpha-masks only the pixels painted by a text widget, so the fade works
// over patterns, dark mode and per-entry card colors without guessing the
// background. The clipped edge follows the widget's alignment: centered
// titles lose pixels at both sides, right-aligned values at the left side.
class OverflowFadeEffect : public QGraphicsEffect {
public:
    using OverflowCheck = std::function<bool(qreal)>;

    OverflowFadeEffect(overflowfade::Edge edge, OverflowCheck overflows,
                       QObject* parent)
        : QGraphicsEffect(parent), edge_(edge), overflows_(std::move(overflows)) {
        setProperty("overflowFadeActive", false);
    }

protected:
    void draw(QPainter* painter) override {
        const QRectF bounds = sourceBoundingRect(Qt::LogicalCoordinates);
        const bool active = bounds.width() > 0 && overflows_(bounds.width());
        if (active != active_) {
            active_ = active;
            setProperty("overflowFadeActive", active_);
        }
        if (!active_) {
            drawSource(painter);
            return;
        }

        QPoint offset;
        QPixmap pixels =
            sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::NoPad);
        if (pixels.isNull()) {
            drawSource(painter);
            return;
        }

        const QSizeF logicalSize = pixels.deviceIndependentSize();
        const QRectF maskRect(QPointF(0, 0), logicalSize);
        const QColor opaque(0, 0, 0, 255);
        const QLinearGradient mask =
            overflowfade::gradient(logicalSize.width(), opaque, edge_);

        QPainter maskPainter(&pixels);
        maskPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        maskPainter.fillRect(maskRect, mask);
        maskPainter.end();
        painter->drawPixmap(offset, pixels);
    }

private:
    overflowfade::Edge edge_;
    OverflowCheck overflows_;
    bool active_ = false;
};

int naturalLabelWidth(const QLabel* label) {
    if (label->textFormat() == Qt::RichText) {
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setDefaultFont(label->font());
        document.setHtml(label->text());
        return qCeil(document.idealWidth());
    }
    return QFontMetrics(label->font()).horizontalAdvance(label->text());
}

void addOverflowFade(QWidget* widget, overflowfade::Edge edge,
                     std::function<int()> naturalWidth) {
    widget->setGraphicsEffect(new OverflowFadeEffect(
        edge,
        [naturalWidth = std::move(naturalWidth)](qreal availableWidth) {
            return naturalWidth() > availableWidth;
        },
        widget));
}

// A user-defined field label must not become the detail pane's hidden
// horizontal minimum.  QLabel's natural width still wins while space is
// available, but the layout may clip it once the card reaches its compact
// phase so the value side remains reachable.
class ShrinkableFieldLabel : public QLabel {
public:
    using QLabel::QLabel;

    QSize sizeHint() const override {
        QSize hint = QLabel::sizeHint();
        // Keep at least ~50 px for the value at the pane's 180 px body
        // floor, even when a custom field name is thousands of characters.
        hint.setWidth(qMin(hint.width(), 84));
        return hint;
    }

    QSize minimumSizeHint() const override {
        QSize hint = QLabel::minimumSizeHint();
        hint.setWidth(0);
        return hint;
    }
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

EntryDetailView::EntryDetailView(QWidget* parent)
    : QScrollArea(parent), entryColor_(nightlock::EntryColor::None) {
    iconreferences::initialize();
    setWindowTitle(tr("Entry Details"));
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(kMinimumPaneWidth);
    new OverlayScrollBar(this);

    content_ = new QWidget;
    content_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contentLayout_ = new QVBoxLayout(content_);
    auto* layout = contentLayout_;
    // Top room for the grip strip: the same gap above and below it.
    layout->setContentsMargins(kPreferredHorizontalInset,
                               kGripGap * 2 + kGripHeight,
                               kPreferredHorizontalInset, 28);
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
    titleLabel_->setTextFormat(Qt::PlainText);
    titleLabel_->setAlignment(Qt::AlignHCenter);
    titleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleLabel_->setMinimumWidth(0);
    addOverflowFade(titleLabel_, overflowfade::Edge::Both,
                    [this] { return naturalLabelWidth(titleLabel_); });
    layout->addWidget(titleLabel_);
    layout->addSpacing(8);

    noteLabel_ = new QLabel;
    noteLabel_->setObjectName(QStringLiteral("detailNote"));
    noteLabel_->setTextFormat(Qt::PlainText);
    noteLabel_->setAlignment(Qt::AlignHCenter);
    noteLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    noteLabel_->setMinimumWidth(0);
    // Let QLabel re-enable height-for-width after the horizontal policy
    // is changed, otherwise wrapped notes keep a stale one-line height.
    noteLabel_->setWordWrap(true);
    QSizePolicy notePolicy = noteLabel_->sizePolicy();
    notePolicy.setHeightForWidth(true);
    noteLabel_->setSizePolicy(notePolicy);
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
    loginCopy_->setObjectName(QStringLiteral("loginValue"));
    loginRow_.layout->addWidget(loginCopy_, 1);
    passwordRow_ = makeRow(fieldsLayout_, tr("Password"));
    // The password hides behind a Telegram-style particle spoiler
    // instead of asterisks.
    passwordRow_.value->hide();
    passwordSpoiler_ = new SpoilerLabel;
    passwordSpoiler_->setObjectName(QStringLiteral("passwordValue"));
    passwordRow_.layout->addWidget(passwordSpoiler_, 1);
    urlRow_ = makeRow(fieldsLayout_, tr("URL"));
    urlRow_.value->setTextFormat(Qt::RichText);
    urlRow_.value->setTextInteractionFlags(Qt::TextBrowserInteraction);
    urlRow_.value->setOpenExternalLinks(true);
    // QLabel lays rich text out from the left even with AlignRight, so a
    // clipped link continues beyond the right edge rather than the left.
    addOverflowFade(urlRow_.value, overflowfade::Edge::Right,
                    [value = urlRow_.value] { return naturalLabelWidth(value); });
    codeRow_ = makeRow(fieldsLayout_, tr("Code"));
    // Unlike the other values, the TOTP value has a ring immediately
    // beside it.  Retain the flexible gap before that pair.
    codeRow_.layout->setStretchFactor(codeRow_.value, 0);
    codeRow_.value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    codeRow_.layout->insertStretch(1, 1);
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
    expirationRow_ = makeRow(metaLayout, tr("Expiration"));
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
        graphButton_->setIcon(appearancesettings::tintedMenuIcon(
            QStringLiteral("graph"), appearancesettings::accentTextColor()));
    };
    tintGraphGlyph();
    connect(appearancesettings::notifier(), &appearancesettings::Notifier::changed, this,
            [this, tintGraphGlyph] {
                tintGraphGlyph();
                refreshUrlText();
                refreshCardColors();
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

    connect(generalsettings::notifier(), &generalsettings::Notifier::changed, this,
            &EntryDetailView::refreshCardColors);
    connect(generalsettings::notifier(), &generalsettings::Notifier::changed, this,
            &EntryDetailView::syncGeneratorVisibility);

    layout->addStretch(1);

    // Child of the content (not the scroll area): in docked mode the
    // content paints the white panel, so a sibling below the viewport
    // would never show through. Lowered under every field widget, the
    // pattern sits behind the icon and scrolls with the entry.
    patternBackdrop_ = new PatternBackdrop(content_);
    patternBackdrop_->lower();

    setWidget(content_);

    // Fixed visual layer above the scrolling viewport and below all
    // interactive header controls.
    headerFade_ = new ScrollHeaderFade(this);
    headerFade_->raise();

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
    generatorButton_ = makeCornerButton(QStringLiteral("dice"), tr("Password generator"));
    connect(generatorButton_, &QToolButton::clicked, this,
            &EntryDetailView::passwordGeneratorRequested);
    editButton_ = makeCornerButton(QStringLiteral("edit"), tr("Edit entry"));
    connect(editButton_, &QToolButton::clicked, this, &EntryDetailView::editRequested);

    floatingControls_ = new FloatingControls(this);
    floatingControls_->move(12, 12);
    floatingControls_->raise();
    floatingControls_->hide();

    floatingBackdrop_ = new FloatingBackdrop(this);
    floatingBackdrop_->lower();
    floatingBackdrop_->hide();

    connect(iconpacks::IconPackManager::instance(),
            &iconpacks::IconPackManager::packChanged, this,
            [this](const QString&) {
                if (!content_->isVisible())
                    return;
                iconPath_ = iconreferences::resolveOrFallback(
                    iconValue_, QStringLiteral(":/icons/entry.png"));
                refreshEntryIcon();
            });

    setEntry(nullptr);
}

QSize EntryDetailView::sizeHint() const {
    QSize hint = QScrollArea::sizeHint();
    hint.setWidth(kPreferredBodyWidth + 2 * kPreferredHorizontalInset);
    return hint;
}

bool EntryDetailView::event(QEvent* event) {
    const bool handled = QScrollArea::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
        refreshEntryIcon();
    return handled;
}

void EntryDetailView::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    updateHorizontalInsets();
    headerFade_->setGeometry(0, 0, width(), kHeaderFadeHeight);
    grip_->move((width() - grip_->width()) / 2, kGripGap);
    // Centered on the grip row, mirroring the floating traffic lights.
    const int buttonY = kGripGap + (kGripHeight - editButton_->height()) / 2;
    editButton_->move(width() - editButton_->width() - 14, buttonY);
    generatorButton_->move(editButton_->x() - generatorButton_->width() - 4, buttonY);
    floatingBackdrop_->setGeometry(rect());
    updatePatternGeometry();
}

void EntryDetailView::updateHorizontalInsets() {
    const int availableWidth = viewport() ? viewport()->width() : width();
    const int horizontalInset =
        qMax(kMinimumHorizontalInset, (availableWidth - kPreferredBodyWidth) / 2);
    const QMargins margins = contentLayout_->contentsMargins();
    if (margins.left() != horizontalInset || margins.right() != horizontalInset) {
        contentLayout_->setContentsMargins(horizontalInset, margins.top(),
                                           horizontalInset, margins.bottom());
    }
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
#ifdef Q_OS_WIN
    // A translucent, frameless panel and painted traffic lights are
    // foreign chrome. Native frames are clearer and fully functional
    // on Windows.
    Q_UNUSED(floating);
    constexpr bool customChrome = false;
#else
    const bool customChrome = floating;
#endif
    setAttribute(Qt::WA_TranslucentBackground, customChrome);
    setProperty("floatingWindow", customChrome);
    floatingControls_->setVisible(customChrome);
    floatingBackdrop_->setGeometry(rect());
    floatingBackdrop_->setVisible(customChrome);
    floatingBackdrop_->lower();
    // Re-evaluate the [floatingWindow] stylesheet selectors.
    style()->unpolish(this);
    style()->polish(this);
    content_->style()->unpolish(content_);
    content_->style()->polish(content_);
}

void EntryDetailView::closeEvent(QCloseEvent* event) {
    if (isWindow()) {
        // Closing a detached detail restores it to the splitter; it
        // must not leave a hidden top-level widget and an empty pane. Hide
        // first and defer re-parenting until native WM_CLOSE processing has
        // unwound; recreating the HWND synchronously here is unsafe on Windows.
        event->ignore();
        hide();
        QTimer::singleShot(0, this, [this] {
            if (isWindow())
                emit dockRequested();
        });
        return;
    }
    QScrollArea::closeEvent(event);
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
    const QString escapedUrl = url_.toHtmlEscaped();
    urlRow_.value->setText(
        QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%1</a> ↗")
            .arg(escapedUrl, appearancesettings::palette().ink.name()));
    urlRow_.value->setToolTip(Qt::convertFromPlainText(url_));
}

void EntryDetailView::setEntry(const nightlock::Entry* entry) {
    content_->setVisible(entry != nullptr);
    editButton_->setVisible(entry != nullptr);
    syncGeneratorVisibility();
    if (!entry) {
        iconValue_ = QString();
        iconPath_.clear();
        iconLabel_->clear();
        entryColor_ = nightlock::EntryColor::None;
        refreshCardColors();
        return;
    }

    entryColor_ = entry->color;
    refreshCardColors();

    iconValue_ = QString::fromStdString(entry->icon);
    iconPath_ = iconreferences::resolveOrFallback(
        iconValue_,
        QStringLiteral(":/icons/entry.png"));
    // Select the variant through QIcon, exactly like the entry list does.
    // Downloadable packs are PNG-only; legacy resource/file paths remain
    // supported by iconreferences.
    refreshEntryIcon();

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
    const nightlock::EntryField* expirationField = nullptr;
    const auto populateRow = [](FieldRow& row, const nightlock::EntryField& field) {
        if (field.secret) {
            row.value->hide();
            auto* spoiler = new SpoilerLabel;
            spoiler->setSecret(toQString(field.value));
            row.layout->addWidget(spoiler, 1);
        } else {
            row.value->setText(toQString(field.value));
        }
    };
    for (const nightlock::EntryField& field : entry->fields) {
        if (field.custom && !field.secret &&
            nightlock::expiration::isLabel(field.label)) {
            if (!expirationField && !field.value.empty())
                expirationField = &field;
            continue;
        }
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
            row.layout->addWidget(spoiler, 1);
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
    expirationRow_.frame->setVisible(expirationField != nullptr);
    if (expirationField) {
        const QDate date = expirationui::date(*expirationField);
        expirationRow_.value->setText(
            date.isValid() ? expirationui::displayText(date)
                           : toQString(expirationField->value));
    } else {
        expirationRow_.value->clear();
    }
    expirationRow_.value->setStyleSheet(
        expirationui::isExpired(*entry)
            ? QStringLiteral("color:#FF2D2D; font-weight:600;")
            : QString{});
    modifiedRow_.value->setText(formatDate(entry->modified));

    refreshLastVisibleRow();

    patternBackdrop_->setEntry(entry);
    updatePatternGeometry();
}

void EntryDetailView::refreshEntryIcon() {
    if (!iconLabel_ || iconPath_.isEmpty())
        return;
    iconLabel_->setPixmap(QIcon(iconPath_).pixmap(QSize(kIconSize, kIconSize),
                                                  iconLabel_->devicePixelRatioF()));
}

void EntryDetailView::syncGeneratorVisibility() {
    generatorButton_->setVisible(content_->isVisible() &&
                                 !generalsettings::hideGeneratorIcon());
}

void EntryDetailView::refreshCardColors() {
    const nightlock::EntryColor effective = generalsettings::entryColorsEnabled()
                                                  ? entryColor_
                                                  : nightlock::EntryColor::None;
    if (effective == nightlock::EntryColor::None) {
        setStyleSheet({});
        return;
    }

    const entrycolors::DetailPalette colors = entrycolors::detailPalette(effective);
    const QString background = colors.background.name(QColor::HexRgb);
    const QString border = colors.border.name(QColor::HexRgb);
    const QString separator = colors.separator.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral(
                      "QFrame#card, QFrame#seedWordsCard {"
                      " background: %1; border: 1px solid %2; border-radius: 9px;"
                      "}"
                      "QFrame#seedCopyBar {"
                      " background: %1; border: 1px solid %2; border-bottom: none;"
                      " border-top-left-radius: 8px; border-top-right-radius: 8px;"
                      " border-bottom-left-radius: 0; border-bottom-right-radius: 0;"
                      "}"
                      "QFrame#fieldSeparator { border: none; background: %3; }")
                      .arg(background, border, separator));
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
    const auto setLabel = [](QLabel* target, const QString& text) {
        target->setText(text);
        target->setToolTip(text);
    };
    setLabel(loginRow_.name, login);
    setLabel(passwordRow_.name, password);
    setLabel(urlRow_.name, url);
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

    row.name = new ShrinkableFieldLabel(label);
    row.name->setObjectName(QStringLiteral("fieldLabel"));
    row.name->setTextFormat(Qt::PlainText);
    row.name->setToolTip(label);
    addOverflowFade(row.name, overflowfade::Edge::Right,
                    [name = row.name] { return naturalLabelWidth(name); });
    row.value = new QLabel;
    row.value->setObjectName(QStringLiteral("fieldValue"));
    row.value->setTextFormat(Qt::PlainText);
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Long values must not dictate the panel's minimum width.  The value
    // owns the flexible half of the row and clips naturally at the final
    // minimum instead of forcing a horizontal scroll area.
    row.value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    addOverflowFade(row.value, overflowfade::Edge::Left,
                    [value = row.value] { return naturalLabelWidth(value); });

    row.layout->addWidget(row.name);
    row.layout->addWidget(row.value, 1);

    rowLayout->addWidget(content);
    row.separator = new QFrame;
    row.separator->setObjectName(QStringLiteral("fieldSeparator"));
    row.separator->setFixedHeight(1);
    row.separator->setVisible(!last);
    rowLayout->addWidget(row.separator);

    cardLayout->addWidget(row.frame);
    return row;
}
