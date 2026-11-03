/**
 * @file      resultscalarsublayer.cpp
 * @brief     Styling of scalar result fields independently of application chrome.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "render/sublayers/resultscalarsublayer.h"
#include "render/classificationcolorsampler.h"

#include <QSignalBlocker>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render
{
using openswmmvis::io::Mesh2DScalarFrame;
using openswmmvis::io::Mesh2DValueStatus;

namespace
{
struct FrameRange {
    double minimum = std::numeric_limits<double>::quiet_NaN();
    double maximum = std::numeric_limits<double>::quiet_NaN();
    QVector<double> samples;
};
/** @brief Use cached frame ranges/samples where valid, deriving only missing pieces.
 * @param frame Borrowed immutable frame. A finite ordered min/max (including equal)
 * is retained; nonempty samples are retained without validation. Derivation scans
 * finite values with an in-bounds Valid status only.
 * @return Copied range/samples plus derived pieces. If no finite Valid cells exist,
 * invalid incoming bounds stay invalid rather than being replaced with invented values. */
FrameRange rangeFor(const Mesh2DScalarFrame &frame)
{
    FrameRange range{frame.minimum, frame.maximum, frame.samples};
    const bool needsRange = !std::isfinite(range.minimum) || !std::isfinite(range.maximum) ||
                            range.maximum < range.minimum;
    const bool needsSamples = range.samples.isEmpty();
    if (!needsRange && !needsSamples)
        return range;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (size_t cell = 0; cell < frame.values.size(); ++cell) {
        if (cell >= frame.status.size() || frame.status[cell] != Mesh2DValueStatus::Valid ||
            !std::isfinite(frame.values[cell]))
            continue;
        const double value = frame.values[cell];
        if (needsSamples)
            range.samples.append(value);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (needsRange && minimum <= maximum) {
        range.minimum = minimum;
        range.maximum = maximum;
    }
    return range;
}
/** @brief Build one SimpleFill legend swatch from supplied metadata.
 * @param id Sublayer ID copied into the row.
 * @param key Stable class/status key copied verbatim.
 * @param label Display text copied without formatting or translation.
 * @param color Fill QColor copied without validation.
 * @param opacity Raw symbol opacity multiplier, without clamping.
 * @return Independent row; numeric range/user-label overrides remain unset. */
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
    m_contourStyle = new IsolineStyle(this);
    connect(m_contourStyle, &SublayerStyle::styleChanged, this, [this] { setDirty(); });
    setAttribute(QString());
    auto initialScheme = scheme();
    initialScheme.setRangeMode(RangeMode::PerFrameAutoStretch);
    setScheme(initialScheme);
}
ResultScalarStyle::Presentation ResultScalarStyle::presentation() const
{
    if (m_isolines && !m_cellFill && !m_filledContours && !m_cellLabels)
        return Presentation::Contours;
    if (m_cellLabels && !m_cellFill && !m_filledContours && !m_isolines)
        return Presentation::Labels;
    return Presentation::Fill;
}
void ResultScalarStyle::setPresentation(Presentation value)
{
    if (value != Presentation::Fill && value != Presentation::Contours &&
        value != Presentation::Labels)
        return;
    const bool fill = value == Presentation::Fill, lines = value == Presentation::Contours,
               labels = value == Presentation::Labels;
    if (m_cellFill == fill && !m_filledContours && m_isolines == lines && m_cellLabels == labels)
        return;
    m_cellFill = fill;
    m_filledContours = false;
    m_isolines = lines;
    m_cellLabels = labels;
    setDirty();
}
namespace
{
/** @brief Assign a toggle and report whether it changed. */
bool assign(bool &field, bool value)
{
    if (field == value)
        return false;
    field = value;
    return true;
}
bool wholeRun(const ClassificationScheme &scheme)
{
    return !scheme.useCustomRange() && scheme.rangeMode() == RangeMode::FixedOverRun;
}
} // namespace
void ResultScalarStyle::setCellFill(bool on)
{
    if (assign(m_cellFill, on))
        setDirty();
}
void ResultScalarStyle::setSmoothFill(bool on)
{
    if (assign(m_smoothFill, on))
        setDirty();
}
void ResultScalarStyle::setFilledContours(bool on)
{
    if (assign(m_filledContours, on))
        setDirty();
}
void ResultScalarStyle::setIsolines(bool on)
{
    if (assign(m_isolines, on))
        setDirty();
}
void ResultScalarStyle::setCellLabels(bool on)
{
    if (assign(m_cellLabels, on))
        setDirty();
}
bool ResultScalarStyle::needsWholeRunRange() const
{
    return fillNeedsWholeRunRange() || isolinesNeedWholeRunRange();
}
bool ResultScalarStyle::fillNeedsWholeRunRange() const
{
    return (m_cellFill || m_filledContours) && wholeRun(scheme());
}
bool ResultScalarStyle::isolinesNeedWholeRunRange() const
{
    return m_isolines && wholeRun(m_contourStyle->scheme());
}
void ResultScalarStyle::setMissingColor(const QColor &color)
{
    if (color.isValid() && color != m_missing) {
        m_missing = color;
        setDirty();
    }
}
void ResultScalarStyle::setWaterlessColor(const QColor &color)
{
    if (color.isValid() && color != m_waterless) {
        m_waterless = color;
        setDirty();
    }
}
void ResultScalarStyle::setNotApplicableColor(const QColor &color)
{
    if (color.isValid() && color != m_notApplicable) {
        m_notApplicable = color;
        setDirty();
    }
}
QJsonObject ResultScalarStyle::toJson() const
{
    auto json = ScalarFillStyle::toJson();
    json.insert("presentation", int(presentation())); // read by older builds
    json.insert(QStringLiteral("cellFill"), m_cellFill);
    json.insert(QStringLiteral("smoothFill"), m_smoothFill);
    json.insert(QStringLiteral("filledContours"), m_filledContours);
    json.insert(QStringLiteral("isolines"), m_isolines);
    json.insert(QStringLiteral("cellLabels"), m_cellLabels);
    json.insert("contourStyle", m_contourStyle->toJson());
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
        if (json.contains(QStringLiteral("cellFill"))) {
            m_cellFill = json.value(QStringLiteral("cellFill")).toBool(true);
            m_smoothFill = json.value(QStringLiteral("smoothFill")).toBool(false);
            m_filledContours = json.value(QStringLiteral("filledContours")).toBool(false);
            m_isolines = json.value(QStringLiteral("isolines")).toBool(false);
            m_cellLabels = json.value(QStringLiteral("cellLabels")).toBool(false);
        } else // saved before the toggles: one presentation
            setPresentation(Presentation(json.value("presentation").toInt(0)));
        if (json.value("contourStyle").isObject())
            m_contourStyle->fromJson(json.value("contourStyle").toObject());
        setMissingColor(QColor(json.value(QStringLiteral("missingColor")).toString()));
        setWaterlessColor(QColor(json.value(QStringLiteral("waterlessColor")).toString()));
        setNotApplicableColor(QColor(json.value(QStringLiteral("notApplicableColor")).toString()));
    }
    setDirty();
}

ResultScalarSublayer::ResultScalarSublayer(QString id, QObject *parent)
    : ISublayer(parent), m_id(id.isEmpty() ? QStringLiteral("results2d.variable.") +
                                                 QUuid::createUuid().toString(QUuid::WithoutBraces)
                                           : std::move(id)),
      m_style(new ResultScalarStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, [this] {
        publishPresentedFrame({});
        emit invalidated();
    });
}
QString
ResultScalarSublayer::unitsLabel(const openswmmvis::io::Mesh2DResultVariable &descriptor) const
{
    return descriptor.unitsKnown && !descriptor.units.isEmpty() ? descriptor.units
                                                                : tr("units unknown");
}
QString ResultScalarSublayer::displayName() const
{
    if (m_descriptor.key() != variableKey() || m_descriptor.label.isEmpty())
        return tr("Unavailable result: %1").arg(variableKey());
    QStringList shown;
    if (m_style->cellFill())
        shown << (m_style->smoothFill() ? tr("smooth fill") : tr("fill"));
    if (m_style->filledContours())
        shown << tr("filled contours");
    if (m_style->isolines())
        shown << tr("contours");
    if (m_style->cellLabels())
        shown << tr("labels");
    // A plain flat fill keeps the bare variable name.
    const bool plainFill = shown.size() == 1 && m_style->cellFill() && !m_style->smoothFill();
    const QString suffix =
        shown.isEmpty() || plainFill ? QString() : tr(" — %1").arg(shown.join(", "));
    return tr("%1 [%2]%3").arg(m_descriptor.label, unitsLabel(m_descriptor), suffix);
}
void ResultScalarSublayer::setDescriptor(const openswmmvis::io::Mesh2DResultVariable &descriptor)
{
    publishPresentedFrame({});
    m_descriptor = descriptor;
    emit invalidated();
}
void ResultScalarSublayer::setVisible(bool value)
{
    if (value != m_visible) {
        if (!value)
            publishPresentedFrame({});
        m_visible = value;
        emit invalidated();
    }
}
void ResultScalarSublayer::setOpacity(qreal value)
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, qreal(0), qreal(1));
    if (value != m_opacity) {
        if (value <= 0)
            publishPresentedFrame({});
        m_opacity = value;
        emit invalidated();
    }
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
    if (!m_visible || m_opacity <= 0 || variableKey().isEmpty() ||
        frame.descriptor.key() != variableKey() || !frame.error.isEmpty())
        return {};
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    const bool validScheme = scheme.validationError(range.minimum, range.maximum).isEmpty();
    const auto edges = scheme.levelEdges(range.minimum, range.maximum, range.samples);
    const int classes = std::max(1, int(edges.size()) - 1);
    const ClassificationColorSampler palette(scheme, range.minimum, range.maximum,
                                             m_style->classified() ? classes : 0);
    QVector<QColor> colors;
    colors.reserve(qsizetype(frame.values.size()));
    for (size_t cell = 0; cell < frame.values.size(); ++cell) {
        const auto status =
            cell < frame.status.size() ? frame.status[cell] : Mesh2DValueStatus::Missing;
        QColor color;
        if (status == Mesh2DValueStatus::Waterless)
            color = m_style->waterlessColor();
        else if (status == Mesh2DValueStatus::NotApplicable)
            color = m_style->notApplicableColor();
        else if (status != Mesh2DValueStatus::Valid || !std::isfinite(frame.values[cell]))
            color = m_style->missingColor();
        else if (!validScheme)
            color = m_style->missingColor();
        else if (m_style->classified())
            color =
                palette.classColor(ClassificationScheme::classIndexFor(frame.values[cell], edges));
        else
            color = palette.valueColor(frame.values[cell]);
        color.setAlphaF(color.alphaF() * m_opacity);
        colors.append(color);
    }
    return colors;
}
bool ResultScalarSublayer::bandStyling(const Mesh2DScalarFrame &frame, std::vector<double> &levels,
                                       QVector<QColor> &colors) const
{
    levels.clear();
    colors.clear();
    if (!m_visible || m_opacity <= 0 || variableKey().isEmpty() ||
        frame.descriptor.key() != variableKey() || !frame.error.isEmpty())
        return false;
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    if (!scheme.validationError(range.minimum, range.maximum).isEmpty())
        return false;
    const auto edges = scheme.levelEdges(range.minimum, range.maximum, range.samples);
    if (edges.size() < 2)
        return false;
    const int bands = int(edges.size()) - 1;
    const ClassificationColorSampler palette(scheme, range.minimum, range.maximum,
                                             m_style->classified() ? bands : 0);
    levels.assign(edges.cbegin(), edges.cend());
    colors.reserve(bands);
    for (int i = 0; i < bands; ++i) {
        QColor color = m_style->classified() ? palette.classColor(i)
                                             : palette.valueColor(0.5 * (levels[size_t(i)] +
                                                                         levels[size_t(i) + 1]));
        color.setAlphaF(color.alphaF() * m_opacity);
        colors.append(color);
    }
    return true;
}
QVector<QColor> ResultScalarSublayer::valueColors(const Mesh2DScalarFrame &frame,
                                                  const std::vector<double> &values) const
{
    if (!m_visible || m_opacity <= 0 || variableKey().isEmpty() ||
        frame.descriptor.key() != variableKey() || !frame.error.isEmpty())
        return {};
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    if (!scheme.validationError(range.minimum, range.maximum).isEmpty())
        return {};
    const auto edges = scheme.levelEdges(range.minimum, range.maximum, range.samples);
    const int classes = std::max(1, int(edges.size()) - 1);
    const ClassificationColorSampler palette(scheme, range.minimum, range.maximum,
                                             m_style->classified() ? classes : 0);
    QVector<QColor> colors;
    colors.reserve(qsizetype(values.size()));
    for (double value : values) {
        QColor color(Qt::transparent);
        if (std::isfinite(value)) {
            color = m_style->classified()
                        ? palette.classColor(ClassificationScheme::classIndexFor(value, edges))
                        : palette.valueColor(value);
            color.setAlphaF(color.alphaF() * m_opacity);
        }
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
QList<LegendSymbolItem>
ResultScalarSublayer::legendSymbolItems(const Mesh2DScalarFrame &frame, bool labelsOnly) const
{
    QList<LegendSymbolItem> result;
    if (!m_visible)
        return result;
    const auto range = rangeFor(frame);
    const auto &scheme = m_style->scheme();
    const auto effective = scheme.effectiveRange(range.minimum, range.maximum);
    const bool matching = !variableKey().isEmpty() && frame.descriptor.key() == variableKey() &&
                          frame.error.isEmpty();
    const QString classificationError = scheme.validationError(range.minimum, range.maximum);
    const QString units = unitsLabel(matching ? frame.descriptor : m_descriptor);
    // One block per shown display, in paint order: fill/bands share the fill
    // scheme's swatches; isolines and cell labels add one row each.
    const auto lineRow = [&](const QString &label) {
        LegendSymbolItem item;
        item.sublayerId = m_id;
        item.label = label;
        SymbolLayer line;
        line.kind = SymbolLayerKind::SimpleLine;
        SymbolProps::writeColor(line.props, QStringLiteral("color"),
                                m_style->contourStyle()->color());
        line.props.insert(QStringLiteral("width"), m_style->contourStyle()->lineWidthPx());
        item.symbol.layers.append(line);
        item.symbol.opacity = m_opacity;
        return item;
    };
    QList<LegendSymbolItem> lines;
    if (matching && m_style->isolines() && !labelsOnly)
        lines.append(lineRow(
            tr("%1 contours [%2] — display interpolation").arg(frame.descriptor.label, units)));
    if (matching && m_style->cellLabels())
        lines.append(lineRow(tr("%1 cell values [%2]").arg(frame.descriptor.label, units)));
    if (labelsOnly || (matching && !m_style->cellFill() && !m_style->filledContours()))
        return lines;
    if (matching && classificationError.isEmpty() && std::isfinite(effective.first) &&
        std::isfinite(effective.second)) {
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
                const double value = effective.first + (effective.second - effective.first) * i /
                                                           std::max(1, count - 1);
                auto item = swatch(
                    m_id, QString::number(i), tr("%1 [%2]").arg(scheme.formatValue(value), units),
                    scheme.colorForValue(value, range.minimum, range.maximum), m_opacity);
                item.range = {value, value};
                result.append(item);
            }
        }
    } else {
        result.append(
            swatch(m_id, QStringLiteral("unavailable"),
                   matching && !classificationError.isEmpty()
                       ? tr("Invalid classification: %1 [%2]").arg(classificationError, units)
                       : tr("Result unavailable [%1]").arg(units),
                   m_style->missingColor(), m_opacity));
    }
    result.append(swatch(m_id, QStringLiteral("missing"), tr("Missing data"),
                         m_style->missingColor(), m_opacity));
    result.append(swatch(m_id, QStringLiteral("waterless"), tr("Waterless"),
                         m_style->waterlessColor(), m_opacity));
    result.append(swatch(m_id, QStringLiteral("notApplicable"), tr("Not applicable"),
                         m_style->notApplicableColor(), m_opacity));
    result.append(lines);
    return result;
}

} // namespace OpenSWMM::Render
