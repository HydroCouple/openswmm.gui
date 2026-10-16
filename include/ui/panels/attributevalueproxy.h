#ifndef OPENSWMM_ATTRIBUTEVALUEPROXY_H
#define OPENSWMM_ATTRIBUTEVALUEPROXY_H

#include "core/attributevalue.h"
#include <QBrush>
#include <QGuiApplication>
#include <QPalette>
#include <QSortFilterProxyModel>

namespace openswmmvis {

// All Attribute Table sources supply scalar roles. Formatting lives here so
// queries, sorting and exports never compare the visible word NULL.
class AttributeValueProxy : public QSortFilterProxyModel
{
public:
    explicit AttributeValueProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent)
    { setSortRole(kAttributeSortRole); }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role == Qt::DisplayRole || role == Qt::ForegroundRole || role == Qt::ToolTipRole) {
            const QVariant value = QSortFilterProxyModel::data(index, kAttributeValueRole);
            if (index.isValid() && isNullAttributeValue(value)) {
                if (role == Qt::DisplayRole) return QStringLiteral("NULL");
                if (role == Qt::ForegroundRole)
                    return QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text);
                const QVariant reason = QSortFilterProxyModel::data(index, role);
                if (reason.isValid() && !reason.toString().isEmpty()) return reason;
                return tr("NULL — no value is available for this row.");
            }
            if (role == Qt::DisplayRole) return value;
        }
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const QVariant a = left.data(kAttributeSortRole), b = right.data(kAttributeSortRole);
        const bool an = isNullAttributeValue(a), bn = isNullAttributeValue(b);
        // Qt reverses the operands for descending order. Keep NULLs last and
        // equal values in stable source-row order in either direction.
        if (an != bn) return sortOrder() == Qt::AscendingOrder ? !an : an;
        int cmp = 0;
        if (!an) {
            const bool na = isNumericValue(a), nb = isNumericValue(b);
            if (na && nb) cmp = compareAttributeNumbers(a, b);
            else if (na != nb) cmp = na ? -1 : 1;
            else cmp = QString::compare(a.toString(), b.toString(), sortCaseSensitivity());
        }
        if (cmp) return cmp < 0;
        return sortOrder() == Qt::AscendingOrder ? left.row() < right.row() : left.row() > right.row();
    }
};

} // namespace openswmmvis
#endif
