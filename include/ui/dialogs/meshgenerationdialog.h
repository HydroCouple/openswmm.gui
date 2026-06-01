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

        // PSLG: pre-extracted from the SWMM model and aux layers
        QVector<mesh::SteinerPoint>      steinerPoints;
        QVector<mesh::ConstraintSegment> constraintSegs;
        QVector<mesh::RegionMarker>      regionMarkers;

        // Coupling-map lookup tables
        QHash<int, QString> nodeMarkerToTag;
        QHash<int, QString> edgeMarkerToTag;

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

        // Mesh-quality knobs
        mesh::GenerationOptions genOpts;

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

        // Elevation interpolation for the no-DTM fallback.  IDW (configurable
        // Shepard power) or natural neighbour (Sibson / Laplace); NN falls back
        // to IDW outside the seed convex hull.  Ignored when a DTM is set.
        ElevInterpMethod elevInterpMethod = ElevInterpMethod::IDW;
        NNVariant        nnVariant        = NNVariant::Sibson;
        double           idwPower         = 2.0;

        // Output
        mesh::MeshOutputMode outputMode    = mesh::MeshOutputMode::External;
        QString              meshOutputPath;
        double               manningsN     = 0.035;
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
    QCheckBox      *m_nodesUseRim     = nullptr;  // rim elevation instead of terrain
    QDoubleSpinBox *m_nodeFlattenSpin = nullptr;  // flatten terrain within radius of nodes

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

    // ── Thinning (terrain-adaptive Steiner points from DTM) ─────────
    QCheckBox      *m_thinningBox            = nullptr;
    QDoubleSpinBox *m_thinningToleranceSpin  = nullptr;
    QSpinBox       *m_thinningIterationsSpin = nullptr;
    QSpinBox       *m_thinningMaxPointsSpin  = nullptr;
    QCheckBox      *m_minSpacingBox  = nullptr;
    QDoubleSpinBox *m_minSpacingSpin = nullptr;

    // ── Roughness / Manning's ───────────────────────────────────────
    QRadioButton  *m_manningsConstant    = nullptr;
    QRadioButton  *m_manningsCategorical = nullptr;
    QRadioButton  *m_manningsField       = nullptr;
    QDoubleSpinBox *m_manningsValueSpin  = nullptr;

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
