#pragma once

#include <QWidget>

#include "standardicons.hpp"

class QButtonGroup;
class QGridLayout;
class QToolButton;

// Exclusive grid of icon buttons for the entry dialog: the standard
// catalog plus a trailing "+" button that requests one more icon from
// the pack gallery (the chosen icon appears as an extra selectable
// button). Wraps into rows, so a larger catalog needs no changes here.
class IconPicker : public QWidget {
    Q_OBJECT
public:
    explicit IconPicker(const QVector<standardicons::StandardIcon>& icons,
                        QWidget* parent = nullptr);

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

    QVector<standardicons::StandardIcon> icons_;
    QButtonGroup* buttons_;
    QGridLayout* grid_;
    QToolButton* plusButton_;
    QToolButton* customButton_ = nullptr;
    QString customPath_;
};
