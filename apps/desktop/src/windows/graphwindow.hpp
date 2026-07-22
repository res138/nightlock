#pragma once

#include <QBasicTimer>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <vector>

namespace nightlock {
class Group;
struct Entry;
}

class QToolButton;

// Obsidian-style graph of the vault. Every directory is a hub node:
// solid gray spokes to the entries directly inside it, dashed gray
// links to its sub-directories. Entries are also linked directly when
// their data matches (same login, same password, same URL base
// domain). Force-directed layout that stays warm: dragging a node
// tugs its whole neighborhood along.
class GraphWindow : public QWidget {
    Q_OBJECT
public:
    explicit GraphWindow(nightlock::Group* root, QWidget* parent = nullptr);

    // Re-derives nodes and edges from the vault. Surviving nodes keep
    // their positions; new entries spawn beside their directory hub.
    void refresh();

    // Zooms in on the entry's node and spotlights it with all its
    // connections for a few seconds.
    void focusEntry(nightlock::Entry* entry);

signals:
    // A node was clicked: reveal `group` (and `entry`, null for hub
    // nodes) in the main window. The graph is a snapshot — pointers
    // may be stale if the vault changed, so the receiver validates.
    void nodeActivated(nightlock::Group* group, nightlock::Entry* entry);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    // Match rules, in edge-color priority order (an edge matching
    // several rules is drawn in the strongest one's style). Directory
    // is the hub-entry spoke, Nesting the hub-hub hierarchy link.
    enum class Rule { Password, Login, Url, Directory, Nesting };

    struct Node {
        QString label;
        nightlock::Group* group = nullptr;  // set on hub nodes
        nightlock::Group* parent = nullptr; // entry's directory, or the hub's parent directory
        nightlock::Entry* entry = nullptr;  // set on entry nodes
        QPointF pos;
        QPointF vel;
        qreal radius = 5;
        qreal mass = 1;
        int degree = 0;
    };

    struct Edge {
        int a = 0;
        int b = 0;
        Rule rule = Rule::Directory;
        int strength = 1;  // how many rules matched this pair
    };

    void build(nightlock::Group* root);
    void simulate();
    int nodeAt(const QPointF& widgetPos) const;
    QPointF toScene(const QPointF& widgetPos) const;
    void zoomBy(qreal factor, const QPointF& anchor);
    void drawLegend(QPainter& painter);

    nightlock::Group* root_;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::vector<std::vector<int>> neighbors_;

    QBasicTimer timer_;
    qreal alpha_ = 1.0;  // simulation heat; reheated by interaction

    // View transform: widget = scene * scale_ + offset_.
    QPointF offset_;
    qreal scale_ = 1.0;
    bool viewInitialized_ = false;

    int hovered_ = -1;
    int focusNode_ = -1;   // last hovered node, kept while focus fades
    qreal focus_ = 0.0;    // 0 = plain view, 1 = neighborhood spotlight
    int focusHold_ = 0;    // ticks the Show-in-Graph spotlight stays lit
    QToolButton* zoomIn_;
    QToolButton* zoomOut_;
    int dragged_ = -1;
    bool panning_ = false;
    bool moved_ = false;   // drag exceeded the click threshold
    QPointF lastMouse_;
    QPointF pressPos_;
};
