/*!
 * \file   projectserializer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "project/projectserializer.h"

#include "connections/basemapconnection.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"
#include "layers/xyztilelayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "project/swmmvisproject.h"
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

// Keys — centralised so the schema is easy to audit in one place
// and the same strings aren't repeated across serialize/parse paths.
// QString-typed so QJsonObject::operator[] / .value() accept them
// across Qt versions without overload ambiguity.
const QString kSchemaVersion = QStringLiteral("schemaVersion");
const QString kInpPath       = QStringLiteral("inpPath");
const QString kEngineVersion = QStringLiteral("engineVersion");
const QString kLayer         = QStringLiteral("layer");
const QString kCrsAuthority  = QStringLiteral("crsAuthority");
const QString kCrsCode       = QStringLiteral("crsCode");
const QString kCategoryOrder = QStringLiteral("categoryOrder");
const QString kObjectOrder   = QStringLiteral("objectOrder");
const QString kHiddenObjects = QStringLiteral("hiddenObjects");
const QString kCanvas        = QStringLiteral("canvas");
const QString kExtent        = QStringLiteral("extent");
const QString kResultLayers  = QStringLiteral("resultLayers");

// Schema v4 — multi-instance project (Slice AA-3.2)
const QString kSessions      = QStringLiteral("sessions");
const QString kSessionId     = QStringLiteral("id");
const QString kSessionTitle  = QStringLiteral("title");

// Basemap keys (schema v3+)
const QString kBasemaps      = QStringLiteral("basemaps");
const QString kBmType        = QStringLiteral("type");
const QString kBmName        = QStringLiteral("name");
const QString kBmUrl         = QStringLiteral("url");
const QString kBmHeaders     = QStringLiteral("headers");
const QString kBmTilePixRatio = QStringLiteral("tilePixelRatio");
const QString kBmAxisOrder   = QStringLiteral("axisOrder");
// WMS/WMTS
const QString kBmLayer       = QStringLiteral("layerName");
const QString kBmStyle       = QStringLiteral("style");
const QString kBmFormat      = QStringLiteral("imageFormat");
const QString kBmCrs         = QStringLiteral("crs");
const QString kBmTms         = QStringLiteral("tileMatrixSet");
const QString kBmDpiMode     = QStringLiteral("dpiMode");

QJsonArray toJsonInts(const QVector<int> &v)
{
    QJsonArray a;
    for (int x : v) a.append(x);
    return a;
}

QVector<int> fromJsonInts(const QJsonArray &a)
{
    QVector<int> v;
    v.reserve(a.size());
    for (const QJsonValue &x : a) v.append(x.toInt());
    return v;
}

} // anonymous

// ---------------------------------------------------------------------------

QString ProjectSerializer::sidecarPathFor(const QString &inpPath)
{
    if (inpPath.isEmpty()) return {};
    const QFileInfo fi(inpPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() +
                                     QStringLiteral(".oswp"));
}

// (toRelativePath / resolveStoredPath now inline in the header so unit
// tests can exercise them without linking the full GUI graph.)

// ---------------------------------------------------------------------------
// Session helpers — one entry per SWMM model instance.  Holds the
// per-instance state that travels with that .inp:  inpPath (relative),
// engineVersion, layer CRS, category/object order, hidden objects.
// ---------------------------------------------------------------------------

QJsonObject ProjectSerializer::serializeSession(SWMMVisProjectWindow *pw,
                                                 const QString &oswpFile)
{
    QJsonObject obj;
    if (!pw) return obj;

    auto *layer = pw->modelLayer();
    if (!layer) return obj;

    obj[kInpPath]       = toRelativePath(layer->modelFilePath(), oswpFile);
    obj[kEngineVersion] = pw->engineVersion();

    QJsonObject layerObj;

    if (auto *srs = layer->srs()) {
        const QString auth = srs->authName();
        const int     code = srs->code();
        if (!auth.isEmpty() && code > 0) {
            layerObj[kCrsAuthority] = auth;
            layerObj[kCrsCode]      = code;
        }
    }

    // Category order (Slice T.2). Only emitted when non-default so
    // first-time saves from a default-ordered project stay tidy.
    const auto order = layer->categoryOrder();
    bool catOrderIsDefault = true;
    for (int i = 0; i < order.size(); ++i) {
        if (int(order[i]) != i) { catOrderIsDefault = false; break; }
    }
    if (!catOrderIsDefault) {
        QJsonArray a;
        for (auto c : order) a.append(int(c));
        layerObj[kCategoryOrder] = a;
    }

    // Per-category object order overrides (Slice T.3).
    QJsonObject objOrderObj;
    for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
        const auto cat = static_cast<SWMMModelLayer::Category>(i);
        const QVector<int> ov = layer->objectOrder(cat);
        if (!ov.isEmpty())
            objOrderObj[QString::number(i)] = toJsonInts(ov);
    }
    if (!objOrderObj.isEmpty())
        layerObj[kObjectOrder] = objOrderObj;

    // Hidden object set (Object Browser leaf checkboxes).
    const QSet<QString> hidden = layer->hiddenObjects();
    if (!hidden.isEmpty()) {
        QJsonArray a;
        for (const QString &n : hidden) a.append(n);
        layerObj[kHiddenObjects] = a;
    }

    obj[kLayer] = layerObj;

    // Result layers — paths stored relative to the .oswp so the project
    // is relocatable.
    if (auto *canvas = pw->canvas()) {
        QJsonArray resultArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                const QString rel = toRelativePath(rl->resultsFilePath(), oswpFile);
                if (!rel.isEmpty())
                    resultArr.append(rel);
            }
        }
        if (!resultArr.isEmpty())
            obj[kResultLayers] = resultArr;
    }

    return obj;
}

bool ProjectSerializer::applySession(const QJsonObject &sessionObj,
                                      SWMMVisProjectWindow *pw,
                                      const QString &oswpFile)
{
    if (!pw) return false;

    if (sessionObj.contains(kEngineVersion))
        pw->setEngineVersion(sessionObj.value(kEngineVersion).toString("6.0.0"));

    auto *layer = pw->modelLayer();
    if (!layer) return true;

    const QJsonObject layerObj = sessionObj.value(kLayer).toObject();

    // Layer CRS. Applied before the canvas CRS so the on-the-fly
    // reprojection path in MapCanvas picks up the right transform.
    if (layerObj.contains(kCrsAuthority) && layerObj.contains(kCrsCode)) {
        const QString auth = layerObj.value(kCrsAuthority).toString();
        const int     code = layerObj.value(kCrsCode).toInt();
        if (!auth.isEmpty() && code > 0) {
            if (auto *srs = SpatialReferenceSystem::fromAuthCode(auth, code, layer))
                layer->setSRS(srs, true);
        }
    }

    if (layerObj.contains(kCategoryOrder)) {
        QVector<SWMMModelLayer::Category> order;
        for (const QJsonValue &v : layerObj.value(kCategoryOrder).toArray()) {
            const int c = v.toInt(-1);
            if (c >= 0 && c < int(SWMMModelLayer::NumCategories))
                order.append(static_cast<SWMMModelLayer::Category>(c));
        }
        if (order.size() == int(SWMMModelLayer::NumCategories))
            layer->setCategoryOrder(order);
    }

    if (layerObj.contains(kObjectOrder)) {
        const QJsonObject obj = layerObj.value(kObjectOrder).toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            bool ok = false;
            const int c = it.key().toInt(&ok);
            if (!ok || c < 0 || c >= int(SWMMModelLayer::NumCategories))
                continue;
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            const QVector<int> v = fromJsonInts(it.value().toArray());
            if (!v.isEmpty())
                layer->setObjectOrder(cat, v);
        }
    }

    if (layerObj.contains(kHiddenObjects)) {
        QStringList names;
        for (const QJsonValue &v : layerObj.value(kHiddenObjects).toArray())
            names << v.toString();
        if (!names.isEmpty())
            layer->setObjectsVisible(names, /*visible=*/false);
    }

    // Result layers — reopen each persisted output file.
    if (sessionObj.contains(kResultLayers) && pw->canvas() && pw->modelLayer()) {
        for (const QJsonValue &v : sessionObj.value(kResultLayers).toArray()) {
            const QString relPath = v.toString();
            if (relPath.isEmpty()) continue;
            const QString absPath = resolveStoredPath(relPath, oswpFile);
            if (!QFile::exists(absPath)) continue;
            auto *rl = new SWMMResultsLayer(absPath, pw->modelLayer());
            rl->setName(QFileInfo(absPath).fileName());
            pw->canvas()->addLayer(rl, /*pushUndo=*/false);
            QList<QString> w, e;
            rl->openResults(w, e);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Save — schema v4 builder over a list of windows.
// ---------------------------------------------------------------------------

bool ProjectSerializer::writeRootJson(const QString &oswpPath,
                                       const QVector<SWMMVisProjectWindow *> &windows,
                                       QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (oswpPath.isEmpty())
    { setErr(QObject::tr("Empty .oswp path")); return false; }
    if (windows.isEmpty())
    { setErr(QObject::tr("No project windows to serialize")); return false; }

    QJsonObject root;
    root[kSchemaVersion] = ProjectSerializer::kCurrentSchemaVersion;

    // Sessions[] — one entry per instance.
    QJsonArray sessionsArr;
    for (SWMMVisProjectWindow *pw : windows) {
        if (!pw) continue;
        const QJsonObject sessionObj = serializeSession(pw, oswpPath);
        if (!sessionObj.isEmpty())
            sessionsArr.append(sessionObj);
    }
    root[kSessions] = sessionsArr;

    // Project-level: canvas + basemaps come from the first window's canvas
    // today.  When Phase 13 lands and a project owns a single shared
    // canvas, switch to project->canvas() — for now the active window is
    // the canonical view.
    auto *canvas = windows.front() ? windows.front()->canvas() : nullptr;
    if (canvas) {
        QJsonObject canvasObj;
        if (auto *srs = canvas->canvasSRS()) {
            const QString auth = srs->authName();
            const int code     = srs->code();
            if (!auth.isEmpty() && code > 0) {
                canvasObj[kCrsAuthority] = auth;
                canvasObj[kCrsCode]      = code;
            }
        }
        const MapExtent e = canvas->extent();
        if (e.isValid()) {
            QJsonArray bbox;
            bbox.append(e.xMin()); bbox.append(e.yMin());
            bbox.append(e.xMax()); bbox.append(e.yMax());
            canvasObj[kExtent] = bbox;
        }
        if (!canvasObj.isEmpty())
            root[kCanvas] = canvasObj;

        QJsonArray basemapsArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            if (!l->isBasemapLayer()) continue;
            const QJsonObject bm = serializeBasemapLayer(l);
            if (!bm.isEmpty())
                basemapsArr.append(bm);
        }
        if (!basemapsArr.isEmpty())
            root[kBasemaps] = basemapsArr;
    }

    QFile f(oswpPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErr(QObject::tr("Cannot write %1: %2").arg(oswpPath, f.errorString()));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// Public single-instance overload — for existing callers (Slice X / AA / AA-2).
bool ProjectSerializer::saveToFile(const QString &oswpPath,
                                    SWMMVisProjectWindow *pw,
                                    QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (!pw) { setErr(QObject::tr("No project window")); return false; }
    if (!pw->modelLayer())
    { setErr(QObject::tr("No SWMM layer to serialize")); return false; }

    QVector<SWMMVisProjectWindow *> windows{pw};
    return writeRootJson(oswpPath, windows, errorOut);
}

// Public multi-instance overload — Slice AA-3.4.
bool ProjectSerializer::saveToFile(const QString &oswpPath,
                                    SWMMVisProject *proj,
                                    QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (!proj) { setErr(QObject::tr("No project")); return false; }

    QVector<SWMMVisProjectWindow *> windows = proj->instances();
    if (windows.isEmpty())
    { setErr(QObject::tr("Project has no instances")); return false; }

    return writeRootJson(oswpPath, windows, errorOut);
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

bool ProjectSerializer::applyFromFile(const QString &oswpPath,
                                       SWMMVisProjectWindow *pw,
                                       QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (oswpPath.isEmpty()) { setErr(QObject::tr("Empty .oswp path")); return false; }
    if (!pw)                { setErr(QObject::tr("No project window")); return false; }

    QFile f(oswpPath);
    if (!f.open(QIODevice::ReadOnly)) {
        setErr(QObject::tr("Cannot open %1: %2").arg(oswpPath, f.errorString()));
        return false;
    }
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &jerr);
    if (doc.isNull()) {
        setErr(QObject::tr("Invalid JSON in %1: %2").arg(oswpPath, jerr.errorString()));
        return false;
    }

    const QJsonObject root = doc.object();
    const int ver = root.value(kSchemaVersion).toInt(0);

    // v0 = pre-Slice-X format (just a `layers` array of inp paths) —
    // nothing we apply from here; the caller's .inp is already open.
    if (ver < 1) return true;

    // ── Resolve the per-instance state ───────────────────────────────
    // v1/v2/v3:  inpPath / engineVersion / layer at root
    // v4+    :   sessions[N] array, applied entry sessions[0] → pw
    //
    // Single-instance applyFromFile reads sessions[0] only — multi-
    // instance hydration is the caller's responsibility (Phase 13 will
    // walk sessions[N] and create one SWMMVisProjectWindow per entry).
    QJsonObject sessionObj;
    if (root.contains(kSessions)) {
        const QJsonArray sessions = root.value(kSessions).toArray();
        if (!sessions.isEmpty())
            sessionObj = sessions.first().toObject();
    } else {
        // Legacy v1/v2/v3 — synthesize a session object from root keys.
        if (root.contains(kEngineVersion))
            sessionObj[kEngineVersion] = root.value(kEngineVersion);
        if (root.contains(kInpPath))
            sessionObj[kInpPath]       = root.value(kInpPath);
        if (root.contains(kLayer))
            sessionObj[kLayer]         = root.value(kLayer);
    }

    auto *layer  = pw->modelLayer();
    auto *canvas = pw->canvas();

    if (layer) {
        applySession(sessionObj, pw, oswpPath);
    } else if (sessionObj.contains(kEngineVersion)) {
        // No layer yet but engineVersion still useful.
        pw->setEngineVersion(sessionObj.value(kEngineVersion).toString("6.0.0"));
    }

    // --- Canvas block (project-scope, same shape v1→v4) -------------------
    if (canvas) {
        const QJsonObject canvasObj = root.value(kCanvas).toObject();
        if (canvasObj.contains(kCrsAuthority) && canvasObj.contains(kCrsCode)) {
            const QString auth = canvasObj.value(kCrsAuthority).toString();
            const int code = canvasObj.value(kCrsCode).toInt();
            if (!auth.isEmpty() && code > 0) {
                if (auto *srs = SpatialReferenceSystem::fromAuthCode(auth, code, canvas))
                    canvas->setCanvasSRS(srs, true);
            }
        }
        const QJsonArray bbox = canvasObj.value(kExtent).toArray();
        if (bbox.size() == 4) {
            const MapExtent e(bbox[0].toDouble(), bbox[1].toDouble(),
                              bbox[2].toDouble(), bbox[3].toDouble());
            if (e.isValid()) canvas->setExtent(e);
        }
    }

    // --- Basemap layers (schema v3+) ---------------------------------------
    if (ver >= 3 && canvas) {
        const QJsonArray basemapsArr = root.value(kBasemaps).toArray();
        for (const QJsonValue &v : basemapsArr) {
            OpenSWMMVisLayer *bm = deserializeBasemapLayer(v.toObject(), canvas);
            if (bm)
                canvas->addLayer(bm, /*pushUndo=*/false);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Basemap helpers
// ---------------------------------------------------------------------------

static QJsonObject headersToJson(const BasemapHttpHeaders &headers)
{
    QJsonObject obj;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        obj[it.key()] = it.value();
    return obj;
}

static BasemapHttpHeaders headersFromJson(const QJsonObject &obj)
{
    BasemapHttpHeaders headers;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        headers.insert(it.key(), it.value().toString());
    return headers;
}

QJsonObject ProjectSerializer::serializeBasemapLayer(OpenSWMMVisLayer *layer)
{
    QJsonObject obj;
    if (auto *xyz = qobject_cast<XYZTileLayer *>(layer)) {
        obj[kBmType]         = QStringLiteral("xyz");
        obj[kBmName]         = xyz->name();
        obj[kBmUrl]          = xyz->urlTemplate();
        obj[kBmTilePixRatio] = xyz->tilePixelRatio();
        obj[kBmAxisOrder]    = static_cast<int>(xyz->axisOrder());
        const QJsonObject hdrs = headersToJson(xyz->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    } else if (auto *wmts = qobject_cast<WMTSLayer *>(layer)) {
        obj[kBmType]  = QStringLiteral("wmts");
        obj[kBmName]  = wmts->name();
        obj[kBmUrl]   = wmts->serviceUrl().toString();
        obj[kBmLayer] = wmts->activeLayerId();
        obj[kBmStyle] = wmts->activeStyle();
        obj[kBmFormat]= wmts->imageFormat();
        obj[kBmTms]   = wmts->activeTileMatrixSet();
        const QJsonObject hdrs = headersToJson(wmts->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    } else if (auto *wms = qobject_cast<WMSLayer *>(layer)) {
        obj[kBmType]   = QStringLiteral("wms");
        obj[kBmName]   = wms->name();
        obj[kBmUrl]    = wms->serviceUrl().toString();
        obj[kBmLayer]  = wms->activeLayerName();
        obj[kBmStyle]  = wms->activeStyle();
        obj[kBmFormat] = wms->imageFormat();
        obj[kBmCrs]    = wms->crs();
        obj[kBmDpiMode]= wms->dpiMode();
        const QJsonObject hdrs = headersToJson(wms->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    }
    return obj;
}

OpenSWMMVisLayer *ProjectSerializer::deserializeBasemapLayer(const QJsonObject &obj,
                                                             QObject *parent)
{
    const QString type = obj.value(kBmType).toString();
    const BasemapHttpHeaders headers = headersFromJson(obj.value(kBmHeaders).toObject());

    if (type == QStringLiteral("xyz")) {
        auto *layer = new XYZTileLayer(obj.value(kBmUrl).toString(), 256, parent);
        layer->setName(obj.value(kBmName).toString());
        layer->setTilePixelRatio(obj.value(kBmTilePixRatio).toInt(0));
        layer->setAxisOrder(static_cast<TileAxisOrder>(obj.value(kBmAxisOrder).toInt(0)));
        layer->setHttpHeaders(headers);
        return layer;
    }
    if (type == QStringLiteral("wmts")) {
        auto *layer = new WMTSLayer(QUrl(obj.value(kBmUrl).toString()));
        layer->setName(obj.value(kBmName).toString());
        layer->setActiveLayerId(obj.value(kBmLayer).toString());
        layer->setActiveStyle(obj.value(kBmStyle).toString());
        layer->setImageFormat(obj.value(kBmFormat).toString());
        layer->setActiveTileMatrixSet(obj.value(kBmTms).toString());
        layer->setHttpHeaders(headers);
        return layer;
    }
    if (type == QStringLiteral("wms")) {
        auto *layer = new WMSLayer(QUrl(obj.value(kBmUrl).toString()));
        layer->setName(obj.value(kBmName).toString());
        layer->setActiveLayerName(obj.value(kBmLayer).toString());
        layer->setActiveStyle(obj.value(kBmStyle).toString());
        layer->setImageFormat(obj.value(kBmFormat).toString());
        layer->setCrs(obj.value(kBmCrs).toString());
        layer->setDpiMode(obj.value(kBmDpiMode).toInt(7));
        layer->setHttpHeaders(headers);
        return layer;
    }
    return nullptr;
}
