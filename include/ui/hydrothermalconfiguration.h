/**
 * @file      hydrothermalconfiguration.h
 * @brief     Persistent configuration of heat components and their owners.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_process_components.h>
#include <QUuid>

namespace OpenSWMM::Ui
{

// A configured heat component needs a persistent owner. Generate a stable,
// collision-resistant relative asset name; the native writer publishes it at
// save time. Existing component paths always retain their authored spelling.
/**
 * @brief Ensure configured heat authoring has a registered persistent component asset name.
 * @param[in] engine Borrowed live engine.
 * @return Authoring-flags query status when it fails or no configured flag is set; SWMM_OK for an existing heat component, otherwise the registration status.
 * @note Registers org.hydrocouple.openswmm.heat with a relative UUID-based heat-*.cfg name only when absent. Does not write a file; native save publishes it later. Existing authored paths are preserved.
 */
inline int ensureHeatConfigurationFile(SWMM_Engine engine)
{
    unsigned flags = 0;
    const int status = swmm_heat_get_authoring_flags(engine, &flags);
    if (status != SWMM_OK || !(flags & 1u))
        return status;
    constexpr auto id = "org.hydrocouple.openswmm.heat";
    if (swmm_process_component_find(engine, id) >= 0)
        return SWMM_OK;
    const auto name = "heat-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".cfg";
    return swmm_process_component_register(engine, id, name.toUtf8().constData());
}

} // namespace OpenSWMM::Ui
