/*!
 * \file   legendcolordelegate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/delegates/legendcolordelegate.h"

#include "ui/models/legendlayertreemodel.h"

#include <QColorDialog>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace openswmmvis::ui {

LegendColorDelegate::LegendColorDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{}

void LegendColorDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    // Paint the row background using the default style so selection /
    // alternating colours look consistent with the rest of the view.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();   // we paint the swatch instead.
    QStyledItemDelegate::paint(painter, opt, index);

    const QColor c = index.data(Qt::EditRole).value<QColor>();
    if (!c.isValid()) {
        // Layer-header row or non-editable cell — nothing to paint.
        return;
    }

    const bool editable = index.data(LegendLayerTreeModel::EditableRole).toBool();

    // Swatch geometry: 16×16 chip with a 1 px outline, vertically centred.
    constexpr int kSwatch = 16;
    const QRect r = option.rect;
    const int x = r.left() + 6;
    const int y = r.top() + (r.height() - kSwatch) / 2;
    const QRect swatch(x, y, kSwatch, kSwatch);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(c);
    painter->setPen(QPen(c.darker(140), 1.0));
    painter->drawRect(swatch);

    // Optional cue: small ⌄ marker next to the chip on editable cells so
    // users know they can click. Subtle so the chip stays the focus.
    if (editable) {
        painter->setPen(option.palette.color(QPalette::WindowText));
        const int ax = swatch.right() + 8;
        const int ay = swatch.top() + swatch.height() / 2;
        painter->drawLine(ax, ay - 2, ax + 3, ay + 2);
        painter->drawLine(ax + 3, ay + 2, ax + 6, ay - 2);
    }
    painter->restore();
}

bool LegendColorDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index)
{
    if (!model || !index.isValid())
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    // Only respond on the actual MouseButtonRelease over the cell, and
    // only when the model says the cell is editable.
    if (event->type() != QEvent::MouseButtonRelease)
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton)
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    if (!index.data(LegendLayerTreeModel::EditableRole).toBool())
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    const QColor current = index.data(Qt::EditRole).value<QColor>();
    const QColor picked = QColorDialog::getColor(
        current.isValid() ? current : QColor(Qt::white),
        nullptr,
        tr("Change legend color"),
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return true;   // user cancelled — event consumed.

    model->setData(index, picked, Qt::EditRole);
    return true;
}

} // namespace openswmmvis::ui
