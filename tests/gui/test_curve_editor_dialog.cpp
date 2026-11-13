/**
 * @file      test_curve_editor_dialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.1 — CurveEditorDialog + CurveProvider +
 *         CurveRegistry + CurvePointTableModel coverage.
 *
 * Initial provider/model/editor scenarios (additional cases below cover lifetime,
 * accessible closing, native engine mirroring and project-unit titles):
 * - Provider rejects non-monotone setAllPoints.
 * - setYAt validates index; emits pointsChanged.
 * - setPointAt enforces strict-monotone with neighbours.
 * - insertPoint inserts at sorted X position; rejects equal-X collision.
 * - removePointsAt handles unsorted duplicate indices.
 * - setType emits typeChanged; does NOT clear points.
 * - Registry create / findByName (case-insensitive) / rename / remove.
 * - CurvePointTableModel binds; setData routes through provider; refresh.
 * - Dialog opens, binds first provider, table reflects points.
 * - Dialog list selection swaps the bound provider in the table model.
 * - createNew factory starts in CreateNew mode; submit transitions to Edit.
 * - Add Row appends + Delete Rows removes selected.
 *
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/unitsystem.h"
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "dialog_a11y_checks.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/panels/curvepointtablemodel.h"
#include "ui/util/usernotice.h"
#include "ui/widgets/curveeditchartview.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QAccessible>
#include <QChartView>
#include <QStatusBar>
#include <QComboBox>
#include <QAction>
#include <QClipboard>
#include <QFont>
#include <QGuiApplication>
#include <QLocale>
#include <QPointer>
#include <QLineEdit>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QApplication>
#include <QUndoStack>
#include <QValueAxis>

using openswmmvis::curve::CurvePoint;
using openswmmvis::curve::CurveProvider;
using openswmmvis::curve::CurveRegistry;
using openswmmvis::curve::CurveType;
using openswmmvis::ui::CurveEditorDialog;
using openswmmvis::ui::CurvePointTableModel;
using openswmmvis::ui::NoticeSeverity;

namespace
{
QStringList g_announcements;
QList<QAccessible::AnnouncementPoliteness> g_politeness;

/*! Accessibility update handler recording what would be announced to a screen reader. */
void recordAnnouncement(QAccessibleEvent *event)
{
    if (event->type() != QAccessible::Announcement)
        return;
    auto *announcement = static_cast<QAccessibleAnnouncementEvent *>(event);
    g_announcements.append(announcement->message());
    g_politeness.append(announcement->politeness());
}

/**
 *
 * @brief Find the first descendant line edit directly parented by curveCreateCard.
 * @param dlg Borrowed non-null editor, retained throughout lookup.
 * @return Borrowed child or null; matching uses the parent's exact objectName rather
 * than the edit name/visibility. Caller retains the dialog and child lifetime.
 *
 */
QLineEdit *findCreateNameEdit(CurveEditorDialog *dlg)
{
    const auto edits = dlg->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        if (e->parentWidget() &&
            e->parentWidget()->objectName() == QStringLiteral("curveCreateCard"))
            return e;
    return nullptr;
}

/**
 *
 * @brief Find the first Create button directly parented by curveCreateCard.
 * @param dlg Borrowed non-null editor, retained throughout lookup.
 * @return Borrowed exact-text/parent-name match or null; no enabled/visible-state check,
 * click or ownership transfer. Pointer expires with the child.
 *
 */
QPushButton *findCreateBtn(CurveEditorDialog *dlg)
{
    const auto btns = dlg->findChildren<QPushButton *>();
    for (QPushButton *b : btns)
        if (b->text() == QStringLiteral("Create") && b->parentWidget() &&
            b->parentWidget()->objectName() == QStringLiteral("curveCreateCard"))
            return b;
    return nullptr;
}

} // namespace

class TestCurveEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require registry destruction to disable/hide the editor and empty its point model.
     * Deletes the registry/provider while the undo stack survives; does not invoke a stale
     * callback or delete a provider independently.
     *
     */
    void ownerRegistryDeletionInvalidatesEditor()
    {
        auto *registry = new CurveRegistry;
        auto *provider = registry->create(QStringLiteral("Owned"), CurveType::Storage);
        QVERIFY(provider);
        QUndoStack stack;
        CurveEditorDialog dialog(registry, &stack);
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
     * retaining its provider.
     * Checks exact provider identity in the surviving registry; no subsequent edit or undo.
     *
     */
    void undoOwnerDeletionInvalidatesEditor()
    {
        CurveRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), CurveType::Storage);
        QVERIFY(provider);
        auto *stack = new QUndoStack;
        CurveEditorDialog dialog(&registry, stack);
        dialog.show();
        delete stack;
        QVERIFY2(!dialog.isEnabled(), "The editor must not retain a dangling undo owner.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
        QVERIFY(registry.findByName(QStringLiteral("Owned")) == provider);
    }

    /**
     *
     * @brief Require a non-null point table with an accessible name and a nonempty dialog
     * description.
     * Checks metadata presence only, without exact wording, screen-reader or keyboard tests.
     *
     */
    void editorTableHasAccessiblePurpose()
    {
        CurveRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), CurveType::Storage);
        QUndoStack stack;
        CurveEditorDialog dialog(&registry, &stack);
        auto *table = dialog.pointTable();
        QVERIFY(table);
        QVERIFY2(!table->accessibleName().isEmpty(),
                 "Data tables need a specific accessible name, beyond their row/column cells.");
        QVERIFY2(!dialog.accessibleDescription().isEmpty(),
                 "Live editors must explain that Close does not undo applied edits.");
    }

    /**
     *
     * @brief Require createNew to expose no provider/rows and leave the original registry
     * provider intact.
     * Deletes the heap dialog before checking registry count and identity; no draft submit
     * or point edit is performed.
     *
     */
    void newDraftDoesNotBindAnExistingObject()
    {
        CurveRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"), CurveType::Storage);
        QUndoStack stack;
        auto *dialog = CurveEditorDialog::createNew(&registry, &stack);
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
     * @brief Require rejection of an initially unchosen curve picker to return empty and preserve
     * the registry count.
     * A capture-free queued callback rejects the active modal curve dialog; no chosen-name
     * or accepted-picker scenario is covered.
     *
     */
    void cancelEmptyPickerDoesNotChooseExistingObject()
    {
        CurveRegistry registry;
        QVERIFY(registry.create(QStringLiteral("Owned"), CurveType::Storage));
        QUndoStack stack;
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<CurveEditorDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        const QString selected = CurveEditorDialog::pickCurve(&registry, &stack, {});
        QVERIFY2(
            selected.isEmpty(),
            "Closing an uncommitted new-object picker must not select an unrelated existing object.");
        QCOMPARE(registry.providerCount(), 1);
    }

    // ── CurveProvider ───────────────────────────────────────────────────────

    /**
     *
     * @brief Require descending x coordinates to fail with one rejection and a sorted three-point
     * vector to succeed.
     * Checks accepted count only, without post-refusal data comparison or non-finite points.
     *
     */
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

    /**
     *
     * @brief Require an accepted y edit to store five and emit once while index99 is refused.
     * Uses a rating curve; does not check rejection count/payload or non-finite values.
     *
     */
    void setYAt_ValidatesIndex()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Rating);
        QVERIFY(p.setAllPoints({{0.0, 1.0}, {1.0, 2.0}}));
        QSignalSpy changed(&p, &CurveProvider::pointsChanged);

        QVERIFY(p.setYAt(1, 5.0));
        QCOMPARE(p.pointAt(1).y, 5.0);
        QCOMPARE(changed.size(), 1);

        QVERIFY(!p.setYAt(99, 1.0)); // out of range
    }

    /**
     *
     * @brief Require moving the middle x outside either neighbour to fail and an interior point
     * to store x/y.
     * Checks accepted point 0.5/99 on a tidal curve; does not check equal-neighbour bounds,
     * rejection signals or hydraulic interpretation of the values.
     *
     */
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

    /**
     *
     * @brief Require front/end insertion to retain ascending x order and duplicate x to return
     * minus one.
     * Checks indices, count and all three x values, then unchanged count after collision;
     * does not assert every y value or signal count.
     *
     */
    void insertPoint_SortsAndRejectsCollision()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QCOMPARE(p.insertPoint(2.0, 20.0), 0);
        QCOMPARE(p.insertPoint(1.0, 10.0), 0); // inserts at front
        QCOMPARE(p.insertPoint(3.0, 30.0), 2); // inserts at end
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(0).x, 1.0);
        QCOMPARE(p.pointAt(1).x, 2.0);
        QCOMPARE(p.pointAt(2).x, 3.0);

        // Collision rejected.
        QCOMPARE(p.insertPoint(2.0, 99.0), -1);
        QCOMPARE(p.pointCount(), 3);
    }

    /**
     *
     * @brief Require unsorted duplicate indices to remove each target once and retain x
     * zero/two/four.
     * Uses indices one/three/one; no invalid-index, all-point removal or undo scenario.
     *
     */
    void removePointsAt_HandlesDuplicateAndReverse()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {4.0, 4.0}}));
        // Reverse + duplicate indices.
        p.removePointsAt({1, 3, 1}); // removes 3 and 1 (1 dedup'd)
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(0).x, 0.0);
        QCOMPARE(p.pointAt(1).x, 2.0);
        QCOMPARE(p.pointAt(2).x, 4.0);
    }

    /**
     *
     * @brief Require changing storage to Pump3 to emit once and retain point count two.
     * Does not compare point values, assert stored type explicitly or test same-type no-op.
     *
     */
    void setType_EmitsAndKeepsPoints()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 0.0}, {1.0, 1.0}}));
        QSignalSpy typeSpy(&p, &CurveProvider::typeChanged);
        p.setType(CurveType::Pump3);
        QCOMPARE(typeSpy.size(), 1);
        QCOMPARE(p.pointCount(), 2); // unchanged
    }

    // ── CurveRegistry ───────────────────────────────────────────────────────

    /**
     *
     * @brief Exercise create, case-insensitive find, duplicate/rename collision refusal,
     * successful rename and remove.
     * Checks provider identities and final count; does not inspect registry signals, old-name
     * absence after rename or engine mirroring in this case.
     *
     */
    void registry_CRUD()
    {
        CurveRegistry reg;
        auto *a = reg.create(QStringLiteral("CV_A"), CurveType::Storage);
        QVERIFY(a);
        QCOMPARE(reg.providerCount(), 1);
        QCOMPARE(reg.findByName(QStringLiteral("cv_a")), a); // case-insensitive

        // Duplicate rejected.
        QVERIFY(reg.create(QStringLiteral("CV_A"), CurveType::Pump1) == nullptr);

        // Rename happy path + collision rejection.
        auto *b = reg.create(QStringLiteral("CV_B"), CurveType::Rating);
        QVERIFY(b);
        QVERIFY(!reg.rename(b, QStringLiteral("CV_A"))); // collision
        QVERIFY(reg.rename(a, QStringLiteral("CV_NEW")));
        QCOMPARE(reg.findByName(QStringLiteral("CV_NEW")), a);

        reg.remove(a);
        QCOMPARE(reg.providerCount(), 1);
    }

    // ── CurvePointTableModel ────────────────────────────────────────────────

    /**
     *
     * @brief Require a three-point binding to expose two columns and route y/x edits into its
     * provider.
     * Checks initial row-zero display and accepted row-one values99/0.5; does not assert
     * refresh signals, invalid edits or undo history.
     *
     */
    void tableModel_BindsAndRefreshes()
    {
        CurveProvider p(QStringLiteral("c"), CurveType::Storage);
        QVERIFY(p.setAllPoints({{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}}));
        CurvePointTableModel m;
        m.setProvider(&p);

        QCOMPARE(m.rowCount(), 3);
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

    /**
     *
     * @brief Require edit-mode construction to bind the first curve and expose its three-by-two
     * table.
     * A second curve is present; no window exposure, list selection change or persistence.
     *
     */
    /*! A-07/A-27: notifyUser() shows the text on the given status bar, records it with the
     *  window title in the Message Log sink and announces it (assertively for an error); with
     *  no sink, no status bar or no context it still does the rest. */
    void notifyUserReachesStatusLogAndScreenReader()
    {
        QWidget window;
        window.setWindowTitle(QStringLiteral("Curve Editor[*]"));
        auto *status = new QStatusBar(&window);
        QList<QPair<NoticeSeverity, QString>> notices;
        openswmmvis::ui::setNoticeSink(
            [&](NoticeSeverity s, const QString &t) { notices.append({s, t}); });
        g_announcements.clear();
        g_politeness.clear();
        const auto previous = QAccessible::installUpdateHandler(recordAnnouncement);
        openswmmvis::ui::notifyUser(&window, NoticeSeverity::Warning,
                                    QStringLiteral("Paste rejected: no rows"), status, 4000);
        openswmmvis::ui::notifyUser(&window, NoticeSeverity::Error,
                                    QStringLiteral("File could not be read"));
        openswmmvis::ui::notifyUser(nullptr, NoticeSeverity::Information,
                                    QStringLiteral("No context"));
        openswmmvis::ui::setNoticeSink({});
        openswmmvis::ui::notifyUser(&window, NoticeSeverity::Warning,
                                    QStringLiteral("Not logged"), status);
        QAccessible::installUpdateHandler(previous);

        QCOMPARE(status->currentMessage(), QStringLiteral("Not logged"));
        QCOMPARE(notices.size(), 3);
        QCOMPARE(notices[0].first, NoticeSeverity::Warning);
        QCOMPARE(notices[0].second, QStringLiteral("Curve Editor: Paste rejected: no rows"));
        QCOMPARE(notices[1].first, NoticeSeverity::Error);
        QCOMPARE(notices[2].second, QStringLiteral("No context"));
        QCOMPARE(g_announcements,
                 QStringList({QStringLiteral("Paste rejected: no rows"),
                              QStringLiteral("File could not be read"),
                              QStringLiteral("Not logged")}));
        QCOMPARE(g_politeness[1], QAccessible::AnnouncementPoliteness::Assertive);
        QCOMPARE(g_politeness[0], QAccessible::AnnouncementPoliteness::Polite);
    }

    /*! A-27: a rejected curve rename no longer vanishes with the status message: it reaches
     *  the Message Log with the editor's title and is announced. */
    void renameCollisionIsLoggedAndAnnounced()
    {
        CurveRegistry reg;
        QVERIFY(reg.create(QStringLiteral("CV_A"), CurveType::Storage));
        QVERIFY(reg.create(QStringLiteral("CV_B"), CurveType::Storage));
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QList<QPair<NoticeSeverity, QString>> notices;
        openswmmvis::ui::setNoticeSink(
            [&](NoticeSeverity s, const QString &t) { notices.append({s, t}); });
        g_announcements.clear();
        const auto previous = QAccessible::installUpdateHandler(recordAnnouncement);
        auto *list = dlg.listView()->model();
        list->setData(list->index(1, 0), QStringLiteral("CV_A"));
        QAccessible::installUpdateHandler(previous);
        openswmmvis::ui::setNoticeSink({});

        QVERIFY(reg.findByName(QStringLiteral("CV_B")));
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices[0].first, NoticeSeverity::Warning);
        QVERIFY2(notices[0].second.contains(QStringLiteral("CV_A")) &&
                     notices[0].second.startsWith(dlg.windowTitle().remove(QStringLiteral("[*]"))),
                 qPrintable(notices[0].second));
        QCOMPARE(g_announcements.size(), 1);
        QVERIFY(g_announcements[0].contains(QStringLiteral("already exists")));
    }

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

    /**
     *
     * @brief Require selecting visible list row one to bind the second curve through the dialog's
     * proxy model.
     * Checks initial identity, visible count two and final identity after pending-event
     * delivery; no point modification or table-content comparison.
     *
     */
    void dialog_ListSelectionRebinds()
    {
        CurveRegistry reg;
        auto *a = reg.create(QStringLiteral("CV_A"), CurveType::Storage);
        auto *b = reg.create(QStringLiteral("CV_B"), CurveType::Pump3);
        QVERIFY(a && b);

        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QVERIFY(dlg.currentProvider() == a);

        // Post-filter the dialog wraps the source QStandardItemModel in a
        // QSortFilterProxyModel for the curve-type filter. Tests interact
        // with the view-visible (proxy) model.
        auto *lm = dlg.listView()->model();
        QVERIFY(lm && lm->rowCount() == 2);
        dlg.listView()->setCurrentIndex(lm->index(1, 0));
        QTest::qWait(0);
        QCOMPARE(dlg.currentProvider(), b);
    }

    /**
     *
     * @brief Require a valid draft name to enable submission, enter Edit and emit one
     * provider-added notification.
     * Checks initial CreateNew/disabled state, create-card controls and final name lookup;
     * deletes the heap editor. Does not compare registry count or save/reopen a file.
     *
     */
    void createNew_TransitionsToEditAndAddsProvider()
    {
        CurveRegistry reg;
        QUndoStack stack;
        QSignalSpy addedSpy(&reg, &CurveRegistry::providerAdded);

        auto *dlg = CurveEditorDialog::createNew(&reg, &stack);
        QVERIFY(dlg);
        QCOMPARE(int(dlg->mode()), int(CurveEditorDialog::Mode::CreateNew));

        auto *edit = findCreateNameEdit(dlg);
        auto *btn = findCreateBtn(dlg);
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

    /**
     *
     * @brief Require two Add actions followed by selected-last-row deletion to retain one net
     * added point.
     * Checks provider counts relative to the original size; does not assert coordinates,
     * undo state or file persistence.
     *
     */
    void addAndDeleteRow_RoundTrip()
    {
        CurveRegistry reg;
        auto *p = reg.create(QStringLiteral("CV"), CurveType::Storage);
        QVERIFY(p);
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QCOMPARE(dlg.currentProvider(), p);

        // Rows change the editor's draft; the curve changes at Apply.
        auto *draft = dlg.editedCurve();
        QVERIFY(draft && draft != p);
        const int n0 = p->pointCount();
        dlg.invokeAddRow();
        QCOMPARE(draft->pointCount(), n0 + 1);
        dlg.invokeAddRow();
        QCOMPARE(draft->pointCount(), n0 + 2);

        // Select the last row + delete.
        dlg.pointTable()->selectRow(draft->pointCount() - 1);
        dlg.invokeDeleteRows();
        QCOMPARE(draft->pointCount(), n0 + 1);
        QCOMPARE(p->pointCount(), n0);
        QVERIFY(dlg.draftSession()->apply());
        QCOMPARE(p->pointCount(), n0 + 1);
        QCOMPARE(stack.count(), 1); // one Apply, one step
    }

    // The editor used to be dismissable only from the title bar. The Close
    // box must hide it, and must not be the default button — Enter in a point
    // cell has to commit the cell, never close the editor.
    /**
     *
     * @brief Require an exposed editor's Close button to be nondefault/non-auto-default and hide
     * the dialog.
     * Runs shared accessibility assertions; does not send Enter from a point cell or inspect
     * settings/engine data in this case.
     *
     */
    void closeButton_HidesDialog()
    {
        CurveRegistry reg;
        QVERIFY(reg.create(QStringLiteral("CV"), CurveType::Storage));
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        // The Close button sits in the Apply / Revert footer.
        auto *btn = dlg.findChild<QPushButton *>(QStringLiteral("curve_closeBtn"));
        QVERIFY(btn);
        QVERIFY(!btn->isDefault());
        QVERIFY(!btn->autoDefault());
        swmmvis_test::assertDialogA11y(&dlg);

        btn->click();
        QTRY_VERIFY(!dlg.isVisible());
    }

    // Tutorial bug report (2026-10-17): a curve authored in the editor stayed
    // in the registry and never reached the engine, so the saved .inp had no
    // [CURVES] entry for it. Closing the editor flushes every provider, and a
    // second flush must not duplicate the points.
    /**
     *
     * @brief Require authored rating points to reach the native engine immediately and stay
     * unduplicated after close/flush.
     * Uses a scope-owned engine, checks three points before opening the dialog, exposes/
     * closes it and checks count after another saveToEngine returning one. No input-file
     * round trip or exact point-value readback is performed.
     *
     */
    void closingEditorFlushesCurvesToEngine()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng);
        auto cleanup = qScopeGuard([eng] { swmm_engine_destroy(eng); });

        CurveRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 0); // binds the engine handle
        auto *p = reg.create(QStringLiteral("RC1"), CurveType::Rating);
        QVERIFY(p);
        QVERIFY(p->insertPoint(0.0, 0.0) >= 0);
        QVERIFY(p->insertPoint(1.0, 2.0) >= 0);
        QVERIFY(p->insertPoint(2.0, 5.0) >= 0);
        // The curve reaches the engine at creation, points included, so a
        // pump or outlet can use it while the editor is still open.
        {
            const int idx0 = swmm_table_index(eng, "RC1");
            QVERIFY2(idx0 >= 0, "create must add the curve to the engine");
            int n0 = 0;
            QCOMPARE(swmm_table_get_point_count(eng, idx0, &n0), SWMM_OK);
            QCOMPARE(n0, 3);
        }

        QUndoStack stack;
        {
            CurveEditorDialog dlg(&reg, &stack);
            dlg.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dlg));
            dlg.close();
            QTRY_VERIFY(!dlg.isVisible());
        }

        const int idx = swmm_table_index(eng, "RC1");
        QVERIFY2(idx >= 0, "closing the editor must create the engine table");
        int n = 0;
        QCOMPARE(swmm_table_get_point_count(eng, idx, &n), SWMM_OK);
        QCOMPARE(n, 3);

        QCOMPARE(reg.saveToEngine(), 1);
        QCOMPARE(swmm_table_get_point_count(eng, idx, &n), SWMM_OK);
        QCOMPARE(n, 3);
    }

    // A pump curve created in the editor must be assignable while the editor
    // is open, keep its engine table through renames and type changes, and
    // never leave a stale copy behind.
    /**
     *
     * @brief Require immediate native curve creation, type/rename identity retention and removal
     * without stale copies.
     * Checks Pump3 type, same native index after rename, one curve after flush, then permits
     * a timeseries and curve sharing Shared while total native table count becomes three.
     * Removes the renamed curve and checks lookup absence; no pump assignment/run occurs.
     *
     */
    void createdCurveIsInEngineAndFollowsRenameAndType()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng);
        auto cleanup = qScopeGuard([eng] { swmm_engine_destroy(eng); });
        CurveRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 0);

        auto *p = reg.create(QStringLiteral("PC1"), CurveType::Pump1);
        QVERIFY(p);
        const int idx = swmm_table_index(eng, "PC1");
        QVERIFY(idx >= 0);
        QCOMPARE(swmm_table_count(eng), 1);

        p->setType(CurveType::Pump3);
        int t = -1;
        QCOMPARE(swmm_table_get_type(eng, idx, &t), SWMM_OK);
        QCOMPARE(t, int(CurveType::Pump3));

        QVERIFY(reg.rename(p, QStringLiteral("LiftCurve")));
        QCOMPARE(swmm_table_index(eng, "PC1"), -1);
        QCOMPARE(swmm_table_index(eng, "LiftCurve"), idx);
        QCOMPARE(reg.saveToEngine(), 1);
        QCOMPARE(swmm_table_count(eng), 1); // no second copy after a flush

        // A timeseries may share a curve's name; it must not be mistaken
        // for the curve.
        QCOMPARE(swmm_timeseries_add(eng, "Shared"), SWMM_OK);
        auto *q = reg.create(QStringLiteral("Shared"), CurveType::Storage);
        QVERIFY(q);
        QCOMPARE(swmm_table_count(eng), 3);

        reg.remove(p);
        QCOMPARE(swmm_table_index(eng, "LiftCurve"), -1);
    }

    // Column headers and axis titles carry the project's units.
    /**
     *
     * @brief Require project unit changes to refresh pump headers, helper titles and the
     * horizontal chart-axis title.
     * Sets process-wide active UnitSystem to a local CFS/CMS instance and resets it to null
     * on scope exit rather than restoring a previous pointer. Checks storage/pump/tidal/
     * shape label examples; does not convert point data or compare plotted coordinates.
     *
     */
    void headersAndAxesCarryUnits()
    {
        const auto restore = qScopeGuard([] { UnitSystem::setActiveProject(nullptr); });
        UnitSystem us;
        us.setFlowUnits(swmm_CFS);
        UnitSystem::setActiveProject(&us);

        CurveRegistry reg;
        auto *p = reg.create(QStringLiteral("PC"), CurveType::Pump3);
        QVERIFY(p);
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        auto *model = dlg.tableModel();
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Head (ft)"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Flow (CFS)"));

        us.setFlowUnits(swmm_CMS);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Head (m)"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Flow (CMS)"));

        QCOMPARE(CurveProvider::xTitle(CurveType::Storage), QStringLiteral("Depth (m)"));
        QCOMPARE(CurveProvider::yTitle(CurveType::Storage), QStringLiteral("Surface Area (m²)"));
        QCOMPARE(CurveProvider::xTitle(CurveType::Pump1), QStringLiteral("Volume (m³)"));
        QCOMPARE(CurveProvider::xTitle(CurveType::Pump5), QStringLiteral("Head (m)"));
        QCOMPARE(CurveProvider::xTitle(CurveType::Tidal), QStringLiteral("Hour (hr)"));
        QCOMPARE(CurveProvider::xTitle(CurveType::Shape), CurveProvider::xLabel(CurveType::Shape));

        const auto axes = dlg.chartView()->chart()->axes(Qt::Horizontal);
        QVERIFY(!axes.isEmpty());
        QCOMPARE(axes.first()->titleText(), QStringLiteral("Head (m)"));
    }

    // Apply / Revert plan, Phase 5: the editor edits a draft; Apply writes the
    // curve (type and points) to the engine as one project undo step that still
    // undoes after the editor is gone. The list marks the curve meanwhile.
    void draftReachesTheEngineAtApplyAsOneUndoStep()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng);
        auto cleanup = qScopeGuard([eng] { swmm_engine_destroy(eng); });
        CurveRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 0);
        auto *p = reg.create(QStringLiteral("RC1"), CurveType::Rating);
        QVERIFY(p && p->setAllPoints({{0.0, 0.0}, {1.0, 2.0}, {2.0, 5.0}}));
        const int idx = swmm_table_index(eng, "RC1");
        const auto engineY1 = [eng, idx] {
            double x = 0, y = -1;
            swmm_table_get_point(eng, idx, 1, &x, &y);
            return y;
        };
        const auto engineType = [eng, idx] {
            int t = -1;
            swmm_table_get_type(eng, idx, &t);
            return t;
        };
        QCOMPARE(engineY1(), 2.0);

        QUndoStack stack;
        QPointer<CurveEditorDialog> dlg = new CurveEditorDialog(&reg, &stack);
        QVERIFY(dlg->tableModel()->setData(dlg->tableModel()->index(1, 1), 3.5));
        dlg->editedCurve()->setType(CurveType::Diversion);
        QVERIFY(dlg->draftSession()->hasDraft());
        QCOMPARE(engineY1(), 2.0); // nothing reached the model yet
        QCOMPARE(engineType(), int(CurveType::Rating));
        QCOMPARE(stack.count(), 0);
        const QModelIndex row = dlg->listView()->model()->index(0, 0);
        QVERIFY(row.data(Qt::FontRole).value<QFont>().italic());

        QVERIFY(dlg->draftSession()->apply());
        QCOMPARE(engineY1(), 3.5);
        QCOMPARE(engineType(), int(CurveType::Diversion));
        QCOMPARE(stack.count(), 1);
        QVERIFY(!row.data(Qt::FontRole).value<QFont>().italic());

        delete dlg; // the step must not need the editor
        stack.undo();
        QCOMPARE(engineY1(), 2.0);
        QCOMPARE(engineType(), int(CurveType::Rating));
        QCOMPARE(p->pointAt(1).y, 2.0);
        stack.redo();
        QCOMPARE(engineY1(), 3.5);
    }

    // Plan D5 / D8: deleting a curve in the editor is one undoable step; Undo
    // brings it back with its type and points, in the engine.
    void deleteUndoBringsBackTypeAndPoints()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng);
        auto cleanup = qScopeGuard([eng] { swmm_engine_destroy(eng); });
        CurveRegistry reg;
        QCOMPARE(reg.loadFromEngine(eng), 0);
        auto *p = reg.create(QStringLiteral("PC2"), CurveType::Pump2);
        QVERIFY(p && p->setAllPoints({{1.0, 10.0}, {2.0, 8.0}}));
        QUndoStack stack;
        {
            CurveEditorDialog dlg(&reg, &stack);
            QCOMPARE(dlg.currentProvider(), p);
            dlg.deleteCurrentSilently();
        }
        QCOMPARE(swmm_table_index(eng, "PC2"), -1);
        QCOMPARE(stack.count(), 1);
        stack.undo();
        const int idx = swmm_table_index(eng, "PC2");
        QVERIFY(idx >= 0);
        int t = -1, n = 0;
        swmm_table_get_type(eng, idx, &t);
        swmm_table_get_point_count(eng, idx, &n);
        QCOMPARE(t, int(CurveType::Pump2));
        QCOMPARE(n, 2);
        auto *back = reg.findByName(QStringLiteral("PC2"));
        QVERIFY(back && back->type() == CurveType::Pump2 && back->pointAt(1).y == 8.0);
        stack.redo();
        QCOMPARE(swmm_table_index(eng, "PC2"), -1);
    }

    // Review E-18: "1,5<tab>2,5" from a comma-decimal spreadsheet is the point
    // (1.5, 2.5), not (1, 5); a header row is skipped.
    void pasteReadsCommaDecimals()
    {
        const QLocale before;
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        auto restore = qScopeGuard([before] { QLocale::setDefault(before); });
        CurveRegistry reg;
        QVERIFY(reg.create(QStringLiteral("CV"), CurveType::Storage));
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QGuiApplication::clipboard()->setText(
            QStringLiteral("Depth\tArea\n1,5\t2,5\n3\t4\n"));
        QAction *paste = nullptr;
        for (QAction *a : dlg.actions())
            if (a->text() == QStringLiteral("Paste"))
                paste = a;
        QVERIFY(paste);
        paste->trigger();
        auto *draft = dlg.editedCurve();
        QCOMPARE(draft->pointCount(), 2);
        QCOMPARE(draft->pointAt(0).x, 1.5);
        QCOMPARE(draft->pointAt(0).y, 2.5);
        QCOMPARE(draft->pointAt(1).x, 3.0);
    }

    /** Table cell, row, paste and type edits are steps of the editor's undo:
     *  each undoes and redoes on the draft, and undoing all of them leaves no
     *  draft. None of it reaches the model. */
    void tableEditsUndoInTheEditor()
    {
        CurveRegistry reg;
        auto *p = reg.create(QStringLiteral("RC1"), CurveType::Rating);
        QVERIFY(p && p->setAllPoints({{0.0, 0.0}, {1.0, 2.0}, {2.0, 5.0}}));
        const CurveProvider::Params applied = p->params();
        QUndoStack stack;
        CurveEditorDialog dlg(&reg, &stack);
        QUndoStack *undo = dlg.draftUndoStack();
        CurveProvider *draft = dlg.editedCurve();
        QVERIFY(undo && draft && draft != p);
        QAction *paste = nullptr;
        for (QAction *a : dlg.actions())
            if (a->text() == QStringLiteral("Paste"))
                paste = a;
        auto *type = dlg.findChild<QComboBox *>(QStringLiteral("curve_typeCombo"));
        QVERIFY(paste && type);

        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 3.5)); // a cell
        QCOMPARE(undo->count(), 1);
        dlg.pointTable()->selectRow(2);
        dlg.invokeAddRow();
        QCOMPARE(draft->pointCount(), 4);
        QCOMPARE(undo->count(), 2);
        dlg.pointTable()->selectRow(0);
        dlg.invokeDeleteRows();
        QCOMPARE(draft->pointCount(), 3);
        QCOMPARE(undo->count(), 3);
        dlg.pointTable()->clearSelection();
        QGuiApplication::clipboard()->setText(QStringLiteral("10\t1\n11\t2\n"));
        paste->trigger(); // appended: two rows, one step
        QCOMPARE(draft->pointCount(), 5);
        QCOMPARE(undo->count(), 4);
        type->setCurrentIndex(type->findData(int(CurveType::Diversion)));
        QCOMPARE(draft->type(), CurveType::Diversion);
        QCOMPARE(undo->count(), 5);
        const CurveProvider::Params edited = draft->params();
        QVERIFY(dlg.draftSession()->hasDraft());

        for (int i = 0; i < 5; ++i)
            undo->undo();
        QVERIFY(draft->params() == applied);
        QCOMPARE(type->currentData().toInt(), int(CurveType::Rating)); // the box follows
        QVERIFY(!dlg.draftSession()->hasDraft());
        for (int i = 0; i < 5; ++i)
            undo->redo();
        QVERIFY(draft->params() == edited);
        QVERIFY(p->params() == applied);
        QCOMPARE(stack.count(), 0);
    }
};

QTEST_MAIN(TestCurveEditorDialog)
#include "test_curve_editor_dialog.moc"
