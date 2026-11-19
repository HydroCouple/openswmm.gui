/**
 * @file      numberlabelformat.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     How a layer writes the numbers in its class labels: decimal places or significant
 *            figures, trailing zeros, thousands separators, a prefix, a units suffix and the
 *            text between the two ends of a range.
 *
 *         A renderer or color scheme owns one; its legend rows, the Legend dock, the Layer Style
 *         dialog and the map legend's color bar ticks all use it, so the layer's numbers read the
 *         same everywhere. Header-only so every target that compiles a renderer gets it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_NUMBERLABELFORMAT_H
#define OPENSWMM_RENDER_NUMBERLABELFORMAT_H

#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

/*!
 * \struct NumberLabelFormat
 * \brief Number and text format of a layer's class labels.
 */
struct NumberLabelFormat {
    /*! \enum Mode
     *  \brief Fixed decimal places or significant figures. */
    enum class Mode : int {
        Decimals = 0,           //!< QString::number(v, 'f', precision)
        SignificantFigures = 1, //!< QString::number(v, 'g', precision)
    };

    Mode mode = Mode::SignificantFigures;           ///< Decimals or significant figures.
    int precision = 3;                              ///< Digits, 0–12 (at least 1 for figures).
    bool trimTrailingZeros = false;                 ///< Drop zeros after the decimal point.
    bool thousandsSeparator = false;                ///< Group the integer digits with commas.
    QString prefix;                                 ///< Text before each value or range.
    QString suffix;                                 ///< Text after each value or range (units).
    QString rangeSeparator = QStringLiteral(" – "); ///< Text between a range's two ends.

    /** @brief Significant figures with @p digits and every text option at its default.
     * @param digits Significant figures. @return Format. */
    static NumberLabelFormat significantFigures(int digits)
    {
        NumberLabelFormat f;
        f.precision = digits;
        return f;
    }

    /** @brief The number alone: mode and precision, trailing zeros, thousands separator.
     * @param v Value. @return Text (nonfinite values as QString::number writes them). */
    [[nodiscard]] QString formatNumber(double v) const
    {
        const int p = std::clamp(precision, 0, 12);
        QString s = mode == Mode::Decimals ? QString::number(v, 'f', p)
                                           : QString::number(v, 'g', std::max(1, p));
        if (!std::isfinite(v))
            return s;
        const qsizetype exp = s.indexOf(QLatin1Char('e'));
        QString mantissa = exp >= 0 ? s.left(exp) : s;
        const QString exponent = exp >= 0 ? s.mid(exp) : QString();
        if (trimTrailingZeros && mantissa.contains(QLatin1Char('.'))) {
            while (mantissa.endsWith(QLatin1Char('0')))
                mantissa.chop(1);
            if (mantissa.endsWith(QLatin1Char('.')))
                mantissa.chop(1);
        }
        if (thousandsSeparator && exponent.isEmpty()) {
            const qsizetype start = mantissa.startsWith(QLatin1Char('-')) ? 1 : 0;
            qsizetype end = mantissa.indexOf(QLatin1Char('.'));
            if (end < 0)
                end = mantissa.size();
            for (qsizetype at = end - 3; at > start; at -= 3)
                mantissa.insert(at, QLatin1Char(','));
        }
        return mantissa + exponent;
    }

    /** @brief One value with the prefix and suffix. @param v Value. @return Label. */
    [[nodiscard]] QString formatLabel(double v) const { return prefix + formatNumber(v) + suffix; }

    /** @brief A range "lo – hi" with the prefix and suffix around it.
     * @param lo Low end. @param hi High end. @return Label. */
    [[nodiscard]] QString formatRange(double lo, double hi) const
    {
        return prefix + formatNumber(lo) + rangeSeparator + formatNumber(hi) + suffix;
    }

    /** @brief "< v" for values below a range. @param v Low end. @return Label. */
    [[nodiscard]] QString formatBelow(double v) const
    {
        return QStringLiteral("< ") + formatLabel(v);
    }

    /** @brief "> v" for values above a range. @param v High end. @return Label. */
    [[nodiscard]] QString formatAbove(double v) const
    {
        return QStringLiteral("> ") + formatLabel(v);
    }

    /** @brief The settings that differ from @p defaults, as JSON (empty when none differ).
     * @param defaults Format whose values are left out. @return JSON object. */
    [[nodiscard]] QJsonObject toJson(const NumberLabelFormat &defaults) const
    {
        QJsonObject o;
        if (mode != defaults.mode)
            o.insert(QStringLiteral("mode"), mode == Mode::Decimals ? QStringLiteral("decimals")
                                                                    : QStringLiteral("figures"));
        if (precision != defaults.precision)
            o.insert(QStringLiteral("precision"), precision);
        if (trimTrailingZeros != defaults.trimTrailingZeros)
            o.insert(QStringLiteral("trimZeros"), trimTrailingZeros);
        if (thousandsSeparator != defaults.thousandsSeparator)
            o.insert(QStringLiteral("thousands"), thousandsSeparator);
        if (prefix != defaults.prefix)
            o.insert(QStringLiteral("prefix"), prefix);
        if (suffix != defaults.suffix)
            o.insert(QStringLiteral("suffix"), suffix);
        if (rangeSeparator != defaults.rangeSeparator)
            o.insert(QStringLiteral("rangeSeparator"), rangeSeparator);
        return o;
    }

    /** @brief Read a format written by toJson(); missing keys take @p defaults' values.
     * @param o JSON object. @param defaults Values for missing keys. @return Format. */
    static NumberLabelFormat fromJson(const QJsonObject &o, const NumberLabelFormat &defaults)
    {
        NumberLabelFormat f = defaults;
        if (o.contains(QStringLiteral("mode")))
            f.mode = o.value(QStringLiteral("mode")).toString() == QStringLiteral("decimals")
                         ? Mode::Decimals
                         : Mode::SignificantFigures;
        f.precision =
            std::clamp(o.value(QStringLiteral("precision")).toInt(defaults.precision), 0, 12);
        f.trimTrailingZeros =
            o.value(QStringLiteral("trimZeros")).toBool(defaults.trimTrailingZeros);
        f.thousandsSeparator =
            o.value(QStringLiteral("thousands")).toBool(defaults.thousandsSeparator);
        f.prefix = o.value(QStringLiteral("prefix")).toString(defaults.prefix);
        f.suffix = o.value(QStringLiteral("suffix")).toString(defaults.suffix);
        f.rangeSeparator =
            o.value(QStringLiteral("rangeSeparator")).toString(defaults.rangeSeparator);
        return f;
    }

    bool operator==(const NumberLabelFormat &) const = default;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_NUMBERLABELFORMAT_H
