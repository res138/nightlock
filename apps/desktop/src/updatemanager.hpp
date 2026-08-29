#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

namespace updates {

// Metadata from GitHub's latest stable release. The platform asset is empty
// only when Nightlock does not publish an installer for the running platform.
struct ReleaseInfo {
    QVersionNumber version;
    QString tagName;
    QString releaseUrl;
    QString assetName;
    QString assetUrl;
    QString notes;
};

// Pure helpers kept public so the release contract can be unit-tested without
// making a network request.
QVersionNumber parseVersion(const QString& value);
bool isNewerVersion(const QVersionNumber& candidate,
                    const QVersionNumber& current);
QString platformAssetName(const QVersionNumber& version);
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& payload,
                                              QString* error = nullptr);

bool checkOnStartupEnabled();
void setCheckOnStartupEnabled(bool enabled);

class UpdateManager final : public QObject {
    Q_OBJECT
public:
    enum class CheckMode {
        Startup,
        Manual,
    };

    static UpdateManager* instance();

#ifdef NIGHTLOCK_UPDATE_TESTING
    // Test builds may point a separate manager at a loopback server and use a
    // short deadline. Production builds expose only instance(), whose endpoint
    // remains the fixed official GitHub API URL.
    static UpdateManager* createForTesting(const QUrl& endpoint,
                                           int requestDeadlineMs,
                                           QObject* parent = nullptr);
#endif

    bool isChecking() const { return checking_; }
    void checkForUpdates(QWidget* parent, CheckMode mode);

signals:
    void checkingChanged(bool checking);

private:
    explicit UpdateManager(QObject* parent = nullptr);
    UpdateManager(const QUrl& endpoint, int requestDeadlineMs,
                  QObject* parent);

    void finishRequest(QNetworkReply* reply);
    void showUpdateAvailable(const ReleaseInfo& release, QWidget* parent);
    void showUpToDate(QWidget* parent);
    void showFailure(const QString& message, QWidget* parent);

    QNetworkAccessManager* network_;
    QUrl endpoint_;
    int requestDeadlineMs_;
    QPointer<QWidget> dialogParent_;
    QByteArray responseBody_;
    CheckMode mode_ = CheckMode::Startup;
    bool checking_ = false;
    bool responseTooLarge_ = false;
};

}  // namespace updates
