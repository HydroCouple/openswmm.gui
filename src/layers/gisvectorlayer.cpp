/*!
 * \file   gisvectorlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/gisvectorlayer.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"

#include <QGraphicsScene>
#include <QDebug>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>
#include <ogr_geometry.h>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

GISVectorLayer::GISVectorLayer(const QString &filePath,
                               const QString &layerName,
                               OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent)
{
    setLayerType(SWMMVectorLayer);

    GDALAllRegister(); // Idempotent – safe to call multiple times

    if (!filePath.isEmpty())
        openDataset(filePath, layerName);
}

GISVectorLayer::~GISVectorLayer()
{
    closeDataset();
}

// ---------------------------------------------------------------------------
// Dataset info
// ---------------------------------------------------------------------------

QString GISVectorLayer::filePath()     const { return m_filePath; }
QString GISVectorLayer::ogrLayerName() const { return m_ogrLayerName; }

int GISVectorLayer::featureCount() const
{
    if (!m_ogrLayer)
        return 0;

    return static_cast<int>(m_ogrLayer->GetFeatureCount(/*bForce=*/false));
}

QStringList GISVectorLayer::fieldNames() const
{
    if (!m_ogrLayer)
        return {};

    OGRFeatureDefn *defn = m_ogrLayer->GetLayerDefn();
    QStringList names;
    for (int i = 0; i < defn->GetFieldCount(); ++i)
        names.append(QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef()));

    return names;
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

QString GISVectorLayer::filterExpression() const { return m_filterExpr; }

void GISVectorLayer::setFilterExpression(const QString &expr)
{
    if (m_filterExpr == expr)
        return;

    m_filterExpr = expr;

    if (m_ogrLayer)
    {
        m_ogrLayer->SetAttributeFilter(
            expr.isEmpty() ? nullptr : expr.toUtf8().constData());
    }

    m_needsRebuild = true;
    emit filterExpressionChanged(expr);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Symbology
// ---------------------------------------------------------------------------

GISVectorSymbol GISVectorLayer::symbol() const { return m_symbol; }

void GISVectorLayer::setSymbol(const GISVectorSymbol &symbol)
{
    m_symbol = symbol;
    m_needsRebuild = true;
    emit symbolChanged(symbol);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QSet<long long> GISVectorLayer::selectedFeatureIds() const { return m_selectedIds; }

void GISVectorLayer::setSelectedFeatureIds(const QSet<long long> &ids)
{
    m_selectedIds = ids;
    m_needsRebuild = true;
    emit selectionChanged(ids);
    emit repaintRequested();
}

void GISVectorLayer::clearSelection()
{
    setSelectedFeatureIds({});
}

// ---------------------------------------------------------------------------
// Identify  (overloaded for MapTool convenience – no canvasSRS needed)
// ---------------------------------------------------------------------------

QList<QVariantMap> GISVectorLayer::identifyAt(double mapX, double mapY,
                                               double tolerance) const
{
    return identifyAt(mapX, mapY, nullptr, tolerance);
}

QList<QVariantMap> GISVectorLayer::identifyAt(double mapX, double mapY,
                                               const SpatialReferenceSystem * /*canvasSRS*/,
                                               double tolerance) const
{
    QList<QVariantMap> results;

    if (!m_ogrLayer)
        return results;

    // Set a spatial filter around the click point
    m_ogrLayer->SetSpatialFilterRect(mapX - tolerance, mapY - tolerance,
                                     mapX + tolerance, mapY + tolerance);
    m_ogrLayer->ResetReading();

    OGRFeature *feat = nullptr;
    while ((feat = m_ogrLayer->GetNextFeature()) != nullptr)
    {
        QVariantMap attrs;
        attrs[QStringLiteral("fid")] = static_cast<long long>(feat->GetFID());

        const OGRFeatureDefn *defn = feat->GetDefnRef();
        for (int i = 0; i < defn->GetFieldCount(); ++i)
        {
            QString fname = QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef());
            OGRFieldType type = defn->GetFieldDefn(i)->GetType();

            if (!feat->IsFieldSet(i) || feat->IsFieldNull(i))
            {
                attrs[fname] = QVariant{};
            }
            else if (type == OFTInteger)
            {
                attrs[fname] = feat->GetFieldAsInteger(i);
            }
            else if (type == OFTInteger64)
            {
                attrs[fname] = static_cast<long long>(feat->GetFieldAsInteger64(i));
            }
            else if (type == OFTReal)
            {
                attrs[fname] = feat->GetFieldAsDouble(i);
            }
            else
            {
                attrs[fname] = QString::fromUtf8(feat->GetFieldAsString(i));
            }
        }

        results.append(attrs);
        OGRFeature::DestroyFeature(feat);
    }

    // Clear spatial filter
    m_ogrLayer->SetSpatialFilter(nullptr);

    return results;
}

// ---------------------------------------------------------------------------
// Scene population (QGraphicsScene / QGraphicsItems)
// ---------------------------------------------------------------------------

/*!
 * \brief Converts a map coordinate to scene coordinates (Y-flipped).
 */
static inline QPointF toScene(double mapX, double mapY)
{
    return QPointF(mapX, -mapY);
}

void GISVectorLayer::populateScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!m_ogrLayer || !isVisible())
        return;

    // Spatial filter must be in LAYER CRS (OGR layer holds layer-CRS data).
    // Two cases:
    //   - layer CRS == canvas CRS  →  m_transform is null  →  use canvas
    //     extent directly (matching CRSes).
    //   - layer CRS != canvas CRS  →  m_transform exists   →  inverse-
    //     transform the four corners of canvasExtent into layer CRS, take
    //     their bounding box. Without this, the filter rejects every
    //     feature and the shapefile "doesn't render".
    if (!m_transform)
    {
        m_ogrLayer->SetSpatialFilterRect(canvasExtent.xMin(), canvasExtent.yMin(),
                                         canvasExtent.xMax(), canvasExtent.yMax());
    }
    else if (auto *inv = m_transform->GetInverse())
    {
        double xs[4] = {canvasExtent.xMin(), canvasExtent.xMax(),
                        canvasExtent.xMax(), canvasExtent.xMin()};
        double ys[4] = {canvasExtent.yMin(), canvasExtent.yMin(),
                        canvasExtent.yMax(), canvasExtent.yMax()};
        if (inv->Transform(4, xs, ys))
        {
            double xMin = xs[0], xMax = xs[0], yMin = ys[0], yMax = ys[0];
            for (int i = 1; i < 4; ++i)
            {
                xMin = qMin(xMin, xs[i]); xMax = qMax(xMax, xs[i]);
                yMin = qMin(yMin, ys[i]); yMax = qMax(yMax, ys[i]);
            }
            m_ogrLayer->SetSpatialFilterRect(xMin, yMin, xMax, yMax);
        }
        else
        {
            // Inverse transform failed (e.g. PROJ-side error) — fall back
            // to no filter rather than silently dropping features.
            m_ogrLayer->SetSpatialFilter(nullptr);
        }
        OGRCoordinateTransformation::DestroyCT(inv);
    }
    else
    {
        m_ogrLayer->SetSpatialFilter(nullptr);
    }
    m_ogrLayer->ResetReading();

    const double baseZ = layerZValue();

    auto addPoint = [&](double mx, double my, qint64 fid, bool selected) {
        auto *item = new VectorPointItem(fid, mx, -my, m_symbol.markerSize / 2.0);
        item->setBrush(QBrush(selected ? Qt::yellow : m_symbol.markerFill));
        item->setPen(QPen(m_symbol.markerOutline, m_symbol.markerOutlineW));
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ + 2);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    auto addLine = [&](const QVector<QPointF> &scenePts, qint64 fid, bool selected) {
        if (scenePts.size() < 2)
            return;
        auto *item = new VectorLineItem(fid, scenePts);
        QPen pen = selected ? QPen(Qt::yellow, m_symbol.linePen.widthF() + 2)
                            : m_symbol.linePen;
        // Cosmetic pen → width stays in screen pixels regardless of zoom.
        // Without this, a layer in a projected CRS (e.g. coords ~1e7) at
        // canvas scale ~1e-3 px/unit renders pen widths < 0.01 viewport
        // pixels — invisible. The SWMM model layer escapes this via
        // Slice R's per-frame painter, but generic GIS-vector items use
        // standard QGraphicsItems.
        pen.setCosmetic(true);
        item->setPen(pen);
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ + 1);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    auto addPolygon = [&](const QVector<QPointF> &scenePts, qint64 fid, bool selected) {
        if (scenePts.size() < 3)
            return;
        QPolygonF poly(scenePts);
        auto *item = new VectorPolygonItem(fid, poly);
        QBrush brush = selected ? QBrush(QColor(255, 255, 0, 100)) : m_symbol.polygonFill;
        item->setBrush(brush);
        QPen polyPen = m_symbol.polygonOutline;
        polyPen.setCosmetic(true);
        item->setPen(polyPen);
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    OGRFeature *feat = nullptr;
    while ((feat = m_ogrLayer->GetNextFeature()) != nullptr)
    {
        qint64 fid = static_cast<qint64>(feat->GetFID());
        bool selected = m_selectedIds.contains(static_cast<long long>(fid));

        OGRGeometry *geom = feat->GetGeometryRef();
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }
        if (m_transform)
            geom->transform(m_transform);

        {
            OGRwkbGeometryType gt = wkbFlatten(geom->getGeometryType());

            if (gt == wkbPoint)
            {
                auto *pt = geom->toPoint();
                addPoint(pt->getX(), pt->getY(), fid, selected);
            }
            else if (gt == wkbLineString)
            {
                auto *ls = geom->toLineString();
                QVector<QPointF> pts;
                pts.reserve(ls->getNumPoints());
                for (int i = 0; i < ls->getNumPoints(); ++i)
                    pts.append(toScene(ls->getX(i), ls->getY(i)));
                addLine(pts, fid, selected);
            }
            else if (gt == wkbPolygon)
            {
                auto *poly = geom->toPolygon();
                OGRLinearRing *ring = poly->getExteriorRing();
                QVector<QPointF> pts;
                pts.reserve(ring->getNumPoints());
                for (int i = 0; i < ring->getNumPoints(); ++i)
                    pts.append(toScene(ring->getX(i), ring->getY(i)));
                addPolygon(pts, fid, selected);
            }
            else if (gt == wkbMultiPoint)
            {
                const auto *mp = geom->toMultiPoint();
                for (int j = 0; j < mp->getNumGeometries(); ++j)
                {
                    auto *pt = mp->getGeometryRef(j)->toPoint();
                    addPoint(pt->getX(), pt->getY(), fid, selected);
                }
            }
            else if (gt == wkbMultiLineString)
            {
                const auto *ml = geom->toMultiLineString();
                for (int j = 0; j < ml->getNumGeometries(); ++j)
                {
                    auto *ls = ml->getGeometryRef(j)->toLineString();
                    QVector<QPointF> pts;
                    for (int k = 0; k < ls->getNumPoints(); ++k)
                        pts.append(toScene(ls->getX(k), ls->getY(k)));
                    addLine(pts, fid, selected);
                }
            }
            else if (gt == wkbMultiPolygon)
            {
                const auto *mpoly = geom->toMultiPolygon();
                for (int j = 0; j < mpoly->getNumGeometries(); ++j)
                {
                    const auto *poly = mpoly->getGeometryRef(j)->toPolygon();
                    const OGRLinearRing *ring = poly->getExteriorRing();
                    QVector<QPointF> pts;
                    for (int k = 0; k < ring->getNumPoints(); ++k)
                        pts.append(toScene(ring->getX(k), ring->getY(k)));
                    addPolygon(pts, fid, selected);
                }
            }
        }

        OGRFeature::DestroyFeature(feat);
    }

    // Remove spatial filter when done
    m_ogrLayer->SetSpatialFilter(nullptr);
}

void GISVectorLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene || m_sceneItems.isEmpty())
        return;

    for (auto *item : std::as_const(m_sceneItems))
    {
        scene->removeItem(item);
        delete item;
    }
    m_sceneItems.clear();
    m_needsRebuild = true;
}

void GISVectorLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    rebuildTransform(newCanvasSRS);
    m_needsRebuild = true;
}

void GISVectorLayer::refreshScene(QGraphicsScene *scene,
                                   const MapExtent &canvasExtent,
                                   const SpatialReferenceSystem *canvasSRS)
{
    if (!m_needsRebuild)
        return;  // Items already in scene at correct positions

    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
    m_needsRebuild = false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void GISVectorLayer::openDataset(const QString &filePath, const QString &layerName)
{
    closeDataset();

    m_dataset = static_cast<GDALDataset *>(
        GDALOpenEx(filePath.toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));

    if (!m_dataset)
    {
        qWarning() << "GISVectorLayer: failed to open" << filePath;
        return;
    }

    if (layerName.isEmpty())
        m_ogrLayer = m_dataset->GetLayer(0);
    else
        m_ogrLayer = m_dataset->GetLayerByName(layerName.toUtf8().constData());

    if (!m_ogrLayer)
    {
        qWarning() << "GISVectorLayer: layer not found in" << filePath;
        return;
    }

    m_filePath     = filePath;
    m_ogrLayerName = QString::fromUtf8(m_ogrLayer->GetName());

    // Compute layer extent
    OGREnvelope env;
    if (m_ogrLayer->GetExtent(&env) == OGRERR_NONE)
        setExtent(MapExtent(env.MinX, env.MinY, env.MaxX, env.MaxY));

    // Set layer CRS from the OGR layer spatial ref
    const OGRSpatialReference *layerSRS = m_ogrLayer->GetSpatialRef();
    if (layerSRS)
    {
        char *wkt = nullptr;
        layerSRS->exportToWkt(&wkt);
        if (wkt)
        {
            setSRS(SpatialReferenceSystem::fromWktOrProj(QString::fromUtf8(wkt)),
                   /*ownsSRS=*/true);
            CPLFree(wkt);
        }
    }

    setName(m_ogrLayerName);

    emit filePathChanged(filePath);
    emit layerNameChanged(m_ogrLayerName);
    emit featureCountChanged(featureCount());
}

void GISVectorLayer::closeDataset()
{
    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }

    m_ogrLayer = nullptr;

    if (m_dataset)
    {
        GDALClose(m_dataset);
        m_dataset = nullptr;
    }
}

void GISVectorLayer::rebuildTransform(const SpatialReferenceSystem *canvasSRS)
{
    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }

    if (!m_ogrLayer || !canvasSRS || !canvasSRS->ogrSpatialReference())
        return;

    const OGRSpatialReference *layerSRS = m_ogrLayer->GetSpatialRef();
    if (!layerSRS)
        return;

    // Only create a transform if the CRS differs
    if (!layerSRS->IsSame(canvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            layerSRS, canvasSRS->ogrSpatialReference());
    }
}
