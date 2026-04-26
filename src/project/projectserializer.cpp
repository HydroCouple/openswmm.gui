/*!
 * \file   projectserializer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "project/projectserializer.h"

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
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
const QString kLayer         = QStringLiteral("layer");
const QString kCrsAuthority  = QStringLiteral("crsAuthority");
const QString kCrsCode       = QStringLiteral("crsCode");
const QString kCategoryOrder = QStringLiteral("categoryOrder");
const QString kObjectOrder   = QStringLiteral("objectOrder");
const QString kHiddenObjects = QStringLiteral("hiddenObjects");
const QString kCanvas        = QStringLiteral("canvas");
const QString kExtent        = QStringLiteral("extent");

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

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

bool ProjectSerializer::saveToFile(const QString &oswpPath,
                                    SWMMVisProjectWindow *pw,
                                    QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (oswpPath.isEmpty()) { setErr(QObject::tr("Empty .oswp path")); return false; }
    if (!pw)               { setErr(QObject::tr("No project window")); return false; }

    auto *layer  = pw->modelLayer();
    auto *canvas = pw->canvas();
    if (!layer)  { setErr(QObject::tr("No SWMM layer to serialize")); return false; }

    QJsonObject root;
    root[kSchemaVersion] = ProjectSerializer::kCurrentSchemaVersion;
    root[kInpPath]       = layer->modelFilePath();

    // --- Layer block -------------------------------------------------------
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

    // Per-category object order overrides (Slice T.3). Sparse: only
    // categories the user reordered carry a vector.
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

    root[kLayer] = layerObj;

    // --- Canvas block ------------------------------------------------------
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
    }

    // --- Write -------------------------------------------------------------
    QFile f(oswpPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErr(QObject::tr("Cannot write %1: %2").arg(oswpPath, f.errorString()));
        return false;
    }
    const QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
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

    auto *layer  = pw->modelLayer();
    auto *canvas = pw->canvas();
    if (!layer) return true;

    // --- Layer block -------------------------------------------------------
    const QJsonObject layerObj = root.value(kLayer).toObject();

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

    // Category order.
    if (layerObj.contains(kCategoryOrder)) {
        QVector<SWMMModelLayer::Category> order;
        for (const QJsonValue &v : layerObj.value(kCategoryOrder).toArray()) {
            const int c = v.toInt(-1);
            if (c >= 0 && c < int(SWMMModelLayer::NumCategories))
                order.append(static_cast<SWMMModelLayer::Category>(c));
        }
        if (order.size() == int(SWMMModelLayer::NumCategories))
            layer->setCategoryOrder(order);   // silently rejects malformed
    }

    // Per-category object order overrides.
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
                layer->setObjectOrder(cat, v);   // validates vs defaultObjectOrder
        }
    }

    // Hidden object set.
    if (layerObj.contains(kHiddenObjects)) {
        QStringList names;
        for (const QJsonValue &v : layerObj.value(kHiddenObjects).toArray())
            names << v.toString();
        if (!names.isEmpty())
            layer->setObjectsVisible(names, /*visible=*/false);
    }

    // --- Canvas block ------------------------------------------------------
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

    return true;
}
