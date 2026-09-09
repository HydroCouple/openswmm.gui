/*!
 * \file   meshgenerationdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/theme/themehelpers.h"
#include "ui/widgets/meshregiondefaultswidget.h"

#include "ui/uiscrollhelpers.h"

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "core/editgeometry.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"

#include "mesh/meshgenerator.h"
#include "mesh/meshnodemapper.h"
#include "mesh/meshpatch.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshresult.h"
#include "mesh/dtmthinner.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/naturalnbinterpolator.h"
#include "mesh/meshreorder.h"
#include "mesh/meshstagecache.h"
#include "mesh/pslgprep.h"
#include "mesh/pslgminsize.h"
#include "mesh/sizefield.h"
#include "mesh/meshminsizecleanup.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_model.h>

#include <gdal_priv.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QtConcurrent/QtConcurrent>

#include <QApplication>
#include <QButtonGroup>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

// lcMeshPerf ("openswmm.mesh.perf") is defined in mesh/meshstagecache.cpp
// and declared by its header, included above.

// ---------------------------------------------------------------------------
// PSLG geometry utilities — implementations live in mesh/pslgprep.{h,cpp};
// the using-declarations keep every unqualified call site below unchanged.
// ---------------------------------------------------------------------------

using mesh::pslg::simplifyPolyline;
using mesh::pslg::simplifyRing;
using mesh::pslg::densifyRing;
using mesh::pslg::distSqToSegment;
using mesh::pslg::snapAndDedupe;

// ---------------------------------------------------------------------------
// Checked coordinate transform
// ---------------------------------------------------------------------------

/*! \brief Reproject \p n points in place, marking any PROJ could not convert
 *         as NaN, and return how many failed.
 *
 * The return value of OGRCoordinateTransformation::Transform() is not on its
 * own a reliable failure signal — GDAL's own documentation (ogr_spatialref.h,
 * the 4D overload) warns that "prior to GDAL 3.11, TRUE could be returned if a
 * transformation could be found but not all points may have necessarily
 * succeed to transform". The per-point \c pabSuccess array is authoritative,
 * so this always passes one and ignores the scalar result.
 *
 * OGR leaves HUGE_VAL in a slot it could not transform. Canonicalising to NaN
 * matters for two reasons:
 *
 *  - NaN is already the pipeline's "no value" sentinel (DTMSampler,
 *    DTMThinner, NaturalNeighbourInterpolator all use it), so downstream
 *    finiteness checks catch it uniformly.
 *  - HUGE_VAL passes as an ordinary large coordinate. It survives a bbox
 *    comparison, gets averaged into a centroid, and reaches Triangle intact.
 *
 * Failures are real: PROJ rejects points outside a projection's domain of
 * validity (a UTM zone transform far from its meridian), points needing a
 * datum grid that is not installed, and inverse projections that do not
 * converge — all of which occur on the edges of regional datasets.
 */
static qsizetype transformChecked(OGRCoordinateTransformation *ct,
                                  qsizetype n, double *x, double *y)
{
    if (!ct || n <= 0) return 0;

    QVector<int> ok(static_cast<int>(n), 0);
    ct->Transform(static_cast<size_t>(n), x, y, nullptr, ok.data());

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    qsizetype nBad = 0;
    for (qsizetype i = 0; i < n; ++i)
    {
        if (ok[static_cast<int>(i)] && std::isfinite(x[i]) && std::isfinite(y[i]))
            continue;
        x[i] = kNaN;
        y[i] = kNaN;
        ++nBad;
    }
    return nBad;
}

/*! \brief Single-point form of transformChecked(). Returns false (and leaves
 *         \p x / \p y NaN) when the point could not be reprojected. A null
 *         \p ct means "same CRS" and succeeds unchanged. */
static bool transformCheckedPt(OGRCoordinateTransformation *ct,
                               double &x, double &y)
{
    return transformChecked(ct, 1, &x, &y) == 0;
}

// ---------------------------------------------------------------------------
// Quad region helpers (QUAD_MESHING_REDESIGN_PLAN_2026-09-06 §3.1)
// ---------------------------------------------------------------------------

/*! \brief Log / attribute spelling of a mesh::QuadRegionMode. */
static const char *quadRegionModeName(mesh::QuadRegionMode m)
{
    switch (m)
    {
    case mesh::QuadRegionMode::Auto:          return "auto";
    case mesh::QuadRegionMode::Mapped:        return "mapped";
    case mesh::QuadRegionMode::Submapped:     return "submapped";
    case mesh::QuadRegionMode::Free:          return "free";
    case mesh::QuadRegionMode::TrianglesOnly: return "triangles";
    }
    return "?";
}

/*! \brief Parse a `quad_mode` attribute value (case-insensitive). Returns
 *         false and leaves \p out untouched on an unknown spelling. */
static bool parseQuadRegionMode(const QString &s, mesh::QuadRegionMode *out)
{
    const QString t = s.trimmed().toLower();
    if      (t == QLatin1String("auto"))      *out = mesh::QuadRegionMode::Auto;
    else if (t == QLatin1String("mapped"))    *out = mesh::QuadRegionMode::Mapped;
    else if (t == QLatin1String("submapped")) *out = mesh::QuadRegionMode::Submapped;
    else if (t == QLatin1String("free"))      *out = mesh::QuadRegionMode::Free;
    else if (t == QLatin1String("triangles")) *out = mesh::QuadRegionMode::TrianglesOnly;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// Worker function — runs on QtConcurrent thread, NO widget access allowed.
// ---------------------------------------------------------------------------

static void
runMeshPipelineImpl(QPromise<MeshGenerationDialog::PipelineResult> &promise,
                    MeshGenerationDialog::PipelineInputs            in)
{
    using PResult = MeshGenerationDialog::PipelineResult;

    // Adds a failure result and marks the promise done.
    // Callers `return` immediately after calling this.
    auto fail = [&](const QString &msg) {
        PResult r; r.ok = false; r.errorMsg = msg;
        promise.addResult(r);
    };

    promise.setProgressRange(0, 100);

    auto progress = [&](int pct, const QString &msg) {
        promise.setProgressValueAndText(pct, msg);
    };

    // 2026-07-19c — sub-stage timing. The 36 %→38 % band (reprojection,
    // Poisson filter, boundary filter) reported no intermediate progress,
    // so a slow stage there was indistinguishable from a hang. stageMark()
    // logs the wall time of the stage that just ended plus the running
    // total; every heavy loop in that band now calls it.
    QElapsedTimer stageClock, totalClock;
    stageClock.start();
    totalClock.start();
    auto stageMark = [&](const char *what) {
        qCDebug(lcMeshPerf).nospace() << "[Mesh][t] " << what << ": "
                           << stageClock.restart() << " ms (total "
                           << totalClock.elapsed() << " ms)";
    };

    // ── Stage-A cache: prepared boundary (domains + hole rings/seeds) ──
    // A hit skips the feature read, UnaryUnion dissolve, exterior-ring prep,
    // AND the 65k-ring hole preparation below.  Keyed on the boundary source
    // identity (path+mtime+size / subcatchment-vertex hash), both CRS WKTs,
    // and the two ring-prep parameters — anything else changing still hits.
    mesh::MeshStageCache cache(in.inpPath);
    mesh::MeshStageCache::BoundaryPrep bprep;
    bool bprepReady = false;
    QByteArray boundaryCacheKey;
    if (in.boundaryKind != MeshGenerationDialog::PipelineInputs::BoundaryKind::AutoBBox
        && cache.isUsable())
    {
        mesh::MeshStageCache::FileIdentity srcId;
        QByteArray subHash;
        if (in.boundaryKind
            == MeshGenerationDialog::PipelineInputs::BoundaryKind::VectorFile)
        {
            const QFileInfo bfi(in.boundaryPath);
            srcId.absPath   = bfi.absoluteFilePath();
            srcId.mtimeMs   = bfi.lastModified().toMSecsSinceEpoch();
            srcId.sizeBytes = bfi.size();
        }
        else
        {
            QByteArray blob;
            {
                QDataStream s(&blob, QIODevice::WriteOnly);
                s.setVersion(QDataStream::Qt_6_0);
                s << in.subcatchPolys;
            }
            subHash = QCryptographicHash::hash(blob, QCryptographicHash::Sha256);
        }
        // minCellSize participates in the key: the cached payload is the
        // PREPARED (and, from 2026-08-17, conditioned) rings, so reusing an
        // entry built at a different minimum size would silently mesh the
        // wrong geometry.
        boundaryCacheKey = mesh::MeshStageCache::boundaryKey(
            srcId, subHash, in.boundaryLayerName, in.boundaryCRSWkt,
            in.meshCRSWkt, in.pslgSimplifyEps, in.maxBoundaryEdgeLen,
            in.minSizePolicy.minCellSize, in.minSizeEnforce);

        QElapsedTimer cacheClock;
        cacheClock.start();
        if (cache.loadBoundary(boundaryCacheKey, &bprep))
        {
            bprepReady   = true;
            in.domains   = bprep.domains;
            in.holeRings = bprep.holeRings;
            qCInfo(lcMeshPerf) << "[Mesh][cache] boundary prep HIT"
                               << boundaryCacheKey.left(12).constData()
                               << "-" << bprep.holeRings.size() << "holes,"
                               << bprep.domains.size() << "domains in"
                               << cacheClock.elapsed() << "ms";
            progress(11, QObject::tr("Boundary loaded from cache (%1 holes)")
                             .arg(bprep.holeRings.size()));
        }
        else
        {
            qCInfo(lcMeshPerf) << "[Mesh][cache] boundary prep miss"
                               << boundaryCacheKey.left(12).constData();
        }
    }

    // ── Boundary ingestion (worker-side) ─────────────────────────────
    // collectInputs records only the boundary source identity; the feature
    // read, UnaryUnion dissolve, and exterior-ring prep run here so the GUI
    // thread never blocks on them.  Vector sources are re-opened by path —
    // GDAL dataset handles and OGR SRS/CT objects must not cross threads.
    if (!bprepReady)
    {
    progress(2, QObject::tr("Reading boundary geometry…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
    {
        using BoundaryKind = MeshGenerationDialog::PipelineInputs::BoundaryKind;

        OGRCoordinateTransformation *boundaryCT = nullptr;  // boundary → mesh CRS
        if (!in.boundaryCRSWkt.isEmpty() && !in.meshCRSWkt.isEmpty())
        {
            OGRSpatialReference bSRS, mSRS;
            if (bSRS.importFromWkt(in.boundaryCRSWkt.toUtf8().constData()) == OGRERR_NONE
                && mSRS.importFromWkt(in.meshCRSWkt.toUtf8().constData()) == OGRERR_NONE)
            {
                bSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                mSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                if (!mSRS.IsSame(&bSRS))
                    boundaryCT = OGRCreateCoordinateTransformation(&bSRS, &mSRS);
            }
        }

        // Boundary vertices PROJ could not reproject. These define the meshing
        // domain, so a dropped or fabricated one silently deforms it — count
        // them and refuse the run below rather than meshing a wrong outline.
        qsizetype nBoundaryXformFailed = 0;

        auto ringToMesh = [&](const OGRLinearRing *r) {
            const int n = r->getNumPoints();
            QVector<QPointF> pts;
            pts.reserve(n);
            if (boundaryCT && n > 0)
            {
                QVector<double> xs(n), ys(n);
                for (int i = 0; i < n; ++i) { xs[i] = r->getX(i); ys[i] = r->getY(i); }
                nBoundaryXformFailed += transformChecked(boundaryCT, n,
                                                         xs.data(), ys.data());
                for (int i = 0; i < n; ++i)
                {
                    // Skip the failures so the ring stays well-formed for the
                    // simplify/densify passes; the count above is what decides
                    // whether the run proceeds.
                    if (std::isfinite(xs[i]) && std::isfinite(ys[i]))
                        pts.append(QPointF(xs[i], ys[i]));
                }
            }
            else
            {
                for (int i = 0; i < n; ++i) pts.append(QPointF(r->getX(i), r->getY(i)));
            }
            return pts;
        };

        auto pushOgrPolygon = [&](const OGRPolygon *poly) {
            if (!poly) return;
            const OGRLinearRing *ext = poly->getExteriorRing();
            if (!ext || ext->getNumPoints() < 3) return;
            // Simplify the exterior ring with RDP, then optionally densify:
            // split edges longer than "Max boundary edge length" (pure vertex
            // insertion — geometry unchanged).
            //
            // Validate the simplified ring the same way prepareHoleRing does
            // for interior rings: RDP on a serpentine/concave boundary can
            // make the exterior self-intersect, and a self-intersecting
            // OUTER ring becomes crossing constrained segments in the PSLG
            // (Triangle abort, or a flooded exterior carve). Fall back to
            // the unsimplified ring — GEOS/OGR output is valid by
            // construction; only RDP can break it.
            const QVector<QPointF> rawExt = ringToMesh(ext);
            QVector<QPointF> simpExt = simplifyRing(rawExt, in.pslgSimplifyEps);
            {
                EditGeometry::RingPolygon check;
                check.exterior = simpExt;
                if (EditGeometry::validateRingPolygon(check)
                    != EditGeometry::RingValidity::Ok)
                    simpExt = rawExt;
            }
            in.domains.append(QPolygonF(
                densifyRing(simpExt, in.maxBoundaryEdgeLen)));
            for (int h = 0; h < poly->getNumInteriorRings(); ++h)
            {
                const OGRLinearRing *hole = poly->getInteriorRing(h);
                if (!hole || hole->getNumPoints() < 3) continue;
                // Raw — mesh::pslg::prepareHoleRings handles these below.
                in.holeRings.append(ringToMesh(hole));
            }
        };

        auto walkOgrGeom = [&](const OGRGeometry *geom) {
            if (!geom) return;
            const auto gt = wkbFlatten(geom->getGeometryType());
            if (gt == wkbPolygon)
                pushOgrPolygon(geom->toPolygon());
            else if (gt == wkbMultiPolygon)
            {
                const auto *mp = geom->toMultiPolygon();
                for (int i = 0; i < mp->getNumGeometries(); ++i)
                    pushOgrPolygon(mp->getGeometryRef(i)->toPolygon());
            }
        };

        bool cancelled = false;
        if (in.boundaryKind == BoundaryKind::Subcatchments)
        {
            // Subcatchment rings arrived as POD copies (mesh CRS).  Dissolve
            // with UnaryUnion so internal boundaries between adjacent
            // subcatchments disappear from the PSLG boundary.
            OGRMultiPolygon mp;
            for (const auto &verts : std::as_const(in.subcatchPolys))
            {
                OGRPolygon poly;
                OGRLinearRing ring;
                for (const QPointF &p : verts) ring.addPoint(p.x(), p.y());
                if (verts.first() != verts.last())
                    ring.addPoint(verts.first().x(), verts.first().y());
                poly.addRing(&ring);
                mp.addGeometry(&poly);
            }
            OGRGeometry *unioned = mp.UnaryUnion();
            stageMark("boundary: UnaryUnion (subcatchments)");
            if (unioned)
            {
                walkOgrGeom(unioned);
                OGRGeometryFactory::destroyGeometry(unioned);
            }
            if (in.domains.isEmpty())
                for (const auto &v : std::as_const(in.subcatchPolys))
                    in.domains.append(QPolygonF(v));
        }
        else if (in.boundaryKind == BoundaryKind::VectorFile)
        {
            GDALDataset *ds = GDALDataset::Open(
                in.boundaryPath.toUtf8().constData(),
                GDAL_OF_VECTOR | GDAL_OF_READONLY);
            if (!ds)
            {
                qWarning() << "[Mesh] boundary source open failed:"
                           << in.boundaryPath
                           << "— falling back to the model-extent box.";
            }
            else
            {
                OGRLayer *ol = in.boundaryLayerName.isEmpty()
                                   ? ds->GetLayer(0)
                                   : ds->GetLayerByName(
                                         in.boundaryLayerName.toUtf8().constData());
                if (!ol)
                {
                    qWarning() << "[Mesh] boundary layer not found:"
                               << in.boundaryLayerName;
                }
                else
                {
                    // Collect every polygon into one multipolygon, dissolve
                    // with UnaryUnion so the boundary is a clean outer shell.
                    // (addGeometry clones, so per-feature destroy is safe.)
                    OGRMultiPolygon mp;
                    ol->ResetReading();
                    OGRFeature *f = nullptr;
                    qint64 nRead = 0;
                    while ((f = ol->GetNextFeature()) != nullptr)
                    {
                        const OGRGeometry *geom = f->GetGeometryRef();
                        if (geom)
                        {
                            const auto gt = wkbFlatten(geom->getGeometryType());
                            if (gt == wkbPolygon)
                                mp.addGeometry(geom);
                            else if (gt == wkbMultiPolygon)
                            {
                                const auto *mpSrc = geom->toMultiPolygon();
                                for (int i = 0; i < mpSrc->getNumGeometries(); ++i)
                                    mp.addGeometry(mpSrc->getGeometryRef(i));
                            }
                        }
                        OGRFeature::DestroyFeature(f);
                        if (((++nRead) & 0xFFF) == 0 && promise.isCanceled())
                        { cancelled = true; break; }
                    }
                    stageMark("boundary: feature read");

                    if (!cancelled)
                    {
                        // UnaryUnion is a single uninterruptible GEOS call;
                        // Stop takes effect at the next check.
                        OGRGeometry *dissolved = mp.UnaryUnion();
                        stageMark("boundary: UnaryUnion");
                        if (dissolved)
                        {
                            walkOgrGeom(dissolved);
                            OGRGeometryFactory::destroyGeometry(dissolved);
                        }
                        else
                        {
                            // Fallback: walk collected polygons without dissolve.
                            for (int i = 0; i < mp.getNumGeometries(); ++i)
                                walkOgrGeom(mp.getGeometryRef(i));
                        }
                    }
                }
                GDALClose(ds);
            }
        }
        // BoundaryKind::AutoBBox falls through to the margin box below.

        if (boundaryCT) OGRCoordinateTransformation::DestroyCT(boundaryCT);
        if (cancelled || promise.isCanceled())
        { fail(QObject::tr("Cancelled.")); return; }

        if (nBoundaryXformFailed > 0)
        {
            // Those vertices were skipped above, so continuing would mesh a
            // domain whose outline differs from the layer the user picked —
            // silently, and in a way nothing downstream could detect.
            fail(QObject::tr(
                "%1 boundary vertices could not be reprojected from the "
                "boundary layer's CRS to the mesh CRS.\n"
                "The meshing domain would not match the boundary layer, so "
                "generation was stopped. This usually means the two CRSs do "
                "not overlap, the boundary extends outside the projection's "
                "domain of validity, or a required datum grid is not "
                "installed.").arg(nBoundaryXformFailed));
            return;
        }

        if (in.domains.isEmpty())
        {
            const double m = 0.05;
            const MapExtent &me = in.modelExtent;
            const double mdx = me.width() * m, mdy = me.height() * m;
            QPolygonF box;
            box << QPointF(me.xMin()-mdx, me.yMin()-mdy)
                << QPointF(me.xMax()+mdx, me.yMin()-mdy)
                << QPointF(me.xMax()+mdx, me.yMax()+mdy)
                << QPointF(me.xMin()-mdx, me.yMax()+mdy);
            in.domains.append(box);
        }
        stageMark("boundary ingestion");
    }
    }   // if (!bprepReady)

    // ── Candidate filtering + marker assignment (worker-side) ────────
    // Mirrors the original collectInputs sequence exactly — junctions →
    // conduits → aux points → aux lines → region markers → snapAndDedupe —
    // so PSLG marker numbering (from 100) is unchanged.
    progress(12, QObject::tr("Filtering features against the domain…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
    {
        mesh::pslg::PointInRingsIndex domIdx;
        domIdx.build(in.domains);
        auto inDomain = [&domIdx](const QPointF &p) { return domIdx.contains(p); };

        auto dedupeSegPath = [](const QVector<QPointF> &src) {
            QVector<QPointF> r;
            r.reserve(src.size());
            for (const QPointF &p : src)
                if (r.isEmpty() || (p - r.last()).manhattanLength() > 1e-9)
                    r.append(p);
            return r;
        };

        // Strip intermediate vertices that lie outside the domain POLYGON.
        // An unconstrained intermediate vertex outside the domain would
        // make the PSLG non-planar and abort Triangle; endpoint filtering
        // below is the primary guard, this strips runaway interior vertices.
        // The test must be against the ring, not its bounding box: on a
        // non-rectangular (e.g. DEM-footprint) domain a link whose middle
        // bulges outside the polygon while staying inside the bbox would
        // otherwise carry segments that cross the boundary ring — the exact
        // "non-planar PSLG" Triangle aborts on.
        auto clipIntermediateToDomain = [&](const QVector<QPointF> &path) {
            if (path.size() <= 2) return path;
            QVector<QPointF> r;
            r.reserve(path.size());
            r.append(path.first());
            for (int k = 1; k < path.size()-1; ++k)
                if (inDomain(path[k])) r.append(path[k]);
            r.append(path.last());
            return dedupeSegPath(r);
        };

        const bool haveDTM = !in.dtmPath.isEmpty();
        int nextMarker = 100;

        if (in.includeJunctions)
        {
            // In-domain candidates first (order preserved = category order:
            // junctions → outfalls → storage → dividers, model row order).
            QVector<int>     nodeIdx;
            QVector<QPointF> nodeXY;
            nodeIdx.reserve(in.candidateNodes.size());
            nodeXY.reserve(in.candidateNodes.size());
            for (int c = 0; c < in.candidateNodes.size(); ++c)
            {
                // Skip nodes outside the meshing domain — Triangle ignores
                // them anyway but filtering early shrinks the PSLG.
                if (!inDomain(in.candidateNodes[c].xy)) continue;
                nodeIdx.append(c);
                nodeXY.append(in.candidateNodes[c].xy);
            }

            // Minimum node separation: a candidate within minSep of an
            // already-kept node is NOT pinned as a mesh vertex (two pinned
            // vertices centimetres apart force tiny triangles in the initial
            // constrained triangulation, before any quality pass can act).
            // Demoted nodes stay in couplingNodes, so the post-generation
            // mapper couples them via their containing CELL instead.
            //
            // Under V2 enforcement the separation is at least h: two pinned
            // nodes closer than the minimum cell size are exactly what the
            // conditioner downstream would otherwise be asked to fix, and
            // demotion here is the cheaper, already-proven answer.
            const double effNodeSep =
                (in.minSizeEnforce && in.minSizePolicy.enabled())
                    ? std::max(in.nodeMinSeparation,
                               in.minSizePolicy.minCellSize)
                    : in.nodeMinSeparation;
            const QVector<bool> keepNode =
                mesh::pslg::greedyMinSeparation(nodeXY, effNodeSep);

            int demoted = 0;
            for (int k = 0; k < nodeIdx.size(); ++k)
            {
                if (!keepNode[k]) { ++demoted; continue; }
                const auto &cand = in.candidateNodes[nodeIdx[k]];
                mesh::SteinerPoint sp;
                sp.xy = cand.xy; sp.marker = nextMarker; sp.tag = cand.name;
                // Rim usage:  useRim → pin vertex to rim (+ flatten list);
                // no DTM → rim is the only elevation source (IDW seed).
                if (cand.hasRim && (in.nodesUseRim || !haveDTM))
                {
                    sp.z    = cand.rimZ;
                    sp.hasZ = true;
                }
                if (cand.hasRim && in.nodesUseRim)
                {
                    in.nodeRimXY.append(cand.xy);
                    in.nodeRimZ.append(cand.rimZ);
                }
                in.steinerPoints.append(sp);
                in.nodeMarkerToTag.insert(nextMarker, cand.name);
                ++nextMarker;
            }
            if (demoted > 0)
                qCInfo(lcMeshPerf) << "[Mesh] node min separation"
                                   << effNodeSep << "-"
                                   << demoted << "node(s) demoted to cell coupling,"
                                   << (nodeIdx.size() - demoted) << "pinned as vertices";
        }

        if (in.includeConduits)
        {
            for (const auto &link : std::as_const(in.candidateLinks))
            {
                // Dedupe then simplify — RDP removes nearly-collinear
                // intermediate vertices from GIS-digitised alignments.
                QVector<QPointF> path = simplifyPolyline(
                    clipIntermediateToDomain(dedupeSegPath(link.second)),
                    in.pslgSimplifyEps);
                if (path.size() < 2) continue;
                // Both endpoints must be inside the domain polygon — a link
                // crossing the boundary without a vertex at the crossing
                // makes the PSLG non-planar and aborts Triangle.
                if (!inDomain(path.first()) || !inDomain(path.last())) continue;
                mesh::ConstraintSegment cs;
                cs.path = std::move(path); cs.marker = nextMarker; cs.tag = link.first;
                in.constraintSegs.append(cs);
                in.edgeMarkerToTag.insert(nextMarker, link.first);
                ++nextMarker;
            }
        }

        for (const auto &ap : std::as_const(in.auxPoints))
        {
            if (!inDomain(ap.xy)) continue;
            if (ap.hasZ)
            {
                mesh::SteinerPoint sp;
                sp.xy = ap.xy; sp.z = ap.z; sp.hasZ = true;
                in.steinerPoints.append(sp);
            }
            else
            {
                in.steinerPoints.append({ap.xy, 0, {}});
            }
        }

        for (const auto &al : std::as_const(in.auxLines))
        {
            // Seed elevCache by coordinate from the RAW (pre-simplify)
            // vertices so PSLG simplification can never desync z.
            if (al.hasZ)
                for (int j = 0; j < al.path.size(); ++j)
                {
                    if (!std::isfinite(al.z[j])) continue;
                    if (!inDomain(al.path[j])) continue;
                    in.featureZSeedXY.append(al.path[j]);
                    in.featureZSeedZ.append(al.z[j]);
                }
            QVector<QPointF> path = simplifyPolyline(
                clipIntermediateToDomain(dedupeSegPath(al.path)),
                in.pslgSimplifyEps);
            if (path.size() < 2) continue;
            // Same planar-graph rule: both endpoints inside the domain.
            if (inDomain(path.first()) && inDomain(path.last()))
                in.constraintSegs.append({std::move(path), 0, {}});
        }

        if (in.includeSubcatch)
        {
            for (const auto &sc : std::as_const(in.subcatchSeeds))
            {
                mesh::RegionMarker rm;
                rm.xy        = sc.second;
                rm.attribute = nextMarker;
                rm.tag       = QStringLiteral("subcatch_%1").arg(sc.first);
                in.regionMarkers.append(rm);
                ++nextMarker;
            }
        }

        // Merge near-coincident untagged Steiner points from different
        // sources; tagged SWMM points (marker != 0) are never merged.
        snapAndDedupe(in.steinerPoints, in.pslgSnapEps);
        stageMark("candidate filtering + markers");
    }

    // ── Minimum cell size conditioning ──────────────────────────────
    // MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17 §4.  Runs HERE, after markers
    // exist (so tagged identity can be honoured as weld priority) and before
    // hole-ring prep (so prepareHoleRings' own validation independently
    // re-checks conditioned rings).  Terrain Steiner sampling happens later
    // and its near-constraint filter reads the conditioned geometry, so DTM
    // points are automatically kept clear of the conditioned features.
    //
    // Diagnostics run whenever a size is set, even if conditioning is
    // subsequently abandoned: knowing WHERE the input cannot hold cells of
    // size h is the actionable part.
    if (in.minSizePolicy.enabled())
    {
        progress(12, QObject::tr("Conditioning geometry for minimum cell size…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

        const double h = in.minSizePolicy.minCellSize;
        {
            const QVector<mesh::pslg::Violation> before =
                mesh::pslg::analyseLocalFeatureSize(
                    in.domains, in.holeRings, in.constraintSegs,
                    in.steinerPoints, h, 20);
            qCInfo(lcMeshPerf) << "[Mesh][minsize] h =" << h
                               << "| worst input feature scale"
                               << (before.isEmpty() ? h : before.first().lfs)
                               << "|" << before.size() << "violation(s) sampled";
            for (const mesh::pslg::Violation &v : before)
                qCDebug(lcMeshPerf) << "[Mesh][minsize]  input"
                                    << mesh::pslg::violationCauseName(v.cause)
                                    << v.lfs << "at" << v.xy
                                    << v.tagA << v.tagB;
        }

        // On a boundary-cache HIT the rings are already prepared (and were
        // conditioned by the run that stored them, since minCellSize is part
        // of the cache key).  Their seeds and validity flags were computed
        // against those exact vertices, so the rings must stay byte-identical
        // here — they take part as proximity context only.
        mesh::pslg::MinSizePolicy pol = in.minSizePolicy;
        pol.ringsReadOnly = bprepReady;

        mesh::pslg::ConditionReport crep;
        const bool ok = mesh::pslg::conditionMinSize(
            &in.domains, &in.holeRings, &in.constraintSegs, &in.steinerPoints,
            pol, &crep, [&promise] { return promise.isCanceled(); });

        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

        if (ok)
        {
            qCInfo(lcMeshPerf) << "[Mesh][minsize]" << crep.summary();
            if (crep.holesDropped > 0)
                qWarning() << "[Mesh][minsize] dropped" << crep.holesDropped
                           << "sub-scale hole ring(s) — the mesh now COVERS "
                              "those regions.";
            if (crep.duplicateSegments > 0)
                qWarning() << "[Mesh][minsize]" << crep.duplicateSegments
                           << "constrained segment(s) were welded onto geometry "
                              "another alignment already occupied — Triangle "
                              "keeps one, so that many edges lose their marker "
                              "and the conduit tag it carried.";
            if (crep.domainAreaBefore != crep.domainAreaAfter)
                qCInfo(lcMeshPerf) << "[Mesh][minsize] domain area"
                                   << crep.domainAreaBefore << "->"
                                   << crep.domainAreaAfter
                                   << "(boundary vertices moved by up to"
                                   << crep.maxDisplacement << ")";
            progress(13, QObject::tr("Geometry conditioned (%1 merged, "
                                     "%2 split, %3 corner(s) trimmed)")
                             .arg(crep.verticesWelded)
                             .arg(crep.segmentsSplit)
                             .arg(crep.cornersTrimmed));
        }
        else
        {
            // Fail-safe: the PSLG was restored untouched.  A slow correct mesh
            // beats a Triangle abort, so carry on unconditioned and say so.
            qWarning() << "[Mesh][minsize] conditioning abandoned —"
                       << crep.summary()
                       << "- generating with the original geometry.";
            progress(13, QObject::tr("Minimum-size conditioning skipped "
                                     "(geometry could not be conditioned safely)"));
        }
        for (const mesh::pslg::Violation &v : std::as_const(crep.residuals))
            qCDebug(lcMeshPerf) << "[Mesh][minsize]  residual"
                                << mesh::pslg::violationCauseName(v.cause)
                                << v.lfs << "at" << v.xy << v.tagA << v.tagB;
        stageMark("minimum cell size conditioning");
    }

    // ── Domain + holes ──────────────────────────────────────────────
    progress(14, QObject::tr("Building input PSLG…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    mesh::MeshGenerator g;
    g.setDomains(in.domains);

    // Interior rings → constraint segments (hole boundary edges) + a seed
    // point that tells Triangle to leave the region unmeshed.  Rings arrive
    // RAW from the boundary ingestion above; simplification, validation (on
    // the small simplified ring — see pslgprep.h), densification, and the
    // interior seed are computed in parallel chunks — or come straight from
    // the Stage-A cache on a hit.
    if (!bprepReady)
    {
        QVector<mesh::pslg::PreparedRing> prepared;
        int skippedRings = 0;
        const bool holesDone = mesh::pslg::prepareHoleRings(
            in.holeRings, in.pslgSimplifyEps, in.maxBoundaryEdgeLen, &prepared,
            [&promise] { return promise.isCanceled(); },
            [&](int done, int total) {
                promise.setProgressValueAndText(
                    14 + static_cast<int>(5.0 * done / std::max(total, 1)),
                    QObject::tr("Preparing hole rings… (%1 / %2)")
                        .arg(done).arg(total));
            },
            &skippedRings);
        if (!holesDone) { fail(QObject::tr("Cancelled.")); return; }

        bprep.domains = in.domains;
        bprep.holeRings.reserve(prepared.size());
        bprep.holeSeeds.reserve(prepared.size());
        bprep.holeValid.reserve(prepared.size());
        for (int k = 0; k < prepared.size(); ++k)
        {
            // Downstream consumers (PIP band index, constraint-segment hash)
            // must see the same densified geometry the PSLG uses.
            in.holeRings[k] = prepared[k].ring;
            bprep.holeRings.append(prepared[k].ring);
            bprep.holeSeeds.append(prepared[k].seed);
            bprep.holeValid.append(prepared[k].valid);
        }
        bprep.skippedRings = skippedRings;
        stageMark("hole ring prep");

        if (!boundaryCacheKey.isEmpty())
        {
            QElapsedTimer storeClock;
            storeClock.start();
            if (cache.storeBoundary(boundaryCacheKey, bprep))
                qCInfo(lcMeshPerf) << "[Mesh][cache] boundary prep stored"
                                   << boundaryCacheKey.left(12).constData()
                                   << "in" << storeClock.elapsed() << "ms";
        }
    }

    for (int k = 0; k < bprep.holeRings.size(); ++k)
    {
        // Invalid rings would break the PSLG (self-intersecting or
        // degenerate) — skip them so a bad hole produces a logged skip
        // rather than a generic Triangle abort failing the whole mesh.
        if (!bprep.holeValid[k]) continue;

        // Boundary edges of the hole must exist in the PSLG…
        mesh::ConstraintSegment cs;
        cs.path   = bprep.holeRings[k];
        cs.marker = 0;
        g.addConstraintSegment(cs);

        // …plus a seed point strictly inside the ring so Triangle carves
        // it.  interiorPoint() is robust for non-convex rings; a vertex
        // centroid could fall outside the ring (or in another region),
        // silently leaving the hole unmeshed or removing the wrong area.
        g.addHole(bprep.holeSeeds[k]);
    }
    if (bprep.skippedRings > 0)
        qWarning() << "[Mesh] Skipped" << bprep.skippedRings
                   << "invalid hole ring(s) — self-intersecting or degenerate.";

    for (const auto &cs : std::as_const(in.constraintSegs))
        g.addConstraintSegment(cs);

    // ── PSLG quad regions (QUAD_MESHING_REDESIGN_PLAN_2026-09-06 §3) ──
    // Layer regions are read HERE with a fresh GDAL handle, exactly as the
    // boundary layer is (handles must not cross threads); subcatchment
    // regions arrive resolved from collectInputs and are appended after
    // them.  Geometry only — the generator validates, classifies Auto, drops
    // terrain Steiners inside Free rings and pairs / cleans / smooths
    // (g.addQuadRegion below).
    QVector<mesh::QuadRegion> quadRegions;
    if (!in.quadRegionLayers.isEmpty())
    {
        progress(19, QObject::tr("Reading quad region polygons…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        qsizetype nRegionXformFailed = 0;
        for (const auto &spec : std::as_const(in.quadRegionLayers))
        {
            OGRCoordinateTransformation *regionCT = nullptr;  // region layer → mesh CRS
            if (!spec.crsWkt.isEmpty() && !in.meshCRSWkt.isEmpty())
            {
                OGRSpatialReference rSRS, mSRS;
                if (rSRS.importFromWkt(spec.crsWkt.toUtf8().constData()) == OGRERR_NONE
                    && mSRS.importFromWkt(in.meshCRSWkt.toUtf8().constData()) == OGRERR_NONE)
                {
                    rSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    mSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    if (!mSRS.IsSame(&rSRS))
                        regionCT = OGRCreateCoordinateTransformation(&rSRS, &mSRS);
                }
            }

            GDALDataset *ds = GDALDataset::Open(
                spec.path.toUtf8().constData(),
                GDAL_OF_VECTOR | GDAL_OF_READONLY);
            if (!ds)
            {
                qWarning() << "[Mesh][quad] region layer open failed:" << spec.path
                           << "— no quad regions from this layer.";
                if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
                continue;
            }
            OGRLayer *ol = spec.layerName.isEmpty()
                               ? ds->GetLayer(0)
                               : ds->GetLayerByName(spec.layerName.toUtf8().constData());
            if (!ol)
            {
                qWarning() << "[Mesh][quad] region layer not found:" << spec.layerName;
                GDALClose(ds);
                if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
                continue;
            }

            // Exterior ring only → mesh CRS → RDP with the same simplifier the
            // domain rings use → QuadRegion carrying the dialog defaults, then
            // the optional per-feature attribute overrides (case-insensitive
            // field names; OGR's GetFieldIndex already ignores case).
            auto pushRegion = [&](const OGRPolygon *poly, const OGRFeature *f) {
                if (!poly) return;
                const OGRLinearRing *ext = poly->getExteriorRing();
                if (!ext || ext->getNumPoints() < 3) return;
                const int n = ext->getNumPoints();
                QVector<double> xs(n), ys(n);
                for (int i = 0; i < n; ++i) { xs[i] = ext->getX(i); ys[i] = ext->getY(i); }
                if (regionCT)
                    nRegionXformFailed += transformChecked(regionCT, n, xs.data(), ys.data());
                QVector<QPointF> pts;
                pts.reserve(n);
                for (int i = 0; i < n; ++i)
                    if (std::isfinite(xs[i]) && std::isfinite(ys[i]))
                        pts.append(QPointF(xs[i], ys[i]));
                if (pts.size() < 3) return;

                mesh::QuadRegion r = in.quadRegionDefaults;
                r.ring = QPolygonF(simplifyRing(pts, in.pslgSimplifyEps));

                auto fieldIdx = [f](const char *name) -> int {
                    const int i = f->GetFieldIndex(name);
                    return (i >= 0 && f->IsFieldSetAndNotNull(i)) ? i : -1;
                };
                if (const int i = fieldIdx("quad_mode"); i >= 0)
                {
                    const QString s = QString::fromUtf8(f->GetFieldAsString(i));
                    if (!parseQuadRegionMode(s, &r.mode))
                        qWarning() << "[Mesh][quad] unknown quad_mode" << s
                                   << "on feature" << qint64(f->GetFID())
                                   << "— using the dialog default";
                }
                if (const int i = fieldIdx("quad_spacing"); i >= 0) r.spacing   = f->GetFieldAsDouble(i);
                if (const int i = fieldIdx("quad_aspect");  i >= 0) r.aspectMax = f->GetFieldAsDouble(i);
                if (const int i = fieldIdx("quad_angle");   i >= 0)
                {
                    r.hasAlignAngle = true;
                    r.alignAngleDeg = f->GetFieldAsDouble(i);
                }
                if      (const int i = fieldIdx("tag");  i >= 0) r.tag = QString::fromUtf8(f->GetFieldAsString(i));
                else if (const int j = fieldIdx("name"); j >= 0) r.tag = QString::fromUtf8(f->GetFieldAsString(j));
                quadRegions.append(std::move(r));
            };

            ol->ResetReading();
            OGRFeature *f = nullptr;
            bool cancelled = false;
            while ((f = ol->GetNextFeature()) != nullptr)
            {
                if (const OGRGeometry *geom = f->GetGeometryRef())
                {
                    const auto gt = wkbFlatten(geom->getGeometryType());
                    if (gt == wkbPolygon)
                        pushRegion(geom->toPolygon(), f);
                    else if (gt == wkbMultiPolygon)
                    {
                        const auto *mp = geom->toMultiPolygon();
                        for (int i = 0; i < mp->getNumGeometries(); ++i)
                            pushRegion(mp->getGeometryRef(i)->toPolygon(), f);
                    }
                }
                OGRFeature::DestroyFeature(f);
                if (promise.isCanceled()) { cancelled = true; break; }
            }
            GDALClose(ds);
            if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
            if (cancelled) { fail(QObject::tr("Cancelled.")); return; }
        }
        if (nRegionXformFailed > 0)
        {
            // Same rule as the boundary: a ring with dropped vertices is a
            // different region from the one the user drew.
            fail(QObject::tr(
                "%1 quad region vertices could not be reprojected from the "
                "region layer's CRS to the mesh CRS, so generation was "
                "stopped.").arg(nRegionXformFailed));
            return;
        }
        stageMark("quad region layer read");
    }
    const int nLayerQuadRegions = quadRegions.size();
    // Subcatchment regions, after the layer ones; same RDP pass (plan §3.2).
    for (const mesh::QuadRegion &src : std::as_const(in.quadRegions))
    {
        mesh::QuadRegion r = src;
        r.ring = QPolygonF(simplifyRing(src.ring, in.pslgSimplifyEps));
        quadRegions.append(std::move(r));
    }
    // ── Quads everywhere (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.1) ──────
    // The toggle needs no polygon: each domain ring becomes a BACKGROUND
    // region covering everything the explicit regions did not claim. Holes
    // are the domain's own hole rings plus every explicit region's ring, so
    // an explicit region keeps its own mode/spacing and the background fills
    // around it. Only the worker can build this — the generator is given
    // hole SEED POINTS, never hole rings.
    const int nExplicitQuadRegions = quadRegions.size();
    if (in.quadEverywhere)
    {
        QVector<QPolygonF> explicitRings;
        explicitRings.reserve(quadRegions.size());
        for (const mesh::QuadRegion &r : std::as_const(quadRegions))
            explicitRings.append(r.ring);

        for (const QPolygonF &dom : std::as_const(in.domains))
        {
            if (dom.size() < 3) continue;
            mesh::QuadRegion bg;
            bg.ring         = dom;
            bg.isBackground = true;
            bg.mode         = mesh::QuadRegionMode::Free;
            bg.spacing      = in.quadEverywhereSpacing;   // 0 = follow the size field
            bg.aspectMax    = in.quadRegionDefaults.aspectMax;
            for (const auto &hr : std::as_const(in.holeRings))
            {
                const QPolygonF h(hr);
                if (h.size() >= 3 && mesh::pointInRing(dom, h.first())) bg.holes.append(h);
            }
            for (const QPolygonF &er : std::as_const(explicitRings))
                if (er.size() >= 3 && mesh::pointInRing(dom, er.first())) bg.holes.append(er);
            quadRegions.append(std::move(bg));
        }
    }

    if (!quadRegions.isEmpty())
        qCInfo(lcMeshPerf).nospace()
            << "[Mesh][quad] " << quadRegions.size() << " regions ("
            << nLayerQuadRegions << " from layers, " << in.quadRegions.size()
            << " from subcatchments, "
            << (quadRegions.size() - nExplicitQuadRegions) << " background; default mode "
            << quadRegionModeName(in.quadRegionDefaults.mode)
            << ", spacing " << in.quadRegionDefaults.spacing
            << ", aspect <= " << in.quadRegionDefaults.aspectMax << ")";

    // Per-region area bounds are clamped to the refinement floor.
    //
    // This matters more than it looks.  Triangle honours regionlist area
    // bounds only when its `vararea` flag is set, and that flag is set only by
    // a BARE `a` switch — so today, with a numeric `a<maxArea>` emitted,
    // RegionMarker::maxArea is silently inert.  Installing the size-function
    // hook below drops the numeric switch and therefore switches region bounds
    // ON for the first time.  Without this clamp a region could ask for cells
    // below the floor the user just set.
    const double areaFloor = in.minSizePolicy.enabled()
                                 ? in.minSizePolicy.minTriangleArea()
                                 : 0.0;
    int nRegionClamped = 0;
    for (const auto &rm : std::as_const(in.regionMarkers))
    {
        mesh::RegionMarker r = rm;
        if (areaFloor > 0.0 && r.maxArea > 0.0 && r.maxArea < areaFloor)
        {
            r.maxArea = areaFloor;
            ++nRegionClamped;
        }
        g.addRegion(r);
    }
    if (nRegionClamped > 0)
        qCInfo(lcMeshPerf) << "[Mesh][minsize] clamped" << nRegionClamped
                           << "region area bound(s) up to the floor" << areaFloor;
    {
        // The G2 tri-pair merge runs in this worker after the elevation fill
        // (its bed-planarity test needs sampled z), not inside generate().
        mesh::GenerationOptions go = in.genOpts;
        go.mergeTrianglePairs = false;
        // Quad regions: acceptance bounds from the dialog; a Free region with
        // spacing 0 and no size function takes the side of the equilateral
        // triangle of maxArea (0 = none → the generator skips the region
        // with a report line).  quadCleanup keeps its defaults (no UI).
        go.quadRegionBounds = in.quadBounds;
        go.quadRegionDefaultSpacing = (in.genOpts.maxArea > 0.0)
            ? std::sqrt(4.0 * in.genOpts.maxArea / std::sqrt(3.0))
            : 0.0;
        g.setOptions(go);
    }
    // G3 structured patches: boundary → PSLG constraints, interior → hole,
    // quads appended after the triangles by generate().
    for (const mesh::PatchMesh &pm : std::as_const(in.patches))
        g.addPatch(pm);
    // PSLG quad regions (layer regions first, then subcatchments — the order
    // quadRegionReports() is indexed in).
    for (const mesh::QuadRegion &qr : std::as_const(quadRegions))
        g.addQuadRegion(qr);

    // ── DTM (optional) — open once, shared for all elevation sampling ──
    // The DEM drives three steps: feature z-interpolation, terrain
    // thinning / grid sampling, and post-Triangle vertex elevation fill.
    // A single DTMThinner instance covers all three so the file is only
    // opened once and the same bilinear sampler is used throughout.
    //
    // When no DTM is provided, vertex z is filled by inverse-distance
    // interpolation from junction rim elevations carried on the SWMM
    // node Steiner points (invert + maxDepth, set in collectInputs).
    const bool useDTM = !in.dtmPath.isEmpty();

    mesh::DTMThinner thinner;
    OGRCoordinateTransformation *meshToDTM = nullptr;
    OGRCoordinateTransformation *dtmToMesh = nullptr;

    if (useDTM)
    {
        progress(20, QObject::tr("Opening DTM raster…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

        if (!thinner.open(in.dtmPath))
        {
            fail(QObject::tr("DTM open failed: %1").arg(thinner.errorMsg()));
            return;
        }

        // CRS transforms shared by all DTM sampling below.
        // GDAL 3 changed the default axis order for geographic CRSs to the
        // ISO/OGC standard (lat-first for EPSG:4326).  Force traditional
        // GIS order (x=east/lon, y=north/lat) on every SRS object we create
        // so that coordinate transforms and IsSame() behave consistently.
        const QString dtmCRSWkt = thinner.crsWkt();
        qCDebug(lcMeshPerf) << "[CRS] meshCRSWkt empty:" << in.meshCRSWkt.isEmpty()
                 << "| dtmCRSWkt empty:" << dtmCRSWkt.isEmpty();
        if (!in.meshCRSWkt.isEmpty() && !dtmCRSWkt.isEmpty())
        {
            OGRSpatialReference mSRS, dSRS;
            mSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            dSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (mSRS.importFromWkt(in.meshCRSWkt.toUtf8().constData()) == OGRERR_NONE &&
                dSRS.importFromWkt(dtmCRSWkt.toUtf8().constData())  == OGRERR_NONE)
            {
                const bool same = mSRS.IsSame(&dSRS);
                qCDebug(lcMeshPerf) << "[CRS] IsSame:" << same;
                if (!same)
                {
                    meshToDTM = OGRCreateCoordinateTransformation(&mSRS, &dSRS);
                    dtmToMesh = OGRCreateCoordinateTransformation(&dSRS, &mSRS);
                    qCDebug(lcMeshPerf) << "[CRS] transforms created:"
                             << "meshToDTM=" << (meshToDTM?"OK":"FAIL")
                             << "dtmToMesh=" << (dtmToMesh?"OK":"FAIL");
                }
            }
        }
    }
    else
    {
        progress(20, QObject::tr("Using junction rim elevations (no DTM selected)…"));
    }

    // elevCache — keeps exact z values for every point we place as a
    // Steiner vertex.  Keyed by quantised mesh-CRS (x,y) at 1e7 precision.
    // The post-Triangle elevation loop consults this first so those vertices
    // are never re-sampled.
    QHash<QPair<qint64,qint64>, double> elevCache;

    // Flat seed arrays for IDW fall-back when no DTM is supplied. Populated
    // alongside elevCache below — only steiner points with known z become
    // seeds.
    QVector<QPointF> seedXY;
    QVector<double>  seedZ;

    // Provenance: keys (same quantisation as elevCache) whose z is already in
    // model/mesh vertical units (rim, feature Z, flattened terrain).  These
    // are excluded from the zConversionFactor multiply, which only applies to
    // raster-unit DTM samples.
    QSet<QPair<qint64,qint64>> modelUnitKeys;
    auto keyOf = [](double x, double y) {
        return qMakePair(qRound64(x * 1e7), qRound64(y * 1e7));
    };

    // 3D aux-line vertices: exact model-unit z seeded by coordinate.  Aux
    // points travel through in.steinerPoints (handled in Step 1); lines only
    // here — no double-seeding.
    for (int i = 0; i < in.featureZSeedXY.size(); ++i)
    {
        const QPointF &p = in.featureZSeedXY[i];
        const auto k = keyOf(p.x(), p.y());
        elevCache.insert(k, in.featureZSeedZ[i]);
        modelUnitKeys.insert(k);
        seedXY.append(p);
        seedZ .append(in.featureZSeedZ[i]);
    }

    // ── Node rim-flatten spatial hash ─────────────────────────────────
    // When nodes use rim elevation and a flatten radius is set, terrain and
    // refinement vertices within radius of a node are forced to that node's
    // rim z.  Spatial hash with cell size = radius (mirrors the Poisson-disk
    // grid below); a 3×3 neighbour scan is guaranteed to find any node within
    // the radius.  flattenZ() returns the nearest in-range rim z, or NaN.
    const bool   doFlatten = in.nodesUseRim && in.nodeFlattenRadius > 0.0
                             && !in.nodeRimXY.isEmpty();
    const double flatR2  = in.nodeFlattenRadius * in.nodeFlattenRadius;
    const double invFlat = doFlatten ? 1.0 / in.nodeFlattenRadius : 0.0;
    QHash<QPair<qint32,qint32>, QVector<int>> flatGrid;
    if (doFlatten)
    {
        flatGrid.reserve(in.nodeRimXY.size());
        for (int n = 0; n < in.nodeRimXY.size(); ++n)
        {
            const qint32 cx = qint32(std::floor(in.nodeRimXY[n].x() * invFlat));
            const qint32 cy = qint32(std::floor(in.nodeRimXY[n].y() * invFlat));
            flatGrid[qMakePair(cx, cy)].append(n);
        }
    }
    auto flattenZ = [&](double px, double py) -> double {
        if (!doFlatten) return std::numeric_limits<double>::quiet_NaN();
        const qint32 cx = qint32(std::floor(px * invFlat));
        const qint32 cy = qint32(std::floor(py * invFlat));
        double best2 = flatR2;
        double bestZ = std::numeric_limits<double>::quiet_NaN();
        for (qint32 dy = -1; dy <= 1; ++dy)
            for (qint32 dx = -1; dx <= 1; ++dx)
            {
                auto it = flatGrid.constFind(qMakePair(cx + dx, cy + dy));
                if (it == flatGrid.constEnd()) continue;
                for (const int n : *it)
                {
                    const double ex = in.nodeRimXY[n].x() - px;
                    const double ey = in.nodeRimXY[n].y() - py;
                    const double d2 = ex*ex + ey*ey;
                    if (d2 <= best2) { best2 = d2; bestZ = in.nodeRimZ[n]; }
                }
            }
        return bestZ;
    };

    // ── Step 1: feature Steiner points — assign z from DEM or model ──
    // SWMM nodes, conduit vertices, aux-layer points, etc.  Their (x,y)
    // is already in mesh CRS; we transform to DTM CRS to sample, then
    // store back in mesh CRS.  When no DTM is selected we keep whatever
    // z the input already carries (junctions arrive with rim z set).
    progress(25, useDTM
                 ? QObject::tr("Interpolating feature elevations from DTM…")
                 : QObject::tr("Reading junction rim elevations…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    // Batch DTM sampling for the points that need a z: one CRS transform and
    // one banded read instead of a Transform(1,…) + 2×2 RasterIO per point.
    // The consuming loop below advances through featureZ under the exact same
    // predicate used to collect the batch, so pairing is positional.
    QVector<double> featureZ;
    if (useDTM)
    {
        QVector<double> fxs, fys;
        for (const auto &sp0 : std::as_const(in.steinerPoints))
            if (!sp0.hasZ)
            {
                fxs.append(sp0.xy.x());
                fys.append(sp0.xy.y());
            }
        if (!fxs.isEmpty())
        {
            // A point PROJ cannot convert becomes NaN, which sampleMany()
            // rejects (dtmthinner.cpp:450) and answers with a NaN z. That is
            // already handled: the consumer below only accepts a finite z.
            // Log the count so a systematically bad CRS pairing is visible
            // rather than looking like a DEM with no coverage.
            const qsizetype nFail = transformChecked(meshToDTM, fxs.size(),
                                                     fxs.data(), fys.data());
            if (nFail > 0)
                qCWarning(lcMeshPerf)
                    << "[Mesh]" << nFail << "of" << fxs.size()
                    << "feature points failed mesh->DTM reprojection;"
                    << "they get no DTM elevation";
            QVector<QPointF> q;
            q.reserve(fxs.size());
            for (qsizetype k = 0; k < fxs.size(); ++k)
                q.append(QPointF(fxs[k], fys[k]));
            thinner.sampleMany(q, &featureZ);
        }
    }

    qsizetype featureZPos = 0;
    for (const auto &sp0 : std::as_const(in.steinerPoints))
    {
        mesh::SteinerPoint sp = sp0;
        // wasPreset: z arrived in model units (rim / feature Z); a value
        // sampled from the DTM below is in raster units instead.
        const bool wasPreset = sp.hasZ;
        if (!sp.hasZ && useDTM)
        {
            const double z = featureZ[featureZPos++];
            if (std::isfinite(z)) { sp.z = z; sp.hasZ = true; }
        }
        if (sp.hasZ)
        {
            const auto k = keyOf(sp.xy.x(), sp.xy.y());
            elevCache.insert(k, sp.z);
            if (wasPreset) modelUnitKeys.insert(k);
            seedXY.append(sp.xy);
            seedZ .append(sp.z);
        }
        g.addSteinerPoint(sp);
    }

    // ── Step 2: DTM terrain points — thinning or full-grid sampling ──
    // Domain bounding box in mesh CRS → transform corners to DTM CRS.
    // Skipped entirely when no DTM is supplied; in that mode the only
    // vertices added to the PSLG come from steiner features + Triangle's
    // own refinement, and z is filled later via IDW from seedXY/seedZ.
    double bx0 = std::numeric_limits<double>::max(),  by0 = bx0;
    double bx1 = std::numeric_limits<double>::lowest(), by1 = bx1;
    for (const auto &dom : std::as_const(in.domains))
        for (const auto &p : dom)
        {
            if (p.x() < bx0) bx0 = p.x(); if (p.x() > bx1) bx1 = p.x();
            if (p.y() < by0) by0 = p.y(); if (p.y() > by1) by1 = p.y();
        }

    qCDebug(lcMeshPerf) << "[Mesh] domain bbox (mesh CRS):"
             << bx0 << by0 << "--" << bx1 << by1;
    if (useDTM)
        qCDebug(lcMeshPerf) << "[Mesh] DTM pixelSize:" << thinner.pixelSize()
                 << "| CRS wkt present:" << !thinner.crsWkt().isEmpty()
                 << "| meshToDTM:" << (meshToDTM ? "YES" : "NO");

    if (useDTM && bx0 < bx1 && by0 < by1)
    {
        double dx0 = bx0, dy0 = by0, dx1 = bx1, dy1 = by1;
        if (meshToDTM)
        {
            double xs[4] = {bx0, bx1, bx0, bx1};
            double ys[4] = {by0, by0, by1, by1};
            if (transformChecked(meshToDTM, 4, xs, ys) > 0)
            {
                // Every DTM read below is windowed by this box. A corner left
                // as HUGE_VAL used to widen it to the whole planet (or, with
                // min/max over a NaN, leave it garbage), so the banded reader
                // would scan far outside the raster.
                OGRCoordinateTransformation::DestroyCT(meshToDTM);
                if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
                fail(QObject::tr(
                    "The meshing extent could not be reprojected into the "
                    "DTM's CRS, so the area to sample cannot be determined.\n"
                    "Check that the DTM and the model share an overlapping "
                    "coordinate system."));
                return;
            }
            dx0 = *std::min_element(xs, xs+4); dx1 = *std::max_element(xs, xs+4);
            dy0 = *std::min_element(ys, ys+4); dy1 = *std::max_element(ys, ys+4);
        }

        qCDebug(lcMeshPerf) << "[Mesh] DTM bbox (DTM CRS):" << dx0 << dy0 << "--" << dx1 << dy1;

        const double gStep = (in.thinnerOpts.gridSpacing > 0.0)
                              ? in.thinnerOpts.gridSpacing
                              : thinner.pixelSize();
        if (gStep <= 0.0)
        {
            if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
            if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
            fail(QObject::tr("DTM pixel size is invalid (%1).").arg(gStep));
            return;
        }

        qCDebug(lcMeshPerf) << "[Mesh] gStep:" << gStep << "| doThinning:" << in.doThinning;

        // 2026-07-19c — gStep is a DTM-CRS quantity (degrees for a geographic
        // raster), but every consumer below — the Poisson filter and the
        // boundary buffer — measures distances in MESH CRS units (metres for
        // a projected mesh). Using gStep directly made bufferDist 0.14 mm for
        // a 1-arc-second DTM, so the segment chunker below tried to cut the
        // ~205 km of subcatchment boundary into ~1.5e9 chunks and generation
        // hung. Convert one grid step at the domain centre into mesh units.
        const double gStepMesh = [&]() -> double {
            if (!dtmToMesh) return gStep;      // same CRS: already mesh units
            const double cx = (dx0 + dx1) * 0.5, cy = (dy0 + dy1) * 0.5;
            double xs[3] = {cx, cx + gStep, cx};
            double ys[3] = {cy, cy,         cy + gStep};
            if (!dtmToMesh->Transform(3, xs, ys)) return gStep;
            const double ex = std::hypot(xs[1] - xs[0], ys[1] - ys[0]);
            const double ey = std::hypot(xs[2] - xs[0], ys[2] - ys[0]);
            // Mean of the two axis steps: a geographic pixel is anisotropic
            // once projected (17.6 m E-W vs 30.9 m N-S at Bellinge's
            // latitude), and the consumers want one scalar "spacing".
            const double s = 0.5 * (ex + ey);
            return (std::isfinite(s) && s > 0.0) ? s : gStep;
        }();

        // Effective terrain point spacing IN MESH CRS UNITS, shared by the
        // boundary-buffer auto formula and the Poisson filter so both agree
        // on "how far apart terrain points end up". minSpacing comes from the
        // UI already in model units, so it is used as-is.
        const double effSpacing = in.thinnerOpts.useMinSpacing
            ? ((in.thinnerOpts.minSpacing > 0.0) ? in.thinnerOpts.minSpacing
                                                 : gStepMesh * 2.0)
            : gStepMesh;

        qCDebug(lcMeshPerf) << "[Mesh] gStep (DTM CRS):" << gStep
                 << "-> gStepMesh (mesh CRS):" << gStepMesh
                 << "| effSpacing:" << effSpacing;

        // Probe the DTM at the centre of the domain to verify sampleAt works.
        {
            const double cx = (dx0 + dx1) * 0.5, cy = (dy0 + dy1) * 0.5;
            const double zc = thinner.sampleAt(cx, cy);
            qCDebug(lcMeshPerf) << "[Mesh] centre probe at (" << cx << "," << cy
                     << ") -> z =" << zc
                     << (std::isfinite(zc) ? "(OK)" : "(NaN — DTM may not cover domain)");
        }

        QVector<QPointF> candidates;
        QVector<double>  candidateZ;

        // ── Stage-B cache: terrain candidates (DTM CRS, pre-filter) ────
        // A hit skips the iterative thinning / bulk pixel read entirely.
        // Reprojection, Poisson, and the boundary filter below rerun on
        // both paths (cheap), so their parameters are not in the key.
        QByteArray terrainCacheKey;
        bool terrainHit = false;
        if (cache.isUsable())
        {
            const QFileInfo dfi(in.dtmPath);
            mesh::MeshStageCache::FileIdentity demId;
            demId.absPath   = dfi.absoluteFilePath();
            demId.mtimeMs   = dfi.lastModified().toMSecsSinceEpoch();
            demId.sizeBytes = dfi.size();
            terrainCacheKey = mesh::MeshStageCache::terrainKey(
                demId, /*band*/ 1, in.thinnerOpts, in.doThinning,
                QRectF(QPointF(dx0, dy0), QPointF(dx1, dy1)));

            mesh::MeshStageCache::TerrainPoints tp;
            QElapsedTimer cacheClock;
            cacheClock.start();
            if (cache.loadTerrain(terrainCacheKey, &tp))
            {
                candidates = std::move(tp.xyDtm);
                candidateZ = std::move(tp.z);
                terrainHit = true;
                qCInfo(lcMeshPerf) << "[Mesh][cache] terrain points HIT"
                                   << terrainCacheKey.left(12).constData()
                                   << "-" << candidates.size() << "pts in"
                                   << cacheClock.elapsed() << "ms";
                progress(36, QObject::tr("Loaded %1 cached terrain points…")
                             .arg(candidates.size()));
            }
            else
            {
                qCInfo(lcMeshPerf) << "[Mesh][cache] terrain points miss"
                                   << terrainCacheKey.left(12).constData();
            }
        }

        if (!terrainHit && in.doThinning)
        {
            progress(30, QObject::tr("Terrain-adaptive thinning…"));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

            const MapExtent dtmBbox(dx0, dy0, dx1, dy1);
            stageClock.restart();
            // Banded thinning streams huge DEMs; the callback maps its
            // fraction into the 30→36% progress window and doubles as the
            // cancellation poll (Stop was dead for the whole stage before).
            candidates = thinner.generatePoints(
                dtmBbox, in.thinnerOpts, &candidateZ,
                [&promise](double f) -> bool {
                    promise.setProgressValueAndText(
                        30 + qBound(0, int(f * 6.0), 6),
                        QObject::tr("Terrain-adaptive thinning… %1%")
                            .arg(int(f * 100.0)));
                    return !promise.isCanceled();
                });
            stageMark("thinning (generatePoints)");
            // Cancel check FIRST: a cancelled run also lands here with an
            // empty result + "cancelled" errorMsg, and should read as
            // "Cancelled.", not as a thinning failure.
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
            // The thinner's safety guards (per-band working-set ceiling,
            // retained-points ceiling, RasterIO failures) return an EMPTY
            // result with the reason in errorMsg().  Silently meshing on with
            // zero terrain points buries that message — fail loudly instead
            // so the user can fix spacing/extent.
            if (candidates.isEmpty() && !thinner.errorMsg().isEmpty())
            {
                fail(QObject::tr("Terrain thinning failed: %1")
                         .arg(thinner.errorMsg()));
                return;
            }
            qCDebug(lcMeshPerf) << "[Mesh] thinning retained" << candidates.size() << "points";
            progress(36, QObject::tr("Thinning retained %1 terrain points…")
                         .arg(candidates.size()));
        }
        else if (!terrainHit)
        {
            // No thinning: read every raster pixel that overlaps the domain bbox
            // in a single bulk RasterIO call.  Pixel centres are returned in
            // DTM CRS; the common loop below transforms them to mesh CRS.
            progress(30, QObject::tr("Sampling DTM terrain points…"));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

            const MapExtent dtmBbox(dx0, dy0, dx1, dy1);
            stageClock.restart();
            thinner.readPixels(dtmBbox, candidates, candidateZ);
            stageMark("bulk readPixels");
            // readPixels refuses pathologically large outputs (see its budget
            // guard) and reports why via errorMsg() — surface it instead of
            // meshing on with zero terrain points.
            if (candidates.isEmpty() && !thinner.errorMsg().isEmpty())
            {
                fail(QObject::tr("DTM sampling failed: %1")
                         .arg(thinner.errorMsg()));
                return;
            }

            qCDebug(lcMeshPerf) << "[Mesh] bulk readPixels returned" << candidates.size() << "points";
            progress(36, QObject::tr("Sampled %1 DTM grid points…")
                         .arg(candidates.size()));
        }

        if (!terrainHit && !terrainCacheKey.isEmpty())
        {
            // Skip pathological payloads (an uncapped readPixels over a huge
            // bbox can reach GBs) so the sidecar stays reasonable.
            constexpr qint64 kMaxTerrainCacheBytes = qint64(512) * 1024 * 1024;
            const qint64 payloadBytes =
                qint64(candidates.size()) * qint64(sizeof(QPointF) + sizeof(double));
            if (payloadBytes <= kMaxTerrainCacheBytes)
            {
                QElapsedTimer storeClock;
                storeClock.start();
                if (cache.storeTerrain(terrainCacheKey, {candidates, candidateZ}))
                    qCInfo(lcMeshPerf) << "[Mesh][cache] terrain points stored"
                                       << terrainCacheKey.left(12).constData()
                                       << "in" << storeClock.elapsed() << "ms";
            }
            else
            {
                qCInfo(lcMeshPerf) << "[Mesh][cache] terrain payload too large to cache:"
                                   << payloadBytes << "bytes";
            }
        }

        // Reproject all candidates from DTM CRS → mesh CRS.
        progress(36, QObject::tr("Reprojecting %1 terrain points…")
                     .arg(candidates.size()));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        stageClock.restart();
        QVector<QPointF> candidatesMesh;
        if (dtmToMesh && !candidates.isEmpty())
        {
            // One batched PROJ call; per-point results are identical to
            // Transform(1,…) in a loop, without the per-call overhead.
            candidatesMesh.reserve(candidates.size());
            QVector<double> txs(candidates.size()), tys(candidates.size());
            for (qsizetype ci = 0; ci < candidates.size(); ++ci)
            {
                txs[ci] = candidates[ci].x();
                tys[ci] = candidates[ci].y();
            }
            // This is the path that used to leak non-finite coordinates into
            // the PSLG: a failed point kept OGR's HUGE_VAL, passed the
            // in-domain bbox test by accident, and became a Steiner vertex.
            // Terrain candidates are optional refinement points, so drop the
            // failures — but keep candidateZ in step, since the two arrays are
            // paired positionally by index further down.
            const qsizetype nFail = transformChecked(dtmToMesh, txs.size(),
                                                     txs.data(), tys.data());
            if (nFail == 0)
            {
                for (qsizetype ci = 0; ci < candidates.size(); ++ci)
                    candidatesMesh.append(QPointF(txs[ci], tys[ci]));
            }
            else
            {
                // candidateZ is indexed by the candidatesMesh position from
                // here on (the Poisson filter and the Steiner loop both do
                // candidateZ[ci]), so it has to shrink in lockstep.
                const bool zPaired = (candidateZ.size() == candidates.size());
                QVector<double> keptZ;
                keptZ.reserve(candidateZ.size());
                for (qsizetype ci = 0; ci < candidates.size(); ++ci)
                {
                    if (!std::isfinite(txs[ci]) || !std::isfinite(tys[ci]))
                        continue;
                    candidatesMesh.append(QPointF(txs[ci], tys[ci]));
                    if (zPaired) keptZ.append(candidateZ[ci]);
                }
                if (zPaired) candidateZ = std::move(keptZ);
                qCWarning(lcMeshPerf)
                    << "[Mesh]" << nFail << "of" << candidates.size()
                    << "terrain points failed DTM->mesh reprojection and were"
                    << "dropped;" << candidatesMesh.size() << "retained";
            }
        }
        else
        {
            // Same CRS — implicit-sharing assignment, no per-point copy and
            // no second 16 B/point buffer (matters at tens of millions of
            // candidates from a large DEM).
            candidatesMesh = candidates;
        }
        // The DTM-CRS array is not read past this point. Release it now: the
        // reprojected path drops its duplicate immediately, and the shared
        // same-CRS copy returns to refcount 1 so the non-const operator[]
        // reads below never trigger a detach deep-copy.
        candidates = QVector<QPointF>();
        stageMark("reproject candidates DTM->mesh CRS");

        // ── Option A: Poisson-disk minimum spacing filter (mesh CRS) ────────
        // Iterates candidates in order; accepts a point only if no already-
        // accepted point is within minSpacing.  Uses a spatial hash grid of
        // cell size = minSpacing for O(N) average complexity.
        if (in.thinnerOpts.useMinSpacing)
        {
            // 2026-07-19 — `spacing` hoisted to effSpacing above (shared
            // with the boundary-buffer auto formula); value is identical.
            const double spacing   = effSpacing;
            const double spacing2  = spacing * spacing;
            const double invCell   = 1.0 / spacing;

            progress(36, QObject::tr("Poisson-disk filter over %1 points…")
                         .arg(candidatesMesh.size()));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
            stageClock.restart();

            // Flat chained-index grid: cellHead maps a cell to its most
            // recently accepted candidate, nextInCell links the rest of that
            // cell's accepted candidates intrusively.  Replaces the previous
            // QHash<CellKey, QVector<int>> (one heap-allocated QVector per
            // occupied cell, ~50–100 B/point) with one flat 4 B/point array
            // plus an int-valued hash — an order of magnitude less overhead
            // at tens of millions of candidates.  Accept/reject decisions are
            // identical: chain order differs from append order, but tooClose
            // is an OR over the same neighbour set.
            using CellKey = QPair<qint32, qint32>;
            QHash<CellKey, int> cellHead;
            QVector<qint32> nextInCell(candidatesMesh.size(), -1);

            QVector<bool> kept(candidatesMesh.size(), false);
            qsizetype nKept = 0;
            for (int ci = 0; ci < candidatesMesh.size(); ++ci)
            {
                const double px = candidatesMesh[ci].x();
                const double py = candidatesMesh[ci].y();
                const qint32 cx = qint32(std::floor(px * invCell));
                const qint32 cy = qint32(std::floor(py * invCell));

                bool tooClose = false;
                for (qint32 dy = -1; dy <= 1 && !tooClose; ++dy)
                    for (qint32 dx = -1; dx <= 1 && !tooClose; ++dx)
                    {
                        auto it = cellHead.constFind(qMakePair(cx+dx, cy+dy));
                        if (it == cellHead.constEnd()) continue;
                        for (int j = it.value(); j != -1; j = nextInCell[j])
                        {
                            const double ddx = candidatesMesh[j].x() - px;
                            const double ddy = candidatesMesh[j].y() - py;
                            if (ddx*ddx + ddy*ddy < spacing2) { tooClose = true; break; }
                        }
                    }

                if (!tooClose)
                {
                    kept[ci] = true;
                    ++nKept;
                    auto it = cellHead.find(qMakePair(cx, cy));
                    if (it == cellHead.end())
                        cellHead.insert(qMakePair(cx, cy), ci);
                    else { nextInCell[ci] = it.value(); it.value() = ci; }
                }
            }

            // Compact: remove rejected candidates.  Reserve the exact
            // survivor count — a full-N reserve here doubled the peak for
            // no benefit when the filter rejects most points.
            QVector<QPointF> filtMesh;
            QVector<double>  filtZ;
            filtMesh.reserve(nKept);
            filtZ.reserve(nKept);
            for (int ci = 0; ci < candidatesMesh.size(); ++ci)
                if (kept[ci]) { filtMesh.append(candidatesMesh[ci]); filtZ.append(candidateZ[ci]); }

            stageMark("Poisson-disk filter");
            qCDebug(lcMeshPerf) << "[Mesh] Poisson-disk:" << candidatesMesh.size()
                     << "->" << filtMesh.size() << "points (spacing" << spacing
                     << "| cells" << cellHead.size() << ")";
            progress(37, QObject::tr("Poisson-disk filter: %1 → %2 points…")
                         .arg(candidatesMesh.size()).arg(filtMesh.size()));

            candidatesMesh = std::move(filtMesh);
            candidateZ     = std::move(filtZ);
        }

        // ── 2026-07-19 — boundary-aware terrain filter ──────────────────────
        // Terrain candidates were sampled over the domain BOUNDING BOX with
        // no awareness of the PSLG: a pixel centre landing millimetres from
        // a constrained boundary/hole/conduit segment (or a mandatory SWMM
        // node vertex) forces a sliver triangle, which the -q quality pass
        // then splits into clusters of tiny cells hugging the boundary.
        // Reject candidates that are (a) outside the domain rings, (b)
        // inside a hole ring (unmeshed interior), (c) closer than bufferDist
        // to any constrained segment, or (d) closer than bufferDist to any
        // mandatory Steiner vertex. Triangle fills the resulting buffer band
        // with properly-sized boundary triangles on its own.
        const double bufferDist = (in.terrainBoundaryBuffer > 0.0)
                                  ? in.terrainBoundaryBuffer
                                  : 0.5 * effSpacing;
        // Auto = 0.5 × effective terrain spacing: keeps terrain points
        // outside the diametral circles of comparable-length boundary
        // segments and never closer to the boundary than to their nearest
        // terrain neighbour, so boundary-adjacent triangles land in the same
        // size class as interior ones. maxArea is deliberately not factored
        // in — when it implies smaller edges than the terrain spacing the
        // refinement is globally fine-grained anyway, and a max() would
        // carve an oversized terrain-free band on coarse-area runs.
        const double bufferDist2 = bufferDist * bufferDist;
        const double invBufCell  = 1.0 / bufferDist;
        using CellKey = QPair<qint32, qint32>;

        // Inside-domain test — semantics identical to collectInputs'
        // inDomain: inside any domain ring AND not inside any hole ring.
        //
        // 2026-07-19b — STALL FIX (part 2): the first cut looped
        // QPolygonF::containsPoint over every ring per candidate, i.e.
        // O(total ring vertices) × O(candidates) — a second slow path on
        // dense dissolved subcatchment boundaries. Replaced with one
        // odd-even crossing count over ALL rings (domains + holes share
        // the parity, so "in domain minus holes" falls out directly for
        // the disjoint rings UnaryUnion produces), with edges bucketed by
        // y-band so each query only visits edges near its scanline.
        progress(37, QObject::tr("Boundary filter: indexing domain rings…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        stageClock.restart();

        mesh::pslg::PointInRingsIndex pipIdx;
        pipIdx.build(in.domains, in.holeRings);
        stageMark("boundary filter: point-in-polygon band index");
        auto insideDomain = [&pipIdx](const QPointF &p) -> bool {
            return pipIdx.contains(p);
        };

        // Constrained-segment spatial hash (same idiom as the Poisson grid
        // above / flatGrid in Step 1): every edge of every domain ring, hole
        // ring, and constraint-segment path, binned into every cell its
        // bufferDist-inflated bbox overlaps.
        //
        // 2026-07-19b — STALL FIX: segments are chunked to <= bufferDist
        // length BEFORE binning. Binning a whole segment rasterised its
        // inflated bounding box at cell size = bufferDist, which is
        // O((len/buffer)²) cells for a diagonal segment — a few hundred
        // metres of subcatchment boundary against a half-pixel buffer
        // exploded into millions of hash insertions per segment and the
        // generation appeared to hang at ~37%. The union of chunks equals
        // the segment (min distance over chunks == distance to segment),
        // each chunk touches a handful of cells, and the total insertion
        // count becomes O(len/buffer). The 3×3 query below is unchanged.
        progress(37, QObject::tr("Boundary filter: indexing constraint segments…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        stageClock.restart();

        // 2026-07-19c — hard ceiling on total chunks. The chunk count is
        // O(totalBoundaryLength / bufferDist), so any future unit slip in
        // bufferDist (see the gStepMesh conversion above — a DTM-CRS value
        // leaking in made this 1.5e9) silently turns into an unbounded loop.
        // Bail loudly instead of hanging: the filter is a quality refinement,
        // and skipping it costs sliver triangles, not a wrong mesh.
        constexpr qint64 kMaxSegChunks = 20'000'000;
        qint64 nChunkTotal = 0;
        bool   chunkOverflow = false;

        QVector<QPair<QPointF, QPointF>> csegs;
        QHash<CellKey, QVector<int>>     segGrid;
        auto addSegPath = [&](const QVector<QPointF> &path, bool closeRing) {
            if (chunkOverflow) return;
            const int n = path.size();
            if (n < 2) return;
            const bool alreadyClosed = (path.first() == path.last());
            const int  segCount = (closeRing && !alreadyClosed) ? n : n - 1;
            for (int i = 0; i < segCount; ++i)
            {
                const QPointF &a = path[i];
                const QPointF &b = path[(i + 1) % n];
                if (a == b) continue;
                const double segLen = std::hypot(b.x() - a.x(), b.y() - a.y());
                if (!std::isfinite(segLen)) continue;   // garbage coords guard

                // Count in double/qint64: segLen/bufferDist overflows int
                // outright once bufferDist is wrong by a few orders.
                const double chunkReal = std::ceil(segLen / bufferDist);
                if (!std::isfinite(chunkReal)
                    || nChunkTotal + qint64(std::min(chunkReal, 1e18))
                           > kMaxSegChunks)
                {
                    chunkOverflow = true;
                    return;
                }
                const int nChunks = std::max(1, static_cast<int>(chunkReal));
                nChunkTotal += nChunks;
                for (int c = 0; c < nChunks; ++c)
                {
                    const double t0 = static_cast<double>(c)     / nChunks;
                    const double t1 = static_cast<double>(c + 1) / nChunks;
                    const QPointF ca(a.x() + t0 * (b.x() - a.x()),
                                     a.y() + t0 * (b.y() - a.y()));
                    const QPointF cb(a.x() + t1 * (b.x() - a.x()),
                                     a.y() + t1 * (b.y() - a.y()));
                    const int segIdx = csegs.size();
                    csegs.append(qMakePair(ca, cb));
                    const qint32 cx0 = qint32(std::floor((std::min(ca.x(), cb.x()) - bufferDist) * invBufCell));
                    const qint32 cx1 = qint32(std::floor((std::max(ca.x(), cb.x()) + bufferDist) * invBufCell));
                    const qint32 cy0 = qint32(std::floor((std::min(ca.y(), cb.y()) - bufferDist) * invBufCell));
                    const qint32 cy1 = qint32(std::floor((std::max(ca.y(), cb.y()) + bufferDist) * invBufCell));
                    for (qint32 gy = cy0; gy <= cy1; ++gy)
                        for (qint32 gx = cx0; gx <= cx1; ++gx)
                            segGrid[qMakePair(gx, gy)].append(segIdx);
                }
            }
        };
        for (const auto &dom : std::as_const(in.domains))
            addSegPath(dom, /*closeRing=*/true);
        for (const auto &hr : std::as_const(in.holeRings))
            addSegPath(hr, /*closeRing=*/true);
        for (const auto &cs : std::as_const(in.constraintSegs))
            addSegPath(cs.path, /*closeRing=*/false);
        stageMark("boundary filter: constraint-segment hash");
        if (chunkOverflow)
        {
            // Abandon the segment index rather than grind: keep the cheap
            // inside-domain and mandatory-vertex rejections, drop only the
            // near-segment one.
            csegs.clear();
            segGrid.clear();
            qWarning() << "[Mesh] boundary filter: segment index exceeded"
                       << kMaxSegChunks
                       << "chunks at bufferDist" << bufferDist
                       << "— skipping the near-segment rejection. Set an "
                          "explicit Boundary buffer if slivers appear.";
        }
        {
            qint64 nIns = 0;
            for (auto it = segGrid.constBegin(); it != segGrid.constEnd(); ++it)
                nIns += it.value().size();
            qCDebug(lcMeshPerf) << "[Mesh] seg hash: bufferDist" << bufferDist
                     << "| chunks" << csegs.size()
                     << "| cells" << segGrid.size()
                     << "| insertions" << nIns
                     << "| overflow" << chunkOverflow;
        }

        auto nearConstraint = [&](double px, double py) -> bool {
            const QPointF p(px, py);
            const qint32 cx = qint32(std::floor(px * invBufCell));
            const qint32 cy = qint32(std::floor(py * invBufCell));
            for (qint32 dy = -1; dy <= 1; ++dy)
                for (qint32 dx = -1; dx <= 1; ++dx)
                {
                    auto it = segGrid.constFind(qMakePair(cx + dx, cy + dy));
                    if (it == segGrid.constEnd()) continue;
                    for (const int si : *it)
                        if (distSqToSegment(p, csegs[si].first, csegs[si].second)
                                < bufferDist2)
                            return true;
                }
            return false;
        };

        // Mandatory-vertex hash: ALL Step-1 Steiner points (SWMM nodes, 3D
        // feature vertices, …) are vertices Triangle must keep — a terrain
        // point centimetres from a junction makes the same sliver at exactly
        // the coupling locations. Rim-flatten only conditions z, not xy.
        QHash<CellKey, QVector<QPointF>> vertGrid;
        stageClock.restart();
        for (const auto &sp0 : std::as_const(in.steinerPoints))
        {
            const qint32 gx = qint32(std::floor(sp0.xy.x() * invBufCell));
            const qint32 gy = qint32(std::floor(sp0.xy.y() * invBufCell));
            vertGrid[qMakePair(gx, gy)].append(sp0.xy);
        }
        auto nearMandatoryVertex = [&](double px, double py) -> bool {
            const qint32 cx = qint32(std::floor(px * invBufCell));
            const qint32 cy = qint32(std::floor(py * invBufCell));
            for (qint32 dy = -1; dy <= 1; ++dy)
                for (qint32 dx = -1; dx <= 1; ++dx)
                {
                    auto it = vertGrid.constFind(qMakePair(cx + dx, cy + dy));
                    if (it == vertGrid.constEnd()) continue;
                    for (const QPointF &v : *it)
                    {
                        const double ddx = v.x() - px, ddy = v.y() - py;
                        if (ddx * ddx + ddy * ddy < bufferDist2) return true;
                    }
                }
            return false;
        };

        // Add surviving terrain candidates as PSLG Steiner vertices.
        // Candidates within a rim node's flatten radius are forced to that
        // node's rim z (model units); the rest keep their raster-unit DTM z.
        stageMark("boundary filter: mandatory-vertex hash");

        int nOutside = 0, nNearSeg = 0, nNearNode = 0, nAdded = 0;
        progress(37, QObject::tr("Boundary filter: testing %1 terrain points…")
                     .arg(candidatesMesh.size()));
        stageClock.restart();
        // No up-front hash reserve: sizing elevCache for every PRE-filter
        // candidate commits multi-GB before a single point survives the
        // boundary filter.  Geometric growth tracks the actual survivor count
        // instead.  The Steiner VECTOR gets a capped reserve — a plain array,
        // bounded to ~192 MB, most candidates survive the filter — which
        // avoids the transient ~2x growth peak without the unbounded-commit
        // hazard the hash reserve had.
        g.reserveSteinerPoints(std::min<qsizetype>(candidatesMesh.size(),
                                                   qsizetype(4) * 1024 * 1024));
        for (int ci = 0; ci < candidatesMesh.size(); ++ci)
        {
            // Cancel check every 64k candidates: this loop is the longest
            // uninterruptible stretch in the band and "Stop" was dead here.
            if ((ci & 0xFFFF) == 0 && promise.isCanceled())
            { fail(QObject::tr("Cancelled.")); return; }

            const double cx = candidatesMesh[ci].x(), cy = candidatesMesh[ci].y();

            // 2026-07-19 — boundary-aware rejection (see block comment above).
            if (!insideDomain(QPointF(cx, cy))) { ++nOutside;  continue; }
            if (nearConstraint(cx, cy))         { ++nNearSeg;  continue; }
            if (nearMandatoryVertex(cx, cy))    { ++nNearNode; continue; }

            double z = candidateZ[ci];
            const double fz = flattenZ(cx, cy);
            const bool flattened = std::isfinite(fz);
            if (flattened) z = fz;

            mesh::SteinerPoint sp;
            sp.xy   = QPointF(cx, cy);
            sp.z    = z;
            sp.hasZ = true;
            g.addSteinerPoint(sp);
            ++nAdded;

            const auto k = keyOf(cx, cy);
            elevCache.insert(k, z);
            if (flattened) modelUnitKeys.insert(k);
        }

        stageMark("boundary filter: candidate rejection loop");
        qCDebug(lcMeshPerf) << "[Mesh] boundary filter:" << candidatesMesh.size()
                 << "->" << nAdded
                 << "(outside" << nOutside
                 << "| near-segment" << nNearSeg
                 << "| near-node" << nNearNode
                 << "| buffer" << bufferDist << ")";
        progress(38, QObject::tr("%1 DTM Steiner points added to PSLG "
                                 "(%2 dropped by boundary filter)")
                     .arg(nAdded)
                     .arg(candidatesMesh.size() - nAdded));
    }
    else if (useDTM)
    {
        qCDebug(lcMeshPerf) << "[Mesh] domain bbox invalid — skipping DTM sampling";
    }

    // ── Run Triangle ────────────────────────────────────────────────
    progress(40, QObject::tr("Running Triangle…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    // Graded size field (V2 plan Track B).  Keeps the uniform cap AT the
    // constrained features and lets the permitted area grow with distance at
    // the user's Lipschitz slope — strictly fewer cells than the uniform cap,
    // with the slope itself the smooth-transition guarantee.  Built from the
    // CONDITIONED geometry so the field grades away from what Triangle will
    // actually see.  Must outlive generate(): the hook samples it per
    // candidate triangle.
    mesh::SizeField sizeField;
    bool useGrading = false;
    if (in.sizeGradation > 0.0 && in.genOpts.maxArea > 0.0)
    {
        QRectF bbox;
        for (const QPolygonF &d : std::as_const(in.domains))
            bbox = bbox.isValid() ? bbox.united(d.boundingRect())
                                  : d.boundingRect();

        // Seeds: constraint segments, valid hole rings, tagged (SWMM node)
        // Steiner points.  The outer domain ring is deliberately not a seed
        // (see sizefield.h).
        QVector<QVector<QPointF>> ringSeeds;
        ringSeeds.reserve(bprep.holeRings.size());
        for (int k = 0; k < bprep.holeRings.size(); ++k)
            if (k < bprep.holeValid.size() && bprep.holeValid[k])
                ringSeeds.append(bprep.holeRings[k]);
        // Structured patch boundaries seed too (TRI_QUAD_MESHING_PLAN §3.2):
        // the stitch keeps the near-feature cap instead of grading up to
        // whatever the surrounding features permit, which is what fans
        // slivers against the patch's boundary nodes.
        for (const mesh::PatchMesh &pm : std::as_const(in.patches))
            for (const auto &seg : pm.boundarySegments)
                if (seg.first >= 0 && seg.first < pm.xy.size()
                    && seg.second >= 0 && seg.second < pm.xy.size())
                    ringSeeds.append({pm.xy[seg.first], pm.xy[seg.second]});
        // Quad region rings seed for the same reason (QUAD_MESHING_REDESIGN
        // §6.1): the triangles outside a region grade away from the region's
        // spacing instead of jumping.  Per edge, wrap edge included — the
        // size field treats a ring as an open path.
        for (const mesh::QuadRegion &qr : std::as_const(quadRegions))
        {
            const int n = qr.ring.size();
            for (int i = 0; i < n; ++i)
            {
                const QPointF &a = qr.ring[i], &b = qr.ring[(i + 1) % n];
                if (a != b) ringSeeds.append({a, b});
            }
        }

        mesh::SizeFieldOptions sfo;
        // Side of the equilateral triangle of maxArea — the near-feature size.
        sfo.nearSize  = std::sqrt(4.0 * in.genOpts.maxArea / std::sqrt(3.0));
        sfo.gradation = in.sizeGradation;
        sfo.areaFloor = areaFloor;
        // With quads everywhere the DTM thinner's points are replaced by the
        // lattice, so their density has to survive as a SIZE or terrain detail
        // is lost (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.4). Only switched on
        // for that case: it is exactly when the points stop being vertices.
        sfo.terrainDensity = in.quadEverywhere;
        useGrading = sizeField.build(bbox, in.constraintSegs, ringSeeds,
                                     in.steinerPoints, sfo);
        if (useGrading)
        {
            qCInfo(lcMeshPerf) << "[Mesh][grading] size field"
                               << sizeField.cols() << "x" << sizeField.rows()
                               << "at pitch" << sizeField.pitch()
                               << "| near size" << sfo.nearSize
                               << "| gradation" << sfo.gradation;
            // Installing a size function switches Triangle to the bare `a`
            // switch, which ACTIVATES per-region area bounds that a numeric
            // `a<maxArea>` leaves inert (trirefinehook.h).  The clamp above
            // bounds them to the refinement floor when one exists; either way
            // a mesh that gains cells needs a traceable cause in the log.
            if (!in.regionMarkers.isEmpty())
                qCInfo(lcMeshPerf)
                    << "[Mesh][grading]" << in.regionMarkers.size()
                    << "region area bound(s) are ACTIVE under graded sizing "
                       "(they are inert under a uniform cap)";
        }
        else
            qWarning() << "[Mesh][grading] size field could not be built "
                          "(no seed features?) — falling back to the uniform "
                          "area cap.";
    }

    // Triangle's refinement pass is otherwise uninterruptible — the Stop button
    // is dead for its entire duration, which on a large PSLG can be minutes.
    // The `-u` user-test hook is the only place Triangle calls back into us
    // during refinement, so cancellation and progress both ride on it.
    {
        mesh::RefineHook hook;
        hook.isCancelled = [&promise] { return promise.isCanceled(); };
        hook.onProgress  = [&progress](qint64 tests) {
            progress(45, QObject::tr("Refining mesh… (%1 M triangle tests)")
                             .arg(tests / 1000000));
        };
        // Refinement floor.  Read trirefinehook.h before touching this: the
        // hook returns the MAXIMUM permitted area and Triangle splits anything
        // larger, so the only thing expressible here is "do not let the
        // uniform cap drive subdivision below the floor the user asked for" —
        // i.e. raise a too-small cap up to the floor.  Returning the floor
        // when there is no cap would instead order the WHOLE domain refined to
        // the minimum size, which is a vertex-count explosion and the exact
        // opposite of the intent; <= 0 means unconstrained, so that is what an
        // absent cap must return.
        //
        // Triangle never coarsens, so this cannot enlarge a cell the input
        // demanded — that is the conditioning pass's job, not this one.
        if (useGrading)
        {
            // The graded field already folds in the floor (SizeFieldOptions::
            // areaFloor) and the near-feature cap, so it replaces both the
            // uniform `-a` switch and the constant-floor lambda below.
            hook.targetAreaAt = [&sizeField](double x, double y) {
                return sizeField.targetAreaAt(x, y);
            };
        }
        else if (areaFloor > 0.0)
        {
            // Constant across the domain, so resolve it once rather than per
            // triangle test.  MinSizePolicy::refinementAreaCap owns the rule
            // (and its regression test).
            const double capped =
                in.minSizePolicy.refinementAreaCap(in.genOpts.maxArea);
            hook.targetAreaAt = [capped](double, double) { return capped; };
        }
        g.setRefineHook(hook);
    }

    stageClock.restart();
    mesh::MeshResult result = g.generate();
    stageMark("Triangle generate()");
    if (promise.isCanceled())
    {
        if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
        if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
        fail(QObject::tr("Cancelled.")); return;
    }
    if (!result.ok)
    {
        if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
        if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
        fail(QObject::tr("Triangle: %1").arg(result.errorMsg)); return;
    }

    // ── Quad region reports (one line per addQuadRegion call) ────────
    // Skipped regions are surfaced the way skipped hole rings are (the
    // qWarning "[Mesh] Skipped" channel) so a silently-triangulated region
    // is never mistaken for a quad one.
    int nQuadRegionQuads = 0;
    for (const mesh::QuadRegionReport &rep : g.quadRegionReports())
    {
        nQuadRegionQuads += rep.quads;
        qCInfo(lcMeshPerf).nospace()
            << "[Mesh][quad] region " << rep.index << ": "
            << quadRegionModeName(rep.requested) << " -> "
            << quadRegionModeName(rep.resolved)
            << " | h " << rep.spacing
            << " | " << rep.quads << " quads + " << rep.triangles << " tris"
            << " (" << rep.templateQuads << " template, " << rep.gapQuads << " gap)"
            << " | points " << rep.generatedPoints << " generated, "
            << rep.droppedSteiners << " terrain dropped"
            << " | min SJ " << rep.minScaledJacobian
            << " | median rect " << rep.medianRectangularity
            << (rep.message.isEmpty() ? QString() : QStringLiteral(" | ") + rep.message);
        if (rep.message.startsWith(QLatin1String("skipped:")))
            qWarning() << "[Mesh] Skipped quad region" << rep.index << "—"
                       << rep.message.mid(int(qstrlen("skipped:"))).trimmed();
    }

    // ── Sub-scale cell cleanup ───────────────────────────────────────
    // MIN_CELL_SIZE_ENFORCEMENT_PLAN §6 Phase 5.  Removes the slivers Triangle
    // inserted on its own; it cannot remove ones the input demanded, because
    // every constrained edge and every tagged/coupled vertex is protected.
    // Runs BEFORE the Hilbert reorder so the reorder's locality is not wasted.
    if (in.minSizeCleanup && in.minSizePolicy.enabled())
    {
        progress(52, QObject::tr("Removing sub-scale cells…"));
        mesh::CleanupPolicy cpol;
        cpol.minCellSize = in.minSizePolicy.minCellSize;
        // V2 enforcement (plan Track A): let cleanup absorb slivers into a
        // single identity vertex and collapse interior constrained edges.
        // Distinct identities still never merge and a lost coupling still
        // aborts the pass — those two rules are cleanup's own, not ours.
        if (in.minSizeEnforce)
        {
            cpol.allowIdentityCollapse = true;
            cpol.beta                  = 0.45;
        }
        mesh::CleanupReport crep;
        const bool ok = mesh::collapseSubScaleCells(&result, cpol, &crep);
        qCInfo(lcMeshPerf) << "[Mesh][minsize] cleanup:" << crep.summary();
        if (!ok)
            qWarning() << "[Mesh][minsize] sliver cleanup abandoned a pass — "
                          "mesh restored to its pre-pass state.";
        if (crep.skippedProtected > 0)
            qCInfo(lcMeshPerf) << "[Mesh][minsize]" << crep.skippedProtected
                               << "sub-scale cell(s) are bounded by constrained "
                                  "or coupled geometry and cannot be collapsed — "
                                  "these need a larger minimum cell size or "
                                  "simpler input geometry.";
        for (const QPointF &p : std::as_const(crep.unfixable))
            qCDebug(lcMeshPerf) << "[Mesh][minsize]  protected sliver at" << p;
        stageMark("sub-scale cell cleanup");
    }

    // Enforcement contract (V2 plan Track A): say plainly what was achieved
    // against what was requested — "enforced" must never be a silent miss.
    if (in.minSizePolicy.enabled() && !result.triangles.isEmpty())
    {
        double minArea = std::numeric_limits<double>::max();
        for (const mesh::MeshTriangle &t : std::as_const(result.triangles))
        {
            if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
            const QPointF &a = result.vertices[t.v0].xy;
            const QPointF &b = result.vertices[t.v1].xy;
            const QPointF &c = result.vertices[t.v2].xy;
            const double ar = 0.5 * std::abs(
                (b.x() - a.x()) * (c.y() - a.y())
                - (c.x() - a.x()) * (b.y() - a.y()));
            if (ar < minArea) minArea = ar;
        }
        // Side of the equilateral triangle of the smallest cell — comparable
        // to the requested h.
        const double side = std::sqrt(4.0 * minArea / std::sqrt(3.0));
        qCInfo(lcMeshPerf).nospace()
            << "[Mesh][minsize] requested h = "
            << in.minSizePolicy.minCellSize
            << (in.minSizeEnforce ? " (enforce)" : " (advisory)")
            << " | achieved min cell side ~ " << side
            << " (area " << minArea << ")";
        if (side < 0.5 * in.minSizePolicy.minCellSize)
            qWarning() << "[Mesh][minsize] the smallest cell is well below the"
                       << "requested minimum — check the residual list above"
                       << "for the features that could not be conditioned.";
    }

    // ── Hilbert renumbering — locality for the engine's explicit marcher ──
    // Pure permutation applied before any index-keyed consumer.  Elevation
    // fill and node mapping are coordinate-keyed; the coupling map is
    // marker-keyed (both permutation-safe).  The engine's cell/vertex index is
    // the file line order, so a well-ordered file benefits the marcher with
    // zero engine changes (meshreorder.h).
    stageClock.restart();
    const double spreadBefore = mesh::meanVertexIndexSpread(result);
    mesh::reorderMeshHilbert(&result);
    const double spreadAfter = mesh::meanVertexIndexSpread(result);
    stageMark("Hilbert reorder");
    qCInfo(lcMeshPerf).nospace()
        << "[Mesh] Hilbert reorder: mean vertex-index spread "
        << spreadBefore << " -> " << spreadAfter
        << " (" << result.triangles.size() << " tris)";

    // ── Elevation fill for all mesh vertices ─────────────────────────
    // Vertices that were PSLG Steiner points (features + terrain) already
    // have their exact z in elevCache.  Only Triangle-inserted refinement
    // vertices need a fresh value — either by DTM sample (preferred) or
    // by inverse-distance interpolation from the seed points.
    //
    // zInModelUnits marks vertices whose z is already in model/mesh units
    // (rim, feature Z, flattened terrain, or IDW from rim seeds) so they are
    // excluded from the raster-unit zConversionFactor multiply below.
    //
    // Quad regions (plan D7): Free-region smoothing already ran inside
    // generate(), so every vertex xy here is final and the z sampled below
    // is the one the mesh keeps — nothing to re-sample after this step.
    progress(70, useDTM
                 ? QObject::tr("Sampling DTM elevations…")
                 : QObject::tr("Interpolating elevations from junction rims…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    QVector<bool> zInModelUnits(result.vertices.size(), false);

    if (useDTM)
    {
        const int nv = result.vertices.size();

        // Pass 1 — resolve elevCache hits (PSLG Steiner vertices); collect
        // the misses (Triangle-inserted refinement vertices) for batching.
        QVector<int>    missIdx;
        QVector<double> missX, missY;
        for (int i = 0; i < nv; ++i)
        {
            const double vx = result.vertices[i].xy.x();
            const double vy = result.vertices[i].xy.y();

            const auto key = keyOf(vx, vy);
            const auto it  = elevCache.constFind(key);
            if (it != elevCache.constEnd())
            {
                result.vertices[i].z = it.value();
                zInModelUnits[i] = modelUnitKeys.contains(key);
                continue;
            }
            missIdx.append(i);
            missX.append(vx);
            missY.append(vy);
        }

        // Pass 2 — one batched CRS transform + banded DTM read per chunk
        // (was a Transform(1,…) + 2×2 RasterIO per miss).  Chunking keeps
        // cancellation responsive on multi-million-vertex meshes.
        stageClock.restart();
        constexpr qsizetype kElevChunk = 2'000'000;
        for (qsizetype base = 0; base < missIdx.size(); base += kElevChunk)
        {
            if (promise.isCanceled())
            {
                if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
                if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);
                fail(QObject::tr("Cancelled.")); return;
            }
            const qsizetype cn = std::min(kElevChunk, missIdx.size() - base);
            // A vertex PROJ cannot convert becomes NaN, sampleMany() answers
            // NaN, and the DEM-coverage fill below interpolates it from its
            // neighbours — same treatment as a NoData hole, which is the right
            // outcome. Logged so a wholesale CRS failure is distinguishable
            // from genuine DEM gaps.
            const qsizetype nFail = transformChecked(meshToDTM, cn,
                                                     missX.data() + base,
                                                     missY.data() + base);
            if (nFail > 0)
                qCWarning(lcMeshPerf)
                    << "[Mesh]" << nFail << "of" << cn
                    << "mesh vertices failed mesh->DTM reprojection;"
                    << "their elevations come from the coverage fill";
            QVector<QPointF> q;
            q.reserve(cn);
            for (qsizetype k = 0; k < cn; ++k)
                q.append(QPointF(missX[base + k], missY[base + k]));
            QVector<double> zs;
            thinner.sampleMany(q, &zs);

            for (qsizetype k = 0; k < cn; ++k)
            {
                const int i = missIdx[base + k];
                result.vertices[i].z = zs[k];  // raster units

                // Refinement vertices near a rim node flatten to its rim z.
                const double fz = flattenZ(result.vertices[i].xy.x(),
                                           result.vertices[i].xy.y());
                if (std::isfinite(fz))
                {
                    result.vertices[i].z = fz;
                    zInModelUnits[i] = true;
                }
            }
        }
        stageMark("elevation fill (batched DTM sampling)");
        qCDebug(lcMeshPerf) << "[Mesh] elevation fill:" << nv << "vertices,"
                            << missIdx.size() << "DTM-sampled misses";
    }
    else
    {
        // No DTM: interpolate vertex z from the scattered seeds (junction rims
        // + 3D feature Z).  Two methods, user-selectable:
        //   IDW              — Shepard's method with configurable power p
        //                      (w = 1/d^p); exactly honours seeds.
        //   Natural neighbour — Sibson / Laplace; smoother, no bullseyes.
        //                      Defined only inside the seed convex hull, so it
        //                      falls back to IDW outside the hull / on failure.
        if (seedXY.isEmpty())
        {
            fail(QObject::tr(
                "No DTM and no junctions with rim elevations inside the "
                "meshing domain — cannot interpolate vertex elevations.\n"
                "Either add a DTM or include at least one junction in "
                "the domain."));
            return;
        }

        const double pw = in.idwPower;   // configurable Shepard exponent

        // Build the natural-neighbour interpolator once (fewer than 3 unique
        // seeds or collinear seeds → nnReady stays false → IDW for everything).
        mesh::NaturalNeighbourInterpolator nn;
        bool nnReady = false;
        if (in.elevInterpMethod == MeshGenerationDialog::ElevInterpMethod::NaturalNeighbour)
        {
            nn.setVariant(in.nnVariant == MeshGenerationDialog::NNVariant::Sibson
                              ? mesh::NaturalNeighbourInterpolator::Variant::Sibson
                              : mesh::NaturalNeighbourInterpolator::Variant::Laplace);
            QString nnErr;
            nnReady = nn.build(seedXY, seedZ, &nnErr);
            if (!nnReady)
                qCDebug(lcMeshPerf) << "[Mesh] natural neighbour unavailable, using IDW:" << nnErr;
        }

        const int nv = result.vertices.size();
        const int ns = seedXY.size();
        for (int i = 0; i < nv; ++i)
        {
            const double vx = result.vertices[i].xy.x();
            const double vy = result.vertices[i].xy.y();

            const auto key = keyOf(vx, vy);
            const auto it  = elevCache.constFind(key);
            if (it != elevCache.constEnd())
            {
                result.vertices[i].z = it.value();
                zInModelUnits[i] = modelUnitKeys.contains(key);
                continue;
            }

            double zval = 0.0;
            bool   haveZ = false;

            // Natural neighbour (inside hull); NaN → fall through to IDW.
            if (nnReady)
            {
                const double zn = nn.interpolate(vx, vy);
                if (std::isfinite(zn)) { zval = zn; haveZ = true; }
            }

            if (!haveZ)
            {
                double wsum = 0.0, zsum = 0.0;
                bool exact = false;
                for (int s = 0; s < ns; ++s)
                {
                    const double dx = seedXY[s].x() - vx;
                    const double dy = seedXY[s].y() - vy;
                    const double d2 = dx*dx + dy*dy;
                    if (d2 < 1e-18)
                    {
                        zval = seedZ[s];
                        exact = true;
                        break;
                    }
                    // power-p IDW: w = 1/d^p = 1/(d2)^(p/2).
                    const double w = 1.0 / std::pow(d2, pw * 0.5);
                    wsum += w;
                    zsum += w * seedZ[s];
                }
                if (!exact)
                    zval = (wsum > 0.0) ? (zsum / wsum) : 0.0;
            }

            result.vertices[i].z = zval;
            // Seeds are rim / feature elevations — model units.
            zInModelUnits[i] = true;

            // Flatten override (in case a node is in range here too).
            const double fz = flattenZ(vx, vy);
            if (std::isfinite(fz)) result.vertices[i].z = fz;

            if ((i & 0x3FFF) == 0 && promise.isCanceled())
            {
                fail(QObject::tr("Cancelled.")); return;
            }
        }
    }

    if (meshToDTM) OGRCoordinateTransformation::DestroyCT(meshToDTM);
    if (dtmToMesh) OGRCoordinateTransformation::DestroyCT(dtmToMesh);

    // ── Vertical unit conversion ─────────────────────────────────────
    // Convert DTM-sampled Z values from the raster's native vertical unit to
    // the requested output vertical unit.  Rim, feature-Z, flattened, and IDW
    // values are already in model units (zInModelUnits) and must NOT be scaled.
    if (in.zConversionFactor != 1.0) {
        const int nv = result.vertices.size();
        for (int i = 0; i < nv; ++i) {
            if (!zInModelUnits[i] && std::isfinite(result.vertices[i].z))
                result.vertices[i].z *= in.zConversionFactor;
        }
    }

    // ── Fill vertices with no DEM coverage ───────────────────────────────
    // Triangle-inserted refinement vertices are re-sampled from the raster in
    // Pass 2 above, and sampleMany() returns NaN by contract for NoData, for
    // points outside the DEM footprint, and on a RasterIO failure. That NaN
    // used to be written straight into MeshVertex::z, from where it reached
    // the INP writer (a literal `nan` in [MESH_VERTICES]) and
    // swmm_2d_set_vertex_z(). A NaN bed elevation does not stay local: the
    // engine's h = eta - z turns every dependent cell NaN too.
    //
    // Fill from the vertices that DID resolve, propagating over the mesh's
    // own edges — each unresolved vertex takes the mean of its resolved
    // neighbours, sweeping until the front stops advancing. Same intent as
    // the no-DTM branch's interpolation, without its cost: seeding
    // NaturalNeighbourInterpolator with the covered set would trigger a
    // second Delaunay triangulation of up to millions of points, and IDW
    // would be O(uncovered x covered). The connectivity is already in hand,
    // so a sweep is O(triangles).
    //
    // Runs AFTER the vertical unit conversion so every z is in output units;
    // averaging earlier would mix raster-unit and model-unit values.
    {
        const int nv = result.vertices.size();
        QVector<int> pending;
        for (int i = 0; i < nv; ++i)
            if (!std::isfinite(result.vertices[i].z)) pending.append(i);

        if (!pending.isEmpty())
        {
            const qsizetype nUncovered = pending.size();
            progress(80, QObject::tr("Filling vertices with no DEM coverage…"));

            // CSR adjacency from the triangle list. Duplicates are left in
            // (a vertex shared by k triangles appears k times), which weights
            // each neighbour by the number of triangles it shares with the
            // centre — an umbrella weighting, and cheaper than deduping.
            // Offsets are qsizetype: 6 x nTriangles overflows int at ~358 M
            // triangles. Vertex indices come pre-validated by MeshGenerator
            // (checked against Triangle's own point count at copy-out), but
            // this is a heap WRITE on the nodata-only path, so guard anyway.
            // A quad (patch cell) contributes its four ring edges, not a
            // diagonal — the same neighbourhood the engine's median dual uses.
            auto cellOk = [nv](const mesh::MeshTriangle &t) {
                const int n = t.vertexCount();
                for (int k = 0; k < n; ++k)
                    if (t.vertex(k) < 0 || t.vertex(k) >= nv) return false;
                return true;
            };
            QVector<qsizetype> off(nv + 1, 0);
            for (const mesh::MeshTriangle &t : result.triangles)
            {
                if (!cellOk(t)) continue;
                const int n = t.vertexCount();
                for (int k = 0; k < n; ++k) off[t.vertex(k) + 1] += 2;
            }
            for (int i = 0; i < nv; ++i) off[i + 1] += off[i];
            QVector<int> adj(off[nv]);
            QVector<qsizetype> cur = off;
            for (const mesh::MeshTriangle &t : result.triangles)
            {
                if (!cellOk(t)) continue;
                const int n = t.vertexCount();
                for (int k = 0; k < n; ++k)
                {
                    const int a = t.vertex(k), b = t.vertex((k + 1) % n);
                    adj[cur[a]++] = b; adj[cur[b]++] = a;
                }
            }

            // Pass A — seed. Jacobi sweeps: collect every fill first, then
            // apply, so the result does not depend on vertex order. One sweep
            // advances the front by one edge, so the sweep count is the hole
            // radius in edges.
            QVector<int> allFilled;
            allFilled.reserve(pending.size());
            int sweeps = 0;
            while (!pending.isEmpty())
            {
                if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
                QVector<int>    filledIdx, stillPending;
                QVector<double> filledZ;
                filledIdx.reserve(pending.size());
                filledZ.reserve(pending.size());
                for (const int i : pending)
                {
                    double sum = 0.0;
                    int    n   = 0;
                    for (qsizetype k = off[i]; k < off[i + 1]; ++k)
                    {
                        const double zn = result.vertices[adj[k]].z;
                        if (std::isfinite(zn)) { sum += zn; ++n; }
                    }
                    if (n > 0) { filledIdx.append(i); filledZ.append(sum / n); }
                    else         stillPending.append(i);
                }
                if (filledIdx.isEmpty()) break;   // front cannot advance
                for (int k = 0; k < filledIdx.size(); ++k)
                    result.vertices[filledIdx[k]].z = filledZ[k];
                allFilled += filledIdx;
                pending = stillPending;
                ++sweeps;
            }

            // Pass B — relax. Pass A propagates inward from the hole boundary
            // and writes each vertex once, so a wide hole comes out flattened
            // toward its centre (measured on a synthetic plane: 5.70 elevation
            // units of error across a 13x13 hole). Relaxing the filled set
            // with the covered vertices held fixed converges to the discrete
            // harmonic interpolant, which carries a linear terrain gradient
            // across the hole instead of collapsing it to the boundary mean
            // (same case: 0.0013). Cost is bounded by the hole size, not the
            // mesh — only previously-unresolved vertices are touched.
            constexpr int    kMaxRelax = 512;
            constexpr double kRelaxTol = 1e-4;   // elevation units
            int relax = 0;
            if (!allFilled.isEmpty())
            {
                QVector<double> next(allFilled.size());
                for (; relax < kMaxRelax; ++relax)
                {
                    if ((relax & 0x3F) == 0 && promise.isCanceled())
                    { fail(QObject::tr("Cancelled.")); return; }
                    for (int k = 0; k < allFilled.size(); ++k)
                    {
                        const int i = allFilled[k];
                        double sum = 0.0;
                        int    n   = 0;
                        for (qsizetype e = off[i]; e < off[i + 1]; ++e)
                        {
                            const double zn = result.vertices[adj[e]].z;
                            if (std::isfinite(zn)) { sum += zn; ++n; }
                        }
                        next[k] = (n > 0) ? sum / n : result.vertices[i].z;
                    }
                    double maxDelta = 0.0;
                    for (int k = 0; k < allFilled.size(); ++k)
                    {
                        double &zi = result.vertices[allFilled[k]].z;
                        maxDelta = std::max(maxDelta, std::fabs(next[k] - zi));
                        zi = next[k];
                    }
                    if (maxDelta < kRelaxTol) break;
                }
            }

            if (!pending.isEmpty())
            {
                // A whole connected component sampled no finite elevation —
                // there is nothing to interpolate from, so fabricating a
                // value here would be inventing terrain.
                fail(QObject::tr(
                    "%1 mesh vertices lie outside the DEM footprint (or on "
                    "NoData) with no elevation-bearing neighbour to "
                    "interpolate from — an entire region of the mesh has no "
                    "terrain coverage.\n"
                    "Extend the DEM to cover the meshing domain, or shrink "
                    "the domain to the DEM extent.").arg(pending.size()));
                return;
            }

            qCInfo(lcMeshPerf) << "[Mesh] DEM coverage fill:" << nUncovered
                               << "vertices with no DEM sample interpolated"
                               << "from neighbours —" << sweeps
                               << "seed sweep(s)," << relax
                               << "relaxation iteration(s)";
        }
    }

    // Note: XY are written in project-CRS units (no GUI-side conversion).
    // The engine multiplies by 0.3048 in SurfaceRouter2D::initialize when
    // SWMM FLOW_UNITS is US.  When the engine has been updated to honour
    // `;; UNITS: SI (m)`, a future producer could opt into SI on disk.

    // ── CouplingMap ──────────────────────────────────────────────────
    // Marker lookup covers the junctions-as-Steiner path (exact coincidence
    // by construction). The decoupled path (Plan Part B) runs the node
    // mapper below instead/in addition.
    mesh::CouplingMap coupling;
    for (int i = 0; i < result.vertices.size(); ++i)
    {
        const QString tag = in.nodeMarkerToTag.value(result.vertices[i].marker);
        if (!tag.isEmpty())
        {
            coupling.vertexToNode.insert(i, tag);
            // Mirror onto the vertex now so the mapper's preserve-existing
            // check sees marker-coupled vertices.
            result.vertices[i].coupledNode = tag;
        }
    }
    for (int i = 0; i < result.triangles.size(); ++i)
    {
        const QString &tag = result.triangles[i].tag;
        if (!tag.isEmpty() && tag.startsWith(QStringLiteral("subcatch_")))
            coupling.triangleToNode.insert(i, tag.mid(int(qstrlen("subcatch_"))));
    }

    // ── Node→mesh mapping (Plan Part B/C) ────────────────────────────
    // Coincident nodes → vertex coupling; interior nodes → containing cell
    // (several nodes may share one cell); outside nodes are skipped here —
    // the toolbar's Remap action reports them interactively.
    if (in.mapNodesAfterGen && !in.couplingNodes.isEmpty())
    {
        progress(82, QObject::tr("Mapping 1D nodes to the mesh…"));
        const mesh::NodeMapResult nm = mesh::mapNodesToMesh(
            result, in.couplingNodes, -1.0, /*preserveExisting=*/true);
        for (auto it = nm.vertexMatches.cbegin();
             it != nm.vertexMatches.cend(); ++it)
        {
            coupling.vertexToNode.insert(it.key(), it.value());
            result.vertices[it.key()].coupledNode = it.value();
        }
        result.cellCouplings += nm.cellMatches;
        qCInfo(lcMeshPerf) << "[Mesh] node mapping:"
                           << nm.vertexMatches.size() << "vertex-coupled,"
                           << nm.cellMatches.size() << "cell-coupled,"
                           << nm.skippedExisting.size() << "already coupled,"
                           << nm.unmatched.size() << "outside mesh,"
                           << nm.sharedCells << "shared cell(s)";
    }

    // ── Seed per-cell hydraulic attributes ───────────────────────────
    // Author the dialog's constant values onto the triangles themselves, not
    // just into the written file: the layer built from this MeshResult is what
    // the toolbar / properties panel read and what a later save patches, so
    // leaving them unset (NaN) makes a generated mesh report defaults it never
    // agreed to and drops the file's values on the next attribute rewrite.
    // GG0d — a region row that was given its own roughness / depth overrides
    // the '*' values on that region's cells. regionHydraulics is empty unless
    // the user edited one, in which case this loop is exactly the loop it has
    // always been.
    for (mesh::MeshTriangle &t : result.triangles)
    {
        t.mannings  = in.manningsN;
        t.initDepth = in.initDepth;

        if (in.regionHydraulics.isEmpty() || t.tag.isEmpty()) continue;
        const auto rh = in.regionHydraulics.constFind(t.tag);
        if (rh == in.regionHydraulics.constEnd()) continue;
        t.mannings  = rh->manningsN;
        t.initDepth = rh->initDepth;
    }

    // ── Region infiltration defaults (GG0d, GUI plan §3.3) ───────────
    // Copied across as ROWS, not stamped per cell: engine decision D-I3 has
    // the engine resolve `override > tag row > '*' row > none` itself, and
    // materialising a per-cell row for every triangle here would flatten that
    // inheritance and freeze the assignment. result.infilOverrides stays
    // untouched — mesh generation authors no per-cell infiltration at all.
    result.infilDefaults = in.infilDefaults;

    // ── Tri-pair merge into quads (G2) ───────────────────────────────
    // Last geometry step: runs on sampled elevations and seeded attributes
    // so the planarity and attribute-equality rules see final values.
    // Every constrained segment (domain outline, hole rings, breaklines,
    // patch boundaries) is a locked edge — no quad straddles one.
    const int nPatchQuads = result.quadCount();
    int nMergedQuads = 0;
    if (in.genOpts.mergeTrianglePairs)
    {
        progress(84, QObject::tr("Merging triangle pairs into quads…"));
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        QSet<QPair<int, int>> locked;
        locked.reserve(result.boundaryEdges.size());
        for (const mesh::MeshEdge &e : std::as_const(result.boundaryEdges))
            locked.insert(mesh::edgeKey(e.v0, e.v1));
        QVector<int> oldToNew;
        nMergedQuads = mesh::mergeTrianglePairs(result, in.genOpts.quadMerge,
                                                locked, &oldToNew);
        if (nMergedQuads > 0 && !coupling.triangleToNode.isEmpty())
        {
            QHash<int, QString> remapped;
            for (auto it = coupling.triangleToNode.cbegin();
                 it != coupling.triangleToNode.cend(); ++it)
                remapped.insert(oldToNew.value(it.key(), it.key()), it.value());
            coupling.triangleToNode = std::move(remapped);
        }
        stageMark("tri-pair merge");
    }
    qCInfo(lcMeshPerf).nospace()
        << "[Mesh] cells: " << (result.triangles.size() - result.quadCount())
        << " triangles + " << result.quadCount() << " quads ("
        << nQuadRegionQuads << " from quad regions, "
        << (nPatchQuads - nQuadRegionQuads) << " from structured patches, "
        << nMergedQuads << " merged from triangle pairs)";

    // ── Write ────────────────────────────────────────────────────────
    progress(85, QObject::tr("Writing mesh file…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    mesh::InpMeshWriter::UnitInfo units;
    units.linearUnitName = in.meshLinearUnitName;  // ;; UNITS:
    units.sourceCrsTag   = in.meshCRSTag;          // ;; SOURCE_CRS:

    QString writeErr;
    if (!mesh::InpMeshWriter::write(in.outputMode, in.inpPath, in.meshOutputPath,
                                     result, coupling, in.manningsN, &writeErr,
                                     units))
    {
        fail(QObject::tr("Write failed: %1").arg(writeErr)); return;
    }

    progress(95, QObject::tr("Done — adding layer…"));

    PResult out;
    out.ok         = true;
    out.meshResult = std::move(result);
    out.coupling   = std::move(coupling);
    out.meshPath   = (in.outputMode == mesh::MeshOutputMode::External)
                         ? in.meshOutputPath : QString();
    out.outputMode = in.outputMode;
    promise.addResult(std::move(out));
}

// QtConcurrent stores an exception thrown by the worker into the future and
// rethrows it on the GUI thread at result() — uncaught, that terminates the
// application with no message. On Windows a large DEM can drive an allocation
// past the COMMIT limit (physical memory still ~50%), so std::bad_alloc here
// was killing the app "without warning" mid-thinning. Convert every exception
// into an ordinary failed PipelineResult; the dialog's progress label still
// names the stage that was running.
static void
runMeshPipeline(QPromise<MeshGenerationDialog::PipelineResult> &promise,
                MeshGenerationDialog::PipelineInputs            in)
{
    using PResult = MeshGenerationDialog::PipelineResult;
    auto failWith = [&](const QString &msg) {
        PResult r; r.ok = false; r.errorMsg = msg;
        promise.addResult(r);
    };
    try {
        runMeshPipelineImpl(promise, std::move(in));
    } catch (const std::bad_alloc &) {
        failWith(QObject::tr(
            "Out of memory: the mesh pipeline exceeded available memory "
            "(on Windows this is the commit limit, which can trip while "
            "physical memory still shows headroom). Increase the grid "
            "spacing, enable terrain thinning, or reduce the domain "
            "extent, then try again."));
    } catch (const std::exception &e) {
        failWith(QObject::tr("Mesh pipeline failed: %1")
                     .arg(QString::fromUtf8(e.what())));
    } catch (...) {
        failWith(QObject::tr("Mesh pipeline failed with an unknown error."));
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MeshGenerationDialog::MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                           QWidget *parent)
    : QDialog(parent),
      m_pw(pw)
{
    setWindowTitle(tr("Generate 2D Mesh"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("MeshGenerationDialog"));
    // Compact default — the tab pages scroll (see buildUi), so the window no
    // longer has to be tall enough to show the tallest page in full.
    resize(540, 560);
    setMinimumHeight(420);
    buildUi();
    seedDefaults();

    // Keep suffix labels and defaults in sync if the user somehow changes
    // flow units while the dialog is open.
    connect(UnitSystem::instance(), &UnitSystem::unitsChanged,
            this, [this]() { updateUnitDisplay(); });
}

MeshGenerationDialog::~MeshGenerationDialog()
{
    if (m_watcher && m_watcher->isRunning())
        m_watcher->cancel();
}

// ---------------------------------------------------------------------------
// UI build
// ---------------------------------------------------------------------------

void MeshGenerationDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // ── Tab widget ──────────────────────────────────────────────────
    auto *tabs = new QTabWidget(this);

    // ================================================================
    // Tab 1 — Sources
    // "What data feeds in": terrain, GIS layers, SWMM coupling
    // ================================================================
    auto *sourcesPage = new QWidget;
    auto *sourcesVBox = new QVBoxLayout(sourcesPage);
    sourcesVBox->setContentsMargins(8, 8, 8, 8);

    // Sources group
    {
        auto *g = new QGroupBox(tr("Sources"), sourcesPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_dtmCombo = new QComboBox(g);
        m_dtmCombo->setToolTip(tr(
            "DTM raster used to sample vertex elevations and drive\n"
            "terrain-adaptive thinning. Optional — when set to (none)\n"
            "the mesh is generated without terrain Steiner points and\n"
            "vertex z is interpolated (IDW) from SWMM junction rim\n"
            "elevations (invert + maxDepth)."));
        f->addRow(tr("&DTM raster:"), m_dtmCombo);

        // Auto-detected DTM vertical unit (informational)
        m_dtmVertUnitLabel = new QLabel(tr("—"), g);
        m_dtmVertUnitLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
        m_dtmVertUnitLabel->setToolTip(tr("Vertical unit detected from the DTM raster's embedded CRS metadata."));
        f->addRow(tr("DTM vertical unit:"), m_dtmVertUnitLabel);

        // Output mesh vertical CRS
        m_meshVertCRSCombo = new QComboBox(g);
        m_meshVertCRSCombo->setToolTip(tr(
            "Vertical unit for Z values written to the output mesh.\n"
            "Choose to match the SWMM model's unit system so that node invert\n"
            "elevations, crown elevations, and mesh Z values are consistent.\n"
            "'Match flow units' converts automatically from the DTM vertical unit."));
        m_meshVertCRSCombo->addItem(tr("Match flow units (auto-convert)"), QStringLiteral("auto"));
        m_meshVertCRSCombo->addItem(tr("Metres (m)"),                      QStringLiteral("m"));
        m_meshVertCRSCombo->addItem(tr("Feet (ft)"),                       QStringLiteral("ft"));
        f->addRow(tr("Mesh vertical unit:"), m_meshVertCRSCombo);

        // Z conversion factor — auto-populated, user-editable override.
        m_zFactorSpin = new QDoubleSpinBox(g);
        m_zFactorSpin->setRange(0.0001, 10000.0);
        m_zFactorSpin->setDecimals(6);
        m_zFactorSpin->setSingleStep(0.001);
        m_zFactorSpin->setValue(1.0);
        m_zFactorSpin->setToolTip(
            tr("Multiplication factor applied to every raw DTM Z value before\n"
               "it is written to the mesh.\n"
               "Auto-computed from the DTM vertical unit and the chosen mesh\n"
               "vertical unit; override here when the auto-detected value is wrong.\n"
               "MeshZ = DTM_Z \xc3\x97 factor"));
        f->addRow(tr("Z conversion (\xc3\x97):"), m_zFactorSpin);

        // Update detected label, auto-select mesh unit, and recompute factor when DTM changes.
        connect(m_dtmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]() {
                    auto *raster = qobject_cast<GISRasterLayer *>(
                        static_cast<QObject *>(m_dtmCombo->currentData().value<void *>()));
                    if (raster) {
                        const QString unit = raster->detectVerticalUnit();
                        m_dtmVertUnitLabel->setText(
                            unit == QLatin1String("ft") ? tr("ft (feet)") : tr("m (metres)"));
                    } else {
                        m_dtmVertUnitLabel->setText(tr("—"));
                    }
                    // Elevation-interpolation options only apply with no DTM.
                    if (m_elevInterpGroup)
                        m_elevInterpGroup->setEnabled(raster == nullptr);
                    updateZFactor();
                });

        connect(m_meshVertCRSCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateZFactor(); });

        m_domainLabel = new QLabel(g);
        m_domainLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
        f->addRow(tr("Domain:"), m_domainLabel);

        sourcesVBox->addWidget(g);
    }

    // Auxiliary feature layers group
    {
        auto *g   = new QGroupBox(tr("Auxiliary feature layers (optional)"), sourcesPage);
        auto *lay = new QVBoxLayout(g);

        auto *boundaryRow = new QHBoxLayout;
        boundaryRow->addWidget(new QLabel(tr("&Boundary polygon:"), g));
        m_boundaryLayerCombo = new QComboBox(g);
        m_boundaryLayerCombo->setToolTip(tr(
            "Polygon layer whose features define the meshing boundary.  "
            "Interior rings (holes) in those polygons are respected — "
            "Triangle leaves those regions unmeshed.  When (none), the mesh "
            "domain falls back to the SWMM model bounding rectangle + 5%."));
        boundaryRow->addWidget(m_boundaryLayerCombo, 1);
        lay->addLayout(boundaryRow);

        lay->addWidget(new QLabel(tr("Constraining &points (check to include):"), g));
        m_pointLayersList = new QListWidget(g);
        m_pointLayersList->setToolTip(tr("Every feature in each checked layer is added as a Steiner point."));
        m_pointLayersList->setMaximumHeight(100);
        m_pointLayersList->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(m_pointLayersList);

        lay->addWidget(new QLabel(tr("Constraining &lines (check to include):"), g));
        m_lineLayersList = new QListWidget(g);
        m_lineLayersList->setToolTip(tr("Every feature in each checked layer becomes a constraint segment."));
        m_lineLayersList->setMaximumHeight(100);
        m_lineLayersList->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(m_lineLayersList);

        sourcesVBox->addWidget(g);
    }

    // 1D geometry-influence group (Plan Part B — decoupled from coupling:
    // these checkboxes only shape the PSLG; the 1D↔2D coupling itself is
    // authored by the post-generation mapper / the toolbar's Remap action).
    {
        auto *g   = new QGroupBox(tr("1D geometry influence (optional)"), sourcesPage);
        auto *lay = new QVBoxLayout(g);

        m_includeJunctions = new QCheckBox(tr("Junctions / outfalls / storage  →  Steiner vertices  (tag = node id)"), g);
        m_includeJunctions->setToolTip(tr(
            "Force a mesh vertex at every node location. Node clusters that\n"
            "are close only for non-physical reasons (weir / orifice / pump\n"
            "endpoints) then force very small cells around them.\n\n"
            "Leave unchecked (default) to let mesh quality drive the cell\n"
            "sizes; nodes are coupled to the mesh afterwards (coincident →\n"
            "vertex, otherwise → containing cell)."));
        m_includeConduits  = new QCheckBox(tr("Conduits  →  constraint segments  (marker = conduit id)"), g);
        m_includeSubcatch  = new QCheckBox(tr("Subcatchments  →  triangle regions  (tag = subcatchment id)"), g);

        lay->addWidget(m_includeJunctions);

        // Node elevation source: interpolate to terrain (default) vs rim.
        m_nodesUseRim = new QCheckBox(
            tr("Use node rim elevation (invert + max depth) instead of terrain"), g);
        m_nodesUseRim->setToolTip(tr(
            "Unchecked (default): node vertices are interpolated from the DTM,\n"
            "like every other vertex.\n"
            "Checked: node vertices are pinned to the rim elevation\n"
            "(invert + maximum depth) read from the SWMM model.\n\n"
            "When no DTM is selected, nodes always use rim elevation and\n"
            "the rest of the mesh is interpolated (IDW) from those rims."));
        auto *rimRow = new QHBoxLayout;
        rimRow->setContentsMargins(20, 0, 0, 0);  // indent under junctions row
        rimRow->addWidget(m_nodesUseRim);
        lay->addLayout(rimRow);

        // Flatten radius: terrain within this distance of a rim node is forced
        // to the node's rim elevation, removing slivers from terrain/rim
        // misalignment.  Only meaningful when nodes use rim elevation.
        auto *flatRow = new QHBoxLayout;
        flatRow->setContentsMargins(20, 0, 0, 0);
        flatRow->addWidget(new QLabel(tr("Flatten terrain within radius:"), g));
        m_nodeFlattenSpin = new QDoubleSpinBox(g);
        m_nodeFlattenSpin->setRange(0.0, 1e9);
        m_nodeFlattenSpin->setDecimals(3);
        m_nodeFlattenSpin->setSingleStep(1.0);
        m_nodeFlattenSpin->setSpecialValueText(tr("(off)"));
        // suffix set by updateUnitDisplay()
        m_nodeFlattenSpin->setToolTip(tr(
            "Radius around each rim node within which all DTM terrain points\n"
            "are forced to that node's rim elevation.  Prevents unnecessarily\n"
            "small triangles where the terrain and rim elevations disagree.\n"
            "0 = off.  Applies only when nodes use rim elevation."));
        flatRow->addWidget(m_nodeFlattenSpin);
        flatRow->addStretch();
        lay->addLayout(flatRow);

        // Minimum node separation: nodes closer than this to an already-kept
        // node are not pinned as mesh vertices — close pinned vertices force
        // tiny triangles in the initial constrained triangulation, before any
        // quality option can act.  Demoted nodes stay coupled via their
        // containing cell (post-generation mapper).
        auto *sepRow = new QHBoxLayout;
        sepRow->setContentsMargins(20, 0, 0, 0);
        m_nodeMinSepBox = new QCheckBox(tr("Enforce minimum node separation:"), g);
        m_nodeMinSepBox->setToolTip(tr(
            "When two nodes are closer than this distance, only the first\n"
            "(junctions → outfalls → storage → dividers, model order) keeps a\n"
            "pinned mesh vertex; the others are coupled to their containing\n"
            "CELL instead ([2D_TRIANGLE_NODE_MAP]).  Prevents clusters of tiny\n"
            "triangles where manholes sit centimetres apart.\n\n"
            "Requires \"Map model nodes to the mesh after generation\" so the\n"
            "demoted nodes still get coupled.  Note: if conduits are included\n"
            "as constraints, their endpoints can still pin vertices at node\n"
            "locations regardless of this setting."));
        m_nodeMinSepSpin = new QDoubleSpinBox(g);
        m_nodeMinSepSpin->setRange(0.0, 1e9);
        m_nodeMinSepSpin->setDecimals(3);
        m_nodeMinSepSpin->setSingleStep(1.0);
        // suffix set by updateUnitDisplay()
        sepRow->addWidget(m_nodeMinSepBox);
        sepRow->addWidget(m_nodeMinSepSpin);
        sepRow->addStretch();
        lay->addLayout(sepRow);

        lay->addWidget(m_includeConduits);
        lay->addWidget(m_includeSubcatch);

        auto syncCoupling = [this]{
            const bool jc = m_includeJunctions->isChecked();
            m_nodesUseRim->setEnabled(jc);
            m_nodeFlattenSpin->setEnabled(jc && m_nodesUseRim->isChecked());
            m_nodeMinSepBox->setEnabled(jc);
            m_nodeMinSepSpin->setEnabled(jc && m_nodeMinSepBox->isChecked());
        };
        connect(m_includeJunctions, &QCheckBox::toggled, this, syncCoupling);
        connect(m_nodesUseRim,      &QCheckBox::toggled, this, syncCoupling);
        connect(m_nodeMinSepBox,    &QCheckBox::toggled, this, syncCoupling);
        syncCoupling();

        sourcesVBox->addWidget(g);
    }

    // 1D↔2D coupling group (Plan Part B) — the mapping itself, decoupled
    // from generation. Re-runnable anytime via the mesh toolbar.
    {
        auto *g   = new QGroupBox(tr("1D ↔ 2D coupling"), sourcesPage);
        auto *lay = new QVBoxLayout(g);
        m_mapNodesAfterGen = new QCheckBox(
            tr("Map model nodes to the mesh after generation "
               "(re-runnable from the Mesh toolbar)"), g);
        m_mapNodesAfterGen->setToolTip(tr(
            "After the mesh is built, couple every SWMM node to it:\n"
            "nodes coincident with a mesh vertex use vertex coupling;\n"
            "other nodes inside the mesh couple to their containing cell\n"
            "(several nodes may share one cell — e.g. weir endpoints).\n"
            "Nodes outside the mesh are reported."));
        lay->addWidget(m_mapNodesAfterGen);
        sourcesVBox->addWidget(g);
    }

    // Elevation interpolation (no-DTM fallback) group
    {
        m_elevInterpGroup = new QGroupBox(tr("Elevation interpolation (no DTM)"), sourcesPage);
        m_elevInterpGroup->setToolTip(tr(
            "How mesh-vertex elevations are interpolated from the seed points\n"
            "(junction rims and 3D feature Z) when no DTM raster is selected.\n"
            "Ignored when a DTM is set."));
        auto *f = new QFormLayout(m_elevInterpGroup);

        m_elevMethodCombo = new QComboBox(m_elevInterpGroup);
        m_elevMethodCombo->addItem(tr("Inverse distance weighting (IDW)"),
                                   int(ElevInterpMethod::IDW));
        m_elevMethodCombo->addItem(tr("Natural neighbour"),
                                   int(ElevInterpMethod::NaturalNeighbour));
        f->addRow(tr("Method:"), m_elevMethodCombo);

        m_nnVariantCombo = new QComboBox(m_elevInterpGroup);
        m_nnVariantCombo->addItem(tr("Sibson (area-stealing)"),
                                  int(NNVariant::Sibson));
        m_nnVariantCombo->addItem(tr("Laplace (edge-ratio)"),
                                  int(NNVariant::Laplace));
        m_nnVariantCombo->setToolTip(tr(
            "Sibson: smooth area-based natural-neighbour coordinates.\n"
            "Laplace: faster non-Sibsonian edge/distance ratio.\n"
            "Both fall back to IDW outside the seed convex hull."));
        f->addRow(tr("NN variant:"), m_nnVariantCombo);

        m_idwPowerSpin = new QDoubleSpinBox(m_elevInterpGroup);
        m_idwPowerSpin->setRange(0.1, 10.0);
        m_idwPowerSpin->setDecimals(2);
        m_idwPowerSpin->setSingleStep(0.5);
        m_idwPowerSpin->setToolTip(tr(
            "Shepard exponent p for IDW: weight = 1 / distance^p.\n"
            "Higher p → sharper, more local influence. Default 2.\n"
            "Also used as the natural-neighbour fallback outside the hull."));
        f->addRow(tr("IDW power:"), m_idwPowerSpin);

        auto syncElevInterp = [this]{
            const bool nn = m_elevMethodCombo->currentData().toInt()
                            == int(ElevInterpMethod::NaturalNeighbour);
            m_nnVariantCombo->setEnabled(nn);
        };
        connect(m_elevMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [syncElevInterp](int){ syncElevInterp(); });
        syncElevInterp();

        sourcesVBox->addWidget(m_elevInterpGroup);
    }

    sourcesVBox->addStretch();
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(sourcesPage, tabs),
                 tr("S&ources"));

    // ================================================================
    // Tab 2 — Quality
    // "How to triangulate": Triangle knobs, PSLG opts, terrain thinning
    // ================================================================
    // MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07 §2 — the eight
    // groups below totalled 37 form rows plus a table (~1700 px) in one
    // column, so the page only fit because addTab wraps it in a scroll area.
    // They are now split across four inner tabs in pipeline order: size the
    // triangles, enforce a floor, thin the terrain, decide on quads. Each
    // group is moved WHOLE — no widget is renamed and no group is split, so
    // test_meshmincelldialog (which finds "Minimum Cell Size" by title and
    // asserts its 2-spin / 4-checkbox census) is unaffected.
    //
    // Every group is still constructed with `qualityPage` as its ctor parent;
    // QLayout::addWidget reparents it to the inner page that owns the layout,
    // which keeps this a minimal diff against the uncommitted quad work.
    auto *qualityPage = new QWidget;
    auto *qualityOuter = new QVBoxLayout(qualityPage);
    qualityOuter->setContentsMargins(0, 0, 0, 0);

    auto *qualityTabs = new QTabWidget(qualityPage);
    qualityTabs->setObjectName(QStringLiteral("meshQualityTabs"));
    qualityOuter->addWidget(qualityTabs);

    // Inner page + layout per tab. Named for the group they receive so the
    // addWidget lines below read as their own documentation.
    auto *sizingPage  = new QWidget(qualityTabs);
    auto *sizingVBox  = new QVBoxLayout(sizingPage);
    sizingVBox->setContentsMargins(8, 8, 8, 8);

    auto *cellSizePage = new QWidget(qualityTabs);
    auto *cellSizeVBox = new QVBoxLayout(cellSizePage);
    cellSizeVBox->setContentsMargins(8, 8, 8, 8);

    auto *terrainPage = new QWidget(qualityTabs);
    auto *terrainVBox = new QVBoxLayout(terrainPage);
    terrainVBox->setContentsMargins(8, 8, 8, 8);

    auto *quadsPage   = new QWidget(qualityTabs);
    auto *quadsVBox   = new QVBoxLayout(quadsPage);
    quadsVBox->setContentsMargins(8, 8, 8, 8);

    // Triangle quality group
    {
        auto *g = new QGroupBox(tr("Triangle quality"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_maxAreaSpin = new QDoubleSpinBox(g);
        m_maxAreaSpin->setRange(0.0, 1e12);
        m_maxAreaSpin->setDecimals(2);
        m_maxAreaSpin->setSpecialValueText(tr("(no cap)"));
        // tooltip updated by updateUnitDisplay()
        f->addRow(tr("Max trian&gle area:"), m_maxAreaSpin);

        m_minAngleSpin = new QDoubleSpinBox(g);
        m_minAngleSpin->setRange(0.0, 33.0);
        m_minAngleSpin->setDecimals(1);
        m_minAngleSpin->setSuffix(QStringLiteral(" °"));
        m_minAngleSpin->setToolTip(tr("Minimum triangle angle (0–33° reliable; above 33° may not terminate)."));
        f->addRow(tr("Min angle:"), m_minAngleSpin);

        // V2 graded sizing (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN).
        m_gradationSpin = new QDoubleSpinBox(g);
        m_gradationSpin->setRange(0.0, 2.0);
        m_gradationSpin->setDecimals(2);
        m_gradationSpin->setSingleStep(0.05);
        m_gradationSpin->setSpecialValueText(tr("(uniform)"));
        m_gradationSpin->setToolTip(tr(
            "How fast cells may grow with distance from constrained features "
            "(conduits, hole edges, SWMM nodes).\n\n"
            "0 = uniform: the max-area cap applies EVERYWHERE, however far a "
            "cell is from anything that needs resolution.\n\n"
            "> 0 = graded: cells at the features keep exactly the max-area "
            "cap; away from them the permitted size grows at this slope, so "
            "the mesh stays fine where it matters and coarsens smoothly "
            "elsewhere — typically far fewer cells for the same feature "
            "resolution.  0.25 is a good starting value.\n\n"
            "Requires a max triangle area cap."));
        f->addRow(tr("Size gradation:"), m_gradationSpin);

        auto syncGradation = [this] {
            if (m_gradationSpin && m_maxAreaSpin)
                m_gradationSpin->setEnabled(m_maxAreaSpin->value() > 0.0);
        };
        connect(m_maxAreaSpin, &QDoubleSpinBox::valueChanged, this, syncGradation);
        syncGradation();

        m_maxSteinerSpin = new QSpinBox(g);
        m_maxSteinerSpin->setRange(-1, 10'000'000);
        m_maxSteinerSpin->setSpecialValueText(tr("(unlimited)"));
        f->addRow(tr("Max Steiner points:"), m_maxSteinerSpin);

        m_allowSteiner = new QCheckBox(tr("Allow Steiner refinement on boundary"), g);
        f->addRow(QString(), m_allowSteiner);

        sizingVBox->addWidget(g);   // Triangle quality
    }

    // PSLG optimisation group
    {
        auto *g = new QGroupBox(tr("PSLG Optimisation"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_simplifyEpsSpin = new QDoubleSpinBox(g);
        m_simplifyEpsSpin->setRange(0.0, 1000.0);
        m_simplifyEpsSpin->setDecimals(3);
        m_simplifyEpsSpin->setSingleStep(0.1);
        // suffix set by updateUnitDisplay()
        m_simplifyEpsSpin->setSpecialValueText(tr("(off)"));
        m_simplifyEpsSpin->setToolTip(tr(
            "Ramer-Douglas-Peucker tolerance applied to every polygon ring and "
            "polyline path before it enters Triangle.\n\n"
            "Points that deviate less than this distance from a straight line "
            "between their neighbours are removed.  0 = disabled.\n"
            "Typical: 0.1 m (tight) – 1.0 m (coarse).\n\n"
            "Increase ε when the boundary's vertices are much denser than the "
            "terrain point spacing — short constrained boundary segments seed "
            "small cells along the perimeter."));
        f->addRow(tr("Geometry simplification &ε:"), m_simplifyEpsSpin);

        m_snapEpsSpin = new QDoubleSpinBox(g);
        m_snapEpsSpin->setRange(0.0, 100.0);
        m_snapEpsSpin->setDecimals(4);
        m_snapEpsSpin->setSingleStep(0.01);
        // suffix set by updateUnitDisplay()
        m_snapEpsSpin->setSpecialValueText(tr("(off)"));
        m_snapEpsSpin->setToolTip(tr(
            "Radius within which near-coincident untagged Steiner points are "
            "merged into one.  SWMM-tagged points are never merged.\n"
            "0 = disabled.  Typical: 0.01–0.5 m."));
        f->addRow(tr("Steiner snap radius:"), m_snapEpsSpin);

        // 2026-07-19 — optional boundary densification. Splits domain/hole
        // ring edges longer than this into equal parts AFTER RDP
        // simplification (pure vertex insertion, geometry unchanged) so the
        // perimeter cell size can match the interior terrain spacing instead
        // of Triangle spanning long constrained edges with oversized fans.
        m_maxBoundaryEdgeBox = new QCheckBox(tr("Max boundary edge length:"), g);
        m_maxBoundaryEdgeBox->setToolTip(tr(
            "When checked, boundary and hole ring edges longer than this are "
            "split into equal parts after RDP simplification.\n\n"
            "Pure vertex insertion — the boundary geometry is unchanged.  "
            "Use to make perimeter cell size follow the interior target "
            "spacing on coarse boundaries."));
        m_maxBoundaryEdgeSpin = new QDoubleSpinBox(g);
        m_maxBoundaryEdgeSpin->setRange(0.0, 1e9);
        m_maxBoundaryEdgeSpin->setDecimals(2);
        m_maxBoundaryEdgeSpin->setSingleStep(5.0);
        // suffix set by updateUnitDisplay()
        m_maxBoundaryEdgeSpin->setSpecialValueText(tr("(off)"));
        f->addRow(m_maxBoundaryEdgeBox, m_maxBoundaryEdgeSpin);
        connect(m_maxBoundaryEdgeBox, &QCheckBox::toggled,
                m_maxBoundaryEdgeSpin, &QWidget::setEnabled);

        sizingVBox->addWidget(g);   // PSLG Optimisation
    }

    // Minimum cell size group — MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.
    {
        auto *g = new QGroupBox(tr("Minimum Cell Size"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_minCellSizeSpin = new QDoubleSpinBox(g);
        m_minCellSizeSpin->setRange(0.0, 1e6);
        m_minCellSizeSpin->setDecimals(3);
        m_minCellSizeSpin->setSingleStep(0.5);
        // suffix set by updateUnitDisplay()
        m_minCellSizeSpin->setSpecialValueText(tr("(off)"));
        m_minCellSizeSpin->setToolTip(tr(
            "Smallest cell the mesh should contain, as a length.\n\n"
            "Triangle cannot produce cells much smaller OR much larger than the "
            "input geometry asks for: constrained polylines with vertices a few "
            "centimetres apart, two alignments passing within a hair, or conduits "
            "meeting at a sharp angle all force cells at that scale, and on the "
            "2D solver a single sliver sets the timestep for the whole domain.\n\n"
            "Setting a minimum therefore CHANGES THE INPUT GEOMETRY slightly: "
            "vertices closer together than this are merged, short segments are "
            "resampled away, dangling endpoints are welded onto the line they "
            "nearly touch, and sharp corners are blunted.  Tagged SWMM nodes are "
            "never moved and never merged with each other.\n\n"
            "0 = off (no geometry changes; existing behaviour)."));
        f->addRow(tr("Minimum cell si&ze:"), m_minCellSizeSpin);

        m_minCellSuggestBtn = new QPushButton(tr("Suggest"), g);
        m_minCellSuggestBtn->setToolTip(tr(
            "Set the minimum to roughly a third of the side length implied by "
            "Max triangle area."));
        f->addRow(QString(), m_minCellSuggestBtn);
        connect(m_minCellSuggestBtn, &QPushButton::clicked, this, [this] {
            const double a = m_maxAreaSpin ? m_maxAreaSpin->value() : 0.0;
            if (a <= 0.0 || !m_minCellSizeSpin) return;
            // Side of the equilateral triangle with that area, then a third.
            const double side = std::sqrt(4.0 * a / std::sqrt(3.0));
            m_minCellSizeSpin->setValue(side / 3.0);
        });

        // V2 enforcement mode (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN
        // Track A).  Measured on real SWMM models, the default (advisory)
        // conditioning is nearly inert: the crowded vertices are almost all
        // coupling identities it is forbidden to touch.
        m_minSizeEnforceBox = new QCheckBox(
            tr("Enforce — may move or merge SWMM coupling points"), g);
        m_minSizeEnforceBox->setToolTip(tr(
            "Off (advisory): tagged SWMM nodes and conduit endpoints never "
            "move and never merge.  On real models nearly every crowded "
            "vertex IS such an identity, so the minimum is rarely achieved.\n\n"
            "On (enforce): two coupling identities closer than the minimum "
            "cell size may merge into one mesh vertex, nodes closer than the "
            "minimum are not pinned as vertices at all, and the post-mesh "
            "cleanup may absorb slivers into an identity vertex.  No coupling "
            "is ever LOST — a merged or demoted node couples to the 2D mesh "
            "via its containing cell instead of a dedicated vertex (a "
            "different exchange stencil, reported in the log).  No point "
            "moves further than the weld radius shown below."));
        f->addRow(QString(), m_minSizeEnforceBox);

        m_trimAngleSpin = new QDoubleSpinBox(g);
        m_trimAngleSpin->setRange(0.0, 60.0);
        m_trimAngleSpin->setDecimals(1);
        m_trimAngleSpin->setSuffix(QStringLiteral(" °"));
        m_trimAngleSpin->setSpecialValueText(tr("(off)"));
        m_trimAngleSpin->setToolTip(tr(
            "Corners where two constraints meet more sharply than this are "
            "blunted — the apex is cut back and bridged by a short segment.\n\n"
            "Sharp input angles are the one cause of small cells that merging "
            "cannot fix, because the two legs legitimately share their vertex; "
            "the cells at such an apex shrink geometrically toward it.\n\n"
            "Corners at tagged SWMM nodes are left alone (see below)."));
        f->addRow(tr("Trim corners sharper than:"), m_trimAngleSpin);

        m_trimAtNodesBox = new QCheckBox(tr("Also trim corners at SWMM nodes"), g);
        m_trimAtNodesBox->setToolTip(tr(
            "Off by default.  A manhole where two conduits meet at a sharp angle "
            "is exactly where fine resolution is usually wanted, and the node is "
            "a coupling location that must not move.\n\n"
            "Turn on when the simulation timestep matters more than resolution at "
            "the node."));
        f->addRow(QString(), m_trimAtNodesBox);

        m_dropSubScaleHolesBox =
            new QCheckBox(tr("Drop holes smaller than one cell"), g);
        m_dropSubScaleHolesBox->setToolTip(tr(
            "Hole rings narrower than the minimum cell size cannot be meshed "
            "around.  When checked they are removed, which means THE MESH COVERS "
            "THEM — a modelling change, reported in the generation log."));
        f->addRow(QString(), m_dropSubScaleHolesBox);

        m_cleanupBox = new QCheckBox(tr("Collapse leftover slivers after meshing"), g);
        m_cleanupBox->setToolTip(tr(
            "A second, post-meshing pass that collapses very short edges "
            "Triangle inserted on its own.\n\n"
            "Constrained edges, the domain outline, and any vertex carrying a tag "
            "or a coupled node are never touched, so this cannot fix a sliver the "
            "input demanded — those are reported in the log instead."));
        f->addRow(QString(), m_cleanupBox);

        m_minCellDerivedLabel = new QLabel(g);
        m_minCellDerivedLabel->setWordWrap(true);
        m_minCellDerivedLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
        f->addRow(QString(), m_minCellDerivedLabel);

        auto syncMinCell = [this] {
            const bool on = m_minCellSizeSpin && m_minCellSizeSpin->value() > 0.0;
            if (m_minSizeEnforceBox)    m_minSizeEnforceBox->setEnabled(on);
            if (m_trimAngleSpin)        m_trimAngleSpin->setEnabled(on);
            if (m_trimAtNodesBox)       m_trimAtNodesBox->setEnabled(on);
            if (m_dropSubScaleHolesBox) m_dropSubScaleHolesBox->setEnabled(on);
            if (m_cleanupBox)           m_cleanupBox->setEnabled(on);
            updateMinCellDerivedLabel();
        };
        connect(m_minSizeEnforceBox, &QCheckBox::toggled, this,
                [this] { updateMinCellDerivedLabel(); });
        connect(m_minCellSizeSpin, &QDoubleSpinBox::valueChanged, this, syncMinCell);
        connect(m_minAngleSpin,    &QDoubleSpinBox::valueChanged, this,
                [this] { updateMinCellDerivedLabel(); });
        syncMinCell();

        cellSizeVBox->addWidget(g);   // Minimum Cell Size
    }

    // Terrain-adaptive thinning group
    {
        auto *g = new QGroupBox(tr("Terrain-Adaptive Thinning"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_thinningBox = new QCheckBox(tr("Enable normal-deviation terrain simplification"), g);
        m_thinningBox->setToolTip(tr(
            "When checked: uses the normal-deviation algorithm to SELECT which "
            "DTM pixels to keep — only terrain-significant points (ridges, "
            "valleys, slope breaks) become Steiner vertices, flat areas are "
            "thinned away.\n\n"
            "When unchecked: every DTM pixel in the domain at the configured "
            "grid spacing becomes a Steiner vertex (no filtering).\n\n"
            "Either way, the selected DTM points are always added to the PSLG "
            "when a DTM raster is configured.  Each point carries its exact DEM "
            "elevation and is not re-sampled after triangulation.\n\n"
            "Very large DEMs are processed in memory-bounded row bands, so any "
            "raster size works at any spacing."));
        f->addRow(QString(), m_thinningBox);

        m_thinningToleranceSpin = new QDoubleSpinBox(g);
        m_thinningToleranceSpin->setRange(-1.0, 1.0);
        m_thinningToleranceSpin->setDecimals(8);
        m_thinningToleranceSpin->setSingleStep(0.001);
        m_thinningToleranceSpin->setToolTip(tr(
            "Removal threshold: a vertex is removed when the minimum dot "
            "product of its vertex-normal with every surrounding face-normal "
            "is ≥ this value (smooth surface).\n\n"
            "score < threshold → terrain feature (ridge/valley/slope break) "
            "→ vertex is KEPT.\n"
            "score ≥ threshold → flat or uniform-slope area → vertex is "
            "REMOVED.\n\n"
            "0.99 → keep bends > ~8° (fine detail)\n"
            "0.95 → keep bends > ~18° (default — channels, levees, ridges)\n"
            "0.90 → keep bends > ~26° (coarse — prominent breaks only)"));
        f->addRow(tr("Normal dot threshold:"), m_thinningToleranceSpin);

        m_thinningIterationsSpin = new QSpinBox(g);
        m_thinningIterationsSpin->setRange(0, std::numeric_limits<int>::max());
        m_thinningIterationsSpin->setSpecialValueText(tr("(unlimited)"));
        m_thinningIterationsSpin->setToolTip(tr(
            "Number of normal-deviation thinning passes.  "
            "0 (unlimited) runs until no further vertices qualify for removal.\n\n"
            "Very large DEMs are processed in memory-bounded bands; results are "
            "identical to a whole-grid run for pass counts up to 64.  "
            "\"(unlimited)\" is capped at 64 passes per band in that mode."));
        f->addRow(tr("Thinning passes:"), m_thinningIterationsSpin);

        m_thinningMaxPointsSpin = new QSpinBox(g);
        m_thinningMaxPointsSpin->setRange(0, 500000);
        m_thinningMaxPointsSpin->setSpecialValueText(tr("(unlimited)"));
        m_thinningMaxPointsSpin->setSingleStep(5000);
        f->addRow(tr("Max thinning points:"), m_thinningMaxPointsSpin);

        // ── Option A: Poisson-disk minimum spacing ─────────────────────────
        m_minSpacingBox = new QCheckBox(tr("Min point spacing (Poisson-disk):"), g);
        m_minSpacingBox->setToolTip(tr(
            "Post-thinning pass: enforces a minimum Euclidean distance between "
            "any two surviving DTM Steiner points.\n\n"
            "Points are accepted greedily in raster order; any candidate closer "
            "than this distance to an already-accepted point is dropped.\n\n"
            "Use to prevent micro-clusters from dominating the mesh even after "
            "normal-deviation thinning.  0 = auto (2 × pixel size)."));
        m_minSpacingSpin = new QDoubleSpinBox(g);
        m_minSpacingSpin->setRange(0.0, 1e9);
        m_minSpacingSpin->setDecimals(3);
        m_minSpacingSpin->setSingleStep(1.0);
        // suffix set by updateUnitDisplay()
        m_minSpacingSpin->setSpecialValueText(tr("(auto)"));
        f->addRow(m_minSpacingBox, m_minSpacingSpin);

        // 2026-07-19 — boundary buffer for the boundary-aware terrain
        // filter. Applies to BOTH the thinned and full-grid DTM paths (both
        // feed the same Steiner add loop), so it is deliberately NOT gated
        // by syncThinning below.
        m_boundaryBufferSpin = new QDoubleSpinBox(g);
        m_boundaryBufferSpin->setRange(0.0, 1e6);
        m_boundaryBufferSpin->setDecimals(3);
        m_boundaryBufferSpin->setSingleStep(0.5);
        // suffix set by updateUnitDisplay()
        m_boundaryBufferSpin->setSpecialValueText(tr("(auto)"));
        m_boundaryBufferSpin->setToolTip(tr(
            "DTM terrain points closer than this to the domain boundary, a "
            "hole ring, a constraint segment (conduits), or a mandatory "
            "vertex (SWMM nodes) are dropped, as are points outside the "
            "domain.  Prevents slivers and clusters of tiny cells along the "
            "boundary.\n\n"
            "(auto) = half the effective terrain point spacing."));
        f->addRow(tr("Boundary buffer:"), m_boundaryBufferSpin);

        terrainVBox->addWidget(g);   // Terrain-Adaptive Thinning
    }

    auto syncThinning = [this]() {
        const bool on = m_thinningBox->isChecked();
        m_thinningToleranceSpin->setEnabled(on);
        m_thinningIterationsSpin->setEnabled(on);
        m_thinningMaxPointsSpin->setEnabled(on);
        m_minSpacingBox->setEnabled(on);
        m_minSpacingSpin->setEnabled(on && m_minSpacingBox->isChecked());
    };
    connect(m_thinningBox,   &QCheckBox::toggled, this, syncThinning);
    connect(m_minSpacingBox, &QCheckBox::toggled, m_minSpacingSpin, &QWidget::setEnabled);
    syncThinning();

    // Quad regions — PSLG-defined polygons meshed with quads
    // (QUAD_MESHING_REDESIGN_PLAN_2026-09-06 §3.1 sources, §6.3). Sources
    // are a polygon layer and/or named subcatchments; the defaults below
    // apply to every region unless a layer feature carries quad_mode /
    // quad_spacing / quad_aspect / quad_angle / tag attributes.
    // Quads everywhere — the toggle (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.5).
    // No polygon required: the domain itself becomes the quad region.
    {
        auto *g = new QGroupBox(tr("Quadrilateral cells"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_quadEverywhereCheck = new QCheckBox(tr("&Generate quadrilateral cells"), g);
        m_quadEverywhereCheck->setToolTip(tr(
            "Quad-mesh the whole domain — no polygon needs to be drawn or "
            "picked.  Each domain ring is filled with a boundary-aligned quad "
            "lattice; holes, conduits and breaklines are respected, and cells "
            "that cannot be paired stay triangles (quad-dominant).\n\n"
            "The regions below remain optional: each one is subtracted from "
            "this background and meshed with its own mode and spacing, so use "
            "them only where a particular area needs different treatment "
            "(including 'Triangles only' to keep an area triangular)."));
        f->addRow(m_quadEverywhereCheck);

        m_quadEverywhereSpacingSpin = new QDoubleSpinBox(g);
        m_quadEverywhereSpacingSpin->setRange(0.0, 1e7);
        m_quadEverywhereSpacingSpin->setDecimals(3);
        m_quadEverywhereSpacingSpin->setValue(0.0);
        m_quadEverywhereSpacingSpin->setSpecialValueText(tr("(follow the size field)"));
        m_quadEverywhereSpacingSpin->setToolTip(tr(
            "Target quad edge length for the whole-domain lattice.\n\n"
            "Leave at 0 — the recommended setting — to follow the graded size "
            "field, so quads stay fine near conduits, inlets and breaklines and "
            "coarsen away from them exactly as the triangles do today.  A fixed "
            "value meshes the entire model at that one spacing, which on a large "
            "model either loses local refinement or explodes the cell count."));
        f->addRow(tr("Target quad si&ze:"), m_quadEverywhereSpacingSpin);

        // Enabling is driven by the shared syncQuad() below, which also owns
        // the quad-quality bounds (they apply to the background lattice too).
        quadsVBox->addWidget(g);   // Quadrilateral cells
    }
    {
        auto *g = new QGroupBox(tr("Quad regions (PSLG) — optional overrides"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        auto *hint = new QLabel(tr(
            "Optional.  Closed polygons inside the domain that need different "
            "treatment from the rest: the ring becomes a constraint loop and the "
            "interior is filled with its own quad lattice (or a structured grid "
            "when the outline is rectangular).  With the toggle above off, these "
            "are the only areas that get quads; with it on, each one is "
            "subtracted from the whole-domain lattice."), g);
        hint->setWordWrap(true);
        hint->setStyleSheet(openswmmvis::ui::theme::hintStyle());
        f->addRow(hint);

        m_quadRegionLayerCombo = new QComboBox(g);
        m_quadRegionLayerCombo->setToolTip(tr(
            "Polygon layer whose features become quad regions (exterior rings "
            "only; read in the worker and reprojected to the mesh CRS).\n\n"
            "Optional per-feature attributes override the defaults below:\n"
            "  quad_mode     auto | mapped | submapped | free | triangles\n"
            "  quad_spacing  target quad edge length (map units)\n"
            "  quad_aspect   maximum side ratio\n"
            "  quad_angle    alignment angle in degrees from +x\n"
            "  tag / name    cell tag"));
        f->addRow(tr("Region &layer:"), m_quadRegionLayerCombo);

        m_quadRegionSubcatchEdit = new QLineEdit(g);
        m_quadRegionSubcatchEdit->setPlaceholderText(tr("comma-separated subcatchment IDs"));
        m_quadRegionSubcatchEdit->setToolTip(tr(
            "Subcatchment polygons to quad-mesh, by ID.  Each becomes one "
            "region with the defaults below and the tag subcatch_<ID>.  An "
            "unknown ID stops generation with an error."));
        f->addRow(tr("Subcatchments:"), m_quadRegionSubcatchEdit);

        m_quadRegionModeCombo = new QComboBox(g);
        m_quadRegionModeCombo->addItem(tr("Auto"),           int(mesh::QuadRegionMode::Auto));
        m_quadRegionModeCombo->addItem(tr("Mapped"),         int(mesh::QuadRegionMode::Mapped));
        m_quadRegionModeCombo->addItem(tr("Submapped"),      int(mesh::QuadRegionMode::Submapped));
        m_quadRegionModeCombo->addItem(tr("Free"),           int(mesh::QuadRegionMode::Free));
        m_quadRegionModeCombo->addItem(tr("Triangles only"), int(mesh::QuadRegionMode::TrianglesOnly));
        m_quadRegionModeCombo->setToolTip(tr(
            "Auto: four-cornered outlines → Mapped, rectilinear outlines → "
            "Submapped, anything else → Free.\n"
            "Mapped / Submapped: structured, perfectly rectangular quads "
            "(fall back to Free when the outline does not allow it).\n"
            "Free: cross-field aligned lattice, quad-dominant with a few "
            "leftover triangles.\n"
            "Triangles only: the ring is still a constraint loop, the "
            "interior stays triangles."));
        f->addRow(tr("Default mode:"), m_quadRegionModeCombo);

        m_quadRegionSpacingSpin = new QDoubleSpinBox(g);
        m_quadRegionSpacingSpin->setRange(0.0, 1e9);
        m_quadRegionSpacingSpin->setDecimals(3);
        m_quadRegionSpacingSpin->setSingleStep(1.0);
        // suffix set by updateUnitDisplay()
        m_quadRegionSpacingSpin->setSpecialValueText(tr("(from max area)"));
        m_quadRegionSpacingSpin->setToolTip(tr(
            "Target quad edge length inside a region.\n"
            "0 = derive it from the size field at the region centroid, or "
            "from Max triangle area when no size field is in use."));
        f->addRow(tr("Default spacing:"), m_quadRegionSpacingSpin);

        m_quadRegionAspectSpin = new QDoubleSpinBox(g);
        m_quadRegionAspectSpin->setRange(1.0, 10.0);
        m_quadRegionAspectSpin->setDecimals(2);
        m_quadRegionAspectSpin->setSingleStep(0.25);
        m_quadRegionAspectSpin->setToolTip(tr(
            "Longest / shortest quad side accepted inside a Free region "
            "after smoothing."));
        f->addRow(tr("Default max aspect:"), m_quadRegionAspectSpin);

        m_quadRegionAngleSpin = new QDoubleSpinBox(g);
        m_quadRegionAngleSpin->setRange(-91.0, 90.0);
        m_quadRegionAngleSpin->setDecimals(1);
        m_quadRegionAngleSpin->setSingleStep(5.0);
        m_quadRegionAngleSpin->setSuffix(QStringLiteral(" °"));
        m_quadRegionAngleSpin->setSpecialValueText(tr("(from boundary)"));
        m_quadRegionAngleSpin->setToolTip(tr(
            "Free regions: constant lattice direction, degrees from +x "
            "(counter-clockwise).\n"
            "At the minimum, \"(from boundary)\", the direction field is "
            "solved from the region's own edges instead."));
        f->addRow(tr("Default alignment:"), m_quadRegionAngleSpin);

        quadsVBox->addWidget(g);   // Quad regions
    }

    // Quad quality — bounds shared by the PSLG quad regions above and the
    // G2 tri-pair merge (TRI_QUAD_MESHING_PLAN §3.1; defaults per
    // QUAD_MESHING_REDESIGN_PLAN §5: 60°/120°, SJ >= 0.866, aspect <= 2).
    {
        auto *g = new QGroupBox(tr("Quad quality"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_quadMergeBox = new QCheckBox(
            tr("Merge triangle pairs into quads (experimental — see tooltip)"), g);
        m_quadMergeBox->setToolTip(tr(
            "After triangulation, pair adjacent triangles into convex "
            "quadrilaterals, best quality first. Unmatched triangles remain "
            "(mixed mesh). Never merges across the domain outline, hole "
            "rings, breaklines, patch or region boundaries, across region "
            "tags, or between cells with different roughness / initial "
            "depth.\n\n"
            "Experimental: Triangle's refinement drives triangles toward "
            "equilateral, and two equilateral triangles form a 60°/120° "
            "rhombus — so this pass tends to produce diamond-shaped quads, "
            "not rectangles, and leaves a salt of unmatched triangles.  "
            "Quad regions (above) place the vertices for rectangular quads "
            "and are the recommended path."));
        f->addRow(QString(), m_quadMergeBox);

        m_quadMinAngleSpin = new QDoubleSpinBox(g);
        m_quadMinAngleSpin->setRange(0.0, 90.0);
        m_quadMinAngleSpin->setDecimals(1);
        m_quadMinAngleSpin->setSuffix(QStringLiteral(" °"));
        m_quadMinAngleSpin->setValue(60.0);
        m_quadMinAngleSpin->setToolTip(tr(
            "Reject a quad whose smallest interior angle is below this.  "
            "Applies to quad regions and to the triangle-pair merge."));
        f->addRow(tr("Min quad angle:"), m_quadMinAngleSpin);

        m_quadMaxAngleSpin = new QDoubleSpinBox(g);
        m_quadMaxAngleSpin->setRange(90.0, 180.0);
        m_quadMaxAngleSpin->setDecimals(1);
        m_quadMaxAngleSpin->setSuffix(QStringLiteral(" °"));
        m_quadMaxAngleSpin->setValue(120.0);
        m_quadMaxAngleSpin->setToolTip(tr(
            "Reject a quad whose largest interior angle is above this.  "
            "Applies to quad regions and to the triangle-pair merge."));
        f->addRow(tr("Max quad angle:"), m_quadMaxAngleSpin);

        m_quadMinSjSpin = new QDoubleSpinBox(g);
        m_quadMinSjSpin->setRange(0.0, 1.0);
        m_quadMinSjSpin->setDecimals(3);   // 0.866 must survive the round trip
        m_quadMinSjSpin->setSingleStep(0.05);
        m_quadMinSjSpin->setValue(0.866);
        m_quadMinSjSpin->setToolTip(tr(
            "Minimum scaled Jacobian = sine of the worst corner (1 for a "
            "rectangle, 0.866 at 60°/120°, 0 when a corner degenerates).  "
            "Applies to quad regions and to the triangle-pair merge."));
        f->addRow(tr("Min scaled Jacobian:"), m_quadMinSjSpin);

        m_quadMaxAspectSpin = new QDoubleSpinBox(g);
        m_quadMaxAspectSpin->setRange(0.0, 100.0);
        m_quadMaxAspectSpin->setDecimals(2);
        m_quadMaxAspectSpin->setSingleStep(0.25);
        m_quadMaxAspectSpin->setSpecialValueText(tr("(off)"));
        m_quadMaxAspectSpin->setValue(2.0);
        m_quadMaxAspectSpin->setToolTip(tr(
            "Reject a quad longer than this ratio (longest / shortest side, "
            "opposite-side means).  0 = no limit.  Applies to quad regions "
            "and to the triangle-pair merge."));
        f->addRow(tr("Max aspect ratio:"), m_quadMaxAspectSpin);

        m_quadPlanaritySpin = new QDoubleSpinBox(g);
        m_quadPlanaritySpin->setRange(0.0, 1000.0);
        m_quadPlanaritySpin->setDecimals(3);
        m_quadPlanaritySpin->setSingleStep(0.05);
        m_quadPlanaritySpin->setSpecialValueText(tr("(off)"));
        m_quadPlanaritySpin->setToolTip(tr(
            "Reject a merged quad whose four bed elevations deviate from a "
            "plane by more than this, so a quad never hides a crest or "
            "channel bank that the two triangles resolved. 0 = ignore."));
        f->addRow(tr("Max bed non-planarity:"), m_quadPlanaritySpin);

        // The four bounds matter whenever anything produces quads: a region
        // source is selected OR the merge is on.  Planarity is merge-only.
        auto syncQuad = [this] {
            const bool merge   = m_quadMergeBox->isChecked();
            const bool regions =
                (m_quadRegionLayerCombo
                 && m_quadRegionLayerCombo->currentData().value<void *>() != nullptr)
                || (m_quadRegionSubcatchEdit
                    && !m_quadRegionSubcatchEdit->text().trimmed().isEmpty());
            const bool everywhere = m_quadEverywhereCheck && m_quadEverywhereCheck->isChecked();
            const bool on = merge || regions || everywhere;
            m_quadMinAngleSpin->setEnabled(on);
            m_quadMaxAngleSpin->setEnabled(on);
            m_quadMinSjSpin->setEnabled(on);
            m_quadMaxAspectSpin->setEnabled(on);
            m_quadPlanaritySpin->setEnabled(merge);
            if (m_quadEverywhereSpacingSpin) m_quadEverywhereSpacingSpin->setEnabled(everywhere);
        };
        connect(m_quadMergeBox, &QCheckBox::toggled, this, syncQuad);
        if (m_quadEverywhereCheck)
            connect(m_quadEverywhereCheck, &QCheckBox::toggled, this, syncQuad);
        connect(m_quadRegionLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [syncQuad](int) { syncQuad(); });
        connect(m_quadRegionSubcatchEdit, &QLineEdit::textChanged,
                this, [syncQuad](const QString &) { syncQuad(); });
        syncQuad();

        quadsVBox->addWidget(g);   // Quad quality
    }

    // Structured patches — G3 (TRI_QUAD_MESHING_PLAN §3.2). Coordinates are
    // typed per row; there is no map-selection plumbing in this dialog.
    {
        auto *g   = new QGroupBox(tr("Structured quad patches"), qualityPage);
        auto *lay = new QVBoxLayout(g);

        auto *hint = new QLabel(tr(
            "Four-corner patch: 4 corners as \"x y; x y; x y; x y\" (mesh CRS), "
            "N × M quads (transfinite). Swept channel: centreline as "
            "\"x y; x y; …\", quads Across the width, target spacing Along "
            "the centreline (0 = one station per vertex), Width. The patch "
            "interior is excluded from triangulation and its quads are "
            "stitched to the surrounding triangles."), g);
        hint->setWordWrap(true);
        lay->addWidget(hint);

        m_patchTable = new QTableWidget(0, 6, g);
        m_patchTable->setHorizontalHeaderLabels(
            {tr("Type"), tr("Points"), tr("N / Across"), tr("M / Along"),
             tr("Width"), tr("Tag")});
        m_patchTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_patchTable->verticalHeader()->setVisible(false);
        m_patchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_patchTable->setMinimumHeight(120);
        lay->addWidget(m_patchTable);

        auto addRow = [this](const QString &type, const QString &n,
                             const QString &m, const QString &w) {
            const int r = m_patchTable->rowCount();
            m_patchTable->insertRow(r);
            auto *typeItem = new QTableWidgetItem(type);
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
            m_patchTable->setItem(r, 0, typeItem);
            m_patchTable->setItem(r, 1, new QTableWidgetItem(QString()));
            m_patchTable->setItem(r, 2, new QTableWidgetItem(n));
            m_patchTable->setItem(r, 3, new QTableWidgetItem(m));
            m_patchTable->setItem(r, 4, new QTableWidgetItem(w));
            m_patchTable->setItem(r, 5, new QTableWidgetItem(QString()));
            m_patchTable->editItem(m_patchTable->item(r, 1));
        };
        auto *btns = new QHBoxLayout;
        auto *addQuad  = new QPushButton(tr("Add four-corner patch"), g);
        auto *addSwept = new QPushButton(tr("Add swept channel patch"), g);
        auto *remove   = new QPushButton(tr("Remove"), g);
        connect(addQuad,  &QPushButton::clicked, this, [addRow] {
            addRow(QStringLiteral("Four-corner"), QStringLiteral("4"),
                   QStringLiteral("4"), QString());
        });
        connect(addSwept, &QPushButton::clicked, this, [addRow] {
            addRow(QStringLiteral("Swept"), QStringLiteral("2"),
                   QStringLiteral("0"), QStringLiteral("10"));
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int r = m_patchTable->currentRow();
            if (r >= 0) m_patchTable->removeRow(r);
        });
        btns->addWidget(addQuad);
        btns->addWidget(addSwept);
        btns->addWidget(remove);
        btns->addStretch();
        lay->addLayout(btns);

        quadsVBox->addWidget(g);   // Structured quad patches
    }

    // Each inner page gets its own stretch so its groups sit at the top
    // rather than spreading down a tall tab.
    sizingVBox->addStretch();
    cellSizeVBox->addStretch();
    terrainVBox->addStretch();
    quadsVBox->addStretch();

    // Inner pages are scroll-wrapped individually: with the groups split
    // four ways only "Quads" (15 rows + the patch table) can still exceed
    // the dialog's 560 px default, and it degrades to a scroll instead of
    // forcing the dialog taller. Tab titles are asserted verbatim by
    // test_meshmincelldialog::qualityTabsStructure.
    qualityTabs->addTab(OpenSWMM::Ui::wrapInScrollArea(sizingPage,   qualityTabs),
                        tr("Sizing"));
    qualityTabs->addTab(OpenSWMM::Ui::wrapInScrollArea(cellSizePage, qualityTabs),
                        tr("Cell Size"));
    qualityTabs->addTab(OpenSWMM::Ui::wrapInScrollArea(terrainPage,  qualityTabs),
                        tr("Terrain"));
    qualityTabs->addTab(OpenSWMM::Ui::wrapInScrollArea(quadsPage,    qualityTabs),
                        tr("Quads"));

    // The outer page holds only the tab widget, so it needs no scroll area
    // of its own — but addTab is kept symmetrical with the other top-level
    // tabs so the dialog's resize behaviour is unchanged.
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(qualityPage, tabs),
                 tr("Quality"));

    // ================================================================
    // Tab 3 — Hydraulics
    // Uniform per-cell seeds only. Spatially varying values are assigned
    // after generation from the Mesh 2D ribbon, against real cells the user
    // can see and select.
    // ================================================================
    auto *hydraulicsPage = new QWidget;
    auto *hydraulicsVBox = new QVBoxLayout(hydraulicsPage);
    hydraulicsVBox->setContentsMargins(8, 8, 8, 8);

    {
        auto *g   = new QGroupBox(tr("Initial cell values"), hydraulicsPage);
        auto *groupVBox = new QVBoxLayout(g);
        auto *form = new QFormLayout;
        groupVBox->addLayout(form);

        m_manningsValueSpin = new QDoubleSpinBox(g);
        m_manningsValueSpin->setRange(0.001, 1.0);
        m_manningsValueSpin->setDecimals(4);
        m_manningsValueSpin->setSingleStep(0.005);
        m_manningsValueSpin->setToolTip(
            tr("Manning's roughness written to every generated cell "
               "([2D_TRIANGLES] MANNINGS_N)."));
        form->addRow(tr("Roughness (Manning's n):"), m_manningsValueSpin);

        const QString dLbl = m_pw && m_pw->unitSystem()
                                 ? m_pw->unitSystem()->depthLabel()
                                 : QStringLiteral("m");
        m_initDepthSpin = new QDoubleSpinBox(g);
        m_initDepthSpin->setRange(0.0, 1000.0);
        m_initDepthSpin->setDecimals(4);
        m_initDepthSpin->setSingleStep(0.05);
        m_initDepthSpin->setSuffix(QStringLiteral(" ") + dLbl);
        m_initDepthSpin->setToolTip(
            tr("Standing water depth written to every generated cell "
               "([2D_TRIANGLES] INIT_DEPTH). 0 starts the surface dry."));
        form->addRow(tr("Initial depth:"), m_initDepthSpin);

        // ── Region defaults (GG0d, GUI plan §3.3) ────────────────────
        // The two spin boxes above remain the '*' row's editors — the table
        // mirrors them read-only — so a user who never touches the table
        // produces exactly the mesh and the file this dialog produced before
        // the table existed.
        m_regionDefaults = new MeshRegionDefaultsWidget(g);
        m_regionDefaults->setDepthUnit(dLbl);
        m_regionDefaults->setStarHydraulics(m_manningsValueSpin->value(),
                                            m_initDepthSpin->value());
        connect(m_manningsValueSpin, &QDoubleSpinBox::valueChanged, this,
                [this](double v) {
                    m_regionDefaults->setStarHydraulics(v, m_initDepthSpin->value());
                });
        connect(m_initDepthSpin, &QDoubleSpinBox::valueChanged, this,
                [this](double v) {
                    m_regionDefaults->setStarHydraulics(m_manningsValueSpin->value(), v);
                });
        // Subcatchments are the only source of mesh::RegionMarker today, so
        // that one checkbox decides whether the table has region rows at all.
        connect(m_includeSubcatch, &QCheckBox::toggled,
                this, &MeshGenerationDialog::refreshRegionRows);
        groupVBox->addWidget(m_regionDefaults, 1);

        auto *hint = new QLabel(
            tr("Assign spatially varying values after generation from the "
               "Mesh 2D tab: select cells and edit them directly, or use "
               "Cell Data to sample a raster or shapefile field."),
            g);
        hint->setWordWrap(true);
        hint->setEnabled(false);
        groupVBox->addWidget(hint);

        hydraulicsVBox->addWidget(g, 1);
    }

    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(hydraulicsPage, tabs),
                 tr("Hydraulics"));

    outer->addWidget(tabs, 1);

    // ================================================================
    // Footer — Output destination (always visible, outside tabs)
    // ================================================================
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    outer->addWidget(sep);

    auto *outputForm = new QFormLayout;
    outputForm->setContentsMargins(0, 4, 0, 4);
    outputForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *modeRow  = new QHBoxLayout;
    auto *outGroup = new QButtonGroup(this);
    m_outputExternal = new QRadioButton(tr("External .2dm"), this);
    m_outputExternal->setToolTip(tr("Write a standalone .2dm file referenced via [2D_MESH_FILE] in the .inp."));
    m_outputInline   = new QRadioButton(tr("Inline in .inp"), this);
    m_outputInline->setToolTip(tr("Embed the mesh directly inside the SWMM .inp file."));
    outGroup->addButton(m_outputExternal);
    outGroup->addButton(m_outputInline);
    modeRow->addWidget(m_outputExternal);
    modeRow->addWidget(m_outputInline);
    modeRow->addStretch();
    outputForm->addRow(tr("Output:"), modeRow);

    auto *pathRow = new QHBoxLayout;
    m_meshPathEdit  = new QLineEdit(this);
    m_meshPathEdit->setPlaceholderText(tr("(default: <project>.2dm)"));
    m_browseMeshBtn = new QPushButton(tr("Browse…"), this);
    pathRow->addWidget(m_meshPathEdit, 1);
    pathRow->addWidget(m_browseMeshBtn);
    outputForm->addRow(tr("Mesh file:"), pathRow);

    outer->addLayout(outputForm);

    // ── Embedded progress (hidden until generation starts) ──────────
    m_progressLabel = new QLabel(this);
    m_progressLabel->setAlignment(Qt::AlignCenter);
    m_progressLabel->setVisible(false);
    outer->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);
    outer->addWidget(m_progressBar);

    // ── Buttons ─────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(this);
    m_generateBtn = bb->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    m_cancelBtn   = bb->addButton(tr("Close"),    QDialogButtonBox::RejectRole);
    outer->addWidget(bb);

    connect(m_browseMeshBtn, &QPushButton::clicked, this, &MeshGenerationDialog::onBrowseMeshPath);
    connect(m_generateBtn,   &QPushButton::clicked, this, &MeshGenerationDialog::onAccept);
    connect(m_cancelBtn,     &QPushButton::clicked, this, &MeshGenerationDialog::onCancelOrReject);

    auto refreshPath = [this]() {
        const bool ext = m_outputExternal->isChecked();
        m_meshPathEdit->setEnabled(ext);
        m_browseMeshBtn->setEnabled(ext);
    };
    connect(m_outputExternal, &QRadioButton::toggled, this, refreshPath);
    connect(m_outputInline,   &QRadioButton::toggled, this, refreshPath);
    refreshPath();
}

// ---------------------------------------------------------------------------
// Defaults + layer combo population
// ---------------------------------------------------------------------------

void MeshGenerationDialog::updateZFactor()
{
    if (!m_zFactorSpin) return;

    // Determine DTM vertical unit from the selected raster.
    auto *raster = qobject_cast<GISRasterLayer *>(
        static_cast<QObject *>(m_dtmCombo ? m_dtmCombo->currentData().value<void *>() : nullptr));
    const QString dtmUnit = raster ? raster->detectVerticalUnit() : QStringLiteral("m");
    const double  dtmToSI = (dtmUnit == QLatin1String("ft")) ? 0.3048 : 1.0;

    // Determine desired output vertical unit.
    const QString outSel = m_meshVertCRSCombo ? m_meshVertCRSCombo->currentData().toString()
                                              : QStringLiteral("auto");
    double outToSI = 1.0;
    if (outSel == QLatin1String("auto")) {
        outToSI = (m_pw && m_pw->unitSystem() && !m_pw->unitSystem()->isSI()) ? 0.3048 : 1.0;
    } else if (outSel == QLatin1String("ft")) {
        outToSI = 0.3048;
    }

    QSignalBlocker b(m_zFactorSpin);
    m_zFactorSpin->setValue(dtmToSI / outToSI);
}

void MeshGenerationDialog::updateUnitDisplay()
{
    const UnitSystem *us  = UnitSystem::instance();
    const QString     len = us->lengthLabel();       // "ft" or "m"
    const QString     len2 = len + QStringLiteral("²"); // "ft²" or "m²"
    const QString     suf  = QStringLiteral(" ") + len;

    if (m_simplifyEpsSpin) m_simplifyEpsSpin->setSuffix(suf);
    if (m_snapEpsSpin)     m_snapEpsSpin->setSuffix(suf);
    if (m_minSpacingSpin)  m_minSpacingSpin->setSuffix(suf);
    if (m_nodeFlattenSpin) m_nodeFlattenSpin->setSuffix(suf);
    if (m_nodeMinSepSpin)  m_nodeMinSepSpin->setSuffix(suf);
    // 2026-07-19 — boundary-aware terrain filter + boundary densification.
    if (m_boundaryBufferSpin)  m_boundaryBufferSpin->setSuffix(suf);
    if (m_maxBoundaryEdgeSpin) m_maxBoundaryEdgeSpin->setSuffix(suf);

    if (m_minCellSizeSpin)     m_minCellSizeSpin->setSuffix(suf);
    if (m_quadPlanaritySpin)   m_quadPlanaritySpin->setSuffix(suf);
    if (m_quadRegionSpacingSpin) m_quadRegionSpacingSpin->setSuffix(suf);

    if (m_maxAreaSpin)
        m_maxAreaSpin->setToolTip(
            tr("Upper bound on triangle area (%1). 0 = no cap.").arg(len2));

    updateMinCellDerivedLabel();
}

void MeshGenerationDialog::updateMinCellDerivedLabel()
{
    if (!m_minCellDerivedLabel) return;

    const double h = m_minCellSizeSpin ? m_minCellSizeSpin->value() : 0.0;
    if (h <= 0.0)
    {
        m_minCellDerivedLabel->setText(
            tr("Off — the input geometry is used as-is and cell size is bounded "
               "below only by the geometry itself."));
        return;
    }

    const UnitSystem *us   = UnitSystem::instance();
    const QString     len  = us->lengthLabel();
    const QString     len2 = len + QStringLiteral("²");

    mesh::pslg::MinSizePolicy p;
    p.minCellSize = h;
    p.resolveDefaults();

    const bool enforce =
        m_minSizeEnforceBox && m_minSizeEnforceBox->isChecked();
    QString txt = enforce
        ? tr("Refinement floor ≈ %1 %2 per cell; vertices closer than %3 %4 "
             "are merged; no vertex moves further than %3 %4. Enforce: SWMM "
             "coupling points closer than %3 %4 may merge or demote — the "
             "affected nodes couple via their containing cell.")
              .arg(p.minTriangleArea(), 0, 'g', 4)
              .arg(len2)
              .arg(p.weldRadius, 0, 'g', 4)
              .arg(len)
        : tr("Refinement floor ≈ %1 %2 per cell; vertices closer than "
             "%3 %4 are merged; no vertex moves further than %3 %4. "
             "Tagged SWMM nodes never move.")
              .arg(p.minTriangleArea(), 0, 'g', 4)
              .arg(len2)
              .arg(p.weldRadius, 0, 'g', 4)
              .arg(len);

    // The angle bound is a real lever on sliver count near unavoidable sharp
    // input angles, and 33° costs 2-4x the vertices of 26° for no practical
    // benefit (see meshgenerator.h).  Worth saying so where it is actionable.
    if (m_minAngleSpin && m_minAngleSpin->value() > 28.0)
        txt += QLatin1Char(' ')
             + tr("Min angle is %1° — consider 26–28° with a minimum cell size, "
                  "as a high angle bound multiplies cells around sharp features.")
                   .arg(m_minAngleSpin->value(), 0, 'f', 1);

    m_minCellDerivedLabel->setText(txt);
}

void MeshGenerationDialog::seedDefaults()
{
    // Junctions default OFF (Plan Part B, decision 2026-07-28): forcing a
    // vertex at every node distorts the mesh around close node clusters
    // (weir/orifice endpoints). Coupling is authored post-generation instead.
    m_includeJunctions->setChecked(false);
    m_includeConduits->setChecked(true);
    m_includeSubcatch->setChecked(true);
    m_mapNodesAfterGen->setChecked(true);
    m_nodesUseRim->setChecked(false);   // interpolate nodes to terrain by default
    m_elevMethodCombo->setCurrentIndex(0);  // IDW
    m_nnVariantCombo->setCurrentIndex(0);   // Sibson
    // Iteration 4 — seed values come from the user-editable 2D Defaults
    // preference page (Preferences → 2D Defaults). The compiled-in struct
    // defaults preserve the historical seeds (33° min angle per the
    // 2026-07-31 decision, SI-canonical distances, thinning on 0.6/3, …).
    const auto t = PreferencesManager::instance()->twoDDefaults();
    m_idwPowerSpin->setValue(t.meshIdwPower);
    m_maxAreaSpin->setValue(t.meshMaxArea);
    m_minAngleSpin->setValue(t.meshMinAngleDeg);
    m_maxSteinerSpin->setValue(t.meshMaxSteiner);
    m_allowSteiner->setChecked(true);
    // Scale distance defaults (stored SI-canonical) to the project's
    // length unit.
    const double toUnit = UnitSystem::instance()->isSI() ? 1.0 : 1.0 / 0.3048;
    m_simplifyEpsSpin->setValue(t.meshSimplifyEpsM * toUnit);
    m_snapEpsSpin->setValue(    t.meshSnapEpsM     * toUnit);
    m_nodeFlattenSpin->setValue(t.meshNodeFlattenRadM * toUnit);
    m_nodeMinSepBox->setChecked(t.meshMinNodeSepOn);
    m_nodeMinSepSpin->setValue(t.meshMinNodeSepM * toUnit);
    m_thinningBox->setChecked(t.meshThinningOn);
    m_thinningToleranceSpin->setValue(t.meshThinningTol);
    m_thinningIterationsSpin->setValue(t.meshThinningPasses);
    m_thinningMaxPointsSpin->setValue(0);
    m_minSpacingBox->setChecked(false);
    m_minSpacingSpin->setValue(0.0);
    m_boundaryBufferSpin->setValue(t.meshBoundaryBufferM * toUnit); // 0 = (auto)
    m_maxBoundaryEdgeBox->setChecked(t.meshMaxBoundaryEdgeOn);
    m_maxBoundaryEdgeSpin->setValue(t.meshMaxBoundaryEdgeM * toUnit);
    m_maxBoundaryEdgeSpin->setEnabled(t.meshMaxBoundaryEdgeOn);
    // Minimum cell size defaults OFF so an existing project reproduces its
    // current mesh exactly; the rest of the group carries the policy defaults
    // from MinSizePolicy and only bites once a size is entered.
    if (m_minCellSizeSpin)      m_minCellSizeSpin->setValue(0.0);
    if (m_minSizeEnforceBox)    m_minSizeEnforceBox->setChecked(false);
    // Gradation defaults OFF (uniform cap) for the same regression-safety
    // reason: an untouched project reproduces its current mesh exactly.
    // 0.25 is the recommended value once turned on (tooltip).
    if (m_gradationSpin)        m_gradationSpin->setValue(0.0);
    if (m_trimAngleSpin)        m_trimAngleSpin->setValue(
                                    mesh::pslg::MinSizePolicy{}.trimAngleDeg);
    if (m_trimAtNodesBox)       m_trimAtNodesBox->setChecked(false);
    if (m_dropSubScaleHolesBox) m_dropSubScaleHolesBox->setChecked(true);
    if (m_cleanupBox)           m_cleanupBox->setChecked(true);
    updateMinCellDerivedLabel();
    // Quad regions + quad quality (QUAD_MESHING_REDESIGN_PLAN §5 defaults).
    // No source selected → no regions; merge stays off.  Like the patch /
    // merge controls before them, these are not persisted anywhere (no
    // QSettings / preference page) — the plan's §6.3 persistence item is
    // still open.
    if (m_quadRegionLayerCombo)   m_quadRegionLayerCombo->setCurrentIndex(0);
    if (m_quadRegionSubcatchEdit) m_quadRegionSubcatchEdit->clear();
    if (m_quadRegionModeCombo)    m_quadRegionModeCombo->setCurrentIndex(0);   // Auto
    if (m_quadRegionSpacingSpin)  m_quadRegionSpacingSpin->setValue(0.0);      // (from max area)
    if (m_quadRegionAspectSpin)   m_quadRegionAspectSpin->setValue(mesh::QuadRegion{}.aspectMax);
    if (m_quadRegionAngleSpin)    m_quadRegionAngleSpin->setValue(m_quadRegionAngleSpin->minimum());
    if (m_quadMergeBox)           m_quadMergeBox->setChecked(false);
    {
        const mesh::QuadQualityBounds qb;
        if (m_quadMinAngleSpin)  m_quadMinAngleSpin->setValue(qb.minAngleDeg);
        if (m_quadMaxAngleSpin)  m_quadMaxAngleSpin->setValue(qb.maxAngleDeg);
        if (m_quadMinSjSpin)     m_quadMinSjSpin->setValue(qb.minScaledJacobian);
        if (m_quadMaxAspectSpin) m_quadMaxAspectSpin->setValue(qb.maxAspect);
    }
    m_manningsValueSpin->setValue(t.meshManningsN);
    m_initDepthSpin->setValue(t.meshInitDepth);
    m_outputExternal->setChecked(t.meshOutputExternal);
    updateUnitDisplay();   // set suffixes and tooltip after values are seeded
    populateLayerCombos();
    updateZFactor();       // seed factor from current DTM + mesh vertical unit
    refreshRegionRows();   // GG0d — region rows follow m_includeSubcatch

    if (m_pw && m_pw->modelLayer())
    {
        const MapExtent ext = m_pw->modelLayer()->extent();
        m_domainLabel->setText(ext.isValid()
            ? tr("model extent [%1, %2 → %3, %4]")
                  .arg(ext.xMin(),0,'f',2).arg(ext.yMin(),0,'f',2)
                  .arg(ext.xMax(),0,'f',2).arg(ext.yMax(),0,'f',2)
            : tr("(model extent not available)"));

        const QString inp = m_pw->modelLayer()->modelFilePath();
        if (!inp.isEmpty())
        {
            const QFileInfo fi(inp);
            m_meshPathEdit->setText(
                fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".2dm")));
        }
    }
}

// ---------------------------------------------------------------------------
// GG0d — region tags for the region-defaults table (GUI plan §3.3)
// ---------------------------------------------------------------------------

/*! Same enumeration collectInputs() runs for PipelineInputs::subcatchSeeds,
 *  carrying the "subcatch_%1" spelling the worker gives mesh::RegionMarker::tag
 *  (and therefore MeshTriangle::tag). The table has to key on the FINAL tag or
 *  its rows would never match a triangle. */
QStringList MeshGenerationDialog::regionTags() const
{
    QStringList tags;
    if (!m_includeSubcatch || !m_includeSubcatch->isChecked()) return tags;
    if (!m_pw || !m_pw->modelLayer())                          return tags;

    SWMMModelLayer *layer = m_pw->modelLayer();
    const auto      cat   = SWMMModelLayer::CatSubcatchments;
    for (int row = 0; row < layer->categoryCount(cat); ++row)
    {
        const QString name = layer->objectNameAt(cat, row);
        if (name.isEmpty()) continue;
        // A subcatchment with no extent gets no region marker, so it would
        // never tag a triangle — leaving it out keeps the table honest.
        if (!layer->objectExtent(name).isValid()) continue;
        tags << QStringLiteral("subcatch_%1").arg(name);
    }
    return tags;
}

void MeshGenerationDialog::refreshRegionRows()
{
    if (m_regionDefaults)
        m_regionDefaults->setRegionTags(regionTags());
}

void MeshGenerationDialog::populateLayerCombos()
{
    if (!m_pw || !m_pw->canvas()) return;
    const auto &layers = m_pw->canvas()->layers();

    m_dtmCombo->clear();
    // Allow generation without a DTM — elevations fall back to IDW from
    // junction rim elevations (invert + maxDepth on each SWMM node).
    m_dtmCombo->addItem(tr("(none — use junction rim elevations)"),
                         QVariant::fromValue<void *>(nullptr));
    for (auto *L : layers)
        if (auto *r = qobject_cast<GISRasterLayer *>(L))
            m_dtmCombo->addItem(r->name(), QVariant::fromValue<void *>(r));

    // Trigger the DTM-changed connection to update the detected vertical unit label.
    emit m_dtmCombo->currentIndexChanged(m_dtmCombo->currentIndex());

    m_boundaryLayerCombo->clear();
    m_boundaryLayerCombo->addItem(tr("(none)"),
                                   QVariant::fromValue<void *>(nullptr));
    m_boundaryLayerCombo->addItem(tr("Use SWMM subcatchment polygons"),
                                   QVariant::fromValue<void *>(reinterpret_cast<void *>(0x1)));
    for (auto *L : layers)
        if (auto *v = qobject_cast<GISVectorLayer *>(L))
            m_boundaryLayerCombo->addItem(v->name(), QVariant::fromValue<void *>(v));

    // Quad region layer: same source list and payload as the boundary combo
    // (no subcatchment pseudo-entry — subcatchments are named in the edit).
    if (m_quadRegionLayerCombo)
    {
        m_quadRegionLayerCombo->clear();
        m_quadRegionLayerCombo->addItem(tr("(none)"),
                                        QVariant::fromValue<void *>(nullptr));
        for (auto *L : layers)
            if (auto *v = qobject_cast<GISVectorLayer *>(L))
                m_quadRegionLayerCombo->addItem(v->name(), QVariant::fromValue<void *>(v));
    }

    // Decide whether a vector layer carries 3D geometry — uses the declared
    // layer type when known, otherwise probes the first feature.
    auto detect3D = [](GISVectorLayer *v) -> bool {
        OGRLayer *ol = v ? v->ogrLayer() : nullptr;
        if (!ol) return false;
        const OGRwkbGeometryType gt = ol->GetGeomType();
        if (gt != wkbUnknown && gt != wkbNone)
            return OGR_GT_HasZ(gt);
        ol->ResetReading();
        bool is3d = false;
        if (OGRFeature *f = ol->GetNextFeature())
        {
            if (auto *geom = f->GetGeometryRef())
                is3d = geom->Is3D();
            OGRFeature::DestroyFeature(f);
        }
        ol->ResetReading();
        return is3d;
    };

    // Each row gets an "include" checkbox and a "use Z" checkbox; the latter
    // is enabled only when the layer's geometry is 3D.  Rows are stashed so
    // collectInputs() can read both checkbox states directly.
    auto fillList = [&](QListWidget *list, QVector<AuxLayerRow> &rows) {
        list->clear();
        rows.clear();
        for (auto *L : layers)
            if (auto *v = qobject_cast<GISVectorLayer *>(L))
            {
                const bool is3D = detect3D(v);

                auto *item = new QListWidgetItem(list);
                auto *row  = new QWidget(list);
                auto *h    = new QHBoxLayout(row);
                h->setContentsMargins(4, 1, 4, 1);
                h->setSpacing(8);

                auto *inc = new QCheckBox(v->name(), row);
                auto *uz  = new QCheckBox(tr("use Z"), row);
                uz->setEnabled(is3D);
                uz->setToolTip(is3D
                    ? tr("Use the feature's Z coordinate as the vertex elevation\n"
                         "(taken as-is in mesh vertical units; not reprojected).\n"
                         "2D features in this layer fall back to the DTM.")
                    : tr("Layer has no Z values — elevation comes from the DTM."));

                h->addWidget(inc, 1);
                h->addWidget(uz);

                item->setSizeHint(row->sizeHint());
                list->setItemWidget(item, row);

                rows.append({v, inc, uz, is3D});
            }
        if (list->count() == 0)
            list->addItem(tr("(no vector layers)"));
    };
    fillList(m_pointLayersList, m_pointLayerRows);
    fillList(m_lineLayersList,  m_lineLayerRows);
}

// ---------------------------------------------------------------------------
// Input collection (main thread — reads widgets + SWMMModelLayer)
// ---------------------------------------------------------------------------

bool MeshGenerationDialog::collectInputs(PipelineInputs *out, QString *errOut) const
{
    auto fail = [&](const QString &m) {
        if (errOut) *errOut = m;
        return false;
    };

    if (!m_pw || !m_pw->modelLayer() || !m_pw->modelLayer()->engine())
        return fail(tr("No active SWMM project."));

    SWMMModelLayer *layer = m_pw->modelLayer();
    out->inpPath = layer->modelFilePath();
    if (out->inpPath.isEmpty())
        return fail(tr("Save the project to a .inp file first (File → Save As)."));

    const MapExtent modelExt = layer->extent();
    if (!modelExt.isValid())
        return fail(tr("Model has no spatial extent — add at least one node first."));

    // ── DTM path (optional) ──────────────────────────────────────────
    // When no DTM is selected, vertex elevations are filled by IDW
    // interpolation from SWMM junction rim elevations (invert + maxDepth).
    auto *dtmLayer = static_cast<GISRasterLayer *>(
        m_dtmCombo->currentData().value<void *>());
    out->dtmPath = dtmLayer ? dtmLayer->filePath() : QString();
    const bool haveDTM = !out->dtmPath.isEmpty();

    // Aux point/line layers that can supply no elevation (no DTM and no usable
    // feature Z) are collected here and reported as a hard-block error below.
    QStringList blockedLayers;

    // Aux geometry PROJ could not reproject into the mesh CRS. Dropped rather
    // than carried as OGR's HUGE_VAL; counted so the user is told instead of
    // silently getting fewer constraints than the layer contains.
    qsizetype nAuxPointsUnprojectable    = 0;
    qsizetype nAuxLineVertsUnprojectable = 0;

    // ── PSLG optimisation parameters ─────────────────────────────────
    out->pslgSimplifyEps = m_simplifyEpsSpin->value();
    out->pslgSnapEps     = m_snapEpsSpin->value();
    // 2026-07-19 — must be set BEFORE the domain polygons are collected
    // below: pushOgrPolygon reads maxBoundaryEdgeLen when densifying rings.
    out->maxBoundaryEdgeLen =
        (m_maxBoundaryEdgeBox->isChecked() && m_maxBoundaryEdgeSpin->value() > 0.0)
            ? m_maxBoundaryEdgeSpin->value()
            : 0.0;

    // ── Mesh CRS — initialised first so every source can reproject to it ──
    // All PSLG inputs (domain polygons, hole rings, constraint segments,
    // Steiner points) must be in the same CRS before Triangle runs.
    // The SWMM model's native CRS is the authoritative mesh CRS; everything
    // else is transformed to it.  The WKT is also forwarded to the worker
    // so it can reproject mesh vertices to the DTM's CRS before sampling.
    OGRSpatialReference meshOGRSRS;
    meshOGRSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    bool meshHasSRS = false;
    if (auto *srs = layer->srs())
        if (auto *ogrSRS = srs->ogrSpatialReference())
        {
            char *wkt = nullptr;
            if (ogrSRS->exportToWkt(&wkt) == OGRERR_NONE)
            {
                out->meshCRSWkt = QString::fromUtf8(wkt);
                meshHasSRS = (meshOGRSRS.importFromWkt(wkt) == OGRERR_NONE);
                if (meshHasSRS)
                    meshOGRSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            }
            CPLFree(wkt);
        }

    // ── Frame validity: mesh CRS must be planar with a known linear unit ──
    // Engine consumes vertex XY as SI metres. Capture the conversion factor
    // here and refuse to mesh when the project CRS cannot be expressed in
    // metres (geographic CRS, undefined units, non-finite scale).
    if (auto *srs = layer->srs())
    {
        const auto lui = srs->planarLinearUnit();
        if (!lui.usable)
        {
            return fail(tr(
                "The project CRS (%1) has no usable planar linear unit.\n"
                "2D mesh generation requires a projected or local CRS in "
                "metres or feet. Geographic (lat/lon) CRSes are not "
                "supported.\n\n"
                "Fix: open Project → Change CRS… and pick a projected CRS, "
                "or use the 'Local projected' option when the source units "
                "are unknown.")
                .arg(srs->description().isEmpty()
                         ? srs->toAuthority() : srs->description()));
        }
        out->meshCRSTag = srs->toAuthority();   // "EPSG:32634" or "Local"
        if (out->meshCRSTag.isEmpty())
            out->meshCRSTag = srs->description();
        out->meshLinearUnitName = lui.name;
    }
    else
    {
        return fail(tr("No CRS is set for the model."));
    }

    // Build an OGRCoordinateTransformation from layerSRS → meshSRS.
    // Returns nullptr when no transform is needed (same CRS or one unknown).
    // Caller owns the returned object and must call DestroyCT().
    auto makeTransform = [&](const OpenSWMMVisLayer *srcLayer)
        -> OGRCoordinateTransformation *
    {
        if (!meshHasSRS || !srcLayer || !srcLayer->srs()) return nullptr;
        OGRSpatialReference *layerOGRSRS = srcLayer->srs()->ogrSpatialReference();
        if (!layerOGRSRS) return nullptr;
        layerOGRSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (meshOGRSRS.IsSame(layerOGRSRS)) return nullptr;
        return OGRCreateCoordinateTransformation(layerOGRSRS, &meshOGRSRS);
    };

    // Transform a mutable (x,y) pair using ct (if non-null). Returns false and
    // leaves x/y NaN when PROJ could not convert the point, so callers can
    // drop it instead of forwarding OGR's HUGE_VAL as a real coordinate.
    auto xformPt = [](OGRCoordinateTransformation *ct, double &x, double &y) {
        return transformCheckedPt(ct, x, y);
    };

    // ── Boundary source identity ─────────────────────────────────────
    // Only the identity of the boundary source is recorded here.  Feature
    // reading, the UnaryUnion dissolve, ring preparation, the in-domain
    // filter, and marker assignment all run on the worker (prologue of
    // runMeshPipeline) — a boundary carrying tens of thousands of
    // building-footprint holes must never freeze the GUI thread.
    out->modelExtent = modelExt;

    void *boundaryPtr = m_boundaryLayerCombo->currentData().value<void *>();
    void * const kSubcatch = reinterpret_cast<void *>(0x1);

    // Conservative aux-layer prefilter rect (mesh CRS).  The exact in-domain
    // test moved to the worker, so this rect only bounds how much the aux
    // OGR reads below scan — too large reads more, it is never wrong.
    QRectF auxBBox;

    if (boundaryPtr == kSubcatch)
    {
        // Subcatchment polygons are in the model's native CRS (= mesh CRS).
        out->boundaryKind = PipelineInputs::BoundaryKind::Subcatchments;
        for (int i = 0; i < layer->cachedSubcatchCount(); ++i)
        {
            auto verts = layer->cachedSubcatchVertices(i);
            if (verts.size() < 3) continue;
            for (const QPointF &p : verts)
            {
                if (auxBBox.isNull())
                    auxBBox = QRectF(p, QSizeF(0, 0));
                else
                    auxBBox = auxBBox.united(QRectF(p, QSizeF(0, 0)));
            }
            out->subcatchPolys.append(std::move(verts));
        }
        if (out->subcatchPolys.isEmpty())
            return fail(tr("No subcatchment polygons found in the model."));
    }
    else if (auto *bLayer = static_cast<GISVectorLayer *>(boundaryPtr))
    {
        out->boundaryKind      = PipelineInputs::BoundaryKind::VectorFile;
        out->boundaryPath      = bLayer->filePath();
        out->boundaryLayerName = bLayer->ogrLayerName();
        // Snapshot the layer's SRS as WKT: the user may have overridden the
        // file's self-declared CRS on the layer, so the layer object — not
        // the file — is authoritative.  The worker rebuilds the transform
        // from this WKT (OGR SRS/CT objects must not cross threads).
        if (bLayer->srs())
            if (auto *bSRS = bLayer->srs()->ogrSpatialReference())
            {
                char *wkt = nullptr;
                if (bSRS->exportToWkt(&wkt) == OGRERR_NONE)
                    out->boundaryCRSWkt = QString::fromUtf8(wkt);
                CPLFree(wkt);
            }
        // Prefilter rect from the layer extent (its own CRS → mesh CRS).
        const MapExtent be = bLayer->extent();
        if (be.isValid())
        {
            double xs[4] = {be.xMin(), be.xMax(), be.xMin(), be.xMax()};
            double ys[4] = {be.yMin(), be.yMin(), be.yMax(), be.yMax()};
            bool bboxOk = true;
            if (OGRCoordinateTransformation *ct = makeTransform(bLayer))
            {
                bboxOk = (transformChecked(ct, 4, xs, ys) == 0);
                OGRCoordinateTransformation::DestroyCT(ct);
            }
            // This rect is only a read prefilter. A corner OGR left at
            // HUGE_VAL used to widen it to the whole planet; a NaN corner
            // would collapse it via min/max and quietly exclude every
            // feature. Leaving it unset reads the layer unfiltered, which is
            // slower but cannot lose data.
            if (bboxOk)
                auxBBox = QRectF(QPointF(*std::min_element(xs, xs + 4),
                                         *std::min_element(ys, ys + 4)),
                                 QPointF(*std::max_element(xs, xs + 4),
                                         *std::max_element(ys, ys + 4)));
        }
    }
    // else: AutoBBox (default) — the worker builds the 5 %-margin box.

    if (auxBBox.isNull())
    {
        const double m = 0.05;
        const double dx = modelExt.width() * m, dy = modelExt.height() * m;
        auxBBox = QRectF(QPointF(modelExt.xMin() - dx, modelExt.yMin() - dy),
                         QPointF(modelExt.xMax() + dx, modelExt.yMax() + dy));
    }

    // ── Node candidates (SWMM nodes — already in mesh CRS) ───────────
    // Engine rim reads (invert + maxDepth) happen here — engine access is a
    // GUI-thread concern — but the in-domain filter and marker assignment
    // happen on the worker once the domains exist.
    SWMM_Engine engineForRim = layer->engine();
    const bool useRim = m_nodesUseRim->isChecked();
    out->includeJunctions = m_includeJunctions->isChecked();
    if (out->includeJunctions)
    {
        for (int c = SWMMModelLayer::CatJunctions; c <= SWMMModelLayer::CatDividers; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->nodeIndex(name);
                if (idx < 0) continue;
                double x = 0, y = 0;
                if (!layer->cachedNodeCoord(idx, &x, &y)) continue;

                PipelineInputs::CandidateNode cand;
                cand.name = name;
                cand.xy   = QPointF(x, y);

                // Read the rim elevation (invert + maxDepth) once.  The
                // worker decides how it is used (rim pin vs DTM sample vs
                // IDW seed) based on nodesUseRim + DTM availability.
                double invert = 0.0, maxDepth = 0.0;
                cand.hasRim = engineForRim
                    && swmm_node_get_invert_elev(engineForRim, idx, &invert) == SWMM_OK
                    && swmm_node_get_max_depth   (engineForRim, idx, &maxDepth) == SWMM_OK;
                cand.rimZ = invert + maxDepth;

                out->candidateNodes.append(cand);
            }
        }
    }
    out->nodesUseRim       = useRim;
    out->nodeFlattenRadius = useRim ? m_nodeFlattenSpin->value() : 0.0;
    out->nodeMinSeparation =
        (m_nodeMinSepBox->isChecked() && m_nodeMinSepSpin->value() > 0.0)
            ? m_nodeMinSepSpin->value()
            : 0.0;

    // ── Coupling node list (Plan Part B) ─────────────────────────────
    // Every node with coordinates, independent of the junctions-as-Steiner
    // checkbox — the post-generation mapper decides vertex vs cell coupling.
    // No inDomain filter: the mapper classifies outside nodes itself.
    out->mapNodesAfterGen = m_mapNodesAfterGen->isChecked();
    if (out->mapNodesAfterGen)
    {
        for (int c = SWMMModelLayer::CatJunctions; c <= SWMMModelLayer::CatDividers; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->nodeIndex(name);
                if (idx < 0) continue;
                double x = 0, y = 0;
                if (!layer->cachedNodeCoord(idx, &x, &y)) continue;
                out->couplingNodes.append({ name, QPointF(x, y) });
            }
        }
    }

    // ── Link candidates (SWMM links — already in mesh CRS) ───────────
    // Raw polylines only; dedupe → clip → simplify → endpoint filter and
    // marker assignment run on the worker.
    out->includeConduits = m_includeConduits->isChecked();
    if (out->includeConduits)
    {
        for (int c = SWMMModelLayer::CatConduits; c <= SWMMModelLayer::CatOutlets; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->linkIndex(name);
                if (idx < 0) continue;
                out->candidateLinks.append({name, layer->cachedLinkPolyline(idx)});
            }
        }
    }

    // ── Aux point layers (may be in a different CRS) ─────────────────
    // Apply a spatial filter to the OGR layer so only features within the
    // domain bbox are returned — avoids full-file scans of large shapefiles.
    // Reproject coordinates to mesh CRS when the layer CRS differs.
    for (const AuxLayerRow &rowP : std::as_const(m_pointLayerRows))
    {
        if (!rowP.include || !rowP.include->isChecked()) continue;
        auto *vp = rowP.layer;
        if (!vp || !vp->ogrLayer()) continue;

        // useFZ: read feature Z as elevation (only honoured for 3D layers).
        // When no DTM is selected and the layer cannot supply Z, the points
        // have no elevation source → hard-block.
        const bool useFZ = rowP.useZ && rowP.useZ->isChecked() && rowP.is3D;
        if (!haveDTM && !useFZ)
        {
            blockedLayers.append(vp->name());
            continue;
        }

        OGRLayer *ol = vp->ogrLayer();

        ol->SetSpatialFilterRect(auxBBox.left(),
                                  std::min(auxBBox.top(), auxBBox.bottom()),
                                  auxBBox.right(),
                                  std::max(auxBBox.top(), auxBBox.bottom()));
        ol->ResetReading();

        OGRCoordinateTransformation *ct = makeTransform(vp);

        // Record one candidate point: carry its Z when 3D + requested, else
        // fall back to the DTM (hasZ=false).  The exact in-domain filter is
        // applied by the worker once the domains exist.
        auto pushPoint = [&](const OGRPoint *p) {
            if (!p) return;
            double x = p->getX(), y = p->getY();
            // Z is vertical — not reprojected by a 2D transform.
            // Drop the point outright if PROJ could not place it: an aux point
            // is an optional refinement seed, and forwarding a non-finite one
            // would abort the whole run at the generator's finiteness screen.
            if (!xformPt(ct, x, y)) { ++nAuxPointsUnprojectable; return; }
            double z = 0.0;
            const bool fz = useFZ && p->Is3D() && std::isfinite(z = p->getZ());
            if (fz)
            {
                out->auxPoints.append({QPointF(x, y), z, true});
            }
            else if (haveDTM)
            {
                out->auxPoints.append({QPointF(x, y), 0.0, false});
            }
            else
            {
                // 2D feature inside a 3D layer with no DTM — no elevation source.
                if (!blockedLayers.contains(vp->name()))
                    blockedLayers.append(vp->name());
            }
        };

        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            if (auto *geom = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(geom->getGeometryType());
                if (gt == wkbPoint)
                {
                    pushPoint(geom->toPoint());
                }
                else if (gt == wkbMultiPoint)
                {
                    const auto *mp = geom->toMultiPoint();
                    for (int j = 0; j < mp->getNumGeometries(); ++j)
                        pushPoint(mp->getGeometryRef(j)->toPoint());
                }
            }
            OGRFeature::DestroyFeature(f);
        }
        if (ct) OGRCoordinateTransformation::DestroyCT(ct);
        ol->SetSpatialFilter(nullptr);  // clear filter for other callers
    }

    // ── Aux line layers (may be in a different CRS) ───────────────────
    for (const AuxLayerRow &rowL : std::as_const(m_lineLayerRows))
    {
        if (!rowL.include || !rowL.include->isChecked()) continue;
        auto *vl = rowL.layer;
        if (!vl || !vl->ogrLayer()) continue;

        const bool useFZ = rowL.useZ && rowL.useZ->isChecked() && rowL.is3D;
        if (!haveDTM && !useFZ)
        {
            blockedLayers.append(vl->name());
            continue;
        }

        OGRLayer *ol = vl->ogrLayer();

        ol->SetSpatialFilterRect(auxBBox.left(),
                                  std::min(auxBBox.top(), auxBBox.bottom()),
                                  auxBBox.right(),
                                  std::max(auxBBox.top(), auxBBox.bottom()));
        ol->ResetReading();

        OGRCoordinateTransformation *ct = makeTransform(vl);
        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            auto pushLS = [&](const OGRLineString *ls) {
                if (!ls || ls->getNumPoints() < 2) return;
                // A 2D feature with no DTM has no elevation source for its
                // vertices — block rather than silently IDW from distant seeds.
                const bool fz = useFZ && ls->Is3D();
                if (!haveDTM && !fz)
                {
                    if (!blockedLayers.contains(vl->name()))
                        blockedLayers.append(vl->name());
                    return;
                }
                // Raw transformed vertices (+ per-vertex Z for 3D lines);
                // featureZ seeding, dedupe/clip/simplify, and the endpoint
                // domain rule are applied by the worker.
                PipelineInputs::AuxLine al;
                al.hasZ = fz;
                al.path.reserve(ls->getNumPoints());
                if (fz) al.z.reserve(ls->getNumPoints());
                for (int j = 0; j < ls->getNumPoints(); ++j)
                {
                    double x = ls->getX(j), y = ls->getY(j);
                    // Z vertical — not reprojected. A vertex PROJ cannot place
                    // is dropped rather than carried as HUGE_VAL; the path is
                    // a constraint polyline, so a missing interior vertex just
                    // straightens that span.
                    if (!xformPt(ct, x, y)) { ++nAuxLineVertsUnprojectable; continue; }
                    al.path.append(QPointF(x, y));
                    if (fz) al.z.append(ls->getZ(j));
                }
                if (al.path.size() < 2) return;   // nothing usable survived
                out->auxLines.append(std::move(al));
            };
            if (auto *gg = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(gg->getGeometryType());
                if (gt == wkbLineString)
                    pushLS(gg->toLineString());
                else if (gt == wkbMultiLineString)
                {
                    const auto *ml = gg->toMultiLineString();
                    for (int j = 0; j < ml->getNumGeometries(); ++j)
                        pushLS(ml->getGeometryRef(j)->toLineString());
                }
            }
            OGRFeature::DestroyFeature(f);
        }
        if (ct) OGRCoordinateTransformation::DestroyCT(ct);
        ol->SetSpatialFilter(nullptr);
    }

    // ── Hard-block: aux features with no elevation source ────────────
    // A constraining point/line layer that is included with neither a DTM nor
    // a usable feature Z has no way to be elevated — refuse to generate.
    if (!blockedLayers.isEmpty())
    {
        blockedLayers.removeDuplicates();
        return fail(tr(
            "These constraining layers have no elevation source — they carry "
            "no Z coordinate and no DTM raster is selected:\n\n  • %1\n\n"
            "Select a DTM raster, enable \"use Z\" on a 3D layer, or uncheck "
            "the layer.")
            .arg(blockedLayers.join(QStringLiteral("\n  • "))));
    }

    if (nAuxPointsUnprojectable > 0 || nAuxLineVertsUnprojectable > 0)
    {
        // Not fatal — these are optional constraints, and the mesh is valid
        // without them. But dropping input silently is not acceptable either.
        qCWarning(lcMeshPerf)
            << "[Mesh] aux geometry dropped, could not reproject to the mesh"
            << "CRS:" << nAuxPointsUnprojectable << "point(s),"
            << nAuxLineVertsUnprojectable << "line vertex/vertices";
    }

    // ── Region marker seeds (subcatchments) ──────────────────────────
    // Use name-based objectExtent() for the seed point — this is safe,
    // consistent, and avoids any index-mapping assumption between
    // categoryCount(CatSubcatchments) and cachedSubcatchVertices(i).
    // The bounding-box centroid is a reliable interior point for all
    // but highly concave subcatchments; Triangle propagates the region
    // attribute to every triangle whose circumcenter is reachable from
    // the seed, so a small positional error is acceptable.  The region
    // attribute (marker) is assigned by the worker, after node/link
    // markers, preserving the original numbering.
    out->includeSubcatch = m_includeSubcatch->isChecked();
    if (out->includeSubcatch)
    {
        const auto cat = SWMMModelLayer::CatSubcatchments;
        for (int row = 0; row < layer->categoryCount(cat); ++row)
        {
            const QString name = layer->objectNameAt(cat, row);
            if (name.isEmpty()) continue;
            const MapExtent ce = layer->objectExtent(name);
            if (!ce.isValid()) continue;
            out->subcatchSeeds.append({name,
                QPointF((ce.xMin()+ce.xMax())*0.5, (ce.yMin()+ce.yMax())*0.5)});
        }
    }

    // Steiner snap-and-dedupe runs on the worker, after it has assembled
    // steinerPoints from the filtered candidates.

    // ── Quality options ──────────────────────────────────────────────
    out->genOpts.maxArea          = m_maxAreaSpin->value();
    out->genOpts.minAngle         = m_minAngleSpin->value();
    out->genOpts.maxSteinerPoints = m_maxSteinerSpin->value();
    out->genOpts.allowSteiner     = m_allowSteiner->isChecked();
    out->genOpts.quiet            = true;

    // ── Quad quality bounds (regions + G2 merge share one set) ───────
    if (m_quadMinAngleSpin)
    {
        out->quadBounds.minAngleDeg       = m_quadMinAngleSpin->value();
        out->quadBounds.maxAngleDeg       = m_quadMaxAngleSpin->value();
        out->quadBounds.minScaledJacobian = m_quadMinSjSpin->value();
        out->quadBounds.maxAspect         = m_quadMaxAspectSpin->value();   // 0 = off
    }
    out->genOpts.quadRegionBounds = out->quadBounds;

    // ── Quads everywhere (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.5) ───
    out->quadEverywhere = m_quadEverywhereCheck && m_quadEverywhereCheck->isChecked();
    out->quadEverywhereSpacing =
        m_quadEverywhereSpacingSpin ? m_quadEverywhereSpacingSpin->value() : 0.0;

    // ── Quad cells (G2 merge) + structured patches (G3) ──────────────
    out->genOpts.mergeTrianglePairs = m_quadMergeBox && m_quadMergeBox->isChecked();
    if (m_quadMergeBox)
    {
        out->genOpts.quadMerge.minAngleDeg        = out->quadBounds.minAngleDeg;
        out->genOpts.quadMerge.maxAngleDeg        = out->quadBounds.maxAngleDeg;
        out->genOpts.quadMerge.minScaledJacobian  = out->quadBounds.minScaledJacobian;
        out->genOpts.quadMerge.maxAspect          = out->quadBounds.maxAspect;
        out->genOpts.quadMerge.maxBedNonPlanarity = m_quadPlanaritySpin->value();
    }

    // ── PSLG quad regions (QUAD_MESHING_REDESIGN_PLAN §3.1 sources) ──
    // Defaults first (applied to every region the worker builds), then the
    // layer identity (read on the worker, like the boundary layer), then the
    // named subcatchments — those rings are already cached in the mesh CRS,
    // so they are resolved here.  Distances are map units, as for every other
    // distance spin in this dialog (no display→SI conversion on collect).
    if (m_quadRegionModeCombo)
    {
        out->quadRegionDefaults.mode =
            mesh::QuadRegionMode(m_quadRegionModeCombo->currentData().toInt());
        out->quadRegionDefaults.spacing   = m_quadRegionSpacingSpin->value();
        out->quadRegionDefaults.aspectMax = m_quadRegionAspectSpin->value();
        // The special value at the minimum means "no fixed angle".
        out->quadRegionDefaults.hasAlignAngle =
            m_quadRegionAngleSpin->value() > m_quadRegionAngleSpin->minimum();
        out->quadRegionDefaults.alignAngleDeg =
            out->quadRegionDefaults.hasAlignAngle ? m_quadRegionAngleSpin->value() : 0.0;
    }
    if (m_quadRegionLayerCombo)
        if (auto *qLayer = static_cast<GISVectorLayer *>(
                m_quadRegionLayerCombo->currentData().value<void *>()))
        {
            PipelineInputs::QuadRegionLayerSpec spec;
            spec.path      = qLayer->filePath();
            spec.layerName = qLayer->ogrLayerName();
            // Layer object, not file, is authoritative for the CRS (the user
            // may have overridden it) — same as boundaryCRSWkt above.
            if (qLayer->srs())
                if (auto *qSRS = qLayer->srs()->ogrSpatialReference())
                {
                    char *wkt = nullptr;
                    if (qSRS->exportToWkt(&wkt) == OGRERR_NONE)
                        spec.crsWkt = QString::fromUtf8(wkt);
                    CPLFree(wkt);
                }
            out->quadRegionLayers.append(std::move(spec));
        }
    if (m_quadRegionSubcatchEdit)
    {
        const QStringList ids = m_quadRegionSubcatchEdit->text()
                                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const auto cat = SWMMModelLayer::CatSubcatchments;
        for (const QString &rawId : ids)
        {
            const QString id = rawId.trimmed();
            if (id.isEmpty()) continue;
            // objectNameAt(CatSubcatchments, row) and cachedSubcatchVertices(row)
            // index the same cache, so the row found by name is the ring's.
            QVector<QPointF> ring;
            bool found = false;
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                if (layer->objectNameAt(cat, row) != id) continue;
                found = true;
                ring  = layer->cachedSubcatchVertices(row);
                break;
            }
            if (!found)
                return fail(tr("Quad region: subcatchment '%1' not found").arg(id));
            if (ring.size() < 3)
                return fail(tr("Quad region: subcatchment '%1' has no polygon").arg(id));
            mesh::QuadRegion r = out->quadRegionDefaults;
            r.ring = QPolygonF(ring);
            // Same spelling the worker gives RegionMarker tags, so the cells
            // inside match the region-defaults table rows.
            r.tag  = QStringLiteral("subcatch_%1").arg(id);
            out->quadRegions.append(std::move(r));
        }
    }
    // Patch boundary vertices are matched to the Triangle output by the
    // same snap radius the Steiner dedupe uses (0 = exact quantised match).
    out->genOpts.patchSnapEps = m_snapEpsSpin->value();
    for (int r = 0; m_patchTable && r < m_patchTable->rowCount(); ++r)
    {
        auto cell = [this, r](int c) {
            const QTableWidgetItem *it = m_patchTable->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        // "x y; x y; …" → points
        QVector<QPointF> pts;
        bool ptsOk = true;
        const QStringList pairs = cell(1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &pr : pairs)
        {
            const QStringList xy = pr.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            bool okx = false, oky = false;
            if (xy.size() == 2)
                pts.append(QPointF(xy[0].toDouble(&okx), xy[1].toDouble(&oky)));
            if (!(okx && oky)) { ptsOk = false; break; }
        }
        if (!ptsOk)
            return fail(tr("Structured patch row %1: points must be \"x y; x y; …\".").arg(r + 1));
        const bool swept = cell(0).startsWith(QStringLiteral("Swept"), Qt::CaseInsensitive);
        QString perr;
        mesh::PatchMesh pm;
        if (swept)
        {
            mesh::SweptPatch sp;
            sp.centreline = pts;
            sp.across     = cell(2).toInt();
            sp.along      = cell(3).toDouble();
            sp.width      = cell(4).toDouble();
            sp.tag        = cell(5);
            pm = mesh::makeSweptPatch(sp, &perr);
        }
        else
        {
            mesh::StructuredPatch st;
            st.corners = pts;
            st.n       = cell(2).toInt();
            st.m       = cell(3).toInt();
            st.tag     = cell(5);
            pm = mesh::makeTransfinitePatch(st, &perr);
        }
        if (!perr.isEmpty())
            return fail(tr("Structured patch row %1: %2").arg(r + 1).arg(perr));
        out->patches.append(std::move(pm));
    }

    // ── Minimum cell size ────────────────────────────────────────────
    out->minSizePolicy = mesh::pslg::MinSizePolicy{};
    out->minSizePolicy.minCellSize =
        m_minCellSizeSpin ? m_minCellSizeSpin->value() : 0.0;
    out->minSizePolicy.trimAngleDeg =
        m_trimAngleSpin ? m_trimAngleSpin->value() : 0.0;
    out->minSizePolicy.trimAtTaggedNodes =
        m_trimAtNodesBox && m_trimAtNodesBox->isChecked();
    out->minSizePolicy.dropSubScaleHoles =
        m_dropSubScaleHolesBox && m_dropSubScaleHolesBox->isChecked();
    // V2 enforcement: identity merges inside the conditioner ride the same
    // policy the weld stage already consults.
    out->minSizeEnforce = m_minSizeEnforceBox && m_minSizeEnforceBox->isChecked()
                          && out->minSizePolicy.minCellSize > 0.0;
    out->minSizePolicy.allowIdentityMerge = out->minSizeEnforce;
    out->minSizePolicy.resolveDefaults();
    out->minSizeCleanup = m_cleanupBox && m_cleanupBox->isChecked();

    // V2 graded sizing; meaningful only with a uniform cap to relax.
    out->sizeGradation =
        (m_gradationSpin && m_maxAreaSpin && m_maxAreaSpin->value() > 0.0)
            ? m_gradationSpin->value()
            : 0.0;

    // ── Thinning ─────────────────────────────────────────────────────
    out->doThinning                      = m_thinningBox->isChecked();
    out->thinnerOpts.normalDotThreshold  = m_thinningToleranceSpin->value();
    out->thinnerOpts.maxIterations       = m_thinningIterationsSpin->value();
    out->thinnerOpts.maxPoints           = m_thinningMaxPointsSpin->value();
    out->thinnerOpts.gridSpacing         = 0.0;
    out->thinnerOpts.useMinSpacing       = m_minSpacingBox->isChecked();
    out->thinnerOpts.minSpacing          = m_minSpacingSpin->value();
    // 2026-07-19 — boundary-aware terrain filter buffer; <= 0 → auto
    // (0.5 × effective terrain spacing, resolved in the worker).
    out->terrainBoundaryBuffer           = (m_boundaryBufferSpin->value() > 0.0)
                                               ? m_boundaryBufferSpin->value()
                                               : -1.0;

    // ── Output ───────────────────────────────────────────────────────
    out->outputMode    = m_outputExternal->isChecked()
                             ? mesh::MeshOutputMode::External
                             : mesh::MeshOutputMode::Inline;
    out->meshOutputPath = m_meshPathEdit->text().trimmed();
    out->manningsN      = m_manningsValueSpin->value();
    out->initDepth      = m_initDepthSpin->value();

    // ── Region defaults (GG0d, GUI plan §3.3) ────────────────────────
    // Read on the GUI thread and copied BY VALUE — the worker never touches
    // the widget. Both containers stay empty for a dialog whose table was
    // never edited, so the pipeline below behaves exactly as it did before
    // the table existed.
    if (m_regionDefaults)
    {
        QString regionErr;
        if (!m_regionDefaults->validate(&regionErr))
            return fail(regionErr);

        out->infilDefaults = m_regionDefaults->infilDefaults();

        const auto rows = m_regionDefaults->rows();
        for (const auto &r : rows)
        {
            // Row 0 is '*', whose values are already in manningsN/initDepth
            // above; a region row with both cells blank is still inheriting
            // and must not be materialised.
            if (r.tag == QLatin1String("*")) continue;
            const bool hasN = !std::isnan(r.manningsN);
            const bool hasD = !std::isnan(r.initDepth);
            if (!hasN && !hasD) continue;

            PipelineInputs::RegionHydraulics rh;
            rh.manningsN = hasN ? r.manningsN : out->manningsN;
            rh.initDepth = hasD ? r.initDepth : out->initDepth;
            out->regionHydraulics.insert(r.tag, rh);
        }
    }

    // ── Vertical Z conversion factor ─────────────────────────────────
    out->zConversionFactor = m_zFactorSpin ? m_zFactorSpin->value() : 1.0;

    // ── Elevation interpolation (no-DTM fallback) ────────────────────
    out->elevInterpMethod = ElevInterpMethod(
        m_elevMethodCombo->currentData().toInt());
    out->nnVariant = NNVariant(
        m_nnVariantCombo->currentData().toInt());
    out->idwPower = m_idwPowerSpin->value();

    return true;
}

// ---------------------------------------------------------------------------
// Accept → launch threaded pipeline
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onAccept()
{
    PipelineInputs inputs;
    QString err;
    if (!collectInputs(&inputs, &err))
    {
        QMessageBox::critical(this, tr("Cannot generate mesh"), err);
        return;
    }

    // Warn before overwriting an existing mesh at the same output path so a
    // regeneration replaces the old mesh rather than silently clobbering it.
    if (inputs.outputMode == mesh::MeshOutputMode::External
        && !inputs.meshOutputPath.isEmpty()
        && QFileInfo::exists(inputs.meshOutputPath))
    {
        const auto ovBtn = QMessageBox::warning(
            this, tr("Overwrite existing mesh?"),
            tr("A mesh file already exists at:\n\n%1\n\nGenerating will "
               "overwrite it and replace the existing mesh. Continue?")
                .arg(QDir::toNativeSeparators(inputs.meshOutputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ovBtn != QMessageBox::Yes)
            return;
    }

    // Show embedded progress bar and switch Generate→disabled, Cancel→"Stop".
    m_progressBar->setValue(0);
    m_progressLabel->setText(tr("Starting…"));
    m_progressBar->setVisible(true);
    m_progressLabel->setVisible(true);
    m_generateBtn->setEnabled(false);
    m_cancelBtn->setText(tr("Cancel Generation"));

    // Watcher forwards progress + completion to the main thread.
    m_watcher = new QFutureWatcher<PipelineResult>(this);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::progressValueChanged,
            m_progressBar, &QProgressBar::setValue);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::progressTextChanged,
            m_progressLabel, &QLabel::setText);
    connect(m_watcher, &QFutureWatcher<PipelineResult>::finished,
            this, &MeshGenerationDialog::onMeshFinished);

    m_watcher->setFuture(
        QtConcurrent::run(runMeshPipeline, std::move(inputs)));
}

// ---------------------------------------------------------------------------
// Completion handler (called on main thread via queued signal)
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onMeshFinished()
{
    // Restore UI state regardless of outcome.
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_generateBtn->setEnabled(true);
    m_cancelBtn->setText(tr("Cancel"));

    m_cancelBtn->setEnabled(true);  // re-enable in all paths

    if (m_watcher->isCanceled())
    {
        m_watcher->deleteLater();
        m_watcher = nullptr;
        return;
    }

    // result() rethrows any exception captured from the worker thread.
    // The worker wrapper already converts exceptions into failed results,
    // but keep a belt-and-braces catch so a throw from the future machinery
    // itself can never terminate the app.
    PipelineResult result;
    try {
        result = m_watcher->result();
    } catch (const std::exception &e) {
        result.ok = false;
        result.errorMsg = tr("Mesh pipeline failed: %1")
                              .arg(QString::fromUtf8(e.what()));
    } catch (...) {
        result.ok = false;
        result.errorMsg = tr("Mesh pipeline failed with an unknown error.");
    }
    m_watcher->deleteLater();
    m_watcher = nullptr;

    if (!result.ok)
    {
        QMessageBox::critical(this, tr("Mesh generation failed"),
            result.errorMsg.isEmpty() ? tr("Unknown error.") : result.errorMsg);
        return;
    }

    // Add the generated mesh layer to the canvas (main thread — safe).
    if (auto *canvas = m_pw->canvas())
    {
        // Deactivate any existing meshes, and REMOVE any mesh layer that
        // already references the same output path — regenerating a mesh at an
        // existing path must REPLACE it, not stack a second (stale) layer on
        // the canvas. A lingering duplicate is not just visually wrong: on save
        // the write path (SWMMVisProjectWindow) pushes *every* mesh layer into
        // the engine, so the old mesh can win and reappear on reopen.
        const QString newMeshCanonical =
            result.meshPath.isEmpty()
                ? QString()
                : QFileInfo(result.meshPath).absoluteFilePath();
        QList<SWMM2DMeshLayer *> staleMeshes;
        for (auto *L : canvas->layers()) {
            auto *m = qobject_cast<SWMM2DMeshLayer *>(L);
            if (!m) continue;
            m->setActiveMesh(false);
            if (!newMeshCanonical.isEmpty()
                && !m->sourcePath().isEmpty()
                && QFileInfo(m->sourcePath()).absoluteFilePath() == newMeshCanonical)
                staleMeshes.append(m);
        }
        for (SWMM2DMeshLayer *stale : staleMeshes) {
            const int idx = canvas->layers().indexOf(stale);
            if (idx >= 0) {
                if (OpenSWMMVisLayer *taken =
                        canvas->takeLayer(idx, /*pushUndo=*/false))
                    taken->deleteLater();
            }
        }

        // Carry the generated 1D<->2D coupling onto the mesh vertices'
        // coupledNode field so the layer (and any later engine-sync save)
        // reflects it without a reload. The descriptive tag stays separate.
        for (auto it = result.coupling.vertexToNode.cbegin();
             it != result.coupling.vertexToNode.cend(); ++it) {
            const int vi = it.key();
            if (vi >= 0 && vi < result.meshResult.vertices.size())
                result.meshResult.vertices[vi].coupledNode = it.value();
        }

        const bool isExt = (result.outputMode == mesh::MeshOutputMode::External);
        // deferHeavyGeometry: build only the light scene geometry here on the
        // GUI thread; wireframe edges / spatial grids / vertex adjacency / BC
        // slots arrive via finishSceneGeometryAsync() below — same
        // progressive-load path as the file-open flow (swmmvis.cpp). The
        // synchronous build both froze the UI on a large generated mesh and
        // could throw bad_alloc inside a slot (std::terminate).
        auto *meshLayer  = new SWMM2DMeshLayer(std::move(result.meshResult),
                                               result.meshPath,
                                               /*parent=*/nullptr,
                                               /*deferHeavyGeometry=*/true);
        meshLayer->setExternalMesh(isExt);
        meshLayer->setActiveMesh(isExt);
        meshLayer->setName(result.meshPath.isEmpty()
                               ? QStringLiteral("Mesh (inline)")
                               : QFileInfo(result.meshPath).fileName());

        // Propagate the SWMM model's CRS to the mesh layer so
        // rebuildSceneGeometry() can reproject from model CRS → canvas CRS.
        // Without this the mesh renders at raw local coordinates and never
        // appears on the map.
        if (m_pw->modelLayer() && m_pw->modelLayer()->srs())
            meshLayer->setSRS(
                new SpatialReferenceSystem(*m_pw->modelLayer()->srs(), meshLayer),
                /*ownsSRS=*/true);

        canvas->addLayer(meshLayer, /*pushUndo=*/true);
        m_pw->attachMeshLayer(meshLayer);
        // Kick the deferred heavy build now that the layer is adopted —
        // mirrors the file-open path (swmmvis.cpp attachMesh2DLayersAsync).
        meshLayer->finishSceneGeometryAsync();
    }

    // Mirror the mesh linkage into the engine's in-memory model so the next
    // save emits (or drops) [2D_MESH_FILE] correctly. External mode points the
    // reference at the freshly-written .2dm; inline mode clears it. Without
    // this the engine re-serialises the .inp on save with mesh_file empty and
    // the just-written linkage is lost — the model silently runs 1D-only.
    if (m_pw->modelLayer() && m_pw->modelLayer()->engine())
    {
        SWMM_Engine eng = m_pw->modelLayer()->engine();
        if (result.outputMode == mesh::MeshOutputMode::External)
            swmm_options_set_ext(eng, "MESH_FILE",
                                 result.meshPath.toUtf8().constData());
        else
            swmm_options_set_ext(eng, "MESH_FILE", "");
    }

    m_pw->setHasChanges(true);
    accept();
}

// ---------------------------------------------------------------------------
// Cancel / close
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onCancelOrReject()
{
    if (m_watcher && m_watcher->isRunning())
    {
        // Worker is active — request cancellation; onMeshFinished will
        // restore the UI and clean up the watcher.
        m_watcher->cancel();
        m_cancelBtn->setEnabled(false);  // prevent double-cancel
        m_progressLabel->setText(tr("Cancelling…"));
    }
    else
    {
        reject();
    }
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void MeshGenerationDialog::onBrowseMeshPath()
{
    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Choose Mesh File"), m_meshPathEdit->text(),
        tr("SWMMVis 2D Mesh (*.2dm);;All files (*)"));
    if (!picked.isEmpty())
        m_meshPathEdit->setText(picked);
}
