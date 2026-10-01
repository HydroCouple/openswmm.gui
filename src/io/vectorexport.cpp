/*!
 * \file   vectorexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "io/vectorexport.h"

#include "io/gdaldrivers.h"

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <memory>
#include <string>
#include <vector>

using openswmmvis::feature::FieldChoice;
using openswmmvis::feature::FieldType;

namespace openswmmvis::io {

namespace {

QString tr_(const char *s) { return QCoreApplication::translate("VectorExport", s); }

const QString kCancelled = QStringLiteral("Cancelled");

QString lastGdalError()
{
    const char *msg = CPLGetLastErrorMsg();
    const QString s = msg ? QString::fromUtf8(msg).trimmed() : QString();
    return s.isEmpty() ? tr_("no further detail from GDAL") : s;
}

/*! The in-memory vector driver: MEM from GDAL 3.11, Memory before. */
GDALDriver *memoryDriver()
{
    GDALDriverManager *mgr = GetGDALDriverManager();
    if (GDALDriver *d = mgr->GetDriverByName("MEM"))
        if (d->GetMetadataItem(GDAL_DCAP_VECTOR)) return d;
    return mgr->GetDriverByName("Memory");
}

/*! Delete \p path and the files that travel with it (Shapefile sidecars, a
 *  File Geodatabase folder, GDAL's .aux.xml). A folder is only ever removed
 *  when it is a .gdb: any other folder named as an output is left alone (the
 *  write into it then fails) rather than deleted with what it holds. */
void removeOutput(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        if (fi.suffix().compare(QLatin1String("gdb"), Qt::CaseInsensitive) == 0)
            QDir(path).removeRecursively();
        return;
    }
    if (fi.suffix().compare(QLatin1String("shp"), Qt::CaseInsensitive) == 0) {
        const QString stem = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
        for (const char *ext : {"shp", "shx", "dbf", "prj", "cpg", "qix", "sbn", "sbx"})
            QFile::remove(stem + QLatin1Char('.') + QLatin1String(ext));
    }
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".aux.xml"));
}

/*! Remove \p folder again when this export created it (\p created) and the
 *  clean-up left it empty; a folder that held anything is never touched. */
bool dropCreatedFolder(bool created, const QString &folder)
{
    if (created) QDir().rmdir(folder);   // rmdir refuses a non-empty folder
    return false;
}

/*! Undo everything written so far and record why. Returns false so callers
 *  can `return fail(...)`. */
bool failAndClean(ExportReport &rep, const QString &why)
{
    for (auto it = rep.files.crbegin(); it != rep.files.crend(); ++it)
        removeOutput(*it);
    rep.files.clear();
    rep.layers.clear();
    rep.error = why;
    return false;
}

OGRwkbGeometryType wkbFor(ExportGeometry g)
{
    switch (g) {
    case ExportGeometry::Point:      return wkbPoint;
    case ExportGeometry::LineString: return wkbLineString;
    case ExportGeometry::Polygon:    return wkbPolygon;
    }
    return wkbUnknown;
}

/*! The OGR geometry for \p row, or nullptr when it has too few points. */
std::unique_ptr<OGRGeometry> geometryFor(const ExportRow &row, ExportGeometry g)
{
    const auto &p = row.points;
    switch (g) {
    case ExportGeometry::Point:
        if (p.isEmpty()) return nullptr;
        return std::make_unique<OGRPoint>(p.first().x(), p.first().y());
    case ExportGeometry::LineString: {
        if (p.size() < 2) return nullptr;
        auto ls = std::make_unique<OGRLineString>();
        for (const QPointF &q : p) ls->addPoint(q.x(), q.y());
        return ls;
    }
    case ExportGeometry::Polygon: {
        if (p.size() < 3) return nullptr;
        auto ring = std::make_unique<OGRLinearRing>();
        for (const QPointF &q : p) ring->addPoint(q.x(), q.y());
        ring->closeRings();
        if (ring->getNumPoints() < 4) return nullptr;
        auto poly = std::make_unique<OGRPolygon>();
        poly->addRingDirectly(ring.release());
        return poly;
    }
    }
    return nullptr;
}

OGRFieldType ogrTypeFor(FieldType t)
{
    switch (t) {
    case FieldType::Integer:
    case FieldType::Boolean: return OFTInteger;
    case FieldType::Real:    return OFTReal;
    case FieldType::Text:    break;
    }
    return OFTString;
}

/*! Bridges GDAL's progress callback to ExportProgress: GDAL reports a
 *  fraction of one translate; this maps it into [base, base + span) and
 *  remembers a cancel, which GDAL only reports as a failed translate. */
struct ProgressBridge
{
    const ExportProgress *progress = nullptr;
    int base = 0, span = 100, total = 100;
    QString what;
    bool cancelled = false;
};

int CPL_STDCALL bridgeProgress(double fraction, const char *, void *data)
{
    auto *b = static_cast<ProgressBridge *>(data);
    if (!b || !b->progress || !*b->progress) return TRUE;
    const int done = b->base + int(fraction * b->span);
    if ((*b->progress)(done, b->total, b->what)) return TRUE;
    b->cancelled = true;
    return FALSE;
}

/*! argv holder for the gdal_utils option parsers. */
class Argv
{
public:
    Argv &operator<<(const QString &s) { m_bytes << s.toUtf8(); return *this; }
    char **data()
    {
        m_ptrs.clear();
        for (QByteArray &b : m_bytes) m_ptrs.push_back(b.data());
        m_ptrs.push_back(nullptr);
        return m_ptrs.data();
    }

private:
    QList<QByteArray> m_bytes;
    std::vector<char *> m_ptrs;
};

/*! The -f / CRS / -lco part every vector translate here shares. A memory
 *  source carries its CRS on its layers; a file source has its own unless
 *  the GUI layer overrides it (options.sourceSrsWkt), which is then assigned
 *  (-a_srs) or used as the source of the reprojection (-s_srs). */
void translateArgs(const gdalcaps::VectorWriteFormat &fmt, const ExportOptions &options,
                   bool sourceIsMemory, Argv &argv)
{
    argv << QStringLiteral("-f") << fmt.driver;
    const bool reproject = fmt.forcesWgs84 || !options.targetSrsWkt.isEmpty();
    if (!sourceIsMemory && !options.sourceSrsWkt.isEmpty())
        argv << (reproject ? QStringLiteral("-s_srs") : QStringLiteral("-a_srs"))
             << options.sourceSrsWkt;
    if (fmt.forcesWgs84)
        argv << QStringLiteral("-t_srs") << QStringLiteral("EPSG:4326");
    else if (!options.targetSrsWkt.isEmpty())
        argv << QStringLiteral("-t_srs") << options.targetSrsWkt;
    if (fmt.driver == QLatin1String("ESRI Shapefile"))
        argv << QStringLiteral("-lco") << QStringLiteral("ENCODING=UTF-8");
    if (fmt.driver == QLatin1String("CSV"))
        argv << QStringLiteral("-lco") << QStringLiteral("GEOMETRY=AS_WKT");
}

/*! Run GDALVectorTranslate(dest ← src) with \p argv. */
bool runTranslate(const QString &dest, GDALDataset *src, Argv &argv,
                  ProgressBridge &bridge, QString *error)
{
    GDALVectorTranslateOptions *opts = GDALVectorTranslateOptionsNew(argv.data(), nullptr);
    if (!opts) {
        if (error) *error = tr_("Invalid export options: %1").arg(lastGdalError());
        return false;
    }
    GDALVectorTranslateOptionsSetProgress(opts, bridgeProgress, &bridge);
    GDALDatasetH srcH = GDALDataset::ToHandle(src);
    int usageError = 0;
    CPLErrorReset();
    GDALDatasetH out = GDALVectorTranslate(dest.toUtf8().constData(), nullptr, 1, &srcH,
                                           opts, &usageError);
    GDALVectorTranslateOptionsFree(opts);
    if (!out) {
        if (error) *error = bridge.cancelled ? kCancelled
                                             : tr_("Could not write \"%1\": %2")
                                                   .arg(dest, lastGdalError());
        return false;
    }
    GDALClose(out);
    return true;
}

/*!
 * \brief Build \p tables in a new in-memory dataset.
 * \param names  Per table, the output field names (shortened for Shapefile).
 */
GDALDataset *buildMemory(const QVector<const ExportTable *> &tables,
                         const QVector<QStringList> &names,
                         const ExportOptions &options, bool valueLists,
                         const ExportProgress &progress, int &done, int total,
                         QString *error, bool *cancelled)
{
    GDALDriver *drv = memoryDriver();
    if (!drv) {
        if (error) *error = tr_("This GDAL build has no in-memory vector driver.");
        return nullptr;
    }
    GDALDataset *ds = drv->Create("", 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        if (error) *error = tr_("Could not create the in-memory dataset: %1").arg(lastGdalError());
        return nullptr;
    }

    std::unique_ptr<OGRSpatialReference> srs;
    if (!options.sourceSrsWkt.isEmpty()) {
        srs = std::make_unique<OGRSpatialReference>();
        srs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (srs->importFromWkt(options.sourceSrsWkt.toUtf8().constData()) != OGRERR_NONE)
            srs.reset();
    }

    for (int t = 0; t < tables.size(); ++t) {
        const ExportTable &table = *tables.at(t);
        OGRLayer *layer = ds->CreateLayer(table.name.toUtf8().constData(), srs.get(),
                                          wkbFor(table.geometry), nullptr);
        if (!layer) {
            if (error) *error = tr_("Could not create layer \"%1\": %2").arg(table.name, lastGdalError());
            GDALClose(ds);
            return nullptr;
        }
        for (int f = 0; f < table.fields.size(); ++f) {
            const ExportField &field = table.fields.at(f);
            OGRFieldDefn defn(names.at(t).at(f).toUtf8().constData(), ogrTypeFor(field.type));
            if (field.type == FieldType::Boolean) defn.SetSubType(OFSTBoolean);
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3, 7, 0)
            defn.SetComment(field.description().toStdString());
#endif
            if (valueLists && !field.choices.isEmpty() && field.type == FieldType::Text) {
                std::vector<OGRCodedValue> values;
                for (const FieldChoice &c : field.choices) {
                    OGRCodedValue cv;
                    cv.pszCode  = CPLStrdup(c.value.toUtf8().constData());
                    cv.pszValue = c.label.isEmpty() ? nullptr
                                                    : CPLStrdup(c.label.toUtf8().constData());
                    values.push_back(cv);
                }
                const std::string domain = (table.name + QLatin1Char('_') + names.at(t).at(f))
                                               .toStdString();
                std::string reason;
                if (ds->AddFieldDomain(std::make_unique<OGRCodedFieldDomain>(
                                           domain, std::string(), OFTString, OFSTNone,
                                           std::move(values)),
                                       reason))
                    defn.SetDomainName(domain);
            }
            if (layer->CreateField(&defn) != OGRERR_NONE) {
                if (error) *error = tr_("Could not create field \"%1\": %2")
                                        .arg(field.name, lastGdalError());
                GDALClose(ds);
                return nullptr;
            }
        }

        for (const ExportRow &row : table.rows) {
            std::unique_ptr<OGRGeometry> geom = geometryFor(row, table.geometry);
            if (!geom) { ++done; continue; }   // the collector reports why
            OGRFeature *feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetGeometryDirectly(geom.release());
            for (int f = 0; f < table.fields.size() && f < row.values.size(); ++f) {
                const QVariant &v = row.values.at(f);
                if (!v.isValid() || v.isNull()) { feat->SetFieldNull(f); continue; }
                switch (table.fields.at(f).type) {
                case FieldType::Integer: feat->SetField(f, v.toInt()); break;
                case FieldType::Boolean: feat->SetField(f, v.toBool() ? 1 : 0); break;
                case FieldType::Real:    feat->SetField(f, v.toDouble()); break;
                case FieldType::Text:
                    feat->SetField(f, v.toString().toUtf8().constData()); break;
                }
            }
            const OGRErr err = layer->CreateFeature(feat);
            OGRFeature::DestroyFeature(feat);
            if (err != OGRERR_NONE) {
                if (error) *error = tr_("Could not add a feature to \"%1\": %2")
                                        .arg(table.name, lastGdalError());
                GDALClose(ds);
                return nullptr;
            }
            ++done;
            if ((done & 0x3FF) == 0 && progress
                && !progress(done, total, tr_("Collecting %1…").arg(table.name))) {
                if (cancelled) *cancelled = true;
                GDALClose(ds);
                return nullptr;
            }
        }
    }
    return ds;
}

/*! Write <shp stem>_fields.csv: one line per field, short name → label, unit. */
bool writeFieldMap(const QString &shpPath, const ExportTable &table,
                   const QStringList &names, QString *csvPath)
{
    const QFileInfo fi(shpPath);
    *csvPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
               + QStringLiteral("_fields.csv");
    QFile f(*csvPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    const auto quote = [](QString s) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    };
    out << "field,name,label,unit\n";
    for (int i = 0; i < table.fields.size(); ++i) {
        const ExportField &fd = table.fields.at(i);
        out << quote(names.at(i)) << ',' << quote(fd.name) << ','
            << quote(fd.label) << ',' << quote(fd.unit) << '\n';
    }
    return true;
}

}   // namespace

// ---------------------------------------------------------------------------
// Names and paths
// ---------------------------------------------------------------------------

QStringList shortFieldNames(const QStringList &names, int maxLen)
{
    QStringList out;
    QSet<QString> used;
    for (const QString &n : names) {
        QString candidate = n.left(maxLen);
        for (int k = 1; used.contains(candidate.toLower()); ++k) {
            const QString suffix = QLatin1Char('_') + QString::number(k);
            candidate = n.left(maxLen - suffix.size()) + suffix;
        }
        used.insert(candidate.toLower());
        out << candidate;
    }
    return out;
}

QString exportPathFor(const ExportOptions &options, const QString &tableName)
{
    const gdalcaps::VectorWriteFormat fmt = gdalcaps::vectorWriteFormat(options.driver);
    if (fmt.multiLayer) return options.destination;
    return QDir(options.destination).filePath(tableName + QLatin1Char('.') + fmt.extension);
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

bool exportVectorTables(const QVector<ExportTable> &tables, const ExportOptions &options,
                        const ExportProgress &progress, ExportReport *report)
{
    ExportReport local;
    ExportReport &rep = report ? *report : local;
    rep = ExportReport{};
    gdalcaps::ensureRegistered();

    const gdalcaps::VectorWriteFormat fmt = gdalcaps::vectorWriteFormat(options.driver);
    if (fmt.driver.isEmpty())
        return failAndClean(rep, tr_("The format \"%1\" cannot be written by this build.")
                                     .arg(options.driver));
    if (options.destination.isEmpty())
        return failAndClean(rep, tr_("No destination was given."));
    if (tables.isEmpty())
        return failAndClean(rep, tr_("Nothing to export."));
    if ((fmt.forcesWgs84 || !options.targetSrsWkt.isEmpty()) && options.sourceSrsWkt.isEmpty())
        return failAndClean(rep, tr_("The data has no coordinate reference system, so it "
                                     "cannot be reprojected."));

    // Every table's field names, shortened once for Shapefile.
    QVector<QStringList> names;
    for (const ExportTable &t : tables) {
        QStringList n;
        for (const ExportField &f : t.fields) n << f.name;
        names << (fmt.shortFieldNames ? shortFieldNames(n) : n);
        for (const QString &s : t.skipped) rep.warnings << s;
    }

    int rowsTotal = 0;
    for (const ExportTable &t : tables) rowsTotal += t.rows.size();
    const int destinations = fmt.multiLayer ? 1 : int(tables.size());
    const int total = rowsTotal + 100 * destinations;
    int done = 0;

    const bool madeFolder = !fmt.multiLayer && !QFileInfo(options.destination).isDir();
    if (fmt.multiLayer) {
        QDir().mkpath(QFileInfo(options.destination).absolutePath());
    } else if (!QDir().mkpath(options.destination)) {
        return failAndClean(rep, tr_("Could not create the folder \"%1\".").arg(options.destination));
    }

    const auto writeOne = [&](const QVector<const ExportTable *> &batch,
                              const QVector<QStringList> &batchNames,
                              const QString &dest) -> bool {
        QString err;
        bool cancelled = false;
        GDALDataset *mem = buildMemory(batch, batchNames, options, fmt.keepsValueLists,
                                       progress, done, total, &err, &cancelled);
        if (!mem) return failAndClean(rep, cancelled ? kCancelled : err);

        removeOutput(dest);   // replace: the dialog asked before overwriting
        Argv argv;
        translateArgs(fmt, options, /*sourceIsMemory=*/true, argv);
        ProgressBridge bridge{&progress, done, 100, total,
                              tr_("Writing %1…").arg(QFileInfo(dest).fileName())};
        rep.files << dest;   // listed first, so a failure part-way removes it
        const bool ok = runTranslate(dest, mem, argv, bridge, &err);
        GDALClose(mem);
        if (!ok) return failAndClean(rep, err);
        done += 100;
        if (progress && !progress(done, total, QFileInfo(dest).fileName()))
            return failAndClean(rep, kCancelled);
        return true;
    };

    if (fmt.multiLayer) {
        QVector<const ExportTable *> all;
        for (const ExportTable &t : tables) all << &t;
        if (!writeOne(all, names, options.destination)) return false;
        for (const ExportTable &t : tables) rep.layers << qMakePair(options.destination, t.name);
    } else {
        for (int i = 0; i < tables.size(); ++i) {
            const QString dest = exportPathFor(options, tables.at(i).name);
            if (!writeOne({&tables.at(i)}, {names.at(i)}, dest))
                return dropCreatedFolder(madeFolder, options.destination);
            rep.layers << qMakePair(dest, QString());
            if (fmt.shortFieldNames) {
                QString csv;
                if (writeFieldMap(dest, tables.at(i), names.at(i), &csv)) rep.files << csv;
                else rep.warnings << tr_("Could not write the field map %1.").arg(csv);
            }
        }
    }
    if (progress) progress(total, total, tr_("Done"));
    return true;
}

// ---------------------------------------------------------------------------
// Vector and feature layers
// ---------------------------------------------------------------------------

bool exportVectorLayer(const VectorLayerSource &source, const ExportOptions &options,
                       const ExportProgress &progress, ExportReport *report)
{
    ExportReport local;
    ExportReport &rep = report ? *report : local;
    rep = ExportReport{};
    gdalcaps::ensureRegistered();

    const gdalcaps::VectorWriteFormat fmt = gdalcaps::vectorWriteFormat(options.driver);
    if (fmt.driver.isEmpty())
        return failAndClean(rep, tr_("The format \"%1\" cannot be written by this build.")
                                     .arg(options.driver));
    if (options.destination.isEmpty())
        return failAndClean(rep, tr_("No destination was given."));

    CPLErrorReset();
    auto *src = static_cast<GDALDataset *>(GDALOpenEx(
        source.path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
        nullptr, nullptr, nullptr));
    if (!src)
        return failAndClean(rep, tr_("Could not open \"%1\": %2").arg(source.path, lastGdalError()));
    OGRLayer *layer = source.layerName.isEmpty()
                          ? src->GetLayer(0)
                          : src->GetLayerByName(source.layerName.toUtf8().constData());
    if (!layer) {
        GDALClose(src);
        return failAndClean(rep, tr_("\"%1\" has no layer \"%2\".").arg(source.path, source.layerName));
    }
    const QString layerName = QString::fromUtf8(layer->GetName());
    const QString outName = source.outputName.isEmpty() ? layerName : source.outputName;

    // GIS/feature layers need the same Shapefile field map as collected model
    // tables. Snapshot descriptions before closing the source, then read the
    // names the output driver actually chose (including collision suffixes).
    ExportTable fieldMap;
    if (fmt.shortFieldNames) {
        const OGRFeatureDefn *def = layer->GetLayerDefn();
        for (int i = 0; i < def->GetFieldCount(); ++i) {
            const auto *fd = def->GetFieldDefn(i);
            ExportField f;
            f.name = QString::fromUtf8(fd->GetNameRef());
#if GDAL_VERSION_NUM >= 3080000
            f.label = QString::fromStdString(fd->GetComment());
#endif
            if (f.label.isEmpty()) f.label = f.name;
            const int unitStart = f.label.lastIndexOf(QStringLiteral(" ("));
            if (unitStart >= 0 && f.label.endsWith(QLatin1Char(')'))) {
                f.unit = f.label.mid(unitStart + 2, f.label.size() - unitStart - 3);
                f.label.truncate(unitStart);
            }
            fieldMap.fields << f;
        }
    }

    QString err;
    Argv argv;
    translateArgs(fmt, options, /*sourceIsMemory=*/false, argv);
    if (!source.fids.isEmpty()) {
        QStringList ids;
        for (qint64 f : source.fids) ids << QString::number(f);
        QString quoted = layerName;
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        argv << QStringLiteral("-sql")
             << QStringLiteral("SELECT * FROM \"%1\" WHERE FID IN (%2)")
                    .arg(quoted, ids.join(QLatin1Char(',')))
             << QStringLiteral("-dialect") << QStringLiteral("OGRSQL");
    }
    argv << QStringLiteral("-nln") << outName;
    if (source.fids.isEmpty()) argv << layerName;   // positional: this layer only

    QString dest = options.destination;
    const bool madeFolder = !fmt.multiLayer && !QFileInfo(options.destination).isDir();
    if (fmt.multiLayer) {
        QDir().mkpath(QFileInfo(dest).absolutePath());
    } else {
        if (!QDir().mkpath(options.destination)) {
            GDALClose(src);
            return failAndClean(rep, tr_("Could not create the folder \"%1\".").arg(options.destination));
        }
        dest = exportPathFor(options, outName);
    }
    removeOutput(dest);
    ProgressBridge bridge{&progress, 0, 100, 100,
                          tr_("Writing %1…").arg(QFileInfo(dest).fileName())};
    rep.files << dest;
    const bool ok = runTranslate(dest, src, argv, bridge, &err);
    GDALClose(src);
    if (!ok || (progress && !progress(100, 100, tr_("Done")))) {
        failAndClean(rep, ok ? kCancelled : err);
        return dropCreatedFolder(madeFolder, options.destination);
    }
    if (fmt.shortFieldNames) {
        QStringList writtenNames;
        auto *written = static_cast<GDALDataset *>(GDALOpenEx(
            dest.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr));
        if (written) {
            if (auto *outLayer = written->GetLayer(0)) {
                const auto *def = outLayer->GetLayerDefn();
                for (int i = 0; i < def->GetFieldCount(); ++i)
                    writtenNames << QString::fromUtf8(def->GetFieldDefn(i)->GetNameRef());
            }
            GDALClose(written);
        }
        QString csv;
        if (writtenNames.size() == fieldMap.fields.size()
            && writeFieldMap(dest, fieldMap, writtenNames, &csv)) rep.files << csv;
        else rep.warnings << tr_("Could not write the field map for %1.").arg(dest);
    }
    rep.layers << qMakePair(dest, fmt.multiLayer ? outName : QString());
    return true;
}

// ---------------------------------------------------------------------------
// Rasters
// ---------------------------------------------------------------------------

bool exportRaster(const QString &sourcePath, const QString &destination,
                  const QString &targetSrsWkt, const ExportProgress &progress,
                  ExportReport *report)
{
    ExportReport local;
    ExportReport &rep = report ? *report : local;
    rep = ExportReport{};
    gdalcaps::ensureRegistered();
    if (!gdalcaps::driverAvailable("GTiff"))
        return failAndClean(rep, tr_("This GDAL build cannot write GeoTIFF."));
    if (destination.isEmpty())
        return failAndClean(rep, tr_("No destination was given."));

    CPLErrorReset();
    GDALDatasetH src = GDALOpenEx(sourcePath.toUtf8().constData(),
                                  GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
    if (!src)
        return failAndClean(rep, tr_("Could not open \"%1\": %2").arg(sourcePath, lastGdalError()));

    QDir().mkpath(QFileInfo(destination).absolutePath());
    removeOutput(destination);
    rep.files << destination;

    Argv argv;
    argv << QStringLiteral("-of") << QStringLiteral("GTiff")
         << QStringLiteral("-co") << QStringLiteral("COMPRESS=DEFLATE")
         << QStringLiteral("-co") << QStringLiteral("TILED=YES")
         << QStringLiteral("-co") << QStringLiteral("BIGTIFF=IF_SAFER");
    ProgressBridge bridge{&progress, 0, 100, 100,
                          tr_("Writing %1…").arg(QFileInfo(destination).fileName())};
    int usageError = 0;
    GDALDatasetH out = nullptr;
    CPLErrorReset();
    if (targetSrsWkt.isEmpty()) {
        GDALTranslateOptions *opts = GDALTranslateOptionsNew(argv.data(), nullptr);
        if (opts) {
            GDALTranslateOptionsSetProgress(opts, bridgeProgress, &bridge);
            out = GDALTranslate(destination.toUtf8().constData(), src, opts, &usageError);
            GDALTranslateOptionsFree(opts);
        }
    } else {
        // Nearest-neighbour (GDAL's default): a warp must not invent values,
        // and the raster may be categorical.
        argv << QStringLiteral("-t_srs") << targetSrsWkt;
        GDALWarpAppOptions *opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
        if (opts) {
            GDALWarpAppOptionsSetProgress(opts, bridgeProgress, &bridge);
            out = GDALWarp(destination.toUtf8().constData(), nullptr, 1, &src, opts, &usageError);
            GDALWarpAppOptionsFree(opts);
        }
    }
    const QString why = lastGdalError();
    GDALClose(src);
    if (!out) {
        return failAndClean(rep, bridge.cancelled
                                     ? kCancelled
                                     : tr_("Could not write \"%1\": %2").arg(destination, why));
    }
    GDALClose(out);
    if (progress && !progress(100, 100, tr_("Done"))) return failAndClean(rep, kCancelled);
    rep.layers << qMakePair(destination, QString());
    return true;
}

}   // namespace openswmmvis::io
