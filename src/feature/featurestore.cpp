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
#include <QLocale>
#include <QLoggingCategory>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// Column metadata: defaults, descriptions, value lists
// (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md P5, §6 "Store").
//
// Defaults are SQL literals in the table definition and every GDAL stores
// them. Descriptions (gpkg_data_columns, OGRFieldDefn::SetComment) and coded
// value lists (gpkg_data_column_constraints, field domains) need GDAL 3.8 for
// the full add / update / delete set; below that the columns are created
// without them and the role registry still supplies the dropdowns.
// ---------------------------------------------------------------------------

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3, 8, 0)
#define FEATURESTORE_FIELD_META 1
#else
#define FEATURESTORE_FIELD_META 0
#endif

/*! The SQL literal OGRFieldDefn::SetDefault expects for \p f's default, or
 *  an empty string when \p f has none. Strings are single-quoted with
 *  embedded quotes doubled; numbers use the shortest round-trip form. */
QString defaultLiteral(const FieldDef &f)
{
    if (!f.defaultValue.isValid() || f.defaultValue.isNull()) return {};
    const QVariant v = coerceToFieldType(f.defaultValue, f.type);
    switch (f.type) {
    case FieldType::Text: {
        QString s = v.toString();
        s.replace(QLatin1Char('\''), QStringLiteral("''"));
        return QLatin1Char('\'') + s + QLatin1Char('\'');
    }
    case FieldType::Integer:
        return QString::number(v.toInt());
    case FieldType::Real:
        return QString::number(v.toDouble(), 'g', QLocale::FloatingPointShortest);
    case FieldType::Boolean:
        return v.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    }
    return {};
}

/*! Inverse of \ref defaultLiteral. An expression GDAL reports verbatim
 *  (CURRENT_TIMESTAMP, …) is not a value and reads back invalid. */
QVariant parseDefaultLiteral(const char *literal, FieldType type)
{
    if (!literal || !*literal) return {};
    const QString s = QString::fromUtf8(literal).trimmed();
    if (s.size() >= 2 && s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))) {
        QString inner = s.mid(1, s.size() - 2);
        inner.replace(QStringLiteral("''"), QStringLiteral("'"));
        return coerceToFieldType(inner, type);
    }
    bool ok = false;
    switch (type) {
    case FieldType::Integer: {
        const qlonglong i = s.toLongLong(&ok);
        if (ok) return QVariant(int(i));
        const double d = s.toDouble(&ok);   // "1.0" written by another tool
        return ok ? QVariant(int(d)) : QVariant();
    }
    case FieldType::Real: {
        const double d = s.toDouble(&ok);
        return ok ? QVariant(d) : QVariant();
    }
    case FieldType::Boolean: {
        const QString l = s.toLower();
        if (l == QLatin1String("1") || l == QLatin1String("true"))  return true;
        if (l == QLatin1String("0") || l == QLatin1String("false")) return false;
        return {};
    }
    case FieldType::Text:
        return {};   // an unquoted text default is an expression
    }
    return {};
}

/*! The name of the coded domain this store creates for \p field of \p table.
 *  Domain names are per GeoPackage, and every FeatureLayer keeps its own
 *  long-open handle that does not see tables created after it opened, so
 *  names must not collide by construction: sanitised names never contain
 *  "__", so "a" + "b_c" → "a__b_c" and "a_b" + "c" → "a_b__c". */
QString domainNameFor(const QString &table, const QString &field)
{
    return table + QStringLiteral("__") + field;
}

#if FEATURESTORE_FIELD_META
/*!
 * True when any field other than field \p selfIdx of \p self (-1: none
 * excluded) names \p domain — in \p self or in any other table of \p ds.
 * Domain names are per dataset, and two tables can derive the same one from
 * domainNameFor() ("a" + "b_c" and "a_b" + "c").
 */
bool domainUsedByOtherField(GDALDataset *ds, const OGRFeatureDefn *self,
                            const std::string &domain, int selfIdx)
{
    if (domain.empty()) return false;
    const auto usedIn = [&domain](const OGRFeatureDefn *d, int skip) {
        if (!d) return false;
        for (int i = 0; i < d->GetFieldCount(); ++i)
            if (i != skip && d->GetFieldDefn(i)->GetDomainName() == domain)
                return true;
        return false;
    };
    if (usedIn(self, selfIdx)) return true;
    for (int l = 0; ds && l < ds->GetLayerCount(); ++l) {
        OGRLayer *layer = ds->GetLayer(l);
        const OGRFeatureDefn *d = layer ? layer->GetLayerDefn() : nullptr;
        if (d && d != self && usedIn(d, -1)) return true;
    }
    return false;
}

/*! The values of coded domain \p name, or empty when it is missing or of
 *  another kind (a range or glob domain authored elsewhere). */
QVector<FieldChoice> readCodedDomain(const GDALDataset *ds, const std::string &name)
{
    QVector<FieldChoice> out;
    if (!ds || name.empty()) return out;
    const OGRFieldDomain *d = ds->GetFieldDomain(name);
    if (!d || d->GetDomainType() != OFDT_CODED) return out;
    const OGRCodedValue *cv = static_cast<const OGRCodedFieldDomain *>(d)->GetEnumeration();
    for (; cv && cv->pszCode; ++cv) {
        FieldChoice c;
        c.value = QString::fromUtf8(cv->pszCode);
        c.label = cv->pszValue ? QString::fromUtf8(cv->pszValue) : QString();
        out.append(c);
    }
    return out;
}

/*!
 * \brief Make a coded domain holding \p choices available for field
 *        \p selfIdx of \p defn (-1 for a field not created yet) and return
 *        its name, or an empty string on failure.
 *
 * \details \p preferred is used when it is free, already holds exactly these
 *          values, or is this field's alone and can be updated in place.
 *          Otherwise — a list another column (of any table) still uses, or a driver that
 *          cannot update a domain (GDAL's GeoPackage driver before 3.x) — the
 *          list goes under the next free name, preferred_2, _3, …, rather than
 *          failing or changing another column's list.
 */
QString ensureCodedDomain(GDALDataset *ds, const OGRFeatureDefn *defn, int selfIdx,
                          const QString &preferred, const QVector<FieldChoice> &choices,
                          QString *error)
{
    const auto build = [&choices](const std::string &name) {
        std::vector<OGRCodedValue> values;
        values.reserve(size_t(choices.size()));
        for (const FieldChoice &c : choices) {
            OGRCodedValue cv;
            // The domain takes ownership of both strings (CPLFree in its dtor).
            cv.pszCode  = CPLStrdup(c.value.toUtf8().constData());
            cv.pszValue = c.label.isEmpty() ? nullptr
                                            : CPLStrdup(c.label.toUtf8().constData());
            values.push_back(cv);
        }
        return std::make_unique<OGRCodedFieldDomain>(
            name, std::string(), OFTString, OFSTNone, std::move(values));
    };

    for (int n = 1; n < 1000; ++n) {
        const QString name = n == 1 ? preferred
                                    : preferred + QLatin1Char('_') + QString::number(n);
        const std::string sname = name.toStdString();
        std::string reason;
        CPLErrorReset();
        const OGRFieldDomain *existing = ds->GetFieldDomain(sname);
        if (!existing) {
            if (ds->AddFieldDomain(build(sname), reason)) return name;
            setErr(error, tr_("Could not store the value list \"%1\": %2")
                              .arg(name, reason.empty() ? lastGdalError()
                                                        : QString::fromStdString(reason)));
            return {};
        }
        if (sameChoiceSet(readCodedDomain(ds, sname), choices)) return name;
        if (!domainUsedByOtherField(ds, defn, sname, selfIdx)
            && ds->UpdateFieldDomain(build(sname), reason))
            return name;
        // In use elsewhere, or not updatable here: try the next name.
    }
    setErr(error, tr_("Could not store the value list \"%1\".").arg(preferred));
    return {};
}

/*! Delete \p domain when it is one this store named for \p table
 *  ("<table>__…", or "<table>_…" from builds before 2026-10-01) and no field
 *  of \p defn uses it any more. Best effort: GeoPackage support for
 *  deleting a domain depends on the GDAL build, and an orphan is harmless. */
void dropOrphanDomain(GDALDataset *ds, const OGRFeatureDefn *defn,
                      const QString &table, const std::string &domain)
{
    if (domain.empty()) return;
    const QString q = QString::fromStdString(domain);
    if (!q.startsWith(table + QLatin1Char('_'))) return;
    if (domainUsedByOtherField(ds, defn, domain, -1)) return;
    std::string reason;
    if (!ds->DeleteFieldDomain(domain, reason))
        qCInfo(lcFeatureStore).noquote()
            << "value list" << q << "kept:" << QString::fromStdString(reason);
}
#endif

/*! Write \p f's default, description and value list onto \p defn. The coded
 *  domain itself must already exist in the dataset (\ref ensureCodedDomain). */
void applyFieldMetadata(OGRFieldDefn &defn, const FieldDef &f, const QString &domainName)
{
    const QString lit = defaultLiteral(f);
    defn.SetDefault(lit.isEmpty() ? nullptr : lit.toUtf8().constData());
#if FEATURESTORE_FIELD_META
    defn.SetComment(f.description.toStdString());
    defn.SetDomainName(f.hasFixedChoices() ? domainName.toStdString() : std::string());
#else
    Q_UNUSED(domainName);
#endif
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
        QString domain;
#if FEATURESTORE_FIELD_META
        // The domain must exist before a column can name it.
        if (f.hasFixedChoices()) {
            domain = ensureCodedDomain(ds, layer->GetLayerDefn(), -1,
                                       domainNameFor(name, f.name), f.choices, error);
            if (domain.isEmpty()) {
                GDALClose(ds);
                return false;
            }
        }
#endif
        applyFieldMetadata(defn, f, domain);
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
        f.defaultValue = parseDefaultLiteral(fd->GetDefault(), f.type);
#if FEATURESTORE_FIELD_META
        f.description = QString::fromStdString(fd->GetComment());
        f.choices     = readCodedDomain(m_dataset, fd->GetDomainName());
        if (!f.choices.isEmpty()) f.choiceSource = ChoiceSource::Fixed;
#endif
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
    QString domain;
#if FEATURESTORE_FIELD_META
    if (f.hasFixedChoices()) {
        domain = ensureCodedDomain(m_dataset, m_layer->GetLayerDefn(), -1,
                                   domainNameFor(m_table, name), f.choices, error);
        if (domain.isEmpty()) return false;
    }
#endif
    applyFieldMetadata(defn, f, domain);

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
#if FEATURESTORE_FIELD_META
    const std::string domain = defn->GetFieldDefn(idx)->GetDomainName();
#endif

    CPLErrorReset();
    if (m_layer->DeleteField(idx) != OGRERR_NONE) {
        setErr(error, tr_("Could not delete the column \"%1\": %2")
                          .arg(name, lastGdalError()));
        return false;
    }
#if FEATURESTORE_FIELD_META
    // Drop the column's own value list with it where the driver allows.
    dropOrphanDomain(m_dataset, m_layer->GetLayerDefn(), m_table, domain);
#endif
    flush();
    return true;
}

bool FeatureStore::renameField(const QString &from, const QString &to, QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    const QString newName = sanitizeFieldName(to);
    if (newName.isEmpty()) {
        setErr(error, tr_("\"%1\" is not a usable column name.").arg(to));
        return false;
    }
    OGRFeatureDefn *defn = m_layer->GetLayerDefn();
    const int idx = defn ? defn->GetFieldIndex(from.toUtf8().constData()) : -1;
    if (idx < 0) {
        setErr(error, tr_("There is no column named \"%1\".").arg(from));
        return false;
    }
    const int clash = defn->GetFieldIndex(newName.toUtf8().constData());
    if (clash >= 0 && clash != idx) {
        setErr(error, tr_("A column named \"%1\" already exists.").arg(newName));
        return false;
    }
    if (!m_layer->TestCapability(OLCAlterFieldDefn)) {
        setErr(error, tr_("This build of GDAL cannot rename a GeoPackage column."));
        return false;
    }

    OGRFieldDefn renamed(defn->GetFieldDefn(idx));
    renamed.SetName(newName.toUtf8().constData());
    CPLErrorReset();
    if (m_layer->AlterFieldDefn(idx, &renamed, ALTER_NAME_FLAG) != OGRERR_NONE) {
        setErr(error, tr_("Could not rename the column \"%1\": %2")
                          .arg(from, lastGdalError()));
        return false;
    }
    flush();
    return true;
}

bool FeatureStore::setFieldMetadata(const QString &name, const FieldDef &meta,
                                    QString *error)
{
    if (!m_layer) { setErr(error, tr_("The feature layer is not open.")); return false; }
    OGRFeatureDefn *defn = m_layer->GetLayerDefn();
    const int idx = defn ? defn->GetFieldIndex(name.toUtf8().constData()) : -1;
    if (idx < 0) {
        setErr(error, tr_("There is no column named \"%1\".").arg(name));
        return false;
    }
    if (!m_layer->TestCapability(OLCAlterFieldDefn)) {
        setErr(error, tr_("This build of GDAL cannot change a GeoPackage column."));
        return false;
    }

    const OGRFieldDefn *current = defn->GetFieldDefn(idx);
    // The type is the column's, not meta's: a default is written as the
    // literal of the type the column actually has.
    FieldDef effective = meta;
    effective.name = QString::fromUtf8(current->GetNameRef());
    switch (current->GetType()) {
    case OFTInteger:
    case OFTInteger64:
        effective.type = current->GetSubType() == OFSTBoolean ? FieldType::Boolean
                                                              : FieldType::Integer;
        break;
    case OFTReal: effective.type = FieldType::Real; break;
    default:      effective.type = FieldType::Text; break;
    }

    OGRFieldDefn altered(current);
    int flags = ALTER_DEFAULT_FLAG;
    QString domain;
#if FEATURESTORE_FIELD_META
    const std::string oldDomain = current->GetDomainName();
    if (effective.hasFixedChoices()) {
        // Prefer the column's own domain (it keeps its name across a rename).
        domain = ensureCodedDomain(m_dataset, defn, idx,
                                   oldDomain.empty() ? domainNameFor(m_table, effective.name)
                                                     : QString::fromStdString(oldDomain),
                                   effective.choices, error);
        if (domain.isEmpty()) return false;
    }
    flags |= ALTER_COMMENT_FLAG | ALTER_DOMAIN_FLAG;
#endif
    applyFieldMetadata(altered, effective, domain);

    CPLErrorReset();
    if (m_layer->AlterFieldDefn(idx, &altered, flags) != OGRERR_NONE) {
        setErr(error, tr_("Could not update the column \"%1\": %2")
                          .arg(name, lastGdalError()));
        return false;
    }
#if FEATURESTORE_FIELD_META
    if (oldDomain != domain.toStdString())   // detached or moved: tidy up
        dropOrphanDomain(m_dataset, m_layer->GetLayerDefn(), m_table, oldDomain);
#endif
    flush();
    return true;
}

bool FeatureStore::storesFieldMetadata()
{
    return FEATURESTORE_FIELD_META != 0;
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
