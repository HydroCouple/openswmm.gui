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
#include <QElapsedTimer>
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
    void historyReductionPreservesThePreviousPublishedHead() {
        FlowParticleSystem system;auto settings=longLife();settings.publicationInterval=1./30.;
        settings.maxHistory=16;settings.historyTolerance=.25;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(40,40,{},true)));QVERIFY(system.spawn({25,20.2}));
        auto previous=system.frame();
        for(int tick=0;tick<300;++tick) {
            QVERIFY(!system.advance(1./60.).pending);
            const auto frame=system.frame();if(frame==previous)continue;
            const auto &p=frame->particles.front();
            const auto sample=std::find_if(p.history.begin(),p.history.end(),[&](const auto &s){return s.age==p.previousAge;});
            QVERIFY2(sample!=p.history.end(),"History reduction removed the head still used by display interpolation");
            QCOMPARE(sample->position,previous->particles.front().position);previous=frame;
        }
    }
    void publishedHistoryAndRetirementCoverTheDisplayDelay() {
        FlowParticleSystem system;auto settings=longLife();settings.publicationInterval=1./30.;
        settings.trailSeconds=.1;settings.historyTolerance=0;settings.maxHistory=64;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,2,{1,0})));QVERIFY(system.spawn({10,.5}));
        QVERIFY(!system.advance(1).pending);
        const auto &p=system.frame()->particles.front();
        QVERIFY(p.history.front().age<=p.previousAge-settings.trailSeconds+1e-10);
        // Numerical time leads visible time by one publication interval. A
        // retired slot must remain until its displayed fade has completed.
        FlowParticleSystem retired;settings.fadeSeconds=.2;
        QVERIFY(retired.setSettings(settings));QVERIFY(retired.setField(field(1,1,{2,0})));QVERIFY(retired.spawn({.9,.5}));
        QVERIFY(!retired.advance(16./60.).pending);QCOMPARE(retired.particles().front().stop,FlowParticleStop::Outflow);
        retired.eraseFinished();QCOMPARE(retired.particles().size(),size_t(1));
        QVERIFY(!retired.advance(.05).pending);retired.eraseFinished();QVERIFY(retired.particles().empty());
    }
    void fadingTrailsDoNotBlockTheNextActiveBirth() {
        const auto snapshot=field(100,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        FlowParticleSettings settings;settings.maxParticles=16;settings.minimumLifetime=settings.maximumLifetime=.2;
        settings.fadeSeconds=.6;settings.publicationInterval=1./30.;
        FlowParticleController controller;controller.configure(settings,{10,0,20,2},1);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        int longestGap=0,gap=0;bool witnessedFadingAndActive=false;
        for(int i=0;i<72;++i) {
            controller.advance(1./60.);controller.waitForDone();
            const auto frame=controller.frame(1);QVERIFY(frame);int active=0,fading=0;
            for(const auto &p:frame->particles) {active+=p.stop==FlowParticleStop::None;fading+=p.stop!=FlowParticleStop::None;}
            gap=active?0:gap+1;longestGap=std::max(longestGap,gap);witnessedFadingAndActive|=active && fading;
            QVERIFY(active<=1);QVERIFY(frame->particles.size()<=size_t(settings.maxParticles));
        }
        QVERIFY2(longestGap<=2,"Retired trails blocked replacement heads during their fade");
        QVERIFY(witnessedFadingAndActive);QVERIFY(controller.frame(1)->births>=5);
    }
    void prewarmPublishesMatureTrailsWithStaggeredAges() {
        const auto snapshot=field(200,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        FlowParticleSettings settings;settings.publicationInterval=1./30.;settings.maxParticles=200;
        settings.maxHistory=128;settings.trailSeconds=1.5;
        FlowParticleAppearance appearance;FlowParticleController controller;
        controller.configure(settings,{10,0,20,2},200,{100,40},appearance,snapshot->mesh->grid);
        int emptyTrails=0,publications=0;
        connect(&controller,&FlowParticleController::ready,this,[&] {
            if(const auto frame=controller.frame(1)) {
                ++publications;
                for(const auto &p:frame->particles)emptyTrails+=p.history.size()<2;
            }
        });
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        const auto frame=controller.frame(1);QVERIFY(frame);QVERIFY(!controller.busy());
        QVERIFY(std::abs(frame->clock-2)<1e-8);QCOMPARE(frame->particles.size(),size_t(200));
        QCOMPARE(publications,1);QVERIFY(emptyTrails<20);
        double youngest=100,oldest=0;
        for(const auto &p:frame->particles) {youngest=std::min(youngest,p.age);oldest=std::max(oldest,p.age);}
        QVERIFY(oldest-youngest>15);QVERIFY(frame->deaths>0);
        QVERIFY(!frame->ribbon.vertices.empty());QCOMPARE(controller.diagnostics().failedBirths,quint64(0));
        QVERIFY(controller.diagnostics().prewarmMs>0);QCOMPARE(controller.opacity(),0.);
        controller.advance(.15);QVERIFY(std::abs(controller.opacity()-.5)<1e-8);controller.waitForDone();
        QVERIFY(std::abs(controller.opacity()-.5)<1e-8);
        FlowParticleView paused{100,40};paused.animated=false;
        controller.configure(settings,{10,0,20,2},200,paused,appearance,snapshot->mesh->grid);
        QTest::qWait(30);QVERIFY(std::abs(controller.opacity()-.5)<1e-8);
        controller.configure(settings,{10,0,20,2},200,{100,40},appearance,snapshot->mesh->grid);
        controller.advance(.15);controller.waitForDone();QVERIFY(std::abs(controller.opacity()-1)<1e-8);
        controller.request(input,true);controller.waitForDone();QCOMPARE(controller.opacity(),0.);
        QVERIFY(std::abs(controller.frame(1)->clock-2)<1e-8);
        controller.configure(settings,{10,0,20,2},200,paused,appearance,snapshot->mesh->grid);
        controller.request(input,true);controller.waitForDone();QCOMPARE(controller.opacity(),1.);
    }
    void aNewViewFillsWithoutWaitingForOffscreenLifetimes() {
        const auto snapshot=field(200,20,{.01,0});
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={.01,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        FlowParticleSettings settings;settings.maxParticles=200;settings.maxHistory=64;
        settings.minimumLifetime=settings.maximumLifetime=100;settings.publicationInterval=1./30.;
        FlowParticleController controller;const QRectF original(10,2,20,10),panned(150,2,20,10);
        controller.configure(settings,original,50,{10,40});controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        const auto first=controller.frame(1);QVERIFY(first);const auto survivor=first->particles.front().id;
        controller.configure(settings,panned,50,{10,40});
        for(int i=0;i<12;++i){controller.advance(1./60.);controller.waitForDone();}
        const auto next=controller.frame(1);QVERIFY(next);int visible=0;bool retained=false;
        for(const auto &p:next->particles){visible+=p.stop==FlowParticleStop::None && panned.contains(p.position);retained|=p.id==survivor;}
        QVERIFY2(visible>=49,qPrintable(QString("Only %1 heads reached the new view after 0.2 seconds").arg(visible)));
        QVERIFY(retained);QVERIFY(next->particles.size()<=size_t(settings.maxParticles));
        // The same view change must finish all bounded birth batches even
        // when each batch first has to reclaim an invisible full-pool slot.
        controller.configure(settings,original,200,{100,40});controller.waitForDone();
        controller.configure(settings,panned,200,{100,40});controller.waitForDone();
        visible=0;for(const auto &p:controller.frame(1)->particles)visible+=p.stop==FlowParticleStop::None && panned.contains(p.position);
        QVERIFY(visible>=199);QVERIFY(controller.diagnostics().offscreenReplacements>0);
        QVERIFY(controller.frame(1)->particles.size()<=size_t(settings.maxParticles));
    }
    void fullPoolsRecycleOnlyInvisibleTrajectories() {
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=3;settings.maxHistory=64;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(200,20,{1,0})));
        const auto invisible=system.spawn({10,5}),visible=system.spawn({155,5}),crossing=system.spawn({145,5});
        QVERIFY(invisible && visible && crossing);QVERIFY(!system.advance(10).pending);
        // The second head is outside this small view, but its trail crosses it.
        const QRectF view(150,0,10,10);QCOMPARE(system.releaseOffscreen(view,2),1);
        for(const auto &p:system.particles())QVERIFY(p.id!=invisible);
        QCOMPARE(system.particles().size(),size_t(2));QCOMPARE(system.frame()->deaths,quint64(0));
        QVERIFY(system.spawn({152,5}));QCOMPARE(system.particles().size(),size_t(3));
    }
    void agedBirthRetainsAnExactForwardTrajectory() {
        const auto snapshot=field(200,2,{1,0});FlowParticleSystem young,aged;
        auto settings=longLife();settings.maxHistory=128;
        for(auto *system:{&young,&aged}) {QVERIFY(system->setSettings(settings));QVERIFY(system->setField(snapshot));}
        QVERIFY(young.spawn({10,.5},7));QVERIFY(aged.spawn({10,.5},7,true,-1,.5));
        QCOMPARE(aged.particles().front().age,50.);QCOMPARE(aged.particles().front().history.front().age,50.);
        for(int i=0;i<120;++i) {
            QVERIFY(!young.advance(1./60.).pending);QVERIFY(!aged.advance(1./60.).pending);
            QCOMPARE(aged.particles().front().position,young.particles().front().position);
            QVERIFY(std::abs(aged.particles().front().age-young.particles().front().age-50)<1e-10);
        }
        QVERIFY(!aged.spawn({10,.5},7,true,-1,1));QVERIFY(!aged.spawn({10,.5},7,true,-1,NAN));
        QCOMPARE(aged.frame()->deaths,quint64(0));
    }
    void displayInterpolationAdvancesBeforeWorkerCompletion() {
        const auto snapshot=field(200,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        auto settings=longLife();settings.publicationInterval=1./30.;
        FlowParticleController controller;controller.configure(settings,{10,0,20,2},20);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        controller.advance(1./30.);controller.waitForDone();
        const auto frame=controller.frame(1);QVERIFY(frame);QVERIFY(frame->clock>frame->previousClock);
        QVERIFY(std::abs(controller.interpolation())<1e-8);
        // GUI time must move the existing frame even while the queued worker
        // completion has not been delivered. No event processing occurs here.
        controller.advance(1./120.);
        QCOMPARE(controller.frame(1),frame);QVERIFY(std::abs(controller.interpolation()-.25)<1e-8);
        controller.advance(1./120.);QVERIFY(std::abs(controller.interpolation()-.5)<1e-8);
        controller.waitForDone();QVERIFY(std::abs(controller.interpolation()-.5)<1e-8);
        const auto visibleAge=[&] {
            const auto &p=controller.frame(1)->particles.front();
            return p.previousAge+(p.age-p.previousAge)*controller.interpolation();
        };
        const double before=visibleAge();controller.advance(1./60.);const double queued=visibleAge();
        QVERIFY(queued>before);controller.waitForDone();QVERIFY(std::abs(visibleAge()-queued)<1e-8);
        const double paused=controller.interpolation();QTest::qWait(50);QCOMPARE(controller.interpolation(),paused);
        auto next=std::make_shared<FlowParticleNativeInput>(*input);next->revision=2;
        controller.setPresentedRevision(2);controller.request(next);controller.waitForDone();
        const auto &p=controller.frame(2)->particles.front();
        QVERIFY(std::abs(p.previousAge+(p.age-p.previousAge)*controller.interpolation()-queued)<1e-8);
        controller.request(next,true);controller.waitForDone();QCOMPARE(controller.frame(2)->clock,0.);
        QCOMPARE(controller.interpolation(),0.);
    }
    void resetUnpinsTheOldFrameAfterItsLastReader() {
        const auto snapshot=field(200,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        auto settings=longLife();settings.maxParticles=8;settings.maxHistory=64;
        FlowParticleAppearance appearance;
        FlowParticleController controller;controller.configure(settings,{10,0,20,2},4,{16,10},appearance);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        controller.advance(1);controller.waitForDone();auto held=controller.frame(1);QVERIFY(held);
        QVERIFY2(held->ribbon.vertices.capacity()>=8*64*2,"The packet reserves only active heads, forcing reallocations for fading trails");
        auto next=std::make_shared<FlowParticleNativeInput>(*input);next->revision=2;
        controller.setPresentedRevision(2);controller.request(next,true);controller.waitForDone();
        QVERIFY(controller.frame(2));QCOMPARE(controller.frame(1),held);
        held.reset();controller.advance(1./60.);controller.waitForDone();
        QVERIFY2(!controller.frame(1),"An unobserved pre-seek frame remains pinned instead of returning to its pool");
    }
    void partialTicksKeepTheCompletedFramePrivateFromStaging() {
        FlowParticleSystem system;auto settings=longLife();settings.maxHistory=64;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(20,2,{1,0})));
        QVERIFY(system.spawn({.13,.42},1,false));QVERIFY(system.spawn({2.13,.42},2,false));
        system.advance(0);QCOMPARE(system.frame()->particles.front().age,0.);
        // No external frame handle is retained here. The published buffer
        // must still remain complete when a later particle exhausts the slice.
        auto work=system.advance(settings.timestep,1);QVERIFY(work.pending);
        QVERIFY(system.particles().front().age>0);QCOMPARE(system.frame()->clock,0.);
        for(const auto &p:system.frame()->particles)QCOMPARE(p.age,0.);
        while(work.pending)work=system.advance(0,1);
        for(const auto &p:system.frame()->particles)QCOMPARE(p.age,settings.timestep);
        settings.publicationInterval=1./30.;QVERIFY(system.setSettings(settings));
        QVERIFY(!system.advance(3*settings.timestep).pending);
        work=system.advance(settings.timestep,1);QVERIFY(work.pending);
        const double previousAge=system.particles().front().previousAge;
        settings.publicationInterval=0;QVERIFY(system.setSettings(settings));
        while(work.pending)work=system.advance(0,1);
        for(const auto &p:system.frame()->particles)QCOMPARE(p.previousAge,previousAge);
    }
    void sharedHistoryPagesStayImmutableThroughWrapAndReduction() {
        FlowParticleRing<FlowParticleSample> history;history.reserve(65);
        for(int i=0;i<64;++i)history.push_back({{double(i),1},double(i),i});
        std::array<FlowParticleRing<FlowParticleSample>,4> retained;
        for(auto &copy:retained)copy=history;
        QCOMPARE(&retained[0][0],&history[0]); // publication retains storage
        tickAllocations=0;countTickAllocations=true;
        for(int i=64;i<1000;++i) {
            history.pop_front();history.push_back({{double(i),1},double(i),i});
            if(i%31==0)retained[size_t(i%4)]=history;
        }
        countTickAllocations=false;QCOMPARE(tickAllocations,size_t(0));
        QCOMPARE(history.front().age,936.);QCOMPARE(history.back().age,999.);
        for(const auto &copy:retained)for(size_t i=1;i<copy.size();++i) {
            QCOMPARE(copy[i].age,copy.front().age+double(i));QCOMPARE(copy[i].position.x(),copy[i].age);
        }
        const auto old=retained[0];const double age=old.front().age;
        retained[0].mutableAt(0).age=-10;QCOMPARE(old.front().age,age);
        history.reserve(256);QCOMPARE(old.front().age,age);QCOMPARE(history.back().age,999.);
    }
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
    void warmedResetsReuseBuffersAndKeepReadersImmutable() {
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=8;settings.maxHistory=64;
        const auto snapshot=field(100,2,{1,0});QVERIFY(system.setSettings(settings));QVERIFY(system.setField(snapshot));
        FlowParticleAppearance appearance;appearance.particleCapacity=8;appearance.historyCapacity=64;
        system.setAppearance(1,appearance);
        for(int p=0;p<8;++p)QVERIFY(system.spawn({20+p*.1,.5},p,false));
        std::array<std::shared_ptr<const FlowParticleFrame>,4> warm;
        for(auto &frame:warm){QVERIFY(!system.advance(.1,100000).pending);frame=system.frame();}
        const auto held=warm.front();const auto original=held->particles.front().history.front().position;warm={};
        const auto reset=[&] {system.reset();};
        bool complete=true;tickAllocations=0;countTickAllocations=true;
        for(int cycle=0;cycle<8;++cycle) {
            reset();complete&=system.clock()==0 && system.lagSeconds()==0 && system.particles().empty();
            complete&=system.setField(snapshot);
            for(int p=0;p<8;++p)complete&=system.spawn({40+cycle+p*.1,.5},p,false)!=0;
            complete&=!system.advance(.1,100000).pending;
        }
        countTickAllocations=false;QVERIFY(complete);QCOMPARE(tickAllocations,size_t(0));
        QCOMPARE(held->particles.front().history.front().position,original);
        QCOMPARE(system.frame()->particles.front().id,quint64(1));QCOMPARE(system.frame()->births,quint64(8));
        QCOMPARE(system.frame()->deaths,quint64(0));QVERIFY(std::abs(system.clock()-.1)<1e-10);
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
        const auto sample=held.back()->particles.front().history.back();
        tickAllocations=0;countTickAllocations=true;
        const auto work=system.advance(3);countTickAllocations=false;
        QVERIFY(!work.pending);QCOMPARE(tickAllocations,size_t(0));
        QCOMPARE(held.back()->particles.front().position,old);QCOMPARE(held.back()->particles.front().age,age);
        QCOMPARE(held.back()->particles.front().history.back().position,sample.position);
        QCOMPARE(held.back()->particles.front().history.back().age,sample.age);
        QVERIFY(system.particles().front().position.x()>old.x());
        const auto residency=flowParticleResidencyForParticles({0,0,1,1},field(100,2)->mesh->grid,system.particles(),2,false);
        const auto head=system.particles().front().position;
        QVERIFY(std::any_of(residency.rectangles.begin(),residency.rectangles.end(),[&](QRectF r){return r.contains(head);}));
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
        settings.publicationInterval=1./30.;
        QVERIFY(system.setSettings(settings));QVERIFY(system.setField(field(100,2,{1,0})));
        FlowParticleAppearance appearance;appearance.particleCapacity=2000;appearance.historyCapacity=64;
        system.setAppearance(1,appearance);
        for(int i=0;i<2000;++i)QVERIFY(system.spawn({20+i*.001,.5},i,false));
        auto work=system.advance(1.1,1000000);QVERIFY(!work.pending);
        std::vector<double> times,workerTimes,ribbonTimes;
        for(int i=0;i<120;++i) {
            QElapsedTimer timer;timer.start();
            work=system.advance(settings.publicationInterval,1000000);QVERIFY(!work.pending);
            workerTimes.push_back(timer.nsecsElapsed()/1e6);
            times.push_back(system.frame()->publicationMs);
            ribbonTimes.push_back(system.frame()->ribbon.buildMs);
        }
        std::sort(times.begin(),times.end());std::sort(workerTimes.begin(),workerTimes.end());std::sort(ribbonTimes.begin(),ribbonTimes.end());
        QFile report(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/publication-budget.json");
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"population",2000},{"historyCapacity",64},
            {"samples",120},{"publicationMedianMs",times[59]},{"publicationP95Ms",times[113]},{"publicationMaxMs",times.back()},
            {"workerMedianMs",workerTimes[59]},{"workerP95Ms",workerTimes[113]},{"workerMaxMs",workerTimes.back()},
            {"ribbonMedianMs",ribbonTimes[59]},{"ribbonP95Ms",ribbonTimes[113]}}).toJson());
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
        FlowParticleSystem system;auto settings=longLife();settings.maxParticles=2000;settings.maxHistory=64;
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
        FlowParticleController controller;controller.configure(longLife(),{10,0,20,2},20,{}, {},snapshot->mesh->grid);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        auto old=controller.frame(1);QVERIFY(old);QCOMPARE(old->particles.size(),size_t(20));
        const auto initialSupport=controller.residency();QVERIFY(initialSupport);
        const auto initialTiles=initialSupport->required.tiles;
        const auto id=old->particles.front().id;const auto start=old->particles.front().position;
        for(int i=1;i<=60;++i) {
            auto next=std::make_shared<FlowParticleNativeInput>(*input);next->revision=i+1;
            controller.request(next);controller.waitForDone();controller.advance(1);controller.waitForDone();
            const auto current=controller.frame(1);QVERIFY(current);
            QVERIFY(std::abs(current->clock-i)<1e-8);QVERIFY(!controller.frame(i+1));
        }
        const auto current=controller.frame(1);QCOMPARE(current->particles.front().id,id);
        const auto support=controller.residency();QVERIFY(support);QCOMPARE(support->geometry,current->geometry);
        QVERIFY(current->clock-support->clock<.25+1e-9);
        QCOMPARE(initialSupport->required.tiles,initialTiles);
        const auto required=flowParticleResidency({10,0,20,2},snapshot->mesh->grid,current.get(),.5,false);
        QCOMPARE(support->required.tiles,required.tiles);
        QVERIFY(std::abs(current->particles.front().position.x()-start.x()-120)<1e-7);
        controller.setPresentedRevision(61);controller.waitForDone();
        const auto updated=controller.frame(61);QVERIFY(updated);QCOMPARE(updated->clock,current->clock);
        QCOMPARE(updated->particles.front().position,current->particles.front().position);
        QCOMPARE(controller.diagnostics().failedBirths,quint64(0));
    }
    void depthFrameHandoffKeepsTheDisplayedTrailsUntilBothPacketsAreReady() {
        const auto snapshot=field(200,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        auto settings=longLife();settings.maxParticles=32;settings.maxHistory=128;
        FlowParticleController controller;controller.configure(settings,{10,0,20,2},20);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        controller.advance(1);controller.waitForDone();auto shown=controller.frame(1);QVERIFY(shown);
        const auto id=shown->particles.front().id, births=shown->births;
        const auto position=shown->particles.front().position;
        const auto nextPacket=[&](quint64 revision) {
            auto next=std::make_shared<FlowParticleNativeInput>(*input);next->revision=revision;
            for(auto &t:next->triangles) {t.meanDepth=.5;t.depth={.5,.5,.5};}
            return next;
        };
        auto next=nextPacket(2);
        controller.request(next);controller.waitForDone();
        // A prepared hydraulic packet alone does not produce a matching
        // display frame: publishing the new water here would blank streaks.
        QVERIFY(!controller.frame(2));
        controller.prepareForPresentation(next);
        QCOMPARE(controller.frame(1),shown); // No wait on the GUI path.
        controller.waitForDone();auto ready=controller.frame(2);QVERIFY(ready);
        QCOMPARE(ready->particles.front().id,id);QCOMPARE(ready->particles.front().position,position);
        QCOMPARE(ready->births,births);QCOMPARE(ready->particles.front().history.size(),shown->particles.front().history.size());
        // A newer source packet cannot steal the handoff from the water
        // frame which is actually ready. Keep the old frame without a reader.
        auto newer=nextPacket(3);controller.request(newer);
        shown.reset();controller.advance(.05);controller.waitForDone();
        QVERIFY(controller.frame(1));QVERIFY(controller.frame(2));QVERIFY(!controller.frame(3));
        controller.setPresentedRevision(2);controller.waitForDone();
        QVERIFY(controller.frame(2));QVERIFY(!controller.frame(1));
        controller.prepareForPresentation(newer);controller.waitForDone();QVERIFY(controller.frame(3));
        controller.setPresentedRevision(3);controller.waitForDone();
        for(quint64 revision=4;revision<=20;++revision) {
            auto packet=nextPacket(revision);controller.request(packet);controller.waitForDone();
            QVERIFY(controller.frame(revision-1));QVERIFY(!controller.frame(revision));
            controller.prepareForPresentation(packet);controller.waitForDone();
            const auto frame=controller.frame(revision);QVERIFY(frame);
            QCOMPARE(frame->particles.front().id,id);QCOMPARE(frame->births,births);
            QVERIFY(frame->particles.front().history.size()>1);
            controller.setPresentedRevision(revision);controller.advance(1./60.);controller.waitForDone();
        }
        // A water candidate can itself be superseded before display. Keep
        // the actually displayed frame, rather than the intermediate one.
        controller.prepareForPresentation(nextPacket(21));controller.waitForDone();QVERIFY(controller.frame(21));
        controller.prepareForPresentation(nextPacket(22));controller.waitForDone();
        QVERIFY(controller.frame(20));QVERIFY(controller.frame(22));
        controller.setPresentedRevision(22);controller.waitForDone();QVERIFY(!controller.frame(20));
    }
    void depthFrameHandoffNeverShowsOldTrailsOnNewlyDryWater() {
        const auto snapshot=field(100,2);
        auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=std::make_shared<FlowParticleMesh>(*snapshot->mesh);
        input->geometry=1;input->revision=1;
        for(const auto &ids:input->mesh->triangles) {
            FlowParticleNativeTriangle t;t.available=true;t.meanDepth=1;t.depth={1,1,1};t.velocityOnly=true;t.velocitySI={2,0};
            for(int k=0;k<3;++k)t.metres[k]=input->mesh->vertices[ids[k]];
            input->triangles.push_back(t);
        }
        auto settings=longLife();settings.maxParticles=8;settings.maxHistory=128;
        FlowParticleController controller;controller.configure(settings,{10,0,20,2},4);
        controller.setPresentedRevision(1);controller.request(input);controller.waitForDone();
        controller.advance(1);controller.waitForDone();const auto old=controller.frame(1);QVERIFY(old);
        QVERIFY(old->particles.front().history.size()>1);
        auto dry=std::make_shared<FlowParticleNativeInput>(*input);dry->revision=2;
        for(auto &t:dry->triangles) {t.meanDepth=0;t.depth={0,0,0};}
        controller.prepareForPresentation(dry);QCOMPARE(controller.frame(1),old);
        controller.waitForDone();const auto ready=controller.frame(2);QVERIFY(ready);
        QCOMPARE(controller.frame(1),old);QVERIFY(old->particles.front().history.size()>1);
        for(const auto &p:ready->particles)QVERIFY(p.history.empty());
        controller.setPresentedRevision(2);controller.waitForDone();QCOMPARE(controller.frame(2),ready);
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
        auto merged=flowParticleResidency({1,1,5,5},source->mesh->grid,nullptr);
        const auto support=flowParticleResidency({1,1,5,5},source->mesh->grid,&frame,2,false);
        mergeFlowParticleResidency(merged,support,source->mesh->grid);QCOMPARE(merged.tiles,regions.tiles);
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
    void diagonalResidencyPinsTheCorridorWithoutItsWholeBoundingBox() {
        FlowParticleResidencyGrid grid;grid.extent={0,0,100,100};grid.cellW=grid.cellH=1;grid.cols=grid.rows=100;
        FlowParticleFrame frame;FlowParticle p;p.position={90,90};
        p.history={{{10,10},0,0},{{90,90},10,0}};frame.particles.push_back(p);
        const auto support=flowParticleResidency({0,0,1,1},grid,&frame,2,false);
        QVERIFY(!support.limited);QVERIFY(support.tiles.size()<3000);
        for(int i=10;i<100;++i)QVERIFY(std::any_of(support.rectangles.begin(),support.rectangles.end(),
            [&](const QRectF &r){return r.contains(QPointF(i,i));}));
        QVERIFY(std::none_of(support.rectangles.begin(),support.rectangles.end(),[](const QRectF &r){return r.contains(QPointF(10,90));}));
        const auto horizontal=flowParticleResidency(QRectF(1,1,10,1),grid,nullptr);
        QVERIFY(horizontal.rectangles.size()<int(horizontal.tiles.size()));
    }
    void overviewWeightsLimitBirthsToNativeSeedCells() {
        auto source=field(10,2);auto mesh=std::make_shared<FlowParticleMesh>(*source->mesh);
        mesh->seedWeights.assign(mesh->triangles.size(),0);mesh->seedWeights.front()=100;source->mesh=mesh;
        const auto seeds=buildFlowParticleSeeds(*source,{0,0,10,2},.001);
        QVERIFY(seeds.wetArea>0);QVERIFY(!seeds.triangles.empty());
        for(int i=0;i<100;++i)QCOMPARE(seeds.sample(i).second,0);
        for(const auto &seed:seeds.triangles)QCOMPARE(seed.cell,0);
        const auto &tri=mesh->triangles.front();const auto a=mesh->vertices[tri[1]]-mesh->vertices[tri[0]],b=mesh->vertices[tri[2]]-mesh->vertices[tri[0]];
        QCOMPARE(seeds.wetArea,std::abs(a.x()*b.y()-a.y()*b.x())*50);
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
