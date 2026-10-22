/**
 * @file      nodecompoundeditbutton.h
 * @brief     Custom cell editor widget for compound node attributes (Inflows / DWF / RDII /
 *            Treatment).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DB.2 — Custom cell editor widget for compound node attributes
 * (Inflows / DWF / RDII / Treatment).
 *
 * Registered with `QPropertyItemDelegate::registerCustomTypeEditorCreator`
 * against the `NodeCompoundEditRef` metatype; QPropertyModel instantiates
 * this widget when the user enters the value cell of any compound row.
 * The widget shows a single `Edit…` button labelled with the current
 * `summary`; clicking opens `NodeCompoundEditDialog` and (when the user
 * commits a change) propagates the refreshed summary back to the model
 * via the `value` `Q_PROPERTY` (USER true).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NODECOMPOUNDEDITBUTTON_H
#define NODECOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/nodecompoundeditref.h"

class QPushButton;

class NodeCompoundEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(NodeCompoundEditRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline NodeCompoundEditRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit NodeCompoundEditButton(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] NodeCompoundEditRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const NodeCompoundEditRef &ref);

signals:
    /*! Emitted after the dialog closes with a (possibly) updated
     *  summary string. The QPropertyItemDelegate listens via the
     *  `notify` signal of the USER property and commits the new value
     *  back to the model.
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Opens the selected modal node page, or modeless groundwater/navigation editor. Modal close refreshes its summary and emits valueChanged even after Cancel because pages can apply immediately; navigation branches return without that modal signal.
     * @details Dialog widgets own their normal Qt lifetimes; engine/model handles remain borrowed. Invoke on the GUI thread.
     */
    void onClicked();

private:
    /**
     * @brief Set the button text from the cached summary, falling back to Edit when empty.
     * @details Does not query the engine or modify model state.
     */
    void refreshLabel();

    NodeCompoundEditRef m_ref;
    QPushButton *m_btn = nullptr;
};

#endif // NODECOMPOUNDEDITBUTTON_H
