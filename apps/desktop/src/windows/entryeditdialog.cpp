#include "entryeditdialog.hpp"

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <nightlock/entry.hpp>

#include "standardicons.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/iconpicker.hpp"

EntryEditDialog::EntryEditDialog(Mode mode, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("entryEditDialog"));
    const QString title = mode == Mode::Add ? tr("New Entry") : tr("Edit Entry");
    setWindowTitle(title);
    setFixedWidth(440);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(heading);
    layout->addSpacing(2);

    iconPicker_ = new IconPicker(standardicons::entryIcons());
    connect(iconPicker_, &IconPicker::addIconRequested, this, [this] {
        auto* gallery = new IconGalleryPopup(this);
        connect(gallery, &IconGalleryPopup::iconSelected, iconPicker_,
                &IconPicker::setCustomIcon);
        gallery->popupAt(QCursor::pos());
    });
    layout->addWidget(makeField(tr("Icon"), iconPicker_));

    nameEdit_ = new QLineEdit;
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

    codeEdit_ = new QLineEdit;
    layout->addWidget(makeField(tr("2FA code"), codeEdit_));

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
    passwordEdit_->setText(QString::fromStdString(entry.password));
    urlEdit_->setText(QString::fromStdString(entry.url));
    codeEdit_->setText(QString::fromStdString(entry.code));
    noteEdit_->setPlainText(QString::fromStdString(entry.note));
    iconPicker_->setSelectedIconValue(QString::fromStdString(entry.icon));
}

void EntryEditDialog::applyTo(nightlock::Entry& entry) const {
    entry.name = nameEdit_->text().trimmed().toStdString();
    entry.login = loginEdit_->text().trimmed().toStdString();
    entry.password = passwordEdit_->text().toStdString();
    entry.url = urlEdit_->text().trimmed().toStdString();
    entry.code = codeEdit_->text().trimmed().toStdString();
    entry.note = noteEdit_->toPlainText().trimmed().toStdString();
    entry.icon = iconPicker_->selectedIconValue().toStdString();
}

QWidget* EntryEditDialog::makeField(const QString& label, QWidget* editor, bool required) {
    auto* field = new QWidget;
    auto* fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(5);

    auto* caption = new QLabel(required ? label + QStringLiteral(" *") : label);
    caption->setObjectName(QStringLiteral("dialogFieldLabel"));
    fieldLayout->addWidget(caption);
    fieldLayout->addWidget(editor);
    return field;
}
