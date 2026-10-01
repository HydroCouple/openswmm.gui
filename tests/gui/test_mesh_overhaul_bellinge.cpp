// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh overhaul Phase 7 (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md §6 /
// MESH_OVERHAUL_PHASE7_HANDOFF_2026-09-30.md step 3), on the triangle engine
// (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §6): the Bellinge
// acceptance harness. Runs A–F of the handoff table against the shipped
// Bellinge domain + SRTM DEM, measures the acceptance metrics and writes
// metrics.csv / report.md / mesh_<run>.2dm under
// tests/output/mesh_overhaul_2026-09/bellinge/.
//
// Deliberately gated on SWMMVIS_MESH_OVERHAUL_BELLINGE=1 (QSKIP otherwise)
// so routine CI stays fast; a full pass takes minutes (run C is generated
// four times for the thread-count determinism gate).
#include "ui/dialogs/meshgenerationdialog.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/sizefield.h"
#include "mesh/terrainsizefield.h"
#include "mesh/terrainerrorfield.h"
#include "mesh/pslgprep.h"
#include "mesh/dtmraster.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPromise>
#include <QTest>
#include <QThreadPool>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <sys/resource.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr double kSqrt3_4 = 0.4330127018922193;   // area of unit equilateral

struct RunSpec
{
    const char *id;
    double      cellSize;
    double      terrainTolerance;
    double      minAngleDeg;     ///< Quality tab "Minimum angle" (20–33).
    bool        conduits;        ///< Conduits as constraint lines.
    double      stripWidth;      ///< Conduit quad strip width; 0 = plain edges.
    const char *purpose;
};

// Handoff step-3 table (revision 3): ratio 1.5, coarsen 20 (the product
// default), min cell = cell/4, trim 5° / 0.1·cell for every run. Terrain
// tolerance 3 m: SRTM is quantised to whole metres, so anything under ~3 m
// traces noise (handoff revision 2, 6b plan §7).
const RunSpec kRuns[] = {
    {"A", 10.0, 0.0, 30.0, false, 0.0, "grading & quality gates"},
    {"B", 10.0, 3.0, 30.0, false, 0.0, "terrain term + DEM break lines"},
    {"C", 3.0,  3.0, 30.0, false, 0.0, "timing/memory/determinism gate"},
    {"D", 10.0, 3.0, 33.0, false, 0.0, "highest minimum angle"},
    {"E", 10.0, 3.0, 30.0, true,  0.0, "conduits as lines (crossing pipes joined)"},
    {"F", 10.0, 3.0, 30.0, true,  4.0, "conduit quad strips 4 m wide"},
};

long peakRssBytes()
{
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    return long(ru.ru_maxrss);              // bytes on macOS
}

double meanEdge(const mesh::MeshResult &m, const mesh::MeshTriangle &c)
{
    // The size h promises: the cell's mean edge length (the engine sizes
    // triangles so the mean edge is h, MESH_TRIANGLE_ENGINE_PLAN §4).
    double sum = 0.0;
    const int n = c.vertexCount();
    for (int k = 0; k < n; ++k)
        sum += QLineF(m.vertices[c.vertex(k)].xy, m.vertices[c.vertex((k + 1) % n)].xy).length();
    return sum / n;
}

double triangleMinAngleDeg(const mesh::MeshResult &m, const mesh::MeshTriangle &c)
{
    double worst = 180.0;
    for (int k = 0; k < 3; ++k) {
        const QPointF p = m.vertices[c.vertex(k)].xy;
        const QPointF u = m.vertices[c.vertex((k + 1) % 3)].xy - p;
        const QPointF v = m.vertices[c.vertex((k + 2) % 3)].xy - p;
        const double d = std::hypot(u.x(), u.y()) * std::hypot(v.x(), v.y());
        if (!(d > 0.0)) return 0.0;
        const double cosA = std::clamp(QPointF::dotProduct(u, v) / d, -1.0, 1.0);
        worst = std::min(worst, std::acos(cosA) * 180.0 / M_PI);
    }
    return worst;
}

} // namespace

class TestMeshOverhaulBellinge : public QObject
{
    Q_OBJECT
    using Inputs = MeshGenerationDialog::PipelineInputs;
    using Result = MeshGenerationDialog::PipelineResult;

    QString m_outDir;
    QString m_repoDir;
    QString m_demPath;
    QString m_inpPath;
    QString m_domainPath;
    QVector<QPointF> m_domainRing;      // outer ring, EPSG:25832
    QPolygonF        m_domainPoly;
    std::unique_ptr<OpenSWMMVisWorkspace> m_workspace;
    std::unique_ptr<SWMMVisProjectWindow> m_window;
    Inputs      m_base;
    QVector<QPair<QString, QVector<QPointF>>> m_links;   // conduits, for runs E/F
    QStringList m_csvRows;
    QStringList m_reportSections;
    QStringList m_gateFailures;         // "<run>: <gate>: <numbers>"

    static Result run(Inputs inputs)
    {
        QPromise<Result> promise;
        auto future = promise.future();
        promise.start();
        MeshGenerationDialog::runMeshPipeline(promise, std::move(inputs));
        promise.finish();
        return future.resultCount() ? future.result() : Result{};
    }

    Inputs makeInputs(const RunSpec &spec) const
    {
        Inputs in = m_base;
        in.cellSize         = spec.cellSize;
        in.coarsenFactor    = 20.0;
        in.sizeRatio        = 1.5;
        in.minCellSize      = spec.cellSize / 4.0;
        in.terrainTolerance = spec.terrainTolerance;
        in.terrainAutoTolerance = false;
        in.terrainAdaptive = false;
        in.refineAtFeatures = true;
        in.genOpts.prioritizeQuality = false;
        in.trimTurnDeg      = 5.0;
        in.trimDeviation    = 0.1 * spec.cellSize;
        in.dtmPath          = m_demPath;
        in.genOpts.maxArea       = kSqrt3_4 * spec.cellSize * spec.cellSize;
        in.genOpts.minCellSize   = spec.cellSize / 4.0;
        in.genOpts.minAngleDeg   = spec.minAngleDeg;
        in.includeConduits       = spec.conduits;
        in.candidateLinks        = spec.conduits ? m_links
                                                 : QVector<QPair<QString, QVector<QPointF>>>{};
        in.conduitStripWidth     = spec.conduits ? spec.stripWidth : 0.0;
        in.boundaryKind      = Inputs::BoundaryKind::VectorFile;
        in.boundaryPath      = m_domainPath;
        in.boundaryLayerName.clear();
        in.boundaryCRSWkt.clear();          // GeoJSON coords are mesh CRS
        in.outputMode        = mesh::MeshOutputMode::Inline;
        in.meshOutputPath.clear();
        // Node mapping ON: nodes the worker demotes from vertex pinning
        // (min-separation rule) keep their coupling identity through the
        // coupling map, which is what the coupling gate audits.
        in.mapNodesAfterGen  = true;
        return in;
    }

    //! Rebuild the worker's size field from the same options (handoff gate
    //! "cells within [0.7h,1.4h]") — seeds are the model constraints
    //! (node points, conduit polylines when the run has them); the outer
    //! domain ring is deliberately not a seed, mirroring runMeshPipeline.
    //! Not mirrored: the worker also ignores the terrain term inside the
    //! cones of DEM break lines it keeps as edges (plan D13), so near those
    //! the reference h is smaller than the worker's.
    bool buildReferenceField(const Inputs &in, mesh::SizeField &field,
                             mesh::TerrainSizeField &terrain,
                             std::function<double(double, double)> &terrainAt) const
    {
        mesh::SizeFieldOptions opt;
        opt.nearSize  = in.cellSize;
        opt.gradation = in.sizeRatio - 1.0;
        opt.areaFloor = kSqrt3_4 * in.minCellSize * in.minCellSize;
        opt.maxSize   = in.cellSize * in.coarsenFactor;

        OGRCoordinateTransformation *meshToDem = nullptr;
        double unitScale = 1.0;
        if (in.terrainTolerance > 0.0) {
            GDALAllRegister();
            auto *ds = static_cast<GDALDataset *>(
                GDALOpen(m_demPath.toUtf8().constData(), GA_ReadOnly));
            if (!ds) return false;
            OGRSpatialReference demSrs, meshSrs;
            demSrs.importFromWkt(ds->GetProjectionRef());
            meshSrs.importFromWkt(m_base.meshCRSWkt.toUtf8().constData());
            demSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            meshSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            meshToDem = OGRCreateCoordinateTransformation(&meshSrs, &demSrs);
            double gt[6] = {0, 1, 0, 0, 0, 1};
            ds->GetGeoTransform(gt);
            const double px = (std::abs(gt[1]) + std::abs(gt[5])) * 0.5;
            GDALClose(ds);
            if (!meshToDem) return false;

            const QRectF bb = m_domainPoly.boundingRect();
            double xs[4] = {bb.left(), bb.right(), bb.left(), bb.right()};
            double ys[4] = {bb.top(), bb.top(), bb.bottom(), bb.bottom()};
            if (!meshToDem->Transform(4, xs, ys)) return false;
            const double dx0 = *std::min_element(xs, xs + 4);
            const double dx1 = *std::max_element(xs, xs + 4);
            const double dy0 = *std::min_element(ys, ys + 4);
            const double dy1 = *std::max_element(ys, ys + 4);

            // Local scale at the domain centre, as the worker computes it.
            auto *demToMesh = OGRCreateCoordinateTransformation(
                &demSrs, &meshSrs);
            if (demToMesh) {
                double sx[2] = {(dx0 + dx1) / 2, (dx0 + dx1) / 2 + px};
                double sy[2] = {(dy0 + dy1) / 2, (dy0 + dy1) / 2};
                if (demToMesh->Transform(2, sx, sy) && px > 0.0) {
                    const double d = std::hypot(sx[1] - sx[0], sy[1] - sy[0]);
                    if (std::isfinite(d) && d > 0.0) unitScale = d / px;
                }
                OGRCoordinateTransformation::DestroyCT(demToMesh);
            }

            mesh::TerrainSizeOptions tso;
            tso.tolerance = in.terrainTolerance;
            const double pitchDem = unitScale > 0.0
                ? 0.5 * opt.nearSize / unitScale : 0.0;
            tso.outLevel = (pitchDem > 0.0 && px > 0.0)
                ? qBound(1, int(std::floor(std::log2(pitchDem / px))), 6) : 1;
            if (!terrain.buildFromFile(m_demPath, 1, dx0, dy0, dx1, dy1, tso,
                                       {}))
                return false;
            terrainAt = [&terrain, meshToDem, unitScale](double x, double y) {
                double gx = x, gy = y;
                if (meshToDem && !meshToDem->Transform(1, &gx, &gy)) return 0.0;
                return terrain.sizeAtGeo(gx, gy) * unitScale;
            };
            opt.terrainSizeAt = terrainAt;
        }

        QVector<mesh::SteinerPoint> pts;
        for (const auto &node : m_base.candidateNodes)
            pts.append({node.xy, 1, {}, 0.0, false});
        QVector<mesh::ConstraintSegment> segs;
        if (in.includeConduits)
            for (const auto &link : in.candidateLinks) {
                mesh::ConstraintSegment cs;
                cs.path = link.second;
                segs.append(cs);
            }
        return field.build(m_domainPoly.boundingRect(), segs, {}, pts, opt);
    }

private slots:
    void initTestCase();
    void acceptance();
    void adaptiveComparison();
    void adaptiveWithModelConstraints();
    void cleanupTestCase();
};

void TestMeshOverhaulBellinge::initTestCase()
{
    if (qEnvironmentVariable("SWMMVIS_MESH_OVERHAUL_BELLINGE") != "1")
        QSKIP("Set SWMMVIS_MESH_OVERHAUL_BELLINGE=1 to run the Bellinge "
              "acceptance harness (minutes; ~1M-cell runs).");

    m_repoDir = QStringLiteral(SWMMVIS_MESH_OVERHAUL_REPO_DIR);
    m_outDir  = qEnvironmentVariable("SWMMVIS_BELLINGE_OUTPUT",QStringLiteral(SWMMVIS_MESH_OVERHAUL_OUT_DIR));
    QVERIFY(QDir().mkpath(m_outDir));
    m_inpPath    = m_repoDir + "/examples/bellinge_2d/BellingeSWMM_v021_nopervious.inp";
    m_demPath    = m_repoDir + "/examples/bellinge_2d/output_SRTMGL1.tif";
    const QString selected=qEnvironmentVariable("SWMMVIS_BELLINGE_INPUT");
    if (!selected.isEmpty()) {
        // All staged output belongs to the review directory, never the user's
        // reference model. The DEM remains a read-only absolute input.
        const QDir source=QFileInfo(selected).absoluteDir();
        m_inpPath=m_outDir+"/reference.inp";
        if (!QFileInfo::exists(m_inpPath)) QVERIFY(QFile::copy(selected,m_inpPath));
        m_demPath=source.filePath("output_SRTMGL1.tif");
        for(const auto &file:source.entryList({"*.dat"},QDir::Files))
            if(!QFileInfo::exists(QDir(m_outDir).filePath(file))) QVERIFY(QFile::copy(source.filePath(file),QDir(m_outDir).filePath(file)));
    }
    m_domainPath = qEnvironmentVariable("SWMMVIS_BELLINGE_DOMAIN",m_outDir + "/domain.geojson");
    QVERIFY(QFileInfo::exists(m_inpPath));
    QVERIFY(QFileInfo::exists(m_demPath));
    QVERIFY2(QFileInfo::exists(m_domainPath),
             "domain.geojson missing — run extract_domain.py first");

    // Domain ring (also the trim / conformity / coupling reference).
    QFile dom(m_domainPath);
    QVERIFY(dom.open(QIODevice::ReadOnly));
    const auto ring = QJsonDocument::fromJson(dom.readAll())
        .object()["features"].toArray().first().toObject()["geometry"]
        .toObject()["coordinates"].toArray().first().toArray();
    for (const auto &pt : ring) {
        const auto xy = pt.toArray();
        m_domainRing.append(QPointF(xy.first().toDouble(), xy.last().toDouble()));
    }
    QVERIFY(m_domainRing.size() > 100);
    if (m_domainRing.first() == m_domainRing.last()) m_domainRing.removeLast();
    m_domainPoly = QPolygonF(m_domainRing);

    // Faithful inputs: the real model through the dialog's own collector.
    m_workspace.reset(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
    m_window = std::make_unique<SWMMVisProjectWindow>(m_workspace.get(),
                                                      m_inpPath, nullptr);
    QList<QString> warnings, errors;
    QVERIFY2(m_window->loadModel(warnings, errors),
             qPrintable(errors.join('\n')));
    MeshGenerationDialog dialog(m_window.get(), nullptr);
    QString error;
    QVERIFY2(dialog.collectInputs(&m_base, &error), qPrintable(error));
    dialog.reject();
    m_base.includeJunctions = true;
    // Runs A–D constrain nodes only (coupling identity rides on the
    // JUNCTION nodes, which the gates check); E/F add the conduits. Bellinge
    // has pipes that cross in plan, three alignments stored backwards and
    // alignments that pass within centimetres of each other: the worker
    // unfolds the alignments and the generator joins lines closer than its
    // refinement floor and splits crossings (MESH_TRIANGLE_ENGINE_PLAN
    // as-built), so E/F mesh the network as it is.
    m_links = m_base.candidateLinks;
    QVERIFY(!m_links.isEmpty());
    m_base.includeConduits  = false;
    m_base.candidateLinks.clear();
    // Match the shipped baseline mesh, which has no subcatchment regions.
    // The dialog default ticks "Subcatchments → triangle regions", and the
    // worker then writes a [2D_TRIANGLE_NODE_MAP] row per tagged cell naming
    // the SUBCATCHMENT id, which the engine rejects as an unknown node
    // (pre-existing behaviour, reported as an open item — not overhaul code).
    m_base.includeSubcatch  = false;
    QVERIFY(!m_base.candidateNodes.isEmpty());
    QVERIFY(!m_base.meshCRSWkt.isEmpty());

    m_csvRows << "run,cells,quads,triangles,ratioMax,ratioP50,ratioP95,"
                 "hist<=1.25,hist<=1.5,hist<=2,hist<=3,hist<=4,hist>4,"
                 "minAngleDeg,cellsBelow10Deg,orthoMedianDeg,orthoMaxDeg,"
                 "quadMinSJ,quadNonConvex,sizeConformPct,trianglesBelowThetaPct,"
                 "nonExemptBelowTheta,stripsPlaced,stripsDropped,"
                 "terrainRmsM,terrainMaxM,wallMs,peakRssB,"
                 "rssPerCellB,ringVertsBefore,ringVertsAfterTrim,"
                 "couplingLost,deterministic";
}

void TestMeshOverhaulBellinge::adaptiveComparison()
{
    QFile csv(m_outDir+"/adaptive_comparison.csv"); QVERIFY(csv.open(QIODevice::WriteOnly));
    csv.write("mode,tolerance,cells,vertices,wall_ms,peak_rss_bytes,max_sample_error,violating_cells,terrain_inserts,size_inserts,quality_inserts\n");
    mesh::TerrainErrorField terrain;
    QVERIFY2(terrain.open(m_demPath,m_base.meshCRSWkt,m_domainPoly.boundingRect()),qPrintable(terrain.errorMsg()));
    for (int mode=0;mode<3;++mode) for(double tolerance:{1.,2.,3.,5.}) {
        RunSpec spec{"comparison",10,tolerance,30,false,0,"terrain refinement comparison"};
        auto in=makeInputs(spec);
        in.terrainAdaptive=mode!=0;
        in.genOpts.prioritizeQuality=mode!=0;
        in.refineAtFeatures=mode!=2;
        in.nodesUseRim=false; in.nodeFlattenRadius=0;
        in.genOpts.quadsBetweenBreaklines=false; // compare triangular terrain interpolation
        QElapsedTimer timer; timer.start();
        const auto result=run(in); const qint64 ms=timer.elapsed();
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        double maxError=0; int violations=0;
        for (const auto &cell:result.meshResult.triangles) {
            QPointF xy[3]; double z[3];
            for(int k=0;k<3;++k) { const auto &v=result.meshResult.vertices[cell.vertex(k)]; xy[k]=v.xy; z[k]=v.z; }
            const auto q=terrain.queryTriangle(xy,z,tolerance,true);
            QVERIFY(q.valid); maxError=std::max(maxError,q.maxError);
            if(q.maxError>tolerance) ++violations;
        }
        const auto &st=result.generationStats;
        const QString line=QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11\n")
            .arg(mode==0?"legacy":mode==1?"adaptive_features":"adaptive_geometry")
            .arg(tolerance).arg(result.meshResult.triangles.size()).arg(result.meshResult.vertices.size())
            .arg(ms).arg(peakRssBytes()).arg(maxError,0,'g',12).arg(violations)
            .arg(st.terrainInserted).arg(st.sizeInserted).arg(st.qualityInserted);
        csv.write(line.toUtf8()); csv.flush(); qInfo().noquote()<<line.trimmed();
        QFile meshFile(m_outDir+QStringLiteral("/%1_%2m.2dm").arg(mode==0?"legacy":mode==1?"adaptive_features":"adaptive_geometry").arg(tolerance));
        QVERIFY(meshFile.open(QIODevice::WriteOnly));
        mesh::InpMeshWriter::UnitInfo units;
        units.linearUnitName=in.meshLinearUnitName; units.sourceCrsTag=in.meshCRSTag;
        meshFile.write(mesh::InpMeshWriter::buildSectionText(result.meshResult,result.coupling,.035,units).toUtf8());
        if(mode && !st.refineCapped) QCOMPARE(violations,st.terrainUnresolved);
    }
}

void TestMeshOverhaulBellinge::adaptiveWithModelConstraints()
{
    const RunSpec spec{"constraints",10,3,30,true,0,"adaptive default with model constraints"};
    auto in=makeInputs(spec);
    in.terrainAdaptive=true; in.terrainAutoTolerance=true;
    in.refineAtFeatures=false; in.genOpts.prioritizeQuality=true;
    QElapsedTimer timer; timer.start(); const auto result=run(in);
    QVERIFY2(result.ok,qPrintable(result.errorMsg));
    const auto &m=result.meshResult;
    const auto edges=mesh::buildEdgeTriangles(m);
    for(auto it=edges.cbegin();it!=edges.cend();++it) QVERIFY(it.value().size()<=2);
    for(const auto &cell:m.triangles) QVERIFY(mesh::cellGeom(m.vertices,cell).area>0);
    QSet<QString> coupled;
    for(const auto &v:m.vertices) if(!v.tag.isEmpty()) coupled.insert(v.tag);
    for(const auto &v:m.cellCouplings) coupled.insert(v.nodeId);
    for(const auto &id:result.coupling.vertexToNode) coupled.insert(id);
    for(const auto &id:result.coupling.triangleToNode) coupled.insert(id);
    int expected=0;
    for(const auto &node:m_base.candidateNodes) if(m_domainPoly.containsPoint(node.xy,Qt::OddEvenFill)) {
        ++expected; QVERIFY2(coupled.contains(node.name),qPrintable(node.name));
    }
    qInfo()<<"adaptive model constraints: cells"<<m.triangles.size()<<"vertices"<<m.vertices.size()
           <<"coupled nodes"<<expected<<"tolerance"<<result.terrainToleranceUsed
           <<"unresolved terrain"<<result.generationStats.terrainUnresolved<<"wall ms"<<timer.elapsed();
    for(const auto &warning:result.alignmentWarnings) qInfo().noquote()<<warning;
    QFile output(m_outDir+"/adaptive_model_constraints.2dm"); QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(mesh::InpMeshWriter::buildSectionText(m,result.coupling).toUtf8());
    QVERIFY(!result.generationStats.refineCapped);
}

void TestMeshOverhaulBellinge::acceptance()
{
    for (const RunSpec &spec : kRuns) {
        const Inputs inputs = makeInputs(spec);

        const long rssBefore = peakRssBytes();
        QElapsedTimer clock; clock.start();
        const Result r = run(inputs);
        const qint64 wallMs = clock.elapsed();
        const long rssAfter = peakRssBytes();
        QVERIFY2(r.ok, qPrintable(QString("run %1: %2")
                                      .arg(spec.id, r.errorMsg)));
        const auto &mesh = r.meshResult;
        const int cells = int(mesh.triangles.size());
        QVERIFY(cells > 0);

        // ── Metrics ────────────────────────────────────────────────────
        const mesh::GradingStats grading = mesh::computeGradingStats(mesh);
        const mesh::QuadStats    quads   = mesh::computeQuadStats(mesh);

        // Boundary trim measured on the domain ring with this run's knobs.
        int removed = 0;
        const auto trimmed = mesh::pslg::trimByStraightness(
            m_domainRing, inputs.trimTurnDeg, inputs.trimDeviation, {},
            /*closed=*/true, &removed);
        Q_UNUSED(trimmed);

        // Coupling identity: every conduit endpoint and node inside the
        // domain must appear exactly among the mesh vertices.
        QHash<QPair<qint64, qint64>, int> vertexAt;
        const double snap = 0.01 * inputs.minCellSize;
        auto key = [snap](const QPointF &p) {
            return qMakePair(qint64(std::llround(p.x() / snap)),
                             qint64(std::llround(p.y() / snap)));
        };
        for (int i = 0; i < mesh.vertices.size(); ++i)
            vertexAt.insert(key(mesh.vertices[i].xy), i);
        auto present = [&](const QPointF &p) {
            const auto k = key(p);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (vertexAt.contains(qMakePair(k.first + dx,
                                                    k.second + dy)))
                        return true;
            return false;
        };
        // A node keeps its coupling identity either pinned as an exact
        // vertex, demoted to cell coupling (the worker's min-separation
        // rule), or carried by the post-generation node→cell coupling map —
        // only a node with NONE of the three is lost.
        QSet<QString> coupled;
        for (const auto &cc : mesh.cellCouplings) coupled.insert(cc.nodeId);
        for (auto it = r.coupling.triangleToNode.cbegin();
             it != r.coupling.triangleToNode.cend(); ++it)
            coupled.insert(it.value());
        int couplingLost = 0;
        for (const auto &node : m_base.candidateNodes)
            if (m_domainPoly.containsPoint(node.xy, Qt::OddEvenFill)
                && !present(node.xy) && !coupled.contains(node.name))
                ++couplingLost;

        // Size conformity against the rebuilt size field; triangles under
        // the run's minimum angle (all causes — the generator's own count
        // leaves out the ones at small input angles).
        mesh::SizeField field;
        mesh::TerrainSizeField terrainField;
        std::function<double(double, double)> terrainAt;
        const bool haveField = buildReferenceField(inputs, field,
                                                   terrainField, terrainAt);
        int conforming = 0, fieldSampled = 0, belowTheta = 0;
        double terrSum2 = 0.0, terrMax = 0.0;
        int terrN = 0;
        mesh::DTMRaster dem;
        OGRCoordinateTransformation *meshToDem = nullptr;
        if (spec.terrainTolerance > 0.0 && dem.open(m_demPath)) {
            GDALAllRegister();
            auto *ds = static_cast<GDALDataset *>(
                GDALOpen(m_demPath.toUtf8().constData(), GA_ReadOnly));
            if (ds) {
                OGRSpatialReference demSrs, meshSrs;
                demSrs.importFromWkt(ds->GetProjectionRef());
                meshSrs.importFromWkt(m_base.meshCRSWkt.toUtf8().constData());
                demSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                meshSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                meshToDem = OGRCreateCoordinateTransformation(&meshSrs, &demSrs);
                GDALClose(ds);
            }
        }
        for (int t = 0; t < mesh.triangles.size(); ++t) {
            const auto &cell = mesh.triangles[t];
            const auto geom = mesh::cellGeom(mesh.vertices, cell);
            const QPointF c = geom.centroid;
            if (!cell.isQuad()
                && triangleMinAngleDeg(mesh, cell) < spec.minAngleDeg - 0.01)
                ++belowTheta;
            if (haveField) {
                const double h = field.sizeAt(c.x(), c.y());
                if (h > 0.0) {
                    ++fieldSampled;
                    const double s = meanEdge(mesh, cell);
                    if (s >= 0.7 * h && s <= 1.4 * h) ++conforming;
                }
            }
            if (meshToDem) {
                double gx = c.x(), gy = c.y();
                if (meshToDem->Transform(1, &gx, &gy)) {
                    const double zDem = dem.sampleAt(gx, gy);
                    if (std::isfinite(zDem)) {
                        double zMesh = (mesh.vertices[cell.v0].z
                                        + mesh.vertices[cell.v1].z
                                        + mesh.vertices[cell.v2].z);
                        int nv = 3;
                        if (cell.isQuad()) { zMesh += mesh.vertices[cell.v3].z; nv = 4; }
                        const double d = std::abs(zMesh / nv - zDem);
                        terrSum2 += d * d; terrMax = std::max(terrMax, d);
                        ++terrN;
                    }
                }
            }
        }
        if (meshToDem) OGRCoordinateTransformation::DestroyCT(meshToDem);

        // Topology: interior edges have exactly 2 cells; no zero-area cells;
        // no duplicate vertices.
        const auto edgeTris = mesh::buildEdgeTriangles(mesh);
        int overusedEdges = 0, boundaryEdgeCount = 0;
        for (auto it = edgeTris.cbegin(); it != edgeTris.cend(); ++it) {
            if (it.value().size() == 1) ++boundaryEdgeCount;
            else if (it.value().size() > 2) ++overusedEdges;
        }
        int zeroArea = 0;
        for (int t = 0; t < mesh.triangles.size(); ++t)
            if (!(mesh::triangleArea(mesh, t) > 0.0)) ++zeroArea;
        QHash<QPair<qint64, qint64>, int> dupCheck;
        int duplicateVerts = 0;
        for (const auto &v : mesh.vertices) {
            const auto k = qMakePair(qint64(std::llround(v.xy.x() * 1e6)),
                                     qint64(std::llround(v.xy.y() * 1e6)));
            if (dupCheck.contains(k)) ++duplicateVerts;
            else dupCheck.insert(k, 1);
        }

        // Determinism (run C only): regenerate under 1/4/8 worker threads
        // and byte-compare the written mesh. The n=1 run is also the
        // single-thread timing sample for the performance gate.
        QString deterministic = "n/a";
        qint64 singleThreadMs = -1;
        const QString deckCopy = m_outDir + QString("/deck_%1.inp").arg(spec.id);
        const QString meshOut  = m_outDir + QString("/mesh_%1.2dm").arg(spec.id);
        QFile::remove(deckCopy);
        QVERIFY(QFile::copy(m_inpPath, deckCopy));
        QString werr;
        QVERIFY2(mesh::InpMeshWriter::writeExternal(deckCopy, meshOut, mesh,
                                                    r.coupling, 0.035, &werr),
                 qPrintable(werr));
        if (QString(spec.id) == "C") {
            QFile ref(meshOut);
            QVERIFY(ref.open(QIODevice::ReadOnly));
            const QByteArray refBytes = ref.readAll();
            deterministic = "yes";
            for (int threads : {1, 4, 8}) {
                QThreadPool::globalInstance()->setMaxThreadCount(threads);
                QElapsedTimer tclock; tclock.start();
                const Result rr = run(inputs);
                if (threads == 1) singleThreadMs = tclock.elapsed();
                QVERIFY2(rr.ok, qPrintable(rr.errorMsg));
                // The .2dm header embeds the source project name, so the
                // per-thread deck copy must carry the SAME basename as the
                // reference or the byte comparison fails on the header.
                const QDir tdir(m_outDir + QString("/det_t%1").arg(threads));
                QVERIFY(QDir().mkpath(tdir.path()));
                const QString tdeck = tdir.filePath(
                    QFileInfo(deckCopy).fileName());
                QFile::remove(tdeck);
                QVERIFY(QFile::copy(m_inpPath, tdeck));
                const QString path = tdir.filePath(
                    QString("mesh_%1.2dm").arg(spec.id));
                QVERIFY(mesh::InpMeshWriter::writeExternal(
                    tdeck, path, rr.meshResult, rr.coupling, 0.035, &werr));
                QFile probe(path);
                QVERIFY(probe.open(QIODevice::ReadOnly));
                if (probe.readAll() != refBytes) deterministic =
                    QString("DIFFERS at %1 threads").arg(threads);
            }
            QThreadPool::globalInstance()->setMaxThreadCount(
                QThread::idealThreadCount());
        }

        // ── Gates (plan §6 with D6) ────────────────────────────────────
        const double conformPct = fieldSampled
            ? 100.0 * conforming / fieldSampled : -1.0;
        const int triangleCount = grading.triangles;
        const double belowThetaPct = triangleCount
            ? 100.0 * belowTheta / triangleCount : 0.0;
        const mesh::GenerationStats &gen = r.generationStats;
        const double nonExemptPct = cells
            ? 100.0 * gen.trianglesBelowAngle / cells : 0.0;
        const int above2 = grading.ratioHistogram[3] + grading.ratioHistogram[4]
                         + grading.ratioHistogram[5];
        const double above2Pct = grading.faces ? 100.0 * above2 / grading.faces : 0.0;
        const double below10Pct = cells ? 100.0 * grading.cellsBelow10Deg / cells : 0.0;
        const double terrRms = terrN ? std::sqrt(terrSum2 / terrN) : 0.0;
        const long   rssDelta = std::max(0L, rssAfter - rssBefore);
        auto gate = [&](bool pass, const QString &what) {
            if (!pass) m_gateFailures << QString("%1: %2").arg(spec.id, what);
            return pass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        };
        QStringList rows;
        // Triangle engine gates (MESH_TRIANGLE_ENGINE_PLAN §6). With every
        // angle >= theta, neighbours' longest edges differ by <= 1/sin theta
        // (2 at 30 deg); triangles at a small INPUT angle (two pipes leaving
        // a manhole 3 deg apart) cannot meet the bound in any mesher, hence
        // the small allowances instead of zero.
        rows << QString("| grading P50 <= 1.3 (max %1, P95 %2) | %3 | %4 |")
                    .arg(grading.ratioMax, 0, 'f', 3)
                    .arg(grading.ratioP95, 0, 'f', 3)
                    .arg(grading.ratioP50, 0, 'f', 3)
                    .arg(gate(grading.ratioP50 <= 1.3, QString("ratioP50 %1")
                                  .arg(grading.ratioP50)));
        rows << QString("| neighbour ratio > 2 on <= 0.05% of faces | %1 (%2%) | %3 |")
                    .arg(above2).arg(above2Pct, 0, 'f', 3)
                    .arg(gate(above2Pct <= 0.05,
                              QString("%1 faces above ratio 2").arg(above2)));
        rows << QString("| triangles under %1 deg (all causes) <= 0.5% | %2 (%3%) | %4 |")
                    .arg(spec.minAngleDeg).arg(belowTheta)
                    .arg(belowThetaPct, 0, 'f', 3)
                    .arg(gate(belowThetaPct <= 0.5,
                              QString("%1 triangles under theta").arg(belowTheta)));
        rows << QString("| ... not at a small input angle <= 0.1% of cells | %1 (%2%) | %3 |")
                    .arg(gen.trianglesBelowAngle).arg(nonExemptPct, 0, 'f', 3)
                    .arg(gate(nonExemptPct <= 0.1,
                              QString("%1 non-exempt triangles under theta")
                                  .arg(gen.trianglesBelowAngle)));
        rows << QString("| cells < 10 deg <= 0.05% (min angle %1) | %2 (%3%) | %4 |")
                    .arg(grading.minAngleDeg, 0, 'f', 2)
                    .arg(grading.cellsBelow10Deg).arg(below10Pct, 0, 'f', 3)
                    .arg(gate(below10Pct <= 0.05,
                              QString("%1 cells under 10 deg")
                                  .arg(grading.cellsBelow10Deg)));
        rows << QString("| orthogonality median / max (report only) | %1 / %2 | info |")
                    .arg(grading.orthoMedianDeg, 0, 'f', 2)
                    .arg(grading.orthoMaxDeg, 0, 'f', 2);
        if (spec.conduits)
            rows << QString("| conduit strips placed / dropped (report only) | %1 / %2 | info |")
                        .arg(gen.conduitStrips).arg(gen.stripsDropped);
        if (grading.quads > 0) {
            rows << QString("| quads convex, min SJ >= 0.5 | nonConvex %1, SJ %2 | %3 |")
                        .arg(quads.nonConvex)
                        .arg(quads.minScaledJacobian, 0, 'f', 3)
                        .arg(gate(quads.nonConvex == 0
                                      && quads.minScaledJacobian >= 0.5,
                                  QString("nonConvex %1 / SJ %2")
                                      .arg(quads.nonConvex)
                                      .arg(quads.minScaledJacobian)));
        }
        // Strips narrower than the cell size make their own cells (and the
        // triangles beside them) smaller than h by design: report only there.
        if (conformPct >= 0.0 && spec.stripWidth > 0.0)
            rows << QString("| size conformity in [0.7h,1.4h] (report only: strips) | %1% | info |")
                        .arg(conformPct, 0, 'f', 1);
        else if (conformPct >= 0.0)
            rows << QString("| size conformity >= 75% in [0.7h,1.4h] (mean edge) | %1% | %2 |")
                        .arg(conformPct, 0, 'f', 1)
                        .arg(gate(conformPct >= 75.0,
                                  QString("conformity %1%").arg(conformPct)));
        if (spec.terrainTolerance > 0.0)
            rows << QString("| terrain RMS <= tol / max <= 3 tol | %1 / %2 m | %3 |")
                        .arg(terrRms, 0, 'f', 3).arg(terrMax, 0, 'f', 3)
                        .arg(gate(terrRms <= spec.terrainTolerance
                                      && terrMax <= 3.0 * spec.terrainTolerance,
                                  QString("terrain %1/%2 vs tol %3")
                                      .arg(terrRms).arg(terrMax)
                                      .arg(spec.terrainTolerance)));
        rows << QString("| topology clean | overused %1, zeroArea %2, dupVerts %3 | %4 |")
                    .arg(overusedEdges).arg(zeroArea).arg(duplicateVerts)
                    .arg(gate(overusedEdges == 0 && zeroArea == 0
                                  && duplicateVerts == 0,
                              "topology"));
        rows << QString("| boundary trim >= 50% | %1 of %2 removed | %3 |")
                    .arg(removed).arg(m_domainRing.size())
                    .arg(gate(removed * 2 >= m_domainRing.size(),
                              QString("trim removed %1/%2")
                                  .arg(removed).arg(m_domainRing.size())));
        rows << QString("| coupling vertices lost == 0 | %1 | %2 |")
                    .arg(couplingLost)
                    .arg(gate(couplingLost == 0,
                              QString("%1 coupling vertices lost")
                                  .arg(couplingLost)));
        if (QString(spec.id) == "C") {
            rows << QString("| determinism across 1/4/8 threads | %1 | %2 |")
                        .arg(deterministic)
                        .arg(gate(deterministic == "yes", deterministic));
            rows << QString("| run C <= 10 s single-thread | %1 ms | %2 |")
                        .arg(singleThreadMs)
                        .arg(gate(singleThreadMs >= 0
                                      && singleThreadMs <= 10000,
                                  QString("%1 ms").arg(singleThreadMs)));
            rows << QString("| run C <= 200 B/cell peak | %1 B/cell | %2 |")
                        .arg(cells ? rssDelta / cells : 0)
                        .arg(gate(cells && rssDelta / cells <= 200,
                                  QString("%1 B/cell")
                                      .arg(cells ? rssDelta / cells : 0)));
        }

        m_reportSections << QString("## Run %1 — cell %2 m, tol %3 m, min angle %4 deg%5\n\n"
                                    "%6 cells (%7 quads, %8 triangles), "
                                    "%9 ms wall, peak-RSS delta %10 MB\n\n"
                                    "| Gate | Measured | Verdict |\n"
                                    "| --- | --- | --- |\n%11\n")
                                .arg(spec.id).arg(spec.cellSize)
                                .arg(spec.terrainTolerance)
                                .arg(spec.minAngleDeg)
                                .arg(!spec.conduits ? QString()
                                         : spec.stripWidth > 0.0
                                             ? QString(", conduits with %1 m strips").arg(spec.stripWidth)
                                             : QString(", conduits as lines"))
                                .arg(cells).arg(grading.quads)
                                .arg(grading.triangles).arg(wallMs)
                                .arg(rssDelta / (1024.0 * 1024.0), 0, 'f', 1)
                                .arg(rows.join('\n'));

        m_csvRows << QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,"
                             "%14,%15,%16,%17,%18,%19,%20,%21,%22,%23,%24,"
                             "%25,%26,%27,%28,%29,%30,%31,%32,%33")
            .arg(spec.id).arg(cells).arg(grading.quads).arg(grading.triangles)
            .arg(grading.ratioMax).arg(grading.ratioP50).arg(grading.ratioP95)
            .arg(grading.ratioHistogram[0]).arg(grading.ratioHistogram[1])
            .arg(grading.ratioHistogram[2]).arg(grading.ratioHistogram[3])
            .arg(grading.ratioHistogram[4]).arg(grading.ratioHistogram[5])
            .arg(grading.minAngleDeg).arg(grading.cellsBelow10Deg)
            .arg(grading.orthoMedianDeg).arg(grading.orthoMaxDeg)
            .arg(quads.minScaledJacobian).arg(quads.nonConvex)
            .arg(conformPct).arg(belowThetaPct).arg(gen.trianglesBelowAngle)
            .arg(gen.conduitStrips).arg(gen.stripsDropped)
            .arg(terrRms).arg(terrMax).arg(wallMs).arg(rssDelta)
            .arg(cells ? rssDelta / cells : 0)
            .arg(m_domainRing.size()).arg(m_domainRing.size() - removed)
            .arg(couplingLost).arg(deterministic);

        qInfo("[bellinge %s] %d cells in %lld ms; grading max %.3f P50 %.3f",
              spec.id, cells, (long long) wallMs, grading.ratioMax,
              grading.ratioP50);
    }
}

void TestMeshOverhaulBellinge::cleanupTestCase()
{
    if (m_csvRows.isEmpty()) return;        // skipped
    QFile csv(m_outDir + "/metrics.csv");
    QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Truncate));
    csv.write(m_csvRows.join('\n').toUtf8() + "\n");
    QFile report(m_outDir + "/report.md");
    QVERIFY(report.open(QIODevice::WriteOnly | QIODevice::Truncate));
    report.write(("# Bellinge acceptance — mesh overhaul Phase 7 on the triangle engine\n\n"
                  + m_reportSections.join('\n')
                  + (m_gateFailures.isEmpty()
                         ? QString("\nAll gates PASS.\n")
                         : QString("\n## Gate failures\n\n- %1\n")
                               .arg(m_gateFailures.join("\n- ")))).toUtf8());
    // The harness records failures in report.md either way (handoff step 3),
    // and also fails the test so ctest shows the verdict.
    QVERIFY2(m_gateFailures.isEmpty(),
             qPrintable("gate failures: " + m_gateFailures.join("; ")));
}

QTEST_MAIN(TestMeshOverhaulBellinge)
#include "test_mesh_overhaul_bellinge.moc"
