#include "iconpicker.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>

#include "iconreferences.hpp"
#include "iconpackmanager.hpp"
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
    iconreferences::initialize();
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
    for (const QString& value : standardicons::recentIconPaths()) {
        auto* button = makeIconButton();
        button->setIcon(QIcon(iconreferences::resolveOrFallback(
            value, fallback.resource)));
        button->setToolTip(iconreferences::displayTitle(value));
        buttons_->addButton(button, static_cast<int>(values_.size()));
        values_ << value;
    }

    plusButton_ = new QToolButton;
    plusButton_->setObjectName(QStringLiteral("iconPickerAdd"));
    plusButton_->setFixedSize(kButtonSize, kButtonSize);
    plusButton_->setText(QStringLiteral("+"));
    plusButton_->setToolTip(tr("Choose from icon packs…"));
    connect(plusButton_, &QToolButton::clicked, this, &IconPicker::addIconRequested);
    connect(iconpacks::IconPackManager::instance(),
            &iconpacks::IconPackManager::packChanged, this,
            [this](const QString&) { refreshIcons(); });

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
    const QString normalized = iconreferences::normalizeStoredValue(value);
    // Unresolved values are intentionally retained: accepting an edit dialog
    // must not erase either a portable reference awaiting pack reinstall or a
    // legacy P1-P7 path that a future published pack may migrate.
    const int id = static_cast<int>(values_.indexOf(normalized));
    if (id >= 0) {
        buttons_->button(id)->setChecked(true);
        return;
    }
    setCustomIcon(normalized);
}

void IconPicker::setCustomIcon(const QString& value) {
    const QString normalized = iconreferences::normalizeStoredValue(value);
    if (normalized.isEmpty())
        return;
    customPath_ = normalized;
    if (!customButton_) {
        customButton_ = makeIconButton();
        buttons_->addButton(customButton_, kCustomId);
        relayout();
    }
    const auto& fallback = standardicons::defaultEntryIcon();
    const QString resolved = iconreferences::resolve(normalized);
    customButton_->setIcon(
        QIcon(resolved.isEmpty() ? fallback.resource : resolved));
    QString tooltip = iconreferences::displayTitle(normalized);
    if (resolved.isEmpty())
        tooltip += tr(" (icon is unavailable)");
    customButton_->setToolTip(tooltip);
    customButton_->setChecked(true);
}

void IconPicker::refreshIcons() {
    const auto& fallback = standardicons::defaultEntryIcon();
    for (int id = 1; id < values_.size(); ++id) {
        if (QAbstractButton* button = buttons_->button(id)) {
            button->setIcon(QIcon(iconreferences::resolveOrFallback(
                values_[id], fallback.resource)));
            button->setToolTip(iconreferences::displayTitle(values_[id]));
        }
    }
    if (!customButton_ || customPath_.isEmpty())
        return;
    const QString resolved = iconreferences::resolve(customPath_);
    customButton_->setIcon(QIcon(resolved.isEmpty() ? fallback.resource : resolved));
    QString tooltip = iconreferences::displayTitle(customPath_);
    if (resolved.isEmpty())
        tooltip += tr(" (icon is unavailable)");
    customButton_->setToolTip(tooltip);
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
