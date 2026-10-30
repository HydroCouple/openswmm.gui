/**
 * @file      culvertcodecombobox.h
 * @brief     Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md — inline combobox editor
 *            for the conduit "Culvert Code" Property Browser row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md — inline
 * combobox editor for the conduit "Culvert Code" Property Browser row.
 * Registered against the `CulvertCodeRef` metatype via
 * `QPropertyItemDelegate::registerCustomTypeEditorCreator`.
 *
 * Apply-as-you-go (same contract the removed LinkCompoundEditDialog
 * culvert page honoured): picking an item writes immediately through
 * `SWMMModelLayer::applyLinkCulvertCode` (or the bare engine setter
 * when no layer is bound, e.g. in tests), then emits `valueChanged()`
 * so the delegate's setModelData round-trip refreshes the row.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef CULVERTCODECOMBOBOX_H
#define CULVERTCODECOMBOBOX_H

#include <QComboBox>

#include "ui/properties/culvertcoderef.h"

class CulvertCodeComboBox : public QComboBox
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  setEditorData / setModelData round-trip. */
    Q_PROPERTY(CulvertCodeRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline CulvertCodeRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit CulvertCodeComboBox(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] CulvertCodeRef value() const { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const CulvertCodeRef &ref);

signals:
    /**
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Apply the current combo item through the model layer or bare engine.
     * @param[in] index Ignored signal index; currentData() supplies the HDS-5 code (0 means none).
     * @details Suppressed during setValue; absent engine/name or unresolved link is a no-op. Otherwise attempts the write, updates the cached code and emits valueChanged without checking the write result.
     */
    void onCurrentIndexChanged(int index);

private:
    CulvertCodeRef m_ref;
    /*! Re-entrancy guard — `setValue` selects the current code
     *  programmatically; the index-changed slot bails while set so the
     *  engine doesn't see a phantom write. */
    bool m_suppressApply = false;
};

#endif // CULVERTCODECOMBOBOX_H
