/**
 * @file      attributevalue.h
 * @brief     Scalar attribute values, null normalization, and exact numeric ordering.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_ATTRIBUTEVALUE_H
#define OPENSWMM_ATTRIBUTEVALUE_H

#include <QVariant>
#include <cmath>

namespace openswmmvis
{

// Scalar values are independent of painted NULL text and structured editors.
inline constexpr int kAttributeValueRole = Qt::UserRole + 31;
inline constexpr int kAttributeSortRole = Qt::UserRole + 32;

/**
 * @brief Test whether a variant stores a native float or double.
 * @param[in] v Variant to inspect; invalid and null variants are accepted.
 * @return True for QMetaType::Float or QMetaType::Double, including non-finite values.
 * @note Numeric strings and other values convertible to double do not qualify.
 */
inline bool isFloatingValue(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float;
}

/**
 * @brief Identify the unsigned integer storage types supported by attribute sorting.
 * @param[in] v Variant whose actual metatype is examined without conversion.
 * @return True for unsigned char, short, int, long, or long long storage; false otherwise.
 * @note Boolean values are handled by isNumericValue(), not by this unsigned category.
 */
inline bool isUnsignedValue(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::ULongLong:
    case QMetaType::ULong:
    case QMetaType::UInt:
    case QMetaType::UShort:
    case QMetaType::UChar:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Identify numeric scalar variants without accepting numeric-looking strings.
 * @param[in] v Variant to classify by its stored Qt metatype.
 * @return True for supported signed/unsigned integers, bool, float, and double.
 * @note This tests type, not validity of a numeric value: NaN and infinity still qualify.
 *       Use isNullAttributeValue() before comparing or displaying missing values.
 */
inline bool isNumericValue(const QVariant &v)
{
    if (isFloatingValue(v) || isUnsignedValue(v))
        return true;
    switch (v.metaType().id()) {
    case QMetaType::LongLong:
    case QMetaType::Long:
    case QMetaType::Int:
    case QMetaType::Short:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Bool:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Apply the shared definition of a missing scalar attribute.
 * @param[in] v Attribute value before display formatting or editor conversion.
 * @return True for invalid variants, Qt null values, and non-finite float/double values.
 *         Empty strings and finite numeric zero are not made null by this function.
 */
inline bool isNullAttributeValue(const QVariant &v)
{
    return !v.isValid() || v.isNull() || (isFloatingValue(v) && !std::isfinite(v.toDouble()));
}

/**
 * @brief Normalize missing attribute values to an invalid QVariant.
 * @param[in] v Raw attribute value; never modified.
 * @return An invalid QVariant when isNullAttributeValue(v) is true; otherwise a copy
 *         retaining the original value and metatype.
 */
inline QVariant attributeValue(const QVariant &v)
{
    return isNullAttributeValue(v) ? QVariant() : v;
}

/**
 * @brief Represent an absent optional object name as a missing attribute.
 * @param[in] name Object name; an empty string means no assignment.
 * @return An invalid QVariant for an empty name, or a string QVariant containing name.
 * @note Whitespace is preserved; identifier validation is a separate operation.
 */
inline QVariant optionalAttributeName(const QString &name)
{
    return name.isEmpty() ? QVariant() : QVariant(name);
}

/**
 * @brief Produce the three-way ordering used by scalar attribute sorting.
 * @tparam T Value type supporting mutually consistent less-than and greater-than tests.
 * @param[in] a Left operand, copied into the comparison.
 * @param[in] b Right operand, copied into the comparison.
 * @return -1 if a is less than b, 1 if a is greater than b, or 0 otherwise.
 * @pre Operands must belong to a totally ordered domain; NaN is not a valid operand.
 */
template <typename T> inline int attributeCompare(T a, T b)
{
    return a < b ? -1 : (a > b ? 1 : 0);
}

/**
 * @brief Compare numeric variants while preserving the ordering of 64-bit integer IDs.
 * @param[in] a Finite numeric scalar accepted by isNumericValue().
 * @param[in] b Finite numeric scalar accepted by isNumericValue().
 * @return -1, 0, or 1 according to the numeric ordering of a and b.
 * @pre Both operands must be non-null and finite. Callers handle missing values before
 *      reaching this function; numeric strings and structured variants are unsupported.
 * @details Signed and unsigned integers are compared without passing through double.
 *          Mixed integer/floating comparisons check the integer range and fractional
 *          remainder explicitly, avoiding precision loss even when long double has
 *          the same precision as double. Neither operand is modified.
 */
inline int compareAttributeNumbers(const QVariant &a, const QVariant &b)
{
    const bool af = isFloatingValue(a), bf = isFloatingValue(b);
    if (af && bf)
        return attributeCompare(a.toDouble(), b.toDouble());
    if (af)
        return -compareAttributeNumbers(b, a);
    if (bf) {
        const double d = b.toDouble();
        if (isUnsignedValue(a)) {
            if (d < 0)
                return 1;
            if (d >= std::ldexp(1.0, 64))
                return -1;
            const auto truncated = static_cast<qulonglong>(d);
            const int cmp = attributeCompare(a.toULongLong(), truncated);
            return cmp ? cmp : (d > std::floor(d) ? -1 : 0);
        }
        if (d < -std::ldexp(1.0, 63))
            return 1;
        if (d >= std::ldexp(1.0, 63))
            return -1;
        const auto truncated = static_cast<qlonglong>(d);
        const int cmp = attributeCompare(a.toLongLong(), truncated);
        return cmp ? cmp : (d > std::trunc(d) ? -1 : (d < std::trunc(d) ? 1 : 0));
    }
    const bool au = isUnsignedValue(a), bu = isUnsignedValue(b);
    if (au && bu)
        return attributeCompare(a.toULongLong(), b.toULongLong());
    if (!au && !bu)
        return attributeCompare(a.toLongLong(), b.toLongLong());
    if (au)
        return -compareAttributeNumbers(b, a);
    if (a.toLongLong() < 0)
        return -1;
    return attributeCompare(static_cast<qulonglong>(a.toLongLong()), b.toULongLong());
}

} // namespace openswmmvis
#endif
