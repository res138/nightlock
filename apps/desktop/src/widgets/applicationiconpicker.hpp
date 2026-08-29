#pragma once

#include <QString>
#include <QWidget>

class QButtonGroup;

// Two (and later more) rendered application icons in one exclusive,
// keyboard-accessible row. This deliberately is not a dropdown: users see
// the actual Dock/taskbar artwork before choosing it.
class ApplicationIconPicker : public QWidget {
    Q_OBJECT
public:
    explicit ApplicationIconPicker(const QString& selectedId,
                                   QWidget* parent = nullptr);

    QString selectedIconId() const;
    void setSelectedIconId(const QString& id);

signals:
    void iconSelected(const QString& id);

private:
    QButtonGroup* buttons_;
};
