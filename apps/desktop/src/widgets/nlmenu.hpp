#pragma once

#include <QMenu>
#include <QPixmap>

class QVariantAnimation;

// Telegram-style context menu: rounded frosted panel (blurred backdrop
// under a translucent white veil) with a soft shadow and a short
// grow-from-cursor reveal animation. Hairlines between adjacent items
// are drawn automatically; addSeparator() produces a thick group band.
// Mark destructive actions with action->setProperty("danger", true) —
// they render in red.
class NlMenu : public QMenu {
    Q_OBJECT
public:
    explicit NlMenu(QWidget* parent = nullptr);

    // popup() placing the visible panel corner (not the shadow margin)
    // at globalPos.
    void popupAt(const QPoint& globalPos);

    // Panel rect at the current reveal-animation stage: grows from the
    // top-left corner. The style clips item painting to it.
    QRect currentPanelRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void captureBackdrop();
    void startRevealAnimation();

    QPixmap backdrop_;        // blurred snapshot of the window behind the panel
    QPoint backdropOffset_;   // offset of the snapshot inside the panel
    QVariantAnimation* revealAnimation_ = nullptr;
    qreal reveal_ = 1.0;      // 0..1 progress of the grow-in animation
};
