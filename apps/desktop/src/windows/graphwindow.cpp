#include "graphwindow.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimerEvent>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include <nightlock/group.hpp>

#include "appearancesettings.hpp"
#include "graphsettings.hpp"

namespace {

constexpr qreal kGoldenAngle = 2.39996;  // radians; spreads spiral placements

// Edge styling per rule; password reuse is the loudest signal.
const QColor kEdgePassword(0xD2, 0x60, 0x5E);
const QColor kEdgeLogin(0x6E, 0x93, 0xD6);
const QColor kEdgeUrl(0x5F, 0xA5, 0x7F);

// Theme-following paints, resolved on every frame.
QColor canvasColor() { return appearancesettings::palette().canvas; }
QColor inkColor() { return appearancesettings::palette().ink; }
QColor labelColor() { return appearancesettings::palette().muted; }
QColor directoryEdgeColor() {
    return appearancesettings::darkActive() ? QColor(0x55, 0x54, 0x5C)
                                            : QColor(0xCB, 0xC7, 0xD0);
}

// One width for every link — except the triple match below.
constexpr qreal kEdgeWidth = 0.71;

// Entry-rule bits for Edge::rules.
constexpr int kMatchPassword = 1;
constexpr int kMatchLogin = 2;
constexpr int kMatchUrl = 4;
constexpr int kMatchAll = kMatchPassword | kMatchLogin | kMatchUrl;

// Rule combinations get their own colors; the full triple is the
// loudest signal and also draws 2.5× thicker.
const QColor kEdgeLoginPassword(0x9A, 0x6E, 0xD6);  // violet
const QColor kEdgeLoginUrl(0xE0, 0x8A, 0x4E);       // orange
const QColor kEdgePasswordUrl(0xD9, 0xA8, 0x3C);    // orange-yellow
const QColor kEdgeTriple(0x8E, 0x2D, 0x44);         // burgundy

QColor matchColor(int rules) {
    switch (rules) {
        case kMatchPassword: return kEdgePassword;
        case kMatchLogin: return kEdgeLogin;
        case kMatchUrl: return kEdgeUrl;
        case kMatchLogin | kMatchPassword: return kEdgeLoginPassword;
        case kMatchLogin | kMatchUrl: return kEdgeLoginUrl;
        case kMatchPassword | kMatchUrl: return kEdgePasswordUrl;
        case kMatchAll: return kEdgeTriple;
    }
    return directoryEdgeColor();
}

// "https://accounts.google.com/x" -> "google.com". Naive two-label
// cut, good enough for vault URLs.
QString baseDomain(const std::string& url) {
    if (url.empty())
        return {};
    QString host = QUrl::fromUserInput(QString::fromStdString(url)).host().toLower();
    if (host.startsWith(QLatin1String("www.")))
        host = host.mid(4);
    const QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() <= 2)
        return host;
    return labels.mid(labels.size() - 2).join(QLatin1Char('.'));
}

}  // namespace

GraphWindow::GraphWindow(nightlock::Group* root, QWidget* parent)
    : QWidget(parent), root_(root) {
    setWindowTitle(tr("NetGraph"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);  // +/- zoom needs key events

    // Zoom steppers in the top-right corner; the keyboard and wheel
    // routes land in the same zoomBy().
    const auto makeZoomButton = [this](const QString& text, const QString& toolTip) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("graphZoomButton"));
        button->setText(text);
        button->setToolTip(toolTip);
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    zoomIn_ = makeZoomButton(QStringLiteral("+"), tr("Zoom in"));
    zoomOut_ = makeZoomButton(QStringLiteral("−"), tr("Zoom out"));
    focusRoot_ = makeZoomButton(QStringLiteral("/"), tr("Center on Root"));
    // The slash glyph runs the full ascent, so it renders larger than
    // the +/− at the same font size; a smaller size evens them out.
    focusRoot_->setProperty("glyph", QStringLiteral("slash"));
    connect(zoomIn_, &QToolButton::clicked, this,
            [this] { zoomBy(1.25, QPointF(width() / 2.0, height() / 2.0)); });
    connect(zoomOut_, &QToolButton::clicked, this,
            [this] { zoomBy(0.8, QPointF(width() / 2.0, height() / 2.0)); });
    // "/" pans to the vault root's hub and spotlights it briefly, a
    // way back home after wandering off across the graph.
    connect(focusRoot_, &QToolButton::clicked, this, [this] {
        for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
            if (nodes_[i].group != root_)
                continue;
            offset_ = QPointF(width() / 2.0, height() / 2.0) - nodes_[i].pos * scale_;
            focusNode_ = i;
            focus_ = 1.0;
            focusHold_ = 22;
            update();
            return;
        }
    });

    // A knob change on Settings → NetGraph re-warms the simulation so
    // the new forces re-layout the graph right away.
    connect(graphsettings::notifier(), &graphsettings::Notifier::changed, this, [this] {
        alpha_ = std::max(alpha_, 0.5);
        update();
    });

    build(root);
    // Pre-settle the layout so the window opens onto a formed graph
    // (alpha stays warm enough for a visible final shuffle).
    for (int i = 0; i < 200; ++i)
        simulate();
}

void GraphWindow::build(nightlock::Group* root) {
    // Per-entry match keys, aligned with nodes_ (empty for hubs).
    struct Keys {
        QString login;
        QString password;
        QString domain;
    };
    std::vector<Keys> keys;
    int hubCount = 0;

    std::function<void(nightlock::Group*, int)> walk = [&](nightlock::Group* group,
                                                           int parentHub) {
        const int hub = static_cast<int>(nodes_.size());
        Node hubNode;
        hubNode.label = QString::fromStdString(group->name());
        hubNode.group = group;
        hubNode.parent = group->parent();
        hubNode.mass = 2.2;
        const qreal angle = hubCount * kGoldenAngle;
        const qreal radius = 90.0 + 34.0 * hubCount;
        hubNode.pos = QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
        ++hubCount;
        if (parentHub >= 0)
            edges_.push_back({parentHub, hub, Rule::Nesting, 0});
        nodes_.push_back(hubNode);
        keys.emplace_back();

        int spoke = 0;
        for (const auto& entry : group->entries()) {
            Node node;
            node.label = QString::fromStdString(entry->name);
            node.entry = entry.get();
            node.parent = group;
            const qreal spokeAngle = spoke * kGoldenAngle + hubCount;
            node.pos = nodes_[hub].pos +
                       QPointF(std::cos(spokeAngle), std::sin(spokeAngle)) *
                           (34.0 + 3.0 * spoke);
            edges_.push_back({hub, static_cast<int>(nodes_.size()), Rule::Directory, 0});
            nodes_.push_back(node);
            keys.push_back({QString::fromStdString(entry->login).toLower(),
                            QString::fromStdString(entry->password),
                            baseDomain(entry->url)});
            ++spoke;
        }
        for (const auto& sub : group->groups())
            walk(sub.get(), hub);
    };
    walk(root, -1);

    // Direct entry-entry edges. A pair matching several rules gets one
    // edge carrying the whole rule set — drawn in the blended color.
    const int count = static_cast<int>(nodes_.size());
    for (int i = 0; i < count; ++i) {
        if (!nodes_[i].entry)
            continue;
        for (int j = i + 1; j < count; ++j) {
            if (!nodes_[j].entry)
                continue;
            const bool password =
                !keys[i].password.isEmpty() && keys[i].password == keys[j].password;
            const bool login = !keys[i].login.isEmpty() && keys[i].login == keys[j].login;
            const bool url = !keys[i].domain.isEmpty() && keys[i].domain == keys[j].domain;
            const int rules = (password ? kMatchPassword : 0) | (login ? kMatchLogin : 0) |
                              (url ? kMatchUrl : 0);
            if (!rules)
                continue;
            const Rule rule = password ? Rule::Password : login ? Rule::Login : Rule::Url;
            edges_.push_back({i, j, rule, rules});
        }
    }

    neighbors_.assign(nodes_.size(), {});
    for (const Edge& edge : edges_) {
        neighbors_[edge.a].push_back(edge.b);
        neighbors_[edge.b].push_back(edge.a);
        ++nodes_[edge.a].degree;
        ++nodes_[edge.b].degree;
    }
    // One dot size for every element — entries and directories alike;
    // hubs stand out by their bullseye rendering only.
    for (Node& node : nodes_)
        node.radius = 5.6;
}

void GraphWindow::refresh() {
    // Remember where every current node sits, keyed by its vault
    // object, so an edit reshuffles edges without scattering the map.
    std::unordered_map<const void*, std::pair<QPointF, QPointF>> kept;
    for (const Node& node : nodes_) {
        const void* key = node.entry ? static_cast<const void*>(node.entry)
                                     : static_cast<const void*>(node.group);
        kept[key] = {node.pos, node.vel};
    }
    nodes_.clear();
    edges_.clear();
    neighbors_.clear();
    build(root_);

    std::unordered_map<const nightlock::Group*, int> hubs;
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i)
        if (nodes_[i].group)
            hubs[nodes_[i].group] = i;
    for (Node& node : nodes_) {
        const void* key = node.entry ? static_cast<const void*>(node.entry)
                                     : static_cast<const void*>(node.group);
        const auto it = kept.find(key);
        if (it != kept.end()) {
            node.pos = it->second.first;
            node.vel = it->second.second;
        }
    }
    // Brand-new nodes (entries and directories alike) start beside
    // their parent hub and let the springs pull them into place.
    int fresh = 0;
    for (Node& node : nodes_) {
        const void* key = node.entry ? static_cast<const void*>(node.entry)
                                     : static_cast<const void*>(node.group);
        if (kept.count(key))
            continue;
        const auto hub = hubs.find(node.parent);
        if (hub == hubs.end())  // the root hub has no parent
            continue;
        ++fresh;
        node.pos = nodes_[hub->second].pos +
                   QPointF(std::cos(fresh * kGoldenAngle), std::sin(fresh * kGoldenAngle)) * 30.0;
        node.vel = QPointF();
    }

    dragged_ = -1;  // indices into the old node list are void now
    hovered_ = -1;
    focusNode_ = -1;
    focus_ = 0.0;
    alpha_ = std::max(alpha_, 0.5);
    update();
}

void GraphWindow::focusEntry(nightlock::Entry* entry) {
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        if (nodes_[i].entry != entry)
            continue;
        // Land right in the node's neighborhood, not on the full-graph
        // overview, and keep it and all its links spotlit for ~4s.
        scale_ = std::max(scale_, 1.6);
        offset_ = QPointF(width() / 2.0, height() / 2.0) - nodes_[i].pos * scale_;
        focusNode_ = i;
        focus_ = 1.0;
        focusHold_ = 22;  // 0.35 seconds of 16ms ticks
        update();
        return;
    }
}

// One physics step. Forces are scaled by alpha_, which cools after
// every interaction but keeps a warm floor so the graph answers a
// drag instantly instead of waking up.
void GraphWindow::simulate() {
    const int count = static_cast<int>(nodes_.size());
    if (count == 0)
        return;
    // The Obsidian-style knobs from Settings → NetGraph, mapped onto
    // this simulation's units.
    const graphsettings::Config config = graphsettings::config();
    const qreal repulsion = config.repelForce * 300.0;
    const qreal spring = config.linkForce * 0.04;
    const qreal gravity = config.centerForce * 0.017;
    constexpr qreal kDamping = 0.88;
    constexpr qreal kMaxSpeed = 16.0;

    std::vector<QPointF> force(count);
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            QPointF d = nodes_[j].pos - nodes_[i].pos;
            qreal dist2 = QPointF::dotProduct(d, d);
            if (dist2 < 1e-3) {  // coincident nodes: deterministic nudge
                d = QPointF(0.1 * (i % 7 - 3), 0.1 * (j % 5 - 2) + 0.05);
                dist2 = QPointF::dotProduct(d, d);
            }
            const qreal dist = std::sqrt(dist2);
            const QPointF push = d / dist * (repulsion / dist2);
            force[i] -= push;
            force[j] += push;
        }
    }
    for (const Edge& edge : edges_) {
        const QPointF d = nodes_[edge.b].pos - nodes_[edge.a].pos;
        const qreal dist = std::max<qreal>(1e-3, std::hypot(d.x(), d.y()));
        // Entry spokes rest short so directories stay tight clusters;
        // hierarchy and rule links stretch between them.
        const qreal rest = config.linkDistance * (edge.rule == Rule::Directory ? 0.7 : 1.3);
        const QPointF pull = d / dist * (spring * (dist - rest));
        force[edge.a] += pull;
        force[edge.b] -= pull;
    }
    for (int i = 0; i < count; ++i) {
        Node& node = nodes_[i];
        if (i == dragged_) {
            node.vel = QPointF();
            continue;
        }
        force[i] -= node.pos * (gravity * node.mass);
        node.vel = (node.vel + force[i] * (alpha_ / node.mass)) * kDamping;
        const qreal speed = std::hypot(node.vel.x(), node.vel.y());
        if (speed > kMaxSpeed)
            node.vel *= kMaxSpeed / speed;
        node.pos += node.vel;
    }
    alpha_ = std::max(0.06, alpha_ * 0.996);
}

QPointF GraphWindow::toScene(const QPointF& widgetPos) const {
    return (widgetPos - offset_) / scale_;
}

int GraphWindow::nodeAt(const QPointF& widgetPos) const {
    const QPointF scene = toScene(widgetPos);
    for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; --i) {
        const QPointF d = nodes_[i].pos - scene;
        const qreal reach = nodes_[i].radius + 4.0;
        if (QPointF::dotProduct(d, d) <= reach * reach)
            return i;
    }
    return -1;
}

void GraphWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), canvasColor());
    painter.setRenderHint(QPainter::Antialiasing);

    painter.save();
    painter.translate(offset_);
    painter.scale(scale_, scale_);

    // Spotlight: the focused node and its neighborhood stay solid,
    // the rest of the graph sinks back.
    const qreal dim = 1.0 - 0.82 * focus_;
    auto inSpotlight = [this](int i) {
        if (focusNode_ < 0)
            return true;
        if (i == focusNode_)
            return true;
        const auto& near = neighbors_[focusNode_];
        return std::find(near.begin(), near.end(), i) != near.end();
    };

    for (const Edge& edge : edges_) {
        const bool structural =
            edge.rule == Rule::Directory || edge.rule == Rule::Nesting;
        QColor color = structural ? directoryEdgeColor() : matchColor(edge.rules);
        const bool lit =
            focusNode_ >= 0 && (edge.a == focusNode_ || edge.b == focusNode_);
        qreal alpha = structural ? 0.55 : 0.72;
        alpha *= lit ? 1.0 : dim;
        color.setAlphaF(alpha);
        QPen pen(color, edge.rules == kMatchAll ? kEdgeWidth * 2.5 : kEdgeWidth);
        if (edge.rule == Rule::Nesting)
            pen.setDashPattern({4.0, 3.0});
        painter.setPen(pen);
        painter.drawLine(nodes_[edge.a].pos, nodes_[edge.b].pos);
    }

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        const Node& node = nodes_[i];
        QColor ink = inkColor();
        ink.setAlphaF(inSpotlight(i) ? 1.0 : dim);
        painter.setBrush(ink);
        painter.drawEllipse(node.pos, node.radius, node.radius);
        if (node.group) {
            // Bullseye hub: black core (70% of the radius), white ring
            // (21%), black rim (9%) closing the dot.
            QColor white(canvasColor());  // the ring punches through to the canvas
            white.setAlphaF(ink.alphaF());
            painter.setBrush(white);
            painter.drawEllipse(node.pos, node.radius * 0.91, node.radius * 0.91);
            painter.setBrush(ink);
            painter.drawEllipse(node.pos, node.radius * 0.70, node.radius * 0.70);
        }
        if (i == dragged_) {  // hover alone stays ring-free
            QColor ring = inkColor();
            ring.setAlphaF(0.35);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(ring, 1.6));
            painter.drawEllipse(node.pos, node.radius + 2.5, node.radius + 2.5);
            painter.setPen(Qt::NoPen);
        }
    }

    // Labels surface with zoom (hubs a step earlier) and always for
    // the spotlit neighborhood.
    const qreal zoomLabel = std::clamp((scale_ - 0.5) / 0.4, 0.0, 1.0);
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) {
        const Node& node = nodes_[i];
        const bool member = inSpotlight(i);
        qreal alpha = node.group ? std::max(0.55, zoomLabel) : zoomLabel;
        if (member && focusNode_ >= 0)
            alpha = std::max(alpha, focus_);
        alpha *= member ? 1.0 : dim * 0.6;
        if (alpha < 0.03)
            continue;
        QFont font = painter.font();
        font.setPixelSize(node.group ? 10 : 9);
        font.setWeight(node.group ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        QColor color = node.group ? inkColor() : labelColor();
        color.setAlphaF(alpha);
        painter.setPen(color);
        painter.drawText(
            QRectF(node.pos.x() - 70, node.pos.y() + node.radius + 3, 140, 14),
            Qt::AlignHCenter | Qt::AlignTop, node.label);
    }
    painter.restore();

    drawLegend(painter);
}

void GraphWindow::drawLegend(QPainter& painter) {
    // Only kinds that actually occur in the current graph get a row.
    bool hasDirectory = false;
    bool hasNesting = false;
    bool present[kMatchAll + 1] = {};
    for (const Edge& edge : edges_) {
        if (edge.rule == Rule::Directory)
            hasDirectory = true;
        else if (edge.rule == Rule::Nesting)
            hasNesting = true;
        else
            present[edge.rules] = true;
    }

    struct Row {
        QString text;
        QColor color;
        bool dashed;
        bool thick;
    };
    QVector<Row> rows;
    if (hasDirectory)
        rows.append({tr("Directory"), directoryEdgeColor(), false, false});
    if (hasNesting)
        rows.append({tr("Subdirectory"), directoryEdgeColor(), true, false});
    const auto addMatchRow = [&](const QString& text, int mask) {
        if (present[mask])
            rows.append({text, matchColor(mask), false, mask == kMatchAll});
    };
    addMatchRow(tr("Same login"), kMatchLogin);
    addMatchRow(tr("Same password"), kMatchPassword);
    addMatchRow(tr("Same URL"), kMatchUrl);
    addMatchRow(tr("Same login, password"), kMatchLogin | kMatchPassword);
    addMatchRow(tr("Same login, URL"), kMatchLogin | kMatchUrl);
    addMatchRow(tr("Same password, URL"), kMatchPassword | kMatchUrl);
    addMatchRow(tr("Same login, password, URL"), kMatchAll);
    if (rows.isEmpty())
        return;

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setWeight(QFont::Normal);
    painter.setFont(font);

    // Plain white card behind the rows: square corners, hairline gray
    // border, the same offset from the left and the top edge.
    constexpr int kMargin = 14;
    constexpr int kRowHeight = 20;
    const QFontMetrics metrics(font);
    int textWidth = 0;
    for (const Row& row : rows)
        textWidth = std::max(textWidth, metrics.horizontalAdvance(row.text));
    painter.setPen(QPen(appearancesettings::palette().borderStrong, 1));
    painter.setBrush(appearancesettings::palette().window);
    painter.drawRect(
        QRect(kMargin, kMargin, 38 + textWidth + 12, 8 + rows.size() * kRowHeight));

    int y = kMargin + 14;
    for (const Row& row : rows) {
        QPen pen(row.color, row.thick ? 5 : 2);  // triple matches draw thicker
        if (row.dashed)
            pen.setDashPattern({3.0, 2.5});
        painter.setPen(pen);
        painter.drawLine(QPointF(kMargin + 8, y), QPointF(kMargin + 30, y));
        painter.setPen(labelColor());
        painter.drawText(QPointF(kMargin + 38, y + 4), row.text);
        y += kRowHeight;
    }
}

void GraphWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    pressPos_ = event->position();
    lastMouse_ = pressPos_;
    moved_ = false;
    dragged_ = nodeAt(pressPos_);
    if (dragged_ >= 0)
        alpha_ = std::max(alpha_, 0.4);
    else
        panning_ = true;
}

void GraphWindow::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if ((pos - pressPos_).manhattanLength() > 4)
        moved_ = moved_ || (event->buttons() & Qt::LeftButton);
    if (dragged_ >= 0) {
        nodes_[dragged_].pos = toScene(pos);
        nodes_[dragged_].vel = QPointF();
        hovered_ = dragged_;
        alpha_ = std::max(alpha_, 0.35);
    } else if (panning_) {
        offset_ += pos - lastMouse_;
    } else {
        hovered_ = nodeAt(pos);
        setCursor(hovered_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    lastMouse_ = pos;
}

void GraphWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    if (!moved_) {
        const int hit = nodeAt(event->position());
        if (hit >= 0) {
            const Node& node = nodes_[hit];
            emit nodeActivated(node.group ? node.group : node.parent, node.entry);
        }
    }
    if (dragged_ >= 0)
        alpha_ = std::max(alpha_, 0.5);  // let the neighborhood resettle
    dragged_ = -1;
    panning_ = false;
}

void GraphWindow::zoomBy(qreal factor, const QPointF& anchor) {
    const QPointF scene = toScene(anchor);
    scale_ = std::clamp(scale_ * factor, 0.25, 4.0);
    offset_ = anchor - scene * scale_;  // the anchor point stays put
    update();
}

void GraphWindow::wheelEvent(QWheelEvent* event) {
    zoomBy(std::pow(1.0015, event->angleDelta().y()), event->position());
}

// Plus/minus step the zoom about the window center; plain keys and
// the ⌘+/⌘- shortcuts land here alike.
void GraphWindow::keyPressEvent(QKeyEvent* event) {
    const QPointF center(width() / 2.0, height() / 2.0);
    switch (event->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomBy(1.25, center);
            break;
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
            zoomBy(0.8, center);
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void GraphWindow::leaveEvent(QEvent*) {
    hovered_ = -1;
    setCursor(Qt::ArrowCursor);
}

void GraphWindow::showEvent(QShowEvent*) {
    if (!viewInitialized_) {
        // Fit the pre-settled graph into the window, with air around
        // it for labels and the last bit of drift.
        QRectF box;
        for (const Node& node : nodes_)
            box |= QRectF(node.pos, QSizeF()).adjusted(-node.radius, -node.radius,
                                                       node.radius, node.radius);
        box.adjust(-50, -50, 50, 50);
        const QPointF center(width() / 2.0, height() / 2.0);
        if (box.isEmpty()) {
            offset_ = center;
        } else {
            scale_ = std::clamp(
                std::min(width() / box.width(), height() / box.height()), 0.25, 1.4);
            offset_ = center - box.center() * scale_;
        }
        viewInitialized_ = true;
    }
    alpha_ = std::max(alpha_, 0.4);
    timer_.start(16, this);
}

void GraphWindow::hideEvent(QHideEvent*) {
    timer_.stop();
}

void GraphWindow::resizeEvent(QResizeEvent* event) {
    if (viewInitialized_ && event->oldSize().isValid())
        offset_ += QPointF((event->size().width() - event->oldSize().width()) / 2.0,
                           (event->size().height() - event->oldSize().height()) / 2.0);
    zoomIn_->move(width() - 14 - zoomIn_->width(), 14);
    zoomOut_->move(width() - 14 - zoomOut_->width(), 14 + zoomIn_->height() + 6);
    focusRoot_->move(width() - 14 - focusRoot_->width(), 14 + 2 * (zoomIn_->height() + 6));
    QWidget::resizeEvent(event);
}

void GraphWindow::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timer_.timerId()) {
        QWidget::timerEvent(event);
        return;
    }
    simulate();
    int focusTarget = dragged_ >= 0 ? dragged_ : hovered_;
    if (focusTarget >= 0) {
        focusHold_ = 0;  // hovering a node takes over the held spotlight
    } else if (focusHold_ > 0) {
        --focusHold_;
        focusTarget = focusNode_;
    }
    focus_ += ((focusTarget >= 0 ? 1.0 : 0.0) - focus_) * 0.18;
    if (focusTarget >= 0)
        focusNode_ = focusTarget;
    else if (focus_ < 0.02)
        focusNode_ = -1;
    update();
}
