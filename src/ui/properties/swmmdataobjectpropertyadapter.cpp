/**
 * @file      swmmdataobjectpropertyadapter.cpp
 * @brief     Common base class for the 14 non-spatial Data Object property adapters.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmmdataobjectpropertyadapter.h"

#include "core/unitsystem.h"

SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter(SWMM_Engine engine, QString name,
                                                             QObject *parent)
    : QObject(parent), m_engine(engine), m_name(std::move(name))
{
    if (auto *u = UnitSystem::instance()) {
        connect(u, &UnitSystem::unitsChanged, this, [this] { emit displayLabelsChanged(); });
    }
}

void SWMMDataObjectPropertyAdapter::setName(const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == m_name)
        return;
    emit renameRequested(m_name, trimmed);
}
