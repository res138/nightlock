#pragma once

#include <QWidget>

class QListWidget;
class QStackedWidget;
class QVBoxLayout;

// The standalone application-preferences window (⌘, or the gear
// button). Obsidian-style split: the category list on the left, the
// selected category's settings on the right. App-level only — no
// vault data lives here, so it opens and survives a vault lock.
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);

    // Switches the left-hand list to the given category row.
    void selectCategory(int index);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void addCategory(const QString& iconName, const QString& title, QWidget* page);

    QWidget* buildGeneralPage();
    QWidget* buildAppearancePage();
    QWidget* buildHotkeysPage();
    QWidget* buildGraphPage();

    QListWidget* nav_;
    QStackedWidget* pages_;
};
