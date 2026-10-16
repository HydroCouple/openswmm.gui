// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/flowparticlenode.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/openswmmvisgraphicsview.h"
#include "map/tools/maptool.h"
#include "render/smoothfillraster.h"
#include "render/flowparticlecontroller.h"
#include <QGraphicsRectItem>
#include "project/projectserializer.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisvectorlayer.h"
#include "project/openswmmvisworkspace.h"
#include "render/maskclipresolver.h"
#include "simulation/simulationrunner.h"
#include "core/preferencesmanager.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include <QTest>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QSGTextureProvider>
#include <QScopeGuard>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QTabWidget>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTextStream>
#include <QPainter>
#include <QMouseEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QSettings>
#include <QtQml/qqml.h>
#include <QQuickWidget>
#include <QScreen>
#include <QQuickWindow>
#include <cmath>
#include <map>
#include <limits>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
using namespace OpenSWMM::Render;

class FlowSource : public IMesh2DSource {
public:
    bool live=false,fluxEnabled=true;
    float speed=2;
    int revision=0,reads=0,generation=0;
    qint64 timeOrigin=1700000000;
    int historyGeneration() const override {return generation;}
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
    QDateTime simTimeAt(int i) const override {return QDateTime::fromSecsSinceEpoch(timeOrigin+i*60,Qt::UTC);}
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
class VelocityOnlyFlowSource : public FlowSource {
public:
    VelocityOnlyFlowSource() {fluxEnabled=false;}
    int subsetReads=0;
    bool hasFaceField(const char* name) const override {
        return QString::fromLatin1(name)=="Mesh2_face_vx" || QString::fromLatin1(name)=="Mesh2_face_vy";
    }
    bool readFaceFieldCellsAt(const char* name,int,const std::vector<int>& ids,std::vector<float>& values) override {
        ++subsetReads;values.assign(ids.size(),QString::fromLatin1(name)=="Mesh2_face_vx"?speed:0);return true;
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
    double wetFraction=1;
    bool regionalMissing=false,regionalMismatch=false;
    virtual int squareIndex(int cell) const {return cell/2;}
    float cellDepth(int time,int cell) const {
        return wetFraction>=1 || squareIndex(cell)%cols+.5<cols*wetFraction?depths.at(size_t(time)):0;
    }
    bool supportsHydraulicCells() const override {return true;}
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int t,const std::vector<int>& ids) override {
        if(!openswmmvis::io::validHydraulicCells(ids,triangleCount())) return {};
        ++regionalReads;if(regionalMissing)return {};
        auto out=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time=regionalMismatch?t+1:t;out->revision=revision;out->generation=generation;out->cells=ids;
        const float signs[8]={1,-1,0,0,0,-1,1,0};
        for(int c:ids) {
            const float d=cellDepth(t,c);out->depths.push_back(d);
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
        ++reads;d.assign(triangleCount(),depths.at(size_t(t)));
        if(wetFraction<1)for(int c=0;c<triangleCount();++c)d[size_t(c)]=cellDepth(t,c);
        return true;
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
            flux[i]=sign[i%8]*cellDepth(t,int(i/4))*speed*(right?2:1);
        }
        return true;
    }
};
class QuadGridFlowSource : public GridFlowSource {
public:
    using GridFlowSource::GridFlowSource;
    int squareIndex(int cell) const override {return cell;}
    int triangleCount() const override {return cols*rows;}
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int t,const std::vector<int>& ids) override {
        if(!openswmmvis::io::validHydraulicCells(ids,triangleCount())) return {};
        ++regionalReads;auto out=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time=t;out->revision=revision;out->generation=generation;out->cells=ids;const float signs[4]={1,0,-1,0};
        for(int c:ids) {
            const float d=cellDepth(t,c);out->depths.push_back(d);
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
        for(size_t i=0;i<flux.size();++i)flux[i]=sign[i%4]*cellDepth(t,int(i/4))*speed;
        return true;
    }
};
class VelocityOnlyGridSource : public GridFlowSource {
public:
    using GridFlowSource::GridFlowSource;
    int subsetReads=0;
    bool readEdgeFluxAt(int,std::vector<float>& values) override {values.clear();return false;}
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int time,const std::vector<int>& ids) override {
        const auto raw=GridFlowSource::readHydraulicCellsAt(time,ids);if(!raw)return {};
        auto out=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>(*raw);out->flux.clear();return out;
    }
    bool hasFaceField(const char* name) const override {
        return QString::fromLatin1(name)=="Mesh2_face_vx" || QString::fromLatin1(name)=="Mesh2_face_vy";
    }
    bool readFaceFieldCellsAt(const char* name,int,const std::vector<int>& ids,std::vector<float>& values) override {
        ++subsetReads;values.assign(ids.size(),QString::fromLatin1(name)=="Mesh2_face_vx"?speed:0);return true;
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
class ParticleHandoffPreview : public QQuickItem {
public:
    std::shared_ptr<const FlowParticleFrame> frame;
    FlowParticleAppearance appearance;
    double interpolation=1,scale=1;
    explicit ParticleHandoffPreview(QQuickItem *parent):QQuickItem(parent){setFlag(ItemHasContents);}
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *root=static_cast<QSGTransformNode *>(old);
        if(!root){root=new QSGTransformNode;root->appendChildNode(new FlowParticleNode(window()));}
        QMatrix4x4 matrix;matrix.translate(160,120);matrix.scale(float(scale),float(scale));root->setMatrix(matrix);
        static_cast<FlowParticleNode *>(root->firstChild())->sync(frame,interpolation,scale,1.5,1.5,2.5,.6,0,appearance);
        return root;
    }
};
static FlowParticleNode *findParticles(QSGNode *n) {
    if(auto *f=dynamic_cast<FlowParticleNode*>(n))return f;
    for(auto *c=n?n->firstChild():nullptr;c;c=c->nextSibling())if(auto *f=findParticles(c))return f;
    return nullptr;
}
static qint64 visibleParticlePixels(MapCanvas &canvas,SWMM2DResultsLayer &layer,const QImage &visible,QString *error) {
    auto *sub=layer.flowStreakSublayer();const double opacity=sub->opacity();
    sub->setOpacity(0);const auto hidden=canvas.captureMap(error).toImage();sub->setOpacity(opacity);
    if(hidden.isNull() || hidden.size()!=visible.size())return -1;
    const auto a=visible.convertToFormat(QImage::Format_ARGB32),b=hidden.convertToFormat(QImage::Format_ARGB32);
    qint64 pixels=0;
    for(int y=0;y<a.height();++y) {
        const auto *p=reinterpret_cast<const QRgb *>(a.constScanLine(y)),*q=reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for(int x=0;x<a.width();++x)pixels+=std::max({std::abs(qRed(p[x])-qRed(q[x])),std::abs(qGreen(p[x])-qGreen(q[x])),
            std::abs(qBlue(p[x])-qBlue(q[x]))})>2;
    }
    return pixels;
}
static void showNativeReview(QWidget &widget) {
    // Window placement otherwise follows the pointer onto either external
    // DPR1 monitor. Keep native measurements on the primary Retina display.
    if(auto *screen=QGuiApplication::primaryScreen())widget.move(screen->availableGeometry().topLeft()+QPoint(16,16));
    widget.show();
}
class FlowStreaksTest:public QObject {
    Q_OBJECT
private slots:
    void legacyStylesMigrateToTheSingleRenderer_data() {
        QTest::addColumn<QJsonObject>("saved");QTest::addColumn<double>("spacing");QTest::addColumn<double>("trail");
        const QJsonObject common{{"color","#bf123456"},{"widthPx",2.7},{"minimumLifetimeSeconds",12.},
            {"maximumLifetimeSeconds",24.},{"animated",false},{"fade",false},{"taper",false},{"speedMultiplier",50.}};
        auto pixel=common;pixel["schemaVersion"]=2;pixel["spacingPx"]=30.;pixel["trailLengthPx"]=400.;
        QTest::newRow("cached-pixel")<<pixel<<28.<<1.5;
        auto duration=common;duration["schemaVersion"]=3;duration["continuousParticles"]=false;
        duration["spacingPx"]=44.;duration["velocityScaledTrail"]=true;duration["trailDurationSeconds"]=2.6;
        QTest::newRow("cached-duration")<<duration<<40.<<3.;
        auto preview=common;preview["schemaVersion"]=3;preview["continuousParticles"]=true;
        preview["spacingPx"]=60.;preview["velocityScaledTrail"]=true;preview["trailDurationSeconds"]=.9;
        QTest::newRow("physical-particle-preview")<<preview<<56.<<.75;
    }
    void legacyStylesMigrateToTheSingleRenderer() {
        QFETCH(QJsonObject,saved);QFETCH(double,spacing);QFETCH(double,trail);
        FlowStreakStyle style;style.fromJson(saved);
        QCOMPARE(style.spacingPx(),spacing);QCOMPARE(style.trailDurationSeconds(),trail);QCOMPARE(style.speedMultiplier(),1.);
        QCOMPARE(style.color(),QColor("#bf123456"));QCOMPARE(style.widthPx(),2.7);QCOMPARE(style.headWidthPx(),2.7);
        QCOMPARE(style.minimumLifetimeSeconds(),12.);QCOMPARE(style.maximumLifetimeSeconds(),24.);QVERIFY(!style.animated());
        QVERIFY(style.motionWasMigrated());QCOMPARE(style.toJson()["schemaVersion"].toInt(),5);
        for(const char *key:{"continuousParticles","trailLengthPx","pathLengthPx","animationTimestepMs","velocityScaledTrail","fade","taper","dryDepthCutoff"}) {
            QVERIFY(!style.toJson().contains(key));QCOMPARE(style.metaObject()->indexOfProperty(key),-1);
        }
        FlowStreakStyle reopened;reopened.fromJson(style.toJson());QCOMPARE(reopened.toJson(),style.toJson());QVERIFY(!reopened.motionWasMigrated());
    }
    void normalizedSavedStylesKeepExactValues() {
        FlowStreakStyle style;auto saved=style.toJson();saved["schemaVersion"]=4;saved["continuousParticles"]=true;
        saved["spacingPx"]=22.;saved["speedMultiplier"]=2.75;saved["trailDurationSeconds"]=4.5;saved["headWidthPx"]=6.2;
        style.fromJson(saved);QCOMPARE(style.spacingPx(),22.);QCOMPARE(style.speedMultiplier(),2.75);
        QCOMPARE(style.trailDurationSeconds(),4.5);QCOMPARE(style.headWidthPx(),6.2);QVERIFY(!style.motionWasMigrated());
    }
    void presetsAndAdvancedSettingsStaySynchronized() {
        SWMM2DResultsLayer layer,other;openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer),second(&layer);
        auto *style=layer.flowStreakSublayer()->streakStyle();auto *advanced=panel.findChild<QWidget*>("flowAdvanced");QVERIFY(advanced);QVERIFY(advanced->isHidden());
        auto *dense=panel.findChild<QPushButton*>("flowDensity2");QVERIFY(dense);dense->click();QCOMPARE(style->spacingPx(),28.);
        QVERIFY(second.findChild<QPushButton*>("flowDensity2")->isChecked());QCOMPARE(other.flowStreakSublayer()->streakStyle()->spacingPx(),40.);
        panel.findChild<QPushButton*>("flowDensity3")->click();QCOMPARE(style->spacingPx(),14.);
        QVERIFY(second.findChild<QPushButton*>("flowDensity3")->isChecked());
        panel.findChild<QDoubleSpinBox*>("spacingPx")->setValue(2);QCOMPARE(style->spacingPx(),2.);
        style->setSpacingPx(22);QVERIFY(!dense->isChecked());QVERIFY(!panel.findChild<QLabel*>("flowDensityCustom")->isHidden());
        auto *motion=panel.findChild<QSlider*>("flowMotion");QVERIFY(motion);motion->setValue(175);QCOMPARE(style->speedMultiplier(),1.75);
        auto *exact=panel.findChild<QDoubleSpinBox*>("motionPixelsPerSecond");QVERIFY(exact);QCOMPARE(exact->value(),84.);
        exact->setValue(96);QCOMPARE(style->speedMultiplier(),2.);QCOMPARE(second.findChild<QSlider*>("flowMotion")->value(),200);
        panel.findChild<QPushButton*>("flowTrail2")->click();QCOMPARE(style->trailDurationSeconds(),3.);
        style->setTrailDurationSeconds(4.5);QCOMPARE(panel.findChild<QDoubleSpinBox*>("trailDurationSeconds")->value(),4.5);
        panel.findChild<QToolButton*>("flowAdvancedToggle")->click();QVERIFY(!advanced->isHidden());
        auto *minimum=panel.findChild<QDoubleSpinBox*>("minimumLifetimeSeconds");auto *maximum=panel.findChild<QDoubleSpinBox*>("maximumLifetimeSeconds");
        minimum->setValue(50);QCOMPARE(maximum->value(),50.);maximum->setValue(10);QCOMPARE(minimum->value(),10.);
        panel.findChild<QCheckBox*>("colorByMagnitude")->setChecked(true);QVERIFY(style->colorByMagnitude());
        QTRY_VERIFY(!panel.findChild<QWidget*>("flowClassification")->isHidden());
        for(const char *name:{"animated","continuousParticles","fade","taper","trailLengthPx","pathLengthPx","animationTimestepMs"})QVERIFY(!panel.findChild<QWidget*>(name));
        FlowStreakStyle reopened;reopened.fromJson(style->toJson());QCOMPARE(reopened.toJson(),style->toJson());
    }
    void higherDensityExceedsTheFormerParticleLimit() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());layer.flowStreakSublayer()->setVisible(true);
        auto *style=layer.flowStreakSublayer()->streakStyle();style->setSpacingPx(2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,16));
        const auto frame=layer.flowParticles(layer.m_sceneBBox,16,layer.frameRevision());QVERIFY(frame);
        QVERIFY2(layer.flowParticleDiagnostics().targetPopulation>250,qPrintable(layer.flowStreakStatus()));
        QVERIFY(frame->particles.size()>250);QVERIFY(frame->particles.size()<=4000);
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths,quint64(0));
        const QRectF zoom(75,-75,25,25);QVERIFY(layer.prepareFlowStreaksForCapture(zoom,64));
        const auto zoomed=layer.flowParticles(zoom,64,layer.frameRevision());QVERIFY(zoomed);int visible=0;
        for(const auto &p:zoomed->particles)visible+=p.stop==FlowParticleStop::None && zoom.contains(p.position);
        QVERIFY2(visible>=layer.flowParticleDiagnostics().targetPopulation-1,qPrintable(QString("visible=%1 total=%2 target=%3 recycled=%4 busy=%5 %6")
            .arg(visible).arg(zoomed->particles.size()).arg(layer.flowParticleDiagnostics().targetPopulation)
            .arg(layer.flowParticleDiagnostics().offscreenReplacements).arg(layer.flowStreakBusy()).arg(layer.flowStreakStatus())));
        QVERIFY(layer.flowParticleDiagnostics().offscreenReplacements>0);
    }
    void advancedEditorsPreserveExtendedSavedValues() {
        SWMM2DResultsLayer layer;openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);auto *style=layer.flowStreakSublayer()->streakStyle();
        for(const auto &setting:{qMakePair("widthPx",24.),qMakePair("headWidthPx",48.),qMakePair("trailDurationSeconds",120.),
            qMakePair("spacingPx",1000.),qMakePair("minimumLifetimeSeconds",600.),qMakePair("maximumLifetimeSeconds",3600.),qMakePair("lifetimeFadeSeconds",30.)}) {
            auto *control=panel.findChild<QDoubleSpinBox*>(setting.first);QVERIFY(control);const auto old=style->property(setting.first);
            control->selectAll();QTest::keyClicks(control,QString::number(setting.second));QCOMPARE(style->property(setting.first),old);
            QTest::keyClick(control,Qt::Key_Return);QCOMPARE(style->property(setting.first).toDouble(),setting.second);
        }
        auto *motion=panel.findChild<QDoubleSpinBox*>("motionPixelsPerSecond");motion->setValue(240000);QCOMPARE(style->speedMultiplier(),5000.);
        openswmmvis::ui::Swmm2DResultsStylePanel second(&layer);QCOMPARE(second.findChild<QDoubleSpinBox*>("motionPixelsPerSecond")->value(),240000.);
        FlowStreakStyle reopened;reopened.fromJson(style->toJson());QCOMPARE(reopened.toJson(),style->toJson());
    }
    void cpuParticlesAnimatePauseAndHide() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setColor(Qt::red);
        auto paint=[&] {QImage image(800,800,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);painter.translate(0,800);painter.scale(8,8);layer.paintFlowStreaks(&painter,layer.m_sceneBBox);return image;};
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));layer.advanceFlowStreaks(.5);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));
        const auto first=paint();layer.advanceFlowStreaks(.5);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));QVERIFY(paint()!=first);
        const auto before=layer.flowParticles(layer.m_sceneBBox,8,layer.frameRevision());style->setAnimated(false);layer.advanceFlowStreaks(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,8));QCOMPARE(layer.flowParticles(layer.m_sceneBBox,8,layer.frameRevision())->clock,before->clock);
        sub->setVisible(false);const auto hidden=paint();QCOMPARE(hidden.pixelColor(400,400),QColor(Qt::white));
        QImage white=hidden;white.fill(Qt::white);QCOMPARE(hidden,white);
    }
    void nativeFiveControlPanel() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Requires a native panel capture");
        SWMM2DResultsLayer layer;layer.flowStreakSublayer()->setVisible(true);
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);panel.resize(600,680);
        auto *page=panel.findChild<QWidget*>("flowStreakTab");QVERIFY(page);
        for(auto *tabs:panel.findChildren<QTabWidget*>())for(int i=0;i<tabs->count();++i)
            if(tabs->widget(i)==page || tabs->widget(i)->isAncestorOf(page))tabs->setCurrentIndex(i);
        panel.setWindowFlags(panel.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));QTest::qWait(100);
        QVERIFY(panel.findChild<QWidget*>("flowAdvanced")->isHidden());
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/native-panel");QDir().mkpath(folder);
        QVERIFY(panel.grab().save(folder+"/controls.png"));
        panel.findChild<QToolButton*>("flowAdvancedToggle")->click();QTest::qWait(100);
        QVERIFY(panel.grab().save(folder+"/advanced.png"));
    }
    void particleDefaultsAndOldMotionMigration() {
        FlowStreakStyle style;
        QCOMPARE(style.spacingPx(),40.);QCOMPARE(style.speedMultiplier(),1.);
        QCOMPARE(style.minimumLifetimeSeconds(),20.);QCOMPARE(style.maximumLifetimeSeconds(),40.);
        QCOMPARE(style.trailDurationSeconds(),1.5);QCOMPARE(style.headWidthPx(),2.5);
        auto old=style.toJson();old["schemaVersion"]=3;old["speedMultiplier"]=50.;
        old["color"]="#ff123456";style.fromJson(old);
        QCOMPARE(style.speedMultiplier(),1.);QCOMPARE(style.color(),QColor("#ff123456"));
        style.setSpeedMultiplier(2.5);FlowStreakStyle reopened;reopened.fromJson(style.toJson());
        QCOMPARE(reopened.toJson(),style.toJson());
    }
    void defaultParticlesNormalizeNativeMotionAndUseVelocityOnlyOutputs() {
        for(double unit:{1.,.3048})for(double scale:{.5,2.,8.})for(bool fallback:{false,true}) {
            SWMM2DResultsLayer layer;layer.setFallbackCoordinateScale(unit);
            if(fallback)layer.setSource(std::make_unique<VelocityOnlyFlowSource>());
            else layer.setSource(std::make_unique<FlowSource>());
            layer.flowStreakSublayer()->setVisible(true);
            QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,scale));
            for(int i=0;i<120;++i) {layer.advanceFlowStreaks(1./60);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,scale));}
            const auto frame=layer.flowParticles(layer.m_sceneBBox,scale,layer.frameRevision());QVERIFY(frame);
            const auto diagnostics=layer.flowParticleDiagnostics();QCOMPARE(diagnostics.failedBirths,quint64(0));
            QCOMPARE(diagnostics.referenceSpeedSI,2.);
            QVERIFY(std::abs(diagnostics.motionGain*(2/unit)*scale-48)<1e-5);
            int moving=0;
            for(const auto &p:frame->particles)if(p.stop==FlowParticleStop::None && p.history.size()>1) {
                const auto &a=p.history.front(),&b=p.history.back();
                const double pixels=(b.position.x()-a.position.x())*scale/(b.age-a.age);
                QVERIFY(std::abs(pixels-48)<1e-4);++moving;
            }
            QVERIFY(moving>0);
            if(fallback)QVERIFY(static_cast<VelocityOnlyFlowSource*>(layer.source())->subsetReads>0);
        }
    }
    void regionalVelocityFallbackIgnoresPlaceholderFluxBuffers() {
        const auto previous=qgetenv("OPENSWMM_REGIONAL_SURFACE");qputenv("OPENSWMM_REGIONAL_SURFACE","1");
        const auto restore=qScopeGuard([previous] {
            if(previous.isNull())qunsetenv("OPENSWMM_REGIONAL_SURFACE");else qputenv("OPENSWMM_REGIONAL_SURFACE",previous);
        });
        SWMM2DResultsLayer layer;auto source=std::make_unique<VelocityOnlyGridSource>(100,100);auto *raw=source.get();
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        const QRectF view(50,-50,10,10);QVERIFY(layer.prepareFlowStreaksForCapture(view,32));
        QVERIFY(layer.regionalHydraulicsPublished());
        const auto input=layer.captureFlowParticleInput(view,32);QVERIFY(input);
        for(const auto &t:input->triangles) {QVERIFY(t.available);QVERIFY(t.velocityOnly);}
        const int fullReads=raw->reads;
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(view,32));
        const auto frame=layer.flowParticles(view,32,layer.frameRevision());QVERIFY(frame && !frame->particles.empty());
        QCOMPARE(raw->reads,fullReads);QVERIFY(raw->subsetReads>0);
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths,quint64(0));
        for(const auto &p:frame->particles) {QVERIFY(p.history.size()>1);QVERIFY(!p.waitingForData);}
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
        auto *style=sub->streakStyle();style->setMinimumLifetimeSeconds(20);
        style->setMaximumLifetimeSeconds(40);style->setTrailDurationSeconds(1);style->setSpeedMultiplier(.1);
        const auto get=[&]{return layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());};
        QTRY_VERIFY_WITH_TIMEOUT(get()!=nullptr,5000);QVERIFY(!get()->particles.empty());
        const auto selected=std::find_if(get()->particles.begin(),get()->particles.end(),[](const auto &p){return p.stop==FlowParticleStop::None && p.lifetime-p.age>1;});
        QVERIFY(selected!=get()->particles.end());const auto id=selected->id;const double lifetime=selected->lifetime;
        const int reads=raw->reads;
        layer.advanceFlowStreaks(.2);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        QCOMPARE(raw->reads,reads);const auto old=get();
        const auto before=std::find_if(old->particles.begin(),old->particles.end(),[&](const auto &p){return p.id==id;});QVERIFY(before!=old->particles.end());
        for(int i=0;i<10;++i) {raw->speed=2+.01f*i;++raw->revision;layer.refreshCurrentFrame();get();}
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=old->revision,5000);
        QTRY_VERIFY_WITH_TIMEOUT(get()!=nullptr && !layer.flowStreakBusy(),5000);
        layer.advanceFlowStreaks(.2);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        bool found=false;
        for(const auto &p:get()->particles)if(p.id==id) {found=true;QCOMPARE(p.lifetime,lifetime);QVERIFY(p.age>=before->age);}
        QVERIFY(found);
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
        style->setColor(QColor(20,40,80,128));style->setSpeedMultiplier(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));layer.advanceFlowStreaks(.5);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        TestRenderer renderer;renderer.setSize({800,800});
        renderer.setProperty("canvasDevicePixelRatio",1);renderer.setLayer(&layer);
        renderer.setMapExtent({0,0,100,100});QSGNode *root=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((root=renderer.updatePaintNode(root,nullptr),
            findParticles(root)->geometry()->vertexCount()>0),5000);
        auto *node=findParticles(root);
        const auto *v=static_cast<const FlowParticleVertex*>(node->geometry()->vertexData());
        QCOMPARE(v->r,uchar(20));QCOMPARE(v->a,uchar(128));
        const int uploads=node->uploads();const auto frame=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());
        style->setHeadWidthPx(6);root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(node->uploads(),uploads+1);QCOMPARE(layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision())->births,frame->births);
        QVERIFY(layer.moveSublayer(layer.sublayers().indexOf(sub),0));root=renderer.updatePaintNode(root,nullptr);
        QCOMPARE(root->firstChild(),node->parent());
        sub->setVisible(false);root=renderer.updatePaintNode(root,nullptr);QCOMPARE(node->geometry()->vertexCount(),0);
        delete root;
    }
    void preparedFieldKeepsDisplayedWaterMovingWithoutCatchUpJump() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();source->live=true;
        layer.setSource(std::move(source));layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(.1);

        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));layer.advanceFlowStreaks(.2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto old=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(!old->particles.empty());
        const auto selected=std::find_if(old->particles.begin(),old->particles.end(),[](const auto &p){return p.stop==FlowParticleStop::None && p.lifetime-p.age>1 && p.position.x()<95;});
        QVERIFY(selected!=old->particles.end());const auto id=selected->id;const auto position=selected->position;
        raw->speed=10;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=old->revision,5000); // live refresh is queued/coalesced
        const auto prepared=[&]{return layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision(),false);};
        QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),5000);QVERIFY(!prepared());
        layer.advanceFlowStreaks(.5);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),5000);
        const auto moving=layer.flowParticles(layer.m_sceneBBox,1,old->revision);QVERIFY(moving);
        QVERIFY(std::abs(moving->clock-old->clock-.5)<1e-8);
        for(const auto &p:moving->particles)if(p.id==id)QVERIFY(p.position.x()>position.x());
        layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());
        QTRY_VERIFY_WITH_TIMEOUT(prepared()!=nullptr && !layer.flowStreakBusy(),5000);
        QCOMPARE(prepared()->clock,moving->clock);
        layer.advanceFlowStreaks(.1);QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(),5000);
        QVERIFY(std::abs(prepared()->clock-old->clock-.6)<1e-8);
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
    void sceneSlicesPreserveChildrenAndDoNotInvalidate() {
        OpenSWMMVisScene scene;
        auto *lower=scene.addRect(QRectF(0,0,100,100),Qt::NoPen,Qt::blue);lower->setZValue(1000);
        auto *upper=scene.addRect(QRectF(0,0,30,30),Qt::NoPen,Qt::red);upper->setZValue(2000);
        upper->setPos(20,20);upper->setOpacity(.5);
        auto *child=new QGraphicsRectItem(QRectF(5,5,10,10),upper);
        child->setPen(Qt::NoPen);child->setBrush(Qt::green);child->setZValue(-10000);
        QCoreApplication::processEvents();QSignalSpy changed(&scene,&QGraphicsScene::changed);
        const auto slice=[&](double low,double high) {
            QImage image(100,100,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);scene.renderSlice(&painter,QRectF(0,0,100,100),QRectF(0,0,100,100),low,high);
            return image;
        };
        const auto background=slice(1000,2000),foreground=slice(2000,3000);
        QCOMPARE(background.pixelColor(27,27),QColor(Qt::blue));
        QCOMPARE(foreground.pixelColor(5,5).alpha(),0);
        QVERIFY(std::abs(foreground.pixelColor(40,40).alpha()-127)<=1);
        QVERIFY(foreground.pixelColor(27,27).green()>100);
        QCoreApplication::processEvents();QCOMPARE(changed.count(),0);
        QVERIFY(lower->isVisible());QVERIFY(upper->isVisible());QVERIFY(child->isVisible());
        const auto all=slice(-INFINITY,INFINITY);QVERIFY(all.pixelColor(40,40).red()>100);
    }
    void visibleCompositionCachesAndFallsBack() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Requires native GPU composition");
        OpenSWMMVisWorkspace workspace(static_cast<QObject *>(nullptr));
        SWMM2DResultsLayer layer("composition results",&workspace);layer.setSource(std::make_unique<FlowSource>());
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();
        style->setColor(Qt::transparent);style->setSpacingPx(25);
        style->setMinimumLifetimeSeconds(20);style->setMaximumLifetimeSeconds(40);
        style->setSpeedMultiplier(1);
        CompositionBackdrop raster(true),vector;
        ParticleReviewCanvas canvas;canvas.resize(480,480);canvas.setShowScaleBar(false);
        canvas.addLayer(&raster,false);canvas.addLayer(&vector,false);canvas.addLayer(&layer,false);
        canvas.reorderLayers({&raster,&vector,&layer},false);
        canvas.setExtent({-10,-10,110,110},false);
        CompositionTool tool(&canvas);canvas.setActiveTool(&tool);showNativeReview(canvas);
        QJsonArray steadyReports;
        const auto checkSteady=[&](const QString &name) {
            QTest::qWait(500);
            const auto before=canvas.compositionStats();
            const int seconds=std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_COMPOSITION_SECONDS"),1,60);
            QTest::qWait(seconds*1000);const auto after=canvas.compositionStats();
            QVERIFY(after.active);QVERIFY(after.renderedFrames>before.renderedFrames+10);
            QCOMPARE(after.fallbackReadbacks,before.fallbackReadbacks);
            QCOMPARE(after.captureReadbacks,before.captureReadbacks);
            QCOMPARE(after.textureUploads,before.textureUploads);
            steadyReports.append(QJsonObject{{"case",name},{"seconds",seconds},
                {"frames",qint64(after.renderedFrames-before.renderedFrames)},
                {"fallbackReadbacks",qint64(after.fallbackReadbacks-before.fallbackReadbacks)},
                {"captureReadbacks",qint64(after.captureReadbacks-before.captureReadbacks)},
                {"textureUploads",qint64(after.textureUploads-before.textureUploads)},
                {"devicePixelRatio",canvas.devicePixelRatioF()}});
        };
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        QTest::qWait(1500);
        auto initial=canvas.compositionStats();QVERIFY(initial.renderedFrames>10);
        auto *surface=canvas.findChild<QWidget*>("flowMapComposition");QVERIFY(surface);
        QVERIFY(surface->testAttribute(Qt::WA_TransparentForMouseEvents));QCOMPARE(surface->focusPolicy(),Qt::NoFocus);
        QTest::mouseClick(canvas.windowHandle(),Qt::LeftButton,Qt::NoModifier,QPoint(100,100));
        QCOMPARE(tool.presses,1);
        if(canvas.compositionStats().nativeSurface) {
            QWindow *native=nullptr;
            for(auto *window:QGuiApplication::allWindows())if(window->objectName()=="flowMapNativeWindow")native=window;
            QVERIFY(native);QVERIFY(!native->flags().testFlag(Qt::WindowTransparentForInput));
            QVERIFY(!native->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
            QMouseEvent click(QEvent::MouseButtonPress,QPointF(100,100),QPointF(canvas.mapToGlobal(QPoint(100,100))),
                              Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(native,&click);QCOMPARE(tool.presses,2);
            const auto original=canvas.extent();
            QPinchGesture pinch;pinch.setScaleFactor(1.25);
            pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
            pinch.setCenterPoint(canvas.mapToGlobal(QPoint(240,240)));
            QGestureEvent gesture({&pinch});
            QCoreApplication::sendEvent(native,&gesture);
            QVERIFY(qFuzzyCompare(canvas.extent().width(),original.width()/1.25));
            canvas.setExtent(original,false);
        }
        QTest::qWait(500);initial=canvas.compositionStats();
        QTest::qWait(1000);auto ordinary=canvas.compositionStats();
        QVERIFY(ordinary.renderedFrames>initial.renderedFrames+10);
        QCOMPARE(ordinary.captureReadbacks,initial.captureReadbacks);
        QCOMPARE(ordinary.fallbackReadbacks,initial.fallbackReadbacks);
        QCOMPARE(ordinary.textureUploads,initial.textureUploads);
        const double scale=canvas.width()/canvas.extent().width();
        const auto extent=canvas.extent();
        const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        {
            style->setAnimated(false);
            auto *timer=canvas.findChild<QTimer*>("flowAnimationTimer");QVERIFY(timer);
            QTRY_VERIFY(!timer->isActive());QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
            const auto paused=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(paused);
            QTest::qWait(150);const auto settled=canvas.compositionStats();QTest::qWait(200);
            const auto still=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(still);
            QCOMPARE(still->clock,paused->clock);
            QVERIFY(canvas.compositionStats().renderedFrames<=settled.renderedFrames+1);
            style->setAnimated(true);QTRY_VERIFY(timer->isActive());
        }
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
        qInfo()<<"Composition capture"<<image.size()<<image.devicePixelRatio()<<"blend"<<blend;
        QVERIFY2(std::abs(blend.red()-110)<=2,qPrintable(blend.name(QColor::HexArgb)));QVERIFY(std::abs(blend.green()-80)<=2);QVERIFY(std::abs(blend.blue()-50)<=2);
        QCOMPARE(blend.alpha(),255);
        auto mask=layer.maskSpec();mask.enabled=true;layer.setMaskSpec(mask);
        QTRY_VERIFY(canvas.compositionStats().active);QVERIFY(layer.qsgOwnsRendering());
        const auto masked=canvas.captureMap(&error);QVERIFY2(!masked.isNull(),qPrintable(error));
        QCOMPARE(pixel(masked.toImage(),240,240),blend);
        mask.enabled=false;layer.setMaskSpec(mask);QTRY_VERIFY(canvas.compositionStats().active);
        auto *fill=layer.cellDepthFillSublayer();fill->setVisible(true);
        fill->fillStyle()->setColorRampName("");
        fill->fillStyle()->setLowColor(QColor(0,0,200));fill->fillStyle()->setHighColor(QColor(0,0,200));
        fill->fillStyle()->setClassified(false);
        SWMM2DResultsLayer second;second.setSource(std::make_unique<FlowSource>());
        second.flowStreakSublayer()->setVisible(true);
        second.flowStreakSublayer()->streakStyle()->setColor(Qt::transparent);
        for(auto *s:second.sublayers())if(s!=second.flowStreakSublayer())s->setVisible(false);
        auto *secondFill=second.cellDepthFillSublayer();secondFill->setVisible(true);
        secondFill->fillStyle()->setColorRampName("");secondFill->fillStyle()->setClassified(false);
        secondFill->fillStyle()->setLowColor(QColor(200,0,0));secondFill->fillStyle()->setHighColor(QColor(200,0,0));
        second.setOpacity(.25);second.setCurrentTimeIndex(1);
        canvas.addLayer(&second,false);QTRY_VERIFY(canvas.compositionStats().active);
        canvas.reorderLayers({&raster,&vector,&layer,&second},false);
        const auto multiple=canvas.captureMap(&error);QVERIFY2(!multiple.isNull(),qPrintable(error));
        const auto combined=pixel(multiple.toImage(),100,240);
        QVERIFY(std::abs(combined.red()-50)<=2);QVERIFY(combined.green()<=2);QVERIFY(std::abs(combined.blue()-150)<=2);
        QCOMPARE(fill->fillStyle()->lowColor(),QColor(0,0,200));
        checkSteady("two-output");
        canvas.takeLayer(canvas.layers().indexOf(&second),false);QTRY_VERIFY(canvas.compositionStats().active);
        // CPU bands retain their stack position above the native output.
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/composition");
        QVERIFY(QDir().mkpath(folder));
        QFile polygon(folder+"/mask.geojson");QVERIFY(polygon.open(QIODevice::WriteOnly));
        polygon.write(R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[0,0],[50,0],[50,100],[0,100],[0,0]],[[20,40],[30,40],[30,60],[20,60],[20,40]]]}}]})");
        polygon.close();GISVectorLayer boundary(polygon.fileName(),{},&workspace);
        QCOMPARE(boundary.featureCount(),1);
        mask.enabled=true;mask.sourceLayerId=boundary.layerId();mask.mode=MaskMode::ClipInside;
        QVERIFY(resolveMaskClip(&layer,mask).ok);layer.setMaskSpec(mask);QTest::qWait(200);
        QVERIFY(canvas.compositionStats().active);
        const auto inside=canvas.captureMap(&error).toImage();QVERIFY2(!inside.isNull(),qPrintable(error));
        QCOMPARE(pixel(inside,100,240),QColor(0,0,200)); // x=15, inside exterior
        QCOMPARE(pixel(inside,380,240),QColor(20,60,100)); // x=85, outside exterior
        QCOMPARE(pixel(inside,140,240),blend); // x=25, in the polygon's hole
        QVERIFY(inside.save(folder+"/mask-inside.png"));checkSteady("masked");
        layer.setOpacity(.4);QTest::qWait(100);
        const auto maskedOpacity=canvas.captureMap(&error).toImage();QVERIFY2(!maskedOpacity.isNull(),qPrintable(error));
        const auto maskedBlue=pixel(maskedOpacity,100,240);
        QVERIFY(std::abs(maskedBlue.red()-12)<=2);QVERIFY(std::abs(maskedBlue.green()-36)<=2);QVERIFY(std::abs(maskedBlue.blue()-140)<=2);
        layer.setOpacity(1);
        boundary.setFilterExpression("1=0");
        QVERIFY(!resolveMaskClip(&layer,mask).ok);
        canvas.invalidate(MapCanvas::Scene|MapCanvas::Overlay,"mask-source-filter");QTest::qWait(100);
        const auto unmasked=canvas.captureMap(&error).toImage();QVERIFY2(!unmasked.isNull(),qPrintable(error));
        QCOMPARE(pixel(unmasked,380,240),QColor(0,0,200));
        boundary.setFilterExpression("");QVERIFY(resolveMaskClip(&layer,mask).ok);
        mask.mode=MaskMode::ClipOutside;layer.setMaskSpec(mask);QTest::qWait(200);
        const auto outside=canvas.captureMap(&error).toImage();QVERIFY2(!outside.isNull(),qPrintable(error));
        QCOMPARE(pixel(outside,100,240),QColor(20,60,100));
        QCOMPARE(pixel(outside,380,240),QColor(0,0,200));
        QCOMPARE(pixel(outside,140,240),QColor(0,0,200));
        mask.enabled=false;layer.setMaskSpec(mask);
        canvas.reorderLayers({&raster,&layer,&vector},false);QTRY_VERIFY(canvas.compositionStats().active);
        const auto ordered=canvas.captureMap(&error).toImage();QVERIFY2(!ordered.isNull(),qPrintable(error));
        const QColor over=pixel(ordered,240,200);
        QVERIFY(std::abs(over.red()-100)<=2);QVERIFY(std::abs(over.green()-50)<=2);QVERIFY(std::abs(over.blue()-100)<=2);
        QVERIFY(ordered.save(folder+"/cpu-above.png"));checkSteady("cpu-above");
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
        QFile report(folder+"/composition.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(steadyReports).toJson());
    }
    void nativeStaticWaterCacheIgnoresUnchangedSync() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Native cached-water synchronization");
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        for(auto* sub:layer.sublayers())sub->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        ParticleReviewCanvas canvas;canvas.resize(640,480);canvas.setShowScaleBar(false);
        canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,110,110},false);
        showNativeReview(canvas);QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        QQuickWindow* mapWindow=nullptr;
        for(auto* window:QGuiApplication::allWindows())if(window->objectName()=="flowMapNativeWindow")
            mapWindow=qobject_cast<QQuickWindow*>(window);
        QVERIFY(mapWindow);QQuickItem* water=nullptr;
        for(auto* item:mapWindow->contentItem()->findChildren<QQuickItem*>())
            if(item->property("cacheStaticContent").toBool())water=item->property("staticItem").value<QQuickItem*>();
        QVERIFY(water);QTest::qWait(500);auto* provider=water->textureProvider();QVERIFY(provider);
        const auto renderers=water->findChildren<SWMM2DResultsQSGRenderer*>();QCOMPARE(renderers.size(),1);
        QSignalSpy changed(provider,&QSGTextureProvider::textureChanged);
        const auto before=canvas.compositionStats().renderedFrames;
        const auto extent=canvas.extent();const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        const double scale=canvas.width()/extent.width();
        // Starting/stopping particles must preserve the already displayed
        // water too, including the first activation after a static frame.
        layer.flowStreakSublayer()->setVisible(true);QTest::qWait(500);
        QCOMPARE(changed.count(),0);
        layer.flowStreakSublayer()->setVisible(false);QTest::qWait(100);
        layer.flowStreakSublayer()->setVisible(true);QTest::qWait(100);
        QCOMPARE(changed.count(),0);
        auto initialFrame=layer.flowParticles(view,scale,layer.frameRevision(),false);QVERIFY(initialFrame);
        const double clock=initialFrame->clock;initialFrame.reset();
        QTimer request;request.setInterval(15);connect(&request,&QTimer::timeout,renderers.front(),&QQuickItem::update);
        request.start();QTest::qWait(500);request.stop();
        const auto current=layer.flowParticles(view,scale,layer.frameRevision(),false);QVERIFY(current);
        QVERIFY(current->clock>clock);
        QVERIFY(canvas.compositionStats().renderedFrames>before+5);
        QCOMPARE(changed.count(),0);
        const auto uploads=canvas.compositionStats().textureUploads;
        // With no raster/CPU layers or foreground there is nothing to upload:
        // the native clear color already supplies the complete background.
        QCOMPARE(uploads,quint64(0));
        auto* style=layer.flowStreakSublayer()->streakStyle();
        for(double spacing:{28.,14.,2.}) {
            style->setSpacingPx(spacing);QTest::qWait(100);
        }
        style->setColor(Qt::red);layer.flowStreakSublayer()->setOpacity(.7);QTest::qWait(100);
        QCOMPARE(changed.count(),0);
        QCOMPARE(canvas.compositionStats().textureUploads,uploads);
        style->setAnimated(false);
        auto* timer=canvas.findChild<QTimer*>("flowAnimationTimer");QVERIFY(timer);
        QTRY_VERIFY(!timer->isActive());QTest::qWait(100);QCOMPARE(changed.count(),0);
        style->setAnimated(true);QTRY_VERIFY(timer->isActive());
        canvas.pan(5,3);QTest::qWait(150);
        QCOMPARE(canvas.compositionStats().textureUploads,quint64(0));
        const QColor background(12,34,56);canvas.setBackgroundColor(background);
        QString error;const auto capture=canvas.captureMap(&error).toImage();
        QVERIFY2(!capture.isNull(),qPrintable(error));
        QCOMPARE(capture.pixelColor(10,10),background);
        QCOMPARE(canvas.compositionStats().textureUploads,quint64(0));
        // Real hydraulic styling still refreshes the water texture.
        layer.smoothDepthFillSublayer()->setOpacity(.4);
        QTRY_VERIFY(changed.count()>0);
        canvas.takeLayer(0,false);
    }
    void nativeDepthUpdatesNeverPublishWaterWithoutMatchingStreaks_data() {
        QTest::addColumn<bool>("live");QTest::addColumn<bool>("bands");
        for(bool live:{false,true})for(bool bands:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(live?"live":"saved",bands?"bands":"cell")))<<live<<bands;
    }
    void nativeDepthUpdatesNeverPublishWaterWithoutMatchingStreaks() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Native depth-frame and particle handoff");
        QFETCH(bool,live);QFETCH(bool,bands);
        auto source=std::make_unique<FlowSource>();auto *raw=source.get();raw->live=live;
        raw->depths={.25f,.55f,.35f,.65f,.3f,.6f,.4f,.7f};
        SWMM2DResultsLayer layer;layer.setAsyncSurface(true);layer.setSource(std::move(source));
        for(auto *sub:layer.sublayers())sub->setVisible(false);
        if(bands)layer.contourBandSublayer()->setVisible(true);else layer.cellDepthFillSublayer()->setVisible(true);
        layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(.02);
        ParticleReviewCanvas canvas;canvas.resize(640,480);canvas.setShowScaleBar(false);
        canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,110,110},false);
        showNativeReview(canvas);QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        QQuickWindow *window=nullptr;
        for(auto *candidate:QGuiApplication::allWindows())if(candidate->objectName()=="flowMapNativeWindow")
            window=qobject_cast<QQuickWindow*>(candidate);
        QVERIFY(window);
        const auto renderers=window->contentItem()->findChildren<SWMM2DResultsQSGRenderer*>();QCOMPARE(renderers.size(),1);
        auto *renderer=renderers.front();auto *controller=layer.findChild<FlowParticleController*>();QVERIFY(controller);
        const auto items=window->contentItem()->findChildren<QQuickItem*>("flowParticleItem");QCOMPARE(items.size(),1);
        auto *particles=items.front();
        QTRY_VERIFY_WITH_TIMEOUT(controller->frame(renderer->displayedFrameRevision())!=nullptr,10000);
        QTRY_COMPARE_WITH_TIMEOUT(particles->property("displayedFrameRevision").toULongLong(),renderer->displayedFrameRevision(),10000);
        std::atomic_int samples=0,missing=0,mismatched=0;
        // Qt keeps the GUI thread locked during afterSynchronizing. Inspect
        // the actual scene-graph handoff, before another worker completion
        // can conceal a blank frame. Capture settlement is deliberately absent.
        const auto monitor=connect(window,&QQuickWindow::afterSynchronizing,window,[&] {
            ++samples;
            if(!controller->frame(renderer->displayedFrameRevision()))++missing;
            if(particles->property("displayedFrameRevision").toULongLong()!=renderer->displayedFrameRevision())++mismatched;
        },Qt::DirectConnection);
        const auto disconnectMonitor=qScopeGuard([&]{disconnect(monitor);});
        for(int i=1;i<8;++i) {
            const auto previous=layer.frameRevision();
            if(live) {raw->depths[0]=raw->depths[size_t(i)];++raw->revision;layer.refreshCurrentFrame();}
            else layer.setCurrentTimeIndex(i);
            QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=previous,5000);
            QTRY_COMPARE_WITH_TIMEOUT(renderer->displayedFrameRevision(),layer.frameRevision(),5000);
            QTest::qWait(50);
        }
        QVERIFY(samples.load()>7);
        QCOMPARE(missing.load(),0);
        QCOMPARE(mismatched.load(),0);
        disconnect(monitor);
        canvas.takeLayer(0,false);
    }
    void nativeIndexedWaterMatchesCpu_data() {
        QTest::addColumn<QString>("waterStyle");
        QTest::addColumn<double>("opacity");
        for(const char* style:{"smooth","cell","flat-bands","smooth-bands"})for(double opacity:{1.,.6})
            QTest::newRow(qPrintable(QString::fromLatin1(style)+(opacity==1?"":"-alpha")))
                <<QString::fromLatin1(style)<<opacity;
    }
    void nativeIndexedWaterMatchesCpu() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Native indexed-water pixel comparison");
        QFETCH(QString,waterStyle);
        QFETCH(double,opacity);
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/indexed-water")+"/"+waterStyle+(opacity==1?"":"-alpha");
        QVERIFY(QDir().mkpath(folder));
        // Cross several uint16 chunks. A zero discharge leaves an exactly
        // horizontal surface, so every interior pixel must have the same color.
        auto source=std::make_unique<GridFlowSource>(250,200);source->speed=0;
        source->depths={.26f,.51f,.26f};
        SWMM2DResultsLayer layer;layer.setSource(std::move(source));
        for(auto* sub:layer.sublayers())sub->setVisible(false);
        if(waterStyle.endsWith("bands")) {
            auto* fill=layer.contourBandSublayer();fill->setVisible(true);fill->setOpacity(opacity);
            auto* style=fill->bandStyle();style->setSmoothBands(waterStyle=="smooth-bands");
            style->setUseCustomRange(true);style->setRangeMin(0);style->setRangeMax(1);style->setBandCount(7);
            style->setColorRampName(QString());style->setLowColor(Qt::blue);style->setHighColor(Qt::red);
        } else {
            auto* fill=waterStyle=="smooth"?static_cast<ISublayer*>(layer.smoothDepthFillSublayer())
                :static_cast<ISublayer*>(layer.cellDepthFillSublayer());
            fill->setVisible(true);fill->setOpacity(opacity);
            auto* style=waterStyle=="smooth"?layer.smoothDepthFillSublayer()->fillStyle():layer.cellDepthFillSublayer()->fillStyle();
            style->setUseCustomRange(true);
            style->setRangeMin(0);style->setRangeMax(1);style->setClassified(false);
            style->setColorRampName(QString());style->setLowColor(Qt::blue);style->setHighColor(Qt::red);
        }
        ParticleReviewCanvas canvas;canvas.resize(640,480);canvas.setShowScaleBar(false);
        canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,260,210},false);
        showNativeReview(canvas);QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        const auto extent=canvas.extent();QVector<QImage> native;
        for(int frame:{0,1}) {
            layer.setCurrentTimeIndex(frame);QTest::qWait(150);
            QString error;native.push_back(canvas.captureMap(&error).toImage());
            QVERIFY2(!native.back().isNull(),qPrintable(error));
            QVERIFY(native.back().save(folder+QString("/water-native-%1.png").arg(frame)));
        }
        canvas.takeLayer(0,false);QVERIFY(!layer.qsgOwnsRendering());
        QGraphicsScene scene;layer.populateScene(&scene,extent,nullptr);
        const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        QJsonArray reports;
        for(int frame:{0,1}) {
            layer.setCurrentTimeIndex(frame);
            QImage cpu(native[frame].size(),QImage::Format_ARGB32_Premultiplied);cpu.fill(Qt::white);
            QPainter painter(&cpu);painter.setRenderHint(QPainter::Antialiasing);
            scene.render(&painter,QRectF(cpu.rect()),view,Qt::IgnoreAspectRatio);painter.end();
            QVERIFY(cpu.save(folder+QString("/water-cpu-%1.png").arg(frame)));
            int samples=0,different=0,maximum=0;
            for(int y=1;y<200;++y)for(int x=1;x<250;++x) {
                // Cell vertices lie on the CPU painter's separately
                // antialiased polygon edges. Compare triangle interiors.
                const int px=qFloor((x+.75-extent.xMin())/extent.width()*cpu.width());
                const int py=qFloor((extent.yMax()-y-.25)/extent.height()*cpu.height());
                const auto a=native[frame].pixelColor(px,py),b=cpu.pixelColor(px,py);
                // Also reject two identically empty captures.
                QVERIFY(a.red()+a.blue()>200);QVERIFY(std::abs(a.green()-qRound((1-opacity)*255))<=2);QCOMPARE(a.alpha(),255);
                const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
                ++samples;different+=delta>3;maximum=std::max(maximum,delta);
            }
            reports.append(QJsonObject{{"frame",frame},{"samples",samples},{"different",different},{"maximumDelta",maximum}});
            qInfo()<<"Indexed water comparison"<<frame<<samples<<different<<maximum;
            QCOMPARE(different,0);
            // Independently require seamless GPU coverage, including shared
            // edges where the CPU's polygon antialiasing leaves pale seams.
            const QColor expected=native[frame].pixelColor(cpu.width()/2,cpu.height()/2);
            const int left=qCeil((1-extent.xMin())/extent.width()*cpu.width());
            const int right=qFloor((249-extent.xMin())/extent.width()*cpu.width());
            const int top=qCeil((extent.yMax()-199)/extent.height()*cpu.height());
            const int bottom=qFloor((extent.yMax()-1)/extent.height()*cpu.height());
            for(int py=top;py<=bottom;++py)for(int px=left;px<=right;++px) {
                const auto color=native[frame].pixelColor(px,py);
                QVERIFY(std::max({std::abs(color.red()-expected.red()),std::abs(color.green()-expected.green()),
                    std::abs(color.blue()-expected.blue())})<=3);
            }
        }
        QVERIFY(native[0].pixelColor(native[0].width()/2,native[0].height()/2)
            !=native[1].pixelColor(native[1].width()/2,native[1].height()/2));
        layer.depopulateScene(&scene);
        QFile report(folder+"/indexed-water.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(reports).toJson());
    }
    void nativeForegroundMatchesFallback() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Native foreground comparison");
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/foreground");
        QVERIFY(QDir().mkpath(folder));
        const auto original=qgetenv("OPENSWMM_FLOW_COMPOSITION");
        const auto restore=qScopeGuard([&]{if(original.isNull())qunsetenv("OPENSWMM_FLOW_COMPOSITION");else qputenv("OPENSWMM_FLOW_COMPOSITION",original);});
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<FlowSource>());
        for(auto *s:layer.sublayers())s->setVisible(false);
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);sub->streakStyle()->setColor(Qt::transparent);
        sub->streakStyle()->setAnimated(false);
        CompositionBackdrop raster(true);ParticleReviewCanvas canvas;canvas.resize(480,360);
        canvas.addLayer(&raster,false);canvas.addLayer(&layer,false);canvas.setExtent({-10,-10,110,110},false);
        canvas.setShowCoordinates(true);canvas.setTerrainElevation(46.);
        showNativeReview(canvas);QTRY_VERIFY(canvas.compositionStats().active);QTest::qWait(500);
        for(const QPoint position:{QPoint(100,100),QPoint(350,200)}) {
            QMouseEvent move(QEvent::MouseMove,QPointF(position),QPointF(canvas.mapToGlobal(position)),
                             Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(&canvas,&move);QTest::qWait(100);
            QString error;const auto native=canvas.captureMap(&error).toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!native.isNull(),qPrintable(error));QVERIFY(native.save(folder+QString("/native-%1.png").arg(position.x())));
            qputenv("OPENSWMM_FLOW_COMPOSITION","0");canvas.update();QTRY_VERIFY(!canvas.compositionStats().active);
            const auto fallback=canvas.captureMap(&error).toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!fallback.isNull(),qPrintable(error));QCOMPARE(native.size(),fallback.size());
            QVERIFY(fallback.save(folder+QString("/fallback-%1.png").arg(position.x())));
            int different=0,maximum=0;
            for(int y=0;y<native.height();++y)for(int x=0;x<native.width();++x) {
                const auto a=native.pixel(x,y),b=fallback.pixel(x,y);
                const int delta=std::max({std::abs(qRed(a)-qRed(b)),std::abs(qGreen(a)-qGreen(b)),std::abs(qBlue(a)-qBlue(b))});
                different+=delta>3;maximum=std::max(maximum,delta);
            }
            qInfo()<<"Foreground comparison"<<position<<"different pixels"<<different<<"maximum delta"<<maximum;
            QVERIFY2(different==0,"Native foreground differs from the directly painted map");
            qunsetenv("OPENSWMM_FLOW_COMPOSITION");canvas.update();QTRY_VERIFY(canvas.compositionStats().active);
        }
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
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_DEFAULTS")) {
        style->setMinimumLifetimeSeconds(20);style->setMaximumLifetimeSeconds(40);
            style->setTrailDurationSeconds(1);style->setSpeedMultiplier(2);style->setSpacingPx(30);
        }
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
        canvas.setExtent({box.left(),-box.bottom(),box.right(),-box.top()},false);
        const auto extent=canvas.extent();
        const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        const double scale=canvas.width()/extent.width();
        QElapsedTimer startup;startup.start();showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(layer.flowParticles(view,scale,layer.frameRevision())!=nullptr,10000);
        const double startupMs=startup.nsecsElapsed()/1e6;
        auto initial=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(initial);
        const int initialParticles=int(initial->particles.size());
        int initialTrails=0;double youngest=INFINITY,oldest=0;
        for(const auto &p:initial->particles) {
            initialTrails+=p.history.size()>1;youngest=std::min(youngest,p.age);oldest=std::max(oldest,p.age);
        }
        QVERIFY(initialTrails>0);QVERIFY(initial->clock>=2-1e-8);
        initial.reset(); // Do not retain an extra publication throughout the cadence measurement.
        QTest::qWait(1500);QVERIFY(layer.qsgOwnsRendering());
        QElapsedTimer elapsed;elapsed.start();QTest::qWait(30000);
        const auto frame=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(frame);
        QVERIFY(frame->births>10);QVERIFY(frame->clock>1);QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::VectorConflict)],quint64(0));
        QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::InvalidField)],quint64(0));
        QString error;const auto image=canvas.captureMap(&error);
        if(image.isNull()) {
            qInfo()<<"Native capture pending:"<<layer.flowStreakStatus()<<"particle busy"<<layer.flowStreakBusy()
                <<"layer revision"<<layer.frameRevision();
            for(auto *widget:canvas.findChildren<QQuickWidget*>())if(auto *root=widget->rootObject())
                for(auto *renderer:root->findChildren<SWMM2DResultsQSGRenderer*>())
                    qInfo()<<widget->objectName()<<"capture ready"<<renderer->captureReady()
                        <<"displayed revision"<<renderer->displayedFrameRevision();
        }
        QVERIFY2(!image.isNull(),qPrintable(error));
        QVERIFY(image.save(folder+"/native.png"));
        const auto composition=canvas.compositionStats();
        const auto particlePixels=visibleParticlePixels(canvas,layer,image.toImage(),&error);
        QVERIFY2(particlePixels>=0,qPrintable(error));
        if(style->color().alpha()>0 || style->colorByMagnitude())QVERIFY2(particlePixels>20,"No visible particle pixels in the native map capture");
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
        auto intervals=composition.renderIntervalsMs;
        std::sort(intervals.begin(),intervals.end());
        const auto renderPercentile=[&](double q){return intervals.isEmpty()?0.:intervals[int(q*(intervals.size()-1))];};
        auto swaps=composition.swapIntervalsMs;std::sort(swaps.begin(),swaps.end());
        const auto swapPercentile=[&](double q){return swaps.isEmpty()?0.:swaps[int(q*(swaps.size()-1))];};
        const auto diagnostics=layer.flowParticleDiagnostics();
        std::vector<double> speeds;
        for(const auto &p:frame->particles)if(p.stop==FlowParticleStop::None && p.history.size()>1) {
            const auto &a=p.history[p.history.size()-2],&b=p.history.back();
            if(b.age>a.age)speeds.push_back(std::hypot((b.position-a.position).x(),(b.position-a.position).y())*scale/(b.age-a.age));
        }
        std::sort(speeds.begin(),speeds.end());
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
            {"visibleParticlePixels",particlePixels},{"nativeSurface",composition.nativeSurface},
            {"textureUploads",qint64(composition.textureUploads)},{"captureReadbacks",qint64(composition.captureReadbacks)},
            {"fallbackReadbacks",qint64(composition.fallbackReadbacks)},
            {"renderIntervalMedianMs",renderPercentile(.5)},{"renderIntervalP95Ms",renderPercentile(.95)},
            {"frameSwappedSamples",swaps.size()},
            {"frameSwappedMedianMs",swaps.isEmpty()?QJsonValue():QJsonValue(swapPercentile(.5))},
            {"frameSwappedP95Ms",swaps.isEmpty()?QJsonValue():QJsonValue(swapPercentile(.95))},
            {"wetFraction",diagnostics.wetFraction},{"referenceVelocitySI",diagnostics.referenceSpeedSI},
            {"motionGain",diagnostics.motionGain},{"targetPopulation",diagnostics.targetPopulation},
            {"motionExponent",diagnostics.speedExponent},{"referencePixelsPerSecond",diagnostics.referencePixelsPerSecond},
            {"status",layer.flowStreakStatus()},
            {"unresolvedOrbits",qint64(frame->unresolvedOrbits)},
            {"birthAttempts",qint64(diagnostics.birthAttempts)},{"failedBirths",qint64(diagnostics.failedBirths)},
            {"firstParticleFrameMs",startupMs},{"prewarmWorkMs",diagnostics.prewarmMs},
            {"initialParticles",initialParticles},{"initialFormedTrails",initialTrails},
            {"initialYoungestAge",youngest},{"initialOldestAge",oldest},
            {"publicationMs",diagnostics.publicationMs},{"integrationMs",diagnostics.integrationMs},
            {"medianParticlePixelsPerSecond",speeds.empty()?0.:speeds[speeds.size()/2]},
            {"presentation","Qt Quick render-completion notifications; not OS display presentation; paint costs are separate"}}).toJson());
        canvas.takeLayer(0,false);
    }
    void nativeBellingeParticleHandoffs() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Native saved-result particle handoffs");
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");if(path.isEmpty())QSKIP("Requires the Bellinge saved result");
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/handoffs");QVERIFY(QDir().mkpath(folder));
        SWMM2DResultsLayer layer;layer.setAsyncSurface(true);
        auto source=std::make_unique<HDF5Mesh2DSource>();QVERIFY(source->open(path));
        layer.setSource(std::move(source));layer.setCurrentTimeIndex(layer.source()->timeCount()-1);layer.flowStreakSublayer()->setVisible(true);
        const auto view=layer.m_sceneBBox;const double scale=1280/view.width();
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        QQuickWindow window;window.setColor(Qt::black);window.resize(320,240);
        auto *preview=new ParticleHandoffPreview(window.contentItem());preview->scale=scale;
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto brightness=[](const QImage &image){double value=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)value+=qRed(image.pixel(x,y));return value;};
        double maximum=0;QJsonArray samples;int comparisons=0;
        for(int tick=0;tick<120;++tick) {
            const auto before=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(before);
            layer.advanceFlowStreaks(1./30.);QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
            const auto after=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(after);
            if(after==before)continue;
            const double alpha=after->clock>after->previousClock
                ?std::clamp((before->clock-after->previousClock)/(after->clock-after->previousClock),0.,1.):0;
            int tested=0;
            for(const auto &p:before->particles) {
                if(p.history.size()<3 || p.stop!=FlowParticleStop::None || (p.id+tick)%7)continue;
                const auto it=std::find_if(after->particles.begin(),after->particles.end(),[&](const auto &q){return q.id==p.id;});
                if(it==after->particles.end())continue;
                auto oldFrame=std::make_shared<FlowParticleFrame>();oldFrame->clock=before->clock;oldFrame->previousClock=before->previousClock;oldFrame->particles.push_back(p);
                auto newFrame=std::make_shared<FlowParticleFrame>();newFrame->clock=after->clock;newFrame->previousClock=after->previousClock;newFrame->particles.push_back(*it);
                preview->appearance.anchor=p.position;preview->frame=oldFrame;preview->interpolation=1;preview->update();const auto oldImage=window.grabWindow();
                preview->frame=newFrame;preview->interpolation=alpha;preview->update();const auto newImage=window.grabWindow();
                const double total=brightness(oldImage);if(total<3000)continue;
                const double change=std::abs(brightness(newImage)-total)/total;++comparisons;
                samples.append(QJsonObject{{"tick",tick},{"particle",qint64(p.id)},{"brightnessChange",change},
                    {"oldSamples",int(p.history.size())},{"newSamples",int(it->history.size())},{"historyLimited",it->historyLimited},
                    {"oldClock",before->clock},{"newClock",after->clock},{"newPreviousClock",after->previousClock},{"interpolation",alpha},
                    {"oldAge",p.age},{"newPreviousAge",it->previousAge},{"newAge",it->age}});
                if(change>maximum){
                    maximum=change;QVERIFY(oldImage.save(folder+"/worst-before.png"));QVERIFY(newImage.save(folder+"/worst-after.png"));
                    QJsonArray oldPoints,newPoints;
                    for(const auto &s:p.history)oldPoints.append(QJsonArray{s.position.x(),s.position.y(),s.age});
                    for(const auto &s:it->history)newPoints.append(QJsonArray{s.position.x(),s.position.y(),s.age});
                    QFile details(folder+"/worst.json");QVERIFY(details.open(QIODevice::WriteOnly));
                    details.write(QJsonDocument(QJsonObject{{"scale",scale},{"oldBrightness",total},{"newBrightness",brightness(newImage)},
                        {"oldHistory",oldPoints},{"newHistory",newPoints},{"oldAge",p.age},{"newPreviousAge",it->previousAge},{"newAge",it->age}}).toJson());
                }
                if(++tested>=3)break;
            }
        }
        QFile report(folder+"/handoffs.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"comparisons",comparisons},{"maximumBrightnessChange",maximum},{"samples",samples}}).toJson());report.close();
        QVERIFY(comparisons>30);QVERIFY2(maximum<.2,qPrintable(QString("Individual trail brightness jumps by %1% at publication").arg(100*maximum)));
    }
    void continuousNativeMemorySoak() {
        const bool waterOnly=qEnvironmentVariableIsSet("FLOW_PARTICLE_SOAK_WATER_ONLY");
        const int seconds=qEnvironmentVariableIntValue("FLOW_PARTICLE_SOAK_SECONDS");
        if(seconds<=0)QSKIP("Set FLOW_PARTICLE_SOAK_SECONDS=600 for the ten-minute native memory gate");
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))QSKIP("Requires native graphics");
#ifndef Q_OS_MACOS
        QSKIP("This memory gate requires macOS process footprint, including graphics allocations; native rendering checks are portable");
#endif
        const QString path=qEnvironmentVariable("FLOW_STREAK_RESULT");QVERIFY(!path.isEmpty());
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/soak");
        QVERIFY(QDir().mkpath(folder));
        const auto memory=[]() -> qint64 {
#ifdef Q_OS_MACOS
            task_vm_info_data_t info{};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
            if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS)
                return qint64(info.phys_footprint);
#endif
            return -1;
        };
        SWMM2DResultsLayer layer;layer.setAsyncSurface(true);
        auto source=std::make_unique<HDF5Mesh2DSource>();QVERIFY(source->open(path));
        layer.setSource(std::move(source));layer.setCurrentTimeIndex(layer.source()->timeCount()-1);
        for(auto *s:layer.sublayers())s->setVisible(false);
        const QString waterStyle=qEnvironmentVariable("FLOW_PARTICLE_SOAK_WATER_STYLE","smooth");
        if(waterStyle=="bands")layer.contourBandSublayer()->setVisible(true);
        else {QCOMPARE(waterStyle,QString("smooth"));layer.smoothDepthFillSublayer()->setVisible(true);}
        ParticleReviewCanvas canvas;canvas.resize(1920,1080);canvas.setShowScaleBar(false);
        canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        canvas.addLayer(&layer,false);const auto bounds=layer.m_sceneBBox;
        canvas.setExtent({bounds.left(),-bounds.bottom(),bounds.right(),-bounds.top()},false);showNativeReview(canvas);
        // Match saved/live layers created by SWMMVis: background water fits,
        // with the initial requested frame fully displayed before the baseline.
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        const auto initialExtent=canvas.extent();
        const QRectF initialView(initialExtent.xMin(),-initialExtent.yMax(),initialExtent.width(),initialExtent.height());
        QVERIFY(layer.prepareFlowStreaksForCapture(initialView,canvas.width()/initialExtent.width()));
        QCOMPARE(layer.currentTimeIndex(),layer.source()->timeCount()-1);
        QQuickWindow *mapWindow=nullptr;
        for(auto *window:QGuiApplication::allWindows())if(window->objectName()=="flowMapNativeWindow")
            mapWindow=qobject_cast<QQuickWindow *>(window);
        QVERIFY(mapWindow);
        const auto renderers=mapWindow->contentItem()->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(),1);auto *displayed=renderers.front();
        QTRY_VERIFY_WITH_TIMEOUT(displayed->captureReady() && displayed->displayedFrameRevision()==layer.frameRevision(),30000);
        const quint64 baselineRevision=layer.frameRevision();
        // Optional inspector rendezvous. Heap inspection pauses the process;
        // these runs are diagnostics, never clean memory/cadence evidence.
        const bool allocationDiagnostic=qEnvironmentVariableIsSet("FLOW_PARTICLE_SOAK_ALLOCATION_DIAGNOSTIC");
        const auto inspectPhase=[&](const QString& phase) {
            if(!allocationDiagnostic)return true;
            QFile marker(folder+"/"+phase+"-ready");
            if(!marker.open(QIODevice::WriteOnly))return false;
            marker.close();QElapsedTimer wait;wait.start();
            while(!QFileInfo::exists(folder+"/"+phase+"-continue") && wait.elapsed()<120000)QTest::qWait(50);
            return wait.elapsed()<120000;
        };
        QTest::qWait(3000);QVERIFY(inspectPhase("water-only"));
        const qint64 baseline=memory();QVERIFY(baseline>0);
        layer.flowStreakSublayer()->setVisible(!waterOnly);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);QTest::qWait(3000);
        QVERIFY(inspectPhase("with-particles"));
        const auto initialStats=canvas.compositionStats();
        QElapsedTimer elapsed;elapsed.start();QJsonArray samples;qint64 peak=baseline;
        QJsonObject peakSample;
        QFile progress(folder+"/memory-samples.jsonl");QVERIFY(progress.open(QIODevice::WriteOnly));
        int previousStep=-1;QVector<qint64> early,late;
        bool canonical=true;
        QEventLoop soak;QTimer sampleTimer;sampleTimer.setInterval(250);
        connect(&sampleTimer,&QTimer::timeout,&soak,[&] {
            const qint64 bytes=memory();
            if(bytes>peak) {
                peak=bytes;
                peakSample={{"seconds",elapsed.elapsed()/1000.},{"physicalFootprintBytes",bytes},
                    {"fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes())},
                    {"waterAuxiliaryBytes",qint64(layer.surfaceAuxiliaryBytes())},
                    {"waterWorkingBytes",qint64(layer.surfaceWorkingBytes())},
                    {"waterVisibilityBytes",qint64(layer.waterVisibilityBytes())},
                    {"contourWorkingBytes",qint64(displayed->contourWorkingBytes())},
                    {"waterAttributeCopyBytes",qint64(displayed->retainedWaterAttributeBytes())},
                    {"waterFits",layer.surfaceFitCount()},{"waterBusy",layer.surfaceBusy()},
                    {"displayedWaterRevision",qint64(displayed->displayedFrameRevision())}};
                progress.write(QJsonDocument(QJsonObject{{"newPeak",peakSample}}).toJson(QJsonDocument::Compact)+"\n");
                progress.flush();
            }
            const int step=int(elapsed.elapsed()/5000);
            if(step!=previousStep) {
                previousStep=step;
                const double zoom=step%3==0?1:step%3==1?.75:.5;
                const QPointF center=bounds.center()+QPointF((step%2?1:-1)*bounds.width()*.08,
                                                            (step%4<2?1:-1)*bounds.height()*.08);
                const double w=bounds.width()*zoom,h=bounds.height()*zoom;
                canvas.setExtent({center.x()-w/2,-center.y()-h/2,center.x()+w/2,-center.y()+h/2},false);
                const double spacings[]={40,14,2};
                layer.flowStreakSublayer()->streakStyle()->setSpacingPx(spacings[(step/3)%3]);
                const int last=layer.source()->timeCount()-1;
                layer.setCurrentTimeIndex(std::max(0,last-(step%5)*std::max(1,last/20)));
                if(elapsed.elapsed()>=10000 && elapsed.elapsed()<70000)early.append(bytes);
                if(elapsed.elapsed()>=qint64(std::max(0,seconds-60))*1000)late.append(bytes);
                const auto stats=canvas.compositionStats();
                canonical&=canvas.width()==1920 && canvas.height()==1080 && qFuzzyCompare(canvas.devicePixelRatioF(),2.);
                samples.append(QJsonObject{{"seconds",elapsed.elapsed()/1000.},{"physicalFootprintBytes",bytes},
                    {"logicalWidth",canvas.width()},{"logicalHeight",canvas.height()},{"devicePixelRatio",canvas.devicePixelRatioF()},
                    {"renderedFrames",qint64(stats.renderedFrames)},{"fallbackReadbacks",qint64(stats.fallbackReadbacks)},
                    {"captureReadbacks",qint64(stats.captureReadbacks)},{"textureUploads",qint64(stats.textureUploads)},
                    {"fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes())},
                    {"waterAuxiliaryBytes",qint64(layer.surfaceAuxiliaryBytes())},
                    {"waterWorkingBytes",qint64(layer.surfaceWorkingBytes())},
                    {"waterVisibilityBytes",qint64(layer.waterVisibilityBytes())},
                    {"waterAttributeCopyBytes",qint64(displayed->retainedWaterAttributeBytes())},
                    {"contourWorkingBytes",qint64(displayed->contourWorkingBytes())},
                    {"waterFits",layer.surfaceFitCount()},{"waterBusy",layer.surfaceBusy()},
                    {"displayedWaterRevision",qint64(displayed->displayedFrameRevision())},
                    {"spacingPx",layer.flowStreakSublayer()->streakStyle()->spacingPx()},
                    {"fieldCaptures",layer.flowParticleFieldCaptures()},{"status",layer.flowStreakStatus()}});
                progress.write(QJsonDocument(samples.last().toObject()).toJson(QJsonDocument::Compact)+"\n");progress.flush();
            }
        });
        sampleTimer.start();QTimer::singleShot(seconds*1000,Qt::PreciseTimer,&soak,&QEventLoop::quit);
        soak.exec();sampleTimer.stop();
        const auto median=[](QVector<qint64> values) {std::sort(values.begin(),values.end());return values.isEmpty()?qint64(-1):values[values.size()/2];};
        const qint64 growth=median(late)-median(early);
        const auto stats=canvas.compositionStats();
        QFile report(folder+"/memory-soak.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"source",path},{"observedSeconds",elapsed.elapsed()/1000.},{"waterOnlyControl",waterOnly},
            {"waterStyle",waterStyle},
            {"allocationDiagnostic",allocationDiagnostic},
            {"baselineWaterOnlyFootprintBytes",baseline},{"peakFootprintBytes",peak},{"peakIncreaseBytes",peak-baseline},
            {"peakSample",peakSample},
            {"backgroundWaterFitting",layer.asyncSurface()},{"baselineWaterRevision",qint64(baselineRevision)},
            {"earlyMedianBytes",median(early)},{"lateMedianBytes",median(late)},{"lateGrowthBytes",growth},
            {"devicePixelRatio",canvas.devicePixelRatioF()},{"logicalWidth",canvas.width()},{"logicalHeight",canvas.height()},
            {"nativeSurface",stats.nativeSurface},
            {"renderedFramesDuringSoak",qint64(stats.renderedFrames-initialStats.renderedFrames)},
            {"tenMinuteGateExercised",seconds>=600},{"memoryMetric","macOS task physical footprint; includes graphics allocations charged to process"},
            {"canonicalViewportThroughout",canonical},
            {"samples",samples}}).toJson());report.close();
        QCOMPARE(stats.fallbackReadbacks,initialStats.fallbackReadbacks);QCOMPARE(stats.captureReadbacks,initialStats.captureReadbacks);
        if(qEnvironmentVariableIsSet("FLOW_PARTICLE_CANONICAL"))QVERIFY(canonical);
        if(!waterOnly)QVERIFY(stats.renderedFrames-initialStats.renderedFrames>quint64(seconds)*10);
        QVERIFY2(peak-baseline<=256*1024*1024,qPrintable(QString("Flow footprint increase: %1 MiB").arg((peak-baseline)/1048576.)));
        if(seconds>=120)QVERIFY2(growth<=32*1024*1024,qPrintable(QString("Late footprint growth: %1 MiB").arg(growth/1048576.)));
        canvas.takeLayer(0,false);
    }
    void continuousNativeSolverImpact() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_SOLVER_BENCHMARK"))QSKIP("Explicit native solver benchmark");
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/solver");
        QVERIFY(QDir().mkpath(folder));
        const int n=std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_SOLVER_GRID"),16,150);
        auto *prefs=PreferencesManager::instance();const int oldTick=prefs->progressTickMs();prefs->setProgressTickMs(250);
        const auto oldBackend=qgetenv("OPENSWMM_2D_BACKEND");qputenv("OPENSWMM_2D_BACKEND","cpu");
        const auto restore=qScopeGuard([&]{prefs->setProgressTickMs(oldTick);
            if(oldBackend.isNull())qunsetenv("OPENSWMM_2D_BACKEND");else qputenv("OPENSWMM_2D_BACKEND",oldBackend);});
        QVector<double> waterOnly,withFlow;QJsonArray runs;QByteArray expectedDepthHash;
        for(int run=0;run<6;++run) {
            const bool flow=run==1 || run==2 || run==5;
            const QString stem=QString("%1-%2").arg(run).arg(flow?"flow":"water");
            const QString input=QDir(folder).absoluteFilePath(stem+".inp");
            QFile deck(input);QVERIFY(deck.open(QIODevice::WriteOnly|QIODevice::Text));
            QTextStream text(&deck);
            text<<"[OPTIONS]\nFLOW_UNITS CMS\nINFILTRATION HORTON\nFLOW_ROUTING DYNWAVE\n"
                "START_DATE 01/01/2026\nSTART_TIME 00:00:00\nEND_DATE 01/01/2026\n"
                "END_TIME 02:00:00\nREPORT_STEP 0:05:00\nROUTING_STEP 5\n\n"
                "[JUNCTIONS]\nJ1 10 1 0 0 0\n\n[OUTFALLS]\nO1 9 FREE NO\n\n"
                "[CONDUITS]\nC1 J1 O1 100 0.013 0 0 0\n\n[XSECTIONS]\nC1 CIRCULAR 0.3 0 0 0 1\n\n"
                "[INFLOWS]\nJ1 FLOW TS1 FLOW 1.0 "<<double(n)*n/256<<"\n\n"
                "[TIMESERIES]\nTS1 0:00 0.2\nTS1 2:00 0.2\n\n"
                "[2D_OPTIONS]\nMAX_TIMESTEP 2\nDRY_DEPTH 0.002\nCOUPLING_CD 0.7\n"
                "REPORT_2D YES\nOUTPUT_FILE "<<stem<<".2d.h5\n\n[2D_VERTICES]\n";
            for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)text<<5*x<<' '<<5*y<<' '<<11-.02*x<<'\n';
            text<<"\n[2D_TRIANGLES]\n";
            const auto vertex=[n](int x,int y){return y*(n+1)+x;};
            for(int y=0;y<n;++y)for(int x=0;x<n;++x)
                text<<vertex(x,y)<<' '<<vertex(x+1,y)<<' '<<vertex(x+1,y+1)<<" 0.03\n"
                    <<vertex(x,y)<<' '<<vertex(x+1,y+1)<<' '<<vertex(x,y+1)<<" 0.03\n";
            text<<"\n[2D_VERTEX_NODE_MAP]\n"<<vertex(n/2,n/2)<<" J1 0.7 2.5\n";text.flush();deck.close();
            SWMM2DResultsLayer layer;layer.setAsyncSurface(true);for(auto *s:layer.sublayers())s->setVisible(false);
            layer.smoothDepthFillSublayer()->setVisible(true);layer.flowStreakSublayer()->setVisible(flow);
            layer.setLiveMinFitIntervalMs(250);
            ParticleReviewCanvas canvas;canvas.resize(1920,1080);canvas.setShowScaleBar(false);
            canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
            canvas.addLayer(&layer,false);canvas.setExtent({0,0,double(5*n),double(5*n)},false);showNativeReview(canvas);
            EngineMesh2DSource *source=nullptr;int ticks=0;bool done=false;
            auto *runner=new SimulationRunner(run,stem,input,QDir(folder).absoluteFilePath(stem+".rpt"),
                                               QDir(folder).absoluteFilePath(stem+".out"),"6.0.0",this);
            connect(runner,&SimulationRunner::twoDInitialized,&layer,[&](int,QString,QVector<double> x,QVector<double> y,QVector<double> z,QVector<int> flat){
                std::vector<std::array<int,4>> cells(size_t(flat.size()/4));
                for(size_t i=0;i<cells.size();++i)for(int j=0;j<4;++j)cells[i][j]=flat[int(i)*4+j];
                auto live=std::make_unique<EngineMesh2DSource>(std::vector<double>(x.begin(),x.end()),std::vector<double>(y.begin(),y.end()),
                    std::vector<double>(z.begin(),z.end()),std::move(cells));live->setDryDepth(.002);live->setMaxFrames(64);
                source=live.get();layer.setSource(std::move(live));layer.setDryDepth(.002);
            });
            connect(runner,&SimulationRunner::twoDEdgeGeometryAvailable,&layer,[&](int,QVector<float> length,QVector<float> nx,QVector<float> ny){
                if(source)source->setEdgeGeometry({length.begin(),length.end()},{nx.begin(),nx.end()},{ny.begin(),ny.end()});
            });
            connect(runner,&SimulationRunner::twoDDepthsAvailable,&layer,[&](int,QVector<float> depths,QDateTime time,double elapsed){
                if(source){source->pushDepths({depths.begin(),depths.end()},time,elapsed);++ticks;layer.refreshTimeRange();}
            });
            connect(runner,&SimulationRunner::twoDFluxAvailable,&layer,[&](int,QVector<float> flux,QDateTime time,double elapsed){
                if(source){source->pushFlux({flux.begin(),flux.end()},time,elapsed);layer.refreshTimeRange();layer.refreshCurrentFrame();}
            });
            connect(runner,&SimulationRunner::twoDVertexDepthsAvailable,&layer,[&](int,QVector<double> depths,QDateTime time,double elapsed){
                if(source){source->pushVertexSignedDepths({depths.begin(),depths.end()},time,elapsed);layer.refreshCurrentFrame();}
            });
            connect(runner,&SimulationRunner::twoDEnvelopesAvailable,&layer,[&](int,QVector<float> depth,QVector<float> speed){
                if(source)source->setEnvelopes({depth.begin(),depth.end()},{speed.begin(),speed.end()});
            });
            connect(runner,&SimulationRunner::finished,&layer,[&]{done=true;if(source)source->markFinished();});
            QSignalSpy finished(runner,&SimulationRunner::finished);QPointer<SimulationRunner> guard(runner);
            const auto cleanup=qScopeGuard([&]{if(guard && !done)guard->cancel();});
            QEventLoop simulation;connect(runner,&SimulationRunner::finished,&simulation,&QEventLoop::quit);
            QTimer::singleShot(300000,Qt::PreciseTimer,&simulation,&QEventLoop::quit);
            QElapsedTimer wall;wall.start();runner->start();simulation.exec();
            const double milliseconds=wall.nsecsElapsed()/1e6;QCOMPARE(finished.count(),1);
            QVERIFY2(finished.front()[1].toBool(),qPrintable(finished.front()[3].toString()));QVERIFY(source);QVERIFY(ticks>0);
            // Live publications follow wall-clock progress and can end at
            // different simulated times. Compare the persisted final report.
            HDF5Mesh2DSource saved;QVERIFY(saved.open(QDir(folder).absoluteFilePath(stem+".2d.h5")));
            std::vector<float> depths;QVERIFY(saved.readDepthsAt(saved.timeCount()-1,depths));
            const auto hash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(depths.data()),qsizetype(depths.size()*sizeof(float))),QCryptographicHash::Sha256).toHex();
            if(expectedDepthHash.isEmpty())expectedDepthHash=hash;else QCOMPARE(hash,expectedDepthHash);
            const auto stats=canvas.compositionStats();const auto diagnostics=layer.flowParticleDiagnostics();
            if(flow){QVERIFY(stats.active);QVERIFY(stats.renderedFrames>10);QVERIFY(diagnostics.birthAttempts>0);withFlow.append(milliseconds);}
            else waterOnly.append(milliseconds);
            runs.append(QJsonObject{{"arm",flow?"flow":"water"},{"run",run},{"wallMs",milliseconds},{"liveTicks",ticks},
                {"nativeCells",2*n*n},{"renderedFrames",qint64(stats.renderedFrames)},
                {"particleBirthAttempts",qint64(diagnostics.birthAttempts)},{"finalDepthSha256",QString::fromLatin1(hash)},
                {"finalSavedTime",saved.simTimeAt(saved.timeCount()-1).toString(Qt::ISODateWithMs)},
                {"lastLiveTime",source->simTimeAt(source->timeCount()-1).toString(Qt::ISODateWithMs)},
                {"devicePixelRatio",canvas.devicePixelRatioF()}});
            QFile partial(folder+"/solver-runs.json");QVERIFY(partial.open(QIODevice::WriteOnly));partial.write(QJsonDocument(runs).toJson());
            canvas.takeLayer(0,false);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        std::sort(waterOnly.begin(),waterOnly.end());std::sort(withFlow.begin(),withFlow.end());
        const double slowdown=withFlow[1]/waterOnly[1]-1;
        QFile report(folder+"/solver-impact.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"runs",runs},{"waterMedianMs",waterOnly[1]},
            {"flowMedianMs",withFlow[1]},{"slowdownFraction",slowdown},{"withinFivePercent",slowdown<=.05},
            {"scope","Actual coupled CPU solver, live hydraulic publications and native map; three alternating runs per arm"}}).toJson());
        QVERIFY2(slowdown<=.05,qPrintable(QString("Median solver slowdown: %1%").arg(slowdown*100)));
    }
    void continuousNativeScaleMatrix() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_SCALE_REVIEW"))QSKIP("Explicit multi-million-cell scale fixture");
        const bool quad=qEnvironmentVariable("FLOW_PARTICLE_SCALE_KIND")=="quad";
        const bool live=qEnvironmentVariableIsSet("FLOW_PARTICLE_SCALE_LIVE");
        bool valid=false;double wet=qEnvironmentVariable("FLOW_PARTICLE_SCALE_WET","100").toDouble(&valid)/100;
        QVERIFY(valid && wet>0 && wet<=1);
        const double fraction=qEnvironmentVariable("FLOW_PARTICLE_SCALE_VIEW","100").toDouble(&valid)/100;
        QVERIFY(valid && fraction>0 && fraction<=1);
        const int seconds=std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_NATIVE_SECONDS"),15,120);
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","tests/verification/flow_particles/scale");
        QVERIFY(QDir().mkpath(folder));
        SWMM2DResultsLayer layer;layer.setAsyncSurface(true);
        std::unique_ptr<GridFlowSource> owned=quad?std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(2000,3000))
                                                :std::make_unique<GridFlowSource>(2000,1500);
        auto *source=owned.get();source->wetFraction=wet;source->live=live;
        const QString sourcePath=qEnvironmentVariable("FLOW_STREAK_RESULT");
        if(sourcePath.isEmpty())layer.setSource(std::move(owned));
        else {
            QVERIFY(!live);source=nullptr;owned.reset();
            auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(sourcePath));layer.setSource(std::move(file));
        }
        for(auto *s:layer.sublayers())s->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setColor(QColor(10,30,80));
        ParticleReviewCanvas canvas;canvas.resize(1920,1080);canvas.setShowScaleBar(false);
        canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        canvas.addLayer(&layer,false);const auto box=layer.m_sceneBBox;
        const double w=box.width()*std::sqrt(fraction),h=box.height()*std::sqrt(fraction);
        canvas.setExtent({box.left(),-box.center().y()-h/2,box.left()+w,-box.center().y()+h/2},false);
        showNativeReview(canvas);QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);
        QQuickWindow *mapWindow=nullptr;
        for(auto *window:QGuiApplication::allWindows())if(window->objectName()=="flowMapNativeWindow")
            mapWindow=qobject_cast<QQuickWindow *>(window);
        QVERIFY(mapWindow);
        const auto renderers=mapWindow->contentItem()->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(),1);auto *displayed=renderers.front();
        const auto extent=canvas.extent();const QRectF view(extent.xMin(),-extent.yMax(),extent.width(),extent.height());
        const double scale=canvas.width()/extent.width();
        QVERIFY2(layer.prepareFlowStreaksForCapture(view,scale),qPrintable(layer.flowStreakStatus()));QTest::qWait(3000);
        if(qEnvironmentVariableIsSet("FLOW_PARTICLE_CANONICAL")) {
            QCOMPARE(canvas.width(),1920);QCOMPARE(canvas.height(),1080);QCOMPARE(canvas.devicePixelRatioF(),2.);
        }
        const auto before=canvas.compositionStats();const int fullReads=source?source->reads:0;
        const auto* flowTimer=canvas.findChild<QTimer*>("flowAnimationTimer");
        const auto* published=layer.findChild<FlowParticleController*>();QVERIFY(published);
        bool exposedThroughout=mapWindow->isExposed();
        QElapsedTimer elapsed;elapsed.start();int tick=-1,noMovingHeads=0,observations=0;
        double noHeadSince=-1,longestNoHeadSeconds=0,previousShownClock=-1;QJsonArray motionSamples;
        // Observe through the ordinary event dispatcher. qWait's polling
        // sleeps can delay frame timers and must not become display timings.
        QEventLoop observation;QTimer observationTimer;observationTimer.setInterval(100);
        connect(&observationTimer,&QTimer::timeout,&observation,[&] {
            exposedThroughout&=mapWindow->isExposed();
            if(live && int(elapsed.elapsed()/3000)!=tick) {
                tick=int(elapsed.elapsed()/3000);source->speed=tick%2?2:3;++source->revision;layer.refreshCurrentFrame();
            }
            const auto shownRevision=displayed->displayedFrameRevision();
            const auto observed=published->frame(shownRevision);
            const double shownClock=observed?observed->previousClock+(observed->clock-observed->previousClock)*layer.flowParticleInterpolation(observed):-1;
            const bool clockAdvanced=observed && (previousShownClock<0 || shownClock>previousShownClock+1e-8);
            if(observed)previousShownClock=shownClock;
            bool movingHead=false;
            if(observed)for(const auto &p:observed->particles)if(p.stop==FlowParticleStop::None && !p.waitingForData && p.history.size()>1) {
                const auto delta=p.history.back().position-p.history[p.history.size()-2].position;
                movingHead|=clockAdvanced && std::hypot(delta.x(),delta.y())*scale>1e-5;
            }
            motionSamples.append(QJsonObject{{"seconds",elapsed.elapsed()/1000.},{"requestedRevision",qint64(layer.frameRevision())},
                {"displayedRevision",qint64(shownRevision)},{"particleClock",shownClock},{"movingHead",movingHead},
                {"nativeWindowExposed",mapWindow->isExposed()}});
            ++observations;
            if(!movingHead) {
                ++noMovingHeads;if(noHeadSince<0)noHeadSince=elapsed.elapsed()/1000.;
                longestNoHeadSeconds=std::max(longestNoHeadSeconds,elapsed.elapsed()/1000.-noHeadSince);
            } else noHeadSince=-1;
        });
        observationTimer.start();QTimer::singleShot(seconds*1000,Qt::PreciseTimer,&observation,&QEventLoop::quit);
        observation.exec();observationTimer.stop();
        const double observedSeconds=elapsed.elapsed()/1000.;
        const auto after=canvas.compositionStats();
        const int observationFullReads=source?source->reads-fullReads:0;
        auto intervals=after.renderIntervalsMs;
        const auto count=after.renderedFrames-before.renderedFrames;
        if(count<quint64(intervals.size()))intervals=intervals.mid(intervals.size()-int(count));
        std::sort(intervals.begin(),intervals.end());
        auto swaps=after.swapIntervalsMs;
        if(count<quint64(swaps.size()))swaps=swaps.mid(swaps.size()-int(count));
        std::sort(swaps.begin(),swaps.end());
        // Preserve the measured window even if a later capture is incomplete.
        QFile observationReport(folder+"/observation.json");QVERIFY(observationReport.open(QIODevice::WriteOnly));
        observationReport.write(QJsonDocument(QJsonObject{{"observedSeconds",observedSeconds},
            {"observationScope","Read-only published controller frames; ordinary map renderer drives preparation and dispatch"},
            {"framesBefore",qint64(before.renderedFrames)},{"framesAfter",qint64(after.renderedFrames)},
            {"nativeWindowExposedThroughout",exposedThroughout},{"nativeWindowExposedAfter",mapWindow->isExposed()},
            {"nativeWindowVisible",mapWindow->isVisible()},{"surfaceBusyAfter",layer.surfaceBusy()},
            {"canvasVisible",canvas.isVisible()},{"canvasMinimized",canvas.window()->isMinimized()},
            {"animationTimerActive",flowTimer && flowTimer->isActive()},
            {"motionEnabled",layer.flowStreaksEnabled()},{"motionAnimated",layer.flowStreakSublayer()->streakStyle()->animated()},
            {"requestedRevision",qint64(layer.frameRevision())},{"displayedRevision",qint64(displayed->displayedFrameRevision())},
            {"motionSamples",motionSamples},{"longestNoMovingHeadSeconds",longestNoHeadSeconds},
            {"frameSwappedSamples",swaps.size()},
            {"frameSwappedP95Ms",swaps.isEmpty()?QJsonValue():QJsonValue(swaps[int(.95*(swaps.size()-1))])},
            {"frameSwapWithinTarget",swaps.isEmpty()?QJsonValue():QJsonValue(swaps[int(.95*(swaps.size()-1))]<=(fraction<1?16.7:33.0))},
            {"status",layer.flowStreakStatus()}}).toJson());observationReport.close();
        QVERIFY(!intervals.isEmpty());
        // A final live tick may still be fitting when observation ends.
        // Settle only the subsequent field/image capture, after saving timing.
        QVERIFY2(layer.prepareFlowStreaksForCapture(view,scale),qPrintable(layer.flowStreakStatus()));
        const auto frame=layer.flowParticles(view,scale,layer.frameRevision(),false);QVERIFY(frame);
        int moving=0,waiting=0;
        for(const auto &p:frame->particles) {
            waiting+=p.waitingForData;
            if(p.history.size()>1) {
                const auto distance=p.history.back().position-p.history.front().position;
                moving+=std::hypot(distance.x(),distance.y())*scale>.25;
            }
        }
        const auto diagnostics=layer.flowParticleDiagnostics();
        const auto input=layer.captureFlowParticleInput(view,scale);QVERIFY2(input,qPrintable(layer.flowStreakStatus()));
        QString error;const auto image=canvas.captureMap(&error);QVERIFY2(!image.isNull(),qPrintable(error));
        QVERIFY(image.save(folder+"/scale.png"));
        const auto particlePixels=visibleParticlePixels(canvas,layer,image.toImage(),&error);
        QVERIFY2(particlePixels>20,"The scale fixture has no visible particle pixels");
        QFile report(folder+"/scale.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"kind",quad?"quad":"triangle"},{"source",sourcePath.isEmpty()?QString(live?"synthetic live":"synthetic held"):sourcePath},
            {"observationScope","Read-only published controller frames; ordinary map renderer drives preparation and dispatch"},
            {"nativeCells",layer.source()->triangleCount()},{"displayTriangles",layer.m_sceneTris.size()},
            {"wetFraction",wet},{"requestedViewFraction",fraction},{"observedSeconds",observedSeconds},
            {"logicalWidth",canvas.width()},{"logicalHeight",canvas.height()},{"devicePixelRatio",canvas.devicePixelRatioF()},
            {"residentFlowTriangles",int(input->triangles.size())},{"estimatedWetFraction",diagnostics.wetFraction},
            {"targetPopulation",diagnostics.targetPopulation},{"particles",int(frame->particles.size())},
            {"ribbonParticleSlots",frame->ribbon.particles},{"ribbonHistorySlots",frame->ribbon.history},
            {"ribbonVertexBytes",qint64(frame->ribbon.vertices.size()*sizeof(FlowParticleVertex))},
            {"reservedRibbonVertexBytes",qint64(frame->ribbon.vertices.capacity()*sizeof(FlowParticleVertex))},
            {"movingParticles",moving},{"waitingForData",waiting},{"renderedFrames",qint64(count)},
            {"visibleParticlePixels",particlePixels},
            {"observations",observations},{"observationsWithoutMovingHeads",noMovingHeads},{"longestNoMovingHeadSeconds",longestNoHeadSeconds},
            {"motionSamples",motionSamples},
            {"renderCompletionMedianMs",intervals[int(.5*(intervals.size()-1))]},
            {"renderCompletionP95Ms",intervals[int(.95*(intervals.size()-1))]},
            {"nativeSurface",after.nativeSurface},{"frameSwappedSamples",swaps.size()},
            {"frameSwappedMedianMs",swaps.isEmpty()?QJsonValue():QJsonValue(swaps[int(.5*(swaps.size()-1))])},
            {"frameSwappedP95Ms",swaps.isEmpty()?QJsonValue():QJsonValue(swaps[int(.95*(swaps.size()-1))])},
            {"frameSwapWithinTarget",swaps.isEmpty()?QJsonValue():QJsonValue(swaps[int(.95*(swaps.size()-1))]<=(fraction<1?16.7:33.0))},
            {"osPresentationP95Ms",QJsonValue()},{"cadenceScope","Separate Qt render completions and native queued-for-presentation swaps; OS scanout not directly measured"},
            {"steadyFallbackReadbacks",qint64(after.fallbackReadbacks-before.fallbackReadbacks)},
            {"steadyCaptureReadbacks",qint64(after.captureReadbacks-before.captureReadbacks)},
            {"fullReadsDuringObservation",source?QJsonValue(observationFullReads):QJsonValue()},
            {"fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes())},{"status",layer.flowStreakStatus()}}).toJson());report.close();
        QVERIFY2(moving>0,qPrintable(layer.flowStreakStatus()));QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        QCOMPARE(waiting,0);QVERIFY2(longestNoHeadSeconds<.5,"The overview repeatedly lost all moving heads");
        if(after.nativeSurface)QVERIFY(swaps.size()>10);
        QCOMPARE(after.fallbackReadbacks,before.fallbackReadbacks);QCOMPARE(after.captureReadbacks,before.captureReadbacks);
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
            layer.flowStreakSublayer()->setVisible(true);
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
        style->setSpeedMultiplier(1);style->setSpacingPx(30);
        style->setMinimumLifetimeSeconds(60);style->setMaximumLifetimeSeconds(90);style->setTrailDurationSeconds(10);
        style->setColor(QColor(10,30,80));
        const QRectF view(cols*.65,-rows*.5,40,30);const double scale=32;
        timer.restart();QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const double prepareMs=timer.nsecsElapsed()/1e6;
        if(large)QVERIFY(layer.regionalHydraulicsPublished());
        const auto input=layer.captureFlowParticleInput(view,scale);QVERIFY(input);
        // Startup support can produce a fresh prefetch packet. Its topology
        // belongs to the worker; settle that captured packet before reading it.
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        QCOMPARE(input->mesh->neighbors.size(),input->triangles.size());
        QVERIFY(input->triangles.size()<size_t(FlowParticleMesh::maxRegionTriangles));
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
        auto first=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(first);
        QVERIFY(first->particles.size()>10);QCOMPARE(raw->reads,fullReads);QVERIFY(raw->regionalReads-regionalReads<10);
        int crossings=0;for(const auto &p:first->particles)crossings+=p.crossings;QVERIFY(crossings>0);
        // A new live packet must use a regional read and retain particle IDs,
        // assigned lifetimes, recorded history and world positions.
        raw->speed=3;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision()!=first->revision,10000);
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        auto updated=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(updated);
        for(const auto &old:first->particles) {
            if(old.stop!=FlowParticleStop::None && first->clock-old.stoppedAt>=style->lifetimeFadeSeconds())continue;
            const auto it=std::find_if(updated->particles.begin(),updated->particles.end(),[&](const auto &p){return p.id==old.id;});
            QVERIFY(it!=updated->particles.end());QCOMPARE(it->position,old.position);QCOMPARE(it->age,old.age);
            QCOMPARE(it->lifetime,old.lifetime);QCOMPARE(it->history.size(),old.history.size());
        }
        if(large) {QCOMPARE(raw->reads,fullReads);QVERIFY(raw->regionalReads>regionalReads);}
        updated.reset(); // Comparison complete; return this publication slot.
        const QRectF distant(cols*.1,-rows*.2,40,30);
        QVERIFY(layer.prepareFlowStreaksForCapture(distant,scale));
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(distant,scale));
        auto away=layer.flowParticles(distant,scale,layer.frameRevision());QVERIFY(away);
        int continued=0,recycled=0;
        for(const auto &old:first->particles) {
            if(old.stop!=FlowParticleStop::None || old.lifetime-old.age<=.2)continue;
            const auto it=std::find_if(away->particles.begin(),away->particles.end(),[&](const auto &p){return p.id==old.id;});
            if(it==away->particles.end()) {
                // A full pool may recycle only trajectories whose entire
                // bounds are outside the new view. No-pressure retention is
                // covered separately by the controller core regression.
                double left=old.position.x(),right=left,top=old.position.y(),bottom=top;
                for(const auto &s:old.history){left=std::min(left,s.position.x());right=std::max(right,s.position.x());top=std::min(top,s.position.y());bottom=std::max(bottom,s.position.y());}
                QVERIFY(right<distant.left() || left>distant.right() || bottom<distant.top() || top>distant.bottom());
                ++recycled;continue;
            }
            QVERIFY(it->position.x()>old.position.x());QVERIFY(!it->waitingForData);QCOMPARE(it->lifetime,old.lifetime);
            ++continued;
        }
        QVERIFY(continued+recycled>10);QVERIFY(layer.flowParticleDiagnostics().offscreenReplacements>=quint64(recycled));
        int visibleAway=0;for(const auto &p:away->particles)visibleAway+=p.stop==FlowParticleStop::None && distant.contains(p.position);
        QVERIFY(visibleAway>=layer.flowParticleDiagnostics().targetPopulation-1);
        away.reset(); // Refill can publish several bounded batches on return.
        QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        auto returned=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(returned);
        int resumed=0;for(const auto &old:first->particles)for(const auto &p:returned->particles)
            if(p.id==old.id && p.position.x()>old.position.x())++resumed;
        QVERIFY(resumed+recycled>10);
        int visibleReturned=0;for(const auto &p:returned->particles)visibleReturned+=p.stop==FlowParticleStop::None && view.contains(p.position);
        QVERIFY2(visibleReturned>=layer.flowParticleDiagnostics().targetPopulation-1,qPrintable(QString("visible=%1 total=%2 target=%3 recycled=%4 busy=%5 %6")
            .arg(visibleReturned).arg(returned->particles.size()).arg(layer.flowParticleDiagnostics().targetPopulation)
            .arg(layer.flowParticleDiagnostics().offscreenReplacements).arg(layer.flowStreakBusy()).arg(layer.flowStreakStatus())));
        QJsonObject report{{"nativeCells",raw->triangleCount()},{"displayTriangles",layer.m_sceneTris.size()},
            {"cellType",quad?"quad":"triangle"},{"loadMs",loadMs},{"regionPrepareMs",prepareMs},
            {"manualAdvanceMs",advanceMs},{"manualAnimationSeconds",2},{"particles",int(first->particles.size())},
            {"cellCrossings",crossings},{"regionTriangles",int(input->triangles.size())},
            {"inputPayloadBytes",qint64(payload)},{"fullReadsAfterLoad",raw->reads-fullReads},
            {"regionalReads",raw->regionalReads},{"resumedParticles",resumed},{"offscreenRecycledParticles",recycled},
            {"memoryScope","input payload only; process high-water includes baseline mesh, map and renderer"}};
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","workplans/artifacts/flow_particle_fallback_2026-10-12");QDir().mkpath(folder);
        const QString name=QString(large?"6m-%1":"regional-%1").arg(quad?"quads":"triangles");
        if(native) {
            ParticleReviewCanvas canvas;canvas.resize(1920,1080);canvas.setShowScaleBar(false);
            canvas.setWindowFlags(canvas.windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
            canvas.addLayer(&layer,false);canvas.setExtent({view.left(),-view.bottom(),view.right(),-view.top()},false);canvas.show();
            QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active,10000);QTest::qWait(3000);
            const auto before=canvas.compositionStats();
            const int seconds=std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_NATIVE_SECONDS"),15,120);
            QTest::qWait(seconds*1000);const auto after=canvas.compositionStats();
            QVERIFY(after.renderedFrames>before.renderedFrames+100);
            QCOMPARE(after.fallbackReadbacks,before.fallbackReadbacks);QCOMPARE(after.captureReadbacks,before.captureReadbacks);
            QCOMPARE(after.textureUploads,before.textureUploads);
            // Select only new intervals, including after the bounded history
            // has evicted the beginning of a longer residency/lifetime run.
            const auto newFrames=after.renderedFrames-before.renderedFrames;
            auto intervals=after.renderIntervalsMs.mid(int(std::max<qint64>(0,after.renderIntervalsMs.size()-qint64(newFrames))));
            std::sort(intervals.begin(),intervals.end());
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
            report.insert("fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes()));
            report.insert("fieldCaptures",layer.flowParticleFieldCaptures());report.insert("fieldSubsetReads",layer.flowParticleFieldReads());
            if(large && seconds>=90) {
                QVERIFY(moving>0);QCOMPARE(waiting,0);QVERIFY(current->deaths>0);
                QCOMPARE(current->deaths,current->stopCounts[size_t(FlowParticleStop::Lifetime)]);
                QCOMPARE(raw->reads,fullReads);
            }
            report.insert("performanceCertified",false); // short profile; residency/lifetime/memory gates remain
            report.insert("births",qint64(current->births));report.insert("deaths",qint64(current->deaths));
            QJsonArray stops;for(auto count:current->stopCounts)stops.append(qint64(count));report.insert("stopCounts",stops);
            QString error;const auto image=canvas.captureMap(&error);QVERIFY2(!image.isNull(),qPrintable(error));QVERIFY(image.save(folder+"/"+name+".png"));
            canvas.takeLayer(0,false);
        }
        QFile file(folder+"/"+name+".json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());
        if(large) {
            // The four-slot publication pool intentionally backpressures a
            // consumer retaining four old frames. These comparisons are done.
            first.reset();updated.reset();away.reset();returned.reset();
            const int readsBeforeOverview=raw->reads;
            const double overviewScale=1280./layer.m_sceneBBox.width();timer.restart();
            const auto overview=layer.captureFlowParticleInput(layer.m_sceneBBox,overviewScale);
            QVERIFY2(overview,qPrintable(layer.flowStreakStatus()));
            QCOMPARE(raw->reads,readsBeforeOverview);
            QVERIFY(overview->triangles.size()<40000);QVERIFY(!overview->mesh->seedWeights.empty());
            const double captureMs=timer.nsecsElapsed()/1e6;
            qInfo()<<"Overview initial capture"<<overview->triangles.size()<<"triangles"<<captureMs<<"ms"<<layer.flowStreakStatus();
            QVERIFY2(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,overviewScale),qPrintable(layer.flowStreakStatus()));
            const auto beforeOverview=layer.flowParticles(layer.m_sceneBBox,overviewScale,layer.frameRevision());QVERIFY(beforeOverview);
            for(int i=0;i<60;++i) {
                layer.advanceFlowStreaks(1./60.);
                QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,overviewScale));
            }
            const auto overviewFrame=layer.flowParticles(layer.m_sceneBBox,overviewScale,layer.frameRevision());QVERIFY(overviewFrame);
            int moving=0;for(const auto &p:overviewFrame->particles)for(const auto &old:beforeOverview->particles)
                moving+=p.id==old.id && std::hypot((p.position-old.position).x(),(p.position-old.position).y())>1e-6;
            QFile evidence(folder+"/"+name+"-overview.json");QVERIFY(evidence.open(QIODevice::WriteOnly));
            evidence.write(QJsonDocument(QJsonObject{{"captureMs",captureMs},{"residentTriangles",int(overview->triangles.size())},
                {"particles",int(overviewFrame->particles.size())},{"movingParticles",moving},
                {"fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes())},
                {"status",layer.flowStreakStatus()}}).toJson());
            QVERIFY(moving>0);QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        }
    }
    void overviewScaleChangesRefreshTheSeedPatchesWithoutATick() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<GridFlowSource>(700,200));
        layer.flowStreakSublayer()->setVisible(true);
        const auto view=layer.m_sceneBBox;
        const auto first=layer.captureFlowParticleInput(view,10);QVERIFY(first);
        const auto resized=layer.captureFlowParticleInput(view,.1);QVERIFY(resized);QVERIFY(resized!=first);
        const auto seeds=[](const auto &input) {
            return std::count_if(input->mesh->seedWeights.begin(),input->mesh->seedWeights.end(),[](double weight){return weight>0;});
        };
        QVERIFY(seeds(first)>seeds(resized));QVERIFY(seeds(resized)>0);
        QCOMPARE(layer.captureFlowParticleInput(view,.1),resized);
    }
    void regionalSeedCoverageUsesMotionDistance() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<GridFlowSource>(400,200));
        layer.flowStreakSublayer()->setVisible(true);
        const QRectF view(150,-100,60,40);
        const auto input=layer.captureFlowParticleInput(view,32);QVERIFY2(input,qPrintable(layer.flowStreakStatus()));
        QVERIFY(input->mesh->seedWeights.empty());QVERIFY(input->mesh->coverage.contains(view));
        QVERIFY2(input->triangles.size()<14000,"A small zoomed view loads a half-viewport margin unrelated to motion");
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(8);
        const auto fast=layer.captureFlowParticleInput(view,32);QVERIFY(fast);
        QVERIFY(fast->triangles.size()>input->triangles.size());
    }
    void backwardSeekDropsThePreviousResidencyExtent() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<GridFlowSource>(400,200));
        layer.flowStreakSublayer()->setVisible(true);layer.setCurrentTimeIndex(2);
        const QRectF view(150,-100,60,40);QVERIFY(layer.prepareFlowStreaksForCapture(view,32));
        const auto before=layer.captureFlowParticleInput(view,32);QVERIFY(before);
        layer.setCurrentTimeIndex(1);const QRectF zoom(175,-85,5,5);
        QVERIFY(layer.prepareFlowStreaksForCapture(zoom,128));
        const auto after=layer.captureFlowParticleInput(zoom,128);QVERIFY(after);
        QVERIFY2(after->triangles.size()<before->triangles.size()/2,"A backward seek keeps the previous view's now-obsolete field extent");
    }
    void pendingSeekKeepsOldHistoryLimitsUntilItsPacketIsReady() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<GridFlowSource>(16,8);auto *raw=source.get();
        layer.setSource(std::move(source));layer.setCurrentTimeIndex(2);layer.flowStreakSublayer()->setVisible(true);
        auto *style=layer.flowStreakSublayer()->streakStyle();style->setSpacingPx(2);
        const auto view=layer.m_sceneBBox;QVERIFY(layer.prepareFlowStreaksForCapture(view,128));
        const auto before=layer.flowParticles(view,128,layer.frameRevision());QVERIFY(before && !before->particles.empty());
        QVERIFY(before->particles.front().history.capacity()<=256);
        style->setSpacingPx(40);layer.setCurrentTimeIndex(1);
        QTRY_VERIFY(layer.frameRevision()!=before->revision);
        // The source has a newer packet, but the displayed surface is still
        // from the preceding content revision. Capture must wait for coherence.
        ++raw->revision;
        layer.advanceFlowStreaks(1./30.);QVERIFY(!layer.prepareFlowStreaksForCapture(view,128));
        const auto waiting=layer.flowParticles(view,128,before->revision,false);QVERIFY(waiting && !waiting->particles.empty());
        QVERIFY2(waiting->particles.front().history.capacity()<=256,"New history limits expand the abandoned population before its reset packet arrives");
        --raw->revision;QVERIFY(layer.prepareFlowStreaksForCapture(view,128));
        const auto after=layer.flowParticles(view,128,layer.frameRevision());QVERIFY(after && !after->particles.empty());
        QVERIFY(after->particles.front().history.capacity()>=512);
    }
    void liveResidencyExtendsFromTheDisplayedHydraulicSnapshot_data() {
        QTest::addColumn<bool>("quad");QTest::addColumn<bool>("overview");
        QTest::newRow("triangle-region")<<false<<false;
        QTest::newRow("triangle-overview")<<false<<true;
        QTest::newRow("quad-region")<<true<<false;
        QTest::newRow("quad-overview")<<true<<true;
    }
    void liveResidencyExtendsFromTheDisplayedHydraulicSnapshot() {
        QFETCH(bool,quad);QFETCH(bool,overview);
        const auto source=[&]() -> std::unique_ptr<GridFlowSource> {
            if(quad)return std::make_unique<QuadGridFlowSource>(400,200);
            return std::make_unique<GridFlowSource>(400,200);
        };
        SWMM2DResultsLayer layer,reference;layer.setAsyncSurface(true);reference.setAsyncSurface(true);
        auto live=source();auto *raw=live.get();layer.setSource(std::move(live));reference.setSource(source());
        layer.flowStreakSublayer()->setVisible(true);reference.flowStreakSublayer()->setVisible(true);
        const QRectF view=overview?layer.m_sceneBBox:QRectF(60,-160,16,12);
        const QRectF nextView=overview?view.translated(10,0):view.translated(80,0);
        const double scale=overview?4:16,nextScale=scale*1.25;
        const auto initial=layer.captureFlowParticleInput(view,scale);QVERIFY(initial);
        const auto expected=reference.captureFlowParticleInput(nextView,nextScale);QVERIFY(expected);
        const auto displayedRevision=layer.frameRevision();const int subsetReads=raw->regionalReads;
        // A mutable live packet advances before its replacement surface is
        // published. New residency must use the same old values as the water.
        raw->speed=-3;raw->depths[0]=.75f;++raw->revision;layer.refreshCurrentFrame();
        const auto extended=layer.captureFlowParticleInput(nextView,nextScale);
        QVERIFY2(extended,"A newer live packet prevents extending the displayed native field");
        QCOMPARE(layer.frameRevision(),displayedRevision);QCOMPARE(extended->revision,displayedRevision);
        QCOMPARE(raw->regionalReads,subsetReads);
        QCOMPARE(extended->mesh->sourceTriangles,expected->mesh->sourceTriangles);
        QCOMPARE(extended->triangles.size(),expected->triangles.size());
        for(size_t i=0;i<expected->triangles.size();++i) {
            const auto &a=extended->triangles[i],&b=expected->triangles[i];
            QCOMPARE(a.meanDepth,b.meanDepth);QVERIFY(a.depth==b.depth);QVERIFY(a.flux==b.flux);
            QVERIFY(a.metres==b.metres);QCOMPARE(a.available,b.available);
        }
        ++raw->generation;
        QVERIFY(!layer.captureFlowParticleInput(nextView.translated(5,0),nextScale*1.1));
    }
    void overviewCullPaddingDoesNotReloadTheField() {
        SWMM2DResultsLayer layer;layer.setSource(std::make_unique<GridFlowSource>(700,200));
        layer.flowStreakSublayer()->setVisible(true);
        const auto view=layer.m_sceneBBox.adjusted(20,20,-20,-20);const double scale=4.1;
        const auto first=layer.captureFlowParticleInput(view,scale);QVERIFY(first);
        const int reads=layer.flowParticleFieldReads();
        // The native renderer culls with two extra pixels. Observation/capture
        // uses the exact viewport, and its scale may have passed through float.
        const auto padded=view.adjusted(-2/scale,-2/scale,2/scale,2/scale);
        QCOMPARE(layer.captureFlowParticleInput(padded,double(float(scale))),first);
        QCOMPARE(layer.captureFlowParticleInput(view,scale),first);
        QCOMPARE(layer.flowParticleFieldReads(),reads);
        const auto panned=layer.captureFlowParticleInput(view.translated(8/scale,0),scale);
        QVERIFY(panned);QVERIFY(panned!=first);
    }
    void missingOrMismatchedResidencyPacketsRetainTheAcceptedField() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<GridFlowSource>(700,200);auto *raw=source.get();
        layer.setSource(std::move(source));auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setSpeedMultiplier(1);
        style->setMinimumLifetimeSeconds(60);style->setMaximumLifetimeSeconds(90);
        const QRectF view(300,-40,30,20);QVERIFY(layer.prepareFlowStreaksForCapture(view,16));
        const auto accepted=layer.captureFlowParticleInput(view,16);QVERIFY(accepted);
        raw->regionalMissing=true;
        const QRectF outside(100,-20,30,20);QVERIFY(!layer.captureFlowParticleInput(outside,16));
        QCOMPARE(layer.captureFlowParticleInput(view,16),accepted);
        raw->regionalMissing=false;raw->regionalMismatch=true;
        QVERIFY(!layer.captureFlowParticleInput(outside,16));QCOMPARE(layer.captureFlowParticleInput(view,16),accepted);
        raw->regionalMismatch=false;
        const auto replacement=layer.captureFlowParticleInput(outside,16);QVERIFY(replacement);QVERIFY(replacement!=accepted);
        // Missing overview probes keep the working regional snapshot too.
        raw->regionalMissing=true;
        QVERIFY(!layer.captureFlowParticleInput(layer.m_sceneBBox,1));
        QCOMPARE(layer.captureFlowParticleInput(outside,16),replacement);QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        raw->regionalMissing=false;
        const auto overview=layer.captureFlowParticleInput(layer.m_sceneBBox,1);QVERIFY(overview);
        QVERIFY(!overview->mesh->seedWeights.empty());QVERIFY(overview->triangles.size()<40000);
    }
    void liveHistoryTrimPreservesAgeButAnActualBackwardSeekResetsIt() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();auto *raw=source.get();raw->live=true;
        layer.setSource(std::move(source));auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setSpeedMultiplier(.1);
        layer.setCurrentTimeIndex(2);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        layer.advanceFlowStreaks(1);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto old=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(!old->particles.empty());
        const auto selected=std::find_if(old->particles.begin(),old->particles.end(),[](const auto &p){return p.stop==FlowParticleStop::None;});
        QVERIFY(selected!=old->particles.end());
        raw->timeOrigin+=120;++raw->generation;layer.setCurrentTimeIndex(0);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto trimmed=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(trimmed);
        const auto retained=std::find_if(trimmed->particles.begin(),trimmed->particles.end(),[&](const auto &p){return p.id==selected->id;});
        QVERIFY(retained!=trimmed->particles.end());QCOMPARE(trimmed->clock,old->clock);
        QCOMPARE(retained->age,selected->age);QCOMPARE(retained->position,selected->position);
        raw->timeOrigin-=120;++raw->revision;layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision()!=trimmed->revision);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto sought=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(sought);
        QVERIFY(!sought->particles.empty());QVERIFY(std::abs(sought->clock-2)<1e-8);
        QVERIFY(sought->particles.front().history.size()>1);QCOMPARE(layer.flowParticleOpacity(),0.);
    }
    void continuousBirthsAreDistributedAcrossTheSelectedLifetimeRange() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();source->speed=.001f;
        layer.setSource(std::move(source));auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setSpeedMultiplier(.001*4/48);
        style->setMinimumLifetimeSeconds(20);style->setMaximumLifetimeSeconds(40);style->setLifetimeFadeSeconds(1);style->setSpacingPx(30);
        const auto view=layer.m_sceneBBox;QVERIFY(layer.prepareFlowStreaksForCapture(view,4));
        auto frame=layer.flowParticles(view,4,layer.frameRevision());QVERIFY(frame);
        const double startClock=frame->clock;QVERIFY(std::abs(startClock-2)<1e-8);
        const auto checkPopulation=[&] {
            int active=0;for(const auto &p:frame->particles)active+=p.stop==FlowParticleStop::None;
            const int target=layer.flowParticleDiagnostics().targetPopulation;
            // A head dying on the final integration tick is replaced on the
            // next tick. Fading trails remain in their own bounded slots.
            QVERIFY(active<=target);QVERIFY(active>=target-1);
        };
        checkPopulation();
        for(int i=0;i<120;++i) {layer.advanceFlowStreaks(1./60);QVERIFY(layer.prepareFlowStreaksForCapture(view,4));}
        frame=layer.flowParticles(view,4,layer.frameRevision());QVERIFY(frame);
        QVERIFY(frame->births>10);QVERIFY(frame->deaths>0);
        QCOMPARE(frame->deaths,frame->stopCounts[size_t(FlowParticleStop::Lifetime)]);
        double minLife=INFINITY,maxLife=0;
        for(const auto &p:frame->particles) {
            minLife=std::min(minLife,p.lifetime);maxLife=std::max(maxLife,p.lifetime);
        }
        QVERIFY(maxLife-minLife>10);
        checkPopulation();
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths,quint64(0));
        QVERIFY(std::abs(frame->clock-startClock-2)<1e-8);
    }
    void continuousResidencyThroughRandomizedDeaths_data() {continuousRegionalLargeMesh_data();}
    void continuousResidencyThroughRandomizedDeaths() {
        QFETCH(bool,quad);const bool large=qEnvironmentVariableIsSet("FLOW_PARTICLE_6M");
        const int cols=large?2000:500,rows=large?(quad?3000:1500):80;
        SWMM2DResultsLayer layer;
        std::unique_ptr<GridFlowSource> source=quad?std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(cols,rows)):
            std::make_unique<GridFlowSource>(cols,rows);auto *raw=source.get();raw->speed=1;
        layer.setSource(std::move(source));for(auto *sub:layer.sublayers())sub->setVisible(false);
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);auto *style=sub->streakStyle();
        style->setSpeedMultiplier((large && !quad?2.:1.)/3);style->setSpacingPx(50);
        style->setMinimumLifetimeSeconds(60);style->setMaximumLifetimeSeconds(90);style->setTrailDurationSeconds(10);
        const QRectF view(large?1000:50,-rows*.5,30,20),distant(large?100:10,-rows*.2,30,20);
        const double scale=16;QVERIFY(layer.prepareFlowStreaksForCapture(view,scale));
        const int fullReads=raw->reads;
        struct Trace {QPointF start;double born,life,lastAge,expectedX;};std::map<quint64,Trace> traces;
        QSet<quint64> deaths;int witnessedDeaths=0,maxWaiting=0,aliveAtEnd=0;double maxError=0;QElapsedTimer timer;timer.start();
        for(int step=0;step<1000;++step) {
            const QRectF requested=step>=300 && step<400?distant:view;
            QVERIFY(layer.prepareFlowStreaksForCapture(requested,scale));
            layer.advanceFlowStreaks(.1);QVERIFY(layer.prepareFlowStreaksForCapture(requested,scale));
            const auto frame=layer.flowParticles(requested,scale,layer.frameRevision());QVERIFY(frame);
            int waiting=0;const auto motion=layer.flowParticleDiagnostics();
            QVERIFY(motion.referenceSpeedSI>0);
            for(const auto &p:frame->particles) {
                auto [it,inserted]=traces.try_emplace(p.id,Trace{p.position,frame->clock-p.age,p.lifetime,p.age,p.position.x()});
                auto &trace=it->second;QCOMPARE(p.lifetime,trace.life);waiting+=p.waitingForData;
                const double nativeSpeed=!quad && trace.start.x()>=cols*.5?2.:1.;
                // Panning changes the normalized reference and its smoothed
                // gain. Retained particles follow that display mapping too.
                const double displayedSpeed=motion.motionGain*motion.referenceSpeedSI
                    *std::pow(nativeSpeed/motion.referenceSpeedSI,motion.speedExponent);
                trace.expectedX+=displayedSpeed*(p.age-trace.lastAge);trace.lastAge=p.age;
                maxError=std::max(maxError,std::abs(p.position.x()-trace.expectedX));
                if(p.stop==FlowParticleStop::None) {
                    QVERIFY2(std::abs(p.age-(frame->clock-trace.born))<1e-6,"A cache boundary froze the assigned life");
                    QVERIFY(!p.waitingForData);
                } else {
                    QCOMPARE(p.stop,FlowParticleStop::Lifetime);QVERIFY(std::abs(p.age-p.lifetime)<1e-6);
                    if(!deaths.contains(p.id)) {deaths.insert(p.id);++witnessedDeaths;}
                }
                QVERIFY(!p.historyLimited);QVERIFY(p.history.size()<=4096);
            }
            maxWaiting=std::max(maxWaiting,waiting);aliveAtEnd=int(frame->particles.size());
            QVERIFY(layer.flowParticleFieldBytes()<=128*1024*1024);
        }
        const auto final=layer.flowParticles(view,scale,layer.frameRevision());QVERIFY(final);
        QVERIFY(witnessedDeaths>5);QVERIFY(final->deaths>5);QVERIFY(aliveAtEnd>10);QVERIFY2(maxError<1e-4,qPrintable(QString("trajectory error %1").arg(maxError)));
        QCOMPARE(raw->reads,fullReads);QVERIFY(layer.flowParticleFieldCaptures()>3);
        QVERIFY(layer.flowParticleFieldCaptures()<450);QVERIFY(layer.flowParticleFieldReads()<450);
        QJsonObject report{{"nativeCells",raw->triangleCount()},{"cellType",quad?"quad":"triangle"},
            {"animationSeconds",final->clock},{"testWallMs",timer.elapsed()},{"births",qint64(final->births)},
            {"assignedLifetimeDeaths",qint64(final->deaths)},{"witnessedDeaths",witnessedDeaths},
            {"maximumWaiting",maxWaiting},{"trajectoryError",maxError},{"particlesAtEnd",aliveAtEnd},
            {"fieldCaptures",layer.flowParticleFieldCaptures()},{"fieldSubsetReads",layer.flowParticleFieldReads()},
            {"fieldWorkingUpperBoundBytes",qint64(layer.flowParticleFieldBytes())},{"fullReadsAfterLoad",raw->reads-fullReads}};
        const QString folder=qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT","workplans/artifacts/flow_particle_residency_2026-10-13");QDir().mkpath(folder);
        QFile output(folder+QString(large?"/6m-lifetimes-%1.json":"/lifetimes-%1.json").arg(quad?"quads":"triangles"));
        QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument(report).toJson());
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

        sub->streakStyle()->setMinimumLifetimeSeconds(20);
        sub->streakStyle()->setMaximumLifetimeSeconds(30);
        sub->streakStyle()->setLifetimeFadeSeconds(1.5);
        sub->streakStyle()->setTrailDurationSeconds(3.5);
        sub->streakStyle()->setColor(QColor(20,40,80,170));sub->streakStyle()->setSpeedMultiplier(12);
        // Explicit saved settings, including the previous defaults, must win
        // over the calmer defaults when an existing project is reopened.

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

    void continuousLongTrailKeepsHistoryAndPopulationBounded() {
        SWMM2DResultsLayer layer;auto source=std::make_unique<FlowSource>();source->speed=.0001f;
        layer.setSource(std::move(source));
        auto *sub=layer.flowStreakSublayer();sub->setVisible(true);
        auto *style=sub->streakStyle();style->setSpeedMultiplier(.0001/48);
        style->setMinimumLifetimeSeconds(600);style->setMaximumLifetimeSeconds(3600);
        // Far beyond int range after conversion to samples: must clamp BEFORE
        // casting, keeping the requested duration rather than wrapping it.
        style->setTrailDurationSeconds(FlowStreakStyle::maximumNumericValue());
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto initial=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(initial);
        layer.advanceFlowStreaks(120);QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox,1));
        const auto frame=layer.flowParticles(layer.m_sceneBBox,1,layer.frameRevision());QVERIFY(frame);
        QVERIFY(!frame->particles.empty());QCOMPARE(frame->deaths,frame->stopCounts[size_t(FlowParticleStop::Lifetime)]);
        int survivors=0;
        for(const auto &p:frame->particles) {
            QVERIFY(p.lifetime>=600 && p.lifetime<=3600);QVERIFY(p.history.size()<=4096);
            QVERIFY(!p.historyLimited);
            const auto before=std::find_if(initial->particles.begin(),initial->particles.end(),[&](const auto &old){return old.id==p.id;});
            if(before==initial->particles.end()) {QCOMPARE(p.history.front().age,0.);continue;}
            QCOMPARE(p.history.front().age,before->history.front().age);
            if(p.stop==FlowParticleStop::None) {++survivors;QVERIFY(std::abs(p.age-before->age-120)<1e-7);}
            else {QCOMPARE(p.stop,FlowParticleStop::Lifetime);QCOMPARE(p.age,p.lifetime);}
            bool reduced=false;for(const auto &sample:p.history)reduced|=sample.error>=0;QVERIFY(reduced);
        }
        QVERIFY(survivors>0);
        QCOMPARE(style->trailDurationSeconds(),FlowStreakStyle::maximumNumericValue());
        // The long-tail history allocation still reduces the maximum pool.
        QVERIFY(frame->particles.size()<=size_t((96*1024*1024)/((6*sizeof(FlowParticleSample)+6*2*sizeof(FlowParticleVertex))*4096)));
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
        // Automatic halo and manifold rings stay within the regional budget.

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
 // fixed accuracy-comparison footprint
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
        auto *motion=app.findChild<QCheckBox*>("flowStreakMotion");QVERIFY(motion);
        QTRY_VERIFY(motion->isEnabled());QVERIFY(!motion->isChecked());
        motion->setChecked(true);QVERIFY(reopened->streakStyle()->animated());
        reopened->streakStyle()->setAnimated(false);QVERIFY(!motion->isChecked());
        for(auto *window:app.findChildren<SWMMVisProjectWindow*>())window->setHasChanges(false);
    }

};
QTEST_MAIN(FlowStreaksTest)
#include "test_flowstreaks.moc"
