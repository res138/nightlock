#include "touchid.hpp"

#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSettings>

#include <mutex>
#include <utility>

namespace touchid {
namespace {

NSString* const kService = @"com.nightlock.Nightlock.touchid";
constexpr auto kEnabledPrefix = "touch-id/enabled/";
constexpr auto kBackendPrefix = "touch-id/backend/";
std::mutex gAuthenticationMutex;
LAContext* gAuthenticationContext = nil;

enum class Backend { None, DataProtection, LoginKeychain };

QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

NSString* accountForPath(const QString& path) {
    return normalizedPath(path).toNSString();
}

QString vaultDigest(const QString& path) {
    const QByteArray digest = QCryptographicHash::hash(
        normalizedPath(path).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest);
}

QString enabledKey(const QString& path) {
    return QLatin1String(kEnabledPrefix) + vaultDigest(path);
}

QString backendKey(const QString& path) {
    return QLatin1String(kBackendPrefix) + vaultDigest(path);
}

Backend backendForVault(const QString& path) {
    QSettings settings;
    if (!settings.value(enabledKey(path), false).toBool())
        return Backend::None;
    const QString value = settings.value(backendKey(path)).toString();
    if (value == QLatin1String("login-keychain"))
        return Backend::LoginKeychain;
    // Opt-ins written before backend tracking used Data Protection.
    return Backend::DataProtection;
}

void setEnabled(const QString& path, Backend backend) {
    QSettings settings;
    if (backend == Backend::None) {
        settings.remove(enabledKey(path));
        settings.remove(backendKey(path));
        return;
    }
    settings.setValue(enabledKey(path), true);
    settings.setValue(backendKey(path),
                      backend == Backend::DataProtection
                          ? QStringLiteral("data-protection")
                          : QStringLiteral("login-keychain"));
}

QString osStatusMessage(OSStatus status) {
    CFStringRef text = SecCopyErrorMessageString(status, nullptr);
    if (!text)
        return QObject::tr("Keychain error %1.").arg(status);
    const QString result = QString::fromCFString(text);
    CFRelease(text);
    return result;
}

NSMutableDictionary* itemQuery(const QString& path, Backend backend) {
    NSMutableDictionary* query = [@{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : kService,
        (__bridge id)kSecAttrAccount : accountForPath(path),
    } mutableCopy];
    if (backend == Backend::DataProtection) {
        // Required on macOS for iOS-style accessibility and
        // SecAccessControl semantics; it does not enable iCloud sync.
        query[(__bridge id)kSecUseDataProtectionKeychain] = @YES;
    }
    return query;
}

OSStatus replaceLoginKeychainItem(const QString& path, NSData* secret) {
    NSMutableDictionary* query = itemQuery(path, Backend::LoginKeychain);
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
    if (status != errSecSuccess && status != errSecItemNotFound)
        return status;

    // SecAccess is the legacy macOS Keychain ACL used by ad-hoc builds that
    // cannot access the Data Protection Keychain. Passing a null trusted list
    // explicitly grants this application access without a confirmation dialog.
    SecAccessRef access = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    status = SecAccessCreate(CFSTR("Nightlock Touch ID"), nullptr, &access);
#pragma clang diagnostic pop
    if (status != errSecSuccess)
        return status;

    query[(__bridge id)kSecValueData] = secret;
    query[(__bridge id)kSecAttrLabel] = @"Nightlock Touch ID";
    query[(__bridge id)kSecAttrAccess] = (__bridge id)access;
    status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
    CFRelease(access);
    return status;
}

OSStatus replaceDataProtectionItem(const QString& path, NSData* secret,
                                   QString* error) {
    CFErrorRef accessError = nullptr;
    SecAccessControlRef access = SecAccessControlCreateWithFlags(
        kCFAllocatorDefault, kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        kSecAccessControlBiometryCurrentSet, &accessError);
    if (!access) {
        if (error) {
            if (accessError) {
                CFStringRef description = CFErrorCopyDescription(accessError);
                *error = QString::fromCFString(description);
                CFRelease(description);
            } else {
                *error = QObject::tr(
                    "Could not protect the Keychain item with Touch ID.");
            }
        }
        if (accessError)
            CFRelease(accessError);
        return errSecParam;
    }

    NSMutableDictionary* query = itemQuery(path, Backend::DataProtection);
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
    if (status == errSecSuccess || status == errSecItemNotFound) {
        query[(__bridge id)kSecValueData] = secret;
        query[(__bridge id)kSecAttrAccessControl] = (__bridge id)access;
        status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
    }
    CFRelease(access);
    return status;
}

bool isCancellation(NSError* error) {
    if (![error.domain isEqualToString:LAErrorDomain])
        return false;
    switch (error.code) {
        case LAErrorUserCancel:
        case LAErrorAppCancel:
        case LAErrorSystemCancel:
        case LAErrorUserFallback:
            return true;
        default:
            return false;
    }
}

void deliver(QPointer<QObject> context, AuthenticationCallback callback,
             AuthenticationResult result) {
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [context, callback = std::move(callback), result = std::move(result)]() mutable {
            if (context)
                callback(std::move(result));
        },
        Qt::QueuedConnection);
}

void clearAuthenticationContext(LAContext* context) {
    const std::lock_guard lock(gAuthenticationMutex);
    if (gAuthenticationContext == context)
        gAuthenticationContext = nil;
}

}  // namespace

bool isAvailable(QString* error) {
    @autoreleasepool {
        LAContext* context = [[LAContext alloc] init];
        NSError* nativeError = nil;
        const bool available = [context
            canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                         error:&nativeError];
        if (!available && error) {
            *error = nativeError
                         ? QString::fromNSString(nativeError.localizedDescription)
                         : QObject::tr("Touch ID is not available on this Mac.");
        }
        return available;
    }
}

bool isEnabledForVault(const QString& vaultPath) {
    return backendForVault(vaultPath) != Backend::None;
}

bool enableForVault(const QString& vaultPath, const QString& password,
                    QString* error) {
    QString availabilityError;
    if (!isAvailable(&availabilityError)) {
        if (error)
            *error = availabilityError;
        return false;
    }

    @autoreleasepool {
        NSData* secret = [password.toNSString()
            dataUsingEncoding:NSUTF8StringEncoding];
        QString protectionError;
        OSStatus status = replaceDataProtectionItem(
            vaultPath, secret, &protectionError);
        if (status == errSecSuccess) {
            // Remove a credential left by an earlier ad-hoc build.
            SecItemDelete((__bridge CFDictionaryRef)itemQuery(
                vaultPath, Backend::LoginKeychain));
            setEnabled(vaultPath, Backend::DataProtection);
            return true;
        }

        // Data Protection Keychain requires a provisioned application
        // identifier. Local/ad-hoc builds have none, so retain the
        // Keychain's application ACL and enforce Touch ID in this app.
        if (status == errSecMissingEntitlement) {
            status = replaceLoginKeychainItem(vaultPath, secret);
            if (status == errSecSuccess) {
                setEnabled(vaultPath, Backend::LoginKeychain);
                return true;
            }
        }

        setEnabled(vaultPath, Backend::None);
        if (error)
            *error = !protectionError.isEmpty() ? protectionError
                                                : osStatusMessage(status);
        return false;
    }
}

bool disableForVault(const QString& vaultPath, QString* error) {
    const Backend backend = backendForVault(vaultPath);
    if (backend == Backend::None)
        return true;
    @autoreleasepool {
        NSMutableDictionary* query = itemQuery(vaultPath, backend);
        const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        if (status != errSecSuccess && status != errSecItemNotFound) {
            if (error)
                *error = osStatusMessage(status);
            return false;
        }
    }
    setEnabled(vaultPath, Backend::None);
    return true;
}

void authenticate(const QString& vaultPath, QObject* context,
                  AuthenticationCallback callback) {
    if (!context)
        return;
    const QPointer<QObject> receiver(context);
    if (!isEnabledForVault(vaultPath)) {
        AuthenticationResult result;
        result.error = QObject::tr("Touch ID is not enabled for this database.");
        deliver(receiver, std::move(callback), std::move(result));
        return;
    }

    cancelAuthentication();
    LAContext* authenticationContext = [[LAContext alloc] init];
    {
        const std::lock_guard lock(gAuthenticationMutex);
        gAuthenticationContext = authenticationContext;
    }
    NSError* evaluationError = nil;
    if (![authenticationContext
            canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                         error:&evaluationError]) {
        AuthenticationResult result;
        result.error = evaluationError
                           ? QString::fromNSString(evaluationError.localizedDescription)
                           : QObject::tr("Touch ID is not available on this Mac.");
        clearAuthenticationContext(authenticationContext);
        deliver(receiver, std::move(callback), std::move(result));
        return;
    }

    const QString requestedPath = vaultPath;
    const Backend requestedBackend = backendForVault(vaultPath);
    [authenticationContext
        evaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
        localizedReason:QObject::tr("Unlock Nightlock with Touch ID").toNSString()
                  reply:^(BOOL success, NSError* nativeError) {
                    AuthenticationResult result;
                    if (!success) {
                        result.cancelled = isCancellation(nativeError);
                        if (!result.cancelled) {
                            result.error = nativeError
                                               ? QString::fromNSString(
                                                     nativeError.localizedDescription)
                                               : QObject::tr("Touch ID authentication failed.");
                        }
                        clearAuthenticationContext(authenticationContext);
                        deliver(receiver, std::move(callback), std::move(result));
                        return;
                    }

                    @autoreleasepool {
                        // The policy was already evaluated above. Reuse
                        // that context and refuse a second implicit UI
                        // prompt while reading the protected item.
                        NSMutableDictionary* query = itemQuery(requestedPath,
                                                              requestedBackend);
                        query[(__bridge id)kSecReturnData] = @YES;
                        query[(__bridge id)kSecMatchLimit] =
                            (__bridge id)kSecMatchLimitOne;
                        // Touch ID has already succeeded. Never let Keychain
                        // replace it with a login-password dialog. For the
                        // legacy backend, an ACL mismatch now fails closed and
                        // requires the credential to be configured again.
                        authenticationContext.interactionNotAllowed = YES;
                        if (requestedBackend == Backend::DataProtection) {
                            query[(__bridge id)kSecUseAuthenticationContext] =
                                authenticationContext;
                        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                            query[(__bridge id)kSecUseAuthenticationUI] =
                                (__bridge id)kSecUseAuthenticationUIFail;
#pragma clang diagnostic pop
                        }

                        CFTypeRef value = nullptr;
                        const OSStatus status = SecItemCopyMatching(
                            (__bridge CFDictionaryRef)query, &value);
                        if (status == errSecSuccess && value) {
                            NSData* data = (__bridge_transfer NSData*)value;
                            result.password = QString::fromUtf8(
                                static_cast<const char*>(data.bytes), data.length);
                        } else {
                            // The credential is no longer usable. Stop
                            // offering Touch ID until explicit setup.
                            setEnabled(requestedPath, Backend::None);
                            result.error = osStatusMessage(status);
                        }
                    }
                    clearAuthenticationContext(authenticationContext);
                    deliver(receiver, std::move(callback), std::move(result));
                  }];
}

void cancelAuthentication() {
    LAContext* context = nil;
    {
        const std::lock_guard lock(gAuthenticationMutex);
        context = gAuthenticationContext;
        gAuthenticationContext = nil;
    }
    [context invalidate];
}

}  // namespace touchid
