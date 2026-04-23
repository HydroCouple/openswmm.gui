/*!
 * \file   swmmlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#include "project/openswmmvisworkspace.h"
#include "layers/openswmmvislayer.h"
#include "map/spatialreferencesystem.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPainter>
#include <QSize>
#include <QUuid>
#include <algorithm>

// ---------------------------------------------------------------------------
// Constructors / Destructor
// ---------------------------------------------------------------------------

OpenSWMMVisLayer::OpenSWMMVisLayer(OpenSWMMVisWorkspace *parent)
    : QObject(parent),
      m_layerId(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      mParent(parent),
      mName("Unlabeled Layer"),
      mLayerType(OpenSWMMVisLayer::OpenSWMMVisLayerType::SWMMDefaultLayer)
{
}

OpenSWMMVisLayer::OpenSWMMVisLayer(const QString &name, OpenSWMMVisWorkspace *parent)
    : QObject(parent),
      m_layerId(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      mParent(parent),
      mName(name),
      mLayerType(OpenSWMMVisLayer::OpenSWMMVisLayerType::SWMMDefaultLayer)
{
}

OpenSWMMVisLayer::~OpenSWMMVisLayer()
{
    if (m_ownsSRS)
        delete m_srs;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

QString OpenSWMMVisLayer::layerId() const { return m_layerId; }

QString OpenSWMMVisLayer::name() const { return mName; }

void OpenSWMMVisLayer::setName(const QString &name)
{
    if (mName != name)
    {
        mName = name;
        emit nameChanged(name);
    }
}

OpenSWMMVisLayer::OpenSWMMVisLayerType OpenSWMMVisLayer::layerType() const
{
    return mLayerType;
}

void OpenSWMMVisLayer::setLayerType(OpenSWMMVisLayerType type)
{
    if (mLayerType != type)
    {
        mLayerType = type;
        emit layerTypeChanged(type);
    }
}

// ---------------------------------------------------------------------------
// Visibility & Opacity
// ---------------------------------------------------------------------------

bool OpenSWMMVisLayer::isVisible() const { return m_visible; }

void OpenSWMMVisLayer::setVisible(bool visible)
{
    if (m_visible != visible)
    {
        m_visible = visible;
        emit visibilityChanged(visible);
        emit repaintRequested();
    }
}

double OpenSWMMVisLayer::opacity() const { return m_opacity; }

void OpenSWMMVisLayer::setOpacity(double opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (!qFuzzyCompare(m_opacity, opacity))
    {
        m_opacity = opacity;
        emit opacityChanged(opacity);
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Spatial Reference & Extent
// ---------------------------------------------------------------------------

SpatialReferenceSystem *OpenSWMMVisLayer::srs() const { return m_srs; }

void OpenSWMMVisLayer::setSRS(SpatialReferenceSystem *srs, bool ownsSRS)
{
    if (m_ownsSRS)
        delete m_srs;

    m_srs     = srs;
    m_ownsSRS = ownsSRS;
    emit srsChanged(srs);
}

MapExtent OpenSWMMVisLayer::extent() const { return m_extent; }

void OpenSWMMVisLayer::setExtent(const MapExtent &extent)
{
    m_extent = extent;
    emit extentChanged(extent);
}

// ---------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------

QVector<OpenSWMMVisLayer *> OpenSWMMVisLayer::children() const
{
    return mChildren;
}

bool OpenSWMMVisLayer::addChild(OpenSWMMVisLayer *child)
{
    if (mChildren.contains(child))
        return false;

    mChildren.append(child);
    emit childrenChanged();
    return true;
}

bool OpenSWMMVisLayer::removeChild(OpenSWMMVisLayer *child)
{
    if (!mChildren.contains(child))
        return false;

    mChildren.removeOne(child);
    emit childrenChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Rendering / Scene
// ---------------------------------------------------------------------------

void OpenSWMMVisLayer::onCanvasCRSChanged(const SpatialReferenceSystem *)
{
    // Default implementation: no-op.
    // Subclasses that cache OGRCoordinateTransformation should override this.
}

void OpenSWMMVisLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene)
        return;

    // Remove all items from the scene that belong to this layer.
    // Subclasses can override for more efficient removal strategies.
    QList<QGraphicsItem *> toRemove;
    for (QGraphicsItem *item : scene->items())
    {
        if (item->data(0).value<quintptr>() == reinterpret_cast<quintptr>(this))
            toRemove.append(item);
    }
    for (QGraphicsItem *item : toRemove)
    {
        scene->removeItem(item);
        delete item;
    }
}

void OpenSWMMVisLayer::refreshScene(QGraphicsScene *scene,
                                const MapExtent &canvasExtent,
                                const SpatialReferenceSystem *canvasSRS)
{
    // Default: full rebuild (backward-compatible for layers that
    // don't override this method).
    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
}

void OpenSWMMVisLayer::render(QPainter * /*painter*/,
                          const MapExtent & /*extent*/,
                          const QSize & /*imageSize*/,
                          const SpatialReferenceSystem * /*srs*/)
{
    // Default: no-op. Raster layer subclasses override this.
}

void OpenSWMMVisLayer::fetchCache(const MapExtent & /*extent*/,
                               const QSize & /*viewportSize*/,
                               const SpatialReferenceSystem * /*srs*/)
{
    // Default: no-op. Raster layer subclasses override this.
}

double OpenSWMMVisLayer::layerZValue() const { return m_layerZValue; }

void OpenSWMMVisLayer::setLayerZValue(double z) { m_layerZValue = z; }