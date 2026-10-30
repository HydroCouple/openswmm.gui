/**
 * @file      swmm2dmeshlayer.h
 * @brief     renders a generated 2D triangular mesh on the canvas.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AU.6 — renders a generated 2D triangular mesh on the canvas.
 *
 * Rendering is handled entirely by SWMM2DMeshQSGRenderer (a QQuickItem
 * inside MapCanvas's QQuickWidget), which replaces the former per-triangle
 * QPainter path.  All scene-space geometry is pre-computed into the public
 * m_scene* caches by rebuildSceneGeometry(); the renderer reads them directly.
 *
 * Elevation visualisation:
 *  - Filled triangles: terrain colour ramp (deep-blue → near-white) ×
 *    slope-based hillshade.
 *  - Edges: colour-mapped by slope; steep breaks rendered wider.
 *  - Nodes: SWMM-coupled (tagged) vertices; off by default (setShowMeshNodes).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H

#include "layers/meshspatialgrid.h"
#include "layers/openswmmvislayer.h"
#include "mesh/meshboundarygraph.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include "render/isublayerhost.h"

#include <QByteArray>
#include <QColor>
#include <QLineF>
#include <QPair>
#include <QPointer>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include <ogr_spatialref.h>

#include <memory>

class QGraphicsScene;
class QGraphicsItem;
class SWMM2DMeshGraphicsItem;
class GeneratedMeshArtifacts;
class SWMMModelLayer;

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class RuleList; // Slice B.5b — see ruleList() override below.
class MeshFillSublayer;
class MeshEdgeSublayer;
class MeshNodeSublayer;
class ContourBandSublayer;
class IsolineSublayer;
class MeshBcSublayer;
class CoupledNodeSublayer;
}

class SWMM2DMeshLayer : public OpenSWMMVisLayer, public OpenSWMM::Render::ISublayerHost
{
    Q_OBJECT

    // Source path + mesh-size metadata for the Properties window. The mesh
    // is immutable after construction so vertex / triangle counts are CONSTANT.
    Q_PROPERTY(QString sourcePath READ sourcePath CONSTANT)
    Q_PROPERTY(int triangleCount READ triangleCount CONSTANT)
    Q_PROPERTY(int vertexCount READ vertexCount CONSTANT)

    // Slice U-5 — Q_PROPERTYs for the unified LayerStyleDialog. Setters
    // already emit repaintRequested() which the dialog treats as NOTIFY.
    Q_PROPERTY(bool showMeshNodes READ showMeshNodes WRITE setShowMeshNodes NOTIFY repaintRequested)
    Q_PROPERTY(bool showEdges READ showEdges WRITE setShowEdges NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeAzimuth READ hillshadeAzimuth WRITE setHillshadeAzimuth NOTIFY
                   repaintRequested)
    Q_PROPERTY(double hillshadeAltitude READ hillshadeAltitude WRITE setHillshadeAltitude NOTIFY
                   repaintRequested)
    Q_PROPERTY(
        double hillshadeZExag READ hillshadeZExag WRITE setHillshadeZExag NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeMinLit READ hillshadeMinLit WRITE setHillshadeMinLit NOTIFY
                   repaintRequested)
    Q_PROPERTY(bool showContours READ showContours WRITE setShowContours NOTIFY repaintRequested)
    Q_PROPERTY(int contourIntervalCount READ contourIntervalCount WRITE setContourIntervalCount
                   NOTIFY repaintRequested)
    Q_PROPERTY(QColor contourColor READ contourColor WRITE setContourColor NOTIFY repaintRequested)
    Q_PROPERTY(double contourLineWidth READ contourLineWidth WRITE setContourLineWidth NOTIFY
                   repaintRequested)
    Q_PROPERTY(
        bool filledContours READ filledContours WRITE setFilledContours NOTIFY repaintRequested)
    Q_PROPERTY(double filledContoursOpacity READ filledContoursOpacity WRITE
                   setFilledContoursOpacity NOTIFY repaintRequested)

    // Zoom at which the wireframe / vertex dots auto-appear, as the minimum
    // on-screen cell size (pixels across). Larger ⇒ must zoom in closer.
    Q_PROPERTY(double edgeZoomMinCellPx READ edgeZoomMinCellPx WRITE setEdgeZoomMinCellPx NOTIFY
                   repaintRequested)
    Q_PROPERTY(double vertexZoomMinCellPx READ vertexZoomMinCellPx WRITE setVertexZoomMinCellPx
                   NOTIFY repaintRequested)

    Q_CLASSINFO("group:sourcePath", "Source")
    Q_CLASSINFO("group:triangleCount", "Mesh")
    Q_CLASSINFO("group:vertexCount", "Mesh")
    Q_CLASSINFO("group:showMeshNodes", "Display")
    Q_CLASSINFO("group:showEdges", "Display")
    Q_CLASSINFO("group:edgeZoomMinCellPx", "Display")
    Q_CLASSINFO("group:vertexZoomMinCellPx", "Display")
    Q_CLASSINFO("group:hillshadeAzimuth", "Hillshade")
    Q_CLASSINFO("group:hillshadeAltitude", "Hillshade")
    Q_CLASSINFO("group:hillshadeZExag", "Hillshade")
    Q_CLASSINFO("group:hillshadeMinLit", "Hillshade")
    Q_CLASSINFO("group:showContours", "Contours")
    Q_CLASSINFO("group:contourIntervalCount", "Contours")
    Q_CLASSINFO("group:contourColor", "Contours")
    Q_CLASSINFO("group:contourLineWidth", "Contours")
    Q_CLASSINFO("group:filledContours", "Contours")
    Q_CLASSINFO("group:filledContoursOpacity", "Contours")

public:
    /*! \param deferHeavyGeometry Progressive-load mode (Mesh Tiled LOD P1.2):
     *  the constructor builds only what the fill needs to draw — scene
     *  triangles, bbox, elevation range, vertex dots and the LOD pyramid —
     *  so a large mesh can join the canvas and render its coarse levels
     *  immediately. The heavy structures (deduplicated wireframe edges,
     *  spatial grids, vertex adjacency, BC slots) arrive later via
     *  finishSceneGeometryAsync().
     * @param[in] result Mesh copied/moved into owned state, with finite practical coordinates/elevations, valid triangle/quad connectivity and compatible unit conventions. Source file is not opened here.
     * @param[in] sourcePath Provenance/reference namespace copied as provided; empty identifies generated mesh.
     * @param[in] parent Optional workspace QObject owner.
     * @note Creates seven owned sublayers and a placeholder SingleSymbol renderer, seeds BC preferences and live preference connections, computes source extent and full/light scene caches. Vertex markers default visible with zoom gating. State/cache/scene operations are GUI-thread serialized; renderer readers must see stable snapshots. Geometry remains in native map units with explicit scene-Y inversion; no CRS is inferred from path.
     */
    explicit SWMM2DMeshLayer(mesh::MeshResult result, const QString &sourcePath = {},
                             OpenSWMMVisWorkspace *parent = nullptr,
                             bool deferHeavyGeometry = false);
    /**
     * @brief Destroy owned coordinate transform and QObject children.
     * @note Remove CPU graphics item before layer destruction because it borrows this layer; async geometry workers operate copied value snapshots and owner-bound completion is disconnected on destruction.
     */
    ~SWMM2DMeshLayer() override;

    /*! \brief Build the deferred heavy geometry (wireframe edges, spatial
     *  grids, vertex adjacency, BC slots) on a background thread and adopt
     *  it on the GUI thread. Emits sceneGeometryReady() then
     *  repaintRequested() when done. No-op when already complete or a build
     *  is in flight. If the scene geometry was re-projected mid-build (CRS
     *  change), the stale result is discarded and the build re-runs.
     * @note Captures mesh/scene geometry and revision; worker computes edges/bounds/grids/adjacency/boundary flags/default Wall slots without live state access. Success retains existing correctly sized BC array, adopts products, refreshes legends and emits ready/repaint without bumping geometry revision.
     * @note std::exception failure logs, clears running flag and leaves limited geometry for explicit retry. Stale revision restarts only while incomplete; GUI-thread entry/adoption, no cancellation/error signal.
     */
    void finishSceneGeometryAsync();

    /*! \brief False while the heavy structures from a deferred load are
     *  still being built — the renderers fall back to the LOD pyramid and
     *  editing/picking is limited until this flips true.
     * @return Stored heavy-build readiness, true for full/empty construction and false during progressive geometry preparation; does not block for completion.
     */
    [[nodiscard]] bool sceneGeometryComplete() const { return m_sceneGeomComplete; }

    /**
     * @return Absolute filesystem interpretation of provenance, or empty for generated mesh; no existence/canonicalization check.
     */
    [[nodiscard]] QString sourcePath() const;
    /**
     * @param[in] path Raw provenance/reference-key text; no normalization, file access, renaming, signal or cache rebuild.
     */
    void setSourcePath(const QString &path) { m_sourcePath = path; }

    /*! \brief True when the mesh came from an external file referenced by
     *  `[2D_MESH_FILE] FILE <path>`. False for an inline mesh, whose
     *  sourcePath() is the .inp itself. Save must not re-point the .inp at
     *  an inline mesh's "source" — that would overwrite the model.
     * @return Stored source-section provenance flag, initially false; does not inspect file or topology.
     */
    [[nodiscard]] bool isExternalMesh() const { return m_isExternal; }
    /**
     * @param[in] external External-file flag copied without signals or source conversion.
     */
    void setExternalMesh(bool external) { m_isExternal = external; }

    /*! \brief True when the mesh file declared `;; UNITS: SI (m)` (or an
     *  equivalent metric keyword). The engine then SKIPS its FLOW_UNITS-based
     *  ft→m mesh scaling, so 2D results coordinates come back in the same
     *  values as the mesh. Consumed by the 2D results layer's coordinate
     *  fallback — see SWMM2DResultsLayer::setFallbackCoordinateScale and
     *  issue #155.
     * @return Stored explicit-SI marker, initially false; flags output-coordinate fallback policy without converting mesh values.
     */
    [[nodiscard]] bool meshUnitsSI() const { return m_meshUnitsSI; }
    /**
     * @param[in] si Explicit-SI provenance flag copied without converting coordinates/elevations or notifying results layers.
     */
    void setMeshUnitsSI(bool si) { m_meshUnitsSI = si; }

    /*! Number of cells (triangles + quads) in the loaded mesh — exposed as
     *  metadata in the Properties window. Name kept for the Q_PROPERTY /
     *  saved-project compatibility.
     * @return Number of original cells (triangles and quads), narrowed to int, not number of display fan triangles.
     */
    [[nodiscard]] int triangleCount() const { return int(m_mesh.triangles.size()); }

    /*! Number of quadrilateral cells (O(n)).
     * @return Number of original quadrilateral cells, scanning the mesh.
     */
    [[nodiscard]] int quadCount() const { return m_mesh.quadCount(); }

    /*! Number of vertices in the loaded mesh — exposed as metadata in
     *  the Properties window.
     * @return Number of original mesh vertices, narrowed to int.
     */
    [[nodiscard]] int vertexCount() const { return int(m_mesh.vertices.size()); }

    /*! Number of boundary edges (domain outline + holes).
     * @return Number of stored boundary-marker rows, not a recomputed unique topological boundary count.
     */
    [[nodiscard]] int boundaryEdgeCount() const { return int(m_mesh.boundaryEdges.size()); }

    /*! Total number of unique mesh edges. m_sceneEdges is the deduplicated
     *  edge set (exact, conformance-independent) built by
     *  rebuildSceneGeometry(); before that runs, fall back to the conforming-
     *  mesh identity total = (Σ nv + B)/2 = (3·T + Q + B)/2.
     * @return Built deduplicated edge count when nonempty; otherwise conforming-mesh identity fallback(3*cellCount+quadCount+boundaryRows)/2, which can be approximate before heavy build/nonconforming input.
     */
    [[nodiscard]] int edgeCount() const
    {
        if (!m_sceneEdges.isEmpty())
            return int(m_sceneEdges.size());
        return (3 * triangleCount() + quadCount() + boundaryEdgeCount()) / 2;
    }

    /*! Steepest edge slope in the mesh (rise/run), cached during scene build.
     * @return Cached maximum absolute native bed rise/native horizontal run over unique edges, zero before heavy build; incremental elevation edits may leave it stale until full rebuild.
     */
    [[nodiscard]] double maxSlope() const { return double(m_maxSlope); }

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Absolute provenance path or generated-mesh placeholder, without IO.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned localized counts/area/quad-quality/elevation/slope/coupling/extent descriptions. Computes cell/quad statistics synchronously; numeric geometry units remain native mesh units.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    /**
     * @return Stored active/edit-target flag, initially false; it changes visual emphasis, not topology/engine ownership.
     */
    [[nodiscard]] bool isActiveMesh() const { return m_active; }
    /**
     * @param[in] active New edit-target/emphasis flag.
     * @note Equality is a no-op; emits activeMeshChanged()/repaintRequested() on change without engine binding/save effects.
     */
    void setActiveMesh(bool active);

    // ----- QSG ownership (Mesh Tiled LOD plan P1.1) -------------------------
    /*! True while SWMM2DMeshQSGRenderer draws this layer inside MapCanvas's
     *  QSG frame; the CPU SWMM2DMeshGraphicsItem::paint() early-returns so
     *  the two pipelines never double-paint. Mirrors
     *  SWMM2DResultsLayer::qsgOwnsRendering.
     * @return Whether CPU painting defers to Qt Quick, initially false; not a renderer-pointer lifetime guarantee.
     */
    [[nodiscard]] bool qsgOwnsRendering() const noexcept { return m_qsgOwnsRendering; }
    /**
     * @param[in] own Whether the CPU item must skip painting.
     * @note Equality is a no-op; change notifies existing item geometry/update and emits repaintRequested() so both rendering pipelines refresh.
     */
    void setQsgOwnsRendering(bool own);

    /**
     * @param[in] source Stable borrowed mesh layer whose name/visibility/opacity/renderer clone/sublayer JSON/zoom thresholds/light policy are copied.
     * @note Uses ordinary setters and JSON loader, so multiple invalidation/repaint signals can fire. Does not copy geometry, source path/CRS, ownership/edit state, selections, generated artifacts or active target flag; no transactional rollback.
     */
    void copyDisplayStateFrom(const SWMM2DMeshLayer &source);

    // ----- Display toggles ------------------------------------------------
    // These remain on the layer so existing UI, JSON, and serialization
    // round-trip the way they always have, but they are now thin shims
    // that forward to the per-sublayer visibility flag.
    /**
     * @return Base vertex marker visibility request, still zoom-gated. False when corresponding sublayer absent.
     */
    [[nodiscard]] bool showMeshNodes() const;
    /**
     * @param[in] show Base vertex marker visibility request, still zoom-gated.
     * @note Forwards to sublayer setVisible(), whose effective change invalidates/request repaint through constructor wiring; no geometry/source edit.
     */
    void setShowMeshNodes(bool show);

    /**
     * @return Wireframe visibility request, still zoom/LOD-gated. False when corresponding sublayer absent.
     */
    [[nodiscard]] bool showEdges() const;
    /**
     * @param[in] show Wireframe visibility request, still zoom/LOD-gated.
     * @note Forwards to sublayer setVisible(), whose effective change invalidates/request repaint through constructor wiring; no geometry/source edit.
     */
    void setShowEdges(bool show);

    // ----- Zoom thresholds for edge / vertex auto-display -----------------
    // Both renderers suppress the wireframe / vertex-dot passes until the
    // mean on-screen cell size reaches these values (pixels across), so the
    // detail appears automatically as the user zooms in and never clutters
    // the far view. Configurable per layer; defaults reproduce the historic
    // LOD gates (√kEdgeMinCellAreaPx ≈ 5.66 px, √kMarkerMinCellAreaPx ≈
    // 14.14 px). 0 ⇒ always draw (when the sublayer is visible).
    /**
     * @return Wireframe minimum mean projected cell width in logical pixels, initially3.
     */
    [[nodiscard]] double edgeZoomMinCellPx() const { return m_edgeMinCellPx; }
    /**
     * @param[in] px Finite projected mean-cell size threshold, clamped0..500logical pixels; zero allows always-on subject to visibility/LOD.
     * @note Fuzzy equality after normalization is a no-op; otherwise stores and requests repaint without geometry rebuild.
     */
    void setEdgeZoomMinCellPx(double px);
    /**
     * @return Vertex/coupled-marker minimum mean projected cell width, initially6logical pixels.
     */
    [[nodiscard]] double vertexZoomMinCellPx() const { return m_vertexMinCellPx; }
    /**
     * @param[in] px Finite projected mean-cell size threshold, clamped0..500logical pixels; zero allows always-on subject to visibility/LOD.
     * @note Fuzzy equality after normalization is a no-op; otherwise stores and requests repaint without geometry rebuild.
     */
    void setVertexZoomMinCellPx(double px);

    /*! Area forms (px²) the renderers actually gate on — square of the
     *  configured cell-size thresholds.
     * @return Square of wireframe width threshold, in logical pixels squared.
     */
    [[nodiscard]] double edgeMinCellAreaPx() const { return m_edgeMinCellPx * m_edgeMinCellPx; }
    /**
     * @return Square of vertex width threshold, in logical pixels squared.
     */
    [[nodiscard]] double vertexMinCellAreaPx() const
    {
        return m_vertexMinCellPx * m_vertexMinCellPx;
    }

    // ----- Hillshade ------------------------------------------------------
    // azimuth/altitude stay on the layer because they describe the light
    // source, which is layer-wide (not per-sublayer). zExag and minLit
    // map to the mesh-fill sublayer style's hillshadeStrength.
    /**
     * @return Stored compass light azimuth degrees, initially225.
     */
    [[nodiscard]] double hillshadeAzimuth() const { return m_hillshadeAz; }
    /**
     * @return Stored light elevation degrees, initially35.264.
     */
    [[nodiscard]] double hillshadeAltitude() const { return m_hillshadeAlt; }
    /**
     * @return Fill-style hillshadeStrength times ten (normally0..10), or fallback3; shader vertical gradient exaggeration.
     */
    [[nodiscard]] double hillshadeZExag() const;
    /**
     * @return Stored dimensionless shadow floor, initially0.15.
     */
    [[nodiscard]] double hillshadeMinLit() const;
    /**
     * @param[in] degrees Finite practical compass angle, wrapped by repeated360degree additions/subtractions into[0,360). Do not supply infinity, which cannot terminate those loops.
     * @note Fuzzy equality is a no-op; change stores/repaints only. No vertical-unit conversion or geometry mutation.
     */
    void setHillshadeAzimuth(double degrees);
    /**
     * @param[in] degrees Finite elevation angle clamped0..90degrees.
     * @note Fuzzy equality is a no-op; change requests repaint without geometry rebuild.
     */
    void setHillshadeAltitude(double degrees);
    /**
     * @param[in] factor Finite requested exaggeration, first clamped0..100 then divided by ten into fill-style hillshadeStrength, whose setter clamps0..1; effective returned exaggeration is therefore at most10.
     * @note Delegated style invalidation requests repaint on effective change; missing style directly requests repaint but retains no fallback value.
     */
    void setHillshadeZExag(double factor);
    /**
     * @param[in] minLit Finite dimensionless shadow floor clamped0..1.
     * @note Fuzzy equality is a no-op; stores per-layer field and requests repaint, independent of fill-style strength.
     */
    void setHillshadeMinLit(double minLit);

    // ----- Bed-elevation contours -----------------------------------------
    // Forward to the isoline + contour-band sublayers; the layer Q_PROPERTYs
    // exist for backwards compatibility with .oswp files written before the
    // sublayer refactor.
    /**
     * @return Isoline sublayer visibility, independent of filled bands. False when corresponding sublayer absent.
     */
    [[nodiscard]] bool showContours() const;
    /**
     * @return Isoline scheme class count (not guaranteed number of drawn lines), fallback10.
     */
    [[nodiscard]] int contourIntervalCount() const;
    /**
     * @return Isoline colour copy, fallback dark grey alpha200.
     */
    [[nodiscard]] QColor contourColor() const;
    /**
     * @return Isoline logical-pixel width, fallback1.
     */
    [[nodiscard]] double contourLineWidth() const;
    /**
     * @param[in] show Isoline sublayer visibility, independent of filled bands.
     * @note Forwards to sublayer setVisible(), whose effective change invalidates/request repaint through constructor wiring; no geometry/source edit.
     */
    void setShowContours(bool show);
    /**
     * @param[in] n Desired count clamped1..200, sent to both isoline isoValueCount and contour-band bandCount schemes.
     * @note Each effective style change invalidates/repaints; one call may notify twice. Does not enable either sublayer or author manual levels.
     */
    void setContourIntervalCount(int n);
    /**
     * @param[in] c Valid QColor copied to isoline style; invalid colours ignored.
     * @note Delegated style setter suppresses equal values and forwards repaint on change; filled-band colours remain independent.
     */
    void setContourColor(const QColor &c);
    /**
     * @param[in] widthPx Finite logical-pixel stroke width clamped0.25..10.
     * @note Delegates isoline style setter/invalidation, without enabling contours or modifying band opacity.
     */
    void setContourLineWidth(double widthPx);

    /**
     * @return Filled contour-band visibility, independent of isolines. False when corresponding sublayer absent.
     */
    [[nodiscard]] bool filledContours() const;
    /**
     * @return Filled-band sublayer opacity, fallback0.55.
     */
    [[nodiscard]] double filledContoursOpacity() const;
    /**
     * @param[in] filled Filled contour-band visibility, independent of isolines.
     * @note Forwards to sublayer setVisible(), whose effective change invalidates/request repaint through constructor wiring; no geometry/source edit.
     */
    void setFilledContours(bool filled);
    /**
     * @param[in] a Finite opacity clamped0..1 then forwarded to band sublayer.
     * @note Effective sublayer change requests repaint; no fill visibility/geometry mutation.
     */
    void setFilledContoursOpacity(double a);

    /**
     * @return Borrowed owned mesh state, valid for layer lifetime but modified by apply/ownership methods. Retain a value copy for an immutable worker snapshot.
     */
    [[nodiscard]] const mesh::MeshResult &mesh() const { return m_mesh; }

    /**
     * @return Current geometry-cache revision; may increment more than once for a single vertex edit and is not a count of user operations.
     */
    [[nodiscard]] quint64 geomRevision() const { return m_geomRevision; }

    /*! \brief Has this layer's mesh state diverged from the engine's copy?
     *
     *  The save path pushes the whole mesh into the engine, which costs
     *  O(nVertices x nTriangles) there (each swmm_2d_set_vertex_z rescans
     *  every triangle). That is minutes on a million-cell mesh, so a save
     *  must not pay it when nothing was edited.
     *
     *  Defaults to **true**: only a layer built from the very file the engine
     *  parsed is known to agree with it, and that case says so explicitly via
     *  SWMMVisProjectWindow::attachMeshLayer(layer, pristine = true). A mesh
     *  generated or imported in-session leaves the engine holding the OLD
     *  mesh, so it must stay dirty.
     * @return Explicit layer-versus-engine divergence flag, initially true; this accessor does not compare source data.
     */
    [[nodiscard]] bool hasUnsavedMeshEdits() const { return m_editsDirty; }

    /*! \brief Mark the layer's mesh state as diverged from the engine.
     * @brief Set divergence flag true without engine comparison, revision update or notification.
     */
    void markMeshEdited() { m_editsDirty = true; }

    /*! \brief Clear the divergence flag — only after a confirmed successful
     *  write, so a failed save still re-pushes next time.
     * @brief Clear divergence flag explicitly after caller-confirmed successful write; performs no persistence itself.
     */
    void setMeshEditsSaved() { m_editsDirty = false; }

    // Generated topology is owned by this layer until a model reload. Save
    // clears dirty state, but the engine can still contain the old topology.
    /**
     * @return Stored topology-ownership/save policy, independent of dirty flag.
     */
    [[nodiscard]] bool ownsGeneratedTopology() const { return m_ownsGeneratedTopology; }
    /**
     * @param[in] owns Topology-ownership flag copied silently; does not change dirty state or engine mesh.
     */
    void setOwnsGeneratedTopology(bool owns) { m_ownsGeneratedTopology = owns; }

    // Imported source sections belong to its topology and must survive patching.
    /**
     * @return Stored policy to preserve source sections when patching imported topology.
     */
    [[nodiscard]] bool preservesImportedSections() const { return m_preservesImportedSections; }
    /**
     * @param[in] preserve Imported-section policy copied without validation/notifications.
     */
    void setPreservesImportedSections(bool preserve) { m_preservesImportedSections = preserve; }
    /**
     * @return Stored save-as-rebase policy for imported source references.
     */
    [[nodiscard]] bool importNeedsSaveAsRebase() const { return m_importNeedsSaveAsRebase; }
    /**
     * @param[in] rebase Save-as path policy copied without IO/notifications.
     */
    void setImportNeedsSaveAsRebase(bool rebase) { m_importNeedsSaveAsRebase = rebase; }

    /**
     * @return Borrowed shared-pointer holder for generation evidence, possibly empty; copy the shared_ptr to retain ownership beyond layer lifetime/replacement.
     */
    const std::shared_ptr<GeneratedMeshArtifacts> &generatedArtifacts() const
    {
        return m_generatedArtifacts;
    }
    /**
     * @param[in] artifacts Shared generation evidence moved into the layer, replacing/releasing previous ownership; null clears. No signals or geometry mutation.
     */
    void setGeneratedArtifacts(std::shared_ptr<GeneratedMeshArtifacts> artifacts)
    {
        m_generatedArtifacts = std::move(artifacts);
    }

    // ---------------------------------------------------------------------
    // Slice §V.VA — mesh-editing foundation: BC storage, picker / hover
    // helpers, write-path apply* family, attributeChanged signal.
    //
    // The layer is the model in the §V MVC; all views (Mesh Editing
    // toolbar, future Property Browser tab, Attribute Table, map renderer)
    // subscribe to attributeChanged and write through the apply* helpers.
    // ---------------------------------------------------------------------

    /*! \brief Per-edge BC values; flat-indexed by `mesh::edgeSlot(cell,
     *  edgeLocal)` (stride mesh::kEdgeStride = 4). Sized to
     *  `mesh::edgeSlotCount(n_cells)` after mesh load; interior-edge slots
     *  and a triangle's unused slot 3 stay at the default Wall value
     *  (engine ignores them).
     * @return Borrowed per-cell stride-four BC slots, possibly not initialized in progressive mode; references/elements change on edits/resize. Layer retains ownership.
     */
    [[nodiscard]] const QVector<mesh::MeshEdgeBC> &edgeBCs() const { return m_bc; }

    /*! \brief Mutable BC view — used by INP reader to bulk-populate after
     *  load. Prefer the apply* helpers for user-driven edits so views
     *  receive the attributeChanged signal. Bumps the BC revision on
     *  every access: both callers assign the whole vector through this
     *  reference, and without the bump the bcSceneEdges() render cache
     *  keyed on m_bcRevision served stale geometry after a bulk reload.
     * @return Borrowed mutable stride-four array; caller must preserve slot/index invariants and avoid retaining through resize/destruction.
     * @note Every access bumps BC revision even without mutation. Does not refresh legends, emit attribute/repaint, validate values or mark dirty. Prefer apply methods for user edits.
     */
    QVector<mesh::MeshEdgeBC> &edgeBCsMutable()
    {
        ++m_bcRevision;
        return m_bc;
    }

    /*! \brief Vertex pick. Returns the closest vertex index to scene
     *  point (sx,sy) within \p tolPx screen-pixels, or -1 if none.
     *  \p pxPerSceneUnit converts scene-units to screen-pixels (use the
     *  active canvas zoom).
     * @param[in] sx,sy Finite scene-map coordinates after canvas reprojection and Y inversion.
     * @param[in] tolPx Nonnegative finite screen-distance tolerance in logical pixels.
     * @param[in] pxPerSceneUnit Positive finite isotropic canvas zoom converting scene units to pixels.
     * @return Nearest original vertex; equal-distance ties favour later vertex. Minus one for empty geometry/nonpositive zoom/no match. Full linear scan; no visibility/selection filter or grid acceleration.
     */
    [[nodiscard]] int pickVertexAt(double sx, double sy, double tolPx, double pxPerSceneUnit) const;

    /*! \brief Edge pick. Returns flat edge slot `mesh::edgeSlot(cell,
     *  edgeLocal)` within \p tolPx of (sx,sy), or -1. When \p boundaryOnly
     *  is true, interior edges are skipped.
     * @param[in] sx,sy Finite scene-map coordinates after canvas reprojection and Y inversion.
     * @param[in] tolPx Nonnegative finite screen-distance tolerance in logical pixels.
     * @param[in] pxPerSceneUnit Positive finite isotropic canvas zoom converting scene units to pixels.
     * @param[in] boundaryOnly Whether to restrict to cached boundary flags; incomplete progressive boundary data can return no edges.
     * @return Nearest stride-four original edge slot; equal-distance ties favour later cell/local slot, so an interior edge can return either half. Minus one for empty geometry/nonpositive zoom/no match. Full linear scan; no visibility/selection filter or grid acceleration.
     */
    [[nodiscard]] int pickEdgeAt(double sx, double sy, double tolPx, double pxPerSceneUnit,
                                 bool boundaryOnly) const;

    /*! \brief Cell (mesh triangle/quad index) containing scene point
     *  (sx,sy), or -1 if outside every cell. A quad is tested on its
     *  sub-triangle fan.
     * @param[in] sx,sy Finite scene-map point in current cached geometry frame.
     * @return Original cell index from first containing display fan triangle. Minus one outside/degenerate; exact bbox then barycentric>=-1e-9 test, grid-candidate fast path when available, otherwise linear scan. Boundary ties follow candidate/fan order. No mutation except read-only spatial candidates.
     */
    [[nodiscard]] int locateTriangleAt(double sx, double sy) const;

    /*! \brief Triangle (cell) containing scene point \p scenePt, or -1.
     *  Peer of `SWMM2DResultsLayer::pickCellAt`; lets the Select 2D Cells
     *  map tool work against the mesh layer when no results layer exists.
     * @param[in] scenePt Finite scene-map point after transform/Y inversion.
     * @return locateTriangleAt() original cell index or minus one, without checking layer visibility.
     */
    [[nodiscard]] int pickCellAt(const QPointF &scenePt) const;

    /*! \brief Triangle indices whose centroid falls inside \p sceneRect.
     *  Peer of `SWMM2DResultsLayer::pickCellsInRect`.
     * @param[in] sceneRect Finite scene-map rectangle, null gives empty.
     * @return Owned original cell indices in mesh order whose display-fan area centroid lies inside. Skips empty fans; tests centroid rather than any geometry intersection. Quads use area-weighted fan centroid, degenerate fan falls back to first triangle centroid.
     */
    [[nodiscard]] QVector<int> pickCellsInRect(const QRectF &sceneRect) const;

    /*! \brief Triangle indices whose centroid falls inside \p scenePoly.
     *  Peer of `SWMM2DResultsLayer::pickCellsInPolygon`.
     * @param[in] scenePoly Finite scene-map polygon; fewer than three vertices gives empty, OddEvenFill containment.
     * @return Owned original cell indices in mesh order whose display-fan area centroid lies inside. Skips empty fans; tests centroid rather than any geometry intersection. Quads use area-weighted fan centroid, degenerate fan falls back to first triangle centroid.
     */
    [[nodiscard]] QVector<int> pickCellsInPolygon(const QPolygonF &scenePoly) const;

    /*! \brief Barycentric Z sample at scene point (sx,sy). Returns NaN
     *  when the point is outside every triangle.
     * @param[in] sx,sy Finite scene-map sample point.
     * @return Piecewise-planar native bed elevation from first containing display subtriangle's float corner heights; NaN outside/zero determinant. Quad follows current VFR split, never bilinear. No vertical-unit/datum conversion; near-boundary barycentric tolerance may slightly extrapolate.
     */
    [[nodiscard]] double sampleZAt(double sx, double sy) const;

    /*! \brief True when this edge slot is a boundary edge in the loaded
     *  mesh (i.e. the corresponding triangle has no neighbour across it).
     *  Boundary classification is derived from the mesh's MeshEdge list
     *  populated by InpMeshReader; non-boundary edges return false.
     * @param[in] triIdx Original zero-based cell index.
     * @param[in] edgeLocal Local edge ordinal, triangle0..2/quad0..3 under mesh convention.
     * @return Cached flag or false out-of-range/unbuilt/padding. Build flags mark single-use topology edges OR explicit boundary-marker pairs, even when a marker pair is shared by cells.
     */
    [[nodiscard]] bool isBoundaryEdge(int triIdx, int edgeLocal) const;

    /*! \brief Boundary-edge connectivity graph, used by the edge-select
     *  tool's Ctrl-click shortest-path picking. Built lazily on first use
     *  and cached until the boundary flags are rebuilt.
     *
     *  Returns an empty graph while a progressive load is still finishing
     *  (`sceneGeometryComplete() == false`), since the boundary flags are
     *  not populated yet; the next call after `sceneGeometryReady()`
     *  builds it for real.
     * @return Borrowed lazily built boundary graph, invalidated/replaced when flags rebuild or layer dies. Progressive incomplete state returns existing empty graph without marking cache valid.
     * @note GUI-thread mutable lazy access; no graph ownership transfer or synchronization.
     */
    [[nodiscard]] const mesh::MeshBoundaryGraph &boundaryGraph();

    /*! Engine §11A helper — find the (tri, eLocal) slot on the other side
     *  of an interior edge. Returns `(-1, -1)` when the edge is on the
     *  mesh boundary (no neighbour) or the indices are out of range.
     *  Uses the vertex→triangles adjacency, so requires
     *  `rebuildVertexAdjacency()` to have run.
     *
     *  Public because the Attribute Table's Edges view canonicalises each
     *  interior edge pair onto a single row and needs the same pairing the
     *  conveyance mirror uses.
     * @param[in] triIdx Original cell index.
     * @param[in] edgeLocal Valid local edge ordinal.
     * @return First other incident cell/local edge sharing both endpoints, or(-1,-1) for invalid/unbuilt adjacency/no neighbour. Does not consult explicit boundary-marker flags; a marked interior edge can still have a neighbour. Nonmanifold data chooses first adjacency match.
     */
    [[nodiscard]] QPair<int, int> findEdgeNeighbour(int triIdx, int edgeLocal) const;

    // ----- Write path (Slice §V.VA / §V.VB / §V.VC) ------------------------
    /*! \brief Set vertex Z. Mutates the layer's MeshResult, updates
     *  scene height caches, emits attributeChanged with the vertex ref name.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] z New native bed elevation with existing datum/unit; caller should supply finite values because this setter does not reject NaN/infinity.
     * @return False for invalid index; true for unchanged value without signals, or after mutation.
     * @note Incrementally rewrites incident fan heights/diagonal and expands range when adjacency/cache shape permits; edge slopes/bounds/overview remain stale until a full rebuild. Otherwise full/light rebuild occurs. Bumps geometry revision, invalidates ownership and emits vertex attributeChanged()/repaintRequested(). Edits GUI state, not engine/file; dirty flag is managed by external project hooks.
     */
    bool applyMeshVertexZ(int vertexIdx, double z);

    /*! \brief Replace the entire BC value for edge `(triIdx,edgeLocal)`.
     *  Emits attributeChanged. Apply-as-you-go.
     * @param[in] triIdx Original zero-based cell index.
     * @param[in] edgeLocal Triangle0..2 or quad0..3 local edge; not a unique-scene-edge index.
     * @param[in] bc Whole authored BC copied verbatim, including type/series/group/head/slope/flow/conveyance. Values use MeshEdgeBC section units; no boundary-only/type/range/reference validation or neighbour mirroring occurs here.
     * @return False for invalid index/unavailable slot, true unchanged or changed.
     * @note Effective edit bumps BC revision, schedules legend scan and emits edge attributeChanged()/repaintRequested(). Use applyMeshEdgeConveyance() for symmetric interior flux attenuation.
     */
    bool applyMeshEdgeBC(int triIdx, int edgeLocal, const mesh::MeshEdgeBC &bc);

    /*! \brief Engine §11A — set the per-edge conveyance (flux attenuation
     *  multiplier) in [0, 1]. Rejects out-of-range values (the C API does
     *  the same; the toolbar's QDoubleSpinBox range guards against this at
     *  the UI). Interior edges are mirrored to the neighbour slot so the
     *  two halves always carry the same value (matches the engine's
     *  symmetry invariant). Emits attributeChanged for the edited slot.
     * @param[in] triIdx Original zero-based cell index.
     * @param[in] edgeLocal Triangle0..2 or quad0..3 local edge; not a unique-scene-edge index.
     * @param[in] conveyance Finite dimensionless0..1 flux multiplier; NaN/infinite/outside range rejected.
     * @return False for invalid/unavailable slot/value; true for already-matching or applied value.
     * @note Mirrors first found neighbour slot when adjacency available, emits attributeChanged() for original edge and repaint only on effective change. Does not increment BC revision, refresh legends or mutate engine directly; progressive missing adjacency can leave no mirrored half.
     */
    bool applyMeshEdgeConveyance(int triIdx, int edgeLocal, double conveyance);

    /*! \brief Set the vertex's descriptive tag ([2D_VERTICES] TAG column).
     *  Distinct from the coupled node. Emits attributeChanged.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] tag Raw descriptive text copied including empty; independent of node coupling.
     * @return False for invalid index; true unchanged without signals or after effective mutation.
     * @note Effective change emits vertex attribute/repaint but no geometry/attribute revision.
     */
    bool applyMeshVertexTag(int vertexIdx, const QString &tag);

    /*! \brief Set the vertex's coupled SWMM node ([2D_VERTEX_NODE_MAP]).
     *  Drives the coupled-vertex glyph. Clearing the coupling resets the
     *  coupling Cd/Area to the engine defaults (0.65 / 1.0 mesh-length units²). Emits
     *  attributeChanged.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] node Raw SWMM node ID copied without existence/coupling validation; empty clears.
     * @return False for invalid index; true unchanged without signals or after effective mutation.
     * @note Clearing resets coefficient0.65/area1.0mesh-length² and marks both authored. Updates scene marker tagged flag, emits vertex attribute/repaint; does not bind/modify engine node.
     */
    bool applyMeshVertexCoupledNode(int vertexIdx, const QString &node);

    /*! \brief Set the vertex's coupling discharge coefficient
     *  ([2D_VERTEX_NODE_MAP] CD column), which marks it authored. Rejects
     *  uncoupled vertices and non-positive values. Emits attributeChanged.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] cd Positive dimensionless discharge coefficient; NaN/nonpositive rejected but positive infinity is not separately rejected.
     * @return False invalid/uncoupled/nonpositive, true already matching authored value or after assignment.
     * @note Authors the parameter even when value equals an unauthored default, emits only vertex attributeChanged() on effective change (no repaint/revision). No engine/file write or model reference validation.
     */
    bool applyMeshVertexCouplingCd(int vertexIdx, double cd);

    /*! \brief Set the vertex's coupling exchange area in mesh length units²
     *  ([2D_VERTEX_NODE_MAP] AREA column), which marks it authored. Rejects
     *  uncoupled vertices and non-positive values. Emits attributeChanged.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] area Positive exchange area in native mesh horizontal-length units squared; NaN/nonpositive rejected but positive infinity not separately rejected.
     * @return False invalid/uncoupled/nonpositive, true already matching authored value or after assignment.
     * @note Authors the parameter even when value equals an unauthored default, emits only vertex attributeChanged() on effective change (no repaint/revision). No engine/file write or model reference validation.
     */
    bool applyMeshVertexCouplingArea(int vertexIdx, double area);

    /*! \brief Set whether the vertex row authors its CD / AREA (an undo
     *  restores the unauthored state the mapper left). Emits
     *  attributeChanged.
     * @param[in] vertexIdx Original zero-based vertex index.
     * @param[in] cdSet,areaSet Whether the existing numeric tokens are authored when saving.
     * @return False invalid index, true unchanged/applied. Coupled-node presence is not required.
     * @note Effective change emits vertex attributeChanged() only; numeric values, redraw/cache revisions and engine state are unchanged.
     */
    bool applyMeshVertexCouplingAuthored(int vertexIdx, bool cdSet, bool areaSet);

    /*! \brief Replace the node→cell coupling row set wholesale
     *  ([2D_TRIANGLE_NODE_MAP] repeated-row form, Plan Part C). Rows with
     *  out-of-range triangles or empty node ids are dropped. Returns the
     *  previous row set so callers (undo commands) can restore it.
     * @param[in] rows Whole candidate row set copied, retaining order/duplicates and raw coefficient/area flags/values. Invalid cell indices/empty node IDs are dropped; node existence and numeric ranges are not checked.
     * @return Owned previous row set for caller undo.
     * @note Always replaces and emits meshEditsChanged()/repaintRequested(), including equal rows. No single-element attributeChanged(), cache revision, engine update or dirty-flag assignment inside this helper.
     */
    QVector<mesh::CellCoupling> applyCellCouplings(const QVector<mesh::CellCoupling> &rows);

    /*! \brief Read-only view of the node→cell coupling rows.
     * @return Borrowed current node-to-cell coupling rows, including repeated rows; invalidated by replacement/layer destruction.
     */
    [[nodiscard]] const QVector<mesh::CellCoupling> &cellCouplings() const
    {
        return m_mesh.cellCouplings;
    }

    /*! \brief Set a triangle's Manning's roughness. Emits attributeChanged
     *  with the cell ref name.
     * @param[in] triIdx Original zero-based cell index, including quads.
     * @param[in] mannings Positive Manning roughness in mesh/project authoring convention; nonpositive/NaN rejected, positive infinity not independently rejected.
     * @return False invalid/value rejected; true unchanged or applied.
     * @note Effective change bumps attribute revision, queues legend refresh, emits cell attributeChanged()/repaintRequested(). Does not update engine/file or independently set divergence flag.
     */
    bool applyMeshTriangleMannings(int triIdx, double mannings);

    /*! \brief Set a triangle's initial water depth in m ([2D_TRIANGLES]
     *  INIT_DEPTH column, default 0 = dry). Emits attributeChanged with the
     *  cell ref name.
     * @param[in] triIdx Original zero-based cell index, including quads.
     * @param[in] depth Nonnegative initial water depth in metres per MeshTriangle storage contract; negative/NaN rejected, positive infinity not independently rejected.
     * @return False invalid/value rejected; true unchanged or applied.
     * @note Effective change bumps attribute revision, queues legend refresh, emits cell attributeChanged()/repaintRequested(). Does not update engine/file or independently set divergence flag.
     */
    bool applyMeshTriangleInitDepth(int triIdx, double depth);

    /*! \brief Set a triangle's descriptive tag ([2D_TRIANGLES] TAG column).
     *  Emits attributeChanged with the cell ref name.
     * @param[in] triIdx Original zero-based cell index, including quads.
     * @param[in] tag Raw region tag copied verbatim, including empty.
     * @return False invalid cell, true unchanged/applied.
     * @note Effective change emits cell attribute/repaint but does not bump attribute revision or refresh ownership/legend caches; consumers depending on tag inheritance need subsequent explicit refresh/invalidation.
     */
    bool applyMeshTriangleTag(int triIdx, const QString &tag);

    /*! \brief Write a per-cell infiltration override ([2D_INFILTRATION] row,
     *  GUI plan §3.2a). The cell stops inheriting from its region tag / the
     *  '*' default until the override is cleared. Emits attributeChanged with
     *  the cell ref name.
     * @param[in] triIdx Original zero-based cell index, including quads.
     * @param[in] row Whole per-cell authoring override in project infiltration units, copied without method/numeric/ownership validation.
     * @return False invalid cell, true matching existing override or after insertion.
     * @note Effective change bumps attribute revision, queues legend refresh and emits cell attribute/repaint. Bypasses surfaceInfiltrationEditable() UI policy and does not refresh engine-resolved ownership immediately.
     */
    bool applyMeshTriangleInfil(int triIdx, const mesh::InfilRow &row);
    /**
     * @param[in] model Optional nonowned model QObject tracked with QPointer; null unbinds.
     * @note Replaces closing/edited connections when identity differs, then always refreshes infiltration/surface ownership. Engine closing clears ownership/source previews and invalidates surface preview; model edits refetch both. QObject ownership is unchanged and all engine/preview reads must be serialized on GUI thread.
     */
    void setInfiltrationModel(::SWMMModelLayer *model);
    /**
     * @brief Rebuild GUI ownership/conflict/source descriptions from the bound engine in one bulk read.
     * @note Success requires matching cell count capacity; missing model/engine/data clears derived arrays. Existing explicit non-NONE GUI override forces conflict for aquifer-owned cells. Effective array change increments attribute revision and emits meshEditsChanged()/repaintRequested(); it does not alter authored infiltration rows or engine ownership.
     */
    void refreshInfiltrationOwnership();
    /**
     * @brief Preview reviewed surface-share rows for a topology-matching bound engine.
     * @note Builds per-cell mesh-weather/source-pervious percentages and status0unassigned/1reviewed/2unreviewed/3invalid. Invalid preview producesNaN percentages; absent rows/mismatch clears arrays. Reviewed nonlumped shares contribute pervious/nativeLID area. Effective value change increments attr revision and emits bulk/repaint; no engine commit or authored row edit. NaN equality can cause repeated notifications.
     */
    void refreshSurfaceOwnership();
    /**
     * @brief Clear derived weather/pervious/status arrays when geometry/model changes.
     * @note Already-empty arrays are a no-op; otherwise increments attr revision and emits meshEditsChanged()/repaintRequested(). Does not erase authored engine owners/infiltration records.
     */
    void invalidateSurfaceOwnership();
    /**
     * @param[in] defaults Whole ordered inherited rows copied without validation/deduplication.
     * @param[in] overrides Whole sparse cell-index rows copied without range/value validation.
     * @note Always increments attr revision, refreshes infiltration ownership and emits attributeChanged(sourcePath())/meshEditsChanged(). Ownership refresh may emit another bulk/repaint; this method does not itself request repaint/legend refresh if ownership is unchanged.
     */
    void replaceInfiltrationRows(const QVector<mesh::InfilDefaultRow> &defaults,
                                 const QHash<int, mesh::InfilRow> &overrides);
    /**
     * @param[in] cell Original cell index queried against available ownership preview.
     * @return True for owner1Surface bank, missing preview or any out-of-range index (including negative); false for other resolved owners. This is UI policy, not cell-index validation or apply-method enforcement.
     */
    bool surfaceInfiltrationEditable(int cell) const;

    /*! \brief Erase the per-cell infiltration override, so the cell resolves
     *  through its region tag / the '*' default again.
     *
     *  The inverse of applyMeshTriangleInfil, and NOT the same as writing back
     *  the inherited numbers: a materialised override with identical values
     *  looks right but silently stops tracking the region, so the next
     *  region-level edit no longer reaches the cell. This is what
     *  MeshSetTriangleInfilCommand::undo() calls for a cell that was
     *  inheriting. Emits attributeChanged with the cell ref name.
     * @param[in] triIdx Original zero-based cell index, including quads.
     * @return False invalid cell, true already absent or after erase.
     * @note Erase resumes tag/default inheritance, bumps attribute revision, queues legend refresh and emits cell attribute/repaint. Does not materialize inherited values or check engine ownership policy.
     */
    bool clearMeshTriangleInfil(int triIdx);

    /*! \brief Write one `[2D_INFILTRATION_DEFAULTS]` row — the region-level
     *  peer of applyMeshTriangleInfil.
     *
     *  \p tag is a [2D_TRIANGLES] TAG value, or "*" for the mesh-wide
     *  fallback. Every cell carrying \p tag that has no per-cell override
     *  starts resolving through the new row, so a region-level edit reaches
     *  those cells WITHOUT materialising an override on any of them — which is
     *  the whole point of engine D-I3, and the difference between an
     *  assignment that stays editable as regions and one frozen into N
     *  per-cell rows.
     *
     *  An existing row for \p tag is replaced in place (section order is
     *  preserved); a new tag is appended. Rejects an empty tag.
     *
     *  Bumps the same attribute-cache revision its per-cell siblings do, so
     *  the fill-colour cache cannot serve stale colours for an inherited
     *  value. The edit has no single element ref — every inheriting cell moved
     *  — so it emits meshEditsChanged() for views that repaint wholesale,
     *  alongside a layer-scope attributeChanged() for the ones that only need
     *  "this mesh was edited".
     * @param[in] tag Exact nonempty region identity or wildcard*, no trimming/existence validation.
     * @param[in] row Complete inherited override in project infiltration units, copied without validation.
     * @return False empty tag; true matching row or after first matching replacement/new append.
     * @note Effective changes use announceInfilDefaultsChanged(); later duplicate default tags are not normalized and no per-cell override is created.
     */
    bool applyMeshInfilDefault(const QString &tag, const mesh::InfilRow &row);

    /*! \brief Erase the `[2D_INFILTRATION_DEFAULTS]` row for \p tag, so cells
     *  carrying it fall through to the '*' row (or to no model at all).
     *
     *  The inverse of applyMeshInfilDefault, and what
     *  MeshSetInfilDefaultsCommand::undo() calls for a tag that had no row
     *  before the edit. Returns true when the row is already absent.
     * @param[in] tag Exact first-matching inherited-default key to remove.
     * @return True even when absent/empty; effective erase announces layer/bulk/repaint through shared helper. No numeric/ownership validation or engine mutation.
     */
    bool clearMeshInfilDefault(const QString &tag);

    // ----- Selection highlighting (§V follow-up) ---------------------------
    /*! \brief Indices of mesh vertices currently selected for highlight
     *  rendering. Populated by the SelectionManager bridge (filtered to
     *  this layer's mesh by MeshObjectRef::layerKey).
     * @return Borrowed raw highlight set. Original vertex IDs. References remain tied to layer state; no validation/ownership transfer.
     */
    [[nodiscard]] const QSet<int> &highlightedVertices() const { return m_selVertices; }
    /**
     * @param[in] indices Original vertex IDs. Copied without index/layer identity validation.
     * @note Equal set is a no-op; change requests repaint only. Does not change application selection manager, engine state or emit selectionInvalidated().
     */
    void setHighlightedVertices(const QSet<int> &indices);

    /*! \brief Flat edge slots (`mesh::edgeSlot(cell, edgeLocal)`) currently
     *  selected for highlight rendering.
     * @return Borrowed raw highlight set. Original stride-four edge slots. References remain tied to layer state; no validation/ownership transfer.
     */
    [[nodiscard]] const QSet<int> &highlightedEdges() const { return m_selEdges; }
    /**
     * @param[in] flatIndices Original stride-four edge slots. Copied without index/layer identity validation.
     * @note Equal set is a no-op; change requests repaint only. Does not change application selection manager, engine state or emit selectionInvalidated().
     */
    void setHighlightedEdges(const QSet<int> &flatIndices);

    /*! \brief Triangle (cell) indices currently selected for highlight
     *  rendering. Populated by the SelectionManager bridge (MeshCell refs).
     * @return Borrowed raw highlight set. Original cell IDs (triangles/quads). References remain tied to layer state; no validation/ownership transfer.
     */
    [[nodiscard]] const QSet<int> &highlightedTriangles() const { return m_selTriangles; }
    /**
     * @param[in] indices Original cell IDs (triangles/quads). Copied without index/layer identity validation.
     * @note Equal set is a no-op; change requests repaint only. Does not change application selection manager, engine state or emit selectionInvalidated().
     */
    void setHighlightedTriangles(const QSet<int> &indices);

signals:
    /*! \brief Emitted on any successful apply* mutation. \p refName is
     *  the MeshObjectRef-encoded element name (see meshobjectref.h).
     * @param[in] refName Original mesh element encoded MeshObjectRef name for most individual writes; defaults use layerKey and bulk infiltration replacement uses absolute sourcePath instead. Some successful equal-value no-ops emit nothing.
     */
    void attributeChanged(const QString &refName);

    /*! \brief Emitted on a bulk mutation that has no single element ref
     *  (currently applyCellCouplings). Companion to attributeChanged for
     *  listeners that only need to know "the mesh was edited" — e.g. the
     *  project dirty flag.
     * @note Bulk coupling/default/ownership-preview changes can emit this even without authored mesh changes or direct divergence-flag assignment. Consumers should inspect context; single edits often use only attributeChanged().
     */
    void meshEditsChanged();

    /*! \brief Emitted when remesh / reload invalidates element indices
     *  in any held selection — toolbar and tools clear on receipt.
     * @note Declared remesh/reload integration notification; current methods in this implementation do not emit it automatically for geometry edits.
     */
    void selectionInvalidated();

    /*! \brief Emitted when isActiveMesh() changes.
     * @param[in] isActive New active-target flag before repaint.
     */
    void activeMeshChanged(bool isActive);

    /*! \brief Background pyramid (overview) rebuild lifecycle — mirrors
     *  GISRasterLayer::overviewBuildStarted/Finished so the app can drive
     *  the busy bar + status message during the build.
     * @param[in] layerName Current layer display name immediately before worker launch.
     */
    void overviewBuildStarted(const QString &layerName);
    /**
     * @param[in] ok False for caught worker std::exception; true after adoption, including empty/small-mesh overview. Does not prove source geometry revision still matches.
     */
    void overviewBuildFinished(bool ok);

    /*! \brief The deferred heavy geometry from a progressive load
     *  (finishSceneGeometryAsync) has been adopted — wireframe, spatial
     *  culling and editing structures are now available.
     * @note Emitted on GUI thread after deferred heavy products/legend inputs adopted, before graphics-item/repaint notification. Failure/stale discarded builds do not emit.
     */
    void sceneGeometryReady();

public:
    // ----- Renderer (Slice BI Phase 8.13.6.6) -----------------------------
    // API plumbing only — paint loop still uses the per-vertex hillshade
    // ramp directly.  Sub-phase 8.13.6.4 (deferred until Slice BB
    // ColorRamp lands) will refactor paint to consult m_renderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default SingleSymbolRenderer so
     *          callers never have to null-check.  Owned by the layer.
     * @return Borrowed owned placeholder renderer, eagerly nonnull; invalidated by replacement/destruction. Decoration paint currently reads sublayer/light state rather than this pointer.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const override;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected.  Emits \ref rendererChanged() when the pointer
     *          actually changes.
     * @param[in] r Fresh exclusively owned nonnull renderer; null ignored, and caller must never wrap an already owned raw renderer.
     * @note Replaces pointer/emits rendererChanged() only, without repaint, style projection, geometry rebuild or rule-list synchronization.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r) override;

    // ----- ISublayerHost interface -----------------------------------------
    /*! Ordered list of sublayers owned by this layer.
     *  Paint order is list order (bottom-up):
     *    mesh fill (static) → contour bands (filled) → mesh edges →
     *    isolines → mesh vertex markers
     *  The layer's existing Q_PROPERTYs (showEdges, showMeshNodes,
     *  showContours, ...) act as thin shims that forward to the
     *  corresponding sublayer's setVisible() / style so saved .oswp files
     *  and existing UI still work.
     * @return Owned list copy of borrowed layer-owned sublayers, lazily seeded bottom-up fill/bands/edges/BC/isolines/vertices/coupled. Mutable order is shared by editor/QSG; CPU fallback uses its own fixed pass sequence. Const lazy access requires serialized state.
     */
    [[nodiscard]] QList<OpenSWMM::Render::ISublayer *> sublayers() const override;

    /*! Reorder sublayers in paint order (bottom-up).  Emits
     *  repaintRequested() on success.  Returns false on out-of-range indices.
     * @param[in] from,to Zero-based positions in current bottom-up paint order.
     * @return False for invalid/equal positions, true after move/repaintRequested(). Does not rebuild rules or CPU fixed paint sequence.
     */
    bool moveSublayer(int from, int to) override;

    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshFillSublayer *meshFillSublayer() const
    {
        return m_meshFillSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshEdgeSublayer *meshEdgeSublayer() const
    {
        return m_meshEdgeSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshNodeSublayer *meshNodeSublayer() const
    {
        return m_meshNodeSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::ContourBandSublayer *contourBandSublayer() const
    {
        return m_contourBandSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::IsolineSublayer *isolineSublayer() const
    {
        return m_isolineSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshBcSublayer *meshBcSublayer() const
    {
        return m_meshBcSublayer;
    }
    /**
     * @return Borrowed layer-owned decoration sublayer created at construction. Its mutable style/visibility emits invalidation forwarded into repaint; callers must not delete it or outlive the layer.
     */
    [[nodiscard]] OpenSWMM::Render::CoupledNodeSublayer *coupledNodeSublayer() const
    {
        return m_coupledNodeSublayer;
    }

    /*! Migration hook (ISublayerHost) — seeds the BC / coupled-node
     *  sublayers from legacy MeshEdgeStyle / MeshNodeStyle JSON when a
     *  pre-split project or style file is loaded.
     * @param[in] sublayersJson Already-loaded host JSON with optional sublayers array.
     * @note Migrates pre-split edge BC fields and vertex tagged-marker fields only when new BC/coupled IDs are absent. Uses existing loaded parent visibility and typed style setters; new-format rows prevent repeated migration. No file write, strict schema validation or batch signal suppression.
     */
    void onSublayersJsonLoaded(const QJsonObject &sublayersJson) override;

    /*! Slice US.3 — bed-elevation range of the loaded mesh, the data range the
     *  contour-band / isoline classification scheme classifies over.
     * @return Cached minimum native bed elevation; incremental edits expand but never tighten until full rebuild. Empty geometry can retain extreme initialization sentinel.
     */
    [[nodiscard]] double zMin() const { return m_zMin; }
    /**
     * @return Cached maximum native bed elevation with the same incremental/empty-state caveats as zMin().
     */
    [[nodiscard]] double zMax() const { return m_zMax; }

    /*! \brief Per-vertex bed-elevation samples, for data-driven classification
     *  (quantile / natural-breaks / std-dev). Strided down to at most
     *  \p maxSamples so a "resample" on a multi-million-vertex mesh stays
     *  responsive; the resulting distribution is representative for binning.
     * @param[in] maxSamples Requested maximum output count, clamped to at least one.
     * @return Owned strided finite original-vertex bed values in native units; no repeats for shared cells. At most normalized cap; deterministic stride can miss rare extrema/classes.
     */
    [[nodiscard]] QVector<double> elevationSamples(int maxSamples = 200000) const;

    // ----- Per-cell attribute colouring (MeshFillStyle::colorByAttribute) ---
    //
    // Values come from mesh::cellParamValue(), the single registry that also
    // drives the editing toolbar and the attribute table — never read
    // MeshTriangle::mannings directly, or the pending gw.* keys will not
    // light up for free when the engine grows them.

    /*! \brief Monotonic counter bumped on every per-cell attribute write.
     *  The QSG renderer folds this into its fill-colour cache key so a
     *  Manning's edit cannot serve a stale cached colour.
     * @return Current attribute-derived cache revision, including ownership refreshes; not every raw metadata/tag edit increments it.
     */
    [[nodiscard]] quint64 attrRevision() const { return m_attrRevision; }

    /*! \brief Monotonic counter bumped on every edge-BC write / resize.
     * @return Current whole-BC/mutable-access revision; conveyance-only helper does not increment this counter.
     */
    [[nodiscard]] quint64 bcRevision() const { return m_bcRevision; }

    /*! \brief Scene-edge indices whose slot carries a **non-Wall** BC.
     *
     *  Parallel index space to m_sceneEdges. Bounded by the boundary ring
     *  rather than the cell count, which is the whole point: the interior
     *  wireframe is correctly suppressed at far zoom (Qsg2DLodPolicy needs
     *  ~32 px per cell before it draws edges at all), but the BC ring is a
     *  small, semantically important subset that must stay visible at every
     *  zoom. The renderer walks this vector instead of rescanning every edge
     *  per frame.
     *
     *  Cached; recomputed when the BC or geometry revision moves.
     * @return Borrowed cached unique scene-edge indices whose corresponding valid BC slot type integer>zero, regardless of boundary/type validity/visibility. Empty when heavy slot array is unavailable/inconsistent.
     * @note Recomputed on BC/geometry revision; references/elements can change on next call/rebuild. Mutable const cache requires serialized access.
     */
    [[nodiscard]] const QVector<qint32> &bcSceneEdges() const;

    /*! \brief Per-cell value of \p key, **parallel to m_sceneTris** (which
     *  skips degenerate triangles, so it is not the mesh triangle order).
     *  NaN marks an unset attribute. Cached; recomputed when the mesh
     *  geometry or attribute revision moves. Empty for an unknown key.
     * @param[in] key Exact mesh::cellParamSpec registry key.
     * @return Borrowed single-slot cache parallel to display fan triangles; quad cell values repeat twice. Unknown key empty, missing numeric values retained as NaN/other registry sentinel; range cache considers finite double originals before float narrowing.
     * @note Recomputed on key/geometry/attribute revision and can resolve inherited/ownership values. A later different-key call overwrites the same buffer; copy if retaining independently. No locking/notifications.
     */
    [[nodiscard]] const QVector<float> &cellAttributeValues(const QByteArray &key) const;

    /*! \brief Strided sample of \p key for data-driven classification
     *  (quantile / natural-breaks / std-dev), NaNs excluded. Empty when the
     *  attribute has no data at all. Mirrors elevationSamples()'s striding so
     *  the classification editor stays responsive on huge meshes.
     * @param[in] key Exact registry attribute identity.
     * @param[in] maxSamples Cap clamped at least one.
     * @return Owned finite float-cache values strided across display fan entries, at most cap; quads can weight sampling twice and skipped invalid cells have no entry. No unit conversion beyond registry value semantics.
     */
    [[nodiscard]] QVector<double> cellAttributeSamples(const QByteArray &key,
                                                       int maxSamples = 200000) const;

    /*! \brief Observed [min,max] over the non-NaN values of \p key.
     *  \returns false (leaving the outputs untouched) when the attribute has
     *           no data — which is the case for every gw.* key today.
     * @param[in] key Exact registry attribute identity.
     * @param[out] vMin,vMax Optional extrema destinations assigned only on success.
     * @return True if any finite registry value exists among current fan cells, false with outputs unchanged otherwise. Primes/changes the single-slot attribute cache but emits nothing.
     */
    [[nodiscard]] bool cellAttributeRange(const QByteArray &key, double *vMin, double *vMax) const;

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    /**
     * @param[in] scene Optional destination scene; null/already tracked item is a no-op.
     * @param[in] canvasExtent Unused map bounds.
     * @param[in] canvasSRS Unused optional CRS; cache transform must have been established separately.
     * @note Creates one scene-owned CPU graphics item borrowing this layer. Does not rebuild geometry or check visibility/QSG gate here; paint/refresh enforce those. GUI-thread operation.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional scene; nonnull receives removeItem() for the tracked item.
     * @note Always deletes/clears a tracked CPU item, including with null scene; other cached geometry remains. External deletion of that raw item is unsupported.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] scene Optional scene; null returns.
     * @param[in] canvasExtent Current map bounds forwarded to population, otherwise unused.
     * @param[in] canvasSRS Optional canvas CRS forwarded but not applied by population.
     * @note Hidden layer deletes its CPU item; visible missing item is created, existing one gets update(). No geometry rebuild, opacity/z resynchronization or source fetch.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Optional borrowed target CRS used immediately to replace layer-to-canvas transform.
     * @note Missing/equivalent/failed CRS leaves no transform and full scene rebuild uses native XY. Always rebuilds all geometry/overview, invalidates surface previews and requests repaint; does not alter original mesh coordinates/SRS or mark edits. GUI-thread serialized OGR/cache access.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*! Slice U-5 — surface the mesh layer itself as the single styleable
     *  subject for the unified LayerStyleDialog. The Q_PROPERTYs declared
     *  above drive the editor; Q_CLASSINFO("group:...") groups them into
     *  Display / Hillshade / Contours tabs. */
    /**
     * @return Owned editor wrappers borrowing the layer plus seven sublayer style QObjects. Wrappers must not outlive layer/children; no data ownership transfers.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

    // ----- Rule Model (Slice B.5b, Phase B) -------------------------------
    //
    // 2D mesh layers carry decoration-style rendering (fill / edges /
    // nodes / hillshade / contours) rather than per-kind renderers, so
    // the seed RuleList holds one Rule per decoration archetype with a
    // SingleSymbolRenderer placeholder. The decoration specs from Z.6
    // (HillshadeSymbolLayerSpec / ContourSymbolLayerSpec / MeshEdge /
    // MeshNode) populate the SymbolStyle props inside each Rule. Paint
    // integration that consumes those specs is the named Z.6a slice.
    /**
     * @return Borrowed owned six-rule decoration model lazily initialized on GUI thread, including through const access. Seeds typed default specs rather than snapshots of current decoration settings; subsequent SingleSymbol rule edits project supported fields back into layer/sublayer state.
     */
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    /**
     * @return Borrowed owned six-rule decoration model lazily initialized on GUI thread, including through const access. Seeds typed default specs rather than snapshots of current decoration settings; subsequent SingleSymbol rule edits project supported fields back into layer/sublayer state.
     */
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- Scene-geometry structs (public for SWMM2DMeshQSGRenderer) ---------

    /*! Per fill triangle: scene-space vertices + per-vertex z (for hillshade).
     *
     *  m_sceneTris is the SUB-TRIANGLE FAN of the mesh cells (workplans/
     *  TRI_QUAD_MESHING_PLAN_2026-09-06.md §5): one entry per triangle, two
     *  per quad (split on the engine's VFR diagonal by mesh::cellGeom), each
     *  carrying the index of the cell it belongs to. Entries of one cell are
     *  consecutive — see m_cellSceneStart. Cells with an out-of-range vertex
     *  id emit nothing. */
    struct SceneTri {
        QPointF a, b, c;
        float zAvg;       ///< Average vertex z — elevation colour.
        float z0, z1, z2; ///< Per-vertex z — hillshade face normal.
        int cell = -1;    ///< Mesh cell index (MeshResult::triangles).
    };

    /*! Append the vertex elevations of a SceneTri fan to \p out, counting
     *  every CELL vertex once: a quad's second sub-triangle shares the
     *  diagonal with its first, so only its off-diagonal vertex is new.
     *  Feeds the quantile / Jenks classification samplers. */
    template <class Tris>
    /**
     * @param[in] tris Stable ordered fan range of SceneTri values; consecutive entries with same nonnegative cell ID are treated as a quad pair.
     * @param[in,out] out Existing samples retained and extended without finite filtering/striding. Adds all first-triangle heights and only XY-nonshared second-triangle vertices; shared vertices across different cells repeat.
     * @note Allocates/reserves owned output; this helper does not guarantee globally unique mesh vertices or validate fan ordering/identity.
     */
    static void appendVertexElevationSamples(const Tris &tris, QVector<double> &out)
    {
        out.reserve(out.size() + int(tris.size()) * 3);
        const SceneTri *prev = nullptr;
        for (const SceneTri &t : tris) {
            if (prev && t.cell >= 0 && t.cell == prev->cell) {
                const QPointF p[3] = {t.a, t.b, t.c};
                const float z[3] = {t.z0, t.z1, t.z2};
                for (int k = 0; k < 3; ++k)
                    if (p[k] != prev->a && p[k] != prev->b && p[k] != prev->c)
                        out.push_back(double(z[k]));
            } else {
                out.push_back(double(t.z0));
                out.push_back(double(t.z1));
                out.push_back(double(t.z2));
            }
            prev = &t;
        }
    }

    /*! Per-edge: scene-space line + elevation + slope.
     *  slope = |Δz| / horizontal_distance_in_map_units.
     *
     *  NB the flat BC slot for an edge lives in the parallel
     *  m_sceneEdgeSlot vector, not here — see the comment on that member. */
    struct SceneEdge {
        QLineF line;
        float zAvg;
        float slope;
    };

    /*! Per-vertex node dot. */
    struct SceneNode {
        QPointF pt;
        float z;
        bool tagged; ///< true = SWMM-coupled vertex.
    };

    // Scene-geometry caches — written by rebuildSceneGeometry(),
    // read by SWMM2DMeshQSGRenderer::updatePaintNode().
    QRectF m_sceneBBox;
    QVector<SceneTri> m_sceneTris;
    QVector<SceneEdge> m_sceneEdges;
    QVector<SceneNode> m_sceneNodes;

    /*! CSR map cell → fan: the SceneTris of cell c are
     *  m_sceneTris[m_cellSceneStart[c] .. m_cellSceneStart[c+1]). Size
     *  n_cells + 1; an empty range marks a cell skipped as degenerate.
     *  Rebuilt with m_sceneTris (light and full builds). */
    QVector<qint32> m_cellSceneStart;

    /*! Flat BC slot (`mesh::edgeSlot(cell, edgeLocal)`) for each entry of m_sceneEdges;
     *  -1 when no slot could be resolved. Parallel vector rather than a field
     *  on SceneEdge: SceneEdge is 40 bytes and an extra int pads it to 48,
     *  whereas this costs exactly 4 bytes per edge and leaves the hot struct's
     *  cache behaviour alone.
     *
     *  m_sceneEdges is deduplicated and buildMeshHeavyGeom is first-writer-
     *  wins, which is exactly right here: a boundary edge belongs to one
     *  triangle and keeps its own slot, while an interior edge is pushed
     *  twice but both its slots are Wall by construction. */
    QVector<qint32> m_sceneEdgeSlot;
    double m_zMin = 0.0;
    double m_zMax = 0.0;
    float m_maxSlope = 0.0f;

    // ── Level-of-detail (LOD) overview for far-zoom rendering ──────────────
    // A coarse, fixed-resolution decimation of m_sceneTris built once by
    // rebuildOverview(): each occupied cell of a low-res aggregation grid
    // becomes a quad (two SceneTris) coloured by the mean elevation of the
    // triangles whose centroid falls in it, with corner heights averaged
    // from neighbouring cells so hillshade still reads. The painter draws
    // this instead of the full mesh when the native triangles project below
    // ~2 px (see SWMM2DMeshGraphicsItem::paint), turning a 5M-triangle fill
    // into a few-tens-of-thousands-triangle fill at full extent. Zoomed-in
    // views (native triangle ≥ threshold) keep the exact legacy path.
    QVector<SceneTri> m_overviewTris; ///< coarse LOD fill geometry (gap-fill floor)
    double m_nativeTriSpan = 0.0;     ///< representative native tri edge (scene units)

    // Triangle indices into m_sceneTris sorted by descending scene-space area.
    // Drives adaptive far-zoom culling: at low zoom the painter walks this from
    // the front, drawing real cells while their projected area stays above a
    // pixel threshold and stopping as soon as it drops below — so large cells
    // render faithfully and tiny sub-pixel cells are skipped, in O(kept) rather
    // than O(all). Built alongside the overview for meshes past the threshold.
    QVector<int> m_trisBySizeDesc;

    /*! \brief True when an LOD overview has been built and is worth using.
     * @return Whether coarse fill geometry is currently nonempty, not whether a build is running or up to date with latest incremental Z edits.
     */
    [[nodiscard]] bool hasOverview() const { return !m_overviewTris.isEmpty(); }

    /*! \brief Rebuild the far-zoom LOD pyramid (overview) on a background
     *  thread. Emits overviewBuildStarted() immediately and
     *  overviewBuildFinished() from the GUI thread when the fresh pyramid
     *  has been adopted (followed by repaintRequested()). No-op while a
     *  build is already running. Surfaced by the "Rebuild pyramid" button
     *  in the layer-properties (LayerStyleDialog) Metadata tab.
     * @note Owner-thread snapshots current fan/bbox, emits started and builds off-thread. Caught std::exception logs/emits finished(false), retaining old products; success installs returned products/emits finished(true), updates CPU item and repaints. No geometry-revision comparison: an edit/CRS rebuild during work can later receive a stale overview. No source file/pyramid sidecar is written.
     */
    void rebuildOverviewAsync();

    /*! \brief True while rebuildOverviewAsync() has a build in flight.
     * @return Stored async overview-inflight flag; successful completion can produce an empty overview for small meshes.
     */
    [[nodiscard]] bool overviewBuildRunning() const { return m_overviewBuildRunning; }

    // Uniform spatial grid over scene-space triangle / edge bboxes.
    // The full type lives in layers/meshspatialgrid.h (extracted so it
    // can be unit-tested without linking the rest of the layer); see
    // that header for storage layout and threading contract.
    MeshSpatialGrid m_triGrid;
    MeshSpatialGrid m_edgeGrid;
    QVector<QRectF> m_triBBoxes;  ///< parallel to m_sceneTris
    QVector<QRectF> m_edgeBBoxes; ///< parallel to m_sceneEdges

signals:
    /*! \brief Emitted when setRenderer() swaps the renderer pointer.
     * @note Emitted after nonnull placeholder renderer replacement; current setRenderer() does not also request repaint or project decoration settings.
     */
    void rendererChanged();

private:
    QPointer<::SWMMModelLayer> m_infiltrationModel;
    QMetaObject::Connection m_ownerClosing, m_ownerEdited;
    /**
     * @brief Synchronously replace transformed scene fan/nodes/edges/grids/elevation/overview caches from owned mesh.
     * @note GUI-thread operation applies current transform then flips Y; skips invalid-index cells but not every geometric/nonfinite degeneracy. Resets display caches, builds native-coordinate slopes, increments geom revision, invalidates surface preview, refreshes legend and existing item geometry. Empty path returns after revision/invalidation. Does not rebuild BC/vertex adjacency itself or emit sceneGeometryReady()/repaint directly.
     */
    void rebuildSceneGeometry();

    /*! Index into m_sceneTris of the fan triangle containing (sx,sy), or -1.
     *  locateTriangleAt() maps this back to the cell; sampleZAt() needs the
     *  sub-triangle itself for the barycentric blend.
     * @param[in] sx,sy Finite scene-map point in current cached geometry frame.
     * @return Display fan-triangle index, not original cell index. Minus one outside/degenerate; exact bbox then barycentric>=-1e-9 test, grid-candidate fast path when available, otherwise linear scan. Boundary ties follow candidate/fan order. No mutation except read-only spatial candidates.
     */
    [[nodiscard]] int locateSceneTriAt(double sx, double sy) const;

    /*! \brief Build the coarse LOD overview (m_overviewTris) from
     *  m_sceneTris. Called at the end of rebuildSceneGeometry(); a no-op
     *  for meshes below the size threshold (small meshes render full-res
     *  fast enough that an overview would only add popping).
     * @brief Replace overview geometry, descending-area index and native-span estimate synchronously from current scene snapshots.
     * @note No notifications, revision or worker launch; small meshes retain empty overview with representative span. Requires exclusive cache mutation.
     */
    void rebuildOverview();
    /**
     * @brief Seed six default SingleSymbol decoration rules and attach per-rule replacement propagation.
     * @note Mesh fill/hillshade/bands/lines/edges/nodes use typed first-layer specs; unsupported renderer/kind/empty layer is ignored. Hillshade handler sets zExag then strength, which share a storage knob. No hooks for externally reloaded/replaced Rule objects or reverse synchronization from ordinary sublayer setters are installed. GUI-thread only.
     */
    void buildRuleListLazy() const; // Slice B.5b — lazy ruleList init.

    // Slice B.5b — Rule Model mirror over the mesh layer's decoration
    // state. Lazy-built on first ruleList() call.
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;

    // §V.VA — keep m_bc sized to mesh::edgeSlotCount(n_cells) in sync with the mesh,
    // and rebuild the vertex→triangles adjacency used by sampleZAt /
    // applyMeshVertexZ.
    /**
     * @brief Resize BC slots to stride-four cell count, preserving existing prefix/default-constructing new Wall entries.
     * @note Rebuilds boundary flags from topology/markers, drops lazy graph, always increments BC revision and refreshes legends. No engine write, selection invalidation or direct repaint/attribute signal.
     */
    void resizeBCsToMesh();
    /**
     * @brief Replace original-vertex to original-cell CSR adjacency from current mesh, skipping invalid vertex incidences.
     * @note GUI-thread exclusive operation; does not change geometry/BC revisions or emit notifications.
     */
    void rebuildVertexAdjacency();

    /*! Recompute the set of BC types present in m_bc and the fill sublayer's
     *  "attribute has data" flag, and push both onto the sublayers, which
     *  need them for their const, context-free legendSymbolItems().
     *  O(n_slots + n_triangles) — call directly only from structural paths
     *  (reload / resize / async adoption).
     * @brief Refresh context-free BC/fill legend inputs from current values and preferences.
     * @note Scans BC types and current attribute/category values, sets wall preview width/attribute data flag/categories/depth unit label. Style/legend setters can emit invalidations/repaints. No source write; may mutate the single-slot attribute cache. Structural callers invoke directly; element writes should coalesce.
     */
    void refreshSublayerLegendInputs();

    /*! Coalesced form of the above for per-element write paths. A bulk edit
     *  (MeshCommands assigning Manning's to 50k cells) would otherwise run
     *  the O(n) scan once per cell; this collapses a whole batch into a
     *  single queued refresh once the event loop comes back round.
     * @brief Coalesce whole-mesh legend scans into one owner-bound queued callback.
     * @note Already queued is a no-op. Callback clears queued flag before refreshSublayerLegendInputs(); synchronous callers do not receive completed legend data from this method.
     */
    void scheduleLegendInputRefresh();
    bool m_legendRefreshQueued = false;

    /*! Shared tail of applyMeshInfilDefault / clearMeshInfilDefault — bumps
     *  the attribute revision and emits the layer-scope + bulk signals.
     * @brief Increment attribute revision, queue legend refresh and emit layer-key attributeChanged(),meshEditsChanged(),repaintRequested().
     * @note Does not update engine, resolve ownership or set the explicit divergence flag itself; project listeners handle persistence policy.
     */
    void announceInfilDefaultsChanged();

    mesh::MeshResult m_mesh;
    QString m_sourcePath;
    bool m_isExternal = false;
    bool m_meshUnitsSI = false; ///< `;; UNITS: SI (m)` (#155)
    bool m_active = false;

    // Mesh Tiled LOD plan P1.1 — see qsgOwnsRendering().
    bool m_qsgOwnsRendering = false;

    // True while rebuildOverviewAsync() has a worker in flight.
    bool m_overviewBuildRunning = false;

    // Progressive load (deferHeavyGeometry ctor mode) — see
    // sceneGeometryComplete() / finishSceneGeometryAsync().
    bool m_sceneGeomComplete = true;
    bool m_heavyBuildRunning = false;
    QVector<QPointF> m_pendingScenePts; ///< transient; freed on adopt

    /*! Phase-A-only scene build for the progressive load: triangles, bbox,
     *  z-range, vertex dots and the LOD pyramid — no edges/grids.
     * @brief Build only scene fan/nodes/bbox/elevation/overview for progressive display.
     * @note Clears heavy edge/grid caches, stores pending projected points and marks incomplete except empty mesh. Increments geom revision, invalidates surface preview and updates CPU item geometry; caller starts heavy async build separately. No BC/adjacency/legend reconstruction.
     */
    void rebuildSceneGeometryLight();

    // Hillshade state lives on the mesh-fill sublayer style; we keep a
    // pair of simple knobs the renderer reads from the layer (azimuth,
    // altitude) so the visual exactly reproduces the historic values
    // when the user has not edited anything. The Q_PROPERTY setters
    // forward through the sublayer styles where applicable.
    double m_hillshadeAz = 225.0;    // compass deg
    double m_hillshadeAlt = 35.264;  // deg above horizon (asin(1/√3))
    double m_hillshadeMinLit = 0.15; // shadow floor (0..1)

    // Zoom thresholds (min on-screen cell size, px) for edge / vertex passes.
    // Moderate defaults so the wireframe/vertices come in at a sensible zoom
    // (roughly a few px per cell) rather than only at extreme close-up.
    double m_edgeMinCellPx = 3.0;
    double m_vertexMinCellPx = 6.0;

    quint64 m_geomRevision = 0;
    // See hasUnsavedMeshEdits(): conservative default, cleared only by an
    // explicit pristine attach or a confirmed successful save.
    bool m_editsDirty = true;
    bool m_ownsGeneratedTopology = false;
    bool m_preservesImportedSections = false;
    bool m_importNeedsSaveAsRebase = false;
    std::shared_ptr<GeneratedMeshArtifacts> m_generatedArtifacts;
    OGRCoordinateTransformation *m_transform = nullptr;
    SWMM2DMeshGraphicsItem *m_graphicsItem = nullptr;

    // Per-cell attribute / BC revisions. Separate from m_geomRevision so an
    // attribute edit invalidates the renderer's fill-colour cache without
    // forcing a full geometry rebuild, and so the cached attribute vector
    // below knows when it went stale.
    quint64 m_attrRevision = 0;
    quint64 m_bcRevision = 0;

    // Cache for bcSceneEdges(); keyed on both revisions because a geometry
    // rebuild re-derives m_sceneEdgeSlot and a BC edit changes which slots
    // are non-Wall.
    mutable QVector<qint32> m_bcSceneEdges;
    mutable quint64 m_bcSceneEdgesBcRev = ~quint64(0);
    mutable quint64 m_bcSceneEdgesGeomRev = ~quint64(0);

    // Single-slot cache for cellAttributeValues() — the renderer asks for the
    // same key every frame, so one slot is enough and switching attributes is
    // a one-off recompute.
    mutable QByteArray m_attrCacheKey;
    mutable quint64 m_attrCacheGeomRev = ~quint64(0);
    mutable quint64 m_attrCacheAttrRev = ~quint64(0);
    mutable QVector<float> m_attrCacheVals;
    mutable double m_attrCacheMin = 0.0;
    mutable double m_attrCacheMax = 0.0;
    mutable bool m_attrCacheHasData = false;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default SingleSymbolRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // §V.VA — per-edge BC storage, sized to mesh::edgeSlotCount(n_cells).
    // Flat indexed as `mesh::edgeSlot(cell, edgeLocal)`. Interior-edge
    // entries stay at the default Wall value; engine consults only boundary
    // slots.
    QVector<mesh::MeshEdgeBC> m_bc;

    // §V.VA — per-vertex CSR adjacency for fast incident-triangle lookup
    // (used by sampleZAt / applyMeshVertexZ). Rebuilt by
    // rebuildVertexAdjacency() in the ctor and on geometry-changing
    // reloads (future).
    QVector<int> m_vertTriPtr; // size = n_vertices + 1
    QVector<int> m_vertTriIdx; // size = sum of incidences

    // §V.VA — precomputed boundary status per (cell, edgeLocal). Flat
    // indexed `mesh::edgeSlot(cell, eLocal)`. true = this edge slot is a
    // boundary edge in the loaded mesh. Rebuilt alongside m_bc / adjacency.
    QVector<bool> m_isBoundary;

    // Lazily built from m_isBoundary by boundaryGraph(); invalidated
    // wherever m_isBoundary is rebuilt (resizeBCsToMesh / the deferred
    // finishSceneGeometryAsync completion).
    mesh::MeshBoundaryGraph m_boundaryGraph;
    bool m_boundaryGraphValid = false;

    // §V follow-up — selection highlight state. Drives the renderer's
    // selection-overlay pass (cyan dots / cyan edges).
    QSet<int> m_selVertices;
    QSet<int> m_selEdges;
    QSet<int> m_selTriangles;

    // Sublayer host members. Each is QObject-parented to `this`, so the
    // ISublayerHost contract is satisfied without manual delete in the
    // dtor. The mesh layer surfaces these in sublayers() in paint order
    // (bottom-up): fill → contour bands → edges → isolines → vertex
    // markers.
    OpenSWMM::Render::MeshFillSublayer *m_meshFillSublayer = nullptr;
    OpenSWMM::Render::MeshEdgeSublayer *m_meshEdgeSublayer = nullptr;
    OpenSWMM::Render::MeshNodeSublayer *m_meshNodeSublayer = nullptr;
    OpenSWMM::Render::ContourBandSublayer *m_contourBandSublayer = nullptr;
    OpenSWMM::Render::IsolineSublayer *m_isolineSublayer = nullptr;
    OpenSWMM::Render::MeshBcSublayer *m_meshBcSublayer = nullptr;
    OpenSWMM::Render::CoupledNodeSublayer *m_coupledNodeSublayer = nullptr;

    // User-customisable paint order (Slice GUI-2026-05-30 §2).
    mutable QList<OpenSWMM::Render::ISublayer *> m_sublayerOrder;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
