// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/lidcontroleditordialog.h"
#include "ui/widgets/treatmentexpressionedit.h"
#include "lid/lidcontrolregistry.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QDir>
#include <cmath>

static void check(bool ok, const char* message) { if (!ok) qFatal("%s", message); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QDir data(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"));
    const QString input=argc==3 ? QString::fromLocal8Bit(argv[1]) : data.filePath("lid_treatment_editor.inp");
    const QDir artifacts(argc==3 ? QString::fromLocal8Bit(argv[2]) : data.filePath("lid_treatment_editor_out"));
    QDir().mkpath(artifacts.absolutePath());
    auto engine=swmm_engine_create(); check(engine,"Engine creation failed");
    const int opened=swmm_engine_open(engine,input.toUtf8().constData(),artifacts.filePath("editor.rpt").toUtf8().constData(),artifacts.filePath("editor.out").toUtf8().constData(),nullptr);
    if(opened!=SWMM_OK) qFatal("Open failed: %d %s",opened,swmm_get_last_error_msg(engine));
    {
        openswmmvis::lid::LidControlRegistry registry;
        check(registry.loadFromEngine(engine)==1,"Registry did not load the control");
        openswmmvis::ui::LidControlEditorDialog dialog(&registry,nullptr);
        dialog.show(); app.processEvents();
        auto* count=dialog.findChild<QSpinBox*>("lidMediaLayerCount");
        auto* layers=dialog.findChild<QTableView*>("lidNodeLayers");
        auto* treatment=dialog.findChild<QTableView*>("lidLayerTreatment");
        check(count && layers && treatment,"Layer controls missing");
        layers->selectRow(1); app.processEvents();
        check(dialog.grab().save(artifacts.filePath("layer-physical-editor.png")),"Physical screenshot failed");
        count->setValue(7); app.processEvents();
        check(layers->model()->rowCount()==8,"Layer count did not update");
        layers->selectRow(1); app.processEvents();
        check(treatment->model()->rowCount()==1,"Selected layer treatment missing");
        for(auto* tabs:dialog.findChildren<QTabWidget*>())
            for(int i=0;i<tabs->count();++i) if(tabs->tabText(i)=="Pollutant treatment")tabs->setCurrentIndex(i);
        const auto rate=treatment->model()->index(0,2);
        auto* rateEditor=treatment->itemDelegateForColumn(2)->createEditor(treatment,QStyleOptionViewItem{},rate);
        auto* spin=qobject_cast<QDoubleSpinBox*>(rateEditor); check(spin,"Rate does not use a numeric editor");
        spin->setValue(1.25);
        treatment->itemDelegateForColumn(2)->setModelData(spin,treatment->model(),rate);
        delete rateEditor;
        const auto expression=treatment->model()->index(0,3);
        auto* expressionEditor=treatment->itemDelegateForColumn(3)->createEditor(treatment,QStyleOptionViewItem{},expression);
        auto* edit=qobject_cast<openswmmvis::ui::TreatmentExpressionEdit*>(expressionEditor);
        check(edit,"Expression does not use the existing editor");
        bool validated=false;
        QObject::connect(edit,&openswmmvis::ui::TreatmentExpressionEdit::validationChanged,[&](bool ok,const QString&,int){validated=ok;});
        edit->setExpression("R = 0.35"); edit->validateNow(); check(validated,"Expression validation failed");
        treatment->itemDelegateForColumn(3)->setModelData(edit,treatment->model(),expression);
        delete expressionEditor;
        for(auto* button:dialog.findChildren<QPushButton*>())
            if(button->text()=="Apply layers and treatment")button->click();
        app.processEvents();
        SWMM_LidLayerTreatment rule{};
        check(swmm_lid_node_treatment_get(engine,0,1,&rule)==SWMM_OK,"Treatment was not applied");
        check(rule.layer==2 && std::abs(rule.decay_per_day-1.25)<1.e-12 && QString::fromUtf8(rule.expression)=="R = 0.35","Rates or expression did not reach engine");
        check(dialog.grab().save(artifacts.filePath("layer-treatment-editor.png")),"Screenshot failed");
        dialog.close();
        openswmmvis::lid::LidControlRegistry reloaded;
        reloaded.loadFromEngine(engine);
        check(reloaded.providers().front()->nodeLayers().size()==8,"Applied count did not persist");
        check(reloaded.providers().front()->treatments[1].expression=="R = 0.35","Applied expression did not persist");
    }
    swmm_engine_close(engine); swmm_engine_destroy(engine);
    qInfo("Layer treatment editor: count, numeric delegate, expression validation, Apply and reload passed");
}
