// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_simulationstatus_snow.cpp
 * @brief     Tests for simulationstatus snow.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QAbstractItemModelTester>
#include "simulation/simulationstatusmodel.h"
class TestSimulationStatusSnow : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require snow-ledger rows to stay undecorated data and retain their order/text as
     * warnings insert and ledger rows replace/clear.
     * Uses QAbstractItemModelTester to enforce model-index/notification invariants;
     * creates an in-memory job without loading or simulating its named INP.
     *
     */
    void balancesRemainDataWhenWarningsAreInserted()
    {
        SimulationStatusModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const int job = model.addJob("snow", "snow.inp");
        const auto parent = model.index(0, 0);
        model.updateSnowLedger(job, {"UEB water residual 0 m3", "UEB energy residual 0 kJ"});
        QCOMPARE(model.rowCount(parent), 2);
        QVERIFY(!model.index(0, 0, parent).data(Qt::DecorationRole).isValid());
        QVERIFY(!model.index(0, 0, parent).data(Qt::ForegroundRole).isValid());
        model.addWarning(job, 1, "forcing fallback");
        QCOMPARE(model.rowCount(parent), 3);
        QCOMPARE(model.index(1, 0, parent).data().toString(), QString("UEB water residual 0 m3"));
        model.updateSnowLedger(job, {"UEB water residual 1e-12 m3"});
        QCOMPARE(model.rowCount(parent), 2);
        for (int i = 0; i < 30; ++i)
            model.addWarning(job, 2, "warning");
        const int last = model.rowCount(parent) - 1;
        QCOMPARE(model.index(last, 0, parent).data().toString(),
                 QString("UEB water residual 1e-12 m3"));
        model.updateSnowLedger(job, {});
        QCOMPARE(model.rowCount(parent), last);
    }
};
QTEST_MAIN(TestSimulationStatusSnow)
#include "test_simulationstatus_snow.moc"
