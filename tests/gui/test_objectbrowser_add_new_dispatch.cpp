/**
 * @file      test_objectbrowser_add_new_dispatch.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BM.0-Add-New (2026-05-24) — pin the TimeseriesEditorDialog
 *         createNew factory contract that ObjectBrowserPanel's Add-New
 *         dispatch relies on after NewDataObjectDialog removal.
 *
 * The panel's static dispatch helpers (`hasComplexEditor`, `gapTooltipFor`)
 * + `launchAddNewEditor` are validated end-to-end at Snoopy Lagoon manual
 * verification time — they're thin switches whose contract is the gap
 * table in §L.BM.0-Add-New of the GUI implementation plan.
 *
 * This file focuses on the MVC plumbing that's most likely to regress:
 * the create-card validation, mode transition, registry side-effects, and
 * the chart-view rebind via setProvider.
 *
 * Coverage (10 cases):
 *   1. createNew starts in CreateNew mode.
 *   2. Create disabled while name is empty / whitespace.
 *   3. Create enabled for a unique non-empty name; pendingName mirrors it.
 *   4. Create disabled on a name collision (case-insensitive).
 *   5. submitCreateNew transitions to Edit mode + adds provider to registry.
 *   6. Closing without submit adds nothing.
 *   7. submitCreateNew is a safe no-op after the dialog leaves CreateNew.
 *   8. submitCreateNew applies the selected source mode to the new provider.
 *   9. chartView()->setProvider rebinds cleanly (and accepts nullptr).
 *   10. Edit-mode ctor path is unaffected (regression guard).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/panels/timeseriestablemodel.h"
#include "ui/widgets/timeserieseditchartview.h"

#include <QComboBox>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::ui::TimeseriesEditorDialog;

namespace
{

/** @brief Find the create-card name editor.
 * @param[in] dlg Required borrowed dialog searched recursively.
 * @return Borrowed first line edit whose direct parent is createCard, or nullptr when absent. */
QLineEdit *findCreateNameEdit(TimeseriesEditorDialog *dlg)
{
    const auto edits = dlg->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->parentWidget() && e->parentWidget()->objectName() == QStringLiteral("createCard"))
            return e;
    return nullptr;
}

/** @brief Find the exact-text Create button.
 * @param[in] dlg Required borrowed dialog searched recursively.
 * @return Borrowed first button labeled Create, or nullptr when absent. */
QPushButton *findCreateBtn(TimeseriesEditorDialog *dlg)
{
    const auto btns = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == QStringLiteral("Create"))
            return b;
    return nullptr;
}

/** @brief Find the create-card source-mode selector.
 * @param[in] dlg Required borrowed dialog searched recursively.
 * @return Borrowed first combo whose direct parent is createCard, or nullptr when absent. */
QComboBox *findSourceModeCombo(TimeseriesEditorDialog *dlg)
{
    const auto combos = dlg->findChildren<QComboBox *>();
    for (QComboBox *c : combos)
        if (c->parentWidget() && c->parentWidget()->objectName() == QStringLiteral("createCard"))
            return c;
    return nullptr;
}

} // namespace

class TestObjectBrowserAddNewDispatch : public QObject
{
    Q_OBJECT

private slots:

    /** @brief Verify the factory starts in CreateNew mode with name/button controls and creation disabled. */
    void createNew_StartsInCreateNewMode()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        QCOMPARE(int(dlg->mode()), int(TimeseriesEditorDialog::Mode::CreateNew));
        QVERIFY(findCreateNameEdit(dlg) != nullptr);
        QVERIFY(findCreateBtn(dlg) != nullptr);
        QVERIFY(!dlg->isCreateEnabled());
        delete dlg;
    }

    /** @brief Verify a whitespace-only draft name cannot enable creation. */
    void createNew_CreateDisabledForEmptyName()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("   ")); // whitespace = empty
        QVERIFY(!dlg->isCreateEnabled());
        delete dlg;
    }

    /** @brief Verify a unique nonempty draft name enables creation and updates pendingName. */
    void createNew_CreateEnabledForUniqueName()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("TS_test"));
        QVERIFY(dlg->isCreateEnabled());
        QCOMPARE(dlg->pendingName(), QStringLiteral("TS_test"));
        delete dlg;
    }

    /** @brief Verify both exact and case-insensitive registry name collisions disable creation. */
    void createNew_CreateDisabledOnNameCollision()
    {
        TimeseriesRegistry reg;
        QVERIFY(reg.create(QStringLiteral("RAIN_A")) != nullptr);
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("RAIN_A"));
        QVERIFY(!dlg->isCreateEnabled());
        // Case-insensitive collision per registry contract.
        edit->setText(QStringLiteral("rain_a"));
        QVERIFY(!dlg->isCreateEnabled());
        delete dlg;
    }

    /** @brief Verify submit changes to Edit mode and adds exactly one named provider with one registry signal. */
    void submitCreateNew_TransitionsToEditAndAddsProvider()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &TimeseriesRegistry::providerAdded);
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);

        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("RAIN_B"));
        QVERIFY(dlg->isCreateEnabled());

        dlg->submitCreateNew();

        QCOMPARE(int(dlg->mode()), int(TimeseriesEditorDialog::Mode::Edit));
        QCOMPARE(addedSpy.size(), 1);
        QCOMPARE(reg.providerCount(), 1);
        QVERIFY(reg.findByName(QStringLiteral("RAIN_B")) != nullptr);

        delete dlg;
    }

    /** @brief Verify deleting a dialog containing an unsubmitted name leaves registry count and added signals unchanged. */
    void closeWithoutSubmit_AddsNothingToRegistry()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &TimeseriesRegistry::providerAdded);
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);

        // Type a name but don't submit.
        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("RAIN_C"));

        delete dlg; // simulates close

        QCOMPARE(addedSpy.size(), 0);
        QCOMPARE(reg.providerCount(), 0);
        QVERIFY(reg.findByName(QStringLiteral("RAIN_C")) == nullptr);
    }

    /** @brief Verify submitting again after entering Edit mode does not create a duplicate provider. */
    void submitCreateNew_IsNoopAfterTransition()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        auto *edit = findCreateNameEdit(dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("RAIN_D"));
        dlg->submitCreateNew();
        QCOMPARE(reg.providerCount(), 1);

        // Second call: already in Edit, name already in registry → no-op.
        dlg->submitCreateNew();
        QCOMPARE(reg.providerCount(), 1);

        delete dlg;
    }

    /** @brief Verify the selected GeopackageObserved source mode reaches the newly created provider. */
    void submitCreateNew_AppliesSourceMode()
    {
        TimeseriesRegistry reg;
        QUndoStack stack;
        auto *dlg = TimeseriesEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);

        auto *edit = findCreateNameEdit(dlg);
        auto *combo = findSourceModeCombo(dlg);
        QVERIFY(edit && combo);
        edit->setText(QStringLiteral("RAIN_E"));

        // Pick GeopackageObserved (index 2).
        combo->setCurrentIndex(2);
        dlg->submitCreateNew();

        auto *p = reg.findByName(QStringLiteral("RAIN_E"));
        QVERIFY(p);
        QCOMPARE(int(p->sourceMode()), int(TimeseriesProvider::SourceMode::GeopackageObserved));

        delete dlg;
    }

    /** @brief Verify the chart can switch between registry-owned providers and clear its borrowed provider with nullptr. */
    void setProvider_OnChartView_RebindsCleanly()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &pa = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(pa.setAllPoints({
            {QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC), 1.0},
            {QDateTime(QDate(2026, 1, 1), QTime(1, 0), Qt::UTC), 2.0},
        }));
        TimeseriesProvider &pb = *reg.create(QStringLiteral("RAIN_B"));
        QVERIFY(pb.setAllPoints({
            {QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC), 5.0},
            {QDateTime(QDate(2026, 1, 1), QTime(1, 0), Qt::UTC), 6.0},
            {QDateTime(QDate(2026, 1, 1), QTime(2, 0), Qt::UTC), 7.0},
        }));

        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &pa);
        QVERIFY(dlg.chartView() != nullptr);
        QCOMPARE(dlg.chartView()->provider(), &pa);

        dlg.chartView()->setProvider(&pb);
        QCOMPARE(dlg.chartView()->provider(), &pb);

        // Rebinding to null clears the provider pointer.
        dlg.chartView()->setProvider(nullptr);
        QCOMPARE(dlg.chartView()->provider(), nullptr);
    }

    /** @brief Verify constructing for an existing provider starts in Edit mode with bound table/chart and no create-card controls. */
    void editModeCtor_StillWorks_RegressionGuard()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_F"));
        QVERIFY(p.setAllPoints({
            {QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC), 1.0},
            {QDateTime(QDate(2026, 1, 1), QTime(6, 0), Qt::UTC), 2.0},
        }));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        QCOMPARE(int(dlg.mode()), int(TimeseriesEditorDialog::Mode::Edit));
        QVERIFY(dlg.tableModel() != nullptr);
        QVERIFY(dlg.chartView() != nullptr);
        QCOMPARE(dlg.chartView()->provider(), &p);
        // No create-card in Edit mode.
        QVERIFY(findCreateNameEdit(&dlg) == nullptr);
        QVERIFY(findCreateBtn(&dlg) == nullptr);
    }
};

QTEST_MAIN(TestObjectBrowserAddNewDispatch)
#include "test_objectbrowser_add_new_dispatch.moc"
