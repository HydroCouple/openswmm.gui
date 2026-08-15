/*!
 * \file   rastergeoref.cpp
 * \author OpenSWMM GUI
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/rastergeoref.h"

#include "io/gdaldrivers.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>

namespace openswmmvis::io::rastergeoref {

namespace {

void setErr(QString *err, const QString &msg)
{
    if (err)
        *err = msg;
}

QString gdalErrorDetail()
{
    const char *msg = CPLGetLastErrorMsg();
    return (msg && *msg) ? QString::fromUtf8(msg) : QStringLiteral("unknown GDAL error");
}

/*! GeoTransform in the comma-space-separated %.16e form GDAL's PAM uses. */
QString geoTransformText(const double gt[6])
{
    QStringList parts;
    for (int i = 0; i < 6; ++i)
        parts << QString::asprintf("%.16e", gt[i]);
    return parts.join(QStringLiteral(", "));
}

/*! "2,1"-style dataAxisToSRSAxisMapping attribute value for \a srs. */
QString axisMappingText(const OGRSpatialReference &srs)
{
    QStringList parts;
    const auto &mapping = srs.GetDataAxisToSRSAxisMapping();
    for (int v : mapping)
        parts << QString::number(v);
    return parts.join(QLatin1Char(','));
}

/*! Attempts the driver-native route: open for update and set the georef
 *  directly. Returns false when the driver refuses update access (PNG/JPEG)
 *  or rejects the set calls — the caller then falls back to the PAM sidecar.
 *  The open is quieted because failure here is expected control flow. */
bool tryNativeUpdate(const QString &imagePath, const double *gt6,
                     const OGRSpatialReference *srs)
{
    CPLPushErrorHandler(CPLQuietErrorHandler);
    GDALDatasetH h = GDALOpenEx(imagePath.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_UPDATE,
                                nullptr, nullptr, nullptr);
    CPLPopErrorHandler();
    if (!h)
        return false;

    auto *ds = GDALDataset::FromHandle(h);
    bool ok = true;
    if (gt6) {
        double gt[6];  // SetGeoTransform takes a non-const pointer
        for (int i = 0; i < 6; ++i)
            gt[i] = gt6[i];
        ok = ds->SetGeoTransform(gt) == CE_None;
    }
    if (ok && srs)
        ok = ds->SetSpatialRef(srs) == CE_None;
    GDALClose(h);
    return ok;
}

/*! Writes/merges the PAM sidecar "<imagePath>.aux.xml". A fresh file is
 *  written wholesale; an existing one is re-serialized token-by-token with
 *  QXmlStreamReader/Writer (Qt Xml is not linked in this project) so
 *  non-georef top-level elements — statistics, histograms, metadata — are
 *  preserved while <SRS> / <GeoTransform> are replaced. */
bool writePamSidecar(const QString &imagePath, const double *gt6,
                     const QString &crsWkt, const QString &axisMapping,
                     QString *err)
{
    const QString pamPath = imagePath + QStringLiteral(".aux.xml");

    QByteArray existing;
    if (QFile::exists(pamPath)) {
        QFile in(pamPath);
        if (!in.open(QIODevice::ReadOnly)) {
            setErr(err, QStringLiteral("Cannot read existing PAM sidecar %1: %2")
                            .arg(pamPath, in.errorString()));
            return false;
        }
        existing = in.readAll();
    }

    QByteArray content;
    QXmlStreamWriter w(&content);
    w.setAutoFormatting(true);
    w.writeStartElement(QStringLiteral("PAMDataset"));
    if (!crsWkt.isEmpty()) {
        w.writeStartElement(QStringLiteral("SRS"));
        if (!axisMapping.isEmpty())
            w.writeAttribute(QStringLiteral("dataAxisToSRSAxisMapping"), axisMapping);
        w.writeCharacters(crsWkt);
        w.writeEndElement();
    }
    if (gt6)
        w.writeTextElement(QStringLiteral("GeoTransform"), geoTransformText(gt6));

    if (!existing.isEmpty()) {
        QXmlStreamReader r(existing);
        if (!r.readNextStartElement()
            || r.name() != QLatin1String("PAMDataset")) {
            setErr(err, QStringLiteral("Existing PAM sidecar %1 is not a "
                                       "<PAMDataset> document — refusing to "
                                       "overwrite it").arg(pamPath));
            return false;
        }
        while (r.readNextStartElement()) {
            const bool replacedSrs =
                !crsWkt.isEmpty() && r.name() == QLatin1String("SRS");
            const bool replacedGt =
                gt6 && r.name() == QLatin1String("GeoTransform");
            if (replacedSrs || replacedGt) {
                r.skipCurrentElement();  // superseded by the values above
                continue;
            }
            // Echo this top-level child's subtree verbatim.
            int depth = 0;
            for (;;) {
                w.writeCurrentToken(r);
                if (r.tokenType() == QXmlStreamReader::StartElement)
                    ++depth;
                else if (r.tokenType() == QXmlStreamReader::EndElement && --depth == 0)
                    break;
                if (r.atEnd())
                    break;
                r.readNext();
                if (r.hasError())
                    break;
            }
            if (r.hasError())
                break;
        }
        if (r.hasError()) {
            setErr(err, QStringLiteral("Cannot merge existing PAM sidecar %1: %2")
                            .arg(pamPath, r.errorString()));
            return false;
        }
    }
    w.writeEndElement();  // PAMDataset
    content.append('\n');

    QFile out(pamPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErr(err, QStringLiteral("Cannot write PAM sidecar %1: %2")
                        .arg(pamPath, out.errorString()));
        return false;
    }
    if (out.write(content) != content.size()) {
        setErr(err, QStringLiteral("Short write to PAM sidecar %1: %2")
                        .arg(pamPath, out.errorString()));
        return false;
    }
    return true;
}

} // namespace

bool parseWorldFile(const QString &path, WorldFileParams *out, QString *err)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setErr(err, QStringLiteral("Cannot open world file %1: %2")
                        .arg(path, file.errorString()));
        return false;
    }
    // simplified() collapses all whitespace (newlines, tabs, CR) to single
    // spaces; QString::toDouble is C-locale, so parsing is locale-independent.
    const QString text = QString::fromLatin1(file.readAll());
    const QStringList tokens =
        text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 6) {
        setErr(err, QStringLiteral("World file %1 has %2 value(s); six are "
                                   "required (A, D, B, E, C, F)")
                        .arg(path).arg(tokens.size()));
        return false;
    }
    double v[6];
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        v[i] = tokens.at(i).toDouble(&ok);
        if (!ok) {
            setErr(err, QStringLiteral("World file %1: value %2 (\"%3\") is "
                                       "not a number")
                            .arg(path).arg(i + 1).arg(tokens.at(i)));
            return false;
        }
    }
    out->a = v[0];
    out->d = v[1];
    out->b = v[2];
    out->e = v[3];
    out->c = v[4];
    out->f = v[5];
    return true;
}

QStringList worldFileCandidates(const QString &imagePath)
{
    const QFileInfo fi(imagePath);
    const QString base = fi.path() + QLatin1Char('/') + fi.completeBaseName();
    const QString ext = fi.suffix().toLower();

    QStringList sidecarExts;
    // Conventional three-letter sidecar for the common formats.
    if (ext == QLatin1String("tif") || ext == QLatin1String("tiff"))
        sidecarExts << QStringLiteral("tfw");
    else if (ext == QLatin1String("png"))
        sidecarExts << QStringLiteral("pgw");
    else if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        sidecarExts << QStringLiteral("jgw");
    else if (ext == QLatin1String("bmp"))
        sidecarExts << QStringLiteral("bpw");
    // Generic "first + last letter of the extension + w" form (tif → tfw).
    if (ext.size() >= 2)
        sidecarExts << QString(ext.front()) + ext.back() + QLatin1Char('w');
    // Full "extension + w" form (tif → tifw).
    if (!ext.isEmpty())
        sidecarExts << ext + QLatin1Char('w');
    // Format-agnostic fallback.
    sidecarExts << QStringLiteral("wld");

    QStringList candidates;
    for (const QString &e : sidecarExts) {
        const QString candidate = base + QLatin1Char('.') + e;
        if (!candidates.contains(candidate))
            candidates << candidate;
    }
    return candidates;
}

void worldFileToGeoTransform(const WorldFileParams &wf, double gt[6])
{
    gt[0] = wf.c - wf.a / 2.0 - wf.b / 2.0;  // pixel CENTER → CORNER
    gt[1] = wf.a;
    gt[2] = wf.b;
    gt[3] = wf.f - wf.d / 2.0 - wf.e / 2.0;  // pixel CENTER → CORNER
    gt[4] = wf.d;
    gt[5] = wf.e;
}

bool applyPamGeoref(const QString &imagePath, const double *gt6,
                    const QString &crsWkt, QString *err)
{
    gdalcaps::ensureRegistered();

    if (!gt6 && crsWkt.isEmpty())
        return true;  // nothing to apply

    if (!QFile::exists(imagePath)) {
        setErr(err, QStringLiteral("Raster file does not exist: %1").arg(imagePath));
        return false;
    }

    // Validate the WKT once up front; the parsed SRS also yields the PAM
    // dataAxisToSRSAxisMapping attribute for the sidecar route.
    OGRSpatialReference srs;
    QString axisMapping;
    if (!crsWkt.isEmpty()) {
        CPLPushErrorHandler(CPLQuietErrorHandler);
        const OGRErr rc = srs.importFromWkt(crsWkt.toUtf8().constData());
        CPLPopErrorHandler();
        if (rc != OGRERR_NONE) {
            setErr(err, QStringLiteral("Invalid CRS WKT (starts \"%1…\")")
                            .arg(crsWkt.left(60)));
            return false;
        }
        axisMapping = axisMappingText(srs);
    }

    if (!tryNativeUpdate(imagePath, gt6, crsWkt.isEmpty() ? nullptr : &srs)) {
        if (!writePamSidecar(imagePath, gt6, crsWkt, axisMapping, err))
            return false;
    }

    // Verify: a plain read-only open must now see the georeferencing.
    GDALDatasetH h = GDALOpenEx(imagePath.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                nullptr, nullptr, nullptr);
    if (!h) {
        setErr(err, QStringLiteral("Reopen of %1 failed after writing "
                                   "georeferencing: %2")
                        .arg(imagePath, gdalErrorDetail()));
        return false;
    }
    auto *ds = GDALDataset::FromHandle(h);
    bool ok = true;
    if (gt6) {
        double got[6];
        if (ds->GetGeoTransform(got) != CE_None) {
            setErr(err, QStringLiteral("GeoTransform written to %1 is not "
                                       "visible on reopen").arg(imagePath));
            ok = false;
        }
    }
    if (ok && !crsWkt.isEmpty() && !ds->GetSpatialRef()) {
        setErr(err, QStringLiteral("CRS written to %1 is not visible on reopen")
                        .arg(imagePath));
        ok = false;
    }
    GDALClose(h);
    return ok;
}

GeorefProbe probeGeoref(const QString &imagePath)
{
    gdalcaps::ensureRegistered();

    GeorefProbe probe;
    CPLPushErrorHandler(CPLQuietErrorHandler);
    GDALDatasetH h = GDALOpenEx(imagePath.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_READONLY,
                                nullptr, nullptr, nullptr);
    CPLPopErrorHandler();
    if (!h)
        return probe;

    auto *ds = GDALDataset::FromHandle(h);
    double gt[6];
    probe.hasGeoTransform = ds->GetGeoTransform(gt) == CE_None;
    if (const OGRSpatialReference *sr = ds->GetSpatialRef()) {
        if (const char *name = sr->GetName())
            probe.crsDescription = QString::fromUtf8(name);
        const char *auth = sr->GetAuthorityName(nullptr);
        const char *code = sr->GetAuthorityCode(nullptr);
        if (auth && code)
            probe.crsAuthCode = QStringLiteral("%1:%2").arg(
                QString::fromUtf8(auth), QString::fromUtf8(code));
    }
    GDALClose(h);
    return probe;
}

QString authCodeToWkt(const QString &authCode, QString *err)
{
    gdalcaps::ensureRegistered();

    OGRSpatialReference srs;
    CPLPushErrorHandler(CPLQuietErrorHandler);
    const OGRErr rc = srs.SetFromUserInput(authCode.toUtf8().constData());
    CPLPopErrorHandler();
    if (rc != OGRERR_NONE) {
        setErr(err, QStringLiteral("Unrecognized CRS \"%1\"").arg(authCode));
        return QString();
    }
    char *wkt = nullptr;
    if (srs.exportToWkt(&wkt) != OGRERR_NONE || !wkt) {
        CPLFree(wkt);
        setErr(err, QStringLiteral("CRS \"%1\" could not be exported to WKT")
                        .arg(authCode));
        return QString();
    }
    const QString result = QString::fromUtf8(wkt);
    CPLFree(wkt);
    return result;
}

} // namespace openswmmvis::io::rastergeoref
