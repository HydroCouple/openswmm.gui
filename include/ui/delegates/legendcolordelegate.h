/**
 * @file      legendcolordelegate.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BB Phase 8.6.16 — QStyledItemDelegate for the LegendLayerTreeModel
 *         "Color" column. Paints a swatch + chip; click opens QColorDialog
 *         and writes the picked colour back through the model (which pushes
 *         a SetRendererClassColorCommand onto the canvas undo stack).
 *
 *         Greyed out (no edit affordance) when the model reports
 *         EditableRole == false for the cell — i.e. the layer's renderer
 *         doesn't support per-class colour edits, or the row is a layer
 *         header rather than an item.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H
#define OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui
{

class LegendColorDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner, null leaves caller responsible for delegate
     *   lifetime.
     * @brief Create GUI-thread swatch delegate, no model binding or editor opened.
     */
    explicit LegendColorDelegate(QObject *parent = nullptr);

    /**
     * @param[in,out] painter Required active view painter; swatch state saved/restored around
     *   custom drawing.
     * @param[in] option Borrowed cell style/rectangle; base paints row background with text
     *   removed.
     * @param[in] index Source cell; EditRole QColor and LegendLayerTreeModel::EditableRole read.
     * @note Invalid QColor leaves only base background. Valid colour draws 16 logical-pixel chip
     *   with darker outline, editable role adds chevron. Does not gray/desaturate noneditable
     *   colours despite old file summary, change selection or model data.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /**
     * @param[in] event Required borrowed Qt event; eligible MouseButtonRelease must actually be
     *   QMouseEvent.
     * @param[in] model Borrowed mutable model; null/invalid index forwarded to styled base.
     * @param[in] option Borrowed option forwarded on unhandled path; no release-position
     *   containment check here.
     * @param[in] index Destination cell; must stay valid through nested modal dialog event loop.
     * @return True for editable left release, including canceled/invalid colour or rejected model
     *   write; other events return base result.
     * @note Opens unparented modal QColorDialog with alpha, using current valid colour or white.
     *   Accepted colour sent to EditRole, ignores setData status. Caller must preserve
     *   delegate/model/index through dialog reentry; model provides undo/notifications. No direct
     *   engine or canvas access.
     */
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

    // No createEditor: QColorDialog is modal and feels native opened from
    // editorEvent() on click, matching how every GIS legend behaves.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DELEGATES_LEGENDCOLORDELEGATE_H
