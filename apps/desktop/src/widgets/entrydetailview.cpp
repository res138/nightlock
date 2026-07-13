#include "entrydetailview.hpp"

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

#include <array>

#include <nightlock/entry.hpp>

#include "totpring.hpp"

namespace {

QString formatDate(std::chrono::system_clock::time_point tp) {
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("dd.MM.yyyy"));
}

}  // namespace

EntryDetailView::EntryDetailView(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);

    content_ = new QWidget;
    auto* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(30, 28, 30, 28);
    layout->setSpacing(0);

    iconLabel_ = new QLabel;
    iconLabel_->setAlignment(Qt::AlignHCenter);
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
    setEntry(nullptr);
}

void EntryDetailView::setEntry(const nightlock::Entry* entry) {
    content_->setVisible(entry != nullptr);
    if (!entry)
        return;

    const QString iconPath = entry->icon.empty() ? QStringLiteral(":/icons/keys.png")
                                                 : QString::fromStdString(entry->icon);
    iconLabel_->setPixmap(QPixmap(iconPath).scaledToHeight(58, Qt::SmoothTransformation));

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
