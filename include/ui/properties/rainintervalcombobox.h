/**
 * @file      rainintervalcombobox.h
 * @brief     DA.2 parity follow-up — inline editable combobox editor for the rain gage "Recording
 *            Interval" Property Browser row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * DA.2 parity follow-up — inline editable combobox editor for the rain gage
 * "Recording Interval" Property Browser row. Mirrors `CulvertCodeComboBox`,
 * but is *editable* (legacy esComboEdit): the user can pick one of the legacy
 * H:MM presets or type a custom clock value.
 *
 * Registered against the `RainIntervalRef` metatype via
 * `QPropertyItemDelegate::registerCustomTypeEditorCreator`.
 *
 * Apply-as-you-go (same contract as CulvertCodeComboBox): committing a value
 * parses the H:MM text to seconds and writes immediately through the engine
 * (or the model layer when bound), then emits `valueChanged()` so the
 * delegate's setModelData round-trip refreshes the row. A malformed entry is
 * ignored (no write), leaving the stored value unchanged.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef RAININTERVALCOMBOBOX_H
#define RAININTERVALCOMBOBOX_H

#include <QComboBox>

#include "ui/properties/rainintervalref.h"

class RainIntervalComboBox : public QComboBox
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  setEditorData / setModelData round-trip. */
    Q_PROPERTY(RainIntervalRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline RainIntervalRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit RainIntervalComboBox(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] RainIntervalRef value() const { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const RainIntervalRef &ref);

signals:
    /**
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Apply a preset choice or completed text edit unless a programmatic update is suppressed.
     */
    void onEdited();

private:
    /**
     * @brief Parse the displayed clock interval and publish a changed second count.
     * @details Malformed text restores the last value without a signal; unchanged seconds are ignored. A valid change updates only the ref and emits valueChanged, leaving engine writes to the adapter.
     */
    void apply();

    RainIntervalRef m_ref;
    /*! Re-entrancy guard — `setValue` sets the text programmatically; the
     *  edit slot bails while set so the engine doesn't see a phantom write. */
    bool m_suppressApply = false;
};

#endif // RAININTERVALCOMBOBOX_H
