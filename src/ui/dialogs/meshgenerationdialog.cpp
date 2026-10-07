/*!
 * \file   meshgenerationdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/meshgenerationdialog.h"
#include "map/channelmeshadoptioncommand.h"
#include "ui/theme/themehelpers.h"
#include "ui/widgets/meshregiondefaultswidget.h"
#include "ui/widgets/corridorsourceswidget.h"

#include "ui/uiscrollhelpers.h"

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "core/editgeometry.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/featurelayer.h"
#include "feature/featureroles.h"

#include "mesh/meshgenerator.h"
#include "mesh/meshnodemapper.h"
#include "mesh/meshpatch.h"
#include "mesh/meshresult.h"
#include <openswmm/engine/openswmm_edit.h>
#include <QScopeGuard>
#include "mesh/dtmraster.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/inpmeshreader.h"
#include "mesh/naturalnbinterpolator.h"
#include "mesh/meshreorder.h"
#include "mesh/boundaryconditioning.h"
#include "mesh/meshstagecache.h"
#include "mesh/pslgprep.h"
#include "mesh/sizefield.h"
#include "mesh/terrainbreaklines.h"
#include "mesh/terrainsizefield.h"
#include "mesh/terrainerrorfield.h"
#include "mesh/terrainlocalcopy.h"
#include "mesh/quadblocks.h"
#include "project/meshcorridorrecipe.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
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
#include <QCloseEvent>
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
#include <QJsonArray>
#include <QJsonObject>
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
#include <memory>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

// lcMeshPerf ("openswmm.mesh.perf") is defined in mesh/meshstagecache.cpp
// and declared by its header, included above.

// ---------------------------------------------------------------------------
// PSLG geometry utilities — implementations live in mesh/pslgprep.{h,cpp};
// the using-declarations keep every unqualified call site below unchanged.
// ---------------------------------------------------------------------------

using mesh::pslg::trimByStraightness;
using mesh::pslg::distSqToSegment;
using mesh::pslg::snapAndDedupe;

/*! Terrain cache MiB for the dialog's "Automatic" (0) setting: one eighth of
 *  physical memory, between 256 MiB and 8 GiB. Cache size bounds residency
 *  only; it never changes the generated mesh. */
static int resolvedTerrainCacheMiB(int requested)
{
    if (requested > 0) return requested;
    qint64 bytes = 0;
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status{}; status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) bytes = qint64(status.ullTotalPhys);
#elif defined(Q_OS_MACOS)
    int64_t mem = 0; size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) bytes = mem;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page > 0) bytes = qint64(pages) * page;
#endif
    return int(std::clamp<qint64>(bytes / 8 / (1024 * 1024), 256, 8192));
}

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
 *  - NaN is already the pipeline's "no value" sentinel (DTMRaster,
 *    NaturalNeighbourInterpolator both use it), so downstream
 *    finiteness checks catch it uniformly.
 *  - HUGE_VAL passes as an ordinary large coordinate. It survives a bbox
 *    comparison, gets averaged into a centroid, and reaches the generator intact.
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
static void
runMeshPipelineImpl(QPromise<MeshGenerationDialog::PipelineResult> &promise,
                    MeshGenerationDialog::PipelineInputs            in)
{
    using PResult = MeshGenerationDialog::PipelineResult;

    // Adds a failure result and marks the promise done.
    // Callers `return` immediately after calling this.
    // Node vertices pinned to their rim (invert + max depth) where a DTM can
    // still lower them: the rim applies only where the terrain is higher.
    QSet<int> rimPinnedMarkers;
    auto fail = [&](const QString &msg) {
        PResult r; r.ok = false; r.errorMsg = msg;
        promise.addResult(r);
    };

    promise.setProgressRange(0, 100);

    auto progress = [&](int pct, const QString &msg) {
        promise.setProgressValueAndText(pct, msg);
    };

    progress(0, QObject::tr("Reading selected corridor features…"));
    auto corridors = mesh::readCorridorSources(in.corridorSources, in.meshCRSWkt,
                                              [&promise] { return promise.isCanceled(); });
    if (!corridors.ok()) { fail(corridors.error); return; }
    QVector<QPolygonF> featureCorridorRings;
    for (const auto &patch : std::as_const(corridors.patches)) {
        QString error;
        const auto boundary = mesh::orderedPatchBoundary(patch, &error);
        if (!error.isEmpty()) { fail(error); return; }
        featureCorridorRings.append(boundary);
        in.patches.append(patch);
    }

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
        // The trimming parameters participate in the key: the cached
        // payload is the PREPARED rings, so an entry built with different
        // trimming would silently mesh the wrong geometry.
        boundaryCacheKey = mesh::MeshStageCache::boundaryKey(
            srcId, subHash, in.boundaryLayerName, in.boundaryCRSWkt,
            in.meshCRSWkt, in.trimTurnDeg, in.trimDeviation,
            in.minCellSize, in.conditionBoundary);

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
            // (constraint recovery failure, or a flooded exterior carve).
            // Fall back to the unsimplified ring — GEOS/OGR output is valid by
            // construction; only RDP can break it.
            const QVector<QPointF> rawExt = ringToMesh(ext);
            // A conditioned boundary is already simplified with its holes in
            // view; trimming the exterior alone could cut through a building
            // merged into it as a notch.
            QVector<QPointF> simpExt = in.conditionBoundary && in.minCellSize > 0.0
                ? rawExt : trimByStraightness(rawExt, in.trimTurnDeg, in.trimDeviation, {}, true);
            {
                EditGeometry::RingPolygon check;
                check.exterior = simpExt;
                if (EditGeometry::validateRingPolygon(check)
                    != EditGeometry::RingValidity::Ok)
                    simpExt = rawExt;
            }
            in.domains.append(QPolygonF(simpExt));
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

        // Footprint conditioning (MESH_REGIONAL_TRIQUAD_PLAN D-R3): close,
        // open and simplify the dissolved region at the minimum cell size so
        // building detail that adds no shape cannot force tiny cells.
        auto conditionRegion = [&](OGRGeometry *&region) {
            if (!in.conditionBoundary || !(in.minCellSize > 0.0)) return;
            mesh::BoundaryConditionReport rep;
            OGRGeometry *conditioned = mesh::conditionMeshRegion(
                region, in.minCellSize, 0.25 * in.minCellSize, &rep);
            if (!conditioned) {
                qCWarning(lcMeshPerf) << "[Mesh][boundary] conditioning failed; meshing the boundary as drawn";
                return;
            }
            OGRGeometryFactory::destroyGeometry(region);
            region = conditioned;
            qCInfo(lcMeshPerf).noquote() << QStringLiteral(
                "[Mesh][boundary] conditioned at %1: polygons %2 -> %3, holes %4 -> %5, vertices %6 -> %7, area %8 -> %9 (%10 ms)")
                .arg(in.minCellSize).arg(rep.polygonsIn).arg(rep.polygonsOut).arg(rep.holesIn).arg(rep.holesOut)
                .arg(rep.verticesIn).arg(rep.verticesOut).arg(rep.areaIn,0,'f',1).arg(rep.areaOut,0,'f',1).arg(rep.milliseconds);
            stageMark("boundary: conditioning");
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
            if (unioned) conditionRegion(unioned);
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
                        if (dissolved) conditionRegion(dissolved);
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

    // Read the DEM window once into the mesh cache as float32
    // (MESH_SPEED_DEM_IO_PLAN_2026-10-03.md): break line ranking, vertex
    // elevations and the final check each read the terrain again, and from a
    // large uncompressed DEM on a slow volume every pass was bound by that
    // volume. Later runs reuse the copy.
    if (!in.dtmPath.isEmpty() && cache.isUsable() && in.terrainReference != 2) {
        QRectF domainBox;
        for (const auto &ring : std::as_const(in.domains)) domainBox = domainBox.united(ring.boundingRect());
        stageClock.restart();
        progress(14, QObject::tr("Caching the terrain window…"));
        const auto local = mesh::prepareLocalTerrain(in.dtmPath, in.meshCRSWkt, domainBox, cache.dir(), [&](double) {
            return !promise.isCanceled();
        }, in.terrainReference == 0 ? in.minCellSize : 0.0);
        if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
        if (!local.path.isEmpty()) {
            qCInfo(lcMeshPerf).noquote() << QStringLiteral("[Mesh][terrain] local copy %1 (%2 MB, %3 x %3 pixel average): %4")
                .arg(local.reused ? QStringLiteral("reused") : QStringLiteral("written"))
                .arg(local.bytes / 1e6, 0, 'f', 0).arg(local.decimation).arg(local.path);
            in.dtmPath = local.path;
            cache.prune();
        } else {
            qCInfo(lcMeshPerf).noquote() << "[Mesh][terrain] no local copy:" << local.note;
        }
        stageMark("local terrain copy");
    }
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
        // make the PSLG non-planar and prevent constraint recovery. Endpoint
        // filtering below is the primary guard; this strips runaway interior vertices.
        // The test must be against the ring, not its bounding box: on a
        // non-rectangular (e.g. DEM-footprint) domain a link whose middle
        // bulges outside the polygon while staying inside the bbox would
        // otherwise carry segments that cross the boundary ring and prevent
        // constraint recovery.
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
                // Skip nodes outside the meshing domain — the mesher ignores
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
            // The separation is at least the minimum cell size (the coupling
            // policy of MESH_OVERHAUL_PLAN_2026-09-29.md §3): two pinned
            // nodes closer than h_min share a cell instead of forcing one.
            const double effNodeSep = std::max(in.nodeMinSeparation, in.minCellSize);
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
                    if (haveDTM) rimPinnedMarkers.insert(nextMarker);
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
            int unfolded = 0;
            for (const auto &link : std::as_const(in.candidateLinks))
            {
                // Dedupe, unfold (a vertex list stored backwards or a stray
                // vertex folds the alignment onto itself), then drop
                // intermediate vertices closer than the minimum cell size
                // (deviation-capped, endpoints kept): the alignment keeps its
                // shape and coupling identity, and no conduit vertex pair can
                // demand a sub-floor cell.
                bool changed = false;
                QVector<QPointF> path = mesh::pslg::unfoldPolyline(dedupeSegPath(link.second), 150.0, &changed);
                if (changed) ++unfolded;
                path = mesh::pslg::resampleMinLength(clipIntermediateToDomain(path),
                                                     in.minCellSize, 0.1 * in.minCellSize);
                if (path.size() < 2) continue;
                // Both endpoints must be inside the domain polygon — a link
                // crossing the boundary without a vertex at the crossing
                // makes the PSLG non-planar and prevents constraint recovery.
                if (!inDomain(path.first()) || !inDomain(path.last())) continue;
                mesh::ConstraintSegment cs;
                cs.path = std::move(path); cs.marker = nextMarker; cs.tag = link.first;
                cs.stripWidth = in.conduitStripWidth;   // 0 = plain edges (MESH_TRIANGLE_ENGINE_PLAN D12.4)
                in.constraintSegs.append(cs);
                in.edgeMarkerToTag.insert(nextMarker, link.first);
                ++nextMarker;
            }
            if (unfolded > 0)
                qInfo() << "[Mesh]" << unfolded
                        << "conduit alignment(s) unfolded (vertices stored backwards or a stray vertex)";
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
            QVector<QPointF> path = trimByStraightness(
                clipIntermediateToDomain(dedupeSegPath(al.path)),
                in.trimTurnDeg, in.trimDeviation);
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
        // sources (automatic tolerance, a hundredth of the floor size);
        // tagged SWMM points (marker != 0) are never merged.
        snapAndDedupe(in.steinerPoints, 0.01 * in.minCellSize);
        stageMark("candidate filtering + markers");
    }

    // ── Domain + holes ──────────────────────────────────────────────
    progress(14, QObject::tr("Building input PSLG…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    mesh::MeshGenerator g;
    g.setDomains(in.domains);

    // Interior rings → constraint segments (hole boundary edges) + a seed
    // point that tells the mesher to leave the region unmeshed.  Rings arrive
    // RAW from the boundary ingestion above; simplification, validation (on
    // the small simplified ring — see pslgprep.h), densification, and the
    // interior seed are computed in parallel chunks — or come straight from
    // the Stage-A cache on a hit.
    if (!bprepReady)
    {
        QVector<mesh::pslg::PreparedRing> prepared;
        int skippedRings = 0;
        // Conditioned rings are already simplified together; trim no further.
        const bool conditioned = in.conditionBoundary && in.minCellSize > 0.0;
        const bool holesDone = mesh::pslg::prepareHoleRings(
            in.holeRings, 0.0, 0.0, &prepared,
            [&promise] { return promise.isCanceled(); },
            [&](int done, int total) {
                promise.setProgressValueAndText(
                    14 + static_cast<int>(5.0 * done / std::max(total, 1)),
                    QObject::tr("Preparing hole rings… (%1 / %2)")
                        .arg(done).arg(total));
            },
            &skippedRings,
            conditioned ? 0.0 : in.trimTurnDeg, conditioned ? 0.0 : in.trimDeviation);
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
        // rather than a generic constraint recovery failure failing the whole mesh.
        if (!bprep.holeValid[k]) continue;

        // Boundary edges of the hole must exist in the PSLG…
        mesh::ConstraintSegment cs;
        cs.path   = bprep.holeRings[k];
        cs.marker = 0;
        g.addConstraintSegment(cs);

        // …plus a seed point strictly inside the ring so the mesher carves
        // it.  interiorPoint() is robust for non-convex rings; a vertex
        // centroid could fall outside the ring (or in another region),
        // silently leaving the hole unmeshed or removing the wrong area.
        g.addHole(bprep.holeSeeds[k]);
    }
    if (bprep.skippedRings > 0)
        qWarning() << "[Mesh] Skipped" << bprep.skippedRings
                   << "invalid hole ring(s) — self-intersecting or degenerate.";

    mesh::TerrainErrorField terrainReference;

    for (const auto &cs : std::as_const(in.constraintSegs))
        g.addConstraintSegment(cs);

    // ── Quad regions (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12.1) ──
    // A region is a polygon with an optional size (field "h" or
    // "quad_spacing"), an optional "cells" choice and an optional "tag"
    // (field names from the role registry — FEATURE_LAYER_ROLES_AND_FIELDS
    // §4.3); a four-sided one is filled with quads aligned to its sides, any
    // other keeps triangles inside, and cells = triangles keeps triangles
    // whatever its shape. Layer regions are read HERE with a fresh GDAL
    // handle (handles must not cross threads); subcatchment regions arrive
    // resolved from collectInputs and are appended after them.
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
                rSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                mSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                if (rSRS.importFromWkt(spec.crsWkt.toUtf8().constData()) == OGRERR_NONE
                    && mSRS.importFromWkt(in.meshCRSWkt.toUtf8().constData()) == OGRERR_NONE
                    && !rSRS.IsSame(&mSRS))
                    regionCT = OGRCreateCoordinateTransformation(&rSRS, &mSRS);
            }
            GDALDataset *ds = static_cast<GDALDataset *>(GDALOpenEx(
                spec.path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr));
            if (!ds)
            {
                if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
                qWarning() << "[Mesh][quad] region layer could not be opened:" << spec.path;
                continue;
            }
            OGRLayer *layer = spec.layerName.isEmpty() ? ds->GetLayer(0)
                                                       : ds->GetLayerByName(spec.layerName.toUtf8().constData());
            if (!layer)
            {
                GDALClose(ds);
                if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
                qWarning() << "[Mesh][quad] region layer not found:" << spec.layerName;
                continue;
            }
            layer->ResetReading();
            const QStringList sizeFields = openswmmvis::feature::regionSizeFieldNames();
            const QByteArray cellsField = openswmmvis::feature::regionCellsFieldName().toUtf8();
            const QByteArray tagField   = openswmmvis::feature::regionTagFieldName().toUtf8();
            while (OGRFeature *f = layer->GetNextFeature())
            {
                auto fieldIdx = [&](const char *name) { return f->GetFieldIndex(name); };
                // One "cells" choice per feature, whatever its part count. An
                // unknown value is logged and treated as auto (plan §6).
                mesh::QuadRegionMode regionMode = mesh::QuadRegionMode::Auto;
                if (const int i = fieldIdx(cellsField.constData());
                    i >= 0 && f->IsFieldSetAndNotNull(i))
                {
                    const QString value = QString::fromUtf8(f->GetFieldAsString(i));
                    bool known = true;
                    if (openswmmvis::feature::regionCellsFromValue(value, &known)
                        == openswmmvis::feature::RegionCells::Triangles)
                        regionMode = mesh::QuadRegionMode::TrianglesOnly;
                    if (!known)
                        qWarning().noquote() << "[Mesh][quad] region feature" << f->GetFID()
                                             << "has cells =" << value
                                             << "— not auto or triangles; treated as auto";
                }
                auto readRing = [&](const OGRPolygon *poly) {
                    mesh::QuadRegion r;
                    const OGRLinearRing *ext = poly ? poly->getExteriorRing() : nullptr;
                    if (!ext || ext->getNumPoints() < 3) return;
                    const int n = ext->getNumPoints();
                    QVector<double> xs(n), ys(n);
                    for (int i = 0; i < n; ++i) { xs[i] = ext->getX(i); ys[i] = ext->getY(i); }
                    if (regionCT) nRegionXformFailed += transformChecked(regionCT, n, xs.data(), ys.data());
                    QVector<QPointF> pts;
                    pts.reserve(n);
                    for (int i = 0; i < n; ++i)
                        if (std::isfinite(xs[i]) && std::isfinite(ys[i])) pts.append(QPointF(xs[i], ys[i]));
                    if (pts.size() < 3) return;
                    r.ring = QPolygonF(trimByStraightness(pts, in.trimTurnDeg, in.trimDeviation, {}, true));
                    r.mode = regionMode;
                    for (const QString &name : sizeFields)
                        if (const int i = fieldIdx(name.toUtf8().constData());
                            i >= 0 && f->IsFieldSetAndNotNull(i))
                        { r.spacing = f->GetFieldAsDouble(i); break; }
                    if (const int i = fieldIdx(tagField.constData()); i >= 0 && f->IsFieldSetAndNotNull(i))
                        r.tag = QString::fromUtf8(f->GetFieldAsString(i));
                    if (!(r.spacing > 0.0) || !std::isfinite(r.spacing)) r.spacing = 0.0;
                    quadRegions.append(std::move(r));
                };
                if (const OGRGeometry *geom = f->GetGeometryRef())
                {
                    const OGRwkbGeometryType t = wkbFlatten(geom->getGeometryType());
                    if (t == wkbPolygon) readRing(geom->toPolygon());
                    else if (t == wkbMultiPolygon)
                        for (const auto *poly : *geom->toMultiPolygon()) readRing(poly);
                }
                OGRFeature::DestroyFeature(f);
            }
            GDALClose(ds);
            if (regionCT) OGRCoordinateTransformation::DestroyCT(regionCT);
        }
        if (nRegionXformFailed > 0)
            qWarning() << "[Mesh][quad]" << nRegionXformFailed << "region vertex(es) failed reprojection and were dropped";
        stageMark("quad region layer read");
    }
    const int nLayerQuadRegions = quadRegions.size();
    for (const mesh::QuadRegion &src : std::as_const(in.quadRegions))
    {
        mesh::QuadRegion r = src;
        r.ring = QPolygonF(trimByStraightness(src.ring, in.trimTurnDeg, in.trimDeviation, {}, true));
        quadRegions.append(std::move(r));
    }
    if (!quadRegions.isEmpty())
        qCInfo(lcMeshPerf).nospace()
            << "[Mesh][quad] " << quadRegions.size() << " regions ("
            << nLayerQuadRegions << " from layers, " << in.quadRegions.size()
            << " from subcatchments)";

    // Refinement floor (area of the equilateral triangle of the minimum
    // cell size); region area bounds may not go below it.
    const double areaFloor = in.minCellSize > 0.0
                                 ? 0.4330127018922193 * in.minCellSize * in.minCellSize
                                 : 0.0;
    for (const auto &rm : std::as_const(in.regionMarkers))
    {
        mesh::RegionMarker r = rm;
        if (areaFloor > 0.0 && r.maxArea > 0.0 && r.maxArea < areaFloor) r.maxArea = areaFloor;
        g.addRegion(r);
    }
    g.setOptions(in.genOpts);
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
    // thinning / grid sampling, and post-mesh vertex elevation fill.
    // A single DTMRaster instance covers all three so the file is only
    // opened once and the same bilinear sampler is used throughout.
    //
    // When no DTM is provided, vertex z is filled by inverse-distance
    // interpolation from junction rim elevations carried on the SWMM
    // node Steiner points (invert + maxDepth, set in collectInputs).
    const bool useDTM = !in.dtmPath.isEmpty();

    mesh::DTMRaster thinner;
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
    // The post-mesh elevation loop consults this first so those vertices
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
            // rejects and answers with a NaN z. That is
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
        // A rim lowers the ground to the node; it never raises it: where the
        // terrain is at or below the rim, the node keeps the terrain.
        if (wasPreset && useDTM && rimPinnedMarkers.contains(sp.marker))
        {
            double gx = sp.xy.x(), gy = sp.xy.y();
            if (!meshToDTM || meshToDTM->Transform(1, &gx, &gy)) {
                const double terrain = thinner.sampleAt(gx, gy) * in.zConversionFactor;
                if (std::isfinite(terrain) && terrain <= sp.z) sp.z = terrain;
            }
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

    // ── Step 2: terrain fidelity as a size (overhaul Stage 2) ────────
    // Domain bounding box in mesh CRS → transform corners to DTM CRS. No
    // terrain vertices are generated: the DEM's roughness bounds the size
    // field instead (mesh/terrainsizefield.h). Without a DTM, z is filled
    // later via IDW from seedXY/seedZ.
    double bx0 = std::numeric_limits<double>::max(),  by0 = bx0;
    double bx1 = std::numeric_limits<double>::lowest(), by1 = bx1;
    for (const auto &dom : std::as_const(in.domains))
        for (const auto &p : dom)
        {
            if (p.x() < bx0) bx0 = p.x(); if (p.x() > bx1) bx1 = p.x();
            if (p.y() < by0) by0 = p.y(); if (p.y() > by1) by1 = p.y();
        }

    // Size field inputs (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 2): the
    // cell size at features, the slope from the neighbour ratio, the floor
    // and the coarsening cap. Built after the DTM block so the terrain
    // term can be sampled.
    mesh::SizeFieldOptions sizeOptions;
    sizeOptions.nearSize  = in.cellSize;
    sizeOptions.gradation = std::max(in.sizeRatio - 1.0, 0.0);
    sizeOptions.areaFloor = areaFloor;
    sizeOptions.maxSize   = in.coarsenFactor > 1.0 ? in.cellSize * in.coarsenFactor : in.cellSize;
    // Terrain-error size term (Stage 2). Lives here so the size field built
    // after the DTM block can still sample it.
    mesh::TerrainSizeField terrainField;
    QSet<QPair<qint64,qint64>> missingTerrainVertices;
    const bool useAdaptiveTerrain = useDTM && in.terrainAdaptive
        && (in.terrainTolerance > 0.0 || in.terrainAutoTolerance);
    if (useAdaptiveTerrain)
    {
        stageClock.restart();
        // Stage T: re-runs on the same DEM and bbox reload the summaries.
        const QRectF referenceBox(bx0,by0,bx1-bx0,by1-by0);
        const QString indexFile = cache.isUsable()
            ? cache.terrainIndexPath(mesh::MeshStageCache::terrainIndexKey(
                  mesh::MeshStageCache::identityOf(in.dtmPath), in.meshCRSWkt, referenceBox, in.zConversionFactor))
            : QString();
        if (!terrainReference.open(in.dtmPath, in.meshCRSWkt, referenceBox,
                in.zConversionFactor,in.terrainCacheMiB,[&](double f) {
                    progress(30+int(5*f),QObject::tr("Indexing terrain for elevation-error refinement…"));
                    return !promise.isCanceled();
                }, indexFile)) {
            fail(QObject::tr("Terrain error index failed: %1").arg(terrainReference.errorMsg())); return;
        }
        if (terrainReference.indexLoaded()) qCInfo(lcMeshPerf) << "[Mesh][cache] terrain index HIT";
        else if (terrainReference.indexSaved()) { qCInfo(lcMeshPerf) << "[Mesh][cache] terrain index stored"; cache.prune(); }
        terrainReference.setCancellation([&] { return promise.isCanceled(); });
        if (in.terrainAutoTolerance) {
            // Quantized elevation data cannot justify sub-quantum precision.
            // The 0.1 m baseline is converted to the mesh's vertical unit.
            // Nor can detail below the DEM's own micro-relief: the median
            // residual of its 16-pixel planes floors the automatic value.
            const double microRelief = terrainReference.leafResidualQuantile(.5);
            in.terrainTolerance = std::max({.1/in.verticalUnitToSI,3.0*terrainReference.verticalQuantum(),
                                            std::isfinite(microRelief)?microRelief:0.0});
        }
        qCInfo(lcMeshPerf) << "[Mesh][terrain] leaf residual (16 px planes) P50" << terrainReference.leafResidualQuantile(.5)
                           << "P90" << terrainReference.leafResidualQuantile(.9) << "P99" << terrainReference.leafResidualQuantile(.99);
        qCInfo(lcMeshPerf) << "[Mesh][terrain] adaptive reference samples" << terrainReference.referenceSamples()
                         << "| summary bytes" << terrainReference.summaryBytes() << "| tolerance" << in.terrainTolerance;
        stageMark("terrain error index");
    }
    const bool useTerrainField = useDTM && in.terrainTolerance > 0.0;

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

        if (useTerrainField && (!useAdaptiveTerrain || in.terrainBreaklines))
        {
            progress(30, QObject::tr("Measuring terrain roughness…"));
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
            // DEM linear unit → mesh unit, from the transform's local scale
            // at the domain centre (1 when the CRSs match).
            double unitScale = 1.0;
            if (dtmToMesh)
            {
                const double cx = (dx0 + dx1) * 0.5, cy = (dy0 + dy1) * 0.5;
                const double step = thinner.pixelSize();
                double xs[2] = {cx, cx + step}, ys[2] = {cy, cy};
                if (dtmToMesh->Transform(2, xs, ys) && step > 0.0)
                {
                    const double d = std::hypot(xs[1] - xs[0], ys[1] - ys[0]);
                    if (std::isfinite(d) && d > 0.0) unitScale = d / step;
                }
            }
            mesh::TerrainSizeOptions tso;
            // The DEM's vertical unit: the z factor maps DEM z → mesh z.
            tso.tolerance = in.zConversionFactor > 0.0
                ? in.terrainTolerance / in.zConversionFactor : in.terrainTolerance;
            // Output cells near the size-field pitch (>= nearSize/2).
            const double pitchDem = sizeOptions.nearSize > 0.0 && unitScale > 0.0
                ? 0.5 * sizeOptions.nearSize / unitScale : 0.0;
            tso.outLevel = (pitchDem > 0.0 && thinner.pixelSize() > 0.0)
                ? qBound(1, int(std::floor(std::log2(pitchDem / thinner.pixelSize()))), 6) : 1;
            // Adaptive mode only needs the streaming geometry here; it does
            // not compute or impose the conservative block-size hierarchy.
            if (useAdaptiveTerrain) {
                tso.rowsOnly = true;
                tso.maxBandBytes = qint64(in.terrainCacheMiB)*1024*1024;
                // Breaklines are detected on a grid about one minimum cell
                // wide (block-averaged, from overviews when present): major
                // features only, and far less DEM to read (D-R4).
                if (in.minCellSize > 0.0 && unitScale > 0.0 && thinner.pixelSize() > 0.0)
                    tso.decimation = std::max(1, int(std::floor(in.minCellSize / (unitScale * thinner.pixelSize()))));
            }
            // Terrain break lines (Phase 6b §2.1) ride the same row pass:
            // one tolerance, one meaning — where the surface departs from a
            // plane by more than it, the mesh gets an edge.
            mesh::TerrainBreaklineExtractor breaklines;
            mesh::TerrainBreaklineOptions blo;
            blo.tolerance = tso.tolerance;
            blo.cacheMiB = in.terrainCacheMiB;
            // Major features only (MESH_REGIONAL_TRIQUAD_PLAN D-R4): the
            // generator keeps no line shorter than 4 x the minimum cell, so do
            // not trace them. On fine lidar this drops millions of chains. A
            // chain point advances at most sqrt(2) pixels, so this count never
            // drops a line the generator would keep.
            if (in.minCellSize > 0.0 && unitScale > 0.0 && thinner.pixelSize() > 0.0)
                blo.minPixels = std::max(blo.minPixels,
                    int(std::floor(4.0 * in.minCellSize / (std::sqrt(2.0) * unitScale * thinner.pixelSize() * tso.decimation))));
            blo.cancelled = [&promise] { return promise.isCanceled(); };
            if (in.terrainBreaklines) tso.rowSink = [&breaklines, &blo](const float *row, int r, int cols, int rows) {
                if (r == 0) breaklines.begin(cols, rows, blo);
                breaklines.pushRow(row);
            };
            // Stage L: adaptive mode streams the DEM only for break lines, so a
            // cached result for the same DEM window and parameters skips it.
            const bool cacheBreaklines = useAdaptiveTerrain && in.terrainBreaklines && cache.isUsable();
            const QByteArray breaklineKey = cacheBreaklines
                ? mesh::MeshStageCache::breaklineKey(mesh::MeshStageCache::identityOf(in.dtmPath), in.meshCRSWkt,
                      QRectF(QPointF(dx0,dy0),QPointF(dx1,dy1)), blo.tolerance, blo.lowRatio, blo.minPixels, blo.maxPixels,
                      tso.decimation)
                : QByteArray();
            mesh::MeshStageCache::Breaklines cachedLines;
            // Keep the most significant lines only, ranked on the terrain
            // reference after the cache (so the cap can change cheaply).
            auto rankLines = [&](const QVector<QVector<QPointF>> &all) {
                if (!useAdaptiveTerrain || in.maxTerrainBreaklines <= 0 || !(in.minCellSize > 0.0)) return all;
                double stats[3] = {};
                QElapsedTimer rankClock; rankClock.start();
                auto kept = mesh::rankBreaklinesByStep(all, [&](double x, double y) { return terrainReference.sampleAt(x, y); },
                                                       in.minCellSize, in.maxTerrainBreaklines, stats,
                                                       [&](const QVector<QRectF> &boxes) { terrainReference.prefetch(boxes); });
                qCInfo(lcMeshPerf).noquote() << QStringLiteral("[Mesh][terrain] break lines ranked: kept %1 of %2 (smallest integrated step %3) in %4 ms")
                    .arg(stats[1]).arg(stats[0]).arg(stats[2]).arg(rankClock.elapsed());
                return kept;
            };
            const bool breaklinesCached = cacheBreaklines && cache.loadBreaklines(breaklineKey, &cachedLines);
            if (breaklinesCached) {
                g.setTerrainBreaklines(rankLines(cachedLines.lines));
                sizeOptions.steps = g.previewTerrainBreaklines();
                qCInfo(lcMeshPerf) << "[Mesh][cache] terrain break lines HIT:" << cachedLines.lines.size()
                                   << "| median length (px)" << cachedLines.medianLength
                                   << "| dropped (reprojection)" << cachedLines.dropped
                                   << (cachedLines.skipped ? "| SKIPPED: DEM window over the pixel cap" : "");
                stageMark("terrain size field");
            } else {
            stageClock.restart();
            const bool built = terrainField.buildFromFile(
                in.dtmPath, 1, dx0, dy0, dx1, dy1, tso,
                [&promise](double f) -> bool {
                    promise.setProgressValueAndText(
                        30 + qBound(0, int(f * 6.0), 6),
                        QObject::tr("Measuring terrain roughness… %1%").arg(int(f * 100.0)));
                    return !promise.isCanceled();
                });
            stageMark("terrain size field");
            if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
            if (!built)
            {
                fail(QObject::tr("Terrain size field failed: %1").arg(terrainField.errorMsg()));
                return;
            }
            qCInfo(lcMeshPerf) << "[Mesh][terrain] size field" << terrainField.outCols()
                               << "x" << terrainField.outRows() << "| levels" << terrainField.levelsUsed()
                               << "| tolerance (DEM units)" << tso.tolerance << "| unit scale" << unitScale;
            {
                // Window pixels → DEM CRS → mesh CRS. A chain with any point
                // that fails reprojection is dropped whole (a gap would be
                // bridged by a straight segment).
                const QVector<QVector<QPointF>> chains = breaklines.finish();
                if (!breaklines.errorMsg().isEmpty()) { fail(breaklines.errorMsg()); return; }
                QVector<QVector<QPointF>> lines;
                lines.reserve(chains.size());
                qsizetype dropped = 0;
                for (const QVector<QPointF> &chain : chains)
                {
                    QVector<double> xs(chain.size()), ys(chain.size());
                    for (int k = 0; k < chain.size(); ++k)
                    {
                        const QPointF geo = terrainField.windowPixelToGeo(chain[k].x(), chain[k].y());
                        xs[k] = geo.x(); ys[k] = geo.y();
                    }
                    if (transformChecked(dtmToMesh, xs.size(), xs.data(), ys.data()) > 0) { ++dropped; continue; }
                    QVector<QPointF> line(chain.size());
                    for (int k = 0; k < chain.size(); ++k) line[k] = QPointF(xs[k], ys[k]);
                    lines.append(std::move(line));
                }
                g.setTerrainBreaklines(rankLines(lines));
                // The lines the generator will keep become mesh edges: the size
                // field must not refine around their steps
                // (MESH_TRIANGLE_ENGINE_PLAN D13). Lines it drops keep theirs.
                sizeOptions.steps = g.previewTerrainBreaklines();
                QVector<int> lengths;
                lengths.reserve(chains.size());
                for (const auto &c : chains) lengths.append(int(c.size()));
                std::sort(lengths.begin(), lengths.end());
                const int medianLength = lengths.isEmpty() ? 0 : lengths[lengths.size() / 2];
                qCInfo(lcMeshPerf) << "[Mesh][terrain] break lines extracted" << lines.size()
                                   << "| median length (px)" << medianLength
                                   << "| dropped (reprojection)" << dropped
                                   << (breaklines.skipped() ? "| SKIPPED: DEM window over the pixel cap" : "");
                if (cacheBreaklines && !promise.isCanceled()
                    && cache.storeBreaklines(breaklineKey, {lines, medianLength, dropped, breaklines.skipped()}))
                    qCInfo(lcMeshPerf) << "[Mesh][cache] terrain break lines stored";
                // Many short chains = the tolerance is inside the DEM's noise
                // (SRTM stores whole metres: anything under ~3 m traces noise).
                if (lines.size() > 500 && medianLength < 10)
                    qWarning() << "[Mesh][terrain]" << lines.size() << "short break lines (median"
                               << medianLength << "px): the terrain tolerance" << in.terrainTolerance
                               << "looks smaller than the DEM's noise — the mesh will follow noise.";
            }
            }
            if (!useAdaptiveTerrain) sizeOptions.terrainSizeAt = [&terrainField, meshToDTM, unitScale](double x, double y) {
                double gx = x, gy = y;
                if (meshToDTM && !meshToDTM->Transform(1, &gx, &gy)) return 0.0;
                return terrainField.sizeAtGeo(gx, gy) * unitScale;
            };
        }
    }
    else if (useDTM)
    {
        qCDebug(lcMeshPerf) << "[Mesh] domain bbox invalid — skipping the terrain size field";
    }

    // ── Generate ────────────────────────────────────────────────────
    progress(40, QObject::tr("Generating mesh…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    // Graded size field (V2 plan Track B).  Keeps the uniform cap AT the
    // constrained features and lets the permitted area grow with distance at
    // the user's Lipschitz slope — strictly fewer cells than the uniform cap,
    // with the slope itself the smooth-transition guarantee.  Built from the
    // CONDITIONED geometry so the field grades away from what the mesher will
    // actually see.  Must outlive generate(): the hook samples it per
    // candidate triangle.
    mesh::SizeField sizeField;
    bool useGrading = false;
    if (sizeOptions.nearSize > 0.0 && sizeOptions.gradation > 0.0)
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

        useGrading = sizeField.build(bbox,
                                     in.refineAtFeatures ? in.constraintSegs : QVector<mesh::ConstraintSegment>{},
                                     in.refineAtFeatures ? ringSeeds : QVector<QVector<QPointF>>{},
                                     in.refineAtFeatures ? in.steinerPoints : QVector<mesh::SteinerPoint>{}, sizeOptions);
        if (useGrading)
        {
            qCInfo(lcMeshPerf) << "[Mesh][grading] size field"
                               << sizeField.cols() << "x" << sizeField.rows()
                               << "at pitch" << sizeField.pitch()
                               << "| near size" << sizeOptions.nearSize
                               << "| gradation" << sizeOptions.gradation
                               << "| max size" << sizeOptions.maxSize
                               << "| terrain" << (useTerrainField ? "on" : "off");
        }
        else
            qWarning() << "[Mesh][grading] size field could not be built "
                          "(no seed features?) — falling back to the uniform "
                          "area cap.";
    }
    // Cancellation and progress ride on the hook; the graded field is the
    // size function (it folds in the floor and the coarsening cap).
    {
        mesh::RefineHook hook;
        hook.isCancelled = [&promise] { return promise.isCanceled(); };
        hook.onProgress  = [&progress](qint64 cells) {
            progress(45, QObject::tr("Building cells… (%1 so far)").arg(cells));
        };
        if (useGrading)
        {
            hook.targetAreaAt = [&sizeField](double x, double y) {
                return sizeField.targetAreaAt(x, y);
            };
        }
        // Ground elevation for the street / ditch trough test (DEM units —
        // only compared with itself).
        if (useDTM)
            hook.elevationAt = [&thinner, meshToDTM](double x, double y) {
                double gx = x, gy = y;
                if (meshToDTM && !meshToDTM->Transform(1, &gx, &gy)) return std::numeric_limits<double>::quiet_NaN();
                return thinner.sampleAt(gx, gy);
            };
        if (useAdaptiveTerrain)
        {
            hook.terrainTolerance = in.terrainTolerance;
            // Adaptive terrain spends the cell budget worst error first.
            hook.terrainWorstFirst = useAdaptiveTerrain;
            // The final terrain verification below measures every cell of the
            // assembled mesh; a second exact pass inside refinement read the
            // whole DEM again for statistics it then overwrites.
            hook.terrainFinalCheck = !useAdaptiveTerrain;
            hook.terrainElevationAt = [&](double x,double y) {
                const auto key=keyOf(x,y);
                const double terrainZ=terrainReference.sampleAt(x,y);
                // Keep source coverage even when a rim value or the later
                // coverage fill supplies a finite model elevation here.
                if (!std::isfinite(terrainZ)) missingTerrainVertices.insert(key);
                const auto it=elevCache.constFind(key);
                if (it!=elevCache.constEnd()) return modelUnitKeys.contains(key)?*it:*it*in.zConversionFactor;
                // The flatten radius lowers terrain to a nearby rim, never raises it.
                const double flat=flattenZ(x,y);
                return std::isfinite(flat) && (!std::isfinite(terrainZ) || terrainZ>flat)?flat:terrainZ;
            };
            hook.terrainError = [&](const QPointF *xy,const double *z,QPointF *out) {
                auto q=useAdaptiveTerrain?terrainReference.queryWorst(xy,z,in.terrainTolerance):mesh::TerrainErrorField::Query{};
                double error=q.valid?q.maxError:0;
                *out=q.point;
                return error;
            };
        }
        g.setRefineHook(hook);
    }

    stageClock.restart();
    mesh::MeshResult result = g.generate();
    mesh::GenerationStats generationStats = g.stats();
    stageMark("generate()");
    // Open-area quad blocks (MESH_REGIONAL_TRIQUAD_PLAN phases 2-3): placed
    // where the cells just generated are nearly uniform and no feature runs,
    // then embedded as mapped quad regions in a second pass. The generator
    // checks every block; one that does not fit stays triangles, and a failed
    // second pass keeps the triangle mesh.
    int openBlocks = 0, openBlockQuads = 0;
    if (result.ok && in.quadMode == 2 && !promise.isCanceled()) {
        progress(70, QObject::tr("Placing open-area quad blocks…"));
        QVector<QVector<QPointF>> blockers;
        for (const auto &ring : std::as_const(in.domains)) { QVector<QPointF> r(ring.begin(), ring.end()); if (!r.isEmpty()) r.append(r.first()); blockers.append(r); }
        for (const auto &ring : std::as_const(bprep.holeRings)) { QVector<QPointF> r(ring); if (!r.isEmpty()) r.append(r.first()); blockers.append(r); }
        for (const auto &cs : std::as_const(in.constraintSegs)) blockers.append(cs.path);
        for (const auto &line : g.acceptedTerrainBreaklines()) blockers.append(line);
        for (const auto &sp : std::as_const(in.steinerPoints)) blockers.append({sp.xy, sp.xy});
        QRectF extent;
        for (const auto &ring : std::as_const(in.domains)) extent = extent.united(ring.boundingRect());
        const double minCell = in.minCellSize > 0.0 ? in.minCellSize : 0.25 * in.cellSize;
        const double pitch = std::max(4.0 * minCell, std::max(extent.width(), extent.height()) / 3000.0);
        const mesh::QuadBlockGrid grid = mesh::quadBlockGridFromMesh(result, pitch, blockers);
        mesh::QuadBlockOptions blockOptions;
        blockOptions.minSpacing = 2.0 * minCell;
        const auto blocks = mesh::placeQuadBlocks(grid, blockOptions);
        stageMark("open quad block placement");
        if (!blocks.isEmpty()) {
            const int quadsBefore = result.quadCount();
            for (const auto &b : blocks) g.addQuadRegion(b);
            mesh::MeshResult withBlocks = g.generate();
            stageMark("generate() with open blocks");
            if (withBlocks.ok && !promise.isCanceled()) {
                result = std::move(withBlocks);
                generationStats = g.stats();
                openBlockQuads = result.quadCount() - quadsBefore;
                for (int i = quadRegions.size(); i < g.quadRegionReports().size(); ++i)
                    openBlocks += g.quadRegionReports()[i].resolved == mesh::QuadRegionMode::Mapped;
            } else if (!withBlocks.ok) {
                qCWarning(lcMeshPerf).noquote() << "[Mesh][quad] open blocks dropped:" << withBlocks.errorMsg;
            }
        }
        qCInfo(lcMeshPerf).noquote() << QStringLiteral("[Mesh][quad] open blocks: %1 placed, %2 embedded, %3 quads (grid %4 x %5 at %6)")
            .arg(blocks.size()).arg(openBlocks).arg(openBlockQuads).arg(grid.cols).arg(grid.rows).arg(pitch);
    }
    if (!g.acceptedTerrainBreaklines().isEmpty())
        qCInfo(lcMeshPerf) << "[Mesh][terrain] break lines kept as mesh edges"
                           << g.acceptedTerrainBreaklines().size();
    {
        const mesh::GenerationStats &st = g.stats();
        qCInfo(lcMeshPerf) << "[Mesh] quad strips: regions" << st.regionPatches << "| conduits" << st.conduitStrips
                           << "| streets/ditches" << st.breaklineStrips << "| dropped" << st.stripsDropped
                           << "| refinement added" << st.refineInserted << "vertices"
                           << "| triangles under the angle bound (strip edges, close inputs)" << st.trianglesBelowAngle;
        if (st.refineCapped)
            qWarning() << "[Mesh] refinement hit its safety cap — some triangles miss the size or angle bound.";
        qCInfo(lcMeshPerf) << "[Mesh][refinement] size" << st.sizeInserted << "| quality" << st.qualityInserted
                         << "| terrain" << st.terrainInserted << "| segment splits" << st.segmentSplits;
    }
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
        fail(QObject::tr("Mesh generation: %1").arg(result.errorMsg)); return;
    }

    // ── Quad region reports ──────────────────────────────────────────
    QStringList alignmentWarnings;
    for (const mesh::QuadRegionReport &rep : g.quadRegionReports())
    {
        if (rep.index >= quadRegions.size()) break;   // open blocks are summarised above
        qCInfo(lcMeshPerf).nospace() << "[Mesh][quad] region " << rep.index
                                     << (rep.accepted ? " accepted" : " skipped")
                                     << (rep.resolved == mesh::QuadRegionMode::Mapped ? " | quads" : " | triangles")
                                     << " | h " << rep.spacing
                                     << (rep.message.isEmpty() ? QString() : QStringLiteral(" | ") + rep.message);
        if (!rep.accepted)
            qWarning() << "[Mesh] Skipped quad region" << rep.index << "—" << rep.message;
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
    // have their exact z in elevCache.  Only mesher-inserted refinement
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

    QVector<bool> zInModelUnits(result.vertices.size(), useAdaptiveTerrain);

    if (useDTM && !useAdaptiveTerrain)
    {
        const int nv = result.vertices.size();

        // Pass 1 — resolve elevCache hits (PSLG Steiner vertices); collect
        // the misses (mesher-inserted refinement vertices) for batching.
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
                // Only where the terrain stands above the rim (model units).
                if (std::isfinite(fz) && (!std::isfinite(zs[k]) || zs[k] * in.zConversionFactor > fz))
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
    else if (!useDTM)
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
    // Mesher-inserted refinement vertices are re-sampled from the raster in
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
            // (checked against the generated vertex count at copy-out), but
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

    if (useAdaptiveTerrain)
    {
        stageClock.restart();
        generationStats.terrainUnresolved=0;
        generationStats.terrainUnknown=0;
        generationStats.maxTerrainError=0;
        QStringList exceptions;
        int elevationOffsetOnly=0;
        // Cells are in Hilbert order: read each chunk's DEM tiles in parallel
        // first, so the exact checks below hit the cache instead of reading
        // the raster tile by tile (USB-attached DEMs were I/O bound).
        constexpr int kVerifyChunk=8192;
        for (int i=0;i<result.triangles.size();++i) {
            if ((i % kVerifyChunk)==0) {
                if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
                progress(85,QObject::tr("Checking final terrain accuracy…"));
                QVector<QRectF> boxes;
                boxes.reserve(kVerifyChunk);
                for (int j=i;j<std::min<qsizetype>(i+kVerifyChunk,result.triangles.size());++j) {
                    const auto &c=result.triangles[j];
                    QRectF b(result.vertices[c.v0].xy,QSizeF(0,0));
                    for (int k=1;k<c.vertexCount();++k) {
                        const QPointF &q=result.vertices[c.vertex(k)].xy;
                        b.setLeft(std::min(b.left(),q.x())); b.setRight(std::max(b.right(),q.x()));
                        b.setTop(std::min(b.top(),q.y())); b.setBottom(std::max(b.bottom(),q.y()));
                    }
                    boxes.append(b);
                }
                terrainReference.prefetch(boxes);
            }
            const auto &cell=result.triangles[i];
            // Same 0-2 diagonal used by mesh profile interpolation for quads.
            bool bad=false,unknown=false;
            bool referenceFails=false;
            double cellError=0; QPointF worstXY;
            for (int half=0;half<(cell.isQuad()?2:1);++half) {
                const int ids[3]={cell.v0,half?cell.v2:cell.v1,half?cell.v3:cell.v2};
                QPointF xy[3]; double z[3];
                for(int k=0;k<3;++k) {
                    xy[k]=result.vertices[ids[k]].xy; z[k]=result.vertices[ids[k]].z;
                    if (!missingTerrainVertices.isEmpty() && missingTerrainVertices.contains(keyOf(xy[k].x(),xy[k].y()))) unknown=true;
                }
                const auto q=terrainReference.queryTriangle(xy,z,in.terrainTolerance);
                unknown=unknown || !q.valid || q.noDataSamples>0;
                bad=bad || q.maxError>in.terrainTolerance;
                if (q.maxError>cellError) { cellError=q.maxError; worstXY=q.point; }
                if (q.maxError>in.terrainTolerance) {
                    double sourceZ[3];
                    for(int k=0;k<3;++k) sourceZ[k]=terrainReference.sampleAt(xy[k].x(),xy[k].y());
                    const auto source=terrainReference.queryTriangle(xy,sourceZ,in.terrainTolerance);
                    referenceFails=referenceFails || !source.valid || source.maxError>in.terrainTolerance;
                }
                generationStats.maxTerrainError=std::max(generationStats.maxTerrainError,q.upperBound);
            }
            if(bad) ++generationStats.terrainUnresolved;
            if(bad && !referenceFails && !unknown) ++elevationOffsetOnly;
            if(unknown) ++generationStats.terrainUnknown;
            if ((bad || unknown) && exceptions.size()<20) {
                if (!bad) worstXY=(result.vertices[cell.v0].xy+result.vertices[cell.v1].xy+result.vertices[cell.v2].xy)/3;
                exceptions << QObject::tr("Cell %1 near (%2, %3): %4; measured error %5.")
                    .arg(i+1).arg(worstXY.x(),0,'g',12).arg(worstXY.y(),0,'g',12)
                    .arg(unknown?QObject::tr("missing terrain coverage"):
                        !referenceFails?QObject::tr("model elevation offset; source-only triangle passes"):
                        QObject::tr("terrain resolution conflict"))
                    .arg(cellError,0,'g',6);
            }
        }
        if (!terrainReference.errorMsg().isEmpty()) { fail(terrainReference.errorMsg()); return; }
        if(generationStats.terrainUnresolved || generationStats.terrainUnknown)
            alignmentWarnings << QObject::tr("Terrain tolerance %1: %2 cells exceed it and %3 could not be verified. "
                "Prescribed elevations, fixed strips or minimum spacing can prevent further refinement. "
                "%5 exceedances disappear when model elevation overrides are removed from the check. "
                "The maximum checked error bound is %4. This mesh does not certify the requested terrain tolerance.")
                .arg(in.terrainTolerance).arg(generationStats.terrainUnresolved).arg(generationStats.terrainUnknown)
                .arg(generationStats.maxTerrainError).arg(elevationOffsetOnly);
        if (!exceptions.isEmpty()) alignmentWarnings << QObject::tr("First terrain exceptions (mesh coordinates):\n%1").arg(exceptions.join('\n'));
        qCInfo(lcMeshPerf) << "[Mesh][terrain] final tolerance" << in.terrainTolerance
                         << "| error upper bound" << generationStats.maxTerrainError
                         << "| unresolved cells" << generationStats.terrainUnresolved
                         << "| unknown cells" << generationStats.terrainUnknown;
        stageMark("final terrain verification");
    }
    if (generationStats.refineCapped)
        alignmentWarnings << QObject::tr("Refinement reached its resource limit. The mesh may not meet the requested size, angle or terrain tolerance. Increase the cell budget or relax the resolution.");

    qCInfo(lcMeshPerf).nospace()
        << "[Mesh] cells: " << (result.triangles.size() - result.quadCount())
        << " triangles + " << result.quadCount() << " quads";

    // Keep generation in memory. Project Save owns all final INP/2DM writes;
    // closing or discarding the project must leave its saved files unchanged.
    progress(85, QObject::tr("Preparing generated mesh for review…"));
    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }

    // Transient writer coupling maps must live on the pending layer too.
    // The mapper may already have produced the same cell/node row.
    QSet<QPair<int, QString>> cellNodes;
    for (const auto &row : std::as_const(result.cellCouplings))
        cellNodes.insert({row.tri, row.nodeId});
    for (auto it = coupling.triangleToNode.cbegin(); it != coupling.triangleToNode.cend(); ++it) {
        if (it.key() < 0 || it.key() >= result.triangles.size() || it.value().isEmpty()) continue;
        const auto key = qMakePair(it.key(), it.value());
        if (cellNodes.contains(key)) continue;
        result.cellCouplings.append({it.key(), it.value()});
        cellNodes.insert(key);
    }
    coupling.triangleToNode.clear();
    for (int i = 0; i < result.triangles.size(); ++i) {
        auto &cell = result.triangles[i];
        if (!std::isfinite(cell.mannings)) cell.mannings = coupling.triangleMannings.value(i, in.manningsN);
        if (!std::isfinite(cell.initDepth)) cell.initDepth = in.initDepth;
    }

    QString intendedMeshPath;
    if (in.outputMode == mesh::MeshOutputMode::External) {
        const QFileInfo model(in.inpPath);
        intendedMeshPath = in.meshOutputPath.isEmpty()
            ? model.absoluteDir().filePath(model.completeBaseName() + QStringLiteral(".2dm"))
            : QFileInfo(in.meshOutputPath).absoluteFilePath();
    }

    if (promise.isCanceled()) { fail(QObject::tr("Cancelled.")); return; }
    QString corridorError;
    if (!mesh::corridorSourceFilesUnchanged(corridors.sourceStamps, &corridorError)) {
        fail(corridorError);
        return;
    }
    progress(95, QObject::tr("Done — adding layer…"));

    PResult out;
    out.ok         = true;
    out.meshResult = std::move(result);
    out.generationStats = generationStats;
    out.terrainToleranceUsed = useAdaptiveTerrain ? in.terrainTolerance : 0.0;
    out.verticalUnitToSI = in.verticalUnitToSI;
    out.coupling   = std::move(coupling);
    out.meshPath   = intendedMeshPath;
    out.outputMode = in.outputMode;
    out.corridorSources = std::move(corridors.resolvedSources);
    out.corridorSourceStamps = std::move(corridors.sourceStamps);
    out.meshUnitsSI = mesh::unitsHeaderIsSI(in.meshLinearUnitName);
    out.alignmentWarnings = std::move(alignmentWarnings);
    promise.addResult(std::move(out));
}

// QtConcurrent stores an exception thrown by the worker into the future and
// rethrows it on the GUI thread at result() — uncaught, that terminates the
// application with no message. On Windows a large DEM can drive an allocation
// past the COMMIT limit (physical memory still ~50%), so std::bad_alloc here
// was killing the app "without warning" mid-thinning. Convert every exception
// into an ordinary failed PipelineResult; the dialog's progress label still
// names the stage that was running.
void
MeshGenerationDialog::runMeshPipeline(QPromise<MeshGenerationDialog::PipelineResult> &promise,
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
            "spacing, increase the terrain tolerance, or reduce the domain "
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
    m_dtmCombo->setObjectName(QStringLiteral("meshDtmCombo"));
    m_meshVertCRSCombo->setObjectName(QStringLiteral("meshVerticalUnitsCombo"));
    m_zFactorSpin->setObjectName(QStringLiteral("meshZFactorSpin"));
    m_includeConduits->setObjectName(QStringLiteral("meshConduitsBox"));
    m_includeSubcatch->setObjectName(QStringLiteral("meshSubcatchBox"));
    m_mapNodesAfterGen->setObjectName(QStringLiteral("meshMapNodesBox"));
    m_nodeFlattenSpin->setObjectName(QStringLiteral("meshFlattenSpin"));
    m_elevMethodCombo->setObjectName(QStringLiteral("meshElevationMethodCombo"));
    m_nnVariantCombo->setObjectName(QStringLiteral("meshNNVariantCombo"));
    m_idwPowerSpin->setObjectName(QStringLiteral("meshIdwPowerSpin"));
    m_coarsenSpin->setObjectName(QStringLiteral("meshCoarsenSpin"));
    m_minCellSizeSpin->setObjectName(QStringLiteral("meshMinCellSizeSpin"));
    m_quadRegionSubcatchEdit->setObjectName(QStringLiteral("meshRegionNamesEdit"));
    m_trimTurnSpin->setObjectName(QStringLiteral("meshTrimTurnSpin"));
    m_trimDeviationSpin->setObjectName(QStringLiteral("meshTrimDeviationSpin"));
    m_manningsValueSpin->setObjectName(QStringLiteral("meshManningsSpin"));
    m_initDepthSpin->setObjectName(QStringLiteral("meshInitialDepthSpin"));
    m_outputExternal->setObjectName(QStringLiteral("meshExternalBox"));
    m_outputInline->setObjectName(QStringLiteral("meshInlineBox"));
    m_meshPathEdit->setObjectName(QStringLiteral("meshOutputPathEdit"));
    m_cellSizeSpin->setProperty("meshDistance",true);
    m_minCellSizeSpin->setProperty("meshDistance",true);
    m_terrainTolSpin->setProperty("meshDistance",true);
    m_conduitStripSpin->setProperty("meshDistance",true);
    m_nodeFlattenSpin->setProperty("meshDistance",true);
    m_nodeMinSepSpin->setProperty("meshDistance",true);
    m_trimDeviationSpin->setProperty("meshDistance",true);
    m_initDepthSpin->setProperty("meshDistance",true);
    m_dtmCombo->setProperty("meshLayerPicker",true);
    m_boundaryLayerCombo->setProperty("meshLayerPicker",true);
    m_quadRegionLayerCombo->setProperty("meshLayerPicker",true);
    seedDefaults();
    restoreOptions();

    // Keep suffix labels and defaults in sync if the user somehow changes
    // flow units while the dialog is open.
    connect(UnitSystem::instance(), &UnitSystem::unitsChanged,
            this, [this]() { updateUnitDisplay(); });
}

MeshGenerationDialog::~MeshGenerationDialog()
{
    if (m_watcher && m_watcher->isRunning())
        m_watcher->cancel();
    clearGenerationGuard();
}

void MeshGenerationDialog::clearGenerationGuard()
{
    for (const auto &connection : std::as_const(m_generationConnections))
        disconnect(connection);
    m_generationConnections.clear();
    m_generationModel.clear();
    m_generationEngine = nullptr;
    m_generationModelPath.clear();
}

void MeshGenerationDialog::beginGenerationGuard()
{
    clearGenerationGuard();
    m_generationInvalidated = false;
    if (!m_pw || !m_pw->modelLayer()) return;
    m_generationModel = m_pw->modelLayer();
    m_generationEngine = m_generationModel->engine();
    m_generationModelPath = m_generationModel->modelFilePath();
    m_generationRevision = m_generationModel->editRevision();
    const auto invalidate = [this]() { m_generationInvalidated = true; };
    m_generationConnections << connect(m_pw.data(), &SWMMVisProjectWindow::aboutToClose,
        this, [this]() {
            m_generationInvalidated = true;
            if (m_watcher) m_watcher->cancel();
        });
    auto *model = m_generationModel.data();
    m_generationConnections << connect(model, &SWMMModelLayer::modelEdited, this, invalidate)
        << connect(model, &SWMMModelLayer::attributeChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::geometryChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::optionsChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::dataObjectsChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::hydrographChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::controlRulesChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::transectChanged, this, invalidate)
        << connect(model, &SWMMModelLayer::modelFilePathChanged, this, invalidate);
    if (auto *canvas = m_pw->canvas()) {
        m_generationConnections << connect(canvas, &MapCanvas::layerAdded, this, invalidate)
                                << connect(canvas, &MapCanvas::layerRemoved, this, invalidate);
        for (auto *layer : canvas->layers()) {
            m_generationConnections << connect(layer, &OpenSWMMVisLayer::srsChanged, this, invalidate)
                                    << connect(layer, &OpenSWMMVisLayer::extentChanged, this, invalidate);
            if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer))
                m_generationConnections << connect(mesh, &SWMM2DMeshLayer::attributeChanged, this, invalidate)
                                        << connect(mesh, &SWMM2DMeshLayer::meshEditsChanged, this, invalidate)
                                        << connect(mesh, &SWMM2DMeshLayer::activeMeshChanged, this, invalidate);
            if (auto *features = qobject_cast<FeatureLayer *>(layer))
                m_generationConnections << connect(features, &FeatureLayer::featuresChanged, this, invalidate)
                                        << connect(features, &FeatureLayer::schemaChanged, this, invalidate);
            if (auto *vector = qobject_cast<GISVectorLayer *>(layer))
                m_generationConnections << connect(vector, &GISVectorLayer::filePathChanged, this, invalidate)
                                        << connect(vector, &GISVectorLayer::layerNameChanged, this, invalidate);
        }
    }
}

bool MeshGenerationDialog::generationOwnerIsCurrent() const
{
    return !m_generationInvalidated && m_pw && !m_pw->isClosing() && m_generationModel
        && m_pw->modelLayer() == m_generationModel
        && m_generationModel->engine() == m_generationEngine
        && m_generationEngine && m_generationModel->modelFilePath() == m_generationModelPath
        && m_generationModel->editRevision() == m_generationRevision;
}

void MeshGenerationDialog::reject()
{
    saveOptions();
    m_generationInvalidated = true;
    if (m_watcher) m_watcher->cancel();
    QDialog::reject();
}

void MeshGenerationDialog::closeEvent(QCloseEvent *event)
{
    saveOptions();
    m_generationInvalidated = true;
    if (m_watcher) m_watcher->cancel();
    QDialog::closeEvent(event);
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
        boundaryRow->addWidget(new QLabel(tr("Boundary polygon:"), g));
        m_boundaryLayerCombo = new QComboBox(g);
        m_boundaryLayerCombo->setObjectName(QStringLiteral("meshBoundaryLayerCombo"));
        m_boundaryLayerCombo->setToolTip(tr(
            "Polygon layer whose features define the meshing boundary.  "
            "Interior rings (holes) in those polygons are respected — "
            "The mesher leaves those regions unmeshed.  When (none), the mesh "
            "domain falls back to the SWMM model bounding rectangle + 5%."));
        boundaryRow->addWidget(m_boundaryLayerCombo, 1);
        lay->addLayout(boundaryRow);
        m_conditionBoundaryBox = new QCheckBox(tr("Simplify footprints to the minimum cell size"), g);
        m_conditionBoundaryBox->setObjectName(QStringLiteral("meshConditionBoundaryBox"));
        m_conditionBoundaryBox->setChecked(true);
        m_conditionBoundaryBox->setToolTip(tr(
            "Before meshing, fill holes narrower than the minimum cell size, cut back building "
            "detail thinner than it, close gaps narrower than it between buildings or the boundary, "
            "and drop vertices that add no shape. Corners stay square. Prevents tiny cells that "
            "slow the simulation; the log reports what changed."));
        lay->addWidget(m_conditionBoundaryBox);

        lay->addWidget(new QLabel(tr("Constraining points (check to include):"), g));
        m_pointLayersList = new QListWidget(g);
        m_pointLayersList->setObjectName(QStringLiteral("meshPointLayersList"));
        m_pointLayersList->setToolTip(tr("Every feature in each checked layer is added as a Steiner point."));
        m_pointLayersList->setMaximumHeight(100);
        m_pointLayersList->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(m_pointLayersList);

        lay->addWidget(new QLabel(tr("Constraining lines (check to include):"), g));
        m_lineLayersList = new QListWidget(g);
        m_lineLayersList->setObjectName(QStringLiteral("meshLineLayersList"));
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

        m_includeJunctions = new QCheckBox(tr("Nodes (junctions, inlets, outfalls, storage, dividers)  →  Steiner vertices  (tag = node id)"), g);
        m_includeJunctions->setObjectName(QStringLiteral("meshNodesAsVerticesBox"));
        m_includeJunctions->setToolTip(tr(
            "Checked (default): force a mesh vertex at every node location —\n"
            "every node type except virtual junctions, which are split points\n"
            "on a conduit with no rim of their own. Node clusters that are\n"
            "close only for non-physical reasons (weir / orifice / pump\n"
            "endpoints) would force very small cells around them; the\n"
            "minimum node separation below demotes those to cell coupling.\n\n"
            "Uncheck to let mesh quality drive the cell sizes; nodes are\n"
            "coupled to the mesh afterwards (coincident → vertex, otherwise →\n"
            "containing cell)."));
        m_includeConduits  = new QCheckBox(tr("Conduits  →  constraint segments  (marker = conduit id)"), g);
        m_includeSubcatch  = new QCheckBox(tr("Subcatchments  →  triangle regions  (tag = subcatchment id)"), g);

        lay->addWidget(m_includeJunctions);

        // Node elevation source: interpolate to terrain (default) vs rim.
        m_nodesUseRim = new QCheckBox(
            tr("Use node rim elevation (invert + max depth) instead of terrain"), g);
        m_nodesUseRim->setObjectName(QStringLiteral("meshNodesUseRimBox"));
        m_nodesUseRim->setToolTip(tr(
            "Checked (default): where the terrain is higher than a node's rim\n"
            "(invert + maximum depth from the SWMM model), the node vertex is\n"
            "lowered to the rim; where the terrain is at or below the rim, the\n"
            "terrain is kept. The rim never raises the ground.\n"
            "Unchecked: node vertices are interpolated from the DTM, like\n"
            "every other vertex.\n\n"
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
            "Radius around each rim node within which terrain higher than the\n"
            "node's rim is lowered to the rim (lower terrain is kept).  Prevents\n"
            "unnecessarily small triangles where the terrain and rim elevations\n"
            "disagree.  0 = off.  Applies only when nodes use rim elevation."));
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
        m_nodeMinSepBox->setObjectName(QStringLiteral("meshMinNodeSepBox"));
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
        m_nodeMinSepSpin->setObjectName(QStringLiteral("meshMinNodeSepSpin"));
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
    // Tab 2 — Quality (MESH_OVERHAUL_PLAN_2026-09-29.md §3,
    // MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5): Resolution (size field),
    // Shape (minimum angle, where quads go: streets, conduits, regions,
    // corridors) and Boundaries (straightness trimming).
    // ================================================================
    auto *qualityPage = new QWidget;
    auto *qualityVBox = new QVBoxLayout(qualityPage);
    qualityVBox->setContentsMargins(8, 8, 8, 8);

    // Resolution
    {
        auto *g = new QGroupBox(tr("Resolution"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_cellSizeSpin = new QDoubleSpinBox(g);
        m_cellSizeSpin->setObjectName(QStringLiteral("meshCellSizeSpin"));
        m_cellSizeSpin->setRange(0.0, 1e9);
        m_cellSizeSpin->setDecimals(3);
        m_cellSizeSpin->setSpecialValueText(tr("(from extent)"));
        m_cellSizeSpin->setToolTip(tr(
            "Target cell edge length at features (conduits, nodes, boundaries, "
            "breaklines, region rings). 0 derives one from the model extent."));
        f->addRow(tr("Cell si&ze at features:"), m_cellSizeSpin);

        m_coarsenSpin = new QDoubleSpinBox(g);
        m_coarsenSpin->setRange(1.0, 1000.0);
        m_coarsenSpin->setDecimals(1);
        m_coarsenSpin->setSingleStep(0.5);
        m_coarsenSpin->setPrefix(QStringLiteral("× "));
        m_coarsenSpin->setToolTip(tr(
            "The largest cell allowed, as a multiple of the cell size. It applies only "
            "where nothing else needs smaller cells: footprints, conduits, break lines, "
            "the minimum angle and terrain error all ask for smaller cells near them. In "
            "dense urban areas they leave little room, so this mostly shows in open "
            "areas (water, parks, large lots). 1 = uniform mesh."));
        {
            auto *row = new QWidget(g);
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(m_coarsenSpin);
            m_coarsenLengthLabel = new QLabel(row);
            m_coarsenLengthLabel->setObjectName(QStringLiteral("coarsenLengthLabel"));
            h->addWidget(m_coarsenLengthLabel);
            h->addStretch(1);
            f->addRow(tr("&Largest cell size:"), row);
            const auto updateLength = [this] {
                const double cell = m_cellSizeSpin->value();
                m_coarsenLengthLabel->setText(cell > 0.0
                    ? tr("= %1%2").arg(QLocale().toString(cell * m_coarsenSpin->value(), 'f', 0), m_cellSizeSpin->suffix())
                    : tr("(of the cell size from the extent)"));
            };
            connect(m_coarsenSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, updateLength);
            connect(m_cellSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, updateLength);
            updateLength();
        }

        m_sizeRatioSpin = new QDoubleSpinBox(g);
        m_sizeRatioSpin->setObjectName(QStringLiteral("meshSizeRatioSpin"));
        m_sizeRatioSpin->setRange(1.05, 2.0);
        m_sizeRatioSpin->setDecimals(2);
        m_sizeRatioSpin->setSingleStep(0.05);
        m_sizeRatioSpin->setToolTip(tr(
            "Requested growth rate for the background size field. Smaller values give smoother transitions and more cells. "
            "Fixed geometry and terrain refinement can produce larger ratios in the final mesh."));
        f->addRow(tr("Size &ratio between neighbours:"), m_sizeRatioSpin);

        m_minCellSizeSpin = new QDoubleSpinBox(g);
        m_minCellSizeSpin->setRange(0.0, 1e9);
        m_minCellSizeSpin->setDecimals(3);
        m_minCellSizeSpin->setSpecialValueText(tr("(cell size / 4)"));
        m_minCellSizeSpin->setToolTip(tr(
            "Minimum requested spacing. Constrained boundaries and triangle quality can create smaller cells. "
            "This also limits terrain refinement; conflicting terrain errors are reported."));
        f->addRow(tr("&Minimum cell size:"), m_minCellSizeSpin);

        m_terrainTolSpin = new QDoubleSpinBox(g);
        m_terrainTolSpin->setObjectName(QStringLiteral("meshTerrainTolSpin"));
        m_terrainTolSpin->setRange(0.0, 1e6);
        m_terrainTolSpin->setDecimals(3);
        m_terrainTolSpin->setSpecialValueText(tr("(automatic from DEM)"));
        m_terrainTolSpin->setToolTip(tr(
            "The largest vertical difference allowed between a triangle's flat surface and the "
            "terrain reference below it, in elevation units. Refinement splits triangles until "
            "every point of the reference inside them is within this. Measured against the DEM "
            "averaged to the minimum cell size (see Terrain reference), features narrower than a "
            "cell — curbs, walls, pixel noise — do not force refinement. 0 selects it from the DEM "
            "(0.1 m, three elevation increments for quantized terrain, or the DEM's own "
            "micro-relief); the resolved value is reported during generation."));
        m_terrainModeCombo = new QComboBox(g);
        m_terrainModeCombo->setObjectName(QStringLiteral("meshTerrainModeCombo"));
        m_terrainModeCombo->addItems({tr("Adaptive elevation error"),tr("Legacy block sizing"),tr("Off")});
        m_terrainModeCombo->setToolTip(tr("Adaptive mode starts coarse and refines where the actual triangles miss the terrain."));
        f->addRow(tr("Terrain refinement:"),m_terrainModeCombo);
        f->addRow(tr("Maximum &vertical error:"), m_terrainTolSpin);
        m_terrainReferenceCombo = new QComboBox(g);
        m_terrainReferenceCombo->setObjectName(QStringLiteral("meshTerrainReferenceCombo"));
        m_terrainReferenceCombo->addItems({tr("DEM averaged to the minimum cell size"),
                                           tr("Full DEM resolution"),
                                           tr("Full DEM resolution, read directly")});
        m_terrainReferenceCombo->setToolTip(tr(
            "The terrain the mesh is measured against and takes its elevations from. The first "
            "two keep a compressed float32 copy of the needed window in the project's .meshcache "
            "folder (made once, reused by later runs). Averaged: detail narrower than a cell is "
            "smoothed out, so the vertical error controls features a cell can represent. Full "
            "resolution: every DEM pixel counts, so small errors refine down to the minimum cell "
            "along curbs and walls. Read directly: no copy; slow for large DEMs on slow drives."));
        f->addRow(tr("Terrain &reference:"), m_terrainReferenceCombo);
        m_terrainBreaklinesBox = new QCheckBox(tr("Capture terrain breaklines"),g);
        m_terrainBreaklinesBox->setObjectName(QStringLiteral("meshTerrainBreaklinesBox"));
        m_terrainBreaklinesBox->setChecked(true);
        m_terrainBreaklinesBox->setToolTip(tr("Align edges with DEM ridges, banks and slope breaks. Authored breaklines are always preserved."));
        f->addRow(m_terrainBreaklinesBox);
        m_refineFeaturesBox = new QCheckBox(tr("Refine around model features"),g);
        m_refineFeaturesBox->setObjectName(QStringLiteral("meshRefineFeaturesBox"));
        m_refineFeaturesBox->setChecked(false);
        m_refineFeaturesBox->setToolTip(tr("Apply the feature cell size around included nodes and lines. When unchecked, geometry and coupling remain constrained, and terrain and triangle quality determine nearby refinement."));
        f->addRow(m_refineFeaturesBox);
        m_maxBreaklinesSpin = new QSpinBox(g);
        m_maxBreaklinesSpin->setObjectName(QStringLiteral("meshMaxBreaklinesSpin"));
        m_maxBreaklinesSpin->setRange(0, 10'000'000);
        m_maxBreaklinesSpin->setSingleStep(1000);
        m_maxBreaklinesSpin->setSpecialValueText(tr("automatic (cell budget ÷ 200)"));
        m_maxBreaklinesSpin->setToolTip(tr(
            "How many detected terrain break lines are kept as mesh edges, the most significant "
            "first (longest, highest step). Every kept line costs cells along it."));
        f->addRow(tr("Maximum terrain &break lines:"), m_maxBreaklinesSpin);
        m_terrainReportLabel=new QLabel(g);
        m_terrainReportLabel->setWordWrap(true);
        m_terrainReportLabel->setObjectName(QStringLiteral("meshTerrainReportLabel"));
        f->addRow(m_terrainReportLabel);
        connect(m_terrainModeCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode) {
            m_terrainTolSpin->setEnabled(mode!=2);
            m_terrainTolSpin->setSpecialValueText(mode==0?tr("(automatic from DEM)"):tr("(off)"));
            m_terrainBreaklinesBox->setEnabled(mode!=2);
        });

        qualityVBox->addWidget(g);
    }

    // Shape
    {
        auto *g = new QGroupBox(tr("Shape"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        // Triangles everywhere, quads only in aligned strips
        // (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D10–D12).
        m_minAngleSpin = new QDoubleSpinBox(g);
        m_minAngleSpin->setObjectName(QStringLiteral("meshMinAngleSpin"));
        m_minAngleSpin->setRange(20.0, 33.0);
        m_minAngleSpin->setDecimals(1);
        m_minAngleSpin->setSuffix(QStringLiteral("°"));
        m_minAngleSpin->setToolTip(tr(
            "Every triangle's smallest angle reaches this: higher gives rounder "
            "triangles and more of them. The exceptions are the wedge between "
            "two lines that meet at a smaller angle and triangles resting on "
            "the edge of a quad strip."));
        f->addRow(tr("Minimum &angle:"), m_minAngleSpin);
        m_quadModeCombo = new QComboBox(g);
        m_quadModeCombo->setObjectName(QStringLiteral("meshQuadModeCombo"));
        m_quadModeCombo->addItems({tr("Off (triangles only)"), tr("Corridors and strips"), tr("Corridors, strips and open blocks")});
        m_quadModeCombo->setCurrentIndex(2);
        m_quadModeCombo->setToolTip(tr(
            "Where the mesh uses quads; triangles fill everywhere else and every join is exact. "
            "Corridors and strips: selected corridors and streets or ditches between facing break "
            "lines (each with its own option). Open blocks: after a first pass, rectangles are "
            "placed where the cells are nearly uniform and no feature runs — water, parks, "
            "yards, wide lots — and meshed as square quads in a second pass (more time). "
            "Quad regions you list are used in every mode except Off."));
        f->addRow(tr("&Quads:"), m_quadModeCombo);
        m_qualityOrderBox = new QCheckBox(tr("Prioritize worst triangle angles"),g);
        m_qualityOrderBox->setObjectName(QStringLiteral("meshQualityOrderBox"));
        m_qualityOrderBox->setChecked(true);
        m_qualityOrderBox->setToolTip(tr("Process poor angles first to reduce unnecessary refinement. Uncheck to compare the previous insertion order."));
        f->addRow(m_qualityOrderBox);
        m_latticeSeedingBox = new QCheckBox(tr("Seed near-equilateral points before refinement"), g);
        m_latticeSeedingBox->setObjectName(QStringLiteral("meshLatticeSeedingBox"));
        m_latticeSeedingBox->setChecked(true);
        m_latticeSeedingBox->setToolTip(tr(
            "Fill open areas with hexagonal point lattices graded by the size field, so triangles "
            "start near equilateral instead of being shaped only by refinement."));
        f->addRow(m_latticeSeedingBox);
        m_smoothingSpin = new QSpinBox(g);
        m_smoothingSpin->setObjectName(QStringLiteral("meshSmoothingSpin"));
        m_smoothingSpin->setRange(0, 10);
        m_smoothingSpin->setValue(4);
        m_smoothingSpin->setSpecialValueText(tr("off"));
        m_smoothingSpin->setToolTip(tr(
            "Passes that move free vertices toward the centre of their neighbours when that "
            "improves the worst angle around them. Vertices on constraints never move."));
        f->addRow(tr("&Smoothing passes:"), m_smoothingSpin);

        m_streetQuadsBox = new QCheckBox(tr("Quads between facing break lines (streets, ditches)"), g);
        m_streetQuadsBox->setObjectName(QStringLiteral("meshStreetQuadsBox"));
        m_streetQuadsBox->setToolTip(tr(
            "Where two terrain break lines run side by side with lower ground "
            "between them — the curbs of a street, the banks of a ditch — the "
            "cells between them are rows of quads along the feature. Needs a "
            "DTM and a terrain tolerance; everywhere else the mesh is "
            "triangles."));
        f->addRow(QString(), m_streetQuadsBox);

        m_conduitStripSpin = new QDoubleSpinBox(g);
        m_conduitStripSpin->setObjectName(QStringLiteral("meshConduitStripSpin"));
        m_conduitStripSpin->setRange(0.0, 1e6);
        m_conduitStripSpin->setDecimals(3);
        m_conduitStripSpin->setSpecialValueText(tr("(off)"));
        m_conduitStripSpin->setToolTip(tr(
            "Width of a strip of quads laid along every conduit, the conduit "
            "on its middle row. Each strip stops one to four widths short of "
            "the conduit's ends so strips never meet at a junction; a strip "
            "that would cross or crowd another feature is left out and the "
            "conduit stays a line of triangle edges."));
        f->addRow(tr("Conduit &quad strip width:"), m_conduitStripSpin);

        m_quadRegionLayerCombo = new QComboBox(g);
        m_quadRegionLayerCombo->setObjectName(QStringLiteral("meshQuadRegionLayerCombo"));
        m_quadRegionLayerCombo->setToolTip(tr(
            "Polygon layer of quad regions (exterior rings only; read in the "
            "worker and reprojected to the mesh CRS). A four-sided polygon is "
            "filled with quads aligned to its sides; any other shape keeps "
            "its outline as cell edges with triangles inside.\n\n"
            "Optional per-feature attributes:\n"
            "  h / quad_spacing   cell size inside the polygon (map units)\n"
            "  tag                cell tag"));
        f->addRow(tr("Region &layer:"), m_quadRegionLayerCombo);

        m_quadRegionSubcatchEdit = new QLineEdit(g);
        m_quadRegionSubcatchEdit->setPlaceholderText(tr("comma-separated subcatchment IDs"));
        m_quadRegionSubcatchEdit->setToolTip(tr(
            "Subcatchment polygons whose rings become cell boundaries, tagged "
            "subcatch_<ID>; four-sided ones are filled with aligned quads. An "
            "unknown ID stops generation with an error."));
        f->addRow(tr("Subcatchments:"), m_quadRegionSubcatchEdit);

        qualityVBox->addWidget(g);
    }

    if (m_pw && !m_pw->corridorRecipeLoadError().isEmpty()) {
        auto *warning = new QLabel(tr("The saved corridor recipe could not be loaded: %1\n"
                                     "Review the source settings. Generating a new mesh can replace this recipe; cancelling preserves it.")
                                      .arg(m_pw->corridorRecipeLoadError()), qualityPage);
        warning->setWordWrap(true);
        warning->setAccessibleName(tr("Saved corridor recipe needs review"));
        qualityVBox->addWidget(warning);
    }
    auto *corridorGroup = new QGroupBox(tr("Corridors"), qualityPage);
    auto *corridorLayout = new QVBoxLayout(corridorGroup);
    m_corridorSources = new CorridorSourcesWidget(corridorGroup);
    corridorLayout->addWidget(m_corridorSources);
    qualityVBox->addWidget(corridorGroup);

    // Boundaries
    {
        auto *g = new QGroupBox(tr("Boundaries"), qualityPage);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

        m_trimTurnSpin = new QDoubleSpinBox(g);
        m_trimTurnSpin->setRange(0.0, 45.0);
        m_trimTurnSpin->setDecimals(1);
        m_trimTurnSpin->setSuffix(QStringLiteral("°"));
        m_trimTurnSpin->setSpecialValueText(tr("(off)"));
        m_trimTurnSpin->setToolTip(tr(
            "Drop a boundary, hole or auxiliary-line vertex where the line "
            "turns by less than this. Conduit alignments and junctions are "
            "never trimmed."));
        f->addRow(tr("Trim boundary &vertices: max turn:"), m_trimTurnSpin);

        m_trimDeviationSpin = new QDoubleSpinBox(g);
        m_trimDeviationSpin->setRange(0.0, 1e9);
        m_trimDeviationSpin->setDecimals(3);
        m_trimDeviationSpin->setSpecialValueText(tr("(cell size / 10)"));
        m_trimDeviationSpin->setToolTip(tr(
            "Trimming may never move a line further than this from its "
            "original vertices."));
        f->addRow(tr("Trim boundary vertices: max &deviation:"), m_trimDeviationSpin);

        qualityVBox->addWidget(g);
    }
    {
        auto *g=new QGroupBox(tr("Large meshes"),qualityPage);
        auto *f=new QFormLayout(g);
        m_maxCellsSpin=new QSpinBox(g);
        m_maxCellsSpin->setObjectName(QStringLiteral("meshMaxCellsSpin"));
        m_maxCellsSpin->setRange(1000,100'000'000);
        m_maxCellsSpin->setSingleStep(1'000'000);
        m_maxCellsSpin->setValue(20'000'000);
        m_maxCellsSpin->setGroupSeparatorShown(true);
        m_maxCellsSpin->setToolTip(tr("Stop with a reported limit if refinement reaches this cell budget. A capped mesh may not meet terrain or quality requirements."));
        f->addRow(tr("Cell budget:"),m_maxCellsSpin);
        m_terrainCacheSpin=new QSpinBox(g);
        m_terrainCacheSpin->setObjectName(QStringLiteral("meshTerrainCacheSpin"));
        m_terrainCacheSpin->setRange(0,65536); m_terrainCacheSpin->setValue(0);
        m_terrainCacheSpin->setSpecialValueText(tr("Automatic"));
        m_terrainCacheSpin->setSuffix(tr(" MiB"));
        m_terrainCacheSpin->setToolTip(tr("Memory per terrain cache (DEM tiles and feature mask). Automatic uses one eighth of physical memory, from 256 MiB to 8 GiB. A larger cache is faster on large DEMs and does not change the mesh. Large feature masks spill to disk. The mesh, feature chains and terrain summaries use additional memory."));
        f->addRow(tr("Terrain cache:"),m_terrainCacheSpin);
        qualityVBox->insertWidget(2,g);
    }
    qualityVBox->addStretch();

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
    const QString     suf = QStringLiteral(" ") + len;

    if (m_nodeFlattenSpin)   m_nodeFlattenSpin->setSuffix(suf);
    if (m_nodeMinSepSpin)    m_nodeMinSepSpin->setSuffix(suf);
    if (m_cellSizeSpin)      m_cellSizeSpin->setSuffix(suf);
    if (m_minCellSizeSpin)   m_minCellSizeSpin->setSuffix(suf);
    if (m_terrainTolSpin)    m_terrainTolSpin->setSuffix(suf);
    if (m_conduitStripSpin)  m_conduitStripSpin->setSuffix(suf);
    if (m_trimDeviationSpin) m_trimDeviationSpin->setSuffix(suf);
}

void MeshGenerationDialog::seedDefaults()
{
    // Iteration 4 — seed values come from the user-editable 2D Defaults
    // preference page (Preferences → 2D Defaults). The compiled-in struct
    // defaults carry the seeds (33° min angle per the 2026-07-31 decision,
    // SI-canonical distances, thinning on 0.75/1, …).
    const auto t = PreferencesManager::instance()->twoDDefaults();
    // Nodes as Steiner vertices default ON (2026-09-11, reversing the
    // 2026-07-28 Plan Part B decision): with rim elevations and the minimum
    // node separation on as well, close clusters (weir / orifice endpoints)
    // are demoted to cell coupling instead of forcing tiny cells. Virtual
    // junctions are never pinned (collectInputs).
    m_includeJunctions->setChecked(t.meshNodesAsVertices);
    m_includeConduits->setChecked(true);
    m_includeSubcatch->setChecked(true);
    m_mapNodesAfterGen->setChecked(true);
    m_nodesUseRim->setChecked(t.meshNodesUseRim);
    m_elevMethodCombo->setCurrentIndex(0);  // IDW
    m_nnVariantCombo->setCurrentIndex(0);   // Sibson
    m_idwPowerSpin->setValue(t.meshIdwPower);

    // Scale distance defaults (stored SI-canonical) to the project's
    // length unit.
    const double toUnit = UnitSystem::instance()->isSI() ? 1.0 : 1.0 / 0.3048;
    m_nodeFlattenSpin->setValue(t.meshNodeFlattenRadM * toUnit);
    m_nodeMinSepBox->setChecked(t.meshMinNodeSepOn);
    m_nodeMinSepSpin->setValue(t.meshMinNodeSepM * toUnit);
    // Resolution / shape / boundaries (MESH_OVERHAUL_PLAN_2026-09-29.md §3),
    // seeded from the 2D Defaults preference page (SI-canonical lengths).
    m_cellSizeSpin->setValue(t.meshCellSizeM * toUnit);
    m_coarsenSpin->setValue(t.meshCoarsenFactor);
    m_sizeRatioSpin->setValue(t.meshSizeRatio);
    m_minCellSizeSpin->setValue(t.meshMinCellSizeM * toUnit);
    m_terrainTolSpin->setValue(t.meshTerrainToleranceM * toUnit);
    m_minAngleSpin->setValue(t.meshMinAngleDeg);
    m_streetQuadsBox->setChecked(t.meshQuadsBetweenBreaklines);
    m_conduitStripSpin->setValue(3.048 * toUnit);   // 10 ft
    m_trimTurnSpin->setValue(t.meshTrimTurnDeg);
    m_trimDeviationSpin->setValue(t.meshTrimDeviationM * toUnit);
    if (m_corridorSources && m_pw) m_corridorSources->setSources(m_pw->corridorSources());
    if (m_quadRegionLayerCombo)   m_quadRegionLayerCombo->setCurrentIndex(0);
    if (m_quadRegionSubcatchEdit) m_quadRegionSubcatchEdit->clear();
    m_manningsValueSpin->setValue(t.meshManningsN);
    m_initDepthSpin->setValue(t.meshInitDepth);
    m_outputExternal->setChecked(t.meshOutputExternal);
    updateUnitDisplay();   // set suffixes and tooltip after values are seeded
    populateLayerCombos();
    if (m_pw && m_pw->activeTerrain()) {
        for (int i=0;i<m_dtmCombo->count();++i)
            if (m_dtmCombo->itemData(i).value<void *>()==m_pw->activeTerrain()) m_dtmCombo->setCurrentIndex(i);
    }
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

namespace {
QString meshPickerIdentity(QComboBox *combo,int index) {
    void *p=combo->itemData(index).value<void *>();
    if (!p) return QStringLiteral("none");
    if (p==reinterpret_cast<void *>(0x1)) return QStringLiteral("subcatchments");
    return static_cast<OpenSWMMVisLayer *>(p)->layerId();
}
}

void MeshGenerationDialog::saveOptions()
{
    if (!m_pw || m_pw->isClosing()) return;
    QJsonObject controls;
    const double toSI=UnitSystem::instance()->isSI()?1.0:.3048;
    for (QWidget *w:findChildren<QWidget *>()) {
        const QString key=w->objectName();
        if (!key.startsWith(QStringLiteral("mesh"))) continue;
        if (auto *spin=qobject_cast<QDoubleSpinBox *>(w))
            controls[key]=spin->value()*(w->property("meshDistance").toBool()?toSI:1.0);
        else if (auto *spin=qobject_cast<QSpinBox *>(w)) controls[key]=spin->value();
        else if (auto *combo=qobject_cast<QComboBox *>(w))
            controls[key]=w->property("meshLayerPicker").toBool()?QJsonValue(meshPickerIdentity(combo,combo->currentIndex())):QJsonValue(combo->currentIndex());
        else if (auto *button=qobject_cast<QAbstractButton *>(w); button && button->isCheckable()) controls[key]=button->isChecked();
        else if (auto *edit=qobject_cast<QLineEdit *>(w)) controls[key]=edit->text();
    }
    QJsonArray sources;
    auto addSources=[&](const QVector<AuxLayerRow> &rows) {
        for(const auto &row:rows) if(row.layer) sources.append(QJsonObject{
            {QStringLiteral("id"),row.layer->layerId()}, {QStringLiteral("include"),row.include->isChecked()},
            {QStringLiteral("useZ"),row.useZ->isChecked()}});
    };
    addSources(m_pointLayerRows); addSources(m_lineLayerRows);
    QJsonObject options=m_pw->meshGenerationOptions();
    options[QStringLiteral("version")]=1;
    options[QStringLiteral("controls")]=controls;
    options[QStringLiteral("sources")]=sources;
    QJsonArray regions;
    const auto finiteJson=[](double v) { return std::isfinite(v)?QJsonValue(v):QJsonValue(QJsonValue::Null); };
    if (m_regionDefaults) for (const auto &row:m_regionDefaults->rows()) {
        QJsonArray params;
        for (double p:row.infil.p) params.append(finiteJson(p));
        regions.append(QJsonObject{{QStringLiteral("tag"),row.tag},
            {QStringLiteral("manningsN"),finiteJson(row.manningsN)},{QStringLiteral("depthM"),finiteJson(row.initDepth*toSI)},
            {QStringLiteral("method"),int(row.infil.method)},{QStringLiteral("params"),params},
            {QStringLiteral("destination"),int(row.infil.dest)}});
    }
    options[QStringLiteral("regions")]=regions;
    if(m_corridorSources && m_pw->modelLayer()) {
        QVector<mesh::CorridorSource> corridors; QJsonObject recipe;
        const QString sidecar=ProjectSerializer::sidecarPathFor(m_pw->modelLayer()->modelFilePath());
        if(m_corridorSources->sources(&corridors,nullptr) && MeshCorridorRecipe::encode(corridors,sidecar,&recipe,nullptr))
            options[QStringLiteral("corridorDraft")]=recipe;
    }
    if (options!=m_pw->meshGenerationOptions()) {
        m_pw->setMeshGenerationOptions(options);
        m_pw->setHasChanges(true);
    }
}

void MeshGenerationDialog::restoreOptions()
{
    if (!m_pw) return;
    const auto options=m_pw->meshGenerationOptions();
    if(options.value(QStringLiteral("version")).toInt()!=1) return;
    const auto controls=options.value(QStringLiteral("controls")).toObject();
    const double fromSI=UnitSystem::instance()->isSI()?1.0:1.0/.3048;
    // Source/unit combos emit dependent updates. Restore those first, then
    // exact saved scalar values such as a manually overridden Z conversion.
    for (QComboBox *combo:findChildren<QComboBox *>()) {
        const auto it=controls.constFind(combo->objectName()); if(it==controls.constEnd()) continue;
        if(combo->property("meshLayerPicker").toBool()) {
            for(int i=0;i<combo->count();++i) if(meshPickerIdentity(combo,i)==it->toString()) { combo->setCurrentIndex(i); break; }
        } else if(it->isDouble() && it->toInt()>=0 && it->toInt()<combo->count()) combo->setCurrentIndex(it->toInt());
    }
    for (QWidget *w:findChildren<QWidget *>()) {
        const auto it=controls.constFind(w->objectName()); if(it==controls.constEnd()) continue;
        if (auto *spin=qobject_cast<QDoubleSpinBox *>(w); spin && it->isDouble())
            spin->setValue(it->toDouble()*(w->property("meshDistance").toBool()?fromSI:1.0));
        else if (auto *spin=qobject_cast<QSpinBox *>(w); spin && it->isDouble()) spin->setValue(it->toInt());
        else if (auto *button=qobject_cast<QAbstractButton *>(w); button && button->isCheckable() && it->isBool()) button->setChecked(it->toBool());
        else if (auto *edit=qobject_cast<QLineEdit *>(w); edit && it->isString()) edit->setText(it->toString());
    }
    auto restoreSources=[&](const QVector<AuxLayerRow> &rows) {
        for(const auto &value:options.value(QStringLiteral("sources")).toArray()) {
            const auto entry=value.toObject();
            for(const auto &row:rows) if(row.layer && row.layer->layerId()==entry.value(QStringLiteral("id")).toString()) {
                row.include->setChecked(entry.value(QStringLiteral("include")).toBool());
                row.useZ->setChecked(row.is3D && entry.value(QStringLiteral("useZ")).toBool());
            }
        }
    };
    restoreSources(m_pointLayerRows); restoreSources(m_lineLayerRows);
    if(m_corridorSources && m_pw->modelLayer() && options.contains(QStringLiteral("corridorDraft"))) {
        QVector<mesh::CorridorSource> corridors;
        const QString sidecar=ProjectSerializer::sidecarPathFor(m_pw->modelLayer()->modelFilePath());
        if(MeshCorridorRecipe::decode(options.value(QStringLiteral("corridorDraft")),sidecar,&corridors,nullptr))
            m_corridorSources->setSources(corridors);
    }
    const auto last=options.value(QStringLiteral("lastRun")).toObject();
    if(!last.isEmpty() && last.value(QStringLiteral("toleranceM")).toDouble()>0)
        m_terrainReportLabel->setText(tr("Last run: %1 cells; terrain tolerance %2 %4; %3 cells exceeded it, %5 could not be verified.")
        .arg(last.value(QStringLiteral("cells")).toInt())
        .arg(last.value(QStringLiteral("toleranceM")).toDouble()*fromSI)
        .arg(last.value(QStringLiteral("unresolved")).toInt()).arg(UnitSystem::instance()->lengthLabel())
        .arg(last.value(QStringLiteral("unknown")).toInt()));
    else if (!last.isEmpty())
        m_terrainReportLabel->setText(tr("Last run: %1 cells; adaptive terrain checking was off.").arg(last.value(QStringLiteral("cells")).toInt()));
    refreshRegionRows();
    if (m_regionDefaults) {
        QVector<MeshRegionDefaultsWidget::RegionRow> rows;
        for (const auto &value:options.value(QStringLiteral("regions")).toArray()) {
            const auto obj=value.toObject(); MeshRegionDefaultsWidget::RegionRow row;
            row.tag=obj.value(QStringLiteral("tag")).toString();
            row.manningsN=obj.value(QStringLiteral("manningsN")).toDouble(qQNaN());
            row.initDepth=obj.value(QStringLiteral("depthM")).toDouble(qQNaN())*fromSI;
            row.infil.method=mesh::InfilMethod(qBound(-1,obj.value(QStringLiteral("method")).toInt(-1),5));
            row.infil.dest=mesh::InfilDest(qBound(0,obj.value(QStringLiteral("destination")).toInt(),2));
            const auto params=obj.value(QStringLiteral("params")).toArray();
            for (int i=0;i<std::min(int(params.size()),mesh::kInfilMaxParams);++i) row.infil.p[i]=params[i].toDouble(qQNaN());
            rows.append(row);
        }
        m_regionDefaults->restoreRows(rows);
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

    // FEATURE_LAYER_ROLES_AND_FIELDS_PLAN §6 (P6, F4): each picker offers only
    // the layers whose geometry it can read, lists the layers of the matching
    // role first, and labels a feature layer with its role. A layer whose
    // geometry type is not declared (a mixed GeoJSON, a layer still opening)
    // is offered everywhere, as before.
    enum class GeomClass { Unknown, Point, Line, Polygon };
    const auto geomClass = [](GISVectorLayer *v) {
        OGRLayer *ol = v ? v->ogrLayer() : nullptr;
        if (!ol) return GeomClass::Unknown;
        switch (wkbFlatten(ol->GetGeomType())) {
        case wkbPoint: case wkbMultiPoint:               return GeomClass::Point;
        case wkbLineString: case wkbMultiLineString:     return GeomClass::Line;
        case wkbPolygon: case wkbMultiPolygon:           return GeomClass::Polygon;
        default:                                         return GeomClass::Unknown;
        }
    };
    const auto pickable = [&](GeomClass want, FeatureLayerRole preferred) {
        QList<GISVectorLayer *> first, rest;
        for (auto *L : layers)
            if (auto *v = qobject_cast<GISVectorLayer *>(L))
            {
                const GeomClass g = geomClass(v);
                if (g != GeomClass::Unknown && g != want) continue;
                auto *fl = qobject_cast<FeatureLayer *>(v);
                (fl && fl->role() == preferred ? first : rest).append(v);
            }
        return first + rest;
    };
    const auto labelFor = [](GISVectorLayer *v) {
        if (auto *fl = qobject_cast<FeatureLayer *>(v))
            return QStringLiteral("◆ %1  —  %2").arg(fl->name(), featureLayerRoleLabel(fl->role()));
        return v->name();
    };

    if (m_corridorSources)
        m_corridorSources->setLayers(pickable(GeomClass::Line, FeatureLayerRole::Corridor));

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
    for (auto *v : pickable(GeomClass::Polygon, FeatureLayerRole::DomainBoundary))
        m_boundaryLayerCombo->addItem(labelFor(v), QVariant::fromValue<void *>(v));

    // Quad region layer: same payload as the boundary combo (no subcatchment
    // pseudo-entry — subcatchments are named in the edit).
    if (m_quadRegionLayerCombo)
    {
        m_quadRegionLayerCombo->clear();
        m_quadRegionLayerCombo->addItem(tr("(none)"),
                                        QVariant::fromValue<void *>(nullptr));
        for (auto *v : pickable(GeomClass::Polygon, FeatureLayerRole::Region))
            m_quadRegionLayerCombo->addItem(labelFor(v), QVariant::fromValue<void *>(v));
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
    auto fillList = [&](QListWidget *list, QVector<AuxLayerRow> &rows, GeomClass want) {
        list->clear();
        rows.clear();
        for (auto *v : pickable(want, FeatureLayerRole::Breakline))
            {
                const bool is3D = detect3D(v);

                auto *item = new QListWidgetItem(list);
                auto *row  = new QWidget(list);
                auto *h    = new QHBoxLayout(row);
                h->setContentsMargins(4, 1, 4, 1);
                h->setSpacing(8);

                auto *inc = new QCheckBox(labelFor(v), row);
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
            list->addItem(want == GeomClass::Point ? tr("(no point layers)")
                                                   : tr("(no line layers)"));
    };
    fillList(m_pointLayersList, m_pointLayerRows, GeomClass::Point);
    fillList(m_lineLayersList,  m_lineLayerRows,  GeomClass::Line);
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

    // ── Resolution / shape / boundaries (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ──
    // Set BEFORE the domain polygons are collected: the boundary ingestion
    // trims rings with these. Distances are map units, as for every other
    // distance spin in this dialog (no display→SI conversion on collect).
    out->cellSize      = m_cellSizeSpin->value();
    out->coarsenFactor = m_coarsenSpin->value();
    out->sizeRatio     = m_sizeRatioSpin->value();
    out->minCellSize   = m_minCellSizeSpin->value();
    out->conditionBoundary = m_conditionBoundaryBox && m_conditionBoundaryBox->isChecked();
    // Major features only (D-R4): about one break line per 200 budgeted cells.
    out->maxTerrainBreaklines = m_maxBreaklinesSpin && m_maxBreaklinesSpin->value() > 0
        ? m_maxBreaklinesSpin->value() : std::max(1, m_maxCellsSpin->value() / 200);
    if (out->cellSize <= 0.0)
    {
        // Derive from the model extent: about 100 cells across the longer side.
        const MapExtent ext = layer->extent();
        const double span = ext.isValid() ? std::max(ext.xMax() - ext.xMin(), ext.yMax() - ext.yMin()) : 0.0;
        out->cellSize = span > 0.0 ? span / 100.0 : 1.0;
    }
    if (out->minCellSize <= 0.0) out->minCellSize = 0.25 * out->cellSize;
    out->terrainTolerance = m_terrainTolSpin->value();
    out->terrainAdaptive = m_terrainModeCombo->currentIndex()==0;
    out->terrainAutoTolerance = out->terrainAdaptive && out->terrainTolerance==0;
    if (m_terrainModeCombo->currentIndex()==2) out->terrainTolerance=0;
    out->terrainBreaklines=m_terrainBreaklinesBox->isChecked();
    out->refineAtFeatures=m_refineFeaturesBox->isChecked();
    out->terrainCacheMiB=resolvedTerrainCacheMiB(m_terrainCacheSpin->value());
    out->trimTurnDeg   = m_trimTurnSpin->value();
    out->trimDeviation = m_trimDeviationSpin->value() > 0.0 ? m_trimDeviationSpin->value()
                                                            : 0.1 * out->cellSize;
    out->genOpts.maxArea       = 0.4330127018922193 * out->cellSize * out->cellSize;
    out->genOpts.minCellSize   = out->minCellSize;
    // Near-equilateral triangles (MESH_REGIONAL_TRIQUAD_PLAN D-R6): graded
    // lattice seeding plus non-degrading smoothing.
    out->genOpts.latticeSeeding  = m_latticeSeedingBox ? m_latticeSeedingBox->isChecked() : true;
    out->genOpts.smoothingPasses = m_smoothingSpin ? m_smoothingSpin->value() : 4;
    out->terrainReference = m_terrainReferenceCombo ? m_terrainReferenceCombo->currentIndex() : 0;
    out->quadMode = m_quadModeCombo ? m_quadModeCombo->currentIndex() : 2;
    out->genOpts.minAngleDeg   = m_minAngleSpin->value();
    out->genOpts.prioritizeQuality=m_qualityOrderBox->isChecked();
    out->genOpts.maxCells=m_maxCellsSpin->value();
    out->genOpts.quadsBetweenBreaklines = m_streetQuadsBox->isChecked() && out->quadMode != 0;
    out->conduitStripWidth     = m_conduitStripSpin->value();

    // ── Mesh CRS — initialised first so every source can reproject to it ──
    // All PSLG inputs (domain polygons, hole rings, constraint segments,
    // Steiner points) must be in the same CRS before triangulation runs.
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
        out->meshLinearUnitToSI = lui.metresPerUnit;
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
                // Every node type is a candidate (junctions, inlet junctions,
                // outfalls, storage, dividers) EXCEPT virtual junctions: they
                // are zero-storage split points on a conduit with no rim of
                // their own. They stay in couplingNodes below.
                if (layer->nodeIsVirtual(idx)) continue;
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
    // but highly concave subcatchments; the mesher propagates the region
    // attribute by flood fill from the seed, bounded by constraints. The region
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

    // ── Quad regions: layer identity (read on the worker) and named
    // subcatchments (rings already cached in the mesh CRS) ─────────────
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
            mesh::QuadRegion r;
            r.ring = QPolygonF(ring);
            // Same spelling the worker gives RegionMarker tags, so the cells
            // inside match the region-defaults table rows.
            r.tag  = QStringLiteral("subcatch_%1").arg(id);
            out->quadRegions.append(std::move(r));
        }
    }
    // Patch boundary vertices are matched to the generator output by a
    // hundredth of the floor size.
    out->genOpts.patchSnapEps = 0.01 * out->minCellSize;

    if (m_corridorSources) {
        QString error;
        if (!m_corridorSources->sources(&out->corridorSources, &error)) return fail(error);
        for (auto &source : out->corridorSources)
            if (source.meshCRSWkt.isEmpty()) source.meshCRSWkt = out->meshCRSWkt;
    }

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
    const QString verticalUnit=m_meshVertCRSCombo->currentData().toString();
    out->verticalUnitToSI = verticalUnit==QLatin1String("ft")
        || (verticalUnit==QLatin1String("auto") && !UnitSystem::instance()->isSI()) ? .3048 : 1.0;
    // The tolerance control is labelled in the model's length unit. Compare
    // it in the same output vertical unit as the generated vertex heights.
    out->terrainTolerance *= (UnitSystem::instance()->isSI()?1.0:.3048)/out->verticalUnitToSI;

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
    saveOptions();
    if (m_watcher) return;
    PipelineInputs inputs;
    QString err;
    if (!collectInputs(&inputs, &err))
    {
        QMessageBox::critical(this, tr("Cannot generate mesh"), err);
        return;
    }
    beginGenerationGuard();

    if (!m_pw->corridorRecipeLoadError().isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Replace unreadable corridor recipe?"),
            tr("The saved corridor recipe could not be loaded:\n\n%1\n\n"
               "Successful generation will replace it with the corridor settings in this dialog. "
               "The saved project changes only on Save. Continue?").arg(m_pw->corridorRecipeLoadError()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) { clearGenerationGuard(); return; }
    }

    // Confirm replacement of the working copy at an existing mesh path.
    // Publication of its saved file remains deferred until project Save.
    if (inputs.outputMode == mesh::MeshOutputMode::External
        && !inputs.meshOutputPath.isEmpty()
        && QFileInfo::exists(inputs.meshOutputPath))
    {
        const auto ovBtn = QMessageBox::warning(
            this, tr("Replace mesh in this project?"),
            tr("A mesh file already exists at:\n\n%1\n\nGenerating will "
               "replace the working mesh in this project. The saved file "
               "will change only when you save the project. Continue?")
                .arg(QDir::toNativeSeparators(inputs.meshOutputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ovBtn != QMessageBox::Yes) {
            clearGenerationGuard();
            return;
        }
    }
    if (!generationOwnerIsCurrent()) {
        clearGenerationGuard();
        QMessageBox::warning(this, tr("Project changed"),
            tr("The project changed while generation was being prepared. Review the inputs and generate again."));
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
        clearGenerationGuard();
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
        clearGenerationGuard();
        QMessageBox::critical(this, tr("Mesh generation failed"),
            result.errorMsg.isEmpty() ? tr("Unknown error.") : result.errorMsg);
        return;
    }

    // Compare ownership before any layer removal or engine mutation.
    if (!generationOwnerIsCurrent() || !m_pw->canvas()) {
        clearGenerationGuard();
        QMessageBox::warning(this, tr("Generated mesh was not applied"),
            tr("The project or its inputs changed while generation was running. "
               "The saved model and mesh were not changed. Review the inputs and generate again."));
        return;
    }
    const QString generatedModelPath = m_generationModelPath;
    const int generatedCellCount = result.meshResult.triangles.size();
    clearGenerationGuard();
    QString corridorError;
    if (!mesh::corridorSourceFilesUnchanged(result.corridorSourceStamps, &corridorError)) {
        QMessageBox::warning(this, tr("Generated mesh was not applied"), corridorError);
        return;
    }

    // Add the generated mesh layer to the canvas (main thread — safe).
    if (auto *canvas = m_pw->canvas())
    {
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
        std::unique_ptr<SWMM2DMeshLayer> pendingLayer;
        try {
            pendingLayer = std::make_unique<SWMM2DMeshLayer>(std::move(result.meshResult),
                                                   isExt ? result.meshPath : generatedModelPath,
                                                   /*parent=*/nullptr,
                                                   /*deferHeavyGeometry=*/true);
            auto *meshLayer = pendingLayer.get();
            meshLayer->setExternalMesh(isExt);
            meshLayer->setOwnsGeneratedTopology(true);
            meshLayer->setMeshUnitsSI(result.meshUnitsSI);
            meshLayer->setActiveMesh(true);
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

        } catch (const std::exception &e) {
            QMessageBox::critical(this, tr("Generated mesh was not applied"),
                tr("Could not prepare the generated layer: %1. The previous mesh remains available.")
                    .arg(QString::fromUtf8(e.what())));
            return;
        } catch (...) {
            QMessageBox::critical(this, tr("Generated mesh was not applied"),
                tr("Could not prepare the generated layer. The previous mesh remains available."));
            return;
        }
        auto *meshLayer = pendingLayer.get();

        QString adoptionError;
        std::unique_ptr<ChannelMeshAdoptionCommand> adoption;
        bool prepared=false;
        try {
            adoption=std::make_unique<ChannelMeshAdoptionCommand>(m_pw,meshLayer,result.corridorSources);
            adoption->setText(tr("Generate mesh"));
            prepared=adoption->prepare({},&adoptionError);
        } catch(const std::exception &e) {
            adoptionError=tr("Could not adopt the generated mesh: %1").arg(QString::fromUtf8(e.what()));
        } catch(...) {
            adoptionError=tr("Could not adopt the generated mesh.");
        }
        if(!prepared) {
            // Once constructed, the command owns detached mesh layers.
            if(adoption) pendingLayer.release();
            QMessageBox::critical(this,tr("Generated mesh was not applied"),adoptionError);
            return;
        }
        pendingLayer.release();
        canvas->undoStack()->push(adoption.release());
        m_pw->attachMeshLayer(meshLayer);
        // Kick the deferred heavy build now that the layer is adopted —
        // mirrors the file-open path (swmmvis.cpp attachMesh2DLayersAsync).
        meshLayer->finishSceneGeometryAsync();
    }

    m_pw->setCorridorSources(result.corridorSources);
    m_pw->setCorridorRecipeLoadError({});
    m_pw->setHasChanges(true);
    if (!result.alignmentWarnings.isEmpty())
    {
        QMessageBox box(QMessageBox::Warning, tr("Mesh requirements need review"),
                        tr("The mesh was generated with unresolved requirements. Review the terrain, quality or alignment notes before running the model."),
                        QMessageBox::Ok, this);
        box.setInformativeText(result.alignmentWarnings.first());
        box.setDetailedText(result.alignmentWarnings.join(QStringLiteral("\n\n")));
        box.exec();
    }
    auto options=m_pw->meshGenerationOptions();
    const double toSI=result.verticalUnitToSI;
    options[QStringLiteral("lastRun")]=QJsonObject{
        {QStringLiteral("cells"),generatedCellCount},
        {QStringLiteral("toleranceM"),result.terrainToleranceUsed*toSI},
        {QStringLiteral("unresolved"),result.generationStats.terrainUnresolved},
        {QStringLiteral("unknown"),result.generationStats.terrainUnknown},
        {QStringLiteral("maxErrorBoundM"),result.generationStats.maxTerrainError*toSI}};
    m_pw->setMeshGenerationOptions(options);
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
