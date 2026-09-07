/*!
 * \file   featurelayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */

#ifndef FEATURELAYER_H
#define FEATURELAYER_H

#include "layers/gisvectorlayer.h"
#include "feature/featuregeometry.h"
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
 * \enum FeatureLayerRole
 * \brief What a feature layer is FOR.
 *
 * A role is a schema template plus a label — never a behaviour switch. The
 * layer stays a plain OGR table whatever its role, so every existing consumer
 * that accepts "a vector layer" keeps working unchanged (PLAN §6). The role
 * only drives: the fields pre-filled by the New Feature Layer dialog, the
 * glyph shown beside the layer in the mesh dialog's combos, and which
 * "Use as …" shortcuts are offered.
 */
enum class FeatureLayerRole
{
    General = 0,      ///< No template.
    DomainBoundary,   ///< Mesh domain polygon(s); interior rings become holes.
    Breakline,        ///< 3D constraint lines / hard points.
    Region,           ///< Refinement, quad and min-cell-size regions.
    ParameterZone,    ///< Roughness, land use, infiltration.
    SwmmDelineation,  ///< Source for Import Feature Layer → SWMM objects.
    BoundaryCondition ///< Lines mapped onto 2D mesh boundary edges.
};

[[nodiscard]] QString featureLayerRoleLabel(FeatureLayerRole r);
[[nodiscard]] QString featureLayerRoleToken(FeatureLayerRole r);
[[nodiscard]] FeatureLayerRole featureLayerRoleFromToken(const QString &token);
/*! \brief The fields a newly created layer of role \p r starts with. */
[[nodiscard]] openswmmvis::feature::Schema featureLayerRoleTemplate(FeatureLayerRole r);
/*! \brief The geometry type a role implies, for the New-layer dialog's default.
 *         GeometryType::None when the role does not imply one. */
[[nodiscard]] openswmmvis::feature::GeometryType
    featureLayerRoleGeometry(FeatureLayerRole r);

/*!
 * \struct ZPolicy
 * \brief How a layer's vertex Z values are obtained (PLAN §4.1).
 *
 * \c source == None means the layer is 2D: its GeoPackage table is created
 * without the 25D flag and \ref FeatureLayer::isThreeD is false. Because that
 * decides the OGR geometry type it is fixed at creation; the SOURCE may be
 * changed afterwards and followed by a Resample Z.
 */
struct ZPolicy
{
    enum class Source { None = 0, Constant, Raster, Mesh };

    Source  source     = Source::None;
    QString sourceLayerId;      ///< OpenSWMMVisLayer::layerId() of the raster or mesh.
    int     rasterBand = 1;
    double  constant   = 0.0;
    /*! Insert vertices every \c densifySpacing map units before sampling, so a
     *  breakline drawn with three clicks follows the terrain instead of
     *  chording it. <= 0 disables. Must stay >= the mesh minimum cell size or
     *  the constraint segments come out sub-cell — the dock warns. */
    double  densifySpacing = 0.0;
    /*! Vertical unit conversion applied to every sample (raster feet → model
     *  metres, etc.). Seeded from GISRasterLayer::detectVerticalUnit. */
    double  zScale     = 1.0;
    /*! Re-sample a vertex's Z when it is moved or inserted. */
    bool    resampleOnEdit = true;

    [[nodiscard]] bool isThreeD() const { return source != Source::None; }
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static ZPolicy fromJson(const QJsonObject &o);
    [[nodiscard]] bool operator==(const ZPolicy &o) const;
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
    using FeatureId       = openswmmvis::feature::FeatureId;
    using Feature         = openswmmvis::feature::Feature;
    using FeatureGeometry = openswmmvis::feature::FeatureGeometry;
    using GeometryType    = openswmmvis::feature::GeometryType;
    using Schema          = openswmmvis::feature::Schema;
    using FieldDef        = openswmmvis::feature::FieldDef;

    /*! \brief Constructs an unopened layer. Use \ref create or \ref openTable. */
    explicit FeatureLayer(OpenSWMMVisWorkspace *parent = nullptr);
    ~FeatureLayer() override;

    /*!
     * \brief Create a new table in \p gpkgPath and return an open layer on it.
     * \param desiredName  Display name; the table name is derived from it via
     *                     FeatureStore::uniqueTableName, so an existing name is
     *                     disambiguated rather than rejected.
     * \param srsWkt       CRS for the geometry column — pass the CANVAS CRS.
     * \returns nullptr on failure, with \p error set.
     */
    [[nodiscard]] static FeatureLayer *create(const QString &gpkgPath,
                                              const QString &desiredName,
                                              GeometryType   type,
                                              const ZPolicy &zPolicy,
                                              const Schema  &schema,
                                              const QString &srsWkt,
                                              FeatureLayerRole role,
                                              QString       *error = nullptr,
                                              OpenSWMMVisWorkspace *parent = nullptr);

    /*! \brief Open an existing table for editing. */
    [[nodiscard]] bool openTable(const QString &gpkgPath, const QString &table,
                                 QString *error = nullptr);

    [[nodiscard]] QString gpkgPath()  const { return m_gpkgPath; }
    [[nodiscard]] QString tableName() const { return m_table; }
    /*! \brief True when the store is open for writing — a CAPABILITY, not a
     *         mode. A layer can be writable and still not be in an edit
     *         session; see \ref isEditing. */
    [[nodiscard]] bool    isEditable() const;

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
     */
    [[nodiscard]] bool isEditing() const { return m_editing; }
    /*! No-op when \p on matches the current state or the layer is not
     *  writable at all; otherwise emits \ref editingChanged. */
    void setEditing(bool on);

    // ----- Schema ---------------------------------------------------------

    [[nodiscard]] Schema schema() const;
    [[nodiscard]] bool addField(const FieldDef &f, QString *error = nullptr);
    [[nodiscard]] bool removeField(const QString &name, QString *error = nullptr);

    // ----- Features -------------------------------------------------------

    [[nodiscard]] GeometryType geometryType() const;
    [[nodiscard]] bool isThreeD() const;
    [[nodiscard]] int  count() const;
    [[nodiscard]] QVector<FeatureId> featureIds() const;
    [[nodiscard]] bool feature(FeatureId id, Feature &out) const;
    [[nodiscard]] QVector<Feature> allFeatures() const;

    /*! \brief Insert \p f, reusing its id when free (stable ids across
     *         undo/redo — see FeatureStore::addFeature). */
    [[nodiscard]] FeatureId addFeature(const Feature &f, QString *error = nullptr);
    [[nodiscard]] bool setGeometry(FeatureId id, const FeatureGeometry &g,
                                   QString *error = nullptr);
    [[nodiscard]] bool setAttributes(FeatureId id, const QVariantMap &attrs,
                                     QString *error = nullptr);
    [[nodiscard]] bool setFeature(FeatureId id, const Feature &f,
                                  QString *error = nullptr);
    [[nodiscard]] bool removeFeature(FeatureId id, QString *error = nullptr);

    // ----- Role and Z -----------------------------------------------------

    [[nodiscard]] FeatureLayerRole role() const { return m_role; }
    void setRole(FeatureLayerRole r);

    [[nodiscard]] ZPolicy zPolicy() const { return m_zPolicy; }
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
     */
    int sampleZ(FeatureGeometry &g, const MapCanvas *canvas,
                bool densify = true) const;

    /*! \brief \ref sampleZ every feature, writing the results back. Returns the
     *         number of features changed. Callers wrap this in a
     *         ResampleZCommand so one Ctrl+Z reverts the lot. */
    int resampleAllZ(const MapCanvas *canvas, QString *error = nullptr);

    /*! \brief Total NaN-Z vertices across every feature. */
    [[nodiscard]] int unsampledZCount() const;

    // ----- Layer overrides ------------------------------------------------

    [[nodiscard]] QString sourceDescription() const override;
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- Persistence ----------------------------------------------------

    /*! \brief The .oswp payload, minus the keys ProjectSerializer owns
     *         (path / name / visible / opacity). */
    [[nodiscard]] QJsonObject toJson() const;
    void applyJson(const QJsonObject &o);

signals:
    /*! \brief Emitted after any successful write. \p ids lists the features
     *         affected; empty means "everything changed" (e.g. a resample). */
    void featuresChanged(const QVector<qint64> &ids);
    void schemaChanged();
    void roleChanged(int role);
    void zPolicyChanged();
    /*! \brief The edit session opened (true) or closed (false). */
    void editingChanged(bool editing);

private:
    /*! Re-attach the store to the base's dataset after an open, and refresh
     *  the cached type / Z flags. */
    void bindStore();
    /*! Flush, mark the scene dirty and emit. Called at the end of every
     *  successful mutator. */
    void afterWrite(const QVector<qint64> &ids);

    /*! Resolve \c m_zPolicy.sourceLayerId against \p canvas. */
    [[nodiscard]] GISRasterLayer  *rasterSource(const MapCanvas *canvas) const;
    // Elaborated type specifier: the inherited enumerator
    // OpenSWMMVisLayer::SWMM2DMeshLayer hides the class name in this scope.
    [[nodiscard]] class SWMM2DMeshLayer *meshSource(const MapCanvas *canvas) const;

    std::unique_ptr<openswmmvis::feature::FeatureStore> m_store;
    QString          m_gpkgPath;
    QString          m_table;
    FeatureLayerRole m_role = FeatureLayerRole::General;
    ZPolicy          m_zPolicy;
    /*! Edit-session flag; see \ref isEditing. Not persisted — a reopened
     *  project starts with every layer closed for editing. */
    bool             m_editing = false;
};

Q_DECLARE_METATYPE(FeatureLayer *)

#endif // FEATURELAYER_H
