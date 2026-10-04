// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/traceanalysislayer.h"
#include "layers/swmmresultslayer.h"
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
double position(double v, const Channel &c, QPair<double, double> r)
{
    if (c.field == "uniform")
        return 1;
    if (!std::isfinite(v))
        return missing;
    double lo = transform(r.first, c.transform), hi = transform(r.second, c.transform);
    return hi > lo ? std::clamp((transform(v, c.transform) - lo) / (hi - lo), 0., 1.) : .5;
}
double symbolSize(double v, bool node, const Channel &c, QPair<double, double> r)
{
    double t = position(v, c, r);
    if (!std::isfinite(t))
        t = 0;
    return node ? std::sqrt(c.minimumSize * c.minimumSize +
                            t * (c.maximumSize * c.maximumSize - c.minimumSize * c.minimumSize))
                : c.minimumSize + t * (c.maximumSize - c.minimumSize);
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
Channel::Channel() { colors.setMode(ClassificationScheme::ClassMode::Continuous); }
QJsonObject Channel::toJson() const
{
    return {{"field", field},
            {"transform", transform},
            {"automatic", automatic},
            {"minimum", minimum},
            {"maximum", maximum},
            {"minimumSize", minimumSize},
            {"maximumSize", maximumSize},
            {"proportional", proportional},
            {"colors", colors.toJson()}};
}
Channel Channel::fromJson(const QJsonObject &j)
{
    Channel c;
    c.field = j.value("field").toString("ratio");
    c.transform = j.value("transform").toString("linear");
    c.automatic = j.value("automatic").toBool(true);
    c.minimum = j.value("minimum").toDouble();
    c.maximum = j.value("maximum").toDouble(1);
    c.minimumSize = std::clamp(j.value("minimumSize").toDouble(2), 0., 80.);
    c.maximumSize = std::clamp(j.value("maximumSize").toDouble(14), .1, 80.);
    c.proportional = j.value("proportional").toBool(true);
    if (j.contains("colors"))
        c.colors = ClassificationScheme::fromJson(j.value("colors").toObject());
    else
    {
        // Migrate the original compact theme without losing its range or ramp.
        c.colors.setUseCustomRange(!c.automatic);
        c.colors.setRangeMin(c.minimum);
        c.colors.setRangeMax(c.maximum);
        int classes = j.value("classes").toInt();
        if (classes > 1)
        {
            c.colors.setMode(ClassificationScheme::ClassMode::Classified);
            c.colors.setClassCount(classes);
        }
        if (j.contains("ramp"))
            c.colors.setCustomRamp(RasterColorRamp::fromJson(j.value("ramp").toObject()),
                                   QObject::tr("Analysis"));
    }
    return c;
}
TraceLabels::TraceLabels()
{
    appearance.fieldName = "ratio";
    appearance.haloEnabled = true;
}
QJsonObject TraceLabels::toJson() const
{
    return {{"appearance", appearance.toJson()},
            {"showId", showId},
            {"percent", percent},
            {"precision", precision}};
}
TraceLabels TraceLabels::fromJson(const QJsonObject &j)
{
    TraceLabels labels;
    if (j.contains("appearance"))
        labels.appearance.fromJson(j.value("appearance").toObject());
    labels.showId = j.value("showId").toBool(true);
    labels.percent = j.value("percent").toBool(true);
    labels.precision = std::clamp(j.value("precision").toInt(2), 0, 8);
    return labels;
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
    if (v != labels())
    {
        nodeLabels.appearance.enabled = v;
        setDirty();
    }
}
QJsonObject TraceStyle::toJson() const
{
    return {{"schema", 2},
            {"linkColor", linkColor.toJson()},
            {"linkWidth", linkWidth.toJson()},
            {"nodeColor", nodeColor.toJson()},
            {"nodeSize", nodeSize.toJson()},
            {"taper", m_taper},
            {"arrows", m_arrows},
            {"labels", labels()},
            {"linkLabels", linkLabels.toJson()},
            {"nodeLabels", nodeLabels.toJson()}};
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
    linkLabels = TraceLabels::fromJson(j.value("linkLabels").toObject());
    nodeLabels = TraceLabels::fromJson(j.value("nodeLabels").toObject());
    if (!j.contains("nodeLabels"))
    {
        nodeLabels.appearance.enabled = j.value("labels").toBool(false);
        nodeLabels.appearance.fieldName = "id";
    }
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
        return QObject::tr("Flow fraction (passage ratio)");
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
    return position(value(node, i, c.field), c, range(node, c));
}
QVector<double> TraceAnalysisLayer::samples(bool node, const Channel &c) const
{
    QVector<double> out;
    const auto &values = node ? m_result->nodes : m_result->links;
    for (int i = 0; i < values.size(); ++i)
        if (values[i].ratio != 0 && std::isfinite(value(node, i, c.field)))
            out.append(value(node, i, c.field));
    return out;
}
QVector<double> TraceAnalysisLayer::colorEdges(bool node, const Channel &channel) const
{
    // The saved dataset is immutable. Reclassify only after a field or scheme
    // edit, so data-driven methods do not rerun on every pan and repaint.
    auto &cache = node ? m_nodeColorCache : m_linkColorCache;
    if (cache.field != channel.field || cache.revision != channel.colors.revision())
    {
        auto c = channel;
        c.automatic = true;
        const auto data = range(node, c);
        cache.edges = c.colors.levelEdges(data.first, data.second, samples(node, c));
        cache.field = c.field;
        cache.revision = c.colors.revision();
    }
    return cache.edges;
}
QVector<QColor> TraceAnalysisLayer::colors(bool node) const
{
    auto c = node ? m_style->nodeColor : m_style->linkColor;
    c.automatic = true;
    const auto data = range(node, c);
    const auto r = c.colors.effectiveRange(data.first, data.second);
    const bool classified = c.colors.mode() == ClassificationScheme::ClassMode::Classified;
    const auto edges = classified ? colorEdges(node, c) : QVector<double>{};
    QVector<QColor> out(node ? m_result->nodes.size() : m_result->links.size());
    for (int i = 0; i < out.size(); ++i)
    {
        double v = value(node, i, c.field);
        out[i] = !std::isfinite(v) ? QColor("#94999f")
                 : classified
                     ? c.colors.colorForClass(ClassificationScheme::classIndexFor(v, edges),
                                              std::max(1, int(edges.size()) - 1))
                     : c.colors.colorAtF(position(v, c, r));
    }
    return out;
}
QString TraceAnalysisLayer::labelText(bool node, int i) const
{
    const auto &labels = node ? m_style->nodeLabels : m_style->linkLabels;
    if (!labels.appearance.enabled)
        return {};
    const auto &d = *m_result->dataset;
    const auto id = node ? d.snapshot.nodes[i].id : d.snapshot.links[i].id;
    const auto field = labels.appearance.fieldName;
    if (field == "id" || field.isEmpty())
        return id;
    double v = value(node, i, field);
    QString unit;
    if (field == "flow" || field == "gross")
        unit = " m³/s";
    else if (field == "time" || field == "local")
        unit = " min";
    else if (field == "volume")
        unit = " m³";
    else if ((field == "ratio" || field == "coverage") && labels.percent)
    {
        v *= 100;
        unit = "%";
    }
    QString text =
        std::isfinite(v) ? QString::number(v, 'f', labels.precision) + unit : tr("Unavailable");
    const auto &estimate = node ? m_result->nodes[i] : m_result->links[i];
    if (field == "time" && std::isfinite(v) && estimate.time_coverage < 1 - 1e-6)
        text += tr(" (%1% coverage)").arg(QString::number(100 * estimate.time_coverage, 'f', 0));
    return labels.showId ? id + ": " + text : text;
}
void TraceAnalysisLayer::paint(QPainter *p, const QVector<QPointF> &nodes,
                               const QVector<QVector<QPointF>> &links) const
{
    auto *part = dynamic_cast<TraceSublayer *>(parent());
    auto *output = part ? qobject_cast<::SWMMResultsLayer *>(part->parent()) : nullptr;
    if (output && !output->isVisible())
        return;
    p->save();
    if (output)
        p->setOpacity(p->opacity() * output->opacity());
    auto tx = p->worldTransform();
    p->resetTransform();
    p->setRenderHint(QPainter::Antialiasing);
    const auto &s = *m_style;
    const auto &d = *m_result->dataset;
    const auto linkColors = colors(false), nodeColors = colors(true);
    const auto widths = linkWidths(), diameters = nodeSizes();
    struct PendingLabel
    {
        QPointF anchor;
        bool node;
        int index;
    };
    QVector<PendingLabel> labels;
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
        double width = widths[i];
        QColor c = linkColors[i];
        auto polygon = taperedPath(line, width, width);
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
        if (m_parts[0]->isVisible() && s.linkLabels.appearance.enabled)
        {
            double total = 0;
            for (int k = 1; k < line.size(); ++k)
                total += QLineF(line[k - 1], line[k]).length();
            double remaining = total * .5;
            for (int k = 1; k < line.size(); ++k)
            {
                QLineF segment(line[k - 1], line[k]);
                if (segment.length() <= 1e-8)
                    continue;
                if (remaining <= segment.length())
                {
                    labels.append({segment.pointAt(remaining / segment.length()) +
                                       QPointF(0, -width * .5 - 4),
                                   false, i});
                    break;
                }
                remaining -= segment.length();
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
                double clearance = m_parts[1]->isVisible() ? diameters[downstream] * .5 + 1 : 0;
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
        double diameter = diameters[i];
        if (m_parts[1]->isVisible())
        {
            p->setOpacity(alpha * m_parts[1]->opacity());
            p->setBrush(nodeColors[i]);
            p->setPen(QPen(i == m_highlight && m_highlightNode ? QColor("#ffb000")
                           : i == m_result->seed               ? Qt::black
                                                               : Qt::white,
                           i == m_result->seed ? 2. : 1.));
            p->drawEllipse(pt, diameter * .5, diameter * .5);
        }
        if (m_parts[1]->isVisible() && s.nodeLabels.appearance.enabled)
            labels.append({pt + QPointF(diameter * .5 + 4, -3), true, i});
    }
    // Paint text last so downstream symbols cannot cover it. A glyph halo
    // keeps numeric values legible over the colored network and basemap.
    if (m_parts[3]->isVisible())
        for (const auto &label : labels)
        {
            const auto &cfg = label.node ? s.nodeLabels.appearance : s.linkLabels.appearance;
            QPainterPath text;
            text.addText(QPointF(), cfg.effectiveFont(), labelText(label.node, label.index));
            QPointF origin = label.anchor;
            if (!label.node)
                origin.rx() -= text.boundingRect().center().x();
            text.translate(origin);
            p->setOpacity(alpha * m_parts[3]->opacity());
            p->setPen(cfg.haloEnabled ? QPen(cfg.haloColor, 2 * cfg.haloRadiusPx, Qt::SolidLine,
                                             Qt::RoundCap, Qt::RoundJoin)
                                      : QPen(Qt::NoPen));
            p->setBrush(cfg.color);
            p->drawPath(text);
            if (cfg.haloEnabled)
                p->fillPath(text, cfg.color);
        }
    p->restore();
}
QList<LegendSymbolItem> TraceAnalysisLayer::legend(bool node) const
{
    QList<LegendSymbolItem> out;
    for (bool size : {false, true})
    {
        auto c = size ? (node ? m_style->nodeSize : m_style->linkWidth)
                      : (node ? m_style->nodeColor : m_style->linkColor);
        if (!size)
            c.automatic = true;
        auto r = range(node, c);
        if (!size)
            r = c.colors.effectiveRange(r.first, r.second);
        bool proportional = size && !node && c.proportional;
        if (proportional)
        {
            r = {0, widthReference()};
            c.transform = "linear";
        }
        LegendSymbolItem heading;
        heading.sublayerId = node ? "nodes" : "links";
        heading.label = QStringLiteral("%1 · %2")
                            .arg(size ? (node ? tr("Node area") : tr("Link width"))
                                      : (node ? tr("Node color") : tr("Link color")),
                                 fieldLabel(c.field, node));
        out.append(heading);
        auto add = [&](QString text, QColor color, double pixels)
        {
            LegendSymbolItem item;
            item.sublayerId = node ? "nodes" : "links";
            const QString unit = c.field == "flow" || c.field == "gross" ? " m³/s"
                                 : c.field == "time" || c.field == "local" ? " min"
                                 : c.field == "volume" ? " m³" : QString();
            item.label = text + unit;
            SymbolLayer sl;
            sl.kind = node ? SymbolLayerKind::SimpleMarker : SymbolLayerKind::SimpleLine;
            SymbolProps::writeColor(sl.props, node ? "fillColor" : "color", color);
            sl.props.insert(node ? "size" : "width", pixels);
            item.symbol.layers.append(sl);
            out.append(item);
        };
        if (!size && c.colors.mode() == ClassificationScheme::ClassMode::Classified)
        {
            const auto edges = colorEdges(node, c);
            if (edges.size() < 2)
                add(c.colors.formatValue(r.first), c.colors.colorForClass(0, 1), node ? 8 : 4);
            for (int k = 0; k + 1 < edges.size(); ++k)
            {
                QString text = c.colors.labelOverride(k);
                if (text.isEmpty())
                    text =
                        c.colors.formatValue(edges[k]) + " – " + c.colors.formatValue(edges[k + 1]);
                add(text, c.colors.colorForClass(k, edges.size() - 1), node ? 8 : 4);
            }
        }
        else
            for (int step = 0; step < (c.field == "uniform" ? 1 : 3); ++step)
            {
                double t = step * .5;
                double scaled =
                    transform(r.first, c.transform) +
                    t * (transform(r.second, c.transform) - transform(r.first, c.transform));
                double raw = c.transform == "sqrt"  ? scaled * scaled
                             : c.transform == "log" ? std::expm1(scaled)
                                                    : scaled;
                double pixels = !size ? (node ? 8 : 4)
                                : proportional
                                    ? (c.field == "uniform" ? c.maximumSize : t * c.maximumSize)
                                    : symbolSize(raw, node, c, r);
                add(c.field == "uniform" ? tr("Constant") : c.colors.formatValue(raw),
                    size ? QColor("#56667a") : c.colors.colorAtF(c.field == "uniform" ? 1 : t),
                    pixels);
            }
    }
    LegendSymbolItem unknown;
    unknown.sublayerId = node ? "nodes" : "links";
    unknown.label = tr("Gray / dashed: unavailable; partial time labels show coverage");
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
    const auto widths = linkWidths();
    const auto diameters = nodeSizes();
    if (m_parts[1]->isVisible())
        for (int i = 0; i < snapshot.nodes.size(); ++i)
        {
            if (!snapshot.nodes[i].hasGeometry || m_result->nodes[i].ratio == 0)
                continue;
            double diameter = diameters[i];
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
            double width = widths[i];
            if (taperedPath(points, width + 6, width + 6).contains(pixel))
            {
                *node = false;
                *index = i;
                return finish(true);
            }
        }
    return finish(false);
}
double TraceAnalysisLayer::widthReference(const Channel &c) const
{
    if (!c.automatic && c.maximum > 0)
        return c.maximum;
    QVector<double> in(m_result->nodes.size()), out(in.size());
    const auto &d = *m_result->dataset;
    double maximum = 0;
    for (int i = 0; i < d.links.size(); ++i)
    {
        if (m_result->links[i].ratio == 0)
            continue;
        double v = value(false, i, c.field);
        if (!std::isfinite(v) || v < 0)
            continue;
        const auto &link = d.snapshot.links[i];
        int a = d.links[i].direction < 0 ? link.to : link.from;
        int b = d.links[i].direction < 0 ? link.from : link.to;
        out[a] += v;
        in[b] += v;
        maximum = std::max({maximum, out[a], in[b]});
    }
    return maximum > 0 ? maximum : 1;
}
QVector<double> TraceAnalysisLayer::linkWidths() const
{
    const auto &c = m_style->linkWidth;
    double reference = widthReference();
    const auto r = range(false, c);
    QVector<double> result(m_result->links.size());
    for (int i = 0; i < result.size(); ++i)
    {
        double v = value(false, i, c.field);
        result[i] = c.field == "uniform" ? c.maximumSize
                    : !c.proportional    ? symbolSize(v, false, c, r)
                    : std::isfinite(v)   ? std::max(0., v) / reference * c.maximumSize
                                         : 1.;
    }
    return result;
}
QVector<double> TraceAnalysisLayer::nodeSizes() const
{
    const auto &c = m_style->nodeSize;
    const auto r = range(true, c);
    QVector<double> result(m_result->nodes.size());
    for (int i = 0; i < result.size(); ++i)
        result[i] = symbolSize(value(true, i, c.field), true, c, r);
    return result;
}
TraceSublayer::TraceSublayer(SWMMResultsLayer *owner, std::shared_ptr<Result> result, bool travel)
    : ISublayer(owner), m_travel(travel)
{
    // Flow and time views share immutable numbers, but have independent themes.
    m_layer = new TraceAnalysisLayer(std::make_shared<Result>(*result), owner->workspace());
    m_layer->setParent(this);
    if (result->style.isEmpty() && travel)
    {
        m_layer->traceStyle()->linkColor.field = "time";
        m_layer->traceStyle()->nodeColor.field = "time";
        m_layer->traceStyle()->linkLabels.appearance.fieldName = "time";
        m_layer->traceStyle()->nodeLabels.appearance.fieldName = "time";
        m_layer->traceStyle()->changed();
    }
    connect(m_layer, &OpenSWMMVisLayer::repaintRequested, this, &ISublayer::invalidate);
    connect(m_layer, &OpenSWMMVisLayer::visibilityChanged, this, &ISublayer::invalidate);
    connect(m_layer, &OpenSWMMVisLayer::opacityChanged, this, &ISublayer::invalidate);
}
QString TraceSublayer::id() const
{
    return QStringLiteral("trace:%1:%2").arg(m_layer->result()->id, m_travel ? "time" : "flow");
}
QString TraceSublayer::displayName() const
{
    const auto &r = *m_layer->result();
    return tr("%1 — %2 %3")
        .arg(m_travel ? tr("Travel time") : tr("Flow balance"),
             r.direction ? tr("upstream of") : tr("downstream of"),
             r.dataset->snapshot.nodes[r.seed].id);
}
QList<LegendSymbolItem> TraceSublayer::legendSymbolItems() const
{
    LegendSymbolItem heading;
    heading.label = displayName();
    QList<LegendSymbolItem> rows{heading};
    rows.append(m_layer->legend(false));
    rows.append(m_layer->legend(true));
    for (auto &row : rows)
        row.sublayerId = id();
    return rows;
}
void TraceAnalysisLayer::highlight(bool node, int i)
{
    m_highlightNode = node;
    m_highlight = i;
    emit repaintRequested();
}
} // namespace openswmmvis::trace
