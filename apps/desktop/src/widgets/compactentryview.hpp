#pragma once

#include <QPointer>
#include <QTableView>

class CompactEntryProxyModel;
class EntryListModel;

// Responsive table used by Compact Mode. It presents the regular
// one-column EntryListModel as Name/Login/Password/URL/Note/Date while
// preserving source indexes for the rest of MainWindow.
class CompactEntryView : public QTableView {
    Q_OBJECT
public:
    explicit CompactEntryView(QWidget* parent = nullptr);
    ~CompactEntryView() override;

    void setSourceModel(EntryListModel* model);
    EntryListModel* sourceModel() const;

    // All points passed to/returned by these helpers use viewport
    // coordinates, matching QAbstractItemView::indexAt().
    QModelIndex sourceIndexAt(const QPoint& viewportPos) const;
    QModelIndex currentSourceIndex() const;
    QModelIndexList selectedSourceIndexes() const;
    // Restores a source-model row selection without collapsing it when
    // assigning the current row. If currentSourceIndex is invalid, the
    // existing current row is retained when it still belongs to the
    // source model; otherwise the first restored row becomes current.
    void setSelectedSourceIndexes(
        const QModelIndexList& sourceIndexes,
        const QModelIndex& currentSourceIndex = {});
    void setCurrentSourceIndex(const QModelIndex& sourceIndex);
    QPoint viewportGlobal(const QPoint& viewportPos) const;
    void scrollTo(const QModelIndex& index,
                  ScrollHint hint = EnsureVisible) override;

signals:
    void contextMenuRequested(const QPoint& viewportPos);
    void currentSourceChanged(const QModelIndex& current,
                              const QModelIndex& previous);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    void layoutColumns();
    void scheduleInteractiveRefresh();
    void refreshInteractiveCells();
    void clearInteractiveCells();

    CompactEntryProxyModel* compactModel_;
    bool layingOutColumns_ = false;
    bool interactiveRefreshPending_ = false;
    int interactiveFirstRow_ = -1;
    int interactiveLastRow_ = -1;
    QPointer<QWidget> interactivePressTarget_;
    bool interactiveGestureActive_ = false;
    bool interactiveGestureDragged_ = false;
    bool forwardingInteractiveMouse_ = false;
};
