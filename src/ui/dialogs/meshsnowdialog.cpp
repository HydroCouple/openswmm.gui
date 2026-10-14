// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/meshsnowdialog.h"
#include "ui/models/meshsnowmodel.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "snowpack/snowpackregistry.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>
namespace openswmmvis::ui {
namespace {
class PackDelegate final:public QStyledItemDelegate {
    MeshSnowModel* model_;
public:
    PackDelegate(MeshSnowModel* model,QObject* parent):QStyledItemDelegate(parent),model_(model){}
    QWidget* createEditor(QWidget* p,const QStyleOptionViewItem&,const QModelIndex&)const override {
        auto* c=new QComboBox(p);c->addItems(model_->packs);return c;
    }
    void setEditorData(QWidget* w,const QModelIndex& i)const override {static_cast<QComboBox*>(w)->setCurrentText(i.data(Qt::EditRole).toString());}
    void setModelData(QWidget* w,QAbstractItemModel* m,const QModelIndex& i)const override {m->setData(i,static_cast<QComboBox*>(w)->currentText());}
};
class SnowUndo final:public QUndoCommand {
    QPointer<SWMMModelLayer> model_;QPointer<SWMM2DMeshLayer> mesh_;SWMM_Engine engine_;quint64 geometry_;
    std::array<QString,3> before_,after_;bool first_=true;
    void install(bool after) {
        std::array<QString,3> current;QString error;
        if(!model_||!mesh_||model_->engine()!=engine_||mesh_->geomRevision()!=geometry_)error=QObject::tr("The model or mesh has changed.");
        else {error=MeshSnowModel::readSections(engine_,current);
            if(error.isEmpty()&&current!=(after?before_:after_))error=QObject::tr("Snow assignments changed outside this undo operation.");
            if(error.isEmpty())error=MeshSnowModel::applySections(engine_,after?after_:before_);}
        if(!error.isEmpty()){setObsolete(true);QMessageBox::warning(QApplication::activeWindow(),QObject::tr("Mesh snow"),error);return;}
        model_->markEdited();emit model_->optionsChanged({"2D_SNOW"});
    }
public:
    SnowUndo(SWMMModelLayer* m,SWMM2DMeshLayer* mesh,std::array<QString,3> before,std::array<QString,3> after):
        model_(m),mesh_(mesh),engine_(m->engine()),geometry_(mesh->geomRevision()),before_(std::move(before)),after_(std::move(after)) {setText(QObject::tr("Assign mesh snow"));}
    void undo()override{install(false);}void redo()override{if(first_){first_=false;return;}install(true);}
};
}
MeshSnowDialog::MeshSnowDialog(SWMMModelLayer* model,SWMM2DMeshLayer* mesh,MapCanvas* canvas,SelectionManager* selection,QWidget* parent):
    QDialog(parent),model_(model),mesh_(mesh),canvas_(canvas),selection_(selection),engine_(model?model->engine():nullptr),geometry_(mesh?mesh->geomRevision():0) {
    setWindowTitle(tr("Mesh snow assignment"));resize(980,700);setObjectName("meshSnowDialog");
    auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
    mode_=new QComboBox(this);mode_->setObjectName("meshSnowMode");mode_->addItems({tr("Automatic — enabled when assigned"),tr("Enabled"),tr("Disabled")});form->addRow(tr("Snowmelt:"),mode_);
    step_=new QDoubleSpinBox(this);step_->setObjectName("meshSnowStep");step_->setRange(0,3600);step_->setDecimals(6);step_->setSpecialValueText(tr("Use global snow step"));step_->setSuffix(tr(" s"));form->addRow(tr("UEB update interval:"),step_);layout->addLayout(form);
    auto* note=new QLabel(tr("Cell override → region tag → whole-mesh default. Blank site parameters inherit; slope and aspect come from cell geometry unless both are set. Shared snowpacks retain their subcatchment assignments."),this);note->setWordWrap(true);layout->addWidget(note);
    auto* controls=new QHBoxLayout;
    scope_=new QComboBox(this);scope_->addItems({tr("Selected cells"),tr("Region tag"),tr("Whole mesh")});scope_->setAccessibleName(tr("Assignment scope"));controls->addWidget(scope_);
    tag_=new QComboBox(this);if(mesh){QStringList tags;for(const auto& cell:mesh->mesh().triangles)if(!cell.tag.isEmpty()&&!tags.contains(cell.tag))tags.append(cell.tag);tags.sort();tag_->addItems(tags);}tag_->setAccessibleName(tr("Region tag"));controls->addWidget(tag_);
    pack_=new QComboBox(this);pack_->setObjectName("meshSnowPack");pack_->setAccessibleName(tr("Shared snowpack"));controls->addWidget(pack_);
    auto* add=new QPushButton(tr("Add / replace assignment"),this);controls->addWidget(add);connect(add,&QPushButton::clicked,this,&MeshSnowDialog::addAssignment);
    auto* edit=new QPushButton(tr("Snowpacks…"),this);controls->addWidget(edit);connect(edit,&QPushButton::clicked,this,&MeshSnowDialog::editPacks);layout->addLayout(controls);
    rows_=new MeshSnowModel(this);table_=new QTableView(this);table_->setObjectName("meshSnowAssignments");table_->setModel(rows_);table_->setItemDelegateForColumn(2,new PackDelegate(rows_,table_));table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);layout->addWidget(table_,2);
    auto* remove=new QPushButton(tr("Remove selected assignments"),this);layout->addWidget(remove);connect(remove,&QPushButton::clicked,this,[this]{auto selected=table_->selectionModel()->selectedRows();std::sort(selected.begin(),selected.end(),[](auto a,auto b){return a.row()>b.row();});for(auto i:selected)rows_->removeRow(i.row());preview();});
    resolved_=new QTableWidget(0,4,this);resolved_->setObjectName("meshSnowPreview");resolved_->setHorizontalHeaderLabels({tr("Cell"),tr("Effective pack"),tr("Assignment source"),tr("Terrain")});resolved_->setEditTriggers(QAbstractItemView::NoEditTriggers);resolved_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(resolved_,1);
    status_=new QLabel(this);status_->setObjectName("meshSnowStatus");status_->setWordWrap(true);layout->addWidget(status_);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close,this);apply_=buttons->button(QDialogButtonBox::Apply);apply_->setObjectName("meshSnowApply");layout->addWidget(buttons);
    connect(apply_,&QPushButton::clicked,this,&MeshSnowDialog::apply);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(rows_,&QAbstractItemModel::dataChanged,this,[this]{preview();});connect(mode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{preview();});connect(step_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{preview();});
    connect(scope_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{tag_->setEnabled(scope_->currentIndex()==1);});tag_->setEnabled(false);
    if(model){connect(model,&SWMMModelLayer::engineAboutToClose,this,&MeshSnowDialog::invalidate);connect(model,&QObject::destroyed,this,&MeshSnowDialog::invalidate);}
    QString error=rows_->load(engine_);if(error.isEmpty())error=MeshSnowModel::readSections(engine_,before_);
    if(!error.isEmpty()){invalidate();status_->setText(error);return;}
    pack_->addItems(rows_->packs);loaded_step_=rows_->step;
    {QSignalBlocker a(mode_),b(step_);mode_->setCurrentIndex(rows_->mode);step_->setValue(loaded_step_);displayed_step_=step_->value();}
    preview();
}
QString MeshSnowDialog::contextError()const {
    if(!model_||!mesh_||!canvas_||!canvas_->undoStack()||!engine_||model_->engine()!=engine_||mesh_->geomRevision()!=geometry_)return tr("The model or mesh changed. Reopen this editor.");
    int state=0;if(swmm_engine_get_state(engine_,&state)!=SWMM_OK||(state!=SWMM_STATE_OPENED&&state!=SWMM_STATE_BUILDING))return tr("Snow assignments can be edited before initialization. Stop and reset the simulation to edit.");
    return {};
}
void MeshSnowDialog::invalidate(){engine_=nullptr;apply_->setEnabled(false);table_->setEnabled(false);status_->setText(tr("The model was closed. Reopen this editor to continue."));}
void MeshSnowDialog::preview() {
    if(!rows_||!status_)return;
    rows_->mode=mode_->currentIndex();rows_->step=step_->value()==displayed_step_?loaded_step_:step_->value();QString error=contextError();QStringList tags;
    if(mesh_)for(const auto& c:mesh_->mesh().triangles)tags.append(c.tag);
    if(error.isEmpty())error=rows_->validate(tags);
    resolved_->setRowCount(std::min(200,int(tags.size())));int active=0;
    for(int i=0;i<tags.size();++i){QString origin;const auto row=rows_->effective(i,tags[i],&origin);active+=row.pack.compare("NONE",Qt::CaseInsensitive)!=0;
        if(i<resolved_->rowCount()){const QString terrain=std::isnan(row.overrides[4])?tr("From cell geometry"):tr("%1° slope · %2° aspect").arg(row.overrides[4]).arg(row.overrides[5]);
            const QStringList values={QString::number(i+1),row.pack,origin,terrain};for(int k=0;k<4;++k)resolved_->setItem(i,k,new QTableWidgetItem(values[k]));}}
    status_->setText(error.isEmpty()?tr("%1 of %2 cells assigned. Preview shows the first %3. Changes remain staged until Apply.").arg(active).arg(tags.size()).arg(resolved_->rowCount()):error);apply_->setEnabled(error.isEmpty());
}
void MeshSnowDialog::addAssignment() {
    if(!contextError().isEmpty())return;
    MeshSnowRow row;row.pack=pack_->currentText();
    if(scope_->currentIndex()==2)rows_->upsert(row);
    else if(scope_->currentIndex()==1){row.tag=tag_->currentText();if(!row.tag.isEmpty())rows_->upsert(row);}
    else if(selection_)for(const auto& ref:selection_->selection()){QString key;int cell=-1;if(mesh::MeshObjectRef::parseCell(ref,&key,&cell)&&key==mesh::MeshObjectRef::layerKey(mesh_->sourcePath())){row.cell=cell;row.tag.clear();rows_->upsert(row);}}
    preview();
}
void MeshSnowDialog::editPacks() {
    if(!model_||!contextError().isEmpty())return;
    auto* registry=qobject_cast<snowpack::SnowpackRegistry*>(model_->ensureSnowpackRegistry());if(!registry)return;
    SnowpackEditorDialog dialog(registry,model_,this);dialog.exec();
    rows_->packs={"NONE"};for(int i=0;i<swmm_snowpack_count(engine_);++i)if(const char* name=swmm_snowpack_id(engine_,i))rows_->packs.append(QString::fromUtf8(name));
    const auto selected=pack_->currentText();pack_->clear();pack_->addItems(rows_->packs);pack_->setCurrentText(selected);preview();
}
void MeshSnowDialog::apply() {
    preview();if(!apply_->isEnabled())return;
    std::array<QString,3> current;QString error=MeshSnowModel::readSections(engine_,current);
    if(error.isEmpty()&&current!=before_)error=tr("Snow assignments changed in another view. Reopen this editor before applying.");
    if(error.isEmpty())error=MeshSnowModel::applySections(engine_,rows_->sections());
    if(!error.isEmpty()){status_->setText(error);return;}
    std::array<QString,3> after;error=MeshSnowModel::readSections(engine_,after);
    if(!error.isEmpty()){status_->setText(error);model_->markEdited();return;}
    if(after!=before_)canvas_->undoStack()->push(new SnowUndo(model_,mesh_,before_,after));
    before_=after;model_->markEdited();emit model_->optionsChanged({"2D_SNOW"});status_->setText(tr("Snow assignments applied. Undo restores the previous configuration."));
}
}
