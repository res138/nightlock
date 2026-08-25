#include "entrycolorpicker.hpp"

#include <QFontMetrics>
#include <QPainter>

#include "appearancesettings.hpp"
#include "entrycolors.hpp"
#include "nlmenu.hpp"

EntryColorPicker::EntryColorPicker(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(34);
    QFont pickerFont = font();
    pickerFont.setPixelSize(13);
    setFont(pickerFont);
    connect(this, &QAbstractButton::clicked, this, &EntryColorPicker::openMenu);
}

void EntryColorPicker::setValue(nightlock::EntryColor color) {
    if (!entrycolors::values().contains(color))
        color = nightlock::EntryColor::None;
    value_ = color;
    update();
}

QSize EntryColorPicker::sizeHint() const {
    int widest = 0;
    const QFontMetrics metrics(font());
    for (nightlock::EntryColor color : entrycolors::values())
        widest = qMax(widest, metrics.horizontalAdvance(entrycolors::title(color)));
    return {27 + widest + 30, 34};
}

void EntryColorPicker::paintEvent(QPaintEvent*) {
    const auto& palette = appearancesettings::palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette.border, 1));
    painter.drawLine(QPointF(0, height() - 0.5),
                     QPointF(width(), height() - 0.5));

    painter.setPen(Qt::NoPen);
    painter.setBrush(entrycolors::swatch(value_));
    painter.drawEllipse(QRectF(2, height() / 2.0 - 4, 8, 8));
    painter.setPen(palette.ink);
    painter.drawText(rect().adjusted(17, 0, -30, 0),
                     Qt::AlignVCenter | Qt::AlignLeft, entrycolors::title(value_));
    appearancesettings::themedMenuIcon(QStringLiteral("chevron-down"))
        .paint(&painter, QRect(width() - 22, (height() - 12) / 2, 12, 12));
}

void EntryColorPicker::enterEvent(QEnterEvent*) {
    update();
}

void EntryColorPicker::leaveEvent(QEvent*) {
    update();
}

void EntryColorPicker::openMenu() {
    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    for (nightlock::EntryColor color : entrycolors::values()) {
        QAction* action = menu->addAction(
            appearancesettings::colorSwatchIcon(entrycolors::swatch(color)),
            entrycolors::title(color), this,
            [this, color] { setValue(color); });
        action->setCheckable(true);
        action->setChecked(color == value_);
    }
    menu->popupAt(mapToGlobal(QPoint(0, height() + 4)));
}
