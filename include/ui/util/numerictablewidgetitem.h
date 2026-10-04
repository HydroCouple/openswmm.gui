#ifndef OPENSWMMVIS_NUMERICTABLEWIDGETITEM_H
#define OPENSWMMVIS_NUMERICTABLEWIDGETITEM_H

#include <QTableWidgetItem>
#include <cmath>

namespace openswmmvis::ui {

// Preserve display formatting while ordering numeric cells by value.
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    static constexpr int SortKeyRole = Qt::UserRole + 1;
    bool operator<(const QTableWidgetItem &other) const override {
        bool leftOk = false, rightOk = false;
        const QVariant leftKey = data(SortKeyRole);
        const QVariant rightKey = other.data(SortKeyRole);
        const double left = (leftKey.isValid() ? leftKey : QVariant(text())).toDouble(&leftOk);
        const double right = (rightKey.isValid() ? rightKey : QVariant(other.text())).toDouble(&rightOk);
        leftOk = leftOk && std::isfinite(left);
        rightOk = rightOk && std::isfinite(right);
        if (leftOk != rightOk) return leftOk;
        if (leftOk) return left < right;
        return QTableWidgetItem::operator<(other);
    }
};

} // namespace openswmmvis::ui
#endif
