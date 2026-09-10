/*!
 * \file   colorcelldelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Colour-swatch table cell with a double-click colour picker.
 *
 *         Paints the cell's Qt::BackgroundRole colour as a rounded swatch
 *         (checkerboard behind translucent colours) and opens QColorDialog
 *         on double-click, writing the pick back to Qt::BackgroundRole.
 *         Shared by the ClassificationEditor class table and the raster
 *         Paletted-renderer class table; originally lived inside
 *         classificationeditor.cpp (from KindRendererPanel).
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H
#define OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui {

class ColorCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override;
    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override;
    bool editorEvent(QEvent *e, QAbstractItemModel *m,
                     const QStyleOptionViewItem &, const QModelIndex &idx) override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H
