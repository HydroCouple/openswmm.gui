/*!
 * \file   datadefined.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/datadefined.h"

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

bool DataDefinedScalar::isValid() const
{
    if (attribute.isEmpty()) return false;
    if (!std::isfinite(valueMin) || !std::isfinite(valueMax)) return false;
    if (!std::isfinite(outMin)   || !std::isfinite(outMax))   return false;
    return true;
}

double DataDefinedScalar::evaluate(double value) const
{
    if (!std::isfinite(value)) return outMin;

    // Degenerate input range — return the centre of the output so callers
    // get a stable, predictable value rather than NaN.
    if (valueMin == valueMax)
        return (outMin + outMax) * 0.5;

    // Clamp before mapping so curve math always sees t ∈ [0, 1].
    double t = (value - valueMin) / (valueMax - valueMin);
    t = std::clamp(t, 0.0, 1.0);

    switch (curve)
    {
    case DDCurve::Linear:
        break;
    case DDCurve::Sqrt:
        t = std::sqrt(t);
        break;
    case DDCurve::Log:
        // Map t ∈ [0,1] → log10(1 + 9*t) ∈ [0,1]. Compresses the high end
        // (which is what users want for flow / runoff that span orders).
        t = std::log10(1.0 + 9.0 * t);
        break;
    }

    return outMin + t * (outMax - outMin);
}

QJsonObject DataDefinedScalar::toJson() const
{
    QJsonObject j;
    j.insert(QStringLiteral("attribute"), attribute);
    j.insert(QStringLiteral("valueMin"),  valueMin);
    j.insert(QStringLiteral("valueMax"),  valueMax);
    j.insert(QStringLiteral("outMin"),    outMin);
    j.insert(QStringLiteral("outMax"),    outMax);
    j.insert(QStringLiteral("curve"),     static_cast<int>(curve));
    return j;
}

DataDefinedScalar DataDefinedScalar::fromJson(const QJsonObject &j)
{
    DataDefinedScalar d;
    d.attribute = j.value(QStringLiteral("attribute")).toString();
    d.valueMin  = j.value(QStringLiteral("valueMin")).toDouble(0.0);
    d.valueMax  = j.value(QStringLiteral("valueMax")).toDouble(1.0);
    d.outMin    = j.value(QStringLiteral("outMin")).toDouble(2.0);
    d.outMax    = j.value(QStringLiteral("outMax")).toDouble(12.0);
    const int c = j.value(QStringLiteral("curve")).toInt(0);
    switch (c) {
    case 1: d.curve = DDCurve::Sqrt; break;
    case 2: d.curve = DDCurve::Log;  break;
    default: d.curve = DDCurve::Linear; break;
    }
    return d;
}

} // namespace OpenSWMM::Render
