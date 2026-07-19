#include "iconpicker.hpp"

#include <QButtonGroup>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>

namespace {

constexpr int kColumns = 8;
constexpr int kButtonSize = 44;
constexpr int kIconSize = 26;
constexpr int kCustomId = 1000;  // button-group id of the gallery-picked icon

QToolButton* makeIconButton() {
    auto* button = new QToolButton;
    button->setObjectName(QStringLiteral("iconPickerButton"));
    button->setCheckable(true);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setIconSize(QSize(kIconSize, kIconSize));
    return button;
}

}  // namespace

IconPicker::IconPicker(const QVector<standardicons::StandardIcon>& icons, QWidget* parent)
    : QWidget(parent), icons_(icons), buttons_(new QButtonGroup(this)) {
    buttons_->setExclusive(true);

    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(8);

    for (int i = 0; i < icons_.size(); ++i) {
        auto* button = makeIconButton();
        button->setIcon(QIcon(icons_[i].resource));
        button->setToolTip(icons_[i].title);
        buttons_->addButton(button, i);
    }

    plusButton_ = new QToolButton;
    plusButton_->setObjectName(QStringLiteral("iconPickerAdd"));
    plusButton_->setFixedSize(kButtonSize, kButtonSize);
    plusButton_->setText(QStringLiteral("+"));
    plusButton_->setToolTip(tr("Choose from icon packs…"));
    connect(plusButton_, &QToolButton::clicked, this, &IconPicker::addIconRequested);

    grid_->setColumnStretch(kColumns, 1);  // keep the grid left-aligned
    relayout();

    if (auto* first = buttons_->button(0))
        first->setChecked(true);
}

QString IconPicker::selectedIconValue() const {
    const int id = buttons_->checkedId();
    if (id == kCustomId)
        return customPath_;
    if (id > 0 && id < icons_.size())
        return icons_[id].resource;
    return {};  // the default icon (or nothing selected)
}

void IconPicker::setSelectedIconValue(const QString& value) {
    if (value.isEmpty()) {
        if (auto* first = buttons_->button(0))
            first->setChecked(true);
        return;
    }
    for (int i = 0; i < icons_.size(); ++i) {
        if (icons_[i].resource == value) {
            buttons_->button(i)->setChecked(true);
            return;
        }
    }
    setCustomIcon(value);
}

void IconPicker::setCustomIcon(const QString& path) {
    if (path.isEmpty())
        return;
    customPath_ = path;
    if (!customButton_) {
        customButton_ = makeIconButton();
        buttons_->addButton(customButton_, kCustomId);
        relayout();
    }
    customButton_->setIcon(QIcon(path));
    customButton_->setToolTip(path);
    customButton_->setChecked(true);
}

void IconPicker::relayout() {
    int cell = 0;
    auto place = [this, &cell](QWidget* widget) {
        grid_->removeWidget(widget);
        grid_->addWidget(widget, cell / kColumns, cell % kColumns);
        ++cell;
    };
    for (auto* button : buttons_->buttons())
        place(button);
    place(plusButton_);
}
