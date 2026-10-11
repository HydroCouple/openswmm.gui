// GPL-3.0-or-later
#include "render/flowstreakpaths.h"
#include "render/flowstreakcache.h"
#include "mesh/meshcellgeom.h"
#include <QTest>
#include <QTimer>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <limits>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace OpenSWMM::Render;

static FlowStreakInput square()
{
    FlowStreakInput in;in.mesh=std::make_shared<FlowStreakMesh>();
    in.mesh->vertices={{0,0},{10,0},{10,10},{0,10}};
    in.mesh->triangles={{0,1,2},{0,2,3}};
    in.mesh->grid.rebuild({QRectF(0,0,10,10),QRectF(0,0,10,10)});
    in.triangles={0,1};in.fields.resize(2);
    for(auto &f:in.fields) {f.vx=2;f.speed=2;f.depth={1,1,1};f.exits=7;}
    in.coverage={0,0,10,10};in.spacing=1;in.step=.25;in.maxLength=8;
    return in;
}
static std::shared_ptr<FlowStreakMesh> regularMesh(int cols,int rows,QPointF origin={})
{
    auto mesh=std::make_shared<FlowStreakMesh>();
    mesh->vertices.reserve((cols+1)*(rows+1));mesh->triangles.reserve(size_t(cols)*rows*2);
    for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x)mesh->vertices.append(origin+QPointF(x,y));
    auto id=[cols](int x,int y){return y*(cols+1)+x;};
    QVector<QRectF> boxes;boxes.reserve(cols*rows*2);
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
        mesh->triangles.push_back({id(x,y),id(x+1,y),id(x+1,y+1)});
        mesh->triangles.push_back({id(x,y),id(x+1,y+1),id(x,y+1)});
        boxes.append(QRectF(origin+QPointF(x,y),QSizeF(1,1)));boxes.append(boxes.back());
    }
    mesh->grid.rebuild(boxes);
    return mesh;
}
static FlowStreakInput regionInput(const std::shared_ptr<FlowStreakMesh> &mesh,QRectF coverage)
{
    FlowStreakInput in;in.mesh=mesh;in.coverage=coverage;in.spacing=2;in.step=.5;in.maxLength=8;
    in.fields.resize(mesh->triangles.size());in.triangles.reserve(in.fields.size());
    for(size_t i=0;i<in.fields.size();++i) {
        in.triangles.push_back(int(i));auto &f=in.fields[i];
        f.vx=2;f.speed=2;f.depth={1,1,1};f.exits=7;
    }
    return in;
}
// Test adapter borrows the same immutable native mesh/fields as the worker.
static FlowStreakInput sampledInput(const FlowStreakInput &source)
{
    std::atomic_bool cancel=false;source.mesh->prepare(cancel);
    auto data=std::make_shared<FlowStreakInput>(source);
    FlowStreakInput in=source;in.mesh.reset();in.triangles.clear();in.fields.clear();
    in.sampler.valid=[]{return true;};
    in.sampler.cell=[data](int i,FlowStreakSample &s) {
        auto it=std::lower_bound(data->triangles.begin(),data->triangles.end(),i);
        if(it==data->triangles.end() || *it!=i)return false;
        s.id=i;s.field=data->fields[size_t(it-data->triangles.begin())];s.neighbors=data->mesh->neighbors[size_t(i)];
        for(int k=0;k<3;++k)s.vertices[k]=data->mesh->vertices[data->mesh->triangles[size_t(i)][k]];
        return true;
    };
    in.sampler.locate=[data,cell=in.sampler.cell](QPointF &p,FlowStreakSample &s) {
        const int *b=nullptr,*e=nullptr;data->mesh->grid.candidatesAtPoint(p.x(),p.y(),b,e);
        const auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
        for(auto it=b;it!=e;++it)if(cell(*it,s)) {
            const auto a=s.vertices[0],ab=s.vertices[1]-a,ac=s.vertices[2]-a;
            const double v=cross(p-a,ac)/cross(ab,ac),w=cross(ab,p-a)/cross(ab,ac);
            if(v>=-1e-9 && w>=-1e-9 && 1-v-w>=-1e-9 && s.field.speed>1e-8
               && (1-v-w)*s.field.depth[0]+v*s.field.depth[1]+w*s.field.depth[2]>data->cutoff) return true;
        }return false;
    };
    return in;
}
static double peakResidentBytes()
{
#ifdef Q_OS_UNIX
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) {
#ifdef Q_OS_MACOS
        return double(usage.ru_maxrss);
#else
        return double(usage.ru_maxrss)*1024;
#endif
    }
#endif
    return 0;
}
class FlowStreakPathsTest:public QObject {
    Q_OBJECT
private slots:
    void velocityUnits() {
        for(int nv:{3,4}) for(double depth:{.25,1.,2.}) for(double speed:{-2.,2.}) {
            float len[4]={1,1,1,1},nx[4]={1,0,-1,0},ny[4]={0,1,0,-1},flux[4];
            if(nv==3) {nx[1]=-.6;ny[1]=.8;nx[2]=-.6;ny[2]=-.8;}
            for(int e=0;e<nv;++e)flux[e]=float(depth*speed*nx[e]*len[e]);
            double u=0,v=0;
            QVERIFY(mesh::cellVelocity(0,nv,flux,len,nx,ny,depth,.001,u,v));
            QVERIFY(std::abs(u-speed)<1e-6);QVERIFY(std::abs(v)<1e-6);
            QVERIFY(!mesh::cellVelocity(0,nv,flux,len,nx,ny,0,0,u,v));QCOMPARE(u,0.);
            QVERIFY(!mesh::cellVelocity(0,nv,flux,len,nx,ny,std::numeric_limits<double>::quiet_NaN(),0,u,v));
        }
    }
    void uniformAdvectionAndDeterminism() {
        auto in=square();std::atomic_bool cancel=false;
        const auto a=buildFlowStreakPaths(in,cancel),b=buildFlowStreakPaths(in,cancel);
        QVERIFY(a.paths.size()>20);QCOMPARE(a.paths.size(),b.paths.size());
        for(size_t i=0;i<a.paths.size();++i) {
            const auto &p=a.paths[i];QCOMPARE(p.points.size(),b.paths[i].points.size());
            for(size_t j=0;j<p.points.size();++j) {
                const auto &pt=p.points[j];QCOMPARE(pt.position,b.paths[i].points[j].position);
                QVERIFY(std::abs(pt.position.y()-p.points[0].position.y())<1e-8);
                QVERIFY(std::abs(pt.position.x()-p.points[0].position.x()-2*pt.seconds)<1e-5);
                QVERIFY(in.coverage.contains(pt.position));
            }
        }
    }
    void exactShorelineAndMissingData() {
        auto in=square();std::atomic_bool cancel=false;
        in.fields[0].depth={1,-1,-1};in.fields[1].depth={1,-1,1};
        auto batch=buildFlowStreakPaths(in,cancel);QVERIFY(!batch.paths.empty());
        for(const auto &p:batch.paths)for(const auto &pt:p.points)QVERIFY(pt.position.x()<=5+1e-8);
        for(auto &f:in.fields)f.depth.fill(std::numeric_limits<float>::quiet_NaN());
        QVERIFY(buildFlowStreakPaths(in,cancel).paths.empty());
    }
    void blockedEdgesAndDisconnectedMesh() {
        auto in=square();std::atomic_bool cancel=false;
        for(auto &f:in.fields)f.exits=0;
        auto batch=buildFlowStreakPaths(in,cancel);QVERIFY(!batch.paths.empty());
        for(const auto &p:batch.paths) {
            const bool above=p.points.front().position.y()>p.points.front().position.x();
            for(const auto &pt:p.points)if(above)QVERIFY(pt.position.y()+1e-8>=pt.position.x());
        }
        in.mesh->triangles.push_back({0,2,3});in.mesh->neighbors.clear();in.mesh->prepare(cancel);
        QCOMPARE(in.mesh->neighbors[0][1],-1); // non-manifold diagonal must not connect
    }
    void budgetAndCancellation() {
        auto in=square();in.maxPoints=100;in.maxPaths=5;std::atomic_bool cancel=false;
        auto b=buildFlowStreakPaths(in,cancel);QVERIFY(b.pointCount()<=100);QVERIFY(b.paths.size()<=5);
        cancel=true;QVERIFY(buildFlowStreakPaths(in,cancel).paths.empty());
        QVERIFY(FlowStreakCache::fitsBudget(1u<<20,10000));
        QVERIFY(!FlowStreakCache::fitsBudget(192u<<20,1));
        QVERIFY(!FlowStreakCache::fitsBudget(1u<<20,10000000));
        cancel=false;in.maxPaths=0;QVERIFY(buildFlowStreakPaths(in,cancel).paths.empty());
    }
    void seedOverlapAndLargeOrigin() {
        auto in=square();std::atomic_bool cancel=false;
        in.seedBudgetArea=100;in.maxPaths=30;
        const auto full=buildFlowStreakPaths(in,cancel);
        in.coverage={0,0,8,10};const auto clipped=buildFlowStreakPaths(in,cancel);
        int matched=0;
        for(const auto &a:clipped.paths)for(const auto &b:full.paths)
            if(a.points.front().position==b.points.front().position) {++matched;QCOMPARE(a.offset,b.offset);break;}
        QCOMPARE(matched,int(clipped.paths.size()));
        const QPointF origin(7000000,-4000000);
        for(auto &v:in.mesh->vertices)v+=origin;
        in.mesh->grid.rebuild({QRectF(origin,QSizeF(10,10)),QRectF(origin,QSizeF(10,10))});
        in.coverage.translate(origin);const auto big=buildFlowStreakPaths(in,cancel);
        QVERIFY(!big.paths.empty());
        for(const auto &p:big.paths)for(const auto &v:p.points)
            QVERIFY(std::abs(v.position.x()-p.points.front().position.x()-2*v.seconds)<1e-5);
    }
    void phaseHasNoWrappingConnector() {
        QCOMPARE(flowStreakOpacity(9,10,2,0,0,true),0.);
        QCOMPARE(flowStreakOpacity(0,10,2,0,1,true),.5);
        QCOMPARE(flowStreakOpacity(5,10,2,0,1,false),0.);
        QCOMPARE(flowStreakOpacity(0,10,2,0,13,true),.5);
    }
    void opacityHasSoftHeadAndLifetime() {
        QCOMPARE(flowStreakOpacity(0,10,2,0,0,true),0.);
        QVERIFY(flowStreakOpacity(0,10,2,0,.001,true)<.001);
        QVERIFY(flowStreakOpacity(5,10,2,0,5.001,true)<.001);
        QCOMPARE(flowStreakOpacity(5,10,2,0,5.001,false),1.);
        // No brightness discontinuity at birth, a moving head, or cycle wrap.
        for(double t:{0.,.5,5.,10.})for(int i=1;i<=2400;++i) {
            const auto a=flowStreakOpacity(t,10,2,0,(i-1)*.01,true);
            const auto b=flowStreakOpacity(t,10,2,0,i*.01,true);
            QVERIFY(a>=0 && a<=1);QVERIFY(std::abs(a-b)<.08);
        }
        QCOMPARE(flowStreakAppearance(-1,0),1.);
        QCOMPARE(flowStreakAppearance(2,2),0.);
        QCOMPARE(flowStreakAppearance(2,2.125),.5);
        QCOMPARE(flowStreakAppearance(2,2.25),1.);
    }
    void playbackPreservesHeadsWithoutOldGeometry() {
        auto make=[](float duration,quint64 seed=42) {
            auto b=std::make_shared<FlowStreakBatch>();FlowStreakPath p;p.seed=seed;p.length=100;
            for(int i=0;i<=10;++i)p.points.push_back({{i*10.,0},i*duration/10,100/duration});
            b->paths.push_back(p);return b;
        };
        FlowStreakPlayback playback;
        const auto first=make(100);QCOMPARE(playback.present(first,47,0,20,1,true),first);
        const auto faster=make(50);const auto next=playback.present(faster,47,1,20,1,true);
        const auto &p=next->paths.front();QCOMPARE(p.appearedAt,-1.f);
        QVERIFY(std::abs(flowStreakHead(50,10,p.offset,47)*2-47)<1e-5);
        QVERIFY(std::abs(flowStreakHead(50,10,p.offset,48)*2-49)<1e-5);
        QCOMPARE(playback.present(faster,48,2,20,1,true),next); // no per-tick copy
        QCOMPARE(faster->paths.front().offset,0.f); // immutable cache untouched
        // A new dry boundary truncates the valid path before the old head.
        auto shortened=make(10);shortened->paths.front().length=20;
        for(auto &pt:shortened->paths.front().points)pt.position*=.2;
        const auto cut=playback.present(shortened,48,2,20,1,true);
        QCOMPARE(cut->paths.front().appearedAt,2.f);
        for(const auto &pt:cut->paths.front().points)QVERIFY(pt.position.x()<=20);
        const auto settled=playback.present(shortened,48,2.1,20,1,false);
        QCOMPARE(settled->paths.front().appearedAt,-1.f);
        QCOMPARE(playback.present(shortened,48,2.1,20,1,true),settled); // resume cannot fade backwards
        const auto empty=std::make_shared<FlowStreakBatch>();
        QVERIFY(playback.present(empty,48,2,20,1,true)->paths.empty());
        const auto fresh=playback.present(first,48,3,20,1,true);
        QCOMPARE(fresh->paths.front().appearedAt,3.f);
        // Reverse direction must not inherit an old head from the opposite flow.
        auto reverse=make(100);for(auto &pt:reverse->paths.front().points)pt.position*=-1;
        QCOMPARE(playback.present(reverse,49,4,20,1,true)->paths.front().appearedAt,4.f);
        playback.clear();QCOMPARE(playback.present(first,49,5,20,1,true),first);
        auto other=make(100,123);
        QCOMPARE(playback.present(other,49,5,20,1,false)->paths.front().appearedAt,-1.f);
        // Preserve the remaining tail when the head has already left the path.
        playback.clear();playback.present(first,110,0,20,1,true);
        const auto draining=playback.present(faster,110,1,20,1,true);
        QVERIFY(std::abs(flowStreakHead(50,10,draining->paths.front().offset,110)-55)<1e-5);
    }
    void workerCacheCoalescingAndInvalidation() {
        FlowStreakCache cache;int captures=0;
        auto capture=[&]{++captures;return square();};
        cache.request("first",capture);cache.request("first",capture);QCOMPARE(captures,1);
        for(int i=0;i<20;++i) cache.request(QByteArray::number(i),capture);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(),5000);
        QVERIFY(cache.lookup("19"));QVERIFY(cache.builds()<=2);
        const int before=cache.builds();cache.request("19",capture);QCOMPARE(cache.builds(),before);
        QVERIFY(cache.cachedBytes()<(32u<<20));cache.clear();QVERIFY(!cache.lookup("19"));
        cache.request("cancel",capture);cache.clear();QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(),5000);
        QVERIFY(!cache.lookup("cancel"));
    }
    void captureWaitSettlesOnlyWorkerEvents() {
        FlowStreakCache cache;
        bool unrelatedEvent=false;
        QTimer::singleShot(0,this,[&]{unrelatedEvent=true;});
        cache.request("old",[]{return square();});
        cache.request("capture",[]{return square();});
        cache.waitForDone();
        QVERIFY(!cache.busy());QVERIFY(cache.lookup("capture"));
        QVERIFY(!unrelatedEvent);
        const int builds=cache.builds();QCoreApplication::processEvents();
        QVERIFY(unrelatedEvent);QCOMPARE(cache.builds(),builds);
        cache.request("cancel",[]{return square();});cache.clear();
        cache.waitForDone();QVERIFY(!cache.lookup("cancel"));
    }
    void sampledNativeParityAndBarriers() {
        for(int scenario=0;scenario<5;++scenario) {
            auto source=square();
            if(scenario==1)for(auto &f:source.fields)f.exits=0;
            if(scenario==2){source.fields[0].depth={1,-1,-1};source.fields[1].depth={1,-1,1};}
            if(scenario==3){source.fields[0].vx=-2;source.fields[1].vx=2;}
            if(scenario==4){source.mesh->triangles.push_back({0,2,3});source.mesh->neighbors.clear();}
            std::atomic_bool cancel=false;const auto expected=buildFlowStreakPaths(source,cancel);
            const auto input=sampledInput(source);FlowStreakTracer trace(input,input.sampler);
            while(!trace.advance()) {}const auto actual=trace.take();
            QCOMPARE(actual.paths.size(),expected.paths.size());QCOMPARE(actual.pointCount(),expected.pointCount());
            for(size_t i=0;i<actual.paths.size();++i) {
                QCOMPARE(actual.paths[i].offset,expected.paths[i].offset);
                QCOMPARE(actual.paths[i].points.size(),expected.paths[i].points.size());
                for(size_t j=0;j<actual.paths[i].points.size();++j) {
                    QCOMPARE(actual.paths[i].points[j].position,expected.paths[i].points[j].position);
                    QCOMPARE(actual.paths[i].points[j].seconds,expected.paths[i].points[j].seconds);
                }
            }
        }
    }
    void overviewSlicesCoalesceAndDiscardStaleFrames() {
        FlowStreakCache cache;bool current=true;
        auto input=sampledInput(square());input.sampler.valid=[&]{return current;};
        cache.request("stale",[&]{return input;});QVERIFY(cache.busy());
        // The request itself performs no tracing, and a queued user action can
        // run before completion. Changing the hydraulic frame cancels publication.
        bool event=false;QTimer::singleShot(0,this,[&]{event=true;current=false;});
        QTRY_VERIFY_WITH_TIMEOUT(!cache.busy(),5000);QVERIFY(event);QVERIFY(!cache.lookup("stale"));
        current=true;cache.request("first",[&]{return input;});
        cache.request("middle",[&]{return input;});cache.request("latest",[&]{return input;});
        cache.waitForDone();QVERIFY(!cache.lookup("first"));QVERIFY(!cache.lookup("middle"));
        QVERIFY(cache.lookup("latest"));QVERIFY(!cache.lookup("latest")->paths.empty());
        event=false;QTimer::singleShot(0,this,[&]{event=true;});
        cache.request("capture",[&]{return input;});cache.waitForDone();
        QVERIFY(cache.lookup("capture"));QVERIFY(!event);
        cache.request("cancel",[&]{return input;});cache.clear();cache.waitForDone();
        QVERIFY(!cache.busy());QVERIFY(!cache.lookup("cancel"));QCoreApplication::processEvents();QVERIFY(event);
    }
    void overviewWorkCaps() {
        auto in=sampledInput(square());in.coverage={0,0,1e8,.00001};in.spacing=.00001;
        int queries=0;in.sampler.locate=[&](QPointF &,FlowStreakSample &){++queries;return false;};
        FlowStreakTracer trace(in,in.sampler);while(!trace.advance()) {}auto batch=trace.take();
        QVERIFY(batch.seedAttempts<=8192);QVERIFY(queries<=8192);QVERIFY(batch.paths.empty());
        in=sampledInput(square());in.maxPoints=75;in.maxPaths=3;
        FlowStreakTracer limited(in,in.sampler);while(!limited.advance()) {}batch=limited.take();
        QVERIFY(batch.pointCount()<=75);QVERIFY(batch.paths.size()<=3);
    }
    void pointBudgetSpreadsSeedsAcrossView() {
        auto in=sampledInput(square());in.spacing=.05;in.maxPaths=2000;in.maxPoints=1000;
        FlowStreakTracer trace(in,in.sampler);while(!trace.advance()) {}const auto batch=trace.take();
        QVERIFY(batch.limited);QVERIFY(batch.pointCount()<=1000);
        bool upper=false,lower=false;
        for(const auto &path:batch.paths) {
            upper|=path.points.front().position.y()<3;lower|=path.points.front().position.y()>7;
        }
        QVERIFY(upper);QVERIFY(lower);
    }
    void regionalPathsMatchWholeMesh() {
        const QPointF origin(7000000,-4000000);
        auto source=regularMesh(100,100,origin);
        const QRectF coverage(origin+QPointF(43,44),QSizeF(12,10));
        auto local=FlowStreakMesh::captureRegion(source->vertices,source->triangles,source->grid,coverage);
        QVERIFY(local);QVERIFY(local->triangles.size()<source->triangles.size()/2);
        QVERIFY(local->vertices.size()<source->vertices.size()/2);QVERIFY(local->grid.seen.empty());
        for(size_t i=0;i<local->triangles.size();++i) for(int k=0;k<3;++k)
            QCOMPARE(local->vertices[local->triangles[i][k]],
                     source->vertices[source->triangles[size_t(local->sourceTriangles[i])][k]]);
        std::atomic_bool cancel=false;
        const auto expected=buildFlowStreakPaths(regionInput(source,coverage),cancel);
        const auto actual=buildFlowStreakPaths(regionInput(local,coverage),cancel);
        QVERIFY(!actual.paths.empty());QCOMPARE(actual.paths.size(),expected.paths.size());
        for(size_t i=0;i<actual.paths.size();++i) {
            QCOMPARE(actual.paths[i].points.size(),expected.paths[i].points.size());
            for(size_t j=0;j<actual.paths[i].points.size();++j) {
                QCOMPARE(actual.paths[i].points[j].position,expected.paths[i].points[j].position);
                QCOMPARE(actual.paths[i].points[j].seconds,expected.paths[i].points[j].seconds);
            }
        }
        // Captures own their regional data: source replacement cannot alter
        // coordinates or the point index while the worker is tracing paths.
        source->vertices.fill({-100,-100});source->triangles.clear();source->grid.clear();
        QCOMPARE(buildFlowStreakPaths(regionInput(local,coverage),cancel).pointCount(),actual.pointCount());
    }
    void regionalCaptureRetainsDryNonManifoldFace() {
        auto source=square();
        // The third face is dry and mostly outside the requested region.
        // Its shared diagonal must still prevent the two wet faces joining.
        source.mesh->vertices.append({-100,100});
        source.mesh->triangles.push_back({0,2,4});
        source.mesh->grid.rebuild({{0,0,10,10},{0,0,10,10},{-100,0,110,100}});
        const QRectF coverage(4,4,2,2);
        auto mesh=FlowStreakMesh::captureRegion(source.mesh->vertices,source.mesh->triangles,
                                               source.mesh->grid,coverage);
        QVERIFY(mesh);QCOMPARE(mesh->sourceTriangles,(std::vector<int>{0,1,2}));
        auto in=regionInput(mesh,coverage);in.triangles.resize(2);in.fields.resize(2);
        std::atomic_bool cancel=false;mesh->prepare(cancel);
        QCOMPARE(mesh->neighbors[0][1],-1);QCOMPARE(mesh->neighbors[1][2],-1);
    }
    void regionalCaptureLimitAndRecovery() {
        auto source=regularMesh(400,400);
        QVERIFY(!FlowStreakMesh::captureRegion(source->vertices,source->triangles,source->grid,{0,0,400,400}));
        auto mesh=FlowStreakMesh::captureRegion(source->vertices,source->triangles,source->grid,{190,190,20,20});
        QVERIFY(mesh);QVERIFY(mesh->triangles.size()<10000);
        FlowStreakCache cache;
        cache.request("overview",[]{return FlowStreakInput{};});
        QVERIFY(!cache.busy());QVERIFY(cache.status().contains("This view"));
        cache.request("zoom",[&]{return regionInput(mesh,{190,190,20,20});});
        cache.waitForDone();QVERIFY(cache.lookup("zoom"));
        QVERIFY(!cache.lookup("zoom")->paths.empty());
    }
    void regionalScaleBenchmark() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_SCALE_6M"))
            QSKIP("Optional 6M native triangles / 6M quads (12M display triangles) benchmark");
        QJsonArray cases;
        struct Case {int cols,rows;bool quads;};
        for(const auto c:{Case{100,100,false},Case{895,895,false},Case{2000,1500,false},Case{3000,2000,true}}) {
            QElapsedTimer clock;clock.start();auto source=regularMesh(c.cols,c.rows);
            const double fixtureMs=clock.nsecsElapsed()/1e6;
            const int displayTriangles=int(source->triangles.size());
            const QRectF coverage(c.cols*.5-25,c.rows*.5-25,50,50);
            QJsonArray runs;
            for(int run=0;run<3;++run) {
                clock.restart();auto mesh=FlowStreakMesh::captureRegion(source->vertices,source->triangles,source->grid,coverage);
                const double captureMs=clock.nsecsElapsed()/1e6;
                QVERIFY(mesh);QVERIFY(mesh->triangles.size()<20000);
                QVERIFY(mesh->bytes()<(size_t(4)<<20));
                clock.restart();auto in=regionInput(mesh,coverage);
                const double fieldsMs=clock.nsecsElapsed()/1e6;
                QVERIFY(FlowStreakCache::fitsBudget(mesh->bytes(),in.fields.size()));
                std::atomic_bool cancel=false;clock.restart();const auto batch=buildFlowStreakPaths(in,cancel);
                const double coldMs=clock.nsecsElapsed()/1e6;
                QVERIFY(!batch.paths.empty());QVERIFY(batch.pointCount()<=in.maxPoints);
                clock.restart();const auto warm=buildFlowStreakPaths(in,cancel);
                const double warmMs=clock.nsecsElapsed()/1e6;QCOMPARE(warm.pointCount(),batch.pointCount());
                runs.append(QJsonObject{{"captureMs",captureMs},{"fieldsMs",fieldsMs},{"coldPathMs",coldMs},
                    {"warmPathMs",warmMs},{"regionalTriangles",int(mesh->triangles.size())},
                    {"regionalMeshPeakBytes",double(mesh->bytes())},{"fieldBytes",double(in.fields.capacity()*sizeof(FlowStreakField))},
                    {"pathBytes",double(batch.bytes())},{"paths",int(batch.paths.size())},{"points",batch.pointCount()}});
            }
            QJsonArray coverageRuns;
            for(double fraction:{.01,.1,1.}) {
                const double side=std::sqrt(fraction);
                const QRectF view(c.cols*(1-side)/2,c.rows*(1-side)/2,c.cols*side,c.rows*side);
                clock.restart();auto mesh=FlowStreakMesh::captureRegion(source->vertices,source->triangles,source->grid,view);
                const double ms=clock.nsecsElapsed()/1e6;
                if(fraction==1. && displayTriangles>FlowStreakMesh::maxRegionTriangles) QVERIFY(!mesh);
                coverageRuns.append(QJsonObject{{"areaFraction",fraction},{"captureMs",ms},{"accepted",bool(mesh)},
                    {"regionalTriangles",mesh?int(mesh->triangles.size()):0}});
            }
            cases.append(QJsonObject{{"nativeCells",c.quads?displayTriangles/2:displayTriangles},
                {"cellType",c.quads?"quads split for display":"triangles"},{"displayTriangles",displayTriangles},
                {"fixtureMs",fixtureMs},{"processLifetimePeakResidentBytes",peakResidentBytes()},
                {"legacyTopologyEstimateBytes",double(displayTriangles)*60},
                {"legacyTopologyAloneFitsBudget",FlowStreakCache::fitsBudget(size_t(displayTriangles)*60,0)},
                {"fixedViewRuns",runs},{"coverageRuns",coverageRuns}});
        }
        QDir().mkpath("workplans/artifacts/flow_streaks");
        QFile f("workplans/artifacts/flow_streaks/regional-scale-6m.json");QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"qt",qVersion()},{"host",QSysInfo::prettyProductName()},
            {"architecture",QSysInfo::currentCpuArchitecture()},{"cases",cases}}).toJson());
    }
    void scaleBenchmark() {
        if(!qEnvironmentVariableIsSet("FLOW_STREAK_SCALE")) QSKIP("Optional 20k/160k/1.6M triangle benchmark");
        QJsonArray cases;
        for(int n:{100,283,895}) {
            QElapsedTimer clock;clock.start();
            FlowStreakInput in;in.mesh=std::make_shared<FlowStreakMesh>();auto &mesh=*in.mesh;
            for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)mesh.vertices.append(QPointF(x,y));
            auto id=[n](int x,int y){return y*(n+1)+x;};
            QVector<QRectF> boxes;
            for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
                mesh.triangles.push_back({id(x,y),id(x+1,y),id(x+1,y+1)});
                mesh.triangles.push_back({id(x,y),id(x+1,y+1),id(x,y+1)});
                boxes.append(QRectF(x,y,1,1));boxes.append(QRectF(x,y,1,1));
            }
            mesh.grid.rebuild(boxes);boxes.clear();boxes.squeeze();
            const double snapshotMs=clock.nsecsElapsed()/1e6;
            in.coverage=QRectF(n*.45,n*.45,n*.1,n*.1);in.spacing=n/300.;in.step=n/2000.;in.maxLength=n*.016;
            const auto ids=mesh.grid.query(in.coverage);in.triangles.assign(ids.begin(),ids.end());
            std::sort(in.triangles.begin(),in.triangles.end());
            in.fields.resize(in.triangles.size());for(auto &f:in.fields){f.vx=2;f.speed=2;f.depth={1,1,1};f.exits=7;}
            QVERIFY(FlowStreakCache::fitsBudget(mesh.bytes(),in.fields.size()));
            QJsonArray runs;std::atomic_bool cancel=false;
            for(int i=0;i<3;++i) {
                clock.restart();auto batch=buildFlowStreakPaths(in,cancel);
                const double elapsed=clock.nsecsElapsed()/1e6;
                QVERIFY(!batch.paths.empty());QVERIFY(batch.pointCount()<=in.maxPoints);
                runs.append(QJsonObject{{"ms",elapsed},{"points",batch.pointCount()},{"paths",int(batch.paths.size())},
                    {"pathBytes",double(batch.bytes())}});
            }
            cases.append(QJsonObject{{"triangles",int(mesh.triangles.size())},{"coveredTriangles",int(in.fields.size())},
                {"snapshotMs",snapshotMs},{"meshPeakBytes",double(mesh.bytes())},{"runs",runs},
                {"overviewFitsBudget",FlowStreakCache::fitsBudget(mesh.bytes(),mesh.triangles.size())}});
        }
        QDir().mkpath("workplans/artifacts/flow_streaks");QFile f("workplans/artifacts/flow_streaks/path-scale.json");
        QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(cases).toJson());
    }
};
QTEST_GUILESS_MAIN(FlowStreakPathsTest)
#include "test_flowstreakpaths.moc"
