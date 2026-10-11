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
            QCOMPARE(p.offset,b.paths[i].offset);
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
            if(a.points.front().position==b.points.front().position) {
                ++matched;QCOMPARE(a.offset,b.offset);break;
            }
        QCOMPARE(matched,int(clipped.paths.size()));
        const QPointF origin(7000000,-4000000);
        for(auto &v:in.mesh->vertices)v+=origin;
        in.mesh->grid.rebuild({QRectF(origin,QSizeF(10,10)),QRectF(origin,QSizeF(10,10))});
        in.coverage.translate(origin);const auto big=buildFlowStreakPaths(in,cancel);
        QVERIFY(!big.paths.empty());
        for(const auto &p:big.paths)for(const auto &v:p.points)
            QVERIFY(std::abs(v.position.x()-p.points.front().position.x()-2*v.seconds)<1e-5);
    }
    void opacityHasSoftHeadAndLifetime() {
        QCOMPARE(flowStreakOpacity(9,2,{0,0,12},true,.5),0.);
        QCOMPARE(flowStreakOpacity(0,2,{1,1,11},true,.5),.5);
        QCOMPARE(flowStreakOpacity(5,2,{1,1,11},false,.5),0.);
        QCOMPARE(flowStreakOpacity(0,2,{0,0,12},true,.5),0.);
        QVERIFY(flowStreakOpacity(5,2,{5.001,5.001,6.999},true,.5)<.001);
        QCOMPARE(flowStreakOpacity(5,2,{5.001,5.001,6.999},false,.5),1.);
        // Birth/death fades operate in seconds, separately from the tail fade.
        QCOMPARE(flowStreakOpacity(0,2,{1,.25,10},true,.5),.25);
        QCOMPARE(flowStreakOpacity(0,2,{1,10,.25},true,.5),.25);
        QCOMPARE(flowStreakOpacity(0,2,{1,10,0},true,.5),0.);
        QCOMPARE(flowStreakOpacity(0,2,{1,0,0},true,0),.5);
        QCOMPARE(flowStreakAppearance(-1,0),1.);
        QCOMPARE(flowStreakAppearance(2,2),0.);
        QCOMPARE(flowStreakAppearance(2,2.125),.5);
        QCOMPARE(flowStreakAppearance(2,2.25),1.);
    }
    void randomizedLifetimesAndCadence() {
        std::array<int,8> bins{};
        for(quint64 seed=0;seed<100;++seed) {
            FlowStreakPath path;path.seed=seed;path.points={{{0,0},0,1},{{1000,0},1000,1}};
            const float randomSeed=flowStreakLifetimeSeed(seed);
            path.lifeRemaining=float(flowStreakLifetime(randomSeed,0,8,16));
            ++bins[std::min(7,int((path.lifeRemaining-8)/1))];
            double birth=0;
            // Compare the constant-work schedule with sequential births over
            // many cycles. New lifetimes vary; there is no idle restart gap.
            for(quint32 generation=0;generation<100;++generation) {
                const double lifetime=generation==0?path.lifeRemaining:flowStreakLifetime(randomSeed,generation,8,16);
                QVERIFY(lifetime>=8 && lifetime<=16);
                for(double fraction:{.001,.25,.75,.999})for(double speed:{.5,2.,10.}) {
                    const double elapsed=birth+fraction*lifetime;
                    const auto motion=flowStreakMotion(path,80,elapsed*speed,elapsed,8,16,speed);
                    QCOMPARE(motion.generation,generation);
                    QVERIFY(std::abs(motion.age-fraction*lifetime)<1e-8);
                    QVERIFY(std::abs(motion.remaining-(1-fraction)*lifetime)<1e-8);
                    QVERIFY(std::abs(motion.head-motion.age*speed)<1e-8);
                }
                birth+=lifetime;
            }
            QVERIFY(flowStreakLifetime(randomSeed,0,8,16)!=flowStreakLifetime(randomSeed,2,8,16));
            // Cadence changes only how often the same elapsed-time result is drawn.
            FlowStreakMotion expected=flowStreakMotion(path,80,240.74,120.37,8,16,2);
            for(int interval:{8,16,33,1000}) {
                double clock=0;int milliseconds=120370;
                while(milliseconds>0) {const int dt=std::min(milliseconds,interval);clock+=dt/1000.;milliseconds-=dt;}
                const auto actual=flowStreakMotion(path,80,clock*2,clock,8,16,2);
                QCOMPARE(actual.generation,expected.generation);
                QVERIFY(std::abs(actual.age-expected.age)<1e-8);
            }
        }
        for(int count:bins)QVERIFY(count>0);
    }
    void lifetimePathsExtendWithinBudgetAndRespectBarriers() {
        auto in=square();in.spacing=2;in.maxLength=1;in.minDuration=3;
        std::atomic_bool cancel=false;const auto extended=buildFlowStreakPaths(in,cancel);
        bool longPath=false;
        for(const auto &path:extended.paths) {
            longPath|=path.length>=6;
            QVERIFY(path.points.back().position.x()<=10);
        }
        QVERIFY(longPath);
        const auto actualInput=sampledInput(in);FlowStreakTracer trace(actualInput,actualInput.sampler);
        while(!trace.advance()) {}QCOMPARE(trace.take().pointCount(),extended.pointCount());
        for(auto &f:in.fields)f.exits=0;
        const auto blocked=buildFlowStreakPaths(in,cancel);
        for(const auto &path:blocked.paths) {
            const auto first=path.points.front().position,last=path.points.back().position;
            if(first.y()>first.x())QVERIFY(last.x()<=last.y()+1e-8);
        }
        in.maxPoints=40;in.minDuration=1000;
        const auto bounded=buildFlowStreakPaths(in,cancel);
        QVERIFY(bounded.pointCount()<=40);QVERIFY(bounded.cellVisits<=256000);
    }
    void newAppearancesAreStaggeredAndNotPostponed() {
        std::atomic_bool cancel=false;
        const auto raw=std::make_shared<FlowStreakBatch>(buildFlowStreakPaths(square(),cancel));
        FlowStreakPlayback playback,repeat;
        for(auto *p:{&playback,&repeat})p->present(std::make_shared<FlowStreakBatch>(),0,0,2,1,true);
        const auto first=playback.present(raw,0,1,2,1,true);
        const auto same=repeat.present(raw,0,1,2,1,true);
        std::array<int,7> bins{};
        for(size_t i=0;i<first->paths.size();++i) {
            const auto start=first->paths[i].appearedAt;
            QVERIFY(start>=1 && start<1.35);++bins[std::min(6,int((start-1)/.05))];
            QCOMPARE(start,same->paths[i].appearedAt);
            QCOMPARE(raw->paths[i].appearedAt,-1.f);
            QCOMPARE(flowStreakAppearance(start,1),0.);
            QCOMPARE(flowStreakAppearance(start,1.6),1.);
        }
        for(int count:bins)QVERIFY(count>0);
        // Force incompatible geometry on each update while paths are entering.
        // Only new wet geometry is retained, without restarting every fade.
        for(int frame=1;frame<=4;++frame) {
            auto changed=std::make_shared<FlowStreakBatch>(*raw);
            for(auto &p:changed->paths)for(auto &pt:p.points)pt.position+=QPointF(frame*10,0);
            const auto next=playback.present(changed,frame*.01,1+frame*.01,2,1,true);
            for(size_t i=0;i<next->paths.size();++i) {
                QCOMPARE(next->paths[i].appearedAt,first->paths[i].appearedAt);
                QCOMPARE(next->paths[i].points.front().position,changed->paths[i].points.front().position);
            }
            QCOMPARE(playback.present(changed,frame*.01+.001,1+frame*.01+.001,2,1,true),next);
        }
    }
    void playbackPreservesHeadsAndLifetimeWithoutOldGeometry() {
        auto make=[](float duration,quint64 seed=42) {
            auto b=std::make_shared<FlowStreakBatch>();FlowStreakPath p;p.seed=seed;p.length=100;p.offset=.25;
            for(int i=0;i<=10;++i)p.points.push_back({{i*10.,0},i*duration/10,100/duration});
            b->paths.push_back(p);return b;
        };
        FlowStreakPlayback playback;
        const auto raw=make(100);const auto first=playback.present(raw,0,0,20,1,true,12,12,1);
        QCOMPARE(raw->paths.front().lifeRemaining,0.f); // immutable cache untouched
        const auto faster=make(50);const auto next=playback.present(faster,1,1,20,1,true,12,12,1);
        const auto &p=next->paths.front();QCOMPARE(p.appearedAt,-1.f);
        const auto motion=flowStreakMotion(p,10,1,1,12,12,1);
        QVERIFY(std::abs(motion.head*2-7)<1e-5);QCOMPARE(motion.age,7.);QCOMPARE(motion.remaining,5.);
        QVERIFY(std::abs(flowStreakMotion(p,10,2,2,12,12,1).head*2-9)<1e-5);
        QCOMPARE(playback.present(faster,2,2,20,1,true,12,12,1),next); // no per-tick copy
        // Editing visual speed preserves the current head and scheduled death.
        const auto changedSpeed=playback.present(faster,2,2,20,1,true,12,12,3);
        const auto moving=flowStreakMotion(changedSpeed->paths.front(),10,5,3,12,12,3);
        QCOMPARE(moving.age,9.);QCOMPARE(moving.remaining,3.);
        QVERIFY(std::abs(moving.head*2-15)<1e-5);
        // A dry boundary truncates support before the old head.
        auto shortened=make(2);shortened->paths.front().length=4;
        for(auto &pt:shortened->paths.front().points)pt.position*=.04;
        const auto cut=playback.present(shortened,5,3,20,1,true,12,12,3);
        QVERIFY(cut->paths.front().appearedAt>=3 && cut->paths.front().appearedAt<3.35);
        for(const auto &pt:cut->paths.front().points)QVERIFY(pt.position.x()<=4);
        const auto settled=playback.present(shortened,5,3.1,20,1,false,12,12,3);
        QCOMPARE(settled->paths.front().appearedAt,-1.f);
        QCOMPARE(playback.present(shortened,5,3.1,20,1,true,12,12,3),settled);
        QVERIFY(playback.present(std::make_shared<FlowStreakBatch>(),5,3,20,1,true)->paths.empty());
        const auto fresh=playback.present(raw,6,4,20,1,true);
        QVERIFY(fresh->paths.front().appearedAt>=4 && fresh->paths.front().appearedAt<4.35);
        auto reverse=make(100);for(auto &pt:reverse->paths.front().points)pt.position*=-1;
        const auto reversed=playback.present(reverse,7,5,20,1,true);
        QVERIFY(reversed->paths.front().appearedAt>=5 && reversed->paths.front().appearedAt<5.35);
        playback.clear();QCOMPARE(playback.present(raw,7,5,20,1,true)->paths.front().appearedAt,-1.f);
    }
    void frameUpdatesDoNotResetRandomLifetime() {
        auto raw=std::make_shared<FlowStreakBatch>();FlowStreakPath path;path.length=100;path.seed=123;
        path.points={{{0,0},0,1},{{100,0},100,1}};raw->paths.push_back(path);
        FlowStreakPlayback playback;auto batch=playback.present(raw,0,0,20,1,true,8,16,1);
        const double firstLifetime=batch->paths.front().lifeRemaining;
        for(int frame=1;frame<=400;++frame) {
            const double clock=frame*.1;
            const auto before=flowStreakMotion(batch->paths.front(),20,clock,clock,8,16,1);
            raw=std::make_shared<FlowStreakBatch>(*raw); // replace the hydraulic frame
            batch=playback.present(raw,clock,clock,20,1,true,8,16,1);
            const auto after=flowStreakMotion(batch->paths.front(),20,clock,clock,8,16,1);
            QCOMPARE(after.generation,before.generation);
            QVERIFY(std::abs(after.head-before.head)<1e-4);
            QVERIFY(std::abs(after.remaining-before.remaining)<1e-4);
            if(clock<firstLifetime)QVERIFY(std::abs(clock+after.remaining-firstLifetime)<1e-4);
        }
    }
    void initialPopulationCoversBothGenerations() {
        auto raw=std::make_shared<FlowStreakBatch>();
        for(int i=0;i<100;++i) {
            FlowStreakPath p;p.seed=i;p.offset=(i+.5)/100.;p.length=100;
            p.points={{{0,0},0,1},{{100,0},100,1}};raw->paths.push_back(p);
        }
        FlowStreakPlayback playback;const auto batch=playback.present(raw,0,0,20,1,true,8,16,1);
        std::array<int,8> phases{};int later=0;
        for(const auto &p:batch->paths) {
            const double first=flowStreakLifetime(flowStreakLifetimeSeed(p.seed),0,8,16);
            const double phase=p.lifeAge+(p.lifeGeneration?first:0);
            ++phases[std::min(7,int(phase/3))];later+=p.lifeGeneration!=0;
            QVERIFY(std::abs(phase-24*p.offset)<1e-5);
            QVERIFY(p.lifeRemaining>0);QCOMPARE(p.lifeHead,p.lifeAge);
        }
        QVERIFY(later>20 && later<80);
        for(int count:phases)QVERIFY(count>=12 && count<=13);
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
