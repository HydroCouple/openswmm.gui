// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_snow2d.h>
#include <algorithm>
#include <cmath>
#include <vector>
namespace openswmmvis::simulation {
// Call on the engine-owning worker; only these value strings cross to the UI.
inline QStringList snowLedgerRows(SWMM_Engine eng)
{
    QStringList rows;
    const auto water=[&](const QString& domain,double initial,double precipitation,double liquid,double sublimation,double removal,double stored,double residual) {
        rows << QStringLiteral("%1 snow · initial %2 · precipitation %3 · liquid out %4 · sublimation %5 · removed %6 · stored %7 · residual %8 m³")
            .arg(domain).arg(initial,0,'g',7).arg(precipitation,0,'g',7).arg(liquid,0,'g',7).arg(sublimation,0,'g',7).arg(removal,0,'g',7).arg(stored,0,'g',7).arg(residual,0,'g',7);
    };
    SWMM_SnowLedger b{};
    if(swmm_snow_get_ueb_ledger(eng,&b)==SWMM_OK) {
        water(QStringLiteral("Subcatchment UEB"),b.initial_m3,b.precipitation_m3,b.liquid_m3+b.throughfall_m3,b.sublimation_m3,b.removed_m3,b.stored_m3,b.residual_m3);
        rows << QStringLiteral("Subcatchment UEB energy · initial %1 · net input %2 · depletion remapping %3 · plowed out %4 · stored %5 · residual %6 kJ")
            .arg(b.initial_energy_kj,0,'g',7).arg(b.net_energy_kj,0,'g',7).arg(b.geometry_energy_kj,0,'g',7).arg(b.plowed_energy_kj,0,'g',7).arg(b.stored_energy_kj,0,'g',7).arg(b.energy_residual_kj,0,'g',7);
    }
    int count=0;
    if(swmm_2d_cell_count(eng,&count)==SWMM_OK&&count>0) {
        // Bounded chunks avoid one giant allocation in a large mesh's UI tick.
        SWMM_MeshSnowLedger sum{};double geometry_energy_kj=0;bool any=false,energy=false,ok=true;
        for(int first=0;first<count;first+=1024) {
            const int n=std::min(1024,count-first);std::vector<SWMM_MeshSnowLedger> cells(n);std::vector<double> geometry(n);
            if(swmm_snow2d_get_ledgers(eng,first,n,cells.data())!=SWMM_OK||
               swmm_snow2d_get_geometry_energy(eng,first,n,geometry.data())!=SWMM_OK){ok=false;break;}
            for(int k=0;k<n;++k)if(std::isfinite(cells[k].stored_m3)) {
                const auto& c=cells[k];
                any=true;sum.initial_m3+=c.initial_m3;sum.precipitation_m3+=c.precipitation_m3;
                sum.liquid_m3+=c.liquid_m3;sum.sublimation_m3+=c.sublimation_m3;sum.stored_m3+=c.stored_m3;sum.residual_m3+=c.residual_m3;
                if(std::isfinite(c.energy_residual_kj)){
                    energy=true;sum.net_energy_kj+=c.net_energy_kj;sum.initial_energy_kj+=c.initial_energy_kj;
                    geometry_energy_kj+=geometry[k];sum.stored_energy_kj+=c.stored_energy_kj;
                    sum.energy_residual_kj+=c.energy_residual_kj;
                }
            }
        }
        if(any&&ok) {
            water(QStringLiteral("Mesh"),sum.initial_m3,sum.precipitation_m3,sum.liquid_m3,sum.sublimation_m3,0,sum.stored_m3,sum.residual_m3);
            if(energy)rows<<QStringLiteral("Mesh UEB energy · initial %1 · net input %2 · depletion remapping %3 · stored %4 · residual %5 kJ")
                .arg(sum.initial_energy_kj,0,'g',7).arg(sum.net_energy_kj,0,'g',7).arg(geometry_energy_kj,0,'g',7)
                .arg(sum.stored_energy_kj,0,'g',7).arg(sum.energy_residual_kj,0,'g',7);
        }
    }
    return rows;
}
}
