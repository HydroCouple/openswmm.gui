/**
 * @file test_timeseriesassignment_transaction.cpp
 * @brief Real model transactions verify prepared-copy binding, rollback and complete undo.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "ui/dialogs/timeseriesassignmentdialog.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "layers/timeserieslayer.h"
#include "plot/timeseriesrunlayer.h"
#include "timeseries/timeseriesstore.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "timeseries/timeseriesregistry.h"
#include "timeseries/timeseriesprovider.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_gages.h>
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineSeries>
#include <QMainWindow>
#include <QMdiArea>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTimeZone>
#include <QUuid>
#include <QValueAxis>
using namespace OpenSWMM::TimeSeries;
namespace
{
/** @brief Own a real model window and retain its fixture and undo snapshots for inspection. */
struct Fixture {
    QString directory, error;
    std::unique_ptr<OpenSWMMVisWorkspace> workspace;
    QMainWindow main;
    SWMMVisProjectWindow *window=nullptr;
    Fixture()
    {
        directory=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath(
            "../verification/timeseries_layers_2026-10-27/assignment-transactions/")+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(directory);
        const auto input=directory+"/model.inp";
        QFile source(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../extensions/data/model.inp"));
        if (!source.open(QIODevice::ReadOnly)) { error="Source fixture unavailable";return; }
        auto bytes=source.readAll();
        bytes+="\n[RAINGAGES]\nRG1 INTENSITY 0:15 1.0 TIMESERIES RAIN\n[TIMESERIES]\nRAIN 01/01/2026 00:00 0\nRAIN 01/01/2026 01:00 0\n";
        QFile file(input);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return;}file.write(bytes);file.close();
        workspace.reset(OpenSWMMVisWorkspace::newInstance({},nullptr));
        auto *mdi=new QMdiArea(&main);main.setCentralWidget(mdi);
        window=new SWMMVisProjectWindow(workspace.get(),input,mdi);mdi->addSubWindow(window);
        QList<QString> warnings,errors;if(!window->loadModel(warnings,errors))error=errors.join(';');
    }
    ~Fixture(){delete window;}
};
/** @brief Already reviewed finite model-unit samples used to isolate transaction behavior. */
PreparedAssignment prepared(AssignmentTarget target=AssignmentTarget::SeriesOnly,const QString &object={})
{
    PreparedAssignment p;p.options.modelSeriesName="OBS_COPY";p.options.target=target;p.options.targetObject=object;p.options.targetUnit="CMS";
    p.times={QDateTime(QDate(2026,1,1),QTime(0,0),QTimeZone::UTC),QDateTime(QDate(2026,1,1),QTime(1,0),QTimeZone::UTC)};
    p.values={0.,2.5};p.provenance={{"datasetId","test-source"},{"modelSeries","OBS_COPY"},{"sourceUnit","ft^3/s"},{"targetUnit","CMS"}};
    return p;
}
/**
 * @brief Create a retained, unlocated observation package for actual comparison-panel tests.
 * @param[in] rows CSV records without a header.
 * @param[in] basis Explicit instant or calendar-date source interpretation.
 * @param[out] error Fixture creation or import detail.
 * @return New native package path, empty on failure.
 */
QString plotPackage(const QStringList &rows, const QString &basis, QString *error)
{
    const QString directory=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath(
        "../verification/timeseries_layers_2026-10-27/assignment-transactions/plot-")+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(directory)) { *error="Could not create plot fixture directory";return {}; }
    QFile csv(directory+"/samples.csv");
    if (!csv.open(QIODevice::WriteOnly)) { *error=csv.errorString();return {}; }
    csv.write("time,value\n");csv.write(rows.join('\n').toUtf8());csv.write("\n");csv.close();
    const QJsonObject defaults{{"provider","fixture"},{"collection","observations"},{"siteId","00123"},
        {"sourceSeriesId","series-01"},{"parameter","00060"},{"title","Plot fixture"},
        {"statistic",basis=="calendar-date"?"mean":"instantaneous"},{"timeBasis",basis},{"unit","ft^3/s"}};
    const QJsonObject descriptor{{"schema","openswmm.timeseries-import/1"},
        {"datasetId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"title","Native plot acceptance"},
        {"sources",QJsonArray{QJsonObject{{"path","samples.csv"},{"defaults",defaults},
        {"columns",QJsonObject{{"time","time"},{"value","value"}}}}}}};
    QFile json(directory+"/import.json");
    if (!json.open(QIODevice::WriteOnly)) { *error=json.errorString();return {}; }
    json.write(QJsonDocument(descriptor).toJson());json.close();
    const auto output=directory+"/series.gpkg";
    return TimeSeriesStore::importDescriptor(directory+"/import.json",output,error)?output:QString();
}
/**
 * @brief Resolve the actual current chart series, excluding deferred stale views and cursor lines.
 * @param[in,out] panel Existing comparison panel; queued chart destruction is flushed.
 * @param[in] caption Native series legend prefix.
 * @return Borrowed rendered line, or null until the worker result is displayed.
 */
QLineSeries *nativeLine(openswmmvis::ui::ComparisonPlotDialog &panel, const QString &caption)
{
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    for (auto *view:panel.findChildren<QChartView*>())
        for (auto *series:view->chart()->series())
            if (auto *line=qobject_cast<QLineSeries*>(series);line && line->name().startsWith(caption)) return line;
    return nullptr;
}
}
/** @brief Test public native assignment entry points against a live loaded model. */
class TestTimeSeriesAssignmentTransaction : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Keep test settings in the reviewable repository evidence directory. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        const auto path=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath("../verification/timeseries_layers_2026-10-27/assignment-transactions/settings");
        QDir().mkpath(path);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,path);
    }
    /** @brief A real comparison panel displays an unlocated collection without model/results and releases it safely. */
    void standaloneNativeComparisonPlot()
    {
        QString error;
        const auto path=plotPackage({"2024-12-01T00:00:00Z,0","2024-12-01T01:00:00Z,1.5","2024-12-01T02:00:00Z,2.5"},"instant",&error);
        QVERIFY2(!path.isEmpty(),qPrintable(error));
        auto layer=std::make_unique<TimeSeriesLayer>();QVERIFY2(layer->openTimeSeries(path,{},&error),qPrintable(error));
        QCOMPARE(layer->seriesIds().size(),1);
        const auto id=layer->seriesIds().first(),caption=layer->seriesCaption(id);
        openswmmvis::ui::ComparisonPlotDialog panel;
        QCOMPARE(panel.model()->runSourceCount(),0);
        QCOMPARE(panel.addTimeSeriesLayerSeries(layer.get(),{id}),1);
        QCOMPARE(panel.model()->runSourceCount(),1);QCOMPARE(panel.model()->seriesCount(),1);
        QCOMPARE(panel.model()->rows().size(),1);QCOMPARE(panel.model()->rows().first().unitsLabel,QString("ft^3/s"));
        QVERIFY(panel.model()->spec(0).descriptor().label().contains("UTC"));
        QTRY_VERIFY_WITH_TIMEOUT(nativeLine(panel,caption) && nativeLine(panel,caption)->count()==3,10000);
        auto *line=nativeLine(panel,caption);QVERIFY(line);
        QCOMPARE(line->at(0).y(),0.);QCOMPARE(line->at(1).y(),1.5);
        QCOMPARE(qRound64(line->at(0).x()),QDateTime(QDate(2024,12,1),QTime(0,0),QTimeZone::UTC).toMSecsSinceEpoch());
        QVERIFY(line->pen().style()!=Qt::NoPen);
        QCOMPARE(panel.addTimeSeriesLayerSeries(layer.get(),{id,id}),0);
        layer.reset();
        QCOMPARE(panel.model()->runSourceCount(),0);QCOMPARE(panel.model()->seriesCount(),0);QVERIFY(panel.model()->rows().isEmpty());
        QVERIFY(!nativeLine(panel,caption));QVERIFY(QFileInfo::exists(path));
    }
    /** @brief Missing and daily values render as honest markers, and unavailable sources remove chart rows. */
    void nativeComparisonGapsAndCalendarDates()
    {
        QString error;
        const auto gapPath=plotPackage({"2024-12-01T00:00:00Z,0","2024-12-01T01:00:00Z,","2024-12-01T02:00:00Z,2"},"instant",&error);
        QVERIFY2(!gapPath.isEmpty(),qPrintable(error));
        const auto dailyPath=plotPackage({"2024-12-01,10","2024-12-02,20"},"calendar-date",&error);
        QVERIFY2(!dailyPath.isEmpty(),qPrintable(error));
        TimeSeriesLayer gap,daily;QVERIFY2(gap.openTimeSeries(gapPath,{},&error),qPrintable(error));QVERIFY2(daily.openTimeSeries(dailyPath,{},&error),qPrintable(error));
        openswmmvis::ui::ComparisonPlotDialog panel;
        const auto gapId=gap.seriesIds().first(),dailyId=daily.seriesIds().first();
        const auto gapCaption=gap.seriesCaption(gapId),dailyCaption=daily.seriesCaption(dailyId);
        QCOMPARE(panel.addTimeSeriesLayerSeries(&gap,{gapId}),1);QCOMPARE(panel.addTimeSeriesLayerSeries(&daily,{dailyId}),1);
        QCOMPARE(panel.model()->rows().size(),2);
        QTRY_VERIFY_WITH_TIMEOUT(nativeLine(panel,gapCaption) && nativeLine(panel,gapCaption)->count()==2,10000);
        QTRY_VERIFY_WITH_TIMEOUT(nativeLine(panel,dailyCaption) && nativeLine(panel,dailyCaption)->count()==2,10000);
        for (const auto &caption:{gapCaption,dailyCaption}) {
            auto *line=nativeLine(panel,caption);QVERIFY(line);QCOMPARE(line->pen().style(),Qt::NoPen);QVERIFY(line->pointsVisible());
        }
        QVERIFY(nativeLine(panel,gapCaption)->name().contains("missing"));
        QVERIFY(nativeLine(panel,dailyCaption)->name().contains("Calendar-date"));
        for (int i=0;i<panel.model()->seriesCount();++i) {
            openswmmvis::plot::SeriesData values;panel.model()->resolveSeries(i,values);
            QVERIFY(values.ok);QVERIFY(values.pointsOnly);QVERIFY(!values.allowPairing);QVERIFY(values.fullStatistics.has_value());
            QCOMPARE(values.fullStatistics->count,2);
        }
        QVERIFY(!gap.openTimeSeries(gapPath+".missing",gap.datasetId(),&error));
        QCOMPARE(panel.model()->runSourceCount(),1);QCOMPARE(panel.model()->seriesCount(),1);QCOMPARE(panel.model()->rows().size(),1);
        QVERIFY(!nativeLine(panel,gapCaption));QVERIFY(nativeLine(panel,dailyCaption));
    }
    /** @brief Copy creation is immediately visible and undo restores provenance together with engine state. */
    void createUndoRedoAndFreshReopen()
    {
        Fixture f;QVERIFY2(f.error.isEmpty(),qPrintable(f.error));QVERIFY(f.window);
        QString error;const auto p=prepared();
        const int before=f.window->canvas()->undoStack()->count();
        QVERIFY2(openswmmvis::ui::applyPreparedTimeSeries(f.window,p,&error),qPrintable(error));
        QCOMPARE(f.window->canvas()->undoStack()->count(),before+1);
        auto *registry=qobject_cast<openswmmvis::timeseries::TimeseriesRegistry*>(f.window->modelLayer()->ensureTimeseriesRegistry());
        QVERIFY(registry);auto *series=registry->findByName("OBS_COPY");QVERIFY(series);QCOMPARE(series->pointCount(),2);
        QCOMPARE(f.window->property("timeSeriesAssignments").toJsonArray().size(),1);
        f.window->canvas()->undoStack()->undo();QVERIFY(swmm_table_index(f.window->modelLayer()->engine(),"OBS_COPY")<0);
        QVERIFY(f.window->property("timeSeriesAssignments").toJsonArray().isEmpty());
        f.window->canvas()->undoStack()->redo();QVERIFY(swmm_table_index(f.window->modelLayer()->engine(),"OBS_COPY")>=0);
        QCOMPARE(f.window->property("timeSeriesAssignments").toJsonArray().size(),1);
        const auto output=f.directory+"/saved.inp";QCOMPARE(swmm_model_write(f.window->modelLayer()->engine(),output.toUtf8().constData()),SWMM_OK);
        auto engine=swmm_engine_create();QVERIFY(engine);QCOMPARE(swmm_engine_open(engine,output.toUtf8().constData(),"","",nullptr),SWMM_OK);
        QVERIFY(swmm_table_index(engine,"OBS_COPY")>=0);swmm_engine_close(engine);swmm_engine_destroy(engine);
    }
    /** @brief A failed destination binding rolls back newly created samples and provenance. */
    void bindingFailureIsAtomic()
    {
        Fixture f;QVERIFY2(f.error.isEmpty(),qPrintable(f.error));QString error;const int before=f.window->canvas()->undoStack()->count();
        QVERIFY(!openswmmvis::ui::applyPreparedTimeSeries(f.window,prepared(AssignmentTarget::NodeFlow,"MISSING"),&error));
        QVERIFY(swmm_table_index(f.window->modelLayer()->engine(),"OBS_COPY")<0);
        QVERIFY(f.window->property("timeSeriesAssignments").toJsonArray().isEmpty());QCOMPARE(f.window->canvas()->undoStack()->count(),before);
    }
    /** @brief Node binding creates exactly one input and duplicate FLOW binding is refused. */
    void nodeFlowBinding()
    {
        Fixture f;QVERIFY2(f.error.isEmpty(),qPrintable(f.error));QString error;
        auto p=prepared(AssignmentTarget::NodeFlow,"J1");
        const int count=swmm_ext_inflow_count(f.window->modelLayer()->engine());
        QVERIFY2(openswmmvis::ui::applyPreparedTimeSeries(f.window,p,&error),qPrintable(error));
        QCOMPARE(swmm_ext_inflow_count(f.window->modelLayer()->engine()),count+1);
        auto second=p;second.options.modelSeriesName="OBS_SECOND";
        QVERIFY(!openswmmvis::ui::applyPreparedTimeSeries(f.window,second,&error));QVERIFY(swmm_table_index(f.window->modelLayer()->engine(),"OBS_SECOND")<0);
        f.window->canvas()->undoStack()->undo();QCOMPARE(swmm_ext_inflow_count(f.window->modelLayer()->engine()),count);
        f.window->canvas()->undoStack()->redo();QCOMPARE(swmm_ext_inflow_count(f.window->modelLayer()->engine()),count+1);
    }
    /** @brief Stage binding preserves target references through undo and redo. */
    void outfallStageBinding()
    {
        Fixture f;QVERIFY2(f.error.isEmpty(),qPrintable(f.error));QString error;auto p=prepared(AssignmentTarget::OutfallStage,"O1");p.options.targetUnit="m";
        QVERIFY2(openswmmvis::ui::applyPreparedTimeSeries(f.window,p,&error),qPrintable(error));
        int index=-1;auto engine=f.window->modelLayer()->engine();QCOMPARE(swmm_node_get_outfall_timeseries(engine,swmm_node_index(engine,"O1"),&index),SWMM_OK);
        QCOMPARE(index,swmm_table_index(engine,"OBS_COPY"));
        f.window->canvas()->undoStack()->undo();QVERIFY(swmm_table_index(f.window->modelLayer()->engine(),"OBS_COPY")<0);
        f.window->canvas()->undoStack()->redo();engine=f.window->modelLayer()->engine();QCOMPARE(swmm_node_get_outfall_timeseries(engine,swmm_node_index(engine,"O1"),&index),SWMM_OK);QCOMPARE(index,swmm_table_index(engine,"OBS_COPY"));
    }
    /** @brief Gage binding restores the previous series when the entire assignment is undone. */
    void rainGageBinding()
    {
        Fixture f;QVERIFY2(f.error.isEmpty(),qPrintable(f.error));QString error;auto p=prepared(AssignmentTarget::RainGage,"RG1");p.options.targetUnit="mm/hr";p.options.rainfallIntervalSeconds=900;
        QVERIFY2(openswmmvis::ui::applyPreparedTimeSeries(f.window,p,&error),qPrintable(error));
        char name[128]={};auto engine=f.window->modelLayer()->engine();QCOMPARE(swmm_gage_get_timeseries(engine,swmm_gage_index(engine,"RG1"),name,sizeof name),SWMM_OK);QCOMPARE(QString::fromUtf8(name),QString("OBS_COPY"));
        f.window->canvas()->undoStack()->undo();engine=f.window->modelLayer()->engine();QCOMPARE(swmm_gage_get_timeseries(engine,swmm_gage_index(engine,"RG1"),name,sizeof name),SWMM_OK);QCOMPARE(QString::fromUtf8(name),QString("RAIN"));
    }
};
/**
 * @brief Run assignment transaction cases within the native layer test application.
 * @param argc Number of Qt Test command-line arguments.
 * @param argv Qt Test command-line arguments owned by the existing application.
 * @return Qt Test exit status; no second QApplication is created.
 */
int runTimeSeriesAssignmentTransactionTests(int argc, char **argv)
{
    TestTimeSeriesAssignmentTransaction tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_timeseriesassignment_transaction.moc"
