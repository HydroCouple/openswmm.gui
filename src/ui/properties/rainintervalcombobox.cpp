/*!
 * \file   rainintervalcombobox.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/rainintervalcombobox.h"

#include <QLineEdit>

RainIntervalComboBox::RainIntervalComboBox(QWidget *parent)
    : QComboBox(parent)
{
    // Legacy esComboEdit: editable combo seeded with the standard presets.
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    addItems(rain_interval::presetsHMM());

    // Selecting a preset or finishing a typed edit recomputes seconds.
    connect(this, QOverload<int>::of(&QComboBox::activated),
            this, &RainIntervalComboBox::onEdited);
    if (QLineEdit *le = lineEdit())
        connect(le, &QLineEdit::editingFinished,
                this, &RainIntervalComboBox::onEdited);
}

void RainIntervalComboBox::setValue(const RainIntervalRef &ref)
{
    m_ref = ref;
    m_suppressApply = true;
    setCurrentText(rain_interval::secondsToHMM(ref.seconds));
    m_suppressApply = false;
}

void RainIntervalComboBox::onEdited()
{
    if (m_suppressApply) return;
    apply();
}

void RainIntervalComboBox::apply()
{
    const int secs = rain_interval::hmmToSeconds(currentText());
    if (secs < 0) {
        // Malformed entry — restore the last good value, no write.
        m_suppressApply = true;
        setCurrentText(rain_interval::secondsToHMM(m_ref.seconds));
        m_suppressApply = false;
        return;
    }
    if (secs == m_ref.seconds) return;
    // The engine write is performed by the owning adapter's WRITE slot
    // (setRainIntervalRef), keeping the engine the single source of truth —
    // same MVC contract as DataObjectRef. The editor only carries the value.
    m_ref.seconds = secs;
    emit valueChanged();
}
