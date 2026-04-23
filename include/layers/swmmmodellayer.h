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

#include <QColor>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef
#include <QFont>
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
     * \brief Classify a name into its SWMM object class by looking it up in
     *        the geometry cache.
     * \details Used by the SelectionManager bridge to translate the layer's
     *          name-only selection set into typed SWMMObjectRefs.
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

private:
    struct NodeGeom    { double x, y; int objectType; int nodeType; QString name; };
    struct LinkGeom    { QVector<QPointF> vertices; int linkType; QString name; };
    struct CatchGeom   { QVector<QPointF> vertices; QString name; };

    void buildGeometryCache();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);

    SWMM_Engine                  m_engine          = nullptr;

    QString                      m_modelFilePath;
    bool                         m_showNodes       = true;
    bool                         m_showLinks       = true;
    bool                         m_showSubcatchments = true;
    bool                         m_showRainGages   = true;
    bool                         m_showLabels      = false;

    // Slice O — per-object hidden set. Names listed here are skipped by
    // populateScene. Object names are unique across a SWMM model, so a
    // flat QSet<QString> covers nodes / links / subcatchments / gages
    // uniformly.
    QSet<QString>                m_hiddenObjects;

    QVector<NodeGeom>            m_nodes;
    QVector<LinkGeom>            m_links;
    QVector<CatchGeom>           m_catchments;
    QVector<NodeGeom>            m_gages;

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
};

Q_DECLARE_METATYPE(SWMMModelLayer *)
Q_DECLARE_METATYPE(SWMMElementSymbol)

#endif // SWMMMODELLAYER_H
