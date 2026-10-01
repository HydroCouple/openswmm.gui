#include "render/sublayers/resultscalarsublayer.h"

#include <QSignalBlocker>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render {
using openswmmvis::io::Mesh2DScalarFrame;
using openswmmvis::io::Mesh2DValueStatus;

namespace {
struct FrameRange {
    double minimum = std::numeric_limits<double>::quiet_NaN();
    double maximum = std::numeric_limits<double>::quiet_NaN();
    QVector<double> samples;
};
FrameRange rangeFor(const Mesh2DScalarFrame &frame)
{
    FrameRange range{frame.minimum, frame.maximum, frame.samples};
    const bool needsRange = !std::isfinite(range.minimum) || !std::isfinite(range.maximum)
        || range.maximum < range.minimum;
    const bool needsSamples = range.samples.isEmpty();
    if (!needsRange && !needsSamples) return range;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (size_t cell = 0; cell < frame.values.size(); ++cell) {
        if (cell >= frame.status.size() || frame.status[cell] != Mesh2DValueStatus::Valid
            || !std::isfinite(frame.values[cell])) continue;
        const double value = frame.values[cell];
        if (needsSamples) range.samples.append(value);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (needsRange && minimum <= maximum) { range.minimum = minimum; range.maximum = maximum; }
    return range;
}
LegendSymbolItem swatch(const QString &id, const QString &key, const QString &label,
                       const QColor &color, qreal opacity)
{
    LegendSymbolItem item;
    item.sublayerId = id;
    item.classKey = key;
    item.label = label;
    SymbolLayer fill;
    fill.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(fill.props, QStringLiteral("color"), color);
    item.symbol.layers.append(fill);
    item.symbol.opacity = opacity;
    return item;
}
}

ResultScalarStyle::ResultScalarStyle(QObject *parent) : ScalarFillStyle(parent)
{
    setAttribute(QString());
    auto initialScheme = scheme();
    initialScheme.setRangeMode(RangeMode::PerFrameAutoStretch);
    setScheme(initialScheme);
}
void ResultScalarStyle::setMissingColor(const QColor &color)
{ if (color.isValid() && color != m_missing) { m_missing = color; setDirty(); } }
void ResultScalarStyle::setWaterlessColor(const QColor &color)
{ if (color.isValid() && color != m_waterless) { m_waterless = color; setDirty(); } }
void ResultScalarStyle::setNotApplicableColor(const QColor &color)
{ if (color.isValid() && color != m_notApplicable) { m_notApplicable = color; setDirty(); } }
QJsonObject ResultScalarStyle::toJson() const
{
    auto json = ScalarFillStyle::toJson();
    json.insert(QStringLiteral("missingColor"), m_missing.name(QColor::HexArgb));
    json.insert(QStringLiteral("waterlessColor"), m_waterless.name(QColor::HexArgb));
    json.insert(QStringLiteral("notApplicableColor"), m_notApplicable.name(QColor::HexArgb));
    return json;
}
void ResultScalarStyle::fromJson(const QJsonObject &json)
{
    {
        const QSignalBlocker blocker(this);
        ScalarFillStyle::fromJson(json);
        setMissingColor(QColor(json.value(QStringLiteral("missingColor")).toString()));
        setWaterlessColor(QColor(json.value(QStringLiteral("waterlessColor")).toString()));
        setNotApplicableColor(QColor(json.value(QStringLiteral("notApplicableColor")).toString()));
    }
    setDirty();
}

ResultScalarSublayer::ResultScalarSublayer(QString id, QObject *parent)
    : ISublayer(parent), m_id(id.isEmpty() ? QStringLiteral("results2d.variable.")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) : std::move(id)),
      m_style(new ResultScalarStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, [this] {
        publishPresentedFrame({});
        emit invalidated();
    });
}
QString ResultScalarSublayer::unitsLabel(const openswmmvis::io::Mesh2DResultVariable &descriptor) const
{
    return descriptor.unitsKnown && !descriptor.units.isEmpty() ? descriptor.units : tr("units unknown");
}
QString ResultScalarSublayer::displayName() const
{
    if (m_descriptor.key() != variableKey() || m_descriptor.label.isEmpty())
        return tr("Unavailable result: %1").arg(variableKey());
    return tr("%1 [%2]").arg(m_descriptor.label, unitsLabel(m_descriptor));
}
void ResultScalarSublayer::setDescriptor(const openswmmvis::io::Mesh2DResultVariable &descriptor)
{
    publishPresentedFrame({});
    m_descriptor = descriptor;
    emit invalidated();
}
void ResultScalarSublayer::setVisible(bool value)
{ if (value != m_visible) { if (!value) publishPresentedFrame({}); m_visible = value; emit invalidated(); } }
void ResultScalarSublayer::setOpacity(qreal value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, qreal(0), qreal(1));
    if (value != m_opacity) { if (value <= 0) publishPresentedFrame({}); m_opacity = value; emit invalidated(); }
}
std::shared_ptr<const Mesh2DScalarFrame> ResultScalarSublayer::presentedFrame() const
{
    return std::atomic_load(&m_presentedFrame);
}
void ResultScalarSublayer::publishPresentedFrame(std::shared_ptr<const Mesh2DScalarFrame> frame)
{
    std::atomic_store(&m_presentedFrame, std::move(frame));
}
QVector<QColor> ResultScalarSublayer::cellColors(const Mesh2DScalarFrame &frame) const
{
    if (!m_visible || m_opacity <= 0 || variableKey().isEmpty()
        || frame.descriptor.key() != variableKey() || !frame.error.isEmpty()) return {};
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    const bool validScheme = scheme.validationError(range.minimum, range.maximum).isEmpty();
    const auto edges = scheme.levelEdges(range.minimum, range.maximum, range.samples);
    const int classes = std::max(1, int(edges.size()) - 1);
    QVector<QColor> colors;
    colors.reserve(qsizetype(frame.values.size()));
    for (size_t cell = 0; cell < frame.values.size(); ++cell) {
        const auto status = cell < frame.status.size() ? frame.status[cell] : Mesh2DValueStatus::Missing;
        QColor color;
        if (status == Mesh2DValueStatus::Waterless) color = m_style->waterlessColor();
        else if (status == Mesh2DValueStatus::NotApplicable) color = m_style->notApplicableColor();
        else if (status != Mesh2DValueStatus::Valid || !std::isfinite(frame.values[cell])) color = m_style->missingColor();
        else if (!validScheme) color = m_style->missingColor();
        else if (m_style->classified()) color = scheme.colorForClass(
            ClassificationScheme::classIndexFor(frame.values[cell], edges), classes);
        else color = scheme.colorForValue(frame.values[cell], range.minimum, range.maximum);
        color.setAlphaF(color.alphaF() * m_opacity);
        colors.append(color);
    }
    return colors;
}
QList<LegendSymbolItem> ResultScalarSublayer::legendSymbolItems() const
{
    Mesh2DScalarFrame frame;
    frame.descriptor = m_descriptor;
    return legendSymbolItems(frame);
}
QList<LegendSymbolItem> ResultScalarSublayer::legendSymbolItems(const Mesh2DScalarFrame &frame) const
{
    QList<LegendSymbolItem> result;
    if (!m_visible) return result;
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    const auto effective = scheme.effectiveRange(range.minimum, range.maximum);
    const bool matching = !variableKey().isEmpty() && frame.descriptor.key() == variableKey() && frame.error.isEmpty();
    const QString classificationError = scheme.validationError(range.minimum, range.maximum);
    const QString units = unitsLabel(matching ? frame.descriptor : m_descriptor);
    if (matching && classificationError.isEmpty()
        && std::isfinite(effective.first) && std::isfinite(effective.second)) {
        if (m_style->classified()) {
            if (effective.first == effective.second) {
                auto item = swatch(m_id, QStringLiteral("0"), scheme.formatValue(effective.first),
                                  scheme.colorForClass(0, 1), m_opacity);
                item.range = {effective.first, effective.second};
                item.userLabel = scheme.labelOverride(0);
                result.append(item);
            } else {
                result = scheme.legendItems(range.minimum, range.maximum, range.samples);
            }
            for (auto &item : result) {
                item.sublayerId = m_id;
                item.symbol.opacity = m_opacity;
                item.label += QStringLiteral(" [%1]").arg(units);
            }
        } else {
            const int count = effective.first == effective.second ? 1 : 6;
            for (int i = 0; i < count; ++i) {
                const double value = effective.first + (effective.second - effective.first) * i / std::max(1, count - 1);
                auto item = swatch(m_id, QString::number(i), tr("%1 [%2]").arg(scheme.formatValue(value), units),
                    scheme.colorForValue(value, range.minimum, range.maximum), m_opacity);
                item.range = {value, value};
                result.append(item);
            }
        }
    } else {
        result.append(swatch(m_id, QStringLiteral("unavailable"), matching && !classificationError.isEmpty()
                            ? tr("Invalid classification: %1 [%2]").arg(classificationError, units)
                            : tr("Result unavailable [%1]").arg(units),
                            m_style->missingColor(), m_opacity));
    }
    result.append(swatch(m_id, QStringLiteral("missing"), tr("Missing data"), m_style->missingColor(), m_opacity));
    result.append(swatch(m_id, QStringLiteral("waterless"), tr("Waterless"), m_style->waterlessColor(), m_opacity));
    result.append(swatch(m_id, QStringLiteral("notApplicable"), tr("Not applicable"), m_style->notApplicableColor(), m_opacity));
    return result;
}

} // namespace OpenSWMM::Render
