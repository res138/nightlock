#pragma once

#include <QString>

#include <functional>

class QObject;

// macOS Touch ID integration. The master password is stored as a
// per-vault Keychain item. Provisioned builds use biometric
// SecAccessControl; ad-hoc builds use the login Keychain's app ACL and
// retrieve the item only after LocalAuthentication succeeds. Other
// platforms provide an unavailable stub so the UI remains portable.
namespace touchid {

struct AuthenticationResult {
    QString password;
    QString error;
    bool cancelled = false;
};

using AuthenticationCallback = std::function<void(AuthenticationResult)>;

bool isAvailable(QString* error = nullptr);
bool isEnabledForVault(const QString& vaultPath);

// Adds or replaces the protected Keychain item and persists the
// per-vault opt-in only after the Keychain write succeeds.
bool enableForVault(const QString& vaultPath, const QString& password,
                    QString* error = nullptr);

// Removes both the Keychain item and the opt-in marker. A missing
// item is considered successfully disabled.
bool disableForVault(const QString& vaultPath, QString* error = nullptr);

// Authenticates asynchronously. callback is always delivered on the
// Qt GUI thread while context is alive.
void authenticate(const QString& vaultPath, QObject* context,
                  AuthenticationCallback callback);

// Dismisses an in-flight system prompt, for example when the user
// unlocks with the password or switches databases instead.
void cancelAuthentication();

}  // namespace touchid
