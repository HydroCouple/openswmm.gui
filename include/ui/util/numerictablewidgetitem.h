/**
 * @file      numerictablewidgetitem.h
 * @brief     Numeric table ordering that preserves formatted display text.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_NUMERICTABLEWIDGETITEM_H
#define OPENSWMMVIS_NUMERICTABLEWIDGETITEM_H

#include <QTableWidgetItem>
#include <cmath>

namespace openswmmvis::ui
{

// Preserve display formatting while ordering numeric cells by value.
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    static constexpr int SortKeyRole = Qt::UserRole + 1;
    /**
     * @brief Sort finite numeric keys before unavailable/text values while preserving formatted display text.
     * @param[in] other Compared table item. Each side uses SortKeyRole when valid, otherwise text() converted through QVariant::toDouble().
     * @return Numeric less-than when both are finite, true when only this side is numeric, or the base QTableWidgetItem comparison when neither converts. An invalid numeric explicit key does not fall back to text.
     */
    bool operator<(const QTableWidgetItem &other) const override
    {
        bool leftOk = false, rightOk = false;
        const QVariant leftKey = data(SortKeyRole);
        const QVariant rightKey = other.data(SortKeyRole);
        const double left = (leftKey.isValid() ? leftKey : QVariant(text())).toDouble(&leftOk);
        const double right =
            (rightKey.isValid() ? rightKey : QVariant(other.text())).toDouble(&rightOk);
        leftOk = leftOk && std::isfinite(left);
        rightOk = rightOk && std::isfinite(right);
        if (leftOk != rightOk)
            return leftOk;
        if (leftOk)
            return left < right;
        return QTableWidgetItem::operator<(other);
    }
};

} // namespace openswmmvis::ui
#endif
