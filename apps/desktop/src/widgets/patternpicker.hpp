#pragma once

#include <QWidget>

#include <nightlock/entry.hpp>

class NlMenu;
class QVariantAnimation;

// Dropdown field of the entry dialog that picks the detail-view
// background pattern. Styled after the dialog's line edits; clicking
// it drops an animated NlMenu with the options (None + the generated
// looks) and swings the chevron while the menu is open.
class PatternPicker : public QWidget {
    Q_OBJECT
public:
    explicit PatternPicker(QWidget* parent = nullptr);

    nightlock::Pattern value() const { return value_; }
    void setValue(nightlock::Pattern value);

    // A random pickable pattern (never None) — the default for a
    // freshly created entry.
    static nightlock::Pattern randomOption();

    // Drops the option menu under the field; public so the screenshot
    // debug hook can open it without synthesizing a click.
    NlMenu* openMenu();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void animateChevron(bool open);

    nightlock::Pattern value_ = nightlock::Pattern::None;
    bool menuOpen_ = false;
    qreal chevronTurn_ = 0.0;  // 0 = closed … 1 = open (half-turn)
    QVariantAnimation* chevronAnimation_ = nullptr;
};
