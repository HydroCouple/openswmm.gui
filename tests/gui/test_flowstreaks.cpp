// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/flowstreaknode.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/mapcanvas.h"
#include "project/projectserializer.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include <QTest>
#include <QSignalSpy>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSettings>
#include <QtQml/qqml.h>
#include <QQuickWidget>
#include <QQuickWindow>
#include <cmath>
using namespace OpenSWMM::Render;

class FlowSource : public IMesh2DSource {
public:
    bool live=false,fluxEnabled=true;
    float speed=2;
    int revision=0,reads=0;
    std::vector<float> depths{.25f,.5f,.25f};
    int vertexCount() const override {return 4;}
    int triangleCount() const override {return 2;}
    int timeCount() const override {return int(depths.size());}
    bool isLive() const override {return live;}
    int frameContentRevision(int) const override {return revision;}
    bool readMeshGeometry(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,
                          std::vector<std::array<int,3>>&t) override {
        x={0,100,100,0};y={0,0,100,100};z={0,0,0,0};t={{0,1,2},{0,2,3}};return true;
    }
    bool readDepthsAt(int t,std::vector<float>&d) override {++reads;d.assign(2,depths.at(size_t(t)));return true;}
    QDateTime simTimeAt(int i) const override {return QDateTime::fromSecsSinceEpoch(1700000000+i*60,Qt::UTC);}
    bool readEdgeGeometry(std::vector<float>&len,std::vector<float>&nx,std::vector<float>&ny) override {
        const float n=std::sqrt(.5f);
        len={100,100/n,100,0,100,100,100/n,0};
        nx={1,-n,0,0,0,-1,n,0};ny={0,n,-1,0,1,0,-n,0};return true;
    }
    bool readEdgeFluxAt(int t,std::vector<float>&f) override {
        if(!fluxEnabled){f.clear();return false;}
        std::vector<float>len,nx,ny;readEdgeGeometry(len,nx,ny);f.resize(len.size());
        for(size_t i=0;i<f.size();++i) f[i]=len[i]*nx[i]*depths.at(size_t(t))*speed;
        return true;
    }
};
class QuadFlowSource : public FlowSource {
public:
    int triangleCount() const override {return 1;}
    bool readCells(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,
                   std::vector<std::array<int,4>>&cells) override {
        x={0,100,100,0};y={0,0,100,100};z={0,0,0,0};cells={{0,1,2,3}};return true;
    }
    bool readDepthsAt(int t,std::vector<float>&d) override {d.assign(1,depths.at(size_t(t)));return true;}
    bool readEdgeGeometry(std::vector<float>&len,std::vector<float>&nx,std::vector<float>&ny) override {
        len={100,100,100,100};nx={1,0,-1,0};ny={0,1,0,-1};return true;
    }
};
class GridFlowSource : public FlowSource {
public:
    GridFlowSource(int cols=400,int rows=400):cols(cols),rows(rows) {live=true;}
    int cols,rows;
    int vertexCount() const override {return (cols+1)*(rows+1);}
    int triangleCount() const override {return cols*rows*2;}
    bool readMeshGeometry(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,
                          std::vector<std::array<int,3>>&triangles) override {
        x.resize(vertexCount());y.resize(vertexCount());z.assign(vertexCount(),0);
        for(int r=0;r<=rows;++r)for(int c=0;c<=cols;++c) {
            const int i=r*(cols+1)+c;x[i]=c;y[i]=r;
        }
        triangles.reserve(triangleCount());
        for(int r=0;r<rows;++r)for(int c=0;c<cols;++c) {
            const int a=r*(cols+1)+c,b=a+1,d=a+cols+1,e=d+1;
            triangles.push_back({a,b,e});triangles.push_back({a,e,d});
        }
        return true;
    }
    bool readDepthsAt(int t,std::vector<float>&d) override {
        ++reads;d.assign(triangleCount(),depths.at(size_t(t)));return true;
    }
    bool readEdgeGeometry(std::vector<float>&len,std::vector<float>&nx,std::vector<float>&ny) override {
        const float s=std::sqrt(.5f);
        const float l[8]={1,1/s,1,0,1,1,1/s,0},a[8]={1,-s,0,0,0,-1,s,0},b[8]={0,s,-1,0,1,0,-s,0};
        const size_t n=size_t(triangleCount())*4;len.resize(n);nx.resize(n);ny.resize(n);
        for(size_t i=0;i<n;++i){len[i]=l[i%8];nx[i]=a[i%8];ny[i]=b[i%8];}
        return true;
    }
    bool readEdgeFluxAt(int t,std::vector<float>&flux) override {
        const float sign[8]={1,-1,0,0,0,-1,1,0};flux.resize(size_t(triangleCount())*4);
        for(size_t i=0;i<flux.size();++i) {
            const bool right=(i/8)%size_t(cols)>=size_t(cols/2);
            flux[i]=sign[i%8]*depths.at(size_t(t))*speed*(right?2:1);
        }
        return true;
    }
};
class TestRenderer:public SWMM2DResultsQSGRenderer {
public:using SWMM2DResultsQSGRenderer::updatePaintNode;
};
static FlowStreakNode *findFlow(QSGNode *n) {
    if(auto *f=dynamic_cast<FlowStreakNode*>(n))return f;
    for(auto *c=n?n->firstChild():nullptr;c;c=c->nextSibling())if(auto *f=findFlow(c))return f;
    return nullptr;
}
class FlowStreaksTest:public QObject {
    Q_OBJECT
private:
    static std::shared_ptr<const FlowStreakBatch> paths(SWMM2DResultsLayer &layer) {
        return layer.flowStreaks(layer.m_sceneBBox,8.,layer.frameRevision());
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("OpenSWMMFlowTests");
        QCoreApplication::setApplicationName("FlowStreaks");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,
                          QDir::current().absoluteFilePath("tests/verification/flow_streaks/settings"));
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
    }
    void layerAndStylesPersistIndependently() {
        SWMM2DResultsLayer a,b;
        auto *sub=a.flowStreakSublayer();QVERIFY(a.sublayers().contains(sub));
        QCOMPARE(sub->id(),QString("results2d.flowStreaks"));QVERIFY(!sub->isVisible());
        QVERIFY(sub->streakStyle()->animated());QVERIFY(a.liveRenderEnabled());
        sub->setVisible(true);sub->setOpacity(.4);sub->streakStyle()->setWidthPx(3.5);
        sub->streakStyle()->setColor(QColor(20,40,80,170));sub->streakStyle()->setSpeedMultiplier(12);
        QVERIFY(a.moveSublayer(a.sublayers().indexOf(sub),0));
        const auto json=ISublayerHost::saveSublayersToJson(a);
        ISublayerHost::loadSublayersFromJson(b,json);
        QCOMPARE(b.sublayers().first()->id(),sub->id());QVERIFY(b.flowStreakSublayer()->isVisible());
        QCOMPARE(b.flowStreakSublayer()->opacity(),.4);
        QCOMPARE(b.flowStreakSublayer()->streakStyle()->toJson(),sub->streakStyle()->toJson());
        b.flowStreakSublayer()->streakStyle()->setWidthPx(7);QCOMPARE(sub->streakStyle()->widthPx(),3.5);
        QVERIFY(!sub->legendSymbolItems().empty());
        SWMM2DResultsLayer old;ISublayerHost::loadSublayersFromJson(old,{});QVERIFY(!old.flowStreakSublayer()->isVisible());
    }
    void stylePanelSynchronizes() {
        SWMM2DResultsLayer layer;openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        auto *width=panel.findChild<QDoubleSpinBox*>("widthPx");QVERIFY(width);
        auto *motion=panel.findChild<QCheckBox*>("animated");QVERIFY(motion);
        auto *style=layer.flowStreakSublayer()->streakStyle();
        width->setValue(4.5);QCOMPARE(style->widthPx(),4.5);
        style->setWidthPx(2.5);QCOMPARE(width->value(),2.5);
        motion->setChecked(false);QVERIFY(!style->animated());style->setAnimated(true);QVERIFY(motion->isChecked());
    }
    void velocitiesHaveCorrectDepthConversion() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        for(int t=0;t<3;++t) {
            layer.setCurrentTimeIndex(t);
            QVERIFY(!layer.m_sceneTris.empty());
            for(const auto &tri:layer.m_sceneTris) {QVERIFY(std::abs(tri.vx-2)<1e-5);QVERIFY(std::abs(tri.vmag-2)<1e-5);}
        }
    }
    void queuedStyleRefreshSurvivesLayerRemoval() {
        auto layer=std::make_unique<SWMM2DResultsLayer>();
        openswmmvis::ui::Swmm2DResultsStylePanel panel(layer.get());
        auto *style=layer->flowStreakSublayer()->streakStyle();
        auto scheme=style->scheme();scheme.setRangeMax(4);style->setScheme(scheme);
        layer.reset();QCoreApplication::processEvents(); // queued editor refresh must not dereference the deleted model
    }
    void quadDiagonalIsNotABarrier() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<QuadFlowSource>());
        layer.flowStreakSublayer()->setVisible(true);QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        int crossings=0;
        for(const auto &p:paths(layer)->paths) {
            const auto a=p.points.front().position,b=p.points.back().position;
            if(a.x()<-a.y() && b.x()>-b.y()) ++crossings;
            for(const auto &v:p.points)QVERIFY(std::abs(v.position.x()-a.x()-2*v.seconds)<1e-4);
        }
        QVERIFY(crossings>0);
    }
    void cachedLivePathsAndPhaseDoNotReadResults() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        auto first=paths(layer);QVERIFY(!first->paths.empty());
        const int reads=raw->reads,builds=layer.flowStreakBuildCount();const auto rev=layer.frameRevision();
        for(int i=0;i<100;++i){layer.advanceFlowStreaks(.033);QCOMPARE(paths(layer),first);}
        QCOMPARE(raw->reads,reads);QCOMPARE(layer.flowStreakBuildCount(),builds);QCOMPARE(layer.frameRevision(),rev);
        layer.flowStreakSublayer()->streakStyle()->setColor(Qt::yellow);QCOMPARE(paths(layer),first);
        layer.flowStreakSublayer()->streakStyle()->setAnimated(false);const auto phase=layer.flowStreakSeconds();
        layer.advanceFlowStreaks(.033);QCOMPARE(layer.flowStreakSeconds(),phase);
        layer.setLiveRenderEnabled(false);QVERIFY(!paths(layer));layer.setLiveRenderEnabled(true);
        layer.setCurrentTimeIndex(1);QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        QVERIFY(layer.flowStreakBuildCount()>builds);
    }
    void wetMaskAndSourceReplacement() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());layer.flowStreakSublayer()->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        auto source=std::make_unique<FlowSource>();source->depths={0};layer.setSource(std::move(source));
        QVERIFY(!paths(layer));
        source=std::make_unique<FlowSource>();source->fluxEnabled=false;layer.setSource(std::move(source));QVERIFY(!paths(layer));
        source=std::make_unique<FlowSource>();source->speed=-2;layer.setSource(std::move(source));
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        for(const auto &p:paths(layer)->paths)QVERIFY(p.points.back().position.x()<p.points.front().position.x());
    }
    void sameFrameLiveReplacementAndFeet() {
        SWMM2DResultsLayer layer;layer.setFallbackCoordinateScale(.3048);
        auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        const auto first=paths(layer);QVERIFY(!first->paths.empty());
        for(const auto &p:first->paths) for(const auto &v:p.points) {
            QVERIFY(std::abs(v.position.x()-p.points.front().position.x()-2*v.seconds/.3048)<1e-4);
        }
        const auto revision=layer.frameRevision();raw->speed=-2;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>revision,5000);
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);QVERIFY(paths(layer)!=first);
        for(const auto &p:paths(layer)->paths)QVERIFY(p.points.back().position.x()<p.points.front().position.x());
    }
    void laggingDisplayRetainsMatchingPaths() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));
        const auto first=paths(layer);const auto oldRevision=layer.frameRevision();
        raw->speed=-2;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision()!=oldRevision);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));
        QVERIFY(paths(layer)!=first);
        QCOMPARE(layer.flowStreaks(layer.m_sceneBBox,8,oldRevision),first);
        layer.flowStreakSublayer()->streakStyle()->setDryDepthCutoff(.4);
        QVERIFY(!layer.flowStreaks(layer.m_sceneBBox,8,oldRevision));
        layer.setSource(std::make_unique<FlowSource>());
        QVERIFY(!layer.flowStreaks(layer.m_sceneBBox,8,oldRevision));
    }
    void liveEngineSourceAndViewCache() {
        FlowSource fixture;std::vector<double>x,y,z;std::vector<std::array<int,3>>tri;
        fixture.readMeshGeometry(x,y,z,tri);
        auto source=std::make_unique<EngineMesh2DSource>(x,y,z,std::vector<std::array<int,4>>{{0,1,2,-1},{0,2,3,-1}});
        auto *raw=source.get();std::vector<float>len,nx,ny,flux;
        fixture.readEdgeGeometry(len,nx,ny);raw->setEdgeGeometry(len,nx,ny);
        raw->pushDepths({.25,.25},fixture.simTimeAt(0),0);fixture.readEdgeFluxAt(0,flux);
        raw->pushFlux(flux,fixture.simTimeAt(0),0);
        SWMM2DResultsLayer layer;layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        QVERIFY(layer.liveRenderEnabled());
        const QRectF a(0,-25,25,25),b(75,-100,25,25);
        auto get=[&](QRectF view){return layer.flowStreaks(view,8,layer.frameRevision());};
        QTRY_VERIFY_WITH_TIMEOUT(get(a)!=nullptr,5000);const auto first=get(a);QVERIFY(!first->paths.empty());
        QTRY_VERIFY_WITH_TIMEOUT(get(b)!=nullptr,5000);const int builds=layer.flowStreakBuildCount();
        QCOMPARE(get(a),first);QCOMPARE(layer.flowStreakBuildCount(),builds);
        raw->pushDepths({.5,.5},fixture.simTimeAt(1),60);fixture.readEdgeFluxAt(1,flux);
        raw->pushFlux(flux,fixture.simTimeAt(1),60);layer.refreshTimeRange();layer.setCurrentTimeIndex(1);
        layer.refreshCurrentFrame();QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(get(a)!=nullptr,5000);QVERIFY(get(a)!=first);
        for(const auto &p:get(a)->paths)for(const auto &v:p.points)QVERIFY(std::abs(v.speed-2)<1e-5);
        raw->markFinished();QVERIFY(!raw->isLive());QVERIFY(get(a));
    }
    void regionalLiveCaptureAfterOverviewLimit() {
        // Opt in to the same production-layer test with six million cells.
        // Normal CI uses 320k, enough to exercise the regional limit/recovery.
        const bool native=qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE_6M");
        const bool large=native || qEnvironmentVariableIsSet("FLOW_STREAK_GUI_6M");
        auto source=std::make_unique<GridFlowSource>(large?2000:400,large?1500:400);
        auto *raw=source.get();const int cells=source->triangleCount();
        SWMM2DResultsLayer layer;QElapsedTimer clock;clock.start();layer.setSource(std::move(source));
        const double loadMs=clock.nsecsElapsed()/1e6;
        layer.flowStreakSublayer()->setVisible(true);
        QVERIFY(layer.liveRenderEnabled());
        clock.restart();QVERIFY(!layer.flowStreaks(layer.m_sceneBBox,1,layer.frameRevision()));
        const double rejectMs=clock.nsecsElapsed()/1e6;
        QVERIFY(layer.flowStreakStatus().contains("This view"));QVERIFY(!layer.flowStreakBusy());
        const QRectF view(raw->cols*.75,-raw->rows*.5,20,20);
        clock.restart();QVERIFY(layer.prepareFlowStreaksForCapture(view,8));
        const double prepareMs=clock.nsecsElapsed()/1e6;
        auto get=[&]{return layer.flowStreaks(view,8,layer.frameRevision());};
        auto first=get();QVERIFY(first && !first->paths.empty());
        for(const auto &p:first->paths)for(const auto &pt:p.points)QVERIFY(std::abs(pt.speed-4)<1e-5);
        const int builds=layer.flowStreakBuildCount(),reads=raw->reads;
        for(int i=0;i<100;++i){layer.advanceFlowStreaks(.033);QCOMPARE(get(),first);}
        QCOMPARE(layer.flowStreakBuildCount(),builds);QCOMPARE(raw->reads,reads);
        const auto revision=layer.frameRevision();raw->speed=-2;++raw->revision;
        clock.restart();layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=revision,30000);
        const double updateMs=clock.nsecsElapsed()/1e6;
        QVERIFY(layer.prepareFlowStreaksForCapture(view,8));QVERIFY(get()!=first);
        for(const auto &p:get()->paths)QVERIFY(p.points.back().position.x()<p.points.front().position.x());
        auto clippedPaint=[&](QRectF exposed) {
            QImage image(160,160,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);painter.scale(8,8);painter.translate(-view.left(),-view.top());
            painter.setClipRect(view);layer.paintFlowStreaks(&painter,exposed);painter.end();return image;
        };
        const auto expected=clippedPaint(view);
        QImage empty(expected.size(),expected.format());empty.fill(Qt::transparent);QVERIFY(expected!=empty);
        QCOMPARE(clippedPaint(layer.m_sceneBBox),expected);
        QJsonObject presentation;
        if(native) {
            layer.meshEdgeSublayer()->setVisible(false);layer.meshNodeSublayer()->setVisible(false);
            MapCanvas canvas;canvas.resize(1920,1080);canvas.addLayer(&layer,false);
            canvas.setExtent({view.left(),-view.bottom(),view.right(),-view.top()},false);canvas.show();
            qInfo()<<"Regional native view"<<canvas.extent().toRectF()<<"mesh"<<layer.m_sceneBBox
                   <<"pixels"<<canvas.size()<<canvas.devicePixelRatioF();
            QString error;clock.restart();
            QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
            const double coldCaptureMs=clock.nsecsElapsed()/1e6;
            QTest::qWait(1500);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),30000);
            QVERIFY(layer.qsgOwnsRendering());
            const int before=layer.flowStreakBuildCount();const double phase=layer.flowStreakSeconds();
            QTest::qWait(150);QVERIFY(layer.flowStreakSeconds()>phase);
            QJsonArray runs;
            for(int run=0;run<3;++run) {
                std::vector<double> ms;
                for(int i=0;i<100;++i) {
                    clock.restart();layer.advanceFlowStreaks(.033);canvas.repaint();
                    ms.push_back(clock.nsecsElapsed()/1e6);QCoreApplication::processEvents();
                }
                std::sort(ms.begin(),ms.end());
                runs.append(QJsonObject{{"medianMs",ms[50]},{"p95Ms",ms[95]},{"maxMs",ms.back()}});
            }
            QCOMPARE(layer.flowStreakBuildCount(),before);
            QDir().mkpath("tests/verification/flow_streaks");
            QVERIFY(canvas.captureMap(&error).save("tests/verification/flow_streaks/regional-native-6m.png"));
            presentation={{"logicalWidth",canvas.width()},{"logicalHeight",canvas.height()},
                {"devicePixelRatio",canvas.devicePixelRatioF()},{"coldCaptureMs",coldCaptureMs},
                {"phasePathRebuilds",layer.flowStreakBuildCount()-before},{"runs",runs}};
        }
        if(large) {
            QDir().mkpath("workplans/artifacts/flow_streaks");
            QFile file("workplans/artifacts/flow_streaks/regional-layer-6m.json");QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{"nativeCells",cells},{"displayTriangles",layer.m_sceneTris.size()},
                {"loadAndSurfaceMs",loadMs},{"overviewRejectionMs",rejectMs},{"coldRegionalPreparationMs",prepareMs},
                {"liveFieldAndSurfaceUpdateMs",updateMs},{"phaseTicks",100},{"phasePathRebuilds",0},
                {"paths",int(first->paths.size())},{"points",first->pointCount()},{"presentation",presentation}}).toJson());
        }
    }
    void gpuPhaseKeepsGeometry() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());layer.flowStreakSublayer()->setVisible(true);
        TestRenderer renderer;renderer.setSize({800,800});renderer.setLayer(&layer);renderer.setMapExtent({0,0,100,100});
        QSGNode *root=renderer.updatePaintNode(nullptr,nullptr);QVERIFY(root);
        QTRY_VERIFY_WITH_TIMEOUT((root=renderer.updatePaintNode(root,nullptr),findFlow(root)->geometry()->vertexCount()>0),5000);
        auto *flow=findFlow(root);const int uploads=flow->uploads();auto *data=flow->geometry()->vertexData();
        const auto frame=renderer.displayedFrameRevision();
        for(int i=0;i<20;++i) {layer.advanceFlowStreaks(.033);root=renderer.updatePaintNode(root,nullptr);}
        QCOMPARE(flow->uploads(),uploads);QCOMPARE(flow->geometry()->vertexData(),data);
        QCOMPARE(renderer.displayedFrameRevision(),frame);
        renderer.setProperty("canvasDevicePixelRatio",2.0);renderer.setSize({1600,1600});
        root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(flow->uploads(),uploads); // same logical-pixel density/width at 2x resolution
        QVERIFY(layer.moveSublayer(layer.sublayers().indexOf(layer.flowStreakSublayer()),0));
        root=renderer.updatePaintNode(root,nullptr);QCOMPARE(root->firstChild(),flow);
        layer.flowStreakSublayer()->setVisible(false);root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(flow->geometry()->vertexCount(),0);delete root;
    }
    void painterFallbackAnimatesAndHides() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        layer.flowStreakSublayer()->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
        auto draw=[&] {
            QImage image(800,800,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);painter.scale(8,8);painter.translate(0,100);
            layer.paintFlowStreaks(&painter,layer.m_sceneBBox);painter.end();return image;
        };
        const auto first=draw();QImage empty(first.size(),first.format());empty.fill(Qt::transparent);
        QVERIFY(first!=empty);QCOMPARE(draw(),first);
        layer.advanceFlowStreaks(.1);QVERIFY(draw()!=first);
        layer.flowStreakSublayer()->setVisible(false);QCOMPARE(draw(),empty);
    }
    void capturePreparesColdPathsAtNewScale() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        layer.flowStreakSublayer()->setVisible(true);
        const double phase=layer.flowStreakSeconds();
        for(double scale:{8.,16.}) {
            QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,scale));
            const auto batch=layer.flowStreaks(layer.m_sceneBBox,scale,layer.frameRevision());
            QVERIFY(batch && !batch->paths.empty());
            const int builds=layer.flowStreakBuildCount();
            QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,scale));
            QCOMPARE(layer.flowStreakBuildCount(),builds);
            QCOMPARE(layer.flowStreakSeconds(),phase);
        }
        layer.setOpacity(0);QVERIFY(!layer.flowStreaksEnabled());
        layer.advanceFlowStreaks(.1);QCOMPARE(layer.flowStreakSeconds(),phase);
    }
    void realSavedResults() {
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(path.isEmpty()) QSKIP("Optional real engine HDF5 result fixture");
        auto source=std::make_unique<HDF5Mesh2DSource>();QVERIFY(source->open(path));
        SWMM2DResultsLayer layer;layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(layer.source()->timeCount()-1);layer.flowStreakSublayer()->setVisible(true);
        const QRectF view=layer.m_sceneBBox;
        auto get=[&]{return layer.flowStreaks(view,800/view.width(),layer.frameRevision());};
        QTRY_VERIFY_WITH_TIMEOUT(get()!=nullptr,10000);QVERIFY(!get()->paths.empty());
        const auto batch=get();const int builds=layer.flowStreakBuildCount();
        for(int i=0;i<20;++i){layer.advanceFlowStreaks(.033);QCOMPARE(get(),batch);}
        QCOMPARE(layer.flowStreakBuildCount(),builds);
    }
    void realLiveReplayMatchesSavedPaths() {
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(path.isEmpty()) QSKIP("Optional real engine HDF5 result fixture");
        auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
        auto *reader=file.get();
        std::vector<double>x,y,z;std::vector<std::array<int,4>>cells;
        std::vector<float>len,nx,ny;
        QVERIFY(reader->readCells(x,y,z,cells));QVERIFY(reader->readEdgeGeometry(len,nx,ny));
        auto stream=std::make_unique<EngineMesh2DSource>(x,y,z,cells);auto *live=stream.get();
        live->setEdgeGeometry(len,nx,ny);
        SWMM2DResultsLayer saved,layer;saved.setSource(std::move(file));layer.setSource(std::move(stream));
        saved.flowStreakSublayer()->setVisible(true);layer.flowStreakSublayer()->setVisible(true);
        const auto view=saved.m_sceneBBox;const double scale=1200/view.width();
        int nonempty=0;
        for(int t=0;t<reader->timeCount();++t) {
            std::vector<float>depth,flux;
            QVERIFY(reader->readDepthsAt(t,depth));QVERIFY(reader->readEdgeFluxAt(t,flux));
            live->pushDepths(depth,reader->simTimeAt(t),t*60.);
            live->pushFlux(flux,reader->simTimeAt(t),t*60.);
            layer.refreshTimeRange();layer.setCurrentTimeIndex(t);saved.setCurrentTimeIndex(t);
            QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
            QVERIFY(saved.prepareFlowStreaksForCapture(view,scale));
            const auto a=layer.flowStreaks(view,scale,layer.frameRevision());
            const auto b=saved.flowStreaks(view,scale,saved.frameRevision());
            QCOMPARE(bool(a),bool(b));if(!a)continue;
            QCOMPARE(a->paths.size(),b->paths.size());
            for(size_t p=0;p<a->paths.size();++p) {
                QCOMPARE(a->paths[p].points.size(),b->paths[p].points.size());
                for(size_t i=0;i<a->paths[p].points.size();++i) {
                    QCOMPARE(a->paths[p].points[i].position,b->paths[p].points[i].position);
                    QCOMPARE(a->paths[p].points[i].speed,b->paths[p].points[i].speed);
                }
            }
            nonempty+=!a->paths.empty();
        }
        QVERIFY(nonempty>0);live->markFinished();
    }
    void projectReopensFlowStyle() {
        const QString result=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(result.isEmpty()) QSKIP("Optional real engine HDF5 result fixture");
        const QString folder=QDir::current().absoluteFilePath("tests/verification/flow_streaks/project with spaces");
        QVERIFY(QDir().mkpath(folder));
        const QString deck=folder+"/flow.inp",project=folder+"/flow.oswp";
        if(!QFile::exists(deck))QVERIFY(QFile::copy("tests/gui/data/userflags_fixture.inp",deck));
        SWMMVisProjectWindow original(nullptr,deck);
        // This fixture tests persistence, using projected coordinates in the
        // map rather than interpreting the result's large easting as degrees.
        original.modelLayer()->setSRS(SpatialReferenceSystem::fromAuthCode("EPSG",3857),true);
        auto *layer=new SWMM2DResultsLayer;
        layer->setProperty("snoopy_h5_path",QFileInfo(result).absoluteFilePath());
        auto *sub=layer->flowStreakSublayer();sub->setVisible(true);sub->setOpacity(.65);
        sub->streakStyle()->setColor(Qt::cyan);sub->streakStyle()->setWidthPx(2.7);
        sub->streakStyle()->setSpacingPx(22);sub->streakStyle()->setAnimated(false);
        original.canvas()->addLayer(layer,false);
        QString error;QVERIFY2(ProjectSerializer::saveToFile(project,&original,&error),qPrintable(error));
        SWMMVis app;
        QVERIFY(QMetaObject::invokeMethod(&app,"onOpenProject",Qt::DirectConnection,Q_ARG(QString,project)));
        auto restored=[&]()->SWMM2DResultsLayer* {
            for(auto *window:app.findChildren<SWMMVisProjectWindow*>())
                if(window->active2DResultsLayer())return window->active2DResultsLayer();
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(restored(),30000);
        const auto *reopened=restored()->flowStreakSublayer();
        QVERIFY(reopened->isVisible());QCOMPARE(reopened->opacity(),sub->opacity());
        QCOMPARE(reopened->streakStyle()->toJson(),sub->streakStyle()->toJson());
        for(auto *window:app.findChildren<SWMMVisProjectWindow*>())window->setHasChanges(false);
    }
    void nativePresentationBenchmark() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE")) QSKIP("Run FLOW_STREAK_NATIVE=1 with a native window for GPU/readback measurements");
        QDir().mkpath("tests/verification/flow_streaks");
        SWMM2DResultsLayer layer;
        const QString result=qEnvironmentVariable("FLOW_STREAK_NATIVE_RESULT");
        if(result.isEmpty()) {
            auto source=std::make_unique<FlowSource>();source->live=true;layer.setSource(std::move(source));
        } else {
            auto source=std::make_unique<HDF5Mesh2DSource>();QVERIFY(source->open(result));
            layer.setSource(std::move(source));layer.setCurrentTimeIndex(layer.source()->timeCount()-1);
            layer.meshEdgeSublayer()->setVisible(false);
            layer.meshNodeSublayer()->setVisible(false);
        }
        const auto box=layer.m_sceneBBox;
        const MapExtent overview(box.left(),-box.bottom(),box.right(),-box.top());
        MapCanvas canvas;canvas.resize(1920,1080);canvas.addLayer(&layer,false);canvas.setExtent(overview,false);
        layer.flowStreakSublayer()->setVisible(true);canvas.show();
        QString error;
        const double capturePhase=layer.flowStreakSeconds();
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        QCOMPARE(layer.flowStreakSeconds(),capturePhase);
        QTest::qWait(1500);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),10000);
        const int builds=layer.flowStreakBuildCount();
        QVERIFY(builds>0);
        QVERIFY(layer.qsgOwnsRendering());
        QQuickWidget *overlay=nullptr;
        for(auto *widget:QApplication::topLevelWidgets())
            if(auto *quick=qobject_cast<QQuickWidget*>(widget))overlay=quick;
        QVERIFY(overlay);
        const double backingDpr=overlay->quickWindow()->effectiveDevicePixelRatio();
        qInfo()<<"Flow capture sizes"<<canvas.size()<<canvas.devicePixelRatioF()
               <<overlay->size()<<backingDpr<<"view"<<canvas.extent().toRectF()
               <<"mesh"<<layer.m_sceneBBox<<"enabled"<<layer.flowStreaksEnabled();
        QVERIFY(std::abs(overlay->width()*backingDpr-canvas.width()*canvas.devicePixelRatioF())<=backingDpr);
        QVERIFY(std::abs(overlay->height()*backingDpr-canvas.height()*canvas.devicePixelRatioF())<=backingDpr);
        const double phase=layer.flowStreakSeconds();QTest::qWait(150);
        QVERIFY(layer.flowStreakSeconds()>phase); // real canvas timer, not only manual advancement
        QJsonArray runs;
        for(int run=0;run<3;++run) {
            std::vector<double> ms;
            for(int i=0;i<100;++i) {
                QElapsedTimer timer;timer.start();layer.advanceFlowStreaks(.033);canvas.repaint();
                ms.push_back(timer.nsecsElapsed()/1e6);QCoreApplication::processEvents();
            }
            std::sort(ms.begin(),ms.end());
            runs.append(QJsonObject{{"medianMs",ms[50]},{"p95Ms",ms[95]},{"maxMs",ms.back()}});
        }
        const QString tag=result.isEmpty()?QStringLiteral("live-native"):QStringLiteral("real-native");
        QVERIFY(canvas.captureMap(&error).save("tests/verification/flow_streaks/"+tag+".png"));
        const int phaseBuilds=layer.flowStreakBuildCount();
        const double paused=layer.flowStreakSeconds();layer.flowStreakSublayer()->streakStyle()->setAnimated(false);
        QTest::qWait(100);QCOMPARE(layer.flowStreakSeconds(),paused);
        QCOMPARE(layer.flowStreakBuildCount(),builds);
        layer.setMaxDepth(layer.maxDepth()*1.2);
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        // Capture immediately after zoom/scrub, without letting the event loop
        // pre-warm either flow paths or forced asynchronous contours.
        QPointF center=box.center();
        const auto batch=layer.flowStreaks(box,canvas.width()/canvas.extent().width(),layer.frameRevision());
        QVERIFY(batch && !batch->paths.empty());
        const auto longest=std::max_element(batch->paths.begin(),batch->paths.end(),
            [](const auto &a,const auto &b){return a.length<b.length;});
        center=longest->points.front().position;
        const QRectF zoom(center-QPointF(box.width()/24,box.height()/24),box.size()/12);
        canvas.setExtent({zoom.left(),-zoom.bottom(),zoom.right(),-zoom.top()},false);
        const auto zoomImage=canvas.captureMap(&error);
        QVERIFY2(!zoomImage.isNull(),qPrintable(error));
        QVERIFY(zoomImage.save("tests/verification/flow_streaks/"+tag+"-zoom.png"));
        layer.flowStreakSublayer()->setVisible(false);
        QVERIFY(canvas.captureMap(&error).toImage()!=zoomImage.toImage());
        layer.flowStreakSublayer()->setVisible(true);
        canvas.setExtent(overview,false);
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        layer.setCurrentTimeIndex(std::max(0,layer.source()->timeCount()-2));
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        layer.setCurrentTimeIndex(layer.source()->timeCount()-1);
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        layer.flowStreakSublayer()->streakStyle()->setAnimated(true);
        canvas.setExtent({box.right()+1000,-box.top()+1000,box.right()+1100,-box.top()+1100},false);canvas.repaint();
        QTest::qWait(100);QCOMPARE(layer.flowStreakSeconds(),paused); // offscreen layer does no animation work
        QFile file("tests/verification/flow_streaks/"+tag+"-timing.json");QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"logicalWidth",canvas.width()},{"logicalHeight",canvas.height()},
            {"devicePixelRatio",canvas.devicePixelRatioF()},{"runs",runs},{"buildsBefore",builds},
            {"overlayWidth",overlay->width()},{"overlayHeight",overlay->height()},{"overlayDpr",backingDpr},
            {"buildsAfter",layer.flowStreakBuildCount()},{"triangles",layer.m_sceneTris.size()},
            {"phaseBuildsAfter",phaseBuilds},
            {"source",result.isEmpty()?QStringLiteral("synthetic"):result}}).toJson());
        canvas.takeLayer(0,false);
    }
};
QTEST_MAIN(FlowStreaksTest)
#include "test_flowstreaks.moc"
