/*!
 * \file   auxiliarystoragespec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  AuxiliaryStorageSpec impl + JSON round-trip (Slice Z.15-data).
 */

#include "render/auxiliarystoragespec.h"

namespace OpenSWMM::Render
{

QJsonObject AuxiliaryStorageSpec::toJson() const
{
    QJsonObject j;
    if (enabled)
        j[QStringLiteral("enabled")] = enabled;
    if (!dbPath.isEmpty())
        j[QStringLiteral("dbPath")] = dbPath;
    return j;
}

AuxiliaryStorageSpec AuxiliaryStorageSpec::fromJson(const QJsonObject &j)
{
    AuxiliaryStorageSpec s;
    s.enabled = j.value(QStringLiteral("enabled")).toBool(false);
    s.dbPath  = j.value(QStringLiteral("dbPath")).toString();
    return s;
}

bool AuxiliaryStorageSpec::operator==(const AuxiliaryStorageSpec &other) const
{
    return enabled == other.enabled && dbPath == other.dbPath;
}

} // namespace OpenSWMM::Render
