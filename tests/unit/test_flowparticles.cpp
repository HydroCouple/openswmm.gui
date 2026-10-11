// GPL-3.0-or-later
#include "render/flowparticles.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
using namespace OpenSWMM::Render;

static std::shared_ptr<FlowParticleSnapshot> field(int columns,int rows,QPointF v={2,0},bool rotate=false) {
    auto mesh=std::make_shared<FlowStreakMesh>();
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
            QVERIFY(p.history.size()>=oldHistory.size());
            for(size_t j=0;j<oldHistory.size();++j)QCOMPARE(p.history[j].position,oldHistory[j].position);
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
    void historyLimitWaitsWithoutKillingTheParticle() {
        FlowParticleSystem system;auto settings=longLife();settings.maxHistory=3;
        system.setSettings(settings);system.setField(field(20,2));const auto id=system.spawn({.13,.42});
        system.advance(1);const auto age=system.particles().front().age;
        QVERIFY(system.particles().front().historyLimited);QCOMPARE(system.particles().front().stop,FlowParticleStop::None);
        system.advance(1);QCOMPARE(system.particles().front().age,age);
        settings.maxHistory=1000;system.setSettings(settings);system.advance(.1);
        QCOMPARE(system.particles().front().id,id);QVERIFY(system.particles().front().age>age);
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
                auto mesh=std::make_shared<FlowStreakMesh>(*snapshot->mesh);
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
};
QTEST_GUILESS_MAIN(FlowParticlesTest)
#include "test_flowparticles.moc"
