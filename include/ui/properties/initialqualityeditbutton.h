/**
 * @file      initialqualityeditbutton.h
 * @brief     Initial-quality UI round — custom cell editor widget for the per-element "Initial
 *            Quality" row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Initial-quality UI round — custom cell editor widget for the
 * per-element "Initial Quality" row. Mirrors UserFlagsEditButton:
 * registered with QPropertyItemDelegate against the
 * InitialQualityEditRef metatype; shows a "summary — Edit…" button
 * that opens InitialQualityDialog scoped to the element and
 * propagates the refreshed summary back to the model via the USER
 * property.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef INITIALQUALITYEDITBUTTON_H
#define INITIALQUALITYEDITBUTTON_H

#include <QWidget>

#include "ui/properties/initialqualityeditref.h"

class QPushButton;

class InitialQualityEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(InitialQualityEditRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline InitialQualityEditRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit InitialQualityEditButton(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] InitialQualityEditRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const InitialQualityEditRef &ref);

signals:
    /*! Emitted after a dialog that actually wrote changes closes, including
     *  Cancel after a partial write; no-op inspection does not emit.
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Runs the scoped initial-quality dialog when engine and element ID exist. Always refreshes the summary; emits valueChanged and sets wroteChanges only when the dialog reports engine edits.
     * @details Dialog widgets own their normal Qt lifetimes; engine/model handles remain borrowed. Invoke on the GUI thread.
     */
    void onClicked();

private:
    /**
     * @brief Set the button text from the cached summary, falling back to Edit when empty.
     * @details Does not query the engine or modify model state.
     */
    void refreshLabel();

    InitialQualityEditRef m_ref;
    QPushButton *m_btn = nullptr;
};

#endif // INITIALQUALITYEDITBUTTON_H
