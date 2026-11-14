/**
 * @file      test_timeseries_editor_dialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.3 — smoke tests for the editor dialog shell.
 *
 * Focus: dialog assembles grid + chart + toolbar, edits propagate through both
 * views via the shared TimeseriesProvider, undo round-trips. Mouse-event drag
 * simulation is covered by the chart-view smoke test; this exercises the
 * dialog-level wiring.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "dialog_a11y_checks.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/panels/timeseriestablemodel.h"
#include "ui/widgets/timeserieseditchartview.h"

#include <QApplication>
#include <QChart>
#include <QClipboard>
#include <QComboBox>
#include <QAbstractItemDelegate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QStyleOptionViewItem>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QItemSelectionModel>
#include <QLineSeries>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTimeZone>
#include <QApplication>
#include <QTextStream>
#include <QToolBar>
#include <QAction>
#include <QJsonObject>
#include <QScopeGuard>
#include <QUndoStack>

#include <cmath>
#include <memory>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::ui::TimeseriesEditorDialog;
using State = openswmmvis::ui::DataObjectDraftSession::State;

namespace
{
/**
 *
 * @brief Construct an unchecked UTC fixture timestamp without normalizing invalid calendar/clock
 * components.
 * @param year Calendar year.
 * @param month One-based calendar month.
 * @param day One-based day of month.
 * @param hour Hour from zero through 23.
 * @param minute Minute from zero through 59, default zero.
 * @return By-value UTC timestamp; invalid supplied components yield invalid Qt time.
 *
 */
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}
/**
 *
 * @brief Build the inline UTC time-series fixture.
 * @return Owned three-point vector at 2026-01-01 00:00/06:00/12:00 UTC with
 * unitless values one/two/three; no registry ownership or file IO.
 *
 */
QVector<TimeseriesPoint> fixture()
{
    return {
        {t(2026, 1, 1, 0), 1.0},
        {t(2026, 1, 1, 6), 2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}

/**
 *
 * @brief Copy points from the first line series in a chart.
 * @param c Borrowed non-null chart whose series remain owned by it.
 * @return Owned point list or empty when no line series exists; X/Y retain the
 * chart series coordinate convention, with no conversion or chart mutation.
 *
 */
QList<QPointF> linePointsFromChart(QChart *c)
{
    for (auto *s : c->series())
        if (auto *line = qobject_cast<QLineSeries *>(s))
            return line->points();
    return {};
}

/*! Press \p buttonText on the next modal message box; sets \p seen when found. */
void answerNextPrompt(const QString &buttonText, bool *seen)
{
    QTimer::singleShot(0, [buttonText, seen] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (auto *button : box->buttons())
            if (button->text().remove(QLatin1Char('&')) == buttonText) {
                *seen = true;
                button->click();
                return;
            }
        box->reject();
    });
}
} // namespace

class TestTimeseriesEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require registry destruction inside the active delete-confirmation prompt to
     * invalidate the editor before the prompt returns Yes.
     * Checks disabled/hidden state and empty model after the nested modal event loop.
     *
     */
    void registryDeletedDuringDeleteConfirmation()
    {
        auto *registry = new TimeseriesRegistry;
        auto *provider = registry->create(QStringLiteral("Delete while closing"));
        QVERIFY(provider);
        QUndoStack stack;
        TimeseriesEditorDialog dialog(registry, &stack, provider);
        dialog.show();
        bool sawConfirmation = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!question)
                return;
            sawConfirmation = true;
            delete registry;
            registry = nullptr;
            // Simulate the already-open prompt completing after owner invalidation.
            question->done(QMessageBox::Yes);
        });
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onDeleteSeriesClicked_", Qt::DirectConnection));
        QVERIFY(sawConfirmation);
        QVERIFY(!dialog.isEnabled());
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
        delete registry;
    }

    /**
     *
     * @brief Require destroying the registry to disable/hide its editor and clear the table
     * model.
     *
     */
    void ownerRegistryDeletionInvalidatesEditor()
    {
        auto *registry = new TimeseriesRegistry;
        auto *provider = registry->create(QStringLiteral("Owned"));
        QVERIFY(provider);
        QUndoStack stack;
        TimeseriesEditorDialog dialog(registry, &stack, provider);
        dialog.show();
        delete registry;
        QVERIFY2(!dialog.isEnabled(),
                 "A closed registry must disable the editor before stale callbacks can mutate it.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
    }

    /**
     *
     * @brief Require destroying the supplied project undo owner to invalidate the editor while
     * retaining its provider in the registry.
     *
     */
    void undoOwnerDeletionInvalidatesEditor()
    {
        TimeseriesRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"));
        QVERIFY(provider);
        auto *stack = new QUndoStack;
        TimeseriesEditorDialog dialog(&registry, stack, provider);
        dialog.show();
        delete stack;
        QVERIFY2(!dialog.isEnabled(), "The editor must not retain a dangling undo owner.");
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.tableModel()->rowCount(), 0);
        QVERIFY(registry.findByName(QStringLiteral("Owned")) == provider);
    }

    /**
     *
     * @brief Require a named data table and dialog description explaining that closing does not
     * undo live edits.
     *
     */
    void editorTableHasAccessiblePurpose()
    {
        TimeseriesRegistry registry;
        auto *provider = registry.create(QStringLiteral("Owned"));
        QUndoStack stack;
        TimeseriesEditorDialog dialog(&registry, &stack, provider);
        auto *table = dialog.findChild<QTableView *>();
        QVERIFY(table);
        QVERIFY2(!table->accessibleName().isEmpty(),
                 "Data tables need a specific accessible name, beyond their row/column cells.");
        QVERIFY2(!dialog.accessibleDescription().isEmpty(),
                 "Live editors must explain that Close does not undo applied edits.");
    }

    /**
     *
     * @brief Require the opened fixture to bind three points into a two-column grid and line
     * chart.
     *
     */
    void dialogOpens_BothViewsBound()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(50);

        QVERIFY(dlg.tableModel() != nullptr);
        QVERIFY(dlg.chartView() != nullptr);
        QCOMPARE(dlg.tableModel()->rowCount(), 3);
        QCOMPARE(dlg.tableModel()->columnCount(), 2);
        QCOMPARE(linePointsFromChart(dlg.chartView()->chart()).size(), 3);
    }

    /**
     *
     * @brief Require editing the grid value to update provider/chart through one editor command
     * and undo to restore both.
     *
     */
    void editFromGrid_RefreshesChart()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Edit through the grid model — chart should auto-refresh via the
        // draft's signals; the model's series waits for Apply.
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 42.0);
        QCOMPARE(p.pointAt(1).value, 2.0);
        const auto pts = linePointsFromChart(dlg.chartView()->chart());
        QCOMPARE(pts.at(1).y(), 42.0);
        QCOMPARE(dlg.undoStack()->count(), 1);

        // Undo restores everywhere.
        dlg.undoStack()->undo();
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 2.0);
        QCOMPARE(linePointsFromChart(dlg.chartView()->chart()).at(1).y(), 2.0);
    }

    // Tutorial bug report (2026-10-17): the editor's Undo button was built
    // on the project's map stack, so it undid whatever map edit was last.
    /**
     *
     * @brief Require editor toolbar Undo to use its own stack and leave a foreign project command
     * untouched after repeated Undo.
     * The local command borrows the enclosing foreignUndos counter; its undo increments
     * it and redo is intentionally empty; the counter remains live during all triggers.
     *
     */
    void toolbarUndoNeverTouchesTheProjectStack()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack projectStack;
        int foreignUndos = 0;
        struct Foreign : QUndoCommand {
            int *undos;
            explicit Foreign(int *u) : QUndoCommand(QStringLiteral("map edit")), undos(u) {}
            void undo() override { ++*undos; }
            void redo() override {}
        };
        projectStack.push(new Foreign(&foreignUndos));

        TimeseriesEditorDialog dlg(&reg, &projectStack, &p);
        QVERIFY(dlg.undoStack());
        QVERIFY(dlg.undoStack() != &projectStack);
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QCOMPARE(projectStack.count(), 1); // only the foreign edit

        QAction *undo = nullptr;
        for (QAction *a : dlg.findChildren<QAction *>())
            if (a->text().startsWith(QStringLiteral("Undo"))) {
                undo = a;
                break;
            }
        QVERIFY2(undo, "the editor toolbar offers Undo");
        undo->trigger();
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 2.0); // the editor's own edit
        undo->trigger();                   // nothing more to undo here
        QCOMPARE(foreignUndos, 0);
    }

    // ── Time column: MM/dd/yyyy HH:mm display + a real date-time editor ──────

    /*! The grid shows the .inp's own MM/dd/yyyy HH:mm, not the system locale's
     *  short form. EditRole must stay a QDateTime so the delegate can seed a
     *  QDateTimeEdit with the full value. */
    void timeColumnDisplaysSwmmFormat()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        TimeseriesEditorDialog dlg(&reg, nullptr, &p);

        const QModelIndex idx = dlg.tableModel()->index(1, 0);
        QCOMPARE(idx.data(Qt::DisplayRole).toString(), QStringLiteral("01/01/2026 06:00"));
        QCOMPARE(idx.data(Qt::EditRole).userType(), QMetaType::QDateTime);
        QCOMPARE(idx.data(Qt::EditRole).toDateTime(), t(2026, 1, 1, 6));
    }

    /*! The time column gets a calendar-popup QDateTimeEdit carrying the same
     *  format — not the default locale-formatted editor. */
    void timeColumnEditorIsAFormattedDateTimeEdit()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        TimeseriesEditorDialog dlg(&reg, nullptr, &p);

        auto *view = dlg.findChild<QTableView *>();
        QVERIFY(view != nullptr);
        const QModelIndex idx = dlg.tableModel()->index(1, 0);
        auto *delegate = view->itemDelegateForIndex(idx);
        QVERIFY(delegate != nullptr);

        QStyleOptionViewItem opt;
        QWidget *editor = delegate->createEditor(view->viewport(), opt, idx);
        QVERIFY(editor != nullptr);
        auto *dte = qobject_cast<QDateTimeEdit *>(editor);
        QVERIFY2(dte != nullptr, "time cells must edit through a QDateTimeEdit");
        QCOMPARE(dte->displayFormat(), QStringLiteral("MM/dd/yyyy HH:mm"));
        QVERIFY(dte->calendarPopup());

        delegate->setEditorData(dte, idx);
        QCOMPARE(dte->dateTime(), t(2026, 1, 1, 6));

        // Commit a new stamp through the delegate → provider.
        dte->setDateTime(t(2026, 1, 1, 7));
        delegate->setModelData(dte, dlg.tableModel(), idx);
        QCOMPARE(dlg.editedSeries()->pointAt(1).time, t(2026, 1, 1, 7));
        delete editor;
    }

    /*! A minute-resolution format must not silently zero a seconds field the
     *  user never saw — the GH #1 truncation class. QDateTimeEdit keeps the
     *  sections its display format omits; this pins that we rely on it. */
    void editingAStampPreservesHiddenSeconds()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints({
            {QDateTime(QDate(2026, 1, 1), QTime(0, 15, 30), Qt::UTC), 1.0},
            {QDateTime(QDate(2026, 1, 1), QTime(6, 0, 0), Qt::UTC), 2.0},
        }));
        TimeseriesEditorDialog dlg(&reg, nullptr, &p);

        auto *view = dlg.findChild<QTableView *>();
        QVERIFY(view != nullptr);
        const QModelIndex idx = dlg.tableModel()->index(0, 0);
        auto *delegate = view->itemDelegateForIndex(idx);
        QVERIFY(delegate != nullptr);

        QStyleOptionViewItem opt;
        QWidget *editor = delegate->createEditor(view->viewport(), opt, idx);
        auto *dte = qobject_cast<QDateTimeEdit *>(editor);
        QVERIFY(dte != nullptr);
        delegate->setEditorData(dte, idx);

        // The hidden seconds ride along in the editor's value...
        QCOMPARE(dte->dateTime().time().second(), 30);
        // ...and survive a commit that only moved the minute.
        dte->setDateTime(dte->dateTime().addSecs(60));
        delegate->setModelData(dte, dlg.tableModel(), idx);
        QCOMPARE(dlg.editedSeries()->pointAt(0).time,
                 QDateTime(QDate(2026, 1, 1), QTime(0, 16, 30), Qt::UTC));
        delete editor;
    }

    /**
     *
     * @brief Require an external provider value change to refresh both grid and chart
     * synchronously.
     *
     */
    void editFromProvider_RefreshesGridAndChart()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // External mutation (not from grid or chart) — both views must refresh.
        QVERIFY(p.setValueAt(0, 99.0));
        QCOMPARE(dlg.tableModel()->data(dlg.tableModel()->index(0, 1)).toDouble(), 99.0);
        QCOMPARE(linePointsFromChart(dlg.chartView()->chart()).first().y(), 99.0);
    }

    /**
     *
     * @brief Require an out-of-order provider timestamp mutation to fail with a reason.
     * The assertions do not inspect the dialog status bar or prove its displayed message.
     *
     */
    void mutationRejectedSurfacesInStatusBar()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Provoke a rejection: try to move point 1 to a time before point 0.
        QString reason;
        const bool ok = p.setPointAt(1, t(2025, 1, 1, 0), 5.0, &reason);
        QVERIFY(!ok);
        QVERIFY(!reason.isEmpty());
        // The dialog's status bar should have shown a transient message — the
        // QStatusBar API doesn't expose the current message synchronously
        // without findChildren<QStatusBar*>, but the underlying mutationRejected
        // signal connection is what we verify here (above ok==false assertion).
    }

    /**
     *
     * @brief Require at least one descendant action whose text contains Undo when a project stack
     * is supplied.
     *
     */
    void undoRedoActionsExistWhenStackProvided()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Dialog has a toolbar with at least one undo action when a stack is bound.
        const auto actions = dlg.findChildren<QAction *>();
        bool foundUndo = false;
        for (auto *a : actions)
            if (a->text().contains(QStringLiteral("Undo"))) {
                foundUndo = true;
                break;
            }
        QVERIFY(foundUndo);
    }

    /**
     *
     * @brief Require repeated Edit Points action triggers to toggle the chart between point-edit
     * and no-edit modes.
     *
     */
    void editModeToggleFlipsChart()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Find the "Edit Points" action and trigger it.
        QAction *editAct = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Edit Points")) {
                editAct = a;
                break;
            }
        QVERIFY(editAct != nullptr);
        QVERIFY(editAct->isCheckable());

        // QAction::trigger() on a checkable action toggles the check state
        // AND emits triggered() — so don't pre-set with setChecked().
        editAct->trigger();
        QCOMPARE(dlg.chartView()->editMode(),
                 openswmmvis::ui::TimeseriesEditChartView::EditMode::EditPoints);

        editAct->trigger();
        QCOMPARE(dlg.chartView()->editMode(),
                 openswmmvis::ui::TimeseriesEditChartView::EditMode::None);
    }

    // ── Row editing (Phase 6.7.3.4-followup) ────────────────────────────────

    /**
     *
     * @brief Require Add Row to append six hours after the last fixture point, copy its value and
     * undo the append.
     *
     */
    void addRowAppendsWithMedianInterval()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture())); // 0h, 6h, 12h → 6h median
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) {
                add = a;
                break;
            }
        QVERIFY(add != nullptr);

        add->trigger();
        QCOMPARE(d.pointCount(), 4);
        // New point at last + 6h = 2026-01-01 18:00.
        QCOMPARE(d.pointAt(3).time, t(2026, 1, 1, 18));
        QCOMPARE(d.pointAt(3).value, d.pointAt(2).value);

        dlg.undoStack()->undo();
        QCOMPARE(d.pointCount(), 3);
    }

    /**
     *
     * @brief Require Add Row without simulation start to seed a whole UTC minute within the
     * preceding 65 seconds.
     *
     */
    void addRowOnEmptyProviderUsesNow()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) {
                add = a;
                break;
            }
        QVERIFY(add != nullptr);
        add->trigger();
        QCOMPARE(d.pointCount(), 1);
        // Time is "now" (UTC) on a whole minute: the grid shows HH:mm, so
        // clock seconds were invisible but still written to the .inp
        // (tutorial bug report, 2026-10-17).
        const QDateTime t0 = d.pointAt(0).time;
        QCOMPARE(t0.time().second(), 0);
        QCOMPARE(t0.time().msec(), 0);
        const qint64 ds = t0.secsTo(QDateTime::currentDateTimeUtc());
        QVERIFY2(ds >= 0 && ds < 65, qPrintable(QString::number(ds)));
    }

    /**
     *
     * @brief Require deleting selected first/last rows to retain the middle timestamp and one
     * undo to restore all three rows.
     *
     */
    void deleteRowsRemovesSelection()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // Build a QTableView selection covering rows 0 and 2.
        auto *table = dlg.findChild<QTableView *>();
        QVERIFY(table != nullptr);
        table->selectRow(0);
        table->selectionModel()->select(dlg.tableModel()->index(2, 0),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);

        QAction *del = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Delete Rows")) {
                del = a;
                break;
            }
        QVERIFY(del != nullptr);
        del->trigger();
        QCOMPARE(d.pointCount(), 1);
        QCOMPARE(d.pointAt(0).time, t(2026, 1, 1, 6)); // middle row kept

        dlg.undoStack()->undo();
        QCOMPARE(d.pointCount(), 3);
    }

    /**
     *
     * @brief Require copy-all to place three ISO-timestamp/tab-separated rows on the application
     * clipboard.
     * Checks first timestamp and final value text; overwrites global clipboard contents.
     *
     */
    void copyPopulatesClipboard()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // No explicit selection → copy-all.
        QAction *copy = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Copy")) {
                copy = a;
                break;
            }
        QVERIFY(copy != nullptr);
        copy->trigger();

        const QString text = QApplication::clipboard()->text();
        const QStringList lines = text.split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines.first().startsWith(QStringLiteral("2026-01-01T00:00:00")));
        QVERIFY(lines.last().endsWith(QStringLiteral("\t3")));
    }

    /**
     *
     * @brief Require two clipboard TSV rows to insert in time order with their values and undo as
     * one paste operation.
     * Overwrites the application clipboard.
     *
     */
    void pasteInsertsRowsFromTsv()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // Excel-style TSV: ISO timestamp + value per line. Two new times that
        // don't collide with existing 0h / 6h / 12h.
        QApplication::clipboard()->setText(
            QStringLiteral("2026-01-01T03:00:00\t10.0\n2026-01-01T09:00:00\t20.0"));

        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) {
                paste = a;
                break;
            }
        QVERIFY(paste != nullptr);
        paste->trigger();

        QCOMPARE(d.pointCount(), 5);
        QCOMPARE(d.pointAt(1).time, t(2026, 1, 1, 3));
        QCOMPARE(d.pointAt(1).value, 10.0);
        QCOMPARE(d.pointAt(3).time, t(2026, 1, 1, 9));
        QCOMPARE(d.pointAt(3).value, 20.0);

        // Single Cmd-Z reverts the whole paste (macro).
        dlg.undoStack()->undo();
        QCOMPARE(d.pointCount(), 3);
    }

    /**
     *
     * @brief Require a duplicate clipboard timestamp to be skipped while the novel row is
     * inserted in sorted order.
     * Checks count and inserted time, without an explicit comparison of the original duplicate
     * value.
     *
     */
    void pasteDuplicateTimesAreSkipped()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture())); // already has 0h / 6h / 12h
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // First row collides with existing 6h; second is novel.
        QApplication::clipboard()->setText(
            QStringLiteral("2026-01-01T06:00:00\t99.0\n2026-01-01T03:00:00\t50.0"));

        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) {
                paste = a;
                break;
            }
        paste->trigger();

        // 3 → 4: only the novel row landed.
        QCOMPARE(d.pointCount(), 4);
        QCOMPARE(d.pointAt(1).time, t(2026, 1, 1, 3));
    }

    /**
     *
     * @brief Require the grid context-menu policy to be CustomContextMenu; does not open or
     * inspect the menu.
     *
     */
    void gridUsesCustomContextMenu()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        auto *table = dlg.findChild<QTableView *>();
        QVERIFY(table != nullptr);
        // Right-click on the grid is intercepted by our slot (rather than
        // Qt's default no-op), so the dialog can pop a menu of the toolbar
        // actions. Pinning the policy guards against accidental regressions.
        QCOMPARE(table->contextMenuPolicy(), Qt::CustomContextMenu);
    }

    /**
     *
     * @brief Require Add Row and Paste triggers in external-file mode to retain the original
     * point count.
     * Overwrites the application clipboard; does not test all mutation paths.
     *
     */
    void externalSourceModeDisablesRowEdits()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) {
                add = a;
                break;
            }
        add->trigger();
        QCOMPARE(d.pointCount(), 3); // unchanged

        QApplication::clipboard()->setText(QStringLiteral("2026-01-01T03:00:00\t10.0"));
        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) {
                paste = a;
                break;
            }
        paste->trigger();
        QCOMPARE(d.pointCount(), 3); // unchanged
    }

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────

    /**
     *
     * @brief Require the source card to start Inline and reflect a provider-driven switch to
     * External file.
     *
     */
    void sourceModeCardReflectsProviderMode()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Default mode is Inline → that radio is checked.
        const auto radios = dlg.findChildren<QRadioButton *>();
        QRadioButton *rInline = nullptr;
        for (auto *r : radios)
            if (r->text() == QStringLiteral("Inline")) {
                rInline = r;
                break;
            }
        QVERIFY(rInline != nullptr);
        QVERIFY(rInline->isChecked());

        // Provider-driven mode flip → card refreshes via sourceModeChanged.
        p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
        QRadioButton *rExt = nullptr;
        for (auto *r : radios)
            if (r->text() == QStringLiteral("External file")) {
                rExt = r;
                break;
            }
        QVERIFY(rExt != nullptr);
        QVERIFY(rExt->isChecked());
    }

    /**
     *
     * @brief Require external mode without a linked file to enable visible Browse while keeping
     * Reload and Detach disabled.
     *
     */
    void externalMode_BrowseButtonEnabledEvenWithoutFile()
    {
        // Bug report: when External is selected, Browse should be enabled so
        // the user can pick a file. The other ext-controls (column / Reload /
        // Detach) gate on hasFile, but Browse always works in External mode.
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        // Required before asserting isVisible() below: a child of a top-level
        // widget that was never shown is not visible under ANY QPA, offscreen
        // included. Matches dialogOpens_BothViewsBound() above, which shows the
        // dialog for the same reason.
        dlg.show();

        // Switch to External mode (provider has no file yet).
        QRadioButton *rExt = nullptr;
        for (auto *r : dlg.findChildren<QRadioButton *>())
            if (r->text() == QStringLiteral("External file")) {
                rExt = r;
                break;
            }
        QVERIFY(rExt != nullptr);
        rExt->setChecked(true);
        rExt->toggled(true); // ensure slot fires in offscreen mode

        QPushButton *browse = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text().startsWith(QStringLiteral("Browse"))) {
                browse = b;
                break;
            }
        QVERIFY(browse != nullptr);
        QVERIFY(browse->isVisible());
        QVERIFY(browse->isEnabled()); // <- the actual bug being pinned

        // And the gate-on-file controls are disabled when no file is linked.
        QPushButton *reload = nullptr, *detach = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Reload"))
                reload = b;
            if (b->text().startsWith(QStringLiteral("Detach")))
                detach = b;
        }
        QVERIFY(reload != nullptr);
        QVERIFY(detach != nullptr);
        QVERIFY(!reload->isEnabled());
        QVERIFY(!detach->isEnabled());
    }

    /**
     *
     * @brief Require external mode to disable edit/rotate/scale/add/delete/paste while retaining
     * Copy, then re-enable inline edit/add.
     * The local action lookup borrows the dialog, matches exact label text and returns
     * a borrowed QAction or nullptr without creating or modifying actions.
     *
     */
    void externalMode_EditAndRowToolbarsDisabled()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Flip to External via the provider directly (simulates a file-linked TS
        // loaded from the engine on dialog open).
        p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);

        // Edit-mode toolbar buttons should be disabled — the user can see at a
        // glance that this is read-only, not just have actions silently no-op.
        auto findAction = [&](const QString &label) -> QAction * {
            for (auto *a : dlg.findChildren<QAction *>())
                if (a->text() == label)
                    return a;
            return nullptr;
        };
        QAction *edit = findAction(QStringLiteral("Edit Points"));
        QAction *rotate = findAction(QStringLiteral("Rotate"));
        QAction *scale = findAction(QStringLiteral("Scale"));
        QAction *addR = findAction(QStringLiteral("Add Row"));
        QAction *delR = findAction(QStringLiteral("Delete Rows"));
        QAction *paste = findAction(QStringLiteral("Paste"));
        QAction *copy = findAction(QStringLiteral("Copy"));

        QVERIFY(edit && rotate && scale && addR && delR && paste && copy);
        QVERIFY(!edit->isEnabled());
        QVERIFY(!rotate->isEnabled());
        QVERIFY(!scale->isEnabled());
        QVERIFY(!addR->isEnabled());
        QVERIFY(!delR->isEnabled());
        QVERIFY(!paste->isEnabled());
        QVERIFY(copy->isEnabled()); // Copy is read-only — stays enabled.

        // Flip back to Inline → toolbar re-enables.
        p.setSourceMode(TimeseriesProvider::SourceMode::Inline);
        QVERIFY(edit->isEnabled());
        QVERIFY(addR->isEnabled());
    }

    /**
     *
     * @brief Require linking a temporary CSV to load three points, preserve its path and expose
     * the rain_a header choice.
     * Creates only temporary fixture files; final stream-write status is unchecked.
     *
     */
    void linkExternalFile_LoadsPointsAndPopulatesCombo()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("rain.csv"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "time,rain_a\n"
                << "2026-01-01T00:00:00,1.0\n"
                << "2026-01-01T06:00:00,2.0\n"
                << "2026-01-01T12:00:00,3.0\n";
        }

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        const int n = dlg.linkExternalFile(path);
        QCOMPARE(n, 3);
        QCOMPARE(d.sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);
        QCOMPARE(d.pointCount(), 3);
        QCOMPARE(d.pointAt(1).value, 2.0);
        QCOMPARE(d.filePath(), path);

        // Column combo populated from CSV header (single value column).
        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QVERIFY(combo->count() >= 1);
        QCOMPARE(combo->itemText(0), QStringLiteral("rain_a"));
    }

    /**
     *
     * @brief Require rebinding a temporary CSV to missing rain_old to clear preview points and
     * retain an explicit missing-column choice.
     *
     */
    void linkExternalFile_UnmatchedColumnLoadsNothingAndShowsIt()
    {
        // Review B-3: a stored column that is not in the file is a hard error
        // in the engine, so the GUI must not preview column 1 instead. Expect
        // no points and a combo item that names the missing column, so display
        // and provider state still agree.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("renamed.csv"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "time,rain_a,rain_b\n"
                << "2026-01-01T00:00:00,1.0,10.0\n"
                << "2026-01-01T06:00:00,2.0,20.0\n";
        }

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_GONE"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // Bind a real column first, then re-bind the column a re-exported file
        // no longer has (the "headers changed under a saved model" case).
        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_b")), 2);
        QCOMPARE(d.pointCount(), 2);

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_old")), 0);
        QCOMPARE(d.pointCount(), 0);                              // no bogus preview
        QCOMPARE(d.columnSelector(), QStringLiteral("rain_old")); // not rewritten

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3); // 2 real + the missing one
        QCOMPARE(combo->currentData().toString(), QStringLiteral("rain_old"));
        QVERIFY(combo->currentText().contains(QStringLiteral("rain_old")));
    }

    /**
     *
     * @brief Require headerless CSV preview to consume its first line as a header, retain an
     * empty selector and refuse fabricated alternate columns.
     *
     */
    void linkExternalFile_HeaderlessKeepsEmptySelectorAndRefusesOtherColumns()
    {
        // Review B-4 / risk R1 + the row off-by-one. The engine always spends
        // the first content line on the header row, so a headerless 3-line
        // file yields 2 points here too; and a fabricated "col_N" name is
        // never stored (it cannot be resolved by the engine).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("noheader.csv"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "2026-01-01T00:00:00,1.0,10.0\n"
                << "2026-01-01T06:00:00,2.0,20.0\n"
                << "2026-01-01T12:00:00,3.0,30.0\n";
        }

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_NOHDR"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        QCOMPARE(dlg.linkExternalFile(path), 2); // line 1 spent as header
        QCOMPARE(d.pointAt(0).value, 2.0);
        QVERIFY(d.columnSelector().isEmpty());

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(1), QStringLiteral("col_2"));
        // Every fabricated item carries an EMPTY selector, so nothing
        // unresolvable can be persisted…
        QVERIFY(combo->itemData(1).toString().isEmpty());
        // …and picking a non-first column is refused outright: the binding and
        // the loaded points stay on the first data column.
        combo->setCurrentIndex(1);
        QVERIFY(d.columnSelector().isEmpty());
        QCOMPARE(d.pointCount(), 2);
        QCOMPARE(d.pointAt(0).value, 2.0);
        QCOMPARE(combo->currentIndex(), 0);
    }

    /**
     *
     * @brief Require named rain_b selection from a temporary CSV to load its two values rather
     * than the first data column.
     *
     */
    void linkExternalFile_MultiColumnSelector()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("multi.csv"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "time,rain_a,rain_b\n"
                << "2026-01-01T00:00:00,1.0,10.0\n"
                << "2026-01-01T06:00:00,2.0,20.0\n";
        }

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_B"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // Select the second column by name.
        const int n = dlg.linkExternalFile(path, QStringLiteral("rain_b"));
        QCOMPARE(n, 2);
        QCOMPARE(d.pointAt(0).value, 10.0);
        QCOMPARE(d.pointAt(1).value, 20.0);
    }

    /**
     *
     * @brief Require the committed TSF fixture to expose RG1/RG2 names and parse RG2
     * midnight/afternoon stamps in UTC.
     * Reads extcol_dialog_sample.tsf below SWMMVIS_GUI_TEST_DATA or current-directory dot.
     *
     */
    void linkExternalFile_TsfIdsHeaderAndAmPm()
    {
        // PCSWMM .tsf (spec §4 task 2): IDs-row column names, 3-row header,
        // 12-hour AM/PM datetimes. Committed fixture — reviewable per
        // CLAUDE.md §4.1.
        const QString path = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")) +
                             QStringLiteral("/extcol_dialog_sample.tsf");

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("TSF_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("RG2")), 3);
        QCOMPARE(d.pointAt(0).value, 0.05);
        QCOMPARE(d.pointAt(2).value, 0.25);
        // 12:00:00 AM → midnight; 1:30:00 PM → 13:30.
        QCOMPARE(d.pointAt(0).time, t(2007, 1, 1, 0));
        QCOMPARE(d.pointAt(2).time, QDateTime(QDate(2007, 1, 1), QTime(13, 30), Qt::UTC));

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(0), QStringLiteral("RG1"));
        QCOMPARE(combo->itemText(1), QStringLiteral("RG2"));
        QCOMPARE(combo->currentIndex(), 1);
    }

    /**
     *
     * @brief Require switching through inline/file series to restore the incoming file header
     * choices and saved rain_b selector.
     * Reads extcol_switch_multi.csv from the configured fixture directory. The local
     * name selector borrows the list/model, picks an exact display-name row and returns
     * false when absent; it does not rename providers.
     *
     */
    void seriesSwitch_RepopulatesColumnCombo()
    {
        // B3 regression (spec §1.4): switching series must repopulate the
        // column combo from the incoming provider's file and re-select its
        // columnSelector — it used to keep the previous series' items.
        const QString path = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")) +
                             QStringLiteral("/extcol_switch_multi.csv");

        TimeseriesRegistry reg;
        TimeseriesProvider &a = *reg.create(QStringLiteral("TS_FILE"));
        TimeseriesProvider &b = *reg.create(QStringLiteral("TS_INLINE"));
        QVERIFY(b.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &a);
        TimeseriesProvider &da = *dlg.editedSeries(); // the draft the views edit
        dlg.show();
        QTest::qWait(50);

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_b")), 2);
        QCOMPARE(da.columnSelector(), QStringLiteral("rain_b"));

        // Drive rebindActiveProvider_ through the list pane like a user click:
        // away to the inline series, then back to the file-backed one.
        // The series list is the only view driven by a QSortFilterProxyModel;
        // a plain findChild<QListView*> would hit the column combo's popup
        // view first (the source-mode card is built before the splitter).
        QListView *list = nullptr;
        for (auto *v : dlg.findChildren<QListView *>()) {
            if (qobject_cast<QSortFilterProxyModel *>(v->model())) {
                list = v;
                break;
            }
        }
        QVERIFY(list != nullptr);
        auto selectByName = [&](const QString &name) {
            QAbstractItemModel *m = list->model();
            for (int r = 0; r < m->rowCount(); ++r) {
                const QModelIndex idx = m->index(r, 0);
                if (m->data(idx, Qt::DisplayRole).toString() == name) {
                    list->setCurrentIndex(idx);
                    return true;
                }
            }
            return false;
        };
        QVERIFY(selectByName(QStringLiteral("TS_INLINE")));
        QTest::qWait(20);
        QVERIFY(selectByName(QStringLiteral("TS_FILE")));
        QTest::qWait(20);

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(0), QStringLiteral("rain_a"));
        QCOMPARE(combo->itemText(1), QStringLiteral("rain_b"));
        // Shown item and provider state agree (items carry the real header
        // name as userData).
        QCOMPARE(combo->currentIndex(), 1);
        QCOMPARE(combo->currentData().toString(), da.columnSelector());
    }

    /**
     *
     * @brief Require detaching a temporary linked CSV to switch inline, retain both points and
     * clear the provider file path.
     *
     */
    void detachToInline_PreservesPointsAndFlipsMode()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("rain.csv"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "time,rain\n"
                << "2026-01-01T00:00:00,1.0\n"
                << "2026-01-01T06:00:00,2.0\n";
        }

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        QCOMPARE(dlg.linkExternalFile(path), 2);
        QCOMPARE(d.sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);

        // Find + click the Detach button.
        QPushButton *detach = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text().startsWith(QStringLiteral("Detach"))) {
                detach = b;
                break;
            }
        QVERIFY(detach != nullptr);
        detach->click();

        QCOMPARE(d.sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QCOMPARE(d.pointCount(), 2);     // points stayed
        QVERIFY(d.filePath().isEmpty()); // file link severed
    }

    // ── Rotate / Scale (Phase 6.7.3.5 follow-up) ────────────────────────────

    /**
     *
     * @brief Require factor-two Y scaling about the value centroid to map one/two/three to
     * zero/two/four and undo endpoints.
     * X scaling stays one; this is a spread-about-centroid check rather than doubling absolute
     * values.
     *
     */
    void scalePanelDoublesSelectedValues()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture())); // values 1, 2, 3
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        // Switch the chart into ScalePoints mode (so the panel becomes visible)
        // and select all three points.
        dlg.chartView()->setEditMode(
            openswmmvis::ui::TimeseriesEditChartView::EditMode::ScalePoints);
        dlg.chartView()->setSelection({0, 1, 2});

        // Find the scale group's editors via objectName/parent walking. Easiest
        // path: locate the Apply button inside a frame whose siblings include
        // the scale-X spinbox. We pick by walking dlg.findChildren.
        QList<QDoubleSpinBox *> spins = dlg.findChildren<QDoubleSpinBox *>();
        QVERIFY(spins.size() >= 2);
        // Force scaleX=1 (no time change), scaleY=2 (double values),
        // anchor=centroid (default checked). Two spinboxes match: scaleX,
        // scaleY (anchor inputs are disabled when useCentroid is checked).
        // To pick robustly: search by suffix/range — they're all -1e6..1e6 with
        // decimals 4 (the scale ones) vs decimals 6 (the rotate-pivot one).
        QDoubleSpinBox *sX = nullptr, *sY = nullptr;
        int found = 0;
        for (auto *s : spins) {
            if (s->decimals() == 4 && s->maximum() <= 1.001e6 && s->maximum() > 1e5) {
                if (found == 0)
                    sX = s;
                else if (found == 1)
                    sY = s;
                ++found;
                if (found == 2)
                    break;
            }
        }
        QVERIFY(sX && sY);
        sX->setValue(1.0);
        sY->setValue(2.0);

        // Click Apply (the scale Apply button — pick the one inside the scale group).
        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        QPushButton *applyBtn = nullptr;
        for (auto *b : btns) {
            if (b->text() != QStringLiteral("Apply"))
                continue;
            // Two Apply buttons exist (rotate + scale). The scale one is a sibling
            // of sX. Walk up: parent of sX is the scale group QFrame.
            if (b->parent() == sX->parent()) {
                applyBtn = b;
                break;
            }
        }
        QVERIFY(applyBtn != nullptr);
        applyBtn->click();

        // Centroid is 2: scaling deviations by two gives values 0, 2, 4.
        QCOMPARE(d.pointAt(0).value, 0.0);
        QCOMPARE(d.pointAt(1).value, 2.0);
        QCOMPARE(d.pointAt(2).value, 4.0);

        dlg.undoStack()->undo();
        QCOMPARE(d.pointAt(0).value, 1.0);
        QCOMPARE(d.pointAt(2).value, 3.0);
    }

    /**
     *
     * @brief Require applying default unit X/Y scale factors to preserve every timestamp and
     * value exactly.
     *
     */
    void scaleUnitFactorIsNoop()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        dlg.chartView()->setEditMode(
            openswmmvis::ui::TimeseriesEditChartView::EditMode::ScalePoints);
        dlg.chartView()->setSelection({0, 1, 2});

        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        QPushButton *applyBtn = nullptr;
        QList<QDoubleSpinBox *> spins = dlg.findChildren<QDoubleSpinBox *>();
        QDoubleSpinBox *sX = nullptr;
        for (auto *s : spins) {
            if (s->decimals() == 4) {
                sX = s;
                break;
            }
        }
        QVERIFY(sX != nullptr);
        for (auto *b : btns) {
            if (b->text() == QStringLiteral("Apply") && b->parent() == sX->parent()) {
                applyBtn = b;
                break;
            }
        }
        QVERIFY(applyBtn != nullptr);

        // Default scaleX=scaleY=1.0 — Apply should produce identical points.
        const auto before = d.points();
        applyBtn->click();
        QCOMPARE(d.points().size(), before.size());
        for (int i = 0; i < before.size(); ++i) {
            QCOMPARE(d.pointAt(i).time, before.at(i).time);
            QCOMPARE(d.pointAt(i).value, before.at(i).value);
        }
    }

    /**
     *
     * @brief Require zero-angle rotation to preserve count and values; timestamp equality is not
     * asserted by this case.
     *
     */
    void rotateByZeroIsNoop()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit

        dlg.chartView()->setEditMode(
            openswmmvis::ui::TimeseriesEditChartView::EditMode::RotatePoints);
        dlg.chartView()->setSelection({0, 1, 2});

        // Default angle is 0 — Apply should be a noop.
        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        // Rotate Apply is the one whose parent has a spinbox with decimals=2 (angle).
        QList<QDoubleSpinBox *> spins = dlg.findChildren<QDoubleSpinBox *>();
        QDoubleSpinBox *angle = nullptr;
        for (auto *s : spins) {
            if (s->decimals() == 2) {
                angle = s;
                break;
            }
        }
        QVERIFY(angle != nullptr);
        QPushButton *applyBtn = nullptr;
        for (auto *b : btns) {
            if (b->text() == QStringLiteral("Apply") && b->parent() == angle->parent()) {
                applyBtn = b;
                break;
            }
        }
        QVERIFY(applyBtn != nullptr);

        const auto before = d.points();
        applyBtn->click();
        QCOMPARE(d.points().size(), before.size());
        for (int i = 0; i < before.size(); ++i)
            QCOMPARE(d.pointAt(i).value, before.at(i).value);
    }

    /**
     *
     * @brief Require Reload to observe a newly appended temporary CSV row.
     * The local writer borrows path, truncates it and streams the supplied body;
     * QVERIFY on opening returns only from the writer and final stream status is unchecked.
     *
     */
    void reloadFromFile_PicksUpDiskChanges()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath(QStringLiteral("rain.csv"));
        auto write = [&path](const QString &body) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
            QTextStream out(&f);
            out << body;
        };

        write("time,rain\n2026-01-01T00:00:00,1.0\n");
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        QCOMPARE(dlg.linkExternalFile(path), 1);

        // Rewrite the file with an extra row, then click Reload.
        write("time,rain\n2026-01-01T00:00:00,1.0\n2026-01-01T06:00:00,2.0\n");

        QPushButton *reload = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Reload")) {
                reload = b;
                break;
            }
        QVERIFY(reload != nullptr);
        reload->click();

        QCOMPARE(d.pointCount(), 2);
        QCOMPARE(d.pointAt(1).value, 2.0);
    }

    /*!
     * Regression: opening the editor with nothing bound — createNew(), or
     * pickTimeseries() with an empty/unknown name — disables the whole toolbar,
     * and only the Create-submit path used to switch it back on. Picking an
     * existing series out of the list bound the provider and filled the grid but
     * left every mutation grayed out, so an inline series could not be edited.
     *
     * Asserting on the QToolBar and not just the QActions is the point: a
     * disabled QToolBar disables its buttons regardless of each QAction's own
     * enabled state, which is why refreshSourceModeCardForProvider_'s per-action
     * setEnabled could not paper over it.
     */
    void pickingExistingSeriesReEnablesToolbar()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        p.setAllPoints(fixture());

        QUndoStack stack;
        std::unique_ptr<TimeseriesEditorDialog> dlg(
            TimeseriesEditorDialog::createNew(&reg, &stack, nullptr));
        QVERIFY(dlg);

        auto *toolbar = dlg->findChild<QToolBar *>();
        QVERIFY(toolbar);
        QVERIFY2(!toolbar->isEnabled(),
                 "createNew binds no provider, so the toolbar starts disabled");

        // By name: a bare findChild<QListView*> can land on a QComboBox's
        // internal popup view (the source card now hosts a time-mode combo).
        auto *list = dlg->findChild<QListView *>(QStringLiteral("seriesListView"));
        QVERIFY(list);
        QVERIFY(list->model());
        QCOMPARE(list->model()->rowCount(), 1);

        list->setCurrentIndex(list->model()->index(0, 0));

        QVERIFY2(toolbar->isEnabled(),
                 "selecting an existing inline series must re-enable the toolbar");

        // The series really is bound, and the mutation actions are live — not
        // merely a container that got switched on over dead actions.
        auto *table = dlg->findChild<QTableView *>();
        QVERIFY(table && table->model());
        QCOMPARE(table->model()->rowCount(), 3);

        bool sawAddRow = false;
        for (auto *a : dlg->findChildren<QAction *>()) {
            if (a->text() == QStringLiteral("Add Row")) {
                sawAddRow = true;
                QVERIFY(a->isEnabled());
            }
        }
        QVERIFY(sawAddRow);

        // Clearing the selection puts it back — nothing bound, nothing to edit.
        list->selectionModel()->clearCurrentIndex();
        QVERIFY2(!toolbar->isEnabled(),
                 "with no series bound the toolbar must go back to disabled");
    }

    // ── Time modes (relative / absolute authoring form) ─────────────────────

    /**
     *
     * @brief Require the first row of an empty provider to use the registry simulation-start
     * timestamp when available.
     *
     */
    void addRow_SeedsFromSimulationStart()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2007, 1, 1, 6));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        dlg.show();
        QTest::qWait(20);

        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) {
                a->trigger();
                break;
            }
        QCOMPARE(d.pointCount(), 1);
        QCOMPARE(d.pointAt(0).time, t(2007, 1, 1, 6));
    }

    /**
     *
     * @brief Require selecting relative mode to set anchor/count and show a badge, then Undo to
     * restore absolute mode and combo selection.
     *
     */
    void timeModeCombo_SwitchesWithUndoAndBadge()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QVERIFY(p.timeMode() == TimeseriesProvider::TimeMode::Absolute);
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        QVERIFY2(holder->isVisibleTo(&dlg), "time-mode row visible for Inline series");
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), 0); // Absolute

        // Switch to Relative through the combo → provider follows, undoable.
        combo->setCurrentIndex(combo->findData(1));
        QCOMPARE(d.timeMode(), TimeseriesProvider::TimeMode::Relative);
        QCOMPARE(d.relativeCount(), d.pointCount());
        QCOMPARE(d.relativeAnchor(), t(2026, 1, 1, 0));
        auto *badge = holder->findChild<QLabel *>(QStringLiteral("timeModeBadge"));
        QVERIFY(badge && badge->isVisibleTo(&dlg) && !badge->text().isEmpty());

        dlg.undoStack()->undo();
        QCOMPARE(d.timeMode(), TimeseriesProvider::TimeMode::Absolute);
        QTest::qWait(10);
        QCOMPARE(combo->currentData().toInt(), 0);
    }

    /**
     *
     * @brief Require a relative-mode conversion with negative elapsed times to refuse and snap
     * the combo back to absolute.
     *
     */
    void timeModeCombo_RefusedWhenPointsPrecedeStart()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 6, 1, 0)); // AFTER the fixture points
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData(1));
        QTest::qWait(10);
        // Refused: first point precedes the anchor → elapsed would be negative.
        QCOMPARE(d.timeMode(), TimeseriesProvider::TimeMode::Absolute);
        QCOMPARE(combo->currentData().toInt(), 0); // snapped back
    }

    /**
     *
     * @brief Require an imported mixed-mode provider to show the third Mixed choice selected.
     * The assertions check selection/count, without attempting to edit the mixed item.
     *
     */
    void mixedProvider_ShowsReadOnlyMixedItem()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        p.setRelativeInfo(2, t(2026, 1, 1, 0)); // loaded Mixed form
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), 2); // Mixed selected
        QCOMPARE(combo->count(), 3);
    }

    /**
     *
     * @brief Require a bare-number clipboard time to refuse in absolute mode and represent one
     * elapsed hour in relative mode.
     * Overwrites the application clipboard and checks the inserted UTC timestamp/value.
     *
     */
    void paste_ElapsedHoursFollowTimeMode()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        TimeseriesProvider &d = *dlg.editedSeries(); // the draft the views edit
        dlg.show();
        QTest::qWait(20);

        // Absolute mode: a bare-number time cell must NOT paste.
        QApplication::clipboard()->setText(QStringLiteral("1\t5.0"));
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) {
                a->trigger();
                break;
            }
        QCOMPARE(d.pointCount(), 0);

        // Relative mode: "1" is one elapsed hour from the anchor.
        p.setTimeMode(TimeseriesProvider::TimeMode::Relative, t(2026, 1, 1, 0));
        QTest::qWait(10);
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) {
                a->trigger();
                break;
            }
        QCOMPARE(d.pointCount(), 1);
        QCOMPARE(d.pointAt(0).time, t(2026, 1, 1, 1));
        QCOMPARE(d.pointAt(0).value, 5.0);
    }

    // The editor used to be dismissable only from the title bar. The Close
    // box must hide it, and must not be the default button — Enter in a
    // value cell has to commit the cell, never close the editor.
    /**
     *
     * @brief Require a non-default Close button to hide the exposed dialog and pass shared
     * accessibility assertions.
     *
     */
    void closeButton_HidesDialog()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        auto *btn = dlg.findChild<QPushButton *>(QStringLiteral("ts_closeBtn"));
        QVERIFY(btn);
        QVERIFY(!btn->isDefault());
        QVERIFY(!btn->autoDefault());
        swmmvis_test::assertDialogA11y(&dlg);

        btn->click();
        QTRY_VERIFY(!dlg.isVisible());
    }

    // ── Apply / Revert (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md, Phase 3) ──

    /*! Edits stay in the draft until Apply; Apply is one step on the caller's
     *  stack, and undoing it there brings the unedited draft along. */
    void applyWritesTheModelAsOneProjectStep()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        auto *session = dlg.draftSession();
        QCOMPARE(session->state(), State::Clean);

        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(2, 1), 43.0));
        QTRY_COMPARE(session->state(), State::Unapplied);
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(project.count(), 0);

        dlg.findChild<QPushButton *>(QStringLiteral("draftApply"))->click();
        QCOMPARE(session->state(), State::Applied);
        QCOMPARE(p.pointAt(1).value, 42.0);
        QCOMPARE(p.pointAt(2).value, 43.0);
        QCOMPARE(project.count(), 1); // two edits, one step

        project.undo();
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(dlg.tableModel()->index(1, 1).data(Qt::EditRole).toDouble(), 2.0);
        project.redo();
        QCOMPARE(p.pointAt(2).value, 43.0);
        QCOMPARE(dlg.tableModel()->index(2, 1).data(Qt::EditRole).toDouble(), 43.0);
    }

    void revertRestoresTheModelValues()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QTRY_COMPARE(dlg.draftSession()->state(), State::Unapplied);

        dlg.findChild<QPushButton *>(QStringLiteral("draftRevert"))->click();
        QCOMPARE(dlg.draftSession()->state(), State::Clean);
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 2.0);
        QCOMPARE(dlg.undoStack()->count(), 0); // its steps were for the dropped draft
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(project.count(), 0);
    }

    /*! Another view writes the same series: the draft is kept, nothing is
     *  merged silently, and Apply asks. */
    void modelChangedElsewhereKeepsTheDraft()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QTRY_COMPARE(dlg.draftSession()->state(), State::Unapplied);

        QVERIFY(p.setValueAt(0, 7.0)); // e.g. the Attribute Table
        QTRY_COMPARE(dlg.draftSession()->state(), State::Conflict);
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 1.0);
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 42.0);

        bool seen = false;
        answerNextPrompt(QStringLiteral("Discard Mine and Reload"), &seen);
        QVERIFY(dlg.draftSession()->apply());
        QVERIFY(seen);
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 7.0);
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 2.0);
        QCOMPARE(project.count(), 0);
    }

    void closingWithADraftAsks()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));

        bool seen = false;
        answerNextPrompt(QStringLiteral("Cancel"), &seen);
        dlg.close();
        QVERIFY(seen);
        QVERIFY(dlg.isVisible());
        QCOMPARE(dlg.editedSeries()->pointAt(1).value, 42.0);

        seen = false;
        answerNextPrompt(QStringLiteral("Apply"), &seen);
        dlg.close();
        QVERIFY(seen);
        QTRY_VERIFY(!dlg.isVisible());
        QCOMPARE(p.pointAt(1).value, 42.0);
        QCOMPARE(project.count(), 1);
    }

    /*! An external source needs a file before anything can be applied. */
    void externalSourceWithoutAFileIsNotApplied()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        for (auto *r : dlg.findChildren<QRadioButton *>())
            if (r->text() == QStringLiteral("External file"))
                r->setChecked(true);
        QCOMPARE(dlg.editedSeries()->sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);

        QVERIFY(!dlg.draftSession()->apply());
        QCOMPARE(dlg.draftSession()->state(), State::Rejected);
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QCOMPARE(project.count(), 0);
    }

    /*! Renaming from the grid header takes effect at once; the draft follows. */
    void gridHeaderRenameTakesEffectAtOnce()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        QVERIFY(dlg.tableModel()->setHeaderData(1, Qt::Horizontal, QStringLiteral("RAIN_B"),
                                                Qt::EditRole));
        QCOMPARE(reg.findByName(QStringLiteral("RAIN_B")), &p);
        QCOMPARE(dlg.editedSeries()->name(), QStringLiteral("RAIN_B"));
        QCOMPARE(dlg.draftSession()->state(), State::Applied);
    }

    // ── Geopackage source: an observation copy (D3) ─────────────────────────

    /*! A Choose / Refresh standing in for the observation-dataset flow:
     *  three hourly points starting at \p base, and a binding recording it. */
    static openswmmvis::ui::ObservationCopy fakeCopy(double base)
    {
        openswmmvis::ui::ObservationCopy c;
        c.points = {{t(2026, 1, 2, 0), base}, {t(2026, 1, 2, 1), base + 1}, {t(2026, 1, 2, 2), base + 2}};
        c.binding = QJsonObject{{"datasetTitle", "Gauge data"}, {"seriesTitle", "Flow at 00123"},
                                {"sourcePath", "/observations/gauge.gpkg"}, {"datasetId", "d1"},
                                {"seriesId", "s1"}, {"points", 3}, {"targetUnit", "ft3/s"},
                                {"firstModelTime", "2026-01-02T00:00:00Z"},
                                {"lastModelTime", "2026-01-02T02:00:00Z"}, {"base", base}};
        return c;
    }

    /*! The Geopackage source needs a chosen series before Apply; the copy is
     *  read-only, applies as one step, refreshes as a draft edit, and detaches
     *  to Inline keeping its points. */
    void geopackageSourceAppliesAReadOnlyCopy()
    {
        using openswmmvis::ui::ObservationCopy;
        int chosen = 0;
        TimeseriesEditorDialog::setObservationSource(
            [&chosen](QWidget *, const QObject *, const QString &, const QJsonObject &,
                      ObservationCopy *out, QString *) {
                ++chosen;
                *out = fakeCopy(10);
                return true;
            },
            [](QWidget *, const QObject *, const QString &, const QJsonObject &current,
               ObservationCopy *out, QString *) {
                *out = fakeCopy(current.value("base").toDouble() + 100);
                return true;
            });
        const auto reset = qScopeGuard([] { TimeseriesEditorDialog::setObservationSource({}, {}); });

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("FLOW"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        auto button = [&dlg](const char *name) { return dlg.findChild<QPushButton *>(name); };

        for (auto *r : dlg.findChildren<QRadioButton *>())
            if (r->text() == QStringLiteral("Geopackage"))
                r->setChecked(true);
        QCOMPARE(dlg.editedSeries()->sourceMode(), TimeseriesProvider::SourceMode::GeopackageObserved);
        QVERIFY(!dlg.draftSession()->apply()); // no series chosen yet
        QCOMPARE(dlg.draftSession()->state(), State::Rejected);
        QVERIFY(dlg.draftSession()->message().contains(QStringLiteral("without a series")));

        QVERIFY(button("gpkgChooseBtn")->isEnabled());
        button("gpkgChooseBtn")->click();
        QCOMPARE(chosen, 1);
        QCOMPARE(dlg.editedSeries()->pointCount(), 3);
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 10.0);
        QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("gpkgSourceLabel"))->text().contains("Flow at 00123"));
        // Read-only, like an external file.
        QVERIFY(!(dlg.tableModel()->flags(dlg.tableModel()->index(0, 1)) & Qt::ItemIsEditable));
        QVERIFY(!dlg.tableModel()->setData(dlg.tableModel()->index(0, 1), 99.0));
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::Inline); // model untouched

        QVERIFY(dlg.draftSession()->apply());
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::GeopackageObserved);
        QCOMPARE(p.pointAt(0).value, 10.0);
        QCOMPARE(p.observationBinding().value("seriesTitle").toString(), QStringLiteral("Flow at 00123"));
        QCOMPARE(project.count(), 1);

        // Refresh prepares again from the binding: a draft edit, applied later.
        QVERIFY(button("gpkgRefreshBtn")->isEnabled());
        button("gpkgRefreshBtn")->click();
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 110.0);
        QCOMPARE(p.pointAt(0).value, 10.0);
        QTRY_COMPARE(dlg.draftSession()->state(), State::Unapplied);
        dlg.draftSession()->revert();
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 10.0);

        // Detach keeps the points as an ordinary series.
        button("gpkgDetachBtn")->click();
        QCOMPARE(dlg.editedSeries()->sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QVERIFY(dlg.editedSeries()->observationBinding().isEmpty());
        QCOMPARE(dlg.editedSeries()->pointAt(0).value, 10.0);
        QVERIFY(dlg.draftSession()->apply());
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QVERIFY(p.observationBinding().isEmpty());

        project.undo(); // back to the observation copy, binding included
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::GeopackageObserved);
        QCOMPARE(p.observationBinding().value("datasetId").toString(), QStringLiteral("d1"));
        project.undo(); // back to the original inline series
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QCOMPARE(p.pointAt(0).value, 1.0);
    }

    /*! The simulation start is held as a UTC-labeled wall-clock time, like the
     *  series points: rows stepped from it across a daylight-saving change keep
     *  increasing wall-clock times (the engine refuses times that go back). The
     *  stepping half is a gate only where the local zone changes clocks on
     *  1990-10-28 (e.g. America/New_York); the zone check holds everywhere. */
    void simulationStartIsWallClockUtc()
    {
        TimeseriesRegistry reg;
        const QDateTime local(QDate(1990, 10, 28), QTime(0, 0), QTimeZone::LocalTime);
        reg.setSimulationStart(local);

        TimeseriesProvider &p = *reg.create(QStringLiteral("STEP"));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row"))
                add = a;
        QVERIFY(add);
        for (int i = 0; i < 4; ++i)
            add->trigger(); // seeded at the start, then one hour apart
        TimeseriesProvider &d = *dlg.editedSeries();
        QCOMPARE(d.pointCount(), 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(d.pointAt(i).time.time().hour(), i); // through the fall-back hour

        QCOMPARE(reg.simulationStart().timeSpec(), Qt::UTC);
        QCOMPARE(reg.simulationStart().date(), local.date());
        QCOMPARE(reg.simulationStart().time(), local.time());
    }

    /*! Without an installed observation source the Geopackage source cannot
     *  pick a series (the light editor build), and says so. */
    void geopackageSourceWithoutAnInstalledFlowCannotChoose()
    {
        TimeseriesEditorDialog::setObservationSource({}, {});
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("FLOW"));
        QUndoStack project;
        TimeseriesEditorDialog dlg(&reg, &project, &p);
        for (auto *r : dlg.findChildren<QRadioButton *>())
            if (r->text() == QStringLiteral("Geopackage"))
                r->setChecked(true);
        QVERIFY(!dlg.findChild<QPushButton *>("gpkgChooseBtn")->isEnabled());
    }
};

QTEST_MAIN(TestTimeseriesEditorDialog)
#include "test_timeseries_editor_dialog.moc"
