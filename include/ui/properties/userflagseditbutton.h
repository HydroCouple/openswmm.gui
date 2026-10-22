/**
 * @file      userflagseditbutton.h
 * @brief     Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — custom cell editor widget for the
 *            per-object "User Flags" row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — custom cell editor
 * widget for the per-object "User Flags" row. Mirrors
 * NodeCompoundEditButton: registered with QPropertyItemDelegate against
 * the UserFlagsEditRef metatype; shows a "summary — Edit…" button that
 * opens UserFlagValuesDialog and propagates the refreshed summary back
 * to the model via the USER property.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef USERFLAGSEDITBUTTON_H
#define USERFLAGSEDITBUTTON_H

#include <QWidget>

#include "ui/properties/userflagseditref.h"

class QPushButton;

class UserFlagsEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(UserFlagsEditRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline UserFlagsEditRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit UserFlagsEditButton(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] UserFlagsEditRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const UserFlagsEditRef &ref);

signals:
    /*! Emitted after the dialog closes with a (possibly) updated
     *  summary; the delegate commits the new value to the model.
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Runs the scoped flag dialog when a model and object ID exist; always pulls its latest summary and emits valueChanged after close.
     * @details Dialog widgets own their normal Qt lifetimes; engine/model handles remain borrowed. Invoke on the GUI thread.
     */
    void onClicked();

private:
    /**
     * @brief Set the button text from the cached summary, falling back to Edit when empty.
     * @details Does not query the engine or modify model state.
     */
    void refreshLabel();

    UserFlagsEditRef m_ref;
    QPushButton *m_btn = nullptr;
};

#endif // USERFLAGSEDITBUTTON_H
