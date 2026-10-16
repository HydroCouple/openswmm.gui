#ifndef OPENSWMM_ATTRIBUTEVALUE_H
#define OPENSWMM_ATTRIBUTEVALUE_H

#include <QVariant>
#include <cmath>

namespace openswmmvis {

// Scalar values are independent of painted NULL text and structured editors.
inline constexpr int kAttributeValueRole = Qt::UserRole + 31;
inline constexpr int kAttributeSortRole = Qt::UserRole + 32;

inline bool isFloatingValue(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float;
}

inline bool isUnsignedValue(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::ULongLong: case QMetaType::ULong: case QMetaType::UInt:
    case QMetaType::UShort: case QMetaType::UChar: return true;
    default: return false;
    }
}

inline bool isNumericValue(const QVariant &v)
{
    if (isFloatingValue(v) || isUnsignedValue(v)) return true;
    switch (v.metaType().id()) {
    case QMetaType::LongLong: case QMetaType::Long: case QMetaType::Int:
    case QMetaType::Short: case QMetaType::Char: case QMetaType::SChar:
    case QMetaType::Bool: return true;
    default: return false;
    }
}

inline bool isNullAttributeValue(const QVariant &v)
{
    return !v.isValid() || v.isNull()
        || (isFloatingValue(v) && !std::isfinite(v.toDouble()));
}

inline QVariant attributeValue(const QVariant &v)
{
    return isNullAttributeValue(v) ? QVariant() : v;
}

inline QVariant optionalAttributeName(const QString &name)
{
    return name.isEmpty() ? QVariant() : QVariant(name);
}

template<typename T> inline int attributeCompare(T a, T b)
{
    return a < b ? -1 : (a > b ? 1 : 0);
}

// Inputs must be finite numeric variants. Do not round 64-bit IDs through
// double (including on Windows, where long double is also 64 bits).
inline int compareAttributeNumbers(const QVariant &a, const QVariant &b)
{
    const bool af = isFloatingValue(a), bf = isFloatingValue(b);
    if (af && bf) return attributeCompare(a.toDouble(), b.toDouble());
    if (af) return -compareAttributeNumbers(b, a);
    if (bf) {
        const double d = b.toDouble();
        if (isUnsignedValue(a)) {
            if (d < 0) return 1;
            if (d >= std::ldexp(1.0, 64)) return -1;
            const auto truncated = static_cast<qulonglong>(d);
            const int cmp = attributeCompare(a.toULongLong(), truncated);
            return cmp ? cmp : (d > std::floor(d) ? -1 : 0);
        }
        if (d < -std::ldexp(1.0, 63)) return 1;
        if (d >= std::ldexp(1.0, 63)) return -1;
        const auto truncated = static_cast<qlonglong>(d);
        const int cmp = attributeCompare(a.toLongLong(), truncated);
        return cmp ? cmp : (d > std::trunc(d) ? -1 : (d < std::trunc(d) ? 1 : 0));
    }
    const bool au = isUnsignedValue(a), bu = isUnsignedValue(b);
    if (au && bu) return attributeCompare(a.toULongLong(), b.toULongLong());
    if (!au && !bu) return attributeCompare(a.toLongLong(), b.toLongLong());
    if (au) return -compareAttributeNumbers(b, a);
    if (a.toLongLong() < 0) return -1;
    return attributeCompare(static_cast<qulonglong>(a.toLongLong()), b.toULongLong());
}

} // namespace openswmmvis
#endif
