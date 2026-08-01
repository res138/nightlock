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

    // Where a fresh install keeps the vault: AppDataLocation/
    // Primary.nlck — the same location the CLI defaults to.
    static QString defaultVaultPath();

    // $NIGHTLOCK_VAULT override, else the session's active file, else
    // defaultVaultPath().
    QString vaultPath() const;
    // Retargets the service at another vault file. The open session
    // does not follow — callers lock first, then switch.
    void setVaultPath(const QString& path);
    bool vaultExists() const;
    bool isUnlocked() const;

    // The startup default ("vault/default" in QSettings): the single
    // vault that opens on the next launch. Every successful unlock or
    // create refreshes it, so the most recent vault wins. A present-
    // but-empty value means the user cleared it — the app then starts
    // on the first-run screen with no vault preselected.
    static QString rememberedVaultPath();
    void setRememberedVaultPath(const QString& path);
    void clearRememberedVaultPath();

    // Launch resolution: env override first, then the remembered vault
    // when its file still exists, then (only if the setting was never
    // written — pre-NL8 installs) an existing default-location vault.
    // Empty result = first-run screen.
    QString startupPath() const;

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
    void vaultPathChanged(const QString& path);
    void rememberedVaultChanged(const QString& path);

private:
    VaultService();

    // The vault that just opened becomes the startup default. Skipped
    // under $NIGHTLOCK_VAULT so test runs never touch the real setting.
    void rememberOpenedVault();

    std::optional<nightlock::VaultFile> vault_;
    std::unique_ptr<nightlock::Group> demoRoot_;
    QString activePath_;  // empty = defaultVaultPath()
    QTimer debounce_;
    bool dirty_ = false;
};
