/*!
 * \file annotationlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/annotationfeatures.h"
#include "map/spatialreferencesystem.h"
#include <QFontMetricsF>
#include <QPainterPath>
#include <QJsonObject>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <memory>
#include <ogrsf_frmts.h>

OpenSWMMVisAnnotationLayer::OpenSWMMVisAnnotationLayer(const QString &name, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(name, parent)
{
    setLayerType(SWMMAnnotationLayer);
}
bool OpenSWMMVisAnnotationLayer::addAnnotation(AnnotationTextItem *item)
{
    if (!item || annotation(item->id())) return false;
    item->setParent(this);
    m_items.append(item);
    connect(item, &AnnotationTextItem::changed, this, [this] {
        emit annotationsChanged();
        emit repaintRequested();
    });
    emit annotationsChanged();
    emit repaintRequested();
    return true;
}
bool OpenSWMMVisAnnotationLayer::removeAnnotation(const QString &id)
{
    auto *item = takeAnnotation(id);
    if (!item) return false;
    delete item;
    return true;
}
AnnotationTextItem *OpenSWMMVisAnnotationLayer::takeAnnotation(const QString &id)
{
    auto *item = annotation(id);
    if (!item) return nullptr;
    m_items.removeOne(item);
    m_bounds.remove(id);
    m_offsets.remove(id);
    item->disconnect(this);
    item->setParent(nullptr);
    emit annotationsChanged();
    emit repaintRequested();
    return item;
}
AnnotationTextItem *OpenSWMMVisAnnotationLayer::annotation(const QString &id) const
{
    for (auto *item : m_items) if (item->id() == id) return item;
    return nullptr;
}
AnnotationTextItem *OpenSWMMVisAnnotationLayer::annotationAtScenePos(const QPointF &pos) const
{
    return annotationAtPixel(m_sceneToPixel.map(pos));
}
AnnotationTextItem *OpenSWMMVisAnnotationLayer::annotationAtPixel(const QPointF &pos) const
{
    if (!isVisible() || opacity() <= 0) return nullptr;
    for (auto it = m_items.crbegin(); it != m_items.crend(); ++it)
        if (m_bounds.contains((*it)->id()) && m_bounds.value((*it)->id()).contains(pos)) return *it;
    return nullptr;
}
void OpenSWMMVisAnnotationLayer::setAutoLayout(bool enabled)
{
    if (enabled == m_autoLayout) return;
    m_autoLayout = enabled;
    emit annotationsChanged();
    emit repaintRequested();
}
QJsonArray OpenSWMMVisAnnotationLayer::toJson() const
{
    QJsonArray result;
    for (auto *item : m_items) result.append(item->toJson());
    return result;
}
void OpenSWMMVisAnnotationLayer::fromJson(const QJsonArray &items)
{
    QSignalBlocker block(this);
    while (!m_items.isEmpty()) removeAnnotation(m_items.last()->id());
    for (const auto &value : items) {
        if (!value.isObject()) continue;
        auto *item = new AnnotationTextItem;
        item->fromJson(value.toObject());
        if (!addAnnotation(item)) delete item;
    }
    block.unblock();
    emit annotationsChanged();
    emit repaintRequested();
}
QJsonObject OpenSWMMVisAnnotationLayer::layerJson() const
{
    return {{"id", layerId()}, {"name", name()}, {"visible", isVisible()}, {"opacity", opacity()},
            {"autoLayout", m_autoLayout}, {"crs", srs() ? srs()->toWkt() : QString()},
            {"items", toJson()}};
}
void OpenSWMMVisAnnotationLayer::restoreLayer(const QJsonObject &o)
{
    restoreLayerId(o.value("id").toString());
    setName(o.value("name").toString(tr("Text")));
    setVisible(o.value("visible").toBool(true));
    setOpacity(o.value("opacity").toDouble(1));
    setAutoLayout(o.value("autoLayout").toBool());
    if (!o.value("crs").toString().isEmpty() && (!srs() || srs()->toWkt() != o.value("crs").toString()))
        setSRS(new SpatialReferenceSystem(o.value("crs").toString(), static_cast<QObject *>(nullptr)), true);
    fromJson(o.value("items").toArray());
}
namespace {
struct PlacedText {
    OpenSWMMVisAnnotationLayer *layer;
    AnnotationTextItem *item;
    QPointF anchor;
    QPainterPath path;
    QRectF rect; // rotated, padded bounds relative to the baseline
    QPointF offset;
    bool linked = false;
    bool visible = true;
};
// A small screen-space index keeps dense generated-label layers responsive.
class Occupancy {
    QHash<QPair<int, int>, QVector<QRectF>> cells;
    QRectF viewport;
public:
    explicit Occupancy(const QRectF &screen) : viewport(screen) {}
    void add(const QRectF &bounds) {
        const QRectF r = bounds.intersected(viewport);
        if (r.isEmpty()) return;
        for (int x = int(std::floor(r.left()/64)); x <= int(std::floor(r.right()/64)); ++x)
            for (int y = int(std::floor(r.top()/64)); y <= int(std::floor(r.bottom()/64)); ++y)
                cells[{x,y}].append(r);
    }
    bool intersects(const QRectF &r) const {
        for (int x = int(std::floor(r.left()/64)); x <= int(std::floor(r.right()/64)); ++x)
            for (int y = int(std::floor(r.top()/64)); y <= int(std::floor(r.bottom()/64)); ++y) {
                const auto it = cells.constFind({x,y});
                if (it != cells.cend()) for (const auto &other : it.value()) if (r.intersects(other)) return true;
            }
        return false;
    }
};
}
void OpenSWMMVisAnnotationLayer::paintLayers(QPainter &p, const QList<OpenSWMMVisLayer *> &layers,
                                             const MapExtent &extent, const SpatialReferenceSystem *canvasSrs,
                                             const QSize &viewport)
{
    for (auto *base : layers) if (auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(base)) {
        layer->m_bounds.clear(); layer->m_offsets.clear();
    }
    if (!extent.isValid() || extent.width() <= 0 || extent.height() <= 0 || viewport.isEmpty()) return;
    const double sx = viewport.width()/extent.width(), sy = viewport.height()/extent.height();
    const QTransform mapToPixel(sx, 0, 0, -sy, -extent.xMin()*sx, extent.yMax()*sy);
    const QRectF screen{QPointF(), QSizeF(viewport)};
    struct Transform { bool valid = true; std::shared_ptr<OGRCoordinateTransformation> ct; };
    QHash<const SpatialReferenceSystem *, Transform> transforms;
    auto toCanvas = [&](QPointF &point, const SpatialReferenceSystem *from) {
        if (!transforms.contains(from)) {
            Transform transform;
            if (from && canvasSrs && from->ogrSpatialReference() && canvasSrs->ogrSpatialReference()
                && !from->ogrSpatialReference()->IsSame(canvasSrs->ogrSpatialReference())) {
                transform.ct = {OGRCreateCoordinateTransformation(from->ogrSpatialReference(), canvasSrs->ogrSpatialReference()),
                                OGRCoordinateTransformation::DestroyCT};
                transform.valid = bool(transform.ct);
            }
            transforms.insert(from, transform);
        }
        const auto &transform = transforms[from];
        if (!transform.valid) return false;
        if (!transform.ct) return true;
        double x = point.x(), y = point.y();
        if (!transform.ct->Transform(1, &x, &y)) return false;
        point = {x, y}; return true;
    };
    QVector<PlacedText> labels;
    Occupancy occupied(screen);
    for (auto *base : layers) {
        auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(base);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0) continue;
        layer->m_sceneToPixel = QTransform(sx, 0, 0, sy, -extent.xMin()*sx, extent.yMax()*sy);
        for (auto *item : layer->m_items) {
            QPointF anchor(item->x(), item->y());
            QString text = item->text();
            bool linked = false;
            if (!item->featureReference().isEmpty()) {
                auto *source = AnnotationFeatures::source(layers, item->featureReference());
                AnnotationFeatures::Feature feature;
                if (source && AnnotationFeatures::resolve(source, item->featureReference(), feature)) {
                    anchor = feature.position;
                    if (!toCanvas(anchor, source->srs())) continue;
                    if (!item->expression().isEmpty()) text = AnnotationFeatures::evaluate(item->expression(), feature);
                    linked = true;
                } else {
                    // Missing/deleted features cannot retain a misleading leader or live value.
                    if (!item->expression().isEmpty()) continue;
                    if (!toCanvas(anchor, layer->srs())) continue;
                }
            } else if (!toCanvas(anchor, layer->srs())) continue;
            anchor = mapToPixel.map(anchor);
            if (text.trimmed().isEmpty()) continue;
            const QFontMetricsF fm(item->font());
            QPainterPath path;
            double y = 0;
            for (const QString &line : text.split('\n')) {
                path.addText(QPointF(0, y), item->font(), line);
                y += fm.lineSpacing();
            }
            double pad = 2;
            if (item->haloEnabled()) pad = std::max(pad, item->haloRadius());
            if (item->outlineEnabled()) pad = std::max(pad, item->outlineWidth()/2);
            if (item->backgroundEnabled()) pad = std::max(pad, item->backgroundPadding() + item->backgroundOutlineWidth()/2);
            QTransform rotation;
            rotation.rotate(-item->rotation());
            QRectF rect = rotation.mapRect(path.boundingRect().adjusted(-pad, -pad, pad, pad));
            if (!screen.adjusted(-200,-200,200,200).intersects(rect.translated(anchor + item->offset()))) continue;
            labels.append({layer, item, anchor, path, rect, item->offset(), linked, true});
            if (linked) occupied.add(QRectF(anchor-QPointF(4,4), QSizeF(8,8)));
        }
    }
    // Manual positions win, including those in other layers.
    for (const auto &l : labels)
        if (!l.layer->autoLayout() || l.item->pinned()) occupied.add(l.rect.translated(l.anchor+l.offset));
    // Highest layer gets first choice. Fixed positions are never modified by layout.
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        auto &l = *it;
        if (!l.layer->autoLayout() || l.item->pinned()) continue;
        bool found = false;
        const QPointF dirs[] = {{1,-1},{1,1},{-1,-1},{-1,1},{1,0},{-1,0},{0,-1},{0,1}};
        for (int ring = 0; ring <= 12 && !found; ++ring) {
            for (const auto &dir : dirs) {
                QPointF offset = l.item->offset();
                if (ring > 0) {
                    const double gap = ring * 12.;
                    const double x = dir.x() > 0 ? gap-l.rect.left() : dir.x() < 0 ? -gap-l.rect.right() : -l.rect.center().x();
                    const double y = dir.y() > 0 ? gap-l.rect.top() : dir.y() < 0 ? -gap-l.rect.bottom() : -l.rect.center().y();
                    offset += QPointF(x,y);
                }
                const QRectF candidate = l.rect.translated(l.anchor+offset);
                if (screen.contains(candidate) && !occupied.intersects(candidate)) {
                    l.offset = offset; occupied.add(candidate); found = true; break;
                }
                if (ring == 0) break;
            }
        }
        l.visible = found;
    }
    // Leaders paint behind all text, so a later callout never covers glyphs.
    for (const auto &l : labels) {
        if (!l.visible || !l.linked || !l.item->calloutEnabled()) continue;
        const QRectF bounds = l.rect.translated(l.anchor+l.offset);
        if (bounds.contains(l.anchor)) continue;
        const QPointF end(std::clamp(l.anchor.x(), bounds.left(), bounds.right()),
                          std::clamp(l.anchor.y(), bounds.top(), bounds.bottom()));
        p.save(); p.setOpacity(l.layer->opacity());
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(l.item->fillColor(), 1)); p.drawLine(l.anchor, end); p.restore();
    }
    for (const auto &l : labels) {
        if (!l.visible) continue;
        auto *item = l.item;
        const QRectF bounds = l.rect.translated(l.anchor+l.offset);
        l.layer->m_bounds[item->id()] = bounds;
        l.layer->m_offsets[item->id()] = l.offset;
        p.save();
        p.setOpacity(l.layer->opacity());
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(l.anchor+l.offset);
        p.rotate(-item->rotation());
        if (item->backgroundEnabled()) {
            const double pad = item->backgroundPadding();
            p.setPen(item->backgroundOutlineWidth() > 0 ? QPen(item->backgroundOutlineColor(), item->backgroundOutlineWidth()) : QPen(Qt::NoPen));
            p.setBrush(item->backgroundFillColor());
            p.drawRoundedRect(l.path.boundingRect().adjusted(-pad,-pad,pad,pad), item->backgroundCornerRadius(), item->backgroundCornerRadius());
        }
        if (item->haloEnabled() && item->haloRadius() > 0) {
            p.setPen(QPen(item->haloColor(), item->haloRadius()*2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush); p.drawPath(l.path);
        }
        if (item->outlineEnabled() && item->outlineWidth() > 0) {
            p.setPen(QPen(item->outlineColor(), item->outlineWidth(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush); p.drawPath(l.path);
        }
        p.fillPath(l.path, item->fillColor());
        p.restore();
    }
}
