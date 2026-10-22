/**
 * @file      featurestore.h
 * @brief     The GeoPackage read/write backing store for editable feature layers
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1, D2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The GeoPackage read/write backing store for editable feature layers
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1, D2).
 *
 * THIS IS THE FIRST OGR WRITE PATH IN THE REPOSITORY. Before it, every vector
 * source was opened GDAL_OF_READONLY (gisvectorlayer.cpp:1088, :1170) and no
 * call site anywhere under src/ used CreateLayer, CreateFeature, SetFeature,
 * DeleteFeature or CreateField. Treat the invariants below as load-bearing.
 *
 * Threading
 * ---------
 * A FeatureStore's GDALDataset must not cross threads — the same rule the mesh
 * pipeline states at meshgenerationdialog.h:89-92 and the attribute-assign
 * dialog at meshattributeassigndialog.h:44-47, where workers RE-OPEN every
 * source by path. This store is owned by FeatureLayer on the GUI thread. Mesh
 * generation and attribute assignment continue to re-open the .gpkg read-only
 * by path on their workers; that is safe concurrently with this handle because
 * every write here is autocommitted and no long-lived SQL transaction is held
 * open (undo granularity comes from the command stack, not from SQL).
 *
 * Error reporting
 * ---------------
 * Every mutating call takes an optional \c QString* and returns false / an
 * invalid id on failure. Multi-step schema/container operations may leave
 * partial tables or domains; no transaction rollback is promised. Callers surface the
 * message; the store never shows UI and never asserts on user-reachable input.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATURESTORE_H
#define FEATURESTORE_H

#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

class GDALDataset;
class OGRLayer;
class OGRFeature;

namespace openswmmvis::feature
{

/*!
 * \class FeatureStore
 * \brief One open, writable OGR table inside a GeoPackage.
 *
 * Non-copyable: it owns a GDALDataset.
 */
class FeatureStore
{
public:
    /**
     * @brief Construct a detached GUI-thread store and register GDAL drivers.
     */
    FeatureStore();
    /**
     * @brief Close an owned dataset or detach from a borrowed dataset through close().
     */
    ~FeatureStore();

    /**
     * @brief Disable copying because dataset connection ownership cannot be duplicated; the reference argument is unavailable at runtime.
     */
    FeatureStore(const FeatureStore &) = delete;
    /**
     * @brief Disable copy assignment of dataset ownership; the reference argument is unavailable at runtime.
     */
    FeatureStore &operator=(const FeatureStore &) = delete;

    // ----- Container-level operations (static; open nothing on success) ----

    /*!
     * \brief Create an empty GeoPackage at \p gpkgPath if it does not exist.
     * \details Succeeds silently when the file is already a readable
     *          GeoPackage, so callers may treat it as "ensure".
     * @param[in] gpkgPath Nonempty destination path; missing parent directories are created. Existing files must open as writable vector datasets; this helper does not explicitly verify the driver identity.
     * @param[out] error Optional diagnostic on failure, not cleared on success.
     * @return True for an existing writable container or a newly created empty one; false on path/driver/open/create errors. Temporary handles are closed before return.
     */
    [[nodiscard]] static bool ensureGeoPackage(const QString &gpkgPath, QString *error = nullptr);

    /*! \brief Table names inside \p gpkgPath; empty when it cannot be read.
     * @param[in] gpkgPath Container to open read-only for enumeration.
     * @return Layer names in driver order; empty on unreadable container or no layers, without a diagnostic.
 */
    [[nodiscard]] static QStringList tables(const QString &gpkgPath);

    /**
     * @brief Check container layer names case-insensitively.
     * @param[in] gpkgPath Container opened briefly for enumeration.
     * @param[in] table Candidate layer name.
     * @return True for an enumerated match; unreadable containers return false.
     */
    [[nodiscard]] static bool tableExists(const QString &gpkgPath, const QString &table);

    /*!
     * \brief Derive a table name from \p desired that is not yet used in
     *        \p gpkgPath, appending _1, _2, … as needed.
     * @param[in] gpkgPath Container whose existing names are checked.
     * @param[in] desired Name sanitized before suffixing; an unusable name uses the feature-table fallback.
     * @return Candidate with numeric suffixes checked through _9999; exhaustion returns unchecked _x. An unreadable container appears to have no existing tables.
     */
    [[nodiscard]] static QString uniqueTableName(const QString &gpkgPath, const QString &desired);

    /*!
     * \brief Create a new table.
     * \param gpkgPath  Container; created via \ref ensureGeoPackage first.
     * \param table     Sanitised name (see \ref sanitizeTableName).
     * \param type      Geometry type; decides the table's OGR geometry type.
     * \param hasZ      When true the table is created 2.5D (wkbSetZ).
     * \param schema    Attribute columns; may be empty. Each column's default,
     *                  description and Fixed value list are stored too.
     * \param srsWkt    CRS WKT for the geometry column; may be empty.
     * \details Fails when the table already exists — the caller is expected to
     *          have resolved the name with \ref uniqueTableName.
     * @param[out] error Optional first failure diagnostic, not cleared on success.
     * @return True after geometry layer and all requested fields are created; false can leave a newly created container, partial table or coded domains because operations are not wrapped in a rollback transaction.
     * @details Sanitizes the requested table name. Empty/invalid CRS WKT is treated as no SRS; field metadata support depends on the GDAL build. Handles are closed before return.
     */
    [[nodiscard]] static bool createTable(const QString &gpkgPath, const QString &table,
                                          GeometryType type, bool hasZ, const Schema &schema,
                                          const QString &srsWkt, QString *error = nullptr);

    /*! \brief Permanently drop \p table from \p gpkgPath.
     * @param[in] gpkgPath Writable container path.
     * @param[in] table Exact layer name resolved by GDAL.
     * @param[out] error Optional failure diagnostic.
     * @return True when the driver deletes the table; false for missing/open/driver errors. This permanently removes its records.
 */
    [[nodiscard]] static bool dropTable(const QString &gpkgPath, const QString &table,
                                        QString *error = nullptr);

    // ----- Session --------------------------------------------------------

    /*! \brief Open \p table in \p gpkgPath for update. The store owns the
     *         dataset and closes it in \ref close.
     * @param[in] gpkgPath Writable GeoPackage path.
     * @param[in] table Existing layer name.
     * @param[out] error Optional open/layer lookup diagnostic.
     * @return True after binding an owned dataset/layer; false leaves this store detached after closing its previous session.
 */
    [[nodiscard]] bool open(const QString &gpkgPath, const QString &table,
                            QString *error = nullptr);

    /*!
     * \brief Adopt a dataset and layer someone else owns.
     *
     * \details FeatureLayer opens its GeoPackage through GISVectorLayer's own
     *          handle (so the paint loop and the write path share one
     *          connection rather than two on the same SQLite file) and then
     *          attaches this store to it. The store performs every write but
     *          never closes the dataset — \ref close only detaches.
     *
     *          \p ds must have been opened with GDAL_OF_UPDATE or every write
     *          fails with GDAL's read-only error, which is reported normally.
     * @param[in] ds Borrowed writable GDAL dataset, whose lifetime/thread must cover store operations.
     * @param[in] layer Borrowed table belonging to ds.
     * @param[in] gpkgPath Container path saved as session metadata.
     * @param[in] table Layer name saved as session metadata.
     * @details Closes/detaches any previous session first; does not validate pointer association or update capability.
     */
    void attach(GDALDataset *ds, OGRLayer *layer, const QString &gpkgPath, const QString &table);

    /**
     * @brief End the current table session and clear its stored path/name.
     * @details Closes/flushed owned dataset via GDALClose; attached datasets are merely detached and remain caller-owned. Idempotent.
     */
    void close();
    /**
     * @return True when a table pointer is bound; does not probe file accessibility or update capability.
     */
    [[nodiscard]] bool isOpen() const { return m_layer != nullptr; }

    /**
     * @return Copied session container path, empty when detached.
     */
    [[nodiscard]] QString gpkgPath() const { return m_gpkgPath; }
    /**
     * @return Copied session layer name, empty when detached.
     */
    [[nodiscard]] QString tableName() const { return m_table; }

    // ----- Introspection --------------------------------------------------

    /*! \brief The schema read back from the open table: names, types,
     *         defaults, descriptions and Fixed value lists (a coded field
     *         domain reads back as ChoiceSource::Fixed). Reflects any field
     *         added, removed or altered since open.
     * @return Current driver field definitions as an owning Schema; empty while detached. Integer64 fields map to the GUI Integer type, while individual attribute reads retain 64-bit QVariant values.
 */
    [[nodiscard]] Schema schema() const;
    /**
     * @return Layer geometry kind with Z flag removed, or None when detached/unsupported.
     */
    [[nodiscard]] GeometryType geometryType() const;
    /**
     * @return True when the bound layer's declared OGR geometry type carries Z; false when detached.
     */
    [[nodiscard]] bool hasZ() const;
    /*! \brief CRS WKT of the geometry column; empty when the table declares
     *         none (the layer then assumes canvas CRS, as GISVectorLayer
     *         already does — see its crsAssumed signal).
     * @return Exported layer CRS WKT, or empty when detached/undeclared/unexportable.
 */
    [[nodiscard]] QString srsWkt() const;
    /**
     * @return Forced OGR feature count narrowed to int; zero while detached; negative driver failures pass through after narrowing.
     */
    [[nodiscard]] int count() const;

    // ----- Reads ----------------------------------------------------------

    /**
     * @brief Enumerate current feature IDs through the layer's sequential cursor.
     * @return Owned list of IDs in driver order; empty while detached. The layer reading cursor is reset/consumed.
     */
    [[nodiscard]] QVector<FeatureId> featureIds() const;
    /**
     * @brief Fetch one record by stable driver feature ID.
     * @param[in] id Feature ID, not a row ordinal.
     * @param[out] out Destination populated only after a record is found.
     * @return True with owned geometry/attributes; false on missing/closed record preserves out.
     */
    [[nodiscard]] bool feature(FeatureId id, Feature &out) const;
    /**
     * @brief Snapshot all records into independent Qt/geometry values.
     * @return Owned records in driver iteration order; empty when detached. Resets/consumes the layer sequential cursor and holds no OGR feature pointers after return.
     */
    [[nodiscard]] QVector<Feature> allFeatures() const;

    // ----- Writes ---------------------------------------------------------

    /*!
     * \brief Insert \p f.
     * \details When \c f.id is a valid id the store attempts to reuse it, so a
     *          redo of AddFeatureCommand restores the SAME id an undo removed —
     *          the stable-id contract AddAnnotationCommand documents at
     *          mapundostack.h:964-970. If the id is taken, OGR assigns a fresh
     *          one and that is what is returned.
     * \returns The stored id, or \ref kInvalidFeatureId on failure.
     * @param[in] f Geometry and attributes to copy; requested id is reused only if unoccupied, otherwise the driver allocates one.
     * @param[out] error Optional allocation/driver diagnostic; unchanged on success.
     * @details Does not run structural geometry validation. Unknown attributes are ignored; present nulls become OGR NULL and absent keys leave fresh-feature defaults. Attempts flush after a successful create; flush status is not propagated.
     */
    [[nodiscard]] FeatureId addFeature(const Feature &f, QString *error = nullptr);

    /**
     * @brief Replace one stored geometry while preserving its attributes.
     * @param[in] id Existing stable feature ID.
     * @param[in] g Geometry copied through the OGR bridge; empty/None clears the geometry. No validate() call is added.
     * @param[out] error Optional missing/driver diagnostic.
     * @return True after SetFeature succeeds; attempts flush but does not verify its status.
     */
    [[nodiscard]] bool setGeometry(FeatureId id, const FeatureGeometry &g,
                                   QString *error = nullptr);
    /**
     * @brief Apply supplied known fields while retaining geometry and omitted columns.
     * @param[in] id Existing stable feature ID.
     * @param[in] attrs Exact stored field-name keys; unknown keys are ignored, present invalid/null QVariant becomes OGR NULL.
     * @param[out] error Optional missing/driver diagnostic.
     * @return True after SetFeature succeeds; coercion follows the current OGR field type without a conversion-error result.
     */
    [[nodiscard]] bool setAttributes(FeatureId id, const QVariantMap &attrs,
                                     QString *error = nullptr);
    /**
     * @brief Replace geometry and supplied attributes of one existing ID.
     * @param[in] id Existing target ID; f.id is ignored.
     * @param[in] f Geometry and present known attribute keys to apply; absent keys are retained.
     * @param[out] error Optional missing/driver diagnostic.
     * @return True after SetFeature succeeds and a flush is attempted; no topology validation or flush-success guarantee is supplied.
     */
    [[nodiscard]] bool setFeature(FeatureId id, const Feature &f, QString *error = nullptr);
    /**
     * @brief Permanently remove one driver record.
     * @param[in] id Stable feature ID passed to DeleteFeature.
     * @param[out] error Optional closed-store/driver failure diagnostic.
     * @return True on driver success, followed by an unchecked flush attempt; false otherwise.
     */
    [[nodiscard]] bool removeFeature(FeatureId id, QString *error = nullptr);

    // ----- Schema evolution ----------------------------------------------

    /**
     * @brief Add one typed column and supported persisted metadata.
     * @param[in] f Definition whose name is sanitized and checked case-insensitively for collisions. Fixed choice domains may be created first.
     * @param[out] error Optional invalid/duplicate/domain/driver diagnostic.
     * @return True after CreateField succeeds; false may leave a newly created coded domain if later column creation fails.
     */
    [[nodiscard]] bool addField(const FieldDef &f, QString *error = nullptr);
    /*!
     * \brief Drop the column \p name.
     * \details GeoPackage column deletion depends on the GDAL build; when the
     *          driver reports no OLCDeleteField capability this fails with an
     *          explanatory message rather than silently leaving the column.
     * @param[in] name Existing column resolved by the OGR field-name lookup.
     * @param[out] error Optional capability/missing/driver diagnostic.
     * @return True after DeleteField succeeds and orphan-domain cleanup/flush are attempted; false on refusal.
     */
    [[nodiscard]] bool removeField(const QString &name, QString *error = nullptr);

    /*!
     * \brief Rename column \p from to \p to (sanitised), keeping its values,
     *        default, description and value list.
     * \details Fails when \p to is already a column or the driver cannot
     *          alter field definitions.
     * @param[in] from Existing column name.
     * @param[in] to Requested name sanitized by sanitizeFieldName(); may retain the current column name.
     * @param[out] error Optional invalid/collision/capability/driver diagnostic.
     * @return True on driver AlterFieldDefn name success, with all other column metadata retained.
     */
    [[nodiscard]] bool renameField(const QString &from, const QString &to,
                                   QString *error = nullptr);

    /*!
     * \brief Replace the stored default, description and Fixed value list of
     *        column \p name with those of \p meta. Name and type are not
     *        touched. A value list is stored as a coded field domain named
     *        "<table>__<field>" (re-using the column's existing domain when it
     *        has one); an empty list detaches it.
     *        (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN §6 "Store".)
     * @param[in] name Existing column name resolved by OGR.
     * @param[in] meta New default, description and Fixed choices; actual column name/type override meta's corresponding fields.
     * @param[out] error Optional missing/capability/domain/driver diagnostic.
     * @return True after AlterFieldDefn succeeds; false may leave a domain created/updated before the field alteration. No rollback transaction is supplied.
     */
    [[nodiscard]] bool setFieldMetadata(const QString &name, const FieldDef &meta,
                                        QString *error = nullptr);

    /*! \brief True when this GDAL build stores column descriptions and value
     *         lists (GDAL >= 3.8). Defaults are stored by every build.
     * @return Compile-time GDAL metadata support flag; does not probe a particular table or storage driver.
 */
    [[nodiscard]] static bool storesFieldMetadata();

    /*! \brief Flush pending writes to disk. Called after every command so a
     *         pending driver writes are requested before a concurrent read-only opener
     *         (a mesh worker) sees current data.
     * @return False when no dataset is bound; otherwise calls SyncToDisk/FlushCache and returns true without checking their error statuses.
     * @details Requests visibility/durability but does not guarantee a successful physical disk write.
 */
    bool flush();

private:
    /*! Fetch \p id; caller must OGRFeature::DestroyFeature the result.
     * @param[in] id Stable driver feature ID.
     * @return Newly allocated OGR feature owned by the caller (DestroyFeature required), or nullptr for detached/missing data.
 */
    [[nodiscard]] OGRFeature *fetch(FeatureId id) const;
    /*! Copy \p attrs onto \p feat according to the current schema.
     * @param[in,out] feat Borrowed OGR record to mutate, possibly nullptr (no-op).
     * @param[in] attrs Exact field-name keys; unknown/missing fields are skipped and null entries become explicit field NULL.
     * @details Converts by OGR storage type without reporting conversion failures; does not persist the record itself.
 */
    void applyAttributes(OGRFeature *feat, const QVariantMap &attrs) const;
    /*! Read \p feat's fields into a QVariantMap.
     * @param[in] feat Borrowed OGR record, possibly nullptr.
     * @return Owned map preserving nulls as invalid QVariant, integer64 as qlonglong and booleans via subtype; empty for missing record/schema.
 */
    [[nodiscard]] QVariantMap readAttributes(const OGRFeature *feat) const;

    GDALDataset *m_dataset = nullptr; ///< Opened GDAL_OF_UPDATE.
    OGRLayer *m_layer = nullptr;      ///< Non-owning pointer into m_dataset.
    /*! False after \ref attach "attach()": the dataset belongs to the caller and must not
     *  be closed here. */
    bool m_ownsDataset = true;
    QString m_gpkgPath;
    QString m_table;
};

} // namespace openswmmvis::feature

#endif // FEATURESTORE_H
