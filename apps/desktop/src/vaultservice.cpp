#include "vaultservice.hpp"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <filesystem>

#include "qsecure.hpp"

namespace {

// The startup-default setting. Present-but-empty is the explicit
// "start with no vault" state; an absent key means the setting was
// never written (pre-NL8 installs fall back to the default location).
constexpr char kRememberedKey[] = "vault/default";

std::filesystem::path toFsPath(const QString& path) {
    return std::filesystem::path(path.toStdString());
}

}  // namespace

VaultService* VaultService::instance() {
    static VaultService service;
    return &service;
}

VaultService::VaultService() {
    debounce_.setSingleShot(true);
    debounce_.setInterval(500);
    connect(&debounce_, &QTimer::timeout, this, [this] { saveNow(); });
}

void VaultService::setDemoRoot(std::unique_ptr<nightlock::Group> root) {
    demoRoot_ = std::move(root);
}

QString VaultService::defaultVaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/Primary.nlck");
}

QString VaultService::vaultPath() const {
    const QString env = qEnvironmentVariable("NIGHTLOCK_VAULT");
    if (!env.isEmpty())
        return env;
    if (!activePath_.isEmpty())
        return activePath_;
    return defaultVaultPath();
}

void VaultService::setVaultPath(const QString& path) {
    if (activePath_ == path)
        return;
    activePath_ = path;
    emit vaultPathChanged(vaultPath());
}

QString VaultService::rememberedVaultPath() {
    return QSettings().value(QLatin1String(kRememberedKey)).toString();
}

void VaultService::setRememberedVaultPath(const QString& path) {
    QSettings().setValue(QLatin1String(kRememberedKey), path);
    emit rememberedVaultChanged(path);
}

void VaultService::clearRememberedVaultPath() {
    QSettings().setValue(QLatin1String(kRememberedKey), QString());
    emit rememberedVaultChanged(QString());
}

QString VaultService::startupPath() const {
    const QString env = qEnvironmentVariable("NIGHTLOCK_VAULT");
    if (!env.isEmpty())
        return env;
    QSettings settings;
    if (settings.contains(QLatin1String(kRememberedKey))) {
        const QString remembered =
            settings.value(QLatin1String(kRememberedKey)).toString();
        if (!remembered.isEmpty() && QFileInfo::exists(remembered))
            return remembered;
        return {};  // cleared, or the remembered file is gone
    }
    if (QFileInfo::exists(defaultVaultPath()))
        return defaultVaultPath();
    return {};
}

void VaultService::rememberOpenedVault() {
    if (qEnvironmentVariableIsSet("NIGHTLOCK_VAULT"))
        return;
    setRememberedVaultPath(vaultPath());
}

bool VaultService::vaultExists() const {
    return QFileInfo::exists(vaultPath());
}

bool VaultService::isUnlocked() const {
    return demoRoot_ != nullptr || (vault_ && vault_->isOpen());
}

nightlock::VaultError VaultService::createNew(const QString& password) {
    nightlock::secure::String pw;
    assignSecret(pw, password);
    auto result =
        nightlock::VaultFile::create(toFsPath(vaultPath()), nightlock::secure::view(pw));
    if (!result)
        return result.error();
    vault_.emplace(result.take());
    rememberOpenedVault();
    return nightlock::VaultError::None;
}

nightlock::VaultError VaultService::unlock(const QString& password) {
    nightlock::secure::String pw;
    assignSecret(pw, password);
    auto result =
        nightlock::VaultFile::open(toFsPath(vaultPath()), nightlock::secure::view(pw));
    if (!result)
        return result.error();
    vault_.emplace(result.take());
    rememberOpenedVault();
    return nightlock::VaultError::None;
}

nightlock::Group* VaultService::root() {
    if (demoRoot_)
        return demoRoot_.get();
    return vault_ ? vault_->root() : nullptr;
}

void VaultService::markDirty() {
    if (demoMode() || !vault_ || !vault_->isOpen())
        return;
    dirty_ = true;
    debounce_.start();  // restarts on every call: bursts fold into one write
}

bool VaultService::saveNow() {
    debounce_.stop();
    // No pending changes -> no disk write and no .bak rotation (the
    // quit path calls this unconditionally).
    if (!dirty_ || demoMode() || !vault_ || !vault_->isOpen())
        return true;
    const nightlock::VaultError error = vault_->save();
    if (error != nightlock::VaultError::None) {
        emit saveFailed(QString::fromUtf8(nightlock::errorMessage(error)));
        return false;
    }
    dirty_ = false;
    return true;
}

void VaultService::lockAndWipe() {
    if (demoMode())
        return;
    if (vault_) {
        saveNow();
        vault_->lock();
        vault_.reset();
    }
}
