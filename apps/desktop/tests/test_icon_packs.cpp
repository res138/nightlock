#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

#include "iconpackmanager.hpp"

namespace {

QByteArray png(const QColor& color, const QSize& size = QSize(24, 24)) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    QByteArray payload;
    QBuffer buffer(&payload);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return payload;
}

bool writeFile(const QString& path, const QByteArray& payload) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(payload) == payload.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool createDirectoryLinkLike(const QString& target, const QString& linkPath) {
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("cmd.exe"),
                  {QStringLiteral("/d"), QStringLiteral("/c"),
                   QStringLiteral("mklink"), QStringLiteral("/J"),
                   QDir::toNativeSeparators(linkPath),
                   QDir::toNativeSeparators(target)});
    return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0 && QFileInfo(linkPath).isJunction();
#else
    return QFile::link(target, linkPath) && QFileInfo(linkPath).isSymLink();
#endif
}

bool removeDirectoryLinkLike(const QString& linkPath) {
    const QFileInfo info(linkPath);
    if (!info.isSymLink() && !info.isJunction())
        return false;
#ifdef Q_OS_WIN
    return QDir().rmdir(linkPath);
#else
    return QFile::remove(linkPath);
#endif
}

QJsonObject iconObject(const QString& id, const QString& file,
                       const QByteArray& payload) {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), id.toUpper()},
        {QStringLiteral("file"), file},
        {QStringLiteral("sha256"),
         QString::fromLatin1(QCryptographicHash::hash(
                                 payload, QCryptographicHash::Sha256)
                                 .toHex())},
        {QStringLiteral("size"), payload.size()},
    };
}

QJsonObject packMetadata(const QString& id = QStringLiteral("test-pack")) {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), QStringLiteral("Test Pack")},
        {QStringLiteral("description"), QStringLiteral("A test icon pack")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("author"), QStringLiteral("Nightlock Tests")},
        {QStringLiteral("license"), QStringLiteral("MIT")},
        {QStringLiteral("platforms"),
         QJsonArray{QStringLiteral("linux"), QStringLiteral("macos")}},
    };
}

QByteArray catalogPayload(const QString& manifest =
                              QStringLiteral("packs/test-pack/manifest.json"),
                          const QString& id = QStringLiteral("test-pack")) {
    QJsonObject entry = packMetadata(id);
    entry.insert(QStringLiteral("manifest"), manifest);
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("packs"), QJsonArray{entry}},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray manifestPayload(const QVector<QJsonObject>& icons,
                           const QString& category = QStringLiteral("applications")) {
    QJsonArray iconArray;
    for (const QJsonObject& icon : icons)
        iconArray.append(icon);
    QJsonObject manifest = packMetadata();
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(
        QStringLiteral("categories"),
        QJsonArray{QJsonObject{
            {QStringLiteral("id"), category},
            {QStringLiteral("title"), QStringLiteral("Ignored local title")},
            {QStringLiteral("icons"), iconArray},
        }});
    return QJsonDocument(manifest).toJson(QJsonDocument::Compact);
}

QByteArray httpResponse(const QByteArray& body, int status = 200,
                        const QByteArray& contentType = "application/json") {
    QByteArray response = status == 200
                              ? QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                              : QByteArrayLiteral("HTTP/1.1 404 Not Found\r\n");
    response += QByteArrayLiteral("Content-Type: ") + contentType +
                QByteArrayLiteral("\r\nConnection: close\r\nContent-Length: ") +
                QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
    return response;
}

QByteArray redirectResponse(const QByteArray& location) {
    return QByteArrayLiteral("HTTP/1.1 302 Found\r\nLocation: ") + location +
           QByteArrayLiteral("\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

class HttpFixture {
public:
    bool listen() {
        if (!server.listen(QHostAddress::LocalHost))
            return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                QTcpSocket* socket = server.nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, &server,
                                 [this, socket, request, handled] {
                                     if (*handled)
                                         return;
                                     request->append(socket->readAll());
                                     if (!request->contains("\r\n\r\n"))
                                         return;
                                     *handled = true;
                                     const QList<QByteArray> words =
                                         request->left(request->indexOf("\r\n"))
                                             .split(' ');
                                     const QString path =
                                         words.size() >= 2
                                             ? QString::fromLatin1(words.at(1))
                                             : QString();
                                     requests.append(path);
                                     const auto redirect = redirects.constFind(path);
                                     if (redirect != redirects.cend()) {
                                         sendResponse(socket, path,
                                                      redirectResponse(redirect.value()));
                                         return;
                                     }
                                     const auto it = responses.constFind(path);
                                     if (it != responses.cend()) {
                                         sendResponse(
                                             socket, path,
                                             httpResponse(
                                                 it.value(), 200,
                                                 path.endsWith(QLatin1String(".png"))
                                                     ? QByteArrayLiteral("image/png")
                                                     : QByteArrayLiteral("application/json")));
                                     } else {
                                         const auto file = fileResponses.constFind(path);
                                         if (file == fileResponses.cend()) {
                                             sendResponse(socket, path,
                                                          httpResponse({}, 404));
                                         } else {
                                             const QByteArray payload = readFile(file.value());
                                             sendResponse(
                                                 socket, path,
                                                 httpResponse(
                                                     payload, 200,
                                                     path.endsWith(QLatin1String(".png"))
                                                         ? QByteArrayLiteral("image/png")
                                                         : QByteArrayLiteral("application/json")));
                                         }
                                     }
                                 });
            }
        });
        return true;
    }

    QUrl catalogUrl() const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/icon-packs/catalog.json")
                        .arg(server.serverPort()));
    }

    void sendResponse(QTcpSocket* socket, const QString& path,
                      const QByteArray& response) {
        const bool isPng = path.endsWith(QLatin1String(".png"));
        if (!isPng) {
            socket->write(response);
            socket->disconnectFromHost();
            return;
        }

        ++activePngRequests;
        maximumConcurrentPngRequests =
            qMax(maximumConcurrentPngRequests, activePngRequests);
        const int delay = pngResponseDelaysMs.value(path,
                                                     pngResponseDelayMs);
        if (delay <= 0) {
            --activePngRequests;
            ++completedPngResponses;
            socket->write(response);
            socket->disconnectFromHost();
            return;
        }

        auto pending = std::make_shared<bool>(true);
        QObject::connect(socket, &QTcpSocket::disconnected, &server,
                         [this, pending] {
                             if (!*pending)
                                 return;
                             *pending = false;
                             --activePngRequests;
                             ++cancelledPngRequests;
                         });
        QTimer::singleShot(delay, socket,
                           [this, socket, response, pending] {
                               if (!*pending)
                                   return;
                               *pending = false;
                               --activePngRequests;
                               if (socket->state() !=
                                   QAbstractSocket::ConnectedState) {
                                   ++cancelledPngRequests;
                                   return;
                               }
                               ++completedPngResponses;
                               socket->write(response);
                               socket->disconnectFromHost();
                           });
    }

    QTcpServer server;
    QHash<QString, QByteArray> responses;
    QHash<QString, QString> fileResponses;
    QHash<QString, QByteArray> redirects;
    QStringList requests;
    int pngResponseDelayMs = 0;
    QHash<QString, int> pngResponseDelaysMs;
    int activePngRequests = 0;
    int maximumConcurrentPngRequests = 0;
    int completedPngResponses = 0;
    int cancelledPngRequests = 0;
};

std::optional<iconpacks::Pack> waitForState(iconpacks::IconPackManager* manager,
                                            const QString& id,
                                            iconpacks::State state,
                                            int timeout = 3000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout) {
        const auto result = manager->pack(id);
        if (result && result->state == state)
            return result;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QTest::qWait(5);
    }
    return manager->pack(id);
}

}  // namespace

class IconPackTests final : public QObject {
    Q_OBJECT

private slots:
    void builtInPackAndTaxonomyAreStable();
    void publishedCatalogPacksInstallFromSourceTree();
    void refreshCatalogAndInstallWithBoundedParallelism();
    void sameOriginManifestRedirectUsesFinalBaseUrl();
    void installedPackLoadsOfflineAndCanBeRemoved();
    void removeFailureKeepsInstalledState();
    void linkLikeDescendantBlocksRecursiveRemoval();
    void catalogRejectsTraversalAndUnknownCategories();
    void manifestRejectsNonPortableDestinationPaths_data();
    void manifestRejectsNonPortableDestinationPaths();
    void startupDiscardsCorruptCacheAndAllowsReinstall_data();
    void startupDiscardsCorruptCacheAndAllowsReinstall();
    void installRejectsHashMismatchAndLeavesNoPartialPack();
};

void IconPackTests::builtInPackAndTaxonomyAreStable() {
    QTemporaryDir data;
    QVERIFY(data.isValid());
    const QString staleStaging = QDir(data.path()).filePath(QStringLiteral(
        "icon-packs/.test-pack.install-00000000-0000-0000-0000-000000000000"));
    QVERIFY(QDir().mkpath(staleStaging));
    QVERIFY(writeFile(QDir(staleStaging).filePath(QStringLiteral("partial.png")),
                      QByteArrayLiteral("partial")));
    HttpFixture fixture;
    QVERIFY(fixture.listen());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1000, &owner);
    QVERIFY(manager);
    QVERIFY(!QFileInfo::exists(staleStaging));

    const QVector<iconpacks::Pack> packs = manager->packs();
    QCOMPARE(packs.size(), 1);
    QCOMPARE(packs.first().id, QStringLiteral("nightlock-default"));
    QCOMPARE(packs.first().state, iconpacks::State::BuiltIn);
    QCOMPARE(packs.first().platforms,
             QStringList{QStringLiteral("cross-platform")});
    QCOMPARE(packs.first().categories.size(), 3);
    QCOMPARE(packs.first().categories.at(0).id, QStringLiteral("applications"));
    QCOMPARE(packs.first().categories.at(0).icons.first().filePath,
             QStringLiteral(":/icons/entry.png"));
    QCOMPARE(packs.first().categories.at(1).id, QStringLiteral("folders-places"));
    QCOMPARE(packs.first().categories.at(1).icons.first().filePath,
             QStringLiteral(":/icons/folder.png"));
    QCOMPARE(packs.first().categories.at(2).id, QStringLiteral("alerts-badges"));
    QCOMPARE(packs.first().categories.at(2).icons.first().filePath,
             QStringLiteral(":/icons/lock.png"));
    QCOMPARE(manager->installedPacks().size(), 1);

    QCOMPARE(iconpacks::normalizedCategoryIds().size(), 16);
    QVERIFY(iconpacks::normalizedCategoryIds().contains(
        QStringLiteral("documents-mimetypes")));
    QCOMPARE(iconpacks::canonicalCategoryTitle(QStringLiteral("folders-places")),
             QStringLiteral("Folders & Places"));
    QVERIFY(iconpacks::canonicalCategoryTitle(QStringLiteral("unknown")).isEmpty());
}

void IconPackTests::publishedCatalogPacksInstallFromSourceTree() {
    const QString sourceRoot = QString::fromUtf8(NIGHTLOCK_SOURCE_DIR);
    const QString iconPacksRoot =
        QDir(sourceRoot).filePath(QStringLiteral("icon-packs"));
    const QString catalogPath =
        QDir(iconPacksRoot).filePath(QStringLiteral("catalog.json"));

    QStringList icoFiles;
    QDirIterator sourceFiles(
        iconPacksRoot, QDir::Files | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (sourceFiles.hasNext()) {
        const QString path = sourceFiles.next();
        if (QFileInfo(path).suffix().compare(QLatin1String("ico"),
                                             Qt::CaseInsensitive) == 0)
            icoFiles.append(QDir(iconPacksRoot).relativeFilePath(path));
    }
    QVERIFY2(icoFiles.isEmpty(),
             qPrintable(QStringLiteral("Published icon-packs contain .ico files: %1")
                            .arg(icoFiles.join(QStringLiteral(", ")))));

    const QByteArray catalogPayload = readFile(catalogPath);
    QVERIFY2(!catalogPayload.isEmpty(), qPrintable(catalogPath));
    QJsonParseError catalogParseError;
    const QJsonDocument catalogDocument =
        QJsonDocument::fromJson(catalogPayload, &catalogParseError);
    QVERIFY2(catalogParseError.error == QJsonParseError::NoError &&
                 catalogDocument.isObject(),
             qPrintable(catalogParseError.errorString()));
    const QJsonArray catalogPacks =
        catalogDocument.object().value(QStringLiteral("packs")).toArray();
    QVERIFY2(!catalogPacks.isEmpty(),
             "The source-tree icon-pack catalog must publish at least one pack");

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload);

    struct PublishedPack {
        QString id;
        int iconCount = 0;
        qint64 payloadBytes = 0;
    };
    QVector<PublishedPack> published;
    const QUrl contentRoot =
        fixture.catalogUrl().adjusted(QUrl::RemoveFilename);
    for (const QJsonValue& catalogValue : catalogPacks) {
        QVERIFY(catalogValue.isObject());
        const QJsonObject catalogEntry = catalogValue.toObject();
        PublishedPack pack;
        pack.id = catalogEntry.value(QStringLiteral("id")).toString();
        const int declaredIconCount =
            catalogEntry.value(QStringLiteral("iconCount")).toInt();
        const qint64 declaredPayloadBytes = static_cast<qint64>(
            catalogEntry.value(QStringLiteral("payloadBytes")).toDouble());
        const QString manifestReference =
            catalogEntry.value(QStringLiteral("manifest")).toString();
        QVERIFY2(!pack.id.isEmpty() && !manifestReference.isEmpty() &&
                     declaredIconCount > 0 && declaredPayloadBytes > 0,
                 "A published catalog entry is missing identity or size metadata");

        const QString manifestPath =
            QDir(iconPacksRoot).filePath(manifestReference);
        const QFileInfo manifestInfo(manifestPath);
        QVERIFY2(manifestInfo.isFile() && !manifestInfo.isSymLink() &&
                     !manifestInfo.isJunction(),
                 qPrintable(manifestPath));
        const QByteArray manifestPayload = readFile(manifestPath);
        QVERIFY2(!manifestPayload.isEmpty(), qPrintable(manifestPath));
        QJsonParseError manifestParseError;
        const QJsonDocument manifestDocument =
            QJsonDocument::fromJson(manifestPayload, &manifestParseError);
        QVERIFY2(manifestParseError.error == QJsonParseError::NoError &&
                     manifestDocument.isObject(),
                 qPrintable(QStringLiteral("%1: %2")
                                .arg(manifestPath,
                                     manifestParseError.errorString())));
        const QJsonObject manifestObject = manifestDocument.object();
        QCOMPARE(manifestObject.value(QStringLiteral("id")).toString(), pack.id);

        const QUrl manifestUrl = contentRoot.resolved(QUrl(manifestReference));
        fixture.fileResponses.insert(manifestUrl.path(), manifestPath);
        const QString manifestDirectory = manifestInfo.absolutePath();
        const QJsonArray categories =
            manifestObject.value(QStringLiteral("categories")).toArray();
        QVERIFY2(!categories.isEmpty(), qPrintable(manifestPath));
        for (const QJsonValue& categoryValue : categories) {
            QVERIFY(categoryValue.isObject());
            const QJsonArray icons = categoryValue.toObject()
                                         .value(QStringLiteral("icons"))
                                         .toArray();
            QVERIFY2(!icons.isEmpty(), qPrintable(manifestPath));
            for (const QJsonValue& iconValue : icons) {
                QVERIFY(iconValue.isObject());
                const QJsonObject icon = iconValue.toObject();
                QString sourceReference =
                    icon.value(QStringLiteral("file")).toString();
                if (icon.contains(QStringLiteral("source")))
                    sourceReference =
                        icon.value(QStringLiteral("source")).toString();
                else if (icon.contains(QStringLiteral("url")))
                    sourceReference = icon.value(QStringLiteral("url")).toString();
                QVERIFY2(!sourceReference.isEmpty(), qPrintable(manifestPath));

                const QUrl relativeSource(sourceReference);
                QVERIFY2(relativeSource.isRelative(),
                         qPrintable(QStringLiteral(
                             "%1 uses a non-local icon source: %2")
                                        .arg(manifestPath, sourceReference)));
                const QUrl requestUrl = manifestUrl.resolved(relativeSource);
                const QString localSource =
                    QDir(manifestDirectory).filePath(sourceReference);
                const QFileInfo sourceInfo(localSource);
                QVERIFY2(sourceInfo.isFile() && !sourceInfo.isSymLink() &&
                             !sourceInfo.isJunction(),
                         qPrintable(localSource));
                QCOMPARE(sourceInfo.suffix().toLower(), QStringLiteral("png"));
                const qint64 declaredSize = static_cast<qint64>(
                    icon.value(QStringLiteral("size")).toDouble());
                QCOMPARE(declaredSize, sourceInfo.size());
                const auto existing =
                    fixture.fileResponses.constFind(requestUrl.path());
                if (existing != fixture.fileResponses.cend())
                    QCOMPARE(existing.value(), localSource);
                else
                    fixture.fileResponses.insert(requestUrl.path(), localSource);
                ++pack.iconCount;
                pack.payloadBytes += declaredSize;
            }
        }
        QVERIFY(pack.iconCount > 0);
        QCOMPARE(pack.iconCount, declaredIconCount);
        QCOMPARE(pack.payloadBytes, declaredPayloadBytes);
        published.append(std::move(pack));
    }

    QTemporaryDir data;
    QVERIFY(data.isValid());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 5000, &owner);
    QVERIFY(manager);
    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 10000);
    QVERIFY2(manager->catalogError().isEmpty(),
             qPrintable(manager->catalogError()));

    for (const PublishedPack& expected : std::as_const(published)) {
        const auto available = manager->pack(expected.id);
        QVERIFY2(available && available->state == iconpacks::State::Available,
                 qPrintable(expected.id));
        QCOMPARE(available->iconCount, expected.iconCount);
        QCOMPARE(available->totalBytes, expected.payloadBytes);
        manager->install(expected.id);
        const auto installed = waitForState(
            manager, expected.id, iconpacks::State::Installed, 60000);
        QVERIFY2(installed && installed->state == iconpacks::State::Installed,
                 qPrintable(installed ? installed->error : expected.id));
        int installedIconCount = 0;
        for (const iconpacks::Category& category : installed->categories) {
            installedIconCount += category.icons.size();
            for (const iconpacks::Icon& icon : category.icons) {
                const QFileInfo installedFile(icon.filePath);
                QVERIFY(installedFile.isFile());
                QCOMPARE(installedFile.suffix().toLower(), QStringLiteral("png"));
            }
        }
        QCOMPARE(installedIconCount, expected.iconCount);
        QCOMPARE(installed->iconCount, expected.iconCount);
        QCOMPARE(installed->totalBytes, expected.payloadBytes);
    }
    QCOMPARE(manager->installedPacks().size(), published.size() + 1);
}

void IconPackTests::refreshCatalogAndInstallWithBoundedParallelism() {
    QVector<QByteArray> images;
    QVector<QJsonObject> icons;
    for (int index = 0; index < 7; ++index) {
        const QByteArray image = png(
            QColor::fromHsv(index * 40, 220, 220),
            QSize(16 + index, 20 + index));
        QVERIFY(!image.isEmpty());
        images.append(image);
        const QString name = QStringLiteral("icon-%1").arg(index);
        icons.append(iconObject(name,
                                QStringLiteral("applications/%1.png").arg(name),
                                image));
    }

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.pngResponseDelayMs = 80;
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        manifestPayload(icons));
    for (int index = 0; index < images.size(); ++index) {
        fixture.responses.insert(
            QStringLiteral("/icon-packs/packs/test-pack/applications/icon-%1.png")
                .arg(index),
            images.at(index));
    }

    QTemporaryDir data;
    QVERIFY(data.isValid());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1500, &owner);
    QVERIFY(manager);
    QSignalSpy catalogSpy(manager, &iconpacks::IconPackManager::catalogChanged);
    QSignalSpy progressSpy(manager, &iconpacks::IconPackManager::progressChanged);

    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
    QVERIFY2(manager->catalogError().isEmpty(),
             qPrintable(manager->catalogError()));
    QCOMPARE(catalogSpy.count(), 1);
    const auto available = manager->pack(QStringLiteral("test-pack"));
    QVERIFY(available);
    QCOMPARE(available->state, iconpacks::State::Available);
    QCOMPARE(available->platforms,
             (QStringList{QStringLiteral("linux"), QStringLiteral("macos")}));

    manager->install(QStringLiteral("test-pack"));
    manager->refreshCatalog();
    QVERIFY(!manager->catalogError().isEmpty());
    const auto installed = waitForState(manager, QStringLiteral("test-pack"),
                                        iconpacks::State::Installed);
    QVERIFY(installed);
    QCOMPARE(installed->state, iconpacks::State::Installed);
    QVERIFY(manager->catalogError().isEmpty());
    QCOMPARE(installed->categories.size(), 1);
    QCOMPARE(installed->categories.first().title, QStringLiteral("Applications"));
    QCOMPARE(installed->categories.first().icons.size(), images.size());
    qint64 expectedBytes = 0;
    for (int index = 0; index < images.size(); ++index) {
        expectedBytes += images.at(index).size();
        QCOMPARE(installed->categories.first().icons.at(index).dimensions,
                 QSize(16 + index, 20 + index));
    }
    QCOMPARE(installed->receivedBytes, expectedBytes);
    QCOMPARE(installed->receivedBytes, installed->totalBytes);
    QVERIFY(progressSpy.count() >= 3);

    QCOMPARE(fixture.maximumConcurrentPngRequests, 4);
    QCOMPARE(fixture.completedPngResponses, images.size());
    QCOMPARE(fixture.cancelledPngRequests, 0);
    QCOMPARE(fixture.activePngRequests, 0);
    QCOMPARE(fixture.requests.size(), images.size() + 2);

    qint64 previousProgress = 0;
    for (const QList<QVariant>& arguments : progressSpy) {
        QCOMPARE(arguments.at(0).toString(), QStringLiteral("test-pack"));
        const qint64 received = arguments.at(1).toLongLong();
        const qint64 total = arguments.at(2).toLongLong();
        QVERIFY(received >= previousProgress);
        QVERIFY(total == 0 || received <= total);
        previousProgress = received;
    }
    QCOMPARE(previousProgress, expectedBytes);

    const QString packDir =
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"));
    QVERIFY(QFileInfo::exists(QDir(packDir).filePath(QStringLiteral("manifest.json"))));
    QVERIFY(QFileInfo::exists(
        QDir(packDir).filePath(QStringLiteral("applications/icon-0.png"))));
    QCOMPARE(QDir(QDir(data.path()).filePath(QStringLiteral("icon-packs")))
                 .entryList({QStringLiteral(".*.install-*")}, QDir::Dirs),
             QStringList{});
}

void IconPackTests::sameOriginManifestRedirectUsesFinalBaseUrl() {
    const QByteArray image = png(Qt::darkCyan);
    QVERIFY(!image.isEmpty());

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.redirects.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        QByteArrayLiteral("/icon-packs/releases/test-pack/manifest.json"));
    fixture.responses.insert(
        QStringLiteral("/icon-packs/releases/test-pack/manifest.json"),
        manifestPayload({iconObject(QStringLiteral("redirected"),
                                    QStringLiteral("redirected.png"), image)}));
    fixture.responses.insert(
        QStringLiteral("/icon-packs/releases/test-pack/redirected.png"), image);

    QTemporaryDir data;
    QVERIFY(data.isValid());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1500, &owner);
    QVERIFY(manager);
    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
    manager->install(QStringLiteral("test-pack"));
    const auto installed = waitForState(manager, QStringLiteral("test-pack"),
                                        iconpacks::State::Installed);
    QVERIFY(installed);
    QCOMPARE(installed->state, iconpacks::State::Installed);
    QVERIFY(fixture.requests.contains(
        QStringLiteral("/icon-packs/releases/test-pack/redirected.png")));
    QVERIFY(!fixture.requests.contains(
        QStringLiteral("/icon-packs/packs/test-pack/redirected.png")));
}

void IconPackTests::installedPackLoadsOfflineAndCanBeRemoved() {
    const QByteArray image = png(Qt::green);
    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        manifestPayload({iconObject(QStringLiteral("offline"),
                                    QStringLiteral("offline.png"), image)}));
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/offline.png"), image);

    QTemporaryDir data;
    QVERIFY(data.isValid());
    {
        QObject owner;
        auto* manager = iconpacks::IconPackManager::createForTesting(
            fixture.catalogUrl(), data.path(), 1500, &owner);
        manager->refreshCatalog();
        QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
        manager->install(QStringLiteral("test-pack"));
        const auto installed = waitForState(manager, QStringLiteral("test-pack"),
                                            iconpacks::State::Installed);
        QVERIFY(installed && installed->state == iconpacks::State::Installed);
    }

    // No refresh: the local manifest and PNG are sufficient after restart.
    QObject offlineOwner;
    auto* offline = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1500, &offlineOwner);
    const auto pack = offline->pack(QStringLiteral("test-pack"));
    QVERIFY(pack);
    QCOMPARE(pack->state, iconpacks::State::Installed);
    QCOMPARE(pack->categories.first().icons.first().dimensions, QSize(24, 24));
    QVERIFY(QFileInfo(pack->categories.first().icons.first().filePath).isAbsolute());
    QCOMPARE(offline->installedPacks().size(), 2);
    QVERIFY(offline->remove(QStringLiteral("test-pack")));
    QVERIFY(!offline->pack(QStringLiteral("test-pack")));
    QVERIFY(!QFileInfo::exists(
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"))));
    QVERIFY(!offline->remove(QStringLiteral("nightlock-default")));
}

void IconPackTests::removeFailureKeepsInstalledState() {
    const QByteArray image = png(Qt::darkGreen);
    QVERIFY(!image.isEmpty());

    QTemporaryDir data;
    QVERIFY(data.isValid());
    const QString packDirectory =
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"));
    const QByteArray manifest = manifestPayload(
        {iconObject(QStringLiteral("local"), QStringLiteral("local.png"), image)});
    QVERIFY(writeFile(QDir(packDirectory).filePath(QStringLiteral("manifest.json")),
                      manifest));
    QVERIFY(writeFile(QDir(packDirectory).filePath(QStringLiteral("local.png")),
                      image));

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"), manifest);
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/local.png"), image);
    {
        QObject owner;
        auto* manager = iconpacks::IconPackManager::createForTesting(
            fixture.catalogUrl(), data.path(), 1000, &owner);
        QVERIFY(manager);
        const auto before = manager->pack(QStringLiteral("test-pack"));
        QVERIFY(before);
        QCOMPARE(before->state, iconpacks::State::Installed);

        // Replace the cache directory after startup with a non-directory entry
        // to deterministically simulate an external removal failure.
        QVERIFY(QDir(packDirectory).removeRecursively());
        QVERIFY(writeFile(packDirectory, QByteArrayLiteral("blocked")));
        QVERIFY(!manager->remove(QStringLiteral("test-pack")));
        const auto after = manager->pack(QStringLiteral("test-pack"));
        QVERIFY(after);
        QCOMPARE(after->state, iconpacks::State::Installed);
        QVERIFY(!after->error.isEmpty());
        QCOMPARE(manager->installedPacks().size(), 2);
    }

    // A restart cannot load a regular file as a pack, but Retry must safely
    // replace this exact validated cache target rather than remain blocked.
    QVERIFY(QFileInfo(packDirectory).isFile());
    QObject restartedOwner;
    auto* restarted = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1000, &restartedOwner);
    QVERIFY(restarted);
    QVERIFY(!restarted->pack(QStringLiteral("test-pack")));
    restarted->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!restarted->isRefreshing(), 2000);
    restarted->install(QStringLiteral("test-pack"));
    const auto recovered = waitForState(restarted, QStringLiteral("test-pack"),
                                        iconpacks::State::Installed);
    QVERIFY(recovered);
    QCOMPARE(recovered->state, iconpacks::State::Installed);
    QVERIFY(QFileInfo(packDirectory).isDir());
    QVERIFY(QFileInfo::exists(
        QDir(packDirectory).filePath(QStringLiteral("local.png"))));
}

void IconPackTests::linkLikeDescendantBlocksRecursiveRemoval() {
    const QByteArray image = png(Qt::darkBlue);
    QVERIFY(!image.isEmpty());

    QTemporaryDir data;
    QTemporaryDir outside;
    QVERIFY(data.isValid());
    QVERIFY(outside.isValid());
    const QString outsideMarker =
        QDir(outside.path()).filePath(QStringLiteral("must-survive.txt"));
    QVERIFY(writeFile(outsideMarker, QByteArrayLiteral("outside")));

    const QString packDirectory =
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"));
    const QByteArray manifest = manifestPayload(
        {iconObject(QStringLiteral("local"), QStringLiteral("local.png"), image)});
    QVERIFY(writeFile(QDir(packDirectory).filePath(QStringLiteral("manifest.json")),
                      manifest));
    QVERIFY(writeFile(QDir(packDirectory).filePath(QStringLiteral("local.png")),
                      image));
    const QString nestedLink =
        QDir(packDirectory).filePath(QStringLiteral("undeclared-link"));
    if (!createDirectoryLinkLike(outside.path(), nestedLink))
        QSKIP("Could not create a directory symlink/junction for this platform");

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1000, &owner);
    QVERIFY(manager);
    const auto installed = manager->pack(QStringLiteral("test-pack"));
    QVERIFY(installed);
    QCOMPARE(installed->state, iconpacks::State::Installed);

    QVERIFY(!manager->remove(QStringLiteral("test-pack")));
    const auto preserved = manager->pack(QStringLiteral("test-pack"));
    QVERIFY(preserved);
    QCOMPARE(preserved->state, iconpacks::State::Installed);
    QVERIFY(QFileInfo::exists(outsideMarker));
    const QFileInfo linkInfo(nestedLink);
    QVERIFY(linkInfo.isSymLink() || linkInfo.isJunction());

    QVERIFY(removeDirectoryLinkLike(nestedLink));
    QVERIFY(QFileInfo::exists(outsideMarker));
    QVERIFY(manager->remove(QStringLiteral("test-pack")));
    QVERIFY(!QFileInfo::exists(packDirectory));
}

void IconPackTests::catalogRejectsTraversalAndUnknownCategories() {
    QTemporaryDir data;
    QVERIFY(data.isValid());

    HttpFixture traversalFixture;
    QVERIFY(traversalFixture.listen());
    traversalFixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                                      catalogPayload(QStringLiteral("../manifest.json")));
    QObject traversalOwner;
    auto* traversal = iconpacks::IconPackManager::createForTesting(
        traversalFixture.catalogUrl(), data.path(), 1000, &traversalOwner);
    traversal->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!traversal->isRefreshing(), 2000);
    QVERIFY(!traversal->catalogError().isEmpty());
    QCOMPARE(traversal->packs().size(), 1);

    HttpFixture reservedFixture;
    QVERIFY(reservedFixture.listen());
    reservedFixture.responses.insert(
        QStringLiteral("/icon-packs/catalog.json"),
        catalogPayload(QStringLiteral("packs/con/manifest.json"),
                       QStringLiteral("con")));
    QObject reservedOwner;
    auto* reserved = iconpacks::IconPackManager::createForTesting(
        reservedFixture.catalogUrl(), data.path(), 1000, &reservedOwner);
    reserved->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!reserved->isRefreshing(), 2000);
    QVERIFY(!reserved->catalogError().isEmpty());
    QCOMPARE(reserved->packs().size(), 1);

    const QByteArray image = png(Qt::yellow);
    HttpFixture categoryFixture;
    QVERIFY(categoryFixture.listen());
    categoryFixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                                     catalogPayload());
    categoryFixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        manifestPayload({iconObject(QStringLiteral("x"), QStringLiteral("x.png"), image)},
                        QStringLiteral("made-up-category")));
    QObject categoryOwner;
    auto* category = iconpacks::IconPackManager::createForTesting(
        categoryFixture.catalogUrl(), data.path(), 1000, &categoryOwner);
    category->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!category->isRefreshing(), 2000);
    category->install(QStringLiteral("test-pack"));
    const auto failed = waitForState(category, QStringLiteral("test-pack"),
                                     iconpacks::State::Failed);
    QVERIFY(failed);
    QCOMPARE(failed->state, iconpacks::State::Failed);
    QVERIFY(!failed->error.isEmpty());
}

void IconPackTests::manifestRejectsNonPortableDestinationPaths_data() {
    QTest::addColumn<QString>("firstPath");
    QTest::addColumn<QString>("secondPath");

    QTest::newRow("uppercase-segment")
        << QStringLiteral("Applications/icon.png") << QString();
    QTest::newRow("non-ascii-segment")
        << QString::fromUtf8("icônes/icon.png") << QString();
    QTest::newRow("reserved-file")
        << QStringLiteral("con.png") << QString();
    QTest::newRow("reserved-directory")
        << QStringLiteral("aux/icon.png") << QString();
    QTest::newRow("trailing-dot")
        << QStringLiteral("icons./icon.png") << QString();
    QTest::newRow("trailing-space")
        << QStringLiteral("icons/icon.png ") << QString();
    QTest::newRow("case-fold-alias")
        << QStringLiteral("icons/icon.png")
        << QStringLiteral("icons/ICON.png");
    QTest::newRow("duplicate-destination")
        << QStringLiteral("icons/icon.png")
        << QStringLiteral("icons/icon.png");
    QTest::newRow("file-directory-alias")
        << QStringLiteral("icons.png/first.png")
        << QStringLiteral("icons.png");
}

void IconPackTests::manifestRejectsNonPortableDestinationPaths() {
    QFETCH(QString, firstPath);
    QFETCH(QString, secondPath);

    const QByteArray image = png(Qt::cyan);
    QVERIFY(!image.isEmpty());
    QVector<QJsonObject> icons = {
        iconObject(QStringLiteral("first"), firstPath, image),
    };
    if (!secondPath.isEmpty())
        icons.append(iconObject(QStringLiteral("second"), secondPath, image));

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        manifestPayload(icons));

    QTemporaryDir data;
    QVERIFY(data.isValid());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1000, &owner);
    QVERIFY(manager);
    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
    manager->install(QStringLiteral("test-pack"));
    const auto failed = waitForState(manager, QStringLiteral("test-pack"),
                                     iconpacks::State::Failed);
    QVERIFY(failed);
    QVERIFY(!failed->error.isEmpty());
    QVERIFY(!QFileInfo::exists(
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"))));
}

void IconPackTests::startupDiscardsCorruptCacheAndAllowsReinstall_data() {
    QTest::addColumn<QString>("corruption");
    QTest::newRow("corrupt-manifest") << QStringLiteral("manifest");
    QTest::newRow("missing-png") << QStringLiteral("missing");
    QTest::newRow("bad-png-signature") << QStringLiteral("signature");
}

void IconPackTests::startupDiscardsCorruptCacheAndAllowsReinstall() {
    QFETCH(QString, corruption);

    const QByteArray image = png(Qt::magenta, QSize(20, 18));
    QVERIFY(!image.isEmpty());
    const QByteArray manifest = manifestPayload(
        {iconObject(QStringLiteral("recovered"),
                    QStringLiteral("applications/recovered.png"), image)});

    QTemporaryDir data;
    QVERIFY(data.isValid());
    const QString packDirectory =
        QDir(data.path()).filePath(QStringLiteral("icon-packs/test-pack"));
    const QString manifestPath =
        QDir(packDirectory).filePath(QStringLiteral("manifest.json"));
    QVERIFY(writeFile(manifestPath,
                      corruption == QLatin1String("manifest")
                          ? QByteArrayLiteral("{broken")
                          : manifest));
    if (corruption == QLatin1String("signature")) {
        QVERIFY(writeFile(
            QDir(packDirectory).filePath(
                QStringLiteral("applications/recovered.png")),
            QByteArray(image.size(), 'x')));
    }

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"), manifest);
    fixture.responses.insert(
        QStringLiteral(
            "/icon-packs/packs/test-pack/applications/recovered.png"),
        image);

    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 1500, &owner);
    QVERIFY(manager);
    QVERIFY(!manager->pack(QStringLiteral("test-pack")));
    QVERIFY(!QFileInfo::exists(packDirectory));

    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
    manager->install(QStringLiteral("test-pack"));
    const auto installed = waitForState(manager, QStringLiteral("test-pack"),
                                        iconpacks::State::Installed);
    QVERIFY(installed);
    QCOMPARE(installed->state, iconpacks::State::Installed);
    QCOMPARE(installed->categories.first().icons.first().dimensions,
             QSize(20, 18));
    QVERIFY(QFileInfo::exists(packDirectory));
}

void IconPackTests::installRejectsHashMismatchAndLeavesNoPartialPack() {
    const QByteArray declared = png(Qt::black);
    const QByteArray delivered = png(Qt::white);
    QVERIFY(!declared.isEmpty());
    QVERIFY(!delivered.isEmpty());
    QVector<QJsonObject> icons = {
        iconObject(QStringLiteral("bad"), QStringLiteral("bad.png"),
                   declared),
    };
    QVector<QByteArray> validImages;
    for (int index = 0; index < 6; ++index) {
        const QByteArray image = png(QColor::fromHsv(index * 50, 180, 200));
        QVERIFY(!image.isEmpty());
        validImages.append(image);
        const QString name = QStringLiteral("valid-%1").arg(index);
        icons.append(iconObject(name, name + QStringLiteral(".png"), image));
    }

    HttpFixture fixture;
    QVERIFY(fixture.listen());
    fixture.pngResponseDelayMs = 1000;
    const QString badPath =
        QStringLiteral("/icon-packs/packs/test-pack/bad.png");
    fixture.pngResponseDelaysMs.insert(badPath, 500);
    fixture.responses.insert(QStringLiteral("/icon-packs/catalog.json"),
                             catalogPayload());
    fixture.responses.insert(
        QStringLiteral("/icon-packs/packs/test-pack/manifest.json"),
        manifestPayload(icons));
    fixture.responses.insert(badPath, delivered);
    for (int index = 0; index < validImages.size(); ++index) {
        fixture.responses.insert(
            QStringLiteral("/icon-packs/packs/test-pack/valid-%1.png")
                .arg(index),
            validImages.at(index));
    }

    QTemporaryDir data;
    QVERIFY(data.isValid());
    QObject owner;
    auto* manager = iconpacks::IconPackManager::createForTesting(
        fixture.catalogUrl(), data.path(), 3000, &owner);
    QSignalSpy packSpy(manager, &iconpacks::IconPackManager::packChanged);
    manager->refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isRefreshing(), 2000);
    manager->install(QStringLiteral("test-pack"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.maximumConcurrentPngRequests, 4, 400);
    manager->refreshCatalog();
    QVERIFY(!manager->catalogError().isEmpty());
    const auto failed = waitForState(manager, QStringLiteral("test-pack"),
                                     iconpacks::State::Failed);
    QVERIFY(failed);
    QCOMPARE(failed->state, iconpacks::State::Failed);
    QVERIFY(manager->catalogError().isEmpty());
    QVERIFY(failed->error.contains(QStringLiteral("SHA-256")) ||
            failed->error.contains(QStringLiteral("size")));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.activePngRequests, 0, 1000);
    QCOMPARE(fixture.maximumConcurrentPngRequests, 4);
    QCOMPARE(fixture.completedPngResponses, 1);
    QCOMPARE(fixture.cancelledPngRequests, 3);
    int requestedPngs = 0;
    for (const QString& request : std::as_const(fixture.requests)) {
        if (request.endsWith(QLatin1String(".png")))
            ++requestedPngs;
    }
    QCOMPARE(requestedPngs, 4);

    const QDir root(QDir(data.path()).filePath(QStringLiteral("icon-packs")));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("test-pack"))));
    QCOMPARE(root.entryList({QStringLiteral(".*.install-*")}, QDir::Dirs),
             QStringList{});

    // The cancelled replies still have delayed server timers. They must not
    // re-enter the failed job, emit a second state change, or restage files.
    const int packChangesAfterFailure = packSpy.count();
    QTest::qWait(1100);
    QCOMPARE(packSpy.count(), packChangesAfterFailure);
    const auto stillFailed = manager->pack(QStringLiteral("test-pack"));
    QVERIFY(stillFailed);
    QCOMPARE(stillFailed->state, iconpacks::State::Failed);
    QCOMPARE(root.entryList({QStringLiteral(".*.install-*")}, QDir::Dirs),
             QStringList{});
}

QTEST_GUILESS_MAIN(IconPackTests)
#include "test_icon_packs.moc"
