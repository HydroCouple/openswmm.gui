/*!
 * \file   swmmmodellayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#ifndef SWMMMODELLAYER_H
#define SWMMMODELLAYER_H

#include "layers/openswmmvislayer.h"

#include <cstdint>
#include <memory>
#include <vector>

// Forward declaration — nanoflann types are confined to swmmmodellayer.cpp
// so the header stays free of the nanoflann.hpp template machinery.
struct SWMMKdTrees;

#include <QColor>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef
#include <QFont>
#include <QMap>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QVariantMap>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;

/*!
 * \struct SWMMElementSymbol
 * \brief Rendering style for a class of SWMM network elements.
 */
struct SWMMElementSymbol
{
    QColor  fillColor    = Qt::blue;
    QColor  outlineColor = Qt::darkBlue;
    double  outlineWidth = 1.0;
    double  size         = 8.0;    /*!< Marker diameter / line width in pixels. */
    bool    showLabel    = false;
    QFont   labelFont;
    QColor  labelColor   = Qt::black;
};

/*!
 * \class SWMMModelLayer
 * \brief Renders the SWMM network elements (nodes, links, subcatchments, rain gages)
 *        for one OpenSWMMCore model.
 * \details The layer uses the coordinate frame of the OpenSWMMCore model as its
 *          native CRS.  When the canvas CRS differs, coordinates are reprojected
 *          using GDAL's OGRCoordinateTransformation.
 *
 *          The layer also provides:
 *          - Selection tracking (highlighted with a secondary colour).
 *          - Identify-by-point returning element attributes as QVariantMap.
 *          - Label display driven by element names.
 */
class SWMMModelLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

    // The batched scene-item renderer reads the SoA + GDAL transform
    // directly so paint() is a single tight pass over the cached data.
    friend class SWMMLayerItem;
    // Phase B.3 — the offscreen GL renderer reads m_linkSceneFlat /
    // m_linkVertexOffset / m_linkVertexCount directly to upload the
    // VBO; same private-data access rationale as SWMMLayerItem.
    friend class SWMMLayerGLRenderer;
    // Phase B.RHI — the QSG renderer reads the same flat buffers to
    // populate its QSGGeometryNode vertex data on geometry change.
    friend class SWMMLayerQSGRenderer;

public:
    /*!
     * \enum Category
     * \brief Stable ordinal used by SWMMObjectTreeModel and by the
     *        category-aware visibility API. Order is preserved across
     *        model rebuilds so QTreeView expansion state / selection
     *        round-trip cleanly.
     */
    enum Category {
        CatJunctions = 0,
        CatOutfalls,
        CatStorage,
        CatDividers,
        CatConduits,
        CatPumps,
        CatOrifices,
        CatWeirs,
        CatOutlets,
        CatSubcatchments,
        CatRainGages,
        NumCategories
    };

    Q_PROPERTY(QString modelFilePath  READ modelFilePath  NOTIFY modelFilePathChanged)
    Q_PROPERTY(bool    showNodes      READ showNodes      WRITE setShowNodes
               NOTIFY showNodesChanged)
    Q_PROPERTY(bool    showLinks      READ showLinks      WRITE setShowLinks
               NOTIFY showLinksChanged)
    Q_PROPERTY(bool    showSubcatchments READ showSubcatchments
               WRITE setShowSubcatchments NOTIFY showSubcatchmentsChanged)
    Q_PROPERTY(bool    showRainGages  READ showRainGages  WRITE setShowRainGages
               NOTIFY showRainGagesChanged)
    Q_PROPERTY(bool    showLabels     READ showLabels     WRITE setShowLabels
               NOTIFY showLabelsChanged)

public:

    explicit SWMMModelLayer(const QString &modelFilePath,
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~SWMMModelLayer() override;

    // ----- Model file -----------------------------------------------------

    [[nodiscard]] QString modelFilePath() const;
    void setModelFilePath(const QString &path);

    /** Raw engine handle — valid only after a successful loadModel(). */
    [[nodiscard]] SWMM_Engine engine() const;

    /*!
     * \brief Loads (or reloads) the SWMM input file and rebuilds geometry caches.
     * \returns true on success.
     */
    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /** Close and destroy the engine, clearing all geometry caches. */
    void closeEngine();

    // ----- Element visibility toggles -------------------------------------

    [[nodiscard]] bool showNodes()        const;
    void setShowNodes(bool show);

    [[nodiscard]] bool showLinks()        const;
    void setShowLinks(bool show);

    [[nodiscard]] bool showSubcatchments() const;
    void setShowSubcatchments(bool show);

    [[nodiscard]] bool showRainGages()    const;
    void setShowRainGages(bool show);

    [[nodiscard]] bool showLabels()       const;
    void setShowLabels(bool show);

    /*! Phase B.3 — when true, SWMMLayerItem::paint() skips link / node
     *  / catchment / gage rendering on the assumption that
     *  SWMMLayerGLRenderer is composing them via the offscreen FBO.
     *  Lets us A/B between the CPU and GL paths without rebuilds. */
    [[nodiscard]] bool glRenderingEnabled() const { return m_glRenderingEnabled; }
    void setGlRenderingEnabled(bool on)            { m_glRenderingEnabled = on; }

    // ----- Per-object visibility (Slice O) --------------------------------

    /*!
     * \brief Per-object visibility flag. Names present in
     *        \ref m_hiddenObjects are skipped by \ref populateScene. The
     *        Object Browser drives this state: a leaf checkbox is the
     *        only source of truth for an individual object; group-header
     *        toggles apply in bulk to every child but do not add a
     *        separate gate — after a group toggle the children's states
     *        fully determine visibility, and subsequent per-leaf toggles
     *        don't back-propagate to the header.
     */
    [[nodiscard]] bool isObjectVisible(const QString &name) const;
    void setObjectVisible(const QString &name, bool visible);

    /*!
     * \brief Batch form — apply the same visibility to every name in
     *        \p names with a single \ref repaintRequested emission.
     *        Used by the group-header checkbox so toggling a category
     *        of 1000+ objects causes only one canvas refresh.
     */
    void setObjectsVisible(const QList<QString> &names, bool visible);

    /*!
     * \brief Names currently hidden via per-object toggles. Used by the
     *        Object Browser to seed leaf-row check states on refresh.
     */
    [[nodiscard]] QSet<QString> hiddenObjects() const { return m_hiddenObjects; }

    /*!
     * \brief Largest rendered marker half-bound across every SWMM
     *        element type, in PIXELS. Callers use this as a floor for
     *        the Select tool's pixel tolerance so clicks landing
     *        inside the visible glyph always hit, regardless of the
     *        user's tolerance preference. For a square Storage glyph
     *        the half-bound is half-width * sqrt(2) (diagonal); for
     *        circular Junctions it's the radius; polygons and
     *        polylines return 0 (they're handled by bbox / segment-
     *        distance tests inside identifyAt, which don't need the
     *        floor).
     */
    [[nodiscard]] double maxMarkerHalfBoundPx() const;

    // ----- Category-aware API (consumed by SWMMObjectTreeModel) -----------

    /*!
     * \brief Number of objects in the given category. O(1).
     */
    [[nodiscard]] int categoryCount(Category c) const;

    /*!
     * \brief Object name at (category, row). O(1) — reads through the
     *        per-category index buckets populated in buildGeometryCache.
     *        Returns an empty string if the indices are out of range.
     */
    [[nodiscard]] QString objectNameAt(Category c, int row) const;

    /*!
     * \brief Aggregate check state for a category: Checked when every
     *        member is visible, Unchecked when every member is hidden,
     *        PartiallyChecked otherwise. O(1) — derived from the
     *        per-category hidden-count counter that setObjectVisibleAt /
     *        setCategoryVisible / setObjectVisible all maintain.
     */
    [[nodiscard]] Qt::CheckState categoryCheckState(Category c) const;

    /*!
     * \brief Toggle visibility for a single leaf referenced by
     *        (category, row). Updates m_hiddenObjects plus the
     *        category hidden count and emits repaintRequested() on change.
     *        Preferred over setObjectVisible(name, …) when the caller
     *        already knows the category — avoids the name→(cat,row)
     *        lookup on the hot path of QTreeView::setData().
     */
    void setObjectVisibleAt(Category c, int row, bool visible);

    /*!
     * \brief Bulk-toggle every member of a category. Used by the
     *        group-header checkbox in the Object Browser; emits a single
     *        repaintRequested() regardless of how many objects the
     *        category contains.
     */
    void setCategoryVisible(Category c, bool visible);

    /*!
     * \brief Locate an object by name, returning true and writing
     *        \p cat + \p row on success. O(1) hash lookup against the
     *        `m_objectLocation` map built at cache time. Used by the
     *        SelectionManager → tree model bridge to convert
     *        SWMMObjectRef into a QModelIndex without rescanning the SoA.
     */
    bool findObjectLocation(const QString &name,
                            Category *cat, int *row) const;

    // ----- Category ordering (Slice T.2) ----------------------------------

    /*!
     * \brief User-configurable display order of categories in the
     *        Object Browser. Defaults to the enum sequence at load
     *        time; Slice T.2's drag-and-drop rewrites the vector and
     *        the tree model re-renders in the new order.
     *
     *        Only the display order changes — `categoryCount()` /
     *        `objectNameAt()` still take the enum category so all
     *        existing call sites keep working.
     */
    [[nodiscard]] QVector<Category> categoryOrder() const;

    /*! Replace the full category order. Rejects an input that doesn't
     *  cover every enum value exactly once (safety net so a malformed
     *  vector can't silently drop categories). Emits `categoryOrderChanged`
     *  on success; Object Browser reacts via `modelReset`. */
    void setCategoryOrder(const QVector<Category> &order);

    // ----- Intra-category object ordering (Slice T.3) ---------------------

    /*!
     * \brief Install a user-defined display order for the given
     *        category. `soaIndices` is a permutation of the underlying
     *        SoA indices for the category (m_nodes for CatJunctions /
     *        Outfalls / Storage / Dividers; m_links for CatConduits /
     *        Pumps / Orifices / Weirs / Outlets; direct m_catchments /
     *        m_gages for the area categories). Size and membership must
     *        match exactly, otherwise the call is rejected (defensive:
     *        a malformed vector would silently drop or duplicate
     *        objects).
     *
     *        After a successful call, `categoryCount()` is unchanged
     *        but `objectNameAt(cat, row)` follows the override.
     *        `m_objectLocation` is rewritten for this category so
     *        `findObjectLocation()` returns the new display row.
     *        Emits `categoryOrderChanged()` — the tree model resets
     *        and re-renders in the new order.
     */
    void setObjectOrder(Category cat, const QVector<int> &soaIndices);

    /*! Drop the user override for this category; display falls back
     *  to the default per-category index bucket. */
    void clearObjectOrder(Category cat);

    /*! Read-only access to the current override for a category. Empty
     *  when no override is installed. */
    [[nodiscard]] QVector<int> objectOrder(Category cat) const;

    /*! Default SoA permutation for a category (visible-row → SoA
     *  index) when NO user override is installed. Used by the tree
     *  model's drag-drop handler as the starting point for a drag
     *  on a category that hasn't been reordered before. */
    [[nodiscard]] QVector<int> defaultObjectOrder(Category cat) const;

    // ----- Symbology ------------------------------------------------------

    [[nodiscard]] SWMMElementSymbol junctionSymbol()   const;
    void setJunctionSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol outfallSymbol()    const;
    void setOutfallSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol storageSymbol()    const;
    void setStorageSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol dividerSymbol()    const;
    void setDividerSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol conduitSymbol()    const;
    void setConduitSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol pumpSymbol()       const;
    void setPumpSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol orificeSymbol()    const;
    void setOrificeSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol weirSymbol()       const;
    void setWeirSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol subcatchmentSymbol() const;
    void setSubcatchmentSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol rainGageSymbol()   const;
    void setRainGageSymbol(const SWMMElementSymbol &s);

    // ----- Selection ------------------------------------------------------

    /*!
     * \brief Returns the names of currently selected network elements.
     */
    [[nodiscard]] QStringList selectedElementNames() const;

    /*!
     * \brief Selects network elements by name (replaces prior selection).
     */
    void setSelectedElementNames(const QStringList &names);

    void clearSelection();

    // ----- Identify -------------------------------------------------------

    /*!
     * \brief Returns attributes of the network element closest to the map point.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS (may be nullptr).
     * \param tolerance   Search radius in map units.
     * \returns           Attribute map, or an empty map if nothing was found.
     *                    Includes "elementType", "elementName", and all SWMM properties.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         const SpatialReferenceSystem *canvasSRS,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Convenience overload — no CRS needed.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Identify an object by name (instead of by map coordinates).
     * \details Used by panels that already know the object reference and just
     *          need its attribute map (Object Browser → AttributePanel).
     *          Returns an empty map if the name doesn't match any cached
     *          node / link / subcatchment / gage.
     */
    [[nodiscard]] QVariantMap identifyByName(const QString &name) const;

    /*!
     * \brief Layer-CRS bounding box of a cached object by name.
     * \details
     *  - Node / Rain Gage: single-point extent (caller pads for a usable zoom).
     *  - Link:             bbox of the cached polyline (endpoints + vertices).
     *  - Subcatchment:     bbox of the cached polygon ring.
     *  Returns an invalid `MapExtent` if the name isn't known, or for a
     *  polygon/polyline with < 1 vertex. The canvas's zoom-to-object flow
     *  uses this so subcatchments and links frame their whole geometry
     *  instead of falling through the "needs X/Y" branch.
     */
    [[nodiscard]] class MapExtent objectExtent(const QString &name) const;

    // ----- Spatial-index queries (O(log N + k), backed by nanoflann KD-tree) ---

    /*!
     * \brief Names of all nodes (any type) whose layer-CRS coordinates fall
     *        inside the rectangle given in **canvas CRS**.
     *        The inverse CRS transform is applied internally, matching the
     *        CRS-aware behaviour of identifyAt.  Hidden objects are excluded.
     *        O(log N + k) via the internal KD-tree; rebuilt lazily after any
     *        coordinate-mutating operation (applyNodeMove / applyNodeAdd /
     *        rollbackTailNodeAdd).
     */
    [[nodiscard]] QStringList nodesInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

    /*!
     * \brief Names of all rain gages whose coordinates fall inside the
     *        canvas-CRS rectangle. Same semantics as nodesInRect.
     */
    [[nodiscard]] QStringList gagesInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

    /*!
     * \brief Names of all links whose polyline bbox overlaps the
     *        canvas-CRS rectangle. Backed by a per-link bbox cache
     *        (built in buildGeometryCache, kept fresh on every
     *        coord-mutating edit). Hidden objects excluded.
     *
     *        Avoids the O(N²) trap the rubber-band tool previously hit
     *        — every link iteration used to call `linkIndex(name)` (a
     *        linear name-scan of m_links) just to look up the polyline
     *        for a fresh bbox compute.
     */
    [[nodiscard]] QStringList linksInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

    /*!
     * \brief Names of all subcatchments whose polygon bbox overlaps
     *        the canvas-CRS rectangle. Same caching pattern as
     *        `linksInRect`.
     */
    [[nodiscard]] QStringList subcatchmentsInRect(double canvasMinX, double canvasMinY,
                                                  double canvasMaxX, double canvasMaxY) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*!
     * \brief Re-read every node / link / subcatchment coordinate from the
     *        engine and rebuild the geometry cache + extent.
     * \details Use after a coordinate-mutating operation (CRS reproject, bulk
     *          coordinate edit) so cached vertices match engine state.
     */
    void reloadGeometry();

    /*!
     * \brief Classify a name into its SWMM object class via O(1) lookup
     *        against `m_objectLocation`.
     * \details Used by the SelectionManager bridge to translate the layer's
     *          name-only selection set into typed SWMMObjectRefs. Hot path
     *          on rubber-band selection (called once per selected name);
     *          must stay O(1) — do not regress to a linear SoA scan.
     * \return  0=Unknown, 1=Node, 2=Link, 3=Subcatchment, 4=RainGage —
     *          matches the SWMMObjectRef::ObjectType enum values.
     */
    [[nodiscard]] int objectTypeFor(const QString &name) const;

    // ----- Geometry editing API (Phase 2) --------------------------------

    /*!
     * \brief Returns the cache index of the node with the given name, or -1
     *        if none matches.
     */
    [[nodiscard]] int nodeIndex(const QString &name) const;

    /*!
     * \brief Returns the cache index of the link with the given name, or -1
     *        if none matches.
     */
    [[nodiscard]] int linkIndex(const QString &name) const;

    /*!
     * \brief Cached layer-CRS coordinate of a node by index.
     * \returns true on success; x / y untouched on failure.
     */
    bool cachedNodeCoord(int idx, double *x, double *y) const;

    /*!
     * \brief Layer-CRS position of any named element (node, gage, or link
     *        midpoint) by object name.
     * \details Uses the internal name→SoA hash for O(1) lookup. For links
     *          the midpoint of the cached polyline is returned.
     * \returns true on success; x / y untouched on failure.
     */
    bool elementPosition(const QString &name, double *x, double *y) const;

    /*!
     * \brief Cached layer-CRS polyline of a link by index, including the
     *        endpoint coordinates of its from/to nodes.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkPolyline(int idx) const;

    /*!
     * \brief Cached interior-only polyline of a link (endpoints stripped).
     * \details The engine's set_link_vertices API writes interior points
     *          only; this helper returns the corresponding subsection of
     *          the cached polyline for round-trip edits.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkInteriorVertices(int idx) const;

    /*!
     * \brief Cached layer-CRS polygon of a subcatchment by index. Vertex
     *        order matches the .inp [Polygons] section. Returns empty
     *        when \p idx is out of range or the subcatchment has no
     *        polygon (engine permits coordinate-less subcatchments).
     */
    [[nodiscard]] QVector<QPointF> cachedSubcatchVertices(int idx) const;

    /*!
     * \brief Number of subcatchments cached. Equivalent to
     *        `categoryCount(CatSubcatchments)` but provided alongside
     *        \ref cachedSubcatchVertices for direct iteration.
     */
    [[nodiscard]] int cachedSubcatchCount() const;

    /*! Monotonically increasing counter, bumped at the end of every
     *  rebuildSceneCoords() call.  Renderers can compare against a cached
     *  value to cheaply detect whether scene geometry has changed without
     *  storing a pointer or connecting a signal. */
    [[nodiscard]] quint64 geomRevision() const { return m_geomRevision; }

    // ----- Simulation-options pass-through (Slice U) ----------------------

    /*!
     * \brief Read a single SWMM OPTIONS key from the engine. Returns the
     *        \p fallback when the engine has no value for the key or the
     *        engine isn't open.
     *
     *        Kept as a layer method (rather than going direct to
     *        `swmm_options_get`) so the SimulationOptionsDialog sees a
     *        single model object rather than a mix of engine + layer
     *        APIs, and so the optionsChanged() signal stays a single
     *        pipe.
     */
    [[nodiscard]] QString getOption(const QByteArray &key,
                                    const QString    &fallback = {}) const;

    /*!
     * \brief Write a single SWMM OPTIONS key. On success emits
     *        `optionsChanged({key})`. Returns true on success, false if
     *        the engine isn't open or rejected the value.
     *
     *        Batch-apply should prefer `setOptions(QMap)` to emit a
     *        single multi-key signal per round-trip.
     */
    bool setOption(const QByteArray &key, const QString &value);

    /*!
     * \brief Batch write — applies a set of key/value pairs, then emits
     *        `optionsChanged(changedKeys)` once. Returns the number of
     *        keys the engine accepted.
     */
    int  setOptions(const QMap<QByteArray, QString> &values);

    /*!
     * \brief Returns the conduit length recorded in the engine, or -1 if
     *        the link is not a conduit / is out of range.
     */
    [[nodiscard]] double engineLinkLength(int linkIdx) const;

    /*!
     * \brief Returns true when the link at \p linkIdx is a conduit (the
     *        only link type whose length is stored as an independent
     *        geometry value and is therefore eligible for auto-length).
     */
    [[nodiscard]] bool isConduit(int linkIdx) const;

    // ----- Tool-facing hit-test (Slice R Phase 3) -------------------------

    /*!
     * \struct PickResult
     * \brief Tool-facing click-pick result. `valid` is false when no
     *        feature was hit within \p tolerance of the canvas-CRS
     *        click point. `cat` and `soaIndex` give the typed SoA
     *        position so tools can reach the underlying geometry
     *        without a second name → index lookup. `name` is the
     *        SWMM object id (matches selection-bus `SWMMObjectRef::name`).
     */
    struct PickResult {
        bool     valid    = false;
        Category cat      = NumCategories;
        int      soaIndex = -1;
        QString  name;
    };

    /*! Tiered click-pick in the same priority order as `identifyAt`
     *  (nodes + gages first, then links at 1/3 tolerance, then
     *  subcatchments via point-in-polygon). Returns the typed hit so
     *  MoveNode / EditVertex / Select-tool right-click don't need to
     *  go through `scene->items()` + dynamic_cast on retiring
     *  `NodeGraphicsItem` / `LinkGraphicsItem` placeholders.
     *
     *  \p sceneX \p sceneY are in canvas CRS (the same coordinate
     *  system the Select tool's `selectAtPoint` passes to
     *  `identifyAt`). \p tolerance is also in canvas CRS units; the
     *  layer inverts through its m_transform as needed. */
    [[nodiscard]] PickResult pickAt(double sceneX, double sceneY,
                                    double tolerance) const;

    /*! Live-preview a node move — mutates the cached SoA coord plus
     *  every attached link's endpoint, emits `repaintRequested()`,
     *  but does NOT write through to the engine. Called by
     *  MapToolMoveNode on every mouseMoveEvent during a drag;
     *  MoveNodeCommand::redo commits the final position via
     *  `applyNodeMove` when the drag is released. Returns false if
     *  the index is out of range or no node cache exists. */
    bool previewNodeMove(int idx, double newX, double newY);

    /*!
     * \brief Indices of links whose from/to endpoint is the given node.
     */
    [[nodiscard]] QVector<int> linksAttachedToNode(int nodeIdx) const;

    /*!
     * \brief Which end of a link is attached to \p nodeIdx: 0 = from,
     *        1 = to, or -1 if the node is not an endpoint of the link.
     */
    [[nodiscard]] int linkEndForNode(int linkIdx, int nodeIdx) const;

    /*!
     * \brief Apply a new coordinate to a node: engine + cache + attached
     *        link endpoint updates. Does not push an undo command — the
     *        caller (tool or MoveNodeCommand) is responsible for that.
     * \param idx                Cache/engine node index.
     * \param newX, newY         New coordinate in the layer CRS.
     * \returns                  true on success.
     *
     * Emits repaintRequested() on success.
     */
    bool applyNodeMove(int idx, double newX, double newY);

    /*!
     * \brief Write the conduit length for a link. No-op if the link is
     *        not a conduit.
     */
    bool applyLinkLength(int linkIdx, double length);

    /*!
     * \brief Apply interior vertices to a link: engine + cache, rebuilding
     *        the cached polyline from the node endpoints + new interior.
     */
    bool applyLinkInteriorVertices(int linkIdx, const QVector<QPointF> &interior);

    /*!
     * \brief Add a new node: engine + cache. Engine must be OPENED.
     * \param name      Unique null-terminated node identifier.
     * \param nodeType  0=Junction, 1=Outfall, 2=Storage, 3=Divider
     *                  (matches SWMM_NodeType).
     * \param x, y      Initial coordinate in the layer CRS.
     * \param[out] outIdx  Newly assigned node index on success.
     * \returns true on success. On failure \p outIdx is -1.
     */
    bool applyNodeAdd(const QString &name, int nodeType,
                      double x, double y,
                      int *outIdx = nullptr);

    /*!
     * \brief Undo an add by removing the *tail* entry of the node cache.
     * \details Only valid immediately after applyNodeAdd when the new node
     *          is still at the end of the node list. Used by AddNodeCommand::undo
     *          while the engine lacks a general-purpose swmm_node_remove.
     *          Returns false if the tail name doesn't match \p name.
     */
    bool rollbackTailNodeAdd(const QString &name);

    /*!
     * \brief Add a new link: engine + cache.
     * \param name             Unique identifier.
     * \param linkType         0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet.
     * \param fromNodeName     From-node name (must already exist).
     * \param toNodeName       To-node name (must already exist).
     * \param interiorVertices Interior (non-endpoint) polyline points.
     * \param[out] outIdx      Link index on success.
     */
    bool applyLinkAdd(const QString &name, int linkType,
                      const QString &fromNodeName, const QString &toNodeName,
                      const QVector<QPointF> &interiorVertices,
                      int *outIdx = nullptr);

    /*! Undo tail link add (swmm_link_pop_last). */
    bool rollbackTailLinkAdd(const QString &name);

    /*! Add a rain gage: engine + cache. */
    bool applyGageAdd(const QString &name, double x, double y,
                      int *outIdx = nullptr);

    /*! Undo tail gage add (swmm_gage_delete on tail). */
    bool rollbackTailGageAdd(const QString &name);

    /*! Add a subcatchment polygon: engine + cache. */
    bool applySubcatchAdd(const QString &name,
                          const QVector<QPointF> &polygon,
                          int *outIdx = nullptr);

    /*! Undo tail subcatch add (swmm_subcatch_delete on tail). */
    bool rollbackTailSubcatchAdd(const QString &name);

    /*!
     * \brief Delete a node, cascade-deleting all attached links.
     * \details Identifies cascade links before deletion so the caller can
     *          snapshot them. Modifies engine state + all caches.
     * \param name                 Node to delete.
     * \param[out] cascadeLinkNames Names of links deleted as cascade.
     */
    bool applyNodeDelete(const QString &name,
                         QStringList *cascadeLinkNames = nullptr);

    /*! Delete a single link. */
    bool applyLinkDelete(const QString &name);

    /*! Delete a rain gage. */
    bool applyGageDelete(const QString &name);

    /*! Delete a subcatchment. */
    bool applySubcatchDelete(const QString &name);

signals:
    void modelFilePathChanged(const QString &path);
    void showNodesChanged(bool show);
    void showLinksChanged(bool show);
    void showSubcatchmentsChanged(bool show);
    void showRainGagesChanged(bool show);
    void showLabelsChanged(bool show);
    void selectionChanged(const QStringList &selectedNames);
    void modelLoaded();
    void modelLoadError(const QString &errorMessage);

    /*!
     * \brief Emitted after any OPTIONS key has been written to the engine
     *        (via `setOption()` — the single entry point used by the
     *        SimulationOptionsDialog's Apply / OK).  Carries the list of
     *        keys that changed in this batch so observers can decide
     *        whether the change is relevant.
     *
     *        Enables MVC-style live sync: the main-window Flow Units
     *        combo, the status-bar Offset-Mode checkbox, and any other
     *        UI mirroring engine state bind once and refresh through
     *        this signal instead of polling on tab-switch.
     */
    void optionsChanged(const QStringList &keys);

    /*! Emitted when `setCategoryOrder()` accepts a new vector. The
     *  Object Browser tree model listens and reshapes without
     *  recomputing the per-category hidden counts (category membership
     *  didn't change, only the display order). */
    void categoryOrderChanged();

    /*! Emitted whenever the set of elements changes — an add, remove,
     *  or rollback of any node / link / gage / subcatchment. Panels
     *  that show element lists (Object Browser, Attribute Table) connect
     *  to this signal and call refresh() so their views stay in sync
     *  without polling. */
    void geometryChanged();

private:
    struct NodeGeom    { double x, y; int objectType; int nodeType; QString name; };
    struct LinkGeom    { QVector<QPointF> vertices; int linkType; QString name; };
    struct CatchGeom   { QVector<QPointF> vertices; QString name; };

    /*! Scene-space drainage connector: PIA of subcatchment → outlet node/subcatchment. */
    struct OutletLine  { QLineF line; int catchIdx; };

    void buildGeometryCache();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);
    void rebuildKdTrees() const;  ///< (Re-)build the nanoflann node + gage trees.
    void ensureKdTrees()  const;  ///< Rebuild only if m_kdDirty is set.

    /*!
     * \brief Recompute every cached scene-coordinate (links, nodes,
     *        catchments, gages) from the SoA + current m_transform.
     *        Called from buildGeometryCache (geometry change) and from
     *        rebuildTransform (CRS change). Edit paths refresh the
     *        affected entries directly via refreshSceneCoordsForNode /
     *        refreshSceneCoordsForLink to avoid a full rebuild on a
     *        single drag preview.
     *
     *        The cached points include the scene-space Y-flip
     *        (toScene(mx, my) = QPointF(mx, -my)) so SWMMLayerItem::paint
     *        can hand them straight to QPainter::drawLines without any
     *        per-vertex math.
     */
    void rebuildSceneCoords();
    void refreshSceneCoordsForNode(int nodeIdx);
    void refreshSceneCoordsForLink(int linkIdx);

    // Incremented at the end of every rebuildSceneCoords() call.
    // SWMMLayerQSGRenderer uses this to invalidate its subcatchment
    // triangulation cache without needing a signal or pointer comparison.
    quint64 m_geomRevision = 0;

    /*!
     * \brief Uniform-grid spatial index over scene-space link bboxes.
     *        Phase A.1 of the 5M-rendering plan (docs/RENDERING_5M_PLAN.md).
     *
     *        SWMMLayerItem::paint queries `query(exposed)` to get the SoA
     *        indices of links whose bbox intersects the viewport, instead
     *        of iterating all m_links and bbox-testing each one. At zoomed
     *        in views this collapses paint's per-link work from O(N) to
     *        O(visible). When the GL pipeline lands (Phase B), the same
     *        query result feeds glDrawArrays so the GPU upload subrange
     *        and the CPU cull share one index.
     *
     *        Grid is keyed off m_linkSceneBBoxes — rebuilt by
     *        rebuildSceneCoords() (geometry / CRS change). Cell size is
     *        chosen automatically as ~16x the median link bbox diagonal
     *        so most links live in 1–2 cells; outliers get inserted into
     *        every cell they cross (no clipping).
     *
     *        Memory: vector<int> per cell, total entries ≈ N for short
     *        links. At 5M with ~100x100 grid → ~500 entries/cell on avg.
     *        Build is O(N); query is O(cells_in_rect + entries_returned).
     */
    struct LinkSpatialGrid
    {
        QRectF       extent;         ///< total bbox covered by the grid (scene space)
        double       cellW = 0.0;
        double       cellH = 0.0;
        int          cols  = 0;
        int          rows  = 0;
        QVector<QVector<int>> cells; ///< size cols*rows; cell (cx,cy) -> link SoA indices

        void clear() { extent = {}; cellW = cellH = 0.0; cols = rows = 0; cells.clear(); }
        [[nodiscard]] bool isEmpty() const { return cells.isEmpty(); }

        /*! Rebuild the grid from a parallel array of scene-space bboxes
         *  (one per link, indexed by SoA position). Called by
         *  rebuildSceneCoords(). */
        void rebuild(const QVector<QRectF> &linkBBoxes);

        /*! Indices of links whose cached bbox intersects \p rect. Order
         *  is grid-traversal order, not SoA order — paint can rely on
         *  this to skip the bbox-intersect check inside its loop. */
        [[nodiscard]] QVector<int> query(const QRectF &rect) const;
    };
    LinkSpatialGrid              m_linkGrid;

    /*!
     * \brief Rebuild the per-category index buckets (m_nodesByType,
     *        m_linksByType), the name→(category, row) lookup, and the
     *        per-category hidden-count array from m_hiddenObjects.
     *        Called whenever the SoA is repopulated (loadModel / add /
     *        remove). Independent of the extent computation in
     *        buildGeometryCache().
     */
    void rebuildCategoryIndex();

    SWMM_Engine                  m_engine          = nullptr;

    QString                      m_modelFilePath;
    bool                         m_showNodes       = true;
    bool                         m_showLinks       = true;
    bool                         m_showSubcatchments = true;
    bool                         m_showRainGages   = true;
    bool                         m_showLabels      = false;
    // Phase B.RHI — disabled by default until the QQuickWidget Metal-layer
    // compositing issue is resolved (the opaque CAMetalLayer covers the
    // raster m_frameBuffer content regardless of setClearColor(transparent)).
    // With this false, SWMMLayerItem::paint() renders links / nodes /
    // subcatchments / gages via QPainter into the QGraphicsScene, selection
    // highlights (yellow) work correctly, and basemap / DTM / mesh remain
    // visible because no Metal layer is composited on top.
    bool                         m_glRenderingEnabled = false;

    // Slice O — per-object hidden set. Names listed here are skipped by
    // populateScene. Object names are unique across a SWMM model, so a
    // flat QSet<QString> covers nodes / links / subcatchments / gages
    // uniformly.
    QSet<QString>                m_hiddenObjects;

    QVector<NodeGeom>            m_nodes;
    QVector<LinkGeom>            m_links;
    QVector<CatchGeom>           m_catchments;
    QVector<NodeGeom>            m_gages;

    // Per-feature bbox caches, parallel to m_links / m_catchments.
    // Computed once in buildGeometryCache() and refreshed on edits
    // that touch coords (applyNodeMove / applyLinkInteriorVertices /
    // applyNodeAdd). linksInRect / subcatchmentsInRect iterate these
    // arrays directly — no per-call name lookup, no per-call vertex
    // loop. Big-model rubber-band selects went from O(N²) (linkIndex
    // linear scan + per-link vertex bbox) to O(N) with constant work
    // per item.
    QVector<MapExtent>           m_linkBboxes;
    QVector<MapExtent>           m_catchBboxes;

    // Scene-space coordinate cache, parallel to the SoAs above. Computed
    // once in rebuildSceneCoords() (on geometry change or CRS change) and
    // refreshed incrementally by edit paths. SWMMLayerItem::paint reads
    // from these directly — no per-vertex Transform()/toScene() call on
    // the paint hot path. Per-feature scene-space bounding rects support
    // viewport cull without a per-paint per-link bbox compute.
    QVector<QPointF>             m_nodeScenePts;
    QVector<QRectF>              m_linkSceneBBoxes;
    QVector<QVector<QPointF>>    m_catchScenePts;
    QVector<QRectF>              m_catchSceneBBoxes;
    QVector<QPointF>             m_gageScenePts;
    QVector<OutletLine>          m_catchOutletLines;  ///< PIA → outlet, built in rebuildSceneCoords.

    // Phase A.3 — flat-array link scene-coords. Replaces the previous
    // per-link QVector<QPointF> with one big std::vector<float> of
    // interleaved (x, y) pairs, plus per-link (offset, count). At 5M
    // links this saves ~120 MB of QVector overhead and gives the GL
    // pipeline (Phase B) a buffer it can `memcpy` straight into a VBO.
    //
    //   m_linkSceneFlat[(m_linkVertexOffset[i] + v) * 2 + 0]  =  x
    //   m_linkSceneFlat[(m_linkVertexOffset[i] + v) * 2 + 1]  =  y   (Y-flipped)
    //   v in [0, m_linkVertexCount[i])
    //
    // Floats not doubles: GL VBOs want floats, and link coords easily
    // fit single precision once translated to scene space. Y is
    // pre-flipped so paint feeds straight into QPainter::drawLines /
    // glDrawArrays without per-vertex math. Any vertex-count change
    // (rare; only when an editor adds/removes a vertex) triggers a
    // full rebuildSceneCoords; in-place node moves only rewrite the
    // affected link's slice of m_linkSceneFlat.
    std::vector<float>           m_linkSceneFlat;
    std::vector<uint32_t>        m_linkVertexOffset;   // size == m_links.size()
    std::vector<uint32_t>        m_linkVertexCount;    // size == m_links.size()

    // Phase A.2 of the 5M-rendering plan: selection / hidden state as
    // dense byte arrays parallel to the SoAs. SWMMLayerItem::paint reads
    // these directly instead of hashing each name into a QSet<QString>
    // every frame. Maintained alongside m_selectedNames / m_hiddenObjects
    // — those QString-keyed members remain the canonical state (used by
    // identify, attribute panel, persistence) but the flag arrays are
    // the fast path on the paint loop. Kept coherent by
    // setSelectedElementNames / setObjectVisible* / rebuildCategoryIndex.
    std::vector<uint8_t>         m_nodeSelectedFlag;   // 1 if m_nodes[i] selected
    std::vector<uint8_t>         m_linkSelectedFlag;
    std::vector<uint8_t>         m_catchSelectedFlag;
    std::vector<uint8_t>         m_gageSelectedFlag;
    std::vector<uint8_t>         m_nodeHiddenFlag;
    std::vector<uint8_t>         m_linkHiddenFlag;
    std::vector<uint8_t>         m_catchHiddenFlag;
    std::vector<uint8_t>         m_gageHiddenFlag;

    // Per-category row → SoA-index buckets. Built in buildGeometryCache,
    // cleared in closeEngine. Used by the virtualised Object Browser tree
    // model to resolve QModelIndex (category, row) → backing name in O(1)
    // without scanning the whole SoA on every data() call.
    QVector<int>                 m_nodesByType[4];   // junction/outfall/storage/divider
    QVector<int>                 m_linksByType[5];   // conduit/pump/orifice/weir/outlet

    // Per-category count of hidden members. Kept in sync by
    // setObjectVisible, setObjectVisibleAt, setCategoryVisible so
    // categoryCheckState() is O(1). Seeded to 0 at cache rebuild.
    int                          m_hiddenCountByCategory[NumCategories] = {};

    // User-controlled display order for the Object Browser (Slice T.2).
    // Seeded to the enum sequence in rebuildCategoryIndex() so a fresh
    // model starts with the default order.
    QVector<Category>            m_categoryOrder;

    // Per-category object order overrides (Slice T.3). Absent key =
    // category uses its default bucket order; present key = visible
    // row r maps to `m_objectOrderOverrides[c][r]`, which is the SoA
    // index for nodes (m_nodes) / links (m_links) / catch (m_catchments)
    // / gages (m_gages). Sparse by design: only categories the user
    // actually reordered carry memory. Cleared on geometry rebuild so
    // stale indices can't survive an add/remove.
    QHash<Category, QVector<int>> m_objectOrderOverrides;

    // name → (category, row) lookup seeded at cache time. Serves the
    // legacy name-based setObjectVisible(name, bool) entry point and
    // the SelectionManager → model-index mapping in the Object Browser
    // without a full O(N) scan of m_nodes / m_links per call.
    QHash<QString, QPair<Category, int>> m_objectLocation;

    // Phase A.2 — name → (kind, SoA index) for the flag-array paint
    // path. `kind` selects which array (m_nodes / m_links /
    // m_catchments / m_gages) the SoA index addresses. Built alongside
    // m_objectLocation in rebuildCategoryIndex and kept coherent on
    // edits. Distinct from m_objectLocation because that one stores
    // *display* row inside per-category buckets, whereas paint needs
    // the raw SoA index — different translation, different consumers.
    enum class SoaKind : int8_t { Node = 0, Link = 1, Catch = 2, Gage = 3 };
    struct SoaLocation { SoaKind kind; int soaIdx; };
    QHash<QString, SoaLocation>  m_nameToSoa;

    /*! Refresh `m_*SelectedFlag` and `m_*HiddenFlag` from
     *  `m_selectedNames` + `m_hiddenObjects`. Cost: O(|selection| +
     *  |hidden|), bounded by user actions, not by total object count.
     *  Called on every selection / visibility mutation so the flag
     *  arrays match the canonical QString-keyed state. */
    void rebuildFlagArrays();

    SWMMElementSymbol            m_junctionSym;
    SWMMElementSymbol            m_outfallSym;
    SWMMElementSymbol            m_storageSym;
    SWMMElementSymbol            m_dividerSym;
    SWMMElementSymbol            m_conduitSym;
    SWMMElementSymbol            m_pumpSym;
    SWMMElementSymbol            m_orificeSym;
    SWMMElementSymbol            m_weirSym;
    SWMMElementSymbol            m_subcatchSym;
    SWMMElementSymbol            m_gageSym;

    QStringList                  m_selectedNames;

    // GDAL transform (layer CRS → canvas CRS)
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool                         m_needsRebuild = true;

    // KD-tree spatial index for point-feature queries (nodes + gages).
    // Mutable so const query methods (identifyAt, nodesInRect, …) can
    // trigger a lazy rebuild without breaking const-correctness.
    mutable bool                                 m_kdDirty = true;
    mutable std::unique_ptr<SWMMKdTrees>         m_kdTrees;

    // Batched scene renderer (created in populateScene). Edit paths that
    // mutate SoA coordinates call refreshBoundingRect() on it so the
    // scene's BSP index stays aligned after moves beyond the prior
    // extent. Cleared in depopulateScene.
    class SWMMLayerItem         *m_batchedItem = nullptr;
};

Q_DECLARE_METATYPE(SWMMModelLayer *)
Q_DECLARE_METATYPE(SWMMElementSymbol)

#endif // SWMMMODELLAYER_H
