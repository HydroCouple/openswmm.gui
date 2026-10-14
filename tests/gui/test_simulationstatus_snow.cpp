// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QAbstractItemModelTester>
#include "simulation/simulationstatusmodel.h"
class TestSimulationStatusSnow:public QObject {
    Q_OBJECT
private slots:
    void balancesRemainDataWhenWarningsAreInserted() {
        SimulationStatusModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        const int job=model.addJob("snow","snow.inp");const auto parent=model.index(0,0);
        model.updateSnowLedger(job,{"UEB water residual 0 m3","UEB energy residual 0 kJ"});
        QCOMPARE(model.rowCount(parent),2);
        QVERIFY(!model.index(0,0,parent).data(Qt::DecorationRole).isValid());
        QVERIFY(!model.index(0,0,parent).data(Qt::ForegroundRole).isValid());
        model.addWarning(job,1,"forcing fallback");
        QCOMPARE(model.rowCount(parent),3);
        QCOMPARE(model.index(1,0,parent).data().toString(),QString("UEB water residual 0 m3"));
        model.updateSnowLedger(job,{"UEB water residual 1e-12 m3"});
        QCOMPARE(model.rowCount(parent),2);
        for(int i=0;i<30;++i)model.addWarning(job,2,"warning");
        const int last=model.rowCount(parent)-1;
        QCOMPARE(model.index(last,0,parent).data().toString(),QString("UEB water residual 1e-12 m3"));
        model.updateSnowLedger(job,{});
        QCOMPARE(model.rowCount(parent),last);
    }
};
QTEST_MAIN(TestSimulationStatusSnow)
#include "test_simulationstatus_snow.moc"
