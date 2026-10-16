// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/linkstroke.h"
#include "render/qsgpremultiply.h"

#include <QImage>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>
#include <QTest>

#include <limits>
#include <vector>

using namespace OpenSWMM::Render;
using Points = std::vector<QPointF>;

static Points stroke(const std::vector<double> &xy, double halfWidth = 12,
                     QPointF origin = {})
{
    Points result;
    appendLinkStrokeTriangles(xy.data(), xy.size()/2, origin.x(), origin.y(),
                              halfWidth, [&](QPointF p) { result.push_back(p); });
    return result;
}

// Strict interior coverage avoids counting shared triangle edges twice.
static int coverage(const Points &v, QPointF p)
{
    auto cross = [](QPointF a, QPointF b) { return a.x()*b.y()-a.y()*b.x(); };
    int hits = 0;
    for (size_t i = 0; i+2 < v.size(); i += 3) {
        const double a = cross(v[i+1]-v[i], p-v[i]);
        const double b = cross(v[i+2]-v[i+1], p-v[i+1]);
        const double c = cross(v[i]-v[i+2], p-v[i+2]);
        if ((a > 1e-8 && b > 1e-8 && c > 1e-8)
            || (a < -1e-8 && b < -1e-8 && c < -1e-8)) ++hits;
    }
    return hits;
}

class StrokePreview : public QQuickItem
{
public:
    explicit StrokePreview(bool colored, QQuickItem *parent)
        : QQuickItem(parent), m_colored(colored)
    { setFlag(ItemHasContents); setSize({224, 184}); }
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override
    {
        delete old;
        const auto vertices = stroke({40,60,160,60,160,140});
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(m_colored
            ? QSGGeometry::defaultAttributes_ColoredPoint2D()
            : QSGGeometry::defaultAttributes_Point2D(), int(vertices.size()));
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        if (m_colored) {
            auto *out = geometry->vertexDataAsColoredPoint2D();
            for (size_t i = 0; i < vertices.size(); ++i)
                out[i].set(float(vertices[i].x()), float(vertices[i].y()),
                           premul(40,128), premul(120,128), premul(200,128), 128);
            node->setMaterial(new QSGVertexColorMaterial);
        } else {
            auto *out = geometry->vertexDataAsPoint2D();
            for (size_t i = 0; i < vertices.size(); ++i)
                out[i].set(float(vertices[i].x()), float(vertices[i].y()));
            auto *material = new QSGFlatColorMaterial;
            material->setColor(QColor(40,120,200,128));
            node->setMaterial(material);
        }
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setFlag(QSGNode::OwnsMaterial);
        return node;
    }
private:
    bool m_colored;
};

class LinkStrokeTest : public QObject
{
    Q_OBJECT
private slots:
    void roundCapsCoverEndpointsWithoutSquareCorners()
    {
        const auto v = stroke({40,60,160,60});
        QVERIFY(coverage(v, {29.2,60.3}) > 0);
        QVERIFY(coverage(v, {170.8,60.3}) > 0);
        QCOMPARE(coverage(v, {29.2,49.2}), 0);
        QCOMPARE(coverage(v, {170.8,49.2}), 0);
    }

    void mitreFillsTheOuterCorner()
    {
        const auto v = stroke({40,60,160,60,160,140});
        // Outside a radius-12 round join, but inside the square mitre.
        QCOMPARE(coverage(v, {169.2,50.7}), 1);
        QCOMPARE(coverage(v, {160.3,60.7}), 1);
    }

    void multipleBendsMatchThePainterStroke_data()
    {
        QTest::addColumn<QVector<QPointF>>("points");
        QTest::newRow("right") << QVector<QPointF>{{40,60},{160,60},{160,140}};
        QTest::newRow("left") << QVector<QPointF>{{40,140},{160,140},{160,60}};
        QTest::newRow("dogleg") << QVector<QPointF>{{30,50},{90,50},{90,110},{180,110}};
        QTest::newRow("diagonal") << QVector<QPointF>{{30,130},{90,60},{150,100},{190,40}};
        QTest::newRow("sharp-bevel") << QVector<QPointF>{{25,100},{180,100},{45,130}};
    }

    void multipleBendsMatchThePainterStroke()
    {
        QFETCH(QVector<QPointF>, points);
        std::vector<double> xy;
        QPainterPath path;
        path.moveTo(points.front());
        for (const auto &p : points) { xy.push_back(p.x()); xy.push_back(p.y()); }
        for (int i = 1; i < points.size(); ++i) path.lineTo(points[i]);
        QPen pen(Qt::black, 24);
        configureLinkStrokePen(pen);
        QPainterPathStroker stroker(pen);
        const QPainterPath reference = stroker.createStroke(path);
        const auto vertices = stroke(xy);
        int differences = 0;
        for (double y = 20.31; y < 170; y += 1.0)
            for (double x = 10.37; x < 215; x += 1.0)
                differences += (coverage(vertices, {x,y}) > 0) != reference.contains({x,y});
        // Arc chords can differ at a few boundary pixels; a missing cap,
        // wrong join, or a bevel-threshold mismatch loses many more.
        QVERIFY2(differences < 45, qPrintable(QString::number(differences)));
    }

    void ordinaryJoinsDoNotDoublePaintTransparentLinks()
    {
        const auto vertices = stroke({40,60,160,60,160,140});
        for (double y = 48.31; y < 74; y += 0.7)
            for (double x = 146.37; x < 173; x += 0.7)
                QVERIFY2(coverage(vertices, {x,y}) <= 1, "An interior join overlaps its own body");
    }

    void repeatedVerticesAreIgnored()
    {
        QCOMPARE(stroke({40,60,40,60,160,60,160,60,160,140,160,140}),
                 stroke({40,60,160,60,160,140}));
        QVERIFY(stroke({40,60,40,60}).empty());
        QVERIFY(stroke({40,60}).empty());
        QVERIFY(stroke({}).empty());
    }

    void sharpReversalsStayFiniteAndBounded()
    {
        for (const auto &xy : {std::vector<double>{40,60,160,60,40,60},
                               std::vector<double>{40,60,160,60,40,60.000001},
                               std::vector<double>{140,60,160,60,140,61}}) {
            const auto vertices = stroke(xy);
            QVERIFY(!vertices.empty());
            for (const auto &p : vertices) {
                QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()));
                QVERIFY(p.x() >= 28 && p.x() <= 208 && p.y() >= 12 && p.y() <= 109);
            }
        }
        QVERIFY(stroke({40,60,160,60}, 0).empty());
        QVERIFY(stroke({40,60,160,60}, -1).empty());
        QVERIFY(stroke({40,60,160,std::numeric_limits<double>::quiet_NaN()}).empty());
    }

    void projectedCoordinatesAreAnchoredBeforeFloatConversion()
    {
        const auto local = stroke({40,60,160,60,160,140});
        const auto map = stroke({2000040,6133060,2000160,6133060,2000160,6133140},
                                12, {2000000,6133000});
        QCOMPARE(map, local);
    }

    void nativeRoundCapsAndMitres_data()
    {
        QTest::addColumn<bool>("colored");
        QTest::newRow("base-link") << true;
        QTest::newRow("selection-halo") << false;
    }

    void nativeRoundCapsAndMitres()
    {
        if (!qEnvironmentVariableIsSet("LINK_STROKE_NATIVE"))
            QSKIP("Set LINK_STROKE_NATIVE=1 on a desktop to check native triangle rendering.");
        QFETCH(bool, colored);
        QQuickWindow window;
        window.setColor(Qt::white);
        window.resize(224,184);
        new StrokePreview(colored, window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QImage image = window.grabWindow();
        QVERIFY(!image.isNull());
        const QString backend = qEnvironmentVariable("QSG_RHI_BACKEND");
        if (backend == QStringLiteral("opengl"))
            QCOMPARE(window.rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);
        if (backend == QStringLiteral("metal"))
            QCOMPARE(window.rendererInterface()->graphicsApi(), QSGRendererInterface::Metal);
        const double scale = image.width()/224.0;
        auto pixel = [&](double x, double y) { return image.pixelColor(int(x*scale),int(y*scale)); };
        const QColor body = pixel(100,60);
        QVERIFY(body.red() < 220 && body.blue() > body.red());
        for (const auto p : {QPointF(29,60), QPointF(160,151), QPointF(169,51), QPointF(160,60)}) {
            const auto c = pixel(p.x(),p.y());
            QVERIFY2(std::abs(c.red()-body.red()) <= 3 && std::abs(c.green()-body.green()) <= 3,
                     "A cap/join has a gap or overlapping translucent triangles");
        }
        QCOMPARE(pixel(29,49), QColor(Qt::white));
    }
};

QTEST_MAIN(LinkStrokeTest)
#include "test_linkstroke.moc"
