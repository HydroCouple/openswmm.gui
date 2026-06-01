/*!
 * \file   joinspec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JoinSpec impl + JSON round-trip (Slice Z.16-data).
 */

#include "render/joinspec.h"

#include <QJsonArray>

namespace OpenSWMM::Render
{

QJsonObject JoinSpec::toJson() const
{
    QJsonObject j;
    if (enabled)
        j[QStringLiteral("enabled")] = enabled;
    if (!sourcePath.isEmpty())
        j[QStringLiteral("sourcePath")] = sourcePath;
    if (!sourceLayerName.isEmpty())
        j[QStringLiteral("sourceLayerName")] = sourceLayerName;
    if (!sourceKeyField.isEmpty())
        j[QStringLiteral("sourceKeyField")] = sourceKeyField;
    if (!targetKeyField.isEmpty())
        j[QStringLiteral("targetKeyField")] = targetKeyField;
    if (!joinFields.isEmpty()) {
        QJsonArray arr;
        for (const QString &f : joinFields)
            arr.append(f);
        j[QStringLiteral("joinFields")] = arr;
    }
    if (!fieldPrefix.isEmpty())
        j[QStringLiteral("fieldPrefix")] = fieldPrefix;
    return j;
}

JoinSpec JoinSpec::fromJson(const QJsonObject &j)
{
    JoinSpec s;
    s.enabled         = j.value(QStringLiteral("enabled")).toBool(false);
    s.sourcePath      = j.value(QStringLiteral("sourcePath")).toString();
    s.sourceLayerName = j.value(QStringLiteral("sourceLayerName")).toString();
    s.sourceKeyField  = j.value(QStringLiteral("sourceKeyField")).toString();
    s.targetKeyField  = j.value(QStringLiteral("targetKeyField")).toString();
    const QJsonArray arr = j.value(QStringLiteral("joinFields")).toArray();
    s.joinFields.reserve(arr.size());
    for (const QJsonValue &v : arr)
        s.joinFields.append(v.toString());
    s.fieldPrefix     = j.value(QStringLiteral("fieldPrefix")).toString();
    return s;
}

bool JoinSpec::operator==(const JoinSpec &other) const
{
    return enabled         == other.enabled
        && sourcePath      == other.sourcePath
        && sourceLayerName == other.sourceLayerName
        && sourceKeyField  == other.sourceKeyField
        && targetKeyField  == other.targetKeyField
        && joinFields      == other.joinFields
        && fieldPrefix     == other.fieldPrefix;
}

} // namespace OpenSWMM::Render
