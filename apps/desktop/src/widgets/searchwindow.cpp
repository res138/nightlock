#include "searchwindow.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <functional>

#include <nightlock/group.hpp>

#include "overlayscrollbar.hpp"
#include "standardicons.hpp"

namespace {

constexpr int kResultIconSize = 26;

// White gradient over the list's top/bottom edge, so scrolled rows
// fade out instead of being cut off.
class EdgeFade : public QWidget {
public:
    EdgeFade(bool top, QWidget* parent) : QWidget(parent), top_(top) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(18);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QLinearGradient gradient(0, top_ ? 0 : height(), 0, top_ ? height() : 0);
        gradient.setColorAt(0.0, QColor(255, 255, 255, 240));
        gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
        QPainter painter(this);
        painter.fillRect(rect(), gradient);
    }

private:
    bool top_;
};

// Two-line result row: the entry icon, then its name over path ·
// login, with a soft pill under the hovered/selected row.
class ResultDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->setRenderHint(QPainter::Antialiasing);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, option.state & QStyle::State_Selected ? 18 : 10));
            painter->drawRoundedRect(QRectF(option.rect).adjusted(2, 1, -2, -1), 8, 8);
        }
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        icon.paint(painter,
                   QRect(option.rect.left() + 10,
                         option.rect.top() + (option.rect.height() - kResultIconSize) / 2,
                         kResultIconSize, kResultIconSize));
        const QRect text = option.rect.adjusted(10 + kResultIconSize + 10, 6, -12, -7);
        QFont font = option.font;
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        painter->setPen(QColor(0x11, 0x11, 0x11));
        painter->drawText(text, Qt::AlignTop | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideRight, text.width()));
        font.setPixelSize(11);
        font.setWeight(QFont::Normal);
        painter->setFont(font);
        painter->setPen(QColor(0x8A, 0x87, 0x92));
        painter->drawText(text, Qt::AlignBottom | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(index.data(Qt::UserRole).toString(),
                                                        Qt::ElideRight, text.width()));
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, 46};
    }
};

}  // namespace

SearchWindow::SearchWindow(nightlock::Group* root, QWidget* parent)
    : QWidget(parent), root_(root) {
    setWindowFlag(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground);  // the qss white background
    setObjectName(QStringLiteral("searchWindow"));
    setWindowTitle(tr("Search Entry"));

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("searchField"));
    field_->setPlaceholderText(tr("Name, Login, URL or Note"));
    field_->setClearButtonEnabled(true);
    field_->setAttribute(Qt::WA_MacShowFocusRect, false);
    field_->installEventFilter(this);
    connect(field_, &QLineEdit::textChanged, this, &SearchWindow::refilter);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("searchResults"));
    list_->setItemDelegate(new ResultDelegate(list_));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setMouseTracking(true);
    list_->setFocusPolicy(Qt::NoFocus);  // keys stay in the field
    list_->setAttribute(Qt::WA_MacShowFocusRect, false);
    new OverlayScrollBar(list_);
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { choose(list_->row(item)); });

    // Scrolled rows melt into the window instead of being cut off.
    // Scroll shadows: each fade only shows when rows are actually
    // scrolled off on its side.
    topFade_ = new EdgeFade(true, list_);
    bottomFade_ = new EdgeFade(false, list_);
    list_->installEventFilter(this);  // keeps the fades glued on resize
    auto* vbar = list_->verticalScrollBar();
    const auto updateFades = [this, vbar] {
        topFade_->setVisible(vbar->value() > vbar->minimum());
        bottomFade_->setVisible(vbar->value() < vbar->maximum());
    };
    connect(vbar, &QAbstractSlider::valueChanged, this, updateFades);
    connect(vbar, &QAbstractSlider::rangeChanged, this, updateFades);
    updateFades();

    empty_ = new QLabel(tr("No matches"));
    empty_->setObjectName(QStringLiteral("searchEmpty"));
    empty_->setAlignment(Qt::AlignHCenter);
    empty_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 12);
    layout->setSpacing(10);
    layout->addWidget(field_);
    layout->addWidget(empty_);
    layout->addWidget(list_, 1);

    resize(380, 460);
    setMinimumSize(320, 300);

    refilter(QString());
}

// The vault is re-flattened on every keystroke (it is tiny), so an
// open search window never shows stale entries.
void SearchWindow::rebuildIndex() {
    all_.clear();
    std::function<void(nightlock::Group*)> walk = [&](nightlock::Group* group) {
        for (const auto& entry : group->entries()) {
            Hit hit;
            hit.group = group;
            hit.entry = entry.get();
            hit.name = QString::fromStdString(entry->name);
            const QString iconPath = QString::fromStdString(entry->icon);
            hit.icon = iconPath.isEmpty()
                           ? QIcon(standardicons::defaultEntryIcon().resource)
                           : QIcon(iconPath);
            const QString login = QString::fromStdString(entry->login);
            hit.sub = QString::fromStdString(group->path());
            if (!login.isEmpty())
                hit.sub += QStringLiteral(" · ") + login;
            hit.haystack = (hit.name + QLatin1Char('\n') + login + QLatin1Char('\n') +
                            QString::fromStdString(entry->url) + QLatin1Char('\n') +
                            QString::fromStdString(entry->note))
                               .toLower();
            all_.push_back(std::move(hit));
        }
        for (const auto& sub : group->groups())
            walk(sub.get());
    };
    walk(root_);
}

void SearchWindow::refilter(const QString& text) {
    rebuildIndex();
    const QString needle = text.trimmed().toLower();
    hits_.clear();
    list_->clear();
    for (int i = 0; i < static_cast<int>(all_.size()); ++i) {
        if (!needle.isEmpty() && !all_[i].haystack.contains(needle))
            continue;
        auto* item = new QListWidgetItem(all_[i].name);
        item->setIcon(all_[i].icon);
        item->setData(Qt::UserRole, all_[i].sub);
        list_->addItem(item);
        hits_.push_back(i);
    }
    empty_->setVisible(hits_.empty());
    if (!hits_.empty())
        list_->setCurrentRow(0);
}

void SearchWindow::choose(int row) {
    if (row < 0 || row >= static_cast<int>(hits_.size()))
        return;
    const Hit& hit = all_[hits_[row]];
    close();
    emit entryChosen(hit.group, hit.entry);
}

void SearchWindow::setQuery(const QString& text) {
    field_->setText(text);
}

// Field keys: arrows walk the result list and Return picks, without
// moving focus out of the text field. List resizes keep the edge
// fades stretched across it.
bool SearchWindow::eventFilter(QObject* object, QEvent* event) {
    if (object == list_ && event->type() == QEvent::Resize) {
        topFade_->setGeometry(0, 0, list_->width(), topFade_->height());
        bottomFade_->setGeometry(0, list_->height() - bottomFade_->height(), list_->width(),
                                 bottomFade_->height());
        topFade_->raise();
        bottomFade_->raise();
    }
    if (object == field_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int last = list_->count() - 1;
        switch (key->key()) {
            case Qt::Key_Down:
                list_->setCurrentRow(qMin(list_->currentRow() + 1, last));
                return true;
            case Qt::Key_Up:
                list_->setCurrentRow(qMax(list_->currentRow() - 1, 0));
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                choose(list_->currentRow());
                return true;
            default:
                break;
        }
    }
    return QWidget::eventFilter(object, event);
}

void SearchWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SearchWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    field_->setFocus();
}
