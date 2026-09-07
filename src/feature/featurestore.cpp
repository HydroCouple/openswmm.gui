/*!
 * \file   featurestore.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "feature/featurestore.h"

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal_priv.h>
#include <ogr_core.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFeatureStore, "openswmmvis.feature.store")

namespace openswmmvis::feature {

namespace {

constexpr const char *kGpkgDriver = "GPKG";

QString tr_(const char *s) { return QCoreApplication::translate("FeatureStore", s); }

void setErr(QString *error, const QString &msg)
{
    if (error) *error = msg;
    qCWarning(lcFeatureStore).noquote() << msg;
}

/*! The last CPL error message, or a generic fallback. GDAL leaves the message
 *  set after a failed call; including it turns "could not write" into
 *  something diagnosable. */
QString lastGdalError()
{
    const char *msg = CPLGetLastErrorMsg();
    const QString s = msg ? QString::fromUtf8(msg).trimmed() : QString();
    return s.isEmpty() ? tr_("no further detail from GDAL") : s;
}

GDALDataset *openGpkg(const QString &path, bool update, QString *error)
{
    CPLErrorReset();
    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(path.toUtf8().constData(),
                   GDAL_OF_VECTOR | (update ? GDAL_OF_UPDATE : GDAL_OF_READONLY),
                   nullptr, nullptr, nullptr));
    if (!ds)
        setErr(error, tr_("Could not open \"%1\": %2")
                          .arg(path, lastGdalError()));
    return ds;
}

/*! Build an OGRSpatialReference from WKT. Caller owns the result (nullptr when
 *  \p wkt is empty or unparseable — an unparseable CRS is not fatal, the table
 *  is simply created without one). */
OGRSpatialReference *srsFromWkt(const QString &wkt)
{
    if (wkt.isEmpty()) return nullptr;
    auto *srs = new OGRSpatialReference();
    if (srs->importFromWkt(wkt.toUtf8().constData()) != OGRERR_NONE) {
        delete srs;
        return nullptr;
    }
    return srs;
}

}   // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FeatureStore::FeatureStore()
{
    GDALAllRegister();   // idempotent, same as GISVectorLayer's ctor
}

FeatureStore::~FeatureStore()
{
    close();
}

// ---------------------------------------------------------------------------
// Container-level operations
// ---------------------------------------------------------------------------

bool FeatureStore::ensureGeoPackage(const QString &gpkgPath, QString *error)
{
    if (gpkgPath.isEmpty()) {
        setErr(error, tr_("No GeoPackage path was given."));
        return false;
    }

    GDALAllRegister();

    if (QFileInfo::exists(gpkgPath)) {
        // Already there: confirm it actually opens as a vector datasource
        // rather than assuming, so a corrupt or non-GPKG file fails here with
        // a clear message instead of at the first write.
        QString openErr;
        GDALDataset *ds = openGpkg(gpkgPath, /*update=*/true, &openErr);
        if (!ds) {
            setErr(error, tr_("\"%1\" exists but is not a writable GeoPackage: %2")
                              .arg(gpkgPath, openErr));
            return false;
        }
        GDALClose(ds);
        return true;
    }

    const QFileInfo fi(gpkgPath);
    if (!fi.absoluteDir().exists()
        && !QDir().mkpath(fi.absolutePath())) {
        setErr(error, tr_("Could not create the folder for \"%1\".").arg(gpkgPath));
        return false;
    }

    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName(kGpkgDriver);
    if (!drv) {
        setErr(error, tr_("This build of GDAL has no GeoPackage driver, so "
                          "feature layers cannot be stored."));
        return false;
    }

    CPLErrorReset();
    GDALDataset *ds = drv->Create(gpkgPath.toUtf8().constData(),
                                  0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        setErr(error, tr_("Could not create \"%1\": %2")
                          .arg(gpkgPath, lastGdalError()));
        return false;
    }
    GDALClose(ds);
    return true;
}

QStringList FeatureStore::tables(const QString &gpkgPath)
{
    QStringList out;
    if (!QFileInfo::exists(gpkgPath)) return out;
    GDALAllRegister();
    GDALDataset *ds = openGpkg(gpkgPath, /*update=*/false, nullptr);
    if (!ds) return out;
    for (int i = 0; i < ds->GetLayerCount(); ++i)
        if (OGRLayer *l = ds->GetLayer(i))
            out << QString::fromUtf8(l->GetName());
    GDALClose(ds);
    return out;
}

bool FeatureStore::tableExists(const QString &gpkgPath, const QString &table)
{
    const QStringList names = tables(gpkgPath);
    for (const QString &n : names)
        if (n.compare(table, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QString FeatureStore::uniqueTableName(const QString &gpkgPath, const QString &desired)
{
    QString base = sanitizeTableName(desired);
    if (base.isEmpty()) base = QStringLiteral("features");
    if (!tableExists(gpkgPath, base)) return base;
    for (int n = 1; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
        if (!tableExists(gpkgPath, candidate)) return candidate;
    }
    return base + QStringLiteral("_x");
}

bool FeatureStore::createTable(const QString &gpkgPath,
                               const QString &table,
                               GeometryType   type,
                               bool           hasZ,
                               const Schema  &schema,
                               const QString &srsWkt,
                               QString       *error)
{
    if (type == GeometryType::None) {
        setErr(error, tr_("A feature layer needs a geometry type."));
        return false;
    }
    const QString name = sanitizeTableName(table);
    if (name.isEmpty()) {
        setErr(error, tr_("\"%1\" is not a usable table name.").arg(table));
        return false;
    }
    if (!ensureGeoPackage(gpkgPath, error))
        return false;
    if (tableExists(gpkgPath, name)) {
        setErr(error, tr_("A table named \"%1\" already exists.").arg(name));
        return false;
    }

    GDALDataset *ds = openGpkg(gpkgPath, /*update=*/true, error);
    if (!ds) return false;

    OGRSpatialReference *srs = srsFromWkt(srsWkt);
    const auto wkbType =
        static_cast<OGRwkbGeometryType>(FeatureGeometry::ogrTypeFor(type, hasZ));

    CPLErrorReset();
    OGRLayer *layer = ds->CreateLayer(name.toUtf8().constData(), srs, wkbType, nullptr);
    if (srs) srs->Release();

    if (!layer) {
        setErr(error, tr_("Could not create table \"%1\": %2")
                          .arg(name, lastGdalError()));
        GDALClose(ds);
        return false;
    }

    for (const FieldDef &f : schema.fields()) {
        OGRFieldDefn defn(f.name.toUtf8().constData(),
                          static_cast<OGRFieldType>(ogrFieldTypeFor(f.type)));
        if (f.type == FieldType::Boolean) defn.SetSubType(OFSTBoolean);
        if (f.type == FieldType::Text)    defn.SetWidth(0);   // unbounded
        CPLErrorReset();
        if (layer->CreateField(&defn) != OGRERR_NONE) {
            setErr(error, tr_("Could not create the column \"%1\": %2")
                              .arg(f.name, lastGdalError()));
            GDALClose(ds);
            return false;
        }
    }

    GDALClose(ds);   // commits
    return true;
}

bool FeatureStore::dropTable(const QString &gpkgPath, const QString &table,
                             QString *error)
{
    GDALDataset *ds = openGpkg(gpkgPath, /*update=*/true, error);
    if (!ds) return false;

    int index = -1;
    for (int i = 0; i < ds->GetLayerCount(); ++i) {
        OGRLayer *l = ds->GetLayer(i);
        if (l && table.compare(QString::fromUtf8(l->GetName()), Qt::CaseInsensitive) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        GDALClose(ds);
        return true;   // already gone — idempotent
    }

    CPLErrorReset();
    const OGRErr err = ds->DeleteLayer(index);
    GDALClose(ds);
    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not delete table \"%1\": %2")
                          .arg(table, lastGdalError()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

bool FeatureStore::open(const QString &gpkgPath, const QString &table, QString *error)
{
    close();

    GDALDataset *ds = openGpkg(gpkgPath, /*update=*/true, error);
    if (!ds) return false;

    OGRLayer *layer = ds->GetLayerByName(table.toUtf8().constData());
    if (!layer) {
        setErr(error, tr_("\"%1\" has no table named \"%2\".").arg(gpkgPath, table));
        GDALClose(ds);
        return false;
    }

    m_dataset     = ds;
    m_layer       = layer;
    m_ownsDataset = true;
    m_gpkgPath    = gpkgPath;
    m_table       = QString::fromUtf8(layer->GetName());
    return true;
}

void FeatureStore::attach(GDALDataset *ds, OGRLayer *layer,
                          const QString &gpkgPath, const QString &table)
{
    close();
    m_dataset     = ds;
    m_layer       = layer;
    m_ownsDataset = false;      // the caller closes it
    m_gpkgPath    = gpkgPath;
    m_table       = layer ? QString::fromUtf8(layer->GetName()) : table;
}

void FeatureStore::close()
{
    m_layer = nullptr;
    if (m_dataset && m_ownsDataset)
        GDALClose(m_dataset);
    m_dataset     = nullptr;
    m_ownsDataset = true;
    m_gpkgPath.clear();
    m_table.clear();
}

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

Schema FeatureStore::schema() const
{
    Schema s;
    if (!m_layer) return s;
    OGRFeatureDefn *defn = m_layer->GetLayerDefn();
    if (!defn) return s;

    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        const OGRFieldDefn *fd = defn->GetFieldDefn(i);
        if (!fd) continue;
        FieldDef f;
        f.name = QString::fromUtf8(fd->GetNameRef());
        switch (fd->GetType()) {
        case OFTInteger:
        case OFTInteger64:
            f.type = (fd->GetSubType() == OFSTBoolean) ? FieldType::Boolean
                                                       : FieldType::Integer;
            break;
        case OFTReal:
            f.type = FieldType::Real;
            break;
        default:
            f.type = FieldType::Text;
            break;
        }
        s.append(f);
    }
    return s;
}

GeometryType FeatureStore::geometryType() const
{
    if (!m_layer) return GeometryType::None;
    switch (wkbFlatten(m_layer->GetGeomType())) {
    case wkbPoint:           return GeometryType::Point;
    case wkbLineString:      return GeometryType::LineString;
    case wkbPolygon:         return GeometryType::Polygon;
    case wkbMultiPoint:      return GeometryType::MultiPoint;
    case wkbMultiLineString: return GeometryType::MultiLineString;
    case wkbMultiPolygon:    return GeometryType::MultiPolygon;
    default:                 return GeometryType::None;
    }
}

bool FeatureStore::hasZ() const
{
    if (!m_layer) return false;
    return wkbHasZ(m_layer->GetGeomType());
}

QString FeatureStore::srsWkt() const
{
    if (!m_layer) return {};
    const OGRSpatialReference *srs = m_layer->GetSpatialRef();
    if (!srs) return {};
    char *wkt = nullptr;
    srs->exportToWkt(&wkt);
    QString out;
    if (wkt) { out = QString::fromUtf8(wkt); CPLFree(wkt); }
    return out;
}

int FeatureStore::count() const
{
    if (!m_layer) return 0;
    return static_cast<int>(m_layer->GetFeatureCount(/*bForce=*/TRUE));
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

QVector<FeatureId> FeatureStore::featureIds() const
{
    QVector<FeatureId> out;
    if (!m_layer) return out;
    m_layer->ResetReading();
    while (OGRFeature *f = m_layer->GetNextFeature()) {
        out.append(static_cast<FeatureId>(f->GetFID()));
        OGRFeature::DestroyFeature(f);
    }
    return out;
}

OGRFeature *FeatureStore::fetch(FeatureId id) const
{
    if (!m_layer || id == kInvalidFeatureId) return nullptr;
    return m_layer->GetFeature(static_cast<GIntBig>(id));
}

QVariantMap FeatureStore::readAttributes(const OGRFeature *feat) const
{
    QVariantMap m;
    if (!feat) return m;
    // GetFieldCount / GetFieldDefnRef are non-const in older GDAL headers;
    // the cast keeps this callable from a const method across versions.
    auto *f = const_cast<OGRFeature *>(feat);
    OGRFeatureDefn *defn = f->GetDefnRef();
    if (!defn) return m;

    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        const OGRFieldDefn *fd = defn->GetFieldDefn(i);
        if (!fd) continue;
        const QString name = QString::fromUtf8(fd->GetNameRef());
        if (!f->IsFieldSetAndNotNull(i)) {
            m.insert(name, QVariant());
            continue;
        }
        switch (fd->GetType()) {
        case OFTInteger:
            if (fd->GetSubType() == OFSTBoolean)
                m.insert(name, f->GetFieldAsInteger(i) != 0);
            else
                m.insert(name, f->GetFieldAsInteger(i));
            break;
        case OFTInteger64:
            m.insert(name, static_cast<qlonglong>(f->GetFieldAsInteger64(i)));
            break;
        case OFTReal:
            m.insert(name, f->GetFieldAsDouble(i));
            break;
        default:
            m.insert(name, QString::fromUtf8(f->GetFieldAsString(i)));
            break;
        }
    }
    return m;
}

bool FeatureStore::feature(FeatureId id, Feature &out) const
{
    OGRFeature *f = fetch(id);
    if (!f) return false;

    out.id         = static_cast<FeatureId>(f->GetFID());
    out.geometry   = FeatureGeometry::fromOGR(f->GetGeometryRef());
    out.attributes = readAttributes(f);

    OGRFeature::DestroyFeature(f);
    return true;
}

QVector<Feature> FeatureStore::allFeatures() const
{
    QVector<Feature> out;
    if (!m_layer) return out;
    out.reserve(count());
    m_layer->ResetReading();
    while (OGRFeature *f = m_layer->GetNextFeature()) {
        Feature feat;
        feat.id         = static_cast<FeatureId>(f->GetFID());
        feat.geometry   = FeatureGeometry::fromOGR(f->GetGeometryRef());
        feat.attributes = readAttributes(f);
        out.append(feat);
        OGRFeature::DestroyFeature(f);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------

void FeatureStore::applyAttributes(OGRFeature *feat, const QVariantMap &attrs) const
{
    if (!feat) return;
    OGRFeatureDefn *defn = feat->GetDefnRef();
    if (!defn) return;

    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        const OGRFieldDefn *fd = defn->GetFieldDefn(i);
        if (!fd) continue;
        const QString name = QString::fromUtf8(fd->GetNameRef());
        const auto it = attrs.constFind(name);
        if (it == attrs.constEnd()) continue;      // leave untouched
        const QVariant &v = it.value();
        if (!v.isValid() || v.isNull()) {
            feat->SetFieldNull(i);
            continue;
        }
        switch (fd->GetType()) {
        case OFTInteger:
            feat->SetField(i, fd->GetSubType() == OFSTBoolean
                                  ? (v.toBool() ? 1 : 0)
                                  : v.toInt());
            break;
        case OFTInteger64:
            feat->SetField(i, static_cast<GIntBig>(v.toLongLong()));
            break;
        case OFTReal:
            feat->SetField(i, v.toDouble());
            break;
        default:
            feat->SetField(i, v.toString().toUtf8().constData());
            break;
        }
    }
}

FeatureId FeatureStore::addFeature(const Feature &f, QString *error)
{
    if (!m_layer) {
        setErr(error, tr_("The feature layer is not open."));
        return kInvalidFeatureId;
    }

    OGRFeature *feat = OGRFeature::CreateFeature(m_layer->GetLayerDefn());
    if (!feat) {
        setErr(error, tr_("Could not allocate a feature."));
        return kInvalidFeatureId;
    }

    // Reuse the caller's id when it is free so redo restores the id undo
    // removed (see the header note). OGR treats an unset FID as "assign one".
    if (f.id != kInvalidFeatureId) {
        if (OGRFeature *existing = fetch(f.id)) {
            OGRFeature::DestroyFeature(existing);   // taken — let OGR assign
        } else {
            feat->SetFID(static_cast<GIntBig>(f.id));
        }
    }

    if (OGRGeometry *g = f.geometry.toOGR(m_layer->GetSpatialRef())) {
        // Directly = takes ownership; nothing to free on this path.
        feat->SetGeometryDirectly(g);
    }
    applyAttributes(feat, f.attributes);

    CPLErrorReset();
    const OGRErr err = m_layer->CreateFeature(feat);
    const FeatureId newId = (err == OGRERR_NONE)
                                ? static_cast<FeatureId>(feat->GetFID())
                                : kInvalidFeatureId;
    OGRFeature::DestroyFeature(feat);

    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not add the feature: %1").arg(lastGdalError()));
        return kInvalidFeatureId;
    }
    flush();
    return newId;
}

bool FeatureStore::setGeometry(FeatureId id, const FeatureGeometry &g, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    OGRFeature *feat = fetch(id);
    if (!feat) {
        setErr(error, tr_("Feature %1 no longer exists.").arg(id));
        return false;
    }

    if (OGRGeometry *og = g.toOGR(m_layer->GetSpatialRef()))
        feat->SetGeometryDirectly(og);
    else
        feat->SetGeometryDirectly(nullptr);

    CPLErrorReset();
    const OGRErr err = m_layer->SetFeature(feat);
    OGRFeature::DestroyFeature(feat);

    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not update the geometry of feature %1: %2")
                          .arg(id).arg(lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::setAttributes(FeatureId id, const QVariantMap &attrs, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    OGRFeature *feat = fetch(id);
    if (!feat) {
        setErr(error, tr_("Feature %1 no longer exists.").arg(id));
        return false;
    }

    applyAttributes(feat, attrs);

    CPLErrorReset();
    const OGRErr err = m_layer->SetFeature(feat);
    OGRFeature::DestroyFeature(feat);

    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not update the attributes of feature %1: %2")
                          .arg(id).arg(lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::setFeature(FeatureId id, const Feature &f, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    OGRFeature *feat = fetch(id);
    if (!feat) {
        setErr(error, tr_("Feature %1 no longer exists.").arg(id));
        return false;
    }

    if (OGRGeometry *og = f.geometry.toOGR(m_layer->GetSpatialRef()))
        feat->SetGeometryDirectly(og);
    else
        feat->SetGeometryDirectly(nullptr);
    applyAttributes(feat, f.attributes);

    CPLErrorReset();
    const OGRErr err = m_layer->SetFeature(feat);
    OGRFeature::DestroyFeature(feat);

    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not update feature %1: %2")
                          .arg(id).arg(lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::removeFeature(FeatureId id, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }

    CPLErrorReset();
    const OGRErr err = m_layer->DeleteFeature(static_cast<GIntBig>(id));
    if (err != OGRERR_NONE) {
        setErr(error, tr_("Could not delete feature %1: %2")
                          .arg(id).arg(lastGdalError()));
        return false;
    }
    flush();
    return true;
}

// ---------------------------------------------------------------------------
// Schema evolution
// ---------------------------------------------------------------------------

bool FeatureStore::addField(const FieldDef &f, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    const QString name = sanitizeFieldName(f.name);
    if (name.isEmpty()) {
        setErr(error, tr_("\"%1\" is not a usable column name.").arg(f.name));
        return false;
    }
    if (schema().contains(name)) {
        setErr(error, tr_("A column named \"%1\" already exists.").arg(name));
        return false;
    }

    OGRFieldDefn defn(name.toUtf8().constData(),
                      static_cast<OGRFieldType>(ogrFieldTypeFor(f.type)));
    if (f.type == FieldType::Boolean) defn.SetSubType(OFSTBoolean);

    CPLErrorReset();
    if (m_layer->CreateField(&defn) != OGRERR_NONE) {
        setErr(error, tr_("Could not add the column \"%1\": %2")
                          .arg(name, lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::removeField(const QString &name, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }

    if (!m_layer->TestCapability(OLCDeleteField)) {
        setErr(error, tr_("This build of GDAL cannot delete a column from a "
                          "GeoPackage table. The column was left in place."));
        return false;
    }

    OGRFeatureDefn *defn = m_layer->GetLayerDefn();
    const int idx = defn ? defn->GetFieldIndex(name.toUtf8().constData()) : -1;
    if (idx < 0) return true;   // already gone — idempotent

    CPLErrorReset();
    if (m_layer->DeleteField(idx) != OGRERR_NONE) {
        setErr(error, tr_("Could not delete the column \"%1\": %2")
                          .arg(name, lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::flush()
{
    if (!m_dataset) return false;
    // SyncToDisk on the layer commits the pending GPKG transaction; FlushCache
    // on the dataset is the belt-and-braces half that also matters for a
    // concurrent read-only opener on a worker thread.
    if (m_layer) m_layer->SyncToDisk();
    m_dataset->FlushCache(/*bAtClosing=*/false);
    return true;
}

}   // namespace openswmmvis::feature
