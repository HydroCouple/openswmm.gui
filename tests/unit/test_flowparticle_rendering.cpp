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
};
QTEST_MAIN(FlowParticleRenderingTest)
#include "test_flowparticle_rendering.moc"
