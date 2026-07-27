#include "vaultservice.hpp"

#include <QFileInfo>
#include <QStandardPaths>

#include <filesystem>

#include "qsecure.hpp"

namespace {

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

QString VaultService::vaultPath() const {
    const QString env = qEnvironmentVariable("NIGHTLOCK_VAULT");
    if (!env.isEmpty())
        return env;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/Primary.nlck");
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
