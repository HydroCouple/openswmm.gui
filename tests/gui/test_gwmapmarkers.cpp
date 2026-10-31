/**
 * @file      test_gwmapmarkers.cpp
 * @brief     R7 (Phase 4A, 2026-10-30): [GW_SOURCES] and [2D_AQUIFER_NODE] map markers
 *            and mesh fill coloured by cell TAG as categories.
 *
 * The markers are read from the model engine (never a separate copy): editing a row
 * through the engine and announcing the edit moves or recolours its marker; hiding the
 * Groundwater Markers sublayer removes them from the rendered map. Files are written to
 * tests/output/gw_phase4a_2026-10-30/markers for review.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/groundwatermapmarkers.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"
#include "render/sublayers/groundwatermarkersublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

using OpenSWMMVis::Groundwater::GroundwaterMapMarker;
using Kind = GroundwaterMapMarker::Kind;

namespace
{
const QString kRoot = QStringLiteral(SWMMVIS_PHASE4A_OUTPUT "/markers");

// Four 10 m cells, tagged PAD, PAD, LAWN and untagged; two located sources, two
// scoped sources (no single location) and two node beds, one with exchange off.
const char *kDeck = R"INP([TITLE]
Phase 4A R7 groundwater markers

[OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 01:00:00

[JUNCTIONS]
J1 8 3 0 0 0

[OUTFALLS]
O1 7 FREE NO

[CONDUITS]
C1 J1 O1 30 0.013 0 0 0

[XSECTIONS]
C1 CIRCULAR 0.5 0 0 0 1

[COORDINATES]
J1 5 5
O1 15 5

[POLLUTANTS]
Tracer MG/L 0 0 0 0 NO * 0 0 0

[2D_OPTIONS]
MAX_TIMESTEP 5
REPORT_2D NO

[2D_VERTICES]
0 0 10
10 0 10
20 0 10
20 10 10
10 10 10
0 10 10

[2D_TRIANGLES]
0 1 4 0.03 0 PAD
0 4 5 0.03 0 PAD
1 2 3 0.03 0 LAWN
1 3 4 0.03 0

[2D_AQUIFER_OPTIONS]
NODE_ENROLMENT ROWS

[2D_AQUIFER]
* 10 5 0.40 0.05 2

[2D_AQUIFER_NODE]
J1 AUTO
O1 AUTO EXCHANGE NO

[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES

[GW_SOURCES]
Well  CELL 3  FLOW 0.0005  Tracer CONC 100
Spot  XY 2 8  FLOW 0.0001
All   *       FLOW 0.0001
Pad   TAG PAD FLOW 0.0001
)INP";

/** @return The mesh of kDeck as the layer reads it (vertices, cells, tags). */
mesh::MeshResult geometry()
{
    mesh::MeshResult m;
    for (auto p : {QPointF(0, 0), QPointF(10, 0), QPointF(20, 0), QPointF(20, 10), QPointF(10, 10),
                   QPointF(0, 10)})
        m.vertices.append({p, 10});
    m.triangles = {{0, 1, 4}, {0, 4, 5}, {1, 2, 3}, {1, 3, 4}};
    m.triangles[0].tag = m.triangles[1].tag = QStringLiteral("PAD");
    m.triangles[2].tag = QStringLiteral("LAWN");
    return m;
}

struct Fixture {
    SWMMModelLayer model{QString()};
    SWMM2DMeshLayer mesh{geometry(), QStringLiteral("markers.2dm")};
    /** @brief Write and load kDeck under kRoot/<test function>, then bind the mesh to it. */
    bool open()
    {
        const QString dir = kRoot + "/" + QString::fromLatin1(QTest::currentTestFunction());
        QDir().mkpath(dir);
        QFile file(dir + "/model.inp");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(kDeck) != qint64(qstrlen(kDeck)))
            return false;
        file.close();
        model.setModelFilePath(file.fileName());
        QList<QString> warnings, errors;
        if (!model.loadModel(warnings, errors))
            return false;
        mesh.setInfiltrationModel(&model);
        return true;
    }
};

/** @return Index of the first scene marker named @p name, or -1. */
int markerIndex(const SWMM2DMeshLayer &mesh, const QString &name)
{
    const auto &markers = mesh.groundwaterSceneMarkers();
    for (int i = 0; i < markers.size(); ++i)
        if (markers[i].name == name)
            return i;
    return -1;
}

/**
 * @brief Render the mesh layer's CPU scene item over its extent.
 * @return 200 x 100 image of scene rect x 0..20, y -10..0 (10 px per metre).
 */
QImage render(SWMM2DMeshLayer &mesh, const QString &file)
{
    QGraphicsScene scene;
    mesh.populateScene(&scene, MapExtent(0, 0, 20, 10), nullptr);
    QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, 200, 100), QRectF(0, -10, 20, 10));
    painter.end();
    image.save(file);
    mesh.depopulateScene(&scene);
    return image;
}

/** @return True when @p c is clearly the default source blue. */
bool sourceBlue(QRgb c)
{
    return qBlue(c) > qRed(c) + 60 && qBlue(c) > qGreen(c) + 40;
}
} // namespace

class TestGwMapMarkers : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QDir().mkpath(kRoot)); }

    /**
     * @brief The engine query places an XY source at its coordinates, a CELL source at its
     * cell, beds at their nodes (exchange-off distinguishable) and omits scoped sources.
     */
    void engineRowsBecomeMarkers()
    {
        Fixture f;
        QVERIFY(f.open());
        const auto markers = OpenSWMMVis::Groundwater::groundwaterMapMarkers(f.model.engine());
        QCOMPARE(markers.size(), 4);
        QCOMPARE(markers[0].name, QString("Well"));
        QCOMPARE(markers[0].kind, Kind::Source);
        QVERIFY(!markers[0].hasPoint);
        QCOMPARE(markers[0].cell, 2); // CELL 3 is one-based
        QCOMPARE(markers[1].name, QString("Spot"));
        QVERIFY(markers[1].hasPoint);
        QCOMPARE(markers[1].point, QPointF(2, 8));
        QCOMPARE(markers[2].name, QString("J1"));
        QCOMPARE(markers[2].kind, Kind::NodeBed);
        QCOMPARE(markers[2].point, QPointF(5, 5));
        QCOMPARE(markers[3].name, QString("O1"));
        QCOMPARE(markers[3].kind, Kind::NodeBedNoExchange);
        QCOMPARE(markers[3].point, QPointF(15, 5));
    }

    /**
     * @brief The mesh layer's sublayer places the markers in the scene, follows engine
     * edits announced as model edits, and is drawn / hidden by its visibility toggle.
     */
    void meshLayerDrawsAndFollowsTheModel()
    {
        Fixture f;
        QVERIFY(f.open());
        auto *sub = f.mesh.groundwaterMarkerSublayer();
        QVERIFY(sub);
        QVERIFY(f.mesh.sublayers().contains(sub));
        QVERIFY(sub->isVisible());
        QCOMPARE(sub->legendSymbolItems().size(), 3);

        const auto &markers = f.mesh.groundwaterSceneMarkers();
        QCOMPARE(markers.size(), 4);
        // Cell 2 (vertices 1, 2, 3): centroid (16.67, 3.33), scene Y down.
        const QPointF well = markers[markerIndex(f.mesh, "Well")].pt;
        QVERIFY(std::abs(well.x() - 50.0 / 3.0) < 1e-9);
        QVERIFY(std::abs(well.y() + 10.0 / 3.0) < 1e-9);
        QCOMPARE(markers[markerIndex(f.mesh, "Spot")].pt, QPointF(2, -8));

        const QImage shown = render(f.mesh, kRoot + "/markers_shown.png");
        QVERIFY(sourceBlue(shown.pixel(20, 20))); // Spot at (2, 8)
        QVERIFY(!sourceBlue(shown.pixel(60, 20)));

        // Edit the engine rows and announce the edit: the markers follow.
        SWMM_Engine engine = f.model.engine();
        QCOMPARE(swmm_gw_source_xy_set(engine, 1, 6, 2), SWMM_OK);
        QCOMPARE(swmm_gw2d_node_set_exchange(engine, 0, 0), SWMM_OK);
        QSignalSpy repaint(&f.mesh, &SWMM2DMeshLayer::repaintRequested);
        f.model.markEdited();
        QVERIFY(!repaint.isEmpty());
        QCOMPARE(f.mesh.groundwaterSceneMarkers()[markerIndex(f.mesh, "Spot")].pt, QPointF(6, -2));
        QCOMPARE(f.mesh.groundwaterSceneMarkers()[markerIndex(f.mesh, "J1")].kind,
                 Kind::NodeBedNoExchange);
        const QImage moved = render(f.mesh, kRoot + "/markers_moved.png");
        QVERIFY(!sourceBlue(moved.pixel(20, 20)));
        QVERIFY(sourceBlue(moved.pixel(60, 80)));

        sub->setVisible(false);
        const QImage hidden = render(f.mesh, kRoot + "/markers_hidden.png");
        QVERIFY(!sourceBlue(hidden.pixel(60, 80)));
    }

    /**
     * @brief Colour by "Cell tag": one category per distinct tag (sorted), untagged cells
     * no data, legend rows named by tag, and a tag edit updates the categories.
     */
    void meshFillColoursByTagCategories()
    {
        SWMM2DMeshLayer mesh{geometry(), QStringLiteral("tags.2dm")};
        auto *fill = mesh.meshFillSublayer();
        auto *style = fill->fillStyle();
        style->setColorByAttribute(OpenSWMM::Render::MeshFillStyle::CellAttribute::Tag);
        QVERIFY(style->colorsByCategory());
        QCOMPARE(OpenSWMM::Render::MeshFillStyle::attributeKey(
                     OpenSWMM::Render::MeshFillStyle::CellAttribute::Tag),
                 QByteArray("tag"));
        QCOMPARE(mesh.cellTagCategories(), (QStringList{"LAWN", "PAD"}));
        const auto &values = mesh.cellAttributeValues("tag");
        QCOMPARE(values.size(), 4);
        QCOMPARE(values[0], 1.f); // PAD
        QCOMPARE(values[1], 1.f);
        QCOMPARE(values[2], 0.f); // LAWN
        QVERIFY(std::isnan(values[3]));
        QVERIFY(style->categoryColorForValue(0) != style->categoryColorForValue(1));

        // The legend rows follow the "Colour by" choice (queued refresh).
        QTRY_COMPARE(fill->legendSymbolItems().size(), 2);
        auto legend = fill->legendSymbolItems();
        QCOMPARE(legend[0].label, QString("LAWN"));
        QCOMPARE(legend[1].label, QString("PAD"));

        const quint64 revision = mesh.attrRevision();
        QVERIFY(mesh.applyMeshTriangleTag(3, QStringLiteral("ROAD")));
        QVERIFY(mesh.attrRevision() != revision);
        QCOMPARE(mesh.cellAttributeValues("tag")[3], 2.f);
        QTRY_COMPARE(fill->legendSymbolItems().size(), 3);
        legend = fill->legendSymbolItems();
        QCOMPARE(legend[2].label, QString("ROAD"));
    }
};

QTEST_MAIN(TestGwMapMarkers)
#include "test_gwmapmarkers.moc"
