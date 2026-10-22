/**
 * @file      test_lidstates.cpp
 * @brief     Tests for lidstates.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/lidstates.h"
#include "simulation/lidstaterecorder.h"
#include <openswmm/engine/openswmm_output.h>
#include <QDir>
#include <QFile>
#include <QTest>
using namespace openswmmvis::io;

class TestLidStates : public QObject
{
    Q_OBJECT
    QString dir() const
    {
        return qEnvironmentVariable("SWMMVIS_LID_TEST_OUTPUT",
                                    QDir::currentPath() + "/tests/gui/data/output_lidstates");
    }
    static QDateTime time(int seconds)
    {
        return QDateTime(QDate(2004, 1, 1), QTime(0, 0), Qt::UTC).addSecs(seconds);
    }
    LidStateFrame sample(int second = 0) const
    {
        LidStateFrame f;
        f.time = time(second);
        LidNodeState n;
        n.name = "S";
        n.richards = true;
        n.cells = {
            {2, 2, 3, .4, .1, -1, 1.5}, {2, 1, 2, .4, .4, 1, 2.5}, {3, 0, 1, .3, .1, -1, -.5}};
        f.nodes = {n};
        return f;
    }
private slots:
    void initTestCase() { QVERIFY(QDir().mkpath(dir())); }
    void perchedBoundaries()
    {
        auto f = sample();
        const auto b = lidSaturationBoundaries(f.nodes[0]);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[0].elevation, 2.0);
        QVERIFY(b[0].waterTable);
        QCOMPARE(b[1].elevation, 1.0);
        QVERIFY(!b[1].waterTable);
        for (auto &c : f.nodes[0].cells)
            c.pressure = 1;
        QVERIFY(lidSaturationBoundaries(f.nodes[0]).isEmpty());
        for (auto &c : f.nodes[0].cells)
            c.pressure = -1;
        QVERIFY(lidSaturationBoundaries(f.nodes[0]).isEmpty());
        f.nodes[0].cells[1].pressure = 0;
        QCOMPARE(lidSaturationBoundaries(f.nodes[0])[0].elevation, 1.5);
        f.nodes[0].richards = false;
        QVERIFY(lidSaturationBoundaries(f.nodes[0]).isEmpty());
    }
    void boundedPlaybackAndIdentity()
    {
        const QString path = dir() + "/synthetic.out";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("sample results");
        }
        LidStateWriter writer;
        QVERIFY(writer.open(path, sample()));
        QVERIFY(writer.append(sample(0)));
        auto later = sample(30);
        later.nodes[0].cells[0].moisture = .3;
        QVERIFY(writer.append(later));
        LidStateReader reader;
        QCOMPARE(reader.frameAt(path, true, time(29), {"S"}).time, time(0));
        const auto live = reader.frameAt(path, true, time(30), {"S"});
        QCOMPARE(live.time, time(30));
        QVERIFY(std::abs(live.nodes[0].cells[0].moisture - .3) < 1e-6);
        QVERIFY(reader.frameAt(path, false, time(30), {"S"}).nodes.isEmpty());
        QVERIFY(writer.finish());
        const auto saved = reader.frameAt(path, false, time(30), {"S"});
        QCOMPARE(saved.nodes[0].cells[0].moisture, live.nodes[0].cells[0].moisture);
        QCOMPARE(reader.frameAt(path, false, time(0), {"S"}).time, time(0));
        QVERIFY(reader.frameAt(path, false, time(-1), {"S"}).nodes.isEmpty());
        QVERIFY(reader.frameAt(path, false, time(30), {"other"}).nodes.isEmpty());
        // A same-size foreign output must not acquire these water states.
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("other  results");
        }
        LidStateReader fresh;
        QVERIFY(fresh.frameAt(path, false, time(30), {"S"}).nodes.isEmpty());
        QVERIFY(fresh.frameAt(dir() + "/missing.out", false, time(30), {"S"}).nodes.isEmpty());
    }
    void incompleteLiveTail()
    {
        const QString path = dir() + "/tail.out";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test");
        }
        {
            LidStateWriter writer;
            QVERIFY(writer.open(path, sample()));
            QVERIFY(writer.append(sample()));
        }
        {
            QFile file(lidStatePath(path));
            QVERIFY(file.open(QIODevice::Append));
            file.write(QByteArray(10, 0));
        }
        LidStateReader reader;
        QCOMPARE(reader.frameAt(path, true, time(30), {"S"}).time, time(0));
        QVERIFY(reader.frameAt(path, false, time(30), {"S"}).nodes.isEmpty());
    }
    void engineRecording_data()
    {
        QTest::addColumn<bool>("si");
        QTest::addColumn<bool>("richards");
        QTest::addColumn<bool>("partial");
        QTest::newRow("US") << false << true << false;
        QTest::newRow("SI") << true << true << false;
        QTest::newRow("partial-US") << false << true << true;
        QTest::newRow("classical-US") << false << false << false;
    }
    void engineRecording()
    {
        QFETCH(bool, si);
        QFETCH(bool, richards);
        QFETCH(bool, partial);
        const QString stem = dir() + "/engine_" + QString::fromLatin1(QTest::currentDataTag());
        QString inp = QString(R"([OPTIONS]
FLOW_UNITS %1
INFILTRATION HORTON
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2004
END_DATE 01/01/2004
END_TIME 00:02:00
REPORT_STEP 00:00:30
ROUTING_STEP 1
[STORAGE]
S %2 %5 0 FUNCTIONAL 0 0 100
[OUTFALLS]
R 0 FREE NO
[ORIFICES]
O S R BOTTOM 0 .6 NO 0
[XSECTIONS]
O CIRCULAR .1 0 0 0
[LID_CONTROLS]
L NODE
L SURFACE %3 .1
L MEDIA %4 .45 .2 .08 10 3 3
L AGGREGATE %4 .4 100
L BOTTOM 0 0
[LID_RICHARDS]
L OPTIONS 4 1e-7 1e-5 30
L 2 .03 8 2.2 .5 .0001
L 3 .01 20 2.5 .5 .0001
[LID_NODES]
S L 30
[LID_NODE_OUTLETS]
O 3 BOTTOM
[REPORT]
NODES ALL
LINKS ALL
)")
                          .arg(si ? "CMS" : "CFS")
                          .arg(si ? 30.48 : 100)
                          .arg(si ? 150 : 6)
                          .arg(si ? 300 : 12)
                          .arg(si ? .75 : 2.5);
        if (!richards) {
            const int begin = inp.indexOf("[LID_RICHARDS]"), end = inp.indexOf("[LID_NODES]");
            inp.remove(begin, end - begin);
        }
        {
            QFile file(stem + ".inp");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(inp.toUtf8());
        }
        struct Engine {
            SWMM_Engine h = swmm_engine_create();
            ~Engine()
            {
                swmm_engine_close(h);
                swmm_engine_destroy(h);
            }
        } engine;
        QCOMPARE(swmm_engine_open(engine.h, qPrintable(stem + ".inp"), qPrintable(stem + ".rpt"),
                                  qPrintable(stem + ".out"), nullptr),
                 0);
        QCOMPARE(swmm_engine_initialize(engine.h), 0);
        QCOMPARE(swmm_engine_start(engine.h, 1), 0);
        openswmmvis::simulation::LidStateRecorder recorder;
        QVERIFY(recorder.start(engine.h, stem + ".out"));
        LidStateReader reader;
        auto initial = reader.frameAt(stem + ".out", true, time(0), {"S"});
        QCOMPARE(initial.nodes.size(), 1);
        if (richards)
            QCOMPARE(initial.nodes[0].cells.size(), 8);
        else
            QVERIFY(!initial.nodes[0].cells.isEmpty());
        QCOMPARE(initial.si, si);
        QVERIFY(std::abs(initial.nodes[0].cells.last().bottom - (si ? 30.48 : 100)) < 1e-6);
        double elapsed = 0;
        do {
            QCOMPARE(swmm_engine_step(engine.h, &elapsed), 0);
            QVERIFY(recorder.sample(elapsed <= 0));
            if (partial && elapsed * 86400 >= 45)
                break;
        } while (elapsed > 0);
        QVERIFY(recorder.sample(true));
        auto live = reader.frameAt(stem + ".out", true, time(120), {"S"});
        if (!partial)
            QCOMPARE(live.time, time(120));
        else
            QVERIFY(live.time < time(120) && live.time >= time(45));
        double pressure = 0, head = 0, water = 0;
        if (richards) {
            QCOMPARE(swmm_lid_richards_state_get(engine.h, swmm_node_index(engine.h, "S"), 1,
                                                 &pressure, &head, &water),
                     0);
            QVERIFY(std::abs(live.nodes[0].cells[0].head - head) < 1e-4);
        } else {
            QVERIFY(!live.nodes[0].richards);
            QVERIFY(std::isnan(live.nodes[0].cells[0].head));
            QVERIFY(std::isnan(live.nodes[0].cells[0].pressure));
        }
        QCOMPARE(swmm_engine_end(engine.h), 0);
        swmm_engine_report(engine.h);
        swmm_engine_close(engine.h);
        QVERIFY(recorder.finish());
        auto saved = reader.frameAt(stem + ".out", false, time(120), {"S"});
        QCOMPARE(saved.time, live.time);
        if (richards)
            QCOMPARE(saved.nodes[0].cells[0].head, live.nodes[0].cells[0].head);
        else
            QVERIFY(std::isnan(saved.nodes[0].cells[0].head));
        auto backwards = reader.frameAt(stem + ".out", false, time(30), {"S"});
        QVERIFY(backwards.time <= time(30));
        QVERIFY(backwards.time > time(0));
    }
};
QTEST_GUILESS_MAIN(TestLidStates)
#include "test_lidstates.moc"
