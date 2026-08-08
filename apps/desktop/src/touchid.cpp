#include "touchid.hpp"

#include <QMetaObject>
#include <QObject>

#include <utility>

namespace touchid {

bool isAvailable(QString* error) {
    if (error)
        *error = QObject::tr("Touch ID is available only on macOS.");
    return false;
}

bool isEnabledForVault(const QString&) {
    return false;
}

bool enableForVault(const QString&, const QString&, QString* error) {
    if (error)
        *error = QObject::tr("Touch ID is available only on macOS.");
    return false;
}

bool disableForVault(const QString&, QString*) {
    return true;
}

void authenticate(const QString&, QObject* context,
                  AuthenticationCallback callback) {
    AuthenticationResult result;
    result.error = QObject::tr("Touch ID is available only on macOS.");
    QMetaObject::invokeMethod(
        context, [callback = std::move(callback), result = std::move(result)]() mutable {
            callback(std::move(result));
        },
        Qt::QueuedConnection);
}

void cancelAuthentication() {}

}  // namespace touchid
