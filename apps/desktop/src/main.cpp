#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QMenu>
#include <QTimer>

#include <nightlock/group.hpp>

#include "appearancesettings.hpp"
#include "demovault.hpp"
#include "standardicons.hpp"
#include "windows/entryeditdialog.hpp"
#include "windows/mainwindow.hpp"

#ifdef Q_OS_MACOS
#include "platform/macwindow.hpp"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Nightlock"));
    QApplication::setApplicationName(QStringLiteral("Nightlock"));

    appearancesettings::applyStylesheet();

    // Dock icon for the running process (the squircle render of the
    // logo); resources/nightlock.icns carries the same art for a
    // future .app bundle.
    QApplication::setWindowIcon(QIcon(QStringLiteral(NIGHTLOCK_ICONS_DIR "/appicon.png")));

    auto vault = createDemoVault();

    MainWindow window(vault.get());
    // Content extends into the title bar zone: the panes and their
    // borders run from the very top edge of the window, with only the
    // traffic-light buttons floating above the directory pane.
    window.setWindowFlag(Qt::ExpandedClientAreaHint, true);
    window.setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
    // Without this the layout still reserves the title-bar safe area,
    // leaving a 28px white strip the pane borders never reach into.
    window.setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    window.resize(843, 617);
    window.show();
#ifdef Q_OS_MACOS
    macwindow::hideTitleBar(&window);
    // Center the traffic lights on the tree-pane header (46px tall,
    // same as MainWindow's kHeaderHeight).
    macwindow::layoutTrafficLights(&window, 20, 23);
#endif

    // Decode the icon packs up front (background thread), so the
    // gallery scrolls smoothly the first time it opens.
    standardicons::preloadGalleryIcons();
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     [] { standardicons::stopGalleryPreload(); });
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

    // Debug hook: NIGHTLOCK_TEST_ADD_ENTRY=<name> creates an entry in
    // the current folder (fresh Created/Modified).
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_ADD_ENTRY"))
        window.debugAddEntry(qEnvironmentVariable("NIGHTLOCK_TEST_ADD_ENTRY"));

    // Debug hook: NIGHTLOCK_TEST_ENTRY_PATTERN="[name:]kind[,name:kind…]"
    // assigns background patterns (glow-soft|glow-bold|icon-tile|
    // icon-tile-v2|icon-tile-v3|ripple|constellation|aurora|halo|none);
    // an omitted name targets the selected entry.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_ENTRY_PATTERN"))
        window.debugSetEntryPattern(qEnvironmentVariable("NIGHTLOCK_TEST_ENTRY_PATTERN"));

    // Debug hook: NIGHTLOCK_TEST_MOVE_ENTRY=<folder> moves the selected
    // entry into the named folder through the same path the "Move to"
    // context menu uses.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_MOVE_ENTRY"))
        window.debugMoveEntry(qEnvironmentVariable("NIGHTLOCK_TEST_MOVE_ENTRY"));

    // Debug hook: NIGHTLOCK_TEST_SPOILER=reveal|copied drives the
    // password spoiler states.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_SPOILER")) {
        const QString state = qEnvironmentVariable("NIGHTLOCK_TEST_SPOILER");
        QTimer::singleShot(500, &window,
                           [&window, state] { window.debugSpoiler(state); });
    }

    // Debug hook: NIGHTLOCK_TEST_FOLDERS=1 exercises folder create,
    // rename and delete through the tree model.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_FOLDERS"))
        window.debugFolderOps();

    // Debug hook: NIGHTLOCK_TEST_RENAME=<folder> opens the inline
    // rename editor on the named folder, as the context menu would.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_RENAME")) {
        const QString name = qEnvironmentVariable("NIGHTLOCK_TEST_RENAME");
        QTimer::singleShot(300, &window,
                           [&window, name] { window.debugRenameFolder(name); });
    }

    // Debug hook: NIGHTLOCK_TEST_SORT=custom|created|modified|site
    // applies a list sort mode through the filter-menu path.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_SORT"))
        window.debugSetSort(qEnvironmentVariable("NIGHTLOCK_TEST_SORT"));

    // Debug hook: NIGHTLOCK_TEST_TREE_PANE=hide|show[,hide|show…]
    // drives the folder-panel toggle button, 450ms between steps.
    // NIGHTLOCK_TEST_TREE_PANE_DELAY=<ms> postpones the first step,
    // e.g. to screenshot the slide animation mid-flight.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_TREE_PANE")) {
        const int delay = qEnvironmentVariableIntValue("NIGHTLOCK_TEST_TREE_PANE_DELAY");
        const QStringList states =
            qEnvironmentVariable("NIGHTLOCK_TEST_TREE_PANE").split(QLatin1Char(','));
        for (int i = 0; i < states.size(); ++i)
            window.debugSetTreePaneDelayed(states[i], delay + i * 450);
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_SORT_MENU=<path> saves the
    // opened sort menu and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_SORT_MENU")) {
        QTimer::singleShot(800, &window, [&window] {
            QMenu* menu = window.popupSortMenuForScreenshot();
            QTimer::singleShot(400, menu, [menu] {
                menu->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_SORT_MENU"));
                QApplication::quit();
            });
        });
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
                // NIGHTLOCK_SCREENSHOT_MENU_ACTIVE=<row> highlights an
                // item as if hovered. Set right before the grab: a stray
                // real mouse-over would reset it otherwise.
                if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_MENU_ACTIVE")) {
                    const int row =
                        qEnvironmentVariableIntValue("NIGHTLOCK_SCREENSHOT_MENU_ACTIVE");
                    if (row >= 0 && row < menu->actions().size())
                        menu->setActiveAction(menu->actions().at(row));
                }
                // NIGHTLOCK_SCREENSHOT_MENU_SUBMENU=<row> pops the
                // sub-menu of that item and captures it instead.
                if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_MENU_SUBMENU")) {
                    const int row =
                        qEnvironmentVariableIntValue("NIGHTLOCK_SCREENSHOT_MENU_SUBMENU");
                    QMenu* sub = row >= 0 && row < menu->actions().size()
                                     ? menu->actions().at(row)->menu()
                                     : nullptr;
                    if (sub) {
                        sub->popup(menu->pos() + QPoint(menu->width(), 40));
                        QTimer::singleShot(400, sub, [sub] {
                            sub->grab().save(
                                qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_MENU"));
                            QApplication::quit();
                        });
                        return;
                    }
                }
                menu->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_MENU"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_TEST_REATTACH=1 floats the detail view and
    // docks it back, exercising both transitions before a screenshot.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_REATTACH")) {
        QTimer::singleShot(400, &window, [&window] { window.debugDetachDetail(); });
        QTimer::singleShot(600, &window, [&window] { window.debugReattachDetail(); });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_DETACHED=<path> floats the
    // detail view, saves it and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_DETACHED")) {
        QTimer::singleShot(800, &window, [&window] {
            QWidget* detached = window.debugDetachDetail();
            QTimer::singleShot(400, detached, [detached] {
                detached->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_DETACHED"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_GRAPH=<path> opens the graph
    // window, lets the force layout settle and saves it.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_GRAPH")) {
        QTimer::singleShot(800, &window, [&window] {
            QWidget* graph = window.openGraphForScreenshot();
            QTimer::singleShot(2800, graph, [graph] {
                graph->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_GRAPH"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_TEST_GRAPH_REFRESH=<name> adds an entry
    // after the graph window has opened, exercising its live refresh.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_TEST_GRAPH_REFRESH")) {
        QTimer::singleShot(1600, &window, [&window] {
            window.debugAddEntry(qEnvironmentVariable("NIGHTLOCK_TEST_GRAPH_REFRESH"));
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_SEARCH=<path> opens the search
    // popup (pre-filled from NIGHTLOCK_SEARCH_QUERY when set), saves
    // it and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_SEARCH")) {
        QTimer::singleShot(800, &window, [&window] {
            QWidget* popup = window.openSearchForScreenshot(
                qEnvironmentVariable("NIGHTLOCK_SEARCH_QUERY"));
            QTimer::singleShot(400, popup, [popup] {
                popup->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_SEARCH"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_SETTINGS=<path> opens the
    // Settings window, saves it and exits. NIGHTLOCK_SETTINGS_PAGE=<n>
    // picks the category row first (0-based, default General).
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_SETTINGS")) {
        QTimer::singleShot(800, &window, [&window] {
            QWidget* settings = window.openSettingsForScreenshot(
                qEnvironmentVariableIntValue("NIGHTLOCK_SETTINGS_PAGE"));
            // NIGHTLOCK_SETTINGS_DROPDOWN=1 opens the page's first
            // dropdown and captures its frosted menu instead.
            if (qEnvironmentVariableIsSet("NIGHTLOCK_SETTINGS_DROPDOWN")) {
                QTimer::singleShot(300, settings, [settings] {
                    // Only the current page's dropdowns are visible.
                    const auto dropdowns = settings->findChildren<QAbstractButton*>(
                        QStringLiteral("settingsDropdown"));
                    for (QAbstractButton* dropdown : dropdowns) {
                        if (!dropdown->isVisible())
                            continue;
                        dropdown->click();
                        break;
                    }
                    QTimer::singleShot(400, settings, [] {
                        if (QWidget* popup = QApplication::activePopupWidget())
                            popup->grab().save(
                                qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_SETTINGS"));
                        QApplication::quit();
                    });
                });
                return;
            }
            QTimer::singleShot(400, settings, [settings] {
                settings->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_SETTINGS"));
                QApplication::quit();
            });
        });
    }

    // Debug hook: NIGHTLOCK_SCREENSHOT_LOCK=<path> locks the vault,
    // saves the window and exits. NIGHTLOCK_LOCK_FAIL=1 first submits
    // a wrong password to capture the error state.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_LOCK")) {
        QTimer::singleShot(800, &window, [&window] {
            window.debugLock(qEnvironmentVariableIsSet("NIGHTLOCK_LOCK_FAIL"));
            QTimer::singleShot(600, &window, [&window] {
                window.grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_LOCK"));
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

    // Debug hook: NIGHTLOCK_SCREENSHOT_PATTERN_MENU=<path> opens the
    // entry dialog, drops its pattern menu, saves the menu and exits.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_SCREENSHOT_PATTERN_MENU")) {
        QTimer::singleShot(800, &window, [&window] {
            auto* dialog =
                qobject_cast<EntryEditDialog*>(window.openEntryDialogForScreenshot());
            QMenu* menu = dialog->openPatternMenuForScreenshot();
            QTimer::singleShot(400, menu, [menu] {
                menu->grab().save(qEnvironmentVariable("NIGHTLOCK_SCREENSHOT_PATTERN_MENU"));
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
