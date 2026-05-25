/*!
 * \file   dataobjectpickereditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    explicit DataObjectPickerEditor(QWidget *parent = nullptr);

    [[nodiscard]] DataObjectRef value() const noexcept { return m_ref; }
    void setValue(const DataObjectRef &ref);

signals:
    void valueChanged();

private slots:
    void onComboTextChanged(const QString &text);
    void onPickerClicked();

private:
    /*! Refill the combo from the engine, filtered by `m_ref.kind` /
     *  `m_ref.typeLock`. Preserves the current selection when possible. */
    void repopulate();

    DataObjectRef        m_ref;
    LabeledPickerCombo  *m_combo = nullptr;
    /*! Re-entrancy guard: when `setValue` or `repopulate` updates the
     *  combo text programmatically, `currentTextChanged` fires — we
     *  bail in that case so the model doesn't see a phantom user edit. */
    bool                 m_suppressTextChange = false;
};

#endif // DATAOBJECTPICKEREDITOR_H
