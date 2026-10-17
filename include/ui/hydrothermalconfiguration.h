#pragma once

#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_process_components.h>
#include <QUuid>

namespace OpenSWMM::Ui {

// A configured heat component needs a persistent owner. Generate a stable,
// collision-resistant relative asset name; the native writer publishes it at
// save time. Existing component paths always retain their authored spelling.
inline int ensureHeatConfigurationFile(SWMM_Engine engine)
{
    unsigned flags=0;
    const int status=swmm_heat_get_authoring_flags(engine,&flags);
    if(status!=SWMM_OK || !(flags&1u))return status;
    constexpr auto id="org.hydrocouple.openswmm.heat";
    if(swmm_process_component_find(engine,id)>=0)return SWMM_OK;
    const auto name="heat-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".cfg";
    return swmm_process_component_register(engine,id,name.toUtf8().constData());
}

} // namespace OpenSWMM::Ui
