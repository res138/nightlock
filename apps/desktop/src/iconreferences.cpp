#include "iconreferences.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include "iconpackmanager.hpp"

namespace iconreferences {
namespace {

constexpr auto kPrefix = "nightlock-icon://";

bool isValidId(const QString& id) {
    static const QRegularExpression expression(
        QStringLiteral(R"(^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$)"));
    return id.size() <= 64 && expression.match(id).hasMatch();
}

bool isValidCategory(const QString& id) {
    return isValidId(id) && iconpacks::normalizedCategoryIds().contains(id);
}

QString comparablePath(const QString& path) {
    if (path.startsWith(QLatin1String(":/")))
        return path;
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

struct Match {
    Reference reference;
    QString filePath;
    QString title;
};

QString portableFileStem(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString name = path.section(QLatin1Char('/'), -1);
    const int suffix = name.lastIndexOf(QLatin1Char('.'));
    if (suffix > 0)
        name.truncate(suffix);
    return name;
}

struct Registry {
    bool dirty = true;
    QHash<QString, Match> byReference;
    QHash<QString, QString> referenceByPackIcon;
    QHash<QString, QString> referenceByPath;
    QHash<QString, QString> uniqueReferenceByStem;
    QSet<QString> ambiguousStems;

    Registry() {
        auto* manager = iconpacks::IconPackManager::instance();
        QObject::connect(manager, &iconpacks::IconPackManager::catalogChanged,
                         manager, [this] { dirty = true; });
        QObject::connect(manager, &iconpacks::IconPackManager::packChanged,
                         manager, [this](const QString&) { dirty = true; });
    }

    void addStem(const QString& rawStem, const QString& value) {
        const QString stem = rawStem.toCaseFolded();
        if (stem.isEmpty() || ambiguousStems.contains(stem))
            return;
        const auto existing = uniqueReferenceByStem.constFind(stem);
        if (existing == uniqueReferenceByStem.cend()) {
            uniqueReferenceByStem.insert(stem, value);
        } else if (*existing != value) {
            uniqueReferenceByStem.remove(stem);
            ambiguousStems.insert(stem);
        }
    }

    void ensureCurrent() {
        if (!dirty)
            return;
        dirty = false;
        byReference.clear();
        referenceByPackIcon.clear();
        referenceByPath.clear();
        uniqueReferenceByStem.clear();
        ambiguousStems.clear();

        const auto packs = iconpacks::IconPackManager::instance()->installedPacks();
        for (const iconpacks::Pack& pack : packs) {
            for (const iconpacks::Category& category : pack.categories) {
                for (const iconpacks::Icon& icon : category.icons) {
                    const QString value = iconreferences::build(
                        pack.id, category.id, icon.id);
                    if (value.isEmpty() || icon.filePath.isEmpty())
                        continue;
                    byReference.insert(
                        value, Match{{pack.id, category.id, icon.id},
                                     icon.filePath, icon.title});
                    referenceByPackIcon.insert(
                        pack.id + QLatin1Char('\n') + icon.id, value);
                    referenceByPath.insert(comparablePath(icon.filePath), value);
                    // A vanished P1-P7 path may only migrate to an optional
                    // downloaded pack. Mapping a generic legacy name such as
                    // "entry" to Nightlock Default would destroy the original
                    // value before its actual legacy pack is installed.
                    if (pack.state == iconpacks::State::Installed) {
                        addStem(icon.id, value);
                        addStem(portableFileStem(icon.filePath), value);
                    }
                }
            }
        }
    }
};

Registry& registry() {
    static Registry value;
    value.ensureCurrent();
    return value;
}

std::optional<Match> findReference(const Reference& reference) {
    Registry& index = registry();
    const QString value = iconreferences::build(
        reference.packId, reference.categoryId, reference.iconId);
    const auto found = index.byReference.constFind(value);
    if (found != index.byReference.cend())
        return *found;

    // Icon IDs are globally unique within a manifest. If a later version
    // reclassifies an icon, keep older stored references alive while the URI
    // still records the category that was current when it was selected.
    const auto currentValue = index.referenceByPackIcon.constFind(
        reference.packId + QLatin1Char('\n') + reference.iconId);
    if (currentValue == index.referenceByPackIcon.cend())
        return std::nullopt;
    const auto current = index.byReference.constFind(*currentValue);
    return current == index.byReference.cend()
               ? std::nullopt
               : std::optional<Match>(*current);
}

std::optional<Match> findPath(const QString& path) {
    if (path.isEmpty())
        return std::nullopt;
    Registry& index = registry();
    const auto value = index.referenceByPath.constFind(comparablePath(path));
    if (value == index.referenceByPath.cend())
        return std::nullopt;
    const auto found = index.byReference.constFind(*value);
    return found == index.byReference.cend()
               ? std::nullopt
               : std::optional<Match>(*found);
}

bool looksLikeLegacyPackPath(QString path) {
    // QFileInfo cannot recognize a Windows drive path after a vault is moved
    // to macOS/Linux, so check both slash forms explicitly.
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    static const QRegularExpression legacyDirectory(
        QStringLiteral(R"((?:^|/)P[1-7]/)"),
        QRegularExpression::CaseInsensitiveOption);
    const bool absolute = path.startsWith(QLatin1Char('/')) ||
                          QRegularExpression(
                              QStringLiteral(R"(^[A-Za-z]:/)"))
                              .match(path)
                              .hasMatch();
    return absolute && legacyDirectory.match(path).hasMatch();
}

std::optional<Match> findUniqueLegacyStem(const QString& path) {
    if (!looksLikeLegacyPackPath(path) || QFile::exists(path))
        return std::nullopt;
    const QString stem = portableFileStem(path);
    if (stem.isEmpty())
        return std::nullopt;

    Registry& index = registry();
    const auto value = index.uniqueReferenceByStem.constFind(stem.toCaseFolded());
    if (value == index.uniqueReferenceByStem.cend())
        return std::nullopt;
    const auto found = index.byReference.constFind(*value);
    return found == index.byReference.cend()
               ? std::nullopt
               : std::optional<Match>(*found);
}

QString readableId(QString id) {
    id.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!id.isEmpty())
        id[0] = id[0].toUpper();
    return id;
}

}  // namespace

QString build(const QString& packId, const QString& categoryId,
              const QString& iconId) {
    if (!isValidId(packId) || !isValidCategory(categoryId) ||
        !isValidId(iconId)) {
        return {};
    }
    return QLatin1String(kPrefix) + packId + QLatin1Char('/') + categoryId +
           QLatin1Char('/') + iconId;
}

std::optional<Reference> parse(const QString& value) {
    if (!value.startsWith(QLatin1String(kPrefix), Qt::CaseSensitive))
        return std::nullopt;
    const QString remainder = value.mid(QLatin1String(kPrefix).size());
    const QStringList components = remainder.split(QLatin1Char('/'),
                                                    Qt::KeepEmptyParts);
    if (components.size() != 3 || !isValidId(components[0]) ||
        !isValidCategory(components[1]) || !isValidId(components[2])) {
        return std::nullopt;
    }
    const Reference reference{components[0], components[1], components[2]};
    if (build(reference.packId, reference.categoryId, reference.iconId) != value)
        return std::nullopt;
    return reference;
}

bool isPortable(const QString& value) {
    return parse(value).has_value();
}

void initialize() {
    (void)registry();
}

QString resolve(const QString& value) {
    if (value.isEmpty())
        return {};
    if (const auto reference = parse(value)) {
        const auto match = findReference(*reference);
        // A failed/partial cache removal can emit packChanged while the
        // manager deliberately keeps the pack Installed so Remove remains
        // retryable. Never hand a vanished path to QIcon: all consumers then
        // get their built-in fallback, independent of signal connection order.
        if (!match || match->filePath.isEmpty() ||
            !QFile::exists(match->filePath)) {
            return {};
        }
        return match->filePath;
    }
    // A malformed string using our scheme must never be interpreted as a
    // filesystem path. This also makes typo/corruption fallback deterministic.
    if (value.startsWith(QLatin1String("nightlock-icon:"), Qt::CaseInsensitive))
        return {};
    if (QFile::exists(value))
        return value;
    if (const auto recovered = findUniqueLegacyStem(value))
        return recovered->filePath;
    return {};
}

QString resolveOrFallback(const QString& value, const QString& fallbackPath) {
    const QString path = resolve(value);
    return path.isEmpty() ? fallbackPath : path;
}

QString displayTitle(const QString& value) {
    if (const auto reference = parse(value)) {
        if (const auto match = findReference(*reference)) {
            if (!match->title.trimmed().isEmpty())
                return match->title.trimmed();
        }
        return readableId(reference->iconId);
    }
    if (const auto match = findPath(value)) {
        if (!match->title.trimmed().isEmpty())
            return match->title.trimmed();
        return readableId(match->reference.iconId);
    }
    const QString baseName = portableFileStem(value);
    return baseName.isEmpty() ? value : baseName;
}

bool isLegacyPackPath(const QString& value) {
    return looksLikeLegacyPackPath(value);
}

std::optional<QString> fromLegacyPath(const QString& path) {
    if (path.isEmpty() || parse(path))
        return std::nullopt;
    auto match = findPath(path);
    if (!match)
        match = findUniqueLegacyStem(path);
    if (!match)
        return std::nullopt;
    const QString value = build(match->reference.packId,
                                match->reference.categoryId,
                                match->reference.iconId);
    return value.isEmpty() ? std::nullopt : std::optional<QString>(value);
}

QString normalizeStoredValue(const QString& value) {
    if (parse(value))
        return value;
    if (const auto portable = fromLegacyPath(value))
        return *portable;
    return value;
}

}  // namespace iconreferences
