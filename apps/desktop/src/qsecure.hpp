#pragma once

#include <QByteArray>
#include <QString>

#include <nightlock/secure.hpp>

// QString <-> secure::String boundary. The QString side is a plain
// copy Qt never zeroizes — an accepted limitation of showing secrets
// in widgets (see docs/security.md); the secure side keeps the vault's
// own storage pinned and scrubbed.

inline QString toQString(const nightlock::secure::String& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

inline void assignSecret(nightlock::secure::String& dst, const QString& src) {
    const QByteArray utf8 = src.toUtf8();
    dst.assign(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}
