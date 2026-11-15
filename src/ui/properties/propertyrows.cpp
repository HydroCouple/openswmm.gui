/**
 * @file      propertyrows.cpp
 * @brief     Font parts on selection and refresh for property grids.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/properties/propertyrows.h"

#include <qpropertymodel.h>

#include <QFont>
#include <QItemSelectionModel>
#include <QTreeView>

#include <functional>

namespace openswmmvis
{

namespace
{

bool isFontRow(const QModelIndex &row)
{
    return row.siblingAtColumn(1).data(Qt::EditRole).userType() == QMetaType::QFont;
}

/** @brief Call @p visit for every column-zero index below @p parent, depth first. */
void forEachRow(const QAbstractItemModel *model, const QModelIndex &parent,
                const std::function<void(const QModelIndex &)> &visit)
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const QModelIndex row = model->index(r, 0, parent);
        visit(row);
        forEachRow(model, row, visit);
    }
}

} // namespace

void showFontPartsOnSelection(QTreeView *tree)
{
    tree->setItemsExpandable(true);
    QObject::connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, tree,
                     [tree](const QModelIndex &current) {
                         const QModelIndex row = current.siblingAtColumn(0);
                         if (row.isValid() && isFontRow(row))
                             tree->expand(row);
                     });
}

void collapseFontRows(QTreeView *tree)
{
    if (!tree->model())
        return;
    forEachRow(tree->model(), {}, [tree](const QModelIndex &row) {
        if (isFontRow(row))
            tree->collapse(row);
    });
}

void refreshPropertyRows(QPropertyModel *model)
{
    if (!model)
        return;
    model->refreshValues();
    // A font row's parts copy the font when they are created; writing the font back unchanged
    // copies it again.
    forEachRow(model, {}, [model](const QModelIndex &row) {
        if (model->rowCount(row) > 0 && isFontRow(row)) {
            const QModelIndex value = row.siblingAtColumn(1);
            model->setData(value, model->data(value, Qt::EditRole), Qt::EditRole);
        }
    });
}

} // namespace openswmmvis
