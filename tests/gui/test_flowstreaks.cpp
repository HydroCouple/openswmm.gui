// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/flowstreaknode.h"
#include "map/flowparticlenode.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/openswmmvisgraphicsview.h"
#include "map/tools/maptool.h"
#include "render/smoothfillraster.h"
#include <QGraphicsRectItem>
#include "project/projectserializer.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include <QTest>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
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
#include <limits>
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
class SlopingFlowSource : public FlowSource {
public:
    SlopingFlowSource() {fluxEnabled=false;}
    bool readMeshGeometry(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,
                          std::vector<std::array<int,3>>&t) override {
        FlowSource::readMeshGeometry(x,y,z,t);z={0,1,1,0};return true;
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
class ParticleReviewCanvas:public MapCanvas {
public:
    std::vector<double> paintMs;
protected:
    void paintEvent(QPaintEvent *event) override {
        QElapsedTimer clock;clock.start();MapCanvas::paintEvent(event);paintMs.push_back(clock.nsecsElapsed()/1e6);
    }
};
class CompositionBackdrop : public OpenSWMMVisLayer {
public:
    explicit CompositionBackdrop(bool raster=false):OpenSWMMVisLayer("composition fixture"),m_raster(raster) {}
    bool isRasterLayer() const override { return m_raster; }
    void render(QPainter *p,const MapExtent &,const QSize &size,const SpatialReferenceSystem *) override {
        if(m_raster)p->fillRect(QRect(QPoint(),size),QColor(20,60,100));
    }
    void populateScene(QGraphicsScene *scene,const MapExtent &,const SpatialReferenceSystem *) override {
        if(m_raster)return;
        auto *item=scene->addRect(QRectF(20,-80,60,60),Qt::NoPen,QColor(200,100,0,128));
        item->setData(0,QVariant::fromValue(reinterpret_cast<quintptr>(this)));item->setZValue(layerZValue());
    }
private:bool m_raster;
};
class CompositionTool : public OpenSWMMVisMapTool {
public:
    explicit CompositionTool(MapCanvas *canvas):OpenSWMMVisMapTool("composition input",canvas) {}
    int presses=0;
    void mousePressEvent(QMouseEvent *) override { ++presses; }
    void paint(QPainter *p,const MapExtent &,const SpatialReferenceSystem *) override {
        p->fillRect(QRect(10,10,20,20),QColor(10,220,30));
    }
};
class HeadWidthPreview:public QQuickItem {
public:
    FlowStreakStyle style;
    std::shared_ptr<FlowStreakBatch> batch=std::make_shared<FlowStreakBatch>();
    double phase=120,clock=0;
    explicit HeadWidthPreview(QQuickItem *parent):QQuickItem(parent) {
        setFlag(ItemHasContents);setSize({256,80});
        style.setWidthPx(2);style.setHeadWidthPx(2);style.setFade(false);style.setTaper(false);
        style.setVelocityScaledTrail(false); // existing head/fade fixtures use an 80 px tail
        FlowStreakPath path;path.length=200;
        path.lifeRemaining=200;path.lifeAge=2;
        for(int i=0;i<=200;++i)path.points.push_back({QPointF(20+i,40),float(i),1});
        batch->paths.push_back(std::move(path));
    }
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *node=old?static_cast<FlowStreakNode*>(old):new FlowStreakNode;
        node->sync(batch,style,phase,1,{},1,clock);return node;
    }
};
static FlowStreakNode *findFlow(QSGNode *n) {
    if(auto *f=dynamic_cast<FlowStreakNode*>(n))return f;
    for(auto *c=n?n->firstChild():nullptr;c;c=c->nextSibling())if(auto *f=findFlow(c))return f;
    return nullptr;
}
static FlowParticleNode *findParticles(QSGNode *n) {
    if(auto *f=dynamic_cast<FlowParticleNode*>(n))return f;
    for(auto *c=n?n->firstChild():nullptr;c;c=c->nextSibling())if(auto *f=findParticles(c))return f;
    return nullptr;
}
class FlowStreaksTest:public QObject {
    Q_OBJECT
private:
    static std::shared_ptr<const FlowStreakBatch> paths(SWMM2DResultsLayer &layer) {
        return layer.flowStreaks(layer.m_sceneBBox,8.,layer.frameRevision());
    }
private slots:
    void continuousModePersistsAndSharesStyleControls() {
        SWMM2DResultsLayer a,b;auto *style=a.flowStreakSublayer()->streakStyle();
        QVERIFY(!style->continuousParticles());style->setContinuousParticles(true);
        style->setColor(QColor(20,40,80,170));style->setHeadWidthPx(5);
        ISublayerHost::loadSublayersFromJson(b,ISublayerHost::saveSublayersToJson(a));
        QCOMPARE(b.flowStreakSublayer()->streakStyle()->toJson(),style->toJson());
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&a);
        auto *mode=panel.findChild<QCheckBox*>("continuousParticles");QVERIFY(mode);QVERIFY(mode->isChecked());
        QVERIFY(!panel.findChild<QDoubleSpinBox*>("pathLengthPx")->isEnabled());
        QVERIFY(!panel.findChild<QDoubleSpinBox*>("trailLengthPx")->isEnabled());
        QVERIFY(panel.findChild<QDoubleSpinBox*>("trailDurationSeconds")->isEnabled());
        mode->setChecked(false);QVERIFY(!style->continuousParticles());
        style->setContinuousParticles(true);QVERIFY(mode->isChecked());
        auto legacy=style->toJson();legacy.remove("continuousParticles");legacy["schemaVersion"]=2;
        style->fromJson(legacy);QVERIFY(!style->continuousParticles());QVERIFY(!mode->isChecked());
    }
    void nativeParticlesHonorSIAndQuadFluxDiagonals() {
        for(bool quad:{false,true})for(double unit:{1.,.3048}) {
            SWMM2DResultsLayer layer;layer.setFallbackCoordinateScale(unit);
            if(quad)layer.setSource(std::make_unique<QuadFlowSource>());
            else layer.setSource(std::make_unique<FlowSource>());
            auto input=layer.captureFlowParticleInput(layer.m_sceneBBox,8);QVERIFY(input);
            std::atomic_bool cancel=false;const auto field=buildFlowParticleField(*input,cancel);QVERIFY(field);
            for(const auto &f:field->fields)for(int k=0;k<3;++k) {
                QVERIFY(std::abs(f.velocity[k].x()-2/unit)<1e-5);
                QVERIFY(std::abs(f.velocity[k].y())<1e-5);QVERIFY(std::abs(f.speed[k]-2)<1e-5);
            }
            FlowParticleSystem system;FlowParticleSettings settings;
            settings.minimumLifetime=settings.maximumLifetime=100;settings.trailSeconds=30;settings.maxHistory=5000;
            system.setSettings(settings);system.setField(field);const auto id=system.spawn({10/unit,-50/unit});QVERIFY(id);
            system.advance(30);const auto &p=system.particles().front();QCOMPARE(p.id,id);
            QVERIFY(p.crossings>0);QCOMPARE(p.stop,FlowParticleStop::None);
            QVERIFY(std::abs(p.position.x()-70/unit)<1e-4);
        }
    }
    void nativeCompatibleUpdatesRetainHistoryAcrossOneThousandPackets() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));
        const auto initial=layer.captureFlowParticleInput(layer.m_sceneBBox,8);QVERIFY(initial);
        std::atomic_bool cancel=false;FlowParticleSystem system;FlowParticleSettings settings;
        settings.minimumLifetime=settings.maximumLifetime=100;settings.trailSeconds=30;settings.maxHistory=5000;
        system.setSettings(settings);system.setField(buildFlowParticleField(*initial,cancel));
        const auto id=system.spawn({10,-50});QVERIFY(id);system.advance(.2);
        const auto history=system.particles().front().history;
        for(int i=0;i<1000;++i) {
            auto packet=std::make_shared<FlowParticleNativeInput>(*initial);packet->revision=i+10;
            const double factor=i%2?1.:1.25;
            for(auto &t:packet->triangles)for(auto &q:t.flux)q*=factor;
            QVERIFY(system.setField(buildFlowParticleField(*packet,cancel),.25));system.advance(1./60);
            QCOMPARE(system.particles().front().id,id);
        }
        QCOMPARE(system.frame()->births,quint64(1));QCOMPARE(system.frame()->deaths,quint64(0));
        for(const auto &sample:system.particles().front().history)if(sample.age<=history.back().age) {
            const auto original=std::find_if(history.begin(),history.end(),[&](const auto &s){return s.age==sample.age;});
            QVERIFY(original!=history.end());QCOMPARE(sample.position,original->position);
        }
        const int reads=raw->reads;system.advance(.1);QCOMPARE(raw->reads,reads);
    }
    void continuousControllerCoalescesLiveUpdatesWithoutParticleReplacement() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setContinuousParticles(true);style->setMinimumLifetimeSeconds(20);
        style->setMaximumLifetimeSeconds(40);style->setTrailDurationSeconds(1);
        const auto get=[&]{return layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());};
        QTRY_VERIFY_WITH_TIMEOUT(get()!=nullptr,5000);QVERIFY(!get()->particles.empty());
        const auto id=get()->particles.front().id;const double lifetime=get()->particles.front().lifetime;
        const int reads=raw->reads;
        layer.advanceFlowStreaks(.2);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        QCOMPARE(raw->reads,reads);const auto old=get();
        for(int i=0;i<10;++i) {raw->speed=2+.01f*i;++raw->revision;layer.refreshCurrentFrame();get();}
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=old->revision,5000);
        QTRY_VERIFY_WITH_TIMEOUT(get()!=nullptr && !layer.flowStreakBusy(),5000);
        layer.advanceFlowStreaks(.2);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        bool found=false;
        for(const auto &p:get()->particles)if(p.id==id) {found=true;QCOMPARE(p.lifetime,lifetime);QVERIFY(p.age>=old->particles.front().age);}
        QVERIFY(found);QCOMPARE(get()->deaths,quint64(0));
        // Stagnation and an incomplete live flux packet must not disable the
        // pool or replace surviving IDs with a newly seeded population.
        auto revision=layer.frameRevision();
        raw->speed=0;++raw->revision;layer.refreshCurrentFrame();QTRY_VERIFY(layer.frameRevision()!=revision);QTRY_VERIFY(get()!=nullptr);
        QVERIFY(layer.flowStreaksEnabled());layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        revision=layer.frameRevision();
        raw->fluxEnabled=false;++raw->revision;layer.refreshCurrentFrame();QTRY_VERIFY(layer.frameRevision()!=revision);QTRY_VERIFY(get()!=nullptr);
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        found=false;
        for(const auto &p:get()->particles)if(p.id==id) {found=true;QVERIFY(p.waitingForData);QCOMPARE(p.stop,FlowParticleStop::None);}
        QVERIFY(found);
        revision=layer.frameRevision();
        raw->fluxEnabled=true;raw->speed=2;++raw->revision;layer.refreshCurrentFrame();QTRY_VERIFY(layer.frameRevision()!=revision);QTRY_VERIFY(get()!=nullptr);
        layer.setLiveRenderEnabled(false);QVERIFY(!get());
        const auto clock=old->clock;layer.advanceFlowStreaks(1);QCOMPARE(old->clock,clock);
        layer.setLiveRenderEnabled(true);QTRY_VERIFY(get()!=nullptr);
    }
    void continuousLayerRendererPreservesSublayerOrderAndAppearance() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);auto *style=sub->streakStyle();
        style->setContinuousParticles(true);style->setColor(QColor(20,40,80,128));style->setSpeedMultiplier(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));layer.advanceFlowStreaks(.5);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        TestRenderer renderer;renderer.setSize({800,800});
        renderer.setProperty("canvasDevicePixelRatio",1);renderer.setLayer(&layer);
        renderer.setMapExtent({0,0,100,100});QSGNode *root=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((root=renderer.updatePaintNode(root,nullptr),
            findParticles(root)->geometry()->vertexCount()>0),5000);
        auto *node=findParticles(root);QCOMPARE(findFlow(root)->geometry()->vertexCount(),0);
        const auto *v=static_cast<const float*>(node->geometry()->vertexData());
        QVERIFY(std::abs(v[11]-20/255.f)<1e-6);QVERIFY(std::abs(v[14]-128/255.f)<1e-6);
        const int uploads=node->uploads();const auto frame=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());
        style->setHeadWidthPx(6);root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(node->uploads(),uploads+1);QCOMPARE(layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision())->births,frame->births);
        QVERIFY(layer.moveSublayer(layer.sublayers().indexOf(sub),0));root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(root->firstChild(),node->parent());
        sub->setVisible(false);root=renderer.updatePaintNode(root,nullptr);QCOMPARE(node->geometry()->vertexCount(),0);
        delete root;
    }
    void preparedFieldWaitsForMatchingVisibleWaterWithoutCatchUpJump() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setContinuousParticles(true);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));layer.advanceFlowStreaks(.2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto old=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(!old->particles.empty());
        const auto id=old->particles.front().id;const auto position=old->particles.front().position;
        raw->speed=10;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=old->revision,5000); // live refresh is queued/coalesced
        const auto prepared=[&]{return layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision(),false);};
        QTRY_VERIFY_WITH_TIMEOUT(prepared()!=nullptr && !layer.flowStreakBusy(),5000);
        layer.advanceFlowStreaks(.5);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),5000);
        QVERIFY(prepared());
        QCOMPARE(prepared()->clock,old->clock);
        for(const auto &p:prepared()->particles)if(p.id==id)QCOMPARE(p.position,position);
        layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());layer.advanceFlowStreaks(.1);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),5000);
        QVERIFY(prepared());
        QVERIFY(std::abs(prepared()->clock-old->clock-.1)<1e-8);
    }
    void savedAndLiveNativeParticleFieldsAgree() {
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(path.isEmpty())QSKIP("Provide a saved native hydraulic fixture");
        auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
        std::vector<double>x,y,z;std::vector<std::array<int,4>>cells;QVERIFY(file->readCells(x,y,z,cells));
        std::vector<float>depth,flux,length,nx,ny;
        const int time=file->timeCount()-1;QVERIFY(file->readDepthsAt(time,depth));QVERIFY(file->readEdgeFluxAt(time,flux));
        QVERIFY(file->readEdgeGeometry(length,nx,ny));const auto simTime=file->simTimeAt(time);
        auto stream=std::make_unique<EngineMesh2DSource>(x,y,z,cells);
        stream->setEdgeGeometry(length,nx,ny);stream->pushDepths(depth,simTime,0);stream->pushFlux(flux,simTime,0);
        SWMM2DResultsLayer saved,live;saved.setSource(std::move(file));saved.setCurrentTimeIndex(time);live.setSource(std::move(stream));
        const auto a=saved.captureFlowParticleInput(saved.m_sceneBBox,1),b=live.captureFlowParticleInput(live.m_sceneBBox,1);
        QVERIFY(a);QVERIFY(b);QCOMPARE(a->triangles.size(),b->triangles.size());
        std::atomic_bool cancel=false;const auto fa=buildFlowParticleField(*a,cancel),fb=buildFlowParticleField(*b,cancel);
        QVERIFY(fa);QVERIFY(fb);
        for(size_t i=0;i<fa->fields.size();++i) {
            QCOMPARE(fa->fields[i].available,fb->fields[i].available);QCOMPARE(fa->fields[i].exits,fb->fields[i].exits);
            for(int k=0;k<3;++k) {
                const auto delta=fa->fields[i].velocity[k]-fb->fields[i].velocity[k];
                QVERIFY(std::hypot(delta.x(),delta.y())<1e-4);
            }
        }
    }
    void cpuCellFillPreservesOpacityAndCapture() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        auto *fill=layer.cellDepthFillSublayer();fill->setVisible(true);fill->setOpacity(.5);
        auto *style=fill->fillStyle();style->setColorRampName("");style->setLowColor(QColor(0,0,200));
        style->setHighColor(QColor(0,0,200));style->setClassified(false);
        layer.setOpacity(.5);auto mask=layer.maskSpec();mask.enabled=true;layer.setMaskSpec(mask);
        MapCanvas canvas;canvas.resize(480,480);canvas.setBackgroundColor(QColor(20,60,100));canvas.setShowScaleBar(false);
        canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,110,110},false);canvas.show();QTest::qWait(100);
        QString error;const auto image=canvas.captureMap(&error).toImage();QVERIFY2(!image.isNull(),qPrintable(error));
        QVERIFY(!layer.qsgOwnsRendering());QVERIFY(!canvas.compositionStats().active);
        const auto color=image.pixelColor(qRound(240*image.devicePixelRatio()),qRound(200*image.devicePixelRatio()));
        QVERIFY2(std::abs(color.red()-15)<=2,qPrintable(color.name()));
        QVERIFY2(std::abs(color.green()-45)<=2,qPrintable(color.name()));
        QVERIFY2(std::abs(color.blue()-125)<=2,qPrintable(color.name()));
        fill->setVisible(false);
        const auto empty=canvas.captureMap(&error).toImage();QVERIFY2(!empty.isNull(),qPrintable(error));
        QCOMPARE(empty.pixelColor(qRound(240*empty.devicePixelRatio()),qRound(200*empty.devicePixelRatio())),QColor(20,60,100));
        canvas.takeLayer(0,false);
    }
    void smoothRasterSharesEdgesAndInterpolatesPremultipliedColors() {
        using namespace SmoothFillRaster;
        QImage image(16,16,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        const QRgb color=qPremultiply(qRgba(40,100,200,128));
        // The diagonal passes through pixel centres. Reversed winding must
        // neither double the opacity nor leave a one-pixel crack.
        triangle(image,{{{{0,0},color},{{16,0},color},{{16,16},color}}});
        triangle(image,{{{{0,0},color},{{0,16},color},{{16,16},color}}});
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)QCOMPARE(image.pixel(x,y),color);
        image.fill(Qt::transparent);
        triangle(image,{{{{0,0},qPremultiply(qRgba(255,0,0,255))},
                         {{16,0},qPremultiply(qRgba(0,255,0,128))},
                         {{0,16},qPremultiply(qRgba(0,0,255,0))}}});
        const QRgb mid=image.pixel(3,3); // weights 9/16, 3.5/16, 3.5/16
        QCOMPARE(qRed(mid),143);QCOMPARE(qGreen(mid),28);QCOMPARE(qBlue(mid),0);QCOMPARE(qAlpha(mid),171);
        const QImage before=image;
        triangle(image,{{{{NAN,0},color},{{1,0},color},{{0,1},color}}});QCOMPARE(image,before);
        triangle(image,{{{{-1e30,-1e30},color},{{-1e30,0},color},{{0,-1e30},color}}});QCOMPARE(image,before);
    }
    void cpuSmoothFillMatchesNativeAndRetainsOrder() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<SlopingFlowSource>());
        double shallow=1e9,deep=-1e9;
        for(const auto &t:layer.m_sceneTris)for(float d:{t.dv0,t.dv1,t.dv2}) {shallow=std::min(shallow,double(d));deep=std::max(deep,double(d));}
        QVERIFY(shallow<.1);QVERIFY(deep>.4);
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        auto *fill=layer.smoothDepthFillSublayer();fill->setVisible(true);fill->setOpacity(.65);layer.setOpacity(.7);
        auto *style=fill->fillStyle();style->setUseCustomRange(true);style->setRangeMin(.1);style->setRangeMax(.4);
        auto mask=layer.maskSpec();mask.enabled=true;layer.setMaskSpec(mask);
        MapCanvas canvas;canvas.resize(480,360);canvas.setBackgroundColor(QColor(20,60,100));canvas.setShowScaleBar(false);
        canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,110,110},false);canvas.show();QTest::qWait(100);
        QString error;
        for(bool classified:{false,true})for(bool invert:{false,true}) {
            style->setClassified(classified);style->setInvertRamp(invert);
            const auto cpu=canvas.captureMap(&error).toImage();QVERIFY2(!cpu.isNull(),qPrintable(error));
            QVERIFY(!layer.qsgOwnsRendering());
            int filled=0,dry=0;
            const auto pixel=[&](const QImage &i,int x,int y) {
                return i.pixelColor(qRound(x*i.devicePixelRatio()),qRound(y*i.devicePixelRatio()));
            };
            for(int y=60;y<300;y+=10)for(int x=60;x<420;x+=10) {
                if(pixel(cpu,x,y)==canvas.backgroundColor())++dry;else ++filled;
            }
            QVERIFY(filled>30);QVERIFY(dry>30);
            if(qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW")) {
                mask.enabled=false;layer.setMaskSpec(mask);QTest::qWait(150);
                const auto native=canvas.captureMap(&error).toImage();QVERIFY2(!native.isNull(),qPrintable(error));
                QVERIFY(layer.qsgOwnsRendering());
                const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","workplans/artifacts/flow_particle_fallback_2026-10-12");QDir().mkpath(folder);
                const QString name=QString("smooth-%1-%2").arg(classified?"classified":"continuous",invert?"inverted":"normal");
                QVERIFY(cpu.save(folder+"/"+name+"-cpu.png"));QVERIFY(native.save(folder+"/"+name+"-native.png"));
                int compared=0;
                for(int y=60;y<300;y+=13)for(int x=60;x<420;x+=11) {
                    const auto a=pixel(cpu,x,y),b=pixel(native,x,y);
                    const int diff=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
                    QVERIFY2(diff<=5,qPrintable(QString("pixel %1,%2 cpu %3 native %4 diff %5").arg(x).arg(y).arg(a.name()).arg(b.name()).arg(diff)));
                    ++compared;
                }
                QVERIFY(compared>200);mask.enabled=true;layer.setMaskSpec(mask);
            }
        }
        // Two fills obey their actual stack order in the CPU fallback.
        style->setColorRampName("");style->setLowColor(QColor(200,0,0));style->setHighColor(QColor(200,0,0));
        style->setClassified(false);style->setUseCustomRange(false);fill->setOpacity(1);layer.setOpacity(1);
        auto *cell=layer.cellDepthFillSublayer();cell->setVisible(true);
        cell->fillStyle()->setColorRampName("");cell->fillStyle()->setLowColor(QColor(0,0,200));cell->fillStyle()->setHighColor(QColor(0,0,200));
        cell->fillStyle()->setClassified(false);
        layer.moveSublayer(layer.sublayers().indexOf(cell),layer.sublayers().size()-1);
        const auto blue=canvas.captureMap(&error).toImage();QVERIFY(!blue.isNull());
        layer.moveSublayer(layer.sublayers().indexOf(fill),layer.sublayers().size()-1);
        const auto red=canvas.captureMap(&error).toImage();QVERIFY(!red.isNull());
        const auto location=canvas.overlayView()->mapFromScene(QPointF(10,-50));
        const auto at=[&](const QImage &i){return i.pixelColor(qRound(location.x()*i.devicePixelRatio()),qRound(location.y()*i.devicePixelRatio()));};
        QCOMPARE(at(blue),QColor(0,0,200));QCOMPARE(at(red),QColor(200,0,0));
        cell->setVisible(false);
        for(double dpr:{1.,2.}) {
            QImage transformed(QSize(600,450)*dpr,QImage::Format_ARGB32_Premultiplied);
            transformed.setDevicePixelRatio(dpr);transformed.fill(QColor(3,5,7));
            QPainter painter(&transformed);painter.setClipRect(QRectF(50,50,400,300));
            painter.translate(180,80);painter.rotate(17);painter.shear(.15,.1);painter.scale(2,2);
            canvas.overlayView()->scene()->render(&painter,QRectF(0,0,100,100),layer.m_sceneBBox,Qt::IgnoreAspectRatio);
            painter.end();int redPixels=0;
            for(int y=0;y<transformed.height();++y)for(int x=0;x<transformed.width();++x) {
                const auto color=transformed.pixelColor(x,y);
                if(x<50*dpr || x>=450*dpr || y<50*dpr || y>=350*dpr)QCOMPARE(color,QColor(3,5,7));
                redPixels+=color.red()>150 && color.blue()<10;
            }
            QVERIFY(redPixels>1000);
        }
        canvas.zoomAroundCursor(1.3,{240,180});canvas.pan(7,3);canvas.resize(520,400);
        QVERIFY(!canvas.captureMap(&error).isNull());canvas.takeLayer(0,false);
    }
    void visibleCompositionCachesAndFallsBack() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Requires native GPU composition");
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setContinuousParticles(true);
        style->setColor(Qt::transparent);style->setSpacingPx(25);
        style->setMinimumLifetimeSeconds(20);style->setMaximumLifetimeSeconds(40);
        style->setSpeedMultiplier(1);style->setAnimationTimestepMs(16);
        CompositionBackdrop raster(true),vector;
        ParticleReviewCanvas canvas;canvas.resize(480,480);canvas.setShowScaleBar(false);
        canvas.addLayer(&raster,false);canvas.addLayer(&vector,false);canvas.addLayer(&layer,false);
        canvas.reorderLayers({&raster,&vector,&layer},false);
        canvas.setExtent({-10,-10,110,110},false);
        CompositionTool tool(&canvas);canvas.setActiveTool(&tool);canvas.show();
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        QTest::qWait(1500);
        auto initial=canvas.compositionStats();QVERIFY(initial.renderedFrames>10);
        auto *surface=canvas.findChild<QQuickWidget*>("flowMapComposition");QVERIFY(surface);
        QVERIFY(surface->testAttribute(Qt::WA_TransparentForMouseEvents));QCOMPARE(surface->focusPolicy(),Qt::NoFocus);
        QTest::mouseClick(canvas.windowHandle(),Qt::LeftButton,Qt::NoModifier,QPoint(100,100));
        QCOMPARE(tool.presses,1);
        QTest::qWait(500);initial=canvas.compositionStats();
        QTest::qWait(1000);auto ordinary=canvas.compositionStats();
        QVERIFY(ordinary.renderedFrames>initial.renderedFrames+10);
        QCOMPARE(ordinary.captureReadbacks,initial.captureReadbacks);
        QCOMPARE(ordinary.fallbackReadbacks,initial.fallbackReadbacks);
        QCOMPARE(ordinary.textureUploads,initial.textureUploads);
        const double scale=canvas.width()/canvas.extent().width();
        const auto extent=canvas.extent();
        const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        // Finish work already queued for this tick before checking that the
        // capture itself dispatches no new tick or particle lifecycle event.
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const auto before=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(before);
        QString error;const QImage image=canvas.captureMap(&error).toImage();QVERIFY2(!image.isNull(),qPrintable(error));
        const auto after=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(after);
        QCOMPARE(after->clock,before->clock);QCOMPARE(after->births,before->births);QCOMPARE(after->deaths,before->deaths);
        const auto pixel=[&](const QImage &im,int x,int y){return im.pixelColor(qRound(x*im.devicePixelRatio()),qRound(y*im.devicePixelRatio()));};
        QCOMPARE(pixel(image,20,20),QColor(10,220,30));
        QCOMPARE(pixel(image,60,60),QColor(20,60,100));
        const QColor blend=pixel(image,240,240);
        QVERIFY(std::abs(blend.red()-110)<=2);QVERIFY(std::abs(blend.green()-80)<=2);QVERIFY(std::abs(blend.blue()-50)<=2);
        QCOMPARE(blend.alpha(),255);
        auto mask=layer.maskSpec();mask.enabled=true;layer.setMaskSpec(mask);
        QTRY_VERIFY(!canvas.compositionStats().active);QVERIFY(!layer.qsgOwnsRendering());
        const auto masked=canvas.captureMap(&error);QVERIFY2(!masked.isNull(),qPrintable(error));
        QCOMPARE(pixel(masked.toImage(),240,240),blend);
        mask.enabled=false;layer.setMaskSpec(mask);QTRY_VERIFY(canvas.compositionStats().active);
        SWMM2DResultsLayer second;second.setSource(std::make_unique<FlowSource>());
        canvas.addLayer(&second,false);QTRY_VERIFY(!canvas.compositionStats().active);
        const auto multiple=canvas.captureMap(&error);QVERIFY2(!multiple.isNull(),qPrintable(error));
        canvas.takeLayer(canvas.layers().indexOf(&second),false);QTRY_VERIFY(canvas.compositionStats().active);
        // A CPU layer above the native output cannot fit into its background.
        auto *fill=layer.cellDepthFillSublayer();fill->setVisible(true);
        fill->fillStyle()->setColorRampName("");
        fill->fillStyle()->setLowColor(QColor(0,0,200));fill->fillStyle()->setHighColor(QColor(0,0,200));
        fill->fillStyle()->setClassified(false);
        canvas.reorderLayers({&raster,&layer,&vector},false);QTRY_VERIFY(!canvas.compositionStats().active);
        const auto ordered=canvas.captureMap(&error).toImage();QVERIFY2(!ordered.isNull(),qPrintable(error));
        const QColor over=pixel(ordered,240,200);
        QVERIFY(std::abs(over.red()-100)<=2);QVERIFY(std::abs(over.green()-50)<=2);QVERIFY(std::abs(over.blue()-100)<=2);
        canvas.reorderLayers({&raster,&vector,&layer},false);QTRY_VERIFY(canvas.compositionStats().active);
        layer.setOpacity(.4);QTest::qWait(100);QVERIFY(canvas.compositionStats().active);
        const auto translucent=canvas.captureMap(&error).toImage();QVERIFY2(!translucent.isNull(),qPrintable(error));
        const QColor opacity=pixel(translucent,240,200);
        QVERIFY(std::abs(opacity.red()-66)<=2);QVERIFY(std::abs(opacity.green()-48)<=2);QVERIFY(std::abs(opacity.blue()-110)<=2);
        QVERIFY(canvas.moveSublayer(&layer,0,int(layer.sublayers().size())-1,false));QTest::qWait(100);
        QVERIFY(canvas.compositionStats().active);QVERIFY(!canvas.captureMap(&error).isNull());
        const auto oldExtent=canvas.extent();canvas.zoomAroundCursor(1.2,{240,240});QTest::qWait(150);
        QVERIFY(canvas.extent()!=oldExtent);QVERIFY(!canvas.captureMap(&error).isNull());
        canvas.pan(5,3);canvas.resize(520,400);QTest::qWait(200);
        QCOMPARE(surface->geometry(),canvas.rect());QVERIFY(!canvas.captureMap(&error).isNull());
        canvas.hide();QTest::qWait(300);const auto hidden=canvas.compositionStats().renderedFrames;
        QTest::qWait(300);QCOMPARE(canvas.compositionStats().renderedFrames,hidden);
        canvas.show();QTRY_VERIFY(canvas.compositionStats().renderedFrames>hidden);
        qputenv("OPENSWMM_FLOW_COMPOSITION","0");canvas.update();QTRY_VERIFY(!canvas.compositionStats().active);
        QVERIFY(!canvas.captureMap(&error).isNull());qunsetenv("OPENSWMM_FLOW_COMPOSITION");
        canvas.update();QTRY_VERIFY(canvas.compositionStats().active);
        canvas.setActiveTool(nullptr);
        canvas.takeLayer(canvas.layers().indexOf(&layer),false);QVERIFY(!layer.qsgOwnsRendering());
        QVERIFY(!canvas.compositionStats().active);QVERIFY(!canvas.captureMap(&error).isNull());
        while(canvas.layerCount())canvas.takeLayer(0,false);
    }
    void continuousNativePresentation() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Run the native layer comparison window");
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/native-layer");QDir().mkpath(folder);
        SWMM2DResultsLayer layer;const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(path.isEmpty())layer.setSource(std::make_unique<GridFlowSource>(400,100));
        else {
            auto source=std::make_unique<HDF5Mesh2DSource>();QVERIFY(source->open(path));
            layer.setSource(std::move(source));layer.setCurrentTimeIndex(layer.source()->timeCount()-1);
        }
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);auto *style=sub->streakStyle();
        style->setContinuousParticles(true);style->setMinimumLifetimeSeconds(20);style->setMaximumLifetimeSeconds(40);
        style->setTrailDurationSeconds(1);style->setSpeedMultiplier(2);style->setSpacingPx(30);
        bool customSpeed=false;const double speed=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_SPEED").toDouble(&customSpeed);
        if(customSpeed)style->setSpeedMultiplier(speed);
        for(const auto &setting:{qMakePair("FLOW_PARTICLE_NATIVE_TRAIL","trailDurationSeconds"),
                                 qMakePair("FLOW_PARTICLE_NATIVE_MIN_LIFE","minimumLifetimeSeconds"),
                                 qMakePair("FLOW_PARTICLE_NATIVE_MAX_LIFE","maximumLifetimeSeconds")}) {
            if(!qEnvironmentVariableIsSet(setting.first))continue;
            bool valid=false;const double value=qEnvironmentVariable(setting.first).toDouble(&valid);QVERIFY(valid);
            style->setProperty(setting.second,value);
        }
        if(qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_COLOR"))style->setColor(QColor(qEnvironmentVariable("FLOW_PARTICLE_NATIVE_COLOR")));
        layer.meshNodeSublayer()->setVisible(false);const auto box=layer.m_sceneBBox;
        ParticleReviewCanvas canvas;canvas.setWindowTitle("Continuous particles — native SWMM layer preview");
        canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        canvas.resize(1280,800);canvas.addLayer(&layer,false);
        canvas.setExtent({box.left(),-box.bottom(),box.right(),-box.top()},false);canvas.show();
        QTest::qWait(1500);QVERIFY(layer.qsgOwnsRendering());
        const double scale=canvas.width()/box.width();
        QTRY_VERIFY_WITH_TIMEOUT(layer.flowParticles(box,scale,layer.frameRevision())!=nullptr,10000);
        QElapsedTimer elapsed;elapsed.start();QTest::qWait(30000);
        const auto frame=layer.flowParticles(box,scale,layer.frameRevision());QVERIFY(frame);
        QVERIFY(frame->births>10);QVERIFY(frame->clock>1);QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::VectorConflict)],quint64(0));
        QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::InvalidField)],quint64(0));
        QString error;const auto image=canvas.captureMap(&error);QVERIFY2(!image.isNull(),qPrintable(error));
        QVERIFY(image.save(folder+"/native.png"));
        std::sort(canvas.paintMs.begin(),canvas.paintMs.end());
        const auto percentile=[&](double q){return canvas.paintMs.empty()?0.:canvas.paintMs[size_t(q*(canvas.paintMs.size()-1))];};
        QJsonArray stops;for(auto count:frame->stopCounts)stops.append(qint64(count));
        int waiting=0,limited=0,crossings=0,maxCrossings=0,reducedSegments=0;size_t history=0,peakHistory=0;
        double maxError=0;
        for(const auto &p:frame->particles) {
            waiting+=p.waitingForData;limited+=p.historyLimited;crossings+=p.crossings;
            maxCrossings=std::max(maxCrossings,p.crossings);history+=p.history.size();
            peakHistory=std::max(peakHistory,p.history.size());
            for(const auto &sample:p.history)if(sample.error>=0) {++reducedSegments;maxError=std::max(maxError,sample.error);}
        }
        const auto composition=canvas.compositionStats();
        auto intervals=composition.renderIntervalsMs;
        std::sort(intervals.begin(),intervals.end());
        const auto renderPercentile=[&](double q){return intervals.isEmpty()?0.:intervals[int(q*(intervals.size()-1))];};
        QFile report(folder+"/native.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"source",path.isEmpty()?"synthetic native SWMM":path},
            {"triangles",layer.m_sceneTris.size()},{"births",qint64(frame->births)},{"deaths",qint64(frame->deaths)},
            {"stopCounts",stops},{"animationSeconds",frame->clock},{"observedWallSeconds",elapsed.elapsed()/1000.},
            {"waitingForData",waiting},{"historyLimited",limited},{"cellCrossings",crossings},
            {"maxParticleCrossings",maxCrossings},{"historySamples",qint64(history)},{"speedMultiplier",style->speedMultiplier()},
            {"reducedSegments",reducedSegments},{"maxParticleHistorySamples",qint64(peakHistory)},
            {"maxRetainedErrorPx",maxError*scale},{"trailSeconds",style->trailDurationSeconds()},
            {"minimumLifetimeSeconds",style->minimumLifetimeSeconds()},{"maximumLifetimeSeconds",style->maximumLifetimeSeconds()},
            {"paintMedianMs",percentile(.5)},{"paintP95Ms",percentile(.95)},
            {"compositionActive",composition.active},{"renderedFrames",qint64(composition.renderedFrames)},
            {"textureUploads",qint64(composition.textureUploads)},{"captureReadbacks",qint64(composition.captureReadbacks)},
            {"fallbackReadbacks",qint64(composition.fallbackReadbacks)},
            {"renderIntervalMedianMs",renderPercentile(.5)},{"renderIntervalP95Ms",renderPercentile(.95)},
            {"presentation","Qt Quick render-completion notifications; not OS display presentation; paint costs are separate"}}).toJson());
        canvas.takeLayer(0,false);
    }
    void continuousRegionalLargeMesh_data() {
        QTest::addColumn<bool>("quad");
        QTest::newRow("native-triangles")<<false;QTest::newRow("native-quads")<<true;
    }
    void canvasAttachmentPreservesUnprojectedRegionalParticles() {
        const auto original=qgetenv("OPENSWMM_REGIONAL_SURFACE");qputenv("OPENSWMM_REGIONAL_SURFACE","1");
        const auto restore=qScopeGuard([&]{if(original.isNull())qunsetenv("OPENSWMM_REGIONAL_SURFACE");else qputenv("OPENSWMM_REGIONAL_SURFACE",original);});
        for(bool quad:{false,true}) {
            SWMM2DResultsLayer layer;
            if(quad)layer.setSource(std::make_unique<QuadGridFlowSource>(160,120));
            else layer.setSource(std::make_unique<GridFlowSource>(160,120));
            layer.flowStreakSublayer()->setVisible(true);layer.flowStreakSublayer()->streakStyle()->setContinuousParticles(true);
            const QRectF view(100,-60,20,20);QVERIFY(layer.prepareFlowStreaksForCapture(view,20));
            layer.advanceFlowStreaks(.3);QVERIFY(layer.prepareFlowStreaksForCapture(view,20));
            const auto before=layer.flowParticles(view,20,layer.frameRevision());QVERIFY(before && !before->particles.empty());
            const auto geometry=layer.geomRevision();const auto revision=layer.frameRevision();
            MapCanvas canvas;canvas.resize(480,480);canvas.addLayer(&layer,false);
            QCOMPARE(layer.geomRevision(),geometry);QCOMPARE(layer.frameRevision(),revision);
            const auto attached=layer.flowParticles(view,20,revision);QCOMPARE(attached,before);
            canvas.setExtent({100,40,120,60},false);QVERIFY(layer.prepareFlowStreaksForCapture(view,20));
            const auto after=layer.flowParticles(view,20,layer.frameRevision());QVERIFY(after);
            for(const auto &old:before->particles) {
                const auto it=std::find_if(after->particles.begin(),after->particles.end(),[&](const auto &p){return p.id==old.id;});
                QVERIFY(it!=after->particles.end());QCOMPARE(it->position,old.position);QCOMPARE(it->age,old.age);
                QCOMPARE(it->lifetime,old.lifetime);QCOMPARE(it->history.size(),old.history.size());
            }
            canvas.takeLayer(0,false);
        }
    }
    void continuousRegionalLargeMesh() {
        QFETCH(bool,quad);
        const bool large=qEnvironmentVariableIsSet("FLOW_PARTICLE_6M");
        const bool native=qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW");
        const int cols=large?2000:160,rows=large?(quad?3000:1500):120;
        QElapsedTimer timer;timer.start();
        SWMM2DResultsLayer layer;
        std::unique_ptr<GridFlowSource> source=quad?std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(cols,rows))
                                                  :std::make_unique<GridFlowSource>(cols,rows);
        auto *raw=source.get();layer.setSource(std::move(source));
        const double loadMs=timer.nsecsElapsed()/1e6;
        QCOMPARE(layer.m_sceneTris.size(),cols*rows*2);
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);auto *style=sub->streakStyle();
        style->setContinuousParticles(true);style->setSpeedMultiplier(1);style->setSpacingPx(30);
        style->setMinimumLifetimeSeconds(60);style->setMaximumLifetimeSeconds(90);style->setTrailDurationSeconds(10);
        style->setAnimationTimestepMs(16);style->setColor(QColor(10,30,80));
        const QRectF view(cols*.65,-rows*.5,40,30);const double scale=32;
        timer.restart();QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const double prepareMs=timer.nsecsElapsed()/1e6;
        if(large)QVERIFY(layer.regionalHydraulicsPublished());
        const auto input=layer.captureFlowParticleInput(view,scale);QVERIFY(input);
        QVERIFY(input->triangles.size()<size_t(FlowStreakMesh::maxRegionTriangles));
        const size_t payload=input->mesh->bytes()+input->triangles.capacity()*sizeof(FlowParticleNativeTriangle);
        QVERIFY(payload<32*1024*1024);
        qInfo()<<"Resident display triangles"<<input->triangles.size()<<"input payload bytes"<<payload;
        int croppedEdges=0;
        for(size_t i=0;i<input->triangles.size();++i)for(int e=0;e<3;++e) {
            if(input->mesh->neighbors[i][e]>=0)continue;
            const auto &ids=input->mesh->triangles[i];
            const auto midpoint=(input->mesh->vertices[ids[(e+1)%3]]+input->mesh->vertices[ids[(e+2)%3]])*.5;
            const auto box=layer.m_sceneBBox;
            if(std::abs(midpoint.x()-box.left())<1e-6 || std::abs(midpoint.x()-box.right())<1e-6
                || std::abs(midpoint.y()-box.top())<1e-6 || std::abs(midpoint.y()-box.bottom())<1e-6)continue;
            QVERIFY2(!(input->triangles[i].residentEdges&(1<<e)),"A regional edge inside the native domain must wait, not become an outlet");
            ++croppedEdges;
        }
        QVERIFY(croppedEdges>0);
        const int fullReads=raw->reads,regionalReads=raw->regionalReads;
        timer.restart();
        for(int i=0;i<120;++i) {
            layer.advanceFlowStreaks(1./60.);QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        }
        const double advanceMs=timer.nsecsElapsed()/1e6;
        const auto first=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(first);
        QVERIFY(first->particles.size()>10);QCOMPARE(raw->reads,fullReads);QCOMPARE(raw->regionalReads,regionalReads);
        int crossings=0;for(const auto &p:first->particles)crossings+=p.crossings;QVERIFY(crossings>0);
        // A new live packet must use a regional read and retain particle IDs,
        // assigned lifetimes, recorded history and world positions.
        raw->speed=3;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=first->revision,10000);
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const auto updated=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(updated);
        for(const auto &old:first->particles) {
            const auto it=std::find_if(updated->particles.begin(),updated->particles.end(),[&](const auto &p){return p.id==old.id;});
            QVERIFY(it!=updated->particles.end());QCOMPARE(it->position,old.position);QCOMPARE(it->age,old.age);
            QCOMPARE(it->lifetime,old.lifetime);QCOMPARE(it->history.size(),old.history.size());
        }
        if(large) {QCOMPARE(raw->reads,fullReads);QVERIFY(raw->regionalReads>regionalReads);}
        const QRectF distant(cols*.1,-rows*.2,40,30);
        QVERIFY(layer.prepareFlowStreaksForCapture(distant,scale));
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(distant,scale));
        const auto away=layer.flowParticles(distant,scale,layer.frameRevision());QVERIFY(away);
        for(const auto &old:first->particles) {
            const auto it=std::find_if(away->particles.begin(),away->particles.end(),[&](const auto &p){return p.id==old.id;});
            QVERIFY(it!=away->particles.end());QCOMPARE(it->position,old.position);QVERIFY(it->waitingForData);
        }
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const auto returned=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(returned);
        int resumed=0;for(const auto &old:first->particles)for(const auto &p:returned->particles)
            if(p.id==old.id && p.position.x()>old.position.x())++resumed;
        QVERIFY(resumed>10);
        QJsonObject report{{"nativeCells",raw->triangleCount()},{"displayTriangles",layer.m_sceneTris.size()},
            {"cellType",quad?"quad":"triangle"},{"loadMs",loadMs},{"regionPrepareMs",prepareMs},
            {"manualAdvanceMs",advanceMs},{"manualAnimationSeconds",2},{"particles",int(first->particles.size())},
            {"cellCrossings",crossings},{"regionTriangles",int(input->triangles.size())},
            {"inputPayloadBytes",qint64(payload)},{"fullReadsAfterLoad",raw->reads-fullReads},
            {"regionalReads",raw->regionalReads},{"resumedParticles",resumed},
            {"memoryScope","input payload only; process high-water includes baseline mesh, map and renderer"}};
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","workplans/artifacts/flow_particle_fallback_2026-10-12");QDir().mkpath(folder);
        const QString name=QString(large?"6m-%1":"regional-%1").arg(quad?"quads":"triangles");
        if(native) {
            ParticleReviewCanvas canvas;canvas.resize(1920,1080);canvas.setShowScaleBar(false);
            canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
            canvas.addLayer(&layer,false);canvas.setExtent({view.left(),-view.bottom(),view.right(),-view.top()},false);canvas.show();
            QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);QTest::qWait(3000);
            const auto before=canvas.compositionStats();QTest::qWait(15000);const auto after=canvas.compositionStats();
            QVERIFY(after.renderedFrames>before.renderedFrames+100);
            QCOMPARE(after.fallbackReadbacks,before.fallbackReadbacks);QCOMPARE(after.captureReadbacks,before.captureReadbacks);
            QCOMPARE(after.textureUploads,before.textureUploads);
            // The bounded interval history has at most 4096 entries; this
            // sample fits without eviction and excludes warm-up intervals.
            auto intervals=after.renderIntervalsMs.mid(before.renderIntervalsMs.size());std::sort(intervals.begin(),intervals.end());
            QVERIFY(intervals.size()>100);
            report.insert("renderIntervalMedianMs",intervals[int(.5*(intervals.size()-1))]);
            report.insert("renderIntervalP95Ms",intervals[int(.95*(intervals.size()-1))]);
            report.insert("intervalWithin30FpsTarget",intervals[int(.95*(intervals.size()-1))]<=33.3);
            report.insert("renderedFrames",qint64(after.renderedFrames-before.renderedFrames));
            report.insert("logicalViewportWidth",canvas.width());report.insert("logicalViewportHeight",canvas.height());
            report.insert("devicePixelRatio",canvas.devicePixelRatioF());
            report.insert("steadyFallbackReadbacks",qint64(after.fallbackReadbacks-before.fallbackReadbacks));
            report.insert("steadyTextureUploads",qint64(after.textureUploads-before.textureUploads));
            report.insert("cadenceScope","Qt Quick render completions; synthetic live source; not solver impact or OS presentation");
            const auto current=layer.flowParticles(view,scale,layer.frameRevision(),false);QVERIFY(current);
            int waiting=0,limited=0,moving=0;size_t history=0;
            for(const auto &p:current->particles) {waiting+=p.waitingForData;limited+=p.historyLimited;history+=p.history.size();
                moving+=!p.waitingForData && p.stop==FlowParticleStop::None;}
            report.insert("steadyParticles",int(current->particles.size()));report.insert("waitingForData",waiting);
            report.insert("shortenedTails",limited);report.insert("historySamples",qint64(history));
            report.insert("animationSeconds",current->clock);
            report.insert("movingParticles",moving);report.insert("motionPresentAtEnd",moving>0);
            report.insert("performanceCertified",false); // short profile; residency/lifetime/memory gates remain
            report.insert("births",qint64(current->births));report.insert("deaths",qint64(current->deaths));
            QJsonArray stops;for(auto count:current->stopCounts)stops.append(qint64(count));report.insert("stopCounts",stops);
            QString error;const auto image=canvas.captureMap(&error);QVERIFY2(!image.isNull(),qPrintable(error));QVERIFY(image.save(folder+"/"+name+".png"));
            canvas.takeLayer(0,false);
        }
        QFile file(folder+"/"+name+".json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());
        if(large) { // The bounded overview rejection must happen before copying a full field.
            timer.restart();QVERIFY(!layer.captureFlowParticleInput(layer.m_sceneBBox,1));
            QVERIFY(timer.elapsed()<1000);QVERIFY(layer.flowStreakStatus().contains("Zoom in"));
        }
    }
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
        sub->streakStyle()->setHeadWidthPx(8);
        sub->streakStyle()->setAnimationTimestepMs(33);
        sub->streakStyle()->setMinimumLifetimeSeconds(20);
        sub->streakStyle()->setMaximumLifetimeSeconds(30);
        sub->streakStyle()->setLifetimeFadeSeconds(1.5);
        sub->streakStyle()->setTrailDurationSeconds(3.5);
        sub->streakStyle()->setColor(QColor(20,40,80,170));sub->streakStyle()->setSpeedMultiplier(12);
        // Explicit saved settings, including the previous defaults, must win
        // over the calmer defaults when an existing project is reopened.
        sub->streakStyle()->setTrailLengthPx(24);sub->streakStyle()->setPathLengthPx(160);
        sub->streakStyle()->setFade(false);sub->streakStyle()->setTaper(false);
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
        auto *head=panel.findChild<QDoubleSpinBox*>("headWidthPx");QVERIFY(head);
        QCOMPARE(head->value(),1.5);
        auto *motion=panel.findChild<QCheckBox*>("animated");QVERIFY(motion);
        auto *style=layer.flowStreakSublayer()->streakStyle();
        for(const auto &setting:{qMakePair("speedMultiplier",2.),qMakePair("trailLengthPx",80.),
                                 qMakePair("pathLengthPx",350.),qMakePair("animationTimestepMs",16.),
                                 qMakePair("minimumLifetimeSeconds",8.),qMakePair("maximumLifetimeSeconds",16.),
                                 qMakePair("lifetimeFadeSeconds",.5),qMakePair("trailDurationSeconds",2.)}) {
            auto *control=panel.findChild<QDoubleSpinBox*>(setting.first);QVERIFY(control);
            QCOMPARE(control->value(),setting.second);
        }
        for(const char *name:{"fade","taper"}) {
            auto *control=panel.findChild<QCheckBox*>(name);QVERIFY(control);QVERIFY(control->isChecked());
        }
        width->setValue(4.5);QCOMPARE(style->widthPx(),4.5);
        style->setWidthPx(2.5);QCOMPARE(width->value(),2.5);
        head->setValue(7);QCOMPARE(style->headWidthPx(),7.);QCOMPARE(style->widthPx(),2.5);
        style->setHeadWidthPx(9);QCOMPARE(head->value(),9.);QCOMPARE(width->value(),2.5);
        auto *timestep=panel.findChild<QDoubleSpinBox*>("animationTimestepMs");QVERIFY(timestep);
        timestep->setValue(33);QCOMPARE(style->animationTimestepMs(),33);
        style->setAnimationTimestepMs(50);QCOMPARE(timestep->value(),50.);
        style->setAnimationTimestepMs(1);QCOMPARE(timestep->value(),8.);
        style->setAnimationTimestepMs(2000);QCOMPARE(timestep->value(),2000.);
        auto *minimum=panel.findChild<QDoubleSpinBox*>("minimumLifetimeSeconds");
        auto *trailMode=panel.findChild<QComboBox*>("velocityScaledTrail");QVERIFY(trailMode);
        auto *trailDuration=panel.findChild<QDoubleSpinBox*>("trailDurationSeconds");
        auto *trailPixels=panel.findChild<QDoubleSpinBox*>("trailLengthPx");
        QCOMPARE(trailMode->currentIndex(),0);QVERIFY(trailDuration->isEnabled());QVERIFY(!trailPixels->isEnabled());
        trailDuration->setValue(3.5);QCOMPARE(style->trailDurationSeconds(),3.5);
        trailMode->setCurrentIndex(1);QVERIFY(!style->velocityScaledTrail());QVERIFY(!trailDuration->isEnabled());QVERIFY(trailPixels->isEnabled());
        style->setVelocityScaledTrail(true);QCOMPARE(trailMode->currentIndex(),0);QVERIFY(trailDuration->isEnabled());
        style->setTrailDurationSeconds(4.5);QCOMPARE(trailDuration->value(),4.5);
        auto *maximum=panel.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds");
        minimum->setValue(20);QCOMPARE(style->minimumLifetimeSeconds(),20.);QCOMPARE(maximum->value(),20.);
        maximum->setValue(10);QCOMPARE(style->maximumLifetimeSeconds(),10.);QCOMPARE(minimum->value(),10.);
        style->setMaximumLifetimeSeconds(25);QCOMPARE(maximum->value(),25.);
        motion->setChecked(false);QVERIFY(!style->animated());style->setAnimated(true);QVERIFY(motion->isChecked());
    }
    void stylePanelAcceptsExtendedRangesAndPreservesSavedValues() {
        SWMM2DResultsLayer layer;openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        auto *style=layer.flowStreakSublayer()->streakStyle();
        // Exercise actual typing/Enter, not just programmatic spin-box edits.
        // Every value exceeds the old UI AND style-bag ceiling.
        for(const auto &setting:{qMakePair("widthPx",24.),qMakePair("headWidthPx",48.),
                                 qMakePair("trailLengthPx",2000.),qMakePair("trailDurationSeconds",120.),
                                 qMakePair("spacingPx",1000.),qMakePair("pathLengthPx",4096.),
                                 qMakePair("speedMultiplier",5000.),qMakePair("minimumLifetimeSeconds",600.),
                                 qMakePair("maximumLifetimeSeconds",3600.),qMakePair("lifetimeFadeSeconds",30.),
                                 qMakePair("animationTimestepMs",5000.),qMakePair("dryDepthCutoff",25.)}) {
            if(QString::fromLatin1(setting.first)=="trailLengthPx")style->setVelocityScaledTrail(false);
            if(QString::fromLatin1(setting.first)=="trailDurationSeconds")style->setVelocityScaledTrail(true);
            auto *control=panel.findChild<QDoubleSpinBox*>(setting.first);QVERIFY(control);
            QVERIFY(control->isEnabled());const auto previous=style->property(setting.first);
            control->selectAll();QTest::keyClicks(control,QString::number(setting.second));
            QCOMPARE(style->property(setting.first),previous); // partially typed entries do not write the model
            QTest::keyClick(control,Qt::Key_Return);
            QCOMPARE(control->value(),setting.second);QCOMPARE(style->property(setting.first).toDouble(),setting.second);
        }
        const auto json=QJsonDocument::fromJson(QJsonDocument(style->toJson()).toJson()).object();
        FlowStreakStyle restored;restored.fromJson(json);QCOMPARE(restored.toJson(),style->toJson());
        // Opening/synchronizing an editor must never truncate an existing value.
        openswmmvis::ui::Swmm2DResultsStylePanel reopened(&layer);
        QCOMPARE(reopened.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds")->value(),3600.);
        style->setMaximumLifetimeSeconds(7200);
        QCOMPARE(panel.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds")->value(),7200.);
        QCOMPARE(reopened.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds")->value(),7200.);
        // Only numeric representation limits remain; derived products stay finite.
        style->setMaximumLifetimeSeconds(std::numeric_limits<double>::max());
        style->setSpeedMultiplier(std::numeric_limits<double>::max());
        QCOMPARE(style->maximumLifetimeSeconds(),FlowStreakStyle::maximumNumericValue());
        QCOMPARE(panel.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds")->value(),style->maximumLifetimeSeconds());
        QVERIFY(std::isfinite(style->pathSettings()["duration"].toDouble()));
        style->setAnimationTimestepMs(std::numeric_limits<int>::max());
        QCOMPARE(panel.findChild<QDoubleSpinBox*>("animationTimestepMs")->value(),double(std::numeric_limits<int>::max()));
    }
    void continuousLongTrailKeepsHistoryAndPopulationBounded() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();source->speed=.0001f;
        layer.setSource(std::move(source));
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setContinuousParticles(true);style->setSpeedMultiplier(1);
        style->setMinimumLifetimeSeconds(600);style->setMaximumLifetimeSeconds(3600);
        // Far beyond int range after conversion to samples: must clamp BEFORE
        // casting, keeping the requested duration rather than wrapping it.
        style->setTrailDurationSeconds(FlowStreakStyle::maximumNumericValue());
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        layer.advanceFlowStreaks(120);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto frame=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(frame);
        QVERIFY(!frame->particles.empty());QCOMPARE(frame->deaths,quint64(0));
        for(const auto &p:frame->particles) {
            QVERIFY(p.lifetime>=600 && p.lifetime<=3600);QVERIFY(p.history.size()<=4096);
            QVERIFY(p.age>119);QVERIFY(!p.historyLimited);QCOMPARE(p.history.front().age,0.);
            bool reduced=false;for(const auto &sample:p.history)reduced|=sample.error>=0;QVERIFY(reduced);
        }
        QCOMPARE(style->trailDurationSeconds(),FlowStreakStyle::maximumNumericValue());
        // The long-tail history allocation still reduces the maximum pool.
        QVERIFY(frame->particles.size()<=size_t((96*1024*1024)/(4*sizeof(FlowParticleSample)*4096)));
    }
    void headWidthPreservesLegacyStyles() {
        FlowStreakStyle style;
        const auto settings=style.pathSettings();
        style.fromJson({{"widthPx",4.}}); // projects saved before independent heads
        QCOMPARE(style.headWidthPx(),4.);
        for(bool taper:{false,true}) {
            style.setTaper(taper);
            for(double a:{0.,.15,.5,.75,.875,1.})
                QCOMPARE(style.widthAtFraction(a),4*(taper?std::max(.15,a):1.));
            style.setHeadWidthPx(10);
            QCOMPARE(style.widthAtFraction(1),10.);
            QCOMPARE(style.widthAtFraction(.5),taper?2.:4.);
            style.setHeadWidthPx(4);
        }
        style.fromJson({{"widthPx",3.},{"headWidthPx",8.}});
        QCOMPARE(style.widthPx(),3.);QCOMPARE(style.headWidthPx(),8.);
        style.setHeadWidthPx(-1);QCOMPARE(style.headWidthPx(),.1);
        style.setHeadWidthPx(30);QCOMPARE(style.headWidthPx(),30.);
        style.setHeadWidthPx(std::numeric_limits<double>::quiet_NaN());
        style.setHeadWidthPx(std::numeric_limits<double>::infinity());QCOMPARE(style.headWidthPx(),30.);
        // Even the narrowest head on the widest tapered trail stays positive.
        style.setWidthPx(12);style.setHeadWidthPx(.1);style.setTaper(true);
        for(int i=0;i<=100;++i)QVERIFY(style.widthAtFraction(i/100.)>0);
        QCOMPARE(style.pathSettings(),settings);
    }
    void trailDurationValidationAndLegacyStyles() {
        FlowStreakStyle style;QVERIFY(style.velocityScaledTrail());QCOMPARE(style.activeTrailDurationSeconds(),2.);
        const auto key=style.pathSettings();
        style.setTrailDurationSeconds(5.5);QCOMPARE(style.pathSettings(),key);
        FlowStreakStyle reopened;reopened.fromJson(style.toJson());QCOMPARE(reopened.toJson(),style.toJson());
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            style.setTrailDurationSeconds(invalid);QCOMPARE(style.trailDurationSeconds(),5.5);
        }
        style.setTrailDurationSeconds(-1);QCOMPARE(style.trailDurationSeconds(),.1);
        style.setTrailDurationSeconds(200);QCOMPARE(style.trailDurationSeconds(),200.);
        style.fromJson({{"trailLengthPx",24.}});QVERIFY(!style.velocityScaledTrail());QCOMPARE(style.activeTrailDurationSeconds(),0.);
        QCOMPARE(style.trailLengthPx(),24.);QCOMPARE(style.pathSettings(),key);
        reopened.fromJson(style.toJson());QCOMPARE(reopened.toJson(),style.toJson());
        style.setVelocityScaledTrail(true);QCOMPARE(style.activeTrailDurationSeconds(),200.);
    }
    void lifetimeValidationAndLegacyStyles() {
        FlowStreakStyle style;
        style.fromJson({{"trailLengthPx",24.},{"speedMultiplier",12.}});
        QCOMPARE(style.minimumLifetimeSeconds(),8.);QCOMPARE(style.maximumLifetimeSeconds(),16.);
        QCOMPARE(style.lifetimeFadeSeconds(),.5);
        const auto key=style.pathSettings();
        style.setMinimumLifetimeSeconds(10);style.setLifetimeFadeSeconds(1.5);
        QCOMPARE(style.pathSettings(),key); // appearance/lower lifetime do not retrace
        style.setMaximumLifetimeSeconds(30);QVERIFY(style.pathSettings()!=key);
        FlowStreakStyle reopened;reopened.fromJson(style.toJson());QCOMPARE(reopened.toJson(),style.toJson());
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            style.setMinimumLifetimeSeconds(invalid);style.setMaximumLifetimeSeconds(invalid);style.setLifetimeFadeSeconds(invalid);
            QCOMPARE(style.toJson(),reopened.toJson());
        }
        style.setMinimumLifetimeSeconds(200);QCOMPARE(style.minimumLifetimeSeconds(),200.);QCOMPARE(style.maximumLifetimeSeconds(),200.);
        style.setMaximumLifetimeSeconds(-1);QCOMPARE(style.minimumLifetimeSeconds(),.1);QCOMPARE(style.maximumLifetimeSeconds(),.1);
        style.setLifetimeFadeSeconds(20);QCOMPARE(style.lifetimeFadeSeconds(),20.);
        style.setLifetimeFadeSeconds(-1);QCOMPARE(style.lifetimeFadeSeconds(),0.);
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
    void animationSpeedIsIndependentOfTimestep() {
        for(int interval:{8,16,33,1000}) {
            SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();
            layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
            auto *style=layer.flowStreakSublayer()->streakStyle();style->setAnimationTimestepMs(interval);
            QTRY_VERIFY_WITH_TIMEOUT(paths(layer)!=nullptr,5000);
            const auto batch=paths(layer);const auto key=style->pathSettings();
            const int reads=raw->reads,builds=layer.flowStreakBuildCount();
            int remaining=1000;
            while(remaining>0) {const int dt=std::min(remaining,interval);layer.advanceFlowStreaks(dt/1000.);remaining-=dt;}
            QVERIFY(std::abs(layer.flowStreakSeconds()-2)<1e-10);
            QVERIFY(std::abs(layer.flowStreakDisplaySeconds()-1)<1e-10);
            QCOMPARE(paths(layer),batch);QCOMPARE(style->pathSettings(),key);
            QCOMPARE(raw->reads,reads);QCOMPARE(layer.flowStreakBuildCount(),builds);
        }
    }
    void playbackUsesMatchingWetRevision() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();
        source->live=true;layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        const auto box=layer.m_sceneBBox;const auto revision=layer.frameRevision();
        auto present=[&](quint64 r){return layer.presentedFlowStreaks(box,8,r);};
        QTRY_VERIFY_WITH_TIMEOUT(present(revision),5000);const auto first=present(revision);
        layer.advanceFlowStreaks(.5);raw->speed=3;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>revision,5000);
        QCOMPARE(present(revision),first);
        const auto current=layer.frameRevision();
        QTRY_VERIFY_WITH_TIMEOUT(present(current),5000);
        const auto next=present(current);QVERIFY(next!=first);
        QVERIFY(!next->paths.empty());QCOMPARE(present(current),next);
        raw->depths.assign(3,0);++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>current,5000);
        const auto dry=present(layer.frameRevision());QVERIFY(!dry || dry->paths.empty());
        layer.setSource(std::make_unique<FlowSource>());
        QTRY_VERIFY_WITH_TIMEOUT(present(layer.frameRevision()),5000);
        for(const auto &p:present(layer.frameRevision())->paths)QCOMPARE(p.appearedAt,-1.f);
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
        // Keep this regional-fit fixture below its fixed cell budget, independent
        // of the user-facing default path length.
        layer.flowStreakSublayer()->streakStyle()->setPathLengthPx(160);
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
        const auto geometry=layer.geomRevision();
        layer.onCanvasCRSChanged(nullptr);
        QCOMPARE(layer.geomRevision(),geometry);QVERIFY(layer.regionalHydraulicsPublished());
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center())-.4)<1e-5);
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
        layer.flowStreakSublayer()->streakStyle()->setPathLengthPx(160); // fixed accuracy-comparison footprint
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
        layer.flowStreakSublayer()->streakStyle()->setPathLengthPx(160); // fixed regional benchmark footprint
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
            MapCanvas canvas;
            // Native benchmark windows display results without accepting
            // desktop gestures that would change the measured viewport.
            canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
            canvas.resize(1920,1080);canvas.addLayer(&layer,false);
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
                    clock.restart();layer.advanceFlowStreaks(.033);
                    QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
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
                {"phasePathRebuilds",layer.flowStreakBuildCount()-before},{"forcedCapture",true},{"runs",runs}};
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
                const auto box=layer.m_sceneBBox;MapCanvas canvas;
                // Native benchmark windows display results without accepting
                // desktop gestures that would change the measured viewport.
                canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
                canvas.resize(1920,1080);canvas.addLayer(&layer,false);
                canvas.setExtent({box.left(),-box.bottom(),box.right(),-box.top()},false);canvas.show();
                QString error;clock.restart();QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
                const double captureMs=clock.nsecsElapsed()/1e6;QTest::qWait(1500);
                QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),30000);
                QVERIFY(layer.qsgOwnsRendering());
                const int builds=layer.flowStreakBuildCount();const double phase=layer.flowStreakSeconds();
                QTest::qWait(150);QVERIFY(layer.flowStreakSeconds()>phase);QJsonArray times;
                for(int run=0;run<3;++run) {
                    std::vector<double> ms;
                    for(int i=0;i<100;++i) {
                        clock.restart();layer.advanceFlowStreaks(.033);
                        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
                        ms.push_back(clock.nsecsElapsed()/1e6);QCoreApplication::processEvents();
                    }
                    std::sort(ms.begin(),ms.end());times.append(QJsonObject{{"medianMs",ms[50]},{"p95Ms",ms[95]},{"maxMs",ms.back()}});
                }
                QCOMPARE(layer.flowStreakBuildCount(),builds);
                QDir().mkpath("tests/verification/flow_streaks");
                QVERIFY(canvas.captureMap(&error).save("tests/verification/flow_streaks/overview-native-6m.png"));
                presentation={{"width",canvas.width()},{"height",canvas.height()},{"dpr",canvas.devicePixelRatioF()},
                    {"coldCaptureMs",captureMs},{"qsgOwnsRendering",layer.qsgOwnsRendering()},{"phaseRebuilds",layer.flowStreakBuildCount()-builds},{"forcedCapture",true},{"runs",times}};
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
        for(int i=0;i<1200;++i) {layer.advanceFlowStreaks(.033);root=renderer.updatePaintNode(root,nullptr);}
        QCOMPARE(flow->uploads(),uploads);QCOMPARE(flow->geometry()->vertexData(),data);
        QCOMPARE(renderer.displayedFrameRevision(),frame);
        const int builds=layer.flowStreakBuildCount();
        const auto batch=paths(layer);
        layer.flowStreakSublayer()->streakStyle()->setHeadWidthPx(10);
        layer.flowStreakSublayer()->streakStyle()->setWidthPx(2);
        layer.flowStreakSublayer()->streakStyle()->setAnimationTimestepMs(8);
        layer.flowStreakSublayer()->streakStyle()->setLifetimeFadeSeconds(.8);
        root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(flow->uploads(),uploads);QCOMPARE(flow->geometry()->vertexData(),data);
        QCOMPARE(paths(layer),batch);QCOMPARE(layer.flowStreakBuildCount(),builds);
        renderer.setProperty("canvasDevicePixelRatio",2.0);renderer.setSize({1600,1600});
        root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(flow->uploads(),uploads); // same logical-pixel density/width at 2x resolution
        layer.flowStreakSublayer()->streakStyle()->setTrailDurationSeconds(4);
        root=renderer.updatePaintNode(root,nullptr);QCOMPARE(flow->uploads(),uploads+1);
        QCOMPARE(paths(layer),batch);QCOMPARE(layer.flowStreakBuildCount(),builds);
        for(int i=0;i<100;++i) {layer.advanceFlowStreaks(.033);root=renderer.updatePaintNode(root,nullptr);}
        QCOMPARE(flow->uploads(),uploads+1); // duration edits do not add tick uploads
        QVERIFY(layer.moveSublayer(layer.sublayers().indexOf(layer.flowStreakSublayer()),0));
        root=renderer.updatePaintNode(root,nullptr);QCOMPARE(root->firstChild(),flow->parent());
        layer.flowStreakSublayer()->setVisible(false);root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(flow->geometry()->vertexCount(),0);delete root;
    }
    void gpuPublishesWaterAndPathsTogether() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();
        source->live=true;layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        TestRenderer renderer;renderer.setSize({800,800});renderer.setLayer(&layer);renderer.setMapExtent({0,0,100,100});
        QSGNode *root=renderer.updatePaintNode(nullptr,nullptr);QVERIFY(root);
        QTRY_VERIFY_WITH_TIMEOUT((root=renderer.updatePaintNode(root,nullptr),findFlow(root)->geometry()->vertexCount()>0),5000);
        const auto old=renderer.displayedFrameRevision();const int uploads=findFlow(root)->uploads();
        raw->speed=3;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>old,5000);
        root=renderer.updatePaintNode(root,nullptr);
        QVERIFY(layer.flowStreakBusy());QCOMPARE(renderer.displayedFrameRevision(),old);
        QCOMPARE(findFlow(root)->uploads(),uploads);QVERIFY(findFlow(root)->geometry()->vertexCount()>0);
        QTRY_VERIFY_WITH_TIMEOUT((root=renderer.updatePaintNode(root,nullptr),renderer.displayedFrameRevision()==layer.frameRevision()),5000);
        QVERIFY(findFlow(root)->geometry()->vertexCount()>0);
        const auto wet=layer.frameRevision();raw->depths.assign(3,0);++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()>wet,5000);
        root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
        QCOMPARE(findFlow(root)->geometry()->vertexCount(),0);delete root;
    }
    void cancelledOverviewDoesNotStallWater() {
        const auto previous=qgetenv("OPENSWMM_FLOW_OVERVIEW");qputenv("OPENSWMM_FLOW_OVERVIEW","1");
        const auto restore=qScopeGuard([&]{if(previous.isNull())qunsetenv("OPENSWMM_FLOW_OVERVIEW");else qputenv("OPENSWMM_FLOW_OVERVIEW",previous);});
        SWMM2DResultsLayer layer;auto source=std::make_unique<GridFlowSource>(200,126);auto *raw=source.get();
        // Publish source changes synchronously so no event-loop turn can
        // accidentally finish the overview job before it is superseded.
        source->live=false;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        layer.contourBandSublayer()->setVisible(true);
        TestRenderer renderer;renderer.setSize({800,504});renderer.setLayer(&layer);renderer.setMapExtent({0,0,200,126});
        std::unique_ptr<QSGNode> root(renderer.updatePaintNode(nullptr,nullptr));
        QTRY_VERIFY_WITH_TIMEOUT((root.reset(renderer.updatePaintNode(root.release(),nullptr)),
                                 findFlow(root.get())->geometry()->vertexCount()>0),10000);
        const auto initial=renderer.displayedFrameRevision();
        raw->speed=3;++raw->revision;layer.refreshCurrentFrame();
        const auto intermediate=layer.frameRevision();QVERIFY(intermediate>initial);
        root.reset(renderer.updatePaintNode(root.release(),nullptr));
        renderer.finishPendingCapture(); // finish only the contour, not the queued overview tracer
        QVERIFY(layer.flowStreakBusy());
        raw->speed=4;++raw->revision;layer.refreshCurrentFrame();
        QVERIFY(layer.frameRevision()>intermediate);
        root.reset(renderer.updatePaintNode(root.release(),nullptr));
        // The obsolete overview cannot complete, but its completed water may
        // still be published while the latest matched presentation prepares.
        QCOMPARE(renderer.displayedFrameRevision(),intermediate);
        QCOMPARE(findFlow(root.get())->geometry()->vertexCount(),0);
        QTRY_VERIFY_WITH_TIMEOUT((root.reset(renderer.updatePaintNode(root.release(),nullptr)),
                                 renderer.displayedFrameRevision()==layer.frameRevision()
                                 && findFlow(root.get())->geometry()->vertexCount()>0),10000);
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
        const int builds=layer.flowStreakBuildCount();
        layer.flowStreakSublayer()->streakStyle()->setHeadWidthPx(10);
        const auto wide=draw();QVERIFY(wide!=first);
        auto alphaSum=[](const QImage &image) {
            qint64 total=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)total+=qAlpha(image.pixel(x,y));
            return total;
        };
        QVERIFY(alphaSum(wide)>alphaSum(first));QCOMPARE(layer.flowStreakBuildCount(),builds);
        layer.flowStreakSublayer()->streakStyle()->setHeadWidthPx(1.5);QCOMPARE(draw(),first);
        layer.flowStreakSublayer()->streakStyle()->setTrailDurationSeconds(4);
        QVERIFY(alphaSum(draw())>alphaSum(first));QCOMPARE(layer.flowStreakBuildCount(),builds);
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
        sub->streakStyle()->setHeadWidthPx(6.2);
        sub->streakStyle()->setAnimationTimestepMs(50);
        sub->streakStyle()->setMinimumLifetimeSeconds(10);
        sub->streakStyle()->setMaximumLifetimeSeconds(25);
        sub->streakStyle()->setLifetimeFadeSeconds(1.2);
        sub->streakStyle()->setTrailDurationSeconds(4.5);
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
    void nativeVelocityDependentTails() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE"))QSKIP("Run with a native window to verify temporal tail lengths");
        QQuickWindow window;window.setColor(Qt::black);window.resize(256,80);
        auto *preview=new HeadWidthPreview(window.contentItem());
        preview->style.setVelocityScaledTrail(true);preview->style.setTrailDurationSeconds(2);
        preview->style.setWidthPx(4);preview->style.setHeadWidthPx(4);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto capture=[&](double speed,bool varying=false) {
            auto &path=preview->batch->paths.front();path.points.clear();
            for(int i=0;i<=200;++i) {
                const double seconds=varying?(i<=50?i/20.:2.5+(i-50)/60.):i/speed;
                path.points.push_back({{20.+i,40},float(seconds),float(varying?(i<=50?20:60):speed)});
            }
            // Publish a new immutable batch so the node uploads changed timestamps.
            preview->batch=std::make_shared<FlowStreakBatch>(*preview->batch);
            preview->phase=varying?4:2.4;preview->update();return window.grabWindow();
        };
        auto bounds=[](const QImage &image) {
            const double dpr=image.width()/256.;int first=image.width(),last=-1;
            for(int x=0;x<image.width();++x)if(qRed(image.pixel(x,int(40.5*dpr)))>127) {first=std::min(first,x);last=x;}
            return qMakePair(first/dpr,(last+1)/dpr);
        };
        const auto slow=capture(30),fast=capture(60);
        const auto a=bounds(slow),b=bounds(fast);
        QVERIFY(std::abs(a.second-a.first-60)<1.1);QVERIFY(std::abs(b.second-b.first-120)<1.1);
        for(double speed:{.5,8.}) {
            preview->style.setSpeedMultiplier(speed);preview->update();QCOMPARE(window.grabWindow(),fast);
        }
        preview->style.setTrailDurationSeconds(1);preview->update();const auto shorter=window.grabWindow();
        const auto c=bounds(shorter);QVERIFY(std::abs(c.second-c.first-60)<1.1);
        preview->style.setTrailDurationSeconds(2);const auto changed=capture(0,true);const auto d=bounds(changed);
        // t=2 is x=60; t=4 is x=160: the two-speed tail spans 100 px.
        QVERIFY(std::abs(d.first-60)<1.1);QVERIFY(std::abs(d.second-160)<1.1);
        const double dpr=changed.width()/256.;
        for(int x=21;x<219;++x) {
            const double sceneX=(std::floor((x+.5)*dpr)+.5)/dpr-20;
            const double t=sceneX<=50?sceneX/20:2.5+(sceneX-50)/60;
            const double expected=flowStreakOpacity(t,2,{4,2,200},false,.5);
            const double pixel=qRed(changed.pixel(int((x+.5)*dpr),int(40.5*dpr)))/255.;
            QVERIFY(std::abs(pixel-expected)<.025);
        }
        QDir().mkpath("tests/verification/flow_streaks");
        QVERIFY(slow.save("tests/verification/flow_streaks/temporal-tail-slow.png"));
        QVERIFY(fast.save("tests/verification/flow_streaks/temporal-tail-fast.png"));
        QVERIFY(changed.save("tests/verification/flow_streaks/temporal-tail-changing-speed.png"));
    }
    void nativeHeadWidth() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE"))QSKIP("Run with a native window to verify shader head widths");
        QQuickWindow window;window.setColor(Qt::black);window.resize(256,80);
        auto *preview=new HeadWidthPreview(window.contentItem());window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto capture=[&] {preview->update();return window.grabWindow();};
        auto thickness=[](const QImage &image,int logicalX) {
            const double dpr=image.width()/256.;const int x=int((logicalX+.5)*dpr);
            int pixels=0;for(int y=0;y<image.height();++y)if(qRed(image.pixel(x,y))>127)++pixels;
            return pixels/dpr;
        };
        const auto thin=capture();QVERIFY(!thin.isNull());
        QVERIFY(std::abs(thickness(thin,137)-2)<1.1);
        preview->style.setHeadWidthPx(12);const auto wide=capture();
        QVERIFY(thickness(wide,137)>10);
        QCOMPARE(thickness(wide,70),thickness(thin,70)); // unchanged trailing body
        preview->phase=150;const auto moved=capture();
        QVERIFY(thickness(moved,167)>10);QVERIFY(thickness(moved,137)<3);
        // The shader and painter share the same profile, including taper and narrow heads.
        for(bool taper:{false,true})for(double head:{.1,2.,12.}) {
            preview->style.setTaper(taper);preview->style.setHeadWidthPx(head);
            const auto image=capture();
            for(int x:{100,140,157,167}) {
                const double fraction=1-(170-(x+.5))/80.;
                QVERIFY(std::abs(thickness(image,x)-preview->style.widthAtFraction(fraction))<1.1);
            }
        }
        QDir().mkpath("tests/verification/flow_streaks");
        QVERIFY(thin.save("tests/verification/flow_streaks/head-width-2px.png"));
        QVERIFY(wide.save("tests/verification/flow_streaks/head-width-12px.png"));
    }
    void nativeSoftFadesAndSubpixelCoverage() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE"))QSKIP("Run with a native window to verify smooth fades and pixel coverage");
        QQuickWindow window;window.setColor(Qt::black);window.resize(256,80);
        auto *preview=new HeadWidthPreview(window.contentItem());window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto capture=[&] {preview->update();return window.grabWindow();};
        auto brightness=[](const QImage &image,int x) {
            const double dpr=image.width()/256.;return qRed(image.pixel(int((x+.5)*dpr),int(40.5*dpr)))/255.;
        };
        preview->style.setWidthPx(4);preview->style.setHeadWidthPx(4);preview->style.setFade(true);
        const auto soft=capture();QVERIFY(!soft.isNull());
        QVERIFY(soft.save("tests/verification/flow_streaks/lifetime-soft.png"));
        for(int x:{70,100,130,137,139}) {
            // Compare at the actual backing pixel centre on every DPR.
            const double dpr=soft.width()/256.;
            const double pointTime=(std::floor((x+.5)*dpr)+.5)/dpr-20;
            const double expected=flowStreakOpacity(pointTime,80,{120,2,200},true,.5);
            QVERIFY2(std::abs(brightness(soft,x)-expected)<.025,qPrintable(QString("x=%1 expected=%2 pixel=%3")
                .arg(x).arg(expected).arg(brightness(soft,x))));
        }
        auto appearing=std::make_shared<FlowStreakBatch>(*preview->batch);
        appearing->paths.front().appearedAt=0;preview->batch=appearing;
        QCOMPARE(brightness(capture(),130),0.);
        preview->clock=.125;const auto half=capture();
        QVERIFY(std::abs(brightness(half,130)-brightness(soft,130)*.5)<.02);
        preview->clock=.25;QCOMPARE(capture(),soft);
        preview->style.setFade(false);
        // Thin tails must not brighten/darken when crossing pixel centres.
        for(double width:{.2,1.5}) {
            preview->style.setWidthPx(width);preview->style.setHeadWidthPx(width);
            for(double y:{0.,.25,.5,.75,1.}) {
                preview->setY(y);const auto image=capture();const double dpr=image.width()/256.;
                double sum=0;const int x=int(100.5*dpr);
                for(int row=0;row<image.height();++row)sum+=qRed(image.pixel(x,row))/255.;
                QVERIFY2(std::abs(sum/dpr-width)<.04,qPrintable(QString("width=%1 y=%2 coverage=%3").arg(width).arg(y).arg(sum/dpr)));
            }
        }
        QDir().mkpath("tests/verification/flow_streaks");
        QVERIFY(soft.save("tests/verification/flow_streaks/soft-streak.png"));
        QVERIFY(half.save("tests/verification/flow_streaks/appearing-streak.png"));
    }
    void nativeStaggeredRecycling() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_NATIVE"))QSKIP("Run with a native window to verify staggered recycling");
        QQuickWindow window;window.setColor(Qt::black);window.resize(256,80);
        auto *preview=new HeadWidthPreview(window.contentItem());
        auto &path=preview->batch->paths.front();path.seed=42;
        path.lifePhase=0;path.lifeHead=0;path.lifeAge=0;path.lifeRemaining=8;
        path.appearedAt=-1;preview->style.setSpeedMultiplier(10);
        preview->style.setWidthPx(4);preview->style.setHeadWidthPx(4);preview->style.setFade(true);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        for(double clock:{0.,.25,4.,7.75,8.,8.25,15.,24.,24.25,1000.}) {
            preview->clock=clock;preview->phase=clock*10;preview->update();const auto image=window.grabWindow();QVERIFY(!image.isNull());
            const double dpr=image.width()/256.;
            QVERIFY(image.save(QString("tests/verification/flow_streaks/lifetime-%1.png").arg(clock)));
            const auto motion=flowStreakMotion(path,80,preview->phase,clock,8,16,10);
            for(int x=21;x<219;x+=3) {
                const double pointTime=(std::floor((x+.5)*dpr)+.5)/dpr-20;
                const double expected=flowStreakOpacity(pointTime,80,motion,true,.5)
                    *flowStreakAppearance(path.appearedAt,preview->clock);
                const double pixel=qRed(image.pixel(int((x+.5)*dpr),int(40.5*dpr)))/255.;
                QVERIFY2(std::abs(pixel-expected)<.025,qPrintable(QString("clock=%1 x=%2 expected=%3 pixel=%4")
                    .arg(clock).arg(x).arg(expected).arg(pixel)));
            }
        }
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
        MapCanvas canvas;
        // Native benchmark windows display results without accepting
        // desktop gestures that would change the measured viewport.
        canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        canvas.resize(1920,1080);canvas.addLayer(&layer,false);canvas.setExtent(overview,false);
        layer.flowStreakSublayer()->setVisible(true);canvas.show();
        QString error;
        const double capturePhase=layer.flowStreakSeconds();
        QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
        QCOMPARE(layer.flowStreakSeconds(),capturePhase);
        QTest::qWait(1500);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),10000);
        auto *animationTimer=canvas.findChild<QTimer*>("flowAnimationTimer");QVERIFY(animationTimer);
        QCOMPARE(animationTimer->interval(),16);
        layer.flowStreakSublayer()->streakStyle()->setAnimationTimestepMs(33);canvas.repaint();
        QCOMPARE(animationTimer->interval(),33);
        layer.flowStreakSublayer()->streakStyle()->setAnimationTimestepMs(1000);canvas.repaint();
        QCOMPARE(animationTimer->interval(),1000);
        layer.flowStreakSublayer()->streakStyle()->setAnimationTimestepMs(5000);canvas.repaint();
        QCOMPARE(animationTimer->interval(),5000);
        const double slowPhase=layer.flowStreakSeconds();QTest::qWait(40);QCOMPARE(layer.flowStreakSeconds(),slowPhase);
        layer.flowStreakSublayer()->streakStyle()->setAnimationTimestepMs(16);canvas.repaint();
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
                QElapsedTimer timer;timer.start();layer.advanceFlowStreaks(.033);
                QVERIFY2(!canvas.captureMap(&error).isNull(),qPrintable(error));
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
