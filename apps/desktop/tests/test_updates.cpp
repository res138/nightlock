#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include "updatemanager.hpp"

namespace {

QByteArray releasePayload(const QString& tag = QStringLiteral("v1.3.0"),
                          bool draft = false, bool prerelease = false,
                          int platformAssetCopies = 1) {
    const QVersionNumber version = updates::parseVersion(tag);
    const QString assetName = updates::platformAssetName(version);
    QJsonArray assets;
    for (int i = 0; i < platformAssetCopies && !assetName.isEmpty(); ++i) {
        assets.append(QJsonObject{
            {QStringLiteral("name"), assetName},
            {QStringLiteral("state"), QStringLiteral("uploaded")},
            {QStringLiteral("browser_download_url"),
             QStringLiteral("https://github.com/res138/nightlock/releases/download/%1/%2")
                 .arg(tag, assetName)},
        });
    }
    assets.append(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("SHA256SUMS")},
        {QStringLiteral("state"), QStringLiteral("uploaded")},
        {QStringLiteral("browser_download_url"),
         QStringLiteral("https://github.com/res138/nightlock/releases/download/%1/SHA256SUMS")
             .arg(tag)},
    });

    return QJsonDocument(QJsonObject{
                             {QStringLiteral("tag_name"), tag},
                             {QStringLiteral("html_url"),
                              QStringLiteral("https://github.com/res138/nightlock/releases/tag/%1")
                                  .arg(tag)},
                             {QStringLiteral("draft"), draft},
                             {QStringLiteral("prerelease"), prerelease},
                             {QStringLiteral("body"), QStringLiteral("Release notes")},
                             {QStringLiteral("assets"), assets},
                         })
        .toJson(QJsonDocument::Compact);
}

QString currentReleaseTag() {
    return QStringLiteral("v%1").arg(QLatin1String(NIGHTLOCK_VERSION));
}

QUrl loopbackReleaseUrl(const QTcpServer& server) {
    return QUrl(QStringLiteral(
                    "http://127.0.0.1:%1/repos/res138/nightlock/releases/latest")
                    .arg(server.serverPort()));
}

QByteArray successfulHttpResponse(const QByteArray& payload) {
    QByteArray response = QByteArrayLiteral(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: ");
    response += QByteArray::number(payload.size());
    response += QByteArrayLiteral("\r\n\r\n");
    response += payload;
    return response;
}

}  // namespace

class UpdateTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void startupCheckDefaultsOnAndPersists();
    void actualGetUsesReleaseContractAndTransitionsCheckingState();
    void slowDripResponseStillHitsWallClockDeadline();
    void versionsAreStrictAndComparedNumerically();
    void platformAssetNamesMatchReleaseWorkflow();
    void stableReleaseSelectsExactPlatformAsset();
    void unstableAndMalformedReleasesAreRejected();
    void duplicateOrMissingPlatformAssetsAreRejected();

private:
    QTemporaryDir settingsDir_;
};

void UpdateTests::initTestCase() {
    QVERIFY2(settingsDir_.isValid(), "Could not create an isolated QSettings directory");

    QCoreApplication::setOrganizationName(QStringLiteral("NightlockTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Updates"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir_.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       settingsDir_.path());
}

void UpdateTests::init() {
    QSettings settings;
    settings.clear();
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

void UpdateTests::startupCheckDefaultsOnAndPersists() {
    QVERIFY(updates::checkOnStartupEnabled());

    updates::setCheckOnStartupEnabled(false);
    QVERIFY(!updates::checkOnStartupEnabled());
    QCOMPARE(QSettings().value(QStringLiteral("updates/check-on-startup")).toBool(),
             false);

    updates::setCheckOnStartupEnabled(true);
    QVERIFY(updates::checkOnStartupEnabled());
}

void UpdateTests::actualGetUsesReleaseContractAndTransitionsCheckingState() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));

    QByteArray requestBytes;
    int acceptedConnections = 0;
    bool responseSent = false;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket* socket = server.nextPendingConnection();
            ++acceptedConnections;
            connect(socket, &QTcpSocket::readyRead, &server, [&, socket] {
                requestBytes += socket->readAll();
                if (responseSent || !requestBytes.contains("\r\n\r\n"))
                    return;

                responseSent = true;
                const QByteArray payload = releasePayload(currentReleaseTag());
                socket->write(successfulHttpResponse(payload));
                socket->disconnectFromHost();
            });
        }
    });

    QObject owner;
    auto* manager = updates::UpdateManager::createForTesting(
        loopbackReleaseUrl(server), 2000, &owner);
    QSignalSpy stateSpy(manager, &updates::UpdateManager::checkingChanged);
    QVERIFY(stateSpy.isValid());

    manager->checkForUpdates(nullptr,
                             updates::UpdateManager::CheckMode::Startup);
    // A second startup caller joins the in-flight request instead of issuing a
    // duplicate GET or emitting another state transition.
    manager->checkForUpdates(nullptr,
                             updates::UpdateManager::CheckMode::Startup);
    QVERIFY(manager->isChecking());
    QCOMPARE(stateSpy.count(), 1);
    QVERIFY(stateSpy.at(0).at(0).toBool());

    QTRY_VERIFY_WITH_TIMEOUT(responseSent, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isChecking(), 2000);

    QCOMPARE(acceptedConnections, 1);
    QCOMPARE(stateSpy.count(), 2);
    QVERIFY(!stateSpy.at(1).at(0).toBool());
    QVERIFY(requestBytes.startsWith(
        "GET /repos/res138/nightlock/releases/latest HTTP/1.1\r\n"));

    const QByteArray lowerRequest = requestBytes.toLower();
    QVERIFY(lowerRequest.contains(
        "\r\naccept: application/vnd.github+json\r\n"));
    QVERIFY(lowerRequest.contains(
        "\r\nx-github-api-version: 2026-03-10\r\n"));
    const QByteArray expectedUserAgent =
        QByteArrayLiteral("\r\nuser-agent: nightlock/") +
        QByteArray(NIGHTLOCK_VERSION).toLower() + QByteArrayLiteral("\r\n");
    QVERIFY(lowerRequest.contains(expectedUserAgent));
}

void UpdateTests::slowDripResponseStillHitsWallClockDeadline() {
    constexpr int kTestDeadlineMs = 750;

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));

    QByteArray requestBytes;
    QPointer<QTcpSocket> dripSocket;
    bool headersSent = false;
    int dripWrites = 0;
    QTimer dripTimer;
    dripTimer.setInterval(50);
    connect(&dripTimer, &QTimer::timeout, &server, [&] {
        if (!dripSocket ||
            dripSocket->state() != QAbstractSocket::ConnectedState) {
            dripTimer.stop();
            return;
        }
        dripSocket->write(QByteArrayLiteral("1\r\nx\r\n"));
        dripSocket->flush();
        ++dripWrites;
    });
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        QTcpSocket* socket = server.nextPendingConnection();
        dripSocket = socket;
        connect(socket, &QTcpSocket::disconnected, &dripTimer,
                &QTimer::stop);
        connect(socket, &QTcpSocket::readyRead, &server, [&, socket] {
            requestBytes += socket->readAll();
            if (headersSent || !requestBytes.contains("\r\n\r\n"))
                return;

            headersSent = true;
            socket->write(QByteArrayLiteral(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Connection: keep-alive\r\n\r\n"));
            socket->flush();
            dripTimer.start();
        });
    });

    QObject owner;
    auto* manager = updates::UpdateManager::createForTesting(
        loopbackReleaseUrl(server), kTestDeadlineMs, &owner);
    QSignalSpy stateSpy(manager, &updates::UpdateManager::checkingChanged);
    QVERIFY(stateSpy.isValid());

    QElapsedTimer elapsed;
    elapsed.start();
    manager->checkForUpdates(nullptr,
                             updates::UpdateManager::CheckMode::Startup);

    QTRY_VERIFY_WITH_TIMEOUT(headersSent, 500);
    QTRY_VERIFY_WITH_TIMEOUT(dripWrites >= 3, 500);
    QVERIFY2(manager->isChecking(),
             "The request ended despite active slow-drip traffic");
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isChecking(), 2000);
    dripTimer.stop();

    QVERIFY2(elapsed.elapsed() < 1800,
             "The wall-clock deadline did not abort the slow-drip response");
    QCOMPARE(stateSpy.count(), 2);
    QVERIFY(stateSpy.at(0).at(0).toBool());
    QVERIFY(!stateSpy.at(1).at(0).toBool());
}

void UpdateTests::versionsAreStrictAndComparedNumerically() {
    QCOMPARE(updates::parseVersion(QStringLiteral("v1.2.10")),
             QVersionNumber(1, 2, 10));
    QCOMPARE(updates::parseVersion(QStringLiteral("1.2.3")),
             QVersionNumber(1, 2, 3));
    QVERIFY(updates::parseVersion(QStringLiteral("v1.2")).isNull());
    QVERIFY(updates::parseVersion(QStringLiteral("v1.2.3-beta.1")).isNull());
    QVERIFY(updates::parseVersion(QStringLiteral("release-1.2.3")).isNull());

    QVERIFY(updates::isNewerVersion(QVersionNumber(1, 10, 0),
                                    QVersionNumber(1, 9, 9)));
    QVERIFY(!updates::isNewerVersion(QVersionNumber(1, 2, 5),
                                     QVersionNumber(1, 2, 5)));
    QVERIFY(!updates::isNewerVersion(QVersionNumber(1, 2, 4),
                                     QVersionNumber(1, 2, 5)));
}

void UpdateTests::platformAssetNamesMatchReleaseWorkflow() {
    const QVersionNumber version(2, 10, 3);
#if defined(Q_OS_MACOS)
    QCOMPARE(updates::platformAssetName(version),
             QStringLiteral("Nightlock-2.10.3-macOS.dmg"));
#elif defined(Q_OS_WIN)
    QCOMPARE(updates::platformAssetName(version),
             QStringLiteral("Nightlock-2.10.3-Windows-Setup.exe"));
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    QCOMPARE(updates::platformAssetName(version),
             QStringLiteral("nightlock_2.10.3_amd64.deb"));
#else
    QVERIFY(updates::platformAssetName(version).isEmpty());
#endif
    QVERIFY(updates::platformAssetName(QVersionNumber()).isEmpty());
}

void UpdateTests::stableReleaseSelectsExactPlatformAsset() {
    QString error;
    const auto release = updates::parseLatestRelease(releasePayload(), &error);
    QVERIFY2(release.has_value(), qPrintable(error));
    QCOMPARE(release->version, QVersionNumber(1, 3, 0));
    QCOMPARE(release->tagName, QStringLiteral("v1.3.0"));
    QCOMPARE(release->releaseUrl,
             QStringLiteral("https://github.com/res138/nightlock/releases/tag/v1.3.0"));
    QCOMPARE(release->assetName,
             updates::platformAssetName(QVersionNumber(1, 3, 0)));
    QVERIFY(release->assetUrl.endsWith(release->assetName));
    QCOMPARE(release->notes, QStringLiteral("Release notes"));
    QVERIFY(error.isEmpty());
}

void UpdateTests::unstableAndMalformedReleasesAreRejected() {
    QString error;
    QVERIFY(!updates::parseLatestRelease(releasePayload(QStringLiteral("v1.3.0"), true),
                                        &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!updates::parseLatestRelease(
        releasePayload(QStringLiteral("v1.3.0"), false, true), &error));
    QVERIFY(!updates::parseLatestRelease(releasePayload(QStringLiteral("1.3.0")),
                                        &error));
    QVERIFY(!updates::parseLatestRelease(releasePayload(QStringLiteral("v1.3.0 ")),
                                        &error));
    QVERIFY(!updates::parseLatestRelease(releasePayload(QStringLiteral("v01.3.0")),
                                        &error));
    QVERIFY(!updates::parseLatestRelease(QByteArrayLiteral("not json"), &error));

    QJsonObject wrongUrl =
        QJsonDocument::fromJson(releasePayload()).object();
    wrongUrl.insert(QStringLiteral("html_url"),
                    QStringLiteral("https://example.com/res138/nightlock/releases/tag/v1.3.0"));
    QVERIFY(!updates::parseLatestRelease(
        QJsonDocument(wrongUrl).toJson(QJsonDocument::Compact), &error));

    QJsonObject incomplete =
        QJsonDocument::fromJson(releasePayload()).object();
    incomplete.remove(QStringLiteral("draft"));
    QVERIFY(!updates::parseLatestRelease(
        QJsonDocument(incomplete).toJson(QJsonDocument::Compact), &error));
}

void UpdateTests::duplicateOrMissingPlatformAssetsAreRejected() {
    if (updates::platformAssetName(QVersionNumber(1, 3, 0)).isEmpty())
        QSKIP("This platform has no Nightlock release artifact contract");

    QString error;
    QVERIFY(!updates::parseLatestRelease(releasePayload(
                                            QStringLiteral("v1.3.0"), false, false, 0),
                                        &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!updates::parseLatestRelease(releasePayload(
                                            QStringLiteral("v1.3.0"), false, false, 2),
                                        &error));

    QJsonObject foreignAsset =
        QJsonDocument::fromJson(releasePayload()).object();
    QJsonArray assets = foreignAsset.value(QStringLiteral("assets")).toArray();
    QJsonObject platformAsset = assets.first().toObject();
    platformAsset.insert(QStringLiteral("browser_download_url"),
                         QStringLiteral("https://example.com/Nightlock.dmg"));
    assets.replace(0, platformAsset);
    foreignAsset.insert(QStringLiteral("assets"), assets);
    QVERIFY(!updates::parseLatestRelease(
        QJsonDocument(foreignAsset).toJson(QJsonDocument::Compact), &error));

    QJsonObject incompleteAsset =
        QJsonDocument::fromJson(releasePayload()).object();
    QJsonArray incompleteAssets =
        incompleteAsset.value(QStringLiteral("assets")).toArray();
    QJsonObject pendingAsset = incompleteAssets.first().toObject();
    pendingAsset.insert(QStringLiteral("state"), QStringLiteral("new"));
    incompleteAssets.replace(0, pendingAsset);
    incompleteAsset.insert(QStringLiteral("assets"), incompleteAssets);
    QVERIFY(!updates::parseLatestRelease(
        QJsonDocument(incompleteAsset).toJson(QJsonDocument::Compact), &error));
}

QTEST_MAIN(UpdateTests)
#include "test_updates.moc"
