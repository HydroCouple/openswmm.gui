/**
 * @file      test_bulkdelete.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Coverage for SWMMModelLayer::BulkEdit and BulkEditCommand.
 *
 *         Deleting a selection used to run the full "the model changed"
 *         cascade once per object: rebuildCategoryIndex() (O(N) hash over
 *         ~1.1M entries on an all-pipes model), a link-spatial-grid rebuild
 *         per cascade link, recomputeExtentFromCaches(), and a
 *         repaintRequested()/geometryChanged() pair whose listeners each do
 *         O(model) work of their own. Measured on ww_2024.inp (103,821 nodes
 *         / 281,049 links), deleting 100 junctions cost 20.7 s, of which only
 *         4.6 s was the engine; undo cost another 16.1 s because the add path
 *         replays the same storm.
 *
 *         BulkEdit defers all of that to one rebuild at batch end.
 *
 *         This file exists because NO test previously constructed a
 *         DeleteObjectCommand at all — the four delete tests in
 *         test_selectionops.cpp all take AttributeTablePanel's headless
 *         fallback branch, so neither the command nor the macro was covered.
 *
 *         The load-bearing test here is bulkDeleteLeavesCachesCoherent().
 *         Skipping compactNodeSceneEntry() also skips the fromNodeIdx /
 *         toNodeIdx renumber it performs, and that field is authoritative
 *         data rather than a derived cache — buildGeometryCache() consumes it
 *         and does not restore it. endBulkEdit() therefore re-reads both
 *         endpoints from the engine. Without that resync every link after a
 *         deleted node draws to the WRONG endpoints, with no error anywhere.
 *
 *         Fixture: selection_trace_fixture.inp (see its [TITLE] for topology).
 *           Nodes  J1 J2 J3 J4 (junctions), O1 (outfall)
 *           Links  C1 J1->J2, C2 J2->J3, C3 J3->O1, P1 J4->J3 (pump)
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUndoCommand>

#include <algorithm>
#include <memory>
#include <utility>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

namespace
{

/**
 *
 * @brief Select SWMMVIS_GUI_TEST_DATA or current-directory dot for bulk-delete fixtures.
 * @return Owned unvalidated directory; no directory creation or file writes.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Load selection_trace_fixture.inp into a caller-owned model layer.
 * @return Unique loaded layer/engine, or nullptr after failed-load destruction.
 * Requires GUI-thread use and readable fixture; diagnostics are local, with no
 * canvas ownership transfer or source INP write.
 *
 */
// One link of every kind; GUT1 (a street conduit, index 1) is a divider's diverted link and
// has an inlet usage, losses, bend vertices, a tag and initial quality (review E-04).
const char *kLinksInp = R"([TITLE]
Delete/undo round trip for links

[OPTIONS]
FLOW_UNITS           CFS
INFILTRATION         HORTON
FLOW_ROUTING         DYNWAVE
START_DATE           01/01/2024
START_TIME           00:00:00
END_DATE             01/01/2024
END_TIME             06:00:00
ROUTING_STEP         0:00:30

[JUNCTIONS]
J3      11  5  0  0  0
J4      10  5  0  0  0
J5      10  5  0  0  0

[OUTFALLS]
O1      8  FREE  *  NO

[DIVIDERS]
D1      12  GUT1  CUTOFF  0.5  5  0  0  0

[CONDUITS]
C1      D1  J3  300  0.013  0  0
GUT1    D1  J4  300  0.016  0  0
C3      J4  O1  300  0.013  0  0

[PUMPS]
P1      J3  J4  PC1  ON  0  0

[ORIFICES]
OR1     J3  J5  SIDE  0  0.65  NO  0

[WEIRS]
W1      J3  J4  TRANSVERSE  1.0  3.33  NO  0  0  YES

[OUTLETS]
OL1     J5  O1  0  FUNCTIONAL/DEPTH  10  0.5  NO

[XSECTIONS]
C1      CIRCULAR   1    0  0  0  1
GUT1    STREET     ST_A
C3      CIRCULAR   2    0  0  0  1
OR1     CIRCULAR   1    0  0  0
W1      RECT_OPEN  1.0  2.0  0  0

[LOSSES]
GUT1    0.5  0.5  0.1  NO  0.01

[STREETS]
ST_A    20.0  0.5  4  0.016  0  0  2  20.0  0.04  0.016

[INLETS]
IN_A    GRATE  2.0  2.0  P_BAR-50

[INLET_USAGE]
GUT1    IN_A  J5  1  10  0  0  0  ON_GRADE

[POLLUTANTS]
TSS     MG/L  0  0  0  0  NO  *  0  0  0

[INITIAL_QUALITY]
LINK    GUT1  TSS  5
LINK    C3    TSS  7

[CURVES]
PC1     Pump2  0  0
PC1            5  10

[TAGS]
Link    GUT1   Gutter

[COORDINATES]
D1      0    0
J3      300  0
J4      300  100
J5      600  0
O1      600  100

[VERTICES]
GUT1    100  50
GUT1    200  80
)";

// N2 (a storage node) is referred to at both ends of its links (one a divider's diverted link),
// by a subcatchment outlet, an inlet usage capture node, inflow, DWF and treatment rows, a tag and
// initial quality; D1 and O1 after it are renumbered by its delete (review E-04).
const char *kNodesInp = R"([TITLE]
Delete/undo round trip for nodes

[OPTIONS]
FLOW_UNITS           CFS
INFILTRATION         HORTON
FLOW_ROUTING         DYNWAVE
START_DATE           01/01/2024
START_TIME           00:00:00
END_DATE             01/01/2024
END_TIME             06:00:00
ROUTING_STEP         0:00:30

[RAINGAGES]
G1      INTENSITY  0:15  1.0  TIMESERIES TS1

[SUBCATCHMENTS]
S1      G1  N2  1  50  100  0.5  0

[SUBAREAS]
S1      0.01  0.1  0.05  0.05  25  OUTLET

[INFILTRATION]
S1      3.0  0.5  4  7  0

[JUNCTIONS]
J1      12  5  0  0  0
J3      11  5  0  0  0
J4      10  5  0  0  0

[STORAGE]
N2      9  6  1  FUNCTIONAL  0  0  100  0  0

[DIVIDERS]
D1      8  DV  CUTOFF  0.5  5  0  0  0

[OUTFALLS]
O1      6  FREE  *  NO

[CONDUITS]
C1      J1  N2  300  0.013  0  0
C2      N2  D1  300  0.013  0  0
DV      D1  N2  300  0.013  0  0
C4      D1  O1  300  0.013  0  0
GUT     J3  J4  300  0.016  0  0

[PUMPS]
P1      N2  J3  PC1  ON  0  0

[XSECTIONS]
C1      CIRCULAR  1  0  0  0  1
C2      CIRCULAR  1  0  0  0  1
DV      CIRCULAR  1  0  0  0  1
C4      CIRCULAR  1  0  0  0  1
GUT     STREET    ST_A

[STREETS]
ST_A    20.0  0.5  4  0.016  0  0  2  20.0  0.04  0.016

[INLETS]
IN_A    GRATE  2.0  2.0  P_BAR-50

[INLET_USAGE]
GUT     IN_A  N2  1  10  0  0  0  ON_GRADE

[POLLUTANTS]
TSS     MG/L  0  0  0  0  NO  *  0  0  0

[INFLOWS]
J4      FLOW  TS1  FLOW    1.0  1.0  1
N2      FLOW  TS1  FLOW    1.0  1.0  2
D1      FLOW  TS1  FLOW    1.0  1.0  3

[DWF]
N2      FLOW  1.5
D1      FLOW  0.5

[TREATMENT]
N2      TSS  R = 0.5
D1      TSS  R = 0.25

[INITIAL_QUALITY]
NODE    N2  TSS  3

[CURVES]
PC1     Pump2  0  0
PC1            5  10

[TIMESERIES]
TS1     0:00  0.5
TS1     1:00  0.0

[TAGS]
Node    N2  Tank

[COORDINATES]
J1      0    0
J3      100  0
J4      200  0
N2      100  100
D1      200  100
O1      300  100

[POLYGONS]
S1      0    200
S1      100  200
S1      100  300

[SYMBOLS]
G1      50   250
)";


// S2 is referred to by S1 (drains to it), an outfall's Route To, a LID usage drain-to and a
// snowpack removal; it owns LID usage rows, coverages, loadings, a groundwater flow expression,
// a polygon and a tag. S3 after it carries the same kinds of rows and S4 drains to it (E-04).
const char *kSubcatchInp = R"([TITLE]
Delete/undo round trip for subcatchments

[OPTIONS]
FLOW_UNITS           CFS
INFILTRATION         HORTON
FLOW_ROUTING         KINWAVE
START_DATE           01/01/2024
START_TIME           00:00:00
END_DATE             01/01/2024
END_TIME             06:00:00

[RAINGAGES]
G1      INTENSITY  0:15  1.0  TIMESERIES TS1

[SNOWPACKS]
SP1     PLOWABLE    0.001 0.001 32 0.1 0 0 0
SP1     IMPERVIOUS  0.001 0.001 32 0.1 0 0 0
SP1     PERVIOUS    0.001 0.001 32 0.1 0 0 0
SP1     REMOVAL     1.0  0  0  0  0  0  S2

[SUBCATCHMENTS]
S1      G1  S2  1  50  100  0.5  0
S2      G1  J1  2  40  200  0.5  10  SP1
S3      G1  J1  3  30  300  0.5  0
S4      G1  S3  4  20  400  0.5  0

[SUBAREAS]
S1      0.01  0.1  0.05  0.05  25  OUTLET
S2      0.01  0.1  0.05  0.05  25  OUTLET
S3      0.01  0.1  0.05  0.05  25  OUTLET
S4      0.01  0.1  0.05  0.05  25  OUTLET

[INFILTRATION]
S1      3.0  0.5  4  7  0
S2      3.1  0.5  4  7  0
S3      3.2  0.5  4  7  0
S4      3.3  0.5  4  7  0

[AQUIFERS]
A1      0.5  0.15  0.30  0.1  12  15  0.35  14  0  0  4  0.3

[GROUNDWATER]
S2      A1  J1  6  1  1  0  0  0  0  *
S3      A1  J1  6  1  1  0  0  0  0  *

[GWF]
S2      LATERAL  0.001*HGW

[LID_CONTROLS]
RB      RB
RB      STORAGE  36  0.75  0  0  YES
RB      DRAIN    0   0.5   0  6

[LID_USAGE]
S1      RB  1  100  0  0  100  0  *  S2
S2      RB  1  100  0  0  100  0
S3      RB  1  100  0  0  100  0

[JUNCTIONS]
J1      0  5  0  0  0

[OUTFALLS]
O1      -1  FREE  NO  S2
O2      -2  FREE  NO  S3

[CONDUITS]
C1      J1  O1  100  0.01  0  0  0  0
C2      J1  O2  100  0.01  0  0  0  0

[XSECTIONS]
C1      CIRCULAR  1  0  0  0  1
C2      CIRCULAR  1  0  0  0  1

[POLLUTANTS]
TSS     MG/L  0  0  0  0  NO  *  0  0  0

[LANDUSES]
LU1     0  0  0

[COVERAGES]
S2      LU1  60
S3      LU1  100

[LOADINGS]
S2      TSS  1.5
S3      TSS  2.5

[TIMESERIES]
TS1     0:00  0.5
TS1     1:00  0.0

[TAGS]
Subcatch  S2  Big

[COORDINATES]
J1      0    0
O1      100  0
O2      100  50

[POLYGONS]
S1      0    200
S1      50   200
S1      50   250
S2      0    100
S2      50   100
S2      50   150
S3      100  100
S3      150  100
S3      150  150
S4      100  200
S4      150  200
S4      150  250

[SYMBOLS]
G1      10   10
)";

/** @brief Save the model to dir/name and return the file's bytes (empty on failure). */
QByteArray saveInp(SWMM_Engine e, const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    if (swmm_model_write(e, path.toUtf8().constData()) != 0)
        return {};
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/** @brief Link names in engine order. */
QStringList engineLinks(SWMM_Engine e)
{
    QStringList names;
    for (int i = 0; i < swmm_link_count(e); ++i)
        names << QString::fromUtf8(swmm_link_id(e, i));
    return names;
}

/** @brief True when every link category row names the engine link at its SoA index. */
bool linkCacheMatchesEngine(const SWMMModelLayer &layer)
{
    SWMM_Engine e = layer.engine();
    int rows = 0;
    for (auto c : {SWMMModelLayer::CatConduits, SWMMModelLayer::CatPumps, SWMMModelLayer::CatOrifices,
                   SWMMModelLayer::CatWeirs, SWMMModelLayer::CatOutlets}) {
        for (int row = 0; row < layer.categoryCount(c); ++row, ++rows) {
            const int soa = layer.soaIndexAt(c, row);
            if (soa < 0 || layer.objectNameAt(c, row) != QString::fromUtf8(swmm_link_id(e, soa)))
                return false;
        }
    }
    return rows == swmm_link_count(e) && layer.cachedLinkCount() == rows &&
           layer.renderLinkCount() == rows;
}

/** @brief Node names in engine order. */
QStringList engineNodes(SWMM_Engine e)
{
    QStringList names;
    for (int i = 0; i < swmm_node_count(e); ++i)
        names << QString::fromUtf8(swmm_node_id(e, i));
    return names;
}

/** @brief True when every node category row names the engine node at its SoA index. */
bool nodeCacheMatchesEngine(const SWMMModelLayer &layer)
{
    SWMM_Engine e = layer.engine();
    int rows = 0;
    for (auto c : {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatOutfalls,
                   SWMMModelLayer::CatStorage, SWMMModelLayer::CatDividers}) {
        for (int row = 0; row < layer.categoryCount(c); ++row, ++rows) {
            const int soa = layer.soaIndexAt(c, row);
            if (soa < 0 || layer.objectNameAt(c, row) != QString::fromUtf8(swmm_node_id(e, soa)))
                return false;
        }
    }
    return rows == swmm_node_count(e) && layer.cachedNodeCount() == rows;
}

/** @brief Subcatchment names in engine order. */
QStringList engineSubcatches(SWMM_Engine e)
{
    QStringList names;
    for (int i = 0; i < swmm_subcatch_count(e); ++i)
        names << QString::fromUtf8(swmm_subcatch_id(e, i));
    return names;
}

/** @brief True when every subcatchment row names the engine subcatchment at its SoA index. */
bool subcatchCacheMatchesEngine(const SWMMModelLayer &layer)
{
    SWMM_Engine e = layer.engine();
    const auto c = SWMMModelLayer::CatSubcatchments;
    for (int row = 0; row < layer.categoryCount(c); ++row) {
        const int soa = layer.soaIndexAt(c, row);
        if (soa < 0 || layer.objectNameAt(c, row) != QString::fromUtf8(swmm_subcatch_id(e, soa)))
            return false;
    }
    return layer.categoryCount(c) == swmm_subcatch_count(e) &&
           layer.cachedSubcatchCount() == swmm_subcatch_count(e);
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("selection_trace_fixture.inp")), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

//! Model-space coordinate of a node, straight from the engine — the
//! independent oracle the cache assertions are checked against.
/**
 *
 * @brief Read an engine node coordinate as an independent cache-comparison oracle.
 * @param eng Borrowed valid loaded engine.
 * @param name Borrowed exact node ID converted to UTF-8 for lookup.
 * @return Project-coordinate point, or origin if lookup fails. Coordinate-getter
 * status is unchecked after lookup, so failure may also return initialized zeros.
 * Origin is not an unambiguous success marker; no CRS conversion or mutation.
 *
 */
QPointF engineNodeXY(SWMM_Engine eng, const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    const int idx = swmm_node_index(eng, utf8.constData());
    if (idx < 0)
        return {};
    double x = 0, y = 0;
    swmm_spatial_get_node_coord(eng, idx, &x, &y);
    return {x, y};
}

} // namespace

class TestBulkDelete : public QObject
{
    Q_OBJECT

private slots:

    // ── The guard collapses the storm ──────────────────────────────────────

    //! Pins the whole plan: N deletions, one repaint + one geometryChanged.
    void macroDeleteEmitsOneGeometryChanged()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 3"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("J4")})
            new DeleteObjectCommand(layer.get(), n, DeleteObjectCommand::DeleteNode, nullptr,
                                    macro);
        stack.push(macro);

        // Three nodes deleted (plus their cascade links) — one notification.
        QCOMPARE(geom.count(), 1);
        QCOMPARE(paint.count(), 1);

        // ...and the deletions really happened, so the count above is not 1
        // because nothing ran.
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), 1); // J3 only
        QCOMPARE(swmm_node_index(layer->engine(), "J1"), -1);
        QCOMPARE(swmm_node_index(layer->engine(), "J2"), -1);
        QCOMPARE(swmm_node_index(layer->engine(), "J4"), -1);
    }

    //! Pins undo/redo symmetry — the add path needs the guard as much as the
    //! delete path, or Ctrl+Z is slower than the delete it reverses.
    void undoRestoresAllObjectsAndEmitsOnce()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int nodes0 = layer->categoryCount(SWMMModelLayer::CatJunctions);
        const int cond0 = layer->categoryCount(SWMMModelLayer::CatConduits);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 2"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2")})
            new DeleteObjectCommand(layer.get(), n, DeleteObjectCommand::DeleteNode, nullptr,
                                    macro);
        stack.push(macro);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0 - 2);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);
        stack.undo();

        QCOMPARE(geom.count(), 1);
        QCOMPARE(paint.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatConduits), cond0);

        // Properties came back, not just the names.
        SWMM_Engine eng = layer->engine();
        const int j1 = swmm_node_index(eng, "J1");
        QVERIFY(j1 >= 0);
        double invert = 0;
        QCOMPARE(swmm_node_get_invert_elev(eng, j1, &invert), 0);
        QCOMPARE(invert, 100.0);
        QCOMPARE(engineNodeXY(eng, QStringLiteral("J1")), QPointF(0.0, 0.0));
    }

    /**
     *
     * @brief Require a bulk delete after undo to emit one geometry change and delete the selected
     * nodes again.
     * Uses a local MapUndoStack and borrowed loaded fixture layer; checks actual engine
     * absence after redo, without saving the mutated model.
     *
     */
    void redoAfterUndoIsSymmetric()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int nodes0 = layer->categoryCount(SWMMModelLayer::CatJunctions);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 2"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2")})
            new DeleteObjectCommand(layer.get(), n, DeleteObjectCommand::DeleteNode, nullptr,
                                    macro);
        stack.push(macro);
        stack.undo();

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        stack.redo();
        QCOMPARE(geom.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0 - 2);
        QCOMPARE(swmm_node_index(layer->engine(), "J1"), -1);
    }

    // ── The silent-corruption guard ────────────────────────────────────────

    //! THE critical test. Deleting SoA index 0 shifts every later node index,
    //! so any link whose endpoint index is not renumbered now resolves to the
    //! WRONG node — and nothing errors, the model just draws wrong.
    //!
    //! Falsifier: comment out syncLinkEndpointIndicesFromEngine() in
    //! endBulkEdit() and C3's polyline starts at J4 (2000,-800) instead of
    //! J3 (2000,0).
    void bulkDeleteLeavesCachesCoherent()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();

        // J1 is node index 0, and C3 (J3->O1) is downstream of it in index
        // order, so deleting J1 shifts C3's from-node index by one.
        QCOMPARE(swmm_node_index(eng, "J1"), 0);
        const QPointF j3xy = engineNodeXY(eng, QStringLiteral("J3"));
        const QPointF j4xy = engineNodeXY(eng, QStringLiteral("J4"));
        QVERIFY(j3xy != j4xy); // else the assertion below could not fail

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete J1"));
        new DeleteObjectCommand(layer.get(), QStringLiteral("J1"), DeleteObjectCommand::DeleteNode,
                                nullptr, macro);
        stack.push(macro);

        // Counts agree with the engine after the deferred rebuild.
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions),
                 3); // J2 J3 J4 (J1 gone)
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatConduits),
                 2); // C1 cascaded with J1

        // Name->index maps agree with the engine, for every survivor.
        for (const char *n : {"J2", "J3", "J4", "O1"})
            QCOMPARE(layer->nodeIndex(QString::fromLatin1(n)), swmm_node_index(eng, n));
        for (const char *l : {"C2", "C3", "P1"})
            QCOMPARE(layer->linkIndex(QString::fromLatin1(l)), swmm_link_index(eng, l));

        // The one that matters: C3's cached polyline must still start at J3.
        // Without the endpoint resync its stale fromNodeIdx now names J4.
        const int c3 = layer->linkIndex(QStringLiteral("C3"));
        QVERIFY(c3 >= 0);
        const QVector<QPointF> poly = layer->cachedLinkPolyline(c3);
        QVERIFY(!poly.isEmpty());
        QCOMPARE(poly.first(), j3xy);
        QCOMPARE(poly.last(), engineNodeXY(eng, QStringLiteral("O1")));

        // And the pump, whose from-node J4 also shifted.
        const int p1 = layer->linkIndex(QStringLiteral("P1"));
        QVERIFY(p1 >= 0);
        const QVector<QPointF> ppoly = layer->cachedLinkPolyline(p1);
        QVERIFY(!ppoly.isEmpty());
        QCOMPARE(ppoly.first(), j4xy);
    }

    //! Same coherence check, but through undo — which re-adds nodes at the
    //! engine TAIL rather than their original index, so every link endpoint
    //! index changes again.
    void undoLeavesCachesCoherent()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete J1"));
        new DeleteObjectCommand(layer.get(), QStringLiteral("J1"), DeleteObjectCommand::DeleteNode,
                                nullptr, macro);
        stack.push(macro);
        stack.undo();

        SWMM_Engine eng = layer->engine();
        for (const char *l : {"C1", "C2", "C3", "P1"}) {
            const int li = layer->linkIndex(QString::fromLatin1(l));
            QVERIFY2(li >= 0, l);
            QCOMPARE(li, swmm_link_index(eng, l));

            // Resolve the endpoint by INDEX straight from the engine — no
            // name lookup, so the oracle shares nothing with the cache it is
            // checking.
            int fromIdx = -1;
            QCOMPARE(swmm_link_get_from_node(eng, li, &fromIdx), 0);
            double fx = 0, fy = 0;
            QCOMPARE(swmm_spatial_get_node_coord(eng, fromIdx, &fx, &fy), 0);

            const QVector<QPointF> poly = layer->cachedLinkPolyline(li);
            QVERIFY2(!poly.isEmpty(), l);
            QCOMPARE(poly.first(), QPointF(fx, fy));
        }
    }

    // ── Cascade reporting ──────────────────────────────────────────────────

    //! J2 sits between C1 (J1->J2) and C2 (J2->J3), so deleting it cascades
    //! both. Pins the cascade-name contract that step 3 re-implements on top
    //! of the engine's own SWMM_ImpactReport.
    void cascadeLinkNamesAreReported()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        QStringList cascade;
        QVERIFY(layer->applyNodeDelete(QStringLiteral("J2"), &cascade));
        cascade.sort();
        QCOMPARE(cascade, QStringList({QStringLiteral("C1"), QStringLiteral("C2")}));
    }

    // ── Entry points ───────────────────────────────────────────────────────

    //! The headless branch of AttributeTablePanel::deleteObjects must batch
    //! too, so tests exercise the same path the UI does — and the terminal
    //! emission must still arrive before deleteObjects() returns (the
    //! "coalesce, never skip" rule).
    void headlessFallbackBatchesToo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QLatin1String("Junctions")))
                combo->setCurrentIndex(i);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        const int n =
            panel.deleteObjects({QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("J4")});
        QCOMPARE(n, 3);
        // Already emitted by the time deleteObjects() returned — never
        // deferred past the call, only coalesced within it.
        QCOMPARE(geom.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), 1);
    }

    //! End-state contract: after a delete through the panel, the canonical
    //! selection bus must hold no ref naming a deleted object.
    //!
    //! HONEST SCOPE NOTE: this passes with AND without the explicit
    //! m_selMgr->clear() in deleteObjects() — some downstream effect of the
    //! post-delete refresh() empties the bus anyway. So it is a regression
    //! guard on the end state, NOT a pin on that call. Verified by probe:
    //! disabling the clear leaves this green.
    //!
    //! The clear is kept regardless, because dropping the canonical
    //! selection BEFORE mutating is the correct ordering, and relying on a
    //! view-bridge side effect to reach the same end state is fragile —
    //! it holds only while a panel is attached to the bus.
    void attributeTableDeleteLeavesNoPhantomRefs()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QLatin1String("Junctions")))
                combo->setCurrentIndex(i);

        selMgr.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, QStringLiteral("J1")},
                                          {SWMMObjectRef::Node, QStringLiteral("J2")}});
        QVERIFY(!selMgr.isEmpty());

        QCOMPARE(panel.deleteObjects({QStringLiteral("J1"), QStringLiteral("J2")}), 2);
        for (const SWMMObjectRef &r : selMgr.selection())
            QVERIFY2(r.name != QLatin1String("J1") && r.name != QLatin1String("J2"),
                     qPrintable(QStringLiteral("phantom ref survived: %1").arg(r.name)));
    }

    // ── Phase A2 — BatchDeleteCommand parity with the per-object path ─────

    //! The batched delete (one swmm_*_delete_many per kind) must land on
    //! exactly the state the per-object reference path produces, and its
    //! undo must restore exactly what per-object undo restores.
    void batchDeleteMatchesPerObjectReference()
    {
        auto batchLayer = openLayer();
        auto refLayer = openLayer();
        QVERIFY(batchLayer);
        QVERIFY(refLayer);

        const QStringList victims{QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("J4")};

        MapUndoStack batchStack;
        QList<BatchDeleteCommand::Target> targets;
        for (const QString &n : victims)
            targets.append({n, DeleteObjectCommand::DeleteNode});
        QSignalSpy geom(batchLayer.get(), &SWMMModelLayer::geometryChanged);
        batchStack.push(
            new BatchDeleteCommand(batchLayer.get(), targets, nullptr, QStringLiteral("Batch")));
        // The batch keeps the one-notification contract.
        QCOMPARE(geom.count(), 1);

        MapUndoStack refStack;
        auto *refMacro = new BulkEditCommand(refLayer.get(), QStringLiteral("Ref"));
        for (const QString &n : victims)
            new DeleteObjectCommand(refLayer.get(), n, DeleteObjectCommand::DeleteNode, nullptr,
                                    refMacro);
        refStack.push(refMacro);

        const auto sameState = [](SWMMModelLayer *a, SWMMModelLayer *b) {
            const int nn = swmm_node_count(a->engine());
            if (nn != swmm_node_count(b->engine()))
                return false;
            for (int i = 0; i < nn; ++i)
                if (QString::fromUtf8(swmm_node_id(a->engine(), i)) !=
                    QString::fromUtf8(swmm_node_id(b->engine(), i)))
                    return false;
            const int nl = swmm_link_count(a->engine());
            if (nl != swmm_link_count(b->engine()))
                return false;
            for (int i = 0; i < nl; ++i)
                if (QString::fromUtf8(swmm_link_id(a->engine(), i)) !=
                    QString::fromUtf8(swmm_link_id(b->engine(), i)))
                    return false;
            // The SoA mirrors must agree with their engines too.
            if (a->cachedNodeCount() != nn || b->cachedNodeCount() != nn)
                return false;
            if (a->cachedLinkCount() != nl || b->cachedLinkCount() != nl)
                return false;
            return true;
        };

        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-delete state diverged from the per-object reference");

        // Undo restores identically on both paths…
        batchStack.undo();
        refStack.undo();
        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-undo state diverged from the per-object reference");
        QCOMPARE(swmm_node_index(batchLayer->engine(), "J1") >= 0, true);

        // …and redo re-deletes identically.
        batchStack.redo();
        refStack.redo();
        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-redo state diverged from the per-object reference");
        QCOMPARE(swmm_node_index(batchLayer->engine(), "J1"), -1);
    }

    //! Select-all + Delete: every object goes, so every SoA lands empty.
    //!
    //! buildGeometryCache() used to bail out early on a fully empty model,
    //! which skipped rebuildSceneCoords() / rebuildKdTrees() /
    //! rebuildFlagArrays() along with the extent it meant to skip. The
    //! category index is rebuilt BEFORE that point, so the object browser
    //! showed the deletion while m_linkVertexCount still described the
    //! previous model — and SWMMLayerQSGRenderer, which walks that array
    //! rather than m_links, kept drawing every deleted link.
    void deleteEveryObjectClearsRenderCaches()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        // Precondition: the fixture really does have links to leave behind.
        QVERIFY(layer->cachedLinkCount() > 0);
        QCOMPARE(layer->renderLinkCount(), layer->cachedLinkCount());

        // Every object, exactly as the Select tool builds the list from a
        // select-all: nodes (the four links cascade with their end nodes),
        // then the gage, then the subcatchments. Every SoA has to land
        // empty — leaving even one populated is what kept the old early-out
        // from firing.
        QList<BatchDeleteCommand::Target> targets;
        for (const char *n : {"J1", "J2", "J3", "J4", "O1"})
            targets.append({QString::fromLatin1(n), DeleteObjectCommand::DeleteNode});
        targets.append({QStringLiteral("RG1"), DeleteObjectCommand::DeleteGage});
        for (const char *s : {"S1", "S2", "S3", "S4"})
            targets.append({QString::fromLatin1(s), DeleteObjectCommand::DeleteSubcatch});

        MapUndoStack stack;
        stack.push(
            new BatchDeleteCommand(layer.get(), targets, nullptr, QStringLiteral("Delete All")));

        // The model really is empty — every SoA, or the old early-out would
        // not have fired and this test would pass without the fix.
        QCOMPARE(swmm_node_count(layer->engine()), 0);
        QCOMPARE(swmm_link_count(layer->engine()), 0);
        QCOMPARE(layer->cachedNodeCount(), 0);
        QCOMPARE(layer->cachedLinkCount(), 0);
        QCOMPARE(layer->cachedGageCount(), 0);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatSubcatchments), 0);
        // …the object browser's index agrees…
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatConduits), 0);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatPumps), 0);
        // …and so does what the renderer would draw.
        QCOMPARE(layer->renderLinkCount(), 0);

        // Undo re-arms the render arrays along with the model. Asserted as
        // the invariant rather than a count, so restore-order questions for
        // subcatchments and their gage can't fail this test for an unrelated
        // reason.
        stack.undo();
        QCOMPARE(layer->renderLinkCount(), layer->cachedLinkCount());
    }

    //! Review E-04: undoing a link delete, single or batch, restores the model exactly: the saved
    //! .inp is byte-identical and the layer's link cache matches the engine index for index. In
    //! a batch that also deletes a node, the link record is undone before the node's (partial)
    //! snapshot restore, so the link still lands at its index. Files go to
    //! tests/output/delete_undo_2026-11-12/link_delete_undo/.
    void deleteUndoRestoresLinksExactly()
    {
        const QDir dir(QStringLiteral(SWMMVIS_DELETE_UNDO_OUTPUT_DIR "/link_delete_undo"));
        QVERIFY(QDir().mkpath(dir.path()));
        {
            QFile inp(dir.filePath(QStringLiteral("input.inp")));
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Truncate));
            inp.write(kLinksInp);
        }
        auto layer = std::make_unique<SWMMModelLayer>(dir.filePath(QStringLiteral("input.inp")),
                                                      nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        SWMM_Engine e = layer->engine();
        const QByteArray original = saveInp(e, dir, QStringLiteral("1_original.inp"));
        QVERIFY(!original.isEmpty());
        const QStringList links = engineLinks(e);
        QCOMPARE(links.size(), 7);
        QVERIFY(linkCacheMatchesEngine(*layer));

        MapUndoStack stack;
        // One link (Object Browser).
        stack.push(new DeleteObjectCommand(layer.get(), QStringLiteral("GUT1"),
                                           DeleteObjectCommand::DeleteLink, nullptr));
        QCOMPARE(swmm_link_index(e, "GUT1"), -1);
        QVERIFY(linkCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineLinks(e), links);
        QVERIFY(linkCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("2_single_undo.inp")), original);
        stack.redo();
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("3_single_redo_undo.inp")), original);

        // A batch with one link of every kind (map Delete key, Attribute Table).
        QList<BatchDeleteCommand::Target> targets;
        for (const char *n : {"C1", "GUT1", "P1", "OR1", "W1", "OL1"})
            targets.append({QString::fromLatin1(n), DeleteObjectCommand::DeleteLink});
        stack.push(new BatchDeleteCommand(layer.get(), targets, nullptr, QStringLiteral("Delete")));
        QCOMPARE(engineLinks(e), QStringList{QStringLiteral("C3")});
        QVERIFY(linkCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineLinks(e), links);
        QVERIFY(linkCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("4_batch_undo.inp")), original);

        // A node and a link together: J5 cascades OR1 and OL1 and loses GUT1's inlet usage.
        stack.push(new BatchDeleteCommand(
            layer.get(),
            {{QStringLiteral("J5"), DeleteObjectCommand::DeleteNode},
             {QStringLiteral("GUT1"), DeleteObjectCommand::DeleteLink}},
            nullptr, QStringLiteral("Delete")));
        QCOMPARE(swmm_link_index(e, "GUT1"), -1);
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("5_mixed_undo.inp")), original);
        QCOMPARE(engineLinks(e), links);
        QVERIFY(linkCacheMatchesEngine(*layer));
        QVERIFY(nodeCacheMatchesEngine(*layer));
    }

    void deleteUndoRestoresNodesExactly()
    {
        const QDir dir(QStringLiteral(SWMMVIS_DELETE_UNDO_OUTPUT_DIR "/node_delete_undo"));
        QVERIFY(QDir().mkpath(dir.path()));
        {
            QFile inp(dir.filePath(QStringLiteral("input.inp")));
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Truncate));
            inp.write(kNodesInp);
        }
        auto layer = std::make_unique<SWMMModelLayer>(dir.filePath(QStringLiteral("input.inp")),
                                                      nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        SWMM_Engine e = layer->engine();
        const QByteArray original = saveInp(e, dir, QStringLiteral("1_original.inp"));
        QVERIFY(!original.isEmpty());
        const QStringList nodes = engineNodes(e), links = engineLinks(e);
        QCOMPARE(nodes.size(), 6);
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));

        MapUndoStack stack;
        // One node (Object Browser): its four links go with it.
        stack.push(new DeleteObjectCommand(layer.get(), QStringLiteral("N2"),
                                           DeleteObjectCommand::DeleteNode, nullptr));
        QCOMPARE(swmm_node_index(e, "N2"), -1);
        QCOMPARE(engineLinks(e), (QStringList{QStringLiteral("C4"), QStringLiteral("GUT")}));
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineNodes(e), nodes);
        QCOMPARE(engineLinks(e), links);
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("2_single_undo.inp")), original);
        stack.redo();
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("3_single_redo_undo.inp")), original);

        // A batch with a node from every subtype table (map Delete key, Attribute Table).
        QList<BatchDeleteCommand::Target> targets;
        for (const char *n : {"J1", "N2", "D1", "O1"})
            targets.append({QString::fromLatin1(n), DeleteObjectCommand::DeleteNode});
        stack.push(new BatchDeleteCommand(layer.get(), targets, nullptr, QStringLiteral("Delete")));
        QCOMPARE(engineNodes(e), (QStringList{QStringLiteral("J3"), QStringLiteral("J4")}));
        QVERIFY(nodeCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineNodes(e), nodes);
        QCOMPARE(engineLinks(e), links);
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("4_batch_undo.inp")), original);

        // Nodes, a link and a rain gage together: each record restores in reverse order.
        stack.push(new BatchDeleteCommand(
            layer.get(),
            {{QStringLiteral("N2"), DeleteObjectCommand::DeleteNode},
             {QStringLiteral("J3"), DeleteObjectCommand::DeleteNode},
             {QStringLiteral("C4"), DeleteObjectCommand::DeleteLink},
             {QStringLiteral("G1"), DeleteObjectCommand::DeleteGage}},
            nullptr, QStringLiteral("Delete")));
        QCOMPARE(engineLinks(e), QStringList{});
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("5_mixed_undo.inp")), original);
        QCOMPARE(engineNodes(e), nodes);
        QCOMPARE(engineLinks(e), links);
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));
    }

    void deleteUndoRestoresSubcatchmentsExactly()
    {
        const QDir dir(QStringLiteral(SWMMVIS_DELETE_UNDO_OUTPUT_DIR "/subcatch_delete_undo"));
        QVERIFY(QDir().mkpath(dir.path()));
        {
            QFile inp(dir.filePath(QStringLiteral("input.inp")));
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Truncate));
            inp.write(kSubcatchInp);
        }
        auto layer = std::make_unique<SWMMModelLayer>(dir.filePath(QStringLiteral("input.inp")),
                                                      nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        SWMM_Engine e = layer->engine();
        const QByteArray original = saveInp(e, dir, QStringLiteral("1_original.inp"));
        QVERIFY(!original.isEmpty());
        const QStringList subcatches = engineSubcatches(e);
        QCOMPARE(subcatches.size(), 4);
        QVERIFY(subcatchCacheMatchesEngine(*layer));

        MapUndoStack stack;
        // One subcatchment (Object Browser).
        stack.push(new DeleteObjectCommand(layer.get(), QStringLiteral("S2"),
                                           DeleteObjectCommand::DeleteSubcatch, nullptr));
        QCOMPARE(swmm_subcatch_index(e, "S2"), -1);
        QVERIFY(subcatchCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineSubcatches(e), subcatches);
        QVERIFY(subcatchCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("2_single_undo.inp")), original);
        stack.redo();
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("3_single_redo_undo.inp")), original);

        // A batch of subcatchments (map Delete key, Attribute Table).
        QList<BatchDeleteCommand::Target> targets;
        for (const char *n : {"S1", "S2", "S3"})
            targets.append({QString::fromLatin1(n), DeleteObjectCommand::DeleteSubcatch});
        stack.push(new BatchDeleteCommand(layer.get(), targets, nullptr, QStringLiteral("Delete")));
        QCOMPARE(engineSubcatches(e), QStringList{QStringLiteral("S4")});
        QVERIFY(subcatchCacheMatchesEngine(*layer));
        stack.undo();
        QCOMPARE(engineSubcatches(e), subcatches);
        QVERIFY(subcatchCacheMatchesEngine(*layer));
        QCOMPARE(saveInp(e, dir, QStringLiteral("4_batch_undo.inp")), original);

        // Every kind together: a subcatchment, the node it drains to, a link and the rain gage.
        stack.push(new BatchDeleteCommand(
            layer.get(),
            {{QStringLiteral("S2"), DeleteObjectCommand::DeleteSubcatch},
             {QStringLiteral("S3"), DeleteObjectCommand::DeleteSubcatch},
             {QStringLiteral("J1"), DeleteObjectCommand::DeleteNode},
             {QStringLiteral("C2"), DeleteObjectCommand::DeleteLink},
             {QStringLiteral("G1"), DeleteObjectCommand::DeleteGage}},
            nullptr, QStringLiteral("Delete")));
        QCOMPARE(swmm_node_index(e, "J1"), -1);
        stack.undo();
        QCOMPARE(saveInp(e, dir, QStringLiteral("5_mixed_undo.inp")), original);
        QCOMPARE(engineSubcatches(e), subcatches);
        QVERIFY(subcatchCacheMatchesEngine(*layer));
        QVERIFY(nodeCacheMatchesEngine(*layer));
        QVERIFY(linkCacheMatchesEngine(*layer));
    }

    // ── Profiling probe (perf-plan Phase 0; skipped unless env-gated) ──────

    //! Bulk-delete baseline on a REAL model: set SWMM_PROFILE_INP=<path.inp>
    //! (the profileExternalModel idiom) and optionally
    //! SWMM_PROFILE_DELETE_N=COUNT (default 1000). Reports snapshot vs
    //! execute+close ms; enable QT_LOGGING_RULES="openswmm.bulkdelete=true"
    //! for the endBulkEdit sub-splits. Never runs in CI.
    void profileBulkDelete()
    {
        const QString inp = qEnvironmentVariable("SWMM_PROFILE_INP");
        if (inp.isEmpty())
            QSKIP("set SWMM_PROFILE_INP=<path.inp> to run the bulk-delete profile");

        auto layer = std::make_unique<SWMMModelLayer>(inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join(QStringLiteral("; "))));

        const int junctions = layer->categoryCount(SWMMModelLayer::CatJunctions);
        const int wanted =
            qEnvironmentVariable("SWMM_PROFILE_DELETE_N", QStringLiteral("1000")).toInt();
        const int k = std::min(wanted, junctions);
        QVERIFY2(k > 0, "profile model has no junctions to delete");

        QStringList names;
        names.reserve(k);
        for (int i = 0; i < k; ++i)
            names.append(layer->objectNameAt(SWMMModelLayer::CatJunctions, i));

        QElapsedTimer timer;
        timer.start();
        MapUndoStack stack;
        QList<BatchDeleteCommand::Target> targets;
        targets.reserve(names.size());
        for (const QString &n : std::as_const(names))
            targets.append({n, DeleteObjectCommand::DeleteNode});
        auto *macro =
            new BatchDeleteCommand(layer.get(), targets, nullptr, QStringLiteral("Profile Delete"));
        const qint64 snapshotMs = timer.elapsed();
        stack.push(macro);
        const qint64 totalMs = timer.elapsed();

        qInfo().nospace() << "[profileBulkDelete] " << k << " node(s) of " << junctions
                          << " — snapshots " << snapshotMs << " ms, execute+close "
                          << (totalMs - snapshotMs) << " ms, total " << totalMs << " ms";
    }
};

QTEST_MAIN(TestBulkDelete)
#include "test_bulkdelete.moc"
