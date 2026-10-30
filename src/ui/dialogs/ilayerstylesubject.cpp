/**
 * @file      ilayerstylesubject.cpp
 * @brief     Slice U-2.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/ilayerstylesubject.h"
#include "render/sublayerstyle.h"

#include <QFont>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>
#include <QColor>

namespace openswmmvis::ui
{

namespace
{

// Convert a QVariant into something JSON-serialisable. Colours go through
// .name(HexArgb), Qt::PenStyle/enum values go to int, everything else
// uses QVariant::toString / toDouble / toBool / toInt.
/**
 * @param[in] v Borrowed property QVariant, actual metatype guides conversion.
 * @return Exact QColor→HexArgb, QFont→toString, primitive bool/int/double/float/string, UInt
 *   narrowed int, LongLong→double; other convertible-to-int enum then string fallback. No universal
 *   lossless integer/container/custom-type encoding, finite validation or source mutation.
 */
QJsonValue variantToJson(const QVariant &v)
{
    if (v.canConvert<QColor>() && v.userType() == qMetaTypeId<QColor>()) {
        return v.value<QColor>().name(QColor::HexArgb);
    }
    // QFont — lossless via toString/fromString (the generic fallback below
    // reduced fonts to an int/string mishmash, making labelFont snapshots
    // lossy on Cancel/undo restore).
    if (v.userType() == qMetaTypeId<QFont>()) {
        return v.value<QFont>().toString();
    }
    switch (v.userType()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
        return v.toInt();
    case QMetaType::UInt:
        return int(v.toUInt());
    case QMetaType::LongLong:
        return double(v.toLongLong());
    case QMetaType::Double:
        return v.toDouble();
    case QMetaType::Float:
        return v.toFloat();
    case QMetaType::QString:
        return v.toString();
    default: {
        // Q_ENUM / Qt::PenStyle / other small enumerations come back
        // as int via QVariant after toInt(); fall through.
        bool ok = false;
        const int as_int = v.toInt(&ok);
        if (ok)
            return as_int;
        return v.toString();
    }
    }
}

/**
 * @param[in] val Borrowed JSON value, wrong primitive JSON type uses Qt default conversions.
 * @param[in] targetType Expected property Qt metatype ID, no QObject schema lookup.
 * @return Valid QColor/QFont parsed string or INVALID on parse failure; bool/int/uint/double/string
 *   conversions, Float also double, default numeric→int, string→QString, other Qt toVariant.
 *   LongLong not special-cased, can narrow numeric restore; UInt negative int wraps. No
 *   validation/write/ownership transfer.
 */
QVariant jsonToVariant(const QJsonValue &val, int targetType)
{
    if (targetType == qMetaTypeId<QColor>()) {
        const QString s = val.toString();
        const QColor c(s);
        return c.isValid() ? QVariant::fromValue(c) : QVariant();
    }
    if (targetType == qMetaTypeId<QFont>()) {
        QFont f;
        return f.fromString(val.toString()) ? QVariant::fromValue(f) : QVariant();
    }
    switch (targetType) {
    case QMetaType::Bool:
        return val.toBool();
    case QMetaType::Int:
        return val.toInt();
    case QMetaType::UInt:
        return uint(val.toInt());
    case QMetaType::Double:
    case QMetaType::Float:
        return val.toDouble();
    case QMetaType::QString:
        return val.toString();
    default:
        if (val.isDouble())
            return QVariant(val.toInt());
        if (val.isString())
            return val.toString();
        return val.toVariant();
    }
}

} // namespace

QJsonObject ILayerStyleSubject::snapshot() const
{
    QObject *obj = propertyObject();
    if (!obj)
        return {};

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
        if (!mp.isReadable() || !mp.isWritable())
            continue;
        out.insert(QString::fromLatin1(mp.name()), variantToJson(mp.read(obj)));
    }
    return out;
}

void ILayerStyleSubject::restore(const QJsonObject &snap)
{
    QObject *obj = propertyObject();
    if (!obj)
        return;

    if (auto *st = qobject_cast<OpenSWMM::Render::SublayerStyle *>(obj)) {
        st->fromJson(snap);
        return;
    }

    const QMetaObject *mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty mp = mo->property(i);
        if (!mp.isWritable())
            continue;
        const QString name = QString::fromLatin1(mp.name());
        if (!snap.contains(name))
            continue;
        const QVariant v = jsonToVariant(snap.value(name), mp.userType());
        if (v.isValid())
            mp.write(obj, v);
    }
}

} // namespace openswmmvis::ui
