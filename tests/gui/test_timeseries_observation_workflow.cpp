/**
 * @file test_timeseries_observation_workflow.cpp
 * @brief Offline full-resolution observation plotting/export and explicit model conversion tests.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "timeseries/timeseriesassignment.h"
#include "timeseries/timeseriesstore.h"
#include "plot/timeseriesrunlayer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QUuid>
#include <cmath>
using namespace OpenSWMM::TimeSeries;
using openswmmvis::plot::TimeSeriesRunLayer;
namespace
{
/** @brief Retain fixture and resulting GeoPackage in a reviewable repository directory. */
std::shared_ptr<const TimeSeriesDataset> makeDataset(const QStringList &rows,
    const QString &basis = "instant", const QString &unit = "ft^3/s")
{
    const QString root = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
        .absoluteFilePath("../verification/timeseries_layers_2026-10-27/plot-assignment/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(root);
    QFile csv(root+"/samples.csv");
    if (!csv.open(QIODevice::WriteOnly)) return {};
    csv.write("time,value,approval,qualifier\n");
    csv.write(rows.join('\n').toUtf8()); csv.write("\n"); csv.close();
    QJsonObject defaults{{"provider","fixture"},{"collection","observations"},{"siteId","00123"},
                         {"sourceSeriesId","series-01"},{"parameter","00060"},{"statistic",basis == "calendar-date" ? "mean" : "instantaneous"},
                         {"timeBasis",basis},{"unit",unit}};
    QJsonObject descriptor{{"schema","openswmm.timeseries-import/1"},
                           {"datasetId",QUuid::createUuid().toString(QUuid::WithoutBraces)},
                           {"title","Observation workflow acceptance"},
                           {"sources",QJsonArray{QJsonObject{{"path","samples.csv"},{"defaults",defaults},
                           {"columns",QJsonObject{{"time","time"},{"value","value"},{"approval","approval"},{"qualifier","qualifier"}}}}}}};
    QFile json(root+"/import.json"); if (!json.open(QIODevice::WriteOnly)) return {};
    json.write(QJsonDocument(descriptor).toJson()); json.close();
    QString error;
    if (!TimeSeriesStore::importDescriptor(root+"/import.json",root+"/series.gpkg",&error)) { qWarning().noquote()<<error; return {}; }
    return TimeSeriesStore::open(root+"/series.gpkg",&error);
}
/** @brief Explicit common preparation choices; individual tests change one semantic constraint. */
AssignmentOptions options(const TimeSeriesDataset &d)
{
    AssignmentOptions o; o.seriesId=d.series.first().id; o.modelSeriesName="OBS_TEST";
    o.targetUnit="CMS"; o.clockConfirmed=true; o.unitInterpretationConfirmed=true;
    return o;
}
}
/** @brief Real indexed fixtures exercise source fidelity, bounded plotting and explicit conversion. */
class TestObservationWorkflow : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Physical conversions preserve zero and apply additive temperature conversions. */
    void units()
    {
        double factor=0,offset=0;
        QVERIFY(conversion("ft^3/s","CMS",factor,offset)); QVERIFY(std::abs(factor-.028316846592)<1e-12); QCOMPARE(offset,0.);
        QVERIFY(conversion("°F","°C",factor,offset)); QVERIFY(std::abs(32*factor+offset)<1e-12);
        QVERIFY(conversion("mm/hr","in/hr",factor,offset)); QVERIFY(std::abs(factor-1/25.4)<1e-12);
        QVERIFY(!conversion("m","CMS",factor,offset)); QVERIFY(!conversion("","",factor,offset));
    }
    /** @brief Model clocks are explicitly mapped, never inherited from the machine timezone. */
    void clockAndFiniteConversion()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,0,Approved,","2024-12-01T01:00:00Z,100,Approved,"}); QVERIFY(d);
        auto o=options(*d); o.clockConfirmed=false; QVERIFY(!prepareAssignment(*d,o).ok());
        o.clockConfirmed=true; o.utcOffsetSeconds=-5*3600;
        const auto p=prepareAssignment(*d,o); QVERIFY2(p.ok(),qPrintable(p.error));
        QCOMPARE(p.times.first().toString("yyyy-MM-dd HH:mm:ss"),QString("2024-11-30 19:00:00"));
        QCOMPARE(p.values.first(),0.); QVERIFY(std::abs(p.values.last()-2.8316846592)<1e-12);
        QCOMPARE(p.provenance.value("fingerprint").toString(),d->fingerprint);
    }
    /** @brief Null samples cannot silently become zero or be skipped across interpolation gaps. */
    void missingAndQualified()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,1,Approved,","2024-12-01T01:00:00Z,,Approved,"}); QVERIFY(d);
        QVERIFY(!prepareAssignment(*d,options(*d)).ok());
        auto q=makeDataset({"2024-12-01T00:00:00Z,1,Provisional,"}); QVERIFY(q);
        auto o=options(*q); QVERIFY(!prepareAssignment(*q,o).ok());
        o.allowQualified=true; QVERIFY(prepareAssignment(*q,o).ok());
    }
    /** @brief Rounding collisions and explicit missing recording intervals prevent assignment. */
    void collisionsAndCadence()
    {
        auto d=makeDataset({"2024-12-01T00:00:00.100Z,1,Approved,","2024-12-01T00:00:00.200Z,2,Approved,"}); QVERIFY(d);
        QVERIFY(!prepareAssignment(*d,options(*d)).ok());
        auto g=makeDataset({"2024-12-01T00:00:00Z,1,Approved,","2024-12-01T00:30:00Z,2,Approved,"}); QVERIFY(g);
        auto o=options(*g); o.expectedIntervalSeconds=900; o.allowIrregularInterpolation=true;
        QVERIFY(!prepareAssignment(*g,o).ok());
    }
    /** @brief Daily statistics require an explicit constant-day interpretation and contiguous days. */
    void calendarDates()
    {
        auto d=makeDataset({"2024-12-01,10,Approved,","2024-12-02,20,Approved,"},"calendar-date"); QVERIFY(d);
        auto o=options(*d); QVERIFY(!prepareAssignment(*d,o).ok()); o.dailyConstantConfirmed=true;
        const auto p=prepareAssignment(*d,o); QVERIFY2(p.ok(),qPrintable(p.error)); QCOMPARE(p.times.size(),4);
        QCOMPARE(p.times[1].time(),QTime(23,59,59)); QCOMPARE(p.values[0],p.values[1]);
        auto g=makeDataset({"2024-12-01,10,Approved,","2024-12-03,20,Approved,"},"calendar-date"); QVERIFY(g);
        o=options(*g);o.dailyConstantConfirmed=true;QVERIFY(!prepareAssignment(*g,o).ok());
        const auto display=TimeSeriesRunLayer::readWindow(*d,d->series.first().id,{},{});
        QVERIFY(display.ok); QVERIFY(display.pointsOnly); QVERIFY(!display.allowPairing); QVERIFY(display.displayNote.contains("Calendar"));
    }
    /** @brief A stage binding requires a datum and flow bindings reject length measurements. */
    void targetValidation()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,10,Approved,"},"instant","ft"); QVERIFY(d);
        auto o=options(*d);o.targetUnit="m";o.target=AssignmentTarget::OutfallStage;o.targetObject="OUT1";
        QVERIFY(!prepareAssignment(*d,o).ok());o.datumConfirmed=true;o.datumOffset=12;
        auto p=prepareAssignment(*d,o);QVERIFY2(p.ok(),qPrintable(p.error));QVERIFY(std::abs(p.values.first()-15.048)<1e-12);
        o.target=AssignmentTarget::NodeFlow;QVERIFY(!prepareAssignment(*d,o).ok());
    }
    /** @brief Display reduction preserves extrema, while statistics and CSV retain all source samples. */
    void boundedPlotAndFullExport()
    {
        QStringList rows;const auto start=QDateTime(QDate(2024,12,1),QTime(0,0),QTimeZone::UTC);
        constexpr int count=20000;
        for(int i=0;i<count;++i) rows.append(start.addSecs(i).toString(Qt::ISODate)+','+QString::number(i==9973?1000000:i)+",Approved,");
        auto d=makeDataset(rows);QVERIFY(d);
        const auto r=TimeSeriesRunLayer::readWindow(*d,d->series.first().id,{},{});
        QVERIFY2(r.ok,qPrintable(r.errorMessage));QVERIFY(r.values.size()<=12000);QVERIFY(r.displayOnly);QVERIFY(r.fullStatistics.has_value());
        QCOMPARE(r.fullStatistics->count,count);QCOMPARE(r.fullStatistics->max,1000000.);
        QVERIFY(std::find(r.values.begin(),r.values.end(),1000000.)!=r.values.end());
        QString error;const auto path=QFileInfo(d->path).absolutePath()+"/export.csv";
        QVERIFY2(TimeSeriesRunLayer::exportCsv({{d,d->series.first().id}},path,&error),qPrintable(error));
        QFile output(path);QVERIFY(output.open(QIODevice::ReadOnly));QCOMPARE(output.readAll().count('\n'),count+1);
    }
    /** @brief Pending work completes asynchronously and native descriptors retain explicit identity. */
    void asyncAdapter()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,0,Approved,","2024-12-01T01:00:00Z,2,Approved,"});QVERIFY(d);
        TimeSeriesRunLayer source(d);QSignalSpy changed(&source,&TimeSeriesRunLayer::dataReady);
        const auto id=d->series.first().id;source.requestWindow({id});QTRY_COMPARE_WITH_TIMEOUT(changed.count(),1,10000);
        openswmmvis::plot::SeriesData result;const auto desc=source.descriptorFor(id);
        QVERIFY(desc.isObservation());QCOMPARE(desc.unitLabel(openswmmvis::plot::UnitSystem::SI),QString("ft^3/s"));
        source.getSeriesAt(openswmmvis::plot::ObjectRef::forObserved(id),desc,result);QVERIFY(result.ok);QCOMPARE(result.values.size(),std::size_t(2));
    }
    /** @brief Stale identities fail all worker paths, and CSV export cannot replace its source package. */
    void staleSourceAndProtectedDestination()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,0,Approved,"});QVERIFY(d);
        auto stale=std::make_shared<TimeSeriesDataset>(*d);stale->fingerprint="stale-fingerprint";
        QVERIFY(!prepareAssignment(*stale,options(*stale)).ok());
        QVERIFY(!TimeSeriesRunLayer::readWindow(*stale,stale->series.first().id,{},{}).ok);
        QString error;const auto path=QFileInfo(d->path).absolutePath()+"/stale.csv";
        QVERIFY(!TimeSeriesRunLayer::exportCsv({{stale,stale->series.first().id}},path,&error));QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!TimeSeriesRunLayer::exportCsv({{d,d->series.first().id}},d->path,&error));
        const auto reopened=TimeSeriesStore::open(d->path,&error);QVERIFY2(reopened,qPrintable(error));QCOMPARE(reopened->fingerprint,d->fingerprint);
    }
    /** @brief Cooperative cancellation publishes neither a prepared model copy nor a CSV. */
    void cancellation()
    {
        auto d=makeDataset({"2024-12-01T00:00:00Z,0,Approved,"});QVERIFY(d);std::atomic_bool cancel{true};
        QVERIFY(!prepareAssignment(*d,options(*d),&cancel).ok());
        QString error;const auto path=QFileInfo(d->path).absolutePath()+"/cancelled.csv";
        QVERIFY(!TimeSeriesRunLayer::exportCsv({{d,d->series.first().id}},path,&error,&cancel));QVERIFY(!QFileInfo::exists(path));
    }
};
QTEST_GUILESS_MAIN(TestObservationWorkflow)
#include "test_timeseries_observation_workflow.moc"
