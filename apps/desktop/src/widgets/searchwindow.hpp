#pragma once

#include <QIcon>
#include <QString>
#include <QWidget>

#include <vector>

namespace nightlock {
class Group;
struct Entry;
}

class QLineEdit;
class QLabel;
class QListWidget;

// Standalone "Search Entry" window (regular system frame and traffic
// lights): one field filtering every entry in the vault by name,
// login, URL or note (case-insensitive substring). Picking a result
// emits entryChosen() and closes.
class SearchWindow : public QWidget {
    Q_OBJECT
public:
    explicit SearchWindow(nightlock::Group* root, QWidget* parent = nullptr);

    // Debug hook: pre-fills the query as if the user typed it.
    void setQuery(const QString& text);

signals:
    void entryChosen(nightlock::Group* group, nightlock::Entry* entry);

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    struct Hit {
        nightlock::Group* group;
        nightlock::Entry* entry;
        QIcon icon;        // the entry's chosen icon (default when unset)
        QString name;
        QString sub;       // "Root/Work/Meta · login"
        QString haystack;  // lowercased name+login+url+note
    };

    void rebuildIndex();  // re-flattens the vault, so results stay current
    void refilter(const QString& text);
    void choose(int row);

    nightlock::Group* root_;
    std::vector<Hit> all_;    // every entry in the vault
    std::vector<int> hits_;   // indices into all_, parallel to list rows
    QLineEdit* field_;
    QListWidget* list_;
    QLabel* empty_;
    QWidget* topFade_;     // white gradients melting scrolled rows
    QWidget* bottomFade_;  // at the list edges
};
