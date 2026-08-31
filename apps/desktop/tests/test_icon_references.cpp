#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QtTest>

#include "iconreferences.hpp"
#include "iconpackmanager.hpp"
#include "standardicons.hpp"
#include "widgets/frostedpanel.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/iconpicker.hpp"

struct IconGalleryPopupTestAccess {
    static void fitHeight(IconGalleryPopup& popup, const QRect& available) {
        popup.fitHeightToAvailableGeometry(available);
    }

    static QPoint clampedCorner(const QPoint& desiredCorner,
                                const QRect& available,
                                const QSize& popupSize) {
        return IconGalleryPopup::clampedCorner(desiredCorner, available,
                                               popupSize);
    }
};

class IconReferencesTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("NightlockTests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("portable-icon-references"));
        QStandardPaths::setTestModeEnabled(true);
        const QString appData = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        QDir(appData).removeRecursively();
        QSettings().clear();
        QVERIFY(QFile::exists(QStringLiteral(":/icons/entry.png")));

        optionalPackDir_ =
            QDir(appData).filePath(QStringLiteral("icon-packs/optional-test"));
        optionalIconPath_ = QDir(optionalPackDir_).filePath(
            QStringLiteral("applications/optional-stale.png"));
        optionalValue_ = iconreferences::build(
            QStringLiteral("optional-test"), QStringLiteral("applications"),
            QStringLiteral("optional-stale"));

        QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::cyan);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        buffer.close();
        QVERIFY(QDir().mkpath(QFileInfo(optionalIconPath_).absolutePath()));
        QFile iconFile(optionalIconPath_);
        QVERIFY(iconFile.open(QIODevice::WriteOnly));
        QCOMPARE(iconFile.write(png), static_cast<qint64>(png.size()));
        iconFile.close();

        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
        const QJsonObject metadata{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("id"), QStringLiteral("optional-test")},
            {QStringLiteral("title"), QStringLiteral("Optional Test")},
            {QStringLiteral("description"), QStringLiteral("Local test pack")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("author"), QStringLiteral("Nightlock Tests")},
            {QStringLiteral("license"), QStringLiteral("MIT")},
            {QStringLiteral("platforms"),
             QJsonArray{QStringLiteral("cross-platform")}},
            {QStringLiteral("categories"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("applications")},
                 {QStringLiteral("icons"),
                  QJsonArray{QJsonObject{
                      {QStringLiteral("id"), QStringLiteral("optional-stale")},
                      {QStringLiteral("title"), QStringLiteral("Optional Stale")},
                      {QStringLiteral("file"),
                       QStringLiteral("applications/optional-stale.png")},
                      {QStringLiteral("sha256"), digest},
                      {QStringLiteral("size"), png.size()},
                  }}},
             }}},
        };
        const QByteArray manifest =
            QJsonDocument(metadata).toJson(QJsonDocument::Compact);
        QFile manifestFile(
            QDir(optionalPackDir_).filePath(QStringLiteral("manifest.json")));
        QVERIFY(manifestFile.open(QIODevice::WriteOnly));
        QCOMPARE(manifestFile.write(manifest),
                 static_cast<qint64>(manifest.size()));
        manifestFile.close();
    }

    void canonicalBuildAndParse() {
        const QString value = iconreferences::build(
            QStringLiteral("nightlock-default"),
            QStringLiteral("applications"), QStringLiteral("entry"));
        QCOMPARE(value,
                 QStringLiteral(
                     "nightlock-icon://nightlock-default/applications/entry"));

        const auto parsed = iconreferences::parse(value);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->packId, QStringLiteral("nightlock-default"));
        QCOMPARE(parsed->categoryId, QStringLiteral("applications"));
        QCOMPARE(parsed->iconId, QStringLiteral("entry"));
    }

    void rejectsNonCanonicalValues() {
        QVERIFY(iconreferences::build(QStringLiteral("Uppercase"),
                                      QStringLiteral("applications"),
                                      QStringLiteral("entry"))
                    .isEmpty());
        QVERIFY(iconreferences::build(QStringLiteral("pack"),
                                      QStringLiteral("made-up-category"),
                                      QStringLiteral("entry"))
                    .isEmpty());

        const QStringList malformed = {
            QStringLiteral("NIGHTLOCK-icon://pack/applications/entry"),
            QStringLiteral("nightlock-icon://pack/applications/entry/extra"),
            QStringLiteral("nightlock-icon://pack/applications/%65ntry"),
            QStringLiteral("nightlock-icon://pack/applications/entry?x=1"),
            QStringLiteral("nightlock-icon://pack//entry"),
            QStringLiteral("nightlock-icon://pack/applications/é"),
        };
        for (const QString& value : malformed)
            QVERIFY2(!iconreferences::parse(value), qPrintable(value));
    }

    void resolvesBuiltInAndDisplaysTitle() {
        const QString value = iconreferences::build(
            QStringLiteral("nightlock-default"),
            QStringLiteral("applications"), QStringLiteral("entry"));
        QCOMPARE(iconreferences::resolve(value),
                 QStringLiteral(":/icons/entry.png"));
        QCOMPARE(iconreferences::displayTitle(value), QStringLiteral("Entry"));
    }

    void unresolvedReferenceUsesFallbackWithoutLosingValue() {
        const QString value = iconreferences::build(
            QStringLiteral("not-installed"),
            QStringLiteral("applications"), QStringLiteral("missing-icon"));
        QVERIFY(iconreferences::isPortable(value));
        QVERIFY(iconreferences::resolve(value).isEmpty());
        QCOMPARE(iconreferences::resolveOrFallback(
                     value, QStringLiteral(":/icons/entry.png")),
                 QStringLiteral(":/icons/entry.png"));
        QCOMPARE(iconreferences::normalizeStoredValue(value), value);
        QCOMPARE(iconreferences::displayTitle(value),
                 QStringLiteral("Missing icon"));

        IconPicker picker;
        picker.setSelectedIconValue(value);
        QCOMPARE(picker.selectedIconValue(), value);

        standardicons::addRecentIconPath(value);
        QVERIFY(standardicons::recentIconPaths().contains(value));
    }

    void migratesKnownLegacyPaths() {
        const QString expected = iconreferences::build(
            QStringLiteral("nightlock-default"),
            QStringLiteral("applications"), QStringLiteral("entry"));

        QCOMPARE(iconreferences::fromLegacyPath(
                     QStringLiteral(":/icons/entry.png")),
                 std::optional<QString>(expected));
        QCOMPARE(iconreferences::normalizeStoredValue(
                     QStringLiteral(":/icons/entry.png")),
                 expected);

        // Old vaults can contain an absolute P1-P7 path after those bundled
        // directories have disappeared. It must remain untouched until a
        // unique optional-pack match exists; the generic built-in "entry"
        // icon is not valid migration evidence.
        const QString missingOldPath =
            QStringLiteral("/definitely-missing/nightlock/icons/P6/ENTRY.ico");
        QVERIFY(!iconreferences::fromLegacyPath(missingOldPath));
        QCOMPARE(iconreferences::normalizeStoredValue(missingOldPath),
                 missingOldPath);
        QVERIFY(iconreferences::resolve(missingOldPath).isEmpty());
        QVERIFY(iconreferences::isLegacyPackPath(missingOldPath));

        IconPicker picker;
        picker.setSelectedIconValue(missingOldPath);
        QCOMPARE(picker.selectedIconValue(), missingOldPath);
        standardicons::addRecentIconPath(missingOldPath);
        QVERIFY(standardicons::recentIconPaths().contains(missingOldPath));
    }

    void galleryUsesPackAndCategorySelectorsWithTwelveColumns() {
        IconGalleryPopup gallery;
        auto* packTabs = gallery.findChild<QTabBar*>(
            QStringLiteral("iconGalleryPackTabs"));
        auto* categoryTabs = gallery.findChild<QTabBar*>(
            QStringLiteral("iconGalleryCategoryTabs"));
        auto* view = gallery.findChild<QListView*>(QStringLiteral("iconGallery"));
        QVERIFY(packTabs);
        QVERIFY(categoryTabs);
        QVERIFY(view);

        QVERIFY(packTabs->count() >= 1);
        QCOMPARE(packTabs->tabData(0).toString(),
                 QStringLiteral("nightlock-default"));
        QCOMPARE(categoryTabs->count(), 3);
        QCOMPARE(view->gridSize(), QSize(46, 46));
        QVERIFY2(view->width() >= 12 * view->gridSize().width(),
                 "The gallery must expose twelve icon columns");
        QCOMPARE(view->model()->rowCount(), 1);

        const QString entryValue =
            view->model()->index(0, 0).data(Qt::UserRole).toString();
        const auto entry = iconreferences::parse(entryValue);
        QVERIFY(entry);
        QCOMPARE(entry->packId, QStringLiteral("nightlock-default"));
        QCOMPARE(entry->categoryId, QStringLiteral("applications"));
        QCOMPARE(entry->iconId, QStringLiteral("entry"));

        categoryTabs->setCurrentIndex(1);
        QCOMPARE(view->model()->rowCount(), 1);
        const auto folder = iconreferences::parse(
            view->model()->index(0, 0).data(Qt::UserRole).toString());
        QVERIFY(folder);
        QCOMPARE(folder->categoryId, QStringLiteral("folders-places"));
        QCOMPARE(folder->iconId, QStringLiteral("folder"));
    }

    void galleryFitsRowsAndClampsSafelyOnSmallScreen() {
        IconGalleryPopup gallery;
        auto* view = gallery.findChild<QListView*>(QStringLiteral("iconGallery"));
        QVERIFY(view);

        const int fullWidth = gallery.width();
        const int gridHeight = view->gridSize().height();
        const int chromeHeight =
            gallery.height() - 2 * frosted::kShadow - view->height();
        const int visibleWidth = fullWidth - 2 * frosted::kShadow;
        const QRect smallAvailable(100, 200, visibleWidth + 40,
                                   chromeHeight + 3 * gridHeight + 7);

        IconGalleryPopupTestAccess::fitHeight(gallery, smallAvailable);
        QCOMPARE(gallery.width(), fullWidth);
        QVERIFY2(view->width() >= 12 * view->gridSize().width(),
                 "Reducing the height must preserve all twelve columns");
        QCOMPARE(view->height(), 3 * gridHeight);
        QVERIFY(gallery.height() - 2 * frosted::kShadow <=
                smallAvailable.height());

        const QPoint corner = IconGalleryPopupTestAccess::clampedCorner(
            smallAvailable.bottomRight() + QPoint(500, 500), smallAvailable,
            gallery.size());
        const QRect visiblePanel(
            corner + QPoint(frosted::kShadow, frosted::kShadow),
            QSize(gallery.width() - 2 * frosted::kShadow,
                  gallery.height() - 2 * frosted::kShadow));
        QVERIFY2(smallAvailable.contains(visiblePanel),
                 "The visible popup panel must stay inside availableGeometry");

        // Both axes have reversed clamp bounds here. The result stays stable
        // and leading-edge aligned instead of invoking qBound with an invalid
        // interval.
        const QRect undersizedAvailable(10, 20, visibleWidth / 2,
                                        chromeHeight / 2);
        IconGalleryPopupTestAccess::fitHeight(gallery, undersizedAvailable);
        QCOMPARE(view->height(), gridHeight);
        QCOMPARE(IconGalleryPopupTestAccess::clampedCorner(
                     QPoint(10000, 10000), undersizedAvailable, gallery.size()),
                 undersizedAvailable.topLeft() -
                     QPoint(frosted::kShadow, frosted::kShadow));
    }

    void failedRemovalOfDamagedPackFallsBackToBuiltIn() {
        QCOMPARE(iconreferences::resolve(optionalValue_), optionalIconPath_);
        const auto before = iconpacks::IconPackManager::instance()->pack(
            QStringLiteral("optional-test"));
        QVERIFY(before);
        QCOMPARE(before->state, iconpacks::State::Installed);

        QVERIFY(QDir(optionalPackDir_).removeRecursively());
        QFile blocker(optionalPackDir_);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("blocked"), qint64{7});
        blocker.close();

        QVERIFY(!iconpacks::IconPackManager::instance()->remove(
            QStringLiteral("optional-test")));
        const auto after = iconpacks::IconPackManager::instance()->pack(
            QStringLiteral("optional-test"));
        QVERIFY(after);
        QCOMPARE(after->state, iconpacks::State::Installed);
        QVERIFY(iconreferences::resolve(optionalValue_).isEmpty());
        QCOMPARE(iconreferences::resolveOrFallback(
                     optionalValue_, QStringLiteral(":/icons/entry.png")),
                 QStringLiteral(":/icons/entry.png"));
    }

private:
    QString optionalPackDir_;
    QString optionalIconPath_;
    QString optionalValue_;
};

QTEST_MAIN(IconReferencesTest)
#include "test_icon_references.moc"
