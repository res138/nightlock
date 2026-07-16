#include <QApplication>
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
                menu->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_MENU"));
                QApplication::quit();
            });
        });
    }

    return app.exec();
}
