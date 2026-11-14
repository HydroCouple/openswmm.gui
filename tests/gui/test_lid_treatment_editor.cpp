// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_lid_treatment_editor.cpp
 * @brief     Tests for lid treatment editor.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/lidcontroleditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include <QListWidget>
#include "ui/widgets/treatmentexpressionedit.h"
#include "lid/lidcontrolregistry.h"
#include "layers/swmmmodellayer.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSplitter>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QtTest/QTest>
#include <QDir>
#include <cmath>
#include <memory>

/**
 *
 * @brief Abort the standalone GUI harness when a required condition fails.
 * @param ok Condition; true returns normally, false calls qFatal.
 * @param message Borrowed nonnull null-terminated diagnostic for the fatal log.
 * No recovery/cleanup return is available after failure.
 *
 */
static void check(bool ok, const char *message)
{
    if (!ok)
        qFatal("%s", message);
}

// Check the rendered curves themselves. Axes and the parameter-entry prompt also paint pixels.
static int curvePixels(const QImage &image, const QColor &color)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == color)
                ++count;
    return count;
}

/**
 *
 * @brief Run the standalone LID layer/treatment/Richards editor integration and screenshot
 * harness.
 * @param argc Process argument count; exactly three selects explicit input/output paths.
 * @param argv Borrowed argc-entry argument array; argv[1]/argv[2] are local-encoded
 * paths when argc==3, otherwise paths derive from SWMMVIS_GUI_TEST_DATA. Qt may
 * consume its own options during QApplication construction.
 * @return Zero by implicit main return after all checks; qFatal terminates on failure.
 * Requires a GUI platform, one-control fixture, expected translated button/tab text
 * and writable artifact directory. Opens editor.rpt/editor.out and overwrites three
 * PNG captures, edits layer counts/rates/expression/retention in memory and verifies
 * engine/registry reload. Directory creation and engine close status are unchecked;
 * no full application event loop is entered.
 *
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QDir data(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"));
    const QString input =
        argc == 3 ? QString::fromLocal8Bit(argv[1]) : data.filePath("lid_treatment_editor.inp");
    const QDir artifacts(argc == 3 ? QString::fromLocal8Bit(argv[2])
                                   : data.filePath("lid_treatment_editor_out"));
    QDir().mkpath(artifacts.absolutePath());
    auto engine = swmm_engine_create();
    check(engine, "Engine creation failed");
    const int opened = swmm_engine_open(
        engine, input.toUtf8().constData(), artifacts.filePath("editor.rpt").toUtf8().constData(),
        artifacts.filePath("editor.out").toUtf8().constData(), nullptr);
    if (opened != SWMM_OK)
        qFatal("Open failed: %d %s", opened, swmm_get_last_error_msg(engine));
    const auto saved = artifacts.filePath("edited-controls.inp").toUtf8();
    {
        openswmmvis::lid::LidControlRegistry registry;
        check(registry.loadFromEngine(engine) == 1, "Registry did not load the control");
        openswmmvis::ui::LidControlEditorDialog dialog(&registry, nullptr);
        dialog.show();
        app.processEvents();
        auto *count = dialog.findChild<QSpinBox *>("lidMediaLayerCount");
        auto *layers = dialog.findChild<QTableView *>("lidNodeLayers");
        auto *treatment = dialog.findChild<QTableView *>("lidLayerTreatment");
        check(count && layers && treatment, "Layer controls missing");
        layers->selectRow(1);
        app.processEvents();
        check(dialog.grab().save(artifacts.filePath("layer-physical-editor.png")),
              "Physical screenshot failed");
        count->setValue(7);
        app.processEvents();
        check(layers->model()->rowCount() == 8, "Layer count did not update");
        layers->selectRow(1);
        app.processEvents();
        check(treatment->model()->rowCount() == 1, "Selected layer treatment missing");
        for (auto *tabs : dialog.findChildren<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i) == "Pollutant treatment")
                    tabs->setCurrentIndex(i);
        const auto rate = treatment->model()->index(0, 2);
        auto *rateEditor = treatment->itemDelegateForColumn(2)->createEditor(
            treatment, QStyleOptionViewItem{}, rate);
        auto *spin = qobject_cast<QDoubleSpinBox *>(rateEditor);
        check(spin, "Rate does not use a numeric editor");
        spin->setValue(1.25);
        treatment->itemDelegateForColumn(2)->setModelData(spin, treatment->model(), rate);
        delete rateEditor;
        const auto expression = treatment->model()->index(0, 3);
        auto *expressionEditor = treatment->itemDelegateForColumn(3)->createEditor(
            treatment, QStyleOptionViewItem{}, expression);
        auto *edit = qobject_cast<openswmmvis::ui::TreatmentExpressionEdit *>(expressionEditor);
        check(edit, "Expression does not use the existing editor");
        bool validated = false;
        QObject::connect(edit, &openswmmvis::ui::TreatmentExpressionEdit::validationChanged,
                         [&](bool ok, const QString &, int) { validated = ok; });
        edit->setExpression("R = 0.35");
        edit->validateNow();
        check(validated, "Expression validation failed");
        treatment->itemDelegateForColumn(3)->setModelData(edit, treatment->model(), expression);
        delete expressionEditor;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == "Apply layers and treatment")
                button->click();
        app.processEvents();
        SWMM_LidLayerTreatment rule{};
        check(swmm_lid_node_treatment_get(engine, 0, 1, &rule) == SWMM_OK,
              "Treatment was not applied");
        check(rule.layer == 2 && std::abs(rule.decay_per_day - 1.25) < 1.e-12 &&
                  QString::fromUtf8(rule.expression) == "R = 0.35",
              "Rates or expression did not reach engine");
        check(dialog.grab().save(artifacts.filePath("layer-treatment-editor.png")),
              "Screenshot failed");
        auto *flow = dialog.findChild<QComboBox *>("lidFlowModel");
        auto *cells = dialog.findChild<QSpinBox *>("lidRichardsCells");
        auto *curves = dialog.findChild<QWidget *>("lidRetentionCurves");
        auto *splitter = dialog.findChild<QSplitter *>("main");
        check(flow && cells && curves && splitter, "Richards controls missing");
        check(!curves->isVisible(), "Existing formulation displayed Richards curves");
        check(flow->currentIndex() == 0, "Existing projects changed flow method");
        flow->setCurrentIndex(1);
        cells->setValue(4);
        const double values[] = {.03, 2, 1.6, .5, 1.e-4};
        for (int row = 1; row < layers->model()->rowCount(); ++row)
            for (int col = 10; col < 15; ++col)
                check(layers->model()->setData(layers->model()->index(row, col), values[col - 10]),
                      "Retention edit failed");
        QWidget *retentionPage = nullptr;
        for (auto *tabs : dialog.findChildren<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i) == "Retention and conductivity") {
                    tabs->setCurrentIndex(i);
                    retentionPage = tabs->widget(i);
                }
        dialog.resize(1180, 920);
        layers->selectRow(1);
        splitter->setSizes({170, 420, 570});
        app.processEvents();
        auto *alpha = dialog.findChild<QDoubleSpinBox *>("lidLayerParameter11");
        check(retentionPage && retentionPage->isAncestorOf(curves) && alpha &&
                  retentionPage->isAncestorOf(alpha) && alpha->isVisible() && curves->isVisible(),
              "Richards curves and inputs must share the active retention tab");
        check(curves->visibleRegion().contains(curves->rect()) && curves->width() >= 225 &&
                  alpha->mapTo(retentionPage, QPoint(alpha->width(), 0)).x() <=
                      curves->mapTo(retentionPage, QPoint()).x(),
              "The inline retention plot was clipped or displaced from beside the inputs");
        const QColor water("#287fb8"), conductivity("#c87818");
        const auto before = curves->grab().toImage();
        check(curvePixels(before, water) > 100 && curvePixels(before, conductivity) > 100,
              "Moisture and conductivity curves were not rendered for the selected media");
        alpha->setValue(0);
        app.processEvents();
        check(curvePixels(curves->grab().toImage(), water) == 0,
              "Invalid material retained the previous layer's curve");
        alpha->setFocus();
        alpha->selectAll();
        QTest::keyClicks(alpha, "12");
        QTest::keyClick(alpha, Qt::Key_Tab);
        app.processEvents();
        const auto edited = curves->grab().toImage();
        check(alpha->value() == 12 && edited != before && curvePixels(edited, water) > 100 &&
                  curvePixels(edited, conductivity) > 100,
              "Editing alpha did not redraw both draft curves before Apply");
        layers->selectRow(2);
        app.processEvents();
        check(alpha->value() == 2 && curvePixels(curves->grab().toImage(), conductivity) > 100,
              "Aggregate selection did not display its own retention curve");
        layers->selectRow(0);
        app.processEvents();
        check(curvePixels(curves->grab().toImage(), water) == 0,
              "Surface selection retained a porous layer's curve");
        layers->selectRow(1);
        check(alpha->value() == 12, "Switching layers lost the media retention edit");
        alpha->setValue(2);
        splitter->setSizes({170, 580, 410});
        app.processEvents();
        check(dialog.grab().save(artifacts.filePath("richards-editor.png")),
              "Richards screenshot failed");
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == "Apply layers and treatment")
                button->click();
        app.processEvents();
        SWMM_LidRichardsOptions options{};
        check(swmm_lid_richards_options_get(engine, 0, &options) == SWMM_OK && options.model == 1 &&
                  options.cells_per_layer == 4,
              "Richards mode did not reach engine");
        SWMM_LidRichardsMaterial material{};
        check(swmm_lid_richards_material_get(engine, 0, 1, &material) == SWMM_OK &&
                  material.alpha == 2 && material.specific_storage == 1.e-4,
              "Retention did not reach engine");
        dialog.close();
        openswmmvis::lid::LidControlRegistry reloaded;
        reloaded.loadFromEngine(engine);
        check(reloaded.providers().front()->flowOptions.model == 1 &&
                  reloaded.providers().front()->retention[1].alpha == 2,
              "Richards did not reload");
        check(reloaded.providers().front()->nodeLayers().size() == 8,
              "Applied count did not persist");
        check(reloaded.providers().front()->treatments[1].expression == "R = 0.35",
              "Applied expression did not persist");
    }
    swmm_engine_close(engine);
    swmm_engine_destroy(engine);
    {
        SWMMModelLayer model(input, nullptr);
        QList<QString> warnings, errors;
        check(model.loadModel(warnings, errors), "Could not open LID fixture in the GUI model");
        using openswmmvis::lid::LidControlRegistry;
        using openswmmvis::ui::LidControlEditorDialog;
        auto *registry = qobject_cast<LidControlRegistry *>(model.ensureLidControlRegistry());
        check(registry, "Missing GUI model registry");
        std::unique_ptr<LidControlEditorDialog> dialog(
            LidControlEditorDialog::createNew(registry, &model));
        dialog->show();
        app.processEvents();
        auto *storage = dialog->findChild<QDoubleSpinBox *>("lidField:surfStorage");
        check(storage && dialog->draftSession(), "Project-owned draft LID editor missing");
        const int standard = swmm_lid_index(model.engine(), "LID2");
        check(standard >= 0, "New LID missing from engine pickers");
        LidControlEditorDialog otherEditor(registry, &model);
        otherEditor.openForLidControl("LID2");
        storage->setValue(1.75);
        auto *type = dialog->findChild<QComboBox *>("lidField:type");
        check(type, "LID type field missing");
        type->setCurrentIndex(type->findData(5));
        app.processEvents();
        int code = -1;
        check(swmm_lid_get_type(model.engine(), standard, &code) == SWMM_OK && code == 0,
              "The type reached the model before Apply");
        check(dialog->draftSession()->apply(), "Apply refused the depth and type");
        app.processEvents();
        auto *otherType = otherEditor.findChild<QComboBox *>("lidField:type");
        check(otherType && otherType->currentText() == "Rain Barrel",
              "A second open editor did not follow the applied type change");
        check(swmm_lid_get_type(model.engine(), standard, &code) == SWMM_OK && code == 5,
              "Selecting Rain Barrel did not update the existing control");
        dialog->invokeNew();
        app.processEvents();
        auto *newLayered = dialog->findChild<QPushButton *>("newLayeredLid");
        check(newLayered, "New layered LID button missing");
        newLayered->click();
        app.processEvents();
        auto *layers = dialog->findChild<QListWidget *>("lidLiveLayerOrder");
        check(dialog->currentProvider()->name() == "LID4" && layers && layers->count() == 4,
              "New layered LID was not selected with complete layers");
        check(swmm_lid_get_type(model.engine(), swmm_lid_index(model.engine(), "LID4"), &code) == SWMM_OK && code == 8,
              "New layered LID was registered as a standard control");
        check(dialog->findChild<QPushButton *>("draftApply"), "Project-owned editor has no Apply");
        check(dialog->grab().save(artifacts.filePath("project-owned-draft-editor.png")), "Draft editor screenshot failed");
        model.flushDataObjectRegistries();
        check(swmm_model_write(model.engine(), saved.constData()) == SWMM_OK,
              "Could not save model");
    }
    engine = swmm_engine_create();
    check(engine, "Could not create engine for reopen");
    if (swmm_engine_open(engine, saved.constData(), artifacts.filePath("reopen.rpt").toUtf8(),
                         artifacts.filePath("reopen.out").toUtf8(), nullptr) != SWMM_OK)
        qFatal("Could not reopen saved controls: %s", swmm_get_last_error_msg(engine));
    {
        openswmmvis::lid::LidControlRegistry registry;
        check(registry.loadFromEngine(engine) == 4, "Control count changed on reopen");
        const auto *standard = registry.findByName("LID2");
        check(standard && standard->type() == 5 && standard->surfStorage() == 1.75,
              "Edited type or surface depth changed on reopen");
        for (const auto *name : {"LID4"}) {
            const auto *node = registry.findByName(name);
            check(node && node->type() == 8 && node->nodeLayers().size() == 4,
                  "Ordered type or layer stack changed on reopen");
        }
    }
    swmm_engine_close(engine);
    swmm_engine_destroy(engine);
    qInfo("LID editor: type selection, live Richards curves, ordered layers, treatment "
          "and save/reopen passed");
}
