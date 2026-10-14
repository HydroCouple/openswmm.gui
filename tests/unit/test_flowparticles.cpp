// GPL-3.0-or-later
#include "render/flowparticles.h"
#include "render/flowparticlefields.h"
#include "render/flowparticlecontroller.h"
#include "render/flowparticleresidency.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdlib>
#include <new>
// Count allocations only inside the explicitly measured integration call.
// Qt setup, field capture, births and test assertions are outside this scope.
static thread_local bool countTickAllocations=false;
static thread_local size_t tickAllocations=0;
void *operator new(std::size_t n) {
    if(countTickAllocations)++tickAllocations;
    if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void operator delete(void *p) noexcept {std::free(p);}
void *operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void *p) noexcept {::operator delete(p);}
using namespace OpenSWMM::Render;

static std::shared_ptr<FlowParticleSnapshot> field(int columns,int rows,QPointF v={2,0},bool rotate=false) {
    auto mesh=std::make_shared<FlowParticleMesh>();
    for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x)mesh->vertices.append({double(x),double(y)});
    QVector<QRectF> boxes;
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
        const int a=y*(columns+1)+x,b=a+1,c=a+columns+2,d=c-1;
        mesh->triangles.push_back({a,b,c});mesh->triangles.push_back({a,c,d});
        boxes.append({double(x),double(y),1,1});boxes.append(boxes.back());
    }
    mesh->grid.rebuild(boxes);mesh->coverage={0,0,double(columns),double(rows)};
    std::atomic_bool cancel=false;mesh->prepare(cancel);
    auto snapshot=std::make_shared<FlowParticleSnapshot>();snapshot->mesh=mesh;snapshot->geometry=1;
    for(const auto &tri:mesh->triangles) {
        FlowParticleField f;
        for(int i=0;i<3;++i) {
            const auto p=mesh->vertices[tri[i]]-QPointF(columns*.5,rows*.5);
            f.velocity[i]=rotate?QPointF(-p.y()*.1,p.x()*.1):v;
        }snapshot->fields.push_back(f);
    }return snapshot;
}
static FlowParticleSettings longLife() {
    FlowParticleSettings s;s.minimumLifetime=s.maximumLifetime=100;s.trailSeconds=100;s.maxHistory=20000;return s;
}
class FlowParticlesTest:public QObject {
    Q_OBJECT
private slots:
    void publicationCallbackCanDestroyThePersistentWorker() {
        const auto snapshot=field(2,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={1,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        auto *controller=new FlowParticleController;
        controller->configure(longLife(),{0,0,2,2},1);
        connect(controller,&FlowParticleController::ready,this,[&]{delete std::exchange(controller,nullptr);});
        controller->request(input);controller->waitForDone();QVERIFY(!controller);
    }
    void largeExactVertexCircuitIsBoundedAndAllocationFree() {
        constexpr int n=160;auto mesh=std::make_shared<FlowParticleMesh>();mesh->vertices.append({0,0});
        for(int i=0;i<n;++i)mesh->vertices.append({std::cos(i*2*M_PI/n),std::sin(i*2*M_PI/n)});
        QVector<QRectF> boxes;
        for(int i=0;i<n;++i) {
            mesh->triangles.push_back({0,i+1,(i+1)%n+1});
            const auto a=mesh->vertices[i+1],b=mesh->vertices[(i+1)%n+1];
            boxes.append(QRectF(a,b).normalized().united(QRectF(-1e-9,-1e-9,2e-9,2e-9)));
        }
        mesh->grid.rebuild(boxes);std::atomic_bool cancel=false;mesh->prepare(cancel);
        auto snapshot=std::make_shared<FlowParticleSnapshot>();snapshot->mesh=mesh;snapshot->geometry=1;
        for(int i=0;i<n;++i) {
            const auto middle=mesh->vertices[i+1]+mesh->vertices[(i+1)%n+1];
            const QPointF v(-middle.y(),middle.x());FlowParticleField f;f.velocity={v,v,v};snapshot->fields.push_back(f);
        }
        FlowParticleSystem system;auto settings=longLife();settings.maxHistory=16;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(snapshot));QVERIFY(system.spawn({0,0},1,true,0));
        tickAllocations=0;countTickAllocations=true;
        const auto work=system.advance(1./60.,10000);countTickAllocations=false;
        QVERIFY(!work.pending);QCOMPARE(tickAllocations,size_t(0));
        QCOMPARE(system.particles().front().stop,FlowParticleStop::VectorConflict);
        QCOMPARE(system.particles().front().position,QPointF(0,0));QVERIFY(work.steps<2000);
    }
    void curvedHistoryReductionDoesNotAllocate() {
        FlowParticleSystem system;auto settings=longLife();settings.maxHistory=16;settings.historyTolerance=1e-20;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,100,{},true)));
        QVERIFY(system.spawn({50,60}));QVERIFY(!system.advance(1,10000).pending);
        tickAllocations=0;countTickAllocations=true;
        const auto work=system.advance(5,10000);countTickAllocations=false;
        QVERIFY(!work.pending);QCOMPARE(tickAllocations,size_t(0));QVERIFY(system.particles().front().historyLimited);
    }
    void forcedHydraulicPublicationDoesNotShiftTheVisibleHead() {
        auto snapshot=field(100,2,{1,0});auto settings=longLife();settings.publicationInterval=1./30.;
        FlowParticleSystem system;QVERIFY(system.setSettings(settings));QVERIFY(system.setField(snapshot));
        QVERIFY(system.spawn({20,.5}));QVERIFY(!system.advance(.05).pending);
        const auto visibleAge=[&] {
            const auto &p=system.frame()->particles.front();
            return p.previousAge+(p.age-p.previousAge)*system.interpolation();
        };
        const double before=visibleAge();const auto position=system.particles().front().position;
        auto next=std::make_shared<FlowParticleSnapshot>(*snapshot);next->revision=1;
        QVERIFY(system.setField(next,.25));QCOMPARE(system.particles().front().position,position);
        QVERIFY(std::abs(visibleAge()-before)<1e-12);
        QVERIFY(!system.advance(1./60.).pending);QVERIFY(visibleAge()>before);
    }
    void publicationCadencePreservesNumericalTrajectories() {
        auto snapshot=field(40,40,{},true);auto settings=longLife();settings.maxHistory=256;
        FlowParticleSystem reference,reduced;QVERIFY(reference.setSettings(settings));
        settings.publicationInterval=1./30.;QVERIFY(reduced.setSettings(settings));
        QVERIFY(reference.setField(snapshot));QVERIFY(reduced.setField(snapshot));
        QVERIFY(reference.spawn({20,30}));QVERIFY(reduced.spawn({20,30}));
        auto published=reduced.frame();double previousAge=0;
        for(int i=0;i<300;++i) {
            QVERIFY(!reference.advance(1./60.).pending);QVERIFY(!reduced.advance(1./60.).pending);
            QCOMPARE(reduced.particles().front().position,reference.particles().front().position);
            QCOMPARE(reduced.particles().front().age,reference.particles().front().age);
            if(reduced.frame()!=published) {
                published=reduced.frame();QCOMPARE(published->particles.front().previousAge,previousAge);
                previousAge=published->particles.front().age;
            }
        }
        QVERIFY(std::abs(published->clock-5)<1e-10);
    }
    void warmedIntegrationAndPublicationDoNotAllocate() {
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=2000;
        settings.maxHistory=64;settings.historyTolerance=0;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,2,{1,0})));
        FlowParticleAppearance appearance;appearance.particleCapacity=2000;appearance.historyCapacity=64;
        system.setAppearance(1,appearance);
        for(int i=0;i<2000;++i)QVERIFY(system.spawn({20+i*.001,.5},i,false));
        QVERIFY(!system.advance(1.2,1000000).pending);
        tickAllocations=0;countTickAllocations=true;
        const auto work=system.advance(.5,1000000);
        countTickAllocations=false;
        QVERIFY(!work.pending);QCOMPARE(tickAllocations,size_t(0));
    }
    void retainedFramesApplyBoundedPublicationBackpressure() {
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=1;settings.maxHistory=64;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,2,{1,0})));
        QVERIFY(system.spawn({20,.5}));
        std::vector<std::shared_ptr<const FlowParticleFrame>> held;
        for(int i=0;i<4;++i) {
            QVERIFY(!system.advance(settings.timestep).pending);held.push_back(system.frame());
        }
        for(size_t i=1;i<held.size();++i)QVERIFY(held[i]!=held[i-1]);
        const auto old=held.back()->particles.front().position;const double age=held.back()->particles.front().age;
        QVERIFY(!system.advance(.2).pending);
        QCOMPARE(held.back()->particles.front().position,old);QCOMPARE(held.back()->particles.front().age,age);
        QVERIFY(system.particles().front().position.x()>old.x());
        held.front().reset();QVERIFY(!system.advance(settings.timestep).pending);
        QVERIFY(system.frame()->clock>age);QVERIFY(system.frame()->deferredPublications>0);
        for(size_t i=1;i<held.size();++i)QVERIFY(held[i]->particles.front().age<=age);
    }
    void compressedSubpixelVertexOrbitsKeepTheirAssignedLifetime() {
        auto snapshot=field(2,2);
        for(size_t i=0;i<snapshot->fields.size();++i) {
            QPointF center;
            for(int v:snapshot->mesh->triangles[i])center+=snapshot->mesh->vertices[v]/3.;
            const QPointF velocity=center.y()<1?(center.x()<1?QPointF(1,0):QPointF(0,1))
                :(center.x()<1?QPointF(0,-1):QPointF(-1,0));
            snapshot->fields[i].velocity={velocity,velocity,velocity};
        }
        FlowParticleSystem system;auto settings=longLife();settings.minimumLifetime=settings.maximumLifetime=3;
        settings.referenceSpeed=1;settings.speedExponent=1./3.;settings.historyTolerance=.25;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(snapshot));
        const auto id=system.spawn({.999,.998});QVERIFY(id);
        const auto work=system.advance(3,2000);QVERIFY(!work.pending);
        const auto &p=system.particles().front();QCOMPARE(p.id,id);QCOMPARE(p.age,3.);
        QCOMPARE(p.stop,FlowParticleStop::Lifetime);QVERIFY(system.frame()->unresolvedOrbits>0);
        QVERIFY(std::hypot(p.position.x()-1,p.position.y()-1)<.25);
        QCOMPARE(system.frame()->stopCounts[size_t(FlowParticleStop::VectorConflict)],quint64(0));
    }
    void compressedVisibleOrbitsKeepMoving() {
        FlowParticleSystem system;auto settings=longLife();
        settings.referenceSpeed=1;settings.speedExponent=1./3.;settings.historyTolerance=.25;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(40,40,{},true)));
        QVERIFY(system.spawn({20.3,30}));const auto work=system.advance(70,100000);
        QVERIFY(!work.pending);QVERIFY(std::abs(system.frame()->clock-70)<1e-8);
        QCOMPARE(system.frame()->unresolvedOrbits,quint64(0));
        QCOMPARE(system.particles().front().stop,FlowParticleStop::None);
        QVERIFY(std::hypot(system.particles().front().position.x()-20.3,system.particles().front().position.y()-30)>5);
    }
    void publicationBudgetAtTwoThousandParticles() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_BENCHMARK"))QSKIP("Opt-in publication measurement");
        FlowParticleSystem system;auto settings=longLife();
        settings.maxParticles=2000;settings.maxHistory=64;settings.historyTolerance=0;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,2,{1,0})));
        for(int i=0;i<2000;++i)QVERIFY(system.spawn({20+i*.001,.5},i,false));
        auto work=system.advance(1.1,1000000);QVERIFY(!work.pending);
        std::vector<double> times;
        for(int i=0;i<9;++i) {
            work=system.advance(settings.timestep,1000000);QVERIFY(!work.pending);
            times.push_back(system.frame()->publicationMs);
        }
        std::sort(times.begin(),times.end());
        QFile report(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/publication-budget.json");
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"population",2000},{"historyCapacity",64},
            {"publicationMedianMs",times[4]},{"publicationMaxMs",times.back()}}).toJson());
    }
    void stagnationFloorRejectsIsolatedEnvelopeExtremes() {
        std::vector<float> envelope(100,1.f);envelope.back()=1000000.f;
        QVERIFY(std::abs(flowParticleStagnationFloor(envelope)-.01)<1e-12);
        QCOMPARE(flowParticleStagnationFloor({0,-1,NAN,INFINITY}),0.);
        QVERIFY(std::abs(flowParticleStagnationFloor({0,2,NAN})-.02)<1e-12);
    }
    void fullComplexHistoryDoesNotRescanOnEveryStep() {
        FlowParticleSystem system;auto settings=longLife();
        settings.maxHistory=16;settings.historyTolerance=1e-20;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,100,{},true)));
        QVERIFY(system.spawn({50,60}));
        const auto work=system.advance(5,500);
        QVERIFY(!work.pending);QVERIFY(work.steps<=500);
        QVERIFY(std::abs(system.frame()->clock-5)<1e-10);
        const auto &p=system.particles().front();QVERIFY(p.history.size()<=16);
        QVERIFY(p.historyLimited);QCOMPARE(p.stop,FlowParticleStop::None);
    }
    void wetSeedsCoverFivePercentWithoutFailedBirths() {
        auto snapshot=field(20,20,{.1,0});
        for(size_t i=0;i<snapshot->fields.size();++i)if(snapshot->mesh->vertices[snapshot->mesh->triangles[i][0]].x()>=1)
            snapshot->fields[i].depth={0,0,0};
        const auto seeds=buildFlowParticleSeeds(*snapshot,{0,0,20,20},.001);
        QVERIFY(std::abs(seeds.wetArea-20)<1e-10);
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=2000;
        system.setSettings(settings);system.setField(snapshot);
        for(quint64 i=0;i<2000;++i) {
            const auto point=seeds.sample(i);QCOMPARE(point,seeds.sample(i));
            QVERIFY(point.first.x()>0 && point.first.x()<1);
            QVERIFY(system.spawn(point.first,i,false,point.second));
        }
        QCOMPARE(system.particles().size(),size_t(2000));
    }
    void seedPolygonsClipPartialWetCellsAndTheView() {
        auto snapshot=field(1,1);
        for(size_t i=0;i<snapshot->fields.size();++i)for(int k=0;k<3;++k)
            snapshot->fields[i].depth[k]=float(snapshot->mesh->vertices[snapshot->mesh->triangles[i][k]].x()-.95);
        const QRectF view(.97,.2,.03,.6);const auto seeds=buildFlowParticleSeeds(*snapshot,view,0);
        QVERIFY(std::abs(seeds.wetArea-.018)<1e-8);
        auto settings=longLife();settings.cutoff=0;FlowParticleSystem system;
        system.setSettings(settings);system.setField(snapshot);
        for(quint64 i=0;i<2000;++i) {
            const auto point=seeds.sample(i);QVERIFY(view.contains(point.first));
            QVERIFY(point.first.x()>.95);QVERIFY(system.spawn(point.first,i,false,point.second));
        }
        for(auto &f:snapshot->fields)f.depth={0,0,0};
        const auto dry=buildFlowParticleSeeds(*snapshot,view,0);QVERIFY(dry.triangles.empty());
        QCOMPARE(dry.sample(1).second,-1);QCOMPARE(dry.population(100,40,2000),0);
    }
    void wetAreaDeterminesDensity() {
        for(int wetColumns:{1,10,20}) {
            auto snapshot=field(20,20);
            for(size_t i=0;i<snapshot->fields.size();++i)
                if(snapshot->mesh->vertices[snapshot->mesh->triangles[i][0]].x()>=wetColumns)snapshot->fields[i].depth={0,0,0};
            const auto seeds=buildFlowParticleSeeds(*snapshot,{0,0,20,20},.001);
            QCOMPARE(seeds.population(100,40,4000),wetColumns*125);
            QCOMPARE(seeds.population(100,40,100),100);
        }
    }
    void motionGainPreservesPixelSpeedAcrossZoomsAndUnits() {
        for(double unit:{1.,.3048})for(double scale:{.5,2.,8.}) {
            auto snapshot=field(10,10,{2/unit,0});auto mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
            for(auto &v:mesh->vertices)v/=unit;snapshot->mesh=mesh;
            for(auto &f:snapshot->fields)f.speed={2,2,2};
            const auto seeds=buildFlowParticleSeeds(*snapshot,{0,0,10/unit,10/unit},.001);
            const double gain=seeds.motionGain(scale,48,.02);
            QVERIFY(std::abs(gain*(2/unit)*scale-48)<1e-10);
            QCOMPARE(seeds.referenceSpeedSI,2.);
            // A run envelope ten times faster than this near-stagnant frame
            // limits amplification to one tenth of normal motion.
            QVERIFY(std::abs(seeds.motionGain(scale,48,20)*(2/unit)*scale-4.8)<1e-10);
        }
    }
    void compressedMotionBoostsSlowFlowAndPreservesDirectionAndPhysicalColour() {
        double previous=0;
        for(double physicalSpeed:{0.,.002,.02,.2,2.}) {
            auto snapshot=field(100,2,{physicalSpeed,0});
            for(auto &f:snapshot->fields)f.speed={float(physicalSpeed),float(physicalSpeed),float(physicalSpeed)};
            FlowParticleSystem system;auto settings=longLife();settings.referenceSpeed=2;settings.speedExponent=1./3.;settings.speed=24;
            system.setSettings(settings);system.setField(snapshot);QVERIFY(system.spawn({.13,.42}));system.advance(.25);
            const auto &p=system.particles().front();const double actual=(p.position.x()-.13)/.25;
            const double expected=48*std::cbrt(physicalSpeed/2);
            QVERIFY(std::abs(actual-expected)<1e-8);QVERIFY(actual>=previous);previous=actual;
            QCOMPARE(p.position.y(),.42);QCOMPARE(p.history.back().speed,float(physicalSpeed));
            if(physicalSpeed==.002)QVERIFY(actual>4.7 && actual<4.9);
        }
        auto snapshot=field(100,100,{.2,.2});FlowParticleSystem system;
        auto settings=longLife();settings.referenceSpeed=2;settings.speedExponent=1./3.;settings.speed=24;
        system.setSettings(settings);system.setField(snapshot);system.spawn({1.13,1.42});system.advance(.25);
        const auto delta=system.particles().front().position-QPointF(1.13,1.42);
        QVERIFY(std::abs(delta.x()-delta.y())<1e-10);
    }
    void wetWeightedSeedsFavorFastWaterWithoutStarvingSlowWater() {
        auto snapshot=field(2,1);
        for(size_t i=0;i<snapshot->fields.size();++i)for(auto &v:snapshot->fields[i].velocity)v={i<2?.2:2.,0};
        const auto seeds=buildFlowParticleSeeds(*snapshot,{0,0,2,1},.001);
        int fast=0;for(quint64 i=0;i<10000;++i)fast+=seeds.sample(i).first.x()>1;
        QVERIFY(fast>6600 && fast<7100);
    }
    void cellVelocityFallbackPreservesSIAndCrossings() {
        for(double unit:{1.,.3048}) {
            const auto snapshot=field(20,2);auto mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
            for(auto &v:mesh->vertices)v/=unit;
            FlowParticleNativeInput input;input.mesh=mesh;input.geometry=1;
            for(const auto &ids:mesh->triangles) {
                FlowParticleNativeTriangle t;t.meanDepth=1;t.depth={1,1,1};t.available=true;t.velocityOnly=true;t.velocitySI={2,0};
                t.flux={NAN,NAN,NAN};for(int k=0;k<3;++k)t.metres[k]=mesh->vertices[ids[k]]*unit;
                input.triangles.push_back(t);
            }
            std::atomic_bool cancelled=false;const auto fallback=buildFlowParticleField(input,cancelled);QVERIFY(fallback);
            for(const auto &f:fallback->fields)for(int k=0;k<3;++k) {
                QVERIFY(f.available);QVERIFY(std::abs(f.velocity[k].x()-2/unit)<1e-10);QCOMPARE(f.speed[k],2.f);
            }
            FlowParticleSystem system;system.setSettings(longLife());system.setField(fallback);
            QVERIFY(system.spawn({.13/unit,.42/unit}));system.advance(5);
            QVERIFY(std::abs(system.particles().front().position.x()-10.13/unit)<1e-8);
            QVERIFY(system.particles().front().crossings>10);QCOMPARE(system.particles().front().stop,FlowParticleStop::None);
        }
    }
    void delayedSurfaceUpdatesNeverFreezeAnimation() {
        const auto snapshot=field(200,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        FlowParticleController controller;controller.configure(longLife(),{10,0,20,2},20);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        auto old=controller.frame(1);QVERIFY(old);QCOMPARE(old->particles.size(),size_t(20));
        const auto id=old->particles.front().id;const auto start=old->particles.front().position;
        for(int i=1;i<=60;++i) {
            auto next=std::make_shared<FlowParticleNativeInput>(*input);next->revision=i+1;
            controller.request(next);controller.waitForDone();controller.advance(1);controller.waitForDone();
            const auto current=controller.frame(1);QVERIFY(current);
            QVERIFY(std::abs(current->clock-i)<1e-8);QVERIFY(!controller.frame(i+1));
        }
        const auto current=controller.frame(1);QCOMPARE(current->particles.front().id,id);
        QVERIFY(std::abs(current->particles.front().position.x()-start.x()-120)<1e-7);
        controller.setPresentedRevision(61);controller.waitForDone();
        const auto updated=controller.frame(61);QVERIFY(updated);QCOMPARE(updated->clock,current->clock);
        QCOMPARE(updated->particles.front().position,current->particles.front().position);
        QCOMPARE(controller.diagnostics().failedBirths,quint64(0));
    }
    void sparseCaptureExcludesTheGapAndIncludesSharedBoundaryFaces() {
        const auto source=field(100,10);
        auto mesh=FlowParticleMesh::captureRegions(source->mesh->vertices,source->mesh->triangles,source->mesh->grid,
            {{2,2,1,1},{90,2,1,1}});QVERIFY(mesh);
        QVERIFY(mesh->triangles.size()<50);
        const int *begin=nullptr,*end=nullptr;mesh->grid.candidatesAtPoint(50,2.5,begin,end);
        QVERIFY(!begin || begin==end);
        // Both triangles that share an exactly clipped edge remain indexed.
        for(int id:{2*(2*100+2),2*(2*100+2)+1})
            QVERIFY(std::binary_search(mesh->sourceTriangles.begin(),mesh->sourceTriangles.end(),id));
        std::atomic_bool cancel=false;mesh->prepare(cancel);
        const auto a=std::lower_bound(mesh->sourceTriangles.begin(),mesh->sourceTriangles.end(),2*(2*100+2));
        const int local=int(a-mesh->sourceTriangles.begin());
        QVERIFY(std::find(mesh->neighbors[local].begin(),mesh->neighbors[local].end(),local+1)!=mesh->neighbors[local].end());
    }
    void residencyPinsAnOffscreenHeadAndEveryReducedTrailSegment() {
        const auto source=field(200,20);
        FlowParticleFrame frame;FlowParticle p;p.position={150,10};
        p.history={{{100,10},0,0},{{150,10},10,0}};frame.particles.push_back(p);
        const auto regions=flowParticleResidency({1,1,5,5},source->mesh->grid,&frame);
        QVERIFY(!regions.limited);QVERIFY(!regions.tiles.empty());
        for(QPointF point:{QPointF(3,3),QPointF(110,10),QPointF(130,10),QPointF(150,10),QPointF(159,10)}) {
            QVERIFY2(std::any_of(regions.rectangles.begin(),regions.rectangles.end(),[&](QRectF r){return r.contains(point);}),
                qPrintable(QString("missing support at %1,%2").arg(point.x()).arg(point.y())));
        }
        // Long thin domains can fit; an overview exceeding the tile budget is declined.
        MeshSpatialGrid grid;grid.cols=100;grid.rows=100;grid.cellW=grid.cellH=1;
        grid.extent={0,0,100,100};grid.cellOffsets={0,1};grid.cellIndices={0};
        const auto limited=flowParticleResidency(grid.extent,grid,nullptr);QVERIFY(limited.limited);QVERIFY(limited.tiles.empty());
    }
    void bufferedSupportSurvivesMotionBetweenResidencyChecks() {
        const auto source=field(200,20);FlowParticleFrame frame;FlowParticle p;p.position={150,10};
        p.history={{{100,10},0,0},{{150,10},10,0}};frame.particles.push_back(p);
        const auto captured=flowParticleResidency({1,1,5,5},source->mesh->grid,&frame,2);
        frame.particles.front().position={155,10};frame.particles.front().history.push_back({{155,10},11,0});
        const auto needed=flowParticleResidency({1,1,5,5},source->mesh->grid,&frame,.5);
        QVERIFY(!needed.limited);QVERIFY(std::includes(captured.tiles.begin(),captured.tiles.end(),needed.tiles.begin(),needed.tiles.end()));
    }
    void aLayoutReplacementStartsFromTheCurrentTemporalBlend() {
        auto initial=field(20,2,{2,0});auto ids=std::make_shared<FlowParticleMesh>(*initial->mesh);
        for(int i=0;i<int(ids->triangles.size());++i)ids->sourceTriangles.push_back(i);initial->mesh=ids;
        FlowParticleSystem reference,regional;
        for(auto *system:{&reference,&regional}) {system->setSettings(longLife());system->setField(initial);QVERIFY(system->spawn({.13,.42},7));system->advance(.1);}
        auto next=std::make_shared<FlowParticleSnapshot>(*initial);next->revision=2;
        for(auto &f:next->fields)for(auto &v:f.velocity)v={4,0};
        for(auto *system:{&reference,&regional}) {system->setField(next,.5);system->advance(.2);}
        auto subset=std::make_shared<FlowParticleSnapshot>(*next);
        auto cropped=FlowParticleMesh::captureRegions(ids->vertices,ids->triangles,ids->grid,{{0,0,8,2}});QVERIFY(cropped);
        std::atomic_bool cancel=false;cropped->prepare(cancel);subset->mesh=cropped;subset->fields.clear();
        for(int id:cropped->sourceTriangles)subset->fields.push_back(next->fields[size_t(id)]);
        const auto history=regional.particles().front().history;const auto id=regional.particles().front().id;
        reference.setField(next,.5);regional.setField(subset,.5);
        for(auto *system:{&reference,&regional})system->advance(.5);
        const auto &p=regional.particles().front();QCOMPARE(p.id,id);QCOMPARE(p.age,reference.particles().front().age);
        QVERIFY(std::hypot((p.position-reference.particles().front().position).x(),(p.position-reference.particles().front().position).y())<1e-9);
        for(size_t i=0;i<history.size();++i)QCOMPARE(p.history[i].position,history[i].position);
        QCOMPARE(p.stop,FlowParticleStop::None);
    }
    void oneIdentityCrossesHundredsOfCellsUntilItsOwnDeath() {
        FlowParticleSystem system;auto settings=longLife();settings.timestep=.1;QVERIFY(system.setSettings(settings));
        QVERIFY(system.setField(field(250,2)));const auto id=system.spawn({.13,.42},42);QVERIFY(id);
        const auto first=system.frame();
        const auto work=system.advance(80,100000);QVERIFY(!work.pending);
        const auto &p=system.particles().front();QCOMPARE(p.id,id);QCOMPARE(p.stop,FlowParticleStop::None);
        QVERIFY(p.crossings>=100);QVERIFY(std::abs(p.position.x()-160.13)<1e-7);
        QVERIFY(std::abs(p.age-80)<1e-8);QCOMPARE(p.history.front().position,QPointF(.13,.42));
        QCOMPARE(first->particles.front().age,0.); // published history never mutates
        const QString folder=QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT);QVERIFY(QDir().mkpath(folder));
        QFile trace(folder+"/tracked-particle.csv");QVERIFY(trace.open(QIODevice::WriteOnly));QTextStream csv(&trace);
        csv<<"particle_id,age,cell,x,y\n";
        for(const auto &sample:p.history)csv<<p.id<<','<<sample.age<<','<<sample.cell<<','<<sample.position.x()<<','<<sample.position.y()<<'\n';
        const int crossings=p.crossings;
        system.advance(20,100000);QCOMPARE(system.particles().front().stop,FlowParticleStop::Lifetime);
        QVERIFY(std::abs(system.particles().front().age-100)<1e-8);QCOMPARE(system.particles().front().id,id);
        QFile evidence(folder+"/continuity.json");QVERIFY(evidence.open(QIODevice::WriteOnly));
        evidence.write(QJsonDocument(QJsonObject{{"particleId",double(id)},{"crossingsAt80Seconds",crossings},
            {"deathAge",system.particles().front().age},{"reason",flowParticleStopName(system.particles().front().stop)}}).toJson());
    }
    void workBudgetResumesWithoutSkippingIntegrationOrChangingLife() {
        FlowParticleSystem reference,sliced;auto s=longLife();
        for(auto *system:{&reference,&sliced}) {QVERIFY(system->setSettings(s));QVERIFY(system->setField(field(20,2)));QVERIFY(system->spawn({.13,.42},7));}
        reference.advance(2,100000);const auto before=sliced.frame();
        auto work=sliced.advance(2,1);QVERIFY(work.pending);QCOMPARE(work.steps,1);
        int slices=1;
        while(work.pending && slices<20000) {work=sliced.advance(0,1);QVERIFY(work.steps<=1);++slices;}
        QVERIFY(!work.pending);QVERIFY(slices>100);
        const auto &a=reference.particles().front(),&b=sliced.particles().front();
        QCOMPARE(a.id,b.id);QCOMPARE(a.lifetime,b.lifetime);QCOMPARE(a.crossings,b.crossings);
        QVERIFY(std::hypot(a.position.x()-b.position.x(),a.position.y()-b.position.y())<1e-10);
        QCOMPARE(a.history.size(),b.history.size());QCOMPARE(before->particles.front().age,0.);
    }
    void cadenceDoesNotChangeTrajectoryOrDeath() {
        auto s=longLife();s.minimumLifetime=s.maximumLifetime=3;
        for(double interval:{1./30,1./60,1./120,.173}) {
            FlowParticleSystem system;system.setSettings(s);system.setField(field(20,2));system.spawn({.13,.42});
            double elapsed=0;while(elapsed<4-1e-12) {const double dt=std::min(interval,4-elapsed);system.advance(dt);elapsed+=dt;}
            const auto &p=system.particles().front();QCOMPARE(p.stop,FlowParticleStop::Lifetime);
            QCOMPARE(p.age,3.);QVERIFY(std::abs(p.position.x()-6.13)<1e-8);
        }
    }
    void curvedLinearFieldConvergesAndCrossesCells() {
        double previousError=1;
        for(double dt:{1./30,1./60,1./120}) {
            FlowParticleSystem system;auto s=longLife();s.timestep=dt;s.trailSeconds=1;system.setSettings(s);
            system.setField(field(40,40,{},true));system.spawn({25,20.2});system.advance(20,100000);
            const auto &p=system.particles().front();QCOMPARE(p.stop,FlowParticleStop::None);QVERIFY(p.crossings>20);
            const QPointF expected(20+5*std::cos(2)-.2*std::sin(2),20+5*std::sin(2)+.2*std::cos(2));
            const double error=std::hypot((p.position-expected).x(),(p.position-expected).y());
            QVERIFY2(error<previousError*.6,qPrintable(QString("dt=%1 error=%2 previous=%3").arg(dt).arg(error).arg(previousError)));
            previousError=error;QVERIFY(p.history.size()<200);
        }
        QVERIFY(previousError<1e-4);
    }
    void compatibleFieldUpdatesKeepIdentityAndHistoricalPositions() {
        auto snapshot=field(250,2);FlowParticleSystem system;auto s=longLife();system.setSettings(s);system.setField(snapshot);
        const auto id=system.spawn({.13,.42},19);system.advance(1);const auto first=system.frame();
        const auto oldHistory=first->particles.front().history;const double lifetime=first->particles.front().lifetime;
        for(int i=1;i<=1000;++i) {
            auto next=std::make_shared<FlowParticleSnapshot>(*snapshot);next->revision=i;
            for(auto &f:next->fields)for(auto &v:f.velocity)v={i%2?2.:3.,0};
            QVERIFY(system.setField(next));system.advance(1./60);
            const auto &p=system.particles().front();QCOMPARE(p.id,id);QCOMPARE(p.lifetime,lifetime);
            for(const auto &sample:p.history)if(sample.age<=oldHistory.back().age) {
                const auto original=std::find_if(oldHistory.begin(),oldHistory.end(),[&](const auto &s){return s.age==sample.age;});
                QVERIFY(original!=oldHistory.end());QCOMPARE(sample.position,original->position);
            }
            QCOMPARE(first->particles.front().history.size(),oldHistory.size()); // immutable earlier publication
        }
        QCOMPARE(system.frame()->births,quint64(1));QCOMPARE(system.frame()->deaths,quint64(0));
    }
    void missingNeighborDataWaitsThenContinues() {
        auto snapshot=field(10,2);snapshot->fields[0].available=false;
        FlowParticleSystem system;system.setSettings(longLife());system.setField(snapshot);const auto id=system.spawn({.13,.42});
        system.advance(1);const auto &waiting=system.particles().front();QVERIFY(waiting.waitingForData);
        QCOMPARE(waiting.stop,FlowParticleStop::None);QCOMPARE(waiting.cell,0);QCOMPARE(waiting.id,id);
        const auto position=waiting.position;const double age=waiting.age;
        system.advance(1);QCOMPARE(system.particles().front().position,position);QCOMPARE(system.particles().front().age,age);
        auto ready=std::make_shared<FlowParticleSnapshot>(*snapshot);ready->fields[0].available=true;
        system.setField(ready);system.advance(.2);const auto &p=system.particles().front();
        QVERIFY(!p.waitingForData);QCOMPARE(p.id,id);QVERIFY(std::abs(p.position.x()-position.x()-.4)<1e-8);
    }
    void reducedHistoryCrossesHundredsOfCellsUntilAssignedDeath() {
        auto snapshot=field(250,2);FlowParticleSystem reference,reduced;
        auto settings=longLife();settings.historyTolerance=0;reference.setSettings(settings);reference.setField(snapshot);reference.spawn({.13,.42},9);
        settings.maxHistory=32;settings.historyTolerance=.001;reduced.setSettings(settings);reduced.setField(snapshot);const auto id=reduced.spawn({.13,.42},9);
        const auto first=reduced.frame();size_t peak=0;
        for(int i=0;i<500;++i) {
            reference.advance(.2,100000);reduced.advance(.2,100000);
            const auto &p=reduced.particles().front(),&r=reference.particles().front();
            QCOMPARE(p.position,r.position);QCOMPARE(p.age,r.age);QCOMPARE(p.crossings,r.crossings);
            QCOMPARE(p.id,id);QCOMPARE(p.lifetime,r.lifetime);QVERIFY(!p.historyLimited);
            QVERIFY(p.history.size()<=32);peak=std::max(peak,p.history.size());
        }
        const auto &p=reduced.particles().front();QCOMPARE(p.stop,FlowParticleStop::Lifetime);QCOMPARE(p.age,100.);
        QCOMPARE(p.history.front().position,QPointF(.13,.42));QCOMPARE(first->particles.front().history.size(),size_t(1));
        QVERIFY(p.crossings>=400);QVERIFY(reference.particles().front().history.size()>6000);
        const QString folder=QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT);QVERIFY(QDir().mkpath(folder));
        QFile evidence(folder+"/bounded-history.json");QVERIFY(evidence.open(QIODevice::WriteOnly));
        evidence.write(QJsonDocument(QJsonObject{{"particleId",qint64(id)},{"deathAge",p.age},{"cellCrossings",p.crossings},
            {"peakSamples",qint64(peak)},{"historyLimit",settings.maxHistory},{"shortenedTail",p.historyLimited}}).toJson());
    }
    void reductionPreservesCurvesAndTimestampInterpolationError() {
        auto snapshot=field(20,20,{},true);FlowParticleSystem reference,reduced;
        auto settings=longLife();settings.minimumLifetime=settings.maximumLifetime=120;settings.trailSeconds=120;
        settings.historyTolerance=0;
        reference.setSettings(settings);reference.setField(snapshot);reference.spawn({14,10.2});
        settings.maxHistory=256;settings.historyTolerance=.01;
        reduced.setSettings(settings);reduced.setField(snapshot);reduced.spawn({14,10.2});
        for(int i=0;i<600;++i) {
            reference.advance(.2,100000);reduced.advance(.2,100000);
            const auto &p=reduced.particles().front();QCOMPARE(p.position,reference.particles().front().position);
            QCOMPARE(p.age,reference.particles().front().age);QVERIFY(p.history.size()<=256);QVERIFY(!p.historyLimited);
        }
        const auto &history=reduced.particles().front().history;size_t index=1;double peakError=0;
        for(const auto &sample:reference.particles().front().history) {
            while(index+1<history.size() && history[index].age<sample.age)++index;
            const auto &a=history[index-1],&b=history[index];
            const auto interpolated=a.position+(b.position-a.position)*((sample.age-a.age)/(b.age-a.age));
            const auto delta=sample.position-interpolated;peakError=std::max(peakError,std::hypot(delta.x(),delta.y()));
        }
        QVERIFY2(peakError<=settings.historyTolerance+1e-9,qPrintable(QString("interpolation error %1").arg(peakError)));
        QCOMPARE(reduced.particles().front().stop,FlowParticleStop::Lifetime);
    }
    void complexHistoryLimitShortensOnlyTheTailAndCanRecover() {
        FlowParticleSystem system;auto settings=longLife();settings.maxHistory=3;settings.historyTolerance=0;
        settings.minimumLifetime=settings.maximumLifetime=2;system.setSettings(settings);
        system.setField(field(20,20,{},true));const auto id=system.spawn({14,10.2});
        system.advance(1,100000);const auto &p=system.particles().front();
        QVERIFY(p.historyLimited);QVERIFY(std::abs(p.age-1)<1e-9);QVERIFY(p.history.size()<=3);
        const auto position=p.position;settings.maxHistory=1000;settings.trailSeconds=.1;system.setSettings(settings);
        system.advance(.2,100000);QVERIFY(!system.particles().front().historyLimited);
        QVERIFY(system.particles().front().position!=position);system.advance(1,100000);
        QCOMPARE(system.particles().front().id,id);QCOMPARE(system.particles().front().age,2.);
        QCOMPARE(system.particles().front().stop,FlowParticleStop::Lifetime);
    }
    void reductionWorkSlicesAndDisplayCadenceKeepTheSamePath() {
        auto snapshot=field(20,20,{},true);FlowParticleSystem reference,sliced;
        auto settings=longLife();settings.maxHistory=32;settings.historyTolerance=.01;
        for(auto *system:{&reference,&sliced}) {system->setSettings(settings);system->setField(snapshot);system->spawn({14,10.2});}
        reference.advance(8,100000);auto work=sliced.advance(8,1);int slices=0;
        while(work.pending && ++slices<20000) {work=sliced.advance(0,1);QVERIFY(work.steps<=1);}
        QVERIFY(!work.pending);const auto &a=reference.particles().front(),&b=sliced.particles().front();
        QCOMPARE(a.position,b.position);QCOMPARE(a.age,b.age);QCOMPARE(a.history.size(),b.history.size());
        for(size_t i=0;i<a.history.size();++i) {QCOMPARE(a.history[i].position,b.history[i].position);QCOMPARE(a.history[i].error,b.history[i].error);}
        for(double interval:{1./30,1./120,.173}) {
            FlowParticleSystem cadence;cadence.setSettings(settings);cadence.setField(snapshot);cadence.spawn({14,10.2});
            double time=0;while(time<8-1e-12) {const double dt=std::min(interval,8-time);cadence.advance(dt,100000);time+=dt;}
            QCOMPARE(cadence.particles().front().position,a.position);QCOMPARE(cadence.particles().front().history.size(),a.history.size());
        }
    }
    void dryingInsideAReducedSegmentHidesTheObsoleteTail() {
        for(bool depthBeforeFlux:{false,true}) {
        auto snapshot=field(20,2);FlowParticleSystem system;auto settings=longLife();settings.maxHistory=32;
        system.setSettings(settings);system.setField(snapshot);system.spawn({.13,.42});system.advance(5,100000);
        const auto history=system.particles().front().history;bool removedCell=false;
        for(size_t i=1;i<history.size();++i)if(history[i].error>=0 && history[i-1].position.x()<4 && history[i].position.x()>5)removedCell=true;
        QVERIFY(removedCell);
        auto dry=std::make_shared<FlowParticleSnapshot>(*snapshot);
        for(size_t i=0;i<dry->fields.size();++i) {
            if(depthBeforeFlux)dry->fields[i].available=false;
            const auto &tri=dry->mesh->triangles[i];double x=0;for(int v:tri)x+=dry->mesh->vertices[v].x()/3;
            if(x>=4 && x<=5) {dry->fields[i].depth={0,0,0};dry->fields[i].available=false;}
        }
        const auto position=system.particles().front().position;const auto life=system.particles().front().lifetime;
        QVERIFY(system.setField(dry));QVERIFY(system.particles().front().visibleAfterAge>2);
        for(const auto &sample:system.frame()->particles.front().history)QVERIFY(sample.position.x()>5);
        QCOMPARE(system.particles().front().history.size(),history.size());system.advance(.1);
        if(depthBeforeFlux) {
            QCOMPARE(system.particles().front().position,position);QVERIFY(system.particles().front().waitingForData);
            auto complete=std::make_shared<FlowParticleSnapshot>(*dry);
            for(auto &f:complete->fields)f.available=true;system.setField(complete);system.advance(.1);
        }
        QVERIFY(system.particles().front().position.x()>position.x());QCOMPARE(system.particles().front().lifetime,life);
        }
    }
    void reductionKeepsCurrentTickSamplesForHeadInterpolation() {
        auto settings=longLife();settings.maxHistory=8;settings.timestep=.2;
        FlowParticleSystem system;system.setSettings(settings);system.setField(field(20,2));system.spawn({.13,.42});
        system.advance(4,100000);const auto &p=system.particles().front();QVERIFY(p.history.size()<=8);
        bool bracket=false;for(const auto &sample:p.history)if(std::abs(sample.age-p.previousAge)<1e-9)bracket=true;
        QVERIFY(bracket);QCOMPARE(p.history.back().position,p.position);
        QCOMPARE(p.history.back().age,p.age);
    }
    void reductionsNeverShortcutAcrossDryCellsOrClosedFaces() {
        for(bool dry:{false,true}) {
            auto snapshot=field(3,3,{1,0});std::vector<bool> island(snapshot->fields.size());
            for(size_t i=0;i<island.size();++i) {
                QPointF center;for(int v:snapshot->mesh->triangles[i])center+=snapshot->mesh->vertices[v]/3;
                island[i]=center.x()>1 && center.x()<2 && center.y()>1 && center.y()<2;
                if(dry && island[i])snapshot->fields[i].depth={0,0,0};
            }
            if(!dry)for(size_t i=0;i<island.size();++i)for(int e=0;e<3;++e) {
                const int next=snapshot->mesh->neighbors[i][e];
                if(next>=0 && island[i]!=island[size_t(next)])snapshot->fields[i].exits&=~(1<<e);
            }
            FlowParticleSystem system;auto settings=longLife();settings.maxHistory=32;
            settings.historyTolerance=100;system.setSettings(settings);system.setField(snapshot);system.spawn({.2,.5});
            system.advance(2,100000);
            auto turn=std::make_shared<FlowParticleSnapshot>(*snapshot);
            for(auto &f:turn->fields)for(auto &v:f.velocity)v={0,1};system.setField(turn);system.advance(2,100000);
            turn=std::make_shared<FlowParticleSnapshot>(*turn);
            for(auto &f:turn->fields)for(auto &v:f.velocity)v={-1,0};system.setField(turn);system.advance(2,100000);
            const auto &p=system.particles().front();QCOMPARE(p.stop,FlowParticleStop::None);
            QVERIFY(!p.historyLimited);QVERIFY(p.history.size()<=32);
            QCOMPARE(p.history.front().position,QPointF(.2,.5));
            for(size_t i=1;i<p.history.size();++i)for(int step=0;step<=128;++step) {
                const auto a=p.history[i-1].position,b=p.history[i].position;
                const auto point=a+(b-a)*(step/128.);
                QVERIFY2(!(point.x()>1+1e-9 && point.x()<2-1e-9 && point.y()>1+1e-9 && point.y()<2-1e-9),
                    qPrintable(QString("shortcut at %1,%2 dry=%3").arg(point.x()).arg(point.y()).arg(dry)));
            }
        }
    }
    void reducedHistorySurvivesLiveReversalWithoutRewritingIdentity() {
        // The half-second velocity ramp needs about 30 original samples at
        // this tight time-interpolation tolerance, in addition to older history.
        auto snapshot=field(30,2);FlowParticleSystem system;auto settings=longLife();settings.maxHistory=64;
        system.setSettings(settings);system.setField(snapshot);const auto id=system.spawn({10.13,.42});
        system.advance(3,100000);const auto first=system.frame();const auto position=system.particles().front().position;
        auto reverse=std::make_shared<FlowParticleSnapshot>(*snapshot);
        for(auto &f:reverse->fields)for(auto &v:f.velocity)v={-2,0};
        system.setField(reverse,.5);system.advance(.5,100000);
        QCOMPARE(system.particles().front().id,id);QVERIFY(std::abs(system.particles().front().position.x()-position.x())<1e-8);
        for(int i=0;i<100;++i) {auto update=std::make_shared<FlowParticleSnapshot>(*reverse);update->revision=i+2;system.setField(update);system.advance(.02,100000);}
        const auto &p=system.particles().front();QCOMPARE(p.id,id);QCOMPARE(p.lifetime,first->particles.front().lifetime);
        QCOMPARE(p.stop,FlowParticleStop::None);QVERIFY(p.history.size()<=64);QVERIFY(!p.historyLimited);
        QCOMPARE(first->particles.front().history.front().position,QPointF(10.13,.42));
    }
    void reductionPreservesNonuniformTimingOnStraightPaths() {
        auto snapshot=field(40,2);FlowParticleSystem reference,reduced;auto settings=longLife();
        settings.historyTolerance=0;reference.setSettings(settings);settings.maxHistory=16;settings.historyTolerance=.001;reduced.setSettings(settings);
        for(auto *system:{&reference,&reduced}) {system->setField(snapshot);system->spawn({.13,.42});}
        for(double speed:{2.,6.,1.}) {
            auto next=std::make_shared<FlowParticleSnapshot>(*snapshot);
            for(auto &f:next->fields)for(auto &v:f.velocity)v={speed,0};
            for(auto *system:{&reference,&reduced}) {system->setField(next);system->advance(2,100000);}
        }
        const auto &p=reduced.particles().front();QVERIFY(!p.historyLimited);QVERIFY(p.history.size()<=16);
        const auto &history=p.history;size_t index=1;
        for(const auto &sample:reference.particles().front().history) {
            while(index+1<history.size() && history[index].age<sample.age)++index;
            const auto &a=history[index-1],&b=history[index];
            const auto interpolated=a.position+(b.position-a.position)*((sample.age-a.age)/(b.age-a.age));
            const auto delta=sample.position-interpolated;
            QVERIFY(std::hypot(delta.x(),delta.y())<=settings.historyTolerance+1e-9);
        }
    }
    void realBoundariesHaveExplicitReasonsAndDoNotBridge() {
        for(auto reason:{FlowParticleStop::ClosedFace,FlowParticleStop::Dry,FlowParticleStop::Outflow}) {
            auto snapshot=field(1,1,{1,0});
            if(reason==FlowParticleStop::ClosedFace)for(auto &f:snapshot->fields)f.exits=0;
            if(reason==FlowParticleStop::Dry)for(size_t t=0;t<snapshot->fields.size();++t)
                for(int k=0;k<3;++k)snapshot->fields[t].depth[k]=float(1-snapshot->mesh->vertices[snapshot->mesh->triangles[t][k]].x());
            FlowParticleSystem system;system.setSettings(longLife());system.setField(snapshot);system.spawn({.13,.42});system.advance(2);
            const auto &p=system.particles().front();QCOMPARE(p.stop,reason);
            const double limit=reason==FlowParticleStop::ClosedFace?.42:reason==FlowParticleStop::Dry?.999:1.;
            QVERIFY(std::abs(p.position.x()-limit)<1e-8);
            for(const auto &sample:p.history)QVERIFY(sample.position.x()<=limit+1e-8);
        }
    }
    void vectorConflictCannotBounceForeverAcrossWorkSlices() {
        auto snapshot=field(1,1,{1,0});for(auto &v:snapshot->fields[0].velocity)v={-1,0};
        FlowParticleSystem system;system.setSettings(longLife());system.setField(snapshot);system.spawn({.13,.42});
        auto work=system.advance(1,1);int slices=0;while(work.pending && ++slices<1000)work=system.advance(0,1);
        QVERIFY(slices<1000);QCOMPARE(system.particles().front().stop,FlowParticleStop::VectorConflict);
    }
    void stationaryFlowLivesWithoutInventedMovement() {
        FlowParticleSystem system;auto s=longLife();s.minimumLifetime=s.maximumLifetime=1;
        system.setSettings(s);system.setField(field(2,2,{0,0}));system.spawn({.13,.42});system.advance(2);
        const auto &p=system.particles().front();QCOMPARE(p.position,QPointF(.13,.42));QCOMPARE(p.stop,FlowParticleStop::Lifetime);
    }
    void multiCellExactVertexConflictCannotLoopAcrossWorkSlices() {
        auto mesh=std::make_shared<FlowParticleMesh>();mesh->vertices={{0,0},{1,0},{-.5,.866},{-.5,-.866}};
        mesh->triangles={{0,1,2},{0,2,3},{0,3,1}};mesh->grid.rebuild({{-.5,0,1.5,.866},{-.5,-.866,.5,1.732},{-.5,-.866,1.5,.866}});
        std::atomic_bool cancel=false;mesh->prepare(cancel);
        auto snapshot=std::make_shared<FlowParticleSnapshot>();snapshot->mesh=mesh;snapshot->geometry=1;
        for(QPointF v:{QPointF(-1,0),QPointF(.5,-1),QPointF(.5,1)}) {
            FlowParticleField f;f.velocity={v,v,v};snapshot->fields.push_back(f);
        }
        FlowParticleSystem system;system.setField(snapshot);QVERIFY(system.spawn({0,0}));
        auto work=system.advance(1,1);int slices=0;
        while(work.pending && ++slices<1000)work=system.advance(0,1);
        QVERIFY(slices<1000);QCOMPARE(system.particles().front().stop,FlowParticleStop::VectorConflict);
        QCOMPARE(system.particles().front().position,QPointF(0,0));
    }
    void independentBirthLifetimesEvenWhenSeedAndIdIncreaseTogether() {
        FlowParticleSystem system;system.setField(field(2,2,{0,0}));
        std::array<int,4> bins{};
        for(int i=1;i<=100;++i) {
            const auto id=system.spawn({.13,.42},i);QCOMPARE(id,quint64(i));
            const double life=system.particles().back().lifetime;QVERIFY(life>=20 && life<40);
            ++bins[int((life-20)/5)];
        }
        for(int count:bins)QVERIFY(count>10);
    }
    void exactVerticesAndEdgeGrazingWorkInBothWindings() {
        for(bool clockwise:{false,true})for(QPointF velocity:{QPointF(1,1),QPointF(1,0)}) {
            auto snapshot=field(30,30,velocity);
            if(clockwise) {
                auto mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
                for(auto &triangle:mesh->triangles)std::swap(triangle[1],triangle[2]);
                mesh->neighbors.clear();std::atomic_bool cancel=false;mesh->prepare(cancel);snapshot->mesh=mesh;
            }
            FlowParticleSystem system;system.setSettings(longLife());system.setField(snapshot);
            system.spawn(velocity.y()==0?QPointF(.13,1):QPointF(.13,.13));
            const auto initial=system.particles().front().position;
            auto work=system.advance(20,1);int slices=0;
            while(work.pending && ++slices<10000)work=system.advance(0,1);
            QVERIFY(!work.pending);const auto &p=system.particles().front();
            QCOMPARE(p.stop,FlowParticleStop::None);QVERIFY(p.crossings>=20);
            QVERIFY(std::hypot((p.position-initial-velocity*20).x(),(p.position-initial-velocity*20).y())<1e-7);
        }
    }
    void emptyAndRetiredPopulationsAlsoRespectWorkSlices() {
        FlowParticleSystem system;system.setField(field(2,2,{0,0}));
        auto work=system.advance(10,3);QCOMPARE(work.ticks,3);QVERIFY(work.pending);
        FlowParticleSystem retired;auto s=longLife();s.minimumLifetime=s.maximumLifetime=.01;
        retired.setSettings(s);retired.setField(field(2,2,{0,0}));retired.spawn({.13,.42});retired.advance(1./60);
        QCOMPARE(retired.particles().front().stop,FlowParticleStop::Lifetime);
        work=retired.advance(10,3);QCOMPARE(work.ticks,3);QVERIFY(work.pending);
    }
    void temporalVelocityBlendPreservesIdentityAndMatchesIntegral() {
        auto initial=field(20,2,{2,0});FlowParticleSystem system;system.setSettings(longLife());
        system.setField(initial);const auto id=system.spawn({.13,.42});system.advance(.1);
        const auto position=system.particles().front().position;const auto history=system.particles().front().history;
        auto next=std::make_shared<FlowParticleSnapshot>(*initial);next->revision=2;
        for(auto &f:next->fields)for(auto &v:f.velocity)v={4,0};
        QVERIFY(system.setField(next,.5));system.advance(.5);
        const auto &p=system.particles().front();QCOMPARE(p.id,id);
        QVERIFY(std::abs(p.position.x()-position.x()-1.5)<1e-8);
        for(size_t i=0;i<history.size();++i)QCOMPARE(p.history[i].position,history[i].position);
        // A replacement during a transition starts at its current blend.
        system.setField(initial,1);system.advance(.25);
        const auto before=system.particles().front().position;
        system.setField(next,1);system.advance(1./60);
        const double moved=system.particles().front().position.x()-before.x();
        QVERIFY(moved>3.49/60 && moved<3.52/60);
    }
    void batchBirthsPublishOneCoherentFrame() {
        FlowParticleSystem system;system.setField(field(20,2));const auto before=system.frame();
        for(int i=0;i<64;++i)QVERIFY(system.spawn({.13+.01*i,.42},i,false));
        QCOMPARE(system.frame(),before);system.advance(0);
        QCOMPARE(system.frame()->births,quint64(64));QCOMPARE(system.frame()->particles.size(),size_t(64));
        QCOMPARE(system.frame()->clock,0.);
    }
    void temporalReversalIntegratesContinuouslyThroughStagnation() {
        auto initial=field(20,2,{2,0});FlowParticleSystem system;system.setSettings(longLife());system.setField(initial);
        const auto id=system.spawn({10.13,.42});const auto start=system.particles().front().position;
        auto reverse=std::make_shared<FlowParticleSnapshot>(*initial);
        for(auto &f:reverse->fields)for(auto &v:f.velocity)v={-2,0};
        system.setField(reverse,.5);system.advance(.5);const auto &p=system.particles().front();
        QCOMPARE(p.id,id);QCOMPARE(p.stop,FlowParticleStop::None);QVERIFY(std::abs(p.position.x()-start.x())<1e-8);
        system.advance(.1);QVERIFY(std::abs(system.particles().front().position.x()-start.x()+.2)<1e-8);
    }
    void depthChangesDuringReversalDoNotInventClosedFaces() {
        auto initial=field(1,1);auto next=std::make_shared<FlowParticleSnapshot>(*initial);
        for(int i=0;i<2;++i) {
            auto &a=initial->fields[size_t(i)],&b=next->fields[size_t(i)];
            a.velocityDepth=i?.01:100.;b.velocityDepth=i?100.:.01;
            for(int k=0;k<3;++k) {a.velocity[k]={2/a.velocityDepth,0};b.velocity[k]={-2/b.velocityDepth,0};}
            const auto &tri=initial->mesh->triangles[size_t(i)];a.exits=b.exits=0;
            for(int e=0;e<3;++e) {
                const auto edge=initial->mesh->vertices[tri[(e+2)%3]]-initial->mesh->vertices[tri[(e+1)%3]];
                a.outwardFlux[e]=2*edge.y();b.outwardFlux[e]=-a.outwardFlux[e];
                if(a.outwardFlux[e]>0)a.exits|=1<<e;if(b.outwardFlux[e]>0)b.exits|=1<<e;
            }
        }
        FlowParticleSystem system;system.setSettings(longLife());system.setField(initial);
        const auto id=system.spawn({.499,.5});QVERIFY(id);system.setField(next,.5);system.advance(.1);
        QCOMPARE(system.particles().front().id,id);QCOMPARE(system.particles().front().stop,FlowParticleStop::None);
        QVERIFY(system.particles().front().crossings>=1);
    }
    void dryUpdateHidesObsoleteHistoryWithoutReplacingRecordedPath() {
        auto initial=field(20,2,{2,0});FlowParticleSystem system;system.setSettings(longLife());
        system.setField(initial);const auto id=system.spawn({.13,.42});system.advance(.2);
        const auto history=system.particles().front().history;
        auto dry=std::make_shared<FlowParticleSnapshot>(*initial);dry->revision=2;
        for(auto &f:dry->fields) {f.depth={0,0,0};f.available=false;} // depth packet arrived before flux
        system.setField(dry,1);QVERIFY(system.frame()->particles.front().history.empty());
        QCOMPARE(system.particles().front().history.size(),history.size());
        system.advance(1./60);QCOMPARE(system.particles().front().id,id);
        QCOMPARE(system.particles().front().stop,FlowParticleStop::Dry);
    }
    void regionalReplacementAndMissingFacesWaitWithoutDeath() {
        auto initial=field(20,2,{2,0});auto mesh=std::make_shared<FlowParticleMesh>(*initial->mesh);
        for(int i=0;i<int(mesh->triangles.size());++i)mesh->sourceTriangles.push_back(i);
        initial->mesh=mesh;FlowParticleSystem system;system.setSettings(longLife());system.setField(initial);
        const auto id=system.spawn({.13,.42});system.advance(.1);
        auto absent=std::make_shared<FlowParticleSnapshot>(*initial);
        auto absentMesh=std::make_shared<FlowParticleMesh>(*mesh);
        for(int &i:absentMesh->sourceTriangles)i+=1000;absent->mesh=absentMesh;
        system.setField(absent);system.advance(.1);
        // Same coordinates are still resident in this synthetic replacement;
        // lookup recovers the cell even when IDs differ.
        QCOMPARE(system.particles().front().id,id);QCOMPARE(system.frame()->deaths,quint64(0));
        auto cropped=std::make_shared<FlowParticleSnapshot>(*initial);
        auto croppedMesh=std::make_shared<FlowParticleMesh>(*mesh);
        for(auto &n:croppedMesh->neighbors)n={-1,-1,-1};cropped->mesh=croppedMesh;
        for(auto &f:cropped->fields)f.residentEdges=0;
        system.setField(cropped);system.advance(1);QVERIFY(system.particles().front().waitingForData);
        const auto position=system.particles().front().position;const auto age=system.particles().front().age;
        system.advance(.1);QCOMPARE(system.particles().front().position,position);QCOMPARE(system.particles().front().age,age);
        system.setField(initial);system.advance(.1);QCOMPARE(system.particles().front().id,id);
        QVERIFY(system.particles().front().position.x()>position.x());QCOMPARE(system.frame()->deaths,quint64(0));
    }
    void nativeReconstructionPreservesFaceFluxAndCoordinateTransform() {
        auto reference=field(2,2);auto mesh=std::make_shared<FlowParticleMesh>(*reference->mesh);
        FlowParticleNativeInput in;in.mesh=mesh;in.geometry=1;in.triangles.resize(mesh->triangles.size());
        for(size_t i=0;i<in.triangles.size();++i) {
            auto &t=in.triangles[i];t.available=true;t.meanDepth=2;t.depth={2,2,2};
            const auto &ids=mesh->triangles[i];
            for(int k=0;k<3;++k)t.metres[k]=mesh->vertices[ids[k]];
            for(int e=0;e<3;++e) {
                const auto edge=t.metres[(e+2)%3]-t.metres[(e+1)%3];
                t.flux[e]=2*(3*edge.y()-edge.x()); // discharge (6,2), CCW outward
            }
        }
        // Projected feet, Y reversal, and a large origin: displacements alone
        // must scale; SI colour magnitudes must stay in metres per second.
        for(auto &p:mesh->vertices)p={1e7+p.x()/0.3048,-2e7-p.y()/0.3048};
        std::atomic_bool cancelled=false;const auto result=buildFlowParticleField(in,cancelled);QVERIFY(result);
        for(const auto &f:result->fields)for(int k=0;k<3;++k) {
            QVERIFY(std::abs(f.velocity[k].x()-3/0.3048)<1e-7);
            QVERIFY(std::abs(f.velocity[k].y()+1/0.3048)<1e-7);
            QVERIFY(std::abs(f.speed[k]-std::sqrt(10))<1e-6);
        }
        // Divergent native flux has an affine RT0 field. Tangential smoothing
        // must not change the integrated normal discharge at any edge point.
        for(size_t i=0;i<in.triangles.size();++i)in.triangles[i].flux[0]+=.1;
        for(auto &p:mesh->vertices)p={(p.x()-1e7)*0.3048,(-p.y()-2e7)*0.3048};
        const auto divergent=buildFlowParticleField(in,cancelled);QVERIFY(divergent);
        for(size_t i=0;i<in.triangles.size();++i)for(int e=0;e<3;++e)for(double fraction:{.1,.5,.9}) {
            const auto &t=in.triangles[i];const auto &f=divergent->fields[i];
            const auto edge=t.metres[(e+2)%3]-t.metres[(e+1)%3];
            const auto v=f.velocity[(e+1)%3]*(1-fraction)+f.velocity[(e+2)%3]*fraction
                +f.edgeCorrection[e]*(4*fraction*(1-fraction));
            QVERIFY(std::abs(2*(v.x()*edge.y()-v.y()*edge.x())-t.flux[e])<1e-7);
        }
    }
};
QTEST_GUILESS_MAIN(FlowParticlesTest)
#include "test_flowparticles.moc"
