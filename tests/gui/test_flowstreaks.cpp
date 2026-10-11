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
    int cols,rows,regionalReads=0;
    bool supportsHydraulicCells() const override {return true;}
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int t,const std::vector<int>& ids) override {
        if(!openswmmvis::io::validHydraulicCells(ids,triangleCount())) return {};
        ++regionalReads;
        auto out=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time=t;out->revision=revision;out->cells=ids;
        const float signs[8]={1,-1,0,0,0,-1,1,0};
        for(int c:ids) {
            const float d=depths.at(size_t(t));out->depths.push_back(d);
            for(int e=0;e<4;++e)out->flux.push_back(signs[(4*c+e)%8]*d*speed*((c/2)%cols>=cols/2?2:1));
        }
        return out;
    }
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
class QuadGridFlowSource : public GridFlowSource {
public:
    using GridFlowSource::GridFlowSource;
    int triangleCount() const override {return cols*rows;}
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int t,const std::vector<int>& ids) override {
        if(!openswmmvis::io::validHydraulicCells(ids,triangleCount())) return {};
        ++regionalReads;auto out=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time=t;out->revision=revision;out->cells=ids;const float signs[4]={1,0,-1,0};
        for(int c:ids) {
            Q_UNUSED(c);const float d=depths.at(size_t(t));out->depths.push_back(d);
            for(float sign:signs)out->flux.push_back(sign*d*speed);
        }return out;
    }
    bool readCells(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,
                   std::vector<std::array<int,4>>&cells) override {
        x.resize(vertexCount());y.resize(vertexCount());z.assign(vertexCount(),0);
        for(int r=0;r<=rows;++r)for(int c=0;c<=cols;++c){const int i=r*(cols+1)+c;x[i]=c;y[i]=r;}
        for(int r=0;r<rows;++r)for(int c=0;c<cols;++c) {
            const int a=r*(cols+1)+c;cells.push_back({a,a+1,a+cols+2,a+cols+1});
        }return true;
    }
    bool readEdgeGeometry(std::vector<float>&len,std::vector<float>&nx,std::vector<float>&ny) override {
        len.assign(size_t(triangleCount())*4,1);nx.resize(len.size());ny.resize(len.size());
        const float a[4]={1,0,-1,0},b[4]={0,1,0,-1};
        for(size_t i=0;i<len.size();++i){nx[i]=a[i%4];ny[i]=b[i%4];}return true;
    }
    bool readEdgeFluxAt(int t,std::vector<float>&flux) override {
        const float sign[4]={1,0,-1,0};flux.resize(size_t(triangleCount())*4);
        for(size_t i=0;i<flux.size();++i)flux[i]=sign[i%4]*depths.at(size_t(t))*speed;
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
    void compactLiveHydraulicSnapshots()
    {
        FlowSource fixture;std::vector<double>x,y,z;std::vector<std::array<int,3>>tri;
        fixture.readMeshGeometry(x,y,z,tri);EngineMesh2DSource live(x,y,z,tri);
        std::vector<float> flux;fixture.readEdgeFluxAt(0,flux);
        live.pushDepths({.25f,.5f},fixture.simTimeAt(0),0);live.pushFlux(flux,fixture.simTimeAt(0),0);
        const auto first=live.readHydraulicCellsAt(0,{1});QVERIFY(first);
        QCOMPARE(first->depths,std::vector<float>{.5f});QCOMPARE(first->flux,std::vector<float>(flux.begin()+4,flux.end()));
        fixture.speed=-2;fixture.readEdgeFluxAt(0,flux);live.pushFlux(flux,fixture.simTimeAt(0),0);
        const auto next=live.readHydraulicCellsAt(0,{1});QVERIFY(next);QVERIFY(first->revision!=next->revision);
        QCOMPARE(first->depths,std::vector<float>{.5f});QCOMPARE(next->depths,std::vector<float>{.5f});
        QCOMPARE(next->flux,std::vector<float>(flux.begin()+4,flux.end()));
        QVERIFY(!live.readHydraulicCellsAt(0,{1,0}));QVERIFY(!live.readHydraulicCellsAt(0,{0,0}));
        QVERIFY(!live.readHydraulicCellsAt(0,{2}));QVERIFY(!live.readHydraulicCellsAt(1,{0}));
        QCOMPARE(first->bytes(),size_t(24));
    }

    void regionalSurfacePreservesAnalysisAndPan()
    {
        auto source=std::make_unique<GridFlowSource>();auto* raw=source.get();
        SWMM2DResultsLayer layer;layer.setSource(std::move(source));layer.setAsyncSurface(true);
        layer.setLiveMinFitIntervalMs(0);
        const QRectF a(295,-205,10,10),b(95,-205,10,10);
        const auto reference=layer.m_sceneTris;
        layer.setHydraulicView(a,8);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        QVERIFY(layer.regionalHydraulicsPublished());QVERIFY(layer.hydraulicRegionCellCount()<50000);
        const int reads=raw->reads;
        for(int i:layer.m_triGrid.query(a)) {
            const auto& t=layer.m_sceneTris[i];const auto& expected=reference[i];
            QVERIFY(std::abs(t.dv0-expected.dv0)<1e-5);QVERIFY(std::abs(t.dv1-expected.dv1)<1e-5);
            QVERIFY(std::abs(t.dv2-expected.dv2)<1e-5);QCOMPARE(t.vmag,expected.vmag);
        }
        layer.setHydraulicView(b,8);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        QCOMPARE(raw->reads,reads);QVERIFY(layer.regionalHydraulicsPublished());
        const auto velocity=layer.captureVelocityField();QVERIFY(velocity.depths.size()<50000);
        float vx=0,vy=0;QVERIFY(layer.velocityAtScene(b.center(),vx,vy,velocity));QVERIFY(std::abs(vx-2)<1e-5);
        const auto oldRevision=layer.frameRevision();raw->depths[0]=.4f;raw->speed=-3;++raw->revision;
        layer.refreshCurrentFrame();QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>oldRevision,10000);
        QCOMPARE(raw->reads,reads);QVERIFY(raw->regionalReads>=3);
        // Replace pending work repeatedly while one fit is active. Only a
        // coherent latest revision may become the final displayed region.
        for(int tick=0;tick<8;++tick) {++raw->revision;raw->speed=-3-float(tick);layer.refreshCurrentFrame();}
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        QCOMPARE(raw->reads,reads);
        for(int i:layer.m_triGrid.query(b)) {QCOMPARE(layer.m_sceneTris[i].depth,.4f);QVERIFY(layer.m_sceneTris[i].vx<0);}
        // Analysis outside the display region still reconstructs the full field.
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center())-.4)<1e-5);
        QVERIFY(raw->reads>reads);const int analysisReads=raw->reads;
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center())-.4)<1e-5);QCOMPARE(raw->reads,analysisReads);
        layer.setHydraulicView(a,8);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        QCOMPARE(raw->reads,analysisReads);
        layer.setHydraulicView(layer.m_sceneBBox,1);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        QVERIFY(!layer.regionalHydraulicsPublished());QCOMPARE(layer.m_sceneTris[0].depth,.4f);
        layer.setHydraulicView(a,8);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        layer.onCanvasCRSChanged(nullptr);
        QVERIFY(!layer.regionalHydraulicsPublished());QCOMPARE(layer.m_sceneTris[0].depth,.4f);
        layer.closeSource();layer.setSource(std::make_unique<FlowSource>());
        QVERIFY(!layer.regionalHydraulicsPublished());
    }

    void regionalSavedSurfaceError()
    {
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(path.isEmpty()) QSKIP("Set FLOW_STREAK_RESULT for saved regional/full reconstruction comparison");
        qputenv("OPENSWMM_REGIONAL_SURFACE","1");
        auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
        SWMM2DResultsLayer layer;layer.setSource(std::move(file));
        const auto full=layer.m_sceneTris;
        int center=-1;for(int i=0;i<full.size();++i) if(full[i].depth>.1 && full[i].vmag>.1) {center=i;break;}
        QVERIFY(center>=0);
        const QRectF view(full[center].centroid-QPointF(15,15),QSizeF(30,30));
        layer.setHydraulicView(view,8);QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
        // This complex wet neighborhood fails the regional error gate. Keep
        // the full-domain surface rather than accepting a faster inaccurate fit.
        QVERIFY(!layer.regionalHydraulicsPublished());
        QCOMPARE(layer.regionalSurfaceFallbackCount(),1);
        QVERIFY(layer.regionalSurfaceError()>.002);
        double maxError=0,rms=0;int count=0,maskChanges=0;
        for(int i:layer.m_triGrid.query(view)) {
            const auto& a=full[i];const auto& b=layer.m_sceneTris[i];
            const float before[]={a.dv0,a.dv1,a.dv2},after[]={b.dv0,b.dv1,b.dv2};
            for(int k=0;k<3;++k) {
                if(std::isfinite(before[k])!=std::isfinite(after[k])) {++maskChanges;continue;}
                if(!std::isfinite(before[k])) continue;
                const double error=std::abs(before[k]-after[k]);maxError=std::max(maxError,error);rms+=error*error;++count;
            }
        }
        qInfo()<<"Regional saved surface fallback; rejected error"<<layer.regionalSurfaceError()<<"corners"<<count
               <<"max error m"<<maxError<<"RMS m"<<std::sqrt(rms/std::max(1,count))<<"mask changes"<<maskChanges;
        QVERIFY(count>0);QCOMPARE(maskChanges,0);QVERIFY(maxError<.002);
        // Scientific profile query must return the full fit, not the approximation.
        const auto& t=full[center];const double expected=(double(t.dv0)+t.dv1+t.dv2)/3;
        QVERIFY(std::abs(layer.signedDepthAtDisplayTriangle(center,t.centroid)-expected)<1e-6);
        qunsetenv("OPENSWMM_REGIONAL_SURFACE");
    }

    void overviewNativeParityAndNarrowChannel() {
        auto source=std::make_unique<GridFlowSource>(30,30);
        SWMM2DResultsLayer layer;layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        const auto view=layer.m_sceneBBox;
        QVERIFY(layer.prepareFlowStreaksForCapture(view,10));
        const auto expected=layer.flowStreaks(view,10,layer.frameRevision());QVERIFY(expected);
        // Force sampling on a separate identical layer; normal close views keep
        // the regional worker. Native edges/quad diagonals share the same tracer.
        qputenv("OPENSWMM_FLOW_OVERVIEW","1");
        SWMM2DResultsLayer sampled;sampled.setSource(std::make_unique<GridFlowSource>(30,30));
        sampled.flowStreakSublayer()->setVisible(true);
        QVERIFY(sampled.prepareFlowStreaksForCapture(view,10));
        const auto actual=sampled.flowStreaks(view,10,sampled.frameRevision());
        QVERIFY(actual);QCOMPARE(actual->paths.size(),expected->paths.size());
        for(size_t i=0;i<actual->paths.size();++i) {
            QCOMPARE(actual->paths[i].points.size(),expected->paths[i].points.size());
            for(size_t j=0;j<actual->paths[i].points.size();++j)
                QCOMPARE(actual->paths[i].points[j].position,expected->paths[i].points[j].position);
        }
        // A one-cell-wide wet channel inside a dry domain must remain visible
        // through native seed refinement and cannot leak into the dry banks.
        for(auto &t:sampled.m_sceneTris) {
            if((t.a.y()+t.b.y()+t.c.y())/3 < -15 || (t.a.y()+t.b.y()+t.c.y())/3 > -14)
                t.dv0=t.dv1=t.dv2=-1;
        }
        sampled.flowStreakSublayer()->streakStyle()->setSpacingPx(80);
        QVERIFY(sampled.prepareFlowStreaksForCapture(view,9));
        const auto channel=sampled.flowStreaks(view,9,sampled.frameRevision());QVERIFY(channel && !channel->paths.empty());
        for(const auto &path:channel->paths)for(const auto &pt:path.points)
            QVERIFY(pt.position.y()>=-15-1e-8 && pt.position.y()<=-14+1e-8);
        qunsetenv("OPENSWMM_FLOW_OVERVIEW");
    }
    void overviewQuadDiagonalParity() {
        SWMM2DResultsLayer regional;regional.setSource(std::make_unique<QuadGridFlowSource>(30,30));
        regional.flowStreakSublayer()->setVisible(true);const auto view=regional.m_sceneBBox;
        QVERIFY(regional.prepareFlowStreaksForCapture(view,10));
        const auto expected=regional.flowStreaks(view,10,regional.frameRevision());QVERIFY(expected);
        qputenv("OPENSWMM_FLOW_OVERVIEW","1");
        SWMM2DResultsLayer sampled;sampled.setSource(std::make_unique<QuadGridFlowSource>(30,30));
        sampled.flowStreakSublayer()->setVisible(true);QVERIFY(sampled.prepareFlowStreaksForCapture(view,10));
        const auto actual=sampled.flowStreaks(view,10,sampled.frameRevision());QVERIFY(actual);
        QCOMPARE(actual->paths.size(),expected->paths.size());
        for(size_t i=0;i<actual->paths.size();++i) {
            QCOMPARE(actual->paths[i].points.size(),expected->paths[i].points.size());
            for(size_t j=0;j<actual->paths[i].points.size();++j)
                QCOMPARE(actual->paths[i].points[j].position,expected->paths[i].points[j].position);
        }
        qunsetenv("OPENSWMM_FLOW_OVERVIEW");
    }
    void overviewDiscardOnReplacement() {
        qputenv("OPENSWMM_FLOW_OVERVIEW","1");
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<GridFlowSource>(80,80));
        layer.flowStreakSublayer()->setVisible(true);
        const auto view=layer.m_sceneBBox;const auto oldRevision=layer.frameRevision();
        QVERIFY(!layer.flowStreaks(view,4,oldRevision));QVERIFY(layer.flowStreakBusy());
        auto replacement=std::make_unique<GridFlowSource>(80,80);replacement->speed=-2;
        layer.setSource(std::move(replacement));
        QVERIFY(layer.prepareFlowStreaksForCapture(view,4));
        const auto result=layer.flowStreaks(view,4,layer.frameRevision());QVERIFY(result && !result->paths.empty());
        for(const auto &path:result->paths)QVERIFY(path.points.back().position.x()<path.points.front().position.x());
        qunsetenv("OPENSWMM_FLOW_OVERVIEW");
    }
    void regionalLiveCaptureAfterOverview() {
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
        const double requestMs=clock.nsecsElapsed()/1e6;
        QVERIFY(layer.flowStreakBusy());
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const double overviewMs=clock.nsecsElapsed()/1e6;
        const auto overview=layer.flowStreaks(layer.m_sceneBBox,1,layer.frameRevision());
        QVERIFY(overview && !overview->paths.empty());
        QVERIFY(overview->candidateTests<=2000000);QVERIFY(overview->cellVisits<=256000);
        QVERIFY(overview->pointCount()<=64000);QVERIFY(overview->paths.size()<=2000);
        const int overviewBuilds=layer.flowStreakBuildCount(),overviewReads=raw->reads;
        for(int i=0;i<100;++i) {layer.advanceFlowStreaks(.033);QCOMPARE(layer.flowStreaks(layer.m_sceneBBox,1,layer.frameRevision()),overview);}
        QCOMPARE(layer.flowStreakBuildCount(),overviewBuilds);QCOMPARE(raw->reads,overviewReads);
        const QRectF view(raw->cols*.75,-raw->rows*.5,20,20);
        clock.restart();QVERIFY(layer.prepareFlowStreaksForCapture(view,8));
        const double prepareMs=clock.nsecsElapsed()/1e6;
        QVERIFY(layer.regionalHydraulicsPublished());
        const int fullReads=raw->reads;
        auto get=[&]{return layer.flowStreaks(view,8,layer.frameRevision());};
        auto first=get();QVERIFY(first && !first->paths.empty());
        for(const auto &p:first->paths)for(const auto &pt:p.points)QVERIFY(std::abs(pt.speed-4)<1e-5);
        const int builds=layer.flowStreakBuildCount(),reads=raw->reads,subsetReads=raw->regionalReads;
        for(int i=0;i<100;++i){layer.advanceFlowStreaks(.033);QCOMPARE(get(),first);}
        QCOMPARE(layer.flowStreakBuildCount(),builds);QCOMPARE(raw->reads,reads);QCOMPARE(raw->regionalReads,subsetReads);
        const auto revision=layer.frameRevision();raw->speed=-2;++raw->revision;
        clock.restart();layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=revision,30000);
        const double updateMs=clock.nsecsElapsed()/1e6;
        QCOMPARE(raw->reads,fullReads);
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
                {"loadAndSurfaceMs",loadMs},{"overviewRequestMs",requestMs},{"overviewPreparationMs",overviewMs},
                {"overviewPaths",int(overview->paths.size())},{"overviewPoints",overview->pointCount()},
                {"overviewCandidateTests",overview->candidateTests},{"overviewCellVisits",overview->cellVisits},{"coldRegionalPreparationMs",prepareMs},
                {"liveFieldAndSurfaceUpdateMs",updateMs},{"regionalHydraulicCells",double(layer.hydraulicRegionCellCount())},{"phaseTicks",100},{"phasePathRebuilds",0},
                {"paths",int(first->paths.size())},{"points",first->pointCount()},{"presentation",presentation}}).toJson());
        }
    }
    void overviewScaleBenchmark() {
        const bool large=qEnvironmentVariableIsSet("FLOW_STREAK_OVERVIEW_6M");
        const bool native=qEnvironmentVariableIsSet("FLOW_STREAK_OVERVIEW_NATIVE");
        QJsonArray cases;
        for(bool quads:{false,true}) {
            std::unique_ptr<GridFlowSource> source;
            if(quads)source=std::make_unique<QuadGridFlowSource>(large?3000:600,large?2000:500);
            else source=std::make_unique<GridFlowSource>(large?2000:400,large?1500:400);
            auto *raw=source.get();const int cells=source->triangleCount();
            SWMM2DResultsLayer layer;QElapsedTimer clock;clock.start();layer.setSource(std::move(source));
            const double loadMs=clock.nsecsElapsed()/1e6;
            layer.flowStreakSublayer()->setVisible(true);QJsonArray runs;
            const int sourceReads=raw->reads;
            for(double fraction:{.01,.1,1.})for(int run=0;run<3;++run) {
                const double side=std::sqrt(fraction);const auto box=layer.m_sceneBBox;
                const QRectF view(box.center()-QPointF(box.width()*side/2,box.height()*side/2),
                                  QSizeF(box.width()*side,box.height()*side));
                const double scale=1920/view.width();
                layer.flowStreakSublayer()->streakStyle()->setPathLengthPx(96+run);
                clock.restart();layer.flowStreaks(view,scale,layer.frameRevision());
                const double requestMs=clock.nsecsElapsed()/1e6;
                QEventLoop loop;QTimer heartbeat;heartbeat.setInterval(1);double previous=requestMs,maxGap=0;
                connect(&heartbeat,&QTimer::timeout,&loop,[&]{
                    const double now=clock.nsecsElapsed()/1e6;maxGap=std::max(maxGap,now-previous);previous=now;
                    if(!layer.flowStreakBusy() || now>30000)loop.quit();
                });
                if(layer.flowStreakBusy()){heartbeat.start();loop.exec();heartbeat.stop();}
                const double elapsed=clock.nsecsElapsed()/1e6;
                QVERIFY(!layer.flowStreakBusy());
                const auto batch=layer.flowStreaks(view,scale,layer.frameRevision());
                QVERIFY(batch && !batch->paths.empty());QVERIFY(batch->paths.size()<=2000);QVERIFY(batch->pointCount()<=64000);
                QVERIFY(batch->candidateTests<=2000000);QVERIFY(batch->cellVisits<=256000);
                QCOMPARE(raw->reads,sourceReads);QCOMPARE(raw->regionalReads,0);
                for(const auto &path:batch->paths)for(const auto &pt:path.points) {
                    QVERIFY2(std::abs(pt.position.y()-path.points.front().position.y())<1e-5,
                        qPrintable(QString("quads=%1 fraction=%2 dy=%3").arg(quads).arg(fraction).arg(pt.position.y()-path.points.front().position.y(),0,'g',16)));
                    QVERIFY(pt.position.x()>=path.points.front().position.x());
                }
                const int builds=layer.flowStreakBuildCount();
                for(int i=0;i<100;++i){layer.advanceFlowStreaks(.033);QCOMPARE(layer.flowStreaks(view,scale,layer.frameRevision()),batch);}
                QCOMPARE(layer.flowStreakBuildCount(),builds);
                runs.append(QJsonObject{{"fraction",fraction},{"run",run},{"requestMs",requestMs},
                    {"preparationMs",elapsed},{"maxEventGapMs",maxGap},{"paths",int(batch->paths.size())},
                    {"points",batch->pointCount()},{"candidateTests",batch->candidateTests},{"cellVisits",batch->cellVisits},
                    {"pathBytes",double(batch->bytes())},{"limited",batch->limited},{"phaseRebuilds",0}});
            }
            QJsonObject presentation;
            if(native && !quads) {
                layer.meshEdgeSublayer()->setVisible(false);layer.meshNodeSublayer()->setVisible(false);
                const auto box=layer.m_sceneBBox;MapCanvas canvas;canvas.resize(1920,1080);canvas.addLayer(&layer,false);
                canvas.setExtent({box.left(),-box.bottom(),box.right(),-box.top()},false);canvas.show();
                QString error;clock.restart();QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
                const double captureMs=clock.nsecsElapsed()/1e6;QTest::qWait(1500);
                QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),30000);
                QVERIFY(layer.qsgOwnsRendering());
                const int builds=layer.flowStreakBuildCount();const double phase=layer.flowStreakSeconds();
                QTest::qWait(150);QVERIFY(layer.flowStreakSeconds()>phase);QJsonArray times;
                for(int run=0;run<3;++run) {
                    std::vector<double> ms;
                    for(int i=0;i<100;++i){clock.restart();layer.advanceFlowStreaks(.033);canvas.repaint();ms.push_back(clock.nsecsElapsed()/1e6);QCoreApplication::processEvents();}
                    std::sort(ms.begin(),ms.end());times.append(QJsonObject{{"medianMs",ms[50]},{"p95Ms",ms[95]},{"maxMs",ms.back()}});
                }
                QCOMPARE(layer.flowStreakBuildCount(),builds);
                QDir().mkpath("tests/verification/flow_streaks");
                QVERIFY(canvas.captureMap(&error).save("tests/verification/flow_streaks/overview-native-6m.png"));
                presentation={{"width",canvas.width()},{"height",canvas.height()},{"dpr",canvas.devicePixelRatioF()},
                    {"coldCaptureMs",captureMs},{"qsgOwnsRendering",layer.qsgOwnsRendering()},{"phaseRebuilds",layer.flowStreakBuildCount()-builds},{"runs",times}};
            }
            cases.append(QJsonObject{{"nativeCells",cells},{"displayTriangles",layer.m_sceneTris.size()},{"quads",quads},{"loadMs",loadMs},{"runs",runs},{"presentation",presentation}});
        }
        QDir().mkpath("workplans/artifacts/flow_streaks");
        QFile file(large?"workplans/artifacts/flow_streaks/overview-6m.json":"workplans/artifacts/flow_streaks/overview-ci.json");
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(cases).toJson());
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
    void captureDoesNotDrainAnalysisEnvelope() {
        auto source=std::make_unique<FlowSource>();source->depths.assign(20,.25f);
        SWMM2DResultsLayer layer;layer.setAsyncSurface(true);layer.setSource(std::move(source));
        layer.flowStreakSublayer()->setVisible(true);layer.requestEnvelope();
        QVERIFY(layer.surfaceBusy());const int fits=layer.surfaceFitCount();
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));
        QCOMPARE(layer.surfaceFitCount(),fits);
        QVERIFY(layer.flowStreaks(layer.m_sceneBBox,8,layer.frameRevision()));
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
