/**
 * @file      test_mesh2d_variable_series.cpp
 * @brief     Tests for mesh2d variable series.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * 2D face variables from the source's own catalog (groundwater terms,
 * infiltration, 2D species) plot as cell time series
 * (workplans/PLOTTABLE_RESULT_SERIES_PLAN_2026-10-02.md, Phase C):
 *   - Mesh2DRunLayer lists every time-varying catalog variable for a cell,
 *     with the file's label and units, and skips static/envelope fields and
 *     datasets a fixed attribute already plots.
 *   - A variable series reads each frame once however many cells are asked,
 *     turns non-Valid cells into gaps (never zero), and honours live tails.
 *   - An unknown key is refused with a message.
 *   - The comparison plot gives each variable its own chart row, titled by
 *     the variable's label.
 *   - A SAVED run (HDF5 source) also serves its aquifer ledger terms on the
 *     System ref and each enrolled node's aquifer exchange on a Node ref
 *     (GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN V3/V4); a live
 *     source offers neither.
 *
 * Full-app link pattern (drives SWMM2DResultsLayer + ComparisonPlotDialog).
 * Only the saved-run case writes a file (GW_RESULTS_TEST_OUTPUT).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/swmmdatetime.h"
#include "gwresults_fixture.h"
#include "layers/swmm2dresultslayer.h"
#include "plot/comparisonplotmodel.h"
#include "plot/mesh2drunlayer.h"
#include "ui/dialogs/comparisonplotdialog.h"

#include <QDateTime>
#include <QObject>
#include <QTest>
#include <QTimeZone>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace openswmmvis::plot;
using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::io::Mesh2DValueStatus;
using openswmmvis::ui::ComparisonPlotDialog;

namespace
{

/**
 * @brief Construct a catalog variable descriptor for the in-memory result-source fixture.
 * @param[in] dataset Dataset identity copied to result, not opened.
 * @param[in] label User-facing label copied as supplied.
 * @param[in] units Native unit text; nonempty sets unitsKnown, no dimensional validation.
 * @param[in] domain Domain enum copied verbatim.
 * @param[in] temporal Temporal sampling enum copied verbatim.
 * @param[in] frames Reported frame count, copied without range checks; static/envelope fixtures
 * use zero.
 * @return Owned descriptor with those fields/default remainder; no file access or external
 * state.
 */
Mesh2DResultVariable variable(const QString &dataset, const QString &label, const QString &units,
                              Mesh2DResultVariable::Domain domain,
                              Mesh2DResultVariable::Temporal temporal, int frames)
{
    Mesh2DResultVariable v;
    v.dataset = dataset;
    v.label = label;
    v.units = units;
    v.unitsKnown = !units.isEmpty();
    v.domain = domain;
    v.temporal = temporal;
    v.frameCount = frames;
    return v;
}

// Two triangles, four frames. The catalog carries a reported groundwater
// table, a held recharge rate, a static base (excluded), a depth dataset a
// fixed attribute covers (excluded) and a time-maximum envelope (excluded).
class CatalogMeshSource : public IMesh2DSource
{
public:
    static constexpr int kFrames = 4;
    int tableReads = 0;

    /**
     * @brief Report four fixed fixture vertices.
     * @return 4, no allocation or source mutation.
     */
    int vertexCount() const override { return 4; }
    /**
     * @brief Report two fixed triangular fixture cells.
     * @return 2, no allocation or source mutation.
     */
    int triangleCount() const override { return 2; }
    /**
     * @brief Report the fixture's four one-minute result frames.
     * @return kFrames(4), no live tail/state mutation.
     */
    int timeCount() const override { return kFrames; }

    /**
     * @brief Copy the unit-square fixture coordinates and two triangle connectivities.
     * @param[out] vx Borrowed vector replaced with four xcoordinates 0/1/1/0.
     * @param[out] vy Borrowed vector replaced with four ycoordinates 0/0/1/1.
     * @param[out] vz Borrowed vector replaced with four elevations 1 in fixture metres.
     * @param[out] tris Borrowed vector replaced with two zero-based triples(0,1,2)/(0,2,3).
     * @return Always true after assignment; allocations may throw with partial vector updates.
     * Outputs own copied data, source is unchanged; no file handles or model ownership.
     */
    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override
    {
        vx = {0.0, 1.0, 1.0, 0.0};
        vy = {0.0, 0.0, 1.0, 1.0};
        vz = {1.0, 1.0, 1.0, 1.0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    /**
     * @brief Return two dry zero depths for an available fixture frame.
     * @param[in] t Zero-based frame index in[0,4); out-of-range refuses.
     * @param[out] depths Borrowed vector replaced with two zero metre values on success,
     * unchanged on refusal.
     * @return True for valid t, false otherwise; no source mutation/IO, vector allocation may
     * throw.
     */
    bool readDepthsAt(int t, std::vector<float> &depths) override
    {
        if (t < 0 || t >= kFrames)
            return false;
        depths = {0.0f, 0.0f};
        return true;
    }
    /**
     * @brief Compute the synthetic UTC frame timestamp without checking frame bounds.
     * @param[in] t Frame offset in one-minute increments from 2026-01-01 T 00:00 Z; any int is
     * extrapolated.
     * @return UTC QDateTime base+60*tseconds; source metadata/state are unchanged.
     */
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 60);
    }

    /**
     * @brief Return five descriptors covering reported/held groundwater and excluded
     * static/fixed/envelope fields.
     * @param[out] warnings Optional borrowed list cleared, no warnings generated.
     * @return Owned descriptors:
     * table(m),recharge(m/s),staticbase(m),reporteddepth(m),maxdepth(m); reported/held use
     * fourframes. No file access, state/ownership retention or validation.
     */
    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings) const override
    {
        if (warnings)
            warnings->clear();
        using D = Mesh2DResultVariable::Domain;
        using T = Mesh2DResultVariable::Temporal;
        return {
            variable(QStringLiteral("Mesh2_face_gw_table_elev"),
                     QStringLiteral("Groundwater — water table elevation"), QStringLiteral("m"),
                     D::Groundwater, T::Reported, kFrames),
            variable(QStringLiteral("Mesh2_face_gw_recharge"),
                     QStringLiteral("Groundwater — recharge"), QStringLiteral("m/s"),
                     D::Groundwater, T::Held, kFrames),
            variable(QStringLiteral("Mesh2_face_gw_bed_elev"),
                     QStringLiteral("Groundwater — aquifer base elevation"), QStringLiteral("m"),
                     D::Groundwater, T::Static, 0),
            variable(QStringLiteral("Mesh2_face_depth"), QStringLiteral("Surface — depth"),
                     QStringLiteral("m"), D::Surface, T::Reported, kFrames),
            variable(QStringLiteral("Mesh2_face_max_depth"), QStringLiteral("Surface — max depth"),
                     QStringLiteral("m"), D::Surface, T::Envelope, 0),
        };
    }

    // Table: cell c at frame t = 10 + t + c/10; cell 1 is Missing at frame 2.
    // Recharge: 1e-6 * (t + 1) everywhere.
    /**
     * @brief Populate groundwater fixture values/status for one requested dataset/frame.
     * @param[in] v Stable descriptor; only dataset exact identity is inspected.
     * @param[in] t Zero-based frame[0,4), otherwise false.
     * @param[out] values Borrowed vector cleared first; two values assigned for supported
     * dataset.
     * @param[out] status Borrowed vector cleared first; two corresponding statuses assigned on
     * success.
     * @return True for table/recharge at valid frame, false with cleared outputs for invalid
     * frame/key. Table values 10+t and 10.1+tmetres, cell 1 Missing/NaN at frame 2; recharge
     * 1e-6*(t+1)m/sValid. Table reads increment tableReads once per successful table call;
     * mutable fixture is not concurrent-safe. Allocations may throw, no IO or retained output
     * references.
     */
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        values.clear();
        status.clear();
        if (t < 0 || t >= kFrames)
            return false;
        if (v.dataset == QStringLiteral("Mesh2_face_gw_table_elev")) {
            ++tableReads;
            values = {10.0f + float(t), 10.1f + float(t)};
            status = {Mesh2DValueStatus::Valid,
                      t == 2 ? Mesh2DValueStatus::Missing : Mesh2DValueStatus::Valid};
            if (t == 2)
                values[1] = std::numeric_limits<float>::quiet_NaN();
            return true;
        }
        if (v.dataset == QStringLiteral("Mesh2_face_gw_recharge")) {
            values = {1.0e-6f * float(t + 1), 1.0e-6f * float(t + 1)};
            status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid};
            return true;
        }
        return false;
    }
};

// The same frames 100 ms apart: a model that reports faster than once a second.
class SubSecondMeshSource : public CatalogMeshSource
{
public:
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addMSecs(qint64(t) * 100);
    }
};

/**
 * @brief Find the first mesh-variable descriptor with an exact user-label match.
 * @param[in] list Stable descriptor values, preserved.
 * @param[in] label Exact case-sensitive desired label, preserved.
 * @return Owning descriptor copy on match, default invalid value if absent; fixed attributes
 * skipped. No external catalog query, ownership transfer or mutation.
 */
ResultDescriptor findByLabel(const QVector<ResultDescriptor> &list, const QString &label)
{
    for (const auto &d : list)
        if (d.isMeshVariable() && d.label() == label)
            return d;
    return {};
}

} // namespace

class TestMesh2DVariableSeries : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Verify per-cell catalog descriptors retain native units and skip
     * static/envelope/fixed-attribute duplicates.
     * @note Fixed depth stays first, only table/recharge dynamic variables listed, table key has
     * groundwater prefix/munits; vertex descriptors acquire none. Layer owns synthetic source,
     * no files.
     */
    void cellDescriptorsListTimeVaryingCatalogVariables();
    /**
     * @brief Verify batched cell series share frame reads and preserve missing values as timed
     * NaN gaps.
     * @note Two cells/fourframes cause exactly fourtable reads, values within 1e-5,cell 1 frame
     * 2 NaN, frame 3=13.1; single-series values agree with batch. Owned source remains alive via
     * layer.
     */
    void variableSeriesReadsEachFrameOnceAndKeepsGaps();
    /**
     * @brief Verify tail offsets, unknown-variable diagnostics and per-cell-only variable
     * restrictions.
     * @note firstPeriod 2 returns two recharge values beginning 3e-6 within 1e-12; unknown Nope
     * key gives failed series naming label, vertex request refuses. Synthetic source/read-only
     * plotting only.
     */
    void variableSeriesTailAndUnknownKey();
    /**
     * @brief Verify two cells across fixed depth and two catalog variables produce six series in
     * three chart rows.
     * @note Groundwater table row keeps label/metres and recharge row two series; every resolved
     * series succeeds. GUI dialog/model/layer lifetimes nested, no file or engine simulation.
     */
    void comparisonPlotGivesEachVariableItsOwnRow();
    /**
     * @brief Verify a saved run serves ledger terms (System) and node exchange (Node), keeps
     * the exchange sign, refuses an unknown node, is not deferred by the comparison plot, and
     * that a live source offers neither.
     * @note Writes runlayer_saved_gw.2d.h5 (two cells, three reports) to GW_RESULTS_TEST_OUTPUT.
     */
    void savedRunServesGroundwaterLedgerAndNodeExchange();
    /** @brief Frames under a second apart keep their own times in fixed-attribute and
     * catalog-variable series (they were rounded to whole seconds and stacked). */
    void subSecondFramesKeepTheirMilliseconds();
};

void TestMesh2DVariableSeries::cellDescriptorsListTimeVaryingCatalogVariables()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<CatalogMeshSource>());
    Mesh2DRunLayer run(&layer);

    const auto cell = run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell);
    // The fixed cell attributes stay first and unchanged.
    QCOMPARE(cell.first(), ResultDescriptor::forAttribute(PlotAttribute::Mesh2DDepth));

    QStringList labels;
    for (const auto &d : cell)
        if (d.isMeshVariable())
            labels << d.label();
    QCOMPARE(labels, (QStringList{QStringLiteral("Groundwater — water table elevation"),
                                  QStringLiteral("Groundwater — recharge")}));

    const auto table = findByLabel(cell, QStringLiteral("Groundwater — water table elevation"));
    QCOMPARE(table.unitLabel(UnitSystem::SI), QStringLiteral("m"));
    QVERIFY(table.variableKey.startsWith(QStringLiteral("groundwater:")));

    // Other kinds are untouched.
    for (const auto &d : run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DVertex))
        QVERIFY(!d.isMeshVariable());
}

void TestMesh2DVariableSeries::variableSeriesReadsEachFrameOnceAndKeepsGaps()
{
    auto owned = std::make_unique<CatalogMeshSource>();
    auto *src = owned.get();
    SWMM2DResultsLayer layer;
    layer.setSource(std::move(owned));
    Mesh2DRunLayer run(&layer);
    const auto table = findByLabel(run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell),
                                   QStringLiteral("Groundwater — water table elevation"));
    QVERIFY(table.isValid());

    QVector<SeriesData> out;
    run.getSeriesBatch(
        {{ObjectRef::forMesh2DCell(0), table, 0}, {ObjectRef::forMesh2DCell(1), table, 0}}, out);
    QCOMPARE(src->tableReads, CatalogMeshSource::kFrames); // once per frame, not per cell
    QVERIFY2(out[0].ok, qPrintable(out[0].errorMessage));
    QVERIFY2(out[1].ok, qPrintable(out[1].errorMessage));
    QCOMPARE(int(out[0].values.size()), CatalogMeshSource::kFrames);
    for (int t = 0; t < CatalogMeshSource::kFrames; ++t)
        QVERIFY(std::fabs(out[0].values[t] - (10.0 + t)) < 1e-5);
    // The Missing cell is a gap, not zero, and the sample keeps its time.
    QCOMPARE(int(out[1].values.size()), CatalogMeshSource::kFrames);
    QVERIFY(std::isnan(out[1].values[2]));
    QVERIFY(std::fabs(out[1].values[3] - 13.1) < 1e-5);

    // The single-series descriptor entry point agrees with the batch.
    SeriesData single;
    run.getSeriesAt(ObjectRef::forMesh2DCell(0), table, single);
    QVERIFY(single.ok);
    QCOMPARE(single.values, out[0].values);
}

void TestMesh2DVariableSeries::variableSeriesTailAndUnknownKey()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<CatalogMeshSource>());
    Mesh2DRunLayer run(&layer);
    const auto recharge = findByLabel(run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell),
                                      QStringLiteral("Groundwater — recharge"));
    QVERIFY(recharge.isValid());

    SeriesData tail;
    tail.firstPeriod = 2;
    run.getSeriesAt(ObjectRef::forMesh2DCell(1), recharge, tail);
    QVERIFY(tail.ok);
    QCOMPARE(int(tail.values.size()), 2);
    QVERIFY(std::fabs(tail.values[0] - 3.0e-6) < 1e-12);

    SeriesData missing;
    run.getSeriesAt(
        ObjectRef::forMesh2DCell(0),
        ResultDescriptor::forMeshVariable(QStringLiteral("groundwater:Mesh2_face_gw_nope"),
                                          QStringLiteral("Nope"), QString()),
        missing);
    QVERIFY(!missing.ok);
    QVERIFY(missing.errorMessage.contains(QStringLiteral("Nope")));

    // Catalog variables are per cell.
    SeriesData vertex;
    run.getSeriesAt(ObjectRef::forMesh2DVertex(0), recharge, vertex);
    QVERIFY(!vertex.ok);
}

void TestMesh2DVariableSeries::comparisonPlotGivesEachVariableItsOwnRow()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<CatalogMeshSource>());
    Mesh2DRunLayer probe(&layer);
    const auto cell = probe.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell);
    const auto table = findByLabel(cell, QStringLiteral("Groundwater — water table elevation"));
    const auto recharge = findByLabel(cell, QStringLiteral("Groundwater — recharge"));

    ComparisonPlotDialog dlg;
    RunSource rs;
    rs.layer = std::make_shared<Mesh2DRunLayer>(&layer);
    const int run = dlg.model()->addRunSource(std::move(rs));
    QCOMPARE(dlg.addCellDescriptorSeries(
                 run, {0, 1},
                 {ResultDescriptor::forAttribute(PlotAttribute::Mesh2DDepth), table, recharge}),
             6);
    const auto &rows = dlg.model()->rows();
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[1].descriptor(), table);
    QCOMPARE(rows[1].descriptor().label(), QStringLiteral("Groundwater — water table elevation"));
    QCOMPARE(rows[1].unitsLabel, QStringLiteral("m"));
    QCOMPARE(rows[2].seriesIndices.size(), 2);

    const auto data = dlg.model()->resolveAllSeries();
    for (const auto &d : data)
        QVERIFY2(d.ok, qPrintable(d.errorMessage));
}

void TestMesh2DVariableSeries::savedRunServesGroundwaterLedgerAndNodeExchange()
{
    // Two triangles, three five-minute reports, a two-term water ledger and
    // two node beds (N1 out of the aquifer, N2 into it).
    const std::vector<double> days = {41089.0, 41089.0 + 300.0 / 86400.0,
                                      41089.0 + 600.0 / 86400.0};
    const hid_t f = gwfixture::create(QStringLiteral("runlayer_saved_gw.2d.h5"));
    QVERIFY(f >= 0);
    gwfixture::doubles(f, "Mesh2_node_x", {4}, {0.0, 1.0, 1.0, 0.0});
    gwfixture::doubles(f, "Mesh2_node_y", {4}, {0.0, 0.0, 1.0, 1.0});
    gwfixture::doubles(f, "Mesh2_node_z", {4}, {1.0, 1.0, 1.0, 1.0});
    {
        const int conn[6] = {0, 1, 2, 0, 2, 3};
        const hsize_t dims[2] = {2, 3};
        const hid_t space = H5Screate_simple(2, dims, nullptr);
        const hid_t data = H5Dcreate2(f, "Mesh2_face_nodes", H5T_NATIVE_INT, space, H5P_DEFAULT,
                                      H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(data, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn);
        H5Dclose(data);
        H5Sclose(space);
    }
    gwfixture::times(f, days);
    gwfixture::doubles(f, "Mesh2_face_depth", {3, 2}, std::vector<double>(6, 0.0));
    gwfixture::doubles(f, "groundwater_ledger", {3, 2}, {0.0, 100.0, 2.0, 101.0, 5.0, 103.0});
    gwfixture::stringAttr(f, "groundwater_ledger", "terms", QStringLiteral("node,storage"));
    gwfixture::stringAttr(f, "groundwater_ledger", "units", QStringLiteral("m3"));
    gwfixture::doubles(f, "groundwater_node_exchange_cum", {3, 2},
                       {0.0, 0.0, 3.0, -1.5, 9.0, -3.0});
    gwfixture::stringAttr(f, "groundwater_node_exchange_cum", "node_names",
                          QStringLiteral("N1,N2"));
    gwfixture::stringAttr(f, "groundwater_node_exchange_cum", "units", QStringLiteral("m3"));
    H5Fclose(f);

    auto source = std::make_unique<HDF5Mesh2DSource>();
    QVERIFY(source->open(gwfixture::path(QStringLiteral("runlayer_saved_gw.2d.h5"))));
    SWMM2DResultsLayer layer;
    layer.setSource(std::move(source));
    Mesh2DRunLayer run(&layer);

    ResultDescriptor storage, rate;
    for (const auto &d : run.resultDescriptorsForKind(ObjectRef::Kind::System))
        if (d.variableKey == QStringLiteral("gwledger:water:storage"))
            storage = d;
    for (const auto &d : run.resultDescriptorsForKind(ObjectRef::Kind::Node))
        if (d.variableKey == QStringLiteral("gw_node_exchange"))
            rate = d;
    QVERIFY(storage.isMeshVariable());
    QVERIFY(rate.isMeshVariable());
    QCOMPARE(storage.unitLabel(UnitSystem::SI), QStringLiteral("m3"));
    QCOMPARE(rate.unitLabel(UnitSystem::SI), QStringLiteral("m3/s"));

    QVector<SeriesData> out;
    run.getSeriesBatch({{ObjectRef::forSystem(), storage, 0},
                        {ObjectRef::forNode(QStringLiteral("N2")), rate, 0},
                        {ObjectRef::forNode(QStringLiteral("J9")), rate, 0}},
                       out);
    QVERIFY2(out[0].ok, qPrintable(out[0].errorMessage));
    QCOMPARE(out[0].values, (std::vector<double>{100.0, 101.0, 103.0}));
    QCOMPARE(out[0].timesJulian, days);
    QVERIFY2(out[1].ok, qPrintable(out[1].errorMessage));
    QCOMPARE(int(out[1].values.size()), 2);
    for (double v : out[1].values) // into the aquifer: negative, as stored
        QVERIFY(std::fabs(v - (-1.5 / 300.0)) < 1e-9);
    QVERIFY(!out[2].ok); // no bed: missing, never zero
    QVERIFY(out[2].values.empty());
    QVERIFY(out[2].errorMessage.contains(QStringLiteral("missing")));

    // Through the comparison plot: the run's saved-file deferral does not
    // apply to these small series, and each gets its own row.
    ComparisonPlotDialog dlg;
    RunSource rs;
    rs.layer = std::make_shared<Mesh2DRunLayer>(&layer);
    const int runIdx = dlg.model()->addRunSource(std::move(rs));
    QVERIFY(dlg.addSeries(runIdx, ObjectRef::forSystem(), storage) >= 0);
    QVERIFY(dlg.addSeries(runIdx, ObjectRef::forNode(QStringLiteral("N1")), rate) >= 0);
    QCOMPARE(dlg.model()->rows().size(), 2);
    for (const auto &d : dlg.model()->resolveAllSeries())
        QVERIFY2(d.ok, qPrintable(d.errorMessage));

    // A live (in-memory) source has no complete file yet: neither is offered.
    SWMM2DResultsLayer live;
    live.setSource(std::make_unique<CatalogMeshSource>());
    Mesh2DRunLayer liveRun(&live);
    for (const auto kind : {ObjectRef::Kind::System, ObjectRef::Kind::Node})
        for (const auto &d : liveRun.resultDescriptorsForKind(kind))
            QVERIFY(!d.isMeshVariable());
}

void TestMesh2DVariableSeries::subSecondFramesKeepTheirMilliseconds()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<SubSecondMeshSource>());
    Mesh2DRunLayer run(&layer);
    const auto descriptors = run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell);
    QVERIFY(!descriptors.isEmpty());
    const auto table =
        findByLabel(descriptors, QStringLiteral("Groundwater — water table elevation"));
    QVERIFY(table.isValid());
    for (const auto &descriptor : {descriptors.first(), table}) {
        SeriesData series;
        run.getSeriesAt(ObjectRef::forMesh2DCell(0), descriptor, series);
        QVERIFY2(series.ok, qPrintable(series.errorMessage));
        QCOMPARE(int(series.timesJulian.size()), CatalogMeshSource::kFrames);
        for (int t = 0; t < CatalogMeshSource::kFrames; ++t)
            QCOMPARE(openswmmvis::core::swmmDateTimeToMilliseconds(series.timesJulian[size_t(t)]),
                     layer.source()->simTimeAt(t));
    }
}

QTEST_MAIN(TestMesh2DVariableSeries)
#include "test_mesh2d_variable_series.moc"
