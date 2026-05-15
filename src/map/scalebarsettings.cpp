/*!
 * \file   scalebarsettings.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Configurable appearance settings for the map scale bar.
 */

#include "map/scalebarsettings.h"

#include <cmath>

ScaleBarSettings::ScaleBarSettings(QObject *parent)
    : QObject(parent)
{
}

void ScaleBarSettings::setPen(const QPen &pen)
{
    if (m_pen != pen)
    {
        m_pen = pen;
        emit changed();
    }
}

void ScaleBarSettings::setFont(const QFont &font)
{
    if (m_font != font)
    {
        m_font = font;
        emit changed();
    }
}

void ScaleBarSettings::setUnits(Units units)
{
    if (m_units != units)
    {
        m_units = units;
        emit changed();
    }
}

void ScaleBarSettings::setPosition(Position position)
{
    if (m_position != position)
    {
        m_position = position;
        emit changed();
    }
}

void ScaleBarSettings::setMaxBarLength(int length)
{
    if (m_maxBarLength != length)
    {
        m_maxBarLength = length;
        emit changed();
    }
}

void ScaleBarSettings::setLabelDecimals(int decimals)
{
    if (m_labelDecimals != decimals) { m_labelDecimals = decimals; emit changed(); }
}

void ScaleBarSettings::setCompactNotation(bool compact)
{
    if (m_compactNotation != compact) { m_compactNotation = compact; emit changed(); }
}

QString ScaleBarSettings::formatLabel(double metres, bool rawUnits) const
{
    // Apply label rounding: -1 = auto (up to 3 sig figs, trim trailing zeros),
    // 0 = whole number, n = n decimal places.
    auto fmt = [this](double v) -> QString {
        if (m_labelDecimals < 0) {
            // Auto: choose decimal places based on magnitude, never use scientific notation.
            int decimals = 0;
            if (v < 10.0)       decimals = 2;
            else if (v < 100.0) decimals = 1;
            double factor = std::pow(10.0, decimals);
            v = std::round(v * factor) / factor;
            return QString::number(v, 'f', decimals);
        }
        // Round to m_labelDecimals decimal places, then format as 'f'.
        double factor = std::pow(10.0, m_labelDecimals);
        v = std::round(v * factor) / factor;
        return QString::number(v, 'f', m_labelDecimals);
    };

    if (rawUnits)
        return QStringLiteral("%1 units").arg(fmt(metres));

    // Compact notation removes the space and abbreviates "km"→"k", "m"→"m".
    switch (m_units)
    {
        case Auto:
            if (metres >= 1000.0) {
                return m_compactNotation
                    ? QStringLiteral("%1k").arg(fmt(metres / 1000.0))
                    : QStringLiteral("%1 km").arg(fmt(metres / 1000.0));
            } else {
                return m_compactNotation
                    ? QStringLiteral("%1m").arg(fmt(metres))
                    : QStringLiteral("%1 m").arg(fmt(metres));
            }
        case Meters:
            return m_compactNotation
                ? QStringLiteral("%1m").arg(fmt(metres))
                : QStringLiteral("%1 m").arg(fmt(metres));
        case Feet:
            return QStringLiteral("%1 ft").arg(fmt(metres * 3.28083989501));
        case Kilometers:
            return m_compactNotation
                ? QStringLiteral("%1k").arg(fmt(metres / 1000.0))
                : QStringLiteral("%1 km").arg(fmt(metres / 1000.0));
        case Miles:
            return QStringLiteral("%1 mi").arg(fmt(metres * 0.000621371192237));
    }
    return {};
}

double ScaleBarSettings::roundedMetres(double metres) const
{
    // Mirrors the rounding in formatLabel() but returns a metres value so the
    // bar length stays in sync with what the label actually says.
    auto roundVal = [this](double v) -> double {
        if (m_labelDecimals < 0) {
            int decimals = 0;
            if (v < 10.0)       decimals = 2;
            else if (v < 100.0) decimals = 1;
            double factor = std::pow(10.0, decimals);
            return std::round(v * factor) / factor;
        }
        double factor = std::pow(10.0, m_labelDecimals);
        return std::round(v * factor) / factor;
    };

    switch (m_units)
    {
        case Auto:
            if (metres >= 1000.0)
                return roundVal(metres / 1000.0) * 1000.0;
            return roundVal(metres);
        case Meters:
            return roundVal(metres);
        case Feet:
            return roundVal(metres * 3.28083989501) / 3.28083989501;
        case Kilometers:
            return roundVal(metres / 1000.0) * 1000.0;
        case Miles:
            return roundVal(metres * 0.000621371192237) / 0.000621371192237;
    }
    return metres;
}
