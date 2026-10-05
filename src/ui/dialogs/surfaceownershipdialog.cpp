// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/surfaceownershipdialog.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "selection/selectionmanager.h"
#include "mesh/meshobjectref.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace openswmmvis::ui {
using namespace assignment;
namespace {
QString crs(SWMMModelLayer* model,SWMM2DMeshLayer* mesh){return (model&&model->srs()?model->srs()->toWkt():QString())+"|"+(mesh&&mesh->srs()?mesh->srs()->toWkt():QString());}
class OwnershipCommand final:public QObject,public QUndoCommand {
 QPointer<SWMMModelLayer> model;QPointer<SWMM2DMeshLayer> mesh;SWMM_Engine engine;
 QVector<int> before,after;QString beforeToken,afterToken,referenceCrs;bool first=true;
 void install(bool forward){
  QString error;const auto target=forward?after:before;const auto expected=forward?beforeToken:afterToken;
  if(!model||!mesh||model->engine()!=engine||crs(model,mesh)!=referenceCrs||!surfaceOwnershipMeshMatches(engine,mesh,&error)||
     !replaceSurfaceOwners(engine,target,expected,&error)){
   setObsolete(true);QMessageBox::warning(QApplication::activeWindow(),QObject::tr("Surface ownership"),error.isEmpty()?QObject::tr("The model or coordinate system changed. Review ownership again."):error);return;
  }
  model->markEdited();emit model->optionsChanged({"2D_SURFACE_OWNERSHIP"});mesh->refreshSurfaceOwnership();
 }
public:
 OwnershipCommand(SWMMModelLayer* m,SWMM2DMeshLayer* l,const SurfaceOwnershipReview& p,const QString& token):model(m),mesh(l),engine(m->engine()),before(p.before),after(p.after),beforeToken(p.token),afterToken(token),referenceCrs(crs(m,l)){setText(QObject::tr("Review surface weather ownership"));}
 void undo()override{install(false);}void redo()override{if(first){first=false;return;}install(true);}
};
QTableWidget* table(const QStringList& headings,const char* name,QWidget* parent){
 auto* t=new QTableWidget(0,headings.size(),parent);t->setObjectName(name);t->setHorizontalHeaderLabels(headings);t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setSelectionMode(QAbstractItemView::SingleSelection);t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);t->horizontalHeader()->setStretchLastSection(true);t->setAlternatingRowColors(true);return t;
}
void row(QTableWidget* t,int r,const QStringList& values){for(int c=0;c<values.size();++c)t->setItem(r,c,new QTableWidgetItem(values[c]));}
QString number(double value){return QString::number(value,'g',9);}
}
SurfaceOwnershipDialog::SurfaceOwnershipDialog(SWMMModelLayer* model,SWMM2DMeshLayer* mesh,MapCanvas* canvas,SelectionManager* selection,QWidget* parent)
 :QDialog(parent),m_model(model),m_mesh(mesh),m_canvas(canvas),m_selection(selection),m_engine(model?model->engine():nullptr){
 int flow=0;swmm_get_flow_units(m_engine,&flow);m_areaFactor=flow<3?43560*.3048*.3048:10000.;m_areaUnit=flow<3?tr("acres"):tr("ha");
 setWindowTitle(tr("Surface ownership review"));setWindowModality(Qt::WindowModal);resize(1120,720);
 auto* root=new QVBoxLayout(this);auto* notice=new QLabel(tr("Authoring review — runtime recharge is unavailable until the completed-interval water, quality and ET adapter is qualified. Applying records will block simulation of this model until that adapter is available or the records are removed."),this);notice->setWordWrap(true);root->addWidget(notice);
 auto* tabs=new QTabWidget(this);root->addWidget(tabs,1);auto* owner=new QWidget(tabs);auto* layout=new QVBoxLayout(owner);auto* form=new QFormLayout;
 m_scope=new QComboBox(owner);m_scope->setObjectName("surfaceOwnerScope");m_scope->addItems({tr("All subcatchments"),tr("Selected subcatchments"),tr("Tag")});form->addRow(tr("Scope"),m_scope);
 m_tag=new QLineEdit(owner);m_tag->setObjectName("surfaceOwnerTag");m_tag->setEnabled(false);form->addRow(tr("Tag"),m_tag);
 m_representation=new QComboBox(owner);m_representation->setObjectName("surfaceOwnerRepresentation");m_representation->addItems({tr("Keep existing / unreviewed"),tr("Subcatchment owns weather"),tr("Mesh conversion — unavailable")});m_representation->setCurrentIndex(1);static_cast<QStandardItemModel*>(m_representation->model())->item(2)->setEnabled(false);form->addRow(tr("Representation"),m_representation);
 m_uniform=new QCheckBox(tr("I accept uniform allocation of pervious and LID areas within each polygon"),owner);m_uniform->setObjectName("surfaceOwnerUniform");form->addRow(tr("Distribution"),m_uniform);layout->addLayout(form);
 m_objects=table({tr("Source"),tr("Stored → proposed"),tr("Model (%1)").arg(m_areaUnit),tr("Polygon (%1)").arg(m_areaUnit),tr("LID (%1)").arg(m_areaUnit),tr("Pervious (%1)").arg(m_areaUnit),tr("Impervious (%1)").arg(m_areaUnit),tr("Inside (%1)").arg(m_areaUnit),tr("Outside (%1)").arg(m_areaUnit),tr("Destination / issue")},"surfaceOwnerObjects",owner);layout->addWidget(m_objects,1);
 auto* inspect=new QHBoxLayout;auto* cells=new QPushButton(tr("Select receiver cells"),owner);cells->setObjectName("surfaceOwnerSelectReceivers");inspect->addWidget(cells);auto* edit=new QPushButton(tr("Open source in existing editor"),owner);inspect->addWidget(edit);inspect->addStretch();layout->addLayout(inspect);tabs->addTab(owner,tr("Surface ownership"));
 auto* receiving=new QWidget(tabs);auto* receivingLayout=new QVBoxLayout(receiving);m_parameters=new QLabel(receiving);m_parameters->setWordWrap(true);receivingLayout->addWidget(m_parameters);
 auto* policy=new QLabel(tr("Proposed geometric areas below are not accepted recharge. The future interval adapter must limit each donor by available water and its source-head capacity, then share physical receiver headroom after pending positive receipts. Outside areas retain their existing infiltration path; lumped groundwater takes precedence."),receiving);policy->setWordWrap(true);receivingLayout->addWidget(policy);
 m_receivers=table({tr("Engine cell"),tr("Source"),tr("Weather m²"),tr("Pervious m²"),tr("LID m²"),tr("Native LID m²"),tr("Mesh weather left m²"),tr("Ownership")},"surfaceOwnerReceivers",receiving);receivingLayout->addWidget(m_receivers,1);auto* sources=new QPushButton(tr("Select all contributors to this cell"),receiving);sources->setObjectName("surfaceOwnerSelectSources");receivingLayout->addWidget(sources);auto* groundwater=new QPushButton(tr("Assign groundwater / review soil and ET parameters"),receiving);receivingLayout->addWidget(groundwater);tabs->addTab(receiving,tr("Receiving budget"));
 auto* results=new QLabel(tr("Unavailable\n\nAccepted non-LID/LID recharge, outside infiltration, potential/actual/unused/pending ET, donor head, awards, pollutant pairing and combined continuity are not recorded by this authoring tranche.\n\nA recorded zero will be shown as 0 when runtime results are available.\n\nExchange schedule: completed intervals aligned to the mesh source stage; qualification pending. No new timestep setting is exposed here."),tabs);results->setWordWrap(true);results->setMargin(20);tabs->addTab(results,tr("Results"));
 m_status=new QLabel(tr("Choose a scope, explicitly accept the distribution, then preview. All competing footprints are validated, including sources outside the edit scope."),this);m_status->setObjectName("surfaceOwnerStatus");m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);root->addWidget(m_status);
 auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);m_preview=buttons->addButton(tr("Preview"),QDialogButtonBox::ActionRole);m_preview->setObjectName("surfaceOwnerPreview");m_apply=buttons->addButton(tr("Apply reviewed records"),QDialogButtonBox::ApplyRole);m_apply->setObjectName("surfaceOwnerApply");m_apply->setEnabled(false);root->addWidget(buttons);
 connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(m_preview,&QPushButton::clicked,this,&SurfaceOwnershipDialog::preview);connect(m_apply,&QPushButton::clicked,this,&SurfaceOwnershipDialog::apply);
 connect(m_scope,&QComboBox::currentIndexChanged,this,[this]{m_tag->setEnabled(m_scope->currentIndex()==2);invalidate();});connect(m_tag,&QLineEdit::textChanged,this,&SurfaceOwnershipDialog::invalidate);connect(m_representation,&QComboBox::currentIndexChanged,this,&SurfaceOwnershipDialog::invalidate);connect(m_uniform,&QCheckBox::toggled,this,&SurfaceOwnershipDialog::invalidate);
 connect(cells,&QPushButton::clicked,this,&SurfaceOwnershipDialog::selectReceivers);connect(sources,&QPushButton::clicked,this,&SurfaceOwnershipDialog::selectCellSources);
 connect(edit,&QPushButton::clicked,this,[this]{const int r=m_objects->currentRow();if(r>=0&&r<m_review.objects.size()){const auto id=QString::fromUtf8(m_review.objects[r].name);close();emit sourceEditorRequested(id);}});connect(groundwater,&QPushButton::clicked,this,&SurfaceOwnershipDialog::groundwaterEditorRequested);
 if(model){connect(model,&SWMMModelLayer::modelEdited,this,&SurfaceOwnershipDialog::invalidate);connect(model,&SWMMModelLayer::engineAboutToClose,this,&SurfaceOwnershipDialog::invalidate);connect(model,&SWMMModelLayer::geometryChanged,this,&SurfaceOwnershipDialog::invalidate);}
 if(mesh){connect(mesh,&SWMM2DMeshLayer::meshEditsChanged,this,&SurfaceOwnershipDialog::invalidate);}
}
void SurfaceOwnershipDialog::reject(){if(!m_busy)QDialog::reject();}
void SurfaceOwnershipDialog::invalidate(){if(m_committing)return;m_ready=false;m_apply->setEnabled(false);m_status->setText(tr("Inputs changed. Preview again before applying."));}
bool SurfaceOwnershipDialog::contextValid(QString* error) const {
 if(!m_model||!m_mesh||!m_canvas||!m_canvas->undoStack()||m_model->engine()!=m_engine){*error=tr("The model or Undo owner is no longer available.");return false;}
 if(m_model->crsAssigned()&&m_model->srs()&&m_model->srs()->isGeographic()){*error=tr("Geographic coordinates need projection to project length units before this area review.");return false;}
 if(m_model->crsAssigned()&&m_model->srs()&&m_mesh->srs()&&m_model->srs()->toWkt()!=m_mesh->srs()->toWkt()){*error=tr("Model and mesh coordinate systems differ. Reproject them to the same coordinates before reviewing.");return false;}
 return surfaceOwnershipMeshMatches(m_engine,m_mesh,error);
}
QVector<int> SurfaceOwnershipDialog::scope(const SurfaceOwnershipReview& review) const {
 QVector<int> out;for(const auto& o:review.objects){const auto name=QString::fromUtf8(o.name);
  if(m_scope->currentIndex()==0||(m_scope->currentIndex()==1&&m_selection&&m_selection->contains({SWMMObjectRef::Subcatchment,name}))||(m_scope->currentIndex()==2&&QString::fromUtf8(o.tag)==m_tag->text().trimmed()))out.append(o.subcatch);
 }return out;
}
void SurfaceOwnershipDialog::preview(){
 if(m_busy)return;QString error;m_ready=false;m_apply->setEnabled(false);if(!contextValid(&error)){m_status->setText(error);return;}
 if(m_representation->currentIndex()==1&&!m_uniform->isChecked()){m_status->setText(tr("Explicitly accept the uniform area assumption before previewing reviewed records."));return;}
 m_busy=true;m_preview->setEnabled(false);auto* tabs=findChild<QTabWidget*>();tabs->setEnabled(false);const auto finish=qScopeGuard([this,tabs]{m_busy=false;m_preview->setEnabled(true);tabs->setEnabled(true);});QProgressDialog progress(tr("Resolving surface footprints…"),tr("Cancel"),0,100,this);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(100);
 QElapsedTimer refresh;refresh.start();
 auto callback=[&](int done,int total){if(refresh.elapsed()>=50){progress.setValue(total?std::min(99,100*done/total):0);QApplication::processEvents();refresh.restart();}return !progress.wasCanceled()&&m_model&&m_model->engine()==m_engine;};
 QVector<int> current;if(!readSurfaceOwners(m_engine,&current,&error)){m_status->setText(error);return;}
 auto base=previewSurfaceOwners(m_engine,current,callback);if(base.token.isEmpty()){m_status->setText(base.error);return;}auto ids=scope(base);if(ids.isEmpty()){m_status->setText(tr("This scope has no subcatchments."));return;}
 QVector<int> proposed=current;for(int id:ids){proposed.removeAll(id);if(m_representation->currentIndex()==1)proposed.append(id);}std::sort(proposed.begin(),proposed.end());
 m_review=previewSurfaceOwners(m_engine,proposed,callback);if(m_review.token.isEmpty()){m_status->setText(m_review.error);return;}
 m_geometry=m_mesh->geomRevision();m_crs=crs(m_model,m_mesh);m_objects->setRowCount(m_review.objects.size());m_receivers->setRowCount(m_review.shares.size());
 for(int i=0;i<m_review.objects.size();++i){const auto& o=m_review.objects[i];row(m_objects,i,{QString::fromUtf8(o.name),tr("%1 → %2").arg(current.contains(o.subcatch)?tr("Subcatchment"):tr("Unreviewed"),o.reviewed?tr("Subcatchment / uniform"):tr("Unreviewed")),number(o.declared_area/m_areaFactor),number(o.polygon_area/m_areaFactor),number(o.lid_area/m_areaFactor),number(o.pervious_area/m_areaFactor),number(o.impervious_area/m_areaFactor),number(o.inside_area/m_areaFactor),number(o.outside_area/m_areaFactor),QString::fromUtf8(o.reason)});if(o.status==3)for(int c=0;c<m_objects->columnCount();++c)m_objects->item(i,c)->setForeground(QColor(190,50,40));}
 for(int i=0;i<m_review.shares.size();++i){const auto& s=m_review.shares[i];const auto& o=m_review.objects[s.subcatch];row(m_receivers,i,{QString::number(s.cell+1),QString::fromUtf8(o.name),number(s.weather_area),number(s.pervious_area),number(s.lid_area),number(s.native_lid_area),number(m_review.meshWeatherArea[s.cell]),o.reviewed?tr("Proposed subcatchment"):tr("Unreviewed")});}
 QStringList params;for(const char* key:{"MODE","SOIL_CHAR","CLOSURE","GW_ET","WILTING_SUCTION","LINK_SEEPAGE"}){char value[128]={};if(swmm_gw2d_option_get(m_engine,key,value,sizeof value)==SWMM_OK)params<<QString::fromLatin1(key)+": "+QString::fromUtf8(value);}m_parameters->setText(params.join("   ·   ")+tr("\nEffective per-cell overrides remain in Assign Groundwater. Capacity and pending receipts are unavailable before simulation."));
 m_ready=m_review.valid&&m_review.before!=m_review.after;m_apply->setEnabled(m_ready);m_status->setText(!m_review.error.isEmpty()?m_review.error:m_ready?tr("%1 scoped source(s) reviewed. The source table uses project area units; receiver details use m². Apply saves one undoable authoring transaction; runtime remains unavailable.").arg(ids.size()):tr("No authoring changes. Geometric overlap does not imply recharge."));progress.setValue(100);
}
void SurfaceOwnershipDialog::apply(){
 if(!m_ready||m_busy)return;QString error;if(!contextValid(&error)||m_mesh->geomRevision()!=m_geometry||crs(m_model,m_mesh)!=m_crs){m_status->setText(error.isEmpty()?tr("Geometry or coordinate system changed. Preview again."):error);m_ready=false;m_apply->setEnabled(false);return;}
 if(!replaceSurfaceOwners(m_engine,m_review.after,m_review.token,&error)){m_status->setText(error);m_ready=false;m_apply->setEnabled(false);return;}
 const auto installed=previewSurfaceOwners(m_engine,m_review.after);m_committing=true;const auto finish=qScopeGuard([this]{m_committing=false;});m_canvas->undoStack()->push(new OwnershipCommand(m_model,m_mesh,m_review,installed.token));m_model->markEdited();emit m_model->optionsChanged({"2D_SURFACE_OWNERSHIP"});m_mesh->refreshSurfaceOwnership();m_ready=false;m_apply->setEnabled(false);m_status->setText(tr("Reviewed records saved. Hydraulic remapping and soil overrides are preserved. Runtime recharge remains unavailable."));emit applied();
}
void SurfaceOwnershipDialog::selectReceivers(){
 if(!m_selection||!m_mesh)return;const int r=m_objects->currentRow();if(r<0||r>=m_review.objects.size())return;const auto& o=m_review.objects[r];QSet<SWMMObjectRef> refs{{SWMMObjectRef::Subcatchment,QString::fromUtf8(o.name)}};for(const auto& s:m_review.shares)if(s.subcatch==o.subcatch)refs.insert(mesh::MeshObjectRef::cell(m_mesh->sourcePath(),s.cell));m_selection->select(refs,SelectionManager::Replace);
}
void SurfaceOwnershipDialog::selectCellSources(){
 if(!m_selection||!m_mesh)return;const int r=m_receivers->currentRow();if(r<0||r>=m_review.shares.size())return;const int cell=m_review.shares[r].cell;QSet<SWMMObjectRef> refs{mesh::MeshObjectRef::cell(m_mesh->sourcePath(),cell)};for(const auto& s:m_review.shares)if(s.cell==cell)refs.insert({SWMMObjectRef::Subcatchment,QString::fromUtf8(m_review.objects[s.subcatch].name)});m_selection->select(refs,SelectionManager::Replace);
}
}
