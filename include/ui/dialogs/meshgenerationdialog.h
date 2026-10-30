/**
 * @file      meshgenerationdialog.h
 * @brief     Tools → Generate Mesh… dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AU.4 — Tools → Generate Mesh… dialog.
 *
 * The dialog owns the QWidget side only; all heavy computation is done
 * in a QtConcurrent worker so the UI stays responsive.  The pipeline is
 * split into two phases:
 *
 *  1. collectInputs()   — main thread, reads widgets + SWMMModelLayer.
 *  2. runMeshPipeline() — worker thread (QtConcurrent::run), no widget
 *                         access.  Cancellation is checked between every
 *                         major stage via QPromise::isCanceled().
 *
 * Hole support: every interior ring of a boundary polygon is turned into
 * a set of constraint segments + an interior seed point that tells Triangle
 * to leave that region unmeshed (OGR interior rings → g.addHole()).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESHGENERATIONDIALOG_H
#define MESHGENERATIONDIALOG_H

#include "mesh/corridorsource.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshquadregion.h"
#include "mesh/meshresult.h"
#include "mesh/meshresolution.h"
#include "mesh/inpmeshwriter.h"
#include "map/mapextent.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <meshkit/events.hpp>
#include "mesh/constraintsimplifier.h"
#include "mesh/constraintpreview.h"

class SWMMVisProjectWindow;
class SWMMModelLayer;
class QCloseEvent;
class GISVectorLayer;
class MeshRegionDefaultsWidget;
class CorridorSourcesWidget;
class MeshResolutionWidget;
class MeshRunLogWidget;
class MeshConstraintSourcesWidget;
class MeshConstraintPreviewWidget;
class QTabWidget;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;

class MeshGenerationDialog : public QDialog
{
    Q_OBJECT

public:
    // -----------------------------------------------------------------------
    // Data types exchanged between the main thread and the worker thread.
    // -----------------------------------------------------------------------

    /*! \brief Elevation interpolation method for the no-DTM fallback. */
    enum class ElevInterpMethod { IDW, NaturalNeighbour };
    /*! \brief Natural-neighbour weighting variant. */
    enum class NNVariant { Sibson, Laplace };

    /*! \brief All inputs needed by the pipeline worker.
     *         Collected on the main thread by collectInputs() so the worker
     *         never touches Qt widgets or SWMMModelLayer. */
    struct PipelineInputs {
        std::shared_ptr<meshkit::EventChannel>
            events; ///< Per-job bounded diagnostics, no GUI handles.
        // Files
        QString inpPath;
        QString dtmPath;

        // Domain (outer boundaries — setDomains feeds these to Triangle)
        QVector<QPolygonF> domains;

        // Holes: interior rings of domain polygons. Each entry is one
        // closed ring of vertices; a validated interior seed is derived from it
        // inside runMeshPipeline and passed to MeshGenerator::addHole().
        QVector<QVector<QPointF>> holeRings;

        // ── Boundary source identity ─────────────────────────────────────
        // collectInputs records only WHICH boundary to use; the worker
        // re-opens vector sources by path (fresh GDAL handle — handles must
        // not cross threads) and runs the UnaryUnion dissolve + ring prep
        // itself, so the GUI thread never blocks on 65k-hole boundaries.
        // domains/holeRings above are left empty by collectInputs and are
        // filled by the worker in its own copy of this struct.
        enum class BoundaryKind { AutoBBox, Subcatchments, VectorFile };
        BoundaryKind boundaryKind = BoundaryKind::AutoBBox;
        QString boundaryPath;      ///< VectorFile: datasource path
        QString boundaryLayerName; ///< VectorFile: OGR layer name ("" = first)
        QString boundaryCRSWkt;    ///< VectorFile: source SRS WKT ("" = mesh CRS)
        QString boundarySourceId, boundarySourceLabel;
        mesh::ConstraintPolicyOverride boundaryPolicy, conduitPolicy;
        /*! Close, open and simplify the dissolved boundary at minCellSize
         *  (building holes and gaps narrower than a cell; redundant
         *  vertices). Off here so other callers keep their geometry; the
         *  dialog turns it on by default. */
        bool conditionBoundary = false;
        /*! Adaptive terrain: keep only this many terrain break lines, ranked
         *  by integrated step (mesh::rankBreaklinesByStep). 0 = keep all. */
        int maxTerrainBreaklines = 0;
        /*! What the terrain is measured against: 0 = the DEM averaged to
         *  the minimum cell size (local copy), 1 = full resolution (local
         *  copy), 2 = full resolution read from the DEM itself. */
        int terrainReference = 0;
        /*! 0 = triangles only, 1 = quads in corridors and strips only,
         *  2 = also automatic open-area quad blocks. */
        int quadMode = 2;
        bool requireMappedRegions = false;
        QVector<QVector<QPointF>> subcatchPolys; ///< Subcatchments: raw rings (mesh CRS)
        MapExtent modelExtent;                   ///< AutoBBox fallback frame

        // ── Unfiltered feature candidates ────────────────────────────────
        // Collected without the in-domain test (domains are unknown on the
        // GUI thread now).  The worker filters them against the finished
        // domains and assigns PSLG markers in the original collectInputs
        // order — junctions → conduits → aux points → aux lines → region
        // markers — so marker numbering is unchanged.
        struct CandidateNode {
            QString name;
            QPointF xy;
            double rimZ = 0.0;
            bool hasRim = false;
        };
        QVector<CandidateNode> candidateNodes;
        QVector<QPair<QString, QVector<QPointF>>> candidateLinks;
        struct AuxPoint {
            QPointF xy;
            double z = 0.0;
            bool hasZ = false;
        };
        QVector<AuxPoint> auxPoints;
        struct AuxLine {
            QVector<QPointF> path;
            QVector<double> z;
            bool hasZ = false;
            QString sourceId;
            int simplificationMode = -1; // -1 inherits, 0 preserves, 1 simplifies
            std::optional<double> xyTolerance, zTolerance;
            mesh::ConstraintPolicyOverride sourcePolicy;
            QString sourceLabel;
        };
        QVector<AuxLine> auxLines;
        QVector<QPair<QString, QPointF>> subcatchSeeds;
        bool includeJunctions = false;
        bool includeConduits = false;
        bool includeSubcatch = false;

        // PSLG: pre-extracted from the SWMM model and aux layers
        QVector<mesh::SteinerPoint> steinerPoints;
        QVector<mesh::ConstraintSegment> constraintSegs;
        QVector<mesh::RegionMarker> regionMarkers;

        // Coupling-map lookup tables
        QHash<int, QString> nodeMarkerToTag;
        QHash<int, QString> edgeMarkerToTag;

        // Plan Part B — decoupled 1D↔2D mapping: every model node with
        // coordinates (id, xy), fed to mesh::mapNodesToMesh after Triangle
        // runs. Independent of the junctions-as-Steiner checkbox.
        QVector<QPair<QString, QPointF>> couplingNodes;
        bool mapNodesAfterGen = true;
        /// False under COUPLING_LAW WEIR_ORIFICE: new map rows leave CD/AREA
        /// unauthored so the node structures govern (mesh::couplingAuthorsCdArea).
        bool authorCouplingCdArea = true;

        // WKT of the mesh (model) CRS — passed to the worker so it can build
        // a mesh→DTM coordinate transform when the raster CRS differs.
        QString meshCRSWkt;

        // ── Resolution (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ──────────
        std::optional<mesh::ResolutionSettings>
            resolution;              ///< Version 2 settings in SI; legacy callers may omit.
        double maximumCellSize = -1; ///< -1 derives legacy multiplier; 0 is unbounded.
        double breaklineTolerance =
            -1; ///< -1 retains legacy shared tolerance; 0 estimates independently.
        double terrainAveragingSize = 0; ///< 0 matches minimum.
        double cellSize = 0.0;           ///< Target cell size at features (map units).
        double coarsenFactor = 20.0;     ///< h_max = cellSize × this away from features.
        double sizeRatio = 1.5;          ///< Allowed size ratio between neighbouring cells.
        double minCellSize = 0.0;        ///< Floor h_min (map units); 0 = cellSize / 4.
        /*! Terrain tolerance (mesh vertical units): > 0 bounds the size field
         *  wherever the DEM deviates from a cell-sized plane by more than
         *  this (mesh::TerrainSizeField). No terrain vertices are generated. */
        double terrainTolerance = 0.0;
        mesh::TerrainTolerancePolicy terrainTolerancePolicy; ///< Reference size in XY units; tolerances in output Z units.

        bool terrainAdaptive =
            true; ///< Certify/refine actual triangles rather than a block size surrogate.
        bool terrainAutoTolerance =
            false; ///< GUI's automatic setting; explicit callers retain 0 = off.
        bool terrainBreaklines = true;
        bool refineAtFeatures =
            true; ///< Constraints remain mandatory when this size influence is disabled.
        int terrainCacheMiB = 64;
        // ── Boundaries ──────────────────────────────────────────────
        double trimTurnDeg = 0.0;   ///< Straightness trimming: max turn (deg); 0 = off.
        double trimDeviation = 0.0; ///< Straightness trimming: max deviation (map units).
        int geometryMode = 0;       // 0 legacy recipe, 1 preserve, 2 bounded simplification
        mesh::ConstraintSimplificationPolicy geometryPolicy;
        bool constraintPreviewOnly = false;
        /*! Conduit quad strip width (map units); 0 = conduits are plain
         *  constrained edges (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12.4). */
        double conduitStripWidth = 0.0;
        // Mesh-quality knobs (cellSize → maxArea, minCellSize, minimum angle, street quads)
        mesh::GenerationOptions genOpts;

        // ── Mixed tri-quad output (TRI_QUAD_MESHING_PLAN §3, G2/G3) ──────
        // Structured patches, already generated and validated on the main
        // thread (mesh::makeTransfinitePatch / makeSweptPatch), handed to
        // MeshGenerator::addPatch. genOpts.mergeTrianglePairs / quadMerge
        // carry the G2 merge request; the worker runs the merge itself AFTER
        // elevation fill and attribute seeding (so the bed-planarity test
        // sees real z) rather than inside generate().
        QVector<mesh::PatchMesh> patches;
        QVector<mesh::CorridorSource> corridorSources;

        // ── Quad regions (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ──────────
        // Regions the GUI thread could resolve itself (named subcatchments —
        // rings come from SWMMModelLayer's cache, mesh CRS). The worker
        // appends these AFTER the layer regions below and hands every region
        // to MeshGenerator::addQuadRegion (four-sided → aligned quads, else a
        // constraint ring with triangles inside; spacing = size, tag).
        QVector<mesh::QuadRegion> quadRegions;
        // Polygon layers the WORKER reads with OGR (a GDAL handle must not
        // cross threads). One mesh::QuadRegion per feature exterior ring,
        // reprojected to the mesh CRS when crsWkt differs from meshCRSWkt;
        // fields "h"/"quad_spacing" and "tag".
        struct QuadRegionLayerSpec {
            QString path, layerName, crsWkt;
        };
        QVector<QuadRegionLayerSpec> quadRegionLayers;
        // Vertical unit conversion: multiply all DTM-sampled Z values by this
        // factor before writing to the mesh.  Accounts for DTM being in a
        // different vertical unit than the SWMM model.
        // e.g., DTM in metres + SWMM in feet → factor = 3.28084
        double zConversionFactor = 1.0;
        double verticalUnitToSI = 1.0;

        // Short, human-readable mesh-CRS identifier ("EPSG:32634", "Local").
        // Emitted in the ;; SOURCE_CRS: header line.  Separate from
        // meshCRSWkt (full WKT) to keep the header compact.
        QString meshCRSTag;

        // Human-readable linear-unit name from the mesh CRS ("metre",
        // "US survey foot", …).  Emitted in the ;; UNITS: header so the
        // file is self-describing.  Writer does NOT use it to convert
        // values — XY are written in project-CRS units, matching today's
        // engine expectations.
        QString meshLinearUnitName;
        double meshLinearUnitToSI = 1.0;

        // 3D aux-line vertices: exact (x,y)->z in mesh CRS, seeded into
        // elevCache by coordinate so PSLG simplification can't desync a
        // per-vertex z carried on the (simplified) constraint segment.
        QVector<QPointF> featureZSeedXY;
        QVector<double> featureZSeedZ;

        // Node rim-flatten: when nodes use rim elevation and a flatten radius
        // is set, every terrain / refinement vertex within radius of a node is
        // forced to that node's rim elevation (invert + maxDepth).  Removes the
        // sliver triangles that terrain/rim misalignment creates around nodes.
        QVector<QPointF> nodeRimXY;
        QVector<double> nodeRimZ;
        double nodeFlattenRadius = 0.0; // mesh units; 0 = off
        bool nodesUseRim = false;

        // Minimum node separation (mesh units; <= 0 = off): a node candidate
        // within this distance of an already-kept node is not pinned as a
        // Steiner vertex — it stays in couplingNodes and the post-generation
        // mapper couples it to its containing cell instead.
        double nodeMinSeparation = 0.0;

        // Elevation interpolation for the no-DTM fallback.  IDW (configurable
        // Shepard power) or natural neighbour (Sibson / Laplace); NN falls back
        // to IDW outside the seed convex hull.  Ignored when a DTM is set.
        ElevInterpMethod elevInterpMethod = ElevInterpMethod::IDW;
        NNVariant nnVariant = NNVariant::Sibson;
        double idwPower = 2.0;

        // Output
        mesh::MeshOutputMode outputMode = mesh::MeshOutputMode::External;
        QString meshOutputPath;
        // Uniform per-cell hydraulic seeds, written onto every generated
        // triangle. Spatially varying values are assigned afterwards from the
        // Mesh 2D ribbon (cell editor / Cell Data assignment).
        double manningsN = 0.035;
        double initDepth = 0.0; // mesh length units

        // ── Region defaults (GG0d, GUI plan §3.3) ────────────────────────
        // Read out of MeshRegionDefaultsWidget by collectInputs() and copied
        // here BY VALUE — the worker must never touch the widget.
        //
        // infilDefaults goes straight to MeshResult::infilDefaults: the '*'
        // row and the per-tag rows, unflattened. The pipeline deliberately
        // writes NO per-cell infiltration rows, because materialising tag rows
        // per triangle would destroy the inheritance engine decision D-I3 is
        // built on. Empty unless a row names a method, so an untouched dialog
        // emits no [2D_INFILTRATION*] section at all.
        QVector<mesh::InfilDefaultRow> infilDefaults;

        // Manning's n / initial depth are per-TRIANGLE data (MeshTriangle
        // carries the fields), so region values for them are stamped onto the
        // cells exactly as manningsN/initDepth above are — only infiltration
        // uses the inheritance model. Keyed by MeshTriangle::tag, and empty
        // until a region row is given a value of its own — while it is empty
        // every triangle takes manningsN/initDepth exactly as it does today.
        struct RegionHydraulics {
            double manningsN = 0.0;
            double initDepth = 0.0;
        };
        QHash<QString, RegionHydraulics> regionHydraulics;
    };

    /*! \brief Result produced by the pipeline worker and consumed on the
     *         main thread inside onMeshFinished(). */
    struct PipelineResult {
        meshkit::RunStatus status = meshkit::RunStatus::Failed;
        bool ok = false;
        QString errorMsg;
        QString summary;
        QJsonObject metrics;
        bool constraintPreviewOnly = false;
        mesh::ConstraintPreview constraintPreview;
        QVector<int> exceptionCells;
        QStringList alignmentWarnings; ///< Visible completion notes for degraded quad alignment.
        mesh::MeshResult meshResult;
        mesh::GenerationStats
            generationStats; ///< Strips placed/dropped, triangles under the angle bound.
        double terrainToleranceUsed = 0.0;
        double verticalUnitToSI = 1.0;
        mesh::CouplingMap coupling;
        QString meshPath;
        bool meshUnitsSI = false; ///< Units of the pending generated mesh.
        mesh::MeshOutputMode outputMode = mesh::MeshOutputMode::External;
        QVector<mesh::CorridorSource> corridorSources;
        QVector<mesh::CorridorSourceStamp> corridorSourceStamps;
    };

    /**
     * @brief Build GUI-thread mesh-generation controls seeded from preferences and project drafts.
     * @param[in,out] pw Nullable borrowed project, QPointer-guarded; provides
     * model/canvas/terrain/layer identities and owns adopted meshes. Generation requires a live
     * saved model with engine, extent and usable planar CRS.
     * @param[in] parent Optional Qt dialog owner.
     * Owns widgets/watcher but not project layers. Restores project generation settings, samples
     * layer catalogs synchronously and starts no mesh worker until Generate. UI distances use
     * selected project/global units; shared resolution settings are SI-canonical.
     */
    explicit MeshGenerationDialog(SWMMVisProjectWindow *pw, QWidget *parent = nullptr);
    /**
     * @brief Request cancellation of an active future and disconnect owner guards before widget
     * teardown.
     * Does not explicitly wait for QtConcurrent completion; the worker owns copied inputs and uses
     * no widgets/model QObjects. Cancellation does not remove already written stage/terrain cache
     * files.
     */
    ~MeshGenerationDialog() override;

public slots:
    /**
     * @brief Save current project option drafts, invalidate this generation and request worker
     * cancellation before rejecting.
     * Does not apply a generated mesh or undo previously accepted project changes; options saving
     * may mark the project dirty.
     */
    void reject() override;

protected:
    /**
     * @brief Save options and invalidate/cancel generation, then delegate Qt close handling.
     * @param[in,out] event Required borrowed Qt close event, forwarded without ownership transfer.
     * Does not block for worker completion or publish final mesh/model files.
     */
    void closeEvent(QCloseEvent *event) override;

private slots:
    /**
     * @brief Prompt for an intended external .2dm path and update its draft on selection.
     * Cancellation leaves the path; no file is written by browsing.
     */
    void onBrowseMeshPath();
    /**
     * @brief Save drafts, validate/collect inputs and launch one asynchronous mesh pipeline after
     * owner checks/confirmations.
     * Ignores Generate while a watcher exists. Confirms replacement of unreadable recipe or
     * existing working mesh path; final file publication remains deferred to project Save. Installs
     * input/owner revision guards, shows progress, disables Generate and wires a new Qt-owned
     * future watcher.
     */
    void onAccept();
    void onPreviewConstraints();
    void startPipeline(bool previewOnly);
    /**
     * @brief Consume completed worker state on the GUI thread and adopt only a valid current-owner
     * result.
     * Requires the active watcher; always restores UI first, ignores canceled jobs and reports
     * failed/exception/stale results. Rechecks source-file stamps before preparing a detached mesh
     * and ChannelMeshAdoptionCommand. Successful undo-stack adoption owns the layer, starts
     * deferred heavy scene geometry, stores corridor/metrics/options, marks project dirty and
     * retains the dialog and run log for review, export and optional exception-cell highlighting.
     * Closing after successful adoption returns Accepted. Preparation failures and resource
     * limits preserve the previous working mesh. Final .inp/.2dm writes are deferred to Save.
     */
    void onMeshFinished();
    /*! Handles both "Cancel" (no job running → close dialog) and
     *  "Cancel Generation" (job running → stop worker). */
    /**
     * @brief Request active worker cancellation and disable repeat cancellation, or reject when
     * idle.
     * Running cancellation leaves the dialog open until onMeshFinished restores controls;
     * noninterruptible library calls finish before a checkpoint observes Stop.
     */
    void onCancelOrReject();

private:
    friend class TestMeshTerrainPipeline;
    friend class TestMeshOverhaulBellinge;
    /**
     * @brief Run the widget-free mesh pipeline and translate computation exceptions into failed
     * results.
     * @param[in,out] promise QtConcurrent-managed result/progress/cancellation channel, required
     * valid for the call. Wrapper appends success/failure through addResult; framework owns
     * start/finish, not this function.
     * @param[in] in Owned copy of file identities, CRS/unit scales, geometry and hydraulic options.
     * Parallel XY/Z/marker arrays must be consistent; legacy direct callers supply valid finite
     * meshing settings, while optional ResolutionSettings are validated and converted.
     * Worker opens fresh GDAL/OGR handles, builds/caches boundaries/terrain,
     * generates/refines/reorders cells, fills elevations, maps coupling and returns pending
     * in-memory topology/metrics. Checks cancellation at stage/loop boundaries; GEOS/other library
     * calls can be uninterruptible. Stage/terrain caches may be written even on later
     * cancellation/failure; final model/mesh destinations are not published here. Catch paths
     * classify bad_alloc, standard and unknown exceptions; promise allocation itself is not a
     * no-throw guarantee.
     */
    static void runMeshPipeline(QPromise<PipelineResult> &promise, PipelineInputs in);
    /**
     * @brief Create owned sources/quality/hydraulics pages, intended output controls and embedded
     * progress/actions once.
     * Binds gating and normal live project node-coupling selector; most controls remain generation
     * drafts. Resolution controls share an SI settings widget; shape angles [20,33] degrees, cell
     * budget [1000,100000000], terrain cache [0,65536] MiB (zero automatic), Manning [.001,1] and
     * initial depth [0,1000] display units.
     */
    void buildUi();
    /**
     * @brief Initialize generation drafts from application 2D defaults and current project/canvas.
     * Converts SI preference lengths to display units, chooses active terrain, populates catalogs,
     * sets default .2dm path and region rows. Enables link/subcatch influence and postmapping
     * initially; no generation or final file write.
     */
    void seedDefaults();
    /**
     * @brief Serialize named mesh controls, SI distance values, stable layer IDs, aux selections,
     * region rows and encodable corridor draft to the project options.
     * No-op without project or while closing. Migrates prior version-one metadata, preserves
     * unrelated keys and marks project dirty only when JSON changes. Does not save the project file
     * or author the accepted corridor recipe.
     */
    void saveOptions();
    /**
     * @brief Hydrate generation drafts from supported project options versions one/two.
     * Restores layer/unit combos before exact scalar overrides, converts stored SI distances,
     * migrates legacy resolution using extent, restores aux/corridor/region drafts and last-run
     * report. Missing/unsupported project state is ignored; widget bounds may clamp displayed
     * numbers.
     */
    void restoreOptions();
    /**
     * @brief Replace source catalogs from current canvas layers, grouped by declared geometry and
     * preferred feature role.
     * Requires a project canvas; null/unknown geometry remains offered in relevant pickers. Stores
     * borrowed raw layer pointers in item data/rows, so selected layers must survive input
     * collection. Probes 3D status with a reset feature cursor, creates owned include/use-Z
     * controls and notifies DTM-dependent UI.
     */
    void populateLayerCombos();
    /**
     * @brief Refresh distance suffixes and shared resolution unit conversion from global
     * UnitSystem.
     * Requires its singleton; updates controls only, without rewriting model coordinates or
     * regenerating cached topology.
     */
    void updateUnitDisplay();
    /**
     * @brief Recompute the editable DTM-Z multiplier from detected raster and requested mesh
     * vertical units under a signal blocker.
     * Feet use .3048 metres/unit, auto follows project flow units; no DTM/unknown raster unit
     * assumes metres. A source/unit change replaces any previous manual multiplier.
     */
    void updateZFactor(); // recomputes m_zFactorSpin from DTM + mesh vertical unit combos

    /*! Region tags the generated mesh will carry, in the order collectInputs()
     *  creates their markers. Empty when no region source is selected, which
     *  is what degenerates the region-defaults table to its single '*' row. */
    /**
     * @brief Enumerate final subcatchment marker tags in model row order.
     * @return Owned subcatch_<ID> list for named subcatchments with valid extents when inclusion is
     * checked; empty without model/selection. Matches worker region tagging, without testing
     * whether seeds lie in the final domain.
     */
    [[nodiscard]] QStringList regionTags() const;
    /*! Pushes regionTags() into the region-defaults table. */
    /**
     * @brief Pass current region tags to the owned region-defaults table when present;
     * inherited/star values remain table-managed.
     */
    void refreshRegionRows();

    /*! Collect all inputs from widgets + SWMMModelLayer on the main thread.
     *  Returns false and sets *errOut on any early-out condition (no project,
     *  no extent, etc.).  Does NOT start the worker. */
    /**
     * @brief Collect GUI/model/auxiliary geometry into an owned worker input value without
     * launching work.
     * @param[in,out] out Required non-null destination, normally fresh/default-initialized; appends
     * candidates/regions and may remain partially populated on failure. Contains only copied
     * values/file identities, not GUI/GDAL handles.
     * @param[out] errOut Optional diagnostic destination written on failure; not explicitly cleared
     * on success.
     * @return True when saved active model, extent, planar CRS, resolution, region/corridor and
     * elevation-source checks pass; false with diagnostic otherwise.
     * GUI-thread only. Reads engine rim/coupling data, temporarily alters OGR spatial filters/read
     * cursors (clearing filters afterward), converts aux XY when possible and logs dropped failed
     * transforms. Feature Z remains in mesh vertical units; DTM Z uses its separate multiplier.
     * Boundary/region file identities defer expensive dissolve/ring reads to the worker. Shared
     * resolution is converted from SI into mesh XY/elevation units; other display distances are
     * scaled once. Does not persist final mesh files or mutate engine topology.
     */
    bool collectInputs(PipelineInputs *out, QString *errOut, bool previewOnly = false) const;

    /**
     * @brief Capture current project model/engine/path/edit revision and observe input mutations.
     * Clears previous subscriptions and resets invalidation; model/layer/feature changes mark the
     * pending result stale, while project close also cancels the watcher. Missing project/model
     * leaves an unarmed guard.
     */
    void beginGenerationGuard();
    /**
     * @brief Disconnect generation-input subscriptions and clear captured model/engine/path
     * identities.
     * Does not cancel a worker or reset the invalidation flag/revision by itself.
     */
    void clearGenerationGuard();
    /**
     * @brief Check whether pending result ownership still matches the generation snapshot.
     * @return True only for a nonclosing surviving project/model, same nonnull engine/path/edit
     * revision and no observed invalidation; false otherwise. Does not inspect corridor file stamps
     * (checked separately).
     */
    bool generationOwnerIsCurrent() const;

    QPointer<SWMMVisProjectWindow> m_pw;
    QPointer<SWMMModelLayer> m_generationModel;
    void *m_generationEngine = nullptr;
    QString m_generationModelPath;
    quint64 m_generationRevision = 0;
    bool m_generationInvalidated = false;
    QVector<QMetaObject::Connection> m_generationConnections;

    // ── Sources ─────────────────────────────────────────────────────
    QComboBox *m_dtmCombo = nullptr;
    QLabel *m_domainLabel = nullptr;
    QLabel *m_dtmVertUnitLabel = nullptr;    // shows auto-detected DTM vertical unit
    QComboBox *m_meshVertCRSCombo = nullptr; // output mesh vertical unit
    QDoubleSpinBox *m_zFactorSpin = nullptr; // user-editable Z conversion factor

    // ── Auxiliary feature-layer constraints (all optional) ──────────
    QComboBox *m_boundaryLayerCombo = nullptr;
    QCheckBox *m_conditionBoundaryBox = nullptr; // footprint conditioning (D-R3)
    QListWidget *m_pointLayersList = nullptr;
    QListWidget *m_lineLayersList = nullptr;

    // One row per vector layer in the point / line lists.  Each row carries
    // an "include" checkbox and a "use feature Z" checkbox; the latter is
    // enabled only when the layer's geometry is 3D.
    struct AuxLayerRow {
        GISVectorLayer *layer = nullptr;
        QCheckBox *include = nullptr;
        QCheckBox *useZ = nullptr;
        bool is3D = false;
    };
    QVector<AuxLayerRow> m_pointLayerRows;
    QVector<AuxLayerRow> m_lineLayerRows;

    // ── Constraints (SWMM-aware tagging) ────────────────────────────
    QCheckBox *m_includeJunctions = nullptr;
    QCheckBox *m_includeConduits = nullptr;
    QCheckBox *m_includeSubcatch = nullptr;
    QCheckBox *m_mapNodesAfterGen = nullptr; // Plan Part B: post-gen mapper
    // 1D2D fine-scale coupling §7.2: map only nodes whose 2D coupling type is
    // not NONE; "Set coupleable…" opens the node coupling selector.
    QCheckBox *m_mapOnlyCoupleable = nullptr;
    QLabel *m_coupleableReadout = nullptr;
    /**
     * @brief Count readable nonvirtual-category node coupling states and display the non-None
     * total.
     * No-op without label; absent model/engine clears it. Reads node state only and does not change
     * coupling eligibility.
     */
    void updateCoupleableReadout();
    QCheckBox *m_nodesUseRim = nullptr;          // rim elevation instead of terrain
    QDoubleSpinBox *m_nodeFlattenSpin = nullptr; // flatten terrain within radius of nodes
    QCheckBox *m_nodeMinSepBox = nullptr;        // enforce minimum node separation
    QDoubleSpinBox *m_nodeMinSepSpin = nullptr;  // separation distance (map units)

    // ── Elevation interpolation (no-DTM fallback) ───────────────────
    QGroupBox *m_elevInterpGroup = nullptr;   // whole group (disabled when DTM set)
    QComboBox *m_elevMethodCombo = nullptr;   // IDW | Natural neighbour
    QComboBox *m_nnVariantCombo = nullptr;    // Sibson | Laplace
    QDoubleSpinBox *m_idwPowerSpin = nullptr; // Shepard exponent

    // ── Quality (MESH_OVERHAUL_PLAN_2026-09-29.md §3, MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5) ──
    // Resolution
    MeshResolutionWidget *m_resolutionWidget = nullptr;
    QDoubleSpinBox *m_cellSizeSpin = nullptr;    ///< cell size at features (map units)
    QDoubleSpinBox *m_sizeRatioSpin = nullptr;   ///< size ratio between neighbouring cells
    QDoubleSpinBox *m_minCellSizeSpin = nullptr; ///< floor (map units); (cell size / 4) at 0
    QDoubleSpinBox *m_terrainTolSpin = nullptr;  ///< terrain tolerance (vertical units); (off) at 0
    QComboBox *m_terrainModeCombo = nullptr;
    QCheckBox *m_terrainBreaklinesBox = nullptr;
    QCheckBox *m_refineFeaturesBox = nullptr;
    QCheckBox *m_qualityOrderBox = nullptr;
    QComboBox *m_terrainReferenceCombo = nullptr;
    QComboBox *m_quadModeCombo = nullptr;
    QCheckBox *m_requireMappedRegions = nullptr;
    QCheckBox *m_latticeSeedingBox = nullptr;
    QSpinBox *m_smoothingSpin = nullptr;
    QSpinBox *m_maxBreaklinesSpin = nullptr;
    QSpinBox *m_terrainCacheSpin = nullptr;
    QSpinBox *m_maxCellsSpin = nullptr;
    QLabel *m_terrainReportLabel = nullptr;
    // Shape
    QDoubleSpinBox *m_minAngleSpin = nullptr; ///< minimum triangle angle (deg)
    QCheckBox *m_streetQuadsBox = nullptr;    ///< quads between facing break lines
    QDoubleSpinBox *m_conduitStripSpin =
        nullptr; ///< conduit quad strip width (map units); (off) at 0
    QComboBox *m_quadRegionLayerCombo =
        nullptr; ///< "(none)" + polygon GISVectorLayers (fields h, angle, tag)
    QLineEdit *m_quadRegionSubcatchEdit = nullptr; ///< comma-separated subcatchment IDs
    CorridorSourcesWidget *m_corridorSources = nullptr;
    // Boundaries
    QDoubleSpinBox *m_trimTurnSpin = nullptr; ///< straightness trim: max turn (deg); (off) at 0
    QDoubleSpinBox *m_trimDeviationSpin = nullptr; ///< straightness trim: max deviation (map units)
    QComboBox *m_geometryModeCombo = nullptr;
    QDoubleSpinBox *m_geometryZSpin = nullptr;
    QDoubleSpinBox *m_geometryAreaSpin = nullptr;
    MeshConstraintSourcesWidget *m_constraintSources = nullptr;
    MeshConstraintPreviewWidget *m_constraintPreview = nullptr;

    // ── Uniform per-cell hydraulic seeds ────────────────────────────
    // These two stay the editors for the '*' row; the region-defaults table
    // below mirrors them read-only (GG0d, GUI plan §3.3).
    QDoubleSpinBox *m_manningsValueSpin = nullptr;
    QDoubleSpinBox *m_initDepthSpin = nullptr;

    // ── Region defaults table (GG0d) ────────────────────────────────
    MeshRegionDefaultsWidget *m_regionDefaults = nullptr;

    // ── Output ──────────────────────────────────────────────────────
    QRadioButton *m_outputExternal = nullptr;
    QRadioButton *m_outputInline = nullptr;
    QLineEdit *m_meshPathEdit = nullptr;
    QPushButton *m_browseMeshBtn = nullptr;

    // ── Thread / embedded progress ─────────────────────────────────
    QFutureWatcher<PipelineResult> *m_watcher = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    MeshRunLogWidget *m_runLog = nullptr;
    QTabWidget *m_tabs = nullptr;
    bool m_meshApplied = false; ///< Keep exec() acceptance semantics while retaining the run log.
    QPushButton *m_generateBtn = nullptr; ///< "Generate" — disabled while running.
    QPushButton *m_previewBtn = nullptr;
    bool m_runningPreview = false;
    QPushButton *m_cancelBtn = nullptr;   ///< "Cancel" / "Cancel Generation".
};

#endif // MESHGENERATIONDIALOG_H
