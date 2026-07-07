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
#include "render/ifeaturerenderer.h"   // complete type for the setRenderer() default
#include "ui/dialogs/ilayerstylesubject.h"

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
// Temporal animation (Slice Z.13-attach)
// ---------------------------------------------------------------------------
//
// TemporalSpec is a value type with `operator==`, so we use it for change
// detection — no repaint is requested unless something actually differs.
// The animation controller (Z.13-controller, follow-up) listens to the
// signal to re-bind its tick scheduling.
void OpenSWMMVisLayer::setTemporalSpec(const OpenSWMM::Render::TemporalSpec &spec)
{
    if (m_temporalSpec == spec) return;
    m_temporalSpec = spec;
    emit temporalSpecChanged(m_temporalSpec);
    emit repaintRequested();
}

// VS.10 — base label config. Subclasses that need extra bookkeeping override
// this, mutate their own state, then chain to this implementation.
void OpenSWMMVisLayer::setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg)
{
    if (m_labelConfig == cfg) return;
    m_labelConfig = cfg;
    emit labelConfigChanged();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Polygon clip mask (Slice Z.14-attach)
// ---------------------------------------------------------------------------
//
// Mirrors setTemporalSpec's change-detection-then-emit pattern. The
// Z.14-paint integration (a separate slice) reads m_maskSpec at paint
// time to clip output to / outside the source polygon layer.
void OpenSWMMVisLayer::setMaskSpec(const OpenSWMM::Render::MaskSpec &spec)
{
    if (m_maskSpec == spec) return;
    m_maskSpec = spec;
    emit maskSpecChanged(m_maskSpec);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Auxiliary storage (Slice Z.15-attach)
// ---------------------------------------------------------------------------
void OpenSWMMVisLayer::setAuxStorageSpec(
    const OpenSWMM::Render::AuxiliaryStorageSpec &spec)
{
    if (m_auxStorageSpec == spec) return;
    m_auxStorageSpec = spec;
    emit auxStorageSpecChanged(m_auxStorageSpec);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// External-table joins (Slice Z.16-attach)
// ---------------------------------------------------------------------------
//
// Whole-list replacement. The tab UI builds a candidate QVector then
// hands it over here; per-entry diffs are the tab's concern, not the
// layer's. We still gate on equality so a no-op apply doesn't spam
// repaints.
void OpenSWMMVisLayer::setJoins(
    const QVector<OpenSWMM::Render::JoinSpec> &joins)
{
    if (m_joins == joins) return;
    m_joins = joins;
    emit joinsChanged(m_joins);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Embedded chart diagram (Slice Z.12-attach)
// ---------------------------------------------------------------------------
void OpenSWMMVisLayer::setDiagramSpec(const OpenSWMM::Render::DiagramSpec &spec)
{
    if (m_diagramSpec == spec) return;
    m_diagramSpec = spec;
    emit diagramSpecChanged(m_diagramSpec);
    emit repaintRequested();
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

QString OpenSWMMVisLayer::crsDescription() const
{
    if (!m_srs)
        return QStringLiteral("(none)");

    const QString auth = m_srs->toAuthority();   // e.g. "EPSG:2926" or ""
    const QString desc = m_srs->description();   // e.g. "NAD83(HARN) / WA South"
    if (!auth.isEmpty() && !desc.isEmpty())
        return auth + QStringLiteral(" - ") + desc;
    if (!auth.isEmpty())
        return auth;
    if (!desc.isEmpty())
        return desc;
    return QStringLiteral("(unknown)");
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

void OpenSWMMVisLayer::setBasicAuth(const QString &username, const QString &password)
{
    if (username.isEmpty()) {
        m_authHeader.clear();
        return;
    }
    m_authHeader = "Basic " + QByteArray((username + ':' + password).toUtf8()).toBase64();
}

void OpenSWMMVisLayer::setHttpHeaders(const BasemapHttpHeaders &headers)
{
    m_httpHeaders = headers;
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
    // VS.1 — accumulate the vacated region so we can mark it dirty after
    // deletion. Without this, toggling a layer OFF (depopulate with no
    // subsequent repopulate) leaves the layer's pixels on screen until an
    // unrelated repaint occurs.
    QRectF dirty;
    for (QGraphicsItem *item : toRemove)
    {
        dirty = dirty.united(item->sceneBoundingRect());
        scene->removeItem(item);
        delete item;
    }
    if (!toRemove.isEmpty())
    {
        if (dirty.isNull())
            scene->update();          // fallback: refresh entire scene
        else
            scene->invalidate(dirty); // refresh only the vacated region
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

// Slice U-2 — default styleSubjects() returns an empty list. Built-in
// General / Rendering / Metadata tabs in LayerStyleDialog still render,
// so layers that don't override remain usable.
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
OpenSWMMVisLayer::styleSubjects()
{
    return {};
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

// Default no-op renderer sink. Defined out-of-line (not inline in the header)
// because destroying the by-value unique_ptr parameter requires the complete
// IFeatureRenderer type; ifeaturerenderer.h is included above.
void OpenSWMMVisLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>) {}