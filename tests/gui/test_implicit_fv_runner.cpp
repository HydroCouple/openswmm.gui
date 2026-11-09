// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_implicit_fv_runner.cpp
 * @brief     Tests for implicit fv runner.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/simulationrunner.h"
#include "ui/dialogs/simoptions/enginecapabilities.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

class TestImplicitFvRunner : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require two native FV runs to preserve prior output in one new backup, then refuse
     * unsupported heat without changing results.
     * Copies SWMMVIS_IMPLICIT_FIXTURE into cwd/native_runner, creates/replaces input/report/
     * output/log and waits up to thirty seconds per worker completion. Requires a valid
     * 16-cell BACKWARD_EULER fixture and isolated output folder; skips engines without
     * the native implicit capability before running the worker. Checks runlog lineage
     * messages and retains all artifacts. Native rerun and failed-preflight paths execute
     * through SimulationRunner, with no GUI project save.
     *
     */
    void nativeRunRerunAndInvalidPreflightPreserveResults()
    {
        const QString fixture = qEnvironmentVariable("SWMMVIS_IMPLICIT_FIXTURE");
        QVERIFY(QFileInfo::exists(fixture));
        SWMM_Engine probe = swmm_engine_create();
        QVERIFY(probe);
        const bool native = openswmmvis::ui::probeEngineCapabilities(
                                probe, QStringLiteral("6.0.0")).implicitFv;
        swmm_engine_destroy(probe);
        if (!native)
            QSKIP("The loaded engine does not advertise native implicit finite-volume support.");
        const QString folder = QDir::current().absoluteFilePath("native_runner");
        QVERIFY(QDir().mkpath(folder));
        QFile source(fixture);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto text = source.readAll();
        const QString input = folder + "/native.inp", report = folder + "/native.rpt",
                      output = folder + "/native.out";
        {
            QFile file(input);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(text), text.size());
        }
        auto read = [](const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return QByteArray{};
            return f.readAll();
        };
        QByteArray first;
        QStringList backups;
        for (int run = 0; run < 2; ++run) {
            SimulationRunner worker(1, "Native FV", input, report, output, "6.0.0");
            QSignalSpy finished(&worker, &SimulationRunner::finished);
            worker.start();
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
            const auto result = finished.takeFirst();
            QVERIFY2(result[1].toBool(), qPrintable(result[3].toString()));
            QVERIFY(QFileInfo(output).size() > 0);
            const auto now = QDir(folder).entryList({"native.out.previous-native-*"}, QDir::Files);
            if (run == 0) {
                first = read(output);
                backups = now;
            } else {
                auto added = now;
                for (const auto &old : backups)
                    added.removeAll(old);
                QCOMPARE(added.size(), 1);
                QCOMPARE(read(folder + "/" + added.first()), first);
            }
        }
        const auto before = read(output);
        {
            QFile file(input);
            QVERIFY(file.open(QIODevice::Append));
            file.write("\n[OPTIONS]\nHEAT_TRANSPORT YES\n");
        }
        SimulationRunner rejected(2, "Unsupported native heat", input, report, output, "6.0.0");
        QSignalSpy finished(&rejected, &SimulationRunner::finished);
        rejected.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        const auto failure = finished.takeFirst();
        QVERIFY(!failure[1].toBool());
        QCOMPARE(read(output), before);
        const auto log = read(folder + "/native.runlog.txt");
        QVERIFY(log.contains("Native implicit FV final work:"));
        QVERIFY(log.contains("BACKWARD_EULER"));
        QVERIFY(log.contains("\"cells\":16"));
        QVERIFY(log.contains("Previous native results preserved:"));
    }
};
QTEST_MAIN(TestImplicitFvRunner)
#include "test_implicit_fv_runner.moc"
