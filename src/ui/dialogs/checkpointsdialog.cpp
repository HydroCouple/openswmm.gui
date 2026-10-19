/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui/dialogs/checkpointsdialog.h"
#include "core/displaynumber.h"
#include "simulation/checkpointmodel.h"
#include <openswmm/engine/openswmm_checkpoint.h>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBrowser>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QFileDialog>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QCloseEvent>
#include <QJsonObject>
#include <QSignalBlocker>
#include <cmath>
#include <QLineEdit>

CheckpointsDialog::CheckpointsDialog(CheckpointModel* model,QWidget* parent):QDialog(parent),m_model(model) {
    setWindowTitle(tr("Checkpoints"));resize(960,720);setMinimumSize(540,440);
    auto* layout=new QVBoxLayout(this);
    auto* explanation=new QLabel(tr("Native checkpoint runs use a saved model and a fixed recording profile. Resume creates a new run folder. Closing this view leaves an active run running; Stop run retains partial files."));
    explanation->setWordWrap(true);layout->addWidget(explanation);
    auto* profileLayout=new QFormLayout;
    m_profile=new QComboBox;m_profile->setObjectName("checkpointProfile");
    m_profile->addItems({tr("No reports"),tr("Binary reports"),tr("Mesh reports"),tr("Binary + mesh reports"),tr("File-backed reports and history")});
    m_history=new QComboBox;m_history->setObjectName("checkpointHistory");
    m_history->addItems({tr("History off"),tr("Accepted-engine history"),tr("Detailed source intervals")});
    m_profile->setCurrentIndex(model->context().outputProfile);
    m_history->setCurrentIndex(model->context().sourceHistory?(model->context().detailedSources?2:1):0);
    profileLayout->addRow(tr("Recording profile"),m_profile);profileLayout->addRow(tr("Source history"),m_history);layout->addLayout(profileLayout);
    auto profileChanged=[this] {
        auto r=m_model->context();r.outputProfile=m_profile->currentIndex();
        if(r.outputProfile==4&&m_history->currentIndex()==0) {QSignalBlocker blocker(m_history);m_history->setCurrentIndex(1);}
        r.sourceHistory=m_history->currentIndex()>0;r.detailedSources=m_history->currentIndex()==2;m_model->setContext(r);
    };
    connect(m_profile,&QComboBox::currentIndexChanged,this,profileChanged);connect(m_history,&QComboBox::currentIndexChanged,this,profileChanged);
    m_list=new QTableView;m_list->setObjectName("checkpointList");m_list->setModel(model);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);m_list->verticalHeader()->hide();m_list->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setStretchLastSection(true);layout->addWidget(m_list,1);
    auto* tabs=new QTabWidget;
    m_details=new QTextBrowser;m_details->setObjectName("checkpointCoverage");m_files=new QTextBrowser;m_files->setObjectName("checkpointFiles");
    tabs->addTab(m_details,tr("Clocks and coverage"));tabs->addTab(m_files,tr("Files"));
    auto* retention=new QWidget;auto* retentionLayout=new QVBoxLayout(retention);auto* retentionForm=new QFormLayout;
    m_retention=new QComboBox;m_retention->addItems({tr("Keep all"),tr("Suggest keeping newest N")});m_keep=new QSpinBox;m_keep->setRange(1,1000000);m_keep->setValue(3);
    retentionForm->addRow(tr("Retention preview"),m_retention);retentionForm->addRow(tr("Newest per run lineage"),m_keep);retentionLayout->addLayout(retentionForm);
    m_storage=new QTextBrowser;m_storage->setObjectName("checkpointStorage");retentionLayout->addWidget(m_storage);
    tabs->addTab(retention,tr("Storage and retention"));layout->addWidget(tabs,2);
    auto* actions=new QGridLayout;int n=0;
    auto button=[&](const QString& label) {auto* b=new QPushButton(label);actions->addWidget(b,n/3,n%3);++n;return b;};
    auto* import=button(tr("Import checkpoint…"));m_inspect=button(tr("Inspect"));m_resume=button(tr("Resume as new run…"));
    m_start=button(tr("Start checkpoint run…"));m_save=button(tr("Save new checkpoint…"));m_export=button(tr("Export bundle…"));
    auto* show=button(tr("Show files"));auto* forget=button(tr("Forget entry · files remain"));m_cancel=button(tr("Cancel check / export"));
    m_stop=button(tr("Stop run"));auto* close=button(tr("Close"));layout->addLayout(actions);
    m_status=new QLabel;m_status->setObjectName("checkpointStatus");m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_status);
    m_notice=new QLabel;m_notice->setWordWrap(true);m_notice->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_notice);
    connect(model,&CheckpointModel::stateChanged,this,&CheckpointsDialog::refresh);
    connect(model,&QAbstractItemModel::dataChanged,this,&CheckpointsDialog::refresh);
    connect(model,&CheckpointModel::notice,this,[this](const QString& text) {m_notice->setText(text);});
    connect(m_list->selectionModel(),&QItemSelectionModel::selectionChanged,this,[this] {if(m_model->ioBusy())m_model->cancelIO();refresh();});
    connect(m_retention,&QComboBox::currentIndexChanged,this,&CheckpointsDialog::refresh);connect(m_keep,&QSpinBox::valueChanged,this,&CheckpointsDialog::refresh);
    connect(import,&QPushButton::clicked,this,[this] {const auto path=QFileDialog::getOpenFileName(this,tr("Import saved checkpoint"),{},tr("Checkpoints (*.chk);;All files (*)"));
        if(!path.isEmpty()&&!m_model->importFile(path))m_notice->setText(tr("Could not register this file."));refresh();});
    connect(m_inspect,&QPushButton::clicked,this,[this] {if(prepare()&&!m_model->inspectEntry(row()))m_notice->setText(tr("Inspection could not be started."));});
    connect(m_resume,&QPushButton::clicked,this,[this] {if(!prepare())return;const auto folder=newFolder(tr("Resume as new run"));if(!folder.isEmpty()&&!m_model->startRun(row(),folder))m_notice->setText(tr("Inspect this checkpoint against the current saved model and profile first."));});
    connect(m_start,&QPushButton::clicked,this,[this] {if(!prepare())return;const auto folder=newFolder(tr("Start checkpoint run"));if(!folder.isEmpty()&&!m_model->startRun(-1,folder))m_notice->setText(tr("Run could not be started."));});
    connect(m_save,&QPushButton::clicked,this,[this] {const auto path=QFileDialog::getSaveFileName(this,tr("Save at the next accepted boundary · new path only"),{},tr("Checkpoints (*.chk)"),nullptr,QFileDialog::DontConfirmOverwrite);
        if(!path.isEmpty()&&!m_model->save(path))m_notice->setText(tr("Run is no longer accepting Save requests."));});
    connect(m_export,&QPushButton::clicked,this,[this] {if(!prepare(false))return;const auto folder=newFolder(tr("Export saved file-backed checkpoint"));if(!folder.isEmpty()&&!m_model->exportEntry(row(),folder))m_notice->setText(tr("Export could not be started."));});
    connect(show,&QPushButton::clicked,this,[this] {if(const auto* e=m_model->entry(row()))QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(e->path).absolutePath()));});
    connect(forget,&QPushButton::clicked,this,[this] {m_model->forget(row());refresh();});
    connect(m_cancel,&QPushButton::clicked,model,&CheckpointModel::cancelIO);connect(m_stop,&QPushButton::clicked,model,&CheckpointModel::cancelRun);
    connect(close,&QPushButton::clicked,this,&QWidget::close);refresh();
}
int CheckpointsDialog::row()const {return m_list->currentIndex().row();}
bool CheckpointsDialog::prepare(bool frozenModel) {return !m_prepare||m_prepare(frozenModel);}
QString CheckpointsDialog::newFolder(const QString& title) {
    const auto parent=QFileDialog::getExistingDirectory(this,title+tr(" · choose parent folder"),m_model->context().stagingRoot);
    if(parent.isEmpty())return {};bool ok=false;
    const auto name=QInputDialog::getText(this,title,tr("New folder / run label"),QLineEdit::Normal,{},&ok).trimmed();
    if(!ok||name.isEmpty())return {};
    if(name=="."||name==".."||name.contains('/')||name.contains('\\')||name.contains(QChar(0))) {m_notice->setText(tr("Enter one new folder name."));return {};}
    return QDir(parent).filePath(name);
}
void CheckpointsDialog::refresh() {
    QSignalBlocker profileBlocker(m_profile),historyBlocker(m_history);
    m_profile->setCurrentIndex(m_model->context().outputProfile);
    m_history->setCurrentIndex(m_model->context().sourceHistory?(m_model->context().detailedSources?2:1):0);
    const auto* e=m_model->entry(row());const bool idle=!m_model->ioBusy();
    const auto caps=m_model->capabilities();
    const bool native=(caps&SWMM_CHECKPOINT_SESSION_BUILDER)!=0;
    const bool binary=(caps&SWMM_CHECKPOINT_BINARY_PROFILE)!=0,mesh=(caps&SWMM_CHECKPOINT_SURFACE_PROFILE)!=0;
    const bool available[]={native,native&&binary,native&&mesh,native&&binary&&mesh,native&&(caps&SWMM_CHECKPOINT_FILE_PROFILE)!=0};
    const int profileIndex=m_model->context().outputProfile;
    const bool ready=profileIndex>=0&&profileIndex<5&&available[profileIndex]&&(!m_model->context().sourceHistory||(caps&SWMM_CHECKPOINT_SOURCE_STREAM));
    auto* profiles=qobject_cast<QStandardItemModel*>(m_profile->model());
    for(int i=0;i<5;++i)profiles->item(i)->setEnabled(available[i]);
    auto* history=qobject_cast<QStandardItemModel*>(m_history->model());
    for(int i=1;i<3;++i)history->item(i)->setEnabled((caps&SWMM_CHECKPOINT_SOURCE_STREAM)!=0);
    m_profile->setEnabled(!m_model->busy());m_history->setEnabled(!m_model->busy());
    m_inspect->setEnabled(e&&idle&&native);m_resume->setEnabled(e&&e->compatible&&idle&&!m_model->runBusy()&&ready);
    m_start->setEnabled(idle&&!m_model->runBusy()&&ready);m_save->setEnabled(m_model->runBusy());m_export->setEnabled(e&&idle&&(caps&SWMM_CHECKPOINT_BUNDLE_EXPORT));
    m_cancel->setEnabled(m_model->ioBusy());m_stop->setEnabled(m_model->runBusy());m_keep->setEnabled(m_retention->currentIndex()==1);
    m_status->setText(m_model->status());
    if(!e) {m_details->setPlainText(tr("Select a checkpoint to see its clocks and recorded report coverage."));m_files->clear();}
    else {
        const auto& m=e->metadata;
        QString details=e->inspection+"\n"+tr("Model wall time · no timezone conversion")+"\n";
        if(m.isEmpty())details+=std::isfinite(e->acceptedSeconds)?tr("Saved at accepted %1 s · %2\nInspect after the run closes to check full coverage and compatibility.").arg(openswmmvis::displayNumber(e->acceptedSeconds),CheckpointModel::calendar(e->acceptedDate)):tr("No validated metadata. Inspect to check compatibility.");
        else details+=CheckpointModel::clocks(m)+tr("\n\nBinary: ")+CheckpointModel::coverage(m.value("binary").toObject())+
            tr("\nMesh: ")+CheckpointModel::coverage(m.value("mesh").toObject())+tr("\nNext report marker: %1 s").arg(openswmmvis::displayNumber(m.value("next_report_seconds").toDouble()))+
            tr("\nSource frames: %1 · saved prefix %2 bytes").arg(m.value("source_frames").toString(),m.value("source_prefix_bytes").toString());
        m_details->setPlainText(details);
        QString files=tr("Required saved bytes and current file bytes are distinct. A live history may have a later tail.\n\n");
        for(const auto& f:m_model->files(row()))files+=tr("%1\n%2\nRequired saved bytes: %3 · Current file bytes: %4\n\n").arg(f.role,f.path,f.requiredBytes.isEmpty()?tr("Unknown"):f.requiredBytes,f.currentBytes);
        m_files->setPlainText(files);
    }
    QList<int> all;for(int i=0;i<m_model->rowCount();++i)all.append(i);const auto total=m_model->storage(all);
    const auto selected=m_retention->currentIndex()==0?QList<int>{}:m_model->suggestions(m_keep->value());const auto potential=m_model->storage(selected);
    QString preview=tr("Current listed file bytes: %1%2\nPotential bytes in selected groups: %3%4\n\n").arg(QString::number(total.bytes),total.complete?QString():tr(" · incomplete"),QString::number(potential.bytes),potential.complete?QString():tr(" · incomplete"));
    preview+=tr("Preview only. Shared companions are counted once by file identity. External copies and references are unknown; these bytes are not guaranteed to be freed. Pins, active groups, unknown lineages and unreadable groups are protected. No files are deleted.\n\n");
    for(int i:selected)preview+=m_model->entry(i)->name+"\n";
    if(selected.isEmpty())preview+=tr("No groups suggested.");m_storage->setPlainText(preview);
}
void CheckpointsDialog::closeEvent(QCloseEvent* event) {if(m_model->ioBusy())m_model->cancelIO();QDialog::closeEvent(event);}
