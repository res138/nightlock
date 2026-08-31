#pragma once

#include <QString>

#include <optional>

// Portable values stored in Entry::icon and Group::icon.  Pack files live in
// per-user application data, so persisting their absolute paths would make a
// vault machine-specific and would break when a pack is reinstalled.
namespace iconreferences {

struct Reference {
    QString packId;
    QString categoryId;
    QString iconId;

    bool operator==(const Reference&) const = default;
};

// Builds the canonical ASCII representation
// nightlock-icon://<pack>/<category>/<icon>. Returns an empty string if any ID
// is invalid or the category is not part of Nightlock's normalized taxonomy.
QString build(const QString& packId, const QString& categoryId,
              const QString& iconId);

// Strictly parses only the canonical representation emitted by build().
// Percent encoding, queries, fragments, extra path components and non-ASCII
// IDs are deliberately rejected.
std::optional<Reference> parse(const QString& value);
bool isPortable(const QString& value);

// Establishes the pack-change observer before UI consumers subscribe. This is
// idempotent; applications normally get it implicitly from resolve().
void initialize();

// Finds an installed icon and returns its local file/resource path. Legacy
// resource and filesystem paths continue to resolve while the file exists.
// An unresolved or malformed portable reference returns an empty string.
QString resolve(const QString& value);
QString resolveOrFallback(const QString& value, const QString& fallbackPath);

// Human-readable icon title. Installed pack metadata is preferred; an
// unresolved portable reference falls back to a readable form of its icon ID.
QString displayTitle(const QString& value);

// Recognizes absolute paths inside the historical bundled icons/P1...P7
// layout, including Windows paths read on another operating system.
bool isLegacyPackPath(const QString& value);

// Converts a path which exactly matches an icon in an installed pack to a
// portable value. Unknown paths are left to the caller as legacy values.
std::optional<QString> fromLegacyPath(const QString& path);

// Leaves canonical portable references alone, upgrades a known legacy path,
// and otherwise returns the input unchanged.
QString normalizeStoredValue(const QString& value);

}  // namespace iconreferences
