// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
namespace meshkit::detail {
struct QtStorage {
    using Point=QPointF;using Box=QRectF;using String=QString;using PointInput=QVector<QPointF>;
    template<class T> using Vector=QVector<T>;
    template<class K,class V> using Hash=QHash<K,V>;
    template<class K,class V> using MultiHash=QMultiHash<K,V>;
    template<class K> using Set=QSet<K>;
    static String message(const char *s){return QString::fromLatin1(s);}
    static String invalidPointMessage(int i){return QStringLiteral("cdt: non-finite input point %1").arg(i);}
};
}
