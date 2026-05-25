/*!
 * \file   test_curve_editor_dialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.1 — CurveEditorDialog + CurveProvider +
 *         CurveRegistry + CurvePointTableModel coverage.
 *
 * Coverage (12 cases):
 *   1. Provider rejects non-monotone setAllPoints.
 *   2. setYAt validates index; emits pointsChanged.
 *   3. setPointAt enforces strict-monotone with neighbours.
 *   4. insertPoint inserts at sorted X position; rejects equal-X collision.
 *   5. removePointsAt safely handles duplicate + reverse-sorted indices.
 *   6. setType emits typeChanged; does NOT clear points.
 *   7. Registry create / findByName (case-insensitive) / rename / remove.
 *   8. CurvePointTableModel binds; setData routes through provider; refresh.
 *   9. Dialog opens, binds first provider, table reflects points.
 *  10. Dialog list selection swaps the bound provider in the table model.
 *  11. createNew factory starts in CreateNew mode; submit transitions to Edit.
 *  12. Add Row appends + Delete Rows removes selected.
 */

#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/panels/curvepointtablemodel.h"

#include <QChartView>
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <QUndoStack>

using openswmmvis::curve::CurvePoint;
using openswmmvis::curve::CurveProvider;
using openswmmvis::curve::CurveRegistry;
using openswmmvis::curve::CurveType;
using openswmmvis::ui::CurveEditorDialog;
using openswmmvis::ui::CurvePointTableModel;

namespace {

QLineEdit *findCreateNameEdit(CurveEditorDialog *dlg)
{
    const auto edits = dlg->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->parentWidget() && e->parentWidget()->objectName()
            == QStringLiteral("curveCreateCard"))
            return e;
    return nullptr;
}

QPushButton *findCreateBtn(CurveEditorDialog *dlg)
{
    const auto btns = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == QStringLiteral("Create")
            && b->parentWidget()
            && b->parentWidget()->objectName() == QStringLiteral("curveCreateCard"))
            return b;
    return nullptr;
}

} // namespace

class TestCurveEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    // ── CurveProvider ───────────────────────────────────────────────────────

    void setAllPoints_RejectsNonMonotone()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QSignalSpy rejected(&p, &CurveProvider::mutationRejected);
        // Non-monotone X.
        QVERIFY(!p.setAllPoints({{0.0, 1.0}, {2.0, 2.0}, {1.0, 3.0}}));
        QCOMPARE(rejected.size(), 1);

        // Monotone OK.
        QVERIFY(p.setAllPoints({{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}}));
        QCOMPARE(p.pointCount(), 3);
    }

    void setYAt_ValidatesIndex()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Rating);
        QVERIFY(p.setAllPoints({{0.0, 1.0}, {1.0, 2.0}}));
        QSignalSpy changed(&p, &CurveProvider::pointsChanged);

        QVERIFY(p.setYAt(1, 5.0));
        QCOMPARE(p.pointAt(1).y, 5.0);
        QCOMPARE(changed.size(), 1);

        QVERIFY(!p.setYAt(99, 1.0));   // out of range
    }

    void setPointAt_EnforcesNeighbourMonotone()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Tidal);
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}}));

        // Try to move middle point's X below left neighbour.
        QVERIFY(!p.setPointAt(1, -0.5, 99.0));
        // Try to move middle point's X above right neighbour.
        QVERIFY(!p.setPointAt(1, 3.0, 99.0));
        // Valid in-between move.
        QVERIFY(p.setPointAt(1, 0.5, 99.0));
        QCOMPARE(p.pointAt(1).x, 0.5);
        QCOMPARE(p.pointAt(1).y, 99.0);
    }

    void insertPoint_SortsAndRejectsCollision()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QCOMPARE(p.insertPoint(2.0, 20.0), 0);
        QCOMPARE(p.insertPoint(1.0, 10.0), 0);   // inserts at front
        QCOMPARE(p.insertPoint(3.0, 30.0), 2);   // inserts at end
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(0).x, 1.0);
        QCOMPARE(p.pointAt(1).x, 2.0);
        QCOMPARE(p.pointAt(2).x, 3.0);

        // Collision rejected.
        QCOMPARE(p.insertPoint(2.0, 99.0), -1);
        QCOMPARE(p.pointCount(), 3);
    }

    void removePointsAt_HandlesDuplicateAndReverse()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0},
                                {3.0, 3.0}, {4.0, 4.0}}));
        // Reverse + duplicate indices.
        p.removePointsAt({1, 3, 1});   // removes 3 and 1 (1 dedup'd)
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(0).x, 0.0);
        QCOMPARE(p.pointAt(1).x, 2.0);
        QCOMPARE(p.pointAt(2).x, 4.0);
    }

    void setType_EmitsAndKeepsPoints()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 1.0}}));
        QSignalSpy typeSpy(&p, &CurveProvider::typeChanged);
        p.setType(CurveType::Pump3);
        QCOMPARE(typeSpy.size(), 1);
        QCOMPARE(p.pointCount(), 2);   // unchanged
    }

    // ── CurveRegistry ───────────────────────────────────────────────────────

    void registry_CRUD()
    {
        CurveRegistry reg;
        auto *a = reg.create(QStringLiteral("CV_A"), CurveType::Storage);
        QVERIFY(a);
        QCOMPARE(reg.providerCount(), 1);
        QCOMPARE(reg.findByName(QStringLiteral("cv_a")), a);   // case-insensitive

        // Duplicate rejected.
        QVERIFY(reg.create(QStringLiteral("CV_A"), CurveType::Pump1) == nullptr);

        // Rename happy path + collision rejection.
        auto *b = reg.create(QStringLiteral("CV_B"), CurveType::Rating);
        QVERIFY(b);
        QVERIFY(!reg.rename(b, QStringLiteral("CV_A")));     // collision
        QVERIFY(reg.rename(a, QStringLiteral("CV_NEW")));
        QCOMPARE(reg.findByName(QStringLiteral("CV_NEW")), a);

        reg.remove(a);
        QCOMPARE(reg.providerCount(), 1);
    }

    // ── CurvePointTableModel ────────────────────────────────────────────────

    void tableModel_BindsAndRefreshes()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}}));
        CurvePointTableModel m;
        m.setProvider(&p);

        QCOMPARE(m.rowCount(),    3);
        QCOMPARE(m.columnCount(), 2);
        QCOMPARE(m.data(m.index(0, 0)).toDouble(), 0.0);
        QCOMPARE(m.data(m.index(0, 1)).toDouble(), 1.0);

        // setData(Y) routes through setYAt.
        QVERIFY(m.setData(m.index(1, 1), 99.0));
        QCOMPARE(p.pointAt(1).y, 99.0);

        // setData(X) routes through setPointAt (in-range value).
        QVERIFY(m.setData(m.index(1, 0), 0.5));
        QCOMPARE(p.pointAt(1).x, 0.5);
    }

    // ── CurveEditorDialog ───────────────────────────────────────────────────

    void dialog_OpensAndBindsFirstProvider()
    {
        CurveRegistry reg;
        auto *a = reg.create(QStringLiteral("CV_A"), CurveType::Storage);
        QVERIFY(a);
        QVERIFY(a->setAllPoints({{0.0, 0.0}, {1.0, 10.0}, {2.0, 40.0}}));
        QVERIFY(reg.create(QStringLiteral("CV_B"), CurveType::Pump1));

        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.currentProvider() == a);
        QCOMPARE(dlg.tableModel()->rowCount(), 3);
        QCOMPARE(dlg.tableModel()->columnCount(), 2);
    }

    void dialog_ListSelectionRebinds()
    {
        CurveRegistry reg;
        auto *a = reg.create(QStringLiteral("CV_A"), CurveType::Storage);
        auto *b = reg.create(QStringLiteral("CV_B"), CurveType::Pump3);
        QVERIFY(a && b);

        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.currentProvider() == a);

        auto *lm = qobject_cast<QStandardItemModel *>(dlg.listView()->model());
        QVERIFY(lm && lm->rowCount() == 2);
        dlg.listView()->setCurrentIndex(lm->index(1, 0));
        QTest::qWait(0);
        QCOMPARE(dlg.currentProvider(), b);
    }

    void createNew_TransitionsToEditAndAddsProvider()
    {
        CurveRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &CurveRegistry::providerAdded);

        auto *dlg = CurveEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        QCOMPARE(int(dlg->mode()), int(CurveEditorDialog::Mode::CreateNew));

        auto *edit = findCreateNameEdit(dlg);
        auto *btn  = findCreateBtn(dlg);
        QVERIFY(edit && btn);
        QVERIFY(!dlg->isCreateEnabled());

        edit->setText(QStringLiteral("CV_NEW"));
        QVERIFY(dlg->isCreateEnabled());

        dlg->submitCreateNew();
        QCOMPARE(int(dlg->mode()), int(CurveEditorDialog::Mode::Edit));
        QCOMPARE(addedSpy.size(), 1);
        QVERIFY(reg.findByName(QStringLiteral("CV_NEW")) != nullptr);
        delete dlg;
    }

    void addAndDeleteRow_RoundTrip()
    {
        CurveRegistry reg;
        auto *p = reg.create(QStringLiteral("CV"), CurveType::Storage);
        QVERIFY(p);
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QCOMPARE(dlg.currentProvider(), p);

        const int n0 = p->pointCount();
        dlg.invokeAddRow();
        QCOMPARE(p->pointCount(), n0 + 1);
        dlg.invokeAddRow();
        QCOMPARE(p->pointCount(), n0 + 2);

        // Select the last row + delete.
        dlg.pointTable()->selectRow(p->pointCount() - 1);
        dlg.invokeDeleteRows();
        QCOMPARE(p->pointCount(), n0 + 1);
    }
};

QTEST_MAIN(TestCurveEditorDialog)
#include "test_curve_editor_dialog.moc"
