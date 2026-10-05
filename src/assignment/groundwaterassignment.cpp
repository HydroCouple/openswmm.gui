#include "assignment/groundwaterassignment.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_2d.h>
#include <cstring>
#include <QSet>
#include <QHash>
#include <cmath>
#include <algorithm>
namespace openswmmvis::assignment {
namespace {
const QStringList properties={"PSI_B","LAMBDA","N","L","C_LOSS","HG0","SOIL_CHAR","CLOSURE","M_LAYERS"};
const QStringList options={"SOIL_CHAR","CLOSURE","M_LAYERS","CAPILLARY_DIFF","C_GW","C_COL","FORCE_CLOSED_FORM","MODE","DUNNE","GW_ET","NODE_ENROLMENT","LINK_SEEPAGE","WILTING_SUCTION","OPTIONS_AUTHORED","CONFIGURED"};
bool fail(QString *error,const QString &text){if(error)*error=text;return false;}
bool editable(SWMM_Engine e){int state=0;return e&&swmm_engine_get_state(e,&state)==SWMM_OK&&(state==SWMM_STATE_BUILDING||state==SWMM_STATE_OPENED);}
double value(const AquiferRow&r,const QString&key){
    if(key=="KS")return r.ks;if(key=="ZS")return r.zs;if(key=="THETA_S")return r.thetaS;
    if(key=="THETA_R")return r.thetaR;if(key=="ALPHA")return r.alpha;
    return r.optional.value(key,std::numeric_limits<double>::quiet_NaN());
}
void assign(AquiferRow&r,const QString&key,double v){
    if(key=="KS")r.ks=v;else if(key=="ZS")r.zs=v;else if(key=="THETA_S")r.thetaS=v;
    else if(key=="THETA_R")r.thetaR=v;else if(key=="ALPHA")r.alpha=v;else r.optional[key]=v;
}
QString validate(const AquiferRow&r){
    for(double v:{r.ks,r.zs,r.thetaS,r.thetaR,r.alpha})if(!std::isfinite(v))return "Aquifer values must be finite.";
    for(double v:r.optional)if(!std::isfinite(v))return "Aquifer optional values must be finite.";
    if(!(r.ks>0&&r.zs>0&&r.alpha>0))return "Conductivity, thickness and alpha must be positive.";
    if(!(r.thetaR>=0&&r.thetaR<r.thetaS&&r.thetaS<=1))return "Residual water content must be less than porosity, and porosity must not exceed 1.";
    if(r.optional.value("PSI_B")<0||r.optional.value("LAMBDA")<=0||r.optional.value("N")<=1||r.optional.value("C_LOSS")<0)
        return "Invalid soil characteristic or deep-loss property.";
    if(r.optional.value("HG0",-1)>r.zs)return "Initial saturated depth exceeds aquifer thickness.";
    return {};
}
bool writeRow(SWMM_Engine e,const AquiferRow&r,int index,int &write,const std::function<bool(int)>&hook){
    auto allowed=[&]{return !hook||hook(++write);};
    const auto tag=r.tag.toUtf8();
    if(!allowed()||swmm_gw2d_row_add(e,r.scope,tag.constData(),r.cell,r.ks,r.zs,r.thetaS,r.thetaR,r.alpha)!=SWMM_OK)return false;
    for(const QString &key:properties){
        const double v=r.optional.value(key);
        // These sentinels represent inheritance. Existing rows are untouched;
        // new rows keep inheritance whenever its value is unambiguous.
        if((key=="M_LAYERS"&&v<0)||(key=="SOIL_CHAR"&&r.optional.value("SOIL_CHAR_SET")==0)||(key=="CLOSURE"&&r.optional.value("CLOSURE_SET")==0))continue;
        if(!allowed()||swmm_gw2d_row_set_property(e,index,key.toUtf8().constData(),v)!=SWMM_OK)return false;
    }
    return true;
}
bool removeTail(SWMM_Engine e,int count){
    int now=0;if(swmm_gw2d_row_count(e,&now)!=SWMM_OK)return false;
    for(int i=now-1;i>=count;--i)if(swmm_gw2d_row_remove(e,i)!=SWMM_OK)return false;
    return true;
}
}
QVector<AquiferTarget> aquiferTargets(){
    const double max=std::numeric_limits<double>::max();
    return {{"KS","Saturated conductivity","rate",0,max,true},
      {"ZS","Aquifer thickness","length",0,max,true},
      {"THETA_S","Porosity","fraction",0,1,true},
      {"THETA_R","Residual water content","fraction",0,1,false},
      {"ALPHA","Sorptive number","inverse-length",0,max,true},
      {"HG0","Initial saturated depth","length",0,max,false},
      {"C_LOSS","Deep-loss coefficient","rate",0,max,false},
      {"PSI_B","Air-entry head","length",0,max,false},
      {"LAMBDA","Brooks–Corey shape","dimensionless",0,max,true},
      {"N","Van Genuchten n","dimensionless",1,max,true},
      {"L","Mualem exponent","dimensionless",-max,max,false},
      {"FLOW","Injection / extraction flow","m3/s",-max,max,false,false,
       "Operational groundwater sources require verified engine water/species ledger support. Aquifer properties and initial state are available."}};
}
bool readAquiferSnapshot(SWMM_Engine e,AquiferSnapshot*out,QString *error){
    if(error)error->clear();if(!e||!out)return fail(error,"No groundwater model is available.");
    AquiferSnapshot result;int count=0;
    if(swmm_gw2d_row_count(e,&count)!=SWMM_OK||count<0)return fail(error,"Cannot read aquifer rows.");
    for(int i=0;i<count;++i){
        AquiferRow r;char tag[4096]{};
        if(swmm_gw2d_row_get(e,i,&r.scope,tag,sizeof tag,&r.cell,&r.ks,&r.zs,&r.thetaS,&r.thetaR,&r.alpha)!=SWMM_OK)
            return fail(error,"Cannot read aquifer row "+QString::number(i+1)+".");
        r.tag=QString::fromUtf8(tag);
        for(const QString&key:properties){double v=0;
            if(swmm_gw2d_row_get_property(e,i,key.toUtf8().constData(),&v)!=SWMM_OK)return fail(error,"Cannot read aquifer property "+key+".");
            r.optional[key]=v;
        }
        for(const auto&key:{"SOIL_CHAR_SET","CLOSURE_SET"}){double presence=0;
            if(swmm_gw2d_row_get_property(e,i,key,&presence)!=SWMM_OK)return fail(error,"This engine cannot preserve aquifer inheritance; update the engine before assigning properties.");
            r.optional[QString::fromLatin1(key)]=presence;
        }
        result.rows.append(r);
    }
    for(const QString&key:options){char buffer[4096]{};
        if(swmm_gw2d_option_get(e,key.toUtf8().constData(),buffer,sizeof buffer)!=SWMM_OK)return fail(error,"Cannot read groundwater option "+key+".");
        result.options[key]=QString::fromUtf8(buffer);
    }
    char projectUnits[80]{};if(swmm_options_get(e,"FLOW_UNITS",projectUnits,sizeof projectUnits)!=SWMM_OK)return fail(error,"Cannot read project units.");
    result.options["PROJECT_FLOW_UNITS"]=QString::fromUtf8(projectUnits);
    result.options["ET_FORCING_REVIEW"]=groundwaterEtForcingStatus(e);
    *out=std::move(result);return true;
}
AquiferPreview previewAquiferAssignment(const AquiferRequest&r){
    AquiferPreview p;p.before=r.before;p.target=r.target;
    auto reject=[&](const QString&message){p.appended.clear();p.cells.clear();p.oldValues.clear();p.newValues.clear();p.error=message;return p;};
    AquiferTarget target;bool found=false;
    for(const auto&t:aquiferTargets())if(t.key==r.target){target=t;found=true;break;}
    if(!found)return reject("Unknown aquifer target.");
    if(!target.supported)return reject(target.unavailableReason);
    if(r.cells.isEmpty()||r.cells.size()!=r.values.size()||r.cells.size()!=r.cellTags.size())return reject("Select cells and provide one value and tag per cell.");
    const AquiferRow *global=nullptr;QHash<QString,const AquiferRow*> tags;QHash<int,const AquiferRow*> cells;
    for(const auto&row:r.before.rows){if(row.scope==0)global=&row;else if(row.scope==1)tags.insert(row.tag,&row);else if(row.scope==2)cells.insert(row.cell,&row);}
    QSet<int> seen;
    for(int i=0;i<r.cells.size();++i){
        const int cell=r.cells[i];const double v=r.values[i];
        if(cell<0||seen.contains(cell))return reject("Invalid or duplicate target cell.");seen.insert(cell);
        if(std::isnan(v)&&r.skipNoData){p.skippedCells.append(cell);continue;}
        if(!std::isfinite(v))return reject(QString("Cell %1 has no finite source value; choose an explicit NoData policy.").arg(cell+1));
        if(v<target.minimum||v>target.maximum||(target.minimumExclusive&&v==target.minimum))return reject(QString("Cell %1 has an out-of-range %2 value.").arg(cell+1).arg(target.label));
        const AquiferRow *effective=cells.value(cell,tags.value(r.cellTags[i],global));
        if(!effective)return reject(QString("Cell %1 has no authored aquifer properties. Define an aquifer row first.").arg(cell+1));
        const double old=value(*effective,r.target);
        if(old==v)continue;
        AquiferRow row=*effective;row.scope=2;row.cell=cell;row.tag.clear();assign(row,r.target,v);
        const QString error=validate(row);if(!error.isEmpty())return reject(QString("Cell %1: %2").arg(cell+1).arg(error));
        p.appended.append(row);p.cells.append(cell);p.oldValues.append(old);p.newValues.append(v);
    }
    return p;
}
bool applyAquiferPreview(SWMM_Engine e,const AquiferPreview&p,QString *error,std::function<bool(int)>hook){
    if(error)error->clear();if(!p.error.isEmpty())return fail(error,p.error);
    if(!editable(e))return fail(error,"Groundwater inputs are editable only before a run starts.");
    AquiferSnapshot now;if(!readAquiferSnapshot(e,&now,error))return false;
    if(now!=p.before)return fail(error,"Aquifer data changed since preview. Preview again before applying.");
    for(const auto&r:p.appended){const auto err=validate(r);if(r.scope!=2||r.cell<0||!err.isEmpty())return fail(error,err.isEmpty()?"Invalid cell override.":err);}
    if(p.appended.size()>std::numeric_limits<int>::max()-p.before.rows.size())return fail(error,"Too many aquifer rows.");
    int writes=0;
    for(int i=0;i<p.appended.size();++i)if(!writeRow(e,p.appended[i],int(p.before.rows.size())+i,writes,hook)){
        const bool restored=removeTail(e,int(p.before.rows.size()));
        return fail(error,restored?"The engine refused an assignment. All appended rows were rolled back.":"The engine refused an assignment and rollback failed. Stop editing and reload the last saved model.");
    }
    return true;
}
bool undoAquiferPreview(SWMM_Engine e,const AquiferPreview&p,QString *error){
    if(error)error->clear();if(!editable(e))return fail(error,"Reset the run before undoing groundwater inputs.");
    AquiferSnapshot now;if(!readAquiferSnapshot(e,&now,error))return false;
    auto expected=p.before;expected.rows+=p.appended;
    if(now!=expected)return fail(error,"Aquifer data changed after this assignment. Undo was refused to preserve the newer edits.");
    if(!removeTail(e,int(p.before.rows.size())))return fail(error,"Unable to remove the assignment rows. Stop editing and reload the last saved model.");
    return true;
}

namespace {
bool ownershipSnapshot(SWMM_Engine e,InfiltrationOwnershipSnapshot* out,QString* error) {
 int count=0;if(swmm_infil2d_get_authored_rows(e,nullptr,0,&count)!=SWMM_OK)return fail(error,"Cannot read authored infiltration rows.");
 out->rows.resize(count);if(count&&swmm_infil2d_get_authored_rows(e,out->rows.data(),count,&count)!=SWMM_OK)return fail(error,"Cannot snapshot infiltration rows.");
 char value[4096]{};if(swmm_options_get_ext(e,"INFIL_DESTINATION",value,sizeof value)!=SWMM_OK)return fail(error,"Cannot read infiltration destination.");
 out->destination=QString::fromUtf8(value);return true;
}
bool sameOwnership(const InfiltrationOwnershipSnapshot& a,const InfiltrationOwnershipSnapshot& b) {
 if(a.destination!=b.destination||a.rows.size()!=b.rows.size())return false;
 for(int i=0;i<a.rows.size();++i){const auto& x=a.rows[i];const auto& y=b.rows[i];
  if(x.cell!=y.cell||std::strcmp(x.tag,y.tag)||x.dest_explicit!=y.dest_explicit||x.row.has_method!=y.row.has_method||x.row.method!=y.row.method||x.row.dest!=y.row.dest)return false;
  for(int k=0;k<5;++k)if(x.row.p[k]!=y.row.p[k])return false;
 }return true;
}
}
InfiltrationOwnershipPreview previewInfiltrationOwnership(SWMM_Engine e,const QStringList& tags,bool removeExplicit) {
 InfiltrationOwnershipPreview p;if(!ownershipSnapshot(e,&p.before,&p.error))return p;p.after=p.before;
 int count=0;if(swmm_2d_triangle_count(e,&count)!=SWMM_OK||count!=tags.size()){p.error="Mesh and model cell counts differ.";return p;}
 for(int cell=0;cell<count;++cell){char tag[4096]{};
  if(swmm_2d_get_triangle_tag(e,cell,tag,sizeof tag)!=SWMM_OK||QString::fromUtf8(tag)!=tags[cell]){p.error="Save pending mesh tag edits before reviewing ownership.";return p;}
 }
 QVector<int> owners(count),sources(count),conflicts(count);
 if(count&&swmm_infil2d_get_ownership_bulk(e,owners.data(),sources.data(),conflicts.data(),count,nullptr)!=SWMM_OK){p.error="Ownership is unavailable for this mesh.";return p;}
 for(int cell=0;cell<count;++cell){const int owner=owners[cell],source=sources[cell],conflict=conflicts[cell];
  InfiltrationOwnershipCell c;c.cell=cell;c.owner=owner;c.conflict=conflict;
  c.source=owner==2?(source<0?QString("Implicit whole-mesh aquifer default"):QString("Aquifer row %1").arg(source+1)):owner==0?QString("Infiltration process OFF"):QString("Surface infiltration bank");
  c.reason=owner==2?QString("Capacity is computed by the aquifer."):owner==0?QString("No water transfer."):QString("Ordinary method applies.");
  if(conflict==1)c.reason=removeExplicit?QString("Remove conflicting surface cell override; aquifer computes capacity."):QString("Explicit surface cell override conflicts; select removal before Apply.");
  if(conflict==2)c.reason=QString("Migrate obsolete AQUIFER_2D destination to LOST; coverage determines the infiltration owner.");
  p.cells.append(c);
 }
 p.after.rows.clear();
 for(auto row:p.before.rows){
  if(row.cell>=0&&row.row.has_method&&p.cells[row.cell].owner==2){
   if(!removeExplicit){p.error="Explicit surface overrides conflict with aquifer ownership. Review and select their removal before Apply.";}
   else {
    static const char* methods[]={"HORTON","MOD_HORTON","GREEN_AMPT","MOD_GREEN_AMPT","CURVE_NUM","CONSTANT"};
    static const int counts[]={5,5,3,3,3,1};QStringList parameters;
    for(int k=0;k<counts[row.row.method];++k)parameters.append(QString::number(row.row.p[k],'g',17));
    p.cells[row.cell].reason=QString("Remove %1 surface override (parameters %2, in project units); aquifer computes capacity.").arg(methods[row.row.method],parameters.join(", "));continue;
   }
  }
  if(row.row.dest==SWMM_INFIL2D_DEST_AQUIFER_2D)row.row.dest=SWMM_INFIL2D_DEST_LOST;
  p.after.rows.append(row);
  if(row.cell<0){const QString tag=QString::fromUtf8(row.tag);int applied=0,skipped=0;
   for(int cell=0;cell<count;++cell)if(tag=="*"||tags[cell]==tag){if(p.cells[cell].owner==2)++skipped;else ++applied;}
   p.summaries.append(QString("Default %1: %2 applied, %3 skipped (aquifer-owned)").arg(tag).arg(applied).arg(skipped));
  }
 }
 if(p.after.destination=="AQUIFER_2D")p.after.destination="LOST";
 p.changed=!sameOwnership(p.before,p.after);return p;
}
bool applyInfiltrationOwnership(SWMM_Engine e,const InfiltrationOwnershipSnapshot& expected,const InfiltrationOwnershipSnapshot& desired,QString* error) {
 if(!editable(e))return fail(error,"Reset the simulation before changing infiltration ownership.");
 InfiltrationOwnershipSnapshot now;if(!ownershipSnapshot(e,&now,error))return false;
 if(!sameOwnership(now,expected))return fail(error,"Infiltration configuration changed since preview. Preview again.");
 if(swmm_options_set_ext(e,"INFIL_DESTINATION",desired.destination.toUtf8().constData())!=SWMM_OK)return fail(error,"Cannot update the reviewed destination.");
 if(swmm_infil2d_replace_authored_rows(e,desired.rows.constData(),desired.rows.size())!=SWMM_OK){
  swmm_options_set_ext(e,"INFIL_DESTINATION",expected.destination.toUtf8().constData());return fail(error,"Cannot apply reviewed rows; the destination was restored.");
 }return true;
}
}

namespace openswmmvis::assignment {
QString groundwaterEtForcingStatus(SWMM_Engine e,const QString& meshMode){
 char token[80]{};swmm_options_get_ext(e,"EVAPORATION",token,sizeof token);const QString mode=meshMode.isEmpty()?QString::fromUtf8(token):meshMode;
 if(mode=="NO")return "Mesh evaporation is Off: atmospheric demand is zero, including prescribed rates.";
 if(mode!="CLIMATE")return "Mesh evaporation is Forced only: prescribe mesh evaporation rates to generate atmospheric demand.";
 int type=0;if(swmm_climate_get_evap_type(e,&type)!=SWMM_OK)return "Climate evaporation configuration is unavailable.";
 if(type==SWMM_EVAP_CONSTANT||type==SWMM_EVAP_MONTHLY){double rates[12]{};if(swmm_climate_get_evap_monthly(e,rates,12)!=SWMM_OK)return "Climate evaporation rates are unavailable.";
  const double maximum=*std::max_element(rates,rates+12);return maximum>0?QString("Project climate evaporation is configured (maximum authored rate %1 in project evaporation units).").arg(maximum,0,'g',12):QString("No positive climate evaporation is authored: demand is zero unless mesh rates are prescribed.");}
 if(type==SWMM_EVAP_TIMESERIES){char series[4096]{};swmm_climate_get_evap_timeseries(e,series,sizeof series);return *series?QString("Climate evaporation uses time series %1; verify its coverage and rates.").arg(QString::fromUtf8(series)):QString("No climate evaporation time series is assigned.");}
 return type==SWMM_EVAP_TEMPERATURE?QString("Climate evaporation derives from temperature; verify temperature forcing coverage."):QString("Climate evaporation uses a climate file; verify file coverage and rates.");
}
AquiferProcessPreview previewAquiferProcesses(SWMM_Engine e,const QString& et,const QString& link,const QString& wilting){
 AquiferProcessPreview p;if(!readAquiferSnapshot(e,&p.before,&p.error))return p;
 char mode[80]{};if(swmm_options_get_ext(e,"GROUNDWATER",mode,sizeof mode)!=SWMM_OK){p.error="Cannot read groundwater enable.";return p;}p.groundwater=mode;p.forcing=groundwaterEtForcingStatus(e);
 if(!QStringList{"AUTO","NONE","BOUNDARY_ET","CAPILLARY_RISE","BOTH"}.contains(et)||!QStringList{"DEFAULT","ONE_WAY","NONE","TWO_WAY"}.contains(link)){p.error="Invalid groundwater process choice.";return p;}
 bool ok=false;const double v=wilting.toDouble(&ok);if(wilting!="AUTO"&&(!ok||!std::isfinite(v)||v<=0)){p.error="Wilting suction must be Automatic or a positive finite project length.";return p;}
 p.after=p.before;p.after.options["GW_ET"]=et;p.after.options["LINK_SEEPAGE"]=link;p.after.options["WILTING_SUCTION"]=wilting;
 if(wilting!="AUTO"&&p.before.options.value("WILTING_SUCTION")!="AUTO"&&std::abs(v-p.before.options.value("WILTING_SUCTION").toDouble())<=5e-10)p.after.options["WILTING_SUCTION"]=p.before.options.value("WILTING_SUCTION");
 p.changed=p.after.options!=p.before.options;
 if(p.changed){p.after.options["OPTIONS_AUTHORED"]="YES";p.after.options["CONFIGURED"]="YES";}
 const auto describe=[&](const AquiferSnapshot& s){const bool enabled=p.groundwater!="NO"&&s.options.value("CONFIGURED")=="YES";const bool mesh=s.options.value("MODE")!="PER_SUBCATCH";const QString et=s.options.value("GW_ET"),link=s.options.value("LINK_SEEPAGE"),wilting=s.options.value("WILTING_SUCTION");
  return QString("ET %1 (%2); seepage %3 (%4); wilting %5").arg(et,enabled?(et=="AUTO"?(mesh?"BOTH":"NONE"):et):"inactive",link,enabled?(link=="DEFAULT"?(mesh?"TWO_WAY":"ONE_WAY"):link):"inactive",wilting=="AUTO"?"Automatic: 150 m":wilting+(QStringList{"CFS","GPM","MGD"}.contains(s.options.value("PROJECT_FLOW_UNITS"))?" ft":" m"));};
 p.beforeEffective=describe(p.before);p.afterEffective=describe(p.after);return p;
}
bool applyAquiferProcesses(SWMM_Engine e,const AquiferSnapshot& expected,const AquiferSnapshot& desired,const QString& groundwater,QString* error){
 AquiferSnapshot now;char mode[80]{};if(!readAquiferSnapshot(e,&now,error)||swmm_options_get_ext(e,"GROUNDWATER",mode,sizeof mode)!=SWMM_OK)return false;
 if(now!=expected||QString::fromUtf8(mode)!=groundwater)return fail(error,"Groundwater inputs changed since review. Preview again.");
 const auto& o=desired.options;
 if(swmm_gw2d_process_options_set(e,o.value("GW_ET").toUtf8().constData(),o.value("LINK_SEEPAGE").toUtf8().constData(),o.value("WILTING_SUCTION").toUtf8().constData(),o.value("OPTIONS_AUTHORED")=="YES")!=SWMM_OK)return fail(error,"The engine refused the process edit; no values changed.");
 return true;
}
}
