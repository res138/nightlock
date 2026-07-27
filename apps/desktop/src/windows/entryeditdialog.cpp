#include "entryeditdialog.hpp"

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <nightlock/entry.hpp>

#include "qsecure.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/iconpicker.hpp"
#include "widgets/nlmenu.hpp"
#include "widgets/patternpicker.hpp"

EntryEditDialog::EntryEditDialog(Mode mode, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("entryEditDialog"));
    const QString title = mode == Mode::Add ? tr("New Entry") : tr("Edit Entry");
    setWindowTitle(title);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    // The dialog cannot be resized: it always hugs its content.
    layout->setSizeConstraint(QLayout::SetFixedSize);

    iconPicker_ = new IconPicker;
    connect(iconPicker_, &IconPicker::addIconRequested, this, [this] {
        auto* gallery = new IconGalleryPopup(this);
        connect(gallery, &IconGalleryPopup::iconSelected, iconPicker_,
                &IconPicker::setCustomIcon);
        gallery->popupAt(QCursor::pos());
    });
    layout->addWidget(makeField(tr("Icon"), iconPicker_));

    // Detail-view background pattern; a fresh entry starts on a
    // random look instead of None.
    patternPicker_ = new PatternPicker;
    if (mode == Mode::Add)
        patternPicker_->setValue(PatternPicker::randomOption());
    layout->addWidget(makeField(tr("Pattern"), patternPicker_));

    nameEdit_ = new QLineEdit;
    nameEdit_->setMinimumWidth(384);  // keeps the fixed dialog ~440px wide
    layout->addWidget(makeField(tr("Name"), nameEdit_, true));

    loginEdit_ = new QLineEdit;
    passwordEdit_ = new QLineEdit;
    auto* credentials = new QWidget;
    auto* credentialsLayout = new QHBoxLayout(credentials);
    credentialsLayout->setContentsMargins(0, 0, 0, 0);
    credentialsLayout->setSpacing(12);
    credentialsLayout->addWidget(makeField(tr("Login"), loginEdit_), 1);
    credentialsLayout->addWidget(makeField(tr("Password"), passwordEdit_), 1);
    layout->addWidget(credentials);

    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("https://"));
    layout->addWidget(makeField(tr("URL"), urlEdit_));

    noteEdit_ = new QPlainTextEdit;
    noteEdit_->setFixedHeight(74);
    noteEdit_->setTabChangesFocus(true);
    layout->addWidget(makeField(tr("Note"), noteEdit_));

    layout->addSpacing(6);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto* cancelButton = new QPushButton(tr("Cancel"));
    cancelButton->setObjectName(QStringLiteral("ghostButton"));
    saveButton_ = new QPushButton(mode == Mode::Add ? tr("Add") : tr("Save"));
    saveButton_->setObjectName(QStringLiteral("primaryButton"));
    saveButton_->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancelButton);
    buttons->addWidget(saveButton_);
    layout->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveButton_, &QPushButton::clicked, this, &QDialog::accept);

    // Only the name is required.
    saveButton_->setEnabled(false);
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        saveButton_->setEnabled(!text.trimmed().isEmpty());
    });
}

void EntryEditDialog::setEntry(const nightlock::Entry& entry) {
    nameEdit_->setText(QString::fromStdString(entry.name));
    loginEdit_->setText(QString::fromStdString(entry.login));
    passwordEdit_->setText(toQString(entry.password));
    urlEdit_->setText(QString::fromStdString(entry.url));
    noteEdit_->setPlainText(QString::fromStdString(entry.note));
    iconPicker_->setSelectedIconValue(QString::fromStdString(entry.icon));
    patternPicker_->setValue(entry.pattern);
}

void EntryEditDialog::applyTo(nightlock::Entry& entry) const {
    entry.name = nameEdit_->text().trimmed().toStdString();
    entry.login = loginEdit_->text().trimmed().toStdString();
    assignSecret(entry.password, passwordEdit_->text());
    entry.url = urlEdit_->text().trimmed().toStdString();
    entry.note = noteEdit_->toPlainText().trimmed().toStdString();
    entry.icon = iconPicker_->selectedIconValue().toStdString();
    entry.pattern = patternPicker_->value();
}

QMenu* EntryEditDialog::openPatternMenuForScreenshot() {
    return patternPicker_->openMenu();
}

QWidget* EntryEditDialog::makeField(const QString& label, QWidget* editor, bool required) {
    auto* field = new QWidget;
    auto* fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(5);

    auto* caption = new QLabel(
        required ? label + QStringLiteral(" <span style=\"color:#FF3B30;\">*</span>") : label);
    caption->setTextFormat(required ? Qt::RichText : Qt::PlainText);
    caption->setObjectName(QStringLiteral("dialogFieldLabel"));
    fieldLayout->addWidget(caption);
    fieldLayout->addWidget(editor);
    return field;
}
