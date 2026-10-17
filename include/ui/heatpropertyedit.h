#pragma once
#include "ui/heatproperties.h"
#include "ui/heatconfigurationundo.h"
#include "ui/hydrothermalconfiguration.h"
#include "ui/dialogs/heatconfigdialog.h"
#include "layers/swmmmodellayer.h"
#include <QUndoStack>
#include <QMessageBox>
#include <QVariant>
#include <QVector>

namespace OpenSWMMVis {
struct HeatPropertyEdit { int attr; int scope; QString name; QVariant value; };
inline QVariant authoredHeatProperty(SWMM_Engine engine,int attr,int scope,const QString& name) {
    int count=0;if(swmm_heat_element_override_count(engine,&count)!=SWMM_OK)return {};
    for(int i=0;i<count;++i){int a=-1,s=0;double value=0;char id[1024]{};
        if(swmm_heat_get_element_override(engine,i,&a,&s,id,sizeof id,&value)==SWMM_OK
            &&a==attr&&s==scope&&QString::fromUtf8(id)==name)return value;}
    return {};
}
// The raster route, attribute table and feature import share the engine row
// store, persistence registration and exact undo contract.
inline bool applyHeatPropertyEdits(SWMMModelLayer* layer,const QVector<HeatPropertyEdit>& edits,
    QUndoStack* stack,const QString& description,QString* error) {
    if(!layer||!layer->engine()){if(error)*error=QObject::tr("The model was closed.");return false;}
    const auto engine=layer->engine();QVector<HeatPropertyEdit> changed;
    for(const auto& edit:edits){
        const QString text=edit.value.toString().trimmed();bool ok=false;const double value=text.toDouble(&ok);
        if(!text.isEmpty()&&(!ok||!heatPropertyValueValid(edit.attr,value))){if(error)*error=QObject::tr("Invalid %1 for %2.").arg(heatPropertyLabel(edit.attr),edit.name);return false;}
        const auto before=authoredHeatProperty(engine,edit.attr,edit.scope,edit.name);
        if((text.isEmpty()&&!before.isValid())||(!text.isEmpty()&&before.isValid()&&before.toDouble()==value))continue;
        changed.append(edit);
    }
    if(changed.isEmpty())return true;
    HeatSnapshot before(swmm_heat_snapshot_create(engine));
    if(!before){if(error)*error=QObject::tr("Cannot snapshot heat settings before editing.");return false;}
    const auto identity=HeatConfigDialog::configurationSignature(engine);
    struct Context { SWMM_Engine engine; const QVector<HeatPropertyEdit>* edits; } context{engine,&changed};
    const int result=swmm_hydrothermal_edit_atomic(engine,[](SWMM_Engine,void* ptr)->int {
        auto& ctx=*static_cast<Context*>(ptr);
        for(const auto& edit:*ctx.edits){const auto name=edit.name.toUtf8();const auto text=edit.value.toString().trimmed();
            if(text.isEmpty()){
                int count=0;int rc=swmm_heat_element_override_count(ctx.engine,&count);if(rc!=SWMM_OK)return rc;
                for(int i=count-1;i>=0;--i){int a=-1,s=0;char id[1024]{};rc=swmm_heat_get_element_override(ctx.engine,i,&a,&s,id,sizeof id,nullptr);if(rc!=SWMM_OK)return rc;
                    if(a==edit.attr&&s==edit.scope&&edit.name==QString::fromUtf8(id)){rc=swmm_heat_remove_element_override(ctx.engine,i);if(rc!=SWMM_OK)return rc;}}
            }else{const int rc=swmm_heat_set_element_override(ctx.engine,edit.attr,edit.scope,name.constData(),text.toDouble());if(rc!=SWMM_OK)return rc;}
        }
        return OpenSWMM::Ui::ensureHeatConfigurationFile(ctx.engine);
    },&context);
    if(result!=SWMM_OK){if(error)*error=QObject::tr("Heat properties were refused (%1). Previous settings were restored.").arg(result);return false;}
    HeatSnapshot after(swmm_heat_snapshot_create(engine));
    if(!after){swmm_heat_snapshot_restore(engine,before.get());if(error)*error=QObject::tr("Cannot record the edit. Previous settings were restored.");return false;}
    QPointer<SWMMModelLayer> model(layer);
    auto notify=[model]{if(model){model->markEdited();emit model->optionsChanged({"HEAT_PROPERTIES"});}};
    if(stack){auto* command=new HeatConfigurationUndo(layer,engine,std::move(before),std::move(after),identity,
        HeatConfigDialog::configurationSignature(engine),[model]{return model?model->engine():nullptr;},
        HeatConfigDialog::configurationSignature,notify,[](const QString& why){QMessageBox::warning(nullptr,QObject::tr("Heat properties"),why);});
        command->setText(description);stack->push(command);}
    notify();return true;
}
}
