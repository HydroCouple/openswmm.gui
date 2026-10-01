/*!
 * \file   meshgenerationdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * a set of constraint segments + a centroid seed point that tells Triangle
 * to leave that region unmeshed (OGR interior rings → g.addHole()).
 */
#ifndef MESHGENERATIONDIALOG_H
#define MESHGENERATIONDIALOG_H

#include "mesh/burnedrasterwriter.h"
#include "mesh/channelburnboundary.h"
#include "mesh/channelburnlattice.h"
#include "mesh/channelburnnetwork.h"
#include "mesh/corridorsource.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshquadregion.h"
#include "mesh/meshresult.h"
#include "mesh/inpmeshwriter.h"
#include "map/mapextent.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class GeneratedMeshArtifacts;
class SWMMVisProjectWindow;
class SWMMModelLayer;
class QCloseEvent;
class GISVectorLayer;
class MeshRegionDefaultsWidget;
class CorridorSourcesWidget;

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
    enum class NNVariant        { Sibson, Laplace };

    /*! \brief All inputs needed by the pipeline worker.
     *         Collected on the main thread by collectInputs() so the worker
     *         never touches Qt widgets or SWMMModelLayer. */
    struct PipelineInputs
    {
        // Files
        QString inpPath;
        QString dtmPath;

        // Domain (outer boundaries — setDomains feeds these to Triangle)
        QVector<QPolygonF>               domains;

        // Holes: interior rings of domain polygons.  Each entry is one
        // closed ring of vertices; a centroid seed point is derived from it
        // inside runMeshPipeline and passed to MeshGenerator::addHole().
        QVector<QVector<QPointF>>        holeRings;

        // ── Boundary source identity ─────────────────────────────────────
        // collectInputs records only WHICH boundary to use; the worker
        // re-opens vector sources by path (fresh GDAL handle — handles must
        // not cross threads) and runs the UnaryUnion dissolve + ring prep
        // itself, so the GUI thread never blocks on 65k-hole boundaries.
        // domains/holeRings above are left empty by collectInputs and are
        // filled by the worker in its own copy of this struct.
        enum class BoundaryKind { AutoBBox, Subcatchments, VectorFile };
        BoundaryKind boundaryKind = BoundaryKind::AutoBBox;
        QString boundaryPath;        ///< VectorFile: datasource path
        QString boundaryLayerName;   ///< VectorFile: OGR layer name ("" = first)
        QString boundaryCRSWkt;      ///< VectorFile: source SRS WKT ("" = mesh CRS)
        QVector<QVector<QPointF>> subcatchPolys;  ///< Subcatchments: raw rings (mesh CRS)
        MapExtent modelExtent;       ///< AutoBBox fallback frame

        // ── Unfiltered feature candidates ────────────────────────────────
        // Collected without the in-domain test (domains are unknown on the
        // GUI thread now).  The worker filters them against the finished
        // domains and assigns PSLG markers in the original collectInputs
        // order — junctions → conduits → aux points → aux lines → region
        // markers — so marker numbering is unchanged.
        struct CandidateNode { QString name; QPointF xy; double rimZ = 0.0; bool hasRim = false; };
        QVector<CandidateNode> candidateNodes;
        QVector<QPair<QString, QVector<QPointF>>> candidateLinks;
        struct AuxPoint { QPointF xy; double z = 0.0; bool hasZ = false; };
        QVector<AuxPoint> auxPoints;
        struct AuxLine { QVector<QPointF> path; QVector<double> z; bool hasZ = false; };
        QVector<AuxLine> auxLines;
        QVector<QPair<QString, QPointF>> subcatchSeeds;
        bool includeJunctions = false;
        bool includeConduits  = false;
        bool includeSubcatch  = false;

        // PSLG: pre-extracted from the SWMM model and aux layers
        QVector<mesh::SteinerPoint>      steinerPoints;
        QVector<mesh::ConstraintSegment> constraintSegs;
        QVector<mesh::RegionMarker>      regionMarkers;

        // Coupling-map lookup tables
        QHash<int, QString> nodeMarkerToTag;
        QHash<int, QString> edgeMarkerToTag;

        // Plan Part B — decoupled 1D↔2D mapping: every model node with
        // coordinates (id, xy), fed to mesh::mapNodesToMesh after Triangle
        // runs. Independent of the junctions-as-Steiner checkbox.
        QVector<QPair<QString, QPointF>> couplingNodes;
        bool mapNodesAfterGen = true;

        // WKT of the mesh (model) CRS — passed to the worker so it can build
        // a mesh→DTM coordinate transform when the raster CRS differs.
        QString meshCRSWkt;

        // ── Resolution (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ──────────
        double cellSize      = 0.0;   ///< Target cell size at features (map units).
        double coarsenFactor = 4.0;   ///< h_max = cellSize × this away from features.
        double sizeRatio     = 1.5;   ///< Allowed size ratio between neighbouring cells.
        double minCellSize   = 0.0;   ///< Floor h_min (map units); 0 = cellSize / 4.
        /*! Terrain tolerance (mesh vertical units): > 0 bounds the size field
         *  wherever the DEM deviates from a cell-sized plane by more than
         *  this (mesh::TerrainSizeField). No terrain vertices are generated. */
        double terrainTolerance = 0.0;
        // ── Boundaries ──────────────────────────────────────────────
        double trimTurnDeg   = 0.0;   ///< Straightness trimming: max turn (deg); 0 = off.
        double trimDeviation = 0.0;   ///< Straightness trimming: max deviation (map units).
        // Mesh-quality knobs (cellSize → maxArea, minCellSize, cell shape, frame angle)
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
        // to MeshGenerator::addQuadRegion (ring = constraint, spacing = size
        // override, alignAngleDeg = own grid frame, tag).
        QVector<mesh::QuadRegion> quadRegions;
        // Polygon layers the WORKER reads with OGR (a GDAL handle must not
        // cross threads). One mesh::QuadRegion per feature exterior ring,
        // reprojected to the mesh CRS when crsWkt differs from meshCRSWkt;
        // fields "h"/"quad_spacing", "angle"/"quad_angle" and "tag".
        struct QuadRegionLayerSpec { QString path, layerName, crsWkt; };
        QVector<QuadRegionLayerSpec> quadRegionLayers;
        // Vertical unit conversion: multiply all DTM-sampled Z values by this
        // factor before writing to the mesh.  Accounts for DTM being in a
        // different vertical unit than the SWMM model.
        // e.g., DTM in metres + SWMM in feet → factor = 3.28084
        double zConversionFactor = 1.0;

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

        // 3D aux-line vertices: exact (x,y)->z in mesh CRS, seeded into
        // elevCache by coordinate so PSLG simplification can't desync a
        // per-vertex z carried on the (simplified) constraint segment.
        QVector<QPointF> featureZSeedXY;
        QVector<double>  featureZSeedZ;

        // Node rim-flatten: when nodes use rim elevation and a flatten radius
        // is set, every terrain / refinement vertex within radius of a node is
        // forced to that node's rim elevation (invert + maxDepth).  Removes the
        // sliver triangles that terrain/rim misalignment creates around nodes.
        QVector<QPointF> nodeRimXY;
        QVector<double>  nodeRimZ;
        double           nodeFlattenRadius = 0.0;  // mesh units; 0 = off
        bool             nodesUseRim       = false;

        // Minimum node separation (mesh units; <= 0 = off): a node candidate
        // within this distance of an already-kept node is not pinned as a
        // Steiner vertex — it stays in couplingNodes and the post-generation
        // mapper couples it to its containing cell instead.
        double           nodeMinSeparation = 0.0;

        // Elevation interpolation for the no-DTM fallback.  IDW (configurable
        // Shepard power) or natural neighbour (Sibson / Laplace); NN falls back
        // to IDW outside the seed convex hull.  Ignored when a DTM is set.
        ElevInterpMethod elevInterpMethod = ElevInterpMethod::IDW;
        NNVariant        nnVariant        = NNVariant::Sibson;
        double           idwPower         = 2.0;

        // Output
        mesh::MeshOutputMode outputMode    = mesh::MeshOutputMode::External;
        QString              meshOutputPath;
        // Uniform per-cell hydraulic seeds, written onto every generated
        // triangle. Spatially varying values are assigned afterwards from the
        // Mesh 2D ribbon (cell editor / Cell Data assignment).
        double               manningsN     = 0.035;
        double               initDepth     = 0.0;   // mesh length units

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
        struct RegionHydraulics { double manningsN = 0.0; double initDepth = 0.0; };
        QHash<QString, RegionHydraulics> regionHydraulics;

        // ── Channel burn-in (CHANNEL_BURN_IN_PLAN_2026-09-21.md) ─────────
        // collectInputs resolves the selector against the MODEL — the worker
        // may not touch the engine — and hands over finished profiles in the
        // mesh CRS and model vertical units, plus the 1D topology as plain
        // data so the worker can classify nodes without a layer.
        //
        // The worker converts the profiles into the raster's frame, writes the
        // burned DEM and redirects dtmPath to it BEFORE the DTM is opened, so
        // MeshStageCache's terrain key is computed on the burned file and a
        // re-burn invalidates the cached terrain for free.
        bool                       burnEnabled = false;
        mesh::BurnOptions          burnOptions;
        QVector<mesh::BurnProfile> burnProfiles;
        mesh::BurnNetwork          burnNetwork;
        QString                    burnOutputDir;    ///< <project dir>/terrain
        /*! The source DEM's CRS and pixel size, read once on the GUI thread.
         *  The worker builds its mesh→DEM transform from these rather than
         *  re-opening the raster, and the auto chainage step is resolved from
         *  the pixel size before the profiles are built. */
        QString                    burnDemCRSWkt;
        double                     burnDemPixel = 0.0;
        QString                    burnFingerprint;  ///< 8 hex over DEM + options + geometry
        QStringList                burnWarnings;     ///< selector + profile-build notes
        /*! Mesh minimum cell size at the time of collection, for the lattice's
         *  densification guard. Mirrors minSizePolicy.minCellSize. */
        double                     burnMinCellSize = 0.0;
    };

    /*! \brief Result produced by the pipeline worker and consumed on the
     *         main thread inside onMeshFinished(). */
    struct PipelineResult
    {
        bool              ok        = false;
        QString           errorMsg;
        QStringList       alignmentWarnings; ///< Visible completion notes for degraded quad alignment.
        mesh::MeshResult  meshResult;
        mesh::CouplingMap coupling;
        QString           meshPath;
        bool              meshUnitsSI = false; ///< Units of the pending generated mesh.
        mesh::MeshOutputMode outputMode = mesh::MeshOutputMode::External;
        QVector<mesh::CorridorSource> corridorSources;
        QVector<mesh::CorridorSourceStamp> corridorSourceStamps;

        // ── Channel burn-in outcome ──────────────────────────────────────
        /*! Everything the GUI thread needs to perform the 1D surgery, computed
         *  in the worker because the outfall invert must come from the MESHED
         *  bed at the coupling point, not the pre-mesh nominal. Applying it
         *  needs MapUndoStack and therefore the GUI thread (plan §16.3). */
        struct BurnSurgery
        {
            QStringList                  burnedConduits;  ///< Leave the 1D network (D-A).
            QVector<mesh::BurnNodePlan>  nodePlans;
            QHash<QString, double>       outfallInvert;   ///< node id → channel bottom.
            QHash<QString, double>       outfallMaxDepth; ///< node id → mesh bed − invert.
        };

        std::shared_ptr<GeneratedMeshArtifacts> generatedArtifacts;
        QString               burnedDemPath;
        QString               burnReportPath;
        mesh::BurnRasterStats burnStats;
        QStringList           burnWarnings;
        BurnSurgery           burnSurgery;
        bool                  burnRan = false;
    };

    explicit MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                  QWidget *parent = nullptr);
    ~MeshGenerationDialog() override;

public slots:
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onBrowseMeshPath();
    void onAccept();
    void onMeshFinished();
    /*! Handles both "Cancel" (no job running → close dialog) and
     *  "Cancel Generation" (job running → stop worker). */
    void onCancelOrReject();

private:
    friend class TestMeshTerrainPipeline;
    friend class TestMeshOverhaulBellinge;
    static void runMeshPipeline(QPromise<PipelineResult> &promise, PipelineInputs in);
    void buildUi();
    void seedDefaults();
    void populateLayerCombos();
    void updateUnitDisplay();
    void updateZFactor();   // recomputes m_zFactorSpin from DTM + mesh vertical unit combos

    /*! Region tags the generated mesh will carry, in the order collectInputs()
     *  creates their markers. Empty when no region source is selected, which
     *  is what degenerates the region-defaults table to its single '*' row. */
    [[nodiscard]] QStringList regionTags() const;
    /*! Pushes regionTags() into the region-defaults table. */
    void refreshRegionRows();

    /*! \brief Channel burn-in options as the tab currently reads
     *         (CHANNEL_BURN_IN_PLAN_2026-09-21.md §3). */
    [[nodiscard]] mesh::BurnOptions burnOptionsFromUi() const;
    /*! \brief The whole tab as one persisted unit (D-H). */
    [[nodiscard]] mesh::ChannelBurnSettings burnSettingsFromUi() const;
    /*! \brief Push persisted settings back into the tab's widgets. */
    void applyBurnSettings(const mesh::ChannelBurnSettings &st);
    /*! \brief Resolve the burn set against the model and build one profile per
     *         accepted conduit. GUI thread only — it reads the engine. */
    bool collectBurnInputs(PipelineInputs *out) const;
    /*! \brief Apply the 1D surgery the worker planned, as one undo macro.
     *         GUI thread only — it drives MapUndoStack (plan §16.3). */
    void applyBurnSurgery(const PipelineResult &res);
    /*! \brief Enable/disable the burn widgets from the master checkbox. */
    void updateBurnEnabled();
    /*! \brief Resolve the burn set and report what WOULD burn, without
     *         writing a raster or meshing anything. */
    void previewBurn();

    /*! Collect all inputs from widgets + SWMMModelLayer on the main thread.
     *  Returns false and sets *errOut on any early-out condition (no project,
     *  no extent, etc.).  Does NOT start the worker. */
    bool collectInputs(PipelineInputs *out, QString *errOut) const;

    void beginGenerationGuard();
    void clearGenerationGuard();
    bool generationOwnerIsCurrent() const;

    QPointer<SWMMVisProjectWindow> m_pw;
    QPointer<SWMMModelLayer> m_generationModel;
    void *m_generationEngine = nullptr;
    QString m_generationModelPath;
    quint64 m_generationRevision = 0;
    bool m_generationInvalidated = false;
    QVector<QMetaObject::Connection> m_generationConnections;

    // ── Sources ─────────────────────────────────────────────────────
    QComboBox      *m_dtmCombo          = nullptr;
    QLabel         *m_domainLabel       = nullptr;
    QLabel         *m_dtmVertUnitLabel  = nullptr;  // shows auto-detected DTM vertical unit
    QComboBox      *m_meshVertCRSCombo  = nullptr;  // output mesh vertical unit
    QDoubleSpinBox *m_zFactorSpin       = nullptr;  // user-editable Z conversion factor

    // ── Auxiliary feature-layer constraints (all optional) ──────────
    QComboBox     *m_boundaryLayerCombo = nullptr;
    QListWidget   *m_pointLayersList    = nullptr;
    QListWidget   *m_lineLayersList     = nullptr;

    // One row per vector layer in the point / line lists.  Each row carries
    // an "include" checkbox and a "use feature Z" checkbox; the latter is
    // enabled only when the layer's geometry is 3D.
    struct AuxLayerRow
    {
        GISVectorLayer *layer   = nullptr;
        QCheckBox      *include = nullptr;
        QCheckBox      *useZ    = nullptr;
        bool            is3D    = false;
    };
    QVector<AuxLayerRow> m_pointLayerRows;
    QVector<AuxLayerRow> m_lineLayerRows;

    // ── Constraints (SWMM-aware tagging) ────────────────────────────
    QCheckBox     *m_includeJunctions = nullptr;
    QCheckBox     *m_includeConduits  = nullptr;
    QCheckBox     *m_includeSubcatch  = nullptr;
    QCheckBox     *m_mapNodesAfterGen = nullptr;  // Plan Part B: post-gen mapper
    QCheckBox      *m_nodesUseRim     = nullptr;  // rim elevation instead of terrain
    QDoubleSpinBox *m_nodeFlattenSpin = nullptr;  // flatten terrain within radius of nodes
    QCheckBox      *m_nodeMinSepBox   = nullptr;  // enforce minimum node separation
    QDoubleSpinBox *m_nodeMinSepSpin  = nullptr;  // separation distance (map units)

    // ── Elevation interpolation (no-DTM fallback) ───────────────────
    QGroupBox      *m_elevInterpGroup = nullptr;  // whole group (disabled when DTM set)
    QComboBox      *m_elevMethodCombo = nullptr;  // IDW | Natural neighbour
    QComboBox      *m_nnVariantCombo  = nullptr;  // Sibson | Laplace
    QDoubleSpinBox *m_idwPowerSpin    = nullptr;  // Shepard exponent

    // ── Quality (MESH_OVERHAUL_PLAN_2026-09-29.md §3: eleven controls) ──
    // Resolution
    QDoubleSpinBox *m_cellSizeSpin     = nullptr;  ///< cell size at features (map units)
    QDoubleSpinBox *m_coarsenSpin      = nullptr;  ///< coarsen away from features up to ×
    QDoubleSpinBox *m_sizeRatioSpin    = nullptr;  ///< size ratio between neighbouring cells
    QDoubleSpinBox *m_minCellSizeSpin  = nullptr;  ///< floor (map units); (cell size / 4) at 0
    QDoubleSpinBox *m_terrainTolSpin   = nullptr;  ///< terrain tolerance (vertical units); (off) at 0
    // Shape
    QComboBox      *m_cellShapeCombo   = nullptr;  ///< Quads where possible | Triangles
    QDoubleSpinBox *m_gridAngleSpin    = nullptr;  ///< grid orientation (deg from +x, CCW)
    QComboBox      *m_quadRegionLayerCombo   = nullptr; ///< "(none)" + polygon GISVectorLayers (fields h, angle, tag)
    QLineEdit      *m_quadRegionSubcatchEdit = nullptr; ///< comma-separated subcatchment IDs
    CorridorSourcesWidget *m_corridorSources = nullptr;
    // Boundaries
    QDoubleSpinBox *m_trimTurnSpin     = nullptr;  ///< straightness trim: max turn (deg); (off) at 0
    QDoubleSpinBox *m_trimDeviationSpin = nullptr; ///< straightness trim: max deviation (map units)

    // ── Channel burn-in tab ──────────────────────────────────────────────
    QCheckBox      *m_burnEnabledBox      = nullptr;
    QRadioButton   *m_burnAllOpenRadio    = nullptr;
    QRadioButton   *m_burnQueryRadio      = nullptr;
    QRadioButton   *m_burnListRadio       = nullptr;
    QLineEdit      *m_burnQueryEdit       = nullptr;
    QLineEdit      *m_burnListEdit        = nullptr;
    QCheckBox      *m_burnStreetsBox      = nullptr;
    QDoubleSpinBox *m_burnForceHalfWidth  = nullptr;
    QDoubleSpinBox *m_burnMaxHalfWidth    = nullptr;
    QCheckBox      *m_burnClipToBanksBox  = nullptr;
    QDoubleSpinBox *m_burnBankPad         = nullptr;
    QDoubleSpinBox *m_burnChainageStep    = nullptr;
    QDoubleSpinBox *m_burnLateralStep     = nullptr;
    QSpinBox       *m_burnStringCount     = nullptr;
    QComboBox      *m_burnAnchorCombo     = nullptr;
    QDoubleSpinBox *m_burnSectionBlend    = nullptr;
    QCheckBox      *m_burnMonotoneBox     = nullptr;
    QDoubleSpinBox *m_burnMaxIncision     = nullptr;
    QCheckBox      *m_burnQuadCorridorBox = nullptr;
    QDoubleSpinBox *m_burnChannelCellSize = nullptr;
    QCheckBox      *m_burnRoughnessBox    = nullptr;
    QCheckBox      *m_burnConvertNodesBox = nullptr;
    QCheckBox      *m_burnTruncateBox     = nullptr;
    QPushButton    *m_burnPreviewBtn      = nullptr;
    QLabel         *m_burnSummaryLabel    = nullptr;
    // ── Uniform per-cell hydraulic seeds ────────────────────────────
    // These two stay the editors for the '*' row; the region-defaults table
    // below mirrors them read-only (GG0d, GUI plan §3.3).
    QDoubleSpinBox *m_manningsValueSpin  = nullptr;
    QDoubleSpinBox *m_initDepthSpin      = nullptr;

    // ── Region defaults table (GG0d) ────────────────────────────────
    MeshRegionDefaultsWidget *m_regionDefaults = nullptr;

    // ── Output ──────────────────────────────────────────────────────
    QRadioButton  *m_outputExternal = nullptr;
    QRadioButton  *m_outputInline   = nullptr;
    QLineEdit     *m_meshPathEdit   = nullptr;
    QPushButton   *m_browseMeshBtn  = nullptr;

    // ── Thread / embedded progress ─────────────────────────────────
    QFutureWatcher<PipelineResult> *m_watcher     = nullptr;
    QProgressBar                   *m_progressBar = nullptr;
    QLabel                         *m_progressLabel = nullptr;
    QPushButton                    *m_generateBtn = nullptr;  ///< "Generate" — disabled while running.
    QPushButton                    *m_cancelBtn   = nullptr;  ///< "Cancel" / "Cancel Generation".
};

#endif // MESHGENERATIONDIALOG_H
