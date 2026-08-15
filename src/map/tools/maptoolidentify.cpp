/*!
 * \file   maptoolidentify.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolidentify.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "layers/openswmmvislayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"

#include <QMouseEvent>
#include <QPainter>

OpenSWMMVisMapToolIdentify::OpenSWMMVisMapToolIdentify(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Identify"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolIdentify::cursor() const
{
    return Qt::WhatsThisCursor;
}

double OpenSWMMVisMapToolIdentify::mapTolerance() const { return m_tolerance; }

void OpenSWMMVisMapToolIdentify::setMapTolerance(double tolerance)
{
    if (!qFuzzyCompare(m_tolerance, tolerance))
    {
        m_tolerance = tolerance;
        emit mapToleranceChanged(tolerance);
    }
}

bool OpenSWMMVisMapToolIdentify::queryAllLayers() const { return m_queryAll; }

void OpenSWMMVisMapToolIdentify::setQueryAllLayers(bool all)
{
    if (m_queryAll != all)
    {
        m_queryAll = all;
        emit queryAllLayersChanged(all);
    }
}

void OpenSWMMVisMapToolIdentify::activate()
{
    m_hasResult = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolIdentify::deactivate()
{
    m_hasResult = false;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolIdentify::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    toMapCoords(event->pos().x(), event->pos().y(), m_lastX, m_lastY);
    m_hasResult = true;

    QList<IdentifyResult> results;

    const QList<OpenSWMMVisLayer *> &layers = m_canvas->layers();

    // Iterate top-down (highest index = topmost visible layer first)
    for (int i = layers.size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = layers.at(i);
        if (!layer->isVisible())
            continue;

        IdentifyResult result;
        result.layerName = layer->name();

        if (auto *vl = qobject_cast<GISVectorLayer *>(layer))
        {
            result.features = vl->identifyAt(m_lastX, m_lastY, m_tolerance);
        }
        else if (auto *ml = qobject_cast<SWMMModelLayer *>(layer))
        {
            QVariantMap feat = ml->identifyAt(m_lastX, m_lastY, m_tolerance);
            if (!feat.isEmpty())
                result.features.append(feat);
        }

        if (!result.features.isEmpty())
        {
            results.append(result);
            if (!m_queryAll)
                break; // Only query topmost layer
        }
    }

    emit identifyResult(results);
    // Identify only paints an overlay marker on the picked location — no
    // raster reload or scene rebuild.
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("identify-tool"));
}

void OpenSWMMVisMapToolIdentify::paint(QPainter *painter,
                                    const MapExtent &,
                                    const SpatialReferenceSystem *)
{
    if (!m_hasResult || !m_canvas)
        return;

    int px, py;
    toPixelCoords(m_lastX, m_lastY, px, py);

    const int r = 8;
    painter->save();
    painter->setPen(QPen(Qt::red, 2));
    painter->drawLine(px - r, py, px + r, py);
    painter->drawLine(px, py - r, px, py + r);
    painter->drawEllipse(QPoint(px, py), r / 2, r / 2);
    painter->restore();
}
