#pragma once

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include <nightlock/entry.hpp>

// Bottom-most child of the detail-view content: an auto-generated
// decorative pattern behind the entry icon, spanning from the top of
// the panel down to the fields card. The palette is extracted from the
// entry icon and the geometry is seeded by the creation date, so the
// same entry always gets the same picture — nothing is stored on disk.
// Every edge dissolves through an elliptical alpha mask, so the layer
// never shows a hard crop.
class PatternBackdrop : public QWidget {
    Q_OBJECT
public:
    explicit PatternBackdrop(QWidget* parent = nullptr);

    // Reads the pattern kind, icon and seed off the entry. Safe to call
    // with nullptr (the layer just goes blank).
    void setEntry(const nightlock::Entry* entry);

    // Vertical center of the entry icon in this widget's coordinates;
    // the ripple variant radiates from it.
    void setIconCenterY(int y);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPixmap generate() const;

    nightlock::Pattern kind_ = nightlock::Pattern::None;
    QString iconPath_;        // resolved icon path (default when empty)
    quint64 seed_ = 0;        // std::hash of Entry::created
    int iconCenterY_ = 0;
    // Rendered patterns keyed by seed/kind/icon/DPR; cleared on resize or a
    // monitor-DPI change so no 1x cache is stretched after moving to 2x.
    mutable QHash<quint64, QPixmap> cache_;
};
