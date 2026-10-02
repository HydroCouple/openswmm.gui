/*!
 * \file   test_mesh2d_variable_series.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 *
 * Full-app link pattern (drives SWMM2DResultsLayer + ComparisonPlotDialog).
 * Pure assertions — writes no files.
 */
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

namespace {

Mesh2DResultVariable variable(const QString &dataset, const QString &label, const QString &units,
                              Mesh2DResultVariable::Domain domain,
                              Mesh2DResultVariable::Temporal temporal, int frames)
{
    Mesh2DResultVariable v;
    v.dataset = dataset; v.label = label; v.units = units; v.unitsKnown = !units.isEmpty();
    v.domain = domain; v.temporal = temporal; v.frameCount = frames;
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

    int vertexCount()   const override { return 4; }
    int triangleCount() const override { return 2; }
    int timeCount()     const override { return kFrames; }

    bool readMeshGeometry(std::vector<double>& vx, std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override
    {
        vx = {0.0, 1.0, 1.0, 0.0};
        vy = {0.0, 0.0, 1.0, 1.0};
        vz = {1.0, 1.0, 1.0, 1.0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    bool readDepthsAt(int t, std::vector<float>& depths) override
    {
        if (t < 0 || t >= kFrames) return false;
        depths = {0.0f, 0.0f};
        return true;
    }
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 60);
    }

    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings) const override
    {
        if (warnings) warnings->clear();
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
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        values.clear(); status.clear();
        if (t < 0 || t >= kFrames) return false;
        if (v.dataset == QStringLiteral("Mesh2_face_gw_table_elev")) {
            ++tableReads;
            values = {10.0f + float(t), 10.1f + float(t)};
            status = {Mesh2DValueStatus::Valid,
                      t == 2 ? Mesh2DValueStatus::Missing : Mesh2DValueStatus::Valid};
            if (t == 2) values[1] = std::numeric_limits<float>::quiet_NaN();
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

ResultDescriptor findByLabel(const QVector<ResultDescriptor> &list, const QString &label)
{
    for (const auto &d : list)
        if (d.isMeshVariable() && d.label() == label) return d;
    return {};
}

} // namespace

class TestMesh2DVariableSeries : public QObject
{
    Q_OBJECT
private slots:
    void cellDescriptorsListTimeVaryingCatalogVariables();
    void variableSeriesReadsEachFrameOnceAndKeepsGaps();
    void variableSeriesTailAndUnknownKey();
    void comparisonPlotGivesEachVariableItsOwnRow();
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
        if (d.isMeshVariable()) labels << d.label();
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
    run.getSeriesBatch({{ObjectRef::forMesh2DCell(0), table, 0},
                        {ObjectRef::forMesh2DCell(1), table, 0}}, out);
    QCOMPARE(src->tableReads, CatalogMeshSource::kFrames);   // once per frame, not per cell
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
    run.getSeriesAt(ObjectRef::forMesh2DCell(0),
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
    QCOMPARE(dlg.addCellDescriptorSeries(run, {0, 1},
                                         {ResultDescriptor::forAttribute(PlotAttribute::Mesh2DDepth),
                                          table, recharge}), 6);
    const auto &rows = dlg.model()->rows();
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[1].descriptor(), table);
    QCOMPARE(rows[1].descriptor().label(), QStringLiteral("Groundwater — water table elevation"));
    QCOMPARE(rows[1].unitsLabel, QStringLiteral("m"));
    QCOMPARE(rows[2].seriesIndices.size(), 2);

    const auto data = dlg.model()->resolveAllSeries();
    for (const auto &d : data) QVERIFY2(d.ok, qPrintable(d.errorMessage));
}

QTEST_MAIN(TestMesh2DVariableSeries)
#include "test_mesh2d_variable_series.moc"
