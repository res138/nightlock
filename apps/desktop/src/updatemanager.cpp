#include "updatemanager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace updates {
namespace {

constexpr auto kCheckOnStartupKey = "updates/check-on-startup";
constexpr auto kLatestReleaseApi =
    "https://api.github.com/repos/res138/nightlock/releases/latest";
constexpr auto kReleasePathPrefix = "/res138/nightlock/releases/";
constexpr qsizetype kMaximumReleasePayloadSize = 1024 * 1024;
constexpr int kRequestDeadlineMs = 15000;
constexpr auto kDeadlineExceededProperty = "nightlockUpdateDeadlineExceeded";

QString versionText(const QVersionNumber& version) {
    return version.toString();
}

bool isTrustedReleaseUrl(const QString& value, const QString& expectedPath) {
    const QUrl url(value);
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host().compare(QLatin1String("github.com"), Qt::CaseInsensitive) == 0 &&
           url.port(-1) == -1 && url.userInfo().isEmpty() &&
           url.query().isEmpty() && url.fragment().isEmpty() &&
           url.path() == expectedPath &&
           expectedPath.startsWith(QLatin1String(kReleasePathPrefix));
}

void setError(QString* target, const QString& message) {
    if (target)
        *target = message;
}

QWidget* effectiveParent(const QPointer<QWidget>& requested) {
    return requested ? requested.data() : QApplication::activeWindow();
}

}  // namespace

QVersionNumber parseVersion(const QString& value) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^v?([0-9]+)\.([0-9]+)\.([0-9]+)$)"));
    const QRegularExpressionMatch match = expression.match(value.trimmed());
    if (!match.hasMatch())
        return {};

    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = false;
    const int major = match.captured(1).toInt(&majorOk);
    const int minor = match.captured(2).toInt(&minorOk);
    const int patch = match.captured(3).toInt(&patchOk);
    if (!majorOk || !minorOk || !patchOk)
        return {};
    return QVersionNumber(major, minor, patch);
}

bool isNewerVersion(const QVersionNumber& candidate,
                    const QVersionNumber& current) {
    return !candidate.isNull() && !current.isNull() && candidate > current;
}

QString platformAssetName(const QVersionNumber& version) {
    if (version.isNull())
        return {};
    const QString value = versionText(version);
#if defined(Q_OS_MACOS)
    return QStringLiteral("Nightlock-%1-macOS.dmg").arg(value);
#elif defined(Q_OS_WIN)
    return QStringLiteral("Nightlock-%1-Windows-Setup.exe").arg(value);
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("nightlock_%1_amd64.deb").arg(value);
#else
    return {};
#endif
}

std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& payload,
                                              QString* error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QCoreApplication::translate(
                            "UpdateManager", "GitHub returned invalid release metadata."));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("draft")).isBool() ||
        !object.value(QStringLiteral("prerelease")).isBool()) {
        setError(error, QCoreApplication::translate(
                            "UpdateManager", "GitHub returned incomplete release metadata."));
        return std::nullopt;
    }
    if (object.value(QStringLiteral("draft")).toBool() ||
        object.value(QStringLiteral("prerelease")).toBool()) {
        setError(error, QCoreApplication::translate(
                            "UpdateManager", "GitHub did not return a stable release."));
        return std::nullopt;
    }

    ReleaseInfo release;
    release.tagName = object.value(QStringLiteral("tag_name")).toString();
    release.version = parseVersion(release.tagName);
    if (release.version.isNull() ||
        release.tagName != QLatin1Char('v') + versionText(release.version)) {
        setError(error, QCoreApplication::translate(
                            "UpdateManager", "The latest release has an invalid version tag."));
        return std::nullopt;
    }

    release.releaseUrl = object.value(QStringLiteral("html_url")).toString();
    const QString expectedReleasePath =
        QStringLiteral("/res138/nightlock/releases/tag/%1").arg(release.tagName);
    if (!isTrustedReleaseUrl(release.releaseUrl, expectedReleasePath)) {
        setError(error, QCoreApplication::translate(
                            "UpdateManager", "The latest release has an invalid GitHub URL."));
        return std::nullopt;
    }
    release.notes = object.value(QStringLiteral("body")).toString();

    const QString expectedAsset = platformAssetName(release.version);
    if (!expectedAsset.isEmpty()) {
        const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
        int matchingAssets = 0;
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            if (asset.value(QStringLiteral("name")).toString() != expectedAsset)
                continue;
            ++matchingAssets;
            if (asset.value(QStringLiteral("state")).toString() != QLatin1String("uploaded"))
                continue;
            const QString candidate =
                asset.value(QStringLiteral("browser_download_url")).toString();
            const QString expectedAssetPath =
                QStringLiteral("/res138/nightlock/releases/download/%1/%2")
                    .arg(release.tagName, expectedAsset);
            if (!isTrustedReleaseUrl(candidate, expectedAssetPath))
                continue;
            release.assetName = expectedAsset;
            release.assetUrl = candidate;
        }
        if (matchingAssets != 1 || release.assetUrl.isEmpty()) {
            setError(error, QCoreApplication::translate(
                                "UpdateManager",
                                "The stable release has no valid installer for this platform."));
            return std::nullopt;
        }
    }

    if (error)
        error->clear();
    return release;
}

bool checkOnStartupEnabled() {
    return QSettings().value(QLatin1String(kCheckOnStartupKey), true).toBool();
}

void setCheckOnStartupEnabled(bool enabled) {
    QSettings settings;
    if (settings.value(QLatin1String(kCheckOnStartupKey), true).toBool() != enabled)
        settings.setValue(QLatin1String(kCheckOnStartupKey), enabled);
}

UpdateManager::UpdateManager(QObject* parent)
    : UpdateManager(QUrl(QLatin1String(kLatestReleaseApi)),
                    kRequestDeadlineMs, parent) {}

UpdateManager::UpdateManager(const QUrl& endpoint, int requestDeadlineMs,
                             QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      endpoint_(endpoint),
      requestDeadlineMs_(requestDeadlineMs) {
    Q_ASSERT(endpoint_.isValid());
    Q_ASSERT(requestDeadlineMs_ > 0);
}

UpdateManager* UpdateManager::instance() {
    static UpdateManager* manager =
        new UpdateManager(QCoreApplication::instance());
    return manager;
}

#ifdef NIGHTLOCK_UPDATE_TESTING
UpdateManager* UpdateManager::createForTesting(const QUrl& endpoint,
                                               int requestDeadlineMs,
                                               QObject* parent) {
    return new UpdateManager(endpoint, requestDeadlineMs, parent);
}
#endif

void UpdateManager::checkForUpdates(QWidget* parent, CheckMode mode) {
    if (checking_) {
        // A manual request made during the startup check should still report
        // its result instead of silently completing in the background.
        if (mode == CheckMode::Manual) {
            mode_ = mode;
            dialogParent_ = parent;
        }
        return;
    }

    checking_ = true;
    mode_ = mode;
    dialogParent_ = parent;
    responseBody_.clear();
    responseTooLarge_ = false;
    emit checkingChanged(true);

    QNetworkRequest request{endpoint_};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2026-03-10");
    request.setRawHeader("User-Agent",
                         QByteArray("Nightlock/") + QByteArray(NIGHTLOCK_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(requestDeadlineMs_);

    QNetworkReply* reply = network_->get(request);
    // QNetworkRequest's transfer timeout is an inactivity timeout. Keep a
    // separate wall-clock deadline so a slow-drip response cannot leave the
    // settings button disabled forever.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply] {
        reply->setProperty(kDeadlineExceededProperty, true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(requestDeadlineMs_);
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        responseBody_.append(reply->readAll());
        if (responseBody_.size() <= kMaximumReleasePayloadSize)
            return;
        responseTooLarge_ = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { finishRequest(reply); });
}

void UpdateManager::finishRequest(QNetworkReply* reply) {
    const CheckMode completedMode = mode_;
    const QPointer<QWidget> requestedParent = dialogParent_;
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const bool deadlineExceeded =
        reply->property(kDeadlineExceededProperty).toBool();
    responseBody_.append(reply->readAll());
    const QByteArray payload = std::move(responseBody_);
    const bool responseTooLarge =
        responseTooLarge_ || payload.size() > kMaximumReleasePayloadSize;
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    checking_ = false;
    responseTooLarge_ = false;
    dialogParent_.clear();
    emit checkingChanged(false);

    QWidget* parent = effectiveParent(requestedParent);
    if (responseTooLarge) {
        const QString message = tr("GitHub returned release metadata that is too large.");
        if (completedMode == CheckMode::Manual)
            showFailure(message, parent);
        else
            qWarning().noquote() << "Nightlock update metadata rejected:" << message;
        return;
    }
    if (networkError != QNetworkReply::NoError || status != 200) {
        if (deadlineExceeded) {
            const QString message = tr("The update check timed out.");
            if (completedMode == CheckMode::Manual)
                showFailure(message, parent);
            else
                qWarning().noquote() << "Nightlock update check failed:" << message;
            return;
        }
        QString detail = networkErrorText;
        const QJsonDocument errorDocument = QJsonDocument::fromJson(payload);
        if (errorDocument.isObject()) {
            const QString githubMessage =
                errorDocument.object().value(QStringLiteral("message")).toString();
            if (!githubMessage.isEmpty())
                detail = githubMessage;
        }
        const QString message = status > 0
                                    ? tr("GitHub returned HTTP %1: %2").arg(status).arg(detail)
                                    : tr("Could not contact GitHub: %1").arg(detail);
        if (completedMode == CheckMode::Manual)
            showFailure(message, parent);
        else
            qWarning().noquote() << "Nightlock update check failed:" << message;
        return;
    }

    QString parseError;
    const std::optional<ReleaseInfo> release =
        parseLatestRelease(payload, &parseError);
    if (!release) {
        if (completedMode == CheckMode::Manual)
            showFailure(parseError, parent);
        else
            qWarning().noquote() << "Nightlock update metadata rejected:" << parseError;
        return;
    }

    const QVersionNumber current = parseVersion(QLatin1String(NIGHTLOCK_VERSION));
    if (current.isNull()) {
        if (completedMode == CheckMode::Manual)
            showFailure(tr("This build has an invalid version number."), parent);
        return;
    }

    if (isNewerVersion(release->version, current)) {
        showUpdateAvailable(*release, parent);
    } else if (completedMode == CheckMode::Manual) {
        showUpToDate(parent);
    }
}

void UpdateManager::showUpdateAvailable(const ReleaseInfo& release,
                                        QWidget* parent) {
    QMessageBox box(QMessageBox::Information, tr("Update Available"),
                    tr("Nightlock %1 is available. You are using %2.")
                        .arg(versionText(release.version),
                             QString::fromLatin1(NIGHTLOCK_VERSION)),
                    QMessageBox::NoButton, parent);
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(
        release.assetUrl.isEmpty()
            ? tr("No installer is published for this platform. You can view the release on GitHub.")
            : tr("Nightlock installers are not code-signed yet. Download the "
                 "installer from the published stable GitHub release?"));
    if (!release.notes.trimmed().isEmpty())
        box.setDetailedText(release.notes.trimmed());

    auto* action = box.addButton(
        release.assetUrl.isEmpty() ? tr("View Release") : tr("Download Update"),
        QMessageBox::AcceptRole);
    auto* later = box.addButton(tr("Later"), QMessageBox::RejectRole);
    // A startup result can arrive while the user is pressing Enter to unlock
    // the vault. Make the non-navigating choice the safe default and escape
    // action so that keystroke can never open an installer unexpectedly.
    box.setDefaultButton(later);
    box.setEscapeButton(later);
    box.exec();

    if (box.clickedButton() != action)
        return;
    const QUrl target(release.assetUrl.isEmpty() ? release.releaseUrl
                                                  : release.assetUrl);
    if (!QDesktopServices::openUrl(target))
        showFailure(tr("Could not open the update in your web browser."), parent);
}

void UpdateManager::showUpToDate(QWidget* parent) {
    QMessageBox::information(
        parent, tr("No Updates Available"),
        tr("No newer stable release is available for Nightlock %1.")
            .arg(QString::fromLatin1(NIGHTLOCK_VERSION)));
}

void UpdateManager::showFailure(const QString& message, QWidget* parent) {
    QMessageBox::warning(parent, tr("Update Check Failed"), message);
}

}  // namespace updates
