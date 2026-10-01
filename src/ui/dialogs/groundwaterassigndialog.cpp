#include "ui/dialogs/groundwaterassigndialog.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "core/unitsystem.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <memory>

namespace openswmmvis::ui {
using namespace openswmmvis::assignment;
namespace {
QMap<QString,QByteArray> fingerprints(const QStringList&paths,QString*error,const std::function<bool()>&cancel={}){
 QMap<QString,QByteArray> result;
 for(const QString&path:paths){QFile file(path);if(!file.open(QIODevice::ReadOnly)){*error=QObject::tr("Cannot snapshot source %1: %2").arg(path,file.errorString());return {};}
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while(!file.atEnd()){
   if(cancel&&cancel()){*error=QObject::tr("Cancelled.");return {};}
   const QByteArray data=file.read(1024*1024);if(data.isEmpty()&&file.error()!=QFile::NoError){*error=file.errorString();return {};}
   hash.addData(data);
  }
  result.insert(QFileInfo(path).absoluteFilePath(),hash.result());
 }
 return result;
}
QStringList sourceFiles(const QString&path,QString*error){
 auto *ds=static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(),GDAL_OF_READONLY|GDAL_OF_RASTER|GDAL_OF_VECTOR,nullptr,nullptr,nullptr));
 if(!ds){*error=QObject::tr("Cannot open the assignment source.");return {};}
 QStringList result;char **list=ds->GetFileList();
 for(int i=0;list&&list[i];++i)result.append(QFileInfo(QString::fromUtf8(list[i])).absoluteFilePath());
 CSLDestroy(list);GDALClose(ds);
 const auto primary=QFileInfo(path).absoluteFilePath();if(!result.contains(primary))result.append(primary);
 result.removeDuplicates();return result;
}
class AssignmentUndo final:public QObject,public QUndoCommand {
 QPointer<SWMMModelLayer> model;QPointer<SWMM2DMeshLayer> mesh;
 SWMM_Engine engine;quint64 geometry;AquiferPreview preview;GroundwaterTransportPreview transport;bool isTransport=false,first=true,valid=true;
 std::function<void(bool)> changed;
 std::function<void(const QString&)> partial;
 bool check(){return valid&&model&&mesh&&model->engine()==engine&&mesh->geomRevision()==geometry;}
 void failed(const QString&error){setObsolete(true);QMessageBox::warning(QApplication::activeWindow(),QObject::tr("Groundwater assignment"),error);}
public:
 AssignmentUndo(SWMMModelLayer*m,SWMM2DMeshLayer*layer,AquiferPreview p,std::function<void(bool)>notify)
  :model(m),mesh(layer),engine(m->engine()),geometry(layer->geomRevision()),preview(std::move(p)),changed(std::move(notify)){
  setText(QObject::tr("Assign aquifer values to %n cell(s)",nullptr,preview.cells.size()));
  connect(m,&SWMMModelLayer::engineAboutToClose,this,[this]{valid=false;});
 }
 AssignmentUndo(SWMMModelLayer*m,SWMM2DMeshLayer*layer,GroundwaterTransportPreview p,std::function<void(bool)>notify,std::function<void(const QString&)>dirty)
  :AssignmentUndo(m,layer,AquiferPreview{},std::move(notify)) {transport=std::move(p);isTransport=true;partial=std::move(dirty);setText(QObject::tr("Assign groundwater transport to %n cell(s)",nullptr,transport.cells.size()));}
 void undo()override{QString error;if(!check()){failed(QObject::tr("The model or mesh changed; assignment undo was refused."));return;}
  if(isTransport){auto r=undoGroundwaterTransport(engine,transport);if(!r.success){if(r.changed&&partial)partial(r.error);failed(r.error);return;}}
  else if(!undoAquiferPreview(engine,preview,&error)){failed(error);return;}if(changed)changed(false);}
 void redo()override{if(first){first=false;return;}QString error;if(!check()){failed(QObject::tr("The model or mesh changed; assignment redo was refused."));return;}
  if(isTransport){auto r=applyGroundwaterTransport(engine,transport);if(!r.success){if(r.changed&&partial)partial(r.error);failed(r.error);return;}}
  else if(!applyAquiferPreview(engine,preview,&error)){failed(error);return;}if(changed)changed(true);}
};
}
GroundwaterAssignDialog::GroundwaterAssignDialog(SWMMModelLayer*model,SWMM2DMeshLayer*mesh,MapCanvas*canvas,
 SelectionManager*selection,const UnitSystem*units,QWidget*parent):QDialog(parent),m_model(model),m_mesh(mesh),m_canvas(canvas),m_selection(selection){
 m_events=new GroundwaterAssignmentEvents(model?static_cast<QObject*>(model):this);
 setWindowTitle(tr("Assign groundwater values"));setWindowModality(Qt::NonModal);resize(780,790);
 m_engine=model?model->engine():nullptr;m_length=units?units->lengthLabel():tr("project length");m_rate=units?(units->isSI()?tr("mm/hr"):tr("in/hr")):tr("project rate");
 auto*outer=new QVBoxLayout(this);auto*intro=new QLabel(tr("Use the existing map rectangle or polygon cell selection, or enter cells below. Spatial property sampling uses cell centroids. Preview preserves existing aquifer rows and creates explicit cell overrides; these stop inheriting later property changes."),this);intro->setWordWrap(true);outer->addWidget(intro);
 auto*scroll=new QScrollArea(this);scroll->setWidgetResizable(true);auto*fields=new QWidget(scroll);auto*form=new QFormLayout(fields);scroll->setWidget(fields);outer->addWidget(scroll,2);
 m_target=new QComboBox(this);m_target->setObjectName("gwAssignmentTarget");
 for(const auto&t:aquiferTargets()){if(t.key=="FLOW")continue;m_target->addItem(t.label,t.key);const int i=m_target->count()-1;m_target->setItemData(i,t.unavailableReason,Qt::ToolTipRole);if(!t.supported)static_cast<QStandardItemModel*>(m_target->model())->item(i)->setEnabled(false);}
 m_target->addItem(tr("Initial species concentration — SAT / UNSAT"),"INITIAL_QUALITY");m_target->addItem(tr("Add injection / extraction sources"),"FLOW");
 form->addRow(tr("&Target:"),m_target);m_units=new QLabel(this);m_units->setWordWrap(true);form->addRow(m_units);
 m_scopeChoice=new QComboBox(this);m_scopeChoice->setObjectName("gwAssignmentScope");m_scopeChoice->addItems({tr("Selected mesh cells"),tr("All mesh cells"),tr("Mesh tag"),tr("Cell numbers"),tr("Polygon WKT in mesh CRS")});form->addRow(tr("&Scope:"),m_scopeChoice);
 m_scopeText=new QLineEdit(this);m_scopeText->setObjectName("gwAssignmentScopeText");m_scopeText->setPlaceholderText(tr("Tag, 1-based cell numbers separated by commas, or POLYGON ((x y, ...))"));form->addRow(tr("Scope &details:"),m_scopeText);
 m_route=new QComboBox(this);m_route->setObjectName("gwAssignmentRoute");m_route->addItems({tr("Manual constant"),tr("Feature layer"),tr("Raster")});form->addRow(tr("Input &method:"),m_route);
 m_value=new QDoubleSpinBox(this);m_value->setObjectName("gwAssignmentValue");m_value->setDecimals(12);m_value->setRange(-1e15,1e15);m_value->setValue(.5);form->addRow(tr("&Value:"),m_value);
 m_transportControls=new QWidget(this);auto*transportForm=new QFormLayout(m_transportControls);transportForm->setContentsMargins(0,0,0,0);
 m_species=new QComboBox(m_transportControls);m_species->setObjectName("gwAssignmentSpecies");QString speciesError;m_speciesCatalog=groundwaterTransportSpecies(m_engine,&speciesError);
 for(const auto&sp:m_speciesCatalog)m_species->addItem(tr("%1 (%2)").arg(sp.id,sp.nativeConcUnits.isEmpty()?tr("units unresolved"):sp.nativeConcUnits),sp.id);
 transportForm->addRow(tr("Species:"),m_species);m_zone=new QComboBox(m_transportControls);m_zone->setObjectName("gwAssignmentZone");m_zone->addItem(tr("Saturated (SAT)"),0);m_zone->addItem(tr("Unsaturated (UNSAT)"),1);transportForm->addRow(tr("Initial zone:"),m_zone);form->addRow(m_transportControls);
 m_sourceControls=new QWidget(this);auto*sourceForm=new QFormLayout(m_sourceControls);sourceForm->setContentsMargins(0,0,0,0);
 m_sourceName=new QLineEdit("Assigned",m_sourceControls);m_sourceName->setObjectName("gwAssignmentSourceName");sourceForm->addRow(tr("Source name prefix:"),m_sourceName);
 m_distribution=new QComboBox(m_sourceControls);m_distribution->setObjectName("gwAssignmentDistribution");m_distribution->addItems({tr("Value for each selected cell"),tr("Total across selected cells (area weighted)")});sourceForm->addRow(tr("Flow and MASS distribution:"),m_distribution);
 m_flowSeries=new QComboBox(m_sourceControls);m_flowSeries->setObjectName("gwAssignmentFlowSeries");m_flowSeries->addItem(tr("Constant value"),QString());GroundwaterTransportSnapshot catalog;
 if(readGroundwaterTransportSnapshot(m_engine,&catalog,&speciesError))for(auto it=catalog.timeSeries.cbegin();it!=catalog.timeSeries.cend();++it)m_flowSeries->addItem(it.key(),it.key());sourceForm->addRow(tr("Flow time series:"),m_flowSeries);
 auto*explanation=new QLabel(tr("Each application adds new named sources alongside existing forcing. Undo removes that batch. Flow is signed m³/s: positive injects; negative extracts at in-situ concentration. CONC uses the species' native concentration units; MASS uses native mass/s. Region totals split flow and MASS by cell area; CONC is unchanged. LAYER seeds and hydraulic boundary chemistry are unavailable. Raster/feature values are per-cell rates, not flux density."),m_sourceControls);explanation->setWordWrap(true);sourceForm->addRow(explanation);
 m_terms=new QTableWidget(0,5,m_sourceControls);m_terms->setObjectName("gwAssignmentTerms");m_terms->setHorizontalHeaderLabels({tr("Species"),tr("Term"),tr("Constant"),tr("Time series"),tr("Native units")});m_terms->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_terms->setMaximumHeight(135);sourceForm->addRow(m_terms);
 auto*termButtons=new QWidget(m_sourceControls);auto*termLayout=new QHBoxLayout(termButtons);termLayout->setContentsMargins(0,0,0,0);auto*addTerm=new QPushButton(tr("Add species term"),termButtons);auto*removeTerm=new QPushButton(tr("Remove selected term"),termButtons);termLayout->addWidget(addTerm);termLayout->addWidget(removeTerm);sourceForm->addRow(termButtons);form->addRow(m_sourceControls);
 addTerm->setAutoDefault(false);removeTerm->setAutoDefault(false);
 m_terms->setAccessibleName(tr("Groundwater source species terms"));m_terms->setAccessibleDescription(tr("Each row adds one species concentration or mass-rate term. Native units are shown in the last column."));
 connect(addTerm,&QPushButton::clicked,this,&GroundwaterAssignDialog::addSpeciesTerm);connect(removeTerm,&QPushButton::clicked,this,[this]{if(m_terms->currentRow()>=0){m_terms->removeRow(m_terms->currentRow());refreshTermAccessibility();invalidatePreview();}});
 connect(m_terms,&QTableWidget::itemChanged,this,&GroundwaterAssignDialog::invalidatePreview);
 for(auto*combo:{m_species,m_zone,m_distribution,m_flowSeries})connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateTarget();sourceChanged();});
 m_source=new QComboBox(this);m_source->setObjectName("gwAssignmentSource");form->addRow(tr("Loaded &source:"),m_source);
 auto*pathRow=new QWidget(this);auto*pathLayout=new QHBoxLayout(pathRow);pathLayout->setContentsMargins(0,0,0,0);m_path=new QLineEdit(pathRow);m_path->setObjectName("gwAssignmentPath");pathLayout->addWidget(m_path);auto*browse=new QPushButton(tr("Browse…"),pathRow);pathLayout->addWidget(browse);form->addRow(tr("Source &file:"),pathRow);
 if(auto*caption=qobject_cast<QLabel*>(form->labelForField(pathRow)))caption->setBuddy(m_path);
 m_path->setAccessibleName(tr("Source file"));m_path->setAccessibleDescription(tr("Feature or raster source file for the selected input method."));
 browse->setAutoDefault(false);browse->setAccessibleName(tr("Browse assignment source file"));
 m_layerName=new QLineEdit(this);form->addRow(tr("Vector layer name:"),m_layerName);m_field=new QLineEdit(this);m_field->setObjectName("gwAssignmentField");form->addRow(tr("Numeric field:"),m_field);
 m_filter=new QLineEdit(this);form->addRow(tr("Feature attribute filter:"),m_filter);m_selectedFeatures=new QCheckBox(tr("Use selected source features only"),this);form->addRow(m_selectedFeatures);
 m_band=new QSpinBox(this);m_band->setRange(1,65535);form->addRow(tr("Raster band / time slice:"),m_band);
 m_scale=new QDoubleSpinBox(this);m_scale->setDecimals(12);m_scale->setRange(-1e15,1e15);m_scale->setValue(1);form->addRow(tr("Source scale:"),m_scale);
 m_offset=new QDoubleSpinBox(this);m_offset->setDecimals(12);m_offset->setRange(-1e15,1e15);form->addRow(tr("Source offset:"),m_offset);
 m_skip=new QCheckBox(tr("Preserve cells with NoData / no coverage (otherwise refuse the batch)"),this);form->addRow(m_skip);
 m_status=new QLabel(tr("Choose a target and scope, then Preview. No model values change during preview."),this);m_status->setObjectName("gwAssignmentStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);outer->addWidget(m_status);
 m_table=new QTableWidget(0,3,this);m_table->setObjectName("gwAssignmentPreview");m_table->setHorizontalHeaderLabels({tr("Cell"),tr("Old effective value"),tr("New value")});m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);outer->addWidget(m_table,1);
 m_table->setAccessibleName(tr("Reviewed groundwater assignments"));m_table->setAccessibleDescription(tr("Preview of affected cell numbers and reviewed values. Apply reviewed values commits this batch."));
 auto*buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);m_preview=buttons->addButton(tr("Preview"),QDialogButtonBox::ActionRole);m_preview->setObjectName("gwAssignmentPreviewButton");m_apply=buttons->addButton(tr("Apply reviewed values"),QDialogButtonBox::ApplyRole);m_apply->setObjectName("gwAssignmentApplyButton");m_apply->setEnabled(false);outer->addWidget(buttons);
 connect(m_preview,&QPushButton::clicked,this,&GroundwaterAssignDialog::preview);connect(m_apply,&QPushButton::clicked,this,&GroundwaterAssignDialog::apply);connect(buttons,&QDialogButtonBox::rejected,this,&GroundwaterAssignDialog::reject);
 // Form buddies provide keyboard focus; explicit descriptions retain the
 // field identity where native combo accessibility uses the selected value.
 for(auto*layout:{form,transportForm,sourceForm})for(int row=0;row<layout->rowCount();++row){
  auto*field=layout->itemAt(row,QFormLayout::FieldRole);auto*labelItem=layout->itemAt(row,QFormLayout::LabelRole);
  auto*editor=field?field->widget():nullptr;auto*caption=labelItem?qobject_cast<QLabel*>(labelItem->widget()):nullptr;
  if(!editor||!caption||(!qobject_cast<QLineEdit*>(editor)&&!qobject_cast<QComboBox*>(editor)&&!qobject_cast<QAbstractSpinBox*>(editor)))continue;
  QString name=caption->text();name.remove('&');if(name.endsWith(':'))name.chop(1);
  editor->setAccessibleName(name);editor->setAccessibleDescription(name);
 }
 connect(m_target,qOverload<int>(&QComboBox::currentIndexChanged),this,&GroundwaterAssignDialog::updateTarget);
 connect(m_route,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{populateSources();sourceChanged();});
 connect(m_source,qOverload<int>(&QComboBox::currentIndexChanged),this,&GroundwaterAssignDialog::sourceChanged);
 for(auto*edit:findChildren<QLineEdit*>())connect(edit,&QLineEdit::textChanged,this,&GroundwaterAssignDialog::invalidatePreview);
 for(auto*spin:findChildren<QDoubleSpinBox*>())connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&GroundwaterAssignDialog::invalidatePreview);
 connect(m_scopeChoice,qOverload<int>(&QComboBox::currentIndexChanged),this,&GroundwaterAssignDialog::invalidatePreview);
 connect(m_band,qOverload<int>(&QSpinBox::valueChanged),this,&GroundwaterAssignDialog::invalidatePreview);connect(m_skip,&QCheckBox::toggled,this,&GroundwaterAssignDialog::invalidatePreview);connect(m_selectedFeatures,&QCheckBox::toggled,this,&GroundwaterAssignDialog::invalidatePreview);
 connect(browse,&QPushButton::clicked,this,[this]{QPointer<GroundwaterAssignDialog> self=this;const QString path=QFileDialog::getOpenFileName(this,tr("Assignment source"));if(!self||path.isEmpty()||!m_engine)return;m_source->setCurrentIndex(0);m_path->setText(path);invalidatePreview();});
 if(model){connect(model,&SWMMModelLayer::engineAboutToClose,this,&GroundwaterAssignDialog::invalidateContext);connect(model,&QObject::destroyed,this,&GroundwaterAssignDialog::invalidateContext);}
 if(mesh)connect(mesh,&QObject::destroyed,this,&GroundwaterAssignDialog::invalidateContext);
 if(selection)connect(selection,&SelectionManager::selectionChanged,this,&GroundwaterAssignDialog::invalidatePreview);
 if(units)connect(units,&UnitSystem::unitsChanged,this,&GroundwaterAssignDialog::invalidateContext);
 populateSources();updateTarget();sourceChanged();if(!model||!mesh||!canvas||!canvas->undoStack()||!m_engine)invalidateContext();
}
GroundwaterAssignDialog::~GroundwaterAssignDialog(){if(m_watcher)m_watcher->cancel();}
void GroundwaterAssignDialog::invalidateContext(){m_engine=nullptr;++m_serial;if(m_watcher)m_watcher->cancel();m_havePreview=false;setEnabled(false);QDialog::reject();}
void GroundwaterAssignDialog::reject(){if(m_watcher){m_watcher->cancel();++m_serial;m_status->setText(tr("Cancelling; the model remains unchanged."));return;}QDialog::reject();}
void GroundwaterAssignDialog::invalidatePreview(){++m_serial;m_havePreview=false;m_apply->setEnabled(false);if(m_watcher)m_watcher->cancel();}
void GroundwaterAssignDialog::updateTarget(){invalidatePreview();const QString key=m_target->currentData().toString();const bool quality=key=="INITIAL_QUALITY",source=key=="FLOW";
 m_transportControls->setVisible(quality);m_sourceControls->setVisible(source);
 if(source){m_units->setText(tr("Flow: m³/s, independent of project length units. Series values keep their authored times; preview lists only the currently authored constant or series reference."));}
 else if(quality){QString unit;for(const auto&sp:m_speciesCatalog)if(sp.id==m_species->currentData().toString())unit=sp.nativeConcUnits;m_units->setText(unit.isEmpty()?tr("Species units are unresolved. Initial quality cannot be assigned until a native unit mapping is available."):tr("Initial concentration: %1. SAT and UNSAT only; no sustained water or mass flux.").arg(unit));}
 else for(const auto&t:aquiferTargets())if(t.key==key){QString unit=t.unitKind;if(unit=="length")unit=m_length;else if(unit=="rate")unit=m_rate;else if(unit=="inverse-length")unit=tr("1/%1").arg(m_length);m_units->setText(tr("Units: %1. Values are authored in project units; source units are not inferred or converted.").arg(unit));break;}
 sourceChanged();
}
void GroundwaterAssignDialog::addSpeciesTerm(){
 const int row=m_terms->rowCount();m_terms->insertRow(row);auto*species=new QComboBox(m_terms);for(const auto&sp:m_speciesCatalog)species->addItem(sp.id,sp.id);auto*kind=new QComboBox(m_terms);kind->addItems({"CONC","MASS"});auto*series=new QComboBox(m_terms);series->addItem(tr("Constant"),QString());for(int i=1;i<m_flowSeries->count();++i)series->addItem(m_flowSeries->itemText(i),m_flowSeries->itemData(i));
 m_terms->setCellWidget(row,0,species);m_terms->setCellWidget(row,1,kind);m_terms->setItem(row,2,new QTableWidgetItem("0"));m_terms->setCellWidget(row,3,series);auto*unit=new QLabel(m_terms);m_terms->setCellWidget(row,4,unit);
 const auto refresh=[this,species,kind,unit]{QString text=tr("unresolved");for(const auto&sp:m_speciesCatalog)if(sp.id==species->currentData().toString()){const bool mass=kind->currentText()=="MASS";text=mass?(sp.massSupported?sp.nativeMassRateUnits:tr("unsupported")):sp.nativeConcUnits;break;}unit->setText(text);refreshTermAccessibility();invalidatePreview();};
 connect(species,qOverload<int>(&QComboBox::currentIndexChanged),this,refresh);connect(kind,qOverload<int>(&QComboBox::currentIndexChanged),this,refresh);connect(series,qOverload<int>(&QComboBox::currentIndexChanged),this,&GroundwaterAssignDialog::invalidatePreview);refresh();
}
void GroundwaterAssignDialog::refreshTermAccessibility(){
 for(int row=0;row<m_terms->rowCount();++row)for(int column=0;column<m_terms->columnCount();++column){
  const QString name=tr("Species term row %1, %2").arg(row+1).arg(m_terms->horizontalHeaderItem(column)->text());
  if(auto*editor=m_terms->cellWidget(row,column)){
   editor->setAccessibleName(name);QString description=name;
   if(auto*unit=qobject_cast<QLabel*>(editor))description+=QStringLiteral(": ")+unit->text();
   editor->setAccessibleDescription(description);
  }else if(auto*item=m_terms->item(row,column))item->setData(Qt::AccessibleDescriptionRole,name);
 }
}
void GroundwaterAssignDialog::populateSources(){m_source->clear();m_source->addItem(tr("Choose a file…"),QString());if(!m_canvas)return;
 for(auto*base:m_canvas->layers()){
  if(m_route->currentIndex()==1)if(auto*layer=qobject_cast<GISVectorLayer*>(base))m_source->addItem(layer->name(),layer->layerId());
  if(m_route->currentIndex()==2)if(auto*layer=qobject_cast<GISRasterLayer*>(base))m_source->addItem(layer->name(),layer->layerId());
 }}
void GroundwaterAssignDialog::sourceChanged(){invalidatePreview();const bool manual=m_route->currentIndex()==0,vector=m_route->currentIndex()==1;
 m_value->setEnabled(manual&&!(m_target->currentData()=="FLOW"&&!m_flowSeries->currentData().toString().isEmpty()));m_source->setEnabled(!manual);m_path->setEnabled(!manual);m_layerName->setEnabled(vector);m_field->setEnabled(vector);m_filter->setEnabled(vector);m_selectedFeatures->setEnabled(vector);m_band->setEnabled(!manual&&!vector);m_scale->setEnabled(!manual);m_offset->setEnabled(!manual);
 if(!m_canvas)return;for(auto*base:m_canvas->layers())if(base->layerId()==m_source->currentData().toString()){
  if(auto*layer=qobject_cast<GISVectorLayer*>(base)){m_path->setText(layer->filePath());m_layerName->setText(layer->ogrLayerName());}
  if(auto*layer=qobject_cast<GISRasterLayer*>(base))m_path->setText(layer->filePath());
 }}
QVector<int> GroundwaterAssignDialog::scope(QString*error)const{
 QVector<int> cells;if(!m_mesh){*error=tr("No mesh.");return cells;}const auto&m=m_mesh->mesh();const int mode=m_scopeChoice->currentIndex();QSet<int> chosen;
 if(mode==0&&m_selection){const auto key=mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());for(const auto&ref:m_selection->selection()){QString layer;int cell=-1;if(mesh::MeshObjectRef::parseCell(ref,&layer,&cell)&&layer==key)chosen.insert(cell);}}
 else if(mode==1||mode==2){for(int i=0;i<m.triangles.size();++i)if(mode==1||m.triangles[i].tag==m_scopeText->text())chosen.insert(i);}
 else if(mode==3){for(const auto&text:m_scopeText->text().split(',',Qt::SkipEmptyParts)){bool ok=false;int n=text.trimmed().toInt(&ok);if(!ok||n<1){*error=tr("Enter 1-based cell numbers separated by commas.");return {};}chosen.insert(n-1);}}
 else if(mode==4){OGRGeometry*g=nullptr;const auto bytes=m_scopeText->text().toUtf8();if(OGRGeometryFactory::createFromWkt(bytes.constData(),nullptr,&g)!=OGRERR_NONE||!g){*error=tr("Enter valid polygon WKT in the mesh CRS.");return {};}
  std::unique_ptr<OGRGeometry,decltype(&OGRGeometryFactory::destroyGeometry)>polygon(g,&OGRGeometryFactory::destroyGeometry);
  if(wkbFlatten(g->getGeometryType())!=wkbPolygon||!g->IsValid()||g->IsEmpty()){*error=tr("The scope must be a valid nonempty polygon; holes are allowed.");return {};}
  for(int i=0;i<m.triangles.size();++i){const auto p=mesh::cellGeom(m.vertices,m.triangles[i]).centroid;OGRPoint point(p.x(),p.y());if(g->Intersects(&point))chosen.insert(i);}
 }
 for(int cell:chosen)if(cell<0||cell>=m.triangles.size()){*error=tr("A selected cell no longer exists.");return {};}else cells.append(cell);
 std::sort(cells.begin(),cells.end());if(cells.isEmpty())*error=tr("No mesh cells are in scope.");return cells;
}
bool GroundwaterAssignDialog::currentContext(QString*error)const{
 if(!m_engine||!m_model||!m_mesh||m_model->engine()!=m_engine){*error=tr("The model is no longer available.");return false;}
 if(m_mesh->geomRevision()!=m_geometry||m_mesh->attrRevision()!=m_attributes||m_mesh->sourcePath()!=m_meshPath||
    (m_mesh->srs()?m_mesh->srs()->toWkt():QString())!=m_meshCrs){*error=tr("The mesh changed since preview. Preview again.");return false;}
 QString why;const auto selected=scope(&why);if(!why.isEmpty()||selected!=m_scope){*error=tr("The assignment scope changed. Preview again.");return false;}return true;
}
void GroundwaterAssignDialog::preview(){
 if(m_watcher||!m_model||!m_mesh||!m_engine)return;invalidatePreview();QString error;
 AquiferRequest request;GroundwaterTransportRequest transport;const QString key=m_target->currentData().toString();const bool isTransport=key=="FLOW"||key=="INITIAL_QUALITY";
 if(isTransport){if(!readGroundwaterTransportSnapshot(m_engine,&transport.before,&error)){m_status->setText(error);return;}}
 else if(!readAquiferSnapshot(m_engine,&request.before,&error)){m_status->setText(error);return;}
 m_scope=scope(&error);if(!error.isEmpty()){m_status->setText(error);return;}request.cells=m_scope;request.target=m_target->currentData().toString();request.skipNoData=m_skip->isChecked();
 m_geometry=m_mesh->geomRevision();m_attributes=m_mesh->attrRevision();m_meshPath=m_mesh->sourcePath();m_meshCrs=m_mesh->srs()?m_mesh->srs()->toWkt():QString();
 MeshAssignmentSampling::Job job;job.strictCrs=true;job.rejectOverlaps=true;job.meshCrsWkt=m_meshCrs;job.triangles=m_scope;job.targetKeys={request.target.toUtf8()};job.targetMin={-std::numeric_limits<double>::max()};job.targetMax={std::numeric_limits<double>::max()};
 for(int cell:m_scope){request.cellTags.append(m_mesh->mesh().triangles[cell].tag);job.centroids.append(mesh::cellGeom(m_mesh->mesh().vertices,m_mesh->mesh().triangles[cell]).centroid);}
 const int route=m_route->currentIndex();
 if(isTransport){transport.cells=request.cells;transport.skipNoData=request.skipNoData;transport.target=key=="FLOW"?GroundwaterTransportTarget::Source:GroundwaterTransportTarget::InitialQuality;transport.species=m_species->currentData().toString();transport.zone=m_zone->currentData().toInt();transport.sourceName=m_sourceName->text().trimmed();transport.flowSeries=m_flowSeries->currentData().toString();transport.distribution=m_distribution->currentIndex()==1?GroundwaterSourceDistribution::RegionTotal:GroundwaterSourceDistribution::PerCell;
  if(key=="INITIAL_QUALITY"){bool known=false;for(const auto&sp:m_speciesCatalog)if(sp.id==transport.species&&!sp.nativeConcUnits.isEmpty())known=true;if(!known){m_status->setText(tr("Choose a species with verified native concentration units."));return;}}
  if(key=="FLOW"){
   if(route&&(transport.distribution==GroundwaterSourceDistribution::RegionTotal||!transport.flowSeries.isEmpty())){m_status->setText(tr("Region-total or time-series flow requires the manual input method. Spatial sampling represents per-cell rates; it does not integrate flux density."));return;}
   for(int cell:m_scope)transport.areaWeights.append(std::abs(mesh::cellSignedArea(m_mesh->mesh().vertices,m_mesh->mesh().triangles[cell])));
   for(int row=0;row<m_terms->rowCount();++row){GroundwaterSpeciesTerm term;term.species=qobject_cast<QComboBox*>(m_terms->cellWidget(row,0))->currentData().toString();term.kind=qobject_cast<QComboBox*>(m_terms->cellWidget(row,1))->currentText();term.series=qobject_cast<QComboBox*>(m_terms->cellWidget(row,3))->currentData().toString();bool ok=false;term.value=m_terms->item(row,2)->text().toDouble(&ok);if(!ok||!std::isfinite(term.value)){m_status->setText(tr("Enter a finite constant for each species term."));return;}transport.terms.append(term);}
  }
 }
 const QString path=m_path->text();job.source=route==1?MeshAssignmentSampling::Source::Vector:MeshAssignmentSampling::Source::Raster;
 job.rasterPath=path;job.vectorPath=path;job.vectorLayerName=m_layerName->text();job.fields={m_field->text()};job.vectorFilterExpr=m_filter->text();job.bands={m_band->value()};job.scale=m_scale->value();job.offset=m_offset->value();
 if(route&&path.isEmpty()){m_status->setText(tr("Choose a source file."));return;}
 if(m_canvas)for(auto*base:m_canvas->layers())if(base->layerId()==m_source->currentData().toString()){
  QString loadedPath;
  if(auto*layer=qobject_cast<GISVectorLayer*>(base))loadedPath=layer->filePath();
  if(auto*layer=qobject_cast<GISRasterLayer*>(base))loadedPath=layer->filePath();
  if(QFileInfo(loadedPath).absoluteFilePath()!=QFileInfo(path).absoluteFilePath())continue;
  if(base->srs())job.assignedSourceCrsWkt=base->srs()->toWkt();
  if(auto*layer=qobject_cast<GISVectorLayer*>(base)){job.filterBySelection=m_selectedFeatures->isChecked();job.selectedIds=layer->selectedFeatureIds();}
 }
 if(route==1&&m_selectedFeatures->isChecked()&&(!job.filterBySelection||job.selectedIds.isEmpty())){m_status->setText(tr("Select source features in a loaded feature layer first."));return;}
 request.values.fill(m_value->value(),m_scope.size());
 m_recipe={{"operationId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"recordKind","snapshot-assignment-audit"},{"version",1},{"target",request.target},{"sourceRoute",route},{"sourcePath",path},{"layerName",job.vectorLayerName},{"field",m_field->text()},{"filter",job.vectorFilterExpr},{"band",m_band->value()},{"scale",job.scale},{"offset",job.offset},{"sampling","centroid"},{"skipNoData",request.skipNoData},{"meshCrsWkt",m_meshCrs},{"assignedSourceCrsWkt",job.assignedSourceCrsWkt},{"constant",m_value->value()},{"scopeMode",m_scopeChoice->currentIndex()},{"scopeText",m_scopeText->text()},{"meshLayerId",m_mesh->layerId()},{"refreshPolicy","snapshot-only"}};
 if(isTransport){m_recipe["species"]=transport.species;m_recipe["zone"]=transport.zone;m_recipe["distribution"]=int(transport.distribution);m_recipe["flowSeries"]=transport.flowSeries;m_recipe["sourceName"]=transport.sourceName;QJsonArray terms;for(const auto&t:transport.terms)terms.append(QJsonObject{{"species",t.species},{"kind",t.kind},{"series",t.series},{"value",t.value}});m_recipe["terms"]=terms;}
 QJsonArray ids;for(int cell:m_scope)ids.append(cell);m_recipe["cells"]=ids;QJsonArray fids;for(auto id:job.selectedIds)fids.append(QString::number(id));m_recipe["featureIds"]=fids;
 const auto serial=m_serial;m_preview->setEnabled(false);m_status->setText(tr("Preparing an immutable preview…"));m_watcher=new QFutureWatcher<WorkResult>(this);
 connect(m_watcher,&QFutureWatcher<WorkResult>::finished,this,[this,serial]{if(serial!=m_serial){m_apply->setEnabled(false);if(m_watcher)m_watcher->cancel();}finishPreview();});
 m_watcher->setFuture(QtConcurrent::run([request,transport,isTransport,job,route,path,serial](QPromise<WorkResult>&promise)mutable{
  Q_UNUSED(serial);WorkResult result;try{
   if(route){auto files=sourceFiles(path,&result.error);if(result.error.isEmpty())result.fingerprints=fingerprints(files,&result.error,[&]{return promise.isCanceled();});
    if(result.error.isEmpty()){
     const auto sampled=sampleMeshAssignment(job,[&]{return promise.isCanceled();});
     result.error=sampled.error;result.cancelled=sampled.cancelled;
     request.values.fill(std::numeric_limits<double>::quiet_NaN(),request.cells.size());
     if(sampled.skippedRange||sampled.skippedNonNumeric)result.error=QObject::tr("The source contains invalid numeric values; no assignment was prepared.");
     if(result.error.isEmpty()&&!result.cancelled){for(int i=0;i<sampled.triangles.size();++i){const auto at=std::lower_bound(request.cells.begin(),request.cells.end(),sampled.triangles[i]);if(at!=request.cells.end()&&*at==sampled.triangles[i]&&!sampled.values.isEmpty())request.values[int(at-request.cells.begin())]=route==1?sampled.values[0][i]*job.scale+job.offset:sampled.values[0][i];}
      if(fingerprints(files,&result.error,[&]{return promise.isCanceled();})!=result.fingerprints&&result.error.isEmpty())result.error=QObject::tr("The source changed while sampling; preview again.");}
    }
   }
   if(result.error.isEmpty()&&!result.cancelled&&!promise.isCanceled()){
    result.isTransport=isTransport;result.values=request.values;
    if(isTransport){transport.values=request.values;result.transport=previewGroundwaterTransport(transport);}else result.preview=previewAquiferAssignment(request);
   }
  }catch(const std::exception&e){result.error=QString::fromUtf8(e.what());}catch(...){result.error=QObject::tr("Assignment preview failed.");}
  result.cancelled=result.cancelled||promise.isCanceled();promise.addResult(result);
 }));
}
void GroundwaterAssignDialog::finishPreview(){
 auto*watcher=m_watcher;m_watcher=nullptr;m_preview->setEnabled(m_engine);if(!watcher)return;
 WorkResult result;try{if(watcher->isCanceled())result.cancelled=true;else result=watcher->result();}catch(...){result.error=tr("Assignment worker failed.");}watcher->deleteLater();
 if(result.cancelled){m_status->setText(tr("Cancelled; the model was not changed."));return;}
 QString error;if(!currentContext(&error)){m_status->setText(error);return;}
 const QString previewError=result.isTransport?result.transport.error:result.preview.error;
 if(!result.error.isEmpty()||!previewError.isEmpty()){m_status->setText(result.error.isEmpty()?previewError:result.error);return;}
 if(result.isTransport){m_reviewed=result;m_havePreview=true;const auto&p=result.transport;m_table->setRowCount(std::min(qsizetype(500),p.cells.size()));
  m_table->setHorizontalHeaderLabels({tr("Cell"),tr("Previous records"),tr("Reviewed value / series")});
  QHash<int,QString> sourceValues;
  if(p.target==GroundwaterTransportTarget::Source)for(int i=p.before.sources.size();i<p.after.sources.size();++i){const auto&r=p.after.sources[i];sourceValues.insert(r.cell,(r.series.isEmpty()?QString::number(r.flow,'g',17):r.series)+tr(" × %1").arg(r.scale,0,'g',17));}
  for(int i=0;i<m_table->rowCount();++i){const int cell=p.cells[i];const int at=int(std::lower_bound(m_scope.begin(),m_scope.end(),cell)-m_scope.begin());QString value=at<result.values.size()?QString::number(result.values[at],'g',17):QString();
   if(p.target==GroundwaterTransportTarget::Source)value=sourceValues.value(cell);
   m_table->setItem(i,0,new QTableWidgetItem(QString::number(cell+1)));m_table->setItem(i,1,new QTableWidgetItem(tr("Preserved / checked upsert")));m_table->setItem(i,2,new QTableWidgetItem(value));}
  QString totals;if(p.target==GroundwaterTransportTarget::Source){double flow=0;bool series=false;for(int i=p.before.sources.size();i<p.after.sources.size();++i){const auto&r=p.after.sources[i];flow+=r.flow*r.scale;series|=!r.series.isEmpty();}totals=series?tr(" Flow series and area scales are preserved; no single instantaneous total is implied."):tr(" Total authored constant flow: %1 m³/s.").arg(flow,0,'g',17);}
  m_status->setText(tr("Preview: %1 cells; %2 NoData cells preserved. Existing unrelated transport records are retained.%3").arg(p.cells.size()).arg(p.skippedCells.size()).arg(totals));m_apply->setEnabled(p.before!=p.after);return;}
 m_table->setHorizontalHeaderLabels({tr("Cell"),tr("Old effective value"),tr("New value")});
 m_reviewed=result;m_havePreview=true;m_table->setRowCount(std::min(qsizetype(500),result.preview.cells.size()));
 for(int i=0;i<m_table->rowCount();++i){m_table->setItem(i,0,new QTableWidgetItem(QString::number(result.preview.cells[i]+1)));m_table->setItem(i,1,new QTableWidgetItem(QString::number(result.preview.oldValues[i],'g',17)));m_table->setItem(i,2,new QTableWidgetItem(QString::number(result.preview.newValues[i],'g',17)));}
 m_status->setText(tr("Preview: %1 changed cells; %2 cells preserved for NoData / no coverage. Original aquifer rows and mesh tags are retained. The table shows up to 500 changed cells. This property/initial-state assignment adds no sustained water or mass rate.").arg(result.preview.cells.size()).arg(result.preview.skippedCells.size()));
 m_apply->setEnabled(!result.preview.appended.isEmpty());
}
void GroundwaterAssignDialog::apply(){
 if(!m_havePreview||m_watcher)return;QString error;if(!currentContext(&error)){m_status->setText(error);invalidatePreview();return;}
 if(m_reviewed.fingerprints.isEmpty()){commitVerified();return;}
 const auto expected=m_reviewed.fingerprints;const auto serial=m_serial;m_apply->setEnabled(false);m_preview->setEnabled(false);m_status->setText(tr("Checking that source files still match the reviewed values…"));
 m_watcher=new QFutureWatcher<WorkResult>(this);connect(m_watcher,&QFutureWatcher<WorkResult>::finished,this,[this,serial,expected]{auto*w=m_watcher;m_watcher=nullptr;m_preview->setEnabled(m_engine);WorkResult r;try{if(w->isCanceled())r.cancelled=true;else r=w->result();}catch(...){r.error=tr("Source verification failed.");}w->deleteLater();
  if(serial!=m_serial||r.cancelled){m_status->setText(tr("Assignment cancelled or edited; preview again."));return;}
  if(!r.error.isEmpty()||r.fingerprints!=expected){m_status->setText(r.error.isEmpty()?tr("Source files changed since preview; preview again."):r.error);invalidatePreview();return;}commitVerified();});
 m_watcher->setFuture(QtConcurrent::run([expected](QPromise<WorkResult>&p){WorkResult r;r.fingerprints=fingerprints(expected.keys(),&r.error,[&]{return p.isCanceled();});r.cancelled=p.isCanceled();p.addResult(r);}));
}
void GroundwaterAssignDialog::commitVerified(){
 QString error;if(!m_havePreview||!currentContext(&error)){m_status->setText(error);invalidatePreview();return;}
 if(!m_canvas||!m_canvas->undoStack()){m_status->setText(tr("The undo owner is no longer available."));invalidatePreview();return;}
 if(m_reviewed.isTransport){const auto result=applyGroundwaterTransport(m_engine,m_reviewed.transport);if(!result.success){if(result.changed&&m_events){auto event=m_recipe;event["event"]="failed-after-write";event["error"]=result.error;event["rollbackComplete"]=result.rollbackComplete;emit m_events->changed(event,true);emit applied();}m_status->setText(result.error);invalidatePreview();return;}}
 else if(!applyAquiferPreview(m_engine,m_reviewed.preview,&error)){m_status->setText(error);invalidatePreview();return;}
 QJsonObject hashes;for(auto it=m_reviewed.fingerprints.begin();it!=m_reviewed.fingerprints.end();++it)hashes[it.key()]=QString::fromLatin1(it.value().toHex());m_recipe["sourceSha256"]=hashes;
 QPointer<GroundwaterAssignmentEvents> events=m_events;const auto recipe=m_recipe;
 const auto changed=[events,recipe](bool installed){if(events){auto event=recipe;event["event"]=installed?"redone":"undone";emit events->changed(event,installed);}};
 const auto partial=[events,recipe](const QString&error){if(events){auto event=recipe;event["event"]="failed-after-write";event["error"]=error;emit events->changed(event,true);}};
 AssignmentUndo*command=m_reviewed.isTransport?new AssignmentUndo(m_model,m_mesh,m_reviewed.transport,changed,partial):new AssignmentUndo(m_model,m_mesh,m_reviewed.preview,changed);m_canvas->undoStack()->push(command);
 if(events){auto event=recipe;event["event"]="applied";emit events->changed(event,true);}
 emit applied();emit recipeAccepted(m_recipe);m_status->setText(tr("Applied %1 cell overrides as one undoable operation.").arg(m_reviewed.isTransport?m_reviewed.transport.cells.size():m_reviewed.preview.cells.size()));invalidatePreview();
}
} // namespace
