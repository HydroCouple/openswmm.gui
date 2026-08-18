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

#include "mesh/meshgenerator.h"
#include "mesh/meshresult.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/dtmthinner.h"
#include "mesh/pslgminsize.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QString>
#include <QVector>

class SWMMVisProjectWindow;
class GISVectorLayer;

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

        // Ramer-Douglas-Peucker epsilon (map units) applied to all
        // polygon rings and polyline paths before they enter Triangle.
        // 0 = disabled.
        double pslgSimplifyEps = 0.0;

        // Grid cell size for Steiner point deduplication before Triangle.
        // Near-coincident points (within this distance) from different
        // sources are merged to one.  0 = disabled.
        double pslgSnapEps = 0.0;

        // 2026-07-19 — boundary-aware terrain filter (worker Step 2). DTM
        // terrain Steiner candidates outside the domain, inside a hole
        // ring, or closer than this to any constrained segment (boundary /
        // hole / constraint paths) or mandatory Steiner vertex are dropped
        // so they cannot force slivers along the boundary. <= 0 = auto
        // (0.5 × effective terrain point spacing).
        double terrainBoundaryBuffer = -1.0;

        // 2026-07-19 — optional boundary densification: split domain/hole
        // ring edges longer than this into equal parts after RDP
        // simplification (pure vertex insertion). <= 0 = off.
        double maxBoundaryEdgeLen = 0.0;

        // Mesh-quality knobs
        mesh::GenerationOptions genOpts;

        // 2026-08-17 — minimum cell size enforcement
        // (MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md).  minSizePolicy
        // carries h plus the derived radii; minSizeCleanup enables the
        // post-Triangle sliver collapse.  Both inert when
        // minSizePolicy.minCellSize <= 0, which is the default, so an
        // untouched project reproduces its current mesh exactly.
        mesh::pslg::MinSizePolicy minSizePolicy;
        bool                      minSizeCleanup = true;

        // Terrain-adaptive thinning
        bool                    doThinning = false;
        mesh::DTMThinnerOptions thinnerOpts;

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
    };

    /*! \brief Result produced by the pipeline worker and consumed on the
     *         main thread inside onMeshFinished(). */
    struct PipelineResult
    {
        bool              ok        = false;
        QString           errorMsg;
        mesh::MeshResult  meshResult;
        mesh::CouplingMap coupling;
        QString           meshPath;
        mesh::MeshOutputMode outputMode = mesh::MeshOutputMode::External;
    };

    explicit MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                  QWidget *parent = nullptr);
    ~MeshGenerationDialog() override;

private slots:
    void onBrowseMeshPath();
    void onAccept();
    void onMeshFinished();
    /*! Handles both "Cancel" (no job running → close dialog) and
     *  "Cancel Generation" (job running → stop worker). */
    void onCancelOrReject();

private:
    void buildUi();
    void seedDefaults();
    void populateLayerCombos();
    void updateUnitDisplay();
    void updateZFactor();   // recomputes m_zFactorSpin from DTM + mesh vertical unit combos
    /*! Refreshes the read-only "min triangle area / max vertex shift" line
     *  under the Minimum Cell Size group. */
    void updateMinCellDerivedLabel();

    /*! Collect all inputs from widgets + SWMMModelLayer on the main thread.
     *  Returns false and sets *errOut on any early-out condition (no project,
     *  no extent, etc.).  Does NOT start the worker. */
    bool collectInputs(PipelineInputs *out, QString *errOut) const;

    SWMMVisProjectWindow *m_pw = nullptr;

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

    // ── Quality ─────────────────────────────────────────────────────
    QDoubleSpinBox *m_maxAreaSpin      = nullptr;
    QDoubleSpinBox *m_minAngleSpin     = nullptr;
    QSpinBox       *m_maxSteinerSpin   = nullptr;
    // PSLG optimizations
    QDoubleSpinBox *m_simplifyEpsSpin  = nullptr; ///< RDP tolerance (map units; 0 = off)
    QDoubleSpinBox *m_snapEpsSpin      = nullptr; ///< Steiner snap radius (map units; 0 = off)
    QCheckBox      *m_allowSteiner   = nullptr;
    // 2026-07-19 — optional boundary densification (edge split after RDP).
    QCheckBox      *m_maxBoundaryEdgeBox  = nullptr;
    QDoubleSpinBox *m_maxBoundaryEdgeSpin = nullptr; ///< split length (map units; (off) at 0)

    // ── Minimum cell size (MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17) ──
    QDoubleSpinBox *m_minCellSizeSpin      = nullptr; ///< h, map units; (off) at 0
    QPushButton    *m_minCellSuggestBtn    = nullptr;
    QDoubleSpinBox *m_trimAngleSpin        = nullptr; ///< corner trim threshold (deg)
    QCheckBox      *m_trimAtNodesBox       = nullptr;
    QCheckBox      *m_dropSubScaleHolesBox = nullptr;
    QCheckBox      *m_cleanupBox           = nullptr; ///< post-mesh sliver collapse
    QLabel         *m_minCellDerivedLabel  = nullptr; ///< derived area / shift readout

    // ── Thinning (terrain-adaptive Steiner points from DTM) ─────────
    QCheckBox      *m_thinningBox            = nullptr;
    QDoubleSpinBox *m_thinningToleranceSpin  = nullptr;
    QSpinBox       *m_thinningIterationsSpin = nullptr;
    QSpinBox       *m_thinningMaxPointsSpin  = nullptr;
    QCheckBox      *m_minSpacingBox  = nullptr;
    QDoubleSpinBox *m_minSpacingSpin = nullptr;
    // 2026-07-19 — boundary-aware terrain filter buffer ((auto) at 0).
    QDoubleSpinBox *m_boundaryBufferSpin = nullptr;

    // ── Uniform per-cell hydraulic seeds ────────────────────────────
    QDoubleSpinBox *m_manningsValueSpin  = nullptr;
    QDoubleSpinBox *m_initDepthSpin      = nullptr;

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
