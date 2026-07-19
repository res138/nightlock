#pragma once

#include <QObject>

class QAbstractScrollArea;
class QTimer;

// macOS-style transient scrollbars: the handle only shows while
// scrolling (driven by the [scrolling] stylesheet property) and fades
// away shortly after the wheel stops.
class ScrollBarFader : public QObject {
public:
    explicit ScrollBarFader(QAbstractScrollArea* area);

private:
    void setActive(QAbstractScrollArea* area, bool active);

    QTimer* timer_;
};
