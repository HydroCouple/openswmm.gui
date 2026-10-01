#pragma once

#include <QDoubleSpinBox>
#include <QVariant>

namespace OpenSWMM::Ui {

// Preserve exact engine values while the user leaves their rounded display
// unchanged. This also protects values outside a legacy editor's range.
inline void setHydratedValue(QDoubleSpinBox *spin, double value)
{
    spin->setValue(value);
    spin->setProperty("swmmExactValue", value);
    spin->setProperty("swmmDisplayedValue", spin->value());
}

inline double preciseValue(const QDoubleSpinBox *spin)
{
    const QVariant original = spin->property("swmmExactValue");
    const QVariant displayed = spin->property("swmmDisplayedValue");
    if (original.isValid() && displayed.isValid()
        && spin->value() == displayed.toDouble())
        return original.toDouble();
    return spin->value();
}

} // namespace OpenSWMM::Ui
