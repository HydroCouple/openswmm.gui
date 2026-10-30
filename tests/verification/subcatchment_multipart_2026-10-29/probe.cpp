// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"
#include "map/swmmlayeritem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "render/subcatchmentgeometry.h"
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSGGeometryNode>
#include <QStyleOptionGraphicsItem>
#include <QDebug>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>
#include <memory>
#include <cmath>

using namespace OpenSWMM::Render::SubcatchmentGeometry;
class Renderer : public SWMMLayerQSGRenderer {
public:
    using SWMMLayerQSGRenderer::updatePaintNode;
};
#define CHECK(condition) do { if (!(condition)) { qCritical() << "FAIL at" << __LINE__ << #condition; return 1; } } while (false)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString root = QDir::currentPath();
    const QString out = root + "/tests/output/subcatchment_multipart/";
    CHECK(QDir().mkpath(out));
    SWMMModelLayer layer(root + "/tests/gui/data/multipart_subcatchments.inp", nullptr);
    QList<QString> warnings, errors;
    CHECK(layer.loadModel(warnings, errors));
    CHECK(layer.cachedSubcatchCount() == 1);
    auto points = layer.cachedSubcatchVertices(0);
    CHECK(rings(points).size() == 4);
    for (const auto &point : {QPointF(20,20), QPointF(150,50), QPointF(60,150)})
        CHECK(layer.identifyAt(point.x(), point.y(), nullptr, .1).value("elementName") == "S1");
    for (const auto &point : {QPointF(50,50), QPointF(110,50), QPointF(20,120)})
        CHECK(layer.identifyAt(point.x(), point.y(), nullptr, .1).isEmpty());
    layer.setShowNodes(false);
    layer.setShowLinks(false);
    layer.setShowRainGages(false);
    layer.setShowLabels(false);
    auto symbol = layer.subcatchmentSymbol();
    symbol.fillColor = Qt::blue;
    layer.setSubcatchmentSymbol(symbol);
    layer.setQsgRenderKinds({});
    QImage image(220,190,QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        SWMMLayerItem item(&layer);
        QPainter painter(&image);
        painter.translate(0,180);
        QStyleOptionGraphicsItem option;
        option.exposedRect = QRectF(0,-180,220,190);
        item.paint(&painter,&option,nullptr);
    }
    CHECK(image.pixelColor(20,160) == QColor(Qt::blue));
    CHECK(image.pixelColor(150,130) == QColor(Qt::blue));
    CHECK(image.pixelColor(60,30) == QColor(Qt::blue));
    CHECK(image.pixelColor(50,130) == QColor(Qt::white));
    CHECK(image.pixelColor(110,130) == QColor(Qt::white));
    CHECK(image.save(out + "model-cpu.png"));

    layer.setQsgRenderKinds(SWMMModelLayer::QsgCatch);
    Renderer renderer;
    renderer.setLayer(&layer);
    renderer.setWidth(220);
    renderer.setHeight(190);
    renderer.setMapExtent(MapExtent(0,0,220,190));
    std::unique_ptr<QSGNode> node(renderer.updatePaintNode(nullptr,nullptr));
    CHECK(node);
    auto *fill = static_cast<QSGGeometryNode *>(node->firstChild());
    const auto *verts = fill->geometry()->vertexDataAsColoredPoint2D();
    double area = 0;
    for (int i=0; i<fill->geometry()->vertexCount(); i+=3) {
        const QPointF a(verts[i].x,verts[i].y), b(verts[i+1].x,verts[i+1].y), c(verts[i+2].x,verts[i+2].y);
        area += std::abs((b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x()))*.5;
        const auto center = (a+b+c)/3.;
        CHECK(contains(points,{center.x()-100,-center.y()-100})); // node anchor: (-100,+100)
    }
    CHECK(area == 10000.);
    layer.setSelectedElementNames({"S1"});
    auto *tree = node.release();
    node.reset(renderer.updatePaintNode(tree,nullptr));
    auto *selected = static_cast<QSGGeometryNode *>(node->firstChild()->nextSibling());
    CHECK(selected->geometry()->vertexCount() == fill->geometry()->vertexCount());
    if (QGuiApplication::platformName() != "offscreen") {
        layer.setSelectedElementNames({});
        QQuickWindow window;
        window.setColor(Qt::white);
        window.resize(220,190);
        auto *native = new Renderer;
        native->setParentItem(window.contentItem());
        native->setLayer(&layer);
        native->setWidth(220);
        native->setHeight(190);
        native->setMapExtent(MapExtent(0,0,220,190));
        window.show();
        QEventLoop settle;
        QTimer::singleShot(1000,&settle,&QEventLoop::quit);
        settle.exec();
        const auto frame = window.grabWindow();
        CHECK(!frame.isNull());
        CHECK(frame.save(out+"model-gpu-native.png"));
        const auto pixel = [&](int x,int y) {
            return frame.pixelColor(x*frame.width()/220,y*frame.height()/190);
        };
        CHECK(pixel(20,170) == QColor(Qt::blue));
        CHECK(pixel(150,140) == QColor(Qt::blue));
        CHECK(pixel(60,40) == QColor(Qt::blue));
        CHECK(pixel(50,140) == QColor(Qt::white));
        CHECK(pixel(110,140) == QColor(Qt::white));
        window.hide();
    }
    // Whole-polygon movement and cache refresh preserve all ring closures.
    for (auto &point : points)
        point += QPointF(5,5);
    CHECK(layer.applySubcatchVertices(0,points));
    CHECK(rings(layer.cachedSubcatchVertices(0)).size() == 4);
    CHECK(layer.identifyAt(155,55,nullptr,.1).value("elementName") == "S1");
    CHECK(swmm_model_write(layer.engine(),(out+"roundtrip.inp").toUtf8().constData()) == 0);
    SWMMModelLayer inp(out+"roundtrip.inp",nullptr);
    CHECK(inp.loadModel(warnings,errors));
    CHECK(inp.cachedSubcatchVertices(0) == points);
    CHECK(swmm_model_write_with_plugin(layer.engine(),(out+"roundtrip.gpkg").toUtf8().constData(),
          "org.hydrocouple.openswmm.plugins.geopackage") == 0);
    SWMMModelLayer gpkg(out+"roundtrip.gpkg",nullptr);
    const bool loadedGpkg = gpkg.loadModel(warnings,errors);
    if (!loadedGpkg) qCritical() << errors;
    CHECK(loadedGpkg);
    CHECK(gpkg.cachedSubcatchVertices(0) == points);
    CHECK(gpkg.identifyAt(155,55,nullptr,.1).value("elementName") == "S1");
    CHECK(gpkg.identifyAt(55,55,nullptr,.1).isEmpty());
    qInfo() << "PASS: model loading, all three islands, hole/gap picking, CPU pixels, GPU fill/selection buffers, geometry refresh, INP and GeoPackage save/reopen";
}
