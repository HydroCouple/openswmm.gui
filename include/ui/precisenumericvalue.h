/**
 * @file      precisenumericvalue.h
 * @brief     Preservation of exact numeric values behind rounded editor displays.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QDoubleSpinBox>
#include <QVariant>

namespace OpenSWMM::Ui
{

// Preserve exact engine values while the user leaves their rounded display
// unchanged. This also protects values outside a legacy editor's range.
/**
 * @brief Store the exact model value behind a possibly rounded/clamped spin display.
 * @param[in,out] spin Required QDoubleSpinBox receiving setValue() plus exact/displayed dynamic properties.
 * @param[in] value Raw model value; Qt may clamp or round its visual representation. Signals are not blocked by this helper.
 */
inline void setHydratedValue(QDoubleSpinBox *spin, double value)
{
    spin->setValue(value);
    spin->setProperty("swmmExactValue", value);
    spin->setProperty("swmmDisplayedValue", spin->value());
}

/**
 * @brief Preserve a hydrated exact value while the current spin value still equals its hydrated display.
 * @param[in] spin Required spin box with optional hydration properties.
 * @return Original exact value if both properties exist and displayed equality holds; otherwise current accepted spin value. Returning to the same displayed value can also recover the original.
 */
inline double preciseValue(const QDoubleSpinBox *spin)
{
    const QVariant original = spin->property("swmmExactValue");
    const QVariant displayed = spin->property("swmmDisplayedValue");
    if (original.isValid() && displayed.isValid() && spin->value() == displayed.toDouble())
        return original.toDouble();
    return spin->value();
}

} // namespace OpenSWMM::Ui
