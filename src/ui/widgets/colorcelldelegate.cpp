/*!
 * \file   colorcelldelegate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/colorcelldelegate.h"

#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QPainter>

namespace openswmmvis::ui {

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
                        p->fillRect(QRect(x, y, cell, cell),
                                    QColor(255, 255, 255));
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

bool ColorCellDelegate::editorEvent(QEvent *e, QAbstractItemModel *m,
                                    const QStyleOptionViewItem &, const QModelIndex &idx)
{
    if (e->type() != QEvent::MouseButtonDblClick) return false;
    QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
    const QColor picked = QColorDialog::getColor(
        cur.isValid() ? cur : QColor(Qt::white),
        nullptr, QObject::tr("Class colour"),
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return true;
    m->setData(idx, picked, Qt::BackgroundRole);
    return true;
}

} // namespace openswmmvis::ui
