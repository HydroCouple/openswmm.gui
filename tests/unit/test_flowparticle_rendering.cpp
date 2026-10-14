// GPL-3.0-or-later
#include "map/flowparticlenode.h"
#include <QTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
using namespace OpenSWMM::Render;
static std::shared_ptr<FlowParticleFrame> bend() {
    auto frame=std::make_shared<FlowParticleFrame>();frame->clock=2;
    FlowParticle particle;particle.id=1;particle.age=2;particle.previousAge=1.5;particle.lifetime=100;
    particle.history={{{20,60},0,0},{{100,60},1,1},{{100,20},2,2}};
    frame->particles.push_back(particle);return frame;
}
class Preview:public QQuickItem {
public:
    std::shared_ptr<FlowParticleFrame> frame=bend();double alpha=1,fade=0;
    std::vector<double> syncTimes;
    Preview(QQuickItem *parent):QQuickItem(parent) {setFlag(ItemHasContents);setSize({160,80});}
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *node=old?static_cast<FlowParticleNode*>(old):new FlowParticleNode(window());
        const auto uploads=node->uploads();
        node->sync(frame,alpha,1,4,4,4,fade);
        if(node->uploads()!=uploads)syncTimes.push_back(node->geometryBuildMs());return node;
    }
};
class FlowParticleRenderingTest:public QObject {
    Q_OBJECT
private slots:
    void nativePublicationKeepsTheVisibleHeadAndBrightnessContinuous() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Native publication continuity");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::black);window.resize(240,80);auto *preview=new Preview(window.contentItem());preview->setSize({240,80});
        preview->frame=std::make_shared<FlowParticleFrame>();preview->frame->clock=2;preview->frame->previousClock=2-1./30.;
        FlowParticle p;p.id=1;p.age=2;p.previousAge=2-1./30.;p.lifetime=100;
        p.history={{{20,40},0,0},{{68,40},1,0},{{116,40},2,0}};preview->frame->particles.push_back(p);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto brightness=[](const QImage &image) {
            double total=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)total+=qRed(image.pixel(x,y));return total;
        };
        double maximumChange=0,maximumHeadError=0,previousHead=0;
        for(int tick=0;tick<15;++tick) {
            preview->alpha=1;preview->update();const auto before=window.grabWindow();QVERIFY(!before.isNull());
            auto next=std::make_shared<FlowParticleFrame>(*preview->frame);
            next->previousClock=next->clock;next->clock+=1./30.;
            auto &particle=next->particles.front();particle.previousAge=particle.age;particle.age+=1./30.;
            particle.history.push_back({{20+48*particle.age,40},particle.age,0});
            preview->frame=next;preview->alpha=0;preview->update();const auto after=window.grabWindow();QVERIFY(!after.isNull());
            const double change=std::abs(brightness(after)-brightness(before))/brightness(before);
            maximumChange=std::max(maximumChange,change);QVERIFY(change<.02);
            for(double alpha:{0.,.5,1.}) {
                preview->alpha=alpha;preview->update();const auto image=window.grabWindow();
                const double dpr=image.width()/240.;const int y=int(40.5*dpr);int last=-1;
                for(int x=0;x<image.width();++x)if(qRed(image.pixel(x,y))>5)last=x;
                const double head=(last+.5)/dpr,expected=20+48*(particle.previousAge+(particle.age-particle.previousAge)*alpha);
                QVERIFY(head>=previousHead);previousHead=head;
                maximumHeadError=std::max(maximumHeadError,std::abs(head-expected));QVERIFY(std::abs(head-expected)<1.1);
            }
        }
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));
        QFile report(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/publication-continuity.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"publications",15},{"interpolatedFrames",45},
            {"maximumHandoffBrightnessChange",maximumChange},{"maximumHeadErrorPx",maximumHeadError}}).toJson());
    }
    void nativeRetirementFadesBetweenPublications() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Native retirement presentation clock");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::black);window.resize(160,80);auto *preview=new Preview(window.contentItem());
        preview->frame=std::make_shared<FlowParticleFrame>();preview->frame->previousClock=1;preview->frame->clock=2;
        FlowParticle p;p.id=1;p.age=p.previousAge=1;p.lifetime=100;p.stoppedAt=1;
        p.history={{{20,40},0,0},{{140,40},1,0}};preview->frame->particles.push_back(p);preview->fade=1;
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        std::vector<double> brightness;
        for(double alpha:{0.,.25,.5,.75,1.}) {
            preview->alpha=alpha;preview->update();const auto image=window.grabWindow();QVERIFY(!image.isNull());
            const double dpr=image.width()/160.;brightness.push_back(qRed(image.pixel(int(100.5*dpr),int(40.5*dpr)))/255.);
            QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));
            QVERIFY(image.save(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+QString("/retirement-%1.png").arg(alpha)));
        }
        QVERIFY(brightness.front()>.5);QVERIFY(brightness.back()<.01);
        for(size_t i=1;i<brightness.size();++i)QVERIFY(brightness[i]<brightness[i-1]);
        for(size_t i=0;i<brightness.size();++i) {
            const double t=double(i)/4,expected=1-t*t*(3-2*t);
            QVERIFY(std::abs(brightness[i]/brightness.front()-expected)<.015);
        }
    }
    void ribbonBudgetAtTwoThousandParticles() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_BENCHMARK"))QSKIP("Opt-in ribbon synchronization measurement");
        auto frame=std::make_shared<FlowParticleFrame>();frame->clock=2;
        for(int i=0;i<2000;++i) {
            FlowParticle p;p.id=i+1;p.age=2;p.previousAge=2-1./30.;p.lifetime=40;
            for(int j=0;j<64;++j)p.history.push_back({{double(j),double(i)},2-(63-j)/30.,0,1});
            frame->particles.push_back(std::move(p));
        }
        FlowParticleAppearance appearance;
        appearance.particleCapacity=2000;appearance.historyCapacity=64;
        buildFlowParticleRibbon(*frame,1,0,appearance,frame->ribbon);
        FlowParticleNode node;std::vector<double> times;
        for(int i=0;i<9;++i) {
            auto publication=std::make_shared<FlowParticleFrame>(*frame);
            node.sync(publication,1,1,2,1.5,2.5,.6);times.push_back(node.geometryBuildMs());
        }
        std::sort(times.begin(),times.end());
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));
        QFile report(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/ribbon-budget.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"population",2000},{"historyCapacity",64},
            {"syncMedianMs",times[4]},{"syncMaxMs",times.back()},
            {"vertexBytes",node.geometry()->vertexCount()*node.geometry()->sizeOfVertex()},
            {"timingBytes",qint64(frame->ribbon.timing.size()*sizeof(FlowParticleTiming))},
            {"indexBytes",node.geometry()->indexCount()*4},{"indexUploads",node.indexUploads()}}).toJson());
    }
    void nativePacketCopyIncludesTimingTexture() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE") || !qEnvironmentVariableIsSet("FLOW_PARTICLE_BENCHMARK"))
            QSKIP("Opt-in native packet synchronization measurement");
        auto frame=std::make_shared<FlowParticleFrame>();frame->clock=2;
        for(int i=0;i<2000;++i) {
            FlowParticle p;p.id=i+1;p.age=2;p.previousAge=2-1./30.;p.lifetime=40;
            for(int j=0;j<64;++j)p.history.push_back({{double(j),double(i)},2-(63-j)/30.,0,1});
            frame->particles.push_back(std::move(p));
        }
        FlowParticleAppearance appearance;appearance.particleCapacity=2000;appearance.historyCapacity=64;
        buildFlowParticleRibbon(*frame,1,0,appearance,frame->ribbon);
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.resize(160,80);auto *preview=new Preview(window.contentItem());preview->frame=frame;
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        for(int i=0;i<20;++i) {
            preview->frame=std::make_shared<FlowParticleFrame>(*frame);preview->update();
            QVERIFY(!window.grabWindow().isNull());
        }
        QVERIFY(preview->syncTimes.size()>=20);
        const double cold=preview->syncTimes.front();
        std::vector<double> warm(preview->syncTimes.begin()+1,preview->syncTimes.end());std::sort(warm.begin(),warm.end());
        QFile report(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/native-packet-budget.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"population",2000},{"historyCapacity",64},
            {"syncMedianMs",warm[(warm.size()-1)/2]},{"syncP95Ms",warm[size_t(.95*(warm.size()-1))]},
            {"syncMaxMs",warm.back()},{"coldSyncMs",cold},{"samples",qint64(warm.size())},
            {"vertexBytes",qint64(frame->ribbon.vertices.size()*sizeof(FlowParticleVertex))},
            {"timingBytes",qint64(frame->ribbon.timing.size()*sizeof(FlowParticleTiming))}}).toJson());
    }
    void adjacentSegmentsShareExactlyTheSameJoint() {
        FlowParticleNode node;const auto frame=bend();node.sync(frame,1,1,4,4,4,0);
        QCOMPARE(node.geometry()->vertexCount(),6);
        QCOMPARE(node.geometry()->indexCount(),12);
        const auto *indices=node.geometry()->indexDataAsUInt();
        QCOMPARE(indices[5],indices[6]);QCOMPARE(indices[2],indices[7]);
        const int indexUploads=node.indexUploads();
        const int uploads=node.uploads();const auto *data=node.geometry()->vertexData();
        for(double alpha:{0.,.25,.5,.75,1.})node.sync(frame,alpha,1,4,4,4,0);
        QCOMPARE(node.uploads(),uploads);QCOMPARE(node.geometry()->vertexData(),data);
        node.sync(frame,1,2,4,4,4,0);QCOMPARE(node.uploads(),uploads+1);
        QCOMPARE(node.indexUploads(),indexUploads);
    }
    void compactTimingPreservesFloatBitsAndOppositeRibbonSides() {
        auto frame=bend();auto &p=frame->particles.front();p.previousAge=1.234567;p.age=1.345678;
        p.lifetime=12345.67;p.stoppedAt=-1;p.historyLimited=true;
        FlowParticleRibbon ribbon;buildFlowParticleRibbon(*frame,1,0,{},ribbon);
        const auto &timing=ribbon.timing.front();
        QCOMPARE(flowParticleTimingValue(timing,0),float(p.previousAge));
        QCOMPARE(flowParticleTimingValue(timing,1),float(p.age));
        QCOMPARE(flowParticleTimingValue(timing,2),float(p.lifetime));
        QCOMPARE(flowParticleTimingValue(timing,3),-1.f);
        QCOMPARE(flowParticleTimingValue(timing,4),float(p.history.front().age));
        QCOMPARE(ribbon.vertices[0].particle,-1.f);QCOMPARE(ribbon.vertices[1].particle,1.f);
        QCOMPARE(sizeof(FlowParticleVertex),size_t(28));
    }
    void emptyHistoryAndUnavailableScaleHaveNoDrawGeometry() {
        FlowParticleNode node;auto frame=bend();frame->particles.front().history.clear();node.sync(frame,1,1);
        QCOMPARE(node.geometry()->vertexCount(),0);
        frame=bend();node.sync(frame,1,0);QCOMPARE(node.geometry()->vertexCount(),0);
    }
    void projectedCoordinatesAndStyleUpdatesRemainAccurate() {
        FlowParticleNode node;auto frame=bend();FlowParticleAppearance appearance;
        appearance.anchor={1e7,-2e7};appearance.color=QColor(20,40,80,128);
        auto &history=frame->particles.front().history;
        for(size_t i=0;i<history.size();++i)history.mutableAt(i).position+=appearance.anchor;
        node.sync(frame,1,1,4,4,4,0,0,appearance);
        const auto *v=static_cast<const float*>(node.geometry()->vertexData());
        QCOMPARE(v[0],20.f);QCOMPARE(v[1],60.f);
        const auto *vertex=static_cast<const FlowParticleVertex*>(node.geometry()->vertexData());
        QCOMPARE(vertex->r,uchar(20));QCOMPARE(vertex->a,uchar(128));
        const int uploads=node.uploads();appearance.opacity=.4;appearance.fade=false;appearance.taper=false;
        node.sync(frame,.5,1,4,4,4,0,0,appearance);QCOMPARE(node.uploads(),uploads);
        appearance.color=Qt::red;node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.uploads(),uploads+1);
    }
    void shortenedHistoryUsesItsVisibleWindowWithoutChangingLifetime() {
        FlowParticleNode node;auto frame=bend();frame->particles.front().historyLimited=true;
        node.sync(frame,1,1,20,4,4,0);
        FlowParticleRibbon ribbon;buildFlowParticleRibbon(*frame,1,0,{},ribbon);
        QCOMPARE(flowParticleTimingValue(ribbon.timing.front(),2),100.f);
        QCOMPARE(flowParticleTimingValue(ribbon.timing.front(),4),0.f); // independent lifetime and tail
        const int uploads=node.uploads();node.sync(frame,.25,1,20,4,4,0);QCOMPARE(node.uploads(),uploads);
    }
    void viewportCullingRetainsVisibleTrailsWithOffscreenHeads() {
        FlowParticleNode node;const auto frame=bend();FlowParticleAppearance appearance;
        appearance.viewport={10,50,20,20}; // the head is outside, the old trail is visible
        node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),6);
        const int uploads=node.uploads();appearance.viewport={120,0,20,20};
        node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),0);QCOMPARE(node.uploads(),uploads+1);
        appearance.viewport={};node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),6);
    }
    void nativeJoinedBrightnessAndInterpolatedHead() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Run with a native window to verify joined ribbons");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::black);window.resize(160,80);
        auto *preview=new Preview(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        preview->update();const auto image=window.grabWindow();QVERIFY(!image.isNull());const double dpr=image.width()/160.;
        // Shared joins must neither leave holes nor double-blend the corner.
        for(const QPoint point:{QPoint(99,59),QPoint(100,60),QPoint(98,60),QPoint(100,58)}) {
            const double value=qRed(image.pixel(int((point.x()+.5)*dpr),int((point.y()+.5)*dpr)))/255.;
            QVERIFY2(value>.60 && value<.85,qPrintable(QString("joint brightness %1 at %2,%3").arg(value).arg(point.x()).arg(point.y())));
        }
        for(double alpha:{0.,.25,.5,.75,1.}) {
            preview->alpha=alpha;preview->update();const auto moved=window.grabWindow();
            int first=moved.height();const int x=int(100.5*dpr);
            for(int y=0;y<moved.height();++y)if(qRed(moved.pixel(x,y))>5)first=std::min(first,y);
            QVERIFY(std::abs(first/dpr-(40-20*alpha))<1.1);
        }
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));
        QVERIFY(image.save(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/joined-bend.png"));
    }
    void nativeTimingRowsKeepIndependentHeadsAndRetirement() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Native independent-particle timing");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::black);window.resize(160,80);auto *preview=new Preview(window.contentItem());
        FlowParticle second;second.id=2;second.age=1.234567;second.previousAge=1.11111;second.lifetime=10;
        second.history={{{20,10},0,0},{{140,10},2,0}};preview->frame->particles.push_back(second);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        for(double alpha:{0.,.25,.5,.75,1.}) {
            preview->alpha=alpha;preview->update();const auto image=window.grabWindow();QVERIFY(!image.isNull());
            const double dpr=image.width()/160.;const int y=int(10.5*dpr);int last=-1;
            for(int x=0;x<image.width();++x)if(qRed(image.pixel(x,y))>5)last=x;
            const double expected=20+60*(second.previousAge+(second.age-second.previousAge)*alpha);
            QVERIFY2(std::abs((last+.5)/dpr-expected)<1.1,qPrintable(QString("head %1 vs %2").arg((last+.5)/dpr).arg(expected)));
        }
        preview->frame=std::make_shared<FlowParticleFrame>(*preview->frame);
        preview->frame->particles.back().stoppedAt=0;preview->update();const auto retired=window.grabWindow();
        const int y=int(10.5*retired.width()/160.);
        for(int x=0;x<retired.width();++x)QVERIFY(qRed(retired.pixel(x,y))<=5);
    }
    void nativeLightTrailsRemainVisibleOnWhite() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Run with a native window to verify light-trail contrast");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::white);window.resize(160,80);auto *preview=new Preview(window.contentItem());
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));preview->update();
        const auto image=window.grabWindow();QVERIFY(!image.isNull());int contrastPixels=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)contrastPixels+=qRed(image.pixel(x,y))<220;
        QVERIFY(contrastPixels>30);
        QDir().mkpath(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT));
        QVERIFY(image.save(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/white-background-trail.png"));
    }
    void nativeShortenedTailFadesAndKeepsInterpolatedHead() {
        if(!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE"))QSKIP("Run with a native window to verify shortened tails");
        QQuickWindow window;window.setFlags(window.flags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::black);window.resize(160,80);auto *preview=new Preview(window.contentItem());
        preview->frame->particles.front().historyLimited=true;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        preview->update();const auto image=window.grabWindow();const double dpr=image.width()/160.;
        QVERIFY(qRed(image.pixel(int(21*dpr),int(60.5*dpr)))<10); // no hard bright cutoff at the retained start
        QVERIFY(qRed(image.pixel(int(70*dpr),int(60.5*dpr)))>30);
        for(double alpha:{0.,.25,.5,.75,1.}) {
            preview->alpha=alpha;preview->update();const auto moved=window.grabWindow();
            int first=moved.height();const int x=int(100.5*dpr);
            for(int y=0;y<moved.height();++y)if(qRed(moved.pixel(x,y))>5)first=std::min(first,y);
            QVERIFY(std::abs(first/dpr-(40-20*alpha))<1.1);
        }
        QVERIFY(image.save(QStringLiteral(FLOW_PARTICLE_TEST_OUTPUT)+"/shortened-tail.png"));
    }
};
QTEST_MAIN(FlowParticleRenderingTest)
#include "test_flowparticle_rendering.moc"
