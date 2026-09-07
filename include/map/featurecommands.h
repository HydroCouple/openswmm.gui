/*!
 * \file   featurecommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Undoable edits to a FeatureLayer
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3).
 *
 * Ownership follows AddAnnotationCommand (mapundostack.h:964-1000), the only
 * existing precedent for a user-created object that a layer owns: the command
 * holds a detached copy while the edit is undone, the layer owns it while the
 * edit is applied, and THE FEATURE ID IS STABLE across redo/undo cycles so
 * selections, property panels and later commands keep referring to the same
 * thing. FeatureStore::addFeature's FID-reuse path exists solely to honour
 * that contract.
 *
 * Every tool and every view mutates a FeatureLayer through these commands and
 * never through the layer's setters directly — that is the controller half of
 * the MVC split CLAUDE.md §5.1 asks for, and it is what makes the same edit
 * undoable whether it came from the map, the attribute table or the property
 * panel.
 *
 * Layer lifetime: commands hold a QPointer, so a command left on the stack
 * after its layer is removed becomes a no-op rather than a crash. That mirrors
 * how the SWMM commands survive a model close.
 */

#ifndef FEATURECOMMANDS_H
#define FEATURECOMMANDS_H

#include "map/mapundostack.h"
#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"
#include "layers/featurelayer.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>

class MapCanvas;

namespace openswmmvis::map {

/*! Command ids for QUndoCommand::mergeWith. Values are arbitrary but must not
 *  collide with the ids the existing map commands use, so they start high. */
enum FeatureCommandId
{
    FeatureEditGeometryId = 0x4645'0001,   // 'FE' 0001
};

/*!
 * \class FeatureCommandBase
 * \brief Shared plumbing: a QPointer to the layer plus error reporting.
 */
class FeatureCommandBase : public MapCommand
{
public:
    FeatureCommandBase(const QString &text, FeatureLayer *layer,
                       MapCanvas *canvas, QUndoCommand *parent = nullptr);

    [[nodiscard]] FeatureLayer *layer() const { return m_layer.data(); }
    /*! \brief The message from the last failed apply, for the caller to show.
     *         Empty when the command has not failed. */
    [[nodiscard]] QString lastError() const { return m_lastError; }

protected:
    /*! Log \p err and remember it. Commands cannot show UI from redo()/undo()
     *  — QUndoStack may be replaying — so a failure is recorded, logged, and
     *  surfaced by whoever pushed the command. */
    void recordFailure(const QString &err) const;

    QPointer<FeatureLayer> m_layer;
    mutable QString        m_lastError;
};

/*!
 * \class AddFeatureCommand
 * \brief Insert one feature; undo removes it.
 *
 * The feature's id is assigned by the first redo and REUSED by every
 * subsequent redo, so undo/redo cycles do not renumber the layer.
 */
class AddFeatureCommand : public FeatureCommandBase
{
public:
    AddFeatureCommand(FeatureLayer *layer,
                      const openswmmvis::feature::Feature &feature,
                      MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

    /*! \brief The stored id; valid only after the first redo. */
    [[nodiscard]] openswmmvis::feature::FeatureId featureId() const { return m_feature.id; }

private:
    openswmmvis::feature::Feature m_feature;
};

/*!
 * \class DeleteFeaturesCommand
 * \brief Remove one or more features; undo restores them with their ids.
 */
class DeleteFeaturesCommand : public FeatureCommandBase
{
public:
    DeleteFeaturesCommand(FeatureLayer *layer,
                          const QVector<openswmmvis::feature::FeatureId> &ids,
                          MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    /*! Full snapshots taken at construction — geometry AND attributes — so
     *  undo restores the feature exactly, not just its outline. */
    QVector<openswmmvis::feature::Feature> m_removed;
};

/*!
 * \class EditFeatureGeometryCommand
 * \brief Replace one feature's geometry.
 *
 * \details Merges with a following edit of the SAME feature so a vertex drag
 *          collapses to one undo step, the way MoveNodeCommand::mergeWith
 *          (mapundostack.h:339) does for a node drag. Set \p mergeable false
 *          for a discrete edit (insert / delete vertex, add hole, add part)
 *          that should stand on its own.
 */
class EditFeatureGeometryCommand : public FeatureCommandBase
{
public:
    EditFeatureGeometryCommand(FeatureLayer *layer,
                               openswmmvis::feature::FeatureId id,
                               const openswmmvis::feature::FeatureGeometry &oldGeom,
                               const openswmmvis::feature::FeatureGeometry &newGeom,
                               const QString &text,
                               bool mergeable,
                               MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

    [[nodiscard]] int id() const override
    { return m_mergeable ? FeatureEditGeometryId : -1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    openswmmvis::feature::FeatureId       m_id;
    openswmmvis::feature::FeatureGeometry m_old;
    openswmmvis::feature::FeatureGeometry m_new;
    bool                                  m_mergeable;
};

/*!
 * \class EditFeatureAttributesCommand
 * \brief Replace one feature's attributes.
 *
 * Used by the attribute table and the property panel alike, which is what
 * keeps the two views synchronised through one undo history.
 */
class EditFeatureAttributesCommand : public FeatureCommandBase
{
public:
    EditFeatureAttributesCommand(FeatureLayer *layer,
                                 openswmmvis::feature::FeatureId id,
                                 const QVariantMap &oldAttrs,
                                 const QVariantMap &newAttrs,
                                 MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    openswmmvis::feature::FeatureId m_id;
    QVariantMap m_old;
    QVariantMap m_new;
};

/*!
 * \class AddFieldCommand
 * \brief Add a schema column; undo drops it.
 *
 * \warning Undo depends on the GDAL build being able to delete a GeoPackage
 *          column (FeatureStore::removeField tests OLCDeleteField). When it
 *          cannot, undo leaves the column in place and records the reason in
 *          \ref lastError rather than pretending to have removed it.
 */
class AddFieldCommand : public FeatureCommandBase
{
public:
    AddFieldCommand(FeatureLayer *layer,
                    const openswmmvis::feature::FieldDef &field,
                    MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    openswmmvis::feature::FieldDef m_field;
};

/*!
 * \class RemoveFieldCommand
 * \brief Drop a schema column, snapshotting its values so undo restores them.
 */
class RemoveFieldCommand : public FeatureCommandBase
{
public:
    RemoveFieldCommand(FeatureLayer *layer, const QString &fieldName,
                       MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

private:
    openswmmvis::feature::FieldDef m_field;    ///< Captured at construction.
    /*! Per-feature values of the dropped column, so undo restores content and
     *  not merely an empty column. */
    QHash<openswmmvis::feature::FeatureId, QVariant> m_values;
};

/*!
 * \class ResampleZCommand
 * \brief Re-sample Z for every feature (or a subset) as ONE undo step.
 *
 * Snapshots the previous geometries; the whole resample reverts together
 * because a partial revert would leave a layer with mixed vintages of terrain.
 */
class ResampleZCommand : public FeatureCommandBase
{
public:
    /*! \param ids  Features to resample; empty means every feature. */
    ResampleZCommand(FeatureLayer *layer,
                     const QVector<openswmmvis::feature::FeatureId> &ids,
                     MapCanvas *canvas, QUndoCommand *parent = nullptr);

    void redo() override;
    void undo() override;

    /*! \brief Vertices left NaN by the last redo — the count the Features dock
     *         reports so an unsampled breakline is visible, not silent. */
    [[nodiscard]] int unsampledCount() const { return m_unsampled; }

private:
    QVector<openswmmvis::feature::FeatureId>              m_ids;
    QVector<openswmmvis::feature::FeatureGeometry>        m_before;
    QVector<openswmmvis::feature::FeatureGeometry>        m_after;
    bool m_captured  = false;   ///< m_after is filled by the first redo only.
    int  m_unsampled = 0;
};

}   // namespace openswmmvis::map

#endif // FEATURECOMMANDS_H
