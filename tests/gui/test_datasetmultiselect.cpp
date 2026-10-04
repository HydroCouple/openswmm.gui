// SPDX-License-Identifier: GPL-3.0-or-later
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/tabulardatalayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "ui/dialogs/sublayerselectiondialog.h"
#include <QtTest>
#include <QQmlEngine>
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include <QFileDialog>
#include <QLineEdit>
#include <QMdiArea>
#include <QTimer>
#include <QThreadPool>
#include <QSettings>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <openswmm/engine/openswmm_engine.h>

class TestMainWindow : public SWMMVis {
public:
    ~TestMainWindow() {
        for(auto *window:findChildren<SWMMVisProjectWindow *>()) {
            window->setHasChanges(false);window->close();
        }
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
};

class TestDatasetMultiselect : public QObject {
    Q_OBJECT
    QString root;
    bool write(const QString &path, const QByteArray &bytes) {
        QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size();
    }
    QString path(const QString &name) const {return root+"/"+name;}
    SWMMVisProjectWindow *open(SWMMVis &app) {
        if(!QMetaObject::invokeMethod(&app,"onOpenProject",Qt::DirectConnection,Q_ARG(QString,path("model.inp"))))return nullptr;
        auto *window=app.findChild<SWMMVisProjectWindow *>();
        if(!window)return nullptr;
        for(int i=0;i<200 && !window->modelLayer()->engine();++i)QTest::qWait(10);
        return window->modelLayer()->engine()?window:nullptr;
    }
    bool select(SWMMVis &app,const char *slot,const QStringList &files,bool cancel=false) {
        bool visited=false,multi=false;QTimer timer;timer.setInterval(10);
        connect(&timer,&QTimer::timeout,&app,[&]{
            auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());if(!dialog)return;
            visited=true;multi=dialog->fileMode()==QFileDialog::ExistingFiles;
            timer.stop();
            if(cancel){dialog->reject();return;}
            dialog->setDirectory(QFileInfo(files.first()).absolutePath());
            QString quoted;for(const auto &file:files)quoted+=QStringLiteral("\"")+QFileInfo(file).fileName()+QStringLiteral("\" ");
            auto *editor=dialog->findChild<QLineEdit *>("fileNameEdit");
            if(!editor){dialog->reject();return;}
            editor->setText(quoted.trimmed());
            qInfo()<<"Selected datasets"<<dialog->selectedFiles();
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        });timer.start();
        QTimer watchdog;watchdog.setSingleShot(true);connect(&watchdog,&QTimer::timeout,&app,[]{if(auto*d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();});watchdog.start(10000);
        const bool invoked=QMetaObject::invokeMethod(&app,slot,Qt::DirectConnection);return invoked && visited && multi;
    }
    template<class T> int count(SWMMVisProjectWindow *window) {
        int n=0;for(auto *layer:window->canvas()->layers())if(qobject_cast<T *>(layer))++n;return n;
    }
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
        root=qEnvironmentVariable("SWMMVIS_DATASET_MULTISELECT_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/dataset_multiselect_2026-10-04/fixtures"));
        QVERIFY(QDir().mkpath(root));QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,path("settings"));
        GDALAllRegister();
        QByteArray deck="[TITLE]\nMultiple dataset fixture\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nCRS EPSG:32618\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 00:02:00\nREPORT_STEP 00:01:00\nROUTING_STEP 5\n[JUNCTIONS]\nJ 8 4 0 0 0\n[OUTFALLS]\nO 7.5 FREE NO\n[CONDUITS]\nC J O 30 .013 0 0 0\n[XSECTIONS]\nC CIRCULAR .5 0 0 0 1\n[COORDINATES]\nJ 0 0\nO 10 10\n[2D_OPTIONS]\nINTEGRATOR EXPLICIT\nLTS_TIERS 1\nMAX_TIMESTEP 5\nDRY_DEPTH .001\nREPORT_2D YES\nOUTPUT_FILE generated.h5\nOUTPUT_PRECISION FLOAT64\nREPORT_2D_VARIABLES DEPTH\n[2D_VERTICES]\n0 0 10\n10 0 10\n0 10 10\n[2D_TRIANGLES]\n0 1 2 .03 .2\n";
        QVERIFY(write(path("model.inp"),deck));
        QCOMPARE(swmm_engine_run(path("model.inp").toUtf8(),path("generated.rpt").toUtf8(),path("generated.out").toUtf8(),nullptr),SWMM_OK);
        for(const auto &name:{"a.out","b.out","a.h5","b.h5"}){QFile::remove(path(name));QVERIFY(QFile::copy(path(QString(name).endsWith(".out")?"generated.out":"generated.h5"),path(name)));}
        // Avoid auto-loading siblings: the result tests explicitly add copies.
        QVERIFY(write(path("model.inp"),deck.replace("OUTPUT_FILE generated.h5","OUTPUT_FILE absent.h5")));
        for(const auto &name:{"a.geojson","b.geojson"})QVERIFY(write(path(name),R"({"type":"FeatureCollection","crs":{"type":"name","properties":{"name":"EPSG:32618"}},"features":[{"type":"Feature","geometry":{"type":"Point","coordinates":[2,3]},"properties":{"id":1}}]})"));
        for(const auto &name:{"a.csv","b.csv"})QVERIFY(write(path(name),"x,y,value\n2,3,1\n"));
        for(const auto &name:{"broken.geojson","broken.tif","broken.out","broken.h5"})QVERIFY(write(path(name),"not a dataset"));
        for(const auto &name:{"a.tif","b.tif"}){
            auto *d=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path(name).toUtf8(),2,2,1,GDT_Byte,nullptr);QVERIFY(d);
            double transform[]={0,1,0,2,0,-1};d->SetGeoTransform(transform);OGRSpatialReference s;s.SetFromUserInput("EPSG:32618");d->SetSpatialRef(&s);QCOMPARE(d->GetRasterBand(1)->Fill(4),CE_None);GDALClose(d);
        }
        QFile::remove(path("multi.gpkg"));auto*d=GetGDALDriverManager()->GetDriverByName("GPKG")->Create(path("multi.gpkg").toUtf8(),0,0,0,GDT_Unknown,nullptr);QVERIFY(d);
        QVERIFY(d->CreateLayer("first",nullptr,wkbPoint));QVERIFY(d->CreateLayer("second",nullptr,wkbPoint));GDALClose(d);
    }
    void vectorBatchContinuesAfterBadFile() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);
        QVERIFY(select(app,"onAddVectorLayer",{path("a.geojson"),path("broken.geojson"),path("b.geojson")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<GISVectorLayer>(window),2,10000);
        QStringList paths;for(auto*l:window->canvas()->layers())if(auto*v=qobject_cast<GISVectorLayer*>(l))paths<<QFileInfo(v->filePath()).fileName();
        QVERIFY(paths.contains("a.geojson"));QVERIFY(paths.contains("b.geojson"));window->setHasChanges(false);QThreadPool::globalInstance()->waitForDone();
    }
    void rasterBatchContinuesAfterBadFile() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);
        QVERIFY(select(app,"onAddRasterLayer",{path("a.tif"),path("broken.tif"),path("b.tif")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<GISRasterLayer>(window),2,10000);window->setHasChanges(false);QThreadPool::globalInstance()->waitForDone();
    }
    void delimitedBatchAndCancel() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);
        QVERIFY(select(app,"onAddDelimitedData",{},true));QCOMPARE(count<TabularDataLayer>(window),0);
        QVERIFY(select(app,"onAddDelimitedData",{path("a.csv"),path("b.csv")}));QTRY_COMPARE(count<TabularDataLayer>(window),2);
    }
    void skippedVectorSublayersContinueBatch() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);bool skipped=false;QTimer picker;picker.setInterval(10);
        connect(&picker,&QTimer::timeout,&app,[&]{if(auto*d=qobject_cast<SublayerSelectionDialog*>(QApplication::activeModalWidget())){skipped=true;d->reject();}});picker.start();
        QVERIFY(select(app,"onAddVectorLayer",{path("multi.gpkg"),path("b.geojson")}));QTRY_COMPARE_WITH_TIMEOUT(count<GISVectorLayer>(window),1,10000);QVERIFY(skipped);picker.stop();window->setHasChanges(false);QThreadPool::globalInstance()->waitForDone();
    }
    void resultBatchesAndDeduplication() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);
        QVERIFY(select(app,"onAddSWMMResultsLayer",{path("a.out"),path("broken.out"),path("b.out")}));
        QTRY_COMPARE_WITH_TIMEOUT(count<SWMMResultsLayer>(window),3,10000);QThreadPool::globalInstance()->waitForDone();QTest::qWait(100);
        QVERIFY(window->activeResultsLayer());
        QCOMPARE(QFileInfo(window->activeResultsLayer()->resultsFilePath()).fileName(),QString("b.out"));
        QCOMPARE(window->activeResultsLayer()->totalTimeSteps(),2);
        QVERIFY(select(app,"onAddSWMMResultsLayer",{path("a.out"),path("b.out")}));QThreadPool::globalInstance()->waitForDone();QTest::qWait(100);QCOMPARE(count<SWMMResultsLayer>(window),3);
        QVERIFY(select(app,"onAdd2DResultsLayer",{path("a.h5"),path("broken.h5"),path("b.h5")}));QTRY_COMPARE_WITH_TIMEOUT(count<SWMM2DResultsLayer>(window),2,10000);
        QVERIFY(select(app,"onAdd2DResultsLayer",{path("a.h5"),path("b.h5")}));QTest::qWait(100);QCOMPARE(count<SWMM2DResultsLayer>(window),2);
    }
    void mainWindowTeardownAfterBatch() {
        auto *app=new SWMMVis;auto *window=open(*app);QVERIFY(window);
        QVERIFY(select(*app,"onAddDelimitedData",{path("a.csv"),path("b.csv")}));
        QTRY_COMPARE(count<TabularDataLayer>(window),2);
        QPointer<SWMMVisProjectWindow> target(window);delete app;QVERIFY(target.isNull());
    }
    void closedTargetStopsBatch() {
        TestMainWindow app;auto *window=open(app);QVERIFY(window);QPointer<SWMMVisProjectWindow> target(window);
        QVERIFY(select(app,"onAddRasterLayer",{path("a.tif"),path("b.tif")}));delete window;
        QTRY_VERIFY(target.isNull());QThreadPool::globalInstance()->waitForDone();QTest::qWait(100);
        QVERIFY(app.findChildren<GISRasterLayer *>().isEmpty());
    }
};
QTEST_MAIN(TestDatasetMultiselect)
#include "test_datasetmultiselect.moc"
