#include <QApplication>
#include <QFile>
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

    // Debug hook: NIGHTLOCK_SCREENSHOT=<path> saves a frame and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT")) {
        QTimer::singleShot(800, &window, [&window] {
            window.grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT"));
            QApplication::quit();
        });
    }

    return app.exec();
}
