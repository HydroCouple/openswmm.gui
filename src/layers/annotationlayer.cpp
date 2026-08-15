/*!
 * \file   annotationlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"

#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPointF>
#include <QRectF>
#include <QStyleOptionGraphicsItem>

namespace {

constexpr double kAnnotationZ = 8500.0;  ///< Above features, below transient overlays.

/*!
 * \brief Scene-space Y convention.
 *
 * MapCanvas applies a transform of `(s, 0, 0, s, dx, dy)` — both axes
 * positive. The data convention is therefore "scene_y = -map_y" so that
 * top-of-extent (highest map_y) lands at the smallest scene_y (which is
 * widget y == 0 after the transform). Every (map_x, map_y) sample we add
 * to the scene must be flipped on the Y axis.
 */
QPointF mapToScenePoint(double mapX, double mapY) noexcept
{
    return { mapX, -mapY };
}

} // anonymous

// ===========================================================================
// AnnotationGraphicsItem — non-QObject QGraphicsItem that renders one
// AnnotationTextItem. Lives in the scene; the layer owns the lifetime.
// Reads style directly from the data item on each paint, so any property
// change followed by `update()` produces a live repaint.
// ===========================================================================

class AnnotationGraphicsItem : public QGraphicsItem
{
public:
    explicit AnnotationGraphicsItem(AnnotationTextItem *data)
        : m_data(data)
    {
        setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        setZValue(kAnnotationZ);
        // Annotations are anchored at the map point; the painter draws around
        // the local origin. ItemIgnoresTransformations keeps the text size
        // constant in screen pixels under zoom (same convention as the
        // existing node-label pass in SWMMLayerItem).
    }

    /*! Direct pointer to the data model (non-owning). */
    [[nodiscard]] AnnotationTextItem *data() const { return m_data; }

    /*! Public wrapper around the protected QGraphicsItem::prepareGeometryChange
     *  so the owning layer can notify the scene's spatial index when the
     *  data item's geometry-affecting fields change. */
    void notifyGeometryChange() { prepareGeometryChange(); }

    QRectF boundingRect() const override
    {
        if (!m_data) return {};
        const QRectF text = textRect();
        QRectF r = text;
        // Background padding extends the rect.
        if (m_data->backgroundEnabled()) {
            const double pad = m_data->backgroundPadding()
                             + 0.5 * m_data->backgroundOutlineWidth();
            r = r.adjusted(-pad, -pad, pad, pad);
        }
        // Halo + outline strokes extend a bit further.
        double extra = 1.0;
        if (m_data->haloEnabled())    extra = std::max(extra, m_data->haloRadius() + 1.0);
        if (m_data->outlineEnabled()) extra = std::max(extra, 0.5 * m_data->outlineWidth() + 1.0);
        r = r.adjusted(-extra, -extra, extra, extra);
        // Rotation: expand to the rotated bounding box of the (already
        // padded) rect so partial repaints don't clip the edges.
        const double rot = m_data->rotation();
        if (rot != 0.0) {
            QTransform t;
            t.rotate(-rot);
            r = t.mapRect(r);
        }
        return r;
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (!m_data) return;
        const QString text = m_data->text();
        if (text.isEmpty()) return;

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);

        // Negative rotation in Qt's Y-down view space rotates CCW visually,
        // matching the "rotation angle CCW" convention exposed in the UI.
        const double rot = m_data->rotation();
        if (rot != 0.0)
            p->rotate(-rot);

        const QFont font = m_data->font();
        const QFontMetricsF fm(font);
        const QRectF text_r = textRect();   // baseline-anchored local rect

        // ---- Background box ------------------------------------------------
        if (m_data->backgroundEnabled()) {
            const double pad = m_data->backgroundPadding();
            const QRectF bg = text_r.adjusted(-pad, -pad, pad, pad);
            QPen pen(m_data->backgroundOutlineColor(),
                     m_data->backgroundOutlineWidth());
            pen.setJoinStyle(Qt::MiterJoin);
            if (m_data->backgroundOutlineWidth() <= 0.0)
                pen = Qt::NoPen;
            p->setPen(pen);
            p->setBrush(m_data->backgroundFillColor());
            const double radius = m_data->backgroundCornerRadius();
            if (radius > 0.0)
                p->drawRoundedRect(bg, radius, radius);
            else
                p->drawRect(bg);
        }

        // The text path is anchored at the baseline (0, 0). QPainterPath::addText
        // expects the baseline point; the resulting path's bounding rect is what
        // we use in textRect() for layout math.
        QPainterPath textPath;
        textPath.addText(QPointF(0.0, 0.0), font, text);

        // ---- Halo (drawn first, behind everything) -------------------------
        if (m_data->haloEnabled() && m_data->haloRadius() > 0.0) {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * m_data->haloRadius());
            stroker.setJoinStyle(Qt::RoundJoin);
            stroker.setCapStyle(Qt::RoundCap);
            const QPainterPath halo = stroker.createStroke(textPath);
            p->setPen(Qt::NoPen);
            p->setBrush(m_data->haloColor());
            p->drawPath(halo);
        }

        // ---- Glyph outline (drawn before fill so fill sits on top) ---------
        if (m_data->outlineEnabled() && m_data->outlineWidth() > 0.0) {
            QPen pen(m_data->outlineColor(), m_data->outlineWidth());
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawPath(textPath);
        }

        // ---- Fill (text body) ----------------------------------------------
        p->setPen(Qt::NoPen);
        p->setBrush(m_data->fillColor());
        p->drawPath(textPath);

        p->restore();
    }

private:
    /*!\brief Text bounds in local item coords, anchored at the baseline (0,0).
     *
     * Computed via QPainterPath so the rect matches the path we draw later;
     * QFontMetrics::tightBoundingRect doesn't include the same descent
     * conventions and led to background boxes that clipped glyph stems. */
    QRectF textRect() const
    {
        if (!m_data) return {};
        const QString text = m_data->text();
        if (text.isEmpty()) return {};
        QPainterPath path;
        path.addText(QPointF(0.0, 0.0), m_data->font(), text);
        return path.boundingRect();
    }

    AnnotationTextItem *m_data;
};

// ===========================================================================
// OpenSWMMVisAnnotationLayer
// ===========================================================================

OpenSWMMVisAnnotationLayer::OpenSWMMVisAnnotationLayer(const QString &name,
                                                       OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(name, parent)
{
    setLayerType(SWMMAnnotationLayer);
}

OpenSWMMVisAnnotationLayer::~OpenSWMMVisAnnotationLayer()
{
    if (m_scene)
        depopulateScene(m_scene);
    // m_items deleted via QObject parent chain.
}

bool OpenSWMMVisAnnotationLayer::addAnnotation(AnnotationTextItem *item)
{
    if (!item) return false;
    if (m_items.contains(item)) return false;
    item->setParent(this);
    m_items.append(item);

    connect(item, &AnnotationTextItem::positionChanged,
            this, &OpenSWMMVisAnnotationLayer::onItemPositionChanged);
    connect(item, &AnnotationTextItem::changed,
            this, &OpenSWMMVisAnnotationLayer::onItemChanged);

    if (m_scene)
        buildGraphicsItem(item);

    emit repaintRequested();
    return true;
}

bool OpenSWMMVisAnnotationLayer::removeAnnotation(const QString &id)
{
    AnnotationTextItem *item = takeAnnotation(id);
    if (!item) return false;
    delete item;
    emit repaintRequested();
    return true;
}

AnnotationTextItem *OpenSWMMVisAnnotationLayer::takeAnnotation(const QString &id)
{
    AnnotationTextItem *item = annotation(id);
    if (!item) return nullptr;

    if (auto *gi = m_graphics.take(id)) {
        if (m_scene && gi->scene() == m_scene)
            m_scene->removeItem(gi);
        delete gi;
    }

    m_items.removeOne(item);
    item->disconnect(this);
    item->setParent(nullptr);
    emit repaintRequested();
    return item;
}

AnnotationTextItem *OpenSWMMVisAnnotationLayer::annotation(const QString &id) const
{
    for (AnnotationTextItem *item : m_items)
        if (item->id() == id)
            return item;
    return nullptr;
}

AnnotationTextItem *
OpenSWMMVisAnnotationLayer::annotationAtScenePos(const QPointF &scenePos) const
{
    // Walk in reverse so the topmost (last-added) annotation wins on overlap.
    for (auto it = m_items.crbegin(); it != m_items.crend(); ++it) {
        auto *gi = m_graphics.value((*it)->id(), nullptr);
        if (!gi) continue;
        // boundingRect is in item-local coords; mapRectToScene applies the
        // current scene position so the test runs in scene space.
        const QRectF sceneRect = gi->mapRectToScene(gi->boundingRect());
        if (sceneRect.contains(scenePos))
            return *it;
    }
    return nullptr;
}

QJsonArray OpenSWMMVisAnnotationLayer::toJson() const
{
    QJsonArray arr;
    for (const AnnotationTextItem *item : m_items)
        arr.append(item->toJson());
    return arr;
}

void OpenSWMMVisAnnotationLayer::fromJson(const QJsonArray &arr)
{
    // Wipe current contents (rare — typically called on a freshly-created
    // layer right after project load).
    while (!m_items.isEmpty()) {
        AnnotationTextItem *item = m_items.takeFirst();
        if (auto *gi = m_graphics.take(item->id())) {
            if (m_scene && gi->scene() == m_scene)
                m_scene->removeItem(gi);
            delete gi;
        }
        delete item;
    }

    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        auto *item = new AnnotationTextItem(this);
        item->fromJson(v.toObject());
        m_items.append(item);
        connect(item, &AnnotationTextItem::positionChanged,
                this, &OpenSWMMVisAnnotationLayer::onItemPositionChanged);
        connect(item, &AnnotationTextItem::changed,
                this, &OpenSWMMVisAnnotationLayer::onItemChanged);
        if (m_scene)
            buildGraphicsItem(item);
    }
    emit repaintRequested();
}

// ----- Scene plumbing -----------------------------------------------------

void OpenSWMMVisAnnotationLayer::populateScene(QGraphicsScene *scene,
                                               const MapExtent &,
                                               const SpatialReferenceSystem *canvasSRS)
{
    if (!scene) return;
    m_scene = scene;
    m_canvasSRS = canvasSRS;

    for (AnnotationTextItem *item : m_items)
        buildGraphicsItem(item);
}

void OpenSWMMVisAnnotationLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene) return;
    for (auto it = m_graphics.begin(); it != m_graphics.end(); ++it) {
        AnnotationGraphicsItem *gi = it.value();
        if (gi && gi->scene() == scene)
            scene->removeItem(gi);
        delete gi;
    }
    m_graphics.clear();
    if (m_scene == scene)
        m_scene = nullptr;
}

void OpenSWMMVisAnnotationLayer::refreshScene(QGraphicsScene *scene,
                                              const MapExtent &canvasExtent,
                                              const SpatialReferenceSystem *canvasSRS)
{
    // CRS-change path: depopulate + populate so scene items are at the
    // freshly-reprojected positions. In steady state we keep the existing
    // graphics items in place — onItemPositionChanged handles incremental
    // updates without a full rebuild.
    if (m_scene == scene && m_canvasSRS == canvasSRS && !m_graphics.isEmpty())
        return;
    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
}

void OpenSWMMVisAnnotationLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    m_canvasSRS = newCanvasSRS;
    // refreshScene will be triggered by MapCanvas after this; depopulate
    // here so the next populate rebuilds at the correct scene positions.
    if (m_scene)
        depopulateScene(m_scene);
}

// ----- Private helpers ----------------------------------------------------

QPointF OpenSWMMVisAnnotationLayer::toCanvas(double x, double y) const
{
    // v1: store positions directly in canvas-CRS coords (no reprojection on
    // populate). The picker (Add Text tool) writes positions that are
    // already in canvas CRS, so this stays consistent until we add a
    // dedicated layer-CRS reprojection path.
    return mapToScenePoint(x, y);
}

void OpenSWMMVisAnnotationLayer::buildGraphicsItem(AnnotationTextItem *item)
{
    if (!m_scene || !item) return;

    // Replace any prior graphics item for this id (covers re-population on
    // CRS change and explicit rebuild paths).
    if (auto *prev = m_graphics.take(item->id())) {
        if (prev->scene() == m_scene)
            m_scene->removeItem(prev);
        delete prev;
    }

    auto *gi = new AnnotationGraphicsItem(item);
    gi->setPos(toCanvas(item->x(), item->y()));
    m_scene->addItem(gi);
    m_graphics.insert(item->id(), gi);
}

void OpenSWMMVisAnnotationLayer::onItemPositionChanged()
{
    auto *item = qobject_cast<AnnotationTextItem *>(sender());
    if (!item) return;
    auto *gi = m_graphics.value(item->id(), nullptr);
    if (!gi) return;
    gi->setPos(toCanvas(item->x(), item->y()));
    emit repaintRequested();
}

void OpenSWMMVisAnnotationLayer::onItemChanged()
{
    auto *item = qobject_cast<AnnotationTextItem *>(sender());
    if (!item) return;
    auto *gi = m_graphics.value(item->id(), nullptr);
    if (!gi) return;
    // Geometry-affecting fields (font, halo, padding, rotation, etc.) all
    // route here; prepareGeometryChange via update + boundingRect recompute
    // keeps the scene's spatial index in sync without per-field branching.
    gi->notifyGeometryChange();
    gi->update();
    emit repaintRequested();
}
