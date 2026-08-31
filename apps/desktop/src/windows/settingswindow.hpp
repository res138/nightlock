#pragma once

#include <QWidget>

class QListWidget;
class QStackedWidget;
class QVBoxLayout;

// The standalone application-preferences window (⌘, or the gear
// button). Obsidian-style split: the category list on the left, the
// selected category's settings on the right. Opens fine while locked
// (the Database page serves the first-run state), but every lock,
// switch or sign-out closes it along with the other vault windows.
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);

    // Switches the left-hand list to the given category row.
    void selectCategory(int index);

signals:
    // Database page picks; MainWindow owns the vault mechanics.
    void switchDatabaseRequested(const QString& path);
    void createDatabaseRequested(const QString& path);
    void signOutRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void addCategory(const QString& iconName, const QString& title, QWidget* page);

    QWidget* buildGeneralPage();
    QWidget* buildDatabasePage();
    QWidget* buildAppearancePage();
    QWidget* buildHotkeysPage();
    QWidget* buildGraphPage();
    QWidget* buildIconsLibraryPage();

    QListWidget* nav_;
    QStackedWidget* pages_;
};
