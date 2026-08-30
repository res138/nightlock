#include <QAbstractButton>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <iterator>

#include "appearancesettings.hpp"
#include "respaths.hpp"
#include "standardicons.hpp"
#include "widgets/applicationiconpicker.hpp"

class ApplicationIconTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void defaultAndInvalidPreferencesFallBack();
    void preferencePersists();
    void appearanceSizeDefaultsAndInvalidValuesFallBack();
    void appearanceSizePreferencesPersistAndNotify();
    void catalogAssetsAreUsableAndIdsAreUnique();
    void lockedResourcesDecodeAndPetalArtworkDiffers();
    void pickerRendersExclusiveOptionsAndSelectsOnClick();

private:
    QTemporaryDir settingsDir_;
};

void ApplicationIconTests::initTestCase() {
    QVERIFY2(settingsDir_.isValid(), "Could not create an isolated QSettings directory");

    QCoreApplication::setOrganizationName(QStringLiteral("NightlockTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ApplicationIcons"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir_.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       settingsDir_.path());
}

void ApplicationIconTests::init() {
    QSettings settings;
    settings.clear();
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

void ApplicationIconTests::defaultAndInvalidPreferencesFallBack() {
    const QString defaultId = standardicons::defaultApplicationIcon().id;
    QCOMPARE(defaultId,
             QString::fromLatin1(appearancesettings::kApplicationIcons[0]));
    QCOMPARE(appearancesettings::applicationIcon(), defaultId);

    QSettings settings;
    settings.setValue(QStringLiteral("appearance/application-icon"),
                      QStringLiteral("removed-icon"));
    settings.sync();
    QCOMPARE(appearancesettings::applicationIcon(), defaultId);
    QVERIFY(!standardicons::applicationIconForId(
                 QStringLiteral("removed-icon"))
                 .isNull());

    appearancesettings::setApplicationIcon(QStringLiteral("still-invalid"));
    settings.sync();
    QCOMPARE(appearancesettings::applicationIcon(), defaultId);
    QCOMPARE(settings.value(QStringLiteral("appearance/application-icon")).toString(),
             defaultId);
}

void ApplicationIconTests::preferencePersists() {
    const auto& catalog = standardicons::applicationIcons();
    QVERIFY2(catalog.size() > 1, "The picker needs at least two application icons");
    const QString selectedId = catalog.at(1).id;

    appearancesettings::setApplicationIcon(selectedId);

    QSettings written;
    written.sync();
    QCOMPARE(written.status(), QSettings::NoError);
    QCOMPARE(written.value(QStringLiteral("appearance/application-icon")).toString(),
             selectedId);

    QSettings reopened(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::organizationName(),
                       QCoreApplication::applicationName());
    QCOMPARE(reopened.value(QStringLiteral("appearance/application-icon")).toString(),
             selectedId);
    QCOMPARE(appearancesettings::applicationIcon(), selectedId);
}

void ApplicationIconTests::appearanceSizeDefaultsAndInvalidValuesFallBack() {
    QCOMPARE(appearancesettings::sidebarItemSize(), QStringLiteral("default"));
    QCOMPARE(appearancesettings::entryListItemSize(), QStringLiteral("default"));

    auto sidebar = appearancesettings::sidebarItemMetrics();
    QCOMPARE(sidebar.fontPixelSize, 17);
    QCOMPARE(sidebar.iconExtent, 22);
    QCOMPARE(sidebar.rowHeight, 26);
    QCOMPARE(sidebar.indentation, 33);

    auto entries = appearancesettings::entryListItemMetrics();
    QCOMPARE(entries.rowHeight, 66);
    QCOMPARE(entries.iconExtent, 34);
    QCOMPARE(entries.nameFontPixelSize, 14);
    QCOMPARE(entries.subtitleFontPixelSize, 11);

    QSettings settings;
    settings.setValue(QStringLiteral("appearance/sidebar-item-size"),
                      QStringLiteral("obsolete"));
    settings.setValue(QStringLiteral("appearance/entry-list-item-size"),
                      QStringLiteral("huge"));
    QCOMPARE(appearancesettings::sidebarItemSize(), QStringLiteral("default"));
    QCOMPARE(appearancesettings::entryListItemSize(), QStringLiteral("default"));
    QCOMPARE(appearancesettings::sidebarItemMetrics().rowHeight, 26);
    QCOMPARE(appearancesettings::entryListItemMetrics().rowHeight, 66);
}

void ApplicationIconTests::appearanceSizePreferencesPersistAndNotify() {
    QSignalSpy sidebarSpy(appearancesettings::notifier(),
                          &appearancesettings::Notifier::sidebarItemSizeChanged);
    QSignalSpy entrySpy(appearancesettings::notifier(),
                        &appearancesettings::Notifier::entryListItemSizeChanged);
    QSignalSpy changedSpy(appearancesettings::notifier(),
                          &appearancesettings::Notifier::changed);

    appearancesettings::setSidebarItemSize(QStringLiteral("small"));
    QCOMPARE(appearancesettings::sidebarItemSize(), QStringLiteral("small"));
    auto sidebar = appearancesettings::sidebarItemMetrics();
    QCOMPARE(sidebar.fontPixelSize, 14);
    QCOMPARE(sidebar.iconExtent, 18);
    QCOMPARE(sidebar.rowHeight, 22);
    QCOMPARE(sidebar.indentation, 27);
    QCOMPARE(sidebarSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);

    // Re-selecting the active preset is a no-op, including its signals.
    appearancesettings::setSidebarItemSize(QStringLiteral("small"));
    QCOMPARE(sidebarSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);

    appearancesettings::setSidebarItemSize(QStringLiteral("large"));
    sidebar = appearancesettings::sidebarItemMetrics();
    QCOMPARE(sidebar.fontPixelSize, 20);
    QCOMPARE(sidebar.iconExtent, 26);
    QCOMPARE(sidebar.rowHeight, 31);
    QCOMPARE(sidebar.indentation, 39);
    QCOMPARE(sidebarSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 2);

    appearancesettings::setEntryListItemSize(QStringLiteral("small"));
    QCOMPARE(appearancesettings::entryListItemSize(), QStringLiteral("small"));
    const auto entries = appearancesettings::entryListItemMetrics();
    QCOMPARE(entries.rowHeight, 52);
    QCOMPARE(entries.iconExtent, 28);
    QCOMPARE(entries.nameFontPixelSize, 13);
    QCOMPARE(entries.subtitleFontPixelSize, 10);
    QCOMPARE(entrySpy.count(), 1);
    QCOMPARE(changedSpy.count(), 3);

    appearancesettings::setEntryListItemSize(QStringLiteral("small"));
    QCOMPARE(entrySpy.count(), 1);
    QCOMPARE(changedSpy.count(), 3);

    QSettings written;
    written.sync();
    QCOMPARE(written.value(QStringLiteral("appearance/sidebar-item-size")).toString(),
             QStringLiteral("large"));
    QCOMPARE(written.value(QStringLiteral("appearance/entry-list-item-size")).toString(),
             QStringLiteral("small"));

    // Unsupported setter values are normalized and persisted as "default".
    appearancesettings::setSidebarItemSize(QStringLiteral("gigantic"));
    appearancesettings::setEntryListItemSize(QStringLiteral("microscopic"));
    QCOMPARE(QSettings()
                 .value(QStringLiteral("appearance/sidebar-item-size"))
                 .toString(),
             QStringLiteral("default"));
    QCOMPARE(QSettings()
                 .value(QStringLiteral("appearance/entry-list-item-size"))
                 .toString(),
             QStringLiteral("default"));
    QCOMPARE(sidebarSpy.count(), 3);
    QCOMPARE(entrySpy.count(), 2);
    QCOMPARE(changedSpy.count(), 5);
}

void ApplicationIconTests::catalogAssetsAreUsableAndIdsAreUnique() {
    const auto& catalog = standardicons::applicationIcons();
    QCOMPARE(catalog.size(),
             static_cast<qsizetype>(std::size(
                 appearancesettings::kApplicationIcons)));

    QSet<QString> ids;
    for (qsizetype index = 0; index < catalog.size(); ++index) {
        const standardicons::ApplicationIcon& choice = catalog.at(index);
        QVERIFY2(!choice.id.isEmpty(), "Application icon id must not be empty");
        QVERIFY2(!ids.contains(choice.id),
                 qPrintable(QStringLiteral("Duplicate application icon id: %1")
                                .arg(choice.id)));
        ids.insert(choice.id);
        QCOMPARE(choice.id,
                 QString::fromLatin1(
                     appearancesettings::kApplicationIcons[index]));

        const QStringList resources = {
            choice.resource, choice.lockedResource, choice.windowsResource};
        for (const QString& resource : resources) {
            QVERIFY2(!resource.isEmpty(),
                     qPrintable(QStringLiteral("Missing resource name for %1")
                                    .arg(choice.id)));
            const QString path = respaths::icon(resource);
            QVERIFY2(QFileInfo(path).isFile(), qPrintable(path));
        }

        QVERIFY2(!standardicons::applicationIconForId(choice.id).isNull(),
                 qPrintable(choice.id));
        QVERIFY2(!standardicons::applicationIconForId(choice.id, true).isNull(),
                 qPrintable(choice.id));
    }
}

void ApplicationIconTests::lockedResourcesDecodeAndPetalArtworkDiffers() {
#ifdef Q_OS_WIN
    QSKIP("Windows deliberately uses the regular multi-frame ICO while locked");
#else
    bool foundPetalKeyhole = false;
    for (const standardicons::ApplicationIcon& choice :
         standardicons::applicationIcons()) {
        const QString regularPath = respaths::icon(choice.resource);
        const QString lockedPath = respaths::icon(choice.lockedResource);
        const QImage regular(regularPath);
        const QImage locked(lockedPath);
        QVERIFY2(!regular.isNull(), qPrintable(regularPath));
        QVERIFY2(!locked.isNull(), qPrintable(lockedPath));

        if (choice.id != QLatin1String("petal-keyhole"))
            continue;
        foundPetalKeyhole = true;
        QVERIFY2(choice.lockedResource != choice.resource,
                 "Petal Keyhole must use separate regular and locked files");

        const QImage regularPixels =
            regular.convertToFormat(QImage::Format_RGBA8888);
        const QImage lockedPixels =
            locked.convertToFormat(QImage::Format_RGBA8888);
        QVERIFY2(regularPixels != lockedPixels,
                 "Petal Keyhole locked artwork must visibly differ from regular artwork");
    }
    QVERIFY2(foundPetalKeyhole, "Petal Keyhole is missing from the application-icon catalog");
#endif
}

void ApplicationIconTests::pickerRendersExclusiveOptionsAndSelectsOnClick() {
    const auto& catalog = standardicons::applicationIcons();
    QVERIFY2(catalog.size() > 1, "The picker needs at least two application icons");

    ApplicationIconPicker picker(catalog.first().id);
    picker.show();
    QCoreApplication::processEvents();

    const auto buttons = picker.findChildren<QAbstractButton*>(
        QStringLiteral("applicationIconOption"));
    QCOMPARE(buttons.size(), catalog.size());

    QSet<QString> renderedIds;
    int checkedCount = 0;
    QAbstractButton* target = nullptr;
    for (QAbstractButton* button : buttons) {
        const QString id = button->property("iconId").toString();
        QVERIFY(!id.isEmpty());
        renderedIds.insert(id);
        QVERIFY(button->isCheckable());
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
        checkedCount += button->isChecked() ? 1 : 0;
        if (id == catalog.at(1).id)
            target = button;
    }
    QCOMPARE(renderedIds.size(), catalog.size());
    QCOMPARE(checkedCount, 1);
    QCOMPARE(picker.selectedIconId(), catalog.first().id);
    QVERIFY(target);

    QSignalSpy selected(&picker, &ApplicationIconPicker::iconSelected);
    QTest::mouseClick(target, Qt::LeftButton);

    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.takeFirst().at(0).toString(), catalog.at(1).id);
    QCOMPARE(picker.selectedIconId(), catalog.at(1).id);
    QVERIFY(target->isChecked());

    checkedCount = 0;
    for (QAbstractButton* button : buttons)
        checkedCount += button->isChecked() ? 1 : 0;
    QCOMPARE(checkedCount, 1);
}

QTEST_MAIN(ApplicationIconTests)
#include "test_application_icons.moc"
