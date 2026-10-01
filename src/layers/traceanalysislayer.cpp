// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/traceanalysislayer.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
#include <limits>
#include <ogr_spatialref.h>

namespace openswmmvis::trace
{
using namespace OpenSWMM::Render;
namespace
{
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
double transform(double v, const QString &t)
{
    if (t == "sqrt")
        return std::sqrt(std::max(0., v));
    if (t == "log")
        return std::log1p(std::max(0., v));
    return v;
}
class Part final : public ISublayer
{
  public:
    Part(TraceAnalysisLayer *p, QString id, QString title, Kind kind, bool visible)
        : ISublayer(p), owner(p), key(id), name(title), type(kind), shown(visible)
    {
    }
    Kind kind() const override { return type; }
    QString id() const override { return key; }
    QString displayName() const override { return name; }
    bool isVisible() const override { return shown; }
    void setVisible(bool v) override
    {
        if (shown != v)
        {
            shown = v;
            invalidate();
        }
    }
    qreal opacity() const override { return alpha; }
    void setOpacity(qreal v) override
    {
        alpha = std::clamp(v, 0., 1.);
        invalidate();
    }
    bool isDynamic() const override { return false; }
    SublayerStyle *style() override { return owner->traceStyle(); }
    QList<LegendSymbolItem> legendSymbolItems() const override
    {
        return key == "links"   ? owner->legend(false)
               : key == "nodes" ? owner->legend(true)
                                : QList<LegendSymbolItem>{};
    }
    QSGNode *buildOrUpdateNode(QSGNode *, const SublayerContext &) override { return nullptr; }

  private:
    TraceAnalysisLayer *owner;
    QString key, name;
    Kind type;
    bool shown;
    double alpha = 1;
};
class Item final : public QGraphicsItem
{
  public:
    TraceAnalysisLayer *layer;
    QVector<QPointF> nodes;
    QVector<QVector<QPointF>> links;
    QRectF bounds;
    QRectF boundingRect() const override { return bounds; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        layer->paint(p, nodes, links);
    }
};
} // namespace
QJsonObject Channel::toJson() const
{
    return {{"field", field},
            {"transform", transform},
            {"automatic", automatic},
            {"minimum", minimum},
            {"maximum", maximum},
            {"minimumSize", minimumSize},
            {"maximumSize", maximumSize},
            {"classes", classes},
            {"ramp", ramp.toJson()}};
}
Channel Channel::fromJson(const QJsonObject &j)
{
    Channel c;
    c.field = j.value("field").toString("ratio");
    c.transform = j.value("transform").toString("linear");
    c.automatic = j.value("automatic").toBool(true);
    c.minimum = j.value("minimum").toDouble();
    c.maximum = j.value("maximum").toDouble(1);
    c.minimumSize = std::clamp(j.value("minimumSize").toDouble(2), .5, 80.);
    c.maximumSize = std::clamp(j.value("maximumSize").toDouble(14), c.minimumSize, 80.);
    c.classes = std::clamp(j.value("classes").toInt(), 0, 20);
    if (j.contains("ramp"))
        c.ramp = RasterColorRamp::fromJson(j.value("ramp").toObject());
    return c;
}
void TraceStyle::setTaper(double v)
{
    v = std::clamp(v, .05, 1.);
    if (v != m_taper)
    {
        m_taper = v;
        setDirty();
    }
}
void TraceStyle::setArrows(bool v)
{
    if (v != m_arrows)
    {
        m_arrows = v;
        setDirty();
    }
}
void TraceStyle::setLabels(bool v)
{
    if (v != m_labels)
    {
        m_labels = v;
        setDirty();
    }
}
QJsonObject TraceStyle::toJson() const
{
    return {{"schema", 1},
            {"linkColor", linkColor.toJson()},
            {"linkWidth", linkWidth.toJson()},
            {"nodeColor", nodeColor.toJson()},
            {"nodeSize", nodeSize.toJson()},
            {"taper", m_taper},
            {"arrows", m_arrows},
            {"labels", m_labels}};
}
void TraceStyle::fromJson(const QJsonObject &j)
{
    if (j.isEmpty())
        return;
    linkColor = Channel::fromJson(j.value("linkColor").toObject());
    linkWidth = Channel::fromJson(j.value("linkWidth").toObject());
    nodeColor = Channel::fromJson(j.value("nodeColor").toObject());
    nodeSize = Channel::fromJson(j.value("nodeSize").toObject());
    m_taper = std::clamp(j.value("taper").toDouble(.3), .05, 1.);
    m_arrows = j.value("arrows").toBool(true);
    m_labels = j.value("labels").toBool(false);
    setDirty();
}
QPainterPath taperedPath(const QVector<QPointF> &input, double start, double end)
{
    QPainterPath path;
    QVector<QPointF> p;
    for (auto v : input)
        if (std::isfinite(v.x()) && std::isfinite(v.y()) &&
            (p.isEmpty() || QLineF(p.last(), v).length() > 1e-8))
            p.append(v);
    if (p.size() < 2)
        return path;
    QVector<double> d(p.size(), 0);
    for (int i = 1; i < p.size(); ++i)
        d[i] = d[i - 1] + QLineF(p[i - 1], p[i]).length();
    if (d.last() <= 0)
        return path;
    path.setFillRule(Qt::WindingFill);
    for (int i = 1; i < p.size(); ++i)
    {
        auto delta = p[i] - p[i - 1];
        double length = QLineF(p[i - 1], p[i]).length();
        QPointF normal(-delta.y() / length, delta.x() / length);
        double a = (start + (end - start) * d[i - 1] / d.last()) * .5,
               b = (start + (end - start) * d[i] / d.last()) * .5;
        QPolygonF quad{p[i - 1] - normal * a, p[i] - normal * b, p[i] + normal * b,
                       p[i - 1] + normal * a};
        path.addPolygon(quad);
        path.closeSubpath();
        if (i < p.size() - 1)
            path.addEllipse(p[i], b, b);
    }
    return path;
}
QString fieldLabel(const QString &f, bool node)
{
    if (f == "ratio")
        return QObject::tr("Passage ratio");
    if (f == "flow")
        return node ? QObject::tr("Mean outgoing flow (m³/s)")
                    : QObject::tr("Mean net magnitude (m³/s)");
    if (f == "gross")
        return node ? QObject::tr("Mean inflow (m³/s)") : QObject::tr("Mean absolute flow (m³/s)");
    if (f == "time")
        return QObject::tr("Expected time (min)");
    if (f == "local")
        return QObject::tr("Local delay (min)");
    if (f == "coverage")
        return QObject::tr("Time coverage (0–1)");
    if (f == "volume")
        return QObject::tr("Mean stored volume (m³)");
    return QObject::tr("Uniform");
}
TraceAnalysisLayer::TraceAnalysisLayer(std::shared_ptr<Result> r, OpenSWMMVisWorkspace *w)
    : OpenSWMMVisLayer(r->title(), w), m_result(std::move(r)), m_style(new TraceStyle(this))
{
    setLayerType(SWMMDefaultLayer);
    setLayerZValue(15);
    const auto &s = m_result->dataset->snapshot;
    if (!s.wkt.isEmpty())
        setSRS(SpatialReferenceSystem::fromWktOrProj(s.wkt), true);
    bool first = true;
    MapExtent ext;
    auto add = [&](QPointF p)
    {
        if (first)
        {
            ext = MapExtent(p.x(), p.y(), p.x(), p.y());
            first = false;
        }
        else
            ext.expandToInclude(p.x(), p.y());
    };
    for (const auto &n : s.nodes)
        if (n.hasGeometry)
            add(n.point);
    for (const auto &l : s.links)
        for (auto p : l.points)
            add(p);
    if (!first)
        setExtent(ext);
    m_style->nodeSize.minimumSize = 5;
    m_style->nodeSize.maximumSize = 20;
    m_parts = {new Part(this, "links", tr("Links"), ISublayer::LineKind, true),
               new Part(this, "nodes", tr("Nodes"), ISublayer::MarkerKind, true),
               new Part(this, "arrows", tr("Flow direction"), ISublayer::ArrowKind, true),
               new Part(this, "labels", tr("Labels"), ISublayer::MarkerKind, true)};
    for (auto *p : m_parts)
        connect(p, &ISublayer::invalidated, this, &OpenSWMMVisLayer::repaintRequested);
    connect(m_style, &TraceStyle::styleChanged, this,
            [this]
            {
                m_result->style = savedStyle();
                for (auto *p : m_parts)
                    p->invalidate();
            });
    restoreStyle(m_result->style);
}
QJsonObject TraceAnalysisLayer::savedStyle() const
{
    QJsonObject j = m_style->toJson();
    QJsonArray a;
    for (auto *p : m_parts)
        a.append(
            QJsonObject{{"id", p->id()}, {"visible", p->isVisible()}, {"opacity", p->opacity()}});
    j.insert("parts", a);
    return j;
}
void TraceAnalysisLayer::restoreStyle(const QJsonObject &j)
{
    m_style->fromJson(j);
    for (auto v : j.value("parts").toArray())
    {
        auto o = v.toObject();
        for (auto *p : m_parts)
            if (p->id() == o.value("id").toString())
            {
                p->setVisible(o.value("visible").toBool(true));
                p->setOpacity(o.value("opacity").toDouble(1));
            }
    }
}
QString TraceAnalysisLayer::sourceDescription() const { return m_result->dataset->packagePath; }
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
TraceAnalysisLayer::styleSubjects()
{
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> s;
    s.push_back(std::make_unique<openswmmvis::ui::LayerStyleSubject>(tr("Flow analysis"), m_style,
                                                                     "flow-trace"));
    return s;
}
void TraceAnalysisLayer::populateScene(QGraphicsScene *scene, const MapExtent &,
                                       const SpatialReferenceSystem *canvas)
{
    if (!scene || !isVisible())
        return;
    auto item = new Item;
    item->layer = this;
    const auto &s = m_result->dataset->snapshot;
    OGRCoordinateTransformation *ct = nullptr;
    if (srs() && canvas)
        ct = srs()->createTransformationTo(*canvas);
    const bool mustTransform = srs() && canvas && !srs()->equals(*canvas);
    if (mustTransform && !ct)
    {
        delete item;
        return;
    }
    auto point = [&](QPointF p)
    {
        double x = p.x(), y = p.y();
        if (ct && !ct->Transform(1, &x, &y))
            return QPointF(missing, missing);
        return QPointF(x, -y);
    };
    for (auto n : s.nodes)
        item->nodes.append(n.hasGeometry ? point(n.point) : QPointF(missing, missing));
    for (auto l : s.links)
    {
        QVector<QPointF> p;
        for (auto v : l.points)
            p.append(point(v));
        if (p.isEmpty() && l.from >= 0 && l.to >= 0)
            p = {item->nodes[l.from], item->nodes[l.to]};
        item->links.append(p);
    }
    if (ct)
        OCTDestroyCoordinateTransformation(ct);
    bool first = true;
    auto include = [&](QPointF p)
    {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return;
        if (first)
        {
            item->bounds = QRectF(p, QSizeF(0, 0));
            first = false;
        }
        else
            item->bounds = item->bounds.united(QRectF(p, QSizeF(1e-9, 1e-9)));
    };
    for (auto p : item->nodes)
        include(p);
    for (auto l : item->links)
        for (auto p : l)
            include(p);
    if (first)
    {
        delete item;
        return;
    }
    double margin = std::max({item->bounds.width(), item->bounds.height(), 1.}) * .1;
    item->bounds.adjust(-margin, -margin, margin, margin);
    item->setData(0, QVariant::fromValue(reinterpret_cast<quintptr>(this)));
    item->setZValue(layerZValue());
    item->setOpacity(opacity());
    scene->addItem(item);
}
double TraceAnalysisLayer::value(bool node, int i, const QString &field) const
{
    const auto &d = *m_result->dataset;
    const auto &v = node ? m_result->nodes[i] : m_result->links[i];
    if (field == "ratio")
        return v.ratio;
    if (field == "time")
        return v.time_s / 60.;
    if (field == "coverage")
        return v.time_coverage;
    if (field == "flow")
        return node ? d.nodes[i].outgoing_m3s : std::abs(d.links[i].net_flow_m3s);
    if (field == "gross")
        return node ? d.nodes[i].inflow_m3s : d.links[i].absolute_flow_m3s;
    if (field == "local")
        return (node ? d.nodes[i].residence_s : d.links[i].travel_s) / 60.;
    if (field == "volume")
        return node ? d.nodes[i].volume_m3 : missing;
    return 1;
}
QPair<double, double> TraceAnalysisLayer::range(bool node, const Channel &c) const
{
    if (!c.automatic)
        return {c.minimum, c.maximum};
    double lo = missing, hi = missing;
    int count = node ? m_result->nodes.size() : m_result->links.size();
    for (int i = 0; i < count; ++i)
    {
        const auto &v = node ? m_result->nodes[i] : m_result->links[i];
        if (v.ratio == 0)
            continue;
        double x = value(node, i, c.field);
        if (!std::isfinite(x))
            continue;
        lo = std::isfinite(lo) ? std::min(lo, x) : x;
        hi = std::isfinite(hi) ? std::max(hi, x) : x;
    }
    return std::isfinite(lo) ? qMakePair(std::min(0., lo), hi) : qMakePair(0., 1.);
}
double TraceAnalysisLayer::normalized(bool node, int i, const Channel &c) const
{
    double v = value(node, i, c.field);
    if (!std::isfinite(v))
        return missing;
    auto r = range(node, c);
    double lo = transform(r.first, c.transform), hi = transform(r.second, c.transform);
    double t = hi > lo ? std::clamp((transform(v, c.transform) - lo) / (hi - lo), 0., 1.) : .5;
    if (c.classes > 1)
        t = std::min(c.classes - 1, int(t * c.classes)) / double(c.classes - 1);
    return t;
}
void TraceAnalysisLayer::paint(QPainter *p, const QVector<QPointF> &nodes,
                               const QVector<QVector<QPointF>> &links) const
{
    p->save();
    auto tx = p->worldTransform();
    p->resetTransform();
    p->setRenderHint(QPainter::Antialiasing);
    const auto &s = *m_style;
    const auto &d = *m_result->dataset;
    // Resolve ranges once per channel per frame, rather than per feature.
    auto color = [&](bool node, int i, const Channel &c, QPair<double, double> r)
    {
        double v = value(node, i, c.field);
        if (!std::isfinite(v))
            return QColor("#94999f");
        double lo = transform(r.first, c.transform), hi = transform(r.second, c.transform);
        double t = hi > lo ? std::clamp((transform(v, c.transform) - lo) / (hi - lo), 0., 1.) : .5;
        if (c.classes > 1)
            t = std::min(c.classes - 1, int(t * c.classes)) / double(c.classes - 1);
        return c.ramp.colorAt(t);
    };
    auto size = [&](bool node, int i, const Channel &c, QPair<double, double> r)
    {
        double v = value(node, i, c.field), lo = transform(r.first, c.transform),
               hi = transform(r.second, c.transform);
        double t =
            std::isfinite(v)
                ? (hi > lo ? std::clamp((transform(v, c.transform) - lo) / (hi - lo), 0., 1.) : .5)
                : 0.;
        return node ? std::sqrt(c.minimumSize * c.minimumSize +
                                t * (c.maximumSize * c.maximumSize - c.minimumSize * c.minimumSize))
                    : c.minimumSize + t * (c.maximumSize - c.minimumSize);
    };
    auto lc = range(false, s.linkColor), lw = range(false, s.linkWidth),
         nc = range(true, s.nodeColor), ns = range(true, s.nodeSize);
    double alpha = p->opacity();
    for (int i = 0; i < links.size(); ++i)
    {
        if (m_result->links[i].ratio == 0 || links[i].size() < 2)
            continue;
        QVector<QPointF> line;
        bool valid = true;
        for (auto point : links[i])
        {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            {
                valid = false;
                break;
            }
            line.append(tx.map(point));
        }
        if (!valid)
            continue;
        if (d.links[i].direction < 0)
            std::reverse(line.begin(), line.end());
        double width = size(false, i, s.linkWidth, lw);
        QColor c = color(false, i, s.linkColor, lc);
        auto polygon = taperedPath(line, width, width * s.taper());
        if (m_parts[0]->isVisible())
        {
            p->setOpacity(alpha * m_parts[0]->opacity());
            p->setPen(m_highlight == i && !m_highlightNode ? QPen(QColor("#ffb000"), 3)
                                                           : Qt::NoPen);
            p->setBrush(c);
            p->drawPath(polygon);
            if (!std::isfinite(value(false, i, s.linkColor.field)))
            {
                QPainterPath center;
                center.moveTo(line.first());
                for (int k = 1; k < line.size(); ++k)
                    center.lineTo(line[k]);
                p->setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
                p->drawPath(center);
            }
        }
        if (m_parts[2]->isVisible() && s.arrows())
        {
            for (int k = line.size() - 1; k > 0; --k)
            {
                QLineF last(line[k - 1], line[k]);
                if (last.length() < 2)
                    continue;
                QPointF tangent = (line[k] - line[k - 1]) / last.length(),
                        normal(-tangent.y(), tangent.x());
                double length = std::min(last.length() * .45, std::max(5., width));
                int downstream =
                    d.links[i].direction > 0 ? d.snapshot.links[i].to : d.snapshot.links[i].from;
                double clearance =
                    m_parts[1]->isVisible() ? size(true, downstream, s.nodeSize, ns) * .5 + 1 : 0;
                QPointF tip = line[k] - tangent * std::min(clearance, last.length() * .3);
                p->setOpacity(alpha * m_parts[2]->opacity());
                p->setPen(Qt::NoPen);
                p->setBrush(c.darker(130));
                p->drawPolygon(QPolygonF{tip, tip - tangent * length + normal * length * .45,
                                         tip - tangent * length - normal * length * .45});
                break;
            }
        }
    }
    for (int i = 0; i < nodes.size(); ++i)
    {
        if ((m_result->nodes[i].ratio == 0 && i != m_result->seed) || !std::isfinite(nodes[i].x()))
            continue;
        auto pt = tx.map(nodes[i]);
        double diameter = size(true, i, s.nodeSize, ns);
        if (m_parts[1]->isVisible())
        {
            p->setOpacity(alpha * m_parts[1]->opacity());
            p->setBrush(color(true, i, s.nodeColor, nc));
            p->setPen(QPen(i == m_highlight && m_highlightNode ? QColor("#ffb000")
                           : i == m_result->seed               ? Qt::black
                                                               : Qt::white,
                           i == m_result->seed ? 2. : 1.));
            p->drawEllipse(pt, diameter * .5, diameter * .5);
        }
        if (m_parts[3]->isVisible() && s.labels())
        {
            p->setOpacity(alpha * m_parts[3]->opacity());
            p->setPen(Qt::black);
            p->drawText(pt + QPointF(diameter * .5 + 3, -3), d.snapshot.nodes[i].id);
        }
    }
    p->restore();
}
QList<LegendSymbolItem> TraceAnalysisLayer::legend(bool node) const
{
    QList<LegendSymbolItem> out;
    const auto &color = node ? m_style->nodeColor : m_style->linkColor;
    const auto &size = node ? m_style->nodeSize : m_style->linkWidth;
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto &c = channel ? size : color;
        auto r = range(node, c);
        for (int step = 0; step < 3; ++step)
        {
            double t = step * .5;
            double scaled =
                transform(r.first, c.transform) +
                t * (transform(r.second, c.transform) - transform(r.first, c.transform));
            double raw = c.transform == "sqrt"  ? scaled * scaled
                         : c.transform == "log" ? std::expm1(scaled)
                                                : scaled;
            double colorPosition =
                c.classes > 1 ? std::min(c.classes - 1, int(t * c.classes)) / double(c.classes - 1)
                              : t;
            LegendSymbolItem item;
            item.sublayerId = node ? "nodes" : "links";
            item.label = QStringLiteral("%1 · %2: %3")
                             .arg(channel ? (node ? tr("Area") : tr("Width")) : tr("Color"),
                                  fieldLabel(c.field, node), QString::number(raw, 'g', 4));
            SymbolLayer sl;
            sl.kind = node ? SymbolLayerKind::SimpleMarker : SymbolLayerKind::SimpleLine;
            SymbolProps::writeColor(sl.props, node ? "fillColor" : "color",
                                    channel ? QColor("#56667a") : c.ramp.colorAt(colorPosition));
            sl.props.insert(node ? "size" : "width",
                            channel ? (node ? std::sqrt(c.minimumSize * c.minimumSize +
                                                        t * (c.maximumSize * c.maximumSize -
                                                             c.minimumSize * c.minimumSize))
                                            : c.minimumSize + t * (c.maximumSize - c.minimumSize))
                            : node  ? 8.
                                    : 4.);
            item.symbol.layers.append(sl);
            out.append(item);
        }
    }
    LegendSymbolItem unknown;
    unknown.sublayerId = node ? "nodes" : "links";
    unknown.label = tr("Gray / dashed: time unavailable; inspect coverage");
    out.append(unknown);
    return out;
}
bool TraceAnalysisLayer::hitTest(QPointF pixel, const QTransform &mapToPixel,
                                 const SpatialReferenceSystem *canvas, bool *node, int *index) const
{
    if (!isVisible())
        return false;
    OGRCoordinateTransformation *ct =
        srs() && canvas ? srs()->createTransformationTo(*canvas) : nullptr;
    if (srs() && canvas && !srs()->equals(*canvas) && !ct)
        return false;
    auto convert = [&](QPointF p)
    {
        double x = p.x(), y = p.y();
        if (ct && !ct->Transform(1, &x, &y))
            return QPointF(missing, missing);
        return mapToPixel.map(QPointF(x, y));
    };
    auto finish = [&](bool hit)
    {
        if (ct)
            OCTDestroyCoordinateTransformation(ct);
        return hit;
    };
    const auto &snapshot = m_result->dataset->snapshot;
    Channel nodeChannel = m_style->nodeSize, linkChannel = m_style->linkWidth;
    auto nodeRange = range(true, nodeChannel), linkRange = range(false, linkChannel);
    nodeChannel.automatic = linkChannel.automatic = false;
    nodeChannel.minimum = nodeRange.first;
    nodeChannel.maximum = nodeRange.second;
    linkChannel.minimum = linkRange.first;
    linkChannel.maximum = linkRange.second;
    if (m_parts[1]->isVisible())
        for (int i = 0; i < snapshot.nodes.size(); ++i)
        {
            if (!snapshot.nodes[i].hasGeometry || m_result->nodes[i].ratio == 0)
                continue;
            const auto &c = nodeChannel;
            double t = normalized(true, i, c);
            if (!std::isfinite(t))
                t = 0;
            double diameter =
                std::sqrt(c.minimumSize * c.minimumSize +
                          t * (c.maximumSize * c.maximumSize - c.minimumSize * c.minimumSize));
            if (QLineF(pixel, convert(snapshot.nodes[i].point)).length() <= diameter * .5 + 3)
            {
                *node = true;
                *index = i;
                return finish(true);
            }
        }
    if (m_parts[0]->isVisible())
        for (int i = 0; i < snapshot.links.size(); ++i)
        {
            if (m_result->links[i].ratio == 0)
                continue;
            const auto &link = snapshot.links[i];
            QVector<QPointF> points;
            for (auto p : link.points)
                points.append(convert(p));
            if (points.isEmpty() && snapshot.nodes[link.from].hasGeometry &&
                snapshot.nodes[link.to].hasGeometry)
                points = {convert(snapshot.nodes[link.from].point),
                          convert(snapshot.nodes[link.to].point)};
            if (m_result->dataset->links[i].direction < 0)
                std::reverse(points.begin(), points.end());
            const auto &c = linkChannel;
            double t = normalized(false, i, c);
            if (!std::isfinite(t))
                t = 0;
            double width = c.minimumSize + t * (c.maximumSize - c.minimumSize);
            if (taperedPath(points, width + 6, width * m_style->taper() + 6).contains(pixel))
            {
                *node = false;
                *index = i;
                return finish(true);
            }
        }
    return finish(false);
}
void TraceAnalysisLayer::highlight(bool node, int i)
{
    m_highlightNode = node;
    m_highlight = i;
    emit repaintRequested();
}
} // namespace openswmmvis::trace
