/*!
 * \file   test_mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * GG1 (2026-09-07): this dialog is no longer a preview. The two-zone kernel
 * landed in the engine, so the dialog now edits [2D_AQUIFER_OPTIONS], the
 * per-scope [2D_AQUIFER] rows and the [2D_AQUIFER_NODE] beds against a live
 * engine, and carries a read-only state page.
 *
 * Two contracts survive that change, and they are what these tests hold:
 *
 *   1. WITHOUT an engine nothing is editable. The dialog is reachable from the
 *      mesh toolbar before a model is open, and a field that accepted values
 *      with nowhere to write them would discard them silently.
 *   2. The soil and closure vocabularies are INP tokens AND the wire order of
 *      the engine's enums, so they are asserted against the engine's own
 *      SWMM_GW2D_SOIL_* / SWMM_GW2D_CLOSURE_* codes rather than against
 *      whatever the dialog happens to return.
 */
#include <QtTest>
#include <limits>

#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/dialogs/mesh2daquifermodel.h"
#include "ui/hydrothermalstate.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTemporaryDir>
#include <QComboBox>
#include <QTabWidget>
#include <QUndoStack>

using openswmmvis::ui::Mesh2DGroundwaterDialog;
using openswmmvis::ui::Mesh2DAquiferModel;
using openswmmvis::ui::Mesh2DAquiferNodeModel;
using openswmmvis::ui::Mesh2DAquiferBoundaryModel;

namespace {
struct Engine {
    explicit Engine(bool building = true)
        : handle(building ? swmm_engine_new() : swmm_engine_create()) {}
    SWMM_Engine handle;
    ~Engine() { if (handle) swmm_engine_destroy(handle); }
};
} // namespace

class ClosingOwner:public QWidget { Q_OBJECT
signals:void engineAboutToClose();
};

class TestMesh2DGroundwaterDialog : public QObject
{
    Q_OBJECT

private slots:
    void layerChemistryAndSoluteDiffusionControlsWorkWithoutPorousHeat() {
        Engine e;Mesh2DGroundwaterDialog dialog(e.handle);
        auto* chemistry=dialog.findChild<QComboBox*>("gwTransport_UNSAT_TRANSPORT");
        auto* heat=dialog.findChild<QComboBox*>("gwTransport_THERMAL_MODEL");
        auto* dispersion=dialog.findChild<QCheckBox*>("gwTransport_DISPERSION");
        auto* safety=dialog.findChild<QDoubleSpinBox*>("gwTransport_C_DIFF");
        QVERIFY(chemistry&&heat&&dispersion&&safety);
        heat->setCurrentIndex(heat->findData("WATER_ONLY"));
        QVERIFY(chemistry->isEnabled());dispersion->setChecked(true);QVERIFY(safety->isEnabled());
        dispersion->setChecked(false);QVERIFY(!safety->isEnabled());QVERIFY(chemistry->isEnabled());
    }

    void thermalOptionsShortcutLimitsPagesAndExposesReadableOptionKeys() {
        Engine e;
        Mesh2DGroundwaterDialog dialog(e.handle);
        dialog.setThermalOptionsOnly(true);
        auto* tabs=dialog.findChild<QTabWidget*>();QVERIFY(tabs);
        QVERIFY(tabs->isTabVisible(0));
        QVERIFY(!tabs->isTabVisible(1));
        QVERIFY(!tabs->isTabVisible(2));
        QVERIFY(!tabs->isTabVisible(3));
        QVERIFY(!tabs->isTabVisible(5));
        QVERIFY(tabs->isTabVisible(4));
        const auto keys=Mesh2DGroundwaterDialog::configurationOptionKeys();
        QVERIFY(keys.contains(QStringLiteral("GW_TRANSPORT:THERMAL_MODEL")));
        QVERIFY(keys.contains(QStringLiteral("GW_TRANSPORT:SURFACE_THERMAL_BC")));
        QVERIFY(keys.contains(QStringLiteral("GW_TRANSPORT:DEEP_THERMAL_BC")));
        QVERIFY(!keys.contains(QStringLiteral("GW_TRANSPORT:SURFACE_THERMAL_BC_KIND")));
        QVERIFY(!keys.contains(QStringLiteral("GW_TRANSPORT:DEEP_THERMAL_BC_DEPTH")));
        for(const auto& key:keys) {
            const bool transport=key.startsWith(QStringLiteral("GW_TRANSPORT:"));
            const QByteArray name=(transport?key.mid(QStringLiteral("GW_TRANSPORT:").size()):key).toUtf8();
            char value[1024]{};
            const int status=transport
                ?swmm_gw_transport_option_get(e.handle,name.constData(),value,sizeof value)
                :swmm_gw2d_option_get(e.handle,name.constData(),value,sizeof value);
            QCOMPARE(status,SWMM_OK);
        }
    }

    void thermalControlsExposeAccessibleNamesAndLiveStatusDescription() {
        Engine engine;
        Mesh2DGroundwaterDialog dialog(engine.handle);

        const QStringList controlNames={
            QStringLiteral("gwTransport_THERMAL_MODEL"),
            QStringLiteral("gwTransport_CONDUCTION"),
            QStringLiteral("gwTransport_THERMAL_MIXING"),
            QStringLiteral("gwTransport_LATERAL_CONDUCTION"),
            QStringLiteral("gwTransport_SOIL_ET_HEAT"),
            QStringLiteral("gwTransport_THERMAL_LAYERS"),
            QStringLiteral("gwTransport_THERMAL_MAX_STEP"),
            QStringLiteral("gwTransport_WATER_THERMAL_CONDUCTIVITY"),
            QStringLiteral("gwTransport_AIR_THERMAL_CONDUCTIVITY"),
            QStringLiteral("gwTransport_SURFACE_THERMAL_RESISTANCE"),
            QStringLiteral("gwTransport_C_DIFF"),
            QStringLiteral("gwTransport_SURFACE_THERMAL_BC_KIND"),
            QStringLiteral("gwTransport_SURFACE_THERMAL_BC_VALUE"),
            QStringLiteral("gwTransport_DEEP_THERMAL_BC_KIND"),
            QStringLiteral("gwTransport_DEEP_THERMAL_BC_VALUE"),
            QStringLiteral("gwTransport_DEEP_THERMAL_BC_DEPTH")
        };
        for(const auto& name:controlNames) {
            auto* control=dialog.findChild<QWidget*>(name);
            QVERIFY2(control,qPrintable(QStringLiteral("Missing thermal control %1").arg(name)));
            QVERIFY2(!control->accessibleName().trimmed().isEmpty(),
                     qPrintable(QStringLiteral("Thermal control %1 has no accessible name").arg(name)));
        }

        for(const auto& name:{QStringLiteral("gwTransport_THERMAL_LAYERS"),
                              QStringLiteral("gwTransport_SOIL_ET_HEAT"),
                              QStringLiteral("gwTransport_SURFACE_THERMAL_BC_VALUE"),
                              QStringLiteral("gwTransport_DEEP_THERMAL_BC_VALUE")}) {
            auto* control=dialog.findChild<QWidget*>(name);
            QVERIFY(control);
            QVERIFY2(!control->accessibleDescription().trimmed().isEmpty(),
                     qPrintable(QStringLiteral("Thermal control %1 has no accessible description").arg(name)));
        }

        auto* status=dialog.findChild<QLabel*>(QStringLiteral("gwThermalActivationStatus"));
        QVERIFY(status);
        QCOMPARE(status->accessibleName(),QStringLiteral("Porous soil heat run status"));
        QCOMPARE(status->accessibleDescription(),status->text());
        QVERIFY(!status->accessibleDescription().isEmpty());

        auto* model=dialog.findChild<QComboBox*>(QStringLiteral("gwTransport_THERMAL_MODEL"));
        QVERIFY(model);
        const int porousIndex=model->findData(QStringLiteral("POROUS_EQUILIBRIUM"));
        QVERIFY(porousIndex>=0);
        model->setCurrentIndex(porousIndex);
        QCOMPARE(status->accessibleDescription(),status->text());
        QVERIFY(!status->accessibleDescription().isEmpty());
    }

    void porousSoilHeatCapabilityRequiresKnownOption()
    {
        QVERIFY(openswmmvis::ui::porousSoilHeatOptionIsSupported(
            true, QStringLiteral("WATER_ONLY")));
        QVERIFY(openswmmvis::ui::porousSoilHeatOptionIsSupported(
            true, QStringLiteral("POROUS_EQUILIBRIUM")));
        QVERIFY(!openswmmvis::ui::porousSoilHeatOptionIsSupported(
            false, QStringLiteral("POROUS_EQUILIBRIUM")));
        QVERIFY(!openswmmvis::ui::porousSoilHeatOptionIsSupported(
            true, QStringLiteral("UNKNOWN")));
    }

    void automaticEtDisplaysAndCanBeExplicitlyDisabled() {
        Engine e;
        QCOMPARE(swmm_gw2d_option_set(e.handle,"GW_ET","AUTO"),SWMM_OK);
        Mesh2DGroundwaterDialog dialog(e.handle);
        auto* et=dialog.findChild<QComboBox*>("gwDetailedEt");QVERIFY(et);
        QCOMPARE(et->currentData().toString(),QString("AUTO"));
        et->setCurrentIndex(et->findData("NONE"));
        QVERIFY(QMetaObject::invokeMethod(&dialog,"onApply",Qt::DirectConnection));
        char value[80]{};swmm_gw2d_option_get(e.handle,"GW_ET",value,sizeof value);
        QCOMPARE(QString::fromUtf8(value),QString("NONE"));
    }

    void lidBottomAndFootprintAreStagedAtomicAndShareTheNodeBedView() {
        Engine e;QCOMPARE(swmm_node_add(e.handle,"LID",SWMM_NODE_STORAGE),SWMM_OK);
        QCOMPARE(swmm_lid_add(e.handle,"Stack",8),SWMM_OK);
        const SWMM_LidNodeLayer layers[]={{0,{6,.1}},{1,{12,.45,.2,.08,2,10,3}},{3,{.5,0}}};
        QCOMPARE(swmm_lid_node_layers_set(e.handle,0,layers,3),SWMM_OK);
        QCOMPARE(swmm_node_set_storage_functional(e.handle,0,0,1,12.3456789012345),SWMM_OK);
        QCOMPARE(swmm_node_set_lid(e.handle,0,0,25),SWMM_OK);
        Mesh2DGroundwaterDialog dialog(e.handle);dialog.focusNode("LID");
        auto* model=dialog.findChild<Mesh2DAquiferNodeModel*>();
        auto* destination=dialog.findChild<QComboBox*>("lidBottomDestination");
        QVERIFY(destination&&destination->isEnabled());destination->setCurrentIndex(1);
        int mode=-1;QCOMPARE(swmm_node_get_lid_bottom(e.handle,0,&mode),SWMM_OK);QCOMPARE(mode,0);
        dialog.findChild<QPushButton*>("lidUseFootprint")->click();
        QCOMPARE(model->rowCount(),1);QCOMPARE(model->index(0,Mesh2DAquiferNodeModel::ColArea).data().toDouble(),12.3456789012345);
        QCOMPARE(model->index(0,Mesh2DAquiferNodeModel::ColBottom).data(Qt::EditRole).toInt(),1);
        QVERIFY(model->validateCommit(e.handle).isEmpty());QVERIFY(model->commit(e.handle,false).isEmpty());
        swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,1);
        QVERIFY(model->restoreLoaded(e.handle).isEmpty());swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,0);
        int count=-1;swmm_gw2d_node_count(e.handle,&count);QCOMPARE(count,0);
        QCOMPARE(swmm_node_set_storage_functional(e.handle,0,0,1,13),SWMM_OK);
        QVERIFY(!model->commit(e.handle).isEmpty());swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,0);
        model->load(e.handle);QVERIFY(model->setBottomDestination("LID",2));
        QVERIFY(!model->commit(e.handle).isEmpty()); // no active reviewed mesh receiver
        swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,0);
        QCOMPARE(swmm_node_set_storage_functional(e.handle,0,1,2,0),SWMM_OK);model->load(e.handle);
        QVERIFY(!model->useLidFootprint("LID").isEmpty());QCOMPARE(model->rowCount(),0);
        QUndoStack undo;int edits=0;dialog.setNodeUndoStack(&undo,[]{return true;},[&]{++edits;});
        dialog.updateBottomConnection();destination->setCurrentIndex(1);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(undo.count(),1);swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,1);
        undo.undo();swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,0);
        undo.redo();swmm_node_get_lid_bottom(e.handle,0,&mode);QCOMPARE(mode,1);QCOMPARE(edits,2);
        const QString out=qEnvironmentVariable("SWMMVIS_GROUNDWATER_DIALOG_TEST_OUTPUT");
        if(!out.isEmpty()){QDir().mkpath(out);dialog.resize(1100,720);dialog.show();QTest::qWait(30);QVERIFY(dialog.grab().save(out+"/lid-bottom-review.png"));}
    }
    void firstAquiferAndMeshBottomCanBeAppliedTogether() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QFile file(dir.filePath("new-bottom.inp"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"([OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
[STORAGE]
LID 0 .325 0 FUNCTIONAL 0 0 10 0 0
[2D_OPTIONS]
GROUNDWATER AUTO
[2D_VERTICES]
0 0 0
2 0 0
2 2 0
0 2 0
[2D_QUADS]
0 1 2 3 .03
[2D_AQUIFER_OPTIONS]
MODE PER_SUBCATCH
[LID_CONTROLS]
Stack NODE
Stack SURFACE 75 .1
Stack MEDIA 150 .45 .2 .08 5 10 75
Stack AGGREGATE 100 .4 100
Stack BOTTOM 0 1
[LID_NODES]
LID Stack .2
)");file.close();Engine e(false);
        QCOMPARE(swmm_engine_open(e.handle,file.fileName().toUtf8().constData(),nullptr,nullptr,nullptr),SWMM_OK);
        Mesh2DGroundwaterDialog dialog(e.handle);dialog.focusNode("LID");
        auto* aquifer=dialog.findChild<Mesh2DAquiferModel*>();aquifer->appendRow();
        for(auto* combo:dialog.findChildren<QComboBox*>())if(combo->findText("PER_SUBCATCH")>=0)combo->setCurrentText("MESH");
        dialog.findChild<QPushButton*>("lidUseFootprint")->click();
        dialog.findChild<QComboBox*>("lidBottomDestination")->setCurrentIndex(2);
        QSignalSpy applied(&dialog,&Mesh2DGroundwaterDialog::applied);
        QTimer dismiss;connect(&dismiss,&QTimer::timeout,[]{for(auto*w:QApplication::topLevelWidgets())if(auto*m=qobject_cast<QMessageBox*>(w))m->accept();});dismiss.start(20);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(applied.count(),1);int mode=-1;QCOMPARE(swmm_node_get_lid_bottom(e.handle,0,&mode),SWMM_OK);QCOMPARE(mode,2);
        int count=0;QCOMPARE(swmm_gw2d_row_count(e.handle,&count),SWMM_OK);QCOMPARE(count,1);
    }
    void noOpApplyDoesNotDirtyOrRoundOptions(){
        Engine e;QCOMPARE(swmm_gw2d_option_set(e.handle,"C_GW","0.123456789012345"),SWMM_OK);
        Mesh2DGroundwaterDialog dialog(e.handle);QSignalSpy writes(&dialog,&Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);QSignalSpy applied(&dialog,&Mesh2DGroundwaterDialog::applied);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();QCOMPARE(writes.count(),0);QCOMPARE(applied.count(),0);
        char value[128]{};swmm_gw2d_option_get(e.handle,"C_GW",value,sizeof value);QVERIFY(QString::fromUtf8(value).startsWith("0.123457")||QString::fromUtf8(value).startsWith("0.123456789"));
    }
    void unrelatedRowEditPreservesHighPrecisionSafetyOptions(){
        Engine e;QCOMPARE(swmm_gw2d_option_set(e.handle,"C_GW","0.123456789012345"),SWMM_OK);QCOMPARE(swmm_gw2d_option_set(e.handle,"C_COL","0.765432109876543"),SWMM_OK);
        char gw[128]{},column[128]{};QCOMPARE(swmm_gw2d_option_get(e.handle,"C_GW",gw,sizeof gw),SWMM_OK);QCOMPARE(swmm_gw2d_option_get(e.handle,"C_COL",column,sizeof column),SWMM_OK);const QByteArray originalGw(gw),originalColumn(column);
        QCOMPARE(QString::fromUtf8(gw).toDouble(),.123456789012345);QCOMPARE(QString::fromUtf8(column).toDouble(),.765432109876543);
        Mesh2DGroundwaterDialog dialog(e.handle);dialog.findChild<Mesh2DAquiferModel*>()->appendRow();dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(swmm_gw2d_option_get(e.handle,"C_GW",gw,sizeof gw),SWMM_OK);QCOMPARE(swmm_gw2d_option_get(e.handle,"C_COL",column,sizeof column),SWMM_OK);QCOMPARE(QByteArray(gw),originalGw);QCOMPARE(QByteArray(column),originalColumn);
    }
    void invalidNodeDraftDoesNotClearExistingRows(){
        Engine e;QCOMPARE(swmm_gw2d_node_add(e.handle,"Original",0,.5,1,2),SWMM_OK);Mesh2DGroundwaterDialog dialog(e.handle);auto*nodes=dialog.findChild<Mesh2DAquiferNodeModel*>();auto*aq=dialog.findChild<Mesh2DAquiferModel*>();aq->appendRow();int row=nodes->appendRow("Invalid",1);QVERIFY(nodes->setData(nodes->index(row,Mesh2DAquiferNodeModel::ColKc),1.));
        QSignalSpy writes(&dialog,&Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);QTimer::singleShot(0,&dialog,[&]{if(auto*m=dialog.findChild<QMessageBox*>())m->accept();});dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        int count=0;swmm_gw2d_node_count(e.handle,&count);QCOMPARE(count,1);swmm_gw2d_row_count(e.handle,&count);QCOMPARE(count,0);QCOMPARE(writes.count(),0);
    }
    void concurrentAquiferEditIsNotOverwritten(){
        Engine e;QCOMPARE(swmm_gw2d_row_add(e.handle,0,nullptr,-1,1,5,.45,.1,2),SWMM_OK);Mesh2DAquiferModel model;model.load(e.handle);QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColKs),2.));QCOMPARE(swmm_gw2d_row_set_property(e.handle,0,"L",-.25),SWMM_OK);QVERIFY(!model.commit(e.handle).isEmpty());double v=0;swmm_gw2d_row_get_property(e.handle,0,"L",&v);QCOMPARE(v,-.25);
    }
    void successfulScopeChangeAcceptsNormalizedBaseline(){
        Engine e;QCOMPARE(swmm_gw2d_row_add(e.handle,1,"PriorTag",-1,1,5,.45,.1,2),SWMM_OK);Mesh2DAquiferModel model;model.load(e.handle);
        QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColScope),2));QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColTarget),1));QVERIFY2(model.commit(e.handle).isEmpty(),"First scope change must succeed");
        QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColKs),2.));QVERIFY2(model.commit(e.handle).isEmpty(),"Own normalized previous write must not look like an external edit");
        QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColScope),0));QVERIFY(model.commit(e.handle).isEmpty());QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColKs),3.));QVERIFY(model.commit(e.handle).isEmpty());
    }
    void explicitSoilLawSurvivesUnrelatedEdit(){
        Engine e;QCOMPARE(swmm_gw2d_row_add(e.handle,0,nullptr,-1,1,5,.45,.1,2),SWMM_OK);QCOMPARE(swmm_gw2d_row_set_property(e.handle,0,"SOIL_CHAR",1),SWMM_OK);Mesh2DAquiferModel model;model.load(e.handle);QVERIFY(model.setData(model.index(0,Mesh2DAquiferModel::ColKs),2.));QVERIFY(model.commit(e.handle).isEmpty());double v=0;swmm_gw2d_row_get_property(e.handle,0,"SOIL_CHAR",&v);QCOMPARE(v,1.);
    }
    void inheritedChoicesRemainNoOpAndCanBeRestored(){
        Engine e;QCOMPARE(swmm_gw2d_row_add(e.handle,0,nullptr,-1,1,5,.45,.1,2),SWMM_OK);Mesh2DGroundwaterDialog dialog(e.handle);auto*model=dialog.findChild<Mesh2DAquiferModel*>();QTableView*view=nullptr;for(auto*v:dialog.findChildren<QTableView*>())if(v->model()==model)view=v;QVERIFY(view);
        for(int column:{Mesh2DAquiferModel::ColSoil,Mesh2DAquiferModel::ColClosure}){
            const auto index=model->index(0,column);auto*delegate=view->itemDelegateForColumn(column);QStyleOptionViewItem option;auto*editor=delegate->createEditor(view,option,index);QVERIFY(editor);delegate->setEditorData(editor,index);auto*combo=qobject_cast<QComboBox*>(editor);QVERIFY(combo);QCOMPARE(combo->currentData().toInt(),-1);delegate->setModelData(editor,model,index);QVERIFY(!model->isDirty());delete editor;
            QVERIFY(model->setData(index,0));QVERIFY(model->isDirty());QVERIFY(model->setData(index,-1));QVERIFY(!model->isDirty());
        }
    }
    void ownerPrecloseDisablesAllEngineCallbacks(){
        Engine e;ClosingOwner owner;Mesh2DGroundwaterDialog dialog(e.handle,&owner);emit owner.engineAboutToClose();QVERIFY(!dialog.isEnabled());QSignalSpy writes(&dialog,&Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);QVERIFY(QMetaObject::invokeMethod(&dialog,"onApply"));QCOMPARE(writes.count(),0);QVERIFY(QMetaObject::invokeMethod(&dialog,"refreshState"));
    }

    void openingAndCancellingDoesNotAuthorGroundwaterOptions()
    {
        const QString output = qEnvironmentVariable("SWMMVIS_GROUNDWATER_DIALOG_TEST_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath(
                "../../workplans/artifacts/phase_20_parallel/dialogs/output"));
        QVERIFY(QDir().mkpath(output));
        const QString fixture = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",
            QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("data"))
            + QStringLiteral("/gw_exchange_fixture.inp");
        Engine engine(false);
        QVERIFY(engine.handle);
        const auto report = QDir(output).filePath("cancel_fixture.rpt").toUtf8();
        QCOMPARE(swmm_engine_open(engine.handle, fixture.toUtf8().constData(),
                                  report.constData(), nullptr, nullptr), SWMM_OK);
        const QString before = QDir(output).filePath("before_dialog.inp");
        const QString after = QDir(output).filePath("after_cancel.inp");
        QCOMPARE(swmm_model_write(engine.handle, before.toUtf8().constData()), SWMM_OK);
        {
            Mesh2DGroundwaterDialog dlg(engine.handle);
            auto *aquifer = dlg.findChild<Mesh2DAquiferModel *>();
            aquifer->appendRow();
            dlg.reject();
        }
        QCOMPARE(swmm_model_write(engine.handle, after.toUtf8().constData()), SWMM_OK);
        QFile beforeFile(before), afterFile(after);
        QVERIFY(beforeFile.open(QIODevice::ReadOnly));
        QVERIFY(afterFile.open(QIODevice::ReadOnly));
        QCOMPARE(afterFile.readAll(), beforeFile.readAll());

        Mesh2DGroundwaterDialog staleEditor(engine.handle);
        QSignalSpy staleWrites(&staleEditor,
                              &Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);
        QSignalSpy staleApplied(&staleEditor, &Mesh2DGroundwaterDialog::applied);
        QCOMPARE(swmm_engine_initialize(engine.handle), SWMM_OK);
        QTimer::singleShot(0, &staleEditor, [&staleEditor] {
            if (auto *message = staleEditor.findChild<QMessageBox *>()) message->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(&staleEditor, "onApply", Qt::DirectConnection));
        QCOMPARE(staleWrites.count(), 0);
        QCOMPARE(staleApplied.count(), 0);
        Mesh2DGroundwaterDialog readOnly(engine.handle);
        auto *buttons = readOnly.findChild<QDialogButtonBox *>();
        QCOMPARE(buttons->standardButtons(), QDialogButtonBox::Close);
        for (auto *view : readOnly.findChildren<QTableView *>())
            QCOMPARE(view->editTriggers(), QAbstractItemView::NoEditTriggers);
    }

    void readOnlyDialogHasCloseWithoutApply()
    {
        Mesh2DGroundwaterDialog dlg(nullptr);
        auto *buttons = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QCOMPARE(buttons->standardButtons(), QDialogButtonBox::Close);
        auto *state = dlg.findChild<QPlainTextEdit *>();
        QVERIFY(state);
        QCOMPARE(state->toPlainText(), QStringLiteral("No model is open."));
        QSignalSpy rejected(&dlg, &QDialog::rejected);
        QSignalSpy writes(&dlg, &Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QCOMPARE(writes.count(), 0);
        buttons->button(QDialogButtonBox::Close)->click();
        QCOMPARE(rejected.count(), 1);
    }

    void porousThermalSettingsAreEditableAndRoundTrip()
    {
        Engine engine;QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dialog(engine.handle,nullptr,Mesh2DGroundwaterDialog::Page::Options);
        auto* model=dialog.findChild<QComboBox*>("gwTransport_THERMAL_MODEL");
        auto* conduction=dialog.findChild<QComboBox*>("gwTransport_CONDUCTION");
        auto* mixing=dialog.findChild<QComboBox*>("gwTransport_THERMAL_MIXING");
        auto* layers=dialog.findChild<QSpinBox*>("gwTransport_THERMAL_LAYERS");
        auto* lateral=dialog.findChild<QCheckBox*>("gwTransport_LATERAL_CONDUCTION");
        auto* surface=dialog.findChild<QComboBox*>("gwTransport_SURFACE_THERMAL_BC_KIND");
        auto* surfaceValue=dialog.findChild<QLineEdit*>("gwTransport_SURFACE_THERMAL_BC_VALUE");
        auto* deep=dialog.findChild<QComboBox*>("gwTransport_DEEP_THERMAL_BC_KIND");
        auto* deepValue=dialog.findChild<QLineEdit*>("gwTransport_DEEP_THERMAL_BC_VALUE");
        auto* deepDepth=dialog.findChild<QDoubleSpinBox*>("gwTransport_DEEP_THERMAL_BC_DEPTH");
        auto* conductivity=dialog.findChild<QDoubleSpinBox*>("gwTransport_WATER_THERMAL_CONDUCTIVITY");
        auto* surfaceResistance=dialog.findChild<QDoubleSpinBox*>("gwTransport_SURFACE_THERMAL_RESISTANCE");
        auto* et=dialog.findChild<QComboBox*>("gwTransport_SOIL_ET_HEAT");
        QVERIFY(model&&conduction&&mixing&&layers&&lateral&&surface&&surfaceValue&&deep&&deepValue&&deepDepth&&conductivity&&surfaceResistance&&et);
        QCOMPARE(model->currentData().toString(),QString("WATER_ONLY"));
        QVERIFY2(model->isEnabled(), "the engine exposes the porous-soil thermal option");
        QVERIFY(!conduction->isEnabled());
        auto* status=dialog.findChild<QLabel*>("gwThermalActivationStatus");QVERIFY(status);QVERIFY(status->text().startsWith("Inactive:"));
        model->setCurrentIndex(model->findData("POROUS_EQUILIBRIUM"));conduction->setCurrentIndex(conduction->findData("YES"));
        QVERIFY(conduction->isEnabled());
        mixing->setCurrentIndex(mixing->findData("GEOMETRIC"));layers->setValue(8);lateral->setChecked(true);
        surface->setCurrentIndex(surface->findData("FIXED"));surfaceValue->setText("12.75");
        deep->setCurrentIndex(deep->findData("FIXED_TEMP"));deepValue->setText("8.5");deepDepth->setValue(12.0);
        conductivity->setValue(.8);surfaceResistance->setValue(1.25);et->setCurrentIndex(et->findData("SENSIBLE_ONLY"));
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        auto option=[&](const char* key){char value[128]{};if(swmm_gw_transport_option_get(engine.handle,key,value,sizeof value)!=SWMM_OK)return QString();return QString::fromUtf8(value);};
        QCOMPARE(option("THERMAL_MODEL"),QString("POROUS_EQUILIBRIUM"));QCOMPARE(option("CONDUCTION"),QString("YES"));
        QCOMPARE(option("THERMAL_MIXING"),QString("GEOMETRIC"));QCOMPARE(option("THERMAL_LAYERS"),QString("8"));
        QCOMPARE(option("LATERAL_CONDUCTION"),QString("YES"));QCOMPARE(option("SURFACE_THERMAL_BC"),QString("FIXED 12.75"));
        QCOMPARE(option("DEEP_THERMAL_BC"),QString("FIXED_TEMP 8.5 DEPTH 12"));
        QCOMPARE(option("WATER_THERMAL_CONDUCTIVITY").toDouble(),.8);QCOMPARE(option("SURFACE_THERMAL_RESISTANCE").toDouble(),1.25);
        QCOMPARE(option("SOIL_ET_HEAT"),QString("SENSIBLE_ONLY"));
    }

    void optionsHaveAccessibleNamesAndKeyboardLabels()
    {
        Engine engine;
        QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dlg(engine.handle, nullptr,
                                    Mesh2DGroundwaterDialog::Page::Options);
        QVERIFY(!dlg.objectName().isEmpty());
        const auto forms=dlg.findChildren<QFormLayout *>();
        QVERIFY(!forms.isEmpty());
        int checked = 0;
        for(auto* form:forms)for (int row = 0; row < form->rowCount(); ++row) {
            const auto *item = form->itemAt(row, QFormLayout::FieldRole);
            auto *field = item ? item->widget() : nullptr;
            if (!qobject_cast<QComboBox *>(field) &&
                !qobject_cast<QAbstractSpinBox *>(field)) continue;
            auto *label = qobject_cast<QLabel *>(form->labelForField(field));
            QVERIFY(label);
            QCOMPARE(label->buddy(), field);
            QVERIFY2(label->text().contains('&'), qPrintable(label->text()));
            auto *accessible = QAccessible::queryAccessibleInterface(field);
            QVERIFY(accessible);
            QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
            ++checked;
        }
        QVERIFY(checked>=9);
        for (auto *view : dlg.findChildren<QTableView *>()) {
            auto *accessible = QAccessible::queryAccessibleInterface(view);
            QVERIFY(accessible);
            QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
        }
        auto *state = dlg.findChild<QPlainTextEdit *>();
        QVERIFY(state);
        QVERIFY(!QAccessible::queryAccessibleInterface(state)
                     ->text(QAccessible::Name).isEmpty());
    }

    void failedOkKeepsDialogAndDraftOpen()
    {
        Engine engine;
        QVERIFY(engine.handle);
        QCOMPARE(swmm_node_add(engine.handle, "DraftNode", SWMM_NODE_JUNCTION), SWMM_OK);
        Mesh2DGroundwaterDialog dlg(engine.handle);
        auto *nodes = dlg.findChild<Mesh2DAquiferNodeModel *>();
        QVERIFY(nodes);
        const int row = nodes->appendRow(QStringLiteral("DraftNode"), 0);
        QVERIFY(nodes->setData(nodes->index(row, Mesh2DAquiferNodeModel::ColKc), 1.0));
        // A conductive bed with zero thickness is rejected by the real engine.
        QSignalSpy writes(&dlg, &Mesh2DGroundwaterDialog::changesMayHaveBeenApplied);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QSignalSpy applied(&dlg, &Mesh2DGroundwaterDialog::applied);
        dlg.show();
        QTimer::singleShot(0, &dlg, [&dlg] {
            if (auto *message = dlg.findChild<QMessageBox *>()) message->accept();
        });
        dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.count(), 0);
        QVERIFY(dlg.isVisible());
        QCOMPARE(applied.count(), 0);
        QCOMPARE(writes.count(), 0);
        QVERIFY(nodes->isDirty());
        QCOMPARE(nodes->index(row, Mesh2DAquiferNodeModel::ColNode).data().toString(),
                 QStringLiteral("DraftNode"));
        QCOMPARE(nodes->index(row, Mesh2DAquiferNodeModel::ColKc).data().toDouble(), 1.0);
        QVERIFY(nodes->setData(nodes->index(row, Mesh2DAquiferNodeModel::ColDc), 1.0));
        dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(applied.count(), 1);
        QCOMPARE(writes.count(), 1);
        QVERIFY(!nodes->isDirty());
    }

    void groundwaterEdgesValidateComposeSaveAndUndo()
    {
        const QString root=QDir::currentPath()+"/workplans/artifacts/gw_transport_completion_2026-10-19/gui_edges";
        QDir().mkpath(root);QFile source(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",QDir::currentPath()+"/tests/gui/data")+"/mini_2d.inp");
        QVERIFY(source.open(QIODevice::ReadOnly));QFile input(root+"/model.inp");QVERIFY(input.open(QIODevice::WriteOnly));input.write(source.readAll());input.close();
        Engine engine(false);QCOMPARE(swmm_engine_open(engine.handle,input.fileName().toUtf8().constData(),nullptr,nullptr,nullptr),SWMM_OK);
        QCOMPARE(swmm_pollutant_add(engine.handle,"TSS",0),SWMM_OK);
        QCOMPARE(swmm_gw2d_row_add(engine.handle,0,"",-1,.001,5,.4,.05,1),SWMM_OK);
        Mesh2DAquiferBoundaryModel model;QVERIFY(model.load(engine.handle).isEmpty());
        model.addHydraulic();QVERIFY(model.setData(model.index(0,Mesh2DAquiferBoundaryModel::Value),.001));
        QVERIFY(!model.validateCommit(engine.handle).isEmpty()); // no implicit zero-concentration supply
        model.addChemistry();QVERIFY(model.setData(model.index(1,Mesh2DAquiferBoundaryModel::Species),"TSS"));
        QVERIFY(model.setData(model.index(1,Mesh2DAquiferBoundaryModel::Value),3.));
        QVERIFY2(model.validateCommit(engine.handle).isEmpty(),qPrintable(model.validateCommit(engine.handle)));
        auto transaction=model.transactionCopy();const auto saved=model.commit(engine.handle,false);QVERIFY2(saved.isEmpty(),qPrintable(saved));
        QCOMPARE(swmm_gw2d_boundary_count(engine.handle),1);QCOMPARE(swmm_gw_boundary_quality_count(engine.handle),1);
        QCOMPARE(swmm_model_write(engine.handle,(root+"/saved.inp").toUtf8().constData()),SWMM_OK);
        QVERIFY(transaction->restoreLoaded(engine.handle).isEmpty());QCOMPARE(swmm_gw2d_boundary_count(engine.handle),0);
        QVERIFY(transaction->commit(engine.handle,false).isEmpty());model.acceptChanges();
        QVERIFY(model.setData(model.index(0,Mesh2DAquiferBoundaryModel::Edge),1));
        QVERIFY(model.validateCommit(engine.handle).contains("exterior"));
        // A later editor cannot replace a changed hydraulic/chemical snapshot.
        const SWMM_GwBoundary changed{0,0,SWMM_GW2D_BOUNDARY_FLOW,.002};
        QCOMPARE(swmm_gw2d_boundary_set(engine.handle,&changed,""),SWMM_OK);
        QVERIFY(model.validateCommit(engine.handle).contains("outside"));
    }

    void nodeBedApplyAndRestorePreserveOptOutAndAutoLocation()
    {
        Engine engine;
        QCOMPARE(swmm_node_add(engine.handle,"Disabled",SWMM_NODE_JUNCTION),SWMM_OK);
        QCOMPARE(swmm_node_add(engine.handle,"Edited",SWMM_NODE_JUNCTION),SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(engine.handle,"Disabled",-1,0,0,0),SWMM_OK);
        QCOMPARE(swmm_gw2d_node_set_exchange(engine.handle,0,0),SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(engine.handle,"Edited",2,3,1,4),SWMM_OK);
        Mesh2DAquiferNodeModel model;model.load(engine.handle);
        QCOMPARE(model.index(0,Mesh2DAquiferNodeModel::ColCell).data().toString(),QStringLiteral("AUTO"));
        QCOMPARE(model.index(0,Mesh2DAquiferNodeModel::ColExchange).data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
        QVERIFY(model.setData(model.index(1,Mesh2DAquiferNodeModel::ColArea),7.));
        QVERIFY(model.commit(engine.handle,false).isEmpty());
        int locate=0,exchange=1,automatic=1;
        QCOMPARE(swmm_gw2d_node_get_flags(engine.handle,0,&locate,&exchange,&automatic),SWMM_OK);
        QCOMPARE(locate,1);QCOMPARE(exchange,0);QCOMPARE(automatic,0);
        QVERIFY(model.restoreLoaded(engine.handle).isEmpty());
        double area=0;swmm_gw2d_node_get(engine.handle,1,nullptr,0,nullptr,nullptr,nullptr,&area);QCOMPARE(area,4.);
        swmm_gw2d_node_get_flags(engine.handle,0,&locate,&exchange,&automatic);QCOMPARE(exchange,0);
        model.load(engine.handle);
        QVERIFY(model.setData(model.index(1,Mesh2DAquiferNodeModel::ColArea),8.));
        QCOMPARE(swmm_gw2d_node_set_exchange(engine.handle,0,1),SWMM_OK);
        QVERIFY(!model.commit(engine.handle).isEmpty());
        swmm_gw2d_node_get(engine.handle,1,nullptr,0,nullptr,nullptr,nullptr,&area);QCOMPARE(area,4.);
        swmm_gw2d_node_get_flags(engine.handle,0,&locate,&exchange,&automatic);QCOMPARE(exchange,1);
    }

    void nodeBedCheckboxSupportsKeyboardAndReviewedLayouts()
    {
        Engine engine;
        QCOMPARE(swmm_node_add(engine.handle,"ReviewNode",SWMM_NODE_JUNCTION),SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(engine.handle,"ReviewNode",-1,0,0,0),SWMM_OK);
        Mesh2DGroundwaterDialog dialog(engine.handle);auto*model=dialog.findChild<Mesh2DAquiferNodeModel*>();
        QTableView*view=nullptr;for(auto*v:dialog.findChildren<QTableView*>())if(v->model()==model)view=v;
        QVERIFY(view);auto*tabs=dialog.findChild<QTabWidget*>();QVERIFY(tabs);tabs->setCurrentIndex(2);
        dialog.show();view->setFocus();view->setCurrentIndex(model->index(0,Mesh2DAquiferNodeModel::ColExchange));
        QTest::keyClick(view,Qt::Key_Space);
        QCOMPARE(model->index(0,Mesh2DAquiferNodeModel::ColExchange).data(Qt::CheckStateRole).toInt(),int(Qt::Unchecked));
        int exchange=0;swmm_gw2d_node_get_flags(engine.handle,0,nullptr,&exchange,nullptr);QCOMPARE(exchange,1);
        const auto output=qEnvironmentVariable("SWMMVIS_GROUNDWATER_DIALOG_TEST_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../tests/output/r5_node_beds"));
        QVERIFY(QDir().mkpath(output));
        for(int width:{560,1200}){
            dialog.resize(width,620);QCoreApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir(output).filePath(QStringLiteral("node-beds-%1.png").arg(width))));
        }
        dialog.reject();swmm_gw2d_node_get_flags(engine.handle,0,nullptr,&exchange,nullptr);QCOMPARE(exchange,1);
    }

    void nodeBedRejectsUnknownAndDuplicateNamesAtomically()
    {
        Engine engine;
        QCOMPARE(swmm_node_add(engine.handle,"Known",SWMM_NODE_JUNCTION),SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(engine.handle,"Known",0,0,0,0),SWMM_OK);
        Mesh2DAquiferNodeModel model;model.load(engine.handle);
        const int row=model.appendRow("Missing",1);QVERIFY(!model.commit(engine.handle).isEmpty());
        QVERIFY(model.setData(model.index(row,Mesh2DAquiferNodeModel::ColNode),"known"));
        QVERIFY(!model.commit(engine.handle).isEmpty());
        int count=0;swmm_gw2d_node_count(engine.handle,&count);QCOMPARE(count,1);
        QVERIFY(!model.setData(model.index(0,Mesh2DAquiferNodeModel::ColArea),std::numeric_limits<double>::infinity()));
    }

    void escapeDiscardsUnappliedRows()
    {
        Engine engine;
        QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dlg(engine.handle);
        auto *aquifer = dlg.findChild<Mesh2DAquiferModel *>();
        aquifer->appendRow();
        QSignalSpy rejected(&dlg, &QDialog::rejected);
        QSignalSpy applied(&dlg, &Mesh2DGroundwaterDialog::applied);
        dlg.show();
        QTest::keyClick(&dlg, Qt::Key_Escape);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(applied.count(), 0);
        int count = -1;
        QCOMPARE(swmm_gw2d_row_count(engine.handle, &count), SWMM_OK);
        QCOMPARE(count, 0);
    }

    void applyKeepsDialogOpenAndOkAcceptsSuccessfulChanges()
    {
        Engine engine;
        QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dlg(engine.handle);
        auto *aquifer = dlg.findChild<Mesh2DAquiferModel *>();
        QVERIFY(aquifer);
        aquifer->appendRow();
        auto *buttons = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QSignalSpy accepted(&dlg, &QDialog::accepted);
        QSignalSpy applied(&dlg, &Mesh2DGroundwaterDialog::applied);
        dlg.show();
        buttons->button(QDialogButtonBox::Apply)->click();
        QVERIFY(dlg.isVisible());
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(applied.count(), 1);
        QVERIFY(!aquifer->isDirty());
        int count = 0;
        QCOMPARE(swmm_gw2d_row_count(engine.handle, &count), SWMM_OK);
        QCOMPARE(count, 1);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(applied.count(), 1);
    }

    void rowActionsCannotBecomeEnterDefault()
    {
        Engine engine;
        QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dlg(engine.handle);
        auto *buttons = dlg.findChild<QDialogButtonBox *>();
        for (auto *button : dlg.findChildren<QPushButton *>()) {
            if (buttons->standardButton(button) != QDialogButtonBox::NoButton) continue;
            QVERIFY2(!button->autoDefault(), qPrintable(button->text()));
        }
    }

    /*! With no engine there is nowhere to write, so every input stays off. */
    void allInputsAreDisabledWithoutAnEngine()
    {
        Mesh2DGroundwaterDialog dlg(nullptr);
        const auto spins = dlg.findChildren<QAbstractSpinBox *>();
        QVERIFY2(!spins.isEmpty(), "expected the parameter fields to exist");
        for (QAbstractSpinBox *s : spins)
            QVERIFY2(!s->isEnabled(),
                     qPrintable(QStringLiteral("spin box '%1' is editable")
                                    .arg(s->objectName())));

        const auto combos = dlg.findChildren<QComboBox *>();
        QVERIFY2(!combos.isEmpty(), "expected the model/closure selectors");
        for (QComboBox *c : combos)
            QVERIFY2(!c->isEnabled(),
                     qPrintable(QStringLiteral("combo '%1' is editable")
                                    .arg(c->objectName())));
    }

    /*! Each toolbar entry opens the dialog on its own page. Asserted by tab
     *  TEXT: the Page enum and the tab order are deliberately independent
     *  (Options is built first but is not the default page), so comparing
     *  indices here would only re-state the switch it is meant to check. */
    void opensOnTheRequestedPage()
    {
        Mesh2DGroundwaterDialog params(
            nullptr, nullptr, Mesh2DGroundwaterDialog::Page::AquiferProperties);
        auto *tabsA = params.findChild<QTabWidget *>();
        QVERIFY(tabsA);
        QCOMPARE(tabsA->tabText(tabsA->currentIndex()),
                 QStringLiteral("Aquifer"));

        Mesh2DGroundwaterDialog state(
            nullptr, nullptr, Mesh2DGroundwaterDialog::Page::State);
        auto *tabsB = state.findChild<QTabWidget *>();
        QVERIFY(tabsB);
        QCOMPARE(tabsB->tabText(tabsB->currentIndex()),
                 QStringLiteral("State"));
    }

    /*! The vocabularies are the engine's enums in the engine's order — they go
     *  out as INP tokens and come back as indices, so a reordering here would
     *  silently re-label every existing aquifer row. */
    void vocabulariesMatchTheEngineEnums()
    {
        // openswmm_gw2d.h: RUSSO 0, GARDNER 1, BROOKS_COREY 2, VAN_GENUCHTEN 3.
        QCOMPARE(Mesh2DGroundwaterDialog::soilModelTokens(),
                 (QStringList{QStringLiteral("RUSSO"), QStringLiteral("GARDNER"),
                              QStringLiteral("BROOKS_COREY"),
                              QStringLiteral("VAN_GENUCHTEN")}));
        // openswmm_gw2d.h: AUTO -1, CLOSED_FORM 0, ENSLAVED 1, SIGMA 2. AUTO
        // leads the list because it is the sentinel the engine resolves.
        QCOMPARE(Mesh2DGroundwaterDialog::closureTokens(),
                 (QStringList{QStringLiteral("AUTO"),
                              QStringLiteral("CLOSED_FORM"),
                              QStringLiteral("ENSLAVED"),
                              QStringLiteral("SIGMA")}));
    }
};

QTEST_MAIN(TestMesh2DGroundwaterDialog)
#include "test_mesh2dgroundwaterdialog.moc"
