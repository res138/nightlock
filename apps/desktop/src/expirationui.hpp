#pragma once

#include <QDate>
#include <QLocale>
#include <QString>

#include <nightlock/expiration.hpp>

namespace expirationui {

inline QDate toQDate(std::chrono::year_month_day date) {
    return {static_cast<int>(date.year()), static_cast<int>(unsigned(date.month())),
            static_cast<int>(unsigned(date.day()))};
}

inline QDate parseStored(std::string_view value) {
    const auto parsed = nightlock::expiration::parseDate(value);
    return parsed ? toQDate(*parsed) : QDate{};
}

inline QDate parseText(const QString& value) {
    const QByteArray utf8 = value.trimmed().toUtf8();
    if (const QDate stored = parseStored(
            std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
        stored.isValid()) {
        return stored;
    }
    return QLocale().toDate(value.trimmed(), QStringLiteral("d MMMM yyyy"));
}

inline QString storedText(const QDate& date) {
    return date.isValid() ? date.toString(QStringLiteral("dd/MM/yyyy")) : QString{};
}

inline QString displayText(const QDate& date) {
    return date.isValid()
               ? QLocale().toString(date, QStringLiteral("d MMMM yyyy"))
               : QString{};
}

inline QDate date(const nightlock::EntryField& field) {
    return parseStored(nightlock::secure::view(field.value));
}

inline bool isExpired(const nightlock::Entry& entry) {
    const QDate today = QDate::currentDate();
    return nightlock::expiration::isExpired(
        entry, std::chrono::year(today.year()) / static_cast<unsigned>(today.month()) /
                   static_cast<unsigned>(today.day()));
}

}  // namespace expirationui
