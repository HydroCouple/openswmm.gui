/*!
 * \file   gisobjectref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "layers/gisobjectref.h"

QString GisObjectRef::layerKey(const QString &layerId)
{
    return QStringLiteral("gis::") + layerId;
}

SWMMObjectRef GisObjectRef::feature(const QString &layerId, long long fid)
{
    const QString name = layerKey(layerId) + QStringLiteral("#f")
                       + QString::number(fid);
    return SWMMObjectRef(SWMMObjectRef::Feature, name);
}

bool GisObjectRef::parseFeature(const SWMMObjectRef &ref,
                                QString *outLayerId,
                                long long *outFid)
{
    if (ref.objectType != SWMMObjectRef::Feature) return false;
    if (!ref.name.startsWith(QStringLiteral("gis::"))) return false;
    // LAST "#f": a layerId may legally contain '#'; the fid never does.
    const int hashIdx = ref.name.lastIndexOf(QStringLiteral("#f"));
    if (hashIdx < 5) return false;   // must leave a non-empty layerId
    bool ok = false;
    const long long fid = ref.name.mid(hashIdx + 2).toLongLong(&ok);
    if (!ok || fid < 0) return false;
    if (outLayerId) *outLayerId = ref.name.mid(5, hashIdx - 5);
    if (outFid)     *outFid     = fid;
    return true;
}
