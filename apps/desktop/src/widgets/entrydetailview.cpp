#include "entrydetailview.hpp"

#include <QDateTime>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <array>

#include <nightlock/entry.hpp>

#include "copylabel.hpp"
#include "spoilerlabel.hpp"
#include "totpring.hpp"

namespace {

constexpr int kIconSize = 58;
constexpr int kDetachThreshold = 12;  // px of grip travel before undocking
constexpr int kGripHeight = 22;
constexpr int kGripGap = 12;          // equal gap above and below the grip
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
        painter.setBrush(QColor(0xA6, 0xA6, 0xA6));
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
        painter.setPen(QColor(0xEA, 0xEA, 0xEA));
        painter.setBrush(Qt::white);
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
    layout->addSpacing(kGripGap);  // same air as between the grip and the icon

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
    layout->addSpacing(20);

    auto* fieldsCard = new QFrame;
    fieldsCard->setObjectName(QStringLiteral("card"));
    auto* fieldsLayout = new QVBoxLayout(fieldsCard);
    fieldsLayout->setContentsMargins(16, 2, 16, 2);
    fieldsLayout->setSpacing(0);
    loginRow_ = makeRow(fieldsLayout, tr("Login"));
    // The login copies on click with the same "Copied" flash as the
    // password (just without the spoiler).
    loginRow_.value->hide();
    loginCopy_ = new CopyLabel;
    loginRow_.layout->addWidget(loginCopy_);
    passwordRow_ = makeRow(fieldsLayout, tr("Password"));
    // The password hides behind a Telegram-style particle spoiler
    // instead of asterisks.
    passwordRow_.value->hide();
    passwordSpoiler_ = new SpoilerLabel;
    passwordRow_.layout->addWidget(passwordSpoiler_);
    urlRow_ = makeRow(fieldsLayout, tr("URL"));
    urlRow_.value->setTextFormat(Qt::RichText);
    urlRow_.value->setTextInteractionFlags(Qt::TextBrowserInteraction);
    urlRow_.value->setOpenExternalLinks(true);
    codeRow_ = makeRow(fieldsLayout, tr("Code"));
    codeRow_.layout->insertWidget(codeRow_.layout->count() - 1, new TotpRing);
    layout->addWidget(fieldsCard);
    layout->addSpacing(26);

    auto* metaHeader = new QLabel(tr("Meta"));
    metaHeader->setObjectName(QStringLiteral("metaHeader"));
    layout->addWidget(metaHeader);
    layout->addSpacing(12);

    auto* metaCard = new QFrame;
    metaCard->setObjectName(QStringLiteral("card"));
    auto* metaLayout = new QVBoxLayout(metaCard);
    metaLayout->setContentsMargins(16, 2, 16, 2);
    metaLayout->setSpacing(0);
    createdRow_ = makeRow(metaLayout, tr("Created"));
    modifiedRow_ = makeRow(metaLayout, tr("Modified"), true);
    layout->addWidget(metaCard);

    layout->addStretch(1);

    setWidget(content_);

    // Overlay child of the scroll area itself (not the content), so it
    // stays put while scrolling and remains visible with no entry.
    grip_ = new DragHandle(this);
    grip_->raise();

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
    floatingBackdrop_->setGeometry(rect());
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

void EntryDetailView::setEntry(const nightlock::Entry* entry) {
    content_->setVisible(entry != nullptr);
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

    noteLabel_->setVisible(!entry->note.empty());
    noteLabel_->setText(QString::fromStdString(entry->note));

    loginCopy_->setText(QString::fromStdString(entry->login));
    passwordSpoiler_->setSecret(QString::fromStdString(entry->password));

    urlRow_.frame->setVisible(!entry->url.empty());
    const QString url = QString::fromStdString(entry->url);
    urlRow_.value->setText(
        QStringLiteral("<a href=\"%1\" style=\"color:#111111;\">%1</a> ↗").arg(url));

    codeRow_.frame->setVisible(!entry->code.empty());
    codeRow_.value->setText(QString::fromStdString(entry->code));

    createdRow_.value->setText(formatDate(entry->created));
    modifiedRow_.value->setText(formatDate(entry->modified));

    refreshLastVisibleRow();
}

// The last visible row of the fields card must not draw its bottom
// border; which row that is depends on the entry's optional fields.
void EntryDetailView::refreshLastVisibleRow() {
    const std::array rows = {loginRow_.frame, passwordRow_.frame, urlRow_.frame, codeRow_.frame};

    QFrame* lastVisible = nullptr;
    for (auto* frame : rows)
        if (!frame->isHidden())
            lastVisible = frame;

    for (auto* frame : rows) {
        const bool isLast = frame == lastVisible;
        if (frame->property("lastVisible").toBool() != isLast) {
            frame->setProperty("lastVisible", isLast);
            frame->style()->unpolish(frame);
            frame->style()->polish(frame);
        }
    }
}

EntryDetailView::FieldRow EntryDetailView::makeRow(QVBoxLayout* cardLayout,
                                                   const QString& label, bool last) {
    FieldRow row;
    row.frame = new QFrame;
    row.frame->setObjectName(last ? QStringLiteral("fieldRowLast")
                                  : QStringLiteral("fieldRow"));
    row.layout = new QHBoxLayout(row.frame);
    row.layout->setContentsMargins(2, 13, 2, 13);
    row.layout->setSpacing(8);

    auto* name = new QLabel(label);
    name->setObjectName(QStringLiteral("fieldLabel"));
    row.value = new QLabel;
    row.value->setObjectName(QStringLiteral("fieldValue"));
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.value->setTextInteractionFlags(Qt::TextSelectableByMouse);

    row.layout->addWidget(name);
    row.layout->addStretch(1);
    row.layout->addWidget(row.value);

    cardLayout->addWidget(row.frame);
    return row;
}
