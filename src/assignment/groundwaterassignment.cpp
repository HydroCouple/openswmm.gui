#include "assignment/groundwaterassignment.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <QSet>
#include <QHash>
#include <cmath>
#include <algorithm>
namespace openswmmvis::assignment {
namespace {
const QStringList properties={"PSI_B","LAMBDA","N","L","C_LOSS","HG0","SOIL_CHAR","CLOSURE","M_LAYERS"};
const QStringList options={"SOIL_CHAR","CLOSURE","M_LAYERS","CAPILLARY_DIFF","C_GW","C_COL","FORCE_CLOSED_FORM","MODE","DUNNE","GW_ET","NODE_ENROLMENT","LINK_SEEPAGE"};
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
}
