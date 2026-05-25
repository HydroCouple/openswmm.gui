/*!
 * \file   test_pattern_editor_dialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.2 — PatternEditorDialog + PatternProvider +
 *         PatternRegistry + PatternFactorTableModel coverage.
 *
 * Coverage (12 cases):
 *   1. Provider factor count is 12 / 7 / 24 / 24 per type.
 *   2. setFactor validates index + non-negative, emits factorChanged.
 *   3. setAllFactors validates size + non-negative, emits factorsChanged.
 *   4. normalize(target) rescales sum to target; rejects sum-zero providers.
 *   5. setType resets factors to default 1.0 and changes factor count.
 *   6. Registry create / findByName (case-insensitive) / rename / remove.
 *   7. PatternFactorTableModel binds: row count matches provider; setData
 *      routes through provider; refresh on factorChanged.
 *   8. Dialog (Edit mode) populates list view + binds first provider.
 *   9. Dialog list selection swaps the bound provider in the table model.
 *  10. Normalize button rescales the active provider to the spin-box target.
 *  11. createNew factory starts in CreateNew mode; submit transitions to Edit.
 *  12. Adding a provider via the registry shows up in the list automatically.
 */

#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/panels/patternfactortablemodel.h"

#include <QChartView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <QUndoStack>

using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::pattern::PatternType;
using openswmmvis::ui::PatternEditorDialog;
using openswmmvis::ui::PatternFactorTableModel;

namespace {

QLineEdit *findCreateNameEdit(PatternEditorDialog *dlg)
{
    const auto edits = dlg->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->parentWidget() && e->parentWidget()->objectName()
            == QStringLiteral("patternCreateCard"))
            return e;
    return nullptr;
}

QPushButton *findCreateBtn(PatternEditorDialog *dlg)
{
    const auto btns = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == QStringLiteral("Create")
            && b->parentWidget()
            && b->parentWidget()->objectName() == QStringLiteral("patternCreateCard"))
            return b;
    return nullptr;
}

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

    // ── PatternProvider ─────────────────────────────────────────────────────

    void factorCounts_PerType()
    {
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Monthly), 12);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Daily),    7);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Hourly),  24);
        QCOMPARE(PatternProvider::factorCountFor(PatternType::Weekend), 24);

        PatternProvider mp(QStringLiteral("m"), PatternType::Monthly);
        QCOMPARE(mp.factorCount(), 12);
        // Default factor is 1.0.
        for (int i = 0; i < 12; ++i) QCOMPARE(mp.factor(i), 1.0);
    }

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
        for (int i = 0; i < 24; ++i) QCOMPARE(p.factor(i), 0.5);
    }

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

    void setType_ResetsFactors()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Monthly);
        QVERIFY(p.setFactor(3, 5.0));   // mutate one factor

        QSignalSpy typeSpy(&p, &PatternProvider::typeChanged);
        QSignalSpy factorsSpy(&p, &PatternProvider::factorsChanged);

        p.setType(PatternType::Hourly);
        QCOMPARE(int(p.type()), int(PatternType::Hourly));
        QCOMPARE(p.factorCount(), 24);
        for (int i = 0; i < 24; ++i) QCOMPARE(p.factor(i), 1.0);
        QCOMPARE(typeSpy.size(), 1);
        QCOMPARE(factorsSpy.size(), 1);
    }

    // ── PatternRegistry ─────────────────────────────────────────────────────

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
        QCOMPARE(reg.findByName(QStringLiteral("dwf")), p);   // case-insensitive

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

    void tableModel_BindsAndRefreshes()
    {
        PatternProvider p(QStringLiteral("p"), PatternType::Daily);
        PatternFactorTableModel m;
        m.setProvider(&p);

        QCOMPARE(m.rowCount(),    7);
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
        auto *lm = qobject_cast<QStandardItemModel *>(dlg.listView()->model());
        QVERIFY(lm && lm->rowCount() == 2);
        dlg.listView()->setCurrentIndex(lm->index(1, 0));
        QTest::qWait(0);   // allow currentChanged signal delivery

        QCOMPARE(dlg.currentProvider(), b);
        QCOMPARE(dlg.tableModel()->rowCount(), 7);
    }

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

    void createNew_TransitionsToEditAndAddsProvider()
    {
        PatternRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &PatternRegistry::providerAdded);

        auto *dlg = PatternEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        QCOMPARE(int(dlg->mode()),
                 int(PatternEditorDialog::Mode::CreateNew));

        auto *edit = findCreateNameEdit(dlg);
        auto *btn  = findCreateBtn(dlg);
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

    void registry_ProviderAdded_AppearsInList()
    {
        PatternRegistry reg;
        QUndoStack stack;
        PatternEditorDialog dlg(&reg, &stack);

        auto *lm = qobject_cast<QStandardItemModel *>(dlg.listView()->model());
        QVERIFY(lm);
        QCOMPARE(lm->rowCount(), 0);

        // Add a provider externally — the dialog's slot should sync the list.
        QVERIFY(reg.create(QStringLiteral("LATE"), PatternType::Monthly));
        QCOMPARE(lm->rowCount(), 1);
        QCOMPARE(lm->item(0)->text(), QStringLiteral("LATE"));
    }
};

QTEST_MAIN(TestPatternEditorDialog)
#include "test_pattern_editor_dialog.moc"
