#pragma once

#include <QMenu>
#include <QPixmap>

class QApplication;
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

    // Consumes a menu produced by createStandardContextMenu(), retaining its
    // already-wired actions while replacing the native popup surface. Nested
    // standard submenus are converted recursively.
    static NlMenu* fromStandardMenu(QMenu* standardMenu,
                                    QWidget* parent = nullptr);

    // Installs one application-wide adapter for the default QLineEdit,
    // QTextEdit and QPlainTextEdit context menus. The adapter is opt-in so
    // platforms with polished native menus can retain them.
    static void installTextContextMenuAdapter(QApplication* application);

    // popup() placing the visible panel corner (not the shadow margin)
    // at globalPos.
    void popupAt(const QPoint& globalPos);

    // Panel rect at the current reveal-animation stage: grows from the
    // top-left corner. The style clips item painting to it.
    QRect currentPanelRect() const;

    // Exposed for diagnostics/tests; true only after the popup HWND accepted
    // Windows 11's Desktop Acrylic backdrop.
    bool nativeBackdropActive() const { return nativeBackdropActive_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void captureBackdrop();
    void startRevealAnimation();

    QPixmap backdrop_;        // blurred snapshot of the window behind the panel
    QPoint backdropOffset_;   // offset of the snapshot inside the panel
    QVariantAnimation* revealAnimation_ = nullptr;
    bool nativeBackdropActive_ = false;
    qreal reveal_ = 1.0;      // 0..1 progress of the grow-in animation
};
