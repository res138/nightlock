#pragma once

#include <QPixmap>
#include <QWidget>

class QAbstractListModel;
class QLabel;
class QListView;
class QModelIndex;
class QResizeEvent;
class QTabBar;

// Frosted popup (same glass look as NlMenu) for browsing the installed icon
// library.  Its two compact tab rows select a pack and a normalized category;
// the grid below always shows exactly that slice of the library.
class IconGalleryPopup : public QWidget {
    Q_OBJECT
public:
    explicit IconGalleryPopup(QWidget* parent = nullptr);

    // show() placing the visible panel corner (not the shadow margin)
    // at globalPos, clamped to the screen.
    void popupAt(const QPoint& globalPos);

signals:
    // Canonical nightlock-icon:// reference; the gallery never exposes a
    // machine-specific downloaded-pack path to persistence code.
    void iconSelected(const QString& value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    friend struct IconGalleryPopupTestAccess;

    void fitHeightToAvailableGeometry(const QRect& available);
    static QPoint clampedCorner(const QPoint& desiredCorner,
                                const QRect& available,
                                const QSize& popupSize);
    void reloadPackTabs();
    void reloadCategoryTabs();
    void reloadIcons();
    void activateIcon(const QModelIndex& index);
    void placeFades();

    QPixmap backdrop_;
    QPoint backdropOffset_;
    QTabBar* packTabs_ = nullptr;
    QTabBar* categoryTabs_ = nullptr;
    QListView* view_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QAbstractListModel* model_ = nullptr;
    QWidget* topFade_ = nullptr;     // blur bands over the grid's top and
    QWidget* bottomFade_ = nullptr;  // bottom rims
    bool selectionCommitted_ = false;
};
