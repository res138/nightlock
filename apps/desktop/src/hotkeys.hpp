#pragma once

#include <QKeySequence>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class QShortcut;
class QWidget;

// The app's rebindable keyboard shortcuts. MainWindow registers each
// action once through bind(); Settings → Hotkeys lists the registry
// and rebinds entries, which retargets the live QShortcut and
// persists the custom sequence across runs.
namespace hotkeys {

struct Action {
    QString id;                    // stable identifier, persisted
    QString title;                 // row title in Settings → Hotkeys
    QKeySequence sequence;         // current binding
    QPointer<QShortcut> shortcut;  // live shortcut, retargeted on rebind
};

// Creates a QShortcut on `parent` bound to the persisted (or default)
// sequence, records it in the registry and returns it. Registration
// order is the display order in Settings.
QShortcut* bind(const QString& id, const QString& title, const QKeySequence& defaultSequence,
                QWidget* parent, std::function<void()> callback);

const QVector<Action>& actions();

// Rebinds `id`: the registry entry, the live shortcut and the
// persisted value all move to `sequence`.
void setSequence(const QString& id, const QKeySequence& sequence);

}  // namespace hotkeys
