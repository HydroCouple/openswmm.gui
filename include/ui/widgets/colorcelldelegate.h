/**
 * @file      colorcelldelegate.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Colour-swatch table cell with a double-click colour picker.
 *
 *         Paints the cell's Qt::BackgroundRole colour as a rounded swatch
 *         (checkerboard behind translucent colours) and opens QColorDialog
 *         on double-click, writing the pick back to Qt::BackgroundRole.
 *         Shared by the ClassificationEditor class table and the raster
 *         Paletted-renderer class table; originally lived inside
 *         classificationeditor.cpp (from KindRendererPanel).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H
#define OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui
{

class ColorCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Paint a BackgroundRole QColor swatch over selection/background with a transparency checkerboard.
     * @param[in,out] p Required painter; its state is saved/restored.
     * @param[in] opt Cell geometry, palette and selection state.
     * @param[in] idx Model cell providing the colour. Invalid colours draw no swatch.
     */
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    /**
     * @brief Disable inline editor widgets; colour selection is handled by editorEvent().
     * @return Always null; the unused arguments are the Qt parent, view options and model index.
     */
    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override;
    /**
     * @brief Open an alpha-enabled colour dialog on a cell double click.
     * @param[in] e Required Qt event; only MouseButtonDblClick is handled.
     * @param[in,out] m Required model receiving a valid pick through BackgroundRole setData().
     * @param[in] idx Target cell; current invalid colour uses white as picker default.
     * @return True for every double-click, including cancellation or rejected model writes; false for other events. The view-options argument is unused.
     */
    bool editorEvent(QEvent *e, QAbstractItemModel *m, const QStyleOptionViewItem &,
                     const QModelIndex &idx) override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H
