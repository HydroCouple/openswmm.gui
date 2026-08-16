/*!
 * \file   swmm2dmeshlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H

#include "layers/meshspatialgrid.h"
#include "layers/openswmmvislayer.h"
#include "mesh/meshboundarygraph.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include "render/isublayerhost.h"

#include <QColor>
#include <QLineF>
#include <QPair>
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

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class RuleList;   // Slice B.5b — see ruleList() override below.
class MeshFillSublayer;
class MeshEdgeSublayer;
class MeshNodeSublayer;
class ContourBandSublayer;
class IsolineSublayer;
}

class SWMM2DMeshLayer : public OpenSWMMVisLayer,
                        public OpenSWMM::Render::ISublayerHost
{
    Q_OBJECT

    // Source path + mesh-size metadata for the Properties window. The mesh
    // is immutable after construction so vertex / triangle counts are CONSTANT.
    Q_PROPERTY(QString sourcePath          READ sourcePath           CONSTANT)
    Q_PROPERTY(int    triangleCount        READ triangleCount        CONSTANT)
    Q_PROPERTY(int    vertexCount          READ vertexCount          CONSTANT)

    // Slice U-5 — Q_PROPERTYs for the unified LayerStyleDialog. Setters
    // already emit repaintRequested() which the dialog treats as NOTIFY.
    Q_PROPERTY(bool   showMeshNodes        READ showMeshNodes        WRITE setShowMeshNodes
               NOTIFY repaintRequested)
    Q_PROPERTY(bool   showEdges            READ showEdges            WRITE setShowEdges
               NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeAzimuth     READ hillshadeAzimuth     WRITE setHillshadeAzimuth
               NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeAltitude    READ hillshadeAltitude    WRITE setHillshadeAltitude
               NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeZExag       READ hillshadeZExag       WRITE setHillshadeZExag
               NOTIFY repaintRequested)
    Q_PROPERTY(double hillshadeMinLit      READ hillshadeMinLit      WRITE setHillshadeMinLit
               NOTIFY repaintRequested)
    Q_PROPERTY(bool   showContours         READ showContours         WRITE setShowContours
               NOTIFY repaintRequested)
    Q_PROPERTY(int    contourIntervalCount READ contourIntervalCount WRITE setContourIntervalCount
               NOTIFY repaintRequested)
    Q_PROPERTY(QColor contourColor         READ contourColor         WRITE setContourColor
               NOTIFY repaintRequested)
    Q_PROPERTY(double contourLineWidth     READ contourLineWidth     WRITE setContourLineWidth
               NOTIFY repaintRequested)
    Q_PROPERTY(bool   filledContours       READ filledContours       WRITE setFilledContours
               NOTIFY repaintRequested)
    Q_PROPERTY(double filledContoursOpacity READ filledContoursOpacity WRITE setFilledContoursOpacity
               NOTIFY repaintRequested)

    // Zoom at which the wireframe / vertex dots auto-appear, as the minimum
    // on-screen cell size (pixels across). Larger ⇒ must zoom in closer.
    Q_PROPERTY(double edgeZoomMinCellPx    READ edgeZoomMinCellPx    WRITE setEdgeZoomMinCellPx
               NOTIFY repaintRequested)
    Q_PROPERTY(double vertexZoomMinCellPx  READ vertexZoomMinCellPx  WRITE setVertexZoomMinCellPx
               NOTIFY repaintRequested)

    Q_CLASSINFO("group:sourcePath",           "Source")
    Q_CLASSINFO("group:triangleCount",        "Mesh")
    Q_CLASSINFO("group:vertexCount",          "Mesh")
    Q_CLASSINFO("group:showMeshNodes",        "Display")
    Q_CLASSINFO("group:showEdges",            "Display")
    Q_CLASSINFO("group:edgeZoomMinCellPx",    "Display")
    Q_CLASSINFO("group:vertexZoomMinCellPx",  "Display")
    Q_CLASSINFO("group:hillshadeAzimuth",     "Hillshade")
    Q_CLASSINFO("group:hillshadeAltitude",    "Hillshade")
    Q_CLASSINFO("group:hillshadeZExag",       "Hillshade")
    Q_CLASSINFO("group:hillshadeMinLit",      "Hillshade")
    Q_CLASSINFO("group:showContours",         "Contours")
    Q_CLASSINFO("group:contourIntervalCount", "Contours")
    Q_CLASSINFO("group:contourColor",         "Contours")
    Q_CLASSINFO("group:contourLineWidth",     "Contours")
    Q_CLASSINFO("group:filledContours",       "Contours")
    Q_CLASSINFO("group:filledContoursOpacity", "Contours")

public:
    /*! \param deferHeavyGeometry Progressive-load mode (Mesh Tiled LOD P1.2):
     *  the constructor builds only what the fill needs to draw — scene
     *  triangles, bbox, elevation range, vertex dots and the LOD pyramid —
     *  so a large mesh can join the canvas and render its coarse levels
     *  immediately. The heavy structures (deduplicated wireframe edges,
     *  spatial grids, vertex adjacency, BC slots) arrive later via
     *  finishSceneGeometryAsync(). */
    explicit SWMM2DMeshLayer(mesh::MeshResult result,
                              const QString &sourcePath = {},
                              OpenSWMMVisWorkspace *parent = nullptr,
                              bool deferHeavyGeometry = false);
    ~SWMM2DMeshLayer() override;

    /*! \brief Build the deferred heavy geometry (wireframe edges, spatial
     *  grids, vertex adjacency, BC slots) on a background thread and adopt
     *  it on the GUI thread. Emits sceneGeometryReady() then
     *  repaintRequested() when done. No-op when already complete or a build
     *  is in flight. If the scene geometry was re-projected mid-build (CRS
     *  change), the stale result is discarded and the build re-runs. */
    void finishSceneGeometryAsync();

    /*! \brief False while the heavy structures from a deferred load are
     *  still being built — the renderers fall back to the LOD pyramid and
     *  editing/picking is limited until this flips true. */
    [[nodiscard]] bool sceneGeometryComplete() const { return m_sceneGeomComplete; }

    [[nodiscard]] QString sourcePath() const;
    void setSourcePath(const QString &path)  { m_sourcePath = path; }

    /*! \brief True when the mesh came from an external file referenced by
     *  `[2D_MESH_FILE] FILE <path>`. False for an inline mesh, whose
     *  sourcePath() is the .inp itself. Save must not re-point the .inp at
     *  an inline mesh's "source" — that would overwrite the model. */
    [[nodiscard]] bool isExternalMesh() const { return m_isExternal; }
    void setExternalMesh(bool external)      { m_isExternal = external; }

    /*! Number of triangles in the loaded mesh — exposed as metadata in
     *  the Properties window. */
    [[nodiscard]] int triangleCount() const { return int(m_mesh.triangles.size()); }

    /*! Number of vertices in the loaded mesh — exposed as metadata in
     *  the Properties window. */
    [[nodiscard]] int vertexCount()   const { return int(m_mesh.vertices.size()); }

    /*! Number of boundary edges (domain outline + holes). */
    [[nodiscard]] int boundaryEdgeCount() const { return int(m_mesh.boundaryEdges.size()); }

    /*! Total number of unique mesh edges. m_sceneEdges is the deduplicated
     *  edge set (exact, conformance-independent) built by
     *  rebuildSceneGeometry(); before that runs, fall back to the conforming-
     *  triangulation identity total = (3·T + B)/2. */
    [[nodiscard]] int edgeCount() const {
        if (!m_sceneEdges.isEmpty()) return int(m_sceneEdges.size());
        return (3 * triangleCount() + boundaryEdgeCount()) / 2;
    }

    /*! Steepest edge slope in the mesh (rise/run), cached during scene build. */
    [[nodiscard]] double maxSlope() const { return double(m_maxSlope); }

    // ----- Self-description for the Layer Properties dialog ----------------
    [[nodiscard]] QString sourceDescription() const override;
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    [[nodiscard]] bool isActiveMesh() const { return m_active; }
    void setActiveMesh(bool active);

    // ----- QSG ownership (Mesh Tiled LOD plan P1.1) -------------------------
    /*! True while SWMM2DMeshQSGRenderer draws this layer inside MapCanvas's
     *  QSG frame; the CPU SWMM2DMeshGraphicsItem::paint() early-returns so
     *  the two pipelines never double-paint. Mirrors
     *  SWMM2DResultsLayer::qsgOwnsRendering. */
    [[nodiscard]] bool qsgOwnsRendering() const noexcept { return m_qsgOwnsRendering; }
    void setQsgOwnsRendering(bool own);

    // ----- Display toggles ------------------------------------------------
    // These remain on the layer so existing UI, JSON, and serialization
    // round-trip the way they always have, but they are now thin shims
    // that forward to the per-sublayer visibility flag.
    [[nodiscard]] bool showMeshNodes() const;
    void setShowMeshNodes(bool show);

    [[nodiscard]] bool showEdges() const;
    void setShowEdges(bool show);

    // ----- Zoom thresholds for edge / vertex auto-display -----------------
    // Both renderers suppress the wireframe / vertex-dot passes until the
    // mean on-screen cell size reaches these values (pixels across), so the
    // detail appears automatically as the user zooms in and never clutters
    // the far view. Configurable per layer; defaults reproduce the historic
    // LOD gates (√kEdgeMinCellAreaPx ≈ 5.66 px, √kMarkerMinCellAreaPx ≈
    // 14.14 px). 0 ⇒ always draw (when the sublayer is visible).
    [[nodiscard]] double edgeZoomMinCellPx()   const { return m_edgeMinCellPx; }
    void setEdgeZoomMinCellPx(double px);
    [[nodiscard]] double vertexZoomMinCellPx() const { return m_vertexMinCellPx; }
    void setVertexZoomMinCellPx(double px);

    /*! Area forms (px²) the renderers actually gate on — square of the
     *  configured cell-size thresholds. */
    [[nodiscard]] double edgeMinCellAreaPx()   const { return m_edgeMinCellPx * m_edgeMinCellPx; }
    [[nodiscard]] double vertexMinCellAreaPx() const { return m_vertexMinCellPx * m_vertexMinCellPx; }

    // ----- Hillshade ------------------------------------------------------
    // azimuth/altitude stay on the layer because they describe the light
    // source, which is layer-wide (not per-sublayer). zExag and minLit
    // map to the mesh-fill sublayer style's hillshadeStrength.
    [[nodiscard]] double hillshadeAzimuth()    const { return m_hillshadeAz; }
    [[nodiscard]] double hillshadeAltitude()   const { return m_hillshadeAlt; }
    [[nodiscard]] double hillshadeZExag()      const;
    [[nodiscard]] double hillshadeMinLit()     const;
    void setHillshadeAzimuth(double degrees);
    void setHillshadeAltitude(double degrees);
    void setHillshadeZExag(double factor);
    void setHillshadeMinLit(double minLit);

    // ----- Bed-elevation contours -----------------------------------------
    // Forward to the isoline + contour-band sublayers; the layer Q_PROPERTYs
    // exist for backwards compatibility with .oswp files written before the
    // sublayer refactor.
    [[nodiscard]] bool   showContours()         const;
    [[nodiscard]] int    contourIntervalCount() const;
    [[nodiscard]] QColor contourColor()         const;
    [[nodiscard]] double contourLineWidth()     const;
    void setShowContours(bool show);
    void setContourIntervalCount(int n);
    void setContourColor(const QColor &c);
    void setContourLineWidth(double widthPx);

    [[nodiscard]] bool   filledContours()        const;
    [[nodiscard]] double filledContoursOpacity() const;
    void setFilledContours(bool filled);
    void setFilledContoursOpacity(double a);

    [[nodiscard]] const mesh::MeshResult &mesh() const { return m_mesh; }

    [[nodiscard]] quint64 geomRevision() const { return m_geomRevision; }

    // ---------------------------------------------------------------------
    // Slice §V.VA — mesh-editing foundation: BC storage, picker / hover
    // helpers, write-path apply* family, attributeChanged signal.
    //
    // The layer is the model in the §V MVC; all views (Mesh Editing
    // toolbar, future Property Browser tab, Attribute Table, map renderer)
    // subscribe to attributeChanged and write through the apply* helpers.
    // ---------------------------------------------------------------------

    /*! \brief Per-edge BC values; flat-indexed [tri*3 + edgeLocal]. Sized
     *  to `n_triangles * 3` after mesh load; interior-edge slots stay at
     *  the default Wall value (engine ignores them). */
    [[nodiscard]] const QVector<mesh::MeshEdgeBC> &edgeBCs() const { return m_bc; }

    /*! \brief Mutable BC view — used by INP reader to bulk-populate after
     *  load. Prefer the apply* helpers for user-driven edits so views
     *  receive the attributeChanged signal. */
    QVector<mesh::MeshEdgeBC> &edgeBCsMutable() { return m_bc; }

    /*! \brief Vertex pick. Returns the closest vertex index to scene
     *  point (sx,sy) within \p tolPx screen-pixels, or -1 if none.
     *  \p pxPerSceneUnit converts scene-units to screen-pixels (use the
     *  active canvas zoom). */
    [[nodiscard]] int pickVertexAt(double sx, double sy,
                                    double tolPx, double pxPerSceneUnit) const;

    /*! \brief Edge pick. Returns flat edge index `tri*3 + edgeLocal`
     *  within \p tolPx of (sx,sy), or -1. When \p boundaryOnly is true,
     *  interior edges are skipped. */
    [[nodiscard]] int pickEdgeAt(double sx, double sy,
                                  double tolPx, double pxPerSceneUnit,
                                  bool boundaryOnly) const;

    /*! \brief Triangle containing scene point (sx,sy), or -1 if outside
     *  every triangle. */
    [[nodiscard]] int locateTriangleAt(double sx, double sy) const;

    /*! \brief Triangle (cell) containing scene point \p scenePt, or -1.
     *  Peer of `SWMM2DResultsLayer::pickCellAt`; lets the Select 2D Cells
     *  map tool work against the mesh layer when no results layer exists. */
    [[nodiscard]] int pickCellAt(const QPointF &scenePt) const;

    /*! \brief Triangle indices whose centroid falls inside \p sceneRect.
     *  Peer of `SWMM2DResultsLayer::pickCellsInRect`. */
    [[nodiscard]] QVector<int> pickCellsInRect(const QRectF &sceneRect) const;

    /*! \brief Triangle indices whose centroid falls inside \p scenePoly.
     *  Peer of `SWMM2DResultsLayer::pickCellsInPolygon`. */
    [[nodiscard]] QVector<int> pickCellsInPolygon(const QPolygonF &scenePoly) const;

    /*! \brief Barycentric Z sample at scene point (sx,sy). Returns NaN
     *  when the point is outside every triangle. */
    [[nodiscard]] double sampleZAt(double sx, double sy) const;

    /*! \brief True when this edge slot is a boundary edge in the loaded
     *  mesh (i.e. the corresponding triangle has no neighbour across it).
     *  Boundary classification is derived from the mesh's MeshEdge list
     *  populated by InpMeshReader; non-boundary edges return false. */
    [[nodiscard]] bool isBoundaryEdge(int triIdx, int edgeLocal) const;

    /*! \brief Boundary-edge connectivity graph, used by the edge-select
     *  tool's Ctrl-click shortest-path picking. Built lazily on first use
     *  and cached until the boundary flags are rebuilt.
     *
     *  Returns an empty graph while a progressive load is still finishing
     *  (`sceneGeometryComplete() == false`), since the boundary flags are
     *  not populated yet; the next call after `sceneGeometryReady()`
     *  builds it for real. */
    [[nodiscard]] const mesh::MeshBoundaryGraph &boundaryGraph();

    /*! Engine §11A helper — find the (tri, eLocal) slot on the other side
     *  of an interior edge. Returns `(-1, -1)` when the edge is on the
     *  mesh boundary (no neighbour) or the indices are out of range.
     *  Uses the vertex→triangles adjacency, so requires
     *  `rebuildVertexAdjacency()` to have run.
     *
     *  Public because the Attribute Table's Edges view canonicalises each
     *  interior edge pair onto a single row and needs the same pairing the
     *  conveyance mirror uses. */
    [[nodiscard]] QPair<int,int> findEdgeNeighbour(int triIdx, int edgeLocal) const;

    // ----- Write path (Slice §V.VA / §V.VB / §V.VC) ------------------------
    /*! \brief Set vertex Z. Mutates the layer's MeshResult, rebuilds
     *  scene geometry, emits attributeChanged with the vertex ref name. */
    bool applyMeshVertexZ(int vertexIdx, double z);

    /*! \brief Replace the entire BC value for edge `(triIdx,edgeLocal)`.
     *  Emits attributeChanged. Apply-as-you-go. */
    bool applyMeshEdgeBC(int triIdx, int edgeLocal, const mesh::MeshEdgeBC &bc);

    /*! \brief Engine §11A — set the per-edge conveyance (flux attenuation
     *  multiplier) in [0, 1]. Rejects out-of-range values (the C API does
     *  the same; the toolbar's QDoubleSpinBox range guards against this at
     *  the UI). Interior edges are mirrored to the neighbour slot so the
     *  two halves always carry the same value (matches the engine's
     *  symmetry invariant). Emits attributeChanged for the edited slot. */
    bool applyMeshEdgeConveyance(int triIdx, int edgeLocal, double conveyance);

    /*! \brief Set the vertex's descriptive tag ([2D_VERTICES] TAG column).
     *  Distinct from the coupled node. Emits attributeChanged. */
    bool applyMeshVertexTag(int vertexIdx, const QString &tag);

    /*! \brief Set the vertex's coupled SWMM node ([2D_VERTEX_NODE_MAP]).
     *  Drives the coupled-vertex glyph. Clearing the coupling resets the
     *  coupling Cd/Area to the engine defaults (0.65 / 1.0 m²). Emits
     *  attributeChanged. */
    bool applyMeshVertexCoupledNode(int vertexIdx, const QString &node);

    /*! \brief Set the vertex's coupling discharge coefficient
     *  ([2D_VERTEX_NODE_MAP] CD column). Rejects uncoupled vertices and
     *  non-positive values. Emits attributeChanged. */
    bool applyMeshVertexCouplingCd(int vertexIdx, double cd);

    /*! \brief Set the vertex's coupling exchange area in m²
     *  ([2D_VERTEX_NODE_MAP] AREA column). Rejects uncoupled vertices and
     *  non-positive values. Emits attributeChanged. */
    bool applyMeshVertexCouplingArea(int vertexIdx, double area);

    /*! \brief Replace the node→cell coupling row set wholesale
     *  ([2D_TRIANGLE_NODE_MAP] repeated-row form, Plan Part C). Rows with
     *  out-of-range triangles or empty node ids are dropped. Returns the
     *  previous row set so callers (undo commands) can restore it. */
    QVector<mesh::CellCoupling> applyCellCouplings(
        const QVector<mesh::CellCoupling> &rows);

    /*! \brief Read-only view of the node→cell coupling rows. */
    [[nodiscard]] const QVector<mesh::CellCoupling> &cellCouplings() const
    { return m_mesh.cellCouplings; }

    /*! \brief Set a triangle's Manning's roughness. Emits attributeChanged
     *  with the cell ref name. */
    bool applyMeshTriangleMannings(int triIdx, double mannings);

    /*! \brief Set a triangle's initial water depth in m ([2D_TRIANGLES]
     *  INIT_DEPTH column, default 0 = dry). Emits attributeChanged with the
     *  cell ref name. */
    bool applyMeshTriangleInitDepth(int triIdx, double depth);

    /*! \brief Set a triangle's descriptive tag ([2D_TRIANGLES] TAG column).
     *  Emits attributeChanged with the cell ref name. */
    bool applyMeshTriangleTag(int triIdx, const QString &tag);

    // ----- Selection highlighting (§V follow-up) ---------------------------
    /*! \brief Indices of mesh vertices currently selected for highlight
     *  rendering. Populated by the SelectionManager bridge (filtered to
     *  this layer's mesh by MeshObjectRef::layerKey). */
    [[nodiscard]] const QSet<int> &highlightedVertices() const { return m_selVertices; }
    void setHighlightedVertices(const QSet<int> &indices);

    /*! \brief Flat edge indices (`tri * 3 + edgeLocal`) currently
     *  selected for highlight rendering. */
    [[nodiscard]] const QSet<int> &highlightedEdges()    const { return m_selEdges; }
    void setHighlightedEdges(const QSet<int> &flatIndices);

    /*! \brief Triangle (cell) indices currently selected for highlight
     *  rendering. Populated by the SelectionManager bridge (MeshCell refs). */
    [[nodiscard]] const QSet<int> &highlightedTriangles() const { return m_selTriangles; }
    void setHighlightedTriangles(const QSet<int> &indices);

signals:
    /*! \brief Emitted on any successful apply* mutation. \p refName is
     *  the MeshObjectRef-encoded element name (see meshobjectref.h). */
    void attributeChanged(const QString &refName);

    /*! \brief Emitted on a bulk mutation that has no single element ref
     *  (currently applyCellCouplings). Companion to attributeChanged for
     *  listeners that only need to know "the mesh was edited" — e.g. the
     *  project dirty flag. */
    void meshEditsChanged();

    /*! \brief Emitted when remesh / reload invalidates element indices
     *  in any held selection — toolbar and tools clear on receipt. */
    void selectionInvalidated();

    /*! \brief Emitted when isActiveMesh() changes. */
    void activeMeshChanged(bool isActive);

    /*! \brief Background pyramid (overview) rebuild lifecycle — mirrors
     *  GISRasterLayer::overviewBuildStarted/Finished so the app can drive
     *  the busy bar + status message during the build. */
    void overviewBuildStarted(const QString &layerName);
    void overviewBuildFinished(bool ok);

    /*! \brief The deferred heavy geometry from a progressive load
     *  (finishSceneGeometryAsync) has been adopted — wireframe, spatial
     *  culling and editing structures are now available. */
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
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const override;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected.  Emits \ref rendererChanged() when the pointer
     *          actually changes.
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
     *  and existing UI still work. */
    [[nodiscard]] QList<OpenSWMM::Render::ISublayer *> sublayers() const override;

    /*! Reorder sublayers in paint order (bottom-up).  Emits
     *  repaintRequested() on success.  Returns false on out-of-range indices. */
    bool moveSublayer(int from, int to) override;

    [[nodiscard]] OpenSWMM::Render::MeshFillSublayer    *meshFillSublayer()    const { return m_meshFillSublayer; }
    [[nodiscard]] OpenSWMM::Render::MeshEdgeSublayer    *meshEdgeSublayer()    const { return m_meshEdgeSublayer; }
    [[nodiscard]] OpenSWMM::Render::MeshNodeSublayer    *meshNodeSublayer()    const { return m_meshNodeSublayer; }
    [[nodiscard]] OpenSWMM::Render::ContourBandSublayer *contourBandSublayer() const { return m_contourBandSublayer; }
    [[nodiscard]] OpenSWMM::Render::IsolineSublayer     *isolineSublayer()     const { return m_isolineSublayer; }

    /*! Slice US.3 — bed-elevation range of the loaded mesh, the data range the
     *  contour-band / isoline classification scheme classifies over. */
    [[nodiscard]] double zMin() const { return m_zMin; }
    [[nodiscard]] double zMax() const { return m_zMax; }

    /*! \brief Per-vertex bed-elevation samples, for data-driven classification
     *  (quantile / natural-breaks / std-dev). Strided down to at most
     *  \p maxSamples so a "resample" on a multi-million-vertex mesh stays
     *  responsive; the resulting distribution is representative for binning. */
    [[nodiscard]] QVector<double> elevationSamples(int maxSamples = 200000) const;

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*! Slice U-5 — surface the mesh layer itself as the single styleable
     *  subject for the unified LayerStyleDialog. The Q_PROPERTYs declared
     *  above drive the editor; Q_CLASSINFO("group:...") groups them into
     *  Display / Hillshade / Contours tabs. */
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
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- Scene-geometry structs (public for SWMM2DMeshQSGRenderer) ---------

    /*! Per-triangle: scene-space vertices + per-vertex z (for hillshade). */
    struct SceneTri
    {
        QPointF a, b, c;
        float   zAvg;       ///< Average vertex z — elevation colour.
        float   z0, z1, z2; ///< Per-vertex z — hillshade face normal.
    };

    /*! Per-edge: scene-space line + elevation + slope.
     *  slope = |Δz| / horizontal_distance_in_map_units. */
    struct SceneEdge
    {
        QLineF  line;
        float   zAvg;
        float   slope;
    };

    /*! Per-vertex node dot. */
    struct SceneNode
    {
        QPointF pt;
        float   z;
        bool    tagged; ///< true = SWMM-coupled vertex.
    };

    // Scene-geometry caches — written by rebuildSceneGeometry(),
    // read by SWMM2DMeshQSGRenderer::updatePaintNode().
    QRectF             m_sceneBBox;
    QVector<SceneTri>  m_sceneTris;
    QVector<SceneEdge> m_sceneEdges;
    QVector<SceneNode> m_sceneNodes;
    double             m_zMin     = 0.0;
    double             m_zMax     = 0.0;
    float              m_maxSlope = 0.0f;

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
    QVector<SceneTri>  m_overviewTris;            ///< coarse LOD fill geometry (gap-fill floor)
    double             m_nativeTriSpan = 0.0;     ///< representative native tri edge (scene units)

    // Triangle indices into m_sceneTris sorted by descending scene-space area.
    // Drives adaptive far-zoom culling: at low zoom the painter walks this from
    // the front, drawing real cells while their projected area stays above a
    // pixel threshold and stopping as soon as it drops below — so large cells
    // render faithfully and tiny sub-pixel cells are skipped, in O(kept) rather
    // than O(all). Built alongside the overview for meshes past the threshold.
    QVector<int>       m_trisBySizeDesc;

    /*! \brief True when an LOD overview has been built and is worth using. */
    [[nodiscard]] bool hasOverview() const { return !m_overviewTris.isEmpty(); }

    /*! \brief Rebuild the far-zoom LOD pyramid (overview) on a background
     *  thread. Emits overviewBuildStarted() immediately and
     *  overviewBuildFinished() from the GUI thread when the fresh pyramid
     *  has been adopted (followed by repaintRequested()). No-op while a
     *  build is already running. Surfaced by the "Rebuild pyramid" button
     *  in the layer-properties (LayerStyleDialog) Metadata tab. */
    void rebuildOverviewAsync();

    /*! \brief True while rebuildOverviewAsync() has a build in flight. */
    [[nodiscard]] bool overviewBuildRunning() const { return m_overviewBuildRunning; }

    // Uniform spatial grid over scene-space triangle / edge bboxes.
    // The full type lives in layers/meshspatialgrid.h (extracted so it
    // can be unit-tested without linking the rest of the layer); see
    // that header for storage layout and threading contract.
    MeshSpatialGrid    m_triGrid;
    MeshSpatialGrid    m_edgeGrid;
    QVector<QRectF>    m_triBBoxes;   ///< parallel to m_sceneTris
    QVector<QRectF>    m_edgeBBoxes;  ///< parallel to m_sceneEdges

signals:
    /*! \brief Emitted when setRenderer() swaps the renderer pointer. */
    void rendererChanged();

private:
    void rebuildSceneGeometry();

    /*! \brief Build the coarse LOD overview (m_overviewTris) from
     *  m_sceneTris. Called at the end of rebuildSceneGeometry(); a no-op
     *  for meshes below the size threshold (small meshes render full-res
     *  fast enough that an overview would only add popping). */
    void rebuildOverview();
    void buildRuleListLazy() const;   // Slice B.5b — lazy ruleList init.

    // Slice B.5b — Rule Model mirror over the mesh layer's decoration
    // state. Lazy-built on first ruleList() call.
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;

    // Adapter-ownership refactor — persistent sublayer style adapters
    // (edge + node), lazily built by styleSubjects(), owned via QObject
    // parenting. Held as QObject* because SymbolStyleAdapter::createFor
    // returns one of several sibling adapter classes.
    QObject *m_meshEdgeAdapter = nullptr;
    QObject *m_meshNodeAdapter = nullptr;

    // §V.VA — keep m_bc sized to n_triangles * 3 in sync with the mesh,
    // and rebuild the vertex→triangles adjacency used by sampleZAt /
    // applyMeshVertexZ.
    void resizeBCsToMesh();
    void rebuildVertexAdjacency();

    mesh::MeshResult             m_mesh;
    QString                      m_sourcePath;
    bool                         m_isExternal    = false;
    bool                         m_active        = false;

    // Mesh Tiled LOD plan P1.1 — see qsgOwnsRendering().
    bool                         m_qsgOwnsRendering = false;

    // True while rebuildOverviewAsync() has a worker in flight.
    bool                         m_overviewBuildRunning = false;

    // Progressive load (deferHeavyGeometry ctor mode) — see
    // sceneGeometryComplete() / finishSceneGeometryAsync().
    bool                         m_sceneGeomComplete   = true;
    bool                         m_heavyBuildRunning   = false;
    QVector<QPointF>             m_pendingScenePts;   ///< transient; freed on adopt

    /*! Phase-A-only scene build for the progressive load: triangles, bbox,
     *  z-range, vertex dots and the LOD pyramid — no edges/grids. */
    void rebuildSceneGeometryLight();

    // Hillshade state lives on the mesh-fill sublayer style; we keep a
    // pair of simple knobs the renderer reads from the layer (azimuth,
    // altitude) so the visual exactly reproduces the historic values
    // when the user has not edited anything. The Q_PROPERTY setters
    // forward through the sublayer styles where applicable.
    double                       m_hillshadeAz     = 225.0;   // compass deg
    double                       m_hillshadeAlt    =  35.264; // deg above horizon (asin(1/√3))
    double                       m_hillshadeMinLit =   0.15;  // shadow floor (0..1)

    // Zoom thresholds (min on-screen cell size, px) for edge / vertex passes.
    // Moderate defaults so the wireframe/vertices come in at a sensible zoom
    // (roughly a few px per cell) rather than only at extreme close-up.
    double                       m_edgeMinCellPx   =   3.0;
    double                       m_vertexMinCellPx =   6.0;

    quint64                      m_geomRevision  = 0;
    OGRCoordinateTransformation *m_transform     = nullptr;
    SWMM2DMeshGraphicsItem      *m_graphicsItem  = nullptr;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default SingleSymbolRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // §V.VA — per-edge BC storage, sized to n_triangles * 3. Flat indexed
    // as `tri * 3 + edgeLocal`. Interior-edge entries stay at the default
    // Wall value; engine consults only boundary slots.
    QVector<mesh::MeshEdgeBC>    m_bc;

    // §V.VA — per-vertex CSR adjacency for fast incident-triangle lookup
    // (used by sampleZAt / applyMeshVertexZ). Rebuilt by
    // rebuildVertexAdjacency() in the ctor and on geometry-changing
    // reloads (future).
    QVector<int>                 m_vertTriPtr;  // size = n_vertices + 1
    QVector<int>                 m_vertTriIdx;  // size = sum of incidences

    // §V.VA — precomputed boundary status per (tri, edgeLocal). Flat
    // indexed `tri*3 + eLocal`. true = this edge slot is a boundary edge
    // in the loaded mesh. Rebuilt alongside m_bc / adjacency.
    QVector<bool>                m_isBoundary;

    // Lazily built from m_isBoundary by boundaryGraph(); invalidated
    // wherever m_isBoundary is rebuilt (resizeBCsToMesh / the deferred
    // finishSceneGeometryAsync completion).
    mesh::MeshBoundaryGraph      m_boundaryGraph;
    bool                         m_boundaryGraphValid = false;

    // §V follow-up — selection highlight state. Drives the renderer's
    // selection-overlay pass (cyan dots / cyan edges).
    QSet<int>                    m_selVertices;
    QSet<int>                    m_selEdges;
    QSet<int>                    m_selTriangles;

    // Sublayer host members. Each is QObject-parented to `this`, so the
    // ISublayerHost contract is satisfied without manual delete in the
    // dtor. The mesh layer surfaces these in sublayers() in paint order
    // (bottom-up): fill → contour bands → edges → isolines → vertex
    // markers.
    OpenSWMM::Render::MeshFillSublayer    *m_meshFillSublayer    = nullptr;
    OpenSWMM::Render::MeshEdgeSublayer    *m_meshEdgeSublayer    = nullptr;
    OpenSWMM::Render::MeshNodeSublayer    *m_meshNodeSublayer    = nullptr;
    OpenSWMM::Render::ContourBandSublayer *m_contourBandSublayer = nullptr;
    OpenSWMM::Render::IsolineSublayer     *m_isolineSublayer     = nullptr;

    // User-customisable paint order (Slice GUI-2026-05-30 §2).
    mutable QList<OpenSWMM::Render::ISublayer *> m_sublayerOrder;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
