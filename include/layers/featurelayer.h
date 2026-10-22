/**
 * @file      featurelayer.h
 * @brief     An EDITABLE vector layer: user-drawn points, lines and polygons (with holes and
 *            multiple parts) carrying a user-defined attribute schema, stored in the project
 *            GeoPackage (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3,
 *            D1/D2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * An EDITABLE vector layer: user-drawn points, lines and polygons (with holes
 * and multiple parts) carrying a user-defined attribute schema, stored in the
 * project GeoPackage
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3, D1/D2).
 *
 * Why it subclasses GISVectorLayer
 * --------------------------------
 * Everything about DISPLAYING an OGR table is already solved by the base:
 * populateScene renders polygons with interior rings (gisvectorlayer.cpp:885),
 * tags every scene item with its FID (:779-788), reprojects to the canvas CRS,
 * and supplies identify, selection sync, symbology, labels, rules and the
 * style-dialog subject. FeatureLayer adds only what is genuinely new — a write
 * path, an authored schema, a Z policy and a role — and inherits the rest.
 *
 * One connection, not two
 * -----------------------
 * The base is opened GDAL_OF_UPDATE (via GISVectorLayer::setOpenFlags) and the
 * layer's FeatureStore is ATTACHED to that same GDALDataset / OGRLayer rather
 * than opening its own. Two SQLite handles on one GeoPackage would be legal but
 * would need cache-coherency care after every write; one handle needs none.
 *
 * Threading
 * ---------
 * GUI thread only, like every other layer. Mesh generation and attribute
 * assignment continue to re-open the .gpkg READ-ONLY by path on their workers
 * (meshgenerationdialog.h:89-92, meshattributeassigndialog.h:44-47); that is
 * safe because every write here is flushed immediately and no long-lived SQL
 * transaction is held open.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATURELAYER_H
#define FEATURELAYER_H

#include "layers/gisvectorlayer.h"
#include "feature/featuregeometry.h"
#include "feature/featureroles.h"
#include "feature/featurestore.h"
#include "feature/featuretypes.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <memory>

class GISRasterLayer;
class SWMM2DMeshLayer;
class MapCanvas;

/*!
 * \struct ZPolicy
 * \brief How a layer's vertex Z values are obtained (PLAN §4.1).
 *
 * \c source == None means the layer is 2D: its GeoPackage table is created
 * without the 25D flag and \ref FeatureLayer::isThreeD is false. Because that
 * decides the OGR geometry type it is fixed at creation; the SOURCE may be
 * changed afterwards and followed by a Resample Z.
 */
struct ZPolicy {
    enum class Source { None = 0, Constant, Raster, Mesh };

    Source source = Source::None;
    QString sourceLayerId; ///< OpenSWMMVisLayer::layerId() of the raster or mesh.
    int rasterBand = 1;
    double constant = 0.0;
    /*! Insert vertices every \c densifySpacing map units before sampling, so a
     *  breakline drawn with three clicks follows the terrain instead of
     *  chording it. <= 0 disables. Must stay >= the mesh minimum cell size or
     *  the constraint segments come out sub-cell — the dock warns. */
    double densifySpacing = 0.0;
    /*! Vertical unit conversion applied to every sample (raster feet → model
     *  metres, etc.). Seeded from GISRasterLayer::detectVerticalUnit. */
    double zScale = 1.0;
    /*! Re-sample a vertex's Z when it is moved or inserted. */
    bool resampleOnEdit = true;

    /**
     * @brief Infer requested geometry dimensionality from the Z source policy.
     * @return True for any source enum other than None, including an unsupported enum; not a query of an existing table's actual geometry type.
     */
    [[nodiscard]] bool isThreeD() const { return source != Source::None; }
    /**
     * @brief Serialize Z sampling policy without accessing a source.
     * @return Owned raw values with lowercase source token and optional sourceLayerId; unsupported source enums omit the source key. Numeric ranges are not validated.
     */
    [[nodiscard]] QJsonObject toJson() const;
    /**
     * @brief Decode permissive Z policy values over their construction defaults.
     * @param[in] o Object with exact lowercase source token; unknown/missing source selects None.
     * @return Independent policy, using Qt conversion defaults for missing/ill-typed values; does not validate band, source identity, scale or spacing.
     */
    [[nodiscard]] static ZPolicy fromJson(const QJsonObject &o);
    /**
     * @brief Compare complete Z policy state using fuzzy numeric equality.
     * @param[in] o Other policy, inspected without mutation.
     * @return Exact source/id/band/flag equality plus qFuzzyCompare(value+1) for constant, spacing and scale. Nonfinite values follow Qt comparison behavior.
     */
    [[nodiscard]] bool operator==(const ZPolicy &o) const;
    /**
     * @brief Test whether Z sampling policy fields differ.
     * @param[in] o Other policy.
     * @return Negation of operator==.
     */
    [[nodiscard]] bool operator!=(const ZPolicy &o) const { return !(*this == o); }
};

/*!
 * \class FeatureLayer
 * \brief An editable, GeoPackage-backed vector layer.
 */
class FeatureLayer : public GISVectorLayer
{
    Q_OBJECT

public:
    using FeatureId = openswmmvis::feature::FeatureId;
    using Feature = openswmmvis::feature::Feature;
    using FeatureGeometry = openswmmvis::feature::FeatureGeometry;
    using GeometryType = openswmmvis::feature::GeometryType;
    using Schema = openswmmvis::feature::Schema;
    using FieldDef = openswmmvis::feature::FieldDef;

    /*! \brief Constructs an unopened layer. Use \ref create or \ref openTable.
     * @param[in] parent Optional workspace QObject owner. Creates an empty owned FeatureStore and configures the vector base for GDAL update access before opening.
     * @note GUI-thread-only; no GeoPackage handle is opened and the edit-session flag starts false.
     */
    explicit FeatureLayer(OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Detach the store before the vector base closes its owned GDAL dataset.
     * @note Does not drop the persistent table; logical layer destruction and data deletion are separate operations.
     */
    ~FeatureLayer() override;

    /*!
     * \brief Create a new table in \p gpkgPath and return an open layer on it.
     * \param desiredName  Display name; the table name is derived from it via
     *                     FeatureStore::uniqueTableName, so an existing name is
     *                     disambiguated rather than rejected.
     * \param srsWkt       CRS for the geometry column — pass the CANVAS CRS.
     * \returns nullptr on failure, with \p error set.
     * @param[in] gpkgPath Writable GeoPackage container path, created if supported/needed by FeatureStore.
     * @param[in] type Requested table geometry family.
     * @param[in] zPolicy Source policy whose isThreeD() fixes the created table's Z dimension; stored without sampling features.
     * @param[in] schema Requested fields/defaults/metadata.
     * @param[in] role Feature workflow role stored without installing template fields or validating geometry compatibility.
     * @param[out] error Optional store/open diagnostic; not cleared on success.
     * @param[in] parent Optional workspace owner for the new QObject layer.
     * @return Newly allocated layer, parent-owned when parent is supplied and otherwise caller-owned, or null. Failure after table creation can leave the new table/container behind; creation/open are not one rollback transaction.
     * @note Table names are sanitized/disambiguated by the store. New display name is desiredName unless empty. File writes may precede any returned layer; no edit session is enabled automatically.
     */
    [[nodiscard]] static FeatureLayer *create(const QString &gpkgPath, const QString &desiredName,
                                              GeometryType type, const ZPolicy &zPolicy,
                                              const Schema &schema, const QString &srsWkt,
                                              FeatureLayerRole role, QString *error = nullptr,
                                              OpenSWMMVisWorkspace *parent = nullptr);

    /*! \brief Open an existing table for editing.
     * @param[in] gpkgPath Existing editable container path.
     * @param[in] table Table selector passed to the vector base's synchronous open.
     * @param[out] error Optional failure diagnostic, unchanged on success.
     * @return True after a base OGR layer exists and the store attaches to its same dataset/layer pointers, false otherwise.
     * @note Intended for an unopened layer: the base closes any old dataset before adopting a result, but this wrapper only reattaches the store after success. Failed reopening does not restore old state. Does not restore role/Z policy from project JSON or activate editing.
     */
    [[nodiscard]] bool openTable(const QString &gpkgPath, const QString &table,
                                 QString *error = nullptr);

    /**
     * @return Original container path token from the last successful openTable(); no canonicalization.
     */
    [[nodiscard]] QString gpkgPath() const { return m_gpkgPath; }
    /**
     * @return Requested table token from the last successful openTable(), which is separate from display name.
     */
    [[nodiscard]] QString tableName() const { return m_table; }
    /*! \brief True when the store is open for writing — a CAPABILITY, not a
     *         mode. A layer can be writable and still not be in an edit
     *         session; see \ref isEditing.
     * @return Whether the owned store reports attached/open; this is independent of the UI edit-session gate and not a fresh driver-permission probe.
     */
    [[nodiscard]] bool isEditable() const;

    // ----- Edit session ---------------------------------------------------

    /*!
     * \brief Whether this layer is currently being edited.
     *
     * \details An explicit edit session, the way a GIS package gates editing:
     *          the digitising tools and the attribute grid are inert until it
     *          is on. It exists so a stray click on a map that happens to
     *          carry a feature layer cannot silently move a breakline — the
     *          user has to say "I am editing this" first.
     *
     *          It is deliberately NOT enforced inside the store or the undo
     *          commands: a command that has already been pushed must be able
     *          to undo/redo regardless of the session, or closing a session
     *          would strand the undo stack.
     * @return Current UI/tool edit-session flag, initially false and not persisted; store/undo mutations do not consult it.
     */
    [[nodiscard]] bool isEditing() const { return m_editing; }
    /*! No-op when \p on matches the current state or the layer is not
     *  writable at all; otherwise emits \ref editingChanged.
     * @param[in] on Requested UI edit state. True is coerced to false if isEditable() is false.
     * @note Equal effective state is a no-op; changes are logged and emit editingChanged() only, without flushing, repainting or changing store writability.
     */
    void setEditing(bool on);

    // ----- Schema ---------------------------------------------------------

    /*! \brief The schema as stored in the GeoPackage (names, types,
     *         defaults, descriptions, Fixed choices).
     * @return Owned stored schema snapshot, empty when no store exists; no role-derived editor hints are injected.
     */
    [[nodiscard]] Schema schema() const;
    /*! \brief \ref schema with the role registry's editor hints — dropdown
     *         sources, units — filled in for the role's own field names
     *         (openswmmvis::feature::editorSchema). What editors read.
     * @return Owned schema with current role registry editor hints added to matching role field names. Does not add missing table columns or change stored metadata.
     */
    [[nodiscard]] Schema editorSchema() const;
    /**
     * @brief Adding a typed column/default/metadata; driver failure can leave a created coded domain.
     * @param[in] f Requested definition; store sanitizes/checks names and may create a coded choice domain before the field.
     * @param[out] error Optional store/not-open diagnostic; not cleared by the successful wrapper path.
     * @return False for a closed store or store mutation refusal, true after success. UI isEditing() is not required.
     * @note Success emits schemaChanged(), attempts store flush, marks scene dirty, emits featuresChanged(empty) then repaintRequested(). Flush success is not propagated and the layer does not create an undo command.
     */
    [[nodiscard]] bool addField(const FieldDef &f, QString *error = nullptr);
    /**
     * @brief Deleting a stored column and its values; driver capability/missing-field errors are returned.
     * @param[in] name Existing field lookup key.
     * @param[out] error Optional store/not-open diagnostic; not cleared by the successful wrapper path.
     * @return False for a closed store or store mutation refusal, true after success. UI isEditing() is not required.
     * @note Success emits schemaChanged(), attempts store flush, marks scene dirty, emits featuresChanged(empty) then repaintRequested(). Flush success is not propagated and the layer does not create an undo command.
     */
    [[nodiscard]] bool removeField(const QString &name, QString *error = nullptr);
    /*! \brief Rename column \p from to \p to, keeping its values.
     * @brief Renaming while retaining column values/type/metadata, subject to driver capability/collision checks.
     * @param[in] from Existing lookup name.
     * @param[in] to Requested new sanitized name.
     * @param[out] error Optional store/not-open diagnostic; not cleared by the successful wrapper path.
     * @return False for a closed store or store mutation refusal, true after success. UI isEditing() is not required.
     * @note Success emits schemaChanged(), attempts store flush, marks scene dirty, emits featuresChanged(empty) then repaintRequested(). Flush success is not propagated and the layer does not create an undo command.
     */
    [[nodiscard]] bool renameField(const QString &from, const QString &to,
                                   QString *error = nullptr);
    /*! \brief Replace the stored default, description and Fixed choices of
     *         column \p name with those of \p meta (name and type unchanged).
     * @brief Altering field metadata without a full rollback transaction; domain changes may precede a later field alteration failure.
     * @param[in] name Existing field lookup key.
     * @param[in] meta Replacement default/description/Fixed choices; existing field name/type remain authoritative.
     * @param[out] error Optional store/not-open diagnostic; not cleared by the successful wrapper path.
     * @return False for a closed store or store mutation refusal, true after success. UI isEditing() is not required.
     * @note Success emits schemaChanged(), attempts store flush, marks scene dirty, emits featuresChanged(empty) then repaintRequested(). Flush success is not propagated and the layer does not create an undo command.
     */
    [[nodiscard]] bool setFieldMetadata(const QString &name, const FieldDef &meta,
                                        QString *error = nullptr);

    // ----- Features -------------------------------------------------------

    /**
     * @brief Inspect the attached editable table.
     * @return Store geometry family, or None without a store.
     */
    [[nodiscard]] GeometryType geometryType() const;
    /**
     * @brief Inspect the attached editable table.
     * @return Actual table/store Z capability, independent of zPolicy().source.
     */
    [[nodiscard]] bool isThreeD() const;
    /**
     * @brief Inspect the attached editable table.
     * @return Store feature count, or zero without a store.
     */
    [[nodiscard]] int count() const;
    /**
     * @brief Inspect the attached editable table.
     * @return Owned stable driver feature IDs in store iteration order, empty without a store. IDs are not row ordinals.
     */
    [[nodiscard]] QVector<FeatureId> featureIds() const;
    /**
     * @brief Read one stable feature ID into independent value storage.
     * @param[in] id Driver ID, not a row index.
     * @param[out] out Replaced with copied geometry/attributes only after a record is found.
     * @return False for a missing/unavailable record, preserving out; true on success. No source mutation or scene notification occurs.
     */
    [[nodiscard]] bool feature(FeatureId id, Feature &out) const;
    /**
     * @brief Inspect the attached editable table.
     * @return Owned geometry/attribute records in driver iteration order, empty without a store; consumes the attached OGR sequential cursor.
     */
    [[nodiscard]] QVector<Feature> allFeatures() const;

    /*! \brief Insert \p f, reusing its id when free (stable ids across
     *         undo/redo — see FeatureStore::addFeature).
     * @param[in] f Geometry and desired ID copied; attributes are conformed to the complete live schema, dropping unknown keys and filling omitted keys from coerced defaults. Desired ID is reused only when free.
     * @param[out] error Optional not-open/allocation/driver diagnostic, not cleared on success.
     * @return Stored feature ID, or kInvalidFeatureId on refusal. No geometry validation or automatic Z resampling is added by this wrapper.
     * @note Requires isEditable(), not isEditing(). Success attempts flush/marks scene dirty, emits featuresChanged(id) then repaintRequested(); no undo command or verified durable-flush guarantee is supplied.
     */
    [[nodiscard]] FeatureId addFeature(const Feature &f, QString *error = nullptr);
    /**
     * @brief Mutate one stored feature and invalidate its displayed scene state.
     * @param[in] id Existing stable feature ID; removeFeature permanently deletes its record.
     * @param[in] g Geometry copied to the record, preserving attributes; empty/None can clear geometry. No automatic sampleZ()/structural validation occurs.
     * @param[out] error Optional not-open/missing/driver diagnostic, not cleared on success.
     * @return True after store success, false otherwise. UI isEditing() is not enforced; callers/tools provide session policy and undo recording.
     * @note Success attempts flush, marks scene dirty, emits featuresChanged(id) then repaintRequested(). Flush result is not checked; no multi-operation rollback is supplied.
     */
    [[nodiscard]] bool setGeometry(FeatureId id, const FeatureGeometry &g,
                                   QString *error = nullptr);
    /**
     * @brief Mutate one stored feature and invalidate its displayed scene state.
     * @param[in] id Existing stable feature ID; removeFeature permanently deletes its record.
     * @param[in] attrs Replaced through a complete schema-conformed map. Unknown keys are dropped and omitted fields reset to coerced defaults rather than retaining their old values. Geometry stays unchanged.
     * @param[out] error Optional not-open/missing/driver diagnostic, not cleared on success.
     * @return True after store success, false otherwise. UI isEditing() is not enforced; callers/tools provide session policy and undo recording.
     * @note Success attempts flush, marks scene dirty, emits featuresChanged(id) then repaintRequested(). Flush result is not checked; no multi-operation rollback is supplied.
     */
    [[nodiscard]] bool setAttributes(FeatureId id, const QVariantMap &attrs,
                                     QString *error = nullptr);
    /**
     * @brief Mutate one stored feature and invalidate its displayed scene state.
     * @param[in] id Existing stable feature ID; removeFeature permanently deletes its record.
     * @param[in] f Replacement geometry and schema-conformed attributes; f.id is ignored in favor of id. Omitted attributes reset to defaults; no topology validation/resampling occurs.
     * @param[out] error Optional not-open/missing/driver diagnostic, not cleared on success.
     * @return True after store success, false otherwise. UI isEditing() is not enforced; callers/tools provide session policy and undo recording.
     * @note Success attempts flush, marks scene dirty, emits featuresChanged(id) then repaintRequested(). Flush result is not checked; no multi-operation rollback is supplied.
     */
    [[nodiscard]] bool setFeature(FeatureId id, const Feature &f, QString *error = nullptr);
    /**
     * @brief Mutate one stored feature and invalidate its displayed scene state.
     * @param[in] id Existing stable feature ID; removeFeature permanently deletes its record.
     * @param[out] error Optional not-open/missing/driver diagnostic, not cleared on success.
     * @return True after store success, false otherwise. UI isEditing() is not enforced; callers/tools provide session policy and undo recording.
     * @note Success attempts flush, marks scene dirty, emits featuresChanged(id) then repaintRequested(). Flush result is not checked; no multi-operation rollback is supplied.
     */
    [[nodiscard]] bool removeFeature(FeatureId id, QString *error = nullptr);

    // ----- Role and Z -----------------------------------------------------

    /**
     * @return Current workflow role, initially General. Does not infer a role from geometry/schema.
     */
    [[nodiscard]] FeatureLayerRole role() const { return m_role; }
    /**
     * @param[in] r Role enum copied without compatibility/range validation.
     * @note Equality is a no-op; change emits roleChanged(int) only. Does not migrate schema, change symbols or repaint directly.
     */
    void setRole(FeatureLayerRole r);

    /**
     * @return Independent authoring policy snapshot; actual table Z dimension is reported by isThreeD().
     */
    [[nodiscard]] ZPolicy zPolicy() const { return m_zPolicy; }
    /**
     * @param[in] p Requested source/scale/densification policy copied without range/source validation.
     * @note Equality before normalization is a no-op. For a 3D table, requesting None logs a warning and retains the old source while accepting other fields; still emits zPolicyChanged(). A 2D table can store a non-None source without migrating its geometry dimension. Does not resample/write features or repaint directly.
     */
    void setZPolicy(const ZPolicy &p);

    /*!
     * \brief Sample Z for \p g according to \ref zPolicy, in place.
     * \param canvas   Used to resolve \c zPolicy.sourceLayerId to the raster or
     *                 mesh layer, and to supply the canvas CRS to the raster
     *                 sampler. May be null, in which case only the Constant
     *                 source does anything.
     * \param densify  Apply \c zPolicy.densifySpacing first. Pass false when
     *                 re-sampling after a single vertex move.
     * \returns The number of vertices left NaN (outside coverage). A NaN sample
     *          is never replaced with zero — the Features dock reports the
     *          count so an unsampled breakline is visible rather than silent.
     * @param[in,out] g Geometry in canvas map coordinates, with existing Z replaced at every exterior/hole vertex. None policy only removes its Z dimension and returns zero.
     * @note Other policies mark geometry 3D, optionally densify in map units, then sample constant/raster/mesh and multiply by zScale. Raster resolves selected band and canvas CRS through valueAt(); mesh input is map-to-scene Y-flipped without a CRS transform here. Missing source/coverage yields NaN. Only NaN is counted, not infinity; caller should supply finite scale/spacing/constants.
     * @note Edits the passed value only, without store writes, signals or consulting resampleOnEdit/isEditing(); callers enforce those policies.
     */
    int sampleZ(FeatureGeometry &g, const MapCanvas *canvas, bool densify = true) const;

    /*! \brief \ref sampleZ every feature, writing the results back. Returns the
     *         number of features changed. Callers wrap this in a
     *         ResampleZCommand so one Ctrl+Z reverts the lot.
     * @param[in] canvas Optional borrowed canvas/source resolver used by sampleZ().
     * @param[out] error Optional not-open or first write-failure diagnostic, unchanged on full success/no change.
     * @return Number of changed feature geometries successfully written before any failure; zero can mean none changed, closed store or initial failure.
     * @note Copies all records, samples with densification and writes sequentially without rollback. Earlier successes remain if a later write fails; if any succeeded, afterWrite(empty) flushes/announces the batch. Caller supplies an undo command when needed; isEditing() is not enforced.
     */
    int resampleAllZ(const MapCanvas *canvas, QString *error = nullptr);

    /*! \brief Total NaN-Z vertices across every feature.
     * @return Total geometry NaN/missing-Z count reported by each stored feature, using an allFeatures() snapshot. No sampling/repair occurs.
     */
    [[nodiscard]] int unsampledZCount() const;

    // ----- Layer overrides ------------------------------------------------

    /**
     * @return Translated container basename/table editability description or Unsaved feature layer when path empty; this is display text rather than a capability probe.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned role/geometry/dimension/count/schema/path/table display rows and, for 3D, unsampled-Z/densify metadata. Reads all features when counting missing Z; GUI-thread synchronous operation.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- Persistence ----------------------------------------------------

    /*! \brief The .oswp payload, minus the keys ProjectSerializer owns
     *         (path / name / visible / opacity).
     * @return Owned table-name/role/Z-policy/symbol object without geometry/schema data, common layer properties or edit-session state. Does not flush or write a project file.
     */
    [[nodiscard]] QJsonObject toJson() const;
    /**
     * @param[in] o Permissively decoded project metadata; unknown/missing role falls back through role helpers and missing Z object selects default None.
     * @note Applies role through its setter, replaces Z policy directly (bypassing the 3D guard) and always emits zPolicyChanged(). Optional symbol object is decoded over current symbol and applied. Does not open/change the stored table from layerName, restore edit mode or validate policy against the table.
     */
    void applyJson(const QJsonObject &o);

signals:
    /*! \brief Emitted after any successful write. \p ids lists the features
     *         affected; empty means "everything changed" (e.g. a resample).
     * @param[in] ids Affected stable IDs, or empty for whole-table/schema/batch invalidation. Borrowed vector; copy if retaining.
     * @note Emitted after an attempted flush and scene-dirty mark, before repaintRequested(); not a guarantee of crash-durable disk flush.
     */
    void featuresChanged(const QVector<qint64> &ids);
    /**
     * @brief Notify successful schema/default/description/choice edits before whole-table feature invalidation.
     */
    void schemaChanged();
    /**
     * @param[in] role New raw FeatureLayerRole integer after a changed setRole(); table/schema is not migrated.
     */
    void roleChanged(int role);
    /**
     * @brief Notify a new/restored Z policy, without implying that existing feature Z values were resampled.
     */
    void zPolicyChanged();
    /*! \brief The edit session opened (true) or closed (false).
     * @param[in] editing Effective UI edit-session flag. Store write capability and undo replay remain independent of it.
     */
    void editingChanged(bool editing);

private:
    /*! Re-attach the store to the base's dataset after an open, and refresh
     *  the cached type / Z flags.
     * @brief Attach the owned store to the vector base's existing dataset/layer handles without opening a second connection.
     * @note Base owns the handles; store must detach before they close. Uses last-successful path/table metadata.
     */
    void bindStore();
    /*! Flush, mark the scene dirty and emit. Called at the end of every
     *  successful mutator.
     * @param[in] ids Stable affected IDs, or empty for broad invalidation.
     * @note Attempts unchecked store flush, marks the vector scene cache dirty, emits featuresChanged(ids) then repaintRequested(). Does not rebuild scene items immediately.
     */
    void afterWrite(const QVector<qint64> &ids);

    /*! Resolve \c m_zPolicy.sourceLayerId against \p canvas.
     * @param[in] canvas Optional borrowed resolver; lookup uses its top-level layers and exact sourceLayerId.
     * @return Borrowed first ID-matching layer cast to GISRasterLayer, or null for missing canvas/ID/wrong type. No ownership/reprojection is supplied.
     */
    [[nodiscard]] GISRasterLayer *rasterSource(const MapCanvas *canvas) const;
    // Elaborated type specifier: the inherited enumerator
    // OpenSWMMVisLayer::SWMM2DMeshLayer hides the class name in this scope.
    /**
     * @param[in] canvas Optional borrowed resolver; lookup uses its top-level layers and exact sourceLayerId.
     * @return Borrowed first ID-matching layer cast to SWMM2DMeshLayer, or null for missing canvas/ID/wrong type. No ownership/reprojection is supplied.
     */
    [[nodiscard]] class SWMM2DMeshLayer *meshSource(const MapCanvas *canvas) const;

    std::unique_ptr<openswmmvis::feature::FeatureStore> m_store;
    QString m_gpkgPath;
    QString m_table;
    FeatureLayerRole m_role = FeatureLayerRole::General;
    ZPolicy m_zPolicy;
    /*! Edit-session flag; see \ref isEditing. Not persisted — a reopened
     *  project starts with every layer closed for editing. */
    bool m_editing = false;
};

Q_DECLARE_METATYPE(FeatureLayer *)

#endif // FEATURELAYER_H
