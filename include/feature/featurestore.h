/*!
 * \file   featurestore.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * invalid id on failure, leaving the store unchanged. Callers surface the
 * message; the store never shows UI and never asserts on user-reachable input.
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

namespace openswmmvis::feature {

/*!
 * \class FeatureStore
 * \brief One open, writable OGR table inside a GeoPackage.
 *
 * Non-copyable: it owns a GDALDataset.
 */
class FeatureStore
{
public:
    FeatureStore();
    ~FeatureStore();

    FeatureStore(const FeatureStore &) = delete;
    FeatureStore &operator=(const FeatureStore &) = delete;

    // ----- Container-level operations (static; open nothing on success) ----

    /*!
     * \brief Create an empty GeoPackage at \p gpkgPath if it does not exist.
     * \details Succeeds silently when the file is already a readable
     *          GeoPackage, so callers may treat it as "ensure".
     */
    [[nodiscard]] static bool ensureGeoPackage(const QString &gpkgPath,
                                               QString *error = nullptr);

    /*! \brief Table names inside \p gpkgPath; empty when it cannot be read. */
    [[nodiscard]] static QStringList tables(const QString &gpkgPath);

    [[nodiscard]] static bool tableExists(const QString &gpkgPath,
                                          const QString &table);

    /*!
     * \brief Derive a table name from \p desired that is not yet used in
     *        \p gpkgPath, appending _1, _2, … as needed.
     */
    [[nodiscard]] static QString uniqueTableName(const QString &gpkgPath,
                                                 const QString &desired);

    /*!
     * \brief Create a new table.
     * \param gpkgPath  Container; created via \ref ensureGeoPackage first.
     * \param table     Sanitised name (see \ref sanitizeTableName).
     * \param type      Geometry type; decides the table's OGR geometry type.
     * \param hasZ      When true the table is created 2.5D (wkbSetZ).
     * \param schema    Attribute columns; may be empty.
     * \param srsWkt    CRS WKT for the geometry column; may be empty.
     * \details Fails when the table already exists — the caller is expected to
     *          have resolved the name with \ref uniqueTableName.
     */
    [[nodiscard]] static bool createTable(const QString &gpkgPath,
                                          const QString &table,
                                          GeometryType   type,
                                          bool           hasZ,
                                          const Schema  &schema,
                                          const QString &srsWkt,
                                          QString       *error = nullptr);

    /*! \brief Permanently drop \p table from \p gpkgPath. */
    [[nodiscard]] static bool dropTable(const QString &gpkgPath,
                                        const QString &table,
                                        QString       *error = nullptr);

    // ----- Session --------------------------------------------------------

    /*! \brief Open \p table in \p gpkgPath for update. The store owns the
     *         dataset and closes it in \ref close. */
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
     */
    void attach(GDALDataset *ds, OGRLayer *layer,
                const QString &gpkgPath, const QString &table);

    void close();
    [[nodiscard]] bool isOpen() const { return m_layer != nullptr; }

    [[nodiscard]] QString gpkgPath()  const { return m_gpkgPath; }
    [[nodiscard]] QString tableName() const { return m_table; }

    // ----- Introspection --------------------------------------------------

    /*! \brief The schema read back from the open table. Reflects any field
     *         added or removed since open. */
    [[nodiscard]] Schema schema() const;
    [[nodiscard]] GeometryType geometryType() const;
    [[nodiscard]] bool hasZ() const;
    /*! \brief CRS WKT of the geometry column; empty when the table declares
     *         none (the layer then assumes canvas CRS, as GISVectorLayer
     *         already does — see its crsAssumed signal). */
    [[nodiscard]] QString srsWkt() const;
    [[nodiscard]] int count() const;

    // ----- Reads ----------------------------------------------------------

    [[nodiscard]] QVector<FeatureId> featureIds() const;
    [[nodiscard]] bool feature(FeatureId id, Feature &out) const;
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
     */
    [[nodiscard]] FeatureId addFeature(const Feature &f, QString *error = nullptr);

    [[nodiscard]] bool setGeometry(FeatureId id, const FeatureGeometry &g,
                                   QString *error = nullptr);
    [[nodiscard]] bool setAttributes(FeatureId id, const QVariantMap &attrs,
                                     QString *error = nullptr);
    [[nodiscard]] bool setFeature(FeatureId id, const Feature &f,
                                  QString *error = nullptr);
    [[nodiscard]] bool removeFeature(FeatureId id, QString *error = nullptr);

    // ----- Schema evolution ----------------------------------------------

    [[nodiscard]] bool addField(const FieldDef &f, QString *error = nullptr);
    /*!
     * \brief Drop the column \p name.
     * \details GeoPackage column deletion depends on the GDAL build; when the
     *          driver reports no OLCDeleteField capability this fails with an
     *          explanatory message rather than silently leaving the column.
     */
    [[nodiscard]] bool removeField(const QString &name, QString *error = nullptr);

    /*! \brief Flush pending writes to disk. Called after every command so a
     *         crash cannot lose an edit and so a concurrent read-only opener
     *         (a mesh worker) sees current data. */
    bool flush();

private:
    /*! Fetch \p id; caller must OGRFeature::DestroyFeature the result. */
    [[nodiscard]] OGRFeature *fetch(FeatureId id) const;
    /*! Copy \p attrs onto \p feat according to the current schema. */
    void applyAttributes(OGRFeature *feat, const QVariantMap &attrs) const;
    /*! Read \p feat's fields into a QVariantMap. */
    [[nodiscard]] QVariantMap readAttributes(const OGRFeature *feat) const;

    GDALDataset *m_dataset = nullptr;   ///< Opened GDAL_OF_UPDATE.
    OGRLayer    *m_layer   = nullptr;   ///< Non-owning pointer into m_dataset.
    /*! False after \ref attach: the dataset belongs to the caller and must not
     *  be closed here. */
    bool         m_ownsDataset = true;
    QString      m_gpkgPath;
    QString      m_table;
};

}   // namespace openswmmvis::feature

#endif // FEATURESTORE_H
