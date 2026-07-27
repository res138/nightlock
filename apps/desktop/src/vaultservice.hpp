#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

#include <nightlock/vault_file.hpp>

// The one owner of the vault session in the desktop app. UI code holds
// non-owning Group*/Entry* into root(); after lockAndWipe() those
// dangle, so views are cleared first (MainWindow::setVaultRoot(nullptr))
// and re-seeded from root() after the next unlock().
//
// Demo mode (NIGHTLOCK_DEMO=1) swaps the encrypted file for the
// in-memory mockup tree and turns every persistence call into a no-op,
// so the screenshot/debug hooks can never scribble over a real vault.
class VaultService : public QObject {
    Q_OBJECT
public:
    static VaultService* instance();

    bool demoMode() const { return demoRoot_ != nullptr; }
    void setDemoRoot(std::unique_ptr<nightlock::Group> root);

    // $NIGHTLOCK_VAULT override, else AppDataLocation/Primary.nlck —
    // the same resolution order the CLI uses.
    QString vaultPath() const;
    bool vaultExists() const;
    bool isUnlocked() const;

    nightlock::VaultError createNew(const QString& password);
    nightlock::VaultError unlock(const QString& password);
    nightlock::Group* root();

    // Debounced autosave: mutation points call this and a 500 ms
    // single-shot timer folds bursts into one disk write.
    void markDirty();
    // Flushes a pending save immediately; emits saveFailed and returns
    // false when the disk write fails.
    bool saveNow();
    // saveNow(), then wipes the decrypted tree and the key.
    void lockAndWipe();

signals:
    void saveFailed(const QString& reason);

private:
    VaultService();

    std::optional<nightlock::VaultFile> vault_;
    std::unique_ptr<nightlock::Group> demoRoot_;
    QTimer debounce_;
    bool dirty_ = false;
};
