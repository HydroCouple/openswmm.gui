#include "ui/dialogs/heatrasterassigndialog.h"
#include "core/displaynumber.h"
#include "assignment/heatassignment.h"
#include "layers/gisrasterlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDateTime>
#include <algorithm>

namespace OpenSWMMVis {
HeatRasterAssignDialog::HeatRasterAssignDialog(SWMMModelLayer* model,MapCanvas* canvas,QWidget* parent):QDialog(parent) {
    setWindowTitle(tr("Assign heat properties from raster"));setObjectName("heatRasterAssignDialog");resize(850,650);
    auto* layout=new QVBoxLayout(this);auto* note=new QLabel(tr("Nodes are sampled at their coordinates, conduits along their full length, and 2D cells over their raster footprint. Continuous cell properties use an area-weighted mean. Values use SI units / °C; missing or invalid samples leave existing settings unchanged."),this);note->setWordWrap(true);layout->addWidget(note);
    auto* form=new QFormLayout;auto* source=new QComboBox(this);source->setObjectName("heatRasterSource");
    if(canvas)for(auto* layer:canvas->layers())if(auto* raster=qobject_cast<GISRasterLayer*>(layer))source->addItem(raster->name(),raster->layerId());
    form->addRow(tr("Loaded raster:"),source);auto* band=new QSpinBox(this);band->setObjectName("heatRasterBand");band->setRange(1,999);form->addRow(tr("Band:"),band);
    auto* property=new QComboBox(this);property->setObjectName("heatRasterProperty");for(int a=0;a<=SWMM_HEAT_ATTR_MESH_POROSITY;++a)property->addItem(heatPropertyLabel(a),a);form->addRow(tr("Property:"),property);
    auto* scope=new QComboBox(this);scope->setObjectName("heatRasterScope");scope->addItem(tr("Nodes"),SWMM_HEAT_SCOPE_NODE);scope->addItem(tr("Conduits"),SWMM_HEAT_SCOPE_LINK);scope->addItem(tr("2D mesh cells"),SWMM_HEAT_SCOPE_CELL2D);form->addRow(tr("Objects:"),scope);
    auto* tagLabel=new QLabel(tr("Tag filter:"),this);auto* tag=new QLineEdit(this);tag->setObjectName("heatRasterTag");tag->setPlaceholderText(tr("All tags"));form->addRow(tagLabel,tag);
    auto* samples=new QSpinBox(this);samples->setRange(1,1000);samples->setValue(20);form->addRow(tr("Samples per conduit:"),samples);layout->addLayout(form);
    auto* table=new QTableWidget(0,4,this);table->setHorizontalHeaderLabels({tr("Object"),tr("Before override"),tr("After override"),tr("Source")});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table,1);
    auto* status=new QLabel(this);status->setWordWrap(true);layout->addWidget(status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close,this);auto* preview=buttons->addButton(tr("Preview"),QDialogButtonBox::ActionRole);preview->setObjectName("heatRasterPreview");auto* apply=buttons->button(QDialogButtonBox::Apply);apply->setObjectName("heatRasterApply");apply->setEnabled(false);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    struct State { HeatRasterPreview preview;QString signature,crs,source;quint64 geometry=0;QDateTime modified; qint64 size=0; };
    auto state=std::make_shared<State>();QPointer<SWMMModelLayer> owner(model);QPointer<MapCanvas> map(canvas);
    auto invalidate=[apply,table,status]{apply->setEnabled(false);table->setRowCount(0);status->setText(tr("Preview the current choices before applying values."));};
    for(auto* combo:{source,property,scope})connect(combo,&QComboBox::currentIndexChanged,this,invalidate);
    connect(band,&QSpinBox::valueChanged,this,invalidate);connect(samples,&QSpinBox::valueChanged,this,invalidate);connect(tag,&QLineEdit::textChanged,this,invalidate);
    if(model){connect(model,&SWMMModelLayer::engineAboutToClose,this,invalidate);connect(model,&SWMMModelLayer::geometryChanged,this,invalidate);connect(model,&SWMMModelLayer::optionsChanged,this,invalidate);}
    auto updateBand=[=]{if(!map)return;for(auto* layer:map->layers())if(layer->layerId()==source->currentData().toString())if(auto* raster=qobject_cast<GISRasterLayer*>(layer))band->setMaximum(std::max(1,raster->bandCount()));};
    connect(source,&QComboBox::currentIndexChanged,this,updateBand);updateBand();samples->setEnabled(false);
    const auto updateScope=[property,scope,samples,tagLabel]{
        const int attr=property->currentData().toInt();auto* items=static_cast<QStandardItemModel*>(scope->model());
        const bool meshPorosity=attr==SWMM_HEAT_ATTR_MESH_POROSITY;
        const bool bedProperty=attr>=SWMM_HEAT_ATTR_THERMAL_DIFFUSIVITY&&attr<SWMM_HEAT_ATTR_MESH_POROSITY;
        items->item(0)->setEnabled(!bedProperty&&!meshPorosity);
        items->item(1)->setEnabled(!meshPorosity);
        items->item(2)->setEnabled(true);
        if(meshPorosity)scope->setCurrentIndex(2);
        else if(bedProperty&&scope->currentData().toInt()==SWMM_HEAT_SCOPE_NODE)scope->setCurrentIndex(1);
        samples->setEnabled(scope->currentData().toInt()==SWMM_HEAT_SCOPE_LINK);
        tagLabel->setText(scope->currentData().toInt()==SWMM_HEAT_SCOPE_CELL2D?QObject::tr("Mesh region tag:"):QObject::tr("Network tag:"));
    };
    connect(scope,&QComboBox::currentIndexChanged,this,[scope,samples,tagLabel]{samples->setEnabled(scope->currentData().toInt()==SWMM_HEAT_SCOPE_LINK);tagLabel->setText(scope->currentData().toInt()==SWMM_HEAT_SCOPE_CELL2D?QObject::tr("Mesh region tag:"):QObject::tr("Network tag:"));});
    connect(property,&QComboBox::currentIndexChanged,this,updateScope);updateScope();
    connect(preview,&QPushButton::clicked,this,[=]{invalidate();if(!owner||!map)return;GISRasterLayer* raster=nullptr;for(auto* layer:map->layers())if(layer->layerId()==source->currentData().toString())raster=qobject_cast<GISRasterLayer*>(layer);if(!raster){status->setText(tr("Load a raster layer into the map first."));return;}
        const auto engine=owner->engine();state->signature=HeatConfigDialog::configurationSignature(engine);state->geometry=owner->geomRevision();state->crs=owner->srs()?owner->srs()->toWkt():QString();
        QPointer<GISRasterLayer> sourceGuard(raster);
        QProgressDialog progress(tr("Sampling heat properties…"),tr("Cancel"),0,100,this);progress.setWindowModality(Qt::WindowModal);
        state->preview=sampleHeatRaster(owner,raster,band->value(),property->currentData().toInt(),scope->currentData().toInt(),tag->text().trimmed(),samples->value(),[&](int done,int total){if(done%20==0){progress.setValue(total?done*100/total:0);QApplication::processEvents();}return !progress.wasCanceled()&&owner&&sourceGuard&&owner->engine()==engine;},[&]{return progress.wasCanceled()||!owner||!sourceGuard||owner->engine()!=engine||owner->geomRevision()!=state->geometry||(owner->srs()?owner->srs()->toWkt():QString())!=state->crs;});
        progress.setValue(100);if(!state->preview.error.isEmpty()){status->setText(state->preview.error);return;}
        const int sampledCount=state->preview.edits.size();
        auto& edits=state->preview.edits;edits.erase(std::remove_if(edits.begin(),edits.end(),[engine](const HeatPropertyEdit& edit){const auto old=authoredHeatProperty(engine,edit.attr,edit.scope,edit.name);return old.isValid()&&old.toDouble()==edit.value.toDouble();}),edits.end());
        state->source=raster->filePath();const QFileInfo file(state->source);state->modified=file.lastModified();state->size=file.size();
        for(const auto& edit:state->preview.edits){if(table->rowCount()>=500)break;const int row=table->rowCount();table->insertRow(row);const auto old=authoredHeatProperty(engine,edit.attr,edit.scope,edit.name);
            const QStringList values={edit.name,old.isValid()?QString::number(old.toDouble(),'g',12):tr("Inherited"),QString::number(edit.value.toDouble(),'g',12),tr("%1 · band %2").arg(raster->name()).arg(band->value())};for(int c=0;c<4;++c)table->setItem(row,c,new QTableWidgetItem(values[c]));table->item(row,1)->setToolTip(old.isValid()?openswmmvis::displayNumber(old.toDouble()):tr("Inherited"));table->item(row,2)->setToolTip(openswmmvis::displayNumber(edit.value.toDouble()));}
        status->setText(tr("%1 valid values; %2 will change. Skipped: %3 NoData / outside; %4 missing geometry; %5 outside the valid range. First 500 changes shown. Apply creates one Undo operation.").arg(sampledCount).arg(state->preview.edits.size()).arg(state->preview.skippedNoData).arg(state->preview.skippedGeometry).arg(state->preview.skippedRange));apply->setEnabled(!state->preview.edits.isEmpty());
    });
    connect(apply,&QPushButton::clicked,this,[=]{if(!owner||!map||!apply->isEnabled())return;const QFileInfo file(state->source);
        if(owner->geomRevision()!=state->geometry||(owner->srs()?owner->srs()->toWkt():QString())!=state->crs||HeatConfigDialog::configurationSignature(owner->engine())!=state->signature||file.lastModified()!=state->modified||file.size()!=state->size){invalidate();status->setText(tr("The model or source changed. Preview again."));return;}
        QString error;if(!applyHeatPropertyEdits(owner,state->preview.edits,map->undoStack(),tr("Assign heat from %1 · band %2").arg(state->source).arg(band->value()),&error)){status->setText(error);return;}apply->setEnabled(false);status->setText(tr("Heat properties applied. Undo restores the exact previous configuration."));
    });
}
}
