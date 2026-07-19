#pragma once

#include <QWidget>

#include "standardicons.hpp"

class QButtonGroup;

// Exclusive grid of standard-icon buttons for the entry dialog. Renders
// whatever the catalog provides and wraps into rows, so a much larger
// icon set later needs no changes here.
class IconPicker : public QWidget {
    Q_OBJECT
public:
    explicit IconPicker(const QVector<standardicons::StandardIcon>& icons,
                        QWidget* parent = nullptr);

    QString selectedId() const;
    void setSelectedId(const QString& id);

signals:
    void selectionChanged(const QString& id);

private:
    QVector<standardicons::StandardIcon> icons_;
    QButtonGroup* buttons_;
};
