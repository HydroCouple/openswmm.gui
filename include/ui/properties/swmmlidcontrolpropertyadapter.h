/**
 * @file      swmmlidcontrolpropertyadapter.h
 * @brief     Property-tree adapter for [LID_CONTROLS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * This property adapter exposes the shared object name and translated labels
 * for [LID_CONTROLS]. Structured parameter editing is handled by the dedicated
 * LID control editor rather than additional scalar properties in this facade.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMLIDCONTROLPROPERTYADAPTER_H
#define SWMMLIDCONTROLPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMLIDControlPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;
    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMLIDCONTROLPROPERTYADAPTER_H
