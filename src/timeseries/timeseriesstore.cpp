/**
 * @file timeseriesstore.cpp
 * @brief Source-neutral, atomic observation imports and indexed GeoPackage readers.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "timeseries/timeseriesstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QUuid>
#include <cmath>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <stdexcept>

namespace OpenSWMM::TimeSeries
{
namespace
{
using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
using Feature = std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;
constexpr int maxMetadata = 100000;
constexpr qint64 maxObservations = 20000000;

/** @brief Raise a storage/validation failure caught by the public boundary. */
void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toUtf8().constData());
}
/** @brief Stop before the next bounded IO operation when cancellation is requested. */
void checkCancel(std::atomic_bool *cancel, const std::function<bool()> &cancelled = {})
{
    require((!cancel || !cancel->load()) && (!cancelled || !cancelled()),
            QStringLiteral("Time-series import/read cancelled."));
}
/** @brief Return compact JSON without modifying source values. */
QString json(const QJsonObject &value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
/** @brief Decode strict object JSON, allowing an explicitly empty optional field. */
QJsonObject object(const QString &value)
{
    if (value.isEmpty())
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(value.toUtf8(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            QStringLiteral("Expected a valid JSON object."));
    return document.object();
}
/** @brief Quote an SQL string literal; table/column identifiers are constant. */
QString literal(QString value)
{
    value.replace('\'', QStringLiteral("''"));
    return '\'' + value + '\'';
}
/** @brief Read one named UTF-8 field. */
QString text(const OGRFeature *feature, const char *name)
{
    return QString::fromUtf8(feature->GetFieldAsString(name));
}
/** @brief Store a copied UTF-8 string. */
void set(OGRFeature *feature, const char *name, const QString &value)
{
    feature->SetField(name, value.toUtf8().constData());
}
/** @brief Own a newly allocated feature until insertion has completed. */
Feature feature(OGRLayer *layer)
{
    return {OGRFeature::CreateFeature(layer->GetLayerDefn()), OGRFeature::DestroyFeature};
}
/** @brief Add a checked schema field. */
void field(OGRLayer *layer, const char *name, OGRFieldType type)
{
    OGRFieldDefn definition(name, type);
    require(layer->CreateField(&definition) == OGRERR_NONE, "Cannot create time-series field.");
}
/** @brief Return a required layer/table. */
OGRLayer *table(GDALDataset *dataset, const char *name)
{
    auto *layer = dataset->GetLayerByName(name);
    require(layer, QStringLiteral("Missing time-series table: %1").arg(name));
    return layer;
}
/** @brief Create a registered nonspatial GeoPackage attribute table. */
OGRLayer *attributes(GDALDataset *dataset, const char *name)
{
    auto *layer = dataset->CreateLayer(name, nullptr, wkbNone, nullptr);
    require(layer, "Cannot create time-series table.");
    return layer;
}
/** @brief Insert one complete row and reject any storage error. */
void insert(OGRLayer *layer, OGRFeature *row)
{
    require(layer->CreateFeature(row) == OGRERR_NONE, "Cannot write time-series row.");
}
/** @brief Execute schema SQL and check driver diagnostics. */
void execute(GDALDataset *dataset, const QString &sql)
{
    CPLErrorReset();
    auto *result = dataset->ExecuteSQL(sql.toUtf8().constData(), nullptr, "SQLITE");
    if (result)
        dataset->ReleaseResultSet(result);
    require(CPLGetLastErrorType() < CE_Failure, QString::fromUtf8(CPLGetLastErrorMsg()));
}
/** @brief Open a local regular GeoPackage with a restrictive driver allowlist. */
Dataset openPackage(const QString &path)
{
    require(QFileInfo(path).isFile(), "The time-series package does not exist.");
    GDALAllRegister();
    const char *drivers[]{"GPKG", nullptr};
    Dataset dataset(static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                                          GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                          drivers, nullptr, nullptr)),
                    GDALClose);
    require(bool(dataset), "Cannot open the time-series GeoPackage.");
    return dataset;
}
/** @brief Read and validate the one-row native schema metadata. */
QJsonObject metadata(GDALDataset *dataset)
{
    auto *layer = table(dataset, "ts_dataset");
    layer->ResetReading();
    Feature row(layer->GetNextFeature(), OGRFeature::DestroyFeature);
    require(bool(row), "The time-series dataset metadata is missing.");
    const auto value = object(text(row.get(), "metadata"));
    require(value.value("schema").toString() == "openswmm.timeseries-dataset/1",
            "Unsupported time-series dataset schema.");
    require(!QUuid(value.value("id").toString()).isNull(), "Invalid dataset identity.");
    return value;
}
/** @brief Strict bounded RFC4180 reader with buffered bytes and incremental source hashing. */
class CsvReader
{
public:
    /** @brief Open a local CSV; no network paths or GDAL drivers are involved. */
    explicit CsvReader(const QString &path) : file(path), hash(QCryptographicHash::Sha256)
    {
        require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly),
                QStringLiteral("Cannot open observation CSV: %1").arg(path));
    }
    /** @brief Read one logical record including quoted newlines; false means clean EOF. */
    bool next(QStringList &cells, std::atomic_bool *cancel, const std::function<bool()> &cancelled)
    {
        cells.clear();
        QByteArray cell;
        bool quoted = false, closed = false, started = false;
        qsizetype bytes = 0;
        const auto append = [&] {
            QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
            const QString decoded = decoder.decode(cell);
            require(!decoder.hasError(), "Observation CSV is not valid UTF-8.");
            cells.append(decoded);
            require(cells.size() <= 512, "Observation CSV has too many columns.");
            cell.clear();
        };
        for (;;) {
            if ((bytes % 65536) == 0)
                checkCancel(cancel, cancelled);
            const int ch = get();
            if (ch < 0) {
                require(!quoted, "Observation CSV ends inside a quoted field.");
                if (!started)
                    return false;
                append();
                return true;
            }
            started = true;
            require(++bytes <= 4 * 1024 * 1024, "Observation CSV record exceeds 4 MiB.");
            if (quoted) {
                if (ch == '"') {
                    if (peek() == '"') {
                        get();
                        cell += '"';
                    } else {
                        quoted = false;
                        closed = true;
                    }
                } else
                    cell += char(ch);
                continue;
            }
            if (ch == ',') {
                append();
                closed = false;
            } else if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && peek() == '\n')
                    get();
                append();
                return true;
            } else if (ch == '"') {
                require(cell.isEmpty() && !closed, "Unexpected quote in observation CSV.");
                quoted = true;
            } else {
                require(!closed, "Unexpected content after a quoted CSV field.");
                cell += char(ch);
            }
        }
    }
    /** @brief Hash all bytes consumed through EOF. */
    QByteArray digest() { return hash.result(); }

private:
    /** @brief Refill a bounded input buffer and distinguish IO failure from EOF. */
    bool fill()
    {
        if (offset < buffer.size())
            return true;
        buffer = file.read(65536);
        offset = 0;
        require(file.error() == QFileDevice::NoError, "Cannot read observation CSV.");
        if (!buffer.isEmpty())
            hash.addData(buffer);
        return !buffer.isEmpty();
    }
    /** @brief Inspect the next byte without consuming it. */
    int peek() { return fill() ? static_cast<unsigned char>(buffer[offset]) : -1; }
    /** @brief Consume the next byte, returning -1 only at clean EOF. */
    int get()
    {
        const int value = peek();
        if (value >= 0)
            ++offset;
        return value;
    }
    QFile file;              ///< Source file owned by the worker.
    QCryptographicHash hash; ///< Fingerprint including quoted metadata and line endings.
    QByteArray buffer;       ///< Bounded source bytes.
    qsizetype offset = 0;    ///< Cursor into buffer.
};

/** @brief Serialize homogeneous series metadata without observation materialization. */
QJsonObject seriesJson(const SeriesInfo &s)
{
    return {{"id", s.id},
            {"title", s.title},
            {"provider", s.provider},
            {"collection", s.collection},
            {"siteId", s.siteId},
            {"parameter", s.parameter},
            {"sourceSeriesId", s.sourceSeriesId},
            {"statistic", s.statistic},
            {"unit", s.unit},
            {"locationId", s.locationId},
            {"timeBasis", s.timeBasis == TimeBasis::Instant ? "instant" : "calendar-date"},
            {"count", QString::number(s.count)},
            {"validCount", QString::number(s.validCount)},
            {"firstKey", QString::number(s.firstKey)},
            {"lastKey", QString::number(s.lastKey)},
            {"attributes", s.attributes}};
}
/** @brief Decode checked persisted series metadata. */
SeriesInfo readSeries(const QJsonObject &v)
{
    SeriesInfo s;
    s.id = v["id"].toString();
    s.title = v["title"].toString();
    s.provider = v["provider"].toString();
    s.collection = v["collection"].toString();
    s.siteId = v["siteId"].toString();
    s.parameter = v["parameter"].toString();
    s.sourceSeriesId = v["sourceSeriesId"].toString();
    s.statistic = v["statistic"].toString();
    s.unit = v["unit"].toString();
    s.locationId = v["locationId"].toString();
    const auto basis = v["timeBasis"].toString();
    require(basis == "instant" || basis == "calendar-date", "Invalid stored time basis.");
    s.timeBasis = basis == "instant" ? TimeBasis::Instant : TimeBasis::CalendarDate;
    bool ok[4]{};
    s.count = v["count"].toString().toLongLong(&ok[0]);
    s.validCount = v["validCount"].toString().toLongLong(&ok[1]);
    s.firstKey = v["firstKey"].toString().toLongLong(&ok[2]);
    s.lastKey = v["lastKey"].toString().toLongLong(&ok[3]);
    require(!QUuid(s.id).isNull() && ok[0] && ok[1] && ok[2] && ok[3] && s.count > 0 &&
                s.count <= maxObservations && s.validCount >= 0 && s.validCount <= s.count &&
                s.firstKey <= s.lastKey,
            "Invalid series metadata.");
    s.attributes = v["attributes"].toObject();
    return s;
}
}

bool TimeSeriesStore::importDescriptor(const QString &descriptorPath, const QString &destination,
                                       QString *error, std::atomic_bool *cancel,
                                       std::function<bool()> cancelled)
{
    if (error)
        error->clear();
    QString staging;
    try {
        checkCancel(cancel, cancelled);
        require(!QFileInfo::exists(destination), "The destination already exists.");
        QFile descriptor(descriptorPath);
        require(QFileInfo(descriptorPath).isFile() && descriptor.open(QIODevice::ReadOnly) &&
                    descriptor.size() <= 8 * 1024 * 1024,
                "Cannot read bounded local import descriptor.");
        const auto bytes = descriptor.readAll();
        QJsonParseError descriptorError;
        const auto document = QJsonDocument::fromJson(bytes, &descriptorError);
        require(descriptorError.error == QJsonParseError::NoError && document.isObject(),
                "The import descriptor is not valid UTF-8 JSON.");
        const auto spec = document.object();
        require(spec["schema"].toString() == "openswmm.timeseries-import/1",
                "Unsupported time-series import descriptor.");
        const QUuid datasetId(spec["datasetId"].toString());
        require(!datasetId.isNull(), "The import descriptor requires a dataset UUID.");
        const auto sources = spec["sources"].toArray();
        require(!sources.isEmpty() && sources.size() <= 1000, "Invalid number of CSV sources.");
        const auto locationSpecs = spec["locations"].toArray();
        require(locationSpecs.size() <= maxMetadata, "Too many monitoring locations.");
        require(QDir().mkpath(QFileInfo(destination).absolutePath()),
                "Cannot create dataset directory.");
        staging = destination + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                  ".partial.gpkg";
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        require(driver, "The GeoPackage driver is unavailable.");
        Dataset dataset(driver->Create(staging.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr),
                        GDALClose);
        require(bool(dataset), "Cannot create the time-series GeoPackage.");
        auto *meta = attributes(dataset.get(), "ts_dataset");
        field(meta, "metadata", OFTString);
        auto *seriesTable = attributes(dataset.get(), "ts_series");
        field(seriesTable, "series_id", OFTString);
        field(seriesTable, "metadata", OFTString);
        auto *samples = attributes(dataset.get(), "ts_observations");
        for (const auto *name : {"series_id", "observation_id", "source_time", "approval",
                                 "qualifier", "properties", "geometry", "row_hash"})
            field(samples, name, OFTString);
        field(samples, "time_key", OFTInteger64);
        field(samples, "value", OFTReal);
        OGRSpatialReference srs;
        srs.importFromEPSG(4326);
        srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *locations = dataset->CreateLayer("ts_locations", &srs, wkbPoint, nullptr);
        require(locations, "Cannot create monitoring locations.");
        for (const auto *name : {"location_id", "title", "attributes"})
            field(locations, name, OFTString);
        execute(
            dataset.get(),
            "CREATE UNIQUE INDEX ts_observations_series_time ON ts_observations(series_id,time_key)");
        execute(dataset.get(), "CREATE UNIQUE INDEX ts_series_identity ON ts_series(series_id)");
        require(dataset->StartTransaction() == OGRERR_NONE, "Cannot start dataset transaction.");
        QSet<QString> locationIds;
        for (const auto &entry : locationSpecs) {
            checkCancel(cancel, cancelled);
            const auto value = entry.toObject();
            const auto id = value["id"].toString();
            require(!id.isEmpty() && !locationIds.contains(id),
                    "Empty or duplicate location identity.");
            locationIds.insert(id);
            auto row = feature(locations);
            set(row.get(), "location_id", id);
            set(row.get(), "title", value["title"].toString());
            set(row.get(), "attributes", json(value["attributes"].toObject()));
            const auto geometry = value["geometry"].toObject();
            if (!geometry.isEmpty()) {
                const auto coordinates = geometry["coordinates"].toArray();
                require(geometry["type"].toString() == "Point" && coordinates.size() >= 2 &&
                            coordinates[0].isDouble() && coordinates[1].isDouble(),
                        "Location must be an RFC7946 point.");
                const double x = coordinates[0].toDouble(), y = coordinates[1].toDouble();
                require(std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 180 &&
                            std::abs(y) <= 90,
                        "Location coordinates are outside longitude/latitude limits.");
                OGRPoint point(x, y);
                row->SetGeometry(&point);
            }
            insert(locations, row.get());
        }
        QMap<QString, SeriesInfo> series;
        QCryptographicHash fingerprint(QCryptographicHash::Sha256);
        fingerprint.addData(bytes);
        qint64 total = 0;
        for (const auto &sourceValue : sources) {
            checkCancel(cancel, cancelled);
            const auto source = sourceValue.toObject(), columns = source["columns"].toObject(),
                       defaults = source["defaults"].toObject();
            const auto filePath = QDir(QFileInfo(descriptorPath).absolutePath())
                                      .absoluteFilePath(source["path"].toString());
            require(!source["path"].toString().isEmpty(), "CSV source path is missing.");
            CsvReader csv(filePath);
            QStringList headers, cells;
            require(csv.next(headers, cancel, cancelled), "CSV header is missing.");
            if (!headers.isEmpty() && headers[0].startsWith(QChar(0xfeff)))
                headers[0].remove(0, 1);
            QSet<QString> headerSet(headers.begin(), headers.end());
            require(headerSet.size() == headers.size() && !headerSet.contains(QString{}),
                    "CSV headers must be unique and nonempty.");
            QMap<QString, int> indices;
            for (auto it = columns.begin(); it != columns.end(); ++it) {
                const int index = headers.indexOf(it.value().toString());
                require(index >= 0,
                        QStringLiteral("Missing mapped CSV column: %1").arg(it.value().toString()));
                indices[it.key()] = index;
            }
            require(indices.contains("time") && indices.contains("value"),
                    "Map CSV time and value columns explicitly.");
            const auto get = [&](const QString &name) {
                return indices.contains(name) ? cells[indices[name]] : defaults[name].toString();
            };
            while (csv.next(cells, cancel, cancelled)) {
                if (cells.size() == 1 && cells[0].isEmpty())
                    continue;
                require(cells.size() == headers.size(),
                        "Observation CSV row has the wrong number of columns.");
                checkCancel(cancel, cancelled);
                const auto time = get("time"), basis = get("timeBasis");
                require(basis == "instant" || basis == "calendar-date",
                        "Specify an explicit instant or calendar-date time basis.");
                qint64 timeKey = 0;
                if (basis == "calendar-date") {
                    const auto date = QDate::fromString(time, Qt::ISODate);
                    require(time.size() == 10 && date.isValid(),
                            "Invalid calendar-date observation.");
                    timeKey = date.toJulianDay();
                } else {
                    static const QRegularExpression offset("(?:Z|[+-][0-9]{2}:[0-9]{2})$");
                    const auto dateTime = QDateTime::fromString(time, Qt::ISODateWithMs);
                    require(dateTime.isValid() && offset.match(time).hasMatch(),
                            "An instant requires an explicit UTC offset.");
                    timeKey = dateTime.toMSecsSinceEpoch();
                }
                const auto valueText = get("value");
                const bool hasValue = !valueText.isEmpty();
                bool numeric = false;
                const double number = valueText.toDouble(&numeric);
                require(!hasValue || (numeric && std::isfinite(number)),
                        "Observation value must be finite or empty.");
                const auto sourceId = get("sourceSeriesId").isEmpty() ? QStringLiteral("series")
                                                                      : get("sourceSeriesId");
                const auto key =
                    QJsonDocument(QJsonArray{get("provider"), get("collection"), get("siteId"),
                                             get("parameter"), sourceId, get("statistic"), basis})
                        .toJson(QJsonDocument::Compact);
                const auto id = QUuid::createUuidV5(datasetId, key).toString(QUuid::WithoutBraces);
                if (!series.contains(id)) {
                    require(series.size() < maxMetadata, "Too many distinct observation series.");
                    SeriesInfo info;
                    info.id = id;
                    info.title = get("title");
                    info.provider = get("provider");
                    info.collection = get("collection");
                    info.siteId = get("siteId");
                    info.parameter = get("parameter");
                    info.sourceSeriesId = sourceId;
                    info.statistic = get("statistic");
                    info.unit = get("unit");
                    info.locationId = get("locationId");
                    if (info.locationId.isEmpty() && locationIds.contains(info.siteId))
                        info.locationId = info.siteId;
                    require(info.locationId.isEmpty() || locationIds.contains(info.locationId),
                            "Series refers to an unknown location.");
                    info.timeBasis =
                        basis == "instant" ? TimeBasis::Instant : TimeBasis::CalendarDate;
                    info.firstKey = info.lastKey = timeKey;
                    info.attributes = source["metadata"].toObject();
                    if (info.title.isEmpty())
                        info.title = QStringList{info.siteId, info.parameter, info.statistic}
                                         .join(" ")
                                         .trimmed();
                    if (info.title.isEmpty())
                        info.title = sourceId;
                    series.insert(id, info);
                }
                auto &info = series[id];
                require(info.unit == get("unit"),
                        "A source series changes units; split it explicitly before import.");
                const auto properties = object(get("properties")),
                           geometry = object(get("geometry"));
                const auto rowContent =
                    QJsonDocument(
                        QJsonObject{{"value", hasValue ? QJsonValue(number) : QJsonValue()},
                                    {"approval", get("approval")},
                                    {"qualifier", get("qualifier")},
                                    {"properties", properties},
                                    {"geometry", geometry},
                                    {"observationId", get("observationId")}})
                        .toJson(QJsonDocument::Compact);
                const auto rowHash = QString::fromLatin1(
                    QCryptographicHash::hash(rowContent, QCryptographicHash::Sha256).toHex());
                // A strictly newer key cannot duplicate any row already accepted for this
                // series. Sorted downloads take this path; arbitrary order and repeats still
                // use the persistent unique index and compare the complete source row hash.
                if (info.count > 0 && timeKey <= info.lastKey) {
                    const auto sql =
                        QStringLiteral(
                            "SELECT row_hash FROM ts_observations WHERE series_id=%1 AND time_key=%2")
                            .arg(literal(id))
                            .arg(timeKey);
                    auto *existing =
                        dataset->ExecuteSQL(sql.toUtf8().constData(), nullptr, "SQLITE");
                    require(existing, "Cannot check observation identity.");
                    Feature prior(existing->GetNextFeature(), OGRFeature::DestroyFeature);
                    const auto priorHash = prior ? text(prior.get(), "row_hash") : QString{};
                    prior.reset();
                    dataset->ReleaseResultSet(existing);
                    if (!priorHash.isEmpty()) {
                        require(priorHash == rowHash,
                                "Conflicting observations share a series and timestamp.");
                        continue;
                    }
                }
                require(++total <= maxObservations,
                        "The collection exceeds 20 million observations.");
                auto row = feature(samples);
                set(row.get(), "series_id", id);
                set(row.get(), "source_time", time);
                set(row.get(), "observation_id", get("observationId"));
                set(row.get(), "approval", get("approval"));
                set(row.get(), "qualifier", get("qualifier"));
                set(row.get(), "properties", json(properties));
                set(row.get(), "geometry", json(geometry));
                set(row.get(), "row_hash", rowHash);
                row->SetField("time_key", static_cast<GIntBig>(timeKey));
                if (hasValue)
                    row->SetField("value", number);
                else
                    row->SetFieldNull(row->GetFieldIndex("value"));
                insert(samples, row.get());
                ++info.count;
                if (hasValue)
                    ++info.validCount;
                info.firstKey = qMin(info.firstKey, timeKey);
                info.lastKey = qMax(info.lastKey, timeKey);
            }
            fingerprint.addData(csv.digest());
        }
        require(total > 0, "No observations were supplied; no layer was created.");
        for (const auto &info : series) {
            auto row = feature(seriesTable);
            set(row.get(), "series_id", info.id);
            set(row.get(), "metadata", json(seriesJson(info)));
            insert(seriesTable, row.get());
        }
        auto row = feature(meta);
        set(row.get(), "metadata",
            json({{"schema", "openswmm.timeseries-dataset/1"},
                  {"id", datasetId.toString(QUuid::WithoutBraces)},
                  {"title", spec["title"]},
                  {"fingerprint", QString::fromLatin1(fingerprint.result().toHex())},
                  {"provenance", spec["provenance"]}}));
        insert(meta, row.get());
        checkCancel(cancel, cancelled);
        require(dataset->CommitTransaction() == OGRERR_NONE, "Cannot commit time-series dataset.");
        dataset.reset();
        checkCancel(cancel, cancelled);
        require(!QFileInfo::exists(destination) && QFile::rename(staging, destination),
                "Cannot publish time-series dataset.");
        return true;
    } catch (const std::exception &failure) {
        if (error)
            *error = QString::fromUtf8(failure.what());
        if (!staging.isEmpty()) {
            QFile::remove(staging);
            QFile::remove(staging + "-wal");
            QFile::remove(staging + "-shm");
            QFile::remove(staging + "-journal");
        }
        return false;
    }
}

std::shared_ptr<const TimeSeriesDataset> TimeSeriesStore::open(const QString &path, QString *error)
{
    if (error)
        error->clear();
    try {
        auto dataset = openPackage(path);
        const auto meta = metadata(dataset.get());
        auto result = std::make_shared<TimeSeriesDataset>();
        result->path = QFileInfo(path).absoluteFilePath();
        result->id = meta["id"].toString();
        result->title = meta["title"].toString();
        result->fingerprint = meta["fingerprint"].toString();
        result->provenance = meta["provenance"].toObject();
        auto *series = table(dataset.get(), "ts_series");
        series->ResetReading();
        QSet<QString> identities;
        while (Feature row{series->GetNextFeature(), OGRFeature::DestroyFeature}) {
            require(result->series.size() < maxMetadata, "Too many stored series.");
            const auto info = readSeries(object(text(row.get(), "metadata")));
            require(!identities.contains(info.id) && text(row.get(), "series_id") == info.id,
                    "Invalid stored series identity.");
            identities.insert(info.id);
            result->series.append(info);
        }
        auto *locations = table(dataset.get(), "ts_locations");
        locations->ResetReading();
        identities.clear();
        while (Feature row{locations->GetNextFeature(), OGRFeature::DestroyFeature}) {
            require(result->locations.size() < maxMetadata, "Too many stored locations.");
            LocationInfo info;
            info.id = text(row.get(), "location_id");
            info.title = text(row.get(), "title");
            require(!info.id.isEmpty() && !identities.contains(info.id),
                    "Invalid stored location identity.");
            identities.insert(info.id);
            info.attributes = object(text(row.get(), "attributes"));
            if (const auto *geometry = row->GetGeometryRef()) {
                require(wkbFlatten(geometry->getGeometryType()) == wkbPoint,
                        "Invalid stored location geometry.");
                const auto *point = geometry->toPoint();
                info.point = QPointF(point->getX(), point->getY());
                require(std::isfinite(info.point.x()) && std::isfinite(info.point.y()) &&
                            std::abs(info.point.x()) <= 180 && std::abs(info.point.y()) <= 90,
                        "Invalid stored location coordinates.");
                info.hasGeometry = true;
            }
            result->locations.append(info);
        }
        for (const auto &s : result->series)
            require(s.locationId.isEmpty() || identities.contains(s.locationId),
                    "Stored series has a missing location.");
        table(dataset.get(), "ts_observations");
        return result;
    } catch (const std::exception &failure) {
        if (error)
            *error = QString::fromUtf8(failure.what());
        return {};
    }
}

ObservationPage TimeSeriesStore::readPage(const QString &path, const QString &seriesId,
                                          const ObservationQuery &query, QString *error,
                                          std::atomic_bool *cancel, std::function<bool()> cancelled)
{
    if (error)
        error->clear();
    ObservationPage page;
    page.nextKey = query.afterKey;
    try {
        checkCancel(cancel, cancelled);
        require(query.limit > 0 && query.fromKey <= query.toKey,
                "Invalid observation query range or limit.");
        auto dataset = openPackage(path);
        const auto source = metadata(dataset.get());
        require((query.expectedDatasetId.isEmpty() ||
                 source["id"].toString() == query.expectedDatasetId) &&
                    (query.expectedFingerprint.isEmpty() ||
                     source["fingerprint"].toString() == query.expectedFingerprint),
                "The observation dataset changed. Reload it before reading more samples.");
        const int limit = qMin(query.limit, 10000);
        const auto sql =
            QStringLiteral(
                "SELECT * FROM ts_observations WHERE series_id=%1 AND time_key>%2 AND time_key>=%3 AND time_key<=%4 ORDER BY time_key LIMIT %5")
                .arg(literal(seriesId))
                .arg(query.afterKey)
                .arg(query.fromKey)
                .arg(query.toKey)
                .arg(limit + 1);
        auto *rows = dataset->ExecuteSQL(sql.toUtf8().constData(), nullptr, "SQLITE");
        require(rows, "Cannot read observation page.");
        try {
            while (Feature row{rows->GetNextFeature(), OGRFeature::DestroyFeature}) {
                checkCancel(cancel, cancelled);
                if (page.observations.size() == limit) {
                    page.hasMore = true;
                    break;
                }
                Observation value;
                value.id = text(row.get(), "observation_id");
                value.sourceTime = text(row.get(), "source_time");
                value.timeKey = row->GetFieldAsInteger64("time_key");
                value.hasValue = row->IsFieldSetAndNotNull(row->GetFieldIndex("value"));
                if (value.hasValue) {
                    value.value = row->GetFieldAsDouble("value");
                    require(std::isfinite(value.value), "Invalid stored sample value.");
                }
                value.approval = text(row.get(), "approval");
                value.qualifier = text(row.get(), "qualifier");
                value.properties = object(text(row.get(), "properties"));
                value.geometry = object(text(row.get(), "geometry"));
                page.nextKey = value.timeKey;
                page.observations.append(value);
            }
        } catch (...) {
            dataset->ReleaseResultSet(rows);
            throw;
        }
        dataset->ReleaseResultSet(rows);
        return page;
    } catch (const std::exception &failure) {
        if (error)
            *error = QString::fromUtf8(failure.what());
        return {};
    }
}
}
