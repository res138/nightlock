#include <QGraphicsEffect>
#include <QLabel>
#include <QPixmap>
#include <QScrollBar>
#include <QTest>

#include <nightlock/entry.hpp>

#include "appearancesettings.hpp"
#include "widgets/copylabel.hpp"
#include "widgets/entrydetailview.hpp"
#include "widgets/spoilerlabel.hpp"

#include <string>
#include <utility>

namespace {

void assignSecret(nightlock::secure::String& target, const std::string& value) {
    target.assign(value.data(), value.size());
}

void render(EntryDetailView& view) {
    QCoreApplication::processEvents();
    const QPixmap pixels = view.grab();
    QVERIFY(!pixels.isNull());
    QCoreApplication::processEvents();
}

}  // namespace

class EntryDetailViewTests final : public QObject {
    Q_OBJECT

private slots:
    void longContentStaysInsideViewportAndFades();
};

void EntryDetailViewTests::longContentStaysInsideViewportAndFades() {
    appearancesettings::applyStylesheet();
    const std::string longText(8192, 'W');
    const std::string longUrl =
        std::string("<img src=\"file:///tmp/not-loaded\">"
                    "https://example.test/?a=1&b=\"two\"&c=<tag>&tail=") +
        longText;
    const QString safeUrlTooltip =
        Qt::convertFromPlainText(QString::fromStdString(longUrl));

    nightlock::Entry entry;
    entry.name = longText;
    entry.login = longText;
    assignSecret(entry.password, longText);
    entry.url = longUrl;

    nightlock::EntryField plainField;
    plainField.label = longText;
    assignSecret(plainField.value, longText);
    plainField.custom = true;
    entry.fields.push_back(std::move(plainField));

    nightlock::EntryField secretField;
    secretField.label = std::string("Secret ") + longText;
    assignSecret(secretField.value, longText);
    secretField.secret = true;
    secretField.custom = true;
    entry.fields.push_back(std::move(secretField));

    EntryDetailView view;
    view.setEntry(&entry);
    view.resize(220, 760);
    view.show();
    QCOMPARE(view.sizeHint().width(), 420);

    for (const int width : {220, 460, 220}) {
        view.resize(width, 760);
        render(view);
        QCOMPARE(view.widget()->width(), view.viewport()->width());
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
    }

    auto* title = view.findChild<QLabel*>(QStringLiteral("detailTitle"));
    QVERIFY(title);
    QVERIFY(title->graphicsEffect());
    QVERIFY(title->graphicsEffect()->property("overflowFadeActive").toBool());

    auto* login = view.findChild<CopyLabel*>(QStringLiteral("loginValue"));
    auto* password =
        view.findChild<SpoilerLabel*>(QStringLiteral("passwordValue"));
    QVERIFY(login);
    QVERIFY(password);
    QVERIFY(login->naturalTextWidth() > login->width());
    QVERIFY(password->naturalTextWidth() > password->width());

    bool customLabelFades = false;
    for (QLabel* label : view.findChildren<QLabel*>(
             QStringLiteral("fieldLabel"))) {
        if (label->toolTip() == QString::fromStdString(longText)) {
            QVERIFY(label->width() <= 84);
            QVERIFY(label->graphicsEffect());
            customLabelFades =
                label->graphicsEffect()->property("overflowFadeActive").toBool();
        }
    }
    QVERIFY(customLabelFades);

    bool longValueFades = false;
    bool escapedUrlFades = false;
    for (QLabel* value : view.findChildren<QLabel*>(
             QStringLiteral("fieldValue"))) {
        if (!value->isVisible() || value->text().isEmpty())
            continue;
        QVERIFY(value->width() >= 40);
        if (value->graphicsEffect() &&
            value->graphicsEffect()->property("overflowFadeActive").toBool()) {
            longValueFades = true;
        }
        if (value->toolTip() == safeUrlTooltip) {
            QVERIFY(value->text().contains(QStringLiteral("&amp;")));
            QVERIFY(value->text().contains(QStringLiteral("&quot;")));
            QVERIFY(value->text().contains(QStringLiteral("&lt;tag&gt;")));
            QVERIFY(value->toolTip().contains(QStringLiteral("&lt;img")));
            escapedUrlFades = value->graphicsEffect()->property(
                "overflowFadeActive").toBool();
        }
    }
    QVERIFY(longValueFades);
    QVERIFY(escapedUrlFades);

    // Reusing the same pane for ordinary data must remove the mask again.
    nightlock::Entry shortEntry;
    shortEntry.name = "Short";
    shortEntry.login = "user";
    assignSecret(shortEntry.password, "secret");
    shortEntry.url = "https://example.test";
    view.setEntry(&shortEntry);
    render(view);
    QCOMPARE(view.sizeHint().width(), 420);
    QVERIFY(!title->graphicsEffect()->property("overflowFadeActive").toBool());
    QCOMPARE(view.widget()->width(), view.viewport()->width());
    QCOMPARE(view.horizontalScrollBar()->maximum(), 0);

    // A long unbroken note wraps vertically, but still cannot create a
    // hidden horizontal range.
    nightlock::Entry noteEntry;
    noteEntry.name = "Long note";
    noteEntry.note = longText;
    view.setEntry(&noteEntry);
    render(view);
    QCOMPARE(view.sizeHint().width(), 420);
    QCOMPARE(view.widget()->width(), view.viewport()->width());
    QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
}

QTEST_MAIN(EntryDetailViewTests)

#include "test_entry_detail_view.moc"
