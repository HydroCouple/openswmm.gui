/*!
 * \file   test_layerstylesubject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for the ILayerStyleSubject snapshot / restore contract that
 *         LayerStyleDialog's Cancel rollback and undo/redo ride on.
 *
 *         The generic implementation walks the property object's Q_PROPERTYs
 *         and funnels each through a QVariant→JSON→QVariant pair. Any type
 *         that pair cannot represent is silently lost on Cancel — which is
 *         what happened to QFont before it got an explicit
 *         `toString()/fromString()` case. These tests pin the round trip over
 *         a real subject: a SwmmElementSymbolAdapter, whose `labelFont` is
 *         the property that regressed.
 */

#include <QtTest/QtTest>

#include <QColor>
#include <QFont>
#include <QJsonObject>

#include <memory>

#include "layers/swmmelementsymboladapter.h"
#include "ui/dialogs/ilayerstylesubject.h"

using openswmmvis::ui::LayerStyleSubject;

class TestLayerStyleSubject : public QObject
{
    Q_OBJECT

private slots:
    void qfontSurvivesSnapshotRestore();
    void scalarPropertiesSurviveSnapshotRestore();
    void restoreIgnoresAbsentKeys();

private:
    /*! An adapter over a throwaway symbol; the writer records the last value
     *  pushed back so we can also see the writeback fire. */
    static std::unique_ptr<SwmmElementSymbolAdapter>
    makeAdapter(SWMMElementSymbol initial, SWMMElementSymbol *sink)
    {
        return std::make_unique<SwmmElementSymbolAdapter>(
            std::move(initial),
            [sink](const SWMMElementSymbol &s) { if (sink) *sink = s; });
    }
};

void TestLayerStyleSubject::qfontSurvivesSnapshotRestore()
{
    SWMMElementSymbol written;
    SWMMElementSymbol initial;
    auto adapter = makeAdapter(initial, &written);
    LayerStyleSubject subject(QStringLiteral("Junctions"), adapter.get(),
                              QStringLiteral("model.junctions"));

    // A font that differs from the default in several independent
    // attributes — family, size, weight and slant all have to survive.
    QFont distinctive(QStringLiteral("Courier New"), 17);
    distinctive.setBold(true);
    distinctive.setItalic(true);
    adapter->setLabelFont(distinctive);
    QCOMPARE(adapter->labelFont().family(), QStringLiteral("Courier New"));

    const QJsonObject snap = subject.snapshot();
    // The snapshot must actually carry the font, not an empty string — an
    // unrepresentable QVariant serialised to "" and restored to nothing.
    QVERIFY(snap.contains(QStringLiteral("labelFont")));
    QVERIFY(!snap.value(QStringLiteral("labelFont")).toString().isEmpty());

    // Simulate the user's edits after the snapshot was taken.
    QFont other(QStringLiteral("Arial"), 8);
    other.setBold(false);
    other.setItalic(false);
    adapter->setLabelFont(other);
    QCOMPARE(adapter->labelFont().family(), QStringLiteral("Arial"));

    // Cancel.
    subject.restore(snap);

    const QFont back = adapter->labelFont();
    QCOMPARE(back.family(),    distinctive.family());
    QCOMPARE(back.pointSize(), distinctive.pointSize());
    QCOMPARE(back.bold(),      distinctive.bold());
    QCOMPARE(back.italic(),    distinctive.italic());
    QCOMPARE(back.toString(),  distinctive.toString());

    // The rollback must reach the owning struct too, not just the adapter's
    // own copy — that writeback is what repaints the map.
    QCOMPARE(written.labelFont.toString(), distinctive.toString());
}

void TestLayerStyleSubject::scalarPropertiesSurviveSnapshotRestore()
{
    SWMMElementSymbol written;
    auto adapter = makeAdapter(SWMMElementSymbol{}, &written);
    LayerStyleSubject subject(QStringLiteral("Conduits"), adapter.get(),
                              QStringLiteral("model.conduits"));

    adapter->setFillColor(QColor(10, 20, 30));
    adapter->setOutlineWidth(3.5);
    adapter->setShowLabel(true);
    adapter->setLabelColor(QColor(200, 100, 50));
    adapter->setShowArrows(true);
    adapter->setArrowSize(13.5);

    const QJsonObject snap = subject.snapshot();

    adapter->setFillColor(QColor(99, 99, 99));
    adapter->setOutlineWidth(0.25);
    adapter->setShowLabel(false);
    adapter->setLabelColor(QColor(1, 2, 3));
    adapter->setShowArrows(false);
    adapter->setArrowSize(1.0);

    subject.restore(snap);

    QCOMPARE(adapter->fillColor(),    QColor(10, 20, 30));
    QCOMPARE(adapter->outlineWidth(), 3.5);
    QCOMPARE(adapter->showLabel(),    true);
    QCOMPARE(adapter->labelColor(),   QColor(200, 100, 50));
    QCOMPARE(adapter->showArrows(),   true);
    QCOMPARE(adapter->arrowSize(),    13.5);
}

void TestLayerStyleSubject::restoreIgnoresAbsentKeys()
{
    SWMMElementSymbol written;
    auto adapter = makeAdapter(SWMMElementSymbol{}, &written);
    LayerStyleSubject subject(QStringLiteral("Weirs"), adapter.get());

    adapter->setOutlineWidth(4.0);
    QJsonObject partial;
    partial.insert(QStringLiteral("fillColor"), QStringLiteral("#ff0000"));

    // A snapshot from an older schema carries only some keys; the properties
    // it omits must be left as they are rather than reset to defaults.
    subject.restore(partial);
    QCOMPARE(adapter->fillColor(),    QColor(255, 0, 0));
    QCOMPARE(adapter->outlineWidth(), 4.0);
}

QTEST_MAIN(TestLayerStyleSubject)
#include "test_layerstylesubject.moc"
