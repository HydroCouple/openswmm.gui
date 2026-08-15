/*!
 * \file   importmapping.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importmapping.h"

#include <QJsonArray>
#include <QJsonValue>

namespace openswmmvis::import {

const AttributeBinding *ImportMapping::binding(const QString &targetKey) const
{
    for (const AttributeBinding &b : bindings)
        if (b.targetKey == targetKey) return &b;
    return nullptr;
}

AttributeBinding &ImportMapping::ensureBinding(const QString &targetKey)
{
    for (AttributeBinding &b : bindings)
        if (b.targetKey == targetKey) return b;
    bindings.append(AttributeBinding{targetKey, QString(), QVariant()});
    return bindings.last();
}

QJsonObject ImportMapping::toJson() const
{
    QJsonObject j;
    j.insert(QStringLiteral("version"), 1);
    j.insert(QStringLiteral("kind"), static_cast<int>(kind));

    QJsonArray arr;
    for (const AttributeBinding &b : bindings) {
        if (!b.isBound()) continue;   // presets carry only meaningful rows
        QJsonObject bo;
        bo.insert(QStringLiteral("target"), b.targetKey);
        if (!b.sourceField.isEmpty())
            bo.insert(QStringLiteral("field"), b.sourceField);
        if (b.defaultValue.isValid())
            bo.insert(QStringLiteral("default"),
                      QJsonValue::fromVariant(b.defaultValue));
        arr.append(bo);
    }
    j.insert(QStringLiteral("bindings"), arr);

    QJsonObject ep;
    ep.insert(QStringLiteral("fromFields"), endpointsFromFields);
    ep.insert(QStringLiteral("snap"), endpointsSnap);
    ep.insert(QStringLiteral("snapTolerance"), snapToleranceMapUnits);
    ep.insert(QStringLiteral("autoCreate"), autoCreateJunctions);
    ep.insert(QStringLiteral("autoPrefix"), autoNodePrefix);
    j.insert(QStringLiteral("endpoints"), ep);

    QJsonObject cf;
    cf.insert(QStringLiteral("policy"),
              conflict == Conflict::Update ? QStringLiteral("update")
                                           : QStringLiteral("skip"));
    cf.insert(QStringLiteral("attributes"), updateAttributes);
    cf.insert(QStringLiteral("geometry"), updateGeometry);
    j.insert(QStringLiteral("conflict"), cf);

    j.insert(QStringLiteral("selectedOnly"), selectedFeaturesOnly);
    return j;
}

std::optional<ImportMapping> ImportMapping::fromJson(const QJsonObject &j,
                                                     QString *errorOut)
{
    const QJsonValue kindVal = j.value(QStringLiteral("kind"));
    if (!kindVal.isDouble()) {
        if (errorOut) *errorOut = QStringLiteral("missing \"kind\"");
        return std::nullopt;
    }
    const int kindInt = kindVal.toInt(-1);
    if (kindInt < static_cast<int>(TargetKind::Junction)
        || kindInt > static_cast<int>(TargetKind::Outlet)) {
        if (errorOut) *errorOut = QStringLiteral("unknown \"kind\" %1").arg(kindInt);
        return std::nullopt;
    }

    ImportMapping m;
    m.kind = static_cast<TargetKind>(kindInt);

    const QJsonArray arr = j.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject bo = v.toObject();
        const QString target = bo.value(QStringLiteral("target")).toString();
        if (target.isEmpty()) continue;
        AttributeBinding b;
        b.targetKey    = target;
        b.sourceField  = bo.value(QStringLiteral("field")).toString();
        if (bo.contains(QStringLiteral("default")))
            b.defaultValue = bo.value(QStringLiteral("default")).toVariant();
        m.bindings.append(b);
    }

    const QJsonObject ep = j.value(QStringLiteral("endpoints")).toObject();
    m.endpointsFromFields   = ep.value(QStringLiteral("fromFields")).toBool(false);
    m.endpointsSnap         = ep.value(QStringLiteral("snap")).toBool(true);
    m.snapToleranceMapUnits = ep.value(QStringLiteral("snapTolerance")).toDouble(1.0);
    m.autoCreateJunctions   = ep.value(QStringLiteral("autoCreate")).toBool(false);
    m.autoNodePrefix        = ep.value(QStringLiteral("autoPrefix"))
                                  .toString(QStringLiteral("J_"));

    const QJsonObject cf = j.value(QStringLiteral("conflict")).toObject();
    m.conflict = cf.value(QStringLiteral("policy")).toString()
                         == QLatin1String("update")
                     ? Conflict::Update : Conflict::Skip;
    m.updateAttributes = cf.value(QStringLiteral("attributes")).toBool(true);
    m.updateGeometry   = cf.value(QStringLiteral("geometry")).toBool(false);

    m.selectedFeaturesOnly =
        j.value(QStringLiteral("selectedOnly")).toBool(false);
    return m;
}

} // namespace openswmmvis::import
