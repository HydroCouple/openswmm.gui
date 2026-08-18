/*!
 * \file   mapundostack.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef MAPUNDOSTACK_H
#define MAPUNDOSTACK_H

#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QUndoStack>
#include <QVector>
#include "map/mapextent.h"
#include "layers/swmmmodellayer.h"   // SWMMModelLayer::Category used by ReorderCategoriesCommand below
#include "selection/selectionmanager.h"   // SWMMObjectRef used by DeleteDataObjectCommand below

// ---------------------------------------------------------------------------
// Plain-data snapshots used by delete / undo
// ---------------------------------------------------------------------------

/*! All properties needed to re-create a deleted node. */
struct NodeSnapshot
{
    QString name;
    int     nodeType  = 0;
    double  x = 0, y = 0;
    double  invertElev     = 0;
    double  maxDepth       = 0;
    double  initDepth      = 0;
    double  surchargeDepth = 0;
    double  pondedArea     = 0;
    // virtual junction: the flag plus its rendering-only ground depth. Without
    // these, undoing the delete of a virtual junction resurrected a plain
    // junction (and the drawn ground line with it).
    int     isVirtual      = 0;
    double  rimDepth       = 0;
    // outfall-specific
    int     outfallType    = 0;
    int     outfallFlapGate = 0;
    // storage-specific
    double  seepRate       = 0;
    // divider-specific
    int     dividerType    = 0;
};

/*! All properties needed to re-create a deleted link. */
struct LinkSnapshot
{
    QString          name;
    int              linkType    = 0;
    QString          fromNode;
    QString          toNode;
    QVector<QPointF> interiorVertices;
    double  length          = 0;
    double  roughness       = 0;
    double  offsetUp        = 0;
    double  offsetDn        = 0;
    double  crestHeight     = 0;
    double  dischargeCoeff  = 0;
    double  endContractions = 0;
    int     flapGate        = 0;
    int     pumpInitState   = 0;
};

/*! Minimal snapshot for a deleted rain gage. */
struct GageSnapshot
{
    QString name;
    double  x = 0, y = 0;
};

/*! Minimal snapshot for a deleted subcatchment. */
struct SubcatchSnapshot
{
    QString          name;
    QVector<QPointF> polygon;
    double  area      = 0;
    double  width     = 0;
    double  slope     = 0;
    double  impervPct = 0;
};

/*!
 * \class MapUndoStack
 * \brief An undo/redo stack for map operations with a configurable maximum depth.
 * \details Extends QUndoStack to expose the undo limit as a Q_PROPERTY so it
 *          can be configured in the application settings and bound to UI controls.
 *
 *          The default limit is 50 operations.  Set to 0 for unlimited.
 *
 * Usage:
 * \code
 *   MapUndoStack *stack = new MapUndoStack(this);
 *   stack->setMaxUndoCount(100);
 *   stack->push(new PanCommand(oldExtent, newExtent, canvas));
 *   stack->undo();
 * \endcode
 */
class MapUndoStack : public QUndoStack
{
    Q_OBJECT
    Q_PROPERTY(int maxUndoCount READ maxUndoCount WRITE setMaxUndoCount
               NOTIFY maxUndoCountChanged)

public:

    explicit MapUndoStack(QObject *parent = nullptr);

    /*!
     * \brief Returns the maximum number of undoable steps stored (0 = unlimited).
     */
    [[nodiscard]] int maxUndoCount() const;

    /*!
     * \brief Sets the maximum number of stored undo operations.
     * \details If the current stack depth exceeds the new limit, oldest commands
     *          are silently discarded.
     * \param count  Maximum depth; 0 means unlimited.
     */
    void setMaxUndoCount(int count);

    /*!
     * \brief Convenience: loads the undo limit from QSettings
     *        (key "MapCanvas/undoLimit").
     */
    void loadSettings();

    /*!
     * \brief Saves the current undo limit to QSettings.
     */
    void saveSettings() const;

signals:
    void maxUndoCountChanged(int count);

private:
    static constexpr int DefaultMaxUndoCount = 50;
};

// ---------------------------------------------------------------------------
// Base class for all undoable map commands
// ---------------------------------------------------------------------------

class MapCanvas;
class OpenSWMMVisLayer;

/*!
 * \class MapCommand
 * \brief Abstract base for all undoable commands that affect the MapCanvas.
 * \details Subclasses store the before/after state needed to redo/undo the
 *          operation.  They receive a pointer to the MapCanvas so they can
 *          apply the change.
 */
class MapCommand : public QUndoCommand
{
public:

    /*!
     * \brief Constructs a map command with a human-readable \p text.
     * \param canvas  Non-owning pointer to the MapCanvas the command affects.
     * \param parent  Optional parent QUndoCommand (for command merging).
     */
    explicit MapCommand(const QString &text, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    [[nodiscard]] MapCanvas *canvas() const;

protected:
    MapCanvas *m_canvas; /*!< Non-owning pointer to the target canvas. */
};

// ---------------------------------------------------------------------------
// Concrete commands
// ---------------------------------------------------------------------------

class MapExtent;
class SpatialReferenceSystem;
class SWMMModelLayer;

/*!
 * \class PanZoomCommand
 * \brief Records an extent change (pan or zoom) for undo/redo.
 */
class PanZoomCommand : public MapCommand
{
public:
    PanZoomCommand(const MapExtent &oldExtent,
                   const MapExtent &newExtent,
                   MapCanvas *canvas,
                   QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    /*!
     * \brief Attempts to merge consecutive pan operations into a single command.
     */
    bool mergeWith(const QUndoCommand *other) override;
    int id() const override { return 1; }

private:
    MapExtent m_oldExtent;
    MapExtent m_newExtent;
};

/*!
 * \class ChangeCRSCommand
 * \brief Records a canvas CRS change for undo/redo.
 */
class ChangeCRSCommand : public MapCommand
{
public:
    ChangeCRSCommand(const QString &oldAuthCode,
                     const QString &newAuthCode,
                     MapCanvas *canvas,
                     QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QString m_oldAuthCode;  /*!< e.g. "EPSG:4326" */
    QString m_newAuthCode;
    bool    m_firstRedo = true; /*!< Skip first redo — CRS already applied by setCanvasSRS. */
};

/*!
 * \class AddLayerCommand
 * \brief Records adding a layer to the canvas layer stack.
 */
class AddLayerCommand : public MapCommand
{
public:
    AddLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                    QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    OpenSWMMVisLayer *m_layer;    /*!< Non-owning: the project owns the layer. */
    int           m_position;
};

/*!
 * \class RemoveLayerCommand
 * \brief Records removing a layer from the canvas layer stack.
 */
class RemoveLayerCommand : public MapCommand
{
public:
    RemoveLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                       QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    OpenSWMMVisLayer *m_layer;
    int           m_position;
};

/*!
 * \class MoveLayerCommand
 * \brief Records reordering a layer within the canvas stack.
 */
class MoveLayerCommand : public MapCommand
{
public:
    MoveLayerCommand(int oldIndex, int newIndex, MapCanvas *canvas,
                     QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    int m_oldIndex;
    int m_newIndex;
};

// ---------------------------------------------------------------------------
// Phase 2 — Interactive map editing
// ---------------------------------------------------------------------------

/*!
 * \class MoveNodeCommand
 * \brief Records a node coordinate change for undo/redo.
 * \details Mutates engine state and the layer's geometry cache through
 *          SWMMModelLayer's edit-mutation API. Optionally recomputes
 *          conduit lengths for every link whose endpoint moved (the
 *          "auto-length" policy — see SWMMVisProjectWindow::isAutoLengthEnabled).
 *
 *          Consecutive commands that move the same node merge into a
 *          single undoable step so rapid drags do not bloat the stack.
 */
class MoveNodeCommand : public MapCommand
{
public:
    /*!
     * \brief Per-link length snapshot used by auto-length.
     * \details \p oldLen / \p newLen are the engine length values
     *          before / after this command's redo().
     */
    struct LengthRec
    {
        int    linkIdx;
        double oldLen;
        double newLen;
    };

    MoveNodeCommand(SWMMModelLayer *layer,
                    int nodeIdx,
                    double oldX, double oldY,
                    double newX, double newY,
                    QVector<LengthRec> lengthRecs,
                    MapCanvas *canvas,
                    QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override { return 10; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    SWMMModelLayer     *m_layer   = nullptr;
    int                 m_nodeIdx = -1;
    double              m_oldX    = 0.0;
    double              m_oldY    = 0.0;
    double              m_newX    = 0.0;
    double              m_newY    = 0.0;
    QVector<LengthRec>  m_lengthRecs;
};

/*!
 * \class EditVertexCommand
 * \brief Records a change to a link's interior polyline vertices.
 * \details Covers drag / insert / delete of interior (non-endpoint)
 *          vertices via the same old → new interior snapshot. When
 *          auto-length is on at commit time, the conduit length is
 *          recomputed from the full polyline and round-tripped through
 *          the engine as part of redo / undo.
 */
class EditVertexCommand : public MapCommand
{
public:
    EditVertexCommand(SWMMModelLayer *layer,
                      int linkIdx,
                      QVector<QPointF> oldInterior,
                      QVector<QPointF> newInterior,
                      double oldLen,
                      double newLen,
                      bool autoLengthApplied,
                      MapCanvas *canvas,
                      QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override { return 11; }

private:
    SWMMModelLayer     *m_layer   = nullptr;
    int                 m_linkIdx = -1;
    QVector<QPointF>    m_oldInterior;
    QVector<QPointF>    m_newInterior;
    double              m_oldLen  = 0.0;
    double              m_newLen  = 0.0;
    bool                m_autoLengthApplied = false;
};

/*!
 * \class EditSubcatchCommand
 * \brief Records a change to a subcatchment's polygon vertices.
 */
class EditSubcatchCommand : public MapCommand
{
public:
    EditSubcatchCommand(SWMMModelLayer *layer,
                        int catchIdx,
                        QVector<QPointF> oldVertices,
                        QVector<QPointF> newVertices,
                        double oldArea,
                        double newArea,
                        bool applyArea,
                        MapCanvas *canvas,
                        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override { return 12; }

private:
    SWMMModelLayer  *m_layer    = nullptr;
    int              m_catchIdx = -1;
    QVector<QPointF> m_old;
    QVector<QPointF> m_new;
    double           m_oldArea  = 0.0;
    double           m_newArea  = 0.0;
    bool             m_applyArea= false;
};

/*!
 * \class AddNodeCommand
 * \brief Records the creation of a SWMM node.
 * \details redo() calls SWMMModelLayer::applyNodeAdd, which pushes the
 *          new node onto the end of the engine's node list and caches
 *          its geometry. undo() calls rollbackTailNodeAdd — the narrow
 *          "pop-last" helper that works only while the added node is
 *          still the most recent.
 *
 *          Consequence: AddNodeCommand cannot be undone after any
 *          subsequent add/remove has shifted it out of the tail
 *          position. The GUI guards against this by rejecting the
 *          undo and leaving the stack untouched (a clean no-op).
 */
class AddNodeCommand : public MapCommand
{
public:
    /*!
     * \brief Creates an add-node command.
     * \param invertElev  If non-zero, written to the engine via
     *        \c swmm_node_set_invert_elev after the node is created.
     *        Defaults to 0.0 (engine default = no elevation override).
     */
    AddNodeCommand(SWMMModelLayer *layer,
                   QString name,
                   int nodeType,
                   double x, double y,
                   MapCanvas *canvas,
                   double invertElev = 0.0,
                   QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override { return 12; }

private:
    SWMMModelLayer *m_layer      = nullptr;
    QString         m_name;
    int             m_nodeType   = 0;
    double          m_x          = 0.0;
    double          m_y          = 0.0;
    double          m_invertElev = 0.0;
    bool            m_present    = false;  // true iff currently applied
};

/*!
 * \class AddLinkCommand
 * \brief Records the creation of a SWMM link (conduit/pump/orifice/weir/outlet).
 * \details redo() calls SWMMModelLayer::applyLinkAdd; undo() calls
 *          rollbackTailLinkAdd — only valid while the link is still the tail.
 */
class AddLinkCommand : public MapCommand
{
public:
    /*!
     * \brief Creates an add-link command.
     * \param offsetUp  Upstream invert offset (m or ft).  Written via
     *        \c swmm_link_set_offset_up after the link is created.  0.0 = no override.
     * \param offsetDn  Downstream invert offset.  0.0 = no override.
     */
    AddLinkCommand(SWMMModelLayer   *layer,
                   QString           name,
                   int               linkType,
                   QString           fromNode,
                   QString           toNode,
                   QVector<QPointF>  interiorVertices,
                   MapCanvas        *canvas,
                   double            offsetUp = 0.0,
                   double            offsetDn = 0.0,
                   QUndoCommand     *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 13; }

private:
    SWMMModelLayer  *m_layer    = nullptr;
    QString          m_name;
    int              m_linkType = 0;
    QString          m_fromNode;
    QString          m_toNode;
    QVector<QPointF> m_interiorVertices;
    double           m_offsetUp = 0.0;
    double           m_offsetDn = 0.0;
    bool             m_present  = false;
    int              m_linkIdx  = -1; // engine index, used for auto-length
};

/*!
 * \class AddGageCommand
 * \brief Records the creation of a rain gage.
 */
class AddGageCommand : public MapCommand
{
public:
    AddGageCommand(SWMMModelLayer *layer,
                   QString         name,
                   double          x, double y,
                   MapCanvas      *canvas,
                   QUndoCommand   *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 14; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString         m_name;
    double          m_x = 0, m_y = 0;
    bool            m_present = false;
};

/*!
 * \class AddSubcatchmentCommand
 * \brief Records the creation of a subcatchment polygon.
 */
class AddSubcatchmentCommand : public MapCommand
{
public:
    AddSubcatchmentCommand(SWMMModelLayer   *layer,
                           QString           name,
                           QVector<QPointF>  polygon,
                           MapCanvas        *canvas,
                           QUndoCommand     *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 15; }

private:
    SWMMModelLayer  *m_layer = nullptr;
    QString          m_name;
    QVector<QPointF> m_polygon;
    bool             m_present  = false;
    int              m_subcatchIdx = -1; // engine index, used for auto-area
};

/*!
 * \class AssignSubcatchGagesCommand
 * \brief Reassigns the rain gage of many subcatchments as ONE undo step.
 * \details Carries parallel arrays rather than pushing a command per
 *          subcatchment, following the mesh::pushCellParamEdits idiom: a bulk
 *          spatial assignment is one user action and should cost one Ctrl+Z.
 *
 *          Rows already holding the target gage are filtered out by the
 *          factory below, so redo() only touches what actually changes.
 *
 *          Everything is keyed by NAME, never by index — gage or subcatchment
 *          deletion re-packs engine indices, and this command may be undone
 *          long after that has happened.
 */
class AssignSubcatchGagesCommand : public MapCommand
{
public:
    AssignSubcatchGagesCommand(SWMMModelLayer *layer,
                               QStringList     subcatchNames,
                               QStringList     newGages,
                               QStringList     oldGages,
                               const QString  &text,
                               MapCanvas      *canvas,
                               QUndoCommand   *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 47; }

private:
    /*! \brief Apply \p gages to \ref m_subcatchNames, skipping unknown names. */
    void apply(const QStringList &gages);

    SWMMModelLayer *m_layer = nullptr;
    QStringList     m_subcatchNames;
    QStringList     m_newGages;
    QStringList     m_oldGages;
};

/*!
 * \class ConfigureGageCommand
 * \brief Snapshot/restore of a rain gage's data-source configuration.
 * \details AddGageCommand only lays down ObjectDefaultsApplier defaults, so a
 *          generated gage still needs its series, rain type, interval and
 *          factors set. This also covers the reverse direction: DeleteObject-
 *          Command::restoreGage brings a gage back with only its name and
 *          coordinates, dropping exactly these fields, so pushing this command
 *          before a delete lets undo put the configuration back.
 */
class ConfigureGageCommand : public MapCommand
{
public:
    /*! \brief The subset of gage state this command owns. */
    struct Config
    {
        int     dataSource  = 0;     ///< SWMM_GageDataSource (0 = TIMESERIES).
        QString timeseries;          ///< Series name when dataSource is TIMESERIES.
        int     rainType    = 0;     ///< SWMM_GageRainType.
        double  intervalSec = 3600;  ///< Recording interval.
        double  scaleFactor = 1.0;
        double  snowFactor  = 1.0;
    };

    /*! \brief Read a gage's current configuration; \p ok reports success. */
    [[nodiscard]] static Config capture(SWMMModelLayer *layer,
                                        const QString &gageName,
                                        bool *ok = nullptr);

    ConfigureGageCommand(SWMMModelLayer *layer,
                         QString         gageName,
                         Config          newConfig,
                         Config          oldConfig,
                         MapCanvas      *canvas,
                         QUndoCommand   *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 48; }

private:
    void apply(const Config &c);

    SWMMModelLayer *m_layer = nullptr;
    QString         m_gageName;
    Config          m_new;
    Config          m_old;
};

/*!
 * \class BulkEditCommand
 * \brief Macro command that holds one SWMMModelLayer::BulkEdit scope open
 *        across every child command.
 * \details Use this instead of a bare QUndoCommand whenever a macro carries
 *          more than a couple of model mutations. Each child still does its
 *          own engine call and SoA update; what the scope removes is the
 *          per-child rebuild of the category index, the model extent and the
 *          link spatial grid, plus the repaintRequested()/geometryChanged()
 *          pair whose listeners each cost O(model).
 *
 *          The macro object is the ONLY correct attachment point. Guarding
 *          the call site that builds the macro would cover the initial
 *          QUndoStack::push() (which calls redo()) but not a later Ctrl+Z —
 *          and undo replays the same storm through the add path, so an
 *          unguarded undo is as slow as the delete it reverses.
 *
 *          Children are unchanged: QUndoCommand::redo() runs them in order
 *          and QUndoCommand::undo() in reverse, exactly as before.
 */
class BulkEditCommand : public QUndoCommand
{
public:
    BulkEditCommand(SWMMModelLayer *layer, const QString &text,
                    QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent), m_layer(layer) {}

    void redo() override
    {
        SWMMModelLayer::BulkEdit guard(m_layer);
        QUndoCommand::redo();
    }

    void undo() override
    {
        SWMMModelLayer::BulkEdit guard(m_layer);
        QUndoCommand::undo();
    }

private:
    QPointer<SWMMModelLayer> m_layer;
};

/*!
 * \class DeleteObjectCommand
 * \brief Undoable deletion of a node, link, rain gage, or subcatchment.
 * \details The constructor snapshots the object's full property state
 *          (before redo() deletes it). undo() re-creates the object and
 *          restores all properties. For node deletion, cascade-deleted
 *          links are also re-created on undo.
 */
class DeleteObjectCommand : public MapCommand
{
public:
    enum TargetKind { DeleteNode, DeleteLink, DeleteGage, DeleteSubcatch };

    /*! Constructor for node deletion. Snapshots the node + identifies cascade links. */
    DeleteObjectCommand(SWMMModelLayer *layer, const QString &name,
                        TargetKind kind, MapCanvas *canvas,
                        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 16; }

private:
    void snapshotNode(const QString &name);
    void snapshotLink(const QString &name);
    void snapshotGage(const QString &name);
    void snapshotSubcatch(const QString &name);
    void restoreNode();
    void restoreLink();
    void restoreGage();
    void restoreSubcatch();

    SWMMModelLayer      *m_layer = nullptr;
    TargetKind           m_kind;
    NodeSnapshot         m_node;
    QVector<LinkSnapshot> m_cascadeLinks; // cascade-deleted links when a node is deleted
    LinkSnapshot         m_link;
    GageSnapshot         m_gage;
    SubcatchSnapshot     m_subcatch;
};

/*!
 * \class DeleteDataObjectCommand
 * \brief Undoable deletion of a non-spatial data object (curve, time series,
 *        or transect) held in a SWMMModelLayer registry.
 * \details Data objects are owned by registries (CurveRegistry,
 *          TimeseriesRegistry, TransectRegistry) rather than the engine's
 *          spatial stores, so this command snapshots the provider's full
 *          state, calls registry->remove() on redo(), and re-creates +
 *          repopulates the provider on undo(). The registry is flushed to the
 *          engine (clear + re-add) after each mutation.
 *
 *          Only Curve, TimeSeries, and Transect are supported today — they
 *          have registry-backed CRUD plus engine table/transect delete.
 *          Patterns, pollutants, aquifers, snowpacks, LID controls, streets,
 *          inlets, land uses, hydrograph groups, and control rules need new
 *          engine delete APIs first (see
 *          workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md); supports()
 *          returns false for those so callers can grey the menu action.
 */
class DeleteDataObjectCommand : public MapCommand
{
public:
    DeleteDataObjectCommand(SWMMModelLayer *layer,
                            const SWMMObjectRef &ref,
                            MapCanvas *canvas,
                            QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 17; }

    /*! True when \p type currently has a registry-backed delete path. */
    static bool supports(SWMMObjectRef::ObjectType type);

private:
    void snapshotCurve();       void restoreCurve();
    void snapshotTimeSeries();  void restoreTimeSeries();
    void snapshotTransect();    void restoreTransect();

    SWMMModelLayer *m_layer = nullptr;
    SWMMObjectRef   m_ref;
    bool            m_captured = false;   // snapshot succeeded

    // --- Curve snapshot ---
    int              m_curveType = -1;    // openswmmvis::curve::CurveType as int
    QVector<QPointF> m_curvePoints;       // (x, y)

    // --- Time series snapshot ---
    QString   m_tsUnits, m_tsDescription;
    int       m_tsSourceMode = 0;         // TimeseriesProvider::SourceMode as int
    QString   m_tsFilePath, m_tsColumnSelector;
    QDateTime m_tsFileMTime;
    QVector<QPair<QDateTime, double>> m_tsPoints;   // (time, value)

    // --- Transect snapshot ---
    QString m_txComments;
    double  m_txNLeft = 0, m_txNRight = 0, m_txNChannel = 0;
    double  m_txXLeftBank = 0, m_txXRightBank = 0;
    double  m_txXLeftEncroach = 0, m_txXRightEncroach = 0;
    double  m_txXFactor = 1, m_txYFactor = 1, m_txLengthFactor = 1;
    QVector<QPair<double, double>> m_txPoints;      // (station, elevation)
};

/*!
 * \class ReorderLayersCommand
 * \brief Records a full layer-stack reorder (e.g. moving an entire category
 *        group) as a single undoable step.
 */
class ReorderLayersCommand : public MapCommand
{
public:
    ReorderLayersCommand(QList<OpenSWMMVisLayer *> oldOrder,
                         QList<OpenSWMMVisLayer *> newOrder,
                         MapCanvas *canvas,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QList<OpenSWMMVisLayer *> m_oldOrder;
    QList<OpenSWMMVisLayer *> m_newOrder;
    bool m_firstRedo = true;
};

// ---------------------------------------------------------------------------
// Object Browser reorder commands (Slice AY)
// ---------------------------------------------------------------------------

/*!
 * \class ReorderCategoriesCommand
 * \brief Records a SWMM object category order change for undo/redo.
 */
class ReorderCategoriesCommand : public QUndoCommand
{
public:
    ReorderCategoriesCommand(SWMMModelLayer *layer,
                              QVector<SWMMModelLayer::Category> oldOrder,
                              QVector<SWMMModelLayer::Category> newOrder,
                              QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    SWMMModelLayer                    *m_layer;
    QVector<SWMMModelLayer::Category>  m_oldOrder;
    QVector<SWMMModelLayer::Category>  m_newOrder;
    bool                               m_firstRedo = true;
};

/*!
 * \class ReorderObjectsCommand
 * \brief Records a per-category object order change for undo/redo.
 * \details Stores the old permutation (or empty = "was default") and the new
 *          one.  undo() calls clearObjectOrder when the old permutation was
 *          the default identity, otherwise restores the old permutation.
 */
class ReorderObjectsCommand : public QUndoCommand
{
public:
    ReorderObjectsCommand(SWMMModelLayer         *layer,
                          SWMMModelLayer::Category cat,
                          QVector<int>             oldOrder,  // empty = was default
                          QVector<int>             newOrder,
                          QUndoCommand            *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    SWMMModelLayer          *m_layer;
    SWMMModelLayer::Category m_cat;
    QVector<int>             m_oldOrder;  // empty means "was default before this cmd"
    QVector<int>             m_newOrder;
    bool                     m_firstRedo = true;
};

class OpenSWMMVisAnnotationLayer;
class AnnotationTextItem;

/*!
 * \class AddAnnotationCommand
 * \brief Records the placement of a single text annotation.
 * \details The command owns the AnnotationTextItem while it is detached
 *          from the layer (undo state). On redo, the item is moved into
 *          the layer (which takes QObject parent ownership). On undo, the
 *          layer detaches the item via takeAnnotation, returning it to
 *          the command. This pattern keeps the item's id stable across
 *          redo/undo cycles so any side-channel references survive.
 */
class AddAnnotationCommand : public MapCommand
{
public:
    /*! \param item  Newly-created annotation. The command takes ownership;
     *               it transfers to the layer on each redo and back to the
     *               command on undo. */
    AddAnnotationCommand(OpenSWMMVisAnnotationLayer *layer,
                         AnnotationTextItem *item,
                         MapCanvas *canvas,
                         QUndoCommand *parent = nullptr);
    ~AddAnnotationCommand() override;

    void undo() override;
    void redo() override;
    int  id()   const override { return 21; }

private:
    OpenSWMMVisAnnotationLayer *m_layer = nullptr;
    AnnotationTextItem         *m_item  = nullptr;  ///< owned only while detached
    QString                     m_itemId;            ///< stable handle into the layer
    bool                        m_present = false;   ///< true iff currently in layer
};

/*!
 * \class InsertVirtualJunctionCommand
 * \brief Records a conduit split that inserts a virtual junction.
 * \details redo() calls SWMMModelLayer::applyInsertVirtualJunction (engine
 *          `swmm_conduit_split`); undo() calls applyFuseVirtualJunction —
 *          the exact engine-side inverse (a split→fuse round-trip restores
 *          the model byte-identically), so no snapshot machinery is needed.
 */
class InsertVirtualJunctionCommand : public MapCommand
{
public:
    InsertVirtualJunctionCommand(SWMMModelLayer *layer,
                                 QString linkName, double t,
                                 QString nodeName, QString newLinkName,
                                 MapCanvas *canvas,
                                 QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 22; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_linkName;      ///< conduit being split (name survives upstream)
    double  m_t = 0.5;       ///< normalized split position
    QString m_nodeName;      ///< inserted virtual junction
    QString m_newLinkName;   ///< new downstream conduit
    bool    m_present = false;
};

/*!
 * \class FuseVirtualJunctionCommand
 * \brief Records the re-fusion (deletion) of a virtual junction.
 * \details The constructor snapshots what a re-split needs: the upstream/
 *          downstream conduit names, the length-ratio split position, the
 *          node invert and its map coordinate. undo() re-splits at the
 *          snapshot position and restores the invert and coordinate so a
 *          grade-break node comes back exactly.
 */
class FuseVirtualJunctionCommand : public MapCommand
{
public:
    FuseVirtualJunctionCommand(SWMMModelLayer *layer,
                               QString nodeName,
                               MapCanvas *canvas,
                               QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 23; }

    /*! \brief False when the node is not a two-conduit through virtual
     *         junction (snapshot failed); the command must not be pushed. */
    bool valid() const { return m_valid; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_nodeName;
    QString m_upLinkName;    ///< surviving conduit
    QString m_dnLinkName;    ///< retired conduit (re-created on undo)
    double  m_t = 0.5;       ///< L_up / (L_up + L_dn)
    double  m_invert = 0.0;  ///< node invert (grade break — not derivable)
    double  m_x = 0.0, m_y = 0.0;
    bool    m_valid   = false;
    bool    m_present = false;   ///< true iff the fuse is currently applied
};

#endif // MAPUNDOSTACK_H
