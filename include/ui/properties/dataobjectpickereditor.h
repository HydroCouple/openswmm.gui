/**
 * @file      dataobjectpickereditor.h
 * @brief     Custom cell editor widget for `DataObjectRef`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.4.3 — Custom cell editor widget for `DataObjectRef`.
 *
 * Registered against the `DataObjectRef` metatype via
 * `QPropertyItemDelegate::registerCustomTypeEditorCreator`. Renders a
 * `LabeledPickerCombo` (combobox + "…" button) filtered by `ref.kind`.
 * Picking an item updates `value()` so the delegate writes back through
 * the adapter's WRITE slot (e.g. `setOutfallTidalCurveRef`). Clicking
 * the "…" button opens a creation flow.
 *
 * Per Slice BM.0-Add-New (2026-05-24): "…" prompts the user for a
 * name and commits via `SWMMModelLayer::createDataObject` for
 * categories with a complex MVC editor (Time Series, Unit Hydrographs).
 * For gap categories (Curves, Patterns), the picker surfaces the
 * tooltip naming the future editor slice instead. Once those editor
 * slices ship, the picker will dispatch through the same
 * `ObjectBrowserPanel::launchAddNewEditor` path as Add-New.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef DATAOBJECTPICKEREDITOR_H
#define DATAOBJECTPICKEREDITOR_H

#include <QWidget>

#include "ui/properties/dataobjectref.h"

class LabeledPickerCombo;

class DataObjectPickerEditor : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(DataObjectRef value READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    /**
     * @brief Create the inline DataObjectRef property editor and its child controls.
     * @param[in] parent Optional owning widget; construct and operate on the GUI thread.
     */
    explicit DataObjectPickerEditor(QWidget *parent = nullptr);

    /**
     * @brief Read the editor's current value object for the delegate commit round trip.
     * @return Value copy; embedded engine/layer/model pointers stay borrowed and the summary may reflect applied dialog edits.
     */
    [[nodiscard]] DataObjectRef value() const noexcept { return m_ref; }
    /**
     * @brief Bind a new cell value and refresh displayed text without applying a user edit.
     * @param[in] ref Value copied into the widget; referenced engine/layer/store must remain valid during editing.
     * @details Programmatic combo updates suppress edit callbacks. The flags/initial-quality buttons disable when their required model/engine or object ID is absent.
     */
    void setValue(const DataObjectRef &ref);

    /*! Adopt \p name as the current value (a map pick landed) and notify.
     * @param[in] name Selected object ID copied verbatim; empty clears the current selection.
     * @details Rebuilds the filtered list and emits valueChanged; adapter WRITE slots normally perform the engine assignment.
     */
    void applyPickedName(const QString &name);

signals:
    /**
     * @brief Notify the item delegate that a user interaction produced a commit candidate.
     * @details Read value() for the reference and cached text; the signal alone is not a guarantee that an engine setter succeeded.
     */
    void valueChanged();

private slots:
    /**
     * @brief Publish a user-selected object name unless refreshing the combo.
     * @param[in] text Current literal object ID, including empty for no assignment.
     * @details Changes the ref only, then emits valueChanged.
     */
    void onComboTextChanged(const QString &text);
    /**
     * @brief Dispatch browse/create/map-pick behavior for the bound reference kind.
     * @details Requires a model layer. May open dialogs or start a layer-owned node-pick session; capture-node picks push an inlet-usage edit through the layer. Successful dialog picks update currentName and notify; unsupported families display guidance.
     */
    void onPickerClicked();

private:
    /*! Refill the combo from the engine, filtered by `m_ref.kind` /
     *  `m_ref.typeLock`. Preserves the current selection when possible.
     * @details Uses the bound layer/engine to enumerate kind/type-filtered IDs; keeps assigned pump/rating curves visible even when their current type is incompatible. Blocks combo signals while replacing items.
     */
    void repopulate();

    DataObjectRef m_ref;
    LabeledPickerCombo *m_combo = nullptr;
    /*! Re-entrancy guard: when `setValue` or `repopulate` updates the
     *  combo text programmatically, `currentTextChanged` fires — we
     *  bail in that case so the model doesn't see a phantom user edit. */
    bool m_suppressTextChange = false;
};

#endif // DATAOBJECTPICKEREDITOR_H
