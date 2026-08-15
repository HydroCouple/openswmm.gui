/*!
 * \file   ilayerstylesubject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/ilayerstylesubject.h"
#include "render/sublayerstyle.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>
#include <QColor>

namespace openswmmvis::ui {

namespace {

// Convert a QVariant into something JSON-serialisable. Colours go through
// .name(HexArgb), Qt::PenStyle/enum values go to int, everything else
// uses QVariant::toString / toDouble / toBool / toInt.
QJsonValue variantToJson(const QVariant &v)
{
    if (v.canConvert<QColor>() && v.userType() == qMetaTypeId<QColor>()) {
        return v.value<QColor>().name(QColor::HexArgb);
    }
    switch (v.userType()) {
        case QMetaType::Bool:        return v.toBool();
        case QMetaType::Int:         return v.toInt();
        case QMetaType::UInt:        return int(v.toUInt());
        case QMetaType::LongLong:    return double(v.toLongLong());
        case QMetaType::Double:      return v.toDouble();
        case QMetaType::Float:       return v.toFloat();
        case QMetaType::QString:     return v.toString();
        default: {
            // Q_ENUM / Qt::PenStyle / other small enumerations come back
            // as int via QVariant after toInt(); fall through.
            bool ok = false;
            const int as_int = v.toInt(&ok);
            if (ok) return as_int;
            return v.toString();
        }
    }
}

QVariant jsonToVariant(const QJsonValue &val, int targetType)
{
    if (targetType == qMetaTypeId<QColor>()) {
        const QString s = val.toString();
        const QColor c(s);
        return c.isValid() ? QVariant::fromValue(c) : QVariant();
    }
    switch (targetType) {
        case QMetaType::Bool:    return val.toBool();
        case QMetaType::Int:     return val.toInt();
        case QMetaType::UInt:    return uint(val.toInt());
        case QMetaType::Double:
        case QMetaType::Float:   return val.toDouble();
        case QMetaType::QString: return val.toString();
        default:
            if (val.isDouble()) return QVariant(val.toInt());
            if (val.isString()) return val.toString();
            return val.toVariant();
    }
}

} // namespace

QJsonObject ILayerStyleSubject::snapshot() const
{
    QObject *obj = propertyObject();
    if (!obj) return {};

    // SublayerStyle subclasses already have a JSON schema for .oswp
    // persistence; defer to them so the snapshot matches what disk
    // round-trips would store.
    if (auto *st = qobject_cast<OpenSWMM::Render::SublayerStyle *>(obj))
        return st->toJson();

    QJsonObject out;
    const QMetaObject *mo = obj->metaObject();
    // Skip objectName (QObject base) — never user-edited via the dialog.
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty mp = mo->property(i);
        if (!mp.isReadable() || !mp.isWritable()) continue;
        out.insert(QString::fromLatin1(mp.name()),
                   variantToJson(mp.read(obj)));
    }
    return out;
}

void ILayerStyleSubject::restore(const QJsonObject &snap)
{
    QObject *obj = propertyObject();
    if (!obj) return;

    if (auto *st = qobject_cast<OpenSWMM::Render::SublayerStyle *>(obj)) {
        st->fromJson(snap);
        return;
    }

    const QMetaObject *mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty mp = mo->property(i);
        if (!mp.isWritable()) continue;
        const QString name = QString::fromLatin1(mp.name());
        if (!snap.contains(name)) continue;
        const QVariant v = jsonToVariant(snap.value(name), mp.userType());
        if (v.isValid())
            mp.write(obj, v);
    }
}

} // namespace openswmmvis::ui
