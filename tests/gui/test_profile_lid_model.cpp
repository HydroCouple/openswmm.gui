#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "animation/animationcontroller.h"
#include "simulation/simulationrunner.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include <QSignalSpy>
#include <QAction>
#include "plot/profilenetworkadapter.h"
#include "plot/profileplotwidget.h"
#include "ui/dialogs/profileplotdialog.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptoolselectprofile.h"
#include "map/profilepathoverlay.h"
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <cmath>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QMouseEvent>
#include <QPainter>
#include <QTest>
#include <QToolBar>
#include <QProgressBar>

class TestProfileLidModel : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("OpenSWMMProfileLidTests");
        QCoreApplication::setApplicationName("ProfileLid");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,outputDir()+"/settings");
    }
    void storageAndOutfallEndpoints_data()
    {
        QTest::addColumn<bool>("projected");
        QTest::newRow("local-feet")<<false;
        QTest::newRow("geographic-on-mercator")<<true;
    }
    void storageAndOutfallEndpoints()
    {
        QFETCH(bool,projected);
        const QString file=QFINDTESTDATA("data/profile_lid_active_backwater.inp");
        QVERIFY(!file.isEmpty());
        SWMMModelLayer model(file); QList<QString> warnings,errors;
        QVERIFY2(model.loadModel(warnings,errors),qPrintable(errors.join("\n")));
        model.setSRS(projected ? SpatialReferenceSystem::fromAuthCode("EPSG",4326)
                               : SpatialReferenceSystem::localFromMapUnits("FEET"),true);
        MapCanvas canvas;
        canvas.resize(1000,700);
        canvas.setCanvasSRS(projected ? SpatialReferenceSystem::fromAuthCode("EPSG",3857)
                                     : SpatialReferenceSystem::localFromMapUnits("FEET"),true);
        canvas.addLayer(&model,false);
        canvas.setExtent(projected ? MapExtent(-20000000,-6000000,14000000,6000000)
                                   : MapExtent(-30,-70,230,70),false);
        OpenSWMMVisMapToolSelectProfile tool(&canvas);
        ProfileRouter::Path accepted;
        connect(&tool,&OpenSWMMVisMapToolSelectProfile::profilePathSelected,
                &tool,[&](const ProfileRouter::Path &path) { accepted=path; });
        auto engine=model.engine();
        SWMMLinkPropertyAdapter overflowProperties(engine,"W_B");
        QCOMPARE(overflowProperties.lidOutletLayer(),0);
        QCOMPARE(overflowProperties.lidOutletPosition(),SWMMLinkPropertyAdapter::NoLayerAnchor);
        SWMMLinkPropertyAdapter drainProperties(engine,"V_BR");
        QCOMPARE(drainProperties.lidOutletLayer(),3);
        QCOMPARE(drainProperties.lidOutletPosition(),SWMMLinkPropertyAdapter::LayerBottom);
        const int a=swmm_node_index(engine,"A");
        auto scenePoint=[&](int node) {
            double x=0,y=0;
            model.cachedNodeCoord(node,&x,&y);
            model.transformLayerToCanvas(x,y,x,y);
            return QPointF(x,-y);
        };
        auto click=[&](const QPointF &scene) {
            int px=0,py=0; canvas.toPixelCoords(scene.x(),-scene.y(),px,py);
            QMouseEvent event(QEvent::MouseButtonPress,QPointF(px,py),QPointF(px,py),
                              Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            tool.mousePressEvent(&event);
        };
        if(projected) {
            // Prove this exercises a real reprojection/wrap, not a silent
            // identity fallback when projection data is unavailable.
            QVERIFY(scenePoint(swmm_node_index(engine,"R")).x() < -17000000);
            QVERIFY(scenePoint(swmm_node_index(engine,"B")).x() > 11000000);
        }
        for(const char *name : {"B","R","FA","FB"}) {
            const int end=swmm_node_index(engine,name);
            accepted={}; tool.clearSelection(); tool.activate();
            click(scenePoint(a)); click(scenePoint(end));
            QTRY_VERIFY_WITH_TIMEOUT(!accepted.nodes.isEmpty(),3000);
            QCOMPARE(accepted.nodes.first(),a);
            QCOMPARE(accepted.nodes.last(),end);
            QVERIFY(!accepted.linkIds.isEmpty());
            const auto path=ProfileNetworkAdapter::buildPathStaticFromModel(&model,accepted);
            QCOMPARE(path.nodes.first().lidLayers.size(),3);
            for(const auto &node:path.nodes) for(const auto &branch:node.branches) {
                if(branch.name=="V_BR" && !node.lidLayers.isEmpty()) {
                    QCOMPARE(branch.lidLayer,3); QVERIFY(!branch.lidAnchorTop);
                }
                if(branch.name=="W_B") QCOMPARE(branch.lidLayer,0);
            }
            if(QString::fromLatin1(name)=="R") {
                ProfilePlotDialog dialog(&model,nullptr,accepted,nullptr);
                auto *plot=dialog.findChild<ProfilePlotWidget *>(); QVERIFY(plot);
                plot->resize(1000,550); plot->fitToExtent();
                QDir().mkpath(outputDir());
                QVERIFY(plot->grab().save(outputDir()+"/active-backwater-profile.png"));
            }
            // Both the path and its halos must use the same projected
            // coordinates as the displayed nodes, including wrapped R.
            ProfilePathOverlay overlay(&model);
            overlay.setPaths({accepted}); overlay.setEndpoints(a,end);
            QImage image(1000,700,QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white); QPainter painter(&image);
            QVector<QPointF> observed;
            overlay.paintOverlay(painter,[&](const QPointF &point) {
                observed.append(point);
                int px=0,py=0; canvas.toPixelCoords(point.x(),-point.y(),px,py);
                return QPointF(px,py);
            });
            QVERIFY(observed.contains(scenePoint(a)));
            QVERIFY(observed.contains(scenePoint(end)));
        }
        // A link click must choose its nearer DISPLAYED endpoint even
        // when the model coordinates are in another CRS.
        const int b=swmm_node_index(engine,"B");
        accepted={}; tool.clearSelection(); tool.activate();
        click(scenePoint(a));
        click(scenePoint(a)*0.2+scenePoint(b)*0.8);
        QTRY_VERIFY_WITH_TIMEOUT(!accepted.nodes.isEmpty(),3000);
        QCOMPARE(accepted.nodes.last(),b);

        accepted={}; tool.clearSelection();
        model.setSelectedElements({{"A",SWMMModelLayer::kKindNode},
                                   {"R",SWMMModelLayer::kKindNode}});
        tool.activate();
        QTRY_VERIFY_WITH_TIMEOUT(!accepted.nodes.isEmpty(),3000);
        QCOMPARE(accepted.nodes.first(),a);
        QCOMPARE(accepted.nodes.last(),swmm_node_index(engine,"R"));
        model.setSelectedElements({});

        // The reported review path runs backwards through the overflow weir,
        // then out through B's bottom drain (FB -> B -> R).
        accepted={}; tool.clearSelection(); tool.activate();
        click(scenePoint(swmm_node_index(engine,"FB")));
        click(scenePoint(swmm_node_index(engine,"R")));
        QTRY_COMPARE_WITH_TIMEOUT(accepted.linkIds.size(),2,3000);
        const auto reviewPath=ProfileNetworkAdapter::buildPathStaticFromModel(&model,accepted);
        QCOMPARE(reviewPath.links[0].name,QString("W_B"));
        QVERIFY(reviewPath.links[0].reversed);
        QCOMPARE(reviewPath.links[1].name,QString("V_BR"));
        ProfilePlotWidget reviewPlot; reviewPlot.resize(1000,550);
        reviewPlot.setPath(reviewPath); reviewPlot.fitToExtent();
        QVERIFY(reviewPlot.grab().save(outputDir()+"/overflow-bottom-profile.png"));

        // A wrapped-coordinate fallback must respect hidden node state.
        model.setObjectVisible("R",false);
        model.setObjectVisible("V_BR",false);
        accepted={}; tool.clearSelection(); tool.activate();
        click(scenePoint(a)); click(scenePoint(swmm_node_index(engine,"R")));
        QCoreApplication::processEvents();
        QVERIFY(accepted.nodes.isEmpty());
        tool.clearSelection();
        canvas.takeLayer(canvas.layers().indexOf(&model),false);
    }
    void authoredStackUsesProjectElevations_data()
    {
        QTest::addColumn<bool>("si");
        QTest::newRow("US-inches-to-feet")<<false;
        QTest::newRow("SI-mm-to-metres")<<true;
    }
    void authoredStackUsesProjectElevations()
    {
        QFETCH(bool,si);
        QDir().mkpath(outputDir());
        const QString file=outputDir()+(si?"/layers-si.inp":"/layers-us.inp");
        QFile input(file); QVERIFY(input.open(QIODevice::WriteOnly));
        const QString text=QStringLiteral(
            "[OPTIONS]\nFLOW_UNITS %1\nFLOW_ROUTING DYNWAVE\n"
            "[STORAGE]\nS %2 %3 0 FUNCTIONAL 0 0 100 0 0\n"
            "Plain %2 2 0 FUNCTIONAL 0 0 100 0 0\n"
            "[OUTFALLS]\nO %2 FREE NO\n"
            "[ORIFICES]\nD S O SIDE 0 .6 NO 0\n"
            "[XSECTIONS]\nD CIRCULAR .2 0 0 0\n"
            "[LID_CONTROLS]\nStack NODE\nStack SURFACE %4 .1\n"
            "Stack MEDIA %5 .45 .2 .08 .01 10 3\n"
            "Stack AGGREGATE %4 .4 100\n"
            "Stack MEDIA %4 .45 .2 .08 .01 10 3\nStack BOTTOM 0 0\n"
            "[LID_NODES]\nS Stack 10\n"
            "[COORDINATES]\nS 0 0\nO 100 0\nPlain 50 50\n")
            .arg(si?"CMS":"CFS",si?"30.48":"100",si?"0.762":"2.5",
                 si?"152.4":"6",si?"304.8":"12");
        input.write(text.toUtf8()); input.close();
        SWMMModelLayer model(file); QList<QString> warnings,errors;
        QVERIFY2(model.loadModel(warnings,errors),qPrintable(errors.join("\n")));
        auto engine=model.engine();
        ProfileRouter::Path route;
        route.nodes={swmm_node_index(engine,"S"),swmm_node_index(engine,"O")};
        route.linkIds={swmm_link_index(engine,"D")};
        const auto path=ProfileNetworkAdapter::buildPathStaticFromModel(&model,route);
        QCOMPARE(path.nodes.size(),2);
        const auto &node=path.nodes.first();
        const double unit=si?0.3048:1.0;
        const double invert=100*unit;
        QCOMPARE(node.kind,ProfileBuilder::NodeKind::Storage);
        QCOMPARE(node.lidLayers.size(),4); // BOTTOM is not an extra material band
        using Layer=ProfileBuilder::NodeLidLayer;
        QCOMPARE(node.lidLayers[0].kind,Layer::Surface);
        QCOMPARE(node.lidLayers[1].kind,Layer::Media);
        QCOMPARE(node.lidLayers[2].kind,Layer::Aggregate);
        QCOMPARE(node.lidLayers[3].kind,Layer::Media);
        double top=invert+2.5*unit;
        for(int i=0;i<4;++i) {
            const auto &layer=node.lidLayers[i];
            const double thickness=(i==1?1.0:0.5)*unit;
            QVERIFY(std::abs(layer.topElev-top)<1e-8);
            QVERIFY(std::abs(layer.topElev-layer.bottomElev-thickness)<1e-8);
            top=layer.bottomElev;
        }
        QVERIFY(std::abs(top-invert)<1e-8);
        QVERIFY(path.nodes.last().lidLayers.isEmpty());
        ProfileRouter::Path ordinary;
        ordinary.nodes={swmm_node_index(engine,"Plain")};
        const auto plain=ProfileNetworkAdapter::buildPathStaticFromModel(&model,ordinary);
        QCOMPARE(plain.nodes.size(),1); QVERIFY(plain.nodes.first().lidLayers.isEmpty());

        ProfilePlotDialog dialog(&model,nullptr,route,nullptr);
        auto *plot=dialog.findChild<ProfilePlotWidget *>(); QVERIFY(plot);
        const auto range=plot->visibleDataRange();
        QVERIFY(range.top()>invert-1);
        QVERIFY(range.top()<invert && range.bottom()>invert+2.5*unit);
        int control=-1; double saturation=0;
        QCOMPARE(swmm_node_get_lid(engine,route.nodes[0],&control,&saturation),SWMM_OK);
        QVector<SWMM_LidNodeLayer> layers(swmm_lid_node_layer_count(engine,control));
        for(int i=0;i<layers.size();++i)
            QCOMPARE(swmm_lid_node_layer_get(engine,control,i,&layers[i]),SWMM_OK);
        layers[1].params[0]*=2;
        QCOMPARE(swmm_lid_node_layers_set(engine,control,layers.data(),layers.size()),SWMM_OK);
        model.markEdited(); QCoreApplication::processEvents();
        QVERIFY(plot->visibleDataRange().bottom()>invert+3.5*unit);
        plot->zoomBy(0.75); const auto manual=plot->visibleDataRange();
        layers[1].params[0]*=2;
        QCOMPARE(swmm_lid_node_layers_set(engine,control,layers.data(),layers.size()),SWMM_OK);
        model.markEdited(); QCoreApplication::processEvents();
        QCOMPARE(plot->visibleDataRange(),manual);
    }
    void recordedStatesFollowProfileCursor()
    {
        QDir().mkpath(outputDir());
        const QString stem=outputDir()+"/backwater-water-states";
        QFile source(QFINDTESTDATA("data/profile_lid_active_backwater.inp")); QVERIFY(source.open(QIODevice::ReadOnly));
        auto input=source.readAll(); input.replace("END_DATE 01/02/2004","END_DATE 01/01/2004");
        input.replace("END_TIME 00:00:00","END_TIME 02:00:00");
        { QFile file(stem+".inp"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(input); }
        SWMMModelLayer model(stem+".inp"); QList<QString> warnings,errors;
        QVERIFY2(model.loadModel(warnings,errors),qPrintable(errors.join("\n")));
        SWMMResultsLayer results(stem+".out",&model);
        AnimationController animation; animation.setPrimaryLayer(&results);
        ProfileRouter::Path route;
        route.nodes={swmm_node_index(model.engine(),"A"),swmm_node_index(model.engine(),"B"),swmm_node_index(model.engine(),"R")};
        route.linkIds={swmm_link_index(model.engine(),"V_AB"),swmm_link_index(model.engine(),"V_BR")};
        ProfilePlotDialog dialog(&model,&animation,route,nullptr);
        auto *plot=dialog.findChild<ProfilePlotWidget *>(); QVERIFY(plot);
        auto *head=dialog.findChild<QAction *>("showLidOutletHead"); QVERIFY(head); head->setChecked(true);
        SimulationRunner runner(986,"LID profile verification",stem+".inp",stem+".rpt",stem+".out");
        QSignalSpy finished(&runner,&SimulationRunner::finished);
        openswmmvis::io::LidStateFrame live;
        connect(&runner,&SimulationRunner::progressChanged,&dialog,[&] {
            QList<QString> openingErrors;
            if (!results.outputHandle()) results.openResultsLive(openingErrors);
            results.refreshLive();
            animation.seekToLast();
            auto frame=results.lidStateAt(results.currentDateTime(),{"A","B"});
            if (!frame.nodes.isEmpty()) live=frame;
        });
        QString pathError;
        QVERIFY(!SimulationRunner::validateRunPaths(stem+".inp",stem+".rpt",stem+".out",
            {openswmmvis::io::lidStatePath(stem+".out")},{},&pathError));
        auto hasHydraulicRows = [&] {
            for (const auto &binding : plot->series()) {
                if (binding.kind != ProfileBuilder::OutputKind::HGL || !binding.visible || !binding.derived) continue;
                const auto &rows = binding.derived->hglByPeriod;
                if (rows.size() != results.totalTimeSteps() || rows.isEmpty()) return false;
                return std::all_of(rows.last().cbegin(), rows.last().cend(),
                                   [](double value) { return std::isfinite(value); });
            }
            return false;
        };
        runner.start();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count()>0,120000);
        QVERIFY2(finished[0][1].toBool(),qPrintable(finished[0][3].toString()));
        QVERIFY2(!live.nodes.isEmpty(),"A live progress tick must expose recorded LID states");
        results.refreshLive(); animation.seekToLast();
        QTRY_VERIFY_WITH_TIMEOUT(hasHydraulicRows(),10000);
        results.closeResults(); errors.clear();
        QVERIFY2(results.openResults(warnings,errors),qPrintable(errors.join("\n")));
        const auto saved=results.lidStateAt(live.time,{"A","B"});
        QCOMPARE(saved.time,live.time);
        QCOMPARE(saved.nodes[0].cells[0].pressure,live.nodes[0].cells[0].pressure);
        animation.seekToLast();
        QTRY_COMPARE(plot->lidWaterState().nodes.size(),2);
        QTRY_VERIFY_WITH_TIMEOUT(hasHydraulicRows(),10000);
        const auto last=plot->lidWaterState().time; QVERIFY(last.isValid());
        const auto range=plot->visibleDataRange();
        animation.seekToFirst();
        QVERIFY(plot->lidWaterState().time<last);
        animation.seekToLast(); QCOMPARE(plot->lidWaterState().time,last);
        QCOMPARE(plot->visibleDataRange(),range);
        dialog.resize(1200,760); dialog.show(); QCoreApplication::processEvents();
        QVERIFY(plot->grab().save(outputDir()+"/backwater-lid-water-profile.png"));
        animation.setPrimaryLayer(nullptr);
        QVERIFY(plot->lidWaterState().nodes.isEmpty());
    }

    void savedResultsOpenedAfterProfile_data()
    {
        QTest::addColumn<bool>("lid");
        QTest::newRow("ordinary-conduit") << false;
        QTest::newRow("lid-bottom-outlet") << true;
    }
    void savedResultsOpenedAfterProfile()
    {
        QFETCH(bool,lid);
        QDir().mkpath(outputDir());
        const QString stem=outputDir()+(lid?"/late-lid-results":"/late-conduit-results");
        QByteArray input;
        if(lid) {
            QFile source(QFINDTESTDATA("data/profile_lid_active_backwater.inp"));
            QVERIFY(source.open(QIODevice::ReadOnly)); input=source.readAll();
            input.replace("END_DATE 01/02/2004","END_DATE 01/01/2004");
            input.replace("END_TIME 00:00:00","END_TIME 00:10:00");
        } else {
            input="[OPTIONS]\nFLOW_UNITS CFS\nFLOW_ROUTING DYNWAVE\n"
                  "START_DATE 01/01/2004\nEND_DATE 01/01/2004\nEND_TIME 00:10:00\n"
                  "REPORT_STEP 00:01:00\nROUTING_STEP 1\n"
                  "[JUNCTIONS]\nJ 10 1 0 0 0\n[OUTFALLS]\nO 9 FREE NO\n"
                  "[CONDUITS]\nC J O 100 .013 0 0 0 0\n"
                  "[XSECTIONS]\nC CIRCULAR .1 0 0 0 1\n"
                  "[INFLOWS]\nJ FLOW Q FLOW 1 1 0\n"
                  "[TIMESERIES]\nQ 01/01/2004 00:00 5\nQ 01/01/2004 00:10 5\n"
                  "[REPORT]\nNODES ALL\nLINKS ALL\n"
                  "[COORDINATES]\nJ 0 0\nO 100 0\n";
        }
        { QFile file(stem+".inp"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(input); }
        SimulationRunner runner(987,"Profile result refresh",stem+".inp",stem+".rpt",stem+".out");
        QSignalSpy finished(&runner,&SimulationRunner::finished); runner.start();
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),60000);
        QVERIFY2(finished[0][1].toBool(),qPrintable(finished[0][3].toString()));
        SWMMModelLayer model(stem+".inp"); QList<QString> warnings,errors;
        QVERIFY2(model.loadModel(warnings,errors),qPrintable(errors.join("\n")));
        SWMMResultsLayer results(stem+".out",&model);
        results.setProfileHglLinePen(QPen(QColor(12,40,230),3));
        results.setProfileEglLinePen(QPen(QColor(230,10,190),3,Qt::DashLine));
        AnimationController animation; animation.setPrimaryLayer(&results);
        ProfileRouter::Path route;
        route.nodes={swmm_node_index(model.engine(),lid?"B":"J"),swmm_node_index(model.engine(),lid?"R":"O")};
        route.linkIds={swmm_link_index(model.engine(),lid?"V_BR":"C")};
        ProfilePlotDialog dialog(&model,&animation,route,nullptr);
        auto *plot=dialog.findChild<ProfilePlotWidget *>(); QVERIFY(plot);
        plot->options()->setLegendVisible(false); plot->options()->setShowNodeRoses(false);
        plot->options()->setCurrentHglFill(false);
        QTRY_COMPARE(plot->series().size(),4);
        QVERIFY(plot->series()[0].derived->hglByPeriod.isEmpty());
        QVERIFY2(results.openResults(warnings,errors),qPrintable(errors.join("\n")));
        animation.seekToLast();
        auto completeRows=[&] {
            if(plot->series().size()!=4) return false;
            const auto &data=*plot->series()[0].derived;
            if(data.hglByPeriod.size()!=results.totalTimeSteps() || data.eglByPeriod.size()!=results.totalTimeSteps()) return false;
            if(data.hglByPeriod.isEmpty() || data.hglByPeriod.last().size()!=2) return false;
            return std::all_of(data.hglByPeriod.last().cbegin(),data.hglByPeriod.last().cend(),
                               [](double value){return std::isfinite(value);});
        };
        QTRY_VERIFY_WITH_TIMEOUT(completeRows(),5000);
        const auto previous=plot->series()[0].derived;
        results.closeResults();
        QVERIFY2(results.openResults(warnings,errors),qPrintable(errors.join("\n")));
        animation.seekToLast();
        QTRY_VERIFY_WITH_TIMEOUT(plot->series()[0].derived!=previous,5000);
        QTRY_VERIFY_WITH_TIMEOUT(completeRows(),5000);
        dialog.resize(1000,650); dialog.show(); QCoreApplication::processEvents();
        const auto automatic=plot->grab().toImage();
        // An explicit cursor push must not be needed to reveal the lines.
        plot->setCurrentPeriod(results.currentTimeStep());
        QCOMPARE(plot->grab().toImage(),automatic);
        QVERIFY(automatic.save(stem+"-profile.png"));
        auto *hgl=dialog.findChild<QAction *>("showCurrentHgl");
        auto *egl=dialog.findChild<QAction *>("showCurrentEgl");
        auto *flood=dialog.findChild<QAction *>("showFloodingGlyphs");
        QVERIFY(hgl && egl && flood);
        for(auto *action : {hgl,egl,flood}) {
            QVERIFY(action->isCheckable()); QVERIFY(action->isChecked());
            QVERIFY(!action->icon().pixmap(20,20).isNull());
        }
        const auto sourceData=plot->series()[0].derived;
        plot->zoomBy(.9); const auto manualRange=plot->visibleDataRange();
        hgl->trigger();
        QTRY_VERIFY(!plot->series()[0].visible);
        QVERIFY(!plot->options()->currentHglLine());
        QVERIFY(!plot->options()->currentHglFill());
        QVERIFY(flood->isChecked());
        hgl->trigger();
        QTRY_VERIFY(plot->series()[0].visible);
        QVERIFY(plot->options()->currentHglLine());
        QVERIFY(!plot->options()->currentHglFill()); // restore line-only choice
        egl->trigger(); QTRY_VERIFY(!plot->series()[1].visible);
        plot->options()->setCurrentEgl(true); // Display Options → toolbar
        QVERIFY(egl->isChecked()); QTRY_VERIFY(plot->series()[1].visible);
        flood->trigger(); QVERIFY(!plot->options()->showFloodingGlyphs());
        plot->options()->setShowFloodingGlyphs(true); QVERIFY(flood->isChecked());
        const auto loadingBars=dialog.findChildren<QProgressBar *>();
        QTRY_VERIFY(std::none_of(loadingBars.cbegin(),loadingBars.cend(),
                                [](const auto *bar){return bar->isVisible();}));
        QCOMPARE(plot->series()[0].derived,sourceData); // no file refetch on toggles
        QCOMPARE(plot->visibleDataRange(),manualRange);
        plot->fitToExtent(); QCoreApplication::processEvents();
        auto *toolbar=dialog.findChild<QToolBar *>("profileToolbar"); QVERIFY(toolbar);
        QVERIFY(toolbar->actions().indexOf(hgl)<toolbar->actions().indexOf(egl));
        QVERIFY(toolbar->actions().indexOf(egl)<toolbar->actions().indexOf(flood));
        QVERIFY(dialog.grab().save(stem+"-dialog.png"));
    }

private:
    static QString outputDir() { return qEnvironmentVariable("SWMMVIS_PROFILE_FIT_OUTPUT","tests/output/profile_lid"); }
};
QTEST_MAIN(TestProfileLidModel)
#include "test_profile_lid_model.moc"
