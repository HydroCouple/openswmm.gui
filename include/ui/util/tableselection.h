/**
 * @file      tableselection.h
 * @brief     Rows a table's Remove button acts on.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_TABLESELECTION_H
#define OPENSWMMVIS_TABLESELECTION_H

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QList>
#include <QSet>

#include <algorithm>
#include <functional>

namespace openswmmvis::ui
{

/**
 * Rows holding a selected cell, highest first so removing them in that order
 * keeps the remaining row numbers valid. With nothing selected, the current
 * row, which is what a single-row Remove button acted on.
 */
inline QList<int> selectedRowsDescending(const QAbstractItemView *view)
{
    QSet<int> rows;
    if (const QItemSelectionModel *sel = view->selectionModel())
        for (const QModelIndex &idx : sel->selectedIndexes())
            rows.insert(idx.row());
    if (rows.isEmpty() && view->currentIndex().isValid())
        rows.insert(view->currentIndex().row());
    QList<int> out(rows.cbegin(), rows.cend());
    std::sort(out.begin(), out.end(), std::greater<int>());
    return out;
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_TABLESELECTION_H
