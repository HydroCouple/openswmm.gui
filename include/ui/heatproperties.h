#pragma once
#include <QCoreApplication>
#include <QString>
#include <array>
#include <cmath>
#include <limits>

namespace OpenSWMMVis {
// Attribute ordinals follow SWMM_HeatAttr. Shared by the table, import mapping
// and raster assignment so units and the refuse-not-clamp ranges agree.
struct HeatProperty { const char* key; const char* label; double minimum; double maximum; };
inline const std::array<HeatProperty,16>& heatProperties() {
    static const std::array<HeatProperty,16> fields={{
        {"albedo","Water albedo",0,1},{"shade","Shade factor",0,1},
        {"sky_view","Sky view",0,1},{"emiss_water","Water emissivity",0,1},
        {"emiss_landcover","Land-cover emissivity",0,1},
        {"landcover_temperature","Land-cover temperature (°C)",-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()},
        {"thermal_diffusivity","Bed thermal diffusivity (m²/s)",std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()},
        {"solute_diffusivity","Bed solute diffusivity (m²/s)",0,std::numeric_limits<double>::max()},
        {"bed_thickness","Bed thickness (m)",std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()},
        {"ground_depth","Deep-ground depth (m)",std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()},
        {"ground_temperature","Ground temperature (°C)",-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()},
        {"hyporheic_velocity","Hyporheic velocity (m/s)",0,std::numeric_limits<double>::max()},
        {"sediment_density","Sediment density (kg/m³)",std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()},
        {"sediment_specific_heat","Sediment specific heat (J/kg/K)",std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()},
        {"mesh_porosity","Mesh-bed porosity (0–1)",0,1},
        {"ground_source","Ground heat source mode (0 prescribed, 1 mesh contacts)",0,1}
    }};return fields;
}
inline QString heatPropertyLabel(int attr) { return QCoreApplication::translate("HeatCfg",heatProperties().at(attr).label); }
inline int heatPropertyIndex(const QString& key) {
    for(int i=0;i<int(heatProperties().size());++i)if(key==QStringLiteral("heat.")+QLatin1String(heatProperties()[i].key))return i;
    return -1;
}
inline bool heatPropertyValueValid(int attr,double value) {
    return attr>=0&&attr<int(heatProperties().size())&&std::isfinite(value)
        &&value>=heatProperties()[attr].minimum&&value<=heatProperties()[attr].maximum
        &&(attr!=15||(value==0.0||value==1.0));
}
}
