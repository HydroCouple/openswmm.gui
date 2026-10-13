// GPL-3.0-or-later
#include "map/flowparticlenode.h"
#include <QTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
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
    std::shared_ptr<FlowParticleFrame> frame=bend();double alpha=1;
    Preview(QQuickItem *parent):QQuickItem(parent) {setFlag(ItemHasContents);setSize({160,80});}
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *node=old?static_cast<FlowParticleNode*>(old):new FlowParticleNode;
        node->sync(frame,alpha,1,4,4,4,0);return node;
    }
};
class FlowParticleRenderingTest:public QObject {
    Q_OBJECT
private slots:
    void adjacentSegmentsShareExactlyTheSameJoint() {
        FlowParticleNode node;const auto frame=bend();node.sync(frame,1,1,4,4,4,0);
        QCOMPARE(node.geometry()->vertexCount(),12);
        const auto *bytes=static_cast<const char*>(node.geometry()->vertexData());const int stride=node.geometry()->sizeOfVertex();
        QCOMPARE(std::memcmp(bytes+5*stride,bytes+6*stride,size_t(stride)),0);
        QCOMPARE(std::memcmp(bytes+2*stride,bytes+7*stride,size_t(stride)),0);
        const int uploads=node.uploads();const auto *data=node.geometry()->vertexData();
        for(double alpha:{0.,.25,.5,.75,1.})node.sync(frame,alpha,1,4,4,4,0);
        QCOMPARE(node.uploads(),uploads);QCOMPARE(node.geometry()->vertexData(),data);
        node.sync(frame,1,2,4,4,4,0);QCOMPARE(node.uploads(),uploads+1);
    }
    void emptyHistoryAndUnavailableScaleHaveNoDrawGeometry() {
        FlowParticleNode node;auto frame=bend();frame->particles.front().history.clear();node.sync(frame,1,1);
        QCOMPARE(node.geometry()->vertexCount(),0);
        frame=bend();node.sync(frame,1,0);QCOMPARE(node.geometry()->vertexCount(),0);
    }
    void projectedCoordinatesAndStyleUpdatesRemainAccurate() {
        FlowParticleNode node;auto frame=bend();FlowParticleAppearance appearance;
        appearance.anchor={1e7,-2e7};appearance.color=QColor(20,40,80,128);
        for(auto &s:frame->particles.front().history)s.position+=appearance.anchor;
        node.sync(frame,1,1,4,4,4,0,0,appearance);
        const auto *v=static_cast<const float*>(node.geometry()->vertexData());
        QCOMPARE(v[0],20.f);QCOMPARE(v[1],60.f);
        QVERIFY(std::abs(v[11]-20/255.f)<1e-6);QVERIFY(std::abs(v[14]-128/255.f)<1e-6);
        const int uploads=node.uploads();appearance.opacity=.4;appearance.fade=false;appearance.taper=false;
        node.sync(frame,.5,1,4,4,4,0,0,appearance);QCOMPARE(node.uploads(),uploads);
        appearance.color=Qt::red;node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.uploads(),uploads+1);
    }
    void shortenedHistoryUsesItsVisibleWindowWithoutChangingLifetime() {
        FlowParticleNode node;auto frame=bend();frame->particles.front().historyLimited=true;
        node.sync(frame,1,1,20,4,4,0);
        const auto *v=static_cast<const float*>(node.geometry()->vertexData());
        QCOMPARE(v[7],100.f);QCOMPARE(v[10],0.f); // lifetime and retained tail start are independent
        const int uploads=node.uploads();node.sync(frame,.25,1,20,4,4,0);QCOMPARE(node.uploads(),uploads);
    }
    void viewportCullingRetainsVisibleTrailsWithOffscreenHeads() {
        FlowParticleNode node;const auto frame=bend();FlowParticleAppearance appearance;
        appearance.viewport={10,50,20,20}; // the head is outside, the old trail is visible
        node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),12);
        const int uploads=node.uploads();appearance.viewport={120,0,20,20};
        node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),0);QCOMPARE(node.uploads(),uploads+1);
        appearance.viewport={};node.sync(frame,1,1,4,4,4,0,0,appearance);QCOMPARE(node.geometry()->vertexCount(),12);
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
