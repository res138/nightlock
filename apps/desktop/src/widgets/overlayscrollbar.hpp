#pragma once

#include <QWidget>

class QAbstractScrollArea;
class QScrollBar;
class QTimer;
class QVariantAnimation;

// Transient overlay scroll bar: replaces an area's built-in vertical
// bar with a floating strip glued over the content's right edge (no
// layout width reserved), fading in on scroll activity and out after
// a short idle pause. Painted by hand — graphics effects are avoided
// on purpose, they glitch on translucent popup windows.
class OverlayScrollBar : public QWidget {
public:
    explicit OverlayScrollBar(QAbstractScrollArea* area);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QRectF handleRect() const;
    void place();
    void activity();
    void setShown(bool shown);

    QAbstractScrollArea* area_;
    QScrollBar* inner_;  // the area's own (hidden) bar holds the truth
    QVariantAnimation* fade_ = nullptr;
    QTimer* idle_;
    qreal opacity_ = 0.0;
    bool shown_ = false;
    bool dragging_ = false;
    qreal dragOffset_ = 0;  // grab point inside the handle
};
