#include "patternpicker.hpp"

#include <QEasingCurve>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <array>

#include "nlmenu.hpp"

namespace {

constexpr int kFieldHeight = 34;   // matches the dialog line edits
constexpr qreal kFieldRadius = 7;
constexpr int kPadLeft = 10;
constexpr int kChevronPad = 14;    // chevron center inset from the right
constexpr int kChevronMs = 130;    // same tempo as the NlMenu reveal

const QColor kFieldBackground(0xFB, 0xF9, 0xFB);
const QColor kFieldBorder(0xEF, 0xEF, 0xEF);
const QColor kOpenBorder(0x00, 0x00, 0x00);
const QColor kTextColor(0x11, 0x11, 0x11);
const QColor kChevronColor(0x8A, 0x8A, 0x8E);

struct PatternOption {
    nightlock::Pattern kind;
    const char* title;
};

constexpr std::array<PatternOption, 10> kOptions = {{
    {nightlock::Pattern::None, QT_TRANSLATE_NOOP("PatternPicker", "None")},
    {nightlock::Pattern::GlowSoft, QT_TRANSLATE_NOOP("PatternPicker", "Glow · soft")},
    {nightlock::Pattern::GlowBold, QT_TRANSLATE_NOOP("PatternPicker", "Glow · bold")},
    {nightlock::Pattern::IconTile, QT_TRANSLATE_NOOP("PatternPicker", "Icon tile · v1")},
    {nightlock::Pattern::IconTileV2, QT_TRANSLATE_NOOP("PatternPicker", "Icon tile · v2")},
    {nightlock::Pattern::IconTileV3, QT_TRANSLATE_NOOP("PatternPicker", "Icon tile · v3")},
    {nightlock::Pattern::Ripple, QT_TRANSLATE_NOOP("PatternPicker", "Ripple")},
    {nightlock::Pattern::Constellation, QT_TRANSLATE_NOOP("PatternPicker", "Constellation")},
    {nightlock::Pattern::Aurora, QT_TRANSLATE_NOOP("PatternPicker", "Aurora")},
    {nightlock::Pattern::Halo, QT_TRANSLATE_NOOP("PatternPicker", "Halo")},
}};

QString titleFor(nightlock::Pattern kind) {
    for (const auto& option : kOptions)
        if (option.kind == kind)
            return PatternPicker::tr(option.title);
    return PatternPicker::tr(kOptions.front().title);
}

}  // namespace

PatternPicker::PatternPicker(QWidget* parent) : QWidget(parent) {
    setFixedHeight(kFieldHeight);
    setCursor(Qt::PointingHandCursor);
    QFont f = font();
    f.setPixelSize(13);
    setFont(f);
}

void PatternPicker::setValue(nightlock::Pattern value) {
    if (value_ == value)
        return;
    value_ = value;
    update();
}

void PatternPicker::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !menuOpen_)
        openMenu();
}

NlMenu* PatternPicker::openMenu() {
    auto* menu = new NlMenu(this);
    for (const auto& option : kOptions) {
        const auto kind = option.kind;
        menu->addAction(tr(option.title), this, [this, kind] { setValue(kind); });
    }
    connect(menu, &QMenu::aboutToHide, this, [this, menu] {
        menuOpen_ = false;
        animateChevron(false);
        menu->deleteLater();
    });
    menuOpen_ = true;
    animateChevron(true);
    menu->popupAt(mapToGlobal(QPoint(0, height() + 6)));
    update();
    return menu;
}

void PatternPicker::animateChevron(bool open) {
    if (chevronAnimation_) {
        chevronAnimation_->stop();
        chevronAnimation_->deleteLater();
    }
    chevronAnimation_ = new QVariantAnimation(this);
    chevronAnimation_->setDuration(kChevronMs);
    chevronAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    chevronAnimation_->setStartValue(chevronTurn_);
    chevronAnimation_->setEndValue(open ? 1.0 : 0.0);
    connect(chevronAnimation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                chevronTurn_ = value.toReal();
                update();
            });
    chevronAnimation_->start();
}

void PatternPicker::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Same idle/focused looks as the dialog's QLineEdit fields.
    painter.setPen(menuOpen_ ? kOpenBorder : kFieldBorder);
    painter.setBrush(menuOpen_ ? Qt::white : kFieldBackground);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            kFieldRadius, kFieldRadius);

    painter.setPen(kTextColor);
    painter.drawText(rect().adjusted(kPadLeft, 0, -2 * kChevronPad, 0),
                     Qt::AlignLeft | Qt::AlignVCenter, titleFor(value_));

    // Downward chevron that flips upside down while the menu is open.
    painter.translate(width() - kChevronPad, height() / 2.0);
    painter.rotate(180.0 * chevronTurn_);
    QPen pen(kChevronColor, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath chevron;
    chevron.moveTo(-3.5, -1.5);
    chevron.lineTo(0.0, 2.0);
    chevron.lineTo(3.5, -1.5);
    painter.drawPath(chevron);
}
