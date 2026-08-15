/*!
 * \file   maskspec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MaskSpec impl + JSON round-trip (Slice Z.14-data).
 */

#include "render/maskspec.h"

#include <array>

namespace OpenSWMM::Render
{

namespace {

struct Mapping { MaskMode m; const char *token; };
constexpr std::array<Mapping, 2> kModeMap = {{
    {MaskMode::ClipInside,  "clipInside"},
    {MaskMode::ClipOutside, "clipOutside"},
}};

} // namespace

QString maskModeToString(MaskMode m)
{
    for (const auto &x : kModeMap)
        if (x.m == m) return QString::fromLatin1(x.token);
    return QStringLiteral("clipInside");
}

MaskMode maskModeFromString(const QString &s)
{
    for (const auto &x : kModeMap)
        if (s == QLatin1String(x.token)) return x.m;
    return MaskMode::ClipInside;
}

QJsonObject MaskSpec::toJson() const
{
    QJsonObject j;
    if (enabled)
        j[QStringLiteral("enabled")] = enabled;
    if (!sourceLayerId.isEmpty())
        j[QStringLiteral("sourceLayerId")] = sourceLayerId;
    if (mode != MaskMode::ClipInside)
        j[QStringLiteral("mode")] = maskModeToString(mode);
    return j;
}

MaskSpec MaskSpec::fromJson(const QJsonObject &j)
{
    MaskSpec s;
    s.enabled       = j.value(QStringLiteral("enabled")).toBool(false);
    s.sourceLayerId = j.value(QStringLiteral("sourceLayerId")).toString();
    s.mode          = maskModeFromString(
                          j.value(QStringLiteral("mode")).toString());
    return s;
}

bool MaskSpec::operator==(const MaskSpec &other) const
{
    return enabled       == other.enabled
        && sourceLayerId == other.sourceLayerId
        && mode          == other.mode;
}

} // namespace OpenSWMM::Render
