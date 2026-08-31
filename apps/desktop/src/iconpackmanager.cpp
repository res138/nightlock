#include "iconpackmanager.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace iconpacks {
namespace {

constexpr auto kCatalogEndpoint =
    "https://raw.githubusercontent.com/res138/nightlock/main/icon-packs/catalog.json";
constexpr auto kProductionHost = "raw.githubusercontent.com";
constexpr auto kProductionRepositoryPath = "/res138/nightlock/main/";
constexpr auto kStorageDirectory = "icon-packs";
constexpr auto kManifestFile = "manifest.json";
constexpr auto kDeadlineExceededProperty = "nightlockIconPackDeadlineExceeded";
constexpr auto kPayloadTooLargeProperty = "nightlockIconPackPayloadTooLarge";
constexpr auto kMaximumCatalogSize = 1024 * 1024;
constexpr auto kMaximumManifestSize = 2 * 1024 * 1024;
constexpr auto kMaximumIconSize = 16 * 1024 * 1024;
constexpr qint64 kMaximumPackSize = 512LL * 1024 * 1024;
constexpr int kMaximumPacks = 256;
constexpr int kMaximumCategories = 16;
constexpr int kMaximumIcons = 4096;
constexpr int kMaximumImageExtent = 4096;
constexpr qint64 kMaximumImagePixels = 16LL * 1024 * 1024;
constexpr int kRequestDeadlineMs = 30000;
constexpr int kMaximumConcurrentIconDownloads = 4;

struct UrlPolicy {
    QUrl endpoint;
    QUrl contentRoot;
    bool testing = false;
};

struct ParsedIcon {
    Icon icon;
    QString destination;
    QUrl sourceUrl;
    int categoryIndex = -1;
    int iconIndex = -1;
};

struct ParsedManifest {
    IconPackManager::Pack pack;
    QVector<ParsedIcon> icons;
};

struct Transfer {
    QNetworkReply* reply = nullptr;
    std::shared_ptr<QByteArray> payload;
};

void setError(QString* target, const QString& message) {
    if (target)
        *target = message;
}

bool isLinkLike(const QFileInfo& info) {
    return info.isSymLink() || info.isJunction();
}

bool treeContainsLinkLikeEntry(const QString& directory) {
    const QFileInfo rootInfo(directory);
    if (!rootInfo.isDir() || isLinkLike(rootInfo))
        return true;

    const QFileInfoList entries = QDir(directory).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (isLinkLike(entry))
            return true;
        if (entry.isDir() && treeContainsLinkLikeEntry(entry.absoluteFilePath()))
            return true;
    }
    return false;
}

bool removeOrdinaryDirectoryTree(const QString& directory) {
    const QFileInfo info(directory);
    if (!info.isDir() || isLinkLike(info) ||
        treeContainsLinkLikeEntry(directory))
        return false;
    return QDir(directory).removeRecursively();
}

bool ensureOrdinaryDirectory(const QString& directory, QString* error) {
    const QFileInfo before(directory);
    if (before.exists() || isLinkLike(before)) {
        if (before.isDir() && !isLinkLike(before))
            return true;
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack directory is unsafe."));
        return false;
    }
    if (!QDir().mkpath(directory)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Could not create the icon directory."));
        return false;
    }
    const QFileInfo after(directory);
    if (!after.isDir() || isLinkLike(after)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack directory is unsafe."));
        return false;
    }
    return true;
}

bool sameOrigin(const QUrl& left, const QUrl& right) {
    const auto defaultPort = [](const QUrl& url) {
        if (url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0)
            return 443;
        if (url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0)
            return 80;
        return -1;
    };
    return left.scheme().compare(right.scheme(), Qt::CaseInsensitive) == 0 &&
           left.host().compare(right.host(), Qt::CaseInsensitive) == 0 &&
           left.port(defaultPort(left)) == right.port(defaultPort(right)) &&
           left.userInfo().isEmpty() && right.userInfo().isEmpty();
}

bool pathIsWithin(const QString& path, const QString& root) {
    return path == root.left(root.size() - (root.endsWith(QLatin1Char('/')) ? 1 : 0)) ||
           path.startsWith(root.endsWith(QLatin1Char('/'))
                               ? root
                               : root + QLatin1Char('/'));
}

bool hasEncodedTraversal(const QString& value) {
    const QString lower = value.toLower();
    return lower.contains(QLatin1String("%2e")) ||
           lower.contains(QLatin1String("%2f")) ||
           lower.contains(QLatin1String("%5c")) ||
           lower.contains(QLatin1String("%25"));
}

bool hasUrlTraversal(const QUrl& url) {
    if (hasEncodedTraversal(url.toString(QUrl::FullyEncoded)))
        return true;
    const QString decodedPath = url.path(QUrl::FullyDecoded);
    if (decodedPath.contains(QLatin1Char('\\')))
        return true;
    const QStringList segments =
        decodedPath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    return std::any_of(segments.cbegin(), segments.cend(), [](const QString& segment) {
        return segment == QLatin1String(".") || segment == QLatin1String("..");
    });
}

bool isWindowsReservedDeviceName(const QString& segment) {
    const QString baseName = segment.section(QLatin1Char('.'), 0, 0);
    static const QRegularExpression expression(QStringLiteral(
        R"(^(?:con|prn|aux|nul|clock\$|conin\$|conout\$|com[1-9]|lpt[1-9])$)"));
    return expression.match(baseName).hasMatch();
}

bool isSafeRelativePath(const QString& path, const QString& suffix,
                        QString* error) {
    if (path.isEmpty() || path.size() > 512 || path.startsWith(QLatin1Char('/')) ||
        path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char('?')) ||
        path.contains(QLatin1Char('#')) || path.contains(QLatin1Char('%')) ||
        path.trimmed() != path ||
        (!suffix.isEmpty() && !path.endsWith(suffix, Qt::CaseSensitive))) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The manifest contains an unsafe relative path."));
        return false;
    }

    const QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& segment : segments) {
        static const QRegularExpression portableSegment(
            QStringLiteral(R"(^[a-z0-9][a-z0-9._-]*$)"));
        if (segment.isEmpty() || segment == QLatin1String(".") ||
            segment == QLatin1String("..") || segment.size() > 128 ||
            segment.trimmed() != segment || segment.contains(QLatin1Char(':'))) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The manifest contains path traversal."));
            return false;
        }
        if (!portableSegment.match(segment).hasMatch() ||
            segment.endsWith(QLatin1Char('.')) ||
            segment.endsWith(QLatin1Char(' ')) ||
            isWindowsReservedDeviceName(segment) ||
            std::any_of(segment.cbegin(), segment.cend(), [](QChar character) {
                return character.unicode() < 0x20 || character.unicode() == 0x7f;
            })) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The manifest contains an unsafe file name."));
            return false;
        }
    }
    return true;
}

bool isValidId(const QString& id) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$)"));
    return expression.match(id).hasMatch() && !isWindowsReservedDeviceName(id);
}

bool isValidVersion(const QString& version) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$)"));
    return version.size() <= 64 && expression.match(version).hasMatch();
}

bool parsePositiveInteger(const QJsonValue& value, qint64 maximum,
                          qint64* output);

bool requiredString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error, int maximum = 256) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Required field “%1” is missing.").arg(key));
        return false;
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.size() > maximum) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Field “%1” is invalid.").arg(key));
        return false;
    }
    *output = text;
    return true;
}

bool optionalString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error, int maximum) {
    if (!object.contains(key)) {
        output->clear();
        return true;
    }
    if (!object.value(key).isString()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Field “%1” has the wrong type.").arg(key));
        return false;
    }
    const QString value = object.value(key).toString().trimmed();
    if (value.size() > maximum) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Field “%1” is too long.").arg(key));
        return false;
    }
    *output = value;
    return true;
}

bool hasSchemaVersionOne(const QJsonObject& object, QString* error) {
    const QJsonValue value = object.value(QStringLiteral("schemaVersion"));
    if (!value.isDouble() || value.toDouble() != 1.0) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Only icon-pack schema version 1 is supported."));
        return false;
    }
    return true;
}

bool parsePlatforms(const QJsonObject& object, QStringList* platforms,
                    QString* error) {
    const QJsonValue value = object.value(QStringLiteral("platforms"));
    if (!value.isArray() || value.toArray().isEmpty() ||
        value.toArray().size() > 4) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The pack has no valid platform list."));
        return false;
    }

    static const QSet<QString> allowed = {
        QStringLiteral("linux"), QStringLiteral("macos"),
        QStringLiteral("windows"), QStringLiteral("cross-platform"),
    };
    QSet<QString> seen;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isString()) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The platform list is invalid."));
            return false;
        }
        const QString platform = item.toString();
        if (!allowed.contains(platform) || seen.contains(platform)) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The platform list contains an invalid value."));
            return false;
        }
        seen.insert(platform);
        platforms->append(platform);
    }
    if (seen.contains(QStringLiteral("cross-platform")) && seen.size() != 1) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Cross-platform cannot be combined with another platform."));
        return false;
    }
    return true;
}

bool parsePackMetadata(const QJsonObject& object, IconPackManager::Pack* pack,
                       QString* error) {
    if (!requiredString(object, QStringLiteral("id"), &pack->id, error, 64) ||
        !isValidId(pack->id)) {
        if (error && error->isEmpty())
            *error = QCoreApplication::translate("IconPackManager", "The pack ID is invalid.");
        return false;
    }
    if (!requiredString(object, QStringLiteral("title"), &pack->title, error) ||
        !requiredString(object, QStringLiteral("version"), &pack->version, error, 64) ||
        !isValidVersion(pack->version) ||
        !requiredString(object, QStringLiteral("author"), &pack->author, error) ||
        !requiredString(object, QStringLiteral("license"), &pack->license, error, 128) ||
        !optionalString(object, QStringLiteral("description"), &pack->description,
                        error, 2048) ||
        !parsePlatforms(object, &pack->platforms, error)) {
        if (error && error->isEmpty())
            *error = QCoreApplication::translate("IconPackManager", "The pack metadata is invalid.");
        return false;
    }
    if (object.contains(QStringLiteral("iconCount"))) {
        qint64 iconCount = 0;
        if (!parsePositiveInteger(object.value(QStringLiteral("iconCount")),
                                  kMaximumIcons, &iconCount)) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The icon count is invalid."));
            return false;
        }
        pack->iconCount = static_cast<int>(iconCount);
    }
    if (object.contains(QStringLiteral("payloadBytes")) &&
        !parsePositiveInteger(object.value(QStringLiteral("payloadBytes")),
                              kMaximumPackSize, &pack->totalBytes)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack size is invalid."));
        return false;
    }
    return true;
}

bool trustedContentUrl(const QUrl& url, const UrlPolicy& policy) {
    return url.isValid() && sameOrigin(url, policy.endpoint) &&
           url.query().isEmpty() && url.fragment().isEmpty() &&
           !hasUrlTraversal(url) &&
           pathIsWithin(url.path(), policy.contentRoot.path());
}

bool trustedIconUrl(const QUrl& url, const UrlPolicy& policy) {
    if (!url.isValid() || url.query().size() || url.fragment().size() ||
        !url.userInfo().isEmpty() || hasUrlTraversal(url))
        return false;
    if (policy.testing)
        return sameOrigin(url, policy.endpoint);
    return url.scheme() == QLatin1String("https") && url.port(-1) == -1 &&
           url.host().compare(QLatin1String(kProductionHost), Qt::CaseInsensitive) == 0 &&
           pathIsWithin(url.path(), QLatin1String(kProductionRepositoryPath));
}

std::optional<QUrl> resolveContentReference(const QString& reference,
                                            const UrlPolicy& policy,
                                            const QString& suffix,
                                            QString* error) {
    if (!isSafeRelativePath(reference, suffix, error))
        return std::nullopt;
    const QUrl resolved = policy.contentRoot.resolved(QUrl(reference));
    if (!trustedContentUrl(resolved, policy)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The catalog URL leaves the icon-packs directory."));
        return std::nullopt;
    }
    return resolved;
}

std::optional<QUrl> resolveIconReference(const QJsonObject& object,
                                         const QString& destination,
                                         const QUrl& manifestUrl,
                                         const UrlPolicy& policy,
                                         QString* error) {
    const bool hasSource = object.contains(QStringLiteral("source"));
    const bool hasUrl = object.contains(QStringLiteral("url"));
    if (hasSource && hasUrl) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "An icon cannot define both source and url."));
        return std::nullopt;
    }

    QString reference = destination;
    if (hasSource || hasUrl) {
        const QJsonValue value = object.value(hasSource ? QStringLiteral("source")
                                                        : QStringLiteral("url"));
        if (!value.isString() || value.toString().isEmpty()) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The icon source URL is invalid."));
            return std::nullopt;
        }
        reference = value.toString();
    }

    const QUrl candidate(reference);
    QUrl resolved;
    if (candidate.isRelative()) {
        if (!isSafeRelativePath(reference, QStringLiteral(".png"), error))
            return std::nullopt;
        resolved = manifestUrl.resolved(candidate);
    } else {
        resolved = candidate;
    }
    if (!trustedIconUrl(resolved, policy) ||
        !resolved.path().endsWith(QLatin1String(".png"), Qt::CaseSensitive)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon source URL is not trusted."));
        return std::nullopt;
    }
    return resolved;
}

bool parsePositiveInteger(const QJsonValue& value, qint64 maximum,
                          qint64* output) {
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 1.0 || number > maximum ||
        std::floor(number) != number)
        return false;
    *output = static_cast<qint64>(number);
    return true;
}

std::optional<ParsedManifest> parseManifest(const QByteArray& payload,
                                            const QUrl& manifestUrl,
                                            const QString& finalDirectory,
                                            const IconPackManager::Pack* expected,
                                            const UrlPolicy& policy,
                                            QString* error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack manifest is not valid JSON."));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    ParsedManifest parsed;
    if (!hasSchemaVersionOne(object, error) ||
        !parsePackMetadata(object, &parsed.pack, error))
        return std::nullopt;
    if (parsed.pack.id == QLatin1String("nightlock-default")) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "A remote pack cannot replace the built-in pack."));
        return std::nullopt;
    }
    if (expected && (parsed.pack.id != expected->id ||
                     parsed.pack.version != expected->version ||
                     parsed.pack.title != expected->title ||
                     parsed.pack.author != expected->author ||
                     parsed.pack.license != expected->license ||
                     parsed.pack.platforms != expected->platforms)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The manifest does not match its catalog entry."));
        return std::nullopt;
    }

    const QJsonValue categoriesValue = object.value(QStringLiteral("categories"));
    if (!categoriesValue.isArray() || categoriesValue.toArray().isEmpty() ||
        categoriesValue.toArray().size() > kMaximumCategories) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The manifest has no valid categories."));
        return std::nullopt;
    }

    QSet<QString> categoryIds;
    QSet<QString> iconIds;
    QSet<QString> files;
    QSet<QString> directories;
    qint64 totalSize = 0;
    int iconCount = 0;
    const QSet<QString> allowedCategories(normalizedCategoryIds().cbegin(),
                                          normalizedCategoryIds().cend());
    for (const QJsonValue& categoryValue : categoriesValue.toArray()) {
        if (!categoryValue.isObject()) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "A category entry is invalid."));
            return std::nullopt;
        }
        const QJsonObject categoryObject = categoryValue.toObject();
        Category category;
        if (!requiredString(categoryObject, QStringLiteral("id"),
                            &category.id, error, 64) ||
            !allowedCategories.contains(category.id) ||
            categoryIds.contains(category.id)) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "A category ID is unknown or duplicated."));
            return std::nullopt;
        }
        categoryIds.insert(category.id);
        category.title = canonicalCategoryTitle(category.id);

        // The optional title is validated for schema hygiene, but clients see
        // the canonical localized title so every platform uses one taxonomy.
        QString ignoredTitle;
        if (!optionalString(categoryObject, QStringLiteral("title"),
                            &ignoredTitle, error, 256))
            return std::nullopt;

        const QJsonValue iconsValue = categoryObject.value(QStringLiteral("icons"));
        if (!iconsValue.isArray() || iconsValue.toArray().isEmpty()) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "A category has no icons."));
            return std::nullopt;
        }

        const int categoryIndex = parsed.pack.categories.size();
        for (const QJsonValue& iconValue : iconsValue.toArray()) {
            if (!iconValue.isObject() || ++iconCount > kMaximumIcons) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "The manifest contains too many or invalid icons."));
                return std::nullopt;
            }
            const QJsonObject iconObject = iconValue.toObject();
            ParsedIcon parsedIcon;
            if (!requiredString(iconObject, QStringLiteral("id"),
                                &parsedIcon.icon.id, error, 64) ||
                !isValidId(parsedIcon.icon.id) || iconIds.contains(parsedIcon.icon.id) ||
                !requiredString(iconObject, QStringLiteral("title"),
                                &parsedIcon.icon.title, error)) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "An icon ID is invalid or duplicated."));
                return std::nullopt;
            }
            iconIds.insert(parsedIcon.icon.id);

            const QJsonValue destinationValue =
                iconObject.value(QStringLiteral("file"));
            if (!requiredString(iconObject, QStringLiteral("file"),
                                &parsedIcon.destination, error, 512) ||
                destinationValue.toString() != parsedIcon.destination ||
                !isSafeRelativePath(parsedIcon.destination,
                                    QStringLiteral(".png"), error)) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "An icon file path is invalid or duplicated."));
                return std::nullopt;
            }
            const QString portablePath = parsedIcon.destination.normalized(
                QString::NormalizationForm_C).toCaseFolded();
            const QStringList destinationSegments =
                portablePath.split(QLatin1Char('/'));
            QString ancestor;
            for (int segmentIndex = 0;
                 segmentIndex + 1 < destinationSegments.size(); ++segmentIndex) {
                if (!ancestor.isEmpty())
                    ancestor += QLatin1Char('/');
                ancestor += destinationSegments.at(segmentIndex);
                if (files.contains(ancestor)) {
                    setError(error, QCoreApplication::translate(
                                        "IconPackManager", "An icon file path is invalid or duplicated."));
                    return std::nullopt;
                }
                directories.insert(ancestor);
            }
            if (files.contains(portablePath) || directories.contains(portablePath)) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "An icon file path is invalid or duplicated."));
                return std::nullopt;
            }
            files.insert(portablePath);

            QString hash;
            if (!requiredString(iconObject, QStringLiteral("sha256"),
                                &hash, error, 64))
                return std::nullopt;
            static const QRegularExpression hashExpression(
                QStringLiteral(R"(^[0-9A-Fa-f]{64}$)"));
            if (!hashExpression.match(hash).hasMatch()) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "An icon SHA-256 digest is invalid."));
                return std::nullopt;
            }
            parsedIcon.icon.sha256 = hash.toLatin1().toLower();
            if (!parsePositiveInteger(iconObject.value(QStringLiteral("size")),
                                      kMaximumIconSize, &parsedIcon.icon.size) ||
                totalSize > kMaximumPackSize - parsedIcon.icon.size) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "An icon or pack exceeds the size limit."));
                return std::nullopt;
            }
            totalSize += parsedIcon.icon.size;

            const std::optional<QUrl> source = resolveIconReference(
                iconObject, parsedIcon.destination, manifestUrl, policy, error);
            if (!source)
                return std::nullopt;
            parsedIcon.sourceUrl = *source;
            parsedIcon.categoryIndex = categoryIndex;
            parsedIcon.iconIndex = category.icons.size();
            parsedIcon.icon.filePath =
                QDir(finalDirectory).filePath(parsedIcon.destination);
            category.icons.append(parsedIcon.icon);
            parsed.icons.append(std::move(parsedIcon));
        }
        parsed.pack.categories.append(std::move(category));
    }

    parsed.pack.totalBytes = totalSize;
    parsed.pack.iconCount = iconCount;
    if (error)
        error->clear();
    return parsed;
}

bool validatePng(const QByteArray& payload, const Icon& icon,
                 QSize* dimensions, QString* error) {
    static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
    if (payload.size() != icon.size || !payload.startsWith(signature)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The downloaded icon has the wrong size or format."));
        return false;
    }
    const QByteArray actualHash =
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    if (actualHash != icon.sha256) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The downloaded icon failed SHA-256 verification."));
        return false;
    }

    QBuffer buffer;
    buffer.setData(payload);
    if (!buffer.open(QIODevice::ReadOnly)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The downloaded icon could not be inspected."));
        return false;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    if (reader.format().toLower() != QByteArrayLiteral("png") || !reader.canRead()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The downloaded file is not a decodable PNG."));
        return false;
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > kMaximumImageExtent ||
        size.height() > kMaximumImageExtent ||
        static_cast<qint64>(size.width()) * size.height() > kMaximumImagePixels) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The PNG dimensions exceed the safety limit."));
        return false;
    }
    const QImage image = reader.read();
    if (image.isNull() || image.size() != size) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The PNG could not be decoded."));
        return false;
    }
    *dimensions = size;
    return true;
}

bool inspectInstalledPng(const QString& packDirectory,
                         const ParsedIcon& parsedIcon, QSize* dimensions,
                         QString* error) {
    QString currentPath = packDirectory;
    const QStringList segments =
        parsedIcon.destination.split(QLatin1Char('/'));
    for (int index = 0; index < segments.size(); ++index) {
        currentPath = QDir(currentPath).filePath(segments.at(index));
        const QFileInfo component(currentPath);
        if (isLinkLike(component) ||
            (index + 1 < segments.size() && !component.isDir())) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "An installed icon path is unsafe."));
            return false;
        }
    }

    const QFileInfo info(currentPath);
    if (!info.isFile() || isLinkLike(info) ||
        info.size() != parsedIcon.icon.size) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "An installed icon is missing or has the wrong size."));
        return false;
    }

    QFile file(currentPath);
    static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
    if (!file.open(QIODevice::ReadOnly) || file.read(signature.size()) != signature ||
        !file.seek(0)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "An installed icon is not a PNG file."));
        return false;
    }

    // Installation already verified the complete payload and SHA-256 before
    // atomically activating the pack. Startup only performs bounded metadata
    // checks so thousands of icons are not synchronously read and decoded.
    QImageReader reader(&file);
    reader.setDecideFormatFromContent(true);
    if (reader.format().toLower() != QByteArrayLiteral("png") ||
        !reader.canRead()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "An installed icon is not a readable PNG file."));
        return false;
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > kMaximumImageExtent ||
        size.height() > kMaximumImageExtent ||
        static_cast<qint64>(size.width()) * size.height() > kMaximumImagePixels) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "An installed PNG exceeds the safety limit."));
        return false;
    }
    *dimensions = size;
    return true;
}

IconPackManager::Pack builtInPack() {
    IconPackManager::Pack pack;
    pack.id = QStringLiteral("nightlock-default");
    pack.title = QCoreApplication::translate("IconPackManager", "Nightlock Default");
    pack.description = QCoreApplication::translate(
        "IconPackManager", "The essential icons included with Nightlock.");
#ifdef NIGHTLOCK_VERSION
    pack.version = QString::fromLatin1(NIGHTLOCK_VERSION);
#else
    pack.version = QStringLiteral("1.0.0");
#endif
    pack.author = QStringLiteral("Nightlock");
    pack.license = QStringLiteral("Nightlock");
    pack.platforms = {QStringLiteral("cross-platform")};
    pack.state = IconPackManager::State::BuiltIn;
    pack.iconCount = 3;

    const auto addIcon = [&pack](const QString& categoryId, const QString& id,
                                 const QString& title, const QString& resource) {
        Category category;
        category.id = categoryId;
        category.title = canonicalCategoryTitle(categoryId);
        Icon icon;
        icon.id = id;
        icon.title = title;
        icon.filePath = resource;
        QFile file(resource);
        if (file.open(QIODevice::ReadOnly))
            icon.size = file.size();
        QImageReader reader(resource);
        icon.dimensions = reader.size();
        category.icons.append(std::move(icon));
        pack.categories.append(std::move(category));
    };
    addIcon(QStringLiteral("applications"), QStringLiteral("entry"),
            QCoreApplication::translate("IconPackManager", "Entry"),
            QStringLiteral(":/icons/entry.png"));
    addIcon(QStringLiteral("folders-places"), QStringLiteral("folder"),
            QCoreApplication::translate("IconPackManager", "Folder"),
            QStringLiteral(":/icons/folder.png"));
    addIcon(QStringLiteral("alerts-badges"), QStringLiteral("lock"),
            QCoreApplication::translate("IconPackManager", "Lock"),
            QStringLiteral(":/icons/lock.png"));
    return pack;
}

Transfer beginGet(QNetworkAccessManager* network, const QUrl& url,
                  int deadlineMs, qsizetype maximumSize,
                  const QByteArray& accept) {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", accept);
    request.setRawHeader("User-Agent", QByteArrayLiteral("Nightlock-Icon-Library/1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(deadlineMs);

    Transfer transfer;
    transfer.payload = std::make_shared<QByteArray>();
    transfer.reply = network->get(request);
    auto* deadline = new QTimer(transfer.reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, transfer.reply,
                     [reply = transfer.reply] {
                         reply->setProperty(kDeadlineExceededProperty, true);
                         reply->abort();
                     });
    QObject::connect(transfer.reply, &QNetworkReply::finished,
                     deadline, &QTimer::stop);
    deadline->start(deadlineMs);

    QObject::connect(transfer.reply, &QIODevice::readyRead, transfer.reply,
                     [reply = transfer.reply, payload = transfer.payload,
                      maximumSize] {
                         payload->append(reply->readAll());
                         if (payload->size() <= maximumSize)
                             return;
                         reply->setProperty(kPayloadTooLargeProperty, true);
                         reply->abort();
                     });
    QObject::connect(transfer.reply, &QNetworkReply::downloadProgress,
                     transfer.reply,
                     [reply = transfer.reply, maximumSize](qint64, qint64 total) {
                         if (total <= maximumSize || total < 0)
                             return;
                         reply->setProperty(kPayloadTooLargeProperty, true);
                         reply->abort();
                     });
    return transfer;
}

bool takeSuccessfulResponse(QNetworkReply* reply, QByteArray* payload,
                            qsizetype maximumSize, QString* error) {
    payload->append(reply->readAll());
    const bool tooLarge = reply->property(kPayloadTooLargeProperty).toBool() ||
                          payload->size() > maximumSize;
    const bool deadlineExceeded =
        reply->property(kDeadlineExceededProperty).toBool();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (tooLarge) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The download exceeds the safety limit."));
        return false;
    }
    if (deadlineExceeded) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack request timed out."));
        return false;
    }
    if (networkError != QNetworkReply::NoError || status != 200) {
        setError(error,
                 status > 0
                     ? QCoreApplication::translate(
                           "IconPackManager", "The server returned HTTP %1: %2")
                           .arg(status)
                           .arg(networkErrorText)
                     : QCoreApplication::translate(
                           "IconPackManager", "The server could not be reached: %1")
                           .arg(networkErrorText));
        return false;
    }
    return true;
}

bool writeAtomicallyInside(const QString& root, const QString& relativePath,
                           const QByteArray& payload, QString* error) {
    if (!isSafeRelativePath(relativePath, {}, error) ||
        !ensureOrdinaryDirectory(root, error))
        return false;

    const QStringList segments = relativePath.split(QLatin1Char('/'));
    QString parent = root;
    for (int index = 0; index + 1 < segments.size(); ++index) {
        const QString child = QDir(parent).filePath(segments.at(index));
        const QFileInfo before(child);
        if (before.exists() || isLinkLike(before)) {
            if (!before.isDir() || isLinkLike(before)) {
                setError(error, QCoreApplication::translate(
                                    "IconPackManager", "The icon-pack path is unsafe."));
                return false;
            }
        } else if (!QDir(parent).mkdir(segments.at(index))) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "Could not create the icon directory."));
            return false;
        }
        const QFileInfo after(child);
        if (!after.isDir() || isLinkLike(after)) {
            setError(error, QCoreApplication::translate(
                                "IconPackManager", "The icon-pack path is unsafe."));
            return false;
        }
        parent = child;
    }

    const QString path = QDir(parent).filePath(segments.constLast());
    const QFileInfo destination(path);
    if (destination.exists() || isLinkLike(destination)) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "The icon-pack destination is unsafe."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() ||
        !file.commit()) {
        setError(error, QCoreApplication::translate(
                            "IconPackManager", "Could not save the downloaded icon pack."));
        return false;
    }
    return true;
}

bool isLoopbackEndpoint(const QUrl& endpoint) {
    if (!endpoint.isValid() || endpoint.userInfo().size() ||
        endpoint.query().size() || endpoint.fragment().size() ||
        (endpoint.scheme() != QLatin1String("http") &&
         endpoint.scheme() != QLatin1String("https")))
        return false;
    if (endpoint.host().compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(endpoint.host());
    return !address.isNull() && address.isLoopback();
}

#ifdef NIGHTLOCK_ICON_PACK_SOURCE_DIR
QByteArray readOrdinaryFile(const QString& path, qsizetype maximumSize,
                            bool* valid) {
    *valid = false;
    const QFileInfo info(path);
    if (!info.isFile() || isLinkLike(info) || info.size() <= 0 ||
        info.size() > maximumSize)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray payload = file.read(maximumSize + 1);
    if (payload.size() != info.size())
        return {};
    *valid = true;
    return payload;
}

QVector<IconPackManager::Pack> sourceTreePreviewPacks(
    const QString& sourceRoot) {
    QVector<IconPackManager::Pack> result;
    const QFileInfo rootInfo(sourceRoot);
    if (!rootInfo.isDir() || isLinkLike(rootInfo))
        return result;

    const QString catalogPath =
        QDir(sourceRoot).filePath(QStringLiteral("catalog.json"));
    bool catalogValid = false;
    const QByteArray catalogPayload = readOrdinaryFile(
        catalogPath, kMaximumCatalogSize, &catalogValid);
    if (!catalogValid)
        return {};

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(catalogPayload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const QJsonObject catalog = document.object();
    QString error;
    if (!hasSchemaVersionOne(catalog, &error) ||
        !catalog.value(QStringLiteral("packs")).isArray() ||
        catalog.value(QStringLiteral("packs")).toArray().size() > kMaximumPacks)
        return {};

    UrlPolicy localPolicy;
    localPolicy.endpoint = QUrl::fromLocalFile(catalogPath);
    localPolicy.contentRoot =
        localPolicy.endpoint.adjusted(QUrl::RemoveFilename);
    localPolicy.testing = true;
    QSet<QString> ids;
    for (const QJsonValue& value :
         catalog.value(QStringLiteral("packs")).toArray()) {
        QString packLabel = QStringLiteral("<unknown>");
        error.clear();
        const std::optional<IconPackManager::Pack> loaded = [&]()
            -> std::optional<IconPackManager::Pack> {
            if (!value.isObject()) {
                error = QCoreApplication::translate(
                    "IconPackManager", "The catalog entry is not an object.");
                return std::nullopt;
            }
            const QJsonObject entry = value.toObject();
            packLabel = entry.value(QStringLiteral("id")).toString(packLabel);
            IconPackManager::Pack expected;
            if (!parsePackMetadata(entry, &expected, &error) ||
                expected.id == QLatin1String("nightlock-default") ||
                ids.contains(expected.id)) {
                if (error.isEmpty())
                    error = QCoreApplication::translate(
                        "IconPackManager", "The pack ID is duplicated or reserved.");
                return std::nullopt;
            }
            ids.insert(expected.id);

            QString manifestReference;
            if (!requiredString(entry, QStringLiteral("manifest"),
                                &manifestReference, &error, 512))
                return std::nullopt;
            const std::optional<QUrl> manifestUrl = resolveContentReference(
                manifestReference, localPolicy, QStringLiteral(".json"), &error);
            if (!manifestUrl || !manifestUrl->isLocalFile()) {
                if (error.isEmpty())
                    error = QCoreApplication::translate(
                        "IconPackManager", "The local manifest path is invalid.");
                return std::nullopt;
            }

            const QString manifestPath = manifestUrl->toLocalFile();
            bool manifestValid = false;
            const QByteArray manifestPayload = readOrdinaryFile(
                manifestPath, kMaximumManifestSize, &manifestValid);
            if (!manifestValid) {
                error = QCoreApplication::translate(
                    "IconPackManager", "The local manifest could not be read.");
                return std::nullopt;
            }
            const QString packDirectory = QFileInfo(manifestPath).absolutePath();
            std::optional<ParsedManifest> parsed = parseManifest(
                manifestPayload, *manifestUrl, packDirectory, &expected,
                localPolicy, &error);
            if (!parsed)
                return std::nullopt;

            for (const ParsedIcon& parsedIcon : std::as_const(parsed->icons)) {
                QSize dimensions;
                if (!inspectInstalledPng(packDirectory, parsedIcon, &dimensions,
                                         &error) ||
                    !parsedIcon.sourceUrl.isLocalFile()) {
                    if (error.isEmpty())
                        error = QCoreApplication::translate(
                            "IconPackManager", "The local PNG path is invalid.");
                    return std::nullopt;
                }
                const QString sourcePath = QFileInfo(
                    parsedIcon.sourceUrl.toLocalFile()).absoluteFilePath();
                if (!pathIsWithin(sourcePath,
                                  QDir(packDirectory).absolutePath())) {
                    error = QCoreApplication::translate(
                        "IconPackManager", "The local PNG leaves its pack directory.");
                    return std::nullopt;
                }
                bool iconValid = false;
                const QByteArray iconPayload = readOrdinaryFile(
                    sourcePath, kMaximumIconSize, &iconValid);
                if (!iconValid || !validatePng(iconPayload, parsedIcon.icon,
                                               &dimensions, &error)) {
                    if (error.isEmpty())
                        error = QCoreApplication::translate(
                            "IconPackManager", "The local PNG could not be read.");
                    return std::nullopt;
                }
                parsed->pack.categories[parsedIcon.categoryIndex]
                    .icons[parsedIcon.iconIndex]
                    .dimensions = dimensions;
            }
            parsed->pack.state = IconPackManager::State::Preview;
            parsed->pack.receivedBytes = parsed->pack.totalBytes;
            return std::move(parsed->pack);
        }();
        if (!loaded) {
            qWarning().noquote()
                << "Nightlock skipped source-tree icon pack" << packLabel
                << ':' << error;
            continue;
        }
        result.append(*loaded);
    }
    return result;
}
#endif

}  // namespace

struct IconPackManager::Private {
    struct RemotePack {
        Pack pack;
        QUrl manifestUrl;
    };

    struct InstallJob {
        struct ActiveIcon {
            int iconIndex = -1;
            qint64 receivedBytes = 0;
        };

        RemotePack remote;
        Pack parsedPack;
        QVector<ParsedIcon> icons;
        QByteArray manifestPayload;
        QString stagingPath;
        int nextIcon = 0;
        int completedIcons = 0;
        qint64 completedBytes = 0;
        qint64 reportedBytes = 0;
        quint64 token = 0;
        QHash<QNetworkReply*, ActiveIcon> activeIcons;
    };

    QNetworkAccessManager* network = nullptr;
    QUrl endpoint;
    UrlPolicy policy;
    QString storageRoot;
    int requestDeadlineMs = kRequestDeadlineMs;
    bool refreshing = false;
    bool busyRefreshError = false;
    QVector<Pack> packs;
    QVector<RemotePack> remotePacks;
    std::unique_ptr<InstallJob> installJob;
    quint64 nextInstallToken = 0;
    QString catalogError;
};

const QStringList& normalizedCategoryIds() {
    static const QStringList ids = {
        QStringLiteral("applications"),
        QStringLiteral("system-applications"),
        QStringLiteral("actions"),
        QStringLiteral("folders-places"),
        QStringLiteral("devices-volumes"),
        QStringLiteral("documents-mimetypes"),
        QStringLiteral("network-sharing"),
        QStringLiteral("users-accounts"),
        QStringLiteral("settings-categories"),
        QStringLiteral("status-menu"),
        QStringLiteral("sidebar-toolbar"),
        QStringLiteral("alerts-badges"),
        QStringLiteral("emblems"),
        QStringLiteral("animations"),
        QStringLiteral("legacy-special"),
        QStringLiteral("ui-symbols"),
    };
    return ids;
}

QString canonicalCategoryTitle(const QString& id) {
    if (id == QLatin1String("applications"))
        return QCoreApplication::translate("IconPackCategories", "Applications");
    if (id == QLatin1String("system-applications"))
        return QCoreApplication::translate("IconPackCategories", "System Applications");
    if (id == QLatin1String("actions"))
        return QCoreApplication::translate("IconPackCategories", "Actions");
    if (id == QLatin1String("folders-places"))
        return QCoreApplication::translate("IconPackCategories", "Folders & Places");
    if (id == QLatin1String("devices-volumes"))
        return QCoreApplication::translate("IconPackCategories", "Devices & Volumes");
    if (id == QLatin1String("documents-mimetypes"))
        return QCoreApplication::translate("IconPackCategories", "Documents & MIME Types");
    if (id == QLatin1String("network-sharing"))
        return QCoreApplication::translate("IconPackCategories", "Network & Sharing");
    if (id == QLatin1String("users-accounts"))
        return QCoreApplication::translate("IconPackCategories", "Users & Accounts");
    if (id == QLatin1String("settings-categories"))
        return QCoreApplication::translate("IconPackCategories", "Settings & Categories");
    if (id == QLatin1String("status-menu"))
        return QCoreApplication::translate("IconPackCategories", "Status & Menu Bar");
    if (id == QLatin1String("sidebar-toolbar"))
        return QCoreApplication::translate("IconPackCategories", "Sidebar & Toolbar");
    if (id == QLatin1String("alerts-badges"))
        return QCoreApplication::translate("IconPackCategories", "Alerts & Badges");
    if (id == QLatin1String("emblems"))
        return QCoreApplication::translate("IconPackCategories", "Emblems");
    if (id == QLatin1String("animations"))
        return QCoreApplication::translate("IconPackCategories", "Animations");
    if (id == QLatin1String("legacy-special"))
        return QCoreApplication::translate("IconPackCategories", "Legacy / Special");
    if (id == QLatin1String("ui-symbols"))
        return QCoreApplication::translate("IconPackCategories", "UI Symbols");
    return {};
}

IconPackManager::IconPackManager(QObject* parent)
    : IconPackManager(QUrl(QLatin1String(kCatalogEndpoint)),
                      QStandardPaths::writableLocation(
                          QStandardPaths::AppLocalDataLocation),
                      kRequestDeadlineMs, false, parent) {}

IconPackManager::IconPackManager(const QUrl& endpoint, const QString& dataDir,
                                 int requestDeadlineMs, bool testing,
                                 QObject* parent)
    : QObject(parent), d_(std::make_unique<Private>()) {
    d_->network = new QNetworkAccessManager(this);
    d_->endpoint = endpoint;
    d_->policy.endpoint = endpoint;
    d_->policy.contentRoot = endpoint.adjusted(QUrl::RemoveFilename);
    d_->policy.testing = testing;
    d_->storageRoot = QDir(dataDir).filePath(QLatin1String(kStorageDirectory));
    d_->requestDeadlineMs = requestDeadlineMs;
    Q_ASSERT(endpoint.isValid());
    Q_ASSERT(requestDeadlineMs > 0);
    Q_ASSERT(!testing || isLoopbackEndpoint(endpoint));

    d_->packs.append(builtInPack());
    loadInstalledPacks();
#ifdef NIGHTLOCK_ICON_PACK_SOURCE_DIR
    if (qEnvironmentVariableIsSet("NIGHTLOCK_DEMO")) {
        const QVector<Pack> previews = sourceTreePreviewPacks(
            QString::fromUtf8(NIGHTLOCK_ICON_PACK_SOURCE_DIR));
        for (const Pack& preview : previews) {
            const bool alreadyInstalled = std::any_of(
                d_->packs.cbegin(), d_->packs.cend(),
                [&preview](const Pack& existing) {
                    return existing.id == preview.id;
                });
            if (!alreadyInstalled)
                d_->packs.append(preview);
        }
    }
#endif
}

IconPackManager::~IconPackManager() {
    if (d_ && d_->installJob && !d_->installJob->stagingPath.isEmpty())
        removeOrdinaryDirectoryTree(d_->installJob->stagingPath);
}

IconPackManager* IconPackManager::instance() {
    static auto* manager = new IconPackManager(QCoreApplication::instance());
    return manager;
}

#ifdef NIGHTLOCK_ICON_PACK_TESTING
IconPackManager* IconPackManager::createForTesting(const QUrl& endpoint,
                                                   const QString& dataDir,
                                                   int requestDeadlineMs,
                                                   QObject* parent) {
    if (!isLoopbackEndpoint(endpoint) || dataDir.isEmpty() || requestDeadlineMs <= 0)
        return nullptr;
    return new IconPackManager(endpoint, dataDir, requestDeadlineMs, true, parent);
}
#endif

QVector<IconPackManager::Pack> IconPackManager::packs() const {
    return d_->packs;
}

std::optional<IconPackManager::Pack> IconPackManager::pack(const QString& id) const {
    for (const Pack& item : d_->packs) {
        if (item.id == id)
            return item;
    }
    return std::nullopt;
}

QVector<IconPackManager::Pack> IconPackManager::installedPacks() const {
    QVector<Pack> result;
    for (const Pack& item : d_->packs) {
        if (item.state == State::BuiltIn || item.state == State::Installed ||
            item.state == State::Preview)
            result.append(item);
    }
    return result;
}

bool IconPackManager::isRefreshing() const {
    return d_->refreshing;
}

QString IconPackManager::catalogError() const {
    return d_->catalogError;
}

void IconPackManager::setCatalogError(const QString& error) {
    if (d_->catalogError == error)
        return;
    d_->catalogError = error;
    emit catalogErrorChanged(error);
}

void IconPackManager::refreshCatalog() {
    if (d_->refreshing)
        return;
    if (d_->installJob) {
        d_->busyRefreshError = true;
        setCatalogError(tr("Wait for the current icon-pack download to finish."));
        return;
    }
    d_->refreshing = true;
    d_->busyRefreshError = false;
    setCatalogError({});
    emit refreshingChanged(true);

    Transfer transfer = beginGet(d_->network, d_->endpoint,
                                 d_->requestDeadlineMs, kMaximumCatalogSize,
                                 QByteArrayLiteral("application/json"));
    connect(transfer.reply, &QNetworkReply::finished, this,
            [this, reply = transfer.reply, payload = transfer.payload] {
                finishCatalogRequest(reply, std::move(*payload));
            });
}

void IconPackManager::finishCatalogRequest(QNetworkReply* reply,
                                           QByteArray payload) {
    const QUrl finalUrl = reply->url();
    QString error;
    const bool successful = takeSuccessfulResponse(
        reply, &payload, kMaximumCatalogSize, &error);
    d_->refreshing = false;
    emit refreshingChanged(false);
    if (!successful || !trustedContentUrl(finalUrl, d_->policy)) {
        if (successful)
            error = tr("The catalog was redirected outside the icon-packs directory.");
        setCatalogError(error);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setCatalogError(tr("The icon-pack catalog is not valid JSON."));
        return;
    }
    const QJsonObject object = document.object();
    if (!hasSchemaVersionOne(object, &error) ||
        !object.value(QStringLiteral("packs")).isArray() ||
        object.value(QStringLiteral("packs")).toArray().size() > kMaximumPacks) {
        setCatalogError(error.isEmpty() ? tr("The icon-pack catalog is invalid.") : error);
        return;
    }

    QVector<Private::RemotePack> remotes;
    QSet<QString> ids;
    for (const QJsonValue& value : object.value(QStringLiteral("packs")).toArray()) {
        if (!value.isObject()) {
            setCatalogError(tr("The icon-pack catalog contains an invalid entry."));
            return;
        }
        const QJsonObject entry = value.toObject();
        Private::RemotePack remote;
        if (!parsePackMetadata(entry, &remote.pack, &error) ||
            remote.pack.id == QLatin1String("nightlock-default") ||
            ids.contains(remote.pack.id)) {
            setCatalogError(error.isEmpty()
                                ? tr("The catalog contains a duplicate or reserved pack ID.")
                                : error);
            return;
        }
        ids.insert(remote.pack.id);
        QString manifest;
        if (!requiredString(entry, QStringLiteral("manifest"), &manifest,
                            &error, 512)) {
            setCatalogError(error);
            return;
        }
        const std::optional<QUrl> manifestUrl = resolveContentReference(
            manifest, d_->policy, QStringLiteral(".json"), &error);
        if (!manifestUrl) {
            setCatalogError(error);
            return;
        }
        remote.manifestUrl = *manifestUrl;
        remote.pack.state = State::Available;
        remotes.append(std::move(remote));
    }

    QHash<QString, Pack> installed;
    for (const Pack& item : std::as_const(d_->packs)) {
        if (item.state == State::Installed || item.state == State::Preview)
            installed.insert(item.id, item);
    }

    QVector<Pack> merged;
    merged.append(builtInPack());
    for (const Private::RemotePack& remote : std::as_const(remotes)) {
        const auto installedIt = installed.find(remote.pack.id);
        if (installedIt != installed.end()) {
            merged.append(installedIt.value());
            installed.erase(installedIt);
        } else {
            merged.append(remote.pack);
        }
    }
    QStringList orphanIds = installed.keys();
    std::sort(orphanIds.begin(), orphanIds.end());
    for (const QString& id : std::as_const(orphanIds))
        merged.append(installed.value(id));

    d_->remotePacks = std::move(remotes);
    d_->packs = std::move(merged);
    setCatalogError({});
    emit catalogChanged();
}

void IconPackManager::install(const QString& id) {
    if (d_->refreshing || d_->installJob)
        return;
    auto remoteIt = std::find_if(
        d_->remotePacks.begin(), d_->remotePacks.end(),
        [&id](const Private::RemotePack& remote) { return remote.pack.id == id; });
    if (remoteIt == d_->remotePacks.end())
        return;
    const std::optional<Pack> current = pack(id);
    if (current && (current->state == State::Installed ||
                    current->state == State::BuiltIn ||
                    current->state == State::Preview))
        return;

    d_->installJob = std::make_unique<Private::InstallJob>();
    d_->installJob->remote = *remoteIt;
    d_->installJob->token = ++d_->nextInstallToken;
    Pack downloading = remoteIt->pack;
    downloading.state = State::Downloading;
    replaceOrAppendPack(std::move(downloading));
    emit packChanged(id);
    emit progressChanged(id, 0, 0);

    Transfer transfer = beginGet(d_->network, remoteIt->manifestUrl,
                                 d_->requestDeadlineMs, kMaximumManifestSize,
                                 QByteArrayLiteral("application/json"));
    connect(transfer.reply, &QNetworkReply::finished, this,
            [this, reply = transfer.reply, payload = transfer.payload] {
                finishManifestRequest(reply, std::move(*payload));
            });
}

void IconPackManager::finishManifestRequest(QNetworkReply* reply,
                                            QByteArray payload) {
    if (!d_->installJob) {
        reply->deleteLater();
        return;
    }
    const QUrl finalUrl = reply->url();
    QString error;
    if (!takeSuccessfulResponse(reply, &payload, kMaximumManifestSize, &error)) {
        failInstall(error);
        return;
    }
    if (!trustedContentUrl(finalUrl, d_->policy)) {
        failInstall(tr("The manifest was redirected outside the icon-packs directory."));
        return;
    }

    const QString finalDirectory =
        QDir(d_->storageRoot).filePath(d_->installJob->remote.pack.id);
    const std::optional<ParsedManifest> parsed = parseManifest(
        payload, finalUrl, finalDirectory,
        &d_->installJob->remote.pack, d_->policy, &error);
    if (!parsed) {
        failInstall(error);
        return;
    }
    if (!ensureOrdinaryDirectory(d_->storageRoot, &error)) {
        failInstall(tr("Could not create the icon-pack storage directory."));
        return;
    }
    const QFileInfo staleInfo(finalDirectory);
    if (staleInfo.exists() || isLinkLike(staleInfo)) {
        if (staleInfo.isDir() && !isLinkLike(staleInfo))
            removeOrdinaryDirectoryTree(finalDirectory);
        else if (staleInfo.isFile() && !isLinkLike(staleInfo))
            QFile::remove(finalDirectory);
        const QFileInfo remaining(finalDirectory);
        if (remaining.exists() || isLinkLike(remaining)) {
            failInstall(tr("The icon-pack destination already exists."));
            return;
        }
    }

    const QString stagingName =
        QStringLiteral(".%1.install-%2")
            .arg(d_->installJob->remote.pack.id,
                 QUuid::createUuid().toString(QUuid::WithoutBraces));
    d_->installJob->stagingPath = QDir(d_->storageRoot).filePath(stagingName);
    if (!QDir(d_->storageRoot).mkdir(stagingName) ||
        !ensureOrdinaryDirectory(d_->installJob->stagingPath, &error) ||
        !writeAtomicallyInside(d_->installJob->stagingPath,
                               QLatin1String(kManifestFile), payload, &error)) {
        failInstall(error.isEmpty() ? tr("Could not stage the icon pack.") : error);
        return;
    }

    d_->installJob->parsedPack = parsed->pack;
    d_->installJob->icons = parsed->icons;
    d_->installJob->manifestPayload = std::move(payload);
    Pack downloading = d_->installJob->parsedPack;
    downloading.state = State::Downloading;
    replaceOrAppendPack(std::move(downloading));
    emit packChanged(d_->installJob->remote.pack.id);
    emit progressChanged(d_->installJob->remote.pack.id, 0,
                         d_->installJob->parsedPack.totalBytes);
    pumpIconDownloads();
}

void IconPackManager::pumpIconDownloads() {
    if (!d_->installJob)
        return;

    const quint64 token = d_->installJob->token;
    while (d_->installJob && d_->installJob->token == token &&
           d_->installJob->activeIcons.size() <
               kMaximumConcurrentIconDownloads &&
           d_->installJob->nextIcon < d_->installJob->icons.size()) {
        const int iconIndex = d_->installJob->nextIcon++;
        const ParsedIcon& icon = d_->installJob->icons.at(iconIndex);
        Transfer transfer = beginGet(d_->network, icon.sourceUrl,
                                     d_->requestDeadlineMs, icon.icon.size,
                                     QByteArrayLiteral("image/png"));
        d_->installJob->activeIcons.insert(
            transfer.reply,
            Private::InstallJob::ActiveIcon{iconIndex, 0});
        connect(transfer.reply, &QNetworkReply::downloadProgress, this,
                [this, reply = transfer.reply, token,
                 expected = icon.icon.size](qint64 received, qint64) {
                    if (!d_->installJob ||
                        d_->installJob->token != token)
                        return;
                    auto active = d_->installJob->activeIcons.find(reply);
                    if (active == d_->installJob->activeIcons.end())
                        return;
                    const qint64 bounded =
                        qBound<qint64>(0, received, expected);
                    active->receivedBytes =
                        qMax(active->receivedBytes, bounded);
                    updateInstallProgress(token);
                });
        connect(transfer.reply, &QNetworkReply::finished, this,
                [this, reply = transfer.reply, payload = transfer.payload,
                 token, iconIndex] {
                    finishIconRequest(reply, std::move(*payload), token,
                                      iconIndex);
                });
    }

    if (d_->installJob && d_->installJob->token == token &&
        d_->installJob->completedIcons == d_->installJob->icons.size() &&
        d_->installJob->activeIcons.isEmpty()) {
        finishInstall();
    }
}

void IconPackManager::updateInstallProgress(quint64 token) {
    if (!d_->installJob || d_->installJob->token != token)
        return;

    qint64 received = d_->installJob->completedBytes;
    for (const Private::InstallJob::ActiveIcon& active :
         std::as_const(d_->installJob->activeIcons)) {
        received += active.receivedBytes;
    }
    const qint64 total = d_->installJob->parsedPack.totalBytes;
    received = qMin(received, total);
    received = qMax(received, d_->installJob->reportedBytes);
    d_->installJob->reportedBytes = received;
    const QString id = d_->installJob->remote.pack.id;
    for (Pack& item : d_->packs) {
        if (item.id == id) {
            item.receivedBytes = received;
            item.totalBytes = total;
            break;
        }
    }
    emit progressChanged(id, received, total);
}

void IconPackManager::finishIconRequest(QNetworkReply* reply,
                                        QByteArray payload, quint64 token,
                                        int iconIndex) {
    if (!d_->installJob || d_->installJob->token != token) {
        reply->deleteLater();
        return;
    }
    const auto active = d_->installJob->activeIcons.find(reply);
    if (active == d_->installJob->activeIcons.end() ||
        active->iconIndex != iconIndex || iconIndex < 0 ||
        iconIndex >= d_->installJob->icons.size()) {
        reply->deleteLater();
        return;
    }
    d_->installJob->activeIcons.erase(active);
    const ParsedIcon icon = d_->installJob->icons.at(iconIndex);
    const QUrl finalUrl = reply->url();
    QString error;
    if (!takeSuccessfulResponse(reply, &payload, icon.icon.size, &error)) {
        failInstall(error);
        return;
    }
    if (!trustedIconUrl(finalUrl, d_->policy)) {
        failInstall(tr("The icon download was redirected to an untrusted URL."));
        return;
    }
    QSize dimensions;
    if (!validatePng(payload, icon.icon, &dimensions, &error)) {
        failInstall(error);
        return;
    }
    if (!writeAtomicallyInside(d_->installJob->stagingPath,
                               icon.destination, payload, &error)) {
        failInstall(error);
        return;
    }

    d_->installJob->parsedPack.categories[icon.categoryIndex]
        .icons[icon.iconIndex]
        .dimensions = dimensions;
    d_->installJob->completedBytes += icon.icon.size;
    ++d_->installJob->completedIcons;
    updateInstallProgress(token);
    pumpIconDownloads();
}

void IconPackManager::finishInstall() {
    if (!d_->installJob)
        return;
    const QString id = d_->installJob->remote.pack.id;
    const QString stagingPath = d_->installJob->stagingPath;
    const QString stagingName = QFileInfo(stagingPath).fileName();
    if (treeContainsLinkLikeEntry(stagingPath)) {
        failInstall(tr("The staged icon pack contains an unsafe link."));
        return;
    }
    if (!QDir(d_->storageRoot).rename(stagingName, id)) {
        failInstall(tr("Could not activate the downloaded icon pack."));
        return;
    }

    Pack installed = d_->installJob->parsedPack;
    installed.state = State::Installed;
    installed.receivedBytes = installed.totalBytes;
    installed.error.clear();
    d_->installJob.reset();
    replaceOrAppendPack(std::move(installed));
    if (d_->busyRefreshError) {
        d_->busyRefreshError = false;
        setCatalogError({});
    }
    emit packChanged(id);
    emit catalogChanged();
}

void IconPackManager::failInstall(const QString& message) {
    if (!d_->installJob)
        return;

    // Detach the job first. QNetworkReply::abort() may synchronously emit
    // signals on some backends; every callback will now see a stale token.
    std::unique_ptr<Private::InstallJob> failedJob =
        std::move(d_->installJob);
    const QString id = failedJob->remote.pack.id;
    const QList<QNetworkReply*> activeReplies = failedJob->activeIcons.keys();
    failedJob->activeIcons.clear();
    for (QNetworkReply* activeReply : activeReplies) {
        if (!activeReply)
            continue;
        disconnect(activeReply, nullptr, this, nullptr);
        activeReply->abort();
        activeReply->deleteLater();
    }
    if (!failedJob->stagingPath.isEmpty())
        removeOrdinaryDirectoryTree(failedJob->stagingPath);
    Pack failed = failedJob->remote.pack;
    failed.state = State::Failed;
    failed.error = message;
    failed.receivedBytes = failedJob->reportedBytes;
    failed.totalBytes = failedJob->parsedPack.totalBytes;
    replaceOrAppendPack(std::move(failed));
    if (d_->busyRefreshError) {
        d_->busyRefreshError = false;
        setCatalogError({});
    }
    emit packChanged(id);
    emit catalogChanged();
}

bool IconPackManager::remove(const QString& id) {
    if (id == QLatin1String("nightlock-default") ||
        (d_->installJob && d_->installJob->remote.pack.id == id))
        return false;
    auto packIt = std::find_if(d_->packs.begin(), d_->packs.end(),
                               [&id](const Pack& item) { return item.id == id; });
    if (packIt == d_->packs.end() || packIt->state != State::Installed)
        return false;

    const QString path = QDir(d_->storageRoot).filePath(id);
    const QFileInfo info(path);
    if (!info.isDir() || isLinkLike(info) ||
        !removeOrdinaryDirectoryTree(path)) {
        packIt->error = tr("Could not remove the installed icon pack.");
        emit packChanged(id);
        return false;
    }

    const auto remoteIt = std::find_if(
        d_->remotePacks.cbegin(), d_->remotePacks.cend(),
        [&id](const Private::RemotePack& remote) { return remote.pack.id == id; });
    if (remoteIt != d_->remotePacks.cend()) {
        *packIt = remoteIt->pack;
    } else {
        d_->packs.erase(packIt);
    }
    emit packChanged(id);
    emit catalogChanged();
    return true;
}

void IconPackManager::replaceOrAppendPack(Pack pack) {
    for (Pack& existing : d_->packs) {
        if (existing.id == pack.id) {
            existing = std::move(pack);
            return;
        }
    }
    d_->packs.append(std::move(pack));
}

void IconPackManager::loadInstalledPacks() {
    if (!ensureOrdinaryDirectory(d_->storageRoot, nullptr))
        return;
    QDir root(d_->storageRoot);
    const QStringList directories = root.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QString& id : directories) {
        static const QRegularExpression stagingPattern(
            QStringLiteral(R"(^\.[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?\.install-[0-9a-f-]{36}$)"));
        if (stagingPattern.match(id).hasMatch()) {
            const QFileInfo stagingInfo(root.filePath(id));
            if (stagingInfo.isDir() && !isLinkLike(stagingInfo))
                removeOrdinaryDirectoryTree(stagingInfo.absoluteFilePath());
            continue;
        }
        if (!isValidId(id))
            continue;
        const QString directory = root.filePath(id);
        const QFileInfo directoryInfo(directory);
        if (!directoryInfo.isDir() || isLinkLike(directoryInfo))
            continue;

        // Downloaded packs are a derived cache. A validated, ordinary pack
        // directory that cannot be verified is removed so a later download
        // is never blocked by a stale destination. Link-like and unknown names
        // are deliberately left untouched.
        const auto discardInvalidPack = [&directory] {
            const QFileInfo info(directory);
            return info.isDir() && !isLinkLike(info) &&
                   removeOrdinaryDirectoryTree(directory);
        };

        const QString manifestPath =
            QDir(directory).filePath(QLatin1String(kManifestFile));
        const QFileInfo manifestInfo(manifestPath);
        if (!manifestInfo.isFile() || isLinkLike(manifestInfo) ||
            manifestInfo.size() <= 0 ||
            manifestInfo.size() > kMaximumManifestSize) {
            discardInvalidPack();
            continue;
        }
        QFile manifestFile(manifestPath);
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            discardInvalidPack();
            continue;
        }
        const QByteArray payload = manifestFile.readAll();
        manifestFile.close();
        if (payload.size() != manifestInfo.size()) {
            discardInvalidPack();
            continue;
        }

        QString error;
        const QUrl syntheticManifestUrl =
            d_->policy.contentRoot.resolved(QUrl(id + QLatin1String("/manifest.json")));
        const std::optional<ParsedManifest> parsed = parseManifest(
            payload, syntheticManifestUrl, directory, nullptr, d_->policy, &error);
        if (!parsed || parsed->pack.id != id) {
            discardInvalidPack();
            continue;
        }

        ParsedManifest verified = *parsed;
        bool valid = true;
        for (const ParsedIcon& icon : std::as_const(verified.icons)) {
            QSize dimensions;
            if (!inspectInstalledPng(directory, icon, &dimensions, &error)) {
                valid = false;
                break;
            }
            verified.pack.categories[icon.categoryIndex]
                .icons[icon.iconIndex]
                .dimensions = dimensions;
        }
        if (!valid) {
            discardInvalidPack();
            continue;
        }
        verified.pack.state = State::Installed;
        verified.pack.receivedBytes = verified.pack.totalBytes;
        d_->packs.append(std::move(verified.pack));
    }
}

}  // namespace iconpacks
