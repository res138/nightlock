#include "entrydetailview.hpp"

#include <QDateTime>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

#include <array>

#include <nightlock/entry.hpp>

#include "totpring.hpp"

namespace {

constexpr int kIconSize = 58;
constexpr int kDetachThreshold = 12;  // px of grip travel before undocking

QString formatDate(std::chrono::system_clock::time_point tp) {
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("dd.MM.yyyy"));
}

// The six-dot grip (two rows of three) that tears the panel off.
class DragHandle : public QWidget {
public:
    explicit DragHandle(EntryDetailView* view) : QWidget(view), view_(view) {
        setFixedSize(30, 18);
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(underMouse() || pressed_ ? QColor(0x6E, 0x6E, 0x6E)
                                                  : QColor(0xA6, 0xA6, 0xA6));
        constexpr qreal kDotRadius = 2.2;
        constexpr int kStep = 9;
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

}  // namespace

EntryDetailView::EntryDetailView(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);

    content_ = new QWidget;
    auto* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(30, 28, 30, 28);
    layout->setSpacing(0);

    iconLabel_ = new QLabel;
    iconLabel_->setAlignment(Qt::AlignCenter);
    // Multi-size icons may resolve below 58px (e.g. 48px .ico packs);
    // a fixed height keeps the header from shifting.
    iconLabel_->setFixedHeight(kIconSize);
    layout->addWidget(iconLabel_);
    layout->addSpacing(10);

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
    passwordRow_ = makeRow(fieldsLayout, tr("Password"));
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

    setEntry(nullptr);
}

void EntryDetailView::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    grip_->move((width() - grip_->width()) / 2, 6);
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
    noteLabel_->setText(QStringLiteral("📙 ") + QString::fromStdString(entry->note));

    loginRow_.value->setText(QString::fromStdString(entry->login));
    passwordRow_.value->setText(QString(19, QChar('*')));

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
