/*!
 * \file   legendcolordelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB Phase 8.6.16 — QStyledItemDelegate for the LegendLayerTreeModel
 *         "Color" column. Paints a swatch + chip; click opens QColorDialog
 *         and writes the picked colour back through the model (which pushes
 *         a SetRendererClassColorCommand onto the canvas undo stack).
 *
 *         Greyed out (no edit affordance) when the model reports
 *         EditableRole == false for the cell — i.e. the layer's renderer
 *         doesn't support per-class colour edits, or the row is a layer
 *         header rather than an item.
 */
#ifndef OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H
#define OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui {

class LegendColorDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit LegendColorDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

    // No createEditor: QColorDialog is modal and feels native opened from
    // editorEvent() on click, matching how every GIS legend behaves.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H
