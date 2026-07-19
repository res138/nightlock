#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QMenu>
#include <QTimer>

#include <nightlock/group.hpp>

#include "demovault.hpp"
#include "windows/mainwindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Nightlock"));

    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    auto vault = createDemoVault();

    MainWindow window(vault.get());
    window.resize(1180, 720);
    window.show();
    window.selectGroupNamed(QStringLiteral("Personal 2020"));
    window.selectEntryNamed(
        qEnvironmentVariable("NIGHTLOCK_SELECT_ENTRY", QStringLiteral("GitHub")));

    // Debug hook: NIGHTLOCK_TEST_MOVE="<group>:<target>" drops a folder
    // onto another through the regular drag-and-drop model path.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_MOVE")) {
        const QStringList parts = qEnvironmentVariable("NIGHTLOCK_TEST_MOVE").split(QLatin1Char(':'));
        if (parts.size() == 2)
            window.debugMoveGroup(parts[0], parts[1]);
    }

    // Debug hook: NIGHTLOCK_TEST_ENTRY_ICON=<path> assigns an icon to
    // the selected entry.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_ENTRY_ICON"))
        window.debugSetEntryIcon(qEnvironmentVariable("NIGHTLOCK_TEST_ENTRY_ICON"));

    // Debug hook: NIGHTLOCK_TEST_FOLDERS=1 exercises folder create,
    // rename and delete through the tree model.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_FOLDERS"))
        window.debugFolderOps();

    // Debug hook: NIGHTLOCK_SCREENSHOT=<path> saves a frame and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT")) {
        QTimer::singleShot(800, &window, [&window] {
            window.grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT"));
            QApplication::quit();
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_MENU=<path> saves the opened
    // context menu of the selected entry and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_MENU")) {
        QTimer::singleShot(800, &window, [&window] {
            QMenu* menu = window.popupEntryMenuForScreenshot();
            if (!menu) {
                QApplication::quit();
                return;
            }
            const int delay = qEnvironmentVariableIntValue("NIGHTLOCK_SCREENSHOT_MENU_DELAY");
            QTimer::singleShot(delay > 0 ? delay : 400, menu, [menu] {
                // NIGHTLOCK_SCREENSHOT_MENU_ACTIVE=<row> highlights an
                // item as if hovered. Set right before the grab: a stray
                // real mouse-over would reset it otherwise.
                if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_MENU_ACTIVE")) {
                    const int row =
                        qEnvironmentVariableIntValue("NIGHTLOCK_SCREENSHOT_MENU_ACTIVE");
                    if (row >= 0 && row < menu->actions().size())
                        menu->setActiveAction(menu->actions().at(row));
                }
                menu->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_MENU"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_GALLERY=<path> saves the icon
    // pack gallery popup and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_GALLERY")) {
        QTimer::singleShot(800, &window, [&window] {
            QWidget* gallery = window.openIconGalleryForScreenshot();
            QTimer::singleShot(400, gallery, [gallery] {
                gallery->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_GALLERY"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_DIALOG=<path> saves the entry
    // edit dialog (prefilled from the selected entry) and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_DIALOG")) {
        QTimer::singleShot(800, &window, [&window] {
            QDialog* dialog = window.openEntryDialogForScreenshot();
            QTimer::singleShot(400, dialog, [dialog] {
                dialog->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_DIALOG"));
                QApplication::quit();
            });
        });
    }

    return app.exec();
}
