/**
 * @file      featurecommands.h
 * @brief     Undoable edits to a FeatureLayer
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::map
{

/*! Command ids for QUndoCommand::mergeWith. Values are arbitrary but must not
 *  collide with the ids the existing map commands use, so they start high. */
enum FeatureCommandId {
    FeatureEditGeometryId = 0x4645'0001, // 'FE' 0001
};

/*!
 * \class FeatureCommandBase
 * \brief Shared plumbing: a QPointer to the layer plus error reporting.
 */
class FeatureCommandBase : public MapCommand
{
public:
    /**
     * @brief Store guarded feature-layer target, borrowed canvas and undo text without changing
     *   layer.
     * @param[in] text Undo-menu text copied to command.
     * @param[in] layer Optional borrowed layer guarded by QPointer; destroyed layer makes derived
     *   apply a no-op.
     * @param[in] canvas Optional borrowed raw canvas used for invalidation/Z sampling; must outlive
     *   command.
     * @param[in] parent Optional command owner, otherwise undo stack owns after push.
     * @note GUI-thread command family: layer edits obey FeatureLayer editable/store checks and
     *   persistence/notification behavior. Failures are logged and remembered rather than
     *   displayed; multi-row commands may partially succeed, with no transaction or automatic stack
     *   rollback.
     */
    FeatureCommandBase(const QString &text, FeatureLayer *layer, MapCanvas *canvas,
                       QUndoCommand *parent = nullptr);

    /**
     * @return Borrowed guarded layer pointer or null after deletion; no ownership transfer,
     *   editable/open-state validation or lifetime pin across calls.
     */
    [[nodiscard]] FeatureLayer *layer() const { return m_layer.data(); }
    /*! \brief The message from the last failed apply, for the caller to show.
     *         Empty when the command has not failed.
     * @return Last recorded failure string by value, empty before first failure. Successful later
     *   applies do not clear it; batch failure overwrites earlier messages, so nonempty does not
     *   prove most recent apply failed.
     */
    [[nodiscard]] QString lastError() const { return m_lastError; }

protected:
    /*! Log \p err and remember it. Commands cannot show UI from redo()/undo()
     *  — QUndoStack may be replaying — so a failure is recorded, logged, and
     *  surfaced by whoever pushed the command.
     * @param[in] err Failure message copied verbatim, including empty input.
     * @note Replaces stored error and writes a warning to feature-command logging category. Const
     *   method mutates mutable error; no UI, signal, rollback or command obsolescence.
     */
    void recordFailure(const QString &err) const;

    QPointer<FeatureLayer> m_layer;
    mutable QString m_lastError;
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
    /**
     * @brief Copy full feature geometry/attributes/optional stable ID for later insertion.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] feature Value snapshot in native layer CRS, including Z and attributes in schema
     *   conventions; invalid ID permits allocation.
     * @note No write at construction. First successful redo stores returned ID; later redo requests
     *   its reuse. Caller must preserve normal undo ordering and avoid another feature taking that
     *   ID.
     */
    AddFeatureCommand(FeatureLayer *layer, const openswmmvis::feature::Feature &feature,
                      MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Insert stored full feature and retain returned stable ID for future redo.
     * @note Missing layer no-op; invalid returned ID records failure and returns before canvas
     *   invalidation. Success uses layer add/persistence/notifications then invalidates Scene and
     *   Overlay. Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Remove the inserted stable feature ID when layer/ID survives.
     * @note Failure logged; live valid-ID path invalidates Scene/Overlay even when removal fails.
     *   ID retained for redo.
     */
    void undo() override;

    /*! \brief The stored id; insertion success must be checked separately.
     * @return Stored snapshot ID, updated after successful insertion. Can be supplied noninvalid
     *   before first redo and remains after undo/failure; does not prove feature currently exists.
     */
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
    /**
     * @brief Snapshot existing requested features at construction before selection or layer
     *   changes.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] ids Stable feature IDs in requested order; missing IDs skipped, duplicates
     *   retained.
     * @note Copies full geometry/attributes and original IDs. Neither clears selection nor deletes
     *   until redo. Bulk replay continues after failures and is not atomic; only snapshotted
     *   features are targeted.
     */
    DeleteFeaturesCommand(FeatureLayer *layer, const QVector<openswmmvis::feature::FeatureId> &ids,
                          MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Remove every construction-snapshotted feature by original ID.
     * @note Continues after individual failures, recording last message; with live layer
     *   invalidates Scene and Overlay even for empty snapshot/all failures. No atomic rollback.
     *   Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Reinsert all original full snapshots requesting their original IDs.
     * @note Continues after errors and invalidates Scene/Overlay. Returned successful IDs are not
     *   verified equal to requested IDs here; layer/store ID contract supplies stable reuse.
     */
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
    /**
     * @brief Capture complete old/new native-layer geometry for a stable feature ID.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] id Stable existing feature ID resolved by store on apply.
     * @param[in] oldGeom Before geometry supplied by caller; no automatic snapshot/validation.
     * @param[in] newGeom Target geometry including Z in native layer CRS; layer applies its
     *   geometry checks.
     * @param[in] text Undo-menu text; empty chooses translated Edit geometry.
     * @param[in] mergeable Allow consecutive same-layer/same-feature geometry edits to merge; false
     *   gives ID minus one.
     * @note Copies values without writing. Undo replaces full geometry rather than merging later
     *   changes; schema attributes remain through layer geometry setter.
     */
    EditFeatureGeometryCommand(FeatureLayer *layer, openswmmvis::feature::FeatureId id,
                               const openswmmvis::feature::FeatureGeometry &oldGeom,
                               const openswmmvis::feature::FeatureGeometry &newGeom,
                               const QString &text, bool mergeable, MapCanvas *canvas,
                               QUndoCommand *parent = nullptr);

    /**
     * @brief Replace target geometry with stored after geometry.
     * @note Layer failure logged, then Scene/Overlay invalidated even on failure; null layer no-op.
     *   Does not update stored before state. Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Restore stored full before geometry.
     * @note Failure logged; live layer invalidates Scene/Overlay even on rejected write. Does not
     *   merge with external geometry changes.
     */
    void undo() override;

    /**
     * @return FeatureEditGeometryId when mergeable, otherwise minus one to disable Qt merge
     *   attempts; no mutation.
     */
    [[nodiscard]] int id() const override { return m_mergeable ? FeatureEditGeometryId : -1; }
    /**
     * @param[in] other Optional borrowed incoming geometry command; both must allow merging and
     *   share guarded layer and stable feature ID.
     * @return True after replacing only stored after geometry; original before snapshot retained.
     *   False otherwise. No time threshold or geometry write here; normal QUndoStack has already
     *   redone incoming command.
     */
    bool mergeWith(const QUndoCommand *other) override;

private:
    openswmmvis::feature::FeatureId m_id;
    openswmmvis::feature::FeatureGeometry m_old;
    openswmmvis::feature::FeatureGeometry m_new;
    bool m_mergeable;
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
    /**
     * @brief Copy complete before/after attribute maps for later schema-conformed replacement.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] id Stable target ID; missing/read-only object fails through layer.
     * @param[in] oldAttrs Caller-captured full before attribute map, not a delta.
     * @param[in] newAttrs Full target map conformed to current schema, with missing/default/type
     *   behavior from Schema::conform.
     * @note No construction-time write. Geometry retained. Independent attribute edits after
     *   capture can be overwritten by undo/redo; no per-key merge or transactional rollback.
     */
    EditFeatureAttributesCommand(FeatureLayer *layer, openswmmvis::feature::FeatureId id,
                                 const QVariantMap &oldAttrs, const QVariantMap &newAttrs,
                                 MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Replace target attribute map with schema-conformed after snapshot.
     * @note Layer failure logged; live-layer path invalidates Scene/Overlay even on failure. No
     *   per-key retry or geometry edit. Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Restore complete before attribute map through current schema.
     * @note Failure logged and Scene/Overlay invalidated with live layer. Current schema
     *   conformance can differ after unrelated schema changes.
     */
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
    /**
     * @brief Copy schema column definition for deferred add and inverse removal.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] field New field name/type/default/metadata; layer/store validate capabilities on
     *   apply.
     * @note No construction-time mutation. Undo deletes the column and its then-current values;
     *   values independently edited after add are not snapshotted. GDAL capability failure leaves
     *   column present and records lastError.
     */
    AddFieldCommand(FeatureLayer *layer, const openswmmvis::feature::FieldDef &field,
                    MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Add captured schema field through FeatureLayer.
     * @note Failure logged; no explicit canvas invalidation in this command. Layer supplies
     *   schemaChanged/afterWrite notifications and storage persistence. Initial QUndoStack push
     *   invokes this method.
     */
    void redo() override;
    /**
     * @brief Delete the column introduced by redo through current storage driver.
     * @note Failure leaves field present and records reason; no saved subsequent column values or
     *   explicit canvas invalidation.
     */
    void undo() override;

private:
    openswmmvis::feature::FieldDef m_field;
};

/*!
 * \class RenameFieldCommand
 * \brief Rename a schema column, keeping its values ("Update fields to
 *        role…" renames the legacy quad_spacing to h —
 *        FEATURE_LAYER_ROLES_AND_FIELDS_PLAN §3 P7).
 */
class RenameFieldCommand : public FeatureCommandBase
{
public:
    /**
     * @brief Capture schema rename, sanitizing destination once at construction.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] from Existing exact column name copied unchanged.
     * @param[in] to Requested destination passed through sanitizeFieldName; sanitized result used
     *   for redo and inverse undo.
     * @note No write until apply. Store handles duplicate/reserved-name/capability rejection, with
     *   logged persistent error. Display text retains originally requested destination.
     */
    RenameFieldCommand(FeatureLayer *layer, const QString &from, const QString &to,
                       MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Rename original field to stored sanitized destination.
     * @note Failure logged; schema notifications/persistence follow layer/store. No explicit canvas
     *   invalidation. Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Rename sanitized destination back to original exact name.
     * @note Current schema/capabilities may reject and leave rename in place; logs reason, no
     *   command-level rollback.
     */
    void undo() override;

private:
    QString m_from;
    QString m_to;
};

/*!
 * \class SetFieldMetadataCommand
 * \brief Replace a column's stored default, description and Fixed value list
 *        (name and type unchanged); undo restores the previous ones.
 */
class SetFieldMetadataCommand : public FeatureCommandBase
{
public:
    /**
     * @brief Capture target metadata while deferring old metadata snapshot until first redo finds
     *   the column.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] fieldName Exact current field name, possibly created/renamed by an earlier macro
     *   child.
     * @param[in] meta Target default/description/fixed-value metadata; existing name/type remain
     *   through layer/store contract.
     * @note Allows macro ordering before column exists. Old valid FieldDef retained across cycles;
     *   a failed missing-field first redo can retry capture later. No row-value rewrite or direct
     *   canvas invalidation by command.
     */
    SetFieldMetadataCommand(FeatureLayer *layer, const QString &fieldName,
                            const openswmmvis::feature::FieldDef &meta, MapCanvas *canvas,
                            QUndoCommand *parent = nullptr);

    /**
     * @brief Capture original metadata if needed, then apply target field metadata.
     * @note Missing column before capture records failure and returns. Later setter failure also
     *   recorded; original metadata retained for undo. Layer handles schema notifications. Initial
     *   QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Restore captured metadata if first redo previously captured a valid field.
     * @note Null layer/uncaptured old field no-op; setter failure logged. Does not restore field
     *   name/type or feature values.
     */
    void undo() override;

private:
    QString m_name;
    openswmmvis::feature::FieldDef m_old; ///< Captured on the first redo.
    openswmmvis::feature::FieldDef m_new;
};

/*!
 * \class RemoveFieldCommand
 * \brief Drop a schema column, snapshotting its values so undo restores them.
 */
class RemoveFieldCommand : public FeatureCommandBase
{
public:
    /**
     * @brief Snapshot schema definition and all present feature values at construction before
     *   dropping column.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @param[in] fieldName Existing exact column name; missing/invalid definition makes undo/redo
     *   no-op.
     * @note Captures values only for features whose attribute map contains key. Undo adds column
     *   then restores each still-existing feature value while retaining its other current
     *   attributes. Does not recreate deleted features; failures can leave partial restoration.
     */
    RemoveFieldCommand(FeatureLayer *layer, const QString &fieldName, MapCanvas *canvas,
                       QUndoCommand *parent = nullptr);

    /**
     * @brief Remove construction-snapshotted field when definition remains valid.
     * @note Missing layer/invalid definition no-op; failure logged. No explicit canvas
     *   invalidation; store capability determines whether field is removed. Initial QUndoStack push
     *   invokes this method.
     */
    void redo() override;
    /**
     * @brief Recreate removed schema field, then restore snapshotted values into still-existing
     *   features.
     * @note Add failure logs and returns; later per-feature errors logged while continuing. Missing
     *   features skipped, other current attributes retained. No atomic restore or direct canvas
     *   invalidation.
     */
    void undo() override;

private:
    openswmmvis::feature::FieldDef m_field; ///< Captured at construction.
    /*! Per-feature values of the dropped column, so undo restores content and
     *  not merely an empty column. */
    QHash<openswmmvis::feature::FeatureId, QVariant> m_values;
};

/*!
 * \class ResampleZCommand
 * \brief Re-sample Z for every feature (or a subset) as ONE undo step.
 *
 * Snapshots previous geometries and replays them in one undo step. Individual
 * feature writes can fail; replay records errors and continues rather than
 * providing an atomic storage transaction.
 */
class ResampleZCommand : public FeatureCommandBase
{
public:
    /*! \param ids  Features to resample; empty means every feature.
     * @brief Snapshot selected feature geometries for one cached terrain/mesh Z resampling
     *   operation.
     * @param[in] layer Optional borrowed FeatureLayer guarded by QPointer; edits require open
     *   writable store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command when nonnull.
     * @param[in] parent Optional command owner, otherwise undo stack after push.
     * @note Empty IDs expand to current layer featureIds at construction; missing IDs get empty
     *   before geometry, duplicate IDs retained. First redo samples/densifies those snapshots once
     *   against then-current canvas sources; future redo reuses cached results rather than
     *   resampling changed terrain. No mutation at construction.
     */
    ResampleZCommand(FeatureLayer *layer, const QVector<openswmmvis::feature::FeatureId> &ids,
                     MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Sample/densify captured geometries once, cache results and write each target geometry.
     * @note Sampling uses current layer policy/canvas only on first call; unsampled Z count cached
     *   even if writes fail. Continues after write errors, invalidates Scene/Overlay with live
     *   layer; not an atomic batch. Initial QUndoStack push invokes this method.
     */
    void redo() override;
    /**
     * @brief Restore every construction-time geometry, including original vertex density and Z.
     * @note Continues after failures and invalidates Scene/Overlay when layer lives. Does not clear
     *   cached after results or unsampled count; partial restore possible.
     */
    void undo() override;

    /*! \brief Vertices left NaN by the last redo — the count the Features dock
     *         reports so an unsampled breakline is visible, not silent.
     * @return Total vertices reported unsampled during first redo sampling, zero initially; cached
     *   unchanged across undo/later redo. This is not a count of failed feature writes or a fresh
     *   source-availability check.
     */
    [[nodiscard]] int unsampledCount() const { return m_unsampled; }

private:
    QVector<openswmmvis::feature::FeatureId> m_ids;
    QVector<openswmmvis::feature::FeatureGeometry> m_before;
    QVector<openswmmvis::feature::FeatureGeometry> m_after;
    bool m_captured = false; ///< m_after is filled by the first redo only.
    int m_unsampled = 0;
};

} // namespace openswmmvis::map

#endif // FEATURECOMMANDS_H
