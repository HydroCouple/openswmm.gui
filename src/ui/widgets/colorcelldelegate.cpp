/**
 * @file      colorcelldelegate.cpp
 * @brief     Paints the cell's Qt::BackgroundRole color as a rounded swatch (checkerboard behind
 *            translucent colors) and opens QColorDialog on double-click, writing the pick back to
 *            Qt::BackgroundRole.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/widgets/colorcelldelegate.h"

#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStandardItem>

namespace openswmmvis::ui
{

QString colorCellAccessibleText(const QColor &color)
{
    if (!color.isValid())
        return {};
    const QString hex = color.name(QColor::HexRgb);
    if (color.alpha() == 255)
        return QObject::tr("Colour %1").arg(hex);
    return QObject::tr("Colour %1, %2% opaque").arg(hex).arg(qRound(color.alphaF() * 100.0));
}

bool setColorCell(QAbstractItemModel *model, const QModelIndex &index, const QColor &color)
{
    const QString text = colorCellAccessibleText(color);
    return model->setItemData(index, {{Qt::BackgroundRole, color},
                                      {Qt::AccessibleTextRole,
                                       text.isEmpty() ? QVariant() : QVariant(text)}});
}

void setColorCell(QStandardItem *item, const QColor &color)
{
    if (item->model()) {
        setColorCell(item->model(), item->index(), color);
        return;
    }
    item->setData(color, Qt::BackgroundRole);
    const QString text = colorCellAccessibleText(color);
    item->setData(text.isEmpty() ? QVariant() : QVariant(text), Qt::AccessibleTextRole);
}

void ColorCellDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                              const QModelIndex &idx) const
{
    const QColor c = idx.data(Qt::BackgroundRole).value<QColor>();
    p->save();
    if (opt.state & QStyle::State_Selected)
        p->fillRect(opt.rect, opt.palette.highlight());
    QRect inner = opt.rect.adjusted(4, 4, -4, -4);
    if (c.isValid()) {
        // Checkered background so users can see transparency (same
        // pattern as ColorButton::paintEvent).
        if (c.alpha() < 255) {
            p->fillRect(inner, QColor(220, 220, 220));
            const int cell = 4;
            for (int y = inner.top(); y < inner.bottom(); y += cell) {
                for (int x = inner.left(); x < inner.right(); x += cell) {
                    if (((x / cell) + (y / cell)) & 1)
                        p->fillRect(QRect(x, y, cell, cell), QColor(255, 255, 255));
                }
            }
        }
        p->setBrush(c);
        p->setPen(QPen(QColor(60, 60, 60), 0.8));
        p->drawRoundedRect(inner, 3, 3);
    }
    p->restore();
}

QWidget *ColorCellDelegate::createEditor(QWidget *, const QStyleOptionViewItem &,
                                         const QModelIndex &) const
{
    return nullptr;
}

bool ColorCellDelegate::editorEvent(QEvent *e, QAbstractItemModel *m, const QStyleOptionViewItem &,
                                    const QModelIndex &idx)
{
    if (e->type() != QEvent::MouseButtonDblClick)
        return false;
    // Queued, and written through guards: the picker's event loop can delete the table and its
    // model (their page rebuilt underneath), and the view still runs after this returns. A
    // refresh can also rebuild the rows before the picker closes: the seed is read now, and the
    // pick then lands on the same cell of the (flat) table, as it did when written synchronously.
    const QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
    QMetaObject::invokeMethod(
        this,
        [model = QPointer<QAbstractItemModel>(m), index = QPersistentModelIndex(idx),
         row = idx.row(), column = idx.column(), cur] {
            const QColor picked = QColorDialog::getColor(cur.isValid() ? cur : QColor(Qt::white),
                                                         nullptr, QObject::tr("Class colour"),
                                                         QColorDialog::ShowAlphaChannel);
            if (!picked.isValid() || !model)
                return;
            const QModelIndex target =
                index.isValid() ? QModelIndex(index) : model->index(row, column);
            if (target.isValid())
                setColorCell(model, target, picked);
        },
        Qt::QueuedConnection);
    return true;
}

} // namespace openswmmvis::ui
