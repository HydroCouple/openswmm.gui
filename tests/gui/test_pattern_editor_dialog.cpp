/**
 * @file      test_pattern_editor_dialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.2 — PatternEditorDialog + PatternProvider +
 *         PatternRegistry + PatternFactorTableModel coverage.
 *
 * Initial provider/model/editor scenarios (additional cases below cover lifetime,
 * CRUD, search, chart rendering, clipboard, preferences and native-engine mirroring):
 * - Provider factor count is 12 / 7 / 24 / 24 per type.
 * - setFactor validates index + non-negative, emits factorChanged.
 * - setAllFactors validates size and emits factorsChanged for an accepted vector.
 * - normalize(target) rescales sum to target; rejects sum-zero providers.
 * - setType resets factors to default 1.0 and changes factor count.
 * - Registry create / findByName (case-insensitive) / rename / remove.
 * - PatternFactorTableModel binds: row count matches provider; setData
 *      routes through provider; refresh on factorChanged.
 * - Dialog (Edit mode) populates list view + binds first provider.
 * - Dialog list selection swaps the bound provider in the table model.
 * - Normalize button rescales the active provider to the spin-box target.
 * - createNew factory starts in CreateNew mode; submit transitions to Edit.
 * - Adding a provider via the registry shows up in the list automatically.
 *
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "dialog_a11y_checks.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/panels/patternfactortablemodel.h"
#include "ui/widgets/patterneditchartview.h" // complete type for chartView()->grab()

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QApplication>
#include <QChartView>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLineSeries>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QScatterSeries>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QApplication>
#include <QUndoStack>

#include "ui/widgets/interactivechartview.h"

using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::pattern::PatternType;
using openswmmvis::ui::PatternEditorDialog;
using openswmmvis::ui::PatternFactorTableModel;

namespace
{

/**
 *
 * @brief Find the first descendant line edit directly parented by patternCreateCard.
 * @param dlg Borrowed non-null editor, retained throughout lookup.
 * @return Borrowed child or null; identity follows parent objectName, without testing
 * visibility, enabled state or the edit's own name. Caller must retain the dialog.
 *
 */
QLineEdit *findCreateNameEdit(PatternEditorDialog *dlg)
{
    const auto edits = dlg->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->parentWidget() &&
            e->parentWidget()->objectName() == QStringLiteral("patternCreateCard"))
            return e;
    return nullptr;
}

/**
 *
 * @brief Find the first Create button directly parented by patternCreateCard.
 * @param dlg Borrowed non-null editor, retained throughout lookup.
 * @return Borrowed child matching exact text and parent objectName, or null; lookup
 * neither clicks nor checks enabled/visible state. Valid only during child lifetime.
 *
 */
QPushButton *findCreateBtn(PatternEditorDialog *dlg)
{
    const auto btns = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == QStringLiteral("Create") && b->parentWidget() &&
            b->parentWidget()->objectName() == QStringLiteral("patternCreateCard"))
            return b;
    return nullptr;
}

/**
 *
 * @brief Return the first descendant double spin box as this fixture's normalization control.
 * @param dlg Borrowed non-null editor, retained throughout lookup.
 * @return Borrowed first spin box or null; no object-name or purpose check is made,
 * so the helper relies on the editor's current descendant ordering.
 *
 */
QDoubleSpinBox *findNormalizeSpin(PatternEditorDialog *dlg)
{
    const auto spins = dlg->findChildren<QDoubleSpinBox *>();
    return spins.isEmpty() ? nullptr : spins.first();
}

} // namespace

class TestPatternEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require registry destruction to disable/hide the live editor and empty its factor
     * table.
     * Creates a provider before deletion; does not invoke a stale callback or test deletion
     * of a standalone provider while its registry survives.
     *
     */
    void ownerRegistryDeletionInvalidatesEditor()
    {
        auto *registry = new PatternRegistry;
        auto *provider = registry->create(QStringLiteral("Owned"), PatternType::Monthly);
        QVERIFY(provider);
        QUndoStack stack;
        PatternEditorDialog dialog(registry, &stack);
        dialog.show();
        delete registry;
        QVERIFY2(!dialog.isEnabled(),
                 "A closed registry must disable the editor before stale callbacks can mutate it.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
    }

    /**
     *
     * @brief Require undo-stack destruction to disable/hide the editor and clear its rows while
     * preserving its provider.
     * The registry remains alive and must retain the exact provider identity afterward;
     * no undo/redo operation or post-destruction edit is attempted.
     *
     */
    void undoOwnerDeletionInvalidatesEditor()
    {
        PatternRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), PatternType::Monthly);
        QVERIFY(provider);
        auto *stack = new QUndoStack;
        PatternEditorDialog dialog(&registry, stack);
        dialog.show();
        delete stack;
        QVERIFY2(!dialog.isEnabled(), "The editor must not retain a dangling undo owner.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
        QVERIFY(registry.findByName(QStringLiteral("Owned")) == provider);
    }

    /**
     *
     * @brief Require a non-null factor table with an accessible name and a dialog description.
     * Checks nonempty accessibility strings; does not inspect their exact wording or
     * exercise a screen reader or keyboard navigation.
     *
     */
    void editorTableHasAccessiblePurpose()
    {
        PatternRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), PatternType::Monthly);
        QUndoStack stack;
        PatternEditorDialog dialog(&registry, &stack);
        auto *table = dialog.factorTable();
        QVERIFY(table);
        QVERIFY2(!table->accessibleName().isEmpty(),
                 "Data tables need a specific accessible name, beyond their row/column cells.");
        QVERIFY2(!dialog.accessibleDescription().isEmpty(),
                 "Live editors must explain that Close does not undo applied edits.");
    }

    /**
     *
     * @brief Require createNew to leave the existing provider unbound and expose zero factor
     * rows.
     * Explicitly deletes the heap dialog, then checks the registry still contains its one
     * original provider; no draft submission, factor change or undo action is performed.
     *
     */
    void newDraftDoesNotBindAnExistingObject()
    {
        PatternRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), PatternType::Monthly);
        QUndoStack stack;
        auto *dialog = PatternEditorDialog::createNew(&registry, &stack);
        const bool hasNoProvider = dialog->currentProvider() == nullptr;
        const int rows = dialog->tableModel()->rowCount();
        delete dialog;
        QVERIFY2(
            hasNoProvider,
            "Creating a new object must not silently expose the previously selected object for editing.");
        QCOMPARE(rows, 0);
        QCOMPARE(registry.providerCount(), 1);
        QVERIFY(registry.findByName(QStringLiteral("Owned")) == provider);
    }

    /**
     *
     * @brief Require rejection of a picker without an initial choice to return an empty name.
     * A queued callback rejects the active pattern dialog during modal execution. Requires
     * the original registry count to remain one; callback does not capture local objects.
     *
     */
    void cancelEmptyPickerDoesNotChooseExistingObject()
    {
        PatternRegistry registry;
        QVERIFY(registry.create(QStringLiteral("Owned"), PatternType::Monthly));
        QUndoStack stack;
        QTimer::singleShot(0, [] {
            if (auto *dialog =
                    qobject_cast<PatternEditorDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        const QString selected = PatternEditorDialog::pickPattern(&registry, &stack, {});
        QVERIFY2(
            selected.isEmpty(),
            "Closing an uncommitted new-object picker must not select an unrelated existing object.");
        QCOMPARE(registry.providerCount(), 1);
    }

    // ── PatternProvider ─────────────────────────────────────────────────────

    /**
     *
     * @brief Pin factor counts to monthly 12, daily 7, hourly/weekend 24 and monthly defaults to
     * one.
     * Only the monthly provider is instantiated; the other type counts use the static
     * query. Does not check calendars, unit conversion or persistence.
     *
     */
    void factorCounts_PerType()
    {
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Monthly), 12);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Daily), 7);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Hourly), 24);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Weekend), 24);

        PatternProvider mp(QStringLiteral("m"), PatternType::Monthly);
        QCOMPARE(mp.factorCount(), 12);
        // Default factor is 1.0.
        for (int i = 0; i < 12; ++i)
            QCOMPARE(mp.factor(i), 1.0);
    }

    /**
     *
     * @brief Require an accepted daily factor edit to emit once and out-of-range/negative edits
     * to be refused.
     * Checks stored 2.5 and cumulative rejection signal counts for index 99 and value -0.5;
     * no NaN/infinity, signal payload or post-refusal value assertion is included.
     *
     */
    void setFactor_ValidatesAndEmits()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Daily);
        QSignalSpy changed(&p, &PatternProvider::factorChanged);
        QSignalSpy rejected(&p, &PatternProvider::mutationRejected);

        QVERIFY(p.setFactor(0, 2.5));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(p.factor(0), 2.5);

        // Out-of-range index rejected.
        QVERIFY(!p.setFactor(99, 1.0));
        QCOMPARE(rejected.size(), 1);

        // Negative value rejected.
        QVERIFY(!p.setFactor(1, -0.5));
        QCOMPARE(rejected.size(), 2);
    }

    /**
     *
     * @brief Require wrong-size hourly vectors to fail without a change signal and 24 half
     * factors to succeed.
     * Checks exactly one successful change signal and all resulting values; this case does
     * not test negative or non-finite vector entries.
     *
     */
    void setAllFactors_ValidatesSize()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Hourly);
        QSignalSpy changed(&p, &PatternProvider::factorsChanged);

        // Wrong size rejected.
        QVERIFY(!p.setAllFactors({1.0, 2.0, 3.0}));
        QCOMPARE(changed.size(), 0);

        // Correct size accepted.
        QVector<double> ok(24, 0.5);
        QVERIFY(p.setAllFactors(ok));
        QCOMPARE(changed.size(), 1);
        for (int i = 0; i < 24; ++i)
            QCOMPARE(p.factor(i), 0.5);
    }

    /**
     *
     * @brief Require seven default daily factors to normalize to unit sum and then back to seven.
     * Checks factors/sum with absolute tolerance 1e-12; no negative target, non-finite
     * input or undo/persistence behavior is exercised.
     *
     */
    void normalize_RescalesToTarget()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Daily);
        // 7 ones = sum 7.0 → normalize(1) → each factor = 1/7.
        QVERIFY(p.normalize(1.0));
        const double expected = 1.0 / 7.0;
        for (int i = 0; i < 7; ++i)
            QVERIFY(qAbs(p.factor(i) - expected) < 1e-12);
        QVERIFY(qAbs(p.sumOfFactors() - 1.0) < 1e-12);

        // Normalize to 7 (avg = 1) returns to default.
        QVERIFY(p.normalize(7.0));
        for (int i = 0; i < 7; ++i)
            QVERIFY(qAbs(p.factor(i) - 1.0) < 1e-12);
    }

    /**
     *
     * @brief Require normalization of twelve zero monthly factors to fail with one rejection
     * signal.
     * Does not separately compare factors after refusal or exercise other invalid totals.
     *
     */
    void normalize_RefusesZeroSumProvider()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Monthly);
        // Zero out every factor.
        QVector<double> zeros(12, 0.0);
        QVERIFY(p.setAllFactors(zeros));
        QSignalSpy rejected(&p, &PatternProvider::mutationRejected);
        QVERIFY(!p.normalize(1.0));
        QCOMPARE(rejected.size(), 1);
    }

    /**
     *
     * @brief Require changing a mutated monthly provider to hourly to create 24 unit factors.
     * Checks exactly one typeChanged and factorsChanged signal, without asserting signal
     * payload/order or editing a same-type provider.
     *
     */
    void setType_ResetsFactors()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Monthly);
        QVERIFY(p.setFactor(3, 5.0)); // mutate one factor

        QSignalSpy typeSpy(&p, &PatternProvider::typeChanged);
        QSignalSpy factorsSpy(&p, &PatternProvider::factorsChanged);

        p.setType(PatternType::Hourly);
        QCOMPARE(int(p.type()), int(PatternType::Hourly));
        QCOMPARE(p.factorCount(), 24);
        for (int i = 0; i < 24; ++i)
            QCOMPARE(p.factor(i), 1.0);
        QCOMPARE(typeSpy.size(), 1);
        QCOMPARE(factorsSpy.size(), 1);
    }

    // ── PatternRegistry ─────────────────────────────────────────────────────

    /**
     *
     * @brief Exercise registry ownership and name lookup across create, rename, collision and
     * removal.
     * Checks case-insensitive find, duplicate-create and rename refusal, successful rename
     * lookup replacement, signal counts and final count after remove. Does not dereference
     * the removed provider, inspect signal payloads or attach an engine.
     *
     */
    void registry_CreateFindRenameRemove()
    {
        PatternRegistry reg;
        QSignalSpy addedSpy(&reg, &PatternRegistry::providerAdded);
        QSignalSpy renamedSpy(&reg, &PatternRegistry::providerRenamed);
        QSignalSpy removedSpy(&reg, &PatternRegistry::providerAboutToBeRemoved);

        auto *p = reg.create(QStringLiteral("DWF"), PatternType::Daily);
        QVERIFY(p);
        QCOMPARE(addedSpy.size(), 1);
        QCOMPARE(reg.providerCount(), 1);
        QCOMPARE(reg.findByName(QStringLiteral("DWF")), p);
        QCOMPARE(reg.findByName(QStringLiteral("dwf")), p); // case-insensitive

        // Duplicate name rejected.
        QVERIFY(reg.create(QStringLiteral("DWF"), PatternType::Monthly) == nullptr);
        QCOMPARE(reg.providerCount(), 1);

        // Rename with collision rejected.
        auto *q = reg.create(QStringLiteral("Other"), PatternType::Hourly);
        QVERIFY(q);
        QVERIFY(!reg.rename(q, QStringLiteral("DWF")));

        // Rename happy path.
        QVERIFY(reg.rename(p, QStringLiteral("DWF_NEW")));
        QCOMPARE(renamedSpy.size(), 1);
        QCOMPARE(reg.findByName(QStringLiteral("DWF_NEW")), p);
        QVERIFY(reg.findByName(QStringLiteral("DWF")) == nullptr);

        // Remove.
        reg.remove(p);
        QCOMPARE(removedSpy.size(), 1);
        QCOMPARE(reg.providerCount(), 1);
    }

    // ── PatternFactorTableModel ─────────────────────────────────────────────

    /**
     *
     * @brief Require a daily binding to expose seven rows/one column and forward accepted edits
     * to its provider.
     * Checks initial unit value, stored 3.5 and one dataChanged signal after editing row two;
     * negative input must return false. Does not check post-refusal signal count or undo.
     *
     */
    void tableModel_BindsAndRefreshes()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Daily);
        PatternFactorTableModel m;
        m.setProvider(&p);

        QCOMPARE(m.rowCount(), 7);
        QCOMPARE(m.columnCount(), 1);

        // Initial value.
        QCOMPARE(m.data(m.index(0, 0)).toDouble(), 1.0);

        // setData routes through provider.
        QSignalSpy dataChangedSpy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(m.setData(m.index(2, 0), 3.5));
        QCOMPARE(p.factor(2), 3.5);
        QCOMPARE(dataChangedSpy.size(), 1);

        // Negative value rejected by provider → setData returns false (no commit).
        // (provider returns false, model never emits dataChanged for this attempt)
        QVERIFY(!m.setData(m.index(0, 0), -1.0));
    }

    // ── PatternEditorDialog ─────────────────────────────────────────────────

    /**
     *
     * @brief Require edit-mode construction to supply list/table/model and bind the first monthly
     * provider.
     * Checks PAT_A and twelve rows with PAT_B also present; does not show the dialog or
     * change the selection in this case.
     *
     */
    void dialog_OpensAndBindsFirstProvider()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT_A"), PatternType::Monthly));
        QVERIFY(reg.create(QStringLiteral("PAT_B"), PatternType::Daily));

        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.listView() != nullptr);
        QVERIFY(dlg.factorTable() != nullptr);
        QVERIFY(dlg.tableModel() != nullptr);

        auto *cur = dlg.currentProvider();
        QVERIFY(cur != nullptr);
        QCOMPARE(cur->name(), QStringLiteral("PAT_A"));
        QCOMPARE(dlg.tableModel()->rowCount(), 12);
    }

    /**
     *
     * @brief Require selecting the second list entry to bind the daily provider and its seven
     * rows.
     * Maps the source index through an optional sorting proxy and delivers pending events;
     * checks original monthly binding first, without modifying factors or saving data.
     *
     */
    void dialog_ListSelectionRebindsTable()
    {
        PatternRegistry reg;
        auto *a = reg.create(QStringLiteral("PAT_A"), PatternType::Monthly);
        auto *b = reg.create(QStringLiteral("PAT_B"), PatternType::Daily);
        QVERIFY(a && b);

        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.currentProvider() == a);
        QCOMPARE(dlg.tableModel()->rowCount(), 12);

        // Programmatically select PAT_B (index 1).
        auto *lm = dlg.patternListModel();
        QVERIFY(lm && lm->rowCount() == 2);
        {
            auto *proxy = qobject_cast<QSortFilterProxyModel *>(dlg.listView()->model());
            const QModelIndex src = lm->index(1, 0);
            dlg.listView()->setCurrentIndex(proxy ? proxy->mapFromSource(src) : src);
        }
        QTest::qWait(0); // allow currentChanged signal delivery

        QCOMPARE(dlg.currentProvider(), b);
        QCOMPARE(dlg.tableModel()->rowCount(), 7);
    }

    /**
     *
     * @brief Require the dialog normalization action to rescale the active daily provider to unit
     * total.
     * Sets the first double spin box to one, invokes the public action and checks every
     * factor equals one seventh within 1e-9; no button click or undo is tested.
     *
     */
    void dialog_NormalizeRescalesActiveProvider()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT"), PatternType::Daily);
        QVERIFY(p);

        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QCOMPARE(dlg.currentProvider(), p);

        auto *spin = findNormalizeSpin(&dlg);
        QVERIFY(spin);
        spin->setValue(1.0);
        dlg.invokeNormalize();

        // 7 ones → normalize(1) → each = 1/7.
        const double expected = 1.0 / 7.0;
        for (int i = 0; i < 7; ++i)
            QVERIFY(qAbs(p->factor(i) - expected) < 1e-9);
    }

    /**
     *
     * @brief Require a valid new name to enable creation and submit exactly one provider before
     * entering Edit mode.
     * Checks initial CreateNew mode/disabled state, child controls, providerAdded count,
     * registry count and name lookup. Deletes the heap dialog; no factor persistence test.
     *
     */
    void createNew_TransitionsToEditAndAddsProvider()
    {
        PatternRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &PatternRegistry::providerAdded);

        auto *dlg = PatternEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        QCOMPARE(int(dlg->mode()), int(PatternEditorDialog::Mode::CreateNew));

        auto *edit = findCreateNameEdit(dlg);
        auto *btn = findCreateBtn(dlg);
        QVERIFY(edit && btn);
        QVERIFY(!dlg->isCreateEnabled());

        edit->setText(QStringLiteral("PAT_NEW"));
        QVERIFY(dlg->isCreateEnabled());

        dlg->submitCreateNew();
        QCOMPARE(int(dlg->mode()), int(PatternEditorDialog::Mode::Edit));
        QCOMPARE(addedSpy.size(), 1);
        QCOMPARE(reg.providerCount(), 1);
        QVERIFY(reg.findByName(QStringLiteral("PAT_NEW")) != nullptr);

        delete dlg;
    }

    /**
     *
     * @brief Require an externally created provider to immediately populate an initially empty
     * editor list.
     * Checks one row and its exact LATE label; does not select/edit it or test engine sync.
     *
     */
    void registry_ProviderAdded_AppearsInList()
    {
        PatternRegistry reg;
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        auto *lm = dlg.patternListModel();
        QVERIFY(lm);
        QCOMPARE(lm->rowCount(), 0);

        // Add a provider externally — the dialog's slot should sync the list.
        QVERIFY(reg.create(QStringLiteral("LATE"), PatternType::Monthly));
        QCOMPARE(lm->rowCount(), 1);
        QCOMPARE(lm->item(0)->text(), QStringLiteral("LATE"));
    }

    // ── CRUD: Rename / Delete (Slice BQ Phase 6.7.2-followup) ───────────────

    /**
     *
     * @brief Require renameCurrent to update provider identity, new/old lookups and the first
     * list label.
     * Uses one monthly provider and does not check undo history, case-only renames or
     * collision behavior, which has a separate case.
     *
     */
    void renameCurrent_UpdatesRegistryAndList()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT_OLD"), PatternType::Monthly);
        QVERIFY(p);
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        QVERIFY(dlg.renameCurrent(QStringLiteral("PAT_NEW")));
        QCOMPARE(p->name(), QStringLiteral("PAT_NEW"));
        QVERIFY(reg.findByName(QStringLiteral("PAT_NEW")) == p);
        QVERIFY(reg.findByName(QStringLiteral("PAT_OLD")) == nullptr);

        auto *lm = dlg.patternListModel();
        QVERIFY(lm);
        QCOMPARE(lm->item(0)->text(), QStringLiteral("PAT_NEW"));
    }

    /**
     *
     * @brief Require a rename to an existing provider name to return false and preserve PAT_A.
     * Pins initial selected provider identity; does not assert signal counts, case-folded
     * collisions or the undo stack contents.
     *
     */
    void renameCurrent_RejectsCollision()
    {
        PatternRegistry reg;
        auto *a = reg.create(QStringLiteral("PAT_A"), PatternType::Monthly);
        QVERIFY(a);
        QVERIFY(reg.create(QStringLiteral("PAT_B"), PatternType::Daily));
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        // dlg auto-selects first → PAT_A.
        QCOMPARE(dlg.currentProvider(), a);
        QVERIFY(!dlg.renameCurrent(QStringLiteral("PAT_B"))); // collision
        QCOMPARE(a->name(), QStringLiteral("PAT_A"));         // unchanged
    }

    /**
     *
     * @brief Require silent deletion of the explicitly selected second provider to retain only
     * PAT_A.
     * Maps the list index through its proxy, delivers selection changes, and checks registry
     * lookup/count and list label/count; no confirmation dialog or undo/redo is exercised.
     *
     */
    void deleteCurrent_RemovesFromRegistryAndList()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT_A"), PatternType::Monthly));
        auto *b = reg.create(QStringLiteral("PAT_B"), PatternType::Daily);
        QVERIFY(b);
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        // Select PAT_B explicitly, then delete it.
        auto *lm = dlg.patternListModel();
        QVERIFY(lm && lm->rowCount() == 2);
        {
            auto *proxy = qobject_cast<QSortFilterProxyModel *>(dlg.listView()->model());
            const QModelIndex src = lm->index(1, 0);
            dlg.listView()->setCurrentIndex(proxy ? proxy->mapFromSource(src) : src);
        }
        QTest::qWait(0);
        QCOMPARE(dlg.currentProvider(), b);

        dlg.deleteCurrentSilently();
        QCOMPARE(reg.providerCount(), 1);
        QVERIFY(reg.findByName(QStringLiteral("PAT_B")) == nullptr);
        QCOMPARE(lm->rowCount(), 1);
        QCOMPARE(lm->item(0)->text(), QStringLiteral("PAT_A"));
    }

    /**
     *
     * @brief Require invokeNew to enter draft mode and submitting PAT_ADDED to return to Edit.
     * Uses the create-card name edit and checks registry lookup after submit, without
     * asserting visual geometry, focus, signal counts or undo history.
     *
     */
    void invokeNew_RevealsCreateCard()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT_A"), PatternType::Monthly));
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QCOMPARE(int(dlg.mode()), int(PatternEditorDialog::Mode::Edit));

        dlg.invokeNew();
        QCOMPARE(int(dlg.mode()), int(PatternEditorDialog::Mode::CreateNew));

        // The create-card is now visible — submit a new pattern.
        auto *edit = findCreateNameEdit(&dlg);
        QVERIFY(edit);
        edit->setText(QStringLiteral("PAT_ADDED"));
        dlg.submitCreateNew();
        QCOMPARE(int(dlg.mode()), int(PatternEditorDialog::Mode::Edit));
        QVERIFY(reg.findByName(QStringLiteral("PAT_ADDED")) != nullptr);
    }

    // ── Slice BR-PAT: Duplicate / Search / Plot rework ──────────────────────

    /**
     *
     * @brief Require duplicate to copy daily type/factors independently and refuse invalid
     * source/new names.
     * Checks missing source, empty name and collision refusals, every clone factor, then
     * edits clone factor zero to 99 while requiring source factor zero to remain 0.1.
     *
     */
    void registry_Duplicate_ClonesNameAndFactors()
    {
        PatternRegistry reg;
        auto *src = reg.create(QStringLiteral("SRC"), PatternType::Daily);
        QVERIFY(src);
        QVector<double> facs{0.1, 0.5, 1.0, 1.5, 2.0, 0.8, 0.2};
        QVERIFY(src->setAllFactors(facs));

        // Bad src name → nullptr.
        QVERIFY(reg.duplicate(QStringLiteral("__missing__"), QStringLiteral("X")) == nullptr);
        // Empty new name → nullptr.
        QVERIFY(reg.duplicate(QStringLiteral("SRC"), QString()) == nullptr);
        // Colliding new name → nullptr.
        QVERIFY(reg.duplicate(QStringLiteral("SRC"), QStringLiteral("SRC")) == nullptr);

        // Happy path.
        auto *clone = reg.duplicate(QStringLiteral("SRC"), QStringLiteral("CLONE"));
        QVERIFY(clone);
        QCOMPARE(clone->name(), QStringLiteral("CLONE"));
        QCOMPARE(int(clone->type()), int(src->type()));
        QCOMPARE(clone->factorCount(), src->factorCount());
        for (int i = 0; i < src->factorCount(); ++i)
            QCOMPARE(clone->factor(i), src->factor(i));

        // Mutations on the clone don't bleed back into the source.
        QVERIFY(clone->setFactor(0, 99.0));
        QCOMPARE(src->factor(0), 0.1);
    }

    /**
     *
     * @brief Require duplicateCurrent to return/select a clone and copy its nondefault first
     * factor.
     * Checks provider pointer identity and value seven for a monthly source; does not
     * compare every factor, inspect the list index directly or undo duplication.
     *
     */
    void duplicateCurrent_SelectsCloneInList()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT"), PatternType::Monthly);
        QVERIFY(p);
        QVERIFY(p->setFactor(0, 7.0));

        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QCOMPARE(dlg.currentProvider(), p);

        auto *clone = dlg.duplicateCurrent(QStringLiteral("PAT_2"));
        QVERIFY(clone);
        QCOMPARE(dlg.currentProvider(), clone); // selection moved
        QCOMPARE(clone->factor(0), 7.0);        // factors copied
    }

    /**
     *
     * @brief Require case-insensitive AA search to show two of three patterns and clearing to
     * restore all.
     * Checks proxy row counts and unchanged registry count, without editing data or
     * asserting selection retention for a filtered-out provider.
     *
     */
    void search_FiltersListAndPreservesData()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("AAA"), PatternType::Monthly));
        QVERIFY(reg.create(QStringLiteral("BBB"), PatternType::Daily));
        QVERIFY(reg.create(QStringLiteral("aab"), PatternType::Hourly));

        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.searchEdit() != nullptr);

        // No filter → all 3 visible.
        QCOMPARE(dlg.listView()->model()->rowCount(), 3);

        // Filter "AA" — case-insensitive — matches "AAA" + "aab".
        dlg.searchEdit()->setText(QStringLiteral("AA"));
        QCOMPARE(dlg.listView()->model()->rowCount(), 2);

        // Clearing restores all rows; underlying registry untouched.
        dlg.searchEdit()->clear();
        QCOMPARE(dlg.listView()->model()->rowCount(), 3);
        QCOMPARE(reg.providerCount(), 3);
    }

    // ── Plot rework: step-line vertices + smooth-mode + markers ─────────────

    /**
     *
     * @brief Require monthly preview point counts to switch between twice-N step and N smooth
     * modes.
     * Toggles back to step and checks counts only; no x coordinates, interpolation shape,
     * marker state or stored settings are asserted.
     *
     */
    void preview_StepLineHasDoubleVertices()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT"), PatternType::Monthly);
        QVERIFY(p);
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        // Default = step-line; one slot pair per factor → 2·N points.
        QVERIFY(dlg.previewLineSeries());
        QCOMPARE(dlg.previewLineSeries()->count(), 2 * p->factorCount());

        // Smooth → one point per slot at the slot centre.
        dlg.setStepLinePreview(false);
        QCOMPARE(dlg.previewLineSeries()->count(), p->factorCount());

        // Toggle back.
        dlg.setStepLinePreview(true);
        QCOMPARE(dlg.previewLineSeries()->count(), 2 * p->factorCount());
    }

    /**
     *
     * @brief Require editing daily factor three to synchronously update its two step-series y
     * values.
     * Checks vertices six/seven equal 4.25; does not inspect their x coordinates, all other
     * vertices or deferred rendering.
     *
     */
    void preview_FactorEditRefreshesSeries()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT"), PatternType::Daily);
        QVERIFY(p);
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        // The step-line emits (i, f_i) and (i+1, f_i). For index 3, both
        // y-values should equal the new factor after the edit.
        QVERIFY(p->setFactor(3, 4.25));
        const auto pts = dlg.previewLineSeries()->points();
        QCOMPARE(pts.at(6).y(), 4.25);
        QCOMPARE(pts.at(7).y(), 4.25);
    }

    /**
     *
     * @brief Require default daily markers to contain N points and disabling to clear/hide the
     * scatter series.
     * Checks public visibility state and series visibility after disable; does not reenable
     * markers or compare coordinates.
     *
     */
    void preview_MarkersToggleAffectsScatter()
    {
        PatternRegistry reg;
        auto *p = reg.create(QStringLiteral("PAT"), PatternType::Daily);
        QVERIFY(p);
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        QVERIFY(dlg.previewMarkerSeries());
        QVERIFY(dlg.arePreviewMarkersVisible());
        QCOMPARE(dlg.previewMarkerSeries()->count(), p->factorCount());

        dlg.setPreviewMarkersVisible(false);
        QVERIFY(!dlg.arePreviewMarkersVisible());
        QCOMPARE(dlg.previewMarkerSeries()->count(), 0);
        QVERIFY(!dlg.previewMarkerSeries()->isVisible());
    }

    /**
     *
     * @brief Require invoking the chart-copy slot to put a non-null pixmap on the application
     * clipboard.
     * Clears the shared clipboard first and invokes the slot directly via Qt metadata;
     * does not click the toolbar or preserve prior clipboard contents.
     *
     */
    void copyChart_PutsPixmapOnClipboard()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT"), PatternType::Monthly));
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        QClipboard *clip = QApplication::clipboard();
        QVERIFY(clip);
        clip->clear();
        QVERIFY(clip->pixmap().isNull());

        // Drive the slot through the public chart view — the only public
        // affordance is the toolbar button click, but the slot is internal.
        // We invoke via QMetaObject so the test doesn't depend on the
        // button being findable by text (icon-only).
        QMetaObject::invokeMethod(&dlg, "onCopyChartClicked_", Qt::DirectConnection);
        QVERIFY(!clip->pixmap().isNull());
    }

    /**
     *
     * @brief Require a chart-view grab to save a nonempty PNG in a temporary directory.
     * Bypasses the file dialog and export slot; checks pixmap/save/file size only, without
     * reading the PNG back or asserting rendered chart content.
     *
     */
    void exportChart_RoundTripsPng()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT"), PatternType::Daily));
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString out = tmp.filePath(QStringLiteral("preview.png"));

        // The export slot pops QFileDialog; bypass it by exporting directly
        // via the same code path — render the chart view to a pixmap and
        // save. (Mirrors what onExportChartClicked_ does for PNG.)
        QPixmap pm = dlg.chartView()->grab();
        QVERIFY(!pm.isNull());
        QVERIFY(pm.save(out, "PNG"));
        QVERIFY(QFileInfo(out).size() > 0);
    }

    /**
     *
     * @brief Require smooth/no-marker choices to survive closing via the actual Close button and
     * reopening.
     * Redirects process-wide QSettings defaults/path to a temporary INI directory without
     * restoring them. Requires exposed windows, hidden-after-click state, restored toggles
     * and N smooth points. Does not assert marker series visibility after reopening.
     *
     */
    void persistence_PlotStyleSurvivesReopen()
    {
        // Use an isolated QSettings file so we don't stomp the user's prefs.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());

        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT"), PatternType::Monthly));
        QUndoStack stack;

        // Flip from default (step + markers) to smooth + no markers, then
        // leave through the Close BUTTON. closeEvent flushes the settings,
        // and on Qt >= 6.3 accept()/reject() skip closeEvent — so this pins
        // the button to close(), not just to "the dialog went away".
        {
            PatternEditorDialog dlg(&reg, &stack);
            QVERIFY(dlg.isStepLinePreview());
            QVERIFY(dlg.arePreviewMarkersVisible());
            dlg.setStepLinePreview(false);
            dlg.setPreviewMarkersVisible(false);
            dlg.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dlg));
            auto *box = dlg.findChild<QDialogButtonBox *>(QStringLiteral("pattern_closeBox"));
            QVERIFY(box);
            box->button(QDialogButtonBox::Close)->click();
            QTRY_VERIFY(!dlg.isVisible());
        }

        // Re-open — the restored toggles take effect.
        {
            PatternEditorDialog dlg2(&reg, &stack);
            QVERIFY(!dlg2.isStepLinePreview());
            QVERIFY(!dlg2.arePreviewMarkersVisible());
            // Series shape reflects the smooth setting (N points, no 2·N).
            auto *p = reg.findByName(QStringLiteral("PAT"));
            QVERIFY(p);
            QCOMPARE(dlg2.previewLineSeries()->count(), p->factorCount());
        }
    }

    // The editor used to be dismissable only from the title bar. The Close
    // box must hide it, and must not be the default button — Enter in a
    // factor cell has to commit the cell, never close the editor.
    /**
     *
     * @brief Require an accessible editor's Close button to be neither default nor auto-default
     * and to hide it.
     * Shows/exposes the window, invokes the shared dialog accessibility assertions and
     * clicks Close; does not send Enter from an edited factor cell or assert saved settings.
     *
     */
    void closeButton_HidesDialog()
    {
        PatternRegistry reg;
        QVERIFY(reg.create(QStringLiteral("PAT"), PatternType::Monthly));
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        auto *box = dlg.findChild<QDialogButtonBox *>(QStringLiteral("pattern_closeBox"));
        QVERIFY(box);
        auto *btn = box->button(QDialogButtonBox::Close);
        QVERIFY(btn);
        QVERIFY(!btn->isDefault());
        QVERIFY(!btn->autoDefault());
        swmmvis_test::assertDialogA11y(&dlg);

        btn->click();
        QTRY_VERIFY(!dlg.isVisible());
    }

    // Tutorial bug report (2026-10-17): swmm_pattern_add stores no factors
    // and factor edits never left the provider, so every pattern authored in
    // the GUI was written with zero multipliers. The registry now mirrors the
    // factors at create time and on every edit.
    /**
     *
     * @brief Require attached-engine creation and scalar/vector edits to retain 24 hourly native
     * factors.
     * Creates a scope-owned engine, checks native count, factor five at 1.25 and factor 23
     * after setting all values to 0.5. Registry/stack-local objects die before engine cleanup;
     * no input-file round trip or hydraulic simulation is performed.
     *
     */
    void patternFactorsReachEngineOnCreateAndEdit()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng);
        auto cleanup = qScopeGuard([eng] { swmm_engine_destroy(eng); });

        PatternRegistry reg;
        reg.attachEngine(eng);
        auto *p = reg.create(QStringLiteral("HP"), PatternType::Hourly);
        QVERIFY(p);
        const int idx = swmm_pattern_index(eng, "HP");
        QVERIFY(idx >= 0);
        int count = 0;
        QCOMPARE(swmm_pattern_get_factor_count(eng, idx, &count), SWMM_OK);
        QCOMPARE(count, 24);

        QVERIFY(p->setFactor(5, 1.25));
        double v = 0.0;
        QCOMPARE(swmm_pattern_get_factor(eng, idx, 5, &v), SWMM_OK);
        QCOMPARE(v, 1.25);

        QVERIFY(p->setAllFactors(QVector<double>(24, 0.5)));
        QCOMPARE(swmm_pattern_get_factor(eng, idx, 23, &v), SWMM_OK);
        QCOMPARE(v, 0.5);
        QCOMPARE(swmm_pattern_get_factor_count(eng, idx, &count), SWMM_OK);
        QCOMPARE(count, 24);
    }
};

QTEST_MAIN(TestPatternEditorDialog)
#include "test_pattern_editor_dialog.moc"
