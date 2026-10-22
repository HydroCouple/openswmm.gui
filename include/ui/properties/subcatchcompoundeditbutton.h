/**
 * @file      subcatchcompoundeditbutton.h
 * @brief     custom cell editor widget for compound subcatchment attributes (land-use coverage /
 *            groundwater / LID usage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 3 — custom cell editor widget for compound subcatchment attributes
 * (land-use coverage / groundwater / LID usage). Mirror of
 * NodeCompoundEditButton: shows a single "summary — Edit…" button that opens
 * SubcatchCompoundEditDialog and propagates the refreshed summary back.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SUBCATCHCOMPOUNDEDITBUTTON_H
#define SUBCATCHCOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/subcatchcompoundeditref.h"

class QPushButton;

class SubcatchCompoundEditButton : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(
        SubcatchCompoundEditRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline SubcatchCompoundEditRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit SubcatchCompoundEditButton(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] SubcatchCompoundEditRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const SubcatchCompoundEditRef &ref);

signals:
    /**
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Opens groundwater modelessly with summary/valueChanged updates on Apply; other pages run modally and refresh/notify after close, including edits applied before Cancel.
     * @details Dialog widgets own their normal Qt lifetimes; engine/model handles remain borrowed. Invoke on the GUI thread.
     */
    void onClicked();

private:
    /**
     * @brief Set the button text from the cached summary, falling back to Edit when empty.
     * @details Does not query the engine or modify model state.
     */
    void refreshLabel();

    SubcatchCompoundEditRef m_ref;
    QPushButton *m_btn = nullptr;
};

#endif // SUBCATCHCOMPOUNDEDITBUTTON_H
