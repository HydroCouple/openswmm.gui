/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui/dialogs/componentexecutiondialog.h"
#include "simulation/componentexecution.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
ComponentExecutionDialog::ComponentExecutionDialog(openswmmvis::simulation::ComponentEngineRegistry *model,QWidget *parent)
    :QDialog(parent),model_(model) {
    setObjectName("componentExecutionDialog");setWindowTitle(tr("Component execution"));resize(620,300);
    auto *layout=new QVBoxLayout(this);auto *form=new QFormLayout;
    mode_=new QComboBox(this);mode_->setObjectName("executionArrangement");
    for(const auto &pair:QList<QPair<QString,QString>>{{tr("Native engine"),"native"},{tr("Component: complete model"),"complete"},
        {tr("Component: split water"),"split_water"},{tr("Component: split pollutant"),"split_material"}})mode_->addItem(pair.first,pair.second);
    mode_->setCurrentIndex(qMax(0,mode_->findData(model_->mode())));form->addRow(tr("Run arrangement"),mode_);
    auto path=[&](const QString &label,const QString &name,const QString &setting,bool directory){
        auto *row=new QWidget(this);auto *box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);
        auto *edit=new QLineEdit(QSettings().value(setting).toString(),row);edit->setObjectName(name);box->addWidget(edit);
        auto *browse=new QPushButton(tr("Browse…"),row);box->addWidget(browse);
        connect(browse,&QPushButton::clicked,this,[this,edit,directory]{const auto value=directory?QFileDialog::getExistingDirectory(this,tr("Component library folder"),edit->text()):QFileDialog::getOpenFileName(this,tr("Composer application"),edit->text());if(!value.isEmpty())edit->setText(value);});
        form->addRow(label,row);return edit;
    };
    program_=path(tr("Composer application"),"composerProgram","SWMMVis/Components/ComposerProgram",false);
    components_=path(tr("Component library folder"),"componentLibraries","SWMMVis/Components/LibraryDirectory",true);
    capacity_=new QSpinBox(this);capacity_->setObjectName("materialCapacity");capacity_->setRange(1,65536);capacity_->setValue(model_->settings().value("capacity").toInt(4096));form->addRow(tr("Material journal capacity"),capacity_);
    layout->addLayout(form);auto *note=new QLabel(tr("Component runs use a fresh results folder beside the model. Only supported model profiles can run. Previous results remain available if a component run fails or is stopped."),this);note->setWordWrap(true);layout->addWidget(note);
    message_=new QLabel(model_->error(),this);message_->setWordWrap(true);message_->setObjectName("executionError");layout->addWidget(message_);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);connect(buttons,&QDialogButtonBox::accepted,this,&ComponentExecutionDialog::apply);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addWidget(buttons);
    auto refresh=[this]{const bool component=mode_->currentData()!="native";program_->setEnabled(component);components_->setEnabled(component);capacity_->setEnabled(mode_->currentData()=="split_material");};
    connect(mode_,&QComboBox::currentIndexChanged,this,refresh);refresh();
    connect(model_,&QObject::destroyed,this,&QDialog::reject);
}
void ComponentExecutionDialog::apply(){
    auto s=model_->settings();s["mode"]=mode_->currentData().toString();s["capacity"]=capacity_->value();QString error;
    if(!model_->setSettings(s,&error)){message_->setText(error);return;}
    auto program=program_->text().trimmed();if(program.endsWith(".app"))program+="/Contents/MacOS/HydroCoupleComposer";
    QSettings settings;settings.setValue("SWMMVis/Components/ComposerProgram",program);settings.setValue("SWMMVis/Components/LibraryDirectory",components_->text().trimmed());accept();
}
