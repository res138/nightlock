#include "iconpicker.hpp"

#include <QButtonGroup>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>

namespace {

constexpr int kColumns = 8;
constexpr int kButtonSize = 44;
constexpr int kIconSize = 26;

}  // namespace

IconPicker::IconPicker(const QVector<standardicons::StandardIcon>& icons, QWidget* parent)
    : QWidget(parent), icons_(icons), buttons_(new QButtonGroup(this)) {
    buttons_->setExclusive(true);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    for (int i = 0; i < icons_.size(); ++i) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("iconPickerButton"));
        button->setCheckable(true);
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setIconSize(QSize(kIconSize, kIconSize));
        button->setIcon(QIcon(icons_[i].resource));
        button->setToolTip(icons_[i].title);
        buttons_->addButton(button, i);
        layout->addWidget(button, i / kColumns, i % kColumns);
    }
    layout->setColumnStretch(kColumns, 1);  // keep the grid left-aligned

    if (auto* first = buttons_->button(0))
        first->setChecked(true);

    connect(buttons_, &QButtonGroup::idClicked, this,
            [this](int id) { emit selectionChanged(icons_[id].id); });
}

QString IconPicker::selectedId() const {
    const int id = buttons_->checkedId();
    return id >= 0 ? icons_[id].id : QString();
}

void IconPicker::setSelectedId(const QString& id) {
    for (int i = 0; i < icons_.size(); ++i) {
        if (icons_[i].id == id) {
            if (auto* button = buttons_->button(i))
                button->setChecked(true);
            return;
        }
    }
}
