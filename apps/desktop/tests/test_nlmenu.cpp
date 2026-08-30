#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopedPointer>
#include <QTest>
#include <QTextEdit>

#include "widgets/nlmenu.hpp"

namespace {

QString actionLabel(QAction* action) {
    QString label = action->text();
    label.remove(QLatin1Char('&'));
    return label;
}

QAction* actionStartingWith(QMenu* menu, const QString& prefix) {
    for (QAction* action : menu->actions()) {
        if (actionLabel(action).startsWith(prefix, Qt::CaseInsensitive))
            return action;
    }
    return nullptr;
}

NlMenu* visibleNlMenu() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* menu = qobject_cast<NlMenu*>(widget); menu && menu->isVisible())
            return menu;
    }
    return nullptr;
}

void closeVisibleNlMenu() {
    if (NlMenu* menu = visibleNlMenu())
        menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

}  // namespace

class NlMenuTests final : public QObject {
    Q_OBJECT

private slots:
    void standardActionsKeepIdentityAndBehaviour();
    void nestedStandardMenusBecomeNlMenus();
    void textAdapterHandlesStandardTextEditors();
};

void NlMenuTests::standardActionsKeepIdentityAndBehaviour() {
    QLineEdit editor(QStringLiteral("nightlock"));
    editor.selectAll();
    QMenu* standard = editor.createStandardContextMenu();
    QVERIFY(standard);

    QAction* copy = actionStartingWith(standard, QStringLiteral("Copy"));
    QVERIFY2(copy, "Qt's standard line-edit menu must expose Copy");
    QPointer<QAction> preservedCopy(copy);
    const int originalActionCount = standard->actions().size();

    QPointer<QMenu> consumedMenu(standard);
    QScopedPointer<NlMenu> menu(NlMenu::fromStandardMenu(standard, &editor));

    QVERIFY(consumedMenu.isNull());
    QCOMPARE(menu->actions().size(), originalActionCount);
    QVERIFY(preservedCopy);
    QVERIFY(menu->actions().contains(preservedCopy));
    QCOMPARE(preservedCopy->parent(), menu.data());
    QVERIFY(menu->style() != QApplication::style());

    QApplication::clipboard()->clear();
    preservedCopy->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("nightlock"));
}

void NlMenuTests::nestedStandardMenusBecomeNlMenus() {
    auto* standard = new QMenu;
    QAction* direct = standard->addAction(QStringLiteral("Direct"));
    direct->setEnabled(false);
    direct->setToolTip(QStringLiteral("preserved tooltip"));
    QPointer<QAction> preservedDirect(direct);

    QMenu* standardSubmenu = standard->addMenu(QStringLiteral("Advanced"));
    standardSubmenu->menuAction()->setToolTip(QStringLiteral("submenu tooltip"));
    standardSubmenu->addAction(QStringLiteral("Nested command"));

    QScopedPointer<NlMenu> menu(NlMenu::fromStandardMenu(standard));
    QVERIFY(preservedDirect);
    QCOMPARE(preservedDirect->parent(), menu.data());
    QVERIFY(!preservedDirect->isEnabled());
    QCOMPARE(preservedDirect->toolTip(), QStringLiteral("preserved tooltip"));

    QAction* advanced = actionStartingWith(menu.data(), QStringLiteral("Advanced"));
    QVERIFY(advanced);
    auto* nested = qobject_cast<NlMenu*>(advanced->menu());
    QVERIFY2(nested, "Nested native QMenu must be recursively converted");
    QCOMPARE(advanced->toolTip(), QStringLiteral("submenu tooltip"));
    QCOMPARE(nested->actions().size(), 1);
    QCOMPARE(actionLabel(nested->actions().constFirst()),
             QStringLiteral("Nested command"));
}

void NlMenuTests::textAdapterHandlesStandardTextEditors() {
    NlMenu::installTextContextMenuAdapter(qApp);

    QLineEdit lineEdit(QStringLiteral("line"));
    lineEdit.resize(180, 30);
    lineEdit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&lineEdit));
    const QPoint linePoint(8, 8);
    QContextMenuEvent lineEvent(QContextMenuEvent::Mouse, linePoint,
                                lineEdit.mapToGlobal(linePoint));
    QApplication::sendEvent(&lineEdit, &lineEvent);
    QTRY_VERIFY(visibleNlMenu());
    QVERIFY(actionStartingWith(visibleNlMenu(), QStringLiteral("Select All")));
    closeVisibleNlMenu();

    QPlainTextEdit plainTextEdit(QStringLiteral("note"));
    plainTextEdit.resize(220, 90);
    plainTextEdit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plainTextEdit));
    QWidget* viewport = plainTextEdit.viewport();
    const QPoint viewportPoint(8, 8);
    QContextMenuEvent plainEvent(QContextMenuEvent::Mouse, viewportPoint,
                                 viewport->mapToGlobal(viewportPoint));
    QApplication::sendEvent(viewport, &plainEvent);
    QTRY_VERIFY(visibleNlMenu());
    QVERIFY(actionStartingWith(visibleNlMenu(), QStringLiteral("Select All")));
    closeVisibleNlMenu();

    QTextEdit textEdit(QStringLiteral("rich text"));
    textEdit.resize(220, 90);
    textEdit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&textEdit));
    viewport = textEdit.viewport();
    QContextMenuEvent textEvent(QContextMenuEvent::Mouse, viewportPoint,
                                viewport->mapToGlobal(viewportPoint));
    QApplication::sendEvent(viewport, &textEvent);
    QTRY_VERIFY(visibleNlMenu());
    QVERIFY(actionStartingWith(visibleNlMenu(), QStringLiteral("Select All")));
    closeVisibleNlMenu();
}

QTEST_MAIN(NlMenuTests)
#include "test_nlmenu.moc"
