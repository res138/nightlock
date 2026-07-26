#include "hotkeys.hpp"

#include <QSettings>
#include <QShortcut>
#include <QWidget>

namespace hotkeys {
namespace {

QString settingsKey(const QString& id) {
    return QStringLiteral("hotkeys/") + id;
}

QVector<Action>& registry() {
    static QVector<Action> actions;
    return actions;
}

}  // namespace

QShortcut* bind(const QString& id, const QString& title, const QKeySequence& defaultSequence,
                QWidget* parent, std::function<void()> callback) {
    QKeySequence sequence = defaultSequence;
    const QString saved = QSettings().value(settingsKey(id)).toString();
    if (!saved.isEmpty())
        sequence = QKeySequence::fromString(saved, QKeySequence::PortableText);

    auto* shortcut = new QShortcut(sequence, parent);
    QObject::connect(shortcut, &QShortcut::activated, parent, std::move(callback));
    registry().append({id, title, sequence, shortcut});
    return shortcut;
}

const QVector<Action>& actions() {
    return registry();
}

void setSequence(const QString& id, const QKeySequence& sequence) {
    for (Action& action : registry()) {
        if (action.id != id)
            continue;
        action.sequence = sequence;
        if (action.shortcut)
            action.shortcut->setKey(sequence);
        QSettings().setValue(settingsKey(id), sequence.toString(QKeySequence::PortableText));
        return;
    }
}

}  // namespace hotkeys
