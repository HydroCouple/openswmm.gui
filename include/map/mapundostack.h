/**
 * @file      mapundostack.h
 * @brief     An undo/redo stack for map operations with a configurable maximum depth.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
#include "map/objectdefaultsapplier.h"
#include "layers/swmmmodellayer.h" // SWMMModelLayer::Category used by ReorderCategoriesCommand below
#include "selection/selectionmanager.h" // SWMMObjectRef used by DeleteDataObjectCommand below

// ---------------------------------------------------------------------------
// Plain-data snapshots used by delete / undo
// ---------------------------------------------------------------------------

/*! Supported node-property subset captured for deletion replay; not a complete engine snapshot. */
struct NodeSnapshot {
    QString name;
    QString tag;
    int nodeType = 0;
    double x = 0, y = 0;
    double invertElev = 0;
    double maxDepth = 0;
    double initDepth = 0;
    double surchargeDepth = 0;
    double pondedArea = 0;
    // virtual junction: the flag plus its rendering-only ground depth. Without
    // these, undoing the delete of a virtual junction resurrected a plain
    // junction (and the drawn ground line with it).
    int isVirtual = 0;
    double rimDepth = 0;
    // inlet junction: the second flag plus the [INLET_USAGE] row it owns.
    // Without them, undoing the delete of an inlet junction resurrected a
    // plain virtual junction and silently dropped the inlet.
    int isInlet = 0;
    bool hasInletUsage = false;
    SWMM_InletUsage inletUsage{};
    QString inletDesignId; ///< resolved by name — indices shift
    QString captureNodeId; ///< resolved by name — indices shift
    // outfall-specific
    int outfallType = 0;
    int outfallFlapGate = 0;
    // storage-specific
    double seepRate = 0;
    // divider-specific
    int dividerType = 0;
};

/*! Supported link-property subset captured for deletion replay; references/subtype fields may be omitted. */
struct LinkSnapshot {
    QString name;
    int linkType = 0;
    QString fromNode;
    QString toNode;
    QVector<QPointF> interiorVertices;
    double length = 0;
    double roughness = 0;
    double offsetUp = 0;
    double offsetDn = 0;
    double crestHeight = 0;
    double dischargeCoeff = 0;
    double endContractions = 0;
    int flapGate = 0;
    int pumpInitState = 0;
    QString tag;
    int shape = -1, barrels = 1, culvertCode = 0;
    double geom[4] = {};
    double initialFlow = 0, maxFlow = 0, seepRate = 0;
    double lossIn = 0, lossOut = 0, lossAverage = 0;
};

/*! Minimal snapshot for a deleted rain gage. */
struct GageSnapshot {
    QString name;
    double x = 0, y = 0;
};

/*! Minimal snapshot for a deleted subcatchment. */
struct SubcatchSnapshot {
    QString name;
    QVector<QPointF> polygon;
    double area = 0;
    double width = 0;
    double slope = 0;
    double impervPct = 0;
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
    Q_PROPERTY(int maxUndoCount READ maxUndoCount WRITE setMaxUndoCount NOTIFY maxUndoCountChanged)

public:
    /**
     * @brief Create empty Qt undo history with limit 50 commands.
     * @param[in] parent Optional QObject owner.
     * @note QObject/QUndoStack access serialized on owning GUI thread; no settings read until
     *   loadSettings.
     */
    explicit MapUndoStack(QObject *parent = nullptr);

    /*!
     * \brief Returns the maximum number of undoable steps stored (0 = unlimited).
     * @return Actual Qt undoLimit, zero meaning unlimited; macro children follow Qt command-count
     *   semantics.
     */
    [[nodiscard]] int maxUndoCount() const;

    /*!
     * \brief Sets the maximum number of stored undo operations.
     * \details If the current stack depth exceeds the new limit, oldest commands
     *          are silently discarded.
     * \param count  Maximum depth; 0 means unlimited.
     * @note Forwards to QUndoStack::setUndoLimit, which requires an empty stack; does not clear
     *   history or enforce the limit independently. When requested count differs from old limit,
     *   emits requested count even if Qt rejects the change; read getter for actual limit. No clamp
     *   or persistence.
     */
    void setMaxUndoCount(int count);

    /*!
     * \brief Convenience: loads the undo limit from QSettings
     *        (key "MapCanvas/undoLimit").
     * @note Reads integer MapCanvas/undoLimit from default QSettings, fallback 50, then delegates
     *   setMaxUndoCount with its base-stack constraints and signal behavior. No explicit
     *   validation/status.
     */
    void loadSettings();

    /*!
     * \brief Saves the current undo limit to QSettings.
     * @note Writes actual current undoLimit to default QSettings key MapCanvas/undoLimit. Does not
     *   force sync or report backend errors/change history.
     */
    void saveSettings() const;

signals:
    /**
     * @param[in] count Requested changed limit, including zero unlimited; may differ from actual Qt
     *   limit if base setter refused.
     * @note Emitted after setter call when request differed from previous value; not a guaranteed
     *   acceptance notification.
     */
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
     * @brief Store undo-menu text and borrowed canvas for derived commands.
     * @param[in] text Human-readable command label.
     * @note Canvas pointer is raw and unguarded; destruction is not detected. Commands run on GUI
     *   thread and stack owns them after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    explicit MapCommand(const QString &text, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @return Borrowed stored canvas, possibly null; no lifetime guard or ownership transfer.
     */
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
    /**
     * @brief Capture before/after canvas extents for reversible navigation.
     * @param[in] oldExtent Before extent copied in canvas CRS.
     * @param[in] newExtent Target extent copied in same CRS.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    PanZoomCommand(const MapExtent &oldExtent, const MapExtent &newExtent, MapCanvas *canvas,
                   QUndoCommand *parent = nullptr);

    /**
     * @brief Restore before extent with pushUndo=false; null canvas no-op. No CRS reprojection of
     *   saved bounds.
     */
    void undo() override;
    /**
     * @brief Apply target extent with pushUndo=false; null canvas no-op, canvas handles
     *   normalization/notifications.
     */
    void redo() override;

    /*!
     * \brief Attempts to merge consecutive pan operations into a single command.
     * @param[in] other Required nonnull incoming PanZoomCommand when its id equals this id;
     *   matching ID alone is statically cast.
     * @return False different ID, otherwise true after replacing target extent and keeping original
     *   before extent. Does not compare target canvases, time or apply extent; caller must avoid
     *   merging unrelated canvas histories.
     */
    bool mergeWith(const QUndoCommand *other) override;
    /**
     * @return Constant command-family ID 1 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
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
    /**
     * @brief Capture authority-code strings for an already-applied canvas CRS change.
     * @param[in] oldAuthCode Before authority:integer code; WKT/local unparseable forms cannot be
     *   recreated.
     * @param[in] newAuthCode Already-applied target authority:integer code.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note First nonnull-canvas redo skips because caller already applied change. Later calls
     *   parse final colon/integer and allocate fresh SRS; parse/import failure silently leaves
     *   current CRS.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ChangeCRSCommand(const QString &oldAuthCode, const QString &newAuthCode, MapCanvas *canvas,
                     QUndoCommand *parent = nullptr);

    /**
     * @brief Recreate original authority CRS and transfer ownership to canvas through
     *   applyCRSInternal; null canvas/unresolvable code no-op.
     */
    void undo() override;
    /**
     * @brief Skip first nonnull-canvas call, then recreate target authority CRS and transfer
     *   ownership through applyCRSInternal. No nested undo.
     */
    void redo() override;

private:
    QString m_oldAuthCode; /*!< e.g. "EPSG:4326" */
    QString m_newAuthCode;
    bool m_firstRedo = true; /*!< Skip first redo — CRS already applied by setCanvasSRS. */
};

/*!
 * \class AddLayerCommand
 * \brief Records adding a layer to the canvas layer stack.
 */
class AddLayerCommand : public MapCommand
{
public:
    /**
     * @brief Capture layer identity and stack position for reversible add.
     * @param[in] layer Borrowed raw layer owned externally/project; command never deletes it and
     *   cannot detect destruction.
     * @param[in] position Insertion index used on restoration; canvas handles invalid/out-of-range
     *   position.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                    QUndoCommand *parent = nullptr);

    /**
     * @brief Find current layer index and take it without deletion or nested undo; missing/null
     *   no-op.
     */
    void undo() override;
    /**
     * @brief Insert borrowed layer at stored position without nested undo; null canvas/layer no-op.
     */
    void redo() override;

private:
    OpenSWMMVisLayer *m_layer; /*!< Non-owning: the project owns the layer. */
    int m_position;
};

/*!
 * \class RemoveLayerCommand
 * \brief Records removing a layer from the canvas layer stack.
 */
class RemoveLayerCommand : public MapCommand
{
public:
    /**
     * @brief Capture layer identity and stack position for reversible remove.
     * @param[in] layer Borrowed raw layer owned externally/project; command never deletes it and
     *   cannot detect destruction.
     * @param[in] position Insertion index used on restoration; canvas handles invalid/out-of-range
     *   position.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    RemoveLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                       QUndoCommand *parent = nullptr);

    /**
     * @brief Reinsert borrowed layer at stored position without nested undo; null pointers no-op.
     */
    void undo() override;
    /**
     * @brief Find current pointer index and take layer without deleting or nested undo;
     *   missing/null no-op.
     */
    void redo() override;

private:
    OpenSWMMVisLayer *m_layer;
    int m_position;
};

/*!
 * \class MoveLayerCommand
 * \brief Records reordering a layer within the canvas stack.
 */
class MoveLayerCommand : public MapCommand
{
public:
    /**
     * @brief Capture before/after layer-stack indices for reorder replay.
     * @param[in] oldIndex Original zero-based layer-stack index.
     * @param[in] newIndex Target zero-based index; caller preserves intervening stack coherence.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    MoveLayerCommand(int oldIndex, int newIndex, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Move layer from new index back to old using pushUndo=false; no pointer identity
     *   snapshot or nested undo.
     */
    void undo() override;
    /**
     * @brief Move layer from old index to new using pushUndo=false; null canvas no-op, canvas
     *   validates indices.
     */
    void redo() override;

private:
    int m_oldIndex;
    int m_newIndex;
};

/*!
 * \class MoveSublayerCommand
 * \brief Records reordering one sublayer inside an ISublayerHost layer's
 *        paint stack (Slice LTR-2026-09-19). Applies through
 *        MapCanvas::moveSublayer(host, from, to, pushUndo = false) so the
 *        canvas emits sublayerOrderChanged() on both redo and undo.
 */
class MoveSublayerCommand : public MapCommand
{
public:
    /**
     * @brief Capture reversible index move inside one layer sublayer host.
     * @param[in] host Borrowed raw ISublayerHost layer owned externally; must outlive command.
     * @param[in] fromIndex Original sublayer ordinal.
     * @param[in] toIndex Target sublayer ordinal.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    MoveSublayerCommand(OpenSWMMVisLayer *host, int fromIndex, int toIndex, MapCanvas *canvas,
                        QUndoCommand *parent = nullptr);

    /**
     * @brief Call canvas moveSublayer(host,to,from,false), preserving no independent sublayer
     *   snapshot; null pointers no-op.
     */
    void undo() override;
    /**
     * @brief Call canvas moveSublayer(host,from,to,false); null host/canvas no-op, host/canvas
     *   handle validation and order signals.
     */
    void redo() override;

private:
    OpenSWMMVisLayer *m_host; /*!< Non-owning: the project owns the layer. */
    int m_fromIndex;
    int m_toIndex;
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
    struct LengthRec {
        int linkIdx;
        double oldLen;
        double newLen;
    };

    /**
     * @brief Capture native node displacement plus caller-computed conduit length history.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] nodeIdx Current zero-based engine node index; must still identify same node at
     *   replay.
     * @param[in] oldX Before native model X.
     * @param[in] oldY Before native model Y.
     * @param[in] newX Target native model X.
     * @param[in] newY Target native model Y.
     * @param[in] lengthRecs Per-link engine indices and old/new project-unit lengths, copied/moved;
     *   no lengths computed by command.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Each replay moves node then writes all supplied link lengths even if move fails; layer
     *   statuses ignored and no atomic rollback. Model mutation APIs supply geometry/edited
     *   signals.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    MoveNodeCommand(SWMMModelLayer *layer, int nodeIdx, double oldX, double oldY, double newX,
                    double newY, QVector<LengthRec> lengthRecs, MapCanvas *canvas,
                    QUndoCommand *parent = nullptr);

    /**
     * @brief Move node to stored before coordinate then write oldLen for every record; null layer
     *   no-op, possible partial replay.
     */
    void undo() override;
    /**
     * @brief Move node to stored target native coordinate then write newLen for every length
     *   record; null layer no-op, per-write statuses ignored.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 10 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 10; }
    /**
     * @param[in] other Required nonnull concrete MoveNodeCommand if matching id; statically cast
     *   after ID check.
     * @return False different ID/model/node index. True replaces target XY and merges length
     *   records by link index, preserving earliest oldLen and latest newLen, appending new link
     *   records. Does not apply geometry or impose time threshold.
     */
    bool mergeWith(const QUndoCommand *other) override;

private:
    SWMMModelLayer *m_layer = nullptr;
    int m_nodeIdx = -1;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
    QVector<LengthRec> m_lengthRecs;
};

/*!
 * \class MoveGageCommand
 * \brief Records a rain-gage coordinate change for undo/redo — the
 *        [SYMBOLS] twin of MoveNodeCommand.
 * \details Commits through SWMMModelLayer::applyGageMove so the engine
 *          value, cached scene point, spatial index and model extent move
 *          together. Gages have no attached links, so there is no
 *          auto-length leg. Consecutive commands moving the same gage
 *          merge into a single undoable step, like node moves.
 */
class MoveGageCommand : public MapCommand
{
public:
    /**
     * @brief Capture rain-gage displacement in native model coordinates.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] gageIdx Zero-based gage index, caller preserves identity across replay.
     * @param[in] oldX Before native X.
     * @param[in] oldY Before native Y.
     * @param[in] newX Target native X.
     * @param[in] newY Target native Y.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note No auto-length operation. Raw model pointer lifetime not guarded; layer mutation
     *   handles cache/signals and failures ignored.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    MoveGageCommand(SWMMModelLayer *layer, int gageIdx, double oldX, double oldY, double newX,
                    double newY, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Restore before gage coordinate through model; null layer no-op, status ignored.
     */
    void undo() override;
    /**
     * @brief Apply stored target gage coordinate through model; null layer no-op, status ignored.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 14 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 14; }
    /**
     * @param[in] other Required nonnull concrete MoveGageCommand when id matches; same ID alone is
     *   statically cast, caller must prevent unrelated command classes.
     * @return False different ID/layer/gage index; true retains original before coordinate and
     *   adopts latest target coordinate. No geometry write or time threshold.
     */
    bool mergeWith(const QUndoCommand *other) override;

private:
    SWMMModelLayer *m_layer = nullptr;
    int m_gageIdx = -1;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
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
    /**
     * @brief Capture link bend snapshots and optional already-computed length changes.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkIdx Current zero-based link index; caller preserves identity.
     * @param[in] oldInterior Before interior bends only, native model coordinates.
     * @param[in] newInterior Target interior bends only, endpoints owned by nodes.
     * @param[in] oldLen Before conduit length in project units.
     * @param[in] newLen Target conduit length in project units, no recomputation.
     * @param[in] autoLengthApplied Replay supplied length change after geometry when true.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Geometry and optional length setters run sequentially even if first fails; statuses
     *   ignored.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    EditVertexCommand(SWMMModelLayer *layer, int linkIdx, QVector<QPointF> oldInterior,
                      QVector<QPointF> newInterior, double oldLen, double newLen,
                      bool autoLengthApplied, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Restore old interior bends and optionally old length; no recomputation or transaction.
     */
    void undo() override;
    /**
     * @brief Apply target interior bends, then target length if enabled; null layer no-op, geometry
     *   failure does not stop length write.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 11 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 11; }

private:
    SWMMModelLayer *m_layer = nullptr;
    int m_linkIdx = -1;
    QVector<QPointF> m_oldInterior;
    QVector<QPointF> m_newInterior;
    double m_oldLen = 0.0;
    double m_newLen = 0.0;
    bool m_autoLengthApplied = false;
};

/*!
 * \class FlipLinkCommand
 * \brief Reverses a link's direction — upstream node becomes downstream.
 * \details Delegates to SWMMModelLayer::applyLinkFlip(), which is
 *          self-inverse, so redo() and undo() are the same call and no
 *          before/after snapshot is needed.
 */
class FlipLinkCommand : public MapCommand
{
public:
    /**
     * @brief Capture link index for the model self-inverse direction reversal.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkIdx Current zero-based link index, caller preserves identity.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Uses applyLinkFlip in both directions, including its endpoint/vertex/offset/loss
     *   notifications and partial-failure limitations. No extra flow sign conversion or property
     *   snapshot.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    FlipLinkCommand(SWMMModelLayer *layer, int linkIdx, MapCanvas *canvas,
                    QUndoCommand *parent = nullptr);

    /**
     * @brief Reverse same link again through applyLinkFlip; requires successful coherent prior flip
     *   for exact inverse.
     */
    void undo() override;
    /**
     * @brief Reverse link through applyLinkFlip; null layer no-op, status ignored.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 13 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 13; }

private:
    SWMMModelLayer *m_layer = nullptr;
    int m_linkIdx = -1;
};

/*!
 * \class EditSubcatchCommand
 * \brief Records a change to a subcatchment's polygon vertices.
 */
class EditSubcatchCommand : public MapCommand
{
public:
    /**
     * @brief Capture subcatchment polygon and optional caller-computed area history.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] catchIdx Current zero-based subcatchment index, caller preserves identity.
     * @param[in] oldVertices Before polygon native model coordinates.
     * @param[in] newVertices After polygon native model coordinates.
     * @param[in] oldArea Before engine area in acres/hectares.
     * @param[in] newArea Target engine area in acres/hectares, not recomputed here.
     * @param[in] applyArea Write supplied area after polygon replacement when true.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Polygon failure does not stop area setter; statuses ignored, no atomic rollback.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    EditSubcatchCommand(SWMMModelLayer *layer, int catchIdx, QVector<QPointF> oldVertices,
                        QVector<QPointF> newVertices, double oldArea, double newArea,
                        bool applyArea, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Restore original polygon then optional old area; null layer no-op, setters can fail
     *   independently.
     */
    void undo() override;
    /**
     * @brief Apply new polygon then optional new area; null layer no-op, delegated model
     *   notifications.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 12 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 12; }

private:
    SWMMModelLayer *m_layer = nullptr;
    int m_catchIdx = -1;
    QVector<QPointF> m_old;
    QVector<QPointF> m_new;
    double m_oldArea = 0.0;
    double m_newArea = 0.0;
    bool m_applyArea = false;
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
 *          undo mutation when tail removal refuses; Qt still controls stack advancement.
 */
class AddNodeCommand : public MapCommand
{
public:
    /*!
     * \brief Creates an add-node command.
     * \param invertElev  If non-zero, written to the engine via
     *        \c swmm_node_set_invert_elev after the node is created.
     *        Defaults to 0.0 (engine default = no elevation override).
     * @brief Capture new named node placement and optional terrain invert for deferred creation.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] name Unique desired exact node identifier.
     * @param[in] nodeType Requested engine node type code; layer/engine validate.
     * @param[in] x Native model X.
     * @param[in] y Native model Y.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note On successful add applies current ObjectDefaultsApplier defaults before nonzero invert
     *   override. Replay can reread changed preferences. Optional LID binding failure attempts tail
     *   rollback; present flag tracks add acceptance, not success of all subsequent setters.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddNodeCommand(SWMMModelLayer *layer, QString name, int nodeType, double x, double y,
                   MapCanvas *canvas, double invertElev = 0.0, QUndoCommand *parent = nullptr);

    /**
     * @param[in] control Exact LID control name to bind after add; empty disables binding.
     * @note Stores value only; must be set before redo. Resolved engine index on creation;
     *   missing/rejected binding attempts rollback. Does not change already-created node.
     */
    void setLidControl(QString control) { m_lidControl = std::move(control); }
    /**
     * @brief Attempt rollbackTailNodeAdd only while present; clears present only on success.
     *   Tail/non-tail refusal leaves node, and Qt stack advancement is not prevented here.
     */
    void undo() override;
    /**
     * @brief If not present and layer exists, add named node, apply preferences, optional LID
     *   binding, then nonzero project-unit invert. Scalar setter errors ignored.
     */
    void redo() override;

    /**
     * @return Constant command-family ID 12 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 12; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    QString m_lidControl;
    int m_nodeType = 0;
    double m_x = 0.0;
    double m_y = 0.0;
    double m_invertElev = 0.0;
    bool m_present = false; // true iff currently applied
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
     * @brief Capture new link endpoints/bends and optional terrain offsets for creation.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] name Unique desired exact link ID.
     * @param[in] linkType Engine conduit/pump/orifice/weir/outlet code.
     * @param[in] fromNode Existing upstream node name.
     * @param[in] toNode Existing downstream node name.
     * @param[in] interiorVertices Interior-only bends in native model coordinates.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Required live layer at replay; no null/present guard in redo. Successful add applies
     *   current preferences; canvas autoLength property suppresses default length and writes
     *   positive model-unit polyline length, not just conduits at command level. Nonzero offsets
     *   applied as engine convention.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddLinkCommand(SWMMModelLayer *layer, QString name, int linkType, QString fromNode,
                   QString toNode, QVector<QPointF> interiorVertices, MapCanvas *canvas,
                   double offsetUp = 0.0, double offsetDn = 0.0, QUndoCommand *parent = nullptr);

    /**
     * @param[in] layer LID outlet-layer code; positive enables binding after add, zero/nonpositive
     *   disables.
     * @param[in] top Whether outlet uses top elevation flag passed to engine.
     * @note Stores creation options only, no live change. Binding rejection attempts tail-link
     *   rollback and clears present flag.
     */
    void setLidAnchor(int layer, bool top)
    {
        m_lidLayer = layer;
        m_lidTop = top;
    }
    /**
     * @brief If present attempt tail-link rollback, retaining present on refusal. Requires live
     *   layer; no reporting or arbitrary-index delete fallback.
     */
    void undo() override;
    /**
     * @brief Attempt named link creation, apply current defaults, optional autoLength/offsets/LID
     *   outlet binding. Requires live layer; repeated redo is not guarded against existing present
     *   object.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 13 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 13; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    int m_linkType = 0;
    QString m_fromNode;
    QString m_toNode;
    QVector<QPointF> m_interiorVertices;
    double m_offsetUp = 0.0;
    double m_offsetDn = 0.0;
    bool m_present = false;
    int m_lidLayer = 0;
    bool m_lidTop = false;
    int m_linkIdx = -1; // engine index, used for auto-length
};

/*!
 * \class AddGageCommand
 * \brief Records the creation of a rain gage.
 */
class AddGageCommand : public MapCommand
{
public:
    /**
     * @brief Capture named rain-gage position for later add/default application.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] name Unique exact rain-gage identifier.
     * @param[in] x Native model X.
     * @param[in] y Native model Y.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Replay requires nonnull live model; redo has no already-present guard. Successful
     *   creation applies current preferences without source-series binding.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddGageCommand(SWMMModelLayer *layer, QString name, double x, double y, MapCanvas *canvas,
                   QUndoCommand *parent = nullptr);

    /**
     * @brief Attempt tail-gage rollback only when present; retain present on refusal. No
     *   arbitrary-index deletion fallback.
     */
    void undo() override;
    /**
     * @brief Attempt gage add at saved native coordinate then apply current rainfall defaults on
     *   success; live layer required.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 14 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 14; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    double m_x = 0, m_y = 0;
    bool m_present = false;
};

/*!
 * \class AddSubcatchmentCommand
 * \brief Records the creation of a subcatchment polygon.
 */
class AddSubcatchmentCommand : public MapCommand
{
public:
    /**
     * @brief Capture named polygon for creation with defaults and optional auto-area.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] name Unique exact subcatchment identifier.
     * @param[in] polygon Native model polygon, copied/moved; caller ensures map-unit scale
     *   compatible with global SI/US area conversion.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Replay requires live model; redo no present guard. Uses canvas property autoLength for
     *   area policy, not a separate autoArea flag. Positive raw polygon area divided by 10000 SI or
     *   43560 US using global UnitSystem, without CRS/geodesic conversion; defaults skip area
     *   whenever flag true even if computed area nonpositive.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddSubcatchmentCommand(SWMMModelLayer *layer, QString name, QVector<QPointF> polygon,
                           MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Attempt tail-subcatchment rollback if present, retaining present on refusal; live
     *   layer required.
     */
    void undo() override;
    /**
     * @brief Attempt add, apply current surface/infiltration defaults then positive auto-area when
     *   canvas autoLength enabled; setter errors ignored.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 15 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 15; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    QVector<QPointF> m_polygon;
    bool m_present = false;
    int m_subcatchIdx = -1; // engine index, used for auto-area
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
    /**
     * @brief Capture name-based gage assignment maps for bulk undo/redo.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] subcatchNames Exact target subcatchment names in parallel order.
     * @param[in] newGages Parallel target gage names; empty means skip, not clear assignment.
     * @param[in] oldGages Parallel caller-captured original names; empty cannot restore an
     *   unassigned state.
     * @param[in] text Undo-menu label copied unchanged.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Common prefix with current gage list is visited; missing subcatchments skipped, setters
     *   ignored, Scene invalidated whenever live model/engine path reached even if no write. No
     *   equality filtering at this constructor.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AssignSubcatchGagesCommand(SWMMModelLayer *layer, QStringList subcatchNames,
                               QStringList newGages, QStringList oldGages, const QString &text,
                               MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Apply original nonempty names; empty old assignment is skipped, so undo cannot express
     *   unassigned state here.
     */
    void undo() override;
    /**
     * @brief Apply target nonempty gage names to currently resolved subcatchments; null
     *   model/closed engine no-op.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 47 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 47; }

private:
    /*! \brief Apply \p gages to \ref m_subcatchNames, skipping unknown names.
     * @param[in] gages Parallel gage names; empty entries skipped and longer tail ignored.
     * @note Resolves each subcatchment name anew after index shifts, delegates applySubcatchSetGage
     *   and ignores failure. Scene invalidated at end when canvas exists; no transaction.
     */
    void apply(const QStringList &gages);

    SWMMModelLayer *m_layer = nullptr;
    QStringList m_subcatchNames;
    QStringList m_newGages;
    QStringList m_oldGages;
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
    struct Config {
        int dataSource = 0;        ///< SWMM_GageDataSource (0 = TIMESERIES).
        QString timeseries;        ///< Series name when dataSource is TIMESERIES.
        int rainType = 0;          ///< SWMM_GageRainType.
        double intervalSec = 3600; ///< Recording interval.
        double scaleFactor = 1.0;
        double snowFactor = 1.0;
    };

    /*! \brief Read a gage's current configuration; \p ok reports success.
     * @param[in] layer Optional borrowed model; null/closed returns defaults.
     * @param[in] gageName Exact current rain-gage identifier.
     * @param[out] ok Optional flag initialized false, true after finding gage even if individual
     *   getter fails.
     * @return Snapshot initialized from Config defaults and overwritten by available scalar
     *   getters/series lookup (256-byte buffer). Does not validate complete file-source state or
     *   getter success; no mutation.
     */
    [[nodiscard]] static Config capture(SWMMModelLayer *layer, const QString &gageName,
                                        bool *ok = nullptr);

    /**
     * @brief Capture supplied before/after subset of named gage configuration.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] gageName Exact current gage ID resolved on every apply.
     * @param[in] newConfig Target rain/source enum, interval seconds, dimensionless scale/snow
     *   factors and optional series name.
     * @param[in] oldConfig Caller-captured original same subset; file path/station metadata not
     *   included.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note No local numeric/reference validation; writes may partly fail. Empty time-series name
     *   selects data-source enum alone, without clearing/restoring a series binding.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ConfigureGageCommand(SWMMModelLayer *layer, QString gageName, Config newConfig,
                         Config oldConfig, MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Apply supplied original subset with same setter ordering/failure behavior; does not
     *   restore unsnapshotted file metadata.
     */
    void undo() override;
    /**
     * @brief Apply stored target scalar/source configuration, mark model edited and invalidate
     *   Scene once existing gage resolves.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 48 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 48; }

private:
    /**
     * @param[in] c Rain/source codes, interval in seconds, scale/snow factors and optional series
     *   name.
     * @note Null model/closed engine/missing gage no-op. Writes scalars first then named existing
     *   series (which sets source) or source enum alone. All statuses ignored, then
     *   markEdited/Scene invalidation regardless of acceptance; no rollback.
     */
    void apply(const Config &c);

    SWMMModelLayer *m_layer = nullptr;
    QString m_gageName;
    Config m_new;
    Config m_old;
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
    /**
     * @brief Create parent macro that coalesces model refresh and preference reads across child
     *   replay.
     * @param[in] layer Borrowed model guarded by QPointer for BulkEdit scope; child commands may
     *   still require their raw model to survive.
     * @param[in] text Undo label copied unchanged.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Child commands attach through parent pointer and remain owned by this macro. BulkEdit
     *   is a refresh/signal optimization, not engine transaction; errors/rejected children do not
     *   roll back other children.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    BulkEditCommand(SWMMModelLayer *layer, const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent), m_layer(layer)
    {
    }

    /**
     * @brief Enter model BulkEdit and ObjectDefaultsApplier BatchScope, then redo Qt children in
     *   construction order. Does not skip children when guarded model is null.
     */
    void redo() override
    {
        SWMMModelLayer::BulkEdit guard(m_layer);
        ObjectDefaultsApplier::BatchScope defaults;
        QUndoCommand::redo();
    }

    /**
     * @brief Enter same scopes then undo Qt children in reverse order; child raw pointer
     *   lifetime/preconditions remain caller responsibility.
     */
    void undo() override
    {
        SWMMModelLayer::BulkEdit guard(m_layer);
        ObjectDefaultsApplier::BatchScope defaults;
        QUndoCommand::undo();
    }

private:
    QPointer<SWMMModelLayer> m_layer;
};

/*!
 * \class DeleteObjectCommand
 * \brief Undoable deletion of a node, link, rain gage, or subcatchment.
 * \details The constructor snapshots the object's represented property subset
 *          (before redo() deletes it). undo() re-creates the object and
 *          restores that subset. For node deletion, cascade-deleted
 *          links are also re-created on undo.
 */
class DeleteObjectCommand : public MapCommand
{
public:
    enum TargetKind { DeleteNode, DeleteLink, DeleteGage, DeleteSubcatch };

    /*! Constructor for node deletion. Snapshots the node + identifies cascade links.
     * @brief Snapshot supported spatial-object subset and node cascade links before deletion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] name Exact existing target identifier; unknown name leaves default/empty snapshot.
     * @param[in] kind DeleteNode/Link/Gage/Subcatch selects snapshot/delete/restore path.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Requires live model during construction/replay. Snapshot structs capture only
     *   represented fields, not complete engine object state: e.g. gage configuration and most
     *   subcatchment attributes are absent. Missing getter values retain defaults, and replay
     *   statuses ignored. Node cascade links may duplicate other batch snapshots; caller must plan
     *   targets/order coherently.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    DeleteObjectCommand(SWMMModelLayer *layer, const QString &name, TargetKind kind,
                        MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /*! Batch mode (perf-plan Phase A2): the owning BatchDeleteCommand's one
     *  swmm_*_delete_many call performs the deletion, so this child keeps
     *  only its snapshot + undo role and redo() becomes a no-op.
     * @param[in] b True makes redo a no-op because owning batch performs deletion; undo always
     *   restores snapshot.
     * @note Stores flag only; no deletion, validation or snapshot change.
     */
    void setEngineAppliedByBatch(bool b) { m_engineAppliedByBatch = b; }

    /**
     * @brief Restore selected snapshot subset and node cascade links as applicable; no defaults
     *   applied. Live model required, setter failures can leave partial restoration.
     */
    void undo() override;
    /**
     * @brief If not externally batch-applied, delete snapshot name through selected model mutation.
     *   Live model required, return status ignored.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 16 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 16; }

private:
    /**
     * @brief Read common node scalar/tag/geometry/virtual/inlet fields and supported subtype
     *   fields, then impact-analyzed cascade link snapshots.
     * @param[in] name Exact existing engine identifier; unknown index returns without populating
     *   snapshot.
     * @note Requires live raw model/engine, serial GUI access. Getter failures generally retain
     *   initialized defaults; no capture-success flag, UI or model mutation. Subtype/related fields
     *   outside snapshot remain unrecorded.
     */
    void snapshotNode(const QString &name);
    /**
     * @brief Read named link through snapshotLinkByIdx, including represented
     *   scalar/geometry/conduit fields.
     * @param[in] name Exact existing engine identifier; unknown index returns without populating
     *   snapshot.
     * @note Requires live raw model/engine, serial GUI access. Getter failures generally retain
     *   initialized defaults; no capture-success flag, UI or model mutation. Subtype/related fields
     *   outside snapshot remain unrecorded.
     */
    void snapshotLink(const QString &name);
    /**
     * @brief Read gage name and native coordinate only; rainfall/source configuration is not
     *   captured.
     * @param[in] name Exact existing engine identifier; unknown index returns without populating
     *   snapshot.
     * @note Requires live raw model/engine, serial GUI access. Getter failures generally retain
     *   initialized defaults; no capture-success flag, UI or model mutation. Subtype/related fields
     *   outside snapshot remain unrecorded.
     */
    void snapshotGage(const QString &name);
    /**
     * @brief Read polygon, area, width, slope percent and impervious percent only.
     * @param[in] name Exact existing engine identifier; unknown index returns without populating
     *   snapshot.
     * @note Requires live raw model/engine, serial GUI access. Getter failures generally retain
     *   initialized defaults; no capture-success flag, UI or model mutation. Subtype/related fields
     *   outside snapshot remain unrecorded.
     */
    void snapshotSubcatch(const QString &name);
    /**
     * @brief Re-add node and cascade links by names, restore represented scalar fields, then
     *   virtual/inlet role and usage.
     * @note Node/common depths in project length, ponded area square length, seep rain-rate units.
     *   Flags restored after conduits so rules can pass; inlet design/capture IDs resolved anew.
     *   Add and most setter errors ignored, possibly modifying an existing same-name object after
     *   rejected add. Unsnapshotted subtype data/references are not reconstructed. Requires live
     *   raw model/engine; no atomic rollback or success return.
     */
    void restoreNode();
    /**
     * @brief Re-add named link/bends then restore represented scalar and conduit properties.
     * @note Resolves fresh link index after add regardless of add success; an existing same-name
     *   object may receive writes. Missing resulting link aborts. No preferences/auto-length;
     *   unsnapshotted pump/curve/inlet references are not reconstructed. Requires live raw
     *   model/engine; no atomic rollback or success return.
     */
    void restoreLink();
    /**
     * @brief Re-add gage name and native coordinate only.
     * @note Does not restore rain format/interval/factors/source-series/file fields; caller can
     *   layer ConfigureGageCommand for its supported subset. Add failure ignored. Requires live raw
     *   model/engine; no atomic rollback or success return.
     */
    void restoreGage();
    /**
     * @brief Re-add named polygon then restore area/width/slope/impervious subset.
     * @note Area acres/hectares, width project length, slope/impervious percentages. Resolves index
     *   even if add fails; remaining infiltration/runoff/source/routing fields retain engine
     *   defaults or existing values. No setter-status checking. Requires live raw model/engine; no
     *   atomic rollback or success return.
     */
    void restoreSubcatch();

    SWMMModelLayer *m_layer = nullptr;
    TargetKind m_kind;
    bool m_engineAppliedByBatch = false;
    NodeSnapshot m_node;
    QVector<LinkSnapshot> m_cascadeLinks; // cascade-deleted links when a node is deleted
    LinkSnapshot m_link;
    GageSnapshot m_gage;
    SubcatchSnapshot m_subcatch;
};

/*!
 * \class BatchDeleteCommand
 * \brief Undoable bulk deletion (perf-plan Phase A2): one engine
 *        swmm_*_delete_many call per kind instead of K per-object deletes.
 * \details The constructor creates one snapshot-only DeleteObjectCommand
 *          child per target — every snapshot is taken BEFORE anything is
 *          deleted, while all indices are still pre-batch — with
 *          setEngineAppliedByBatch(true) so the children's redo() are
 *          no-ops.  redo() performs the whole deletion through
 *          SWMMModelLayer::applyDeleteMany inside the inherited BulkEdit
 *          scope; undo() is inherited: children restore per-object in
 *          reverse order (the rare direction — documented as O(K·N)).
 */
class BatchDeleteCommand : public BulkEditCommand
{
public:
    struct Target {
        QString name;
        DeleteObjectCommand::TargetKind kind;
    };

    /**
     * @brief Snapshot all requested targets as child restore commands before one per-kind bulk
     *   deletion.
     * @param[in] layer Required live model at construction; guarded pointer gates later redo, child
     *   restore pointers are raw.
     * @param[in] targets Ordered names/kinds, all snapshotted before editing; caller avoids
     *   duplicate/cascading overlap that cannot restore cleanly.
     * @param[in] text Undo-menu label copied unchanged.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @note Children have engineAppliedByBatch so redo no-ops; inherited undo restores reverse
     *   construction order inside BulkEdit. Bulk deletion status ignored; failed deletions do not
     *   prune snapshots and undo may try to recreate objects not removed.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    BatchDeleteCommand(SWMMModelLayer *layer, const QList<Target> &targets, MapCanvas *canvas,
                       const QString &text);

    /**
     * @brief In one BulkEdit call model applyDeleteMany for grouped names then redo snapshot-only
     *   children.
     * @note Deleted guarded model no-op. Status ignored; deletion can partially succeed across
     *   kinds. This override does not create preference BatchScope but children do no writes. Undo
     *   inherited from BulkEditCommand.
     */
    void redo() override;

private:
    QPointer<SWMMModelLayer> m_layer;
    QStringList m_nodeNames, m_linkNames, m_subcatchNames, m_gageNames;
};

/*!
 * \class DeleteDataObjectCommand
 * \brief Undoable deletion of a non-spatial data object (curve, time series,
 *        or transect) held in a SWMMModelLayer registry.
 * \details Data objects are owned by registries (CurveRegistry,
 *          TimeseriesRegistry, TransectRegistry) rather than the engine's
 *          spatial stores, so this command snapshots the provider's full
 *          state, calls registry->remove() on redo(), and re-creates +
 *          repopulates the provider on undo(). Removal deletes the engine object through the
 *   registry; undo saves the
 *          registry contents back to the engine after recreating the provider.
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
    /**
     * @brief Snapshot supported registry provider before deferred engine-authoritative removal.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] ref Object reference copied; Curve, TimeSeries or Transect supported, selected by
     *   objectType and exact name.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note May lazily initialize corresponding model-owned registry while snapshotting.
     *   Unsupported/missing provider leaves uncaptured command; redo can still attempt removal but
     *   undo requires capture. Raw model must outlive command.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    DeleteDataObjectCommand(SWMMModelLayer *layer, const SWMMObjectRef &ref, MapCanvas *canvas,
                            QUndoCommand *parent = nullptr);

    /**
     * @brief If captured and model lives, recreate missing named provider and write captured
     *   subset, then save entire registry to engine; statuses ignored and no atomic rollback.
     */
    void undo() override;
    /**
     * @brief Resolve supported registry/name and remove provider, letting registry delete engine
     *   table/transect; null model/missing registry/provider no-op. No full reflush on delete.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 17 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 17; }

    /*! True when \p type currently has a registry-backed delete path.
     * @param[in] type Object kind to check; no engine/model inspection.
     * @return True only Curve, TimeSeries and Transect; false others. Does not guarantee object
     *   exists or registry/engine mutation will succeed.
     */
    static bool supports(SWMMObjectRef::ObjectType type);

private:
    /**
     * @brief Snapshot provider type and ordered XY points; sets captured only when provider exists.
     * @note Lazily ensure model-owned registry; absent model/registry/provider no-op. Values remain
     *   provider unit/time conventions, copied unchanged. Does not remove or flush provider; raw
     *   model lifetime required.
     */
    void snapshotCurve();
    /**
     * @brief Create absent named curve with captured type/XY points and save whole curve registry
     *   to engine.
     * @note Existing name/absent registry/create failure no-op; curve additionally requires valid
     *   captured type. Provider setters and registry save statuses ignored, so restoration can be
     *   partial. No new preferences, independent transaction or exact engine-row-order guarantee.
     */
    void restoreCurve();
    /**
     * @brief Snapshot units/description/source mode/file path-column-mtime and current time-value
     *   points; sets captured on provider success.
     * @note Lazily ensure model-owned registry; absent model/registry/provider no-op. Values remain
     *   provider unit/time conventions, copied unchanged. Does not remove or flush provider; raw
     *   model lifetime required.
     */
    void snapshotTimeSeries();
    /**
     * @brief Create absent named time series, restore metadata/file source/current points and save
     *   whole registry to engine.
     * @note Existing name/absent registry/create failure no-op; curve additionally requires valid
     *   captured type. Provider setters and registry save statuses ignored, so restoration can be
     *   partial. No new preferences, independent transaction or exact engine-row-order guarantee.
     */
    void restoreTimeSeries();
    /**
     * @brief Snapshot comments, roughness, bank/encroachment stations, modifiers and
     *   station/elevation points; sets captured on provider success.
     * @note Lazily ensure model-owned registry; absent model/registry/provider no-op. Values remain
     *   provider unit/time conventions, copied unchanged. Does not remove or flush provider; raw
     *   model lifetime required.
     */
    void snapshotTransect();
    /**
     * @brief Create absent named transect, restore stored hydraulic/geometry/metadata fields and
     *   save whole registry to engine.
     * @note Existing name/absent registry/create failure no-op; curve additionally requires valid
     *   captured type. Provider setters and registry save statuses ignored, so restoration can be
     *   partial. No new preferences, independent transaction or exact engine-row-order guarantee.
     */
    void restoreTransect();

    SWMMModelLayer *m_layer = nullptr;
    SWMMObjectRef m_ref;
    bool m_captured = false; // snapshot succeeded

    // --- Curve snapshot ---
    int m_curveType = -1;           // openswmmvis::curve::CurveType as int
    QVector<QPointF> m_curvePoints; // (x, y)

    // --- Time series snapshot ---
    QString m_tsUnits, m_tsDescription;
    int m_tsSourceMode = 0; // TimeseriesProvider::SourceMode as int
    QString m_tsFilePath, m_tsColumnSelector;
    QDateTime m_tsFileMTime;
    QVector<QPair<QDateTime, double>> m_tsPoints; // (time, value)

    // --- Transect snapshot ---
    QString m_txComments;
    double m_txNLeft = 0, m_txNRight = 0, m_txNChannel = 0;
    double m_txXLeftBank = 0, m_txXRightBank = 0;
    double m_txXLeftEncroach = 0, m_txXRightEncroach = 0;
    double m_txXFactor = 1, m_txYFactor = 1, m_txLengthFactor = 1;
    QVector<QPair<double, double>> m_txPoints; // (station, elevation)
};

/*!
 * \class ReorderLayersCommand
 * \brief Records a full layer-stack reorder (e.g. moving an entire category
 *        group) as a single undoable step.
 */
class ReorderLayersCommand : public MapCommand
{
public:
    /**
     * @brief Capture complete before/after raw layer-pointer orders for an already-applied reorder.
     * @param[in] oldOrder Previous full pointer permutation; external layer ownership/lifetimes
     *   retained.
     * @param[in] newOrder Already-applied new permutation, later passed back without
     *   reconstruction.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note First redo skips unconditionally, caller must have applied new order before push.
     *   Subsequent calls use reorderLayers(...,false) and canvas validation; no layer deletion.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ReorderLayersCommand(QList<OpenSWMMVisLayer *> oldOrder, QList<OpenSWMMVisLayer *> newOrder,
                         MapCanvas *canvas, QUndoCommand *parent = nullptr);
    /**
     * @brief Restore old full order through canvas without nested undo; null canvas no-op.
     */
    void undo() override;
    /**
     * @brief Skip first call unconditionally; later apply new full order without nested undo when
     *   canvas exists.
     */
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
    /**
     * @brief Capture model display category permutation after caller already applied it.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] oldOrder Previous category ordering.
     * @param[in] newOrder Already-applied target category ordering.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Required live model on undo/later redo, no null guard. Only presentation order changes;
     *   engine object ordering remains unchanged. First redo skips.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ReorderCategoriesCommand(SWMMModelLayer *layer, QVector<SWMMModelLayer::Category> oldOrder,
                             QVector<SWMMModelLayer::Category> newOrder,
                             QUndoCommand *parent = nullptr);
    /**
     * @brief Set stored original category order through required live model; setter supplies
     *   normalization/signals.
     */
    void undo() override;
    /**
     * @brief Skip first call; later set stored new category order through live model.
     */
    void redo() override;

private:
    SWMMModelLayer *m_layer;
    QVector<SWMMModelLayer::Category> m_oldOrder;
    QVector<SWMMModelLayer::Category> m_newOrder;
    bool m_firstRedo = true;
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
    /**
     * @brief Capture per-category display bucket order after caller already applied it.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] cat Category whose display ordering is changed.
     * @param[in] oldOrder Prior bucket-index permutation, empty means restore default via
     *   clearObjectOrder.
     * @param[in] newOrder Already-applied target permutation; model validates on replay.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Required live model, no null guard. Saved indices belong to category buckets and can
     *   become stale after topology edits; no object-name remapping. First redo skips.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ReorderObjectsCommand(SWMMModelLayer *layer, SWMMModelLayer::Category cat,
                          QVector<int> oldOrder, // empty = was default
                          QVector<int> newOrder, QUndoCommand *parent = nullptr);
    /**
     * @brief Clear custom order if old vector empty, otherwise set old permutation. Does not change
     *   engine indices.
     */
    void undo() override;
    /**
     * @brief Skip first call; later set target bucket-index order. Live raw model required.
     */
    void redo() override;

private:
    SWMMModelLayer *m_layer;
    SWMMModelLayer::Category m_cat;
    QVector<int> m_oldOrder; // empty means "was default before this cmd"
    QVector<int> m_newOrder;
    bool m_firstRedo = true;
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
     *               command on undo.
     * @brief Take detached annotation ownership until deferred insertion into layer.
     * @param[in] layer Optional borrowed raw annotation layer, must outlive command; not guarded by
     *   QPointer.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Null item is accepted but cannot apply. Stores stable item ID at construction. Layer
     *   owns QObject parent chain after successful redo; command owns detached item after
     *   successful undo. Canvas invalidated only for successful transition.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    AddAnnotationCommand(OpenSWMMVisAnnotationLayer *layer, AnnotationTextItem *item,
                         MapCanvas *canvas, QUndoCommand *parent = nullptr);
    /**
     * @brief Delete annotation only while command still owns detached item; present layer-owned
     *   item remains.
     * @note Does not remove an applied annotation, invalidate canvas or detect raw layer
     *   destruction. GUI-thread destruction required.
     */
    ~AddAnnotationCommand() override;

    /**
     * @brief If layer/present, take annotation by stable ID, regain ownership and invalidate Scene
     *   only if found; missing item leaves present flag unchanged.
     */
    void undo() override;
    /**
     * @brief If layer/item exist and not present, add annotation, transfer ownership to layer,
     *   clear raw owned item pointer and invalidate Scene on success.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 21 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 21; }

private:
    OpenSWMMVisAnnotationLayer *m_layer = nullptr;
    AnnotationTextItem *m_item = nullptr; ///< owned only while detached
    QString m_itemId;                     ///< stable handle into the layer
    bool m_present = false;               ///< true iff currently in layer
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
    /**
     * @brief Capture name-based conduit split for deferred node insertion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkName Existing conduit identifier, survives on upstream half.
     * @param[in] t Normalized geometric split fraction, caller supplies interior 0<t<1;
     *   engine/model enforce split rules.
     * @param[in] nodeName Unique new inserted node ID.
     * @param[in] newLinkName Unique new downstream conduit ID.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Raw model borrowed, null replay no-op. Current named objects/topology must still
     *   satisfy engine rules; command keeps present state only after reported split success. No
     *   clamp, coordinate conversion or geometry calculation at construction.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    InsertVirtualJunctionCommand(SWMMModelLayer *layer, QString linkName, double t,
                                 QString nodeName, QString newLinkName, MapCanvas *canvas,
                                 QUndoCommand *parent = nullptr);

    /**
     * @brief If present, fuse inserted virtual junction by name; clear present only on success,
     *   otherwise keep applied state. No separate full snapshot.
     */
    void undo() override;
    /**
     * @brief If not present, apply virtual junction split; set present only on model true. Failed
     *   model post-processing can leave partial split despite false.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 22 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 22; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_linkName;    ///< conduit being split (name survives upstream)
    double m_t = 0.5;      ///< normalized split position
    QString m_nodeName;    ///< inserted virtual junction
    QString m_newLinkName; ///< new downstream conduit
    bool m_present = false;
};

/*!
 * \class InsertJunctionSplitCommand
 * \brief Records a conduit split that inserts a plain (non-virtual) junction.
 * \details redo() calls SWMMModelLayer::applyInsertJunctionSplit (engine
 *          `swmm_conduit_split` with `make_virtual = 0`); undo() calls
 *          applyFuseJunctionSplit, which flags the node virtual just long
 *          enough to reuse the engine's exact fuse inverse and rolls the flag
 *          back if the fuse is refused. Like the virtual-junction pair, a
 *          split→fuse round-trip restores the model byte-identically, so no
 *          snapshot machinery is needed.
 */
class InsertJunctionSplitCommand : public MapCommand
{
public:
    /**
     * @brief Capture name-based conduit split for deferred node insertion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkName Existing conduit identifier, survives on upstream half.
     * @param[in] t Normalized geometric split fraction, caller supplies interior 0<t<1;
     *   engine/model enforce split rules.
     * @param[in] nodeName Unique new inserted node ID.
     * @param[in] newLinkName Unique new downstream conduit ID.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Raw model borrowed, null replay no-op. Current named objects/topology must still
     *   satisfy engine rules; command keeps present state only after reported split success. No
     *   clamp, coordinate conversion or geometry calculation at construction.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    InsertJunctionSplitCommand(SWMMModelLayer *layer, QString linkName, double t, QString nodeName,
                               QString newLinkName, MapCanvas *canvas,
                               QUndoCommand *parent = nullptr);

    /**
     * @brief If present, apply plain split fuse inverse and clear present on success. Additional
     *   links/rule violations can refuse fuse; present retained, no rollback.
     */
    void undo() override;
    /**
     * @brief If not present, apply plain junction split and set present on success; null layer
     *   no-op.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 27 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 27; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_linkName;    ///< conduit being split (name survives upstream)
    double m_t = 0.5;      ///< normalized split position
    QString m_nodeName;    ///< inserted junction
    QString m_newLinkName; ///< new downstream conduit
    bool m_present = false;
};

/*!
 * \class InsertNodeSplitCommand
 * \brief Records "place a node ON a conduit": the conduit is split at the
 *        picked point and a node of the requested SWMM type is inserted there
 *        (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md step 2).
 * \details The engine's `swmm_conduit_split` always creates a JUNCTION, so
 *          for STORAGE / DIVIDER redo() splits, then converts the new junction
 *          with `applyNodeConvert` and applies the type's creation defaults
 *          (the same ObjectDefaultsApplier pass AddNodeCommand runs). A plain
 *          junction takes the split as-is — the engine's interpolated invert
 *          and conduit-derived depth are better than the defaults. Undo runs
 *          the inverse in reverse: convert back to a junction (the fuse
 *          inverse only accepts junctions), then re-fuse the conduit pair.
 *          OUTFALL is never accepted (an outfall must be terminal).
 *
 *          If the conversion is refused the node stays a junction and the
 *          engine's message is kept in warnings(); the split itself stands.
 */
class InsertNodeSplitCommand : public MapCommand
{
public:
    /**
     * @brief Capture name-based conduit split for deferred node insertion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkName Existing conduit identifier, survives on upstream half.
     * @param[in] t Normalized geometric split fraction, caller supplies interior 0<t<1;
     *   engine/model enforce split rules.
     * @param[in] nodeName Unique new inserted node ID.
     * @param[in] newLinkName Unique new downstream conduit ID.
     * @param[in] nodeType Requested junction 0/storage 2/divider 3; outfall unsupported by intended
     *   tool flow, model validates conversion.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Raw model borrowed, null replay no-op. Current named objects/topology must still
     *   satisfy engine rules; command keeps present state only after reported split success. No
     *   clamp, coordinate conversion or geometry calculation at construction.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    InsertNodeSplitCommand(SWMMModelLayer *layer, QString linkName, double t, QString nodeName,
                           QString newLinkName, int nodeType, MapCanvas *canvas,
                           QUndoCommand *parent = nullptr);

    /**
     * @brief If present, convert successfully retyped nonjunction back to junction, then fuse plain
     *   split. Either failure leaves partial applied state; successful demotion clears retyped
     *   before fuse attempt.
     */
    void undo() override;
    /**
     * @brief Clear warnings/retyped on eligible attempt, split ordinary junction, then optionally
     *   convert requested subtype and apply current node defaults. Conversion failure leaves split
     *   standing and stores warning.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 49 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 49; }

    /*! Engine warnings / the conversion error from the last redo() (e.g. a
     *  divider inserted mid-conduit has only two links until a third is drawn).
     * @return Copy of latest eligible redo warnings/conversion error. Null/already-present redo
     *   does not clear; undo does not clear. Split failure may return empty warnings; not a
     *   complete success indicator.
     */
    [[nodiscard]] QStringList warnings() const { return m_warnings; }
    /*! True when the node was actually retyped to nodeType (always true for a
     *  junction, which needs no conversion).
     * @return Stored successful requested-type flag: true after successful junction split or
     *   requested conversion, false initially/failed conversion/undo demotion. Does not
     *   independently query engine or guarantee defaults accepted; may remain true after junction
     *   undo.
     */
    [[nodiscard]] bool retyped() const { return m_retyped; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_linkName;    ///< conduit being split (name survives upstream)
    double m_t = 0.5;      ///< normalized split position
    QString m_nodeName;    ///< inserted node
    QString m_newLinkName; ///< new downstream conduit
    int m_nodeType = 0;    ///< SWMM_NodeType (0 junction, 2 storage, 3 divider)
    bool m_present = false;
    bool m_retyped = false;
    QStringList m_warnings;
};

/*!
 * \struct GeneratedOutfallSpec
 * \brief How a node the channel burn converts is configured as an outfall
 *        (CHANNEL_BURN_IN_PLAN_2026-09-21.md §6).
 *
 * \details Every field is optional so the command stays usable as a plain
 *          retype. The defaults are the burn's: a NORMAL, UNGATED outfall —
 *          a `Gated YES` flap gate BLOCKS the 2D tailwater override
 *          (`2D_INPUT_FORMAT_SPEC §4.4`), which would silently disable the
 *          very coupling the conversion exists to create.
 */
struct GeneratedOutfallSpec {
    static constexpr int kOutfallNormal = 1; ///< 0=FREE 1=NORMAL 2=FIXED 3=TIDAL 4=TIMESERIES.

    bool applyOutfall = false; ///< Master switch; false = retype only.
    int outfallType = kOutfallNormal;
    bool flapGate = false; ///< Must stay false for a coupled outfall.
    bool hasInvert = false;
    double invert = 0.0; ///< The channel bottom at the coupling point.
    bool hasMaxDepth = false;
    double maxDepth = 0.0; ///< Mesh bed elevation at the coupling point − invert.
    QString tag;           ///< e.g. "burn:outfall", so the run is reversible by hand.
};

/*!
 * \class ConvertNodeTypeCommand
 * \brief Undoable node retype, with the burn's outfall configuration
 *        (CHANNEL_BURN_IN_PLAN_2026-09-21.md §6).
 *
 * \details Retyping a node previously bypassed undo entirely except inside
 *          \ref InsertNodeSplitCommand. This is the general command, modelled
 *          on it: redo() converts through `SWMMModelLayer::applyNodeConvert`
 *          (so the cache and the four refresh signals behave) and then applies
 *          \ref GeneratedOutfallSpec; undo() converts back to the type the
 *          constructor snapshotted and restores invert, max depth and tag.
 *
 * \warning **A conversion is lossy when the node carried type-specific data.**
 *          The engine preserves the common properties and CLEARS the rest, and
 *          no API hands a storage curve or a divider's parameters back
 *          afterwards. So undo restores the TYPE and the common properties,
 *          not a storage node's curve. \ref isLossy reports it, and the burn
 *          names such nodes in its report rather than converting them quietly.
 *          For the burn's own case — a junction becoming an outfall — there is
 *          nothing to lose and the round trip is exact.
 */
class ConvertNodeTypeCommand : public MapCommand
{
public:
    /**
     * @brief Snapshot node type/invert/max-depth/tag for deferred retype and optional generated
     *   outfall settings.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] nodeName Exact current node ID.
     * @param[in] newNodeType Requested engine node-type code; model/engine validate.
     * @param[in] spec Optional outfall configuration, project-unit invert/depth and nonempty tag
     *   overlay.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Snapshot marked available after finding node; individual scalar getter failures retain
     *   defaults, tag buffer 256 bytes. Subtype data is not captured and conversion can lose it
     *   irreversibly. Does not apply general creation defaults.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    ConvertNodeTypeCommand(SWMMModelLayer *layer, QString nodeName, int newNodeType,
                           GeneratedOutfallSpec spec, MapCanvas *canvas,
                           QUndoCommand *parent = nullptr);

    /**
     * @brief If converted, convert back if needed then restore captured invert/max-depth/tag.
     *   Rejected inverse retains converted; successful type inverse clears it even if later scalar
     *   writes fail. Subtype data not restored.
     */
    void undo() override;
    /**
     * @brief If captured/not already converted, clear warnings, convert if types differ then apply
     *   spec; same-type request still marks converted and applies spec. Converted tracks type
     *   operation, not spec setter success.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 50 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 50; }

    /*! Engine warnings from the last redo(), plus the lossy-conversion note.
     * @return Last eligible redo conversion warnings/error plus lossy-data note when cleared fields
     *   reported; unchanged by undo or skipped redo. Does not include individual outfall-spec
     *   setter failures.
     */
    [[nodiscard]] QStringList warnings() const { return m_warnings; }
    /*! True once the node is at \c newNodeType.
     * @return Stored successful conversion/same-type processing flag, not live engine query or
     *   guarantee spec applied. Cleared after accepted undo; false initially/failure.
     */
    [[nodiscard]] bool converted() const { return m_converted; }
    /*! True when the ORIGINAL type carried type-specific data undo cannot
     *  restore (anything but a junction).
     * @return Original captured type code greater than zero (nonjunction), a conservative
     *   subtype-loss indicator even for same-type request. False for junction/default uncaptured
     *   type; no engine query.
     */
    [[nodiscard]] bool isLossy() const { return m_oldType > 0; }

private:
    /**
     * @brief If enabled, apply optional invert/depth plus outfall type/gate and nonempty tag
     *   directly to named node.
     * @note Null/closed/missing target or applyOutfall=false no-op. Does not validate target
     *   outfall subtype or check statuses, markEdited or explicitly invalidate/refresh after direct
     *   scalars. Project feet/metres; empty tag leaves current tag unchanged.
     */
    void applySpec();

    SWMMModelLayer *m_layer = nullptr;
    QString m_nodeName;
    int m_newType = 0;
    int m_oldType = 0;
    GeneratedOutfallSpec m_spec;

    // Snapshot of the common properties, which the engine preserves across the
    // conversion but the spec may overwrite.
    double m_oldInvert = 0.0;
    double m_oldMaxDepth = 0.0;
    QString m_oldTag;
    bool m_haveSnapshot = false;
    bool m_converted = false;
    QStringList m_warnings;
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
    /**
     * @brief Snapshot conduit names, length ratio, node invert/native position before fusion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] nodeName Existing candidate junction ID; rules rechecked on actual fuse.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Scans all links and keeps last incoming and last outgoing, requiring distinct indices
     *   and positive summed lengths. Snapshot readiness does not prove exactly two links, conduit
     *   types or virtual/inlet role; actual engine fuse can reject. Native XY and project-unit
     *   invert/length copied without getter-status validation.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    FuseVirtualJunctionCommand(SWMMModelLayer *layer, QString nodeName, MapCanvas *canvas,
                               QUndoCommand *parent = nullptr);

    /**
     * @brief If fused, re-split surviving conduit at stored length ratio, then restore saved
     *   invert/native XY. Clears fuse flag after split, before unchecked later restores; no full
     *   junction subtype snapshot.
     */
    void undo() override;
    /**
     * @brief If valid/not fused, call model virtual-junction fuse and mark applied only on success.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 23 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 23; }

    /*! \brief False when the minimum fusion snapshot could not be captured;
     *         valid does not replace engine topology/role validation.
     * @return Snapshot-ready flag, requiring named node, distinct found incoming/outgoing links and
     *   positive summed lengths. Not full topology/type/role validation; engine fuse still may
     *   refuse.
     */
    bool valid() const { return m_valid; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_nodeName;
    QString m_upLinkName;  ///< surviving conduit
    QString m_dnLinkName;  ///< retired conduit (re-created on undo)
    double m_t = 0.5;      ///< L_up / (L_up + L_dn)
    double m_invert = 0.0; ///< node invert (grade break — not derivable)
    double m_x = 0.0, m_y = 0.0;
    bool m_valid = false;
    bool m_present = false; ///< true iff the fuse is currently applied
};

/*!
 * \class SetInletUsageCommand
 * \brief Records a change to one inlet-usage row (conduit host or inlet
 *        junction host — the same command serves both, per §2.4/§3.4 of
 *        workplans/INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md).
 * \details The constructor snapshots the host's PRIOR row (or its absence),
 *          so undo restores the previous design / capture node / counts, or
 *          removes the row again when there was none. redo() applies the new
 *          row; passing an "absent" new state makes this a removal command.
 */
class SetInletUsageCommand : public MapCommand
{
public:
    /*! \param newUsage   Row to install on redo.
     *  \param removing   true to REMOVE the host's row instead of setting it
     *                    (only newUsage.host_kind / host_idx are then read).
     * @brief Snapshot existing host usage row or its absence before set/removal.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Raw SWMM_InletUsage numeric indices copied unchanged, not name-resolved after topology
     *   repacking. Caller must preserve host/design/capture index identities for replay;
     *   constructor getter failure is indistinguishable from absent row. No first-redo skip.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    SetInletUsageCommand(SWMMModelLayer *layer, const SWMM_InletUsage &newUsage, bool removing,
                         MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Restore prior whole usage if captured, otherwise remove selected new host. Numeric
     *   indices must remain coherent; statuses ignored.
     */
    void undo() override;
    /**
     * @brief Apply new usage or remove selected host according to stored removing flag; null layer
     *   no-op, mutation statuses ignored.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 24 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 24; }

private:
    SWMMModelLayer *m_layer = nullptr;
    SWMM_InletUsage m_new{};
    SWMM_InletUsage m_old{};
    bool m_removing = false; ///< redo removes rather than sets
    bool m_hadOld = false;   ///< a row existed before this command
};

/*!
 * \class SetNodeCoupling2DCommand
 * \brief Records a change to one or more nodes' 1D↔2D coupling structure
 *        ([2D_NODE_COUPLING]) — a property edit, a bulk "set coupleable" or
 *        the remap dialog's node changes — as one undo step.
 * \details The constructor snapshots each node's prior state; redo() writes
 *          the new states (no first-redo skip: push() applies them).
 */
class SetNodeCoupling2DCommand : public MapCommand
{
public:
    /**
     * @brief Snapshot each requested named node coupling state before bulk replacement.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] newStates Target State list, names copied with structure/gate/project-unit
     *   parameters; duplicates retained.
     * @param[in] text Undo-menu label copied unchanged.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Each old lookup result ignored and default State appended on failure, so undo may
     *   attempt empty/default state. Name-based State survives index shifts but raw model lifetime
     *   still required; no validation/atomicity.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    SetNodeCoupling2DCommand(SWMMModelLayer *layer, QVector<nodecoupling2d::State> newStates,
                             const QString &text, MapCanvas *canvas,
                             QUndoCommand *parent = nullptr);

    /**
     * @brief Apply captured states in reverse order; default states from failed capture are still
     *   attempted, no rollback/status reporting.
     */
    void undo() override;
    /**
     * @brief Apply target states in forward order through model; null layer no-op, statuses
     *   ignored, partial changes possible.
     */
    void redo() override;

private:
    SWMMModelLayer *m_layer = nullptr;
    QVector<nodecoupling2d::State> m_new;
    QVector<nodecoupling2d::State> m_old;
};

/*!
 * \class InsertInletJunctionCommand
 * \brief Records a STREET-conduit split that inserts a configured inlet
 *        junction (`swmm_conduit_split_inlet`).
 * \details Like InsertVirtualJunctionCommand, undo is the engine's exact
 *          inverse (`swmm_inlet_junction_fuse`), which removes the usage row
 *          and re-fuses the pair — so no snapshot machinery is needed.
 */
class InsertInletJunctionCommand : public MapCommand
{
public:
    /**
     * @brief Capture name-based conduit split for deferred inlet junction creation.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] linkName Existing conduit identifier, survives on upstream half.
     * @param[in] t Normalized geometric split fraction, caller supplies interior 0<t<1;
     *   engine/model enforce split rules.
     * @param[in] nodeName Unique new inserted node ID.
     * @param[in] newLinkName Unique new downstream conduit ID.
     * @param[in] inletId Existing inlet design name for STREET split.
     * @param[in] captureNode Existing receiving/capture node name, distinct topology constraints
     *   checked by engine.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Raw model borrowed, null replay no-op. Current named objects/topology must still
     *   satisfy engine rules; command keeps present state only after reported split success. No
     *   clamp, coordinate conversion or geometry calculation at construction.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    InsertInletJunctionCommand(SWMMModelLayer *layer, QString linkName, double t, QString nodeName,
                               QString newLinkName, QString inletId, QString captureNode,
                               MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief If present, fuse inlet junction, deleting usage and merged half through engine
     *   inverse; clear present only on success.
     */
    void undo() override;
    /**
     * @brief If not present, apply configured inlet junction STREET-conduit split and set present
     *   only on success.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 25 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 25; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_linkName;    ///< conduit being split (name survives upstream)
    double m_t = 0.5;      ///< normalized split position
    QString m_nodeName;    ///< inserted inlet junction
    QString m_newLinkName; ///< new downstream conduit
    QString m_inletId;     ///< inlet design name
    QString m_captureNode; ///< receiving (underdrain) node name
    bool m_present = false;
};

/*!
 * \class FuseInletJunctionCommand
 * \brief Records the re-fusion (deletion) of an inlet junction.
 * \details Snapshots what FuseVirtualJunctionCommand does (conduit names,
 *          split ratio, invert, coordinate) PLUS the usage row, because the
 *          fuse drops it. undo() re-splits through `swmm_conduit_split_inlet`
 *          — which recreates a default usage row — then restores the exact
 *          snapshot row via `swmm_inlet_usage_set`.
 */
class FuseInletJunctionCommand : public MapCommand
{
public:
    /**
     * @brief Snapshot conduit names, length ratio, node invert/native position and inlet usage
     *   names/row before fusion.
     * @param[in] layer Borrowed raw model pointer, must outlive command and refer to intended
     *   engine during replay; GUI-thread access.
     * @param[in] nodeName Existing candidate junction ID; rules rechecked on actual fuse.
     * @param[in] canvas Optional borrowed raw canvas; caller keeps it alive during command
     *   lifetime.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns after push.
     * @note Scans all links and keeps last incoming and last outgoing, requiring distinct indices
     *   and positive summed lengths. Snapshot readiness does not prove exactly two links, conduit
     *   types or virtual/inlet role; actual engine fuse can reject. Native XY and project-unit
     *   invert/length copied without getter-status validation.
     * @note Constructor stores/snapshots state without applying. Normal QUndoStack push immediately
     *   invokes redo; errors do not automatically reverse the stack index.
     */
    FuseInletJunctionCommand(SWMMModelLayer *layer, QString nodeName, MapCanvas *canvas,
                             QUndoCommand *parent = nullptr);

    /**
     * @brief Re-split surviving STREET conduit using saved design/capture names, restore invert/XY
     *   then old usage with current host/design/capture indices. Clears fuse flag after split;
     *   failed later restores not rolled back.
     */
    void undo() override;
    /**
     * @brief If valid/not fused, call model inlet-junction fuse and mark applied only on success.
     */
    void redo() override;
    /**
     * @return Constant command-family ID 26 used by Qt merge dispatch; no state mutation. Only
     *   classes with explicit mergeWith implement merging, and callers must not pass another
     *   concrete class solely because IDs collide.
     */
    int id() const override { return 26; }

    /*! \brief False when the minimum fusion snapshot/usage could not be captured;
     *         valid does not replace engine topology/role validation.
     * @return Snapshot-ready flag, requiring named node, distinct found incoming/outgoing links and
     *   positive summed lengths, readable node-host usage and nonempty design/capture names. Not
     *   full topology/type/role validation; engine fuse still may refuse.
     */
    bool valid() const { return m_valid; }

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_nodeName;
    QString m_upLinkName;  ///< surviving conduit
    QString m_dnLinkName;  ///< retired conduit (re-created on undo)
    double m_t = 0.5;      ///< L_up / (L_up + L_dn)
    double m_invert = 0.0; ///< node invert (grade break — not derivable)
    double m_x = 0.0, m_y = 0.0;
    QString m_inletId;         ///< design name at snapshot time
    QString m_captureNode;     ///< capture node name at snapshot time
    SWMM_InletUsage m_usage{}; ///< full row (counts, clogging, placement, …)
    bool m_valid = false;
    bool m_present = false; ///< true iff the fuse is currently applied
};

#endif // MAPUNDOSTACK_H
