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

#endif // MAPUNDOSTACK_H
