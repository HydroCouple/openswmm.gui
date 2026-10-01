/*!
 * \file   featurelayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "layers/featurelayer.h"

#include "layers/gisrasterlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QLoggingCategory>

#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(lcFeatureLayer, "openswmmvis.feature.layer")

using namespace openswmmvis::feature;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QString tr_(const char *s) { return QCoreApplication::translate("FeatureLayer", s); }

/*! Scene coordinates are map coordinates with Y flipped — the same mapping
 *  GISVectorLayer::toScene applies at gisvectorlayer.cpp:803-812. The mesh
 *  sampler takes scene coordinates, the raster sampler takes map coordinates,
 *  so exactly one of the two needs this. */
inline QPointF mapToScene(const QPointF &p) { return QPointF(p.x(), -p.y()); }

}   // namespace

// The role helpers (label, token, geometry, template) live in the role
// registry, feature/featureroles.cpp.

// ---------------------------------------------------------------------------
// ZPolicy
// ---------------------------------------------------------------------------

QJsonObject ZPolicy::toJson() const
{
    QJsonObject o;
    switch (source) {
    case Source::Constant: o.insert(QStringLiteral("source"), QStringLiteral("constant")); break;
    case Source::Raster:   o.insert(QStringLiteral("source"), QStringLiteral("raster"));   break;
    case Source::Mesh:     o.insert(QStringLiteral("source"), QStringLiteral("mesh"));     break;
    case Source::None:     o.insert(QStringLiteral("source"), QStringLiteral("none"));     break;
    }
    if (!sourceLayerId.isEmpty())
        o.insert(QStringLiteral("sourceLayerId"), sourceLayerId);
    o.insert(QStringLiteral("rasterBand"),     rasterBand);
    o.insert(QStringLiteral("constant"),       constant);
    o.insert(QStringLiteral("densifySpacing"), densifySpacing);
    o.insert(QStringLiteral("zScale"),         zScale);
    o.insert(QStringLiteral("resampleOnEdit"), resampleOnEdit);
    return o;
}

ZPolicy ZPolicy::fromJson(const QJsonObject &o)
{
    ZPolicy p;
    const QString s = o.value(QStringLiteral("source")).toString();
    if      (s == QLatin1String("constant")) p.source = Source::Constant;
    else if (s == QLatin1String("raster"))   p.source = Source::Raster;
    else if (s == QLatin1String("mesh"))     p.source = Source::Mesh;
    else                                     p.source = Source::None;

    p.sourceLayerId  = o.value(QStringLiteral("sourceLayerId")).toString();
    p.rasterBand     = o.value(QStringLiteral("rasterBand")).toInt(1);
    p.constant       = o.value(QStringLiteral("constant")).toDouble(0.0);
    p.densifySpacing = o.value(QStringLiteral("densifySpacing")).toDouble(0.0);
    p.zScale         = o.value(QStringLiteral("zScale")).toDouble(1.0);
    p.resampleOnEdit = o.value(QStringLiteral("resampleOnEdit")).toBool(true);
    return p;
}

bool ZPolicy::operator==(const ZPolicy &o) const
{
    return source == o.source
        && sourceLayerId == o.sourceLayerId
        && rasterBand == o.rasterBand
        && qFuzzyCompare(constant + 1.0, o.constant + 1.0)
        && qFuzzyCompare(densifySpacing + 1.0, o.densifySpacing + 1.0)
        && qFuzzyCompare(zScale + 1.0, o.zScale + 1.0)
        && resampleOnEdit == o.resampleOnEdit;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FeatureLayer::FeatureLayer(OpenSWMMVisWorkspace *parent)
    // Empty path: the base ctor must NOT open, because the open mode has to
    // change first (gisvectorlayer.cpp:202-203 only opens a non-empty path).
    : GISVectorLayer(QString(), QString(), parent)
    , m_store(std::make_unique<FeatureStore>())
{
    setLayerType(SWMMFeatureLayer);
    setOpenFlags(GDAL_OF_VECTOR | GDAL_OF_UPDATE);
}

FeatureLayer::~FeatureLayer()
{
    // Detach before the base closes the dataset the store is attached to.
    if (m_store) m_store->close();
}

FeatureLayer *FeatureLayer::create(const QString &gpkgPath,
                                   const QString &desiredName,
                                   GeometryType   type,
                                   const ZPolicy &zPolicy,
                                   const Schema  &schema,
                                   const QString &srsWkt,
                                   FeatureLayerRole role,
                                   QString       *error,
                                   OpenSWMMVisWorkspace *parent)
{
    const QString table = FeatureStore::uniqueTableName(gpkgPath, desiredName);
    if (!FeatureStore::createTable(gpkgPath, table, type, zPolicy.isThreeD(),
                                   schema, srsWkt, error))
        return nullptr;

    auto *layer = new FeatureLayer(parent);
    layer->m_role    = role;
    layer->m_zPolicy = zPolicy;
    if (!layer->openTable(gpkgPath, table, error)) {
        delete layer;
        return nullptr;
    }
    layer->setName(desiredName.isEmpty() ? table : desiredName);
    return layer;
}

bool FeatureLayer::openTable(const QString &gpkgPath, const QString &table,
                             QString *error)
{
    // The base opens with m_openFlags (GDAL_OF_UPDATE, set in the ctor) and
    // sets extent, CRS and name from the table.
    openDataset(gpkgPath, table);

    if (!ogrLayer()) {
        if (error)
            *error = tr_("Could not open table \"%1\" in \"%2\" for editing.")
                         .arg(table, gpkgPath);
        return false;
    }

    m_gpkgPath = gpkgPath;
    m_table    = table;
    bindStore();
    return true;
}

void FeatureLayer::bindStore()
{
    if (!m_store) m_store = std::make_unique<FeatureStore>();
    m_store->attach(dataset(), ogrLayer(), m_gpkgPath, m_table);
}

bool FeatureLayer::isEditable() const
{
    return m_store && m_store->isOpen();
}

void FeatureLayer::setEditing(bool on)
{
    // A layer whose store never opened cannot be edited; refusing here means
    // the toolbar's checked state can never disagree with reality.
    if (on && !isEditable())
        on = false;
    if (on == m_editing)
        return;
    m_editing = on;
    qCInfo(lcFeatureLayer).nospace()
        << "[feature] " << (on ? "opened" : "closed") << " edit session on \""
        << m_table << '"';
    emit editingChanged(m_editing);
}

// ---------------------------------------------------------------------------
// Write plumbing
// ---------------------------------------------------------------------------

void FeatureLayer::afterWrite(const QVector<qint64> &ids)
{
    if (m_store) m_store->flush();
    // GISVectorLayer::refreshScene short-circuits on a clean dirty flag, so
    // the scene must be told the underlying table changed.
    markSceneDirty();
    emit featuresChanged(ids);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

Schema FeatureLayer::schema() const
{
    return m_store ? m_store->schema() : Schema{};
}

bool FeatureLayer::addField(const FieldDef &f, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->addField(f, error)) return false;
    emit schemaChanged();
    afterWrite({});
    return true;
}

bool FeatureLayer::removeField(const QString &name, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->removeField(name, error)) return false;
    emit schemaChanged();
    afterWrite({});
    return true;
}

Schema FeatureLayer::editorSchema() const
{
    return openswmmvis::feature::editorSchema(m_role, schema());
}

bool FeatureLayer::renameField(const QString &from, const QString &to, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->renameField(from, to, error)) return false;
    emit schemaChanged();
    afterWrite({});
    return true;
}

bool FeatureLayer::setFieldMetadata(const QString &name, const FieldDef &meta,
                                    QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->setFieldMetadata(name, meta, error)) return false;
    emit schemaChanged();
    afterWrite({});
    return true;
}

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------

GeometryType FeatureLayer::geometryType() const
{
    return m_store ? m_store->geometryType() : GeometryType::None;
}

bool FeatureLayer::isThreeD() const
{
    return m_store && m_store->hasZ();
}

int FeatureLayer::count() const
{
    return m_store ? m_store->count() : 0;
}

QVector<FeatureLayer::FeatureId> FeatureLayer::featureIds() const
{
    return m_store ? m_store->featureIds() : QVector<FeatureId>{};
}

bool FeatureLayer::feature(FeatureId id, Feature &out) const
{
    return m_store && m_store->feature(id, out);
}

QVector<FeatureLayer::Feature> FeatureLayer::allFeatures() const
{
    return m_store ? m_store->allFeatures() : QVector<Feature>{};
}

FeatureLayer::FeatureId FeatureLayer::addFeature(const Feature &f, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return kInvalidFeatureId;
    }

    // Conform the attributes to the live schema before writing, so a stale map
    // from an undo record cannot reintroduce a column that has been removed.
    Feature copy = f;
    copy.attributes = schema().conform(copy.attributes);

    const FeatureId id = m_store->addFeature(copy, error);
    if (id == kInvalidFeatureId) return id;
    afterWrite({id});
    return id;
}

bool FeatureLayer::setGeometry(FeatureId id, const FeatureGeometry &g, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->setGeometry(id, g, error)) return false;
    afterWrite({id});
    return true;
}

bool FeatureLayer::setAttributes(FeatureId id, const QVariantMap &attrs, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->setAttributes(id, schema().conform(attrs), error)) return false;
    afterWrite({id});
    return true;
}

bool FeatureLayer::setFeature(FeatureId id, const Feature &f, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    Feature copy = f;
    copy.attributes = schema().conform(copy.attributes);
    if (!m_store->setFeature(id, copy, error)) return false;
    afterWrite({id});
    return true;
}

bool FeatureLayer::removeFeature(FeatureId id, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return false;
    }
    if (!m_store->removeFeature(id, error)) return false;
    afterWrite({id});
    return true;
}

// ---------------------------------------------------------------------------
// Role and Z
// ---------------------------------------------------------------------------

void FeatureLayer::setRole(FeatureLayerRole r)
{
    if (m_role == r) return;
    m_role = r;
    emit roleChanged(static_cast<int>(r));
}

void FeatureLayer::setZPolicy(const ZPolicy &p)
{
    if (m_zPolicy == p) return;
    // The 2D/3D dimension is fixed at creation (it decides the OGR geometry
    // type); only the SOURCE may change afterwards. Guard against a policy
    // that would silently imply a table migration.
    ZPolicy next = p;
    if (isThreeD() && next.source == ZPolicy::Source::None) {
        qCWarning(lcFeatureLayer)
            << "FeatureLayer" << name()
            << ": ignoring a request to drop the Z source on a 3D table;"
               " re-create the layer as 2D instead.";
        next.source = m_zPolicy.source;
    }
    m_zPolicy = next;
    emit zPolicyChanged();
}

GISRasterLayer *FeatureLayer::rasterSource(const MapCanvas *canvas) const
{
    if (!canvas || m_zPolicy.sourceLayerId.isEmpty()) return nullptr;
    for (OpenSWMMVisLayer *l : canvas->layers())
        if (l && l->layerId() == m_zPolicy.sourceLayerId)
            return qobject_cast<GISRasterLayer *>(l);
    return nullptr;
}

SWMM2DMeshLayer *FeatureLayer::meshSource(const MapCanvas *canvas) const
{
    if (!canvas || m_zPolicy.sourceLayerId.isEmpty()) return nullptr;
    for (OpenSWMMVisLayer *l : canvas->layers())
        if (l && l->layerId() == m_zPolicy.sourceLayerId)
            // 'class' tag: the inherited enumerator
            // OpenSWMMVisLayer::SWMM2DMeshLayer hides the class name here.
            return qobject_cast<class SWMM2DMeshLayer *>(l);
    return nullptr;
}

int FeatureLayer::sampleZ(FeatureGeometry &g, const MapCanvas *canvas,
                          bool densify) const
{
    if (m_zPolicy.source == ZPolicy::Source::None) {
        g.setHasZ(false);
        return 0;
    }

    g.setHasZ(true);
    if (densify && m_zPolicy.densifySpacing > 0.0)
        g.densify(m_zPolicy.densifySpacing);

    GISRasterLayer        *raster = nullptr;
    class SWMM2DMeshLayer *mesh   = nullptr;
    const SpatialReferenceSystem *canvasSRS = canvas ? canvas->canvasSRS() : nullptr;

    switch (m_zPolicy.source) {
    case ZPolicy::Source::Raster: raster = rasterSource(canvas); break;
    case ZPolicy::Source::Mesh:   mesh   = meshSource(canvas);   break;
    default: break;
    }

    // Per-vertex sampler. Returns NaN when there is no value — never zero:
    // the mesh pipeline distinguishes an unsampled seed via SteinerPoint::hasZ
    // (meshgenerator.h:50-51) and a fabricated zero would be routed as real
    // terrain at sea level.
    const auto sampleOne = [&](const QPointF &p) -> double {
        switch (m_zPolicy.source) {
        case ZPolicy::Source::Constant:
            return m_zPolicy.constant * m_zPolicy.zScale;
        case ZPolicy::Source::Raster: {
            if (!raster) return kNaN;
            bool ok = false;
            const double v = raster->valueAt(p.x(), p.y(), canvasSRS,
                                             m_zPolicy.rasterBand, &ok);
            return ok ? v * m_zPolicy.zScale : kNaN;
        }
        case ZPolicy::Source::Mesh: {
            if (!mesh) return kNaN;
            const QPointF s = mapToScene(p);
            const double v = mesh->sampleZAt(s.x(), s.y());
            return std::isnan(v) ? kNaN : v * m_zPolicy.zScale;
        }
        case ZPolicy::Source::None:
            break;
        }
        return kNaN;
    };

    int unsampled = 0;
    const auto doRing = [&](Ring &r) {
        r.ensureZ();
        for (int i = 0; i < r.pts.size(); ++i) {
            const double v = sampleOne(r.pts.at(i));
            r.z[i] = v;
            if (std::isnan(v)) ++unsampled;
        }
    };

    for (Part &part : g.parts()) {
        doRing(part.exterior);
        for (Ring &h : part.holes) doRing(h);
    }
    return unsampled;
}

int FeatureLayer::resampleAllZ(const MapCanvas *canvas, QString *error)
{
    if (!isEditable()) {
        if (error) *error = tr_("This feature layer is not open for editing.");
        return 0;
    }

    int changed = 0;
    const QVector<Feature> feats = allFeatures();
    for (const Feature &f : feats) {
        FeatureGeometry g = f.geometry;
        sampleZ(g, canvas, /*densify=*/true);
        if (g == f.geometry) continue;
        QString localErr;
        if (!m_store->setGeometry(f.id, g, &localErr)) {
            if (error) *error = localErr;
            break;
        }
        ++changed;
    }

    if (changed > 0)
        afterWrite({});   // empty ⇒ "everything changed"
    return changed;
}

int FeatureLayer::unsampledZCount() const
{
    int n = 0;
    const QVector<Feature> feats = allFeatures();
    for (const Feature &f : feats) n += f.geometry.unsampledZCount();
    return n;
}

// ---------------------------------------------------------------------------
// Self-description
// ---------------------------------------------------------------------------

QString FeatureLayer::sourceDescription() const
{
    if (m_gpkgPath.isEmpty()) return tr_("Unsaved feature layer");
    return tr_("%1 [%2] — editable")
               .arg(QFileInfo(m_gpkgPath).fileName(), m_table);
}

QVector<QPair<QString, QString>> FeatureLayer::extendedMetadata() const
{
    QVector<QPair<QString, QString>> rows;
    rows.append({tr_("Kind"),      tr_("Editable feature layer")});
    rows.append({tr_("Role"),      featureLayerRoleLabel(m_role)});
    rows.append({tr_("Geometry"),  geometryTypeLabel(geometryType())});
    rows.append({tr_("Dimension"), isThreeD() ? tr_("3D (Z)") : tr_("2D")});
    rows.append({tr_("Features"),  QString::number(count())});
    rows.append({tr_("Columns"),   QString::number(schema().count())});
    rows.append({tr_("GeoPackage"), m_gpkgPath});
    rows.append({tr_("Table"),      m_table});

    if (isThreeD()) {
        const int unsampled = unsampledZCount();
        rows.append({tr_("Unsampled Z vertices"), QString::number(unsampled)});
        if (m_zPolicy.densifySpacing > 0.0)
            rows.append({tr_("Densify spacing"),
                         QString::number(m_zPolicy.densifySpacing, 'g', 6)});
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

QJsonObject FeatureLayer::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("layerName"), m_table);
    o.insert(QStringLiteral("role"),      featureLayerRoleToken(m_role));
    o.insert(QStringLiteral("zPolicy"),   m_zPolicy.toJson());
    o.insert(QStringLiteral("symbol"),    symbol().toJson());
    return o;
}

void FeatureLayer::applyJson(const QJsonObject &o)
{
    setRole(featureLayerRoleFromToken(o.value(QStringLiteral("role")).toString()));

    ZPolicy p = ZPolicy::fromJson(o.value(QStringLiteral("zPolicy")).toObject());
    // Restore without the 3D guard in setZPolicy: the stored policy is by
    // definition the one the table was created with.
    m_zPolicy = p;
    emit zPolicyChanged();

    if (o.contains(QStringLiteral("symbol"))) {
        GISVectorSymbol s = symbol();
        s.fromJson(o.value(QStringLiteral("symbol")).toObject());
        setSymbol(s);
    }
}
