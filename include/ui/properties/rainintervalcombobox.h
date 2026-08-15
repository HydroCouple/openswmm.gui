/*!
 * \file   rainintervalcombobox.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    Q_PROPERTY(RainIntervalRef value
               READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit RainIntervalComboBox(QWidget *parent = nullptr);

    [[nodiscard]] RainIntervalRef value() const { return m_ref; }
    void setValue(const RainIntervalRef &ref);

signals:
    void valueChanged();

private slots:
    void onEdited();

private:
    void apply();

    RainIntervalRef m_ref;
    /*! Re-entrancy guard — `setValue` sets the text programmatically; the
     *  edit slot bails while set so the engine doesn't see a phantom write. */
    bool            m_suppressApply = false;
};

#endif // RAININTERVALCOMBOBOX_H
