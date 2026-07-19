#pragma once

#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QGridLayout;
class QToolButton;

// Exclusive grid of icon buttons for the entry dialog: the default
// icon, up to ten recently used pack icons, and a trailing "+" button
// that requests one more from the gallery (the chosen icon appears as
// an extra selectable button). Wraps into rows as the set grows.
class IconPicker : public QWidget {
    Q_OBJECT
public:
    explicit IconPicker(QWidget* parent = nullptr);

    // The value stored in Entry::icon: empty for the default icon, a
    // resource/file path otherwise.
    QString selectedIconValue() const;
    void setSelectedIconValue(const QString& value);

public slots:
    // Shows `path` as the custom-icon button and selects it.
    void setCustomIcon(const QString& path);

signals:
    // The "+" button was clicked; open a gallery and feed the choice
    // back through setCustomIcon().
    void addIconRequested();

private:
    void relayout();

    QStringList values_;  // per-button icon values, parallel to group ids
    QButtonGroup* buttons_;
    QGridLayout* grid_;
    QToolButton* plusButton_;
    QToolButton* customButton_ = nullptr;
    QString customPath_;
};
