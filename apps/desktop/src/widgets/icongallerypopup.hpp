#pragma once

#include <QPixmap>
#include <QWidget>

// Frosted popup (same glass look as NlMenu) with a flat scrollable grid
// of every icon from the bundled "P*" packs — no per-pack tabs, one
// merged wall of icons. Emits iconSelected() and closes on click.
class IconGalleryPopup : public QWidget {
    Q_OBJECT
public:
    explicit IconGalleryPopup(QWidget* parent = nullptr);

    // show() placing the visible panel corner (not the shadow margin)
    // at globalPos, clamped to the screen.
    void popupAt(const QPoint& globalPos);

signals:
    void iconSelected(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QPixmap backdrop_;
    QPoint backdropOffset_;
    QWidget* topFade_;     // blur bands over the grid's top and
    QWidget* bottomFade_;  // bottom rims
};
