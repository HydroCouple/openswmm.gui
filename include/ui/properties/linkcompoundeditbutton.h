/**
 * @file      linkcompoundeditbutton.h
 * @brief     Custom cell editor widget for compound link attributes (XSection / InletUsage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice SC.1 — Custom cell editor widget for compound link attributes
 * (XSection / InletUsage).
 *
 * Registered with `QPropertyItemDelegate::registerCustomTypeEditorCreator`
 * against the `LinkCompoundEditRef` metatype; QPropertyModel instantiates
 * this widget when the user enters the value cell of any compound row.
 * Mirrors `NodeCompoundEditButton` line-for-line.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef LINKCOMPOUNDEDITBUTTON_H
#define LINKCOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/linkcompoundeditref.h"

class QPushButton;

class LinkCompoundEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(LinkCompoundEditRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline LinkCompoundEditRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit LinkCompoundEditButton(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] LinkCompoundEditRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const LinkCompoundEditRef &ref);

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
     * @brief Runs the link compound dialog; on either close outcome copies the live summary and emits valueChanged because pages may already have applied edits.
     * @details Dialog widgets own their normal Qt lifetimes; engine/model handles remain borrowed. Invoke on the GUI thread.
     */
    void onClicked();

private:
    /**
     * @brief Set the button text from the cached summary, falling back to Edit when empty.
     * @details Does not query the engine or modify model state.
     */
    void refreshLabel();

    LinkCompoundEditRef m_ref;
    QPushButton *m_btn = nullptr;
};

#endif // LINKCOMPOUNDEDITBUTTON_H
