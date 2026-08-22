/*!
 * \file   test_openprogress.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  LOAD_PERF plan Phase 1 — OpenProgressModel contract.
 *
 *         The status bar's determinate percent is composed from stages that
 *         complete on three different threads in a non-deterministic order.
 *         These cases pin the invariants the view depends on:
 *           - weights sum to 100 and the bar can actually reach it
 *           - percent NEVER decreases, whatever order stages arrive in
 *           - a skipped stage group (no 2D mesh) jumps forward, not backward
 *           - finished() fires exactly once, only when every stage is done
 *           - progressChanged does not fire for no-op ticks
 */
#include "core/loadprogress.h"

#include <QSignalSpy>
#include <QTest>

class TestOpenProgress : public QObject
{
    Q_OBJECT

private slots:
    void weightsSumTo100();
    void reachesExactly100();
    void neverDecreasesOnOutOfOrderStages();
    void skippedMeshStagesJumpForward();
    void noopTicksDoNotEmit();
    void labelOnlyChangeEmits();
    void finishedFiresExactlyOnce();
    void engineParseWithoutSubTicksStillReaches100();
};

void TestOpenProgress::weightsSumTo100()
{
    int sum = 0;
    for (int i = 0; i < static_cast<int>(OpenStage::Count_); ++i)
        sum += OpenProgressModel::stageWeight(static_cast<OpenStage>(i));
    QCOMPARE(sum, 100);
}

void TestOpenProgress::reachesExactly100()
{
    OpenProgressModel m;
    QCOMPARE(m.percent(), 0);
    QVERIFY(!m.isComplete());

    for (int i = 0; i < static_cast<int>(OpenStage::Count_); ++i)
        m.finishStage(static_cast<OpenStage>(i));

    QCOMPARE(m.percent(), 100);
    QVERIFY(m.isComplete());
}

void TestOpenProgress::neverDecreasesOnOutOfOrderStages()
{
    // The mesh worker genuinely overlaps the GUI-thread sidecar apply, so
    // stages complete out of nominal order. The bar must not stutter.
    OpenProgressModel m;
    QSignalSpy spy(&m, &OpenProgressModel::progressChanged);

    const OpenStage order[] = {
        OpenStage::MeshParse,     // mesh worker gets there first
        OpenStage::EngineParse,
        OpenStage::MeshSceneA,
        OpenStage::SoaCopy,
        OpenStage::Sidecar,
        OpenStage::GeomCache,
        OpenStage::Results,
        OpenStage::CrsFinish,
        OpenStage::MeshSceneB,
    };

    int last = 0;
    for (OpenStage s : order) {
        m.finishStage(s);
        QVERIFY2(m.percent() >= last,
                 qPrintable(QStringLiteral("percent went backwards: %1 -> %2")
                                .arg(last).arg(m.percent())));
        last = m.percent();
    }
    QCOMPARE(m.percent(), 100);

    // Every emitted value is also monotonic, not just the polled one.
    int prevEmitted = -1;
    for (const QList<QVariant> &args : spy) {
        const int pct = args.at(0).toInt();
        QVERIFY(pct >= prevEmitted);
        prevEmitted = pct;
    }
}

void TestOpenProgress::skippedMeshStagesJumpForward()
{
    // A model with no 2D mesh finishes the three mesh stages immediately.
    // That is a forward jump of exactly their combined weight.
    OpenProgressModel m;
    m.finishStage(OpenStage::EngineParse);
    const int before = m.percent();

    m.finishStage(OpenStage::MeshParse);
    m.finishStage(OpenStage::MeshSceneA);
    m.finishStage(OpenStage::MeshSceneB);

    const int meshWeight = OpenProgressModel::stageWeight(OpenStage::MeshParse)
                         + OpenProgressModel::stageWeight(OpenStage::MeshSceneA)
                         + OpenProgressModel::stageWeight(OpenStage::MeshSceneB);
    QCOMPARE(m.percent(), before + meshWeight);
    QVERIFY(m.percent() > before);
}

void TestOpenProgress::noopTicksDoNotEmit()
{
    // Masked loops tick often and mostly report the same percent; the model
    // must absorb that rather than flooding the GUI event queue.
    OpenProgressModel m;
    m.setStage(OpenStage::EngineParse, 50, QStringLiteral("Parsing model…"));

    QSignalSpy spy(&m, &OpenProgressModel::progressChanged);
    for (int i = 0; i < 20; ++i)
        m.setStage(OpenStage::EngineParse, 50, QStringLiteral("Parsing model…"));
    QCOMPARE(spy.count(), 0);

    // A backwards local report is absorbed too (stale-revision retry).
    m.setStage(OpenStage::EngineParse, 10, QStringLiteral("Parsing model…"));
    QCOMPARE(spy.count(), 0);
}

void TestOpenProgress::labelOnlyChangeEmits()
{
    OpenProgressModel m;
    m.setStage(OpenStage::EngineParse, 40, QStringLiteral("Parsing model…"));

    QSignalSpy spy(&m, &OpenProgressModel::progressChanged);
    m.setStage(OpenStage::EngineParse, 40, QStringLiteral("Resolving references…"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Resolving references…"));
    QCOMPARE(m.label(), QStringLiteral("Resolving references…"));
}

void TestOpenProgress::finishedFiresExactlyOnce()
{
    OpenProgressModel m;
    QSignalSpy done(&m, &OpenProgressModel::finished);

    m.finishAll();
    QCOMPARE(done.count(), 1);
    QCOMPARE(m.percent(), 100);

    // Redundant completion must not re-fire.
    m.finishAll();
    m.finishStage(OpenStage::EngineParse);
    QCOMPARE(done.count(), 1);
}

void TestOpenProgress::engineParseWithoutSubTicksStillReaches100()
{
    // Phase-4 degradation case: built against an engine with no open-progress
    // symbol, EngineParse never reports intra-stage percent. The bar must
    // still be monotonic and still land on 100.
    OpenProgressModel m;
    QSignalSpy spy(&m, &OpenProgressModel::progressChanged);

    m.finishStage(OpenStage::EngineParse);          // 0 -> 28 in one step
    QCOMPARE(m.percent(), OpenProgressModel::stageWeight(OpenStage::EngineParse));

    for (int i = 1; i < static_cast<int>(OpenStage::Count_); ++i)
        m.finishStage(static_cast<OpenStage>(i));

    QCOMPARE(m.percent(), 100);
    QVERIFY(spy.count() > 0);
}

QTEST_MAIN(TestOpenProgress)
#include "test_openprogress.moc"
