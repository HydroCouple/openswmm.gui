#include "mesh/corridorsource.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace mesh {
namespace {
using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
using Feature = std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;
using Transform = std::unique_ptr<OGRCoordinateTransformation, decltype(&OCTDestroyCoordinateTransformation)>;

bool stampFile(const QString &path, CorridorSourceStamp &stamp, QString &error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.canonicalFilePath().isEmpty()) {
        error = QStringLiteral("Corridor source dependency '%1' must be an existing local file.").arg(path);
        return false;
    }
    stamp.path = info.absoluteFilePath();
    stamp.canonicalPath = info.canonicalFilePath();
    stamp.size = info.size();
    stamp.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    return true;
}

bool readCRS(const QString &text, OGRSpatialReference &srs)
{
    if (text.trimmed().isEmpty() || srs.SetFromUserInput(text.toUtf8().constData()) != OGRERR_NONE)
        return false;
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return true;
}

void digestString(QCryptographicHash &hash, const QByteArray &value)
{
    hash.addData(QByteArray::number(static_cast<qlonglong>(value.size())));
    hash.addData(":", 1);
    hash.addData(value);
}
} // namespace

bool corridorSourceFilesUnchanged(const QVector<CorridorSourceStamp> &stamps, QString *error)
{
    if (error) error->clear();
    for (const auto &expected : stamps) {
        CorridorSourceStamp now;
        QString why;
        if (!stampFile(expected.path, now, why)
            || now.canonicalPath != expected.canonicalPath || now.size != expected.size
            || now.modifiedMs != expected.modifiedMs) {
            if (error) *error = QStringLiteral("Corridor source '%1' changed or became unavailable during generation. Reload the source and retry.").arg(expected.path);
            return false;
        }
    }
    return true;
}

CorridorReadResult readCorridorSources(const QVector<CorridorSource> &sources,
                                      const QString &targetCRSWkt,
                                      const std::function<bool()> &cancel)
{
    CorridorReadResult result;
    auto fail = [](const QString &error) {
        CorridorReadResult failed;
        failed.error = error;
        return failed;
    };
    auto cancelled = [&] { return cancel && cancel(); };
    if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
    if (sources.isEmpty()) return result;
    OGRSpatialReference target;
    if (!readCRS(targetCRSWkt, target))
        return fail(QStringLiteral("Corridor mesh CRS is missing or invalid."));
    const double unitScale = target.GetLinearUnits(nullptr);
    if ((!target.IsProjected() && !target.IsLocal()) || !std::isfinite(unitScale) || unitScale <= 0)
        return fail(QStringLiteral("Corridors require a planar mesh CRS with finite positive linear units."));
    GDALAllRegister();
    QSet<QString> recordedPaths;
    for (auto source : sources) {
        if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
        const QString context = QStringLiteral("Corridor source '%1' (%2): ").arg(source.path, source.layerName);
        if (source.featureIds.isEmpty())
            return fail(context + QStringLiteral("select at least one line feature; an empty selection does not mean all features."));
        if (source.bankPair && (source.featureIds.size() != 2 || source.featureIds[0] == source.featureIds[1]))
            return fail(context + QStringLiteral("a bank pair requires exactly two distinct selected feature IDs."));
        if (source.bankPair && !source.widthField.isEmpty())
            return fail(context + QStringLiteral("a bank pair derives width from its two lines; remove the width field."));
        if (source.layerName.trimmed().isEmpty())
            return fail(context + QStringLiteral("an explicit source layer name is required."));
        // Width is unused by bank geometry but remains persisted metadata;
        // keep it valid so a successful read always yields a serializable recipe.
        if (!std::isfinite(source.width) || source.width <= 0 || !std::isfinite(source.along)
            || source.along < 0 || source.across < 1)
            return fail(context + (source.bankPair
                ? QStringLiteral("retained width metadata must be finite and positive, bank spacing finite and nonnegative, and across subdivisions positive.")
                : QStringLiteral("width must be finite and positive, spacing finite and nonnegative, and across subdivisions positive.")));
        OGRSpatialReference sourceSrs, boundMeshSrs;
        if (!readCRS(source.sourceCRSWkt, sourceSrs))
            return fail(context + QStringLiteral("source CRS is missing or invalid; assign a CRS explicitly before adding this source."));
        if (!readCRS(source.meshCRSWkt, boundMeshSrs) || !boundMeshSrs.IsSame(&target))
            return fail(context + QStringLiteral("mesh CRS changed or was not recorded. Re-add the source with width and spacing in the current mesh CRS units."));
        CorridorSourceStamp initial;
        QString error;
        if (!stampFile(source.path, initial, error)) return fail(error);
        Dataset dataset(static_cast<GDALDataset *>(GDALOpenEx(source.path.toUtf8().constData(),
                         GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr)), GDALClose);
        if (!dataset) return fail(context + QStringLiteral("could not open the source dataset."));
        auto *layer = dataset->GetLayerByName(source.layerName.toUtf8().constData());
        if (!layer) return fail(context + QStringLiteral("source layer no longer exists."));
        QVector<CorridorSourceStamp> stamps{initial};
        QSet<QString> dependencyPaths{initial.canonicalPath};
        char **files = dataset->GetFileList();
        for (int i = 0; files && files[i]; ++i) {
            CorridorSourceStamp stamp;
            if (!stampFile(QString::fromUtf8(files[i]), stamp, error)) {
                CSLDestroy(files);
                return fail(error);
            }
            if (!dependencyPaths.contains(stamp.canonicalPath)) {
                dependencyPaths.insert(stamp.canonicalPath);
                stamps.append(stamp);
            }
        }
        CSLDestroy(files);
        source.sourceFiles = dependencyPaths.values();
        std::sort(source.sourceFiles.begin(), source.sourceFiles.end());
        std::sort(source.featureIds.begin(), source.featureIds.end());
        source.featureIds.erase(std::unique(source.featureIds.begin(), source.featureIds.end()), source.featureIds.end());
        const int widthIndex = source.widthField.isEmpty() ? -1
            : layer->GetLayerDefn()->GetFieldIndex(source.widthField.toUtf8().constData());
        if (!source.widthField.isEmpty()) {
            if (widthIndex < 0) return fail(context + QStringLiteral("width field '%1' no longer exists.").arg(source.widthField));
            const auto type = layer->GetLayerDefn()->GetFieldDefn(widthIndex)->GetType();
            if (type != OFTInteger && type != OFTInteger64 && type != OFTReal)
                return fail(context + QStringLiteral("width field '%1' must have a numeric field type.").arg(source.widthField));
        }
        Transform transform(nullptr, OCTDestroyCoordinateTransformation);
        if (!sourceSrs.IsSame(&target)) {
            transform.reset(OGRCreateCoordinateTransformation(&sourceSrs, &target));
            if (!transform) return fail(context + QStringLiteral("could not create the source-to-mesh CRS transformation."));
        }
        QCryptographicHash digest(QCryptographicHash::Sha256);
        // Preserve persisted centreline digests exactly. The new role has its
        // own prefix so the same selected geometry cannot change roles silently.
        if (source.bankPair) digestString(digest, QByteArrayLiteral("bank-pair-v1"));
        char *canonicalSrs = nullptr;
        if (sourceSrs.exportToWkt(&canonicalSrs) != OGRERR_NONE)
            return fail(context + QStringLiteral("could not serialize source CRS."));
        digestString(digest, QByteArray(canonicalSrs));
        CPLFree(canonicalSrs);
        digestString(digest, source.widthField.toUtf8());
        QVector<SweptPatch> pending;
        for (const qint64 fid : source.featureIds) {
            if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
            const QString featureContext = context + QStringLiteral("feature %1: ").arg(fid);
            if (fid < 0) return fail(featureContext + QStringLiteral("a persistent nonnegative feature ID is required."));
            CPLErrorReset();
            Feature feature(layer->GetFeature(static_cast<GIntBig>(fid)), OGRFeature::DestroyFeature);
            if (CPLGetLastErrorType() >= CE_Failure)
                return fail(featureContext + QStringLiteral("could not read selected feature: %1").arg(QString::fromUtf8(CPLGetLastErrorMsg())));
            if (!feature) return fail(featureContext + QStringLiteral("selected feature no longer exists."));
            double width = source.width;
            if (widthIndex >= 0) {
                if (!feature->IsFieldSetAndNotNull(widthIndex))
                    return fail(featureContext + QStringLiteral("width field '%1' is null or unset.").arg(source.widthField));
                width = feature->GetFieldAsDouble(widthIndex);
                if (!std::isfinite(width) || width <= 0)
                    return fail(featureContext + QStringLiteral("width field '%1' must be finite and positive in mesh CRS units.").arg(source.widthField));
            }
            auto *geometry = feature->GetGeometryRef();
            if (!geometry || geometry->IsEmpty()) return fail(featureContext + QStringLiteral("geometry is empty."));
            const auto type = wkbFlatten(geometry->getGeometryType());
            if (source.bankPair && type != wkbLineString)
                return fail(featureContext + QStringLiteral("each bank must be a single open LineString; multipart geometry is ambiguous."));
            if (type != wkbLineString && type != wkbMultiLineString)
                return fail(featureContext + QStringLiteral("expected a LineString or MultiLineString geometry."));
            const auto bytes = geometry->WkbSize();
            if (bytes > size_t(std::numeric_limits<qsizetype>::max()))
                return fail(featureContext + QStringLiteral("geometry is too large to read safely."));
            QByteArray wkb(static_cast<qsizetype>(bytes), Qt::Uninitialized);
            if (geometry->exportToWkb(wkbNDR, reinterpret_cast<unsigned char *>(wkb.data())) != OGRERR_NONE)
                return fail(featureContext + QStringLiteral("could not read original geometry."));
            digestString(digest, QByteArray::number(fid));
            digestString(digest, wkb);
            if (widthIndex >= 0) digestString(digest, QByteArray::number(width, 'g', 17));
            const auto *multi = type == wkbMultiLineString ? geometry->toMultiLineString() : nullptr;
            const int parts = multi ? multi->getNumGeometries() : 1;
            for (int part = 0; part < parts; ++part) {
                const auto *line = multi ? multi->getGeometryRef(part) : geometry->toLineString();
                const QString partContext = featureContext + QStringLiteral("part %1: ").arg(part + 1);
                if (!line || line->getNumPoints() < 2)
                    return fail(partContext + QStringLiteral("line requires at least two vertices."));
                if (source.bankPair && line->getX(0) == line->getX(line->getNumPoints() - 1)
                    && line->getY(0) == line->getY(line->getNumPoints() - 1))
                    return fail(partContext + QStringLiteral("each bank must be open; a closed line cannot define a bank pair."));
                SweptPatch patch;
                patch.width = width; patch.along = source.along; patch.across = source.across;
                patch.tag = source.tag.isEmpty() ? QStringLiteral("%1:%2:%3").arg(source.layerName).arg(fid).arg(part + 1) : source.tag;
                patch.centreline.reserve(line->getNumPoints());
                for (int vertex = 0; vertex < line->getNumPoints(); ++vertex) {
                    if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
                    double x = line->getX(vertex), y = line->getY(vertex);
                    if (!std::isfinite(x) || !std::isfinite(y)
                        || (transform && !transform->Transform(1, &x, &y))
                        || !std::isfinite(x) || !std::isfinite(y))
                        return fail(partContext + QStringLiteral("vertex %1 is nonfinite or cannot be transformed to the mesh CRS; the whole source was rejected.").arg(vertex + 1));
                    patch.centreline.append(QPointF(x, y));
                }
                pending.append(std::move(patch));
            }
        }
        const QString actualDigest = QString::fromLatin1(digest.result().toHex());
        if (!source.geometryDigest.isEmpty() && source.geometryDigest != actualDigest)
            return fail(context + QStringLiteral("selected geometry or width values changed. Re-select and re-add the source to approve the updated features."));
        source.geometryDigest = actualDigest;
        if (!corridorSourceFilesUnchanged(stamps, &error)) return fail(error);
        if (source.bankPair) {
            if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
            BankPairPatch banks;
            banks.bankA = pending[0].centreline;
            banks.bankB = pending[1].centreline;
            banks.across = source.across;
            banks.along = source.along;
            banks.tag = source.tag.isEmpty()
                ? QStringLiteral("%1:%2+%3").arg(source.layerName).arg(source.featureIds[0]).arg(source.featureIds[1])
                : source.tag;
            auto mesh = makeBankPairPatch(banks, &error);
            if (!error.isEmpty() || mesh.quads.isEmpty())
                return fail(context + banks.tag + QStringLiteral(": ")
                    + (error.isEmpty() ? QStringLiteral("bank pair produced no cells.") : error));
            result.patches.append(std::move(mesh));
        } else {
            for (const auto &patch : pending) {
                if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
                auto mesh = makeSweptPatch(patch, &error);
                if (!error.isEmpty()) return fail(context + patch.tag + QStringLiteral(": ") + error);
                result.patches.append(std::move(mesh));
            }
        }
        for (const auto &stamp : stamps) {
            if (!recordedPaths.contains(stamp.path)) {
                recordedPaths.insert(stamp.path);
                result.sourceStamps.append(stamp);
            }
        }
        result.resolvedSources.append(std::move(source));
    }
    QString error;
    if (cancelled()) return fail(QStringLiteral("Corridor extraction cancelled."));
    if (!corridorSourceFilesUnchanged(result.sourceStamps, &error)) return fail(error);
    return result;
}
} // namespace mesh
