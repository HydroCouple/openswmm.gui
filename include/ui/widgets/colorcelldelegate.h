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

class QColor;
class QStandardItem;

namespace openswmmvis::ui
{

/**
 * @brief Text a screen reader announces for a colour swatch cell (A-05).
 * @param[in] color Swatch colour.
 * @return "Colour #rrggbb", with ", N% opaque" appended for a translucent colour; empty for an
 *   invalid colour.
 */
[[nodiscard]] QString colorCellAccessibleText(const QColor &color);

/**
 * @brief Write a swatch colour and its accessible text into one cell as a single change.
 * @details Every swatch write goes through here so the text never goes stale. Both roles are
 *   written by one setItemData(), so a QStandardItemModel reports one itemChanged and a panel
 *   that commits on itemChanged commits once.
 * @param[in,out] model Required model owning \p index.
 * @param[in] index Target cell.
 * @param[in] color Colour stored as-is in Qt::BackgroundRole; Qt::AccessibleTextRole gets
 *   colorCellAccessibleText() (removed for an invalid colour).
 * @return The model's setItemData() result.
 */
bool setColorCell(QAbstractItemModel *model, const QModelIndex &index, const QColor &color);

/**
 * @brief QStandardItem form of setColorCell(), for rows built before they are inserted.
 * @param[in,out] item Required item; when it already belongs to a model the write goes through
 *   that model as one change.
 * @param[in] color Swatch colour.
 */
void setColorCell(QStandardItem *item, const QColor &color);

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
     * @brief Open an alpha-enabled colour dialog on a cell double click, from a queued call
     *   once the view has finished handling the event.
     * @param[in] e Required Qt event; only MouseButtonDblClick is handled.
     * @param[in,out] m Required model receiving a valid pick through BackgroundRole setData(),
     *   unless the model or the cell is gone by the time the dialog closes.
     * @param[in] idx Target cell, read now: its colour seeds the picker (white when invalid).
     *   Rows rebuilt meanwhile take the pick at the same row/column of the flat table.
     * @return True for every double-click, including cancellation or rejected model writes; false for other events. The view-options argument is unused.
     */
    bool editorEvent(QEvent *e, QAbstractItemModel *m, const QStyleOptionViewItem &,
                     const QModelIndex &idx) override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_COLORCELLDELEGATE_H
