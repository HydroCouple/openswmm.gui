/**
 * @file      test_raingagecoords.cpp
 * @brief     Rain-gage map position ([SYMBOLS] X / Y) is readable AND editable
 *         from both editing surfaces.
 *
 * A rain gage is a placed object, but the DA.2 adapter set derives it from the
 * non-spatial data-object base, so the Property Browser exposed no X/Y at all
 * and the Attribute Table carried them read-only — the gage was the one placed
 * object whose location could not be typed in (only drawn, via Add Rain Gage).
 *
 * Both surfaces now write through `SWMMModelLayer::applyGageMove`, the
 * [SYMBOLS] twin of `applyNodeMove`, so the engine value and the cached scene
 * point move together. These tests pin:
 *   - the adapter reads the engine coordinate and DEFERS its write (it emits
 *     coordChangeRequested rather than calling the engine itself — that is
 *     what lets the panel refresh the canvas caches);
 *   - the table's X / Y columns are editable and commit through the layer;
 *   - applyGageMove updates the engine, the layer cache and the identify map,
 *     and reports the model as edited;
 *   - a coordinate written here survives a save → reopen round trip.
 *
 * Fixture: `typed_selection_fixture.inp` — one gage `S1` at (-2000, -2000).
 * Output is written next to the fixture (reviewable, CLAUDE.md §4.1).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

#include <memory>

using openswmmvis::ColumnSpec;

namespace
{

/**
 *
 * @brief Read SWMMVIS_GUI_TEST_DATA or use the current directory for coordinate fixtures.
 * @return Owned unvalidated directory string; no creation.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Join a fixture/output name beneath dataDir().
 * @param name Borrowed relative filename, or any path QDir::filePath accepts.
 * @return Owned path without traversal/existence/writability validation or IO.
 *
 */
QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

/**
 *
 * @brief Load a coordinate fixture into a caller-owned model layer.
 * @param file Borrowed filename under fixture(), default typed_selection_fixture.inp.
 * @return Unique layer on successful engine load, nullptr on failure (temporary
 * layer/engine are released). Warnings/errors are discarded; referenced file IO
 * may occur but no model is written. Call on the GUI thread.
 *
 */
std::unique_ptr<SWMMModelLayer>
openLayer(const QString &file = QStringLiteral("typed_selection_fixture.inp"))
{
    auto layer = std::make_unique<SWMMModelLayer>(fixture(file), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/**
 *
 * @brief Find the first attribute-table column with an exact setter token.
 * @param specs Borrowed ordered column specifications.
 * @param tag Borrowed case-sensitive setter identifier.
 * @return Zero-based index or -1 when absent; no model or input mutation.
 *
 */
int colForSetter(const QList<ColumnSpec> &specs, const QString &tag)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].setter == tag)
            return i;
    return -1;
}

constexpr double kNewX = 1234.5;
constexpr double kNewY = -6789.25;

// Every gage field the .inp carries and every reference to a gage (review E-04):
// subcatchments on both sides of the middle gage's index, a unit-hydrograph group,
// a user flag and map coordinates.
const char *kGagesInp = R"([TITLE]
Delete/undo round trip for rain gages

[OPTIONS]
FLOW_UNITS           CFS
INFILTRATION         HORTON
FLOW_ROUTING         KINWAVE
START_DATE           01/01/2024
START_TIME           00:00:00
END_DATE             01/01/2024
END_TIME             06:00:00

[RAINGAGES]
G1      INTENSITY  0:15   1.0   TIMESERIES TS1
G2      VOLUME     0:05   0.8   FILE "rain.dat" STA42 MM * 2.5
G3      CUMULATIVE 1:00   1.1   TIMESERIES TS1

[SUBCATCHMENTS]
S1      G2       J1     1    50      100   0.5    0
S2      G3       J1     2    40      100   0.5    0
S3      G2       J1     3    30      100   0.5    0

[SUBAREAS]
S1      0.01  0.1  0.05  0.05  25  OUTLET
S2      0.01  0.1  0.05  0.05  25  OUTLET
S3      0.01  0.1  0.05  0.05  25  OUTLET

[INFILTRATION]
S1      3.0  0.5  4  7  0
S2      3.0  0.5  4  7  0
S3      3.0  0.5  4  7  0

[JUNCTIONS]
J1      0  5  0  0  0

[OUTFALLS]
O1      -1  FREE  NO

[CONDUITS]
C1      J1  O1  100  0.01  0  0  0  0

[XSECTIONS]
C1      CIRCULAR  1  0  0  0  1

[HYDROGRAPHS]
UH1     G2
UH1     ALL  SHORT  0.1  1  2

[TIMESERIES]
TS1     0:00  0.5
TS1     1:00  0.0

[USER_FLAGS]
Owner   STRING

[USER_FLAG_VALUES]
GAGE    G2   Owner  city

[COORDINATES]
J1      0    0
O1      100  0

[SYMBOLS]
G1      10   10
G2      20   20
G3      30   30
)";

/**
 * @brief Save the model to dir/name and return the file's bytes (empty on failure).
 * @param e Live engine. @param dir Output folder. @param name File name in it.
 */
QByteArray saveInp(SWMM_Engine e, const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    if (swmm_model_write(e, path.toUtf8().constData()) != 0)
        return {};
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/** @brief Gage names in engine order. */
QStringList engineGages(SWMM_Engine e)
{
    QStringList names;
    for (int i = 0; i < swmm_gage_count(e); ++i)
        names << QString::fromUtf8(swmm_gage_id(e, i));
    return names;
}

/** @brief Gage names in the layer's cache order (row == SoA index == engine index). */
QStringList layerGages(const SWMMModelLayer &layer)
{
    QStringList names;
    for (int row = 0; row < layer.categoryCount(SWMMModelLayer::CatRainGages); ++row)
        names << layer.objectNameAt(SWMMModelLayer::CatRainGages, row);
    return names;
}

/** @brief A subcatchment's gage index, or -2 when the getter fails. */
int subcatchGage(SWMM_Engine e, const char *subcatch)
{
    int g = -2;
    swmm_subcatch_get_gage(e, swmm_subcatch_index(e, subcatch), &g);
    return g;
}

} // namespace

class TestRainGageCoords : public QObject
{
    Q_OBJECT

private slots:

    //! The Property Browser exposes X/Y, reads them from the engine, and
    //! defers the write so the panel can route it through the layer.
    void adapterReadsAndDefersCoordinateWrites()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int gi = swmm_gage_index(e, "S1");
        QVERIFY(gi >= 0);

        SWMMRainGagePropertyAdapter adapter(e, QStringLiteral("S1"));
        QCOMPARE(adapter.xCoord(), -2000.0);
        QCOMPARE(adapter.yCoord(), -2000.0);

        // Labelled like the node adapter's rows.
        QCOMPARE(adapter.displayLabelFor(QStringLiteral("xCoord")), QStringLiteral("X Coordinate"));
        QCOMPARE(adapter.displayLabelFor(QStringLiteral("yCoord")), QStringLiteral("Y Coordinate"));

        // Both properties are on the meta-object, so QPropertyModel shows them.
        QVERIFY(adapter.metaObject()->indexOfProperty("xCoord") >= 0);
        QVERIFY(adapter.metaObject()->indexOfProperty("yCoord") >= 0);

        // The setter must NOT write the engine itself — it asks the panel to,
        // carrying both coordinates.
        QSignalSpy spy(&adapter, &SWMMRainGagePropertyAdapter::coordChangeRequested);
        adapter.setXCoord(kNewX);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), kNewX);
        QCOMPARE(spy.at(0).at(1).toDouble(), -2000.0); // Y unchanged
        double x = 0.0, y = 0.0;
        swmm_spatial_get_gage_coord(e, gi, &x, &y);
        QCOMPARE(x, -2000.0); // still untouched: the panel applies it

        adapter.setYCoord(kNewY);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toDouble(), -2000.0);
        QCOMPARE(spy.at(1).at(1).toDouble(), kNewY);

        // Setting the value it already has is a no-op (no spurious edit).
        adapter.setXCoord(-2000.0);
        QCOMPARE(spy.count(), 2);
    }

    //! applyGageMove writes the engine, the layer cache and the identify map,
    //! and reports the model as edited.
    void layerMoveUpdatesEngineAndCaches()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int gi = swmm_gage_index(e, "S1");
        QVERIFY(gi >= 0);

        QSignalSpy edited(layer.get(), &SWMMModelLayer::modelEdited);
        QSignalSpy repaint(layer.get(), &SWMMModelLayer::repaintRequested);

        QVERIFY(layer->applyGageMove(gi, kNewX, kNewY));

        double x = 0.0, y = 0.0;
        QCOMPARE(swmm_spatial_get_gage_coord(e, gi, &x, &y), 0);
        QCOMPARE(x, kNewX);
        QCOMPARE(y, kNewY);

        // The layer's own cached position (what the canvas and identify use).
        // Kind-scoped: this fixture deliberately has a subcatchment ALSO
        // named S1, and the unscoped lookup resolves the collision by the
        // legacy precedence (node → link → catchment → gage) — so the gage
        // is only reachable with the gage kind bit, exactly as the
        // Attribute Table's row cache reads it.
        const QVariantMap m =
            layer->identifyByName(QStringLiteral("S1"), SWMMModelLayer::kKindGage);
        QCOMPARE(m.value(QStringLiteral("X")).toDouble(), kNewX);
        QCOMPARE(m.value(QStringLiteral("Y")).toDouble(), kNewY);

        // The collision itself: unscoped identify still yields the
        // subcatchment (legacy precedence), which carries no "X".
        QVERIFY(!layer->identifyByName(QStringLiteral("S1")).contains(QStringLiteral("X")));

        QVERIFY(edited.count() >= 1);
        QVERIFY(repaint.count() >= 1);

        // Out-of-range index is refused, not clamped.
        QVERIFY(!layer->applyGageMove(-1, 0.0, 0.0));
        QVERIFY(!layer->applyGageMove(9999, 0.0, 0.0));
    }

    //! The Attribute Table's X / Y columns are editable and commit through
    //! the layer (they were read-only before).
    void tableCoordinateColumnsAreEditableAndCommit()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();

        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        QCOMPARE(model.rowCount(), 1);

        const auto specs = model.columnSpecs();
        const int xCol = colForSetter(specs, QStringLiteral("gage_coord_x"));
        const int yCol = colForSetter(specs, QStringLiteral("gage_coord_y"));
        QVERIFY2(xCol >= 0, "rain-gage X column is not editable (no setter tag)");
        QVERIFY2(yCol >= 0, "rain-gage Y column is not editable (no setter tag)");

        QVERIFY(model.flags(model.index(0, xCol)) & Qt::ItemIsEditable);
        QVERIFY(model.flags(model.index(0, yCol)) & Qt::ItemIsEditable);

        // Reads come from the layer's identify map, as before.
        QCOMPARE(model.data(model.index(0, xCol), Qt::EditRole).toDouble(), -2000.0);

        QVERIFY(model.setData(model.index(0, xCol), kNewX, Qt::EditRole));
        QVERIFY(model.setData(model.index(0, yCol), kNewY, Qt::EditRole));

        double x = 0.0, y = 0.0;
        QCOMPARE(swmm_spatial_get_gage_coord(e, swmm_gage_index(e, "S1"), &x, &y), 0);
        QCOMPARE(x, kNewX);
        QCOMPARE(y, kNewY);
        // And the cell re-reads the new value.
        QCOMPARE(model.data(model.index(0, xCol), Qt::EditRole).toDouble(), kNewX);
        QCOMPARE(model.data(model.index(0, yCol), Qt::EditRole).toDouble(), kNewY);
    }

    //! A typed coordinate reaches [SYMBOLS] and survives a reopen.
    void editedCoordinateSurvivesSaveAndReopen()
    {
        const QString out = fixture(QStringLiteral("raingage_coords_out.inp"));
        {
            auto layer = openLayer();
            QVERIFY(layer);
            SWMM_Engine e = layer->engine();
            const int gi = swmm_gage_index(e, "S1");
            QVERIFY(layer->applyGageMove(gi, kNewX, kNewY));
            QCOMPARE(swmm_model_write(e, out.toUtf8().constData()), 0);
        }

        auto reopened = openLayer(QStringLiteral("raingage_coords_out.inp"));
        QVERIFY(reopened);
        double x = 0.0, y = 0.0;
        const int gi = swmm_gage_index(reopened->engine(), "S1");
        QVERIFY(gi >= 0);
        QCOMPARE(swmm_spatial_get_gage_coord(reopened->engine(), gi, &x, &y), 0);
        QCOMPARE(x, kNewX);
        QCOMPARE(y, kNewY);
    }

    /**
     *
     * @brief Require gage drag preview to change only cached coordinates, then merged move
     * commands to commit/undo/redo engine coordinates.
     * Reads the typed-selection fixture, uses a local undo stack and writes no file;
     * restores the cache-only preview before pushing the first command.
     *
     */
    void dragPreviewAndMoveGageCommand()
    {
        // The map drag path (Edit Existing → move tool): previewGageMove
        // is cache-only during the drag; MoveGageCommand commits through
        // applyGageMove on release, merges consecutive same-gage moves,
        // and one undo returns to the pre-drag position.
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int gi = swmm_gage_index(e, "S1");
        QVERIFY(gi >= 0);

        // Preview: the layer's cached position follows, the engine does not.
        QVERIFY(layer->previewGageMove(gi, 111.0, 222.0));
        QCOMPARE(layer->identifyByName(QStringLiteral("S1"), SWMMModelLayer::kKindGage)
                     .value(QStringLiteral("X"))
                     .toDouble(),
                 111.0);
        double ex = 0.0, ey = 0.0;
        QCOMPARE(swmm_spatial_get_gage_coord(e, gi, &ex, &ey), 0);
        QCOMPARE(ex, -2000.0);
        // The tool restores the preview before pushing the command.
        QVERIFY(layer->previewGageMove(gi, -2000.0, -2000.0));

        QUndoStack stack;
        stack.push(new MoveGageCommand(layer.get(), gi, -2000.0, -2000.0, 10.0, 20.0, nullptr));
        stack.push(new MoveGageCommand(layer.get(), gi, 10.0, 20.0, 30.0, 40.0, nullptr));
        QCOMPARE(stack.count(), 1); // same-gage moves merged
        QCOMPARE(swmm_spatial_get_gage_coord(e, gi, &ex, &ey), 0);
        QCOMPARE(ex, 30.0);
        QCOMPARE(ey, 40.0);

        stack.undo();
        QCOMPARE(swmm_spatial_get_gage_coord(e, gi, &ex, &ey), 0);
        QCOMPARE(ex, -2000.0);
        QCOMPARE(ey, -2000.0);

        stack.redo();
        QCOMPARE(swmm_spatial_get_gage_coord(e, gi, &ex, &ey), 0);
        QCOMPARE(ex, 30.0);
        QCOMPARE(ey, 40.0);
    }

    // Review E-03 (workplans/GUI_COMPREHENSIVE_REVIEW_2026-11-12.md): the
    // move command stored the node's engine index. Undoing a delete re-adds
    // the node at the END of the list, so the older move then undid onto
    // whichever node had taken its old index.
    /** @brief Verify a move undone after delete+undo of the same node moves that node, not its old index's new owner. */
    void moveUndoFollowsNodeAcrossDeleteUndo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int j1 = swmm_node_index(e, "J1");
        QCOMPARE(j1, 0);

        QUndoStack stack;
        stack.push(new MoveNodeCommand(layer.get(), j1, 0.0, 0.0, 5.0, 5.0, {}, nullptr));
        stack.push(new DeleteObjectCommand(layer.get(), QStringLiteral("J1"),
                                           DeleteObjectCommand::DeleteNode, nullptr));
        QCOMPARE(swmm_node_index(e, "J1"), -1);
        stack.undo(); // J1 returns at its own index (E-04 restores in place)
        QCOMPARE(swmm_node_index(e, "J1"), j1);
        stack.undo(); // the move

        double x = 0.0, y = 0.0;
        QCOMPARE(swmm_spatial_get_node_coord(e, swmm_node_index(e, "J1"), &x, &y), 0);
        QCOMPARE(x, 0.0);
        QCOMPARE(y, 0.0);
        QCOMPARE(swmm_spatial_get_node_coord(e, swmm_node_index(e, "X1"), &x, &y), 0);
        QCOMPARE(x, 1000.0);
        QCOMPARE(y, 0.0);

        stack.redo(); // move again: J1, not X1
        QCOMPARE(swmm_spatial_get_node_coord(e, swmm_node_index(e, "J1"), &x, &y), 0);
        QCOMPARE(x, 5.0);
        QCOMPARE(swmm_spatial_get_node_coord(e, swmm_node_index(e, "X1"), &x, &y), 0);
        QCOMPARE(x, 1000.0);
    }

    /**
     * @brief Review E-04: undoing a gage delete, single or batch, puts back the model the delete
     * started from: the saved .inp is byte-identical, gages keep their order in the engine and the
     * layer cache, and subcatchments point at the same gages again. Redo then undo again too.
     * Input and saved files go to tests/output/delete_undo_2026-11-12/gage_delete_undo/.
     */
    void deleteUndoRestoresGagesExactly()
    {
        const QDir dir(QStringLiteral(SWMMVIS_DELETE_UNDO_OUTPUT_DIR "/gage_delete_undo"));
        QVERIFY(QDir().mkpath(dir.path()));
        {
            QFile inp(dir.filePath(QStringLiteral("input.inp")));
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Truncate));
            inp.write(kGagesInp);
            QFile rain(dir.filePath(QStringLiteral("rain.dat")));
            QVERIFY(rain.open(QIODevice::WriteOnly | QIODevice::Truncate));
        }
        auto layer = std::make_unique<SWMMModelLayer>(dir.filePath(QStringLiteral("input.inp")),
                                                      nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        SWMM_Engine e = layer->engine();

        const QByteArray original = saveInp(e, dir, QStringLiteral("1_original.inp"));
        QVERIFY(!original.isEmpty());
        const QStringList order{QStringLiteral("G1"), QStringLiteral("G2"), QStringLiteral("G3")};
        QCOMPARE(engineGages(e), order);
        QCOMPARE(layerGages(*layer), order);
        const auto expectReferences = [&] {
            QCOMPARE(engineGages(e), order);
            QCOMPARE(layerGages(*layer), order);
            // [SUBCATCHMENTS] falls back to a retained gage name when the index is -1,
            // so the indices are checked directly.
            QCOMPARE(subcatchGage(e, "S1"), 1);
            QCOMPARE(subcatchGage(e, "S2"), 2);
            QCOMPARE(subcatchGage(e, "S3"), 1);
        };

        QUndoStack stack;
        // One gage (Object Browser, gage assignment).
        stack.push(new DeleteObjectCommand(layer.get(), QStringLiteral("G2"),
                                           DeleteObjectCommand::DeleteGage, nullptr));
        QCOMPARE(swmm_gage_index(e, "G2"), -1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatRainGages), 2);
        stack.undo();
        expectReferences();
        QCOMPARE(saveInp(e, dir, QStringLiteral("2_single_undo.inp")), original);
        stack.redo();
        QCOMPARE(swmm_gage_index(e, "G2"), -1);
        stack.undo();
        expectReferences();
        QCOMPARE(saveInp(e, dir, QStringLiteral("3_single_redo_undo.inp")), original);

        // A batch (map Delete key, Attribute Table): the first and last gage.
        stack.push(new BatchDeleteCommand(
            layer.get(),
            {{QStringLiteral("G1"), DeleteObjectCommand::DeleteGage},
             {QStringLiteral("G3"), DeleteObjectCommand::DeleteGage}},
            nullptr, QStringLiteral("Delete gages")));
        QCOMPARE(engineGages(e), QStringList{QStringLiteral("G2")});
        QCOMPARE(layerGages(*layer), QStringList{QStringLiteral("G2")});
        stack.undo();
        expectReferences();
        QCOMPARE(saveInp(e, dir, QStringLiteral("4_batch_undo.inp")), original);
        stack.redo();
        stack.undo();
        expectReferences();
        QCOMPARE(saveInp(e, dir, QStringLiteral("5_batch_redo_undo.inp")), original);
    }
};

QTEST_MAIN(TestRainGageCoords)
#include "test_raingagecoords.moc"
