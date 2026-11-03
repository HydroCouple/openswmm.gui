/**
 * @file      resultvectorsublayer.cpp
 * @brief     Arrows and streaks for one vector result (an x/y component pair).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "render/sublayers/resultvectorsublayer.h"
#include "render/classificationcolorsampler.h"

#include <QSignalBlocker>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render
{
namespace
{
/** @brief Assign and report a change. */
template <typename T> bool assign(T &field, const T &value)
{
    if (field == value)
        return false;
    field = value;
    return true;
}
/** @return Finite value clamped to [lo, hi], or the fallback. */
double clampFinite(double value, double lo, double hi, double fallback)
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
} // namespace

ResultVectorStyle::ResultVectorStyle(QObject *parent) : SublayerStyle(parent)
{
    m_colorScheme.setRampName(QStringLiteral("Viridis"));
    m_colorScheme.setMode(ClassificationScheme::ClassMode::Continuous);
}

void ResultVectorStyle::setVectorKey(const QString &key)
{
    if (assign(m_vectorKey, key))
        setDirty();
}
void ResultVectorStyle::setArrows(bool on)
{
    if (assign(m_arrows, on))
        setDirty();
}
void ResultVectorStyle::setStreaks(bool on)
{
    if (assign(m_streaks, on))
        setDirty();
}
void ResultVectorStyle::setSpacingPx(double px)
{
    // 1 px draws every cell under the glyph cap, as the former velocity arrows did.
    if (assign(m_spacingPx, clampFinite(px, 1, 1000, m_spacingPx)))
        setDirty();
}
void ResultVectorStyle::setMinLengthPx(double px)
{
    if (assign(m_minLengthPx, clampFinite(px, 0, 1000, m_minLengthPx)))
        setDirty();
}
void ResultVectorStyle::setMaxLengthPx(double px)
{
    if (assign(m_maxLengthPx, clampFinite(px, 1, 1000, m_maxLengthPx)))
        setDirty();
}
void ResultVectorStyle::setHeadSizePx(double px)
{
    if (assign(m_headSizePx, clampFinite(px, 0, 200, m_headSizePx)))
        setDirty();
}
void ResultVectorStyle::setShaftWidthPx(double px)
{
    if (assign(m_shaftWidthPx, clampFinite(px, 0.1, 50, m_shaftWidthPx)))
        setDirty();
}
void ResultVectorStyle::setLengthScaling(LengthScaling scaling)
{
    if (scaling != LengthScaling::Linear && scaling != LengthScaling::SquareRoot &&
        scaling != LengthScaling::Log)
        return;
    if (assign(m_lengthScaling, scaling))
        setDirty();
}
void ResultVectorStyle::setSizeMode(SizeMode mode)
{
    if (mode != SizeMode::Magnitude && mode != SizeMode::Constant && mode != SizeMode::Scalar &&
        mode != SizeMode::FixedScale)
        return;
    if (assign(m_sizeMode, mode))
        setDirty();
}
void ResultVectorStyle::setScalePxPerUnit(double px)
{
    if (assign(m_scalePxPerUnit, clampFinite(px, 0, 1e9, m_scalePxPerUnit)))
        setDirty();
}
void ResultVectorStyle::setDryDepthCutoff(double metres)
{
    if (assign(m_dryDepthCutoff, clampFinite(metres, 0, 1000, m_dryDepthCutoff)))
        setDirty();
}
void ResultVectorStyle::setSizeKey(const QString &key)
{
    if (assign(m_sizeKey, key))
        setDirty();
}
void ResultVectorStyle::setSizeUseCustomRange(bool on)
{
    if (assign(m_sizeUseCustomRange, on))
        setDirty();
}
void ResultVectorStyle::setSizeRangeMin(double value)
{
    if (std::isfinite(value) && assign(m_sizeRangeMin, value))
        setDirty();
}
void ResultVectorStyle::setSizeRangeMax(double value)
{
    if (std::isfinite(value) && assign(m_sizeRangeMax, value))
        setDirty();
}
void ResultVectorStyle::setColorMode(ColorMode mode)
{
    if (mode != ColorMode::Constant && mode != ColorMode::Magnitude && mode != ColorMode::Scalar)
        return;
    if (assign(m_colorMode, mode))
        setDirty();
}
void ResultVectorStyle::setColorKey(const QString &key)
{
    if (assign(m_colorKey, key))
        setDirty();
}
void ResultVectorStyle::setColor(const QColor &color)
{
    if (color.isValid() && assign(m_color, color))
        setDirty();
}
void ResultVectorStyle::setStreakLengthPx(double px)
{
    if (assign(m_streakLengthPx, clampFinite(px, 4, 1000, m_streakLengthPx)))
        setDirty();
}
void ResultVectorStyle::setStreakWidthPx(double px)
{
    if (assign(m_streakWidthPx, clampFinite(px, 0.1, 50, m_streakWidthPx)))
        setDirty();
}
void ResultVectorStyle::setStreakSpacingPx(double px)
{
    if (assign(m_streakSpacingPx, clampFinite(px, 4, 1000, m_streakSpacingPx)))
        setDirty();
}
void ResultVectorStyle::setColorScheme(const ClassificationScheme &scheme)
{
    if (m_colorScheme.toJson() == scheme.toJson())
        return;
    m_colorScheme = scheme;
    setDirty();
}

double ResultVectorStyle::lengthPxFor(double value, double lo, double hi) const
{
    const double shortest = std::min(m_minLengthPx, m_maxLengthPx),
                 longest = std::max(m_minLengthPx, m_maxLengthPx);
    if (m_sizeMode == SizeMode::Constant)
        return longest;
    if (!std::isfinite(value))
        return 0;
    if (m_sizeMode == SizeMode::FixedScale) {
        if (value <= 0)
            return 0;
        const double f = m_lengthScaling == LengthScaling::SquareRoot ? std::sqrt(value)
                         : m_lengthScaling == LengthScaling::Log      ? std::log1p(value)
                                                                      : value;
        return std::clamp(m_scalePxPerUnit * f, shortest, longest);
    }
    double t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0, 1.0) : 1.0;
    switch (m_lengthScaling) {
    case LengthScaling::SquareRoot:
        t = std::sqrt(t);
        break;
    case LengthScaling::Log:
        t = std::log1p(9 * t) / std::log(10.0);
        break;
    case LengthScaling::Linear:
        break;
    }
    return shortest + (longest - shortest) * t;
}

QJsonObject ResultVectorStyle::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("vectorKey"), m_vectorKey);
    json.insert(QStringLiteral("arrows"), m_arrows);
    json.insert(QStringLiteral("streaks"), m_streaks);
    json.insert(QStringLiteral("spacingPx"), m_spacingPx);
    json.insert(QStringLiteral("minLengthPx"), m_minLengthPx);
    json.insert(QStringLiteral("maxLengthPx"), m_maxLengthPx);
    json.insert(QStringLiteral("headSizePx"), m_headSizePx);
    json.insert(QStringLiteral("shaftWidthPx"), m_shaftWidthPx);
    json.insert(QStringLiteral("lengthScaling"), int(m_lengthScaling));
    json.insert(QStringLiteral("sizeMode"), int(m_sizeMode));
    json.insert(QStringLiteral("scalePxPerUnit"), m_scalePxPerUnit);
    json.insert(QStringLiteral("dryDepthCutoff"), m_dryDepthCutoff);
    json.insert(QStringLiteral("sizeKey"), m_sizeKey);
    json.insert(QStringLiteral("sizeUseCustomRange"), m_sizeUseCustomRange);
    json.insert(QStringLiteral("sizeRangeMin"), m_sizeRangeMin);
    json.insert(QStringLiteral("sizeRangeMax"), m_sizeRangeMax);
    json.insert(QStringLiteral("colorMode"), int(m_colorMode));
    json.insert(QStringLiteral("colorKey"), m_colorKey);
    json.insert(QStringLiteral("color"), m_color.name(QColor::HexArgb));
    json.insert(QStringLiteral("streakLengthPx"), m_streakLengthPx);
    json.insert(QStringLiteral("streakWidthPx"), m_streakWidthPx);
    json.insert(QStringLiteral("streakSpacingPx"), m_streakSpacingPx);
    json.insert(QStringLiteral("colorScheme"), m_colorScheme.toJson());
    return json;
}

void ResultVectorStyle::fromJson(const QJsonObject &json)
{
    {
        const QSignalBlocker blocker(this);
        const auto number = [&](const char *key, double fallback) {
            return json.value(QLatin1String(key)).toDouble(fallback);
        };
        if (json.contains(QStringLiteral("vectorKey")))
            setVectorKey(json.value(QStringLiteral("vectorKey")).toString());
        setArrows(json.value(QStringLiteral("arrows")).toBool(m_arrows));
        setStreaks(json.value(QStringLiteral("streaks")).toBool(m_streaks));
        setSpacingPx(number("spacingPx", m_spacingPx));
        setMinLengthPx(number("minLengthPx", m_minLengthPx));
        setMaxLengthPx(number("maxLengthPx", m_maxLengthPx));
        setHeadSizePx(number("headSizePx", m_headSizePx));
        setShaftWidthPx(number("shaftWidthPx", m_shaftWidthPx));
        setLengthScaling(
            LengthScaling(json.value(QStringLiteral("lengthScaling")).toInt(int(m_lengthScaling))));
        setSizeMode(SizeMode(json.value(QStringLiteral("sizeMode")).toInt(int(m_sizeMode))));
        setScalePxPerUnit(number("scalePxPerUnit", m_scalePxPerUnit));
        setDryDepthCutoff(number("dryDepthCutoff", m_dryDepthCutoff));
        if (json.contains(QStringLiteral("sizeKey")))
            setSizeKey(json.value(QStringLiteral("sizeKey")).toString());
        setSizeUseCustomRange(
            json.value(QStringLiteral("sizeUseCustomRange")).toBool(m_sizeUseCustomRange));
        setSizeRangeMin(number("sizeRangeMin", m_sizeRangeMin));
        setSizeRangeMax(number("sizeRangeMax", m_sizeRangeMax));
        setColorMode(ColorMode(json.value(QStringLiteral("colorMode")).toInt(int(m_colorMode))));
        if (json.contains(QStringLiteral("colorKey")))
            setColorKey(json.value(QStringLiteral("colorKey")).toString());
        setColor(QColor(json.value(QStringLiteral("color")).toString()));
        setStreakLengthPx(number("streakLengthPx", m_streakLengthPx));
        setStreakWidthPx(number("streakWidthPx", m_streakWidthPx));
        setStreakSpacingPx(number("streakSpacingPx", m_streakSpacingPx));
        if (json.value(QStringLiteral("colorScheme")).isObject())
            setColorScheme(
                ClassificationScheme::fromJson(json.value(QStringLiteral("colorScheme")).toObject()));
    }
    setDirty();
}

ResultVectorSublayer::ResultVectorSublayer(QString id, QObject *parent)
    : ISublayer(parent), m_id(id.isEmpty() ? QStringLiteral("results2d.vector.") +
                                                 QUuid::createUuid().toString(QUuid::WithoutBraces)
                                           : std::move(id)),
      m_style(new ResultVectorStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

QString ResultVectorSublayer::displayName() const
{
    if (m_descriptor.key() != vectorKey() || m_descriptor.label.isEmpty())
        return tr("Unavailable vector result: %1").arg(vectorKey());
    QStringList shown;
    if (m_style->arrows())
        shown << tr("arrows");
    if (m_style->streaks())
        shown << tr("streaks");
    const QString units = m_descriptor.x.unitsKnown && !m_descriptor.x.units.isEmpty()
                              ? m_descriptor.x.units
                              : tr("units unknown");
    return shown.isEmpty() ? tr("%1 [%2]").arg(m_descriptor.label, units)
                           : tr("%1 [%2] — %3").arg(m_descriptor.label, units, shown.join(", "));
}

void ResultVectorSublayer::setVisible(bool value)
{
    if (value != m_visible) {
        m_visible = value;
        emit invalidated();
    }
}

void ResultVectorSublayer::setOpacity(qreal value)
{
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, qreal(0), qreal(1));
    if (value != m_opacity) {
        m_opacity = value;
        emit invalidated();
    }
}

void ResultVectorSublayer::setDescriptor(const openswmmvis::io::Mesh2DResultVector &descriptor)
{
    m_descriptor = descriptor;
    emit invalidated();
}

QColor ResultVectorSublayer::colorFor(double value, double lo, double hi) const
{
    QColor color = m_style->color();
    if (m_style->colorMode() != ResultVectorStyle::ColorMode::Constant) {
        const auto &scheme = m_style->colorScheme();
        if (!std::isfinite(value) || !scheme.validationError(lo, hi).isEmpty())
            return QColor(Qt::transparent);
        if (scheme.mode() == ClassificationScheme::ClassMode::Classified) {
            const auto edges = scheme.levelEdges(lo, hi);
            const int classes = std::max(1, int(edges.size()) - 1);
            color = scheme.colorForClass(ClassificationScheme::classIndexFor(value, edges), classes);
        } else {
            const auto range = scheme.effectiveRange(lo, hi);
            color = ClassificationColorSampler(scheme, lo, hi)
                        .valueColor(std::clamp(value, range.first, range.second));
        }
    }
    color.setAlphaF(color.alphaF() * m_opacity);
    return color;
}

QList<LegendSymbolItem> ResultVectorSublayer::legendSymbolItems() const
{
    return legendSymbolItems(std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(), QString());
}

QList<LegendSymbolItem> ResultVectorSublayer::legendSymbolItems(double colorMin, double colorMax,
                                                                const QString &colorUnits) const
{
    QList<LegendSymbolItem> rows;
    if (!m_visible)
        return rows;
    const auto row = [&](const QString &key, const QString &label, const QColor &color) {
        LegendSymbolItem item;
        item.sublayerId = m_id;
        item.classKey = key;
        item.label = label;
        SymbolLayer marker;
        marker.kind = SymbolLayerKind::SimpleMarker;
        marker.props.insert(QStringLiteral("shape"), QStringLiteral("arrow"));
        SymbolProps::writeColor(marker.props, QStringLiteral("color"), color);
        marker.props.insert(QStringLiteral("size"), 12.0);
        item.symbol.layers.append(marker);
        item.symbol.opacity = m_opacity;
        return item;
    };
    const QString name = displayName();
    const auto &scheme = m_style->colorScheme();
    if (m_style->colorMode() == ResultVectorStyle::ColorMode::Constant ||
        !std::isfinite(colorMin) || !std::isfinite(colorMax) ||
        !scheme.validationError(colorMin, colorMax).isEmpty()) {
        rows.append(row(QStringLiteral("vector"), name, m_style->color()));
        return rows;
    }
    rows.append(row(QStringLiteral("vector"), name, QColor(Qt::transparent)));
    const QString units = colorUnits.isEmpty() ? tr("units unknown") : colorUnits;
    if (scheme.mode() == ClassificationScheme::ClassMode::Classified) {
        const auto edges = scheme.levelEdges(colorMin, colorMax);
        for (int i = 0; i + 1 < int(edges.size()); ++i)
            rows.append(row(QString::number(i),
                            tr("%1 – %2 [%3]")
                                .arg(scheme.formatValue(edges[i]), scheme.formatValue(edges[i + 1]),
                                     units),
                            scheme.colorForClass(i, int(edges.size()) - 1)));
    } else {
        const auto range = scheme.effectiveRange(colorMin, colorMax);
        const int count = range.first == range.second ? 1 : 5;
        for (int i = 0; i < count; ++i) {
            const double value =
                range.first + (range.second - range.first) * i / std::max(1, count - 1);
            rows.append(row(QString::number(i),
                            tr("%1 [%2]").arg(scheme.formatValue(value), units),
                            ClassificationColorSampler(scheme, colorMin, colorMax).valueColor(value)));
        }
    }
    return rows;
}

} // namespace OpenSWMM::Render
