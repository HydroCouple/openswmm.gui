#include "assignment/groundwatertransportassignment.h"
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_tables.h>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::assignment {
namespace {
bool fail(QString *error,const QString &text){if(error)*error=text;return false;}
bool editable(SWMM_Engine engine){int state=0;return engine&&swmm_engine_get_state(engine,&state)==SWMM_OK&&(state==SWMM_STATE_BUILDING||state==SWMM_STATE_OPENED);}
QString option(SWMM_Engine e,const char *key,bool groundwater,bool *ok=nullptr)
{
    char value[4096]{}; const int code=groundwater?swmm_gw_transport_option_get(e,key,value,sizeof value):swmm_options_get(e,key,value,sizeof value);
    if(ok)*ok=code==SWMM_OK; return code==SWMM_OK?QString::fromUtf8(value):QString();
}
const TransportSpecies *species(const GroundwaterTransportSnapshot &s,const QString &id)
{for(const auto &row:s.species)if(row.id==id)return &row;return nullptr;}
bool sameKey(const GroundwaterQualityRow &a,const GroundwaterQualityRow &b)
{return a.scope==b.scope&&a.cell==b.cell&&a.tag==b.tag&&a.zone==b.zone&&a.layer==b.layer&&a.species==b.species;}
QString seriesError(const GroundwaterTransportSnapshot &s,const QString &name,bool signedValues)
{
    if(name.isEmpty())return {};
    if(!s.timeSeries.contains(name)||s.timeSeries.value(name).isEmpty())return "The named time series is missing or has no readable samples: "+name;
    double previous=-std::numeric_limits<double>::infinity();
    for(const auto &point:s.timeSeries.value(name)) {
        if(!std::isfinite(point.x())||!std::isfinite(point.y())||(!signedValues&&point.y()<0))return "The time series has nonfinite or disallowed negative values: "+name;
        if(point.x()<=previous)return "The time series must have strictly increasing times: "+name;
        previous=point.x();
    }
    return {};
}
QString termError(const GroundwaterTransportSnapshot &s,const GroundwaterSpeciesTerm &term)
{
    const auto *metadata=species(s,term.species);
    if(!metadata)return "The groundwater species is unavailable: "+term.species;
    if(term.kind!="CONC"&&term.kind!="MASS")return "A source species term must be CONC or MASS.";
    if(term.kind=="MASS"&&!metadata->massSupported)return "Mass-rate units are not established for "+term.species+". Use a supported concentration term.";
    if(term.kind=="CONC"&&metadata->nativeConcUnits.isEmpty())return "Concentration units are not established for "+term.species+".";
    const bool signedValues=term.kind=="CONC"&&metadata->signedConcentration;
    if(!std::isfinite(term.value)||(!signedValues&&term.value<0))return "A species term has a nonfinite or disallowed negative value.";
    return seriesError(s,term.series,signedValues);
}
QString sourceError(const GroundwaterTransportSnapshot &s,const GroundwaterSourceRow &row)
{
    if(row.scope!=2||row.cell<0||row.cell>=s.cellCount||row.name.isEmpty()||!std::isfinite(row.flow)||!std::isfinite(row.scale)||row.scale<0)return "Invalid cell source, flow or scale.";
    QString error=seriesError(s,row.series,true);if(!error.isEmpty())return error;
    QSet<QString> names;
    for(const auto &term:row.terms){if(names.contains(term.species))return "Each source may have only one term per species.";names.insert(term.species);error=termError(s,term);if(!error.isEmpty())return error;}
    return {};
}
bool writeQuality(SWMM_Engine e,const GroundwaterQualityRow &row)
{return swmm_gw_init_quality_set(e,row.scope,row.tag.toUtf8().constData(),row.cell,row.zone,row.layer,row.species.toUtf8().constData(),row.value)==SWMM_OK;}
bool transition(SWMM_Engine e,const GroundwaterTransportSnapshot &from,const GroundwaterTransportSnapshot &to,
                GroundwaterTransportTarget target,const std::function<bool(int)> &hook,int &writes)
{
    int attempt=0;
    auto allowed=[&]{return !hook||hook(++attempt);};
    if(target==GroundwaterTransportTarget::InitialQuality){
        for(int i=from.quality.size()-1;i>=to.quality.size();--i){if(!allowed()||swmm_gw_init_quality_remove(e,i)!=SWMM_OK)return false;++writes;}
        for(int i=0;i<to.quality.size();++i)if(i>=from.quality.size()||from.quality[i]!=to.quality[i]){if(!allowed()||!writeQuality(e,to.quality[i]))return false;++writes;}
    }else{
        for(int i=from.sources.size()-1;i>=to.sources.size();--i){if(!allowed()||swmm_gw_source_remove(e,i)!=SWMM_OK)return false;++writes;}
        for(int i=from.sources.size();i<to.sources.size();++i){
            const auto &row=to.sources[i];
            if(!allowed()||swmm_gw_source_set(e,row.name.toUtf8().constData(),row.scope,row.tag.toUtf8().constData(),row.cell,row.flow,row.series.toUtf8().constData())!=SWMM_OK)return false;++writes;
            if(!allowed()||swmm_gw_source_scale_set(e,i,row.scale)!=SWMM_OK)return false;++writes;
            for(const auto &term:row.terms){if(!allowed()||swmm_gw_source_species_set(e,i,term.species.toUtf8().constData(),term.kind.toUtf8().constData(),term.value,term.series.toUtf8().constData())!=SWMM_OK)return false;++writes;}
        }
    }
    return true;
}
GroundwaterTransportResult commit(SWMM_Engine e,const GroundwaterTransportSnapshot &from,const GroundwaterTransportSnapshot &to,
                                  GroundwaterTransportTarget target,const std::function<bool(int)> &hook)
{
    GroundwaterTransportResult result;
    if(!editable(e)){result.error="Groundwater transport inputs are editable only before a run starts.";return result;}
    GroundwaterTransportSnapshot current;
    if(!readGroundwaterTransportSnapshot(e,&current,&result.error))return result;
    if(current!=from){result.error="Groundwater inputs or species/time-series definitions changed. Preview again before applying or undoing.";return result;}
    if(from==to){result.success=true;return result;}
    int writes=0;
    const bool wrote=transition(e,from,to,target,hook,writes);
    result.changed=writes>0;
    QString readError;
    if(wrote&&readGroundwaterTransportSnapshot(e,&current,&readError)&&current==to){result.success=true;return result;}
    // Source rows are appended and initial quality preserves original keys and
    // order. Restore only the managed suffix/values; no clear-and-rebuild.
    bool restored=readGroundwaterTransportSnapshot(e,&current,&readError);
    int rollbackWrites=0;
    if(restored)restored=transition(e,current,from,target,{},rollbackWrites);
    GroundwaterTransportSnapshot check;
    restored=restored&&readGroundwaterTransportSnapshot(e,&check,&readError)&&check==from;
    result.changed=result.changed||rollbackWrites>0||!restored;
    result.rollbackComplete=restored;
    result.error=restored?(result.changed?"The engine refused or changed the assignment. Managed rows were restored; the project is marked changed because engine authoring metadata may have changed.":"The engine refused the assignment before changing managed rows.")
        :"The engine refused the assignment and restoration could not be verified. Reload the last saved model before further editing.";
    return result;
}
}
bool GroundwaterTransportSnapshot::operator==(const GroundwaterTransportSnapshot &other) const
{
    if(cellCount!=other.cellCount||aquifers!=other.aquifers||cellTags!=other.cellTags||activeCells!=other.activeCells
        ||quality!=other.quality||qualityFile!=other.qualityFile||sources!=other.sources||species!=other.species||options!=other.options
        ||timeSeries.keys()!=other.timeSeries.keys())return false;
    // QPointF equality can be fuzzy; stale forcing detection must be exact.
    for(auto it=timeSeries.cbegin();it!=timeSeries.cend();++it){
        const auto &a=it.value();const auto &b=other.timeSeries.value(it.key());if(a.size()!=b.size())return false;
        for(int i=0;i<a.size();++i)if(a[i].x()!=b[i].x()||a[i].y()!=b[i].y())return false;
    }
    return true;
}
QVector<TransportSpecies> groundwaterTransportSpecies(SWMM_Engine e,QString *error)
{
    if(error)error->clear();QVector<TransportSpecies> result;
    if(!e){fail(error,"No groundwater model is available.");return result;}
    const bool quality=option(e,"IGNORE_QUALITY",false)!="YES";
    if(quality&&option(e,"TRANSPORT_POLLUTANTS",true)=="YES"){
        const int count=swmm_pollutant_count(e);
        for(int i=0;i<count;++i){const char *name=swmm_pollutant_id(e,i);int unit=-1;
            if(!name||swmm_pollutant_get_units(e,i,&unit)!=SWMM_OK){fail(error,"Unable to read pollutant identity or units.");return {};}
            TransportSpecies s;s.id=QString::fromUtf8(name);s.massSupported=unit>=0&&unit<=2;
            if(unit==0){s.nativeConcUnits="mg/L";s.nativeMassRateUnits="mg/s";}
            else if(unit==1){s.nativeConcUnits="ug/L";s.nativeMassRateUnits="ug/s";}
            else if(unit==2){s.nativeConcUnits="count/L";s.nativeMassRateUnits="count/s";}
            result.append(s);
        }
    }
    if(quality&&option(e,"TRANSPORT_MSX",true)=="YES"){
        QVector<TransportSpecies> reactions;bool hasWall=false;
        for(int i=0;i<swmm_reaction_species_count(e);++i){char name[4096]{},unit[4096]{};int wall=0;double atol=0,rtol=0;
            if(swmm_reaction_species_get(e,i,name,sizeof name,&wall,unit,sizeof unit,&atol,&rtol)!=SWMM_OK){fail(error,"Unable to read reaction species.");return {};}
            hasWall=hasWall||wall;reactions.append({QString::fromUtf8(name),QString::fromUtf8(unit),{},false,false});
        }
        // The groundwater engine refuses systems containing WALL species.
        if(!hasWall)for(const auto &s:reactions){bool duplicate=false;for(const auto &existing:result)if(existing.id==s.id)duplicate=true;if(!duplicate)result.append(s);}
    }
    // Groundwater stores authored age directly in seconds. Surface output's
    // conversion to hours does not apply to groundwater initial/source rows.
    if(option(e,"WATER_AGE",false)=="YES"&&option(e,"TRANSPORT_AGE",true)=="YES")result.append({"__WATER_AGE__","s",{},false,false});
    if(option(e,"HEAT_TRANSPORT",false)=="YES"&&option(e,"TRANSPORT_TEMPERATURE",true)=="YES")result.append({"__TEMPERATURE__","degC",{},false,true});
    return result;
}
bool readGroundwaterTransportSnapshot(SWMM_Engine e,GroundwaterTransportSnapshot *out,QString *error)
{
    if(error)error->clear();if(!e||!out)return fail(error,"No groundwater model is available.");
    GroundwaterTransportSnapshot result;
    if(swmm_2d_cell_count(e,&result.cellCount)!=SWMM_OK)return fail(error,"Cannot read the groundwater cell count.");
    QString readError;
    if(!readAquiferSnapshot(e,&result.aquifers,&readError))return fail(error,readError);
    int global=-1; QHash<QString,int> tags; QHash<int,int> cells;
    for(int i=0;i<result.aquifers.rows.size();++i){const auto &row=result.aquifers.rows[i];
        if(row.scope==0)global=i;else if(row.scope==1)tags[row.tag]=i;else if(row.scope==2)cells[row.cell]=i;
    }
    result.cellTags.reserve(result.cellCount);result.activeCells.reserve(result.cellCount);
    for(int cell=0;cell<result.cellCount;++cell){
        char tag[4096]{};if(swmm_2d_get_triangle_tag(e,cell,tag,sizeof tag)!=SWMM_OK)return fail(error,"Cannot read target cell tags.");
        const QString cellTag=QString::fromUtf8(tag);result.cellTags.append(cellTag);
        const int index=cells.value(cell,tags.value(cellTag,global));
        const AquiferRow *effective=index>=0?&result.aquifers.rows[index]:nullptr;
        result.activeCells.append(effective&&effective->ks>0&&effective->zs>0&&effective->thetaS>effective->thetaR);
    }
    result.species=groundwaterTransportSpecies(e,&readError);if(!readError.isEmpty())return fail(error,readError);
    for(const auto &key:{"TRANSPORT_POLLUTANTS","TRANSPORT_MSX","TRANSPORT_AGE","TRANSPORT_TEMPERATURE","DISPERSION","CONDUCTION","THERMAL_MIXING","SURFACE_THERMAL_BC","DEEP_THERMAL_BC","C_DIFF"}){
        bool ok=false;result.options[key]=option(e,key,true,&ok);if(!ok)return fail(error,"Cannot read groundwater transport option "+QString::fromLatin1(key));
    }
    for(const auto &key:{"IGNORE_QUALITY","WATER_AGE","HEAT_TRANSPORT"}){bool ok=false;result.options[QStringLiteral("GLOBAL:")+key]=option(e,key,false,&ok);if(!ok)return fail(error,"Cannot read global transport option "+QString::fromLatin1(key));}
    char path[32768]{};if(swmm_gw_init_quality_file_get(e,path,sizeof path)!=SWMM_OK)return fail(error,"Cannot read initial-quality FILE reference.");result.qualityFile=QString::fromUtf8(path);
    const int qualities=swmm_gw_init_quality_count(e);if(qualities<0)return fail(error,"Cannot read initial-quality rows.");
    for(int i=0;i<qualities;++i){GroundwaterQualityRow row;char tag[4096]{},name[4096]{};
        if(swmm_gw_init_quality_get(e,i,&row.scope,tag,sizeof tag,&row.cell,&row.zone,&row.layer,name,sizeof name,&row.value)!=SWMM_OK)return fail(error,"Cannot read initial-quality row.");
        row.tag=QString::fromUtf8(tag);row.species=QString::fromUtf8(name);result.quality.append(row);
    }
    const int sources=swmm_gw_source_count(e);if(sources<0)return fail(error,"Cannot read groundwater sources.");
    for(int i=0;i<sources;++i){GroundwaterSourceRow row;char name[4096]{},tag[4096]{},series[4096]{};
        if(swmm_gw_source_get(e,i,name,sizeof name,&row.scope,tag,sizeof tag,&row.cell,&row.flow,series,sizeof series)!=SWMM_OK||swmm_gw_source_scale_get(e,i,&row.scale)!=SWMM_OK)return fail(error,"Cannot read groundwater source or scale.");
        row.name=QString::fromUtf8(name);row.tag=QString::fromUtf8(tag);row.series=QString::fromUtf8(series);
        const int terms=swmm_gw_source_species_count(e,i);if(terms<0)return fail(error,"Cannot read source species terms.");
        for(int t=0;t<terms;++t){GroundwaterSpeciesTerm term;char speciesName[4096]{},kind[64]{},ts[4096]{};
            if(swmm_gw_source_species_get(e,i,t,speciesName,sizeof speciesName,kind,sizeof kind,&term.value,ts,sizeof ts)!=SWMM_OK)return fail(error,"Cannot read source species term.");
            term.species=QString::fromUtf8(speciesName);term.kind=QString::fromUtf8(kind);term.series=QString::fromUtf8(ts);row.terms.append(term);
        }result.sources.append(row);
    }
    const int tables=swmm_table_count(e);if(tables<0)return fail(error,"Cannot read time-series catalog.");
    for(int i=0;i<tables;++i){int type=-1;if(swmm_table_get_type(e,i,&type)!=SWMM_OK)return fail(error,"Cannot read table type.");if(type!=0)continue;
        const char *name=swmm_table_id(e,i);int count=0;if(!name||swmm_table_get_point_count(e,i,&count)!=SWMM_OK||count<0||count>1000000)return fail(error,"Cannot read bounded time-series samples.");
        QVector<QPointF> points;points.reserve(count);
        for(int p=0;p<count;++p){double x=0,y=0;if(swmm_table_get_point(e,i,p,&x,&y)!=SWMM_OK)return fail(error,"Cannot read time-series point.");points.append({x,y});}
        result.timeSeries[QString::fromUtf8(name)]=points;
    }
    *out=std::move(result);return true;
}
GroundwaterTransportPreview previewGroundwaterTransport(const GroundwaterTransportRequest &r)
{
    GroundwaterTransportPreview p;p.before=r.before;p.after=r.before;p.target=r.target;
    auto reject=[&](const QString &error){p.after=p.before;p.cells.clear();p.error=error;return p;};
    if(r.cells.isEmpty()||r.cells.size()!=r.values.size())return reject("Select cells and provide one value per cell.");
    if(r.target!=GroundwaterTransportTarget::InitialQuality&&r.target!=GroundwaterTransportTarget::Source)return reject("Unknown groundwater transport target.");
    if(r.target==GroundwaterTransportTarget::InitialQuality&&(!r.before.qualityFile.isEmpty()))return reject("Initial-quality rows are linked to a FILE source. Edit that source before assigning cell values.");
    const auto *metadata=species(r.before,r.species);
    if(r.target==GroundwaterTransportTarget::InitialQuality){if(r.zone!=0&&r.zone!=1)return reject("Choose the saturated or unsaturated zone.");if(!metadata||metadata->nativeConcUnits.isEmpty())return reject("Choose an enabled species with declared concentration units.");}
    QSet<int> seen;QVector<int> valid;
    for(int i=0;i<r.cells.size();++i){if(r.cells[i]<0||r.cells[i]>=r.before.cellCount||seen.contains(r.cells[i]))return reject("A target cell is out of range or repeated.");seen.insert(r.cells[i]);
        if(std::isnan(r.values[i])&&r.skipNoData){p.skippedCells.append(r.cells[i]);continue;}
        if(!std::isfinite(r.values[i]))return reject("A target value is nonfinite. Select an explicit NoData policy.");
        if(r.before.activeCells.size()!=r.before.cellCount||!r.before.activeCells[r.cells[i]])return reject("A selected cell has no active authored aquifer. Define aquifer properties before assigning transport.");
        valid.append(i);
    }
    if(valid.isEmpty())return reject("No target cells have usable source values.");
    if(r.target==GroundwaterTransportTarget::InitialQuality){
        QHash<int,int> targetRows;
        for(int i=0;i<p.after.quality.size();++i){const auto &row=p.after.quality[i];
            if(row.scope==2&&row.zone==r.zone&&row.layer==-1&&row.species==r.species)
                targetRows[row.cell]=targetRows.contains(row.cell)?-2:i;
        }
        for(int i:valid){if(r.values[i]<0&&!metadata->signedConcentration)return reject("This species does not accept negative initial concentrations.");
            GroundwaterQualityRow row;row.scope=2;row.cell=r.cells[i];row.zone=r.zone;row.layer=-1;row.species=r.species;row.value=r.values[i];
            const int match=targetRows.value(row.cell,-1);
            if(match==-2)return reject("Duplicate authored initial-quality keys must be resolved before assignment.");
            if(match<0){targetRows[row.cell]=p.after.quality.size();p.after.quality.append(row);}else if(p.after.quality[match].value==row.value)continue;else p.after.quality[match]=row;
            p.cells.append(row.cell);
        }
    }else{
        static const QRegularExpression namePattern(QStringLiteral("^[^\\s\\\";\\[\\]]+$"));
        if(r.sourceName.size()>60||!namePattern.match(r.sourceName).hasMatch())return reject("Use a nonempty source name without whitespace, quotes or section delimiters (maximum 60 characters).");
        if(r.distribution!=GroundwaterSourceDistribution::PerCell&&r.distribution!=GroundwaterSourceDistribution::RegionTotal)return reject("Unknown source distribution.");
        const bool total=r.distribution==GroundwaterSourceDistribution::RegionTotal;double area=0;
        if(total){if(r.areaWeights.size()!=r.cells.size())return reject("Region-total sources require one positive cell area per selection.");
            for(int i:valid){if(!std::isfinite(r.areaWeights[i])||r.areaWeights[i]<=0||r.values[i]!=r.values[valid.front()])return reject("Region-total sources require a single total flow and positive finite cell areas.");area+=r.areaWeights[i];}
            if(!std::isfinite(area)||area<=0)return reject("The selected total area is not finite and positive.");
        }
        GroundwaterSourceRow common;common.name=r.sourceName;common.cell=r.cells[valid.front()];common.flow=r.values[valid.front()];common.series=r.flowSeries;common.terms=r.terms;
        const QString commonError=sourceError(r.before,common);if(!commonError.isEmpty())return reject(commonError);
        const auto seriesNonzero=[&](const QString &name){for(const auto &point:r.before.timeSeries.value(name))if(point.y()!=0)return true;return false;};
        bool massNonzero=false;
        for(const auto &term:r.terms)if(term.kind=="MASS"&&(term.series.isEmpty()?term.value!=0:seriesNonzero(term.series)))massNonzero=true;
        const bool flowSeriesNonzero=!r.flowSeries.isEmpty()&&seriesNonzero(r.flowSeries);
        QSet<QString> names;for(const auto &row:p.before.sources)names.insert(row.name);
        for(int i:valid){
            if(!massNonzero&&(r.flowSeries.isEmpty()?r.values[i]==0:!flowSeriesNonzero))continue;GroundwaterSourceRow row;row.cell=r.cells[i];row.flow=r.values[i];row.scale=total?r.areaWeights[i]/area:1;row.series=r.flowSeries;row.terms=r.terms;
            const QString base=r.sourceName+"_cell_"+QString::number(row.cell+1);row.name=base;int suffix=2;while(names.contains(row.name))row.name=base+"_"+QString::number(suffix++);names.insert(row.name);
            p.after.sources.append(row);p.cells.append(row.cell);
        }
    }
    return p;
}
namespace {
QString previewError(const GroundwaterTransportPreview &p)
{
    if(!p.error.isEmpty())return p.error;
    auto metadata=p.after;metadata.quality=p.before.quality;metadata.sources=p.before.sources;
    if(metadata!=p.before)return "The preview contains changes outside the selected transport inputs.";
    if(p.target==GroundwaterTransportTarget::InitialQuality){
        if(p.after.sources!=p.before.sources||p.after.quality.size()<p.before.quality.size())return "Invalid initial-quality preview.";
        for(int i=0;i<p.after.quality.size();++i){const auto &row=p.after.quality[i];
            if(i<p.before.quality.size()&&row==p.before.quality[i])continue;
            if(i<p.before.quality.size()&&!sameKey(row,p.before.quality[i]))return "Initial-quality row identity changed in preview.";
            const auto *s=species(p.before,row.species);
            if(!p.before.qualityFile.isEmpty()||row.scope!=2||row.cell<0||row.cell>=p.before.cellCount||p.before.activeCells.size()!=p.before.cellCount||!p.before.activeCells[row.cell]
                ||(row.zone!=0&&row.zone!=1)||row.layer!=-1||!s||s->nativeConcUnits.isEmpty()||!std::isfinite(row.value)||(!s->signedConcentration&&row.value<0))return "Invalid initial-quality cell, zone, species or value in preview.";
        }
    }else if(p.target==GroundwaterTransportTarget::Source){
        if(p.after.quality!=p.before.quality||p.after.sources.size()<p.before.sources.size())return "Invalid source preview.";
        QSet<QString> names;
        for(int i=0;i<p.before.sources.size();++i){if(p.before.sources[i]!=p.after.sources[i])return "Existing sources cannot be replaced by an assignment.";names.insert(p.before.sources[i].name);}
        const int first=p.before.sources.size();
        if(first<p.after.sources.size()){const auto error=sourceError(p.before,p.after.sources[first]);if(!error.isEmpty())return error;}
        for(int i=first;i<p.after.sources.size();++i){const auto &row=p.after.sources[i];const auto &common=p.after.sources[first];
            if(row.scope!=2||row.cell<0||row.cell>=p.before.cellCount||p.before.activeCells.size()!=p.before.cellCount||!p.before.activeCells[row.cell]
                ||row.name.isEmpty()||names.contains(row.name)||!std::isfinite(row.flow)||!std::isfinite(row.scale)||row.scale<0
                ||row.series!=common.series||row.terms!=common.terms)return "Invalid or inconsistent cell sources in preview.";
            names.insert(row.name);
        }
    }else return "Unknown transport target.";
    return {};
}
}
GroundwaterTransportResult applyGroundwaterTransport(SWMM_Engine e,const GroundwaterTransportPreview &p,std::function<bool(int)> hook)
{
    const QString error=previewError(p);if(!error.isEmpty())return {false,false,true,error};
    return commit(e,p.before,p.after,p.target,hook);
}
GroundwaterTransportResult undoGroundwaterTransport(SWMM_Engine e,const GroundwaterTransportPreview &p)
{
    const QString error=previewError(p);if(!error.isEmpty())return {false,false,true,error};
    return commit(e,p.after,p.before,p.target,{});
}
}
