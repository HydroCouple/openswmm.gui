/**
 * @file      attributevalueproxy.h
 * @brief     Numeric sorting and display formatting for scalar attribute values.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_ATTRIBUTEVALUEPROXY_H
#define OPENSWMM_ATTRIBUTEVALUEPROXY_H

#include "core/attributevalue.h"
#include <QBrush>
#include <QGuiApplication>
#include <QPalette>
#include <QSortFilterProxyModel>

namespace openswmmvis
{

// All Attribute Table sources supply scalar roles. Formatting lives here so
// queries, sorting and exports never compare the visible word NULL.
class AttributeValueProxy : public QSortFilterProxyModel
{
public:
    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create GUI-thread sort/filter proxy using kAttributeSortRole; source model assigned
     *   separately.
     * @note No data copy/format settings/engine binding; source must supply scalar attribute roles
     *   for display/sort, not already formatted NULL text.
     */
    explicit AttributeValueProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent)
    {
        setSortRole(kAttributeSortRole);
    }

    /**
     * @param[in] index Expected proxy index; base maps to source, validity checked before null
     *   display handling.
     * @param[in] role Display/Foreground/ToolTip intercept scalar null state, other roles delegate
     *   unchanged.
     * @return For valid null scalar, Display literal NULL, Foreground disabled-text palette brush,
     *   ToolTip source nonempty reason or translated missing-value fallback. Nonnull Display
     *   returns raw kAttributeValueRole (can be wrapper QVariant), other cases delegate base. No
     *   EditRole/data normalization or source mutation.
     * @note Reads source attribute-value role even for null tooltip/foreground; QGuiApplication
     *   palette access requires GUI context. Does not convert units or affect sorting/query scalar
     *   values.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role == Qt::DisplayRole || role == Qt::ForegroundRole || role == Qt::ToolTipRole) {
            const QVariant value = QSortFilterProxyModel::data(index, kAttributeValueRole);
            if (index.isValid() && isNullAttributeValue(value)) {
                if (role == Qt::DisplayRole)
                    return QStringLiteral("NULL");
                if (role == Qt::ForegroundRole)
                    return QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text);
                const QVariant reason = QSortFilterProxyModel::data(index, role);
                if (reason.isValid() && !reason.toString().isEmpty())
                    return reason;
                return tr("NULL — no value is available for this row.");
            }
            if (role == Qt::DisplayRole)
                return value;
        }
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    /**
     * @param[in] left Borrowed SOURCE index, not proxy; scalar sort role queried.
     * @param[in] right Borrowed source index compared, same compatible source model expected.
     * @return Ascending comparator adapted for Qt operand reversal: null values remain LAST in both
     *   sort directions; otherwise numeric pairs use compareAttributeNumbers, numbers precede
     *   strings ascending, strings compare configured case sensitivity. Equal/null ties use source
     *   row order preserved in either direction.
     * @note Pure source queries, no source mutation/cache. Relies on role helpers for null/numeric
     *   classification and current sortOrder; does not compare visible formatted NULL string or use
     *   locale-aware collator. Caller/model serialization required.
     */
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const QVariant a = left.data(kAttributeSortRole), b = right.data(kAttributeSortRole);
        const bool an = isNullAttributeValue(a), bn = isNullAttributeValue(b);
        // Qt reverses the operands for descending order. Keep NULLs last and
        // equal values in stable source-row order in either direction.
        if (an != bn)
            return sortOrder() == Qt::AscendingOrder ? !an : an;
        int cmp = 0;
        if (!an) {
            const bool na = isNumericValue(a), nb = isNumericValue(b);
            if (na && nb)
                cmp = compareAttributeNumbers(a, b);
            else if (na != nb)
                cmp = na ? -1 : 1;
            else
                cmp = QString::compare(a.toString(), b.toString(), sortCaseSensitivity());
        }
        if (cmp)
            return cmp < 0;
        return sortOrder() == Qt::AscendingOrder ? left.row() < right.row()
                                                 : left.row() > right.row();
    }
};

} // namespace openswmmvis
#endif
