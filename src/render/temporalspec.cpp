/*!
 * \file   temporalspec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  TemporalSpec impl + JSON round-trip (Slice Z.13-data).
 */

#include "render/temporalspec.h"

#include <algorithm>
#include <array>

namespace OpenSWMM::Render
{

namespace {

struct Mapping { TemporalMode m; const char *token; };
constexpr std::array<Mapping, 3> kModeMap = {{
    {TemporalMode::Single,     "single"},
    {TemporalMode::Range,      "range"},
    {TemporalMode::Cumulative, "cumulative"},
}};

/*! Frame-rate clamp matches the existing AnimationController's
 *  speed-range expectations: 0.1 fps minimum (10 s per frame is the
 *  slowest meaningful tick), 60 fps maximum (modern displays cap there). */
qreal clampFps(qreal v)
{
    return std::clamp(v, 0.1, 60.0);
}

} // namespace

QString temporalModeToString(TemporalMode m)
{
    for (const auto &x : kModeMap)
        if (x.m == m) return QString::fromLatin1(x.token);
    return QStringLiteral("single");
}

TemporalMode temporalModeFromString(const QString &s)
{
    for (const auto &x : kModeMap)
        if (s == QLatin1String(x.token)) return x.m;
    return TemporalMode::Single;
}

QJsonObject TemporalSpec::toJson() const
{
    QJsonObject j;
    if (enabled)
        j[QStringLiteral("enabled")] = enabled;
    if (!timeField.isEmpty())
        j[QStringLiteral("timeField")] = timeField;
    if (mode != TemporalMode::Single)
        j[QStringLiteral("mode")] = temporalModeToString(mode);
    if (!qFuzzyCompare(frameRateFps + 1.0, 12.0 + 1.0))
        j[QStringLiteral("frameRateFps")] = clampFps(frameRateFps);
    if (loop)
        j[QStringLiteral("loop")] = loop;
    if (pingPong)
        j[QStringLiteral("pingPong")] = pingPong;
    if (startTime.isValid())
        j[QStringLiteral("startTime")] = startTime.toString(Qt::ISODate);
    if (endTime.isValid())
        j[QStringLiteral("endTime")] = endTime.toString(Qt::ISODate);
    if (rangeWindowSec > 0.0)
        j[QStringLiteral("rangeWindowSec")] = rangeWindowSec;
    return j;
}

TemporalSpec TemporalSpec::fromJson(const QJsonObject &j)
{
    TemporalSpec s;
    s.enabled        = j.value(QStringLiteral("enabled")).toBool(false);
    s.timeField      = j.value(QStringLiteral("timeField")).toString();
    s.mode           = temporalModeFromString(
                           j.value(QStringLiteral("mode")).toString());
    if (j.contains(QStringLiteral("frameRateFps")))
        s.frameRateFps = clampFps(
            j.value(QStringLiteral("frameRateFps")).toDouble(12.0));
    s.loop           = j.value(QStringLiteral("loop")).toBool(false);
    s.pingPong       = j.value(QStringLiteral("pingPong")).toBool(false);
    if (j.contains(QStringLiteral("startTime"))) {
        s.startTime = QDateTime::fromString(
            j.value(QStringLiteral("startTime")).toString(), Qt::ISODate);
    }
    if (j.contains(QStringLiteral("endTime"))) {
        s.endTime = QDateTime::fromString(
            j.value(QStringLiteral("endTime")).toString(), Qt::ISODate);
    }
    s.rangeWindowSec = j.value(QStringLiteral("rangeWindowSec")).toDouble(0.0);
    return s;
}

bool TemporalSpec::operator==(const TemporalSpec &other) const
{
    return enabled        == other.enabled
        && timeField      == other.timeField
        && mode           == other.mode
        && qFuzzyCompare(frameRateFps + 1.0, other.frameRateFps + 1.0)
        && loop           == other.loop
        && pingPong       == other.pingPong
        && startTime      == other.startTime
        && endTime        == other.endTime
        && qFuzzyCompare(rangeWindowSec + 1.0, other.rangeWindowSec + 1.0);
}

} // namespace OpenSWMM::Render
