#pragma once

#include <QAbstractButton>

#include <nightlock/entry.hpp>

// Color dropdown matching Settings → Appearance → Accent color: a
// compact swatch, label, chevron and the shared frosted popup menu.
class EntryColorPicker : public QAbstractButton {
public:
    explicit EntryColorPicker(QWidget* parent = nullptr);

    nightlock::EntryColor value() const { return value_; }
    void setValue(nightlock::EntryColor color);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void openMenu();

    nightlock::EntryColor value_ = nightlock::EntryColor::None;
};
