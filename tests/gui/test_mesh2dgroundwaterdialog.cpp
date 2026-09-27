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

#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/dialogs/mesh2daquifermodel.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QComboBox>
#include <QTabWidget>

using openswmmvis::ui::Mesh2DGroundwaterDialog;
using openswmmvis::ui::Mesh2DAquiferModel;
using openswmmvis::ui::Mesh2DAquiferNodeModel;

namespace {
struct Engine {
    explicit Engine(bool building = true)
        : handle(building ? swmm_engine_new() : swmm_engine_create()) {}
    SWMM_Engine handle;
    ~Engine() { if (handle) swmm_engine_destroy(handle); }
};
} // namespace

class TestMesh2DGroundwaterDialog : public QObject
{
    Q_OBJECT

private slots:
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

    void optionsHaveAccessibleNamesAndKeyboardLabels()
    {
        Engine engine;
        QVERIFY(engine.handle);
        Mesh2DGroundwaterDialog dlg(engine.handle, nullptr,
                                    Mesh2DGroundwaterDialog::Page::Options);
        QVERIFY(!dlg.objectName().isEmpty());
        auto *form = dlg.findChild<QFormLayout *>();
        QVERIFY(form);
        int checked = 0;
        for (int row = 0; row < form->rowCount(); ++row) {
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
        QCOMPARE(checked, 7);
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
        QCOMPARE(writes.count(), 1);
        QVERIFY(nodes->isDirty());
        QCOMPARE(nodes->index(row, Mesh2DAquiferNodeModel::ColNode).data().toString(),
                 QStringLiteral("DraftNode"));
        QCOMPARE(nodes->index(row, Mesh2DAquiferNodeModel::ColKc).data().toDouble(), 1.0);
        QVERIFY(nodes->setData(nodes->index(row, Mesh2DAquiferNodeModel::ColDc), 1.0));
        dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(applied.count(), 1);
        QCOMPARE(writes.count(), 2);
        QVERIFY(!nodes->isDirty());
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
        QCOMPARE(applied.count(), 2);
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
