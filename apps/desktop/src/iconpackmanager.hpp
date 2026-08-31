#pragma once

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <optional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace iconpacks {

// Stable cross-platform taxonomy shared by Linux, macOS and Windows packs.
// Manifests may only use one of these identifiers.
const QStringList& normalizedCategoryIds();
QString canonicalCategoryTitle(const QString& id);

struct Icon {
    QString id;
    QString title;
    // A Qt resource path for the built-in pack or an absolute local path for
    // an installed pack. Remote URLs are never exposed as selectable icons.
    QString filePath;
    QByteArray sha256;
    qint64 size = 0;
    QSize dimensions;
};

struct Category {
    QString id;
    QString title;
    QVector<Icon> icons;
};

class IconPackManager final : public QObject {
    Q_OBJECT
public:
    enum class State {
        Available,
        Downloading,
        Installed,
        Failed,
        BuiltIn,
        // Source-tree packs exposed only by NIGHTLOCK_DEMO builds. They are
        // selectable without being copied into the application bundle and
        // cannot be removed from the Icons Library.
        Preview,
    };
    Q_ENUM(State)

    struct Pack {
        QString id;
        QString title;
        QString description;
        QString version;
        QString author;
        QString license;
        QStringList platforms;
        State state = State::Available;
        QVector<Category> categories;
        int iconCount = 0;
        qint64 receivedBytes = 0;
        qint64 totalBytes = 0;
        QString error;
    };

    static IconPackManager* instance();
    ~IconPackManager() override;

#ifdef NIGHTLOCK_ICON_PACK_TESTING
    // dataDir is an isolated replacement for
    // QStandardPaths::AppLocalDataLocation; packs are stored below
    // dataDir/icon-packs. Tests may use an HTTP loopback endpoint; production
    // always uses the fixed official HTTPS URL.
    static IconPackManager* createForTesting(const QUrl& endpoint,
                                             const QString& dataDir,
                                             int requestDeadlineMs,
                                             QObject* parent = nullptr);
#endif

    QVector<Pack> packs() const;
    std::optional<Pack> pack(const QString& id) const;
    // Includes nightlock-default first, followed by downloaded packs.
    QVector<Pack> installedPacks() const;

    bool isRefreshing() const;
    QString catalogError() const;

public slots:
    void refreshCatalog();
    void install(const QString& id);
    bool remove(const QString& id);

signals:
    void catalogChanged();
    void catalogErrorChanged(const QString& error);
    void refreshingChanged(bool refreshing);
    void packChanged(const QString& id);
    void progressChanged(const QString& id, qint64 received, qint64 total);

private:
    explicit IconPackManager(QObject* parent = nullptr);
    IconPackManager(const QUrl& endpoint, const QString& dataDir,
                    int requestDeadlineMs, bool testing,
                    QObject* parent);

    struct Private;

    void loadInstalledPacks();
    void finishCatalogRequest(QNetworkReply* reply, QByteArray payload);
    void finishManifestRequest(QNetworkReply* reply, QByteArray payload);
    void pumpIconDownloads();
    void updateInstallProgress(quint64 token);
    void finishIconRequest(QNetworkReply* reply, QByteArray payload,
                           quint64 token, int iconIndex);
    void failInstall(const QString& message);
    void finishInstall();
    void replaceOrAppendPack(Pack pack);
    void setCatalogError(const QString& error);

    std::unique_ptr<Private> d_;
};

using Pack = IconPackManager::Pack;
using State = IconPackManager::State;

}  // namespace iconpacks

Q_DECLARE_METATYPE(iconpacks::IconPackManager::State)
