#include "iconpicker.hpp"

#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>

#include "standardicons.hpp"

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

IconPicker::IconPicker(QWidget* parent)
    : QWidget(parent), buttons_(new QButtonGroup(this)) {
    buttons_->setExclusive(true);

    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(8);

    const auto& fallback = standardicons::defaultEntryIcon();
    values_ << QString();  // the default icon persists as an empty value
    auto* defaultButton = makeIconButton();
    defaultButton->setIcon(QIcon(fallback.resource));
    defaultButton->setToolTip(fallback.title);
    buttons_->addButton(defaultButton, 0);

    // The user's fourteen most recent gallery picks. Together with
    // the default icon, this keeps the main picker at 15 choices.
    for (const QString& path : standardicons::recentIconPaths()) {
        auto* button = makeIconButton();
        button->setIcon(QIcon(path));
        button->setToolTip(QFileInfo(path).completeBaseName());
        buttons_->addButton(button, static_cast<int>(values_.size()));
        values_ << path;
    }

    plusButton_ = new QToolButton;
    plusButton_->setObjectName(QStringLiteral("iconPickerAdd"));
    plusButton_->setFixedSize(kButtonSize, kButtonSize);
    plusButton_->setText(QStringLiteral("+"));
    plusButton_->setToolTip(tr("Choose from icon packs…"));
    connect(plusButton_, &QToolButton::clicked, this, &IconPicker::addIconRequested);

    grid_->setColumnStretch(kColumns, 1);  // keep the grid left-aligned
    relayout();

    buttons_->button(0)->setChecked(true);
}

QString IconPicker::selectedIconValue() const {
    const int id = buttons_->checkedId();
    if (id == kCustomId)
        return customPath_;
    if (id > 0 && id < values_.size())
        return values_[id];
    return {};  // the default icon (or nothing selected)
}

void IconPicker::setSelectedIconValue(const QString& value) {
    if (!value.isEmpty() && !QFile::exists(value)) {
        buttons_->button(0)->setChecked(true);
        return;
    }
    const int id = static_cast<int>(values_.indexOf(value));
    if (id >= 0) {
        buttons_->button(id)->setChecked(true);
        return;
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
    customButton_->setToolTip(QFileInfo(path).completeBaseName());
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
