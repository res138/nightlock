#include "applicationiconpicker.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QGridLayout>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFocusRect>

#include "appearancesettings.hpp"
#include "standardicons.hpp"

namespace {

constexpr int kColumns = 4;
constexpr int kTileWidth = 116;
constexpr int kTileHeight = 128;
constexpr int kArtworkBoxSize = 100;
constexpr int kArtworkSize = 82;

class ApplicationIconButton final : public QAbstractButton {
public:
    ApplicationIconButton(const QString& id, const QString& title,
                          const QIcon& artwork, QWidget* parent = nullptr)
        : QAbstractButton(parent), id_(id), title_(title) {
        setObjectName(QStringLiteral("applicationIconOption"));
        setProperty("iconId", id_);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(kTileWidth, kTileHeight);
        setIcon(artwork);
        setIconSize(QSize(kArtworkSize, kArtworkSize));
        setToolTip(title_);
        setAccessibleName(title_);
        setAccessibleDescription(tr("Use %1 as the Nightlock application icon.").arg(title_));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& palette = appearancesettings::palette();
        const QColor accent = appearancesettings::accentColor();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF tile((width() - kArtworkBoxSize) / 2.0, 1,
                          kArtworkBoxSize, kArtworkBoxSize);
        if (isChecked()) {
            painter.setBrush(palette.window);
            painter.setPen(QPen(accent, 2.0));
        } else if (underMouse()) {
            painter.setBrush(palette.inputHover);
            painter.setPen(Qt::NoPen);
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(Qt::NoPen);
        }
        painter.drawRoundedRect(tile, 18, 18);

        const QRect artworkRect((width() - kArtworkSize) / 2,
                                (kArtworkBoxSize - kArtworkSize) / 2 + 1,
                                kArtworkSize, kArtworkSize);
        icon().paint(&painter, artworkRect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled,
                     isChecked() ? QIcon::On : QIcon::Off);

        QFont labelFont = font();
        labelFont.setPixelSize(12);
        labelFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
        painter.setFont(labelFont);
        painter.setPen(isChecked() ? accent : palette.ink);
        const QString label = painter.fontMetrics().elidedText(
            title_, Qt::ElideRight, width() - 8);
        painter.drawText(QRect(4, kArtworkBoxSize + 7, width() - 8, 19),
                         Qt::AlignHCenter | Qt::AlignVCenter, label);

        if (hasFocus()) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.rect = tile.toAlignedRect().adjusted(4, 4, -4, -4);
            focus.backgroundColor = palette.window;
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
        }
    }

    void enterEvent(QEnterEvent* event) override {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    QString id_;
    QString title_;
};

}  // namespace

ApplicationIconPicker::ApplicationIconPicker(const QString& selectedId,
                                             QWidget* parent)
    : QWidget(parent), buttons_(new QButtonGroup(this)) {
    setObjectName(QStringLiteral("applicationIconPicker"));
    setAccessibleName(tr("Application icon"));
    buttons_->setExclusive(true);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(14);
    layout->setVerticalSpacing(14);

    int index = 0;
    for (const standardicons::ApplicationIcon& choice :
         standardicons::applicationIcons()) {
        auto* button = new ApplicationIconButton(
            choice.id, choice.title,
            standardicons::applicationIconForId(choice.id), this);
        buttons_->addButton(button);
        layout->addWidget(button, index / kColumns, index % kColumns);
        ++index;
        connect(button, &QAbstractButton::clicked, this,
                [this, id = choice.id] { emit iconSelected(id); });
    }
    layout->setColumnStretch(kColumns, 1);
    setSelectedIconId(selectedId);

    connect(appearancesettings::notifier(),
            &appearancesettings::Notifier::changed, this, [this] {
                for (QAbstractButton* button : buttons_->buttons())
                    button->update();
            });
    connect(appearancesettings::notifier(),
            &appearancesettings::Notifier::applicationIconChanged, this,
            [this] { setSelectedIconId(appearancesettings::applicationIcon()); });
}

QString ApplicationIconPicker::selectedIconId() const {
    if (QAbstractButton* button = buttons_->checkedButton())
        return button->property("iconId").toString();
    return standardicons::defaultApplicationIcon().id;
}

void ApplicationIconPicker::setSelectedIconId(const QString& id) {
    QAbstractButton* fallback = nullptr;
    for (QAbstractButton* button : buttons_->buttons()) {
        if (!fallback)
            fallback = button;
        if (button->property("iconId").toString() == id) {
            button->setChecked(true);
            return;
        }
    }
    if (fallback)
        fallback->setChecked(true);
}
