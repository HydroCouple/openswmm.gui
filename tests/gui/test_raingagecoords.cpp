/*!
 * \file   test_raingagecoords.cpp
 * \brief  Rain-gage map position ([SYMBOLS] X / Y) is readable AND editable
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
 */

#include "layers/swmmmodellayer.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using openswmmvis::ColumnSpec;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &file =
                                              QStringLiteral("typed_selection_fixture.inp"))
{
    auto layer = std::make_unique<SWMMModelLayer>(fixture(file), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

int colForSetter(const QList<ColumnSpec> &specs, const QString &tag)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].setter == tag) return i;
    return -1;
}

constexpr double kNewX = 1234.5;
constexpr double kNewY = -6789.25;

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
        QCOMPARE(adapter.displayLabelFor(QStringLiteral("xCoord")),
                 QStringLiteral("X Coordinate"));
        QCOMPARE(adapter.displayLabelFor(QStringLiteral("yCoord")),
                 QStringLiteral("Y Coordinate"));

        // Both properties are on the meta-object, so QPropertyModel shows them.
        QVERIFY(adapter.metaObject()->indexOfProperty("xCoord") >= 0);
        QVERIFY(adapter.metaObject()->indexOfProperty("yCoord") >= 0);

        // The setter must NOT write the engine itself — it asks the panel to,
        // carrying both coordinates.
        QSignalSpy spy(&adapter, &SWMMRainGagePropertyAdapter::coordChangeRequested);
        adapter.setXCoord(kNewX);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), kNewX);
        QCOMPARE(spy.at(0).at(1).toDouble(), -2000.0);   // Y unchanged
        double x = 0.0, y = 0.0;
        swmm_spatial_get_gage_coord(e, gi, &x, &y);
        QCOMPARE(x, -2000.0);   // still untouched: the panel applies it

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
        const QVariantMap m = layer->identifyByName(
            QStringLiteral("S1"), SWMMModelLayer::kKindGage);
        QCOMPARE(m.value(QStringLiteral("X")).toDouble(), kNewX);
        QCOMPARE(m.value(QStringLiteral("Y")).toDouble(), kNewY);

        // The collision itself: unscoped identify still yields the
        // subcatchment (legacy precedence), which carries no "X".
        QVERIFY(!layer->identifyByName(QStringLiteral("S1"))
                     .contains(QStringLiteral("X")));

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
};

QTEST_MAIN(TestRainGageCoords)
#include "test_raingagecoords.moc"
