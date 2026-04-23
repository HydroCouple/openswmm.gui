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

#include <QObject>
#include <QUndoStack>
#include "map/mapextent.h"

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
    AddNodeCommand(SWMMModelLayer *layer,
                   QString name,
                   int nodeType,
                   double x, double y,
                   MapCanvas *canvas,
                   QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override { return 12; }

private:
    SWMMModelLayer *m_layer    = nullptr;
    QString         m_name;
    int             m_nodeType = 0;
    double          m_x        = 0.0;
    double          m_y        = 0.0;
    bool            m_present  = false;  // true iff currently applied
};

#endif // MAPUNDOSTACK_H
