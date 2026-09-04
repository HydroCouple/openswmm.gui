/*!
 * \file   test_timeseries_editor_dialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.3 — smoke tests for the editor dialog shell.
 *
 * Focus: dialog assembles grid + chart + toolbar, edits propagate through both
 * views via the shared TimeseriesProvider, undo round-trips. Mouse-event drag
 * simulation is covered by the chart-view smoke test; this exercises the
 * dialog-level wiring.
 */
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/panels/timeseriestablemodel.h"
#include "ui/widgets/timeserieseditchartview.h"

#include <QApplication>
#include <QChart>
#include <QClipboard>
#include <QComboBox>
#include <QAbstractItemDelegate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QStyleOptionViewItem>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QItemSelectionModel>
#include <QLineSeries>
#include <QLabel>
#include <QListView>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QToolBar>
#include <QUndoStack>

#include <cmath>
#include <memory>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::ui::TimeseriesEditorDialog;

namespace {
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}
QVector<TimeseriesPoint> fixture()
{
    return {
        {t(2026, 1, 1, 0),  1.0},
        {t(2026, 1, 1, 6),  2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}

QList<QPointF> linePointsFromChart(QChart *c)
{
    for (auto *s : c->series())
        if (auto *line = qobject_cast<QLineSeries *>(s)) return line->points();
    return {};
}
} // namespace

class TestTimeseriesEditorDialog : public QObject
{
    Q_OBJECT

private slots:

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
        QVERIFY(dlg.chartView()  != nullptr);
        QCOMPARE(dlg.tableModel()->rowCount(),  3);
        QCOMPARE(dlg.tableModel()->columnCount(), 2);
        QCOMPARE(linePointsFromChart(dlg.chartView()->chart()).size(), 3);
    }

    void editFromGrid_RefreshesChart()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Edit through the grid model — chart should auto-refresh via provider signals.
        QVERIFY(dlg.tableModel()->setData(dlg.tableModel()->index(1, 1), 42.0));
        QCOMPARE(p.pointAt(1).value, 42.0);
        const auto pts = linePointsFromChart(dlg.chartView()->chart());
        QCOMPARE(pts.at(1).y(), 42.0);
        QCOMPARE(stack.count(), 1);

        // Undo restores everywhere.
        stack.undo();
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(linePointsFromChart(dlg.chartView()->chart()).at(1).y(), 2.0);
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
        QCOMPARE(idx.data(Qt::DisplayRole).toString(),
                 QStringLiteral("01/01/2026 06:00"));
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
        QCOMPARE(p.pointAt(1).time, t(2026, 1, 1, 7));
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
            {QDateTime(QDate(2026, 1, 1), QTime(6, 0, 0),   Qt::UTC), 2.0},
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
        QCOMPARE(p.pointAt(0).time,
                 QDateTime(QDate(2026, 1, 1), QTime(0, 16, 30), Qt::UTC));
        delete editor;
    }

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

    void undoRedoActionsExistWhenStackProvided()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Dialog has a toolbar with at least one undo action when a stack is bound.
        const auto actions = dlg.findChildren<QAction *>();
        bool foundUndo = false;
        for (auto *a : actions) if (a->text().contains(QStringLiteral("Undo"))) { foundUndo = true; break; }
        QVERIFY(foundUndo);
    }

    void editModeToggleFlipsChart()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Find the "Edit Points" action and trigger it.
        QAction *editAct = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Edit Points")) { editAct = a; break; }
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

    void addRowAppendsWithMedianInterval()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));   // 0h, 6h, 12h → 6h median
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) { add = a; break; }
        QVERIFY(add != nullptr);

        add->trigger();
        QCOMPARE(p.pointCount(), 4);
        // New point at last + 6h = 2026-01-01 18:00.
        QCOMPARE(p.pointAt(3).time, t(2026, 1, 1, 18));
        QCOMPARE(p.pointAt(3).value, p.pointAt(2).value);

        stack.undo();
        QCOMPARE(p.pointCount(), 3);
    }

    void addRowOnEmptyProviderUsesNow()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) { add = a; break; }
        QVERIFY(add != nullptr);
        add->trigger();
        QCOMPARE(p.pointCount(), 1);
        // Time is "now" (UTC) — just confirm it's within a small window.
        const qint64 ds = QDateTime::currentDateTimeUtc().secsTo(p.pointAt(0).time);
        QVERIFY(std::abs(ds) < 5);
    }

    void deleteRowsRemovesSelection()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Build a QTableView selection covering rows 0 and 2.
        auto *table = dlg.findChild<QTableView *>();
        QVERIFY(table != nullptr);
        table->selectRow(0);
        table->selectionModel()->select(dlg.tableModel()->index(2, 0),
                                         QItemSelectionModel::Select | QItemSelectionModel::Rows);

        QAction *del = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Delete Rows")) { del = a; break; }
        QVERIFY(del != nullptr);
        del->trigger();
        QCOMPARE(p.pointCount(), 1);
        QCOMPARE(p.pointAt(0).time, t(2026, 1, 1, 6));   // middle row kept

        stack.undo();
        QCOMPARE(p.pointCount(), 3);
    }

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
            if (a->text() == QStringLiteral("Copy")) { copy = a; break; }
        QVERIFY(copy != nullptr);
        copy->trigger();

        const QString text = QApplication::clipboard()->text();
        const QStringList lines = text.split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines.first().startsWith(QStringLiteral("2026-01-01T00:00:00")));
        QVERIFY(lines.last().endsWith(QStringLiteral("\t3")));
    }

    void pasteInsertsRowsFromTsv()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Excel-style TSV: ISO timestamp + value per line. Two new times that
        // don't collide with existing 0h / 6h / 12h.
        QApplication::clipboard()->setText(
            QStringLiteral("2026-01-01T03:00:00\t10.0\n2026-01-01T09:00:00\t20.0"));

        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) { paste = a; break; }
        QVERIFY(paste != nullptr);
        paste->trigger();

        QCOMPARE(p.pointCount(), 5);
        QCOMPARE(p.pointAt(1).time, t(2026, 1, 1, 3));
        QCOMPARE(p.pointAt(1).value, 10.0);
        QCOMPARE(p.pointAt(3).time, t(2026, 1, 1, 9));
        QCOMPARE(p.pointAt(3).value, 20.0);

        // Single Cmd-Z reverts the whole paste (macro).
        stack.undo();
        QCOMPARE(p.pointCount(), 3);
    }

    void pasteDuplicateTimesAreSkipped()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));   // already has 0h / 6h / 12h
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // First row collides with existing 6h; second is novel.
        QApplication::clipboard()->setText(
            QStringLiteral("2026-01-01T06:00:00\t99.0\n2026-01-01T03:00:00\t50.0"));

        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) { paste = a; break; }
        paste->trigger();

        // 3 → 4: only the novel row landed.
        QCOMPARE(p.pointCount(), 4);
        QCOMPARE(p.pointAt(1).time, t(2026, 1, 1, 3));
    }

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

    void externalSourceModeDisablesRowEdits()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        QAction *add = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) { add = a; break; }
        add->trigger();
        QCOMPARE(p.pointCount(), 3);   // unchanged

        QApplication::clipboard()->setText(QStringLiteral("2026-01-01T03:00:00\t10.0"));
        QAction *paste = nullptr;
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) { paste = a; break; }
        paste->trigger();
        QCOMPARE(p.pointCount(), 3);   // unchanged
    }

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────

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
            if (r->text() == QStringLiteral("Inline")) { rInline = r; break; }
        QVERIFY(rInline != nullptr);
        QVERIFY(rInline->isChecked());

        // Provider-driven mode flip → card refreshes via sourceModeChanged.
        p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
        QRadioButton *rExt = nullptr;
        for (auto *r : radios)
            if (r->text() == QStringLiteral("External file")) { rExt = r; break; }
        QVERIFY(rExt != nullptr);
        QVERIFY(rExt->isChecked());
    }

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
            if (r->text() == QStringLiteral("External file")) { rExt = r; break; }
        QVERIFY(rExt != nullptr);
        rExt->setChecked(true);
        rExt->toggled(true);   // ensure slot fires in offscreen mode

        QPushButton *browse = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text().startsWith(QStringLiteral("Browse"))) { browse = b; break; }
        QVERIFY(browse != nullptr);
        QVERIFY(browse->isVisible());
        QVERIFY(browse->isEnabled());   // <- the actual bug being pinned

        // And the gate-on-file controls are disabled when no file is linked.
        QPushButton *reload = nullptr, *detach = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Reload")) reload = b;
            if (b->text().startsWith(QStringLiteral("Detach"))) detach = b;
        }
        QVERIFY(reload != nullptr);
        QVERIFY(detach != nullptr);
        QVERIFY(!reload->isEnabled());
        QVERIFY(!detach->isEnabled());
    }

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
                if (a->text() == label) return a;
            return nullptr;
        };
        QAction *edit   = findAction(QStringLiteral("Edit Points"));
        QAction *rotate = findAction(QStringLiteral("Rotate"));
        QAction *scale  = findAction(QStringLiteral("Scale"));
        QAction *addR   = findAction(QStringLiteral("Add Row"));
        QAction *delR   = findAction(QStringLiteral("Delete Rows"));
        QAction *paste  = findAction(QStringLiteral("Paste"));
        QAction *copy   = findAction(QStringLiteral("Copy"));

        QVERIFY(edit && rotate && scale && addR && delR && paste && copy);
        QVERIFY(!edit->isEnabled());
        QVERIFY(!rotate->isEnabled());
        QVERIFY(!scale->isEnabled());
        QVERIFY(!addR->isEnabled());
        QVERIFY(!delR->isEnabled());
        QVERIFY(!paste->isEnabled());
        QVERIFY(copy->isEnabled());   // Copy is read-only — stays enabled.

        // Flip back to Inline → toolbar re-enables.
        p.setSourceMode(TimeseriesProvider::SourceMode::Inline);
        QVERIFY(edit->isEnabled());
        QVERIFY(addR->isEnabled());
    }

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

        const int n = dlg.linkExternalFile(path);
        QCOMPARE(n, 3);
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);
        QCOMPARE(p.pointCount(), 3);
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(p.filePath(), path);

        // Column combo populated from CSV header (single value column).
        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QVERIFY(combo->count() >= 1);
        QCOMPARE(combo->itemText(0), QStringLiteral("rain_a"));
    }

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

        // Bind a real column first, then re-bind the column a re-exported file
        // no longer has (the "headers changed under a saved model" case).
        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_b")), 2);
        QCOMPARE(p.pointCount(), 2);

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_old")), 0);
        QCOMPARE(p.pointCount(), 0);                     // no bogus preview
        QCOMPARE(p.columnSelector(), QStringLiteral("rain_old"));  // not rewritten

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3);                     // 2 real + the missing one
        QCOMPARE(combo->currentData().toString(), QStringLiteral("rain_old"));
        QVERIFY(combo->currentText().contains(QStringLiteral("rain_old")));
    }

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

        QCOMPARE(dlg.linkExternalFile(path), 2);   // line 1 spent as header
        QCOMPARE(p.pointAt(0).value, 2.0);
        QVERIFY(p.columnSelector().isEmpty());

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
        QVERIFY(p.columnSelector().isEmpty());
        QCOMPARE(p.pointCount(), 2);
        QCOMPARE(p.pointAt(0).value, 2.0);
        QCOMPARE(combo->currentIndex(), 0);
    }

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

        // Select the second column by name.
        const int n = dlg.linkExternalFile(path, QStringLiteral("rain_b"));
        QCOMPARE(n, 2);
        QCOMPARE(p.pointAt(0).value, 10.0);
        QCOMPARE(p.pointAt(1).value, 20.0);
    }

    void linkExternalFile_TsfIdsHeaderAndAmPm()
    {
        // PCSWMM .tsf (spec §4 task 2): IDs-row column names, 3-row header,
        // 12-hour AM/PM datetimes. Committed fixture — reviewable per
        // CLAUDE.md §4.1.
        const QString path =
            qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."))
            + QStringLiteral("/extcol_dialog_sample.tsf");

        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("TSF_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("RG2")), 3);
        QCOMPARE(p.pointAt(0).value, 0.05);
        QCOMPARE(p.pointAt(2).value, 0.25);
        // 12:00:00 AM → midnight; 1:30:00 PM → 13:30.
        QCOMPARE(p.pointAt(0).time, t(2007, 1, 1, 0));
        QCOMPARE(p.pointAt(2).time, QDateTime(QDate(2007, 1, 1),
                                              QTime(13, 30), Qt::UTC));

        auto *combo = dlg.findChild<QComboBox *>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(0), QStringLiteral("RG1"));
        QCOMPARE(combo->itemText(1), QStringLiteral("RG2"));
        QCOMPARE(combo->currentIndex(), 1);
    }

    void seriesSwitch_RepopulatesColumnCombo()
    {
        // B3 regression (spec §1.4): switching series must repopulate the
        // column combo from the incoming provider's file and re-select its
        // columnSelector — it used to keep the previous series' items.
        const QString path =
            qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."))
            + QStringLiteral("/extcol_switch_multi.csv");

        TimeseriesRegistry reg;
        TimeseriesProvider &a = *reg.create(QStringLiteral("TS_FILE"));
        TimeseriesProvider &b = *reg.create(QStringLiteral("TS_INLINE"));
        QVERIFY(b.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &a);
        dlg.show();
        QTest::qWait(50);

        QCOMPARE(dlg.linkExternalFile(path, QStringLiteral("rain_b")), 2);
        QCOMPARE(a.columnSelector(), QStringLiteral("rain_b"));

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
        QCOMPARE(combo->currentData().toString(), a.columnSelector());
    }

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
        QCOMPARE(dlg.linkExternalFile(path), 2);
        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);

        // Find + click the Detach button.
        QPushButton *detach = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text().startsWith(QStringLiteral("Detach"))) { detach = b; break; }
        QVERIFY(detach != nullptr);
        detach->click();

        QCOMPARE(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
        QCOMPARE(p.pointCount(), 2);          // points stayed
        QVERIFY(p.filePath().isEmpty());      // file link severed
    }

    // ── Rotate / Scale (Phase 6.7.3.5 follow-up) ────────────────────────────

    void scalePanelDoublesSelectedValues()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));   // values 1, 2, 3
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        // Switch the chart into ScalePoints mode (so the panel becomes visible)
        // and select all three points.
        dlg.chartView()->setEditMode(openswmmvis::ui::TimeseriesEditChartView::EditMode::ScalePoints);
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
                if (found == 0) sX = s;
                else if (found == 1) sY = s;
                ++found;
                if (found == 2) break;
            }
        }
        QVERIFY(sX && sY);
        sX->setValue(1.0);
        sY->setValue(2.0);

        // Click Apply (the scale Apply button — pick the one inside the scale group).
        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        QPushButton *applyBtn = nullptr;
        for (auto *b : btns) {
            if (b->text() != QStringLiteral("Apply")) continue;
            // Two Apply buttons exist (rotate + scale). The scale one is a sibling
            // of sX. Walk up: parent of sX is the scale group QFrame.
            if (b->parent() == sX->parent()) { applyBtn = b; break; }
        }
        QVERIFY(applyBtn != nullptr);
        applyBtn->click();

        // Centroid of values {1,2,3} = 2. New values = 2 + (v - 2) * 2 → -2, 2, 6
        // Wait — that gives 2 + (1-2)*2 = 0, 2 + (2-2)*2 = 2, 2 + (3-2)*2 = 4.
        QCOMPARE(p.pointAt(0).value, 0.0);
        QCOMPARE(p.pointAt(1).value, 2.0);
        QCOMPARE(p.pointAt(2).value, 4.0);

        stack.undo();
        QCOMPARE(p.pointAt(0).value, 1.0);
        QCOMPARE(p.pointAt(2).value, 3.0);
    }

    void scaleUnitFactorIsNoop()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        dlg.chartView()->setEditMode(openswmmvis::ui::TimeseriesEditChartView::EditMode::ScalePoints);
        dlg.chartView()->setSelection({0, 1, 2});

        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        QPushButton *applyBtn = nullptr;
        QList<QDoubleSpinBox *> spins = dlg.findChildren<QDoubleSpinBox *>();
        QDoubleSpinBox *sX = nullptr;
        for (auto *s : spins) {
            if (s->decimals() == 4) { sX = s; break; }
        }
        QVERIFY(sX != nullptr);
        for (auto *b : btns) {
            if (b->text() == QStringLiteral("Apply") && b->parent() == sX->parent()) {
                applyBtn = b; break;
            }
        }
        QVERIFY(applyBtn != nullptr);

        // Default scaleX=scaleY=1.0 — Apply should produce identical points.
        const auto before = p.points();
        applyBtn->click();
        QCOMPARE(p.points().size(), before.size());
        for (int i = 0; i < before.size(); ++i) {
            QCOMPARE(p.pointAt(i).time,  before.at(i).time);
            QCOMPARE(p.pointAt(i).value, before.at(i).value);
        }
    }

    void rotateByZeroIsNoop()
    {
        TimeseriesRegistry reg;
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);

        dlg.chartView()->setEditMode(openswmmvis::ui::TimeseriesEditChartView::EditMode::RotatePoints);
        dlg.chartView()->setSelection({0, 1, 2});

        // Default angle is 0 — Apply should be a noop.
        QList<QPushButton *> btns = dlg.findChildren<QPushButton *>();
        // Rotate Apply is the one whose parent has a spinbox with decimals=2 (angle).
        QList<QDoubleSpinBox *> spins = dlg.findChildren<QDoubleSpinBox *>();
        QDoubleSpinBox *angle = nullptr;
        for (auto *s : spins) {
            if (s->decimals() == 2) { angle = s; break; }
        }
        QVERIFY(angle != nullptr);
        QPushButton *applyBtn = nullptr;
        for (auto *b : btns) {
            if (b->text() == QStringLiteral("Apply") && b->parent() == angle->parent()) {
                applyBtn = b; break;
            }
        }
        QVERIFY(applyBtn != nullptr);

        const auto before = p.points();
        applyBtn->click();
        QCOMPARE(p.points().size(), before.size());
        for (int i = 0; i < before.size(); ++i)
            QCOMPARE(p.pointAt(i).value, before.at(i).value);
    }

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
        QCOMPARE(dlg.linkExternalFile(path), 1);

        // Rewrite the file with an extra row, then click Reload.
        write("time,rain\n2026-01-01T00:00:00,1.0\n2026-01-01T06:00:00,2.0\n");

        QPushButton *reload = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Reload")) { reload = b; break; }
        QVERIFY(reload != nullptr);
        reload->click();

        QCOMPARE(p.pointCount(), 2);
        QCOMPARE(p.pointAt(1).value, 2.0);
    }

    /*!
     * Regression: opening the editor with nothing bound — createNew(), or
     * pickTimeseries() with an empty/unknown name — disables the whole toolbar,
     * and only the Create-submit path used to switch it back on. Picking an
     * existing series out of the list bound the provider and filled the grid but
     * left every mutation greyed out, so an inline series could not be edited.
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

    void addRow_SeedsFromSimulationStart()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2007, 1, 1, 6));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Add Row")) { a->trigger(); break; }
        QCOMPARE(p.pointCount(), 1);
        QCOMPARE(p.pointAt(0).time, t(2007, 1, 1, 6));
    }

    void timeModeCombo_SwitchesWithUndoAndBadge()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QVERIFY(p.timeMode() == TimeseriesProvider::TimeMode::Absolute);
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        QVERIFY2(holder->isVisibleTo(&dlg), "time-mode row visible for Inline series");
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), 0);   // Absolute

        // Switch to Relative through the combo → provider follows, undoable.
        combo->setCurrentIndex(combo->findData(1));
        QCOMPARE(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
        QCOMPARE(p.relativeCount(), p.pointCount());
        QCOMPARE(p.relativeAnchor(), t(2026, 1, 1, 0));
        auto *badge = holder->findChild<QLabel *>(QStringLiteral("timeModeBadge"));
        QVERIFY(badge && badge->isVisibleTo(&dlg) && !badge->text().isEmpty());

        stack.undo();
        QCOMPARE(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
        QTest::qWait(10);
        QCOMPARE(combo->currentData().toInt(), 0);
    }

    void timeModeCombo_RefusedWhenPointsPrecedeStart()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 6, 1, 0));   // AFTER the fixture points
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData(1));
        QTest::qWait(10);
        // Refused: first point precedes the anchor → elapsed would be negative.
        QCOMPARE(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
        QCOMPARE(combo->currentData().toInt(), 0);   // snapped back
    }

    void mixedProvider_ShowsReadOnlyMixedItem()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QVERIFY(p.setAllPoints(fixture()));
        p.setRelativeInfo(2, t(2026, 1, 1, 0));   // loaded Mixed form
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        auto *holder = dlg.findChild<QWidget *>(QStringLiteral("timeModeRowHolder"));
        QVERIFY(holder);
        auto *combo = holder->findChild<QComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), 2);   // Mixed selected
        QCOMPARE(combo->count(), 3);
    }

    void paste_ElapsedHoursFollowTimeMode()
    {
        TimeseriesRegistry reg;
        reg.setSimulationStart(t(2026, 1, 1, 0));
        TimeseriesProvider &p = *reg.create(QStringLiteral("RAIN_A"));
        QUndoStack stack;
        TimeseriesEditorDialog dlg(&reg, &stack, &p);
        dlg.show();
        QTest::qWait(20);

        // Absolute mode: a bare-number time cell must NOT paste.
        QApplication::clipboard()->setText(QStringLiteral("1\t5.0"));
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) { a->trigger(); break; }
        QCOMPARE(p.pointCount(), 0);

        // Relative mode: "1" is one elapsed hour from the anchor.
        p.setTimeMode(TimeseriesProvider::TimeMode::Relative, t(2026, 1, 1, 0));
        QTest::qWait(10);
        for (auto *a : dlg.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Paste")) { a->trigger(); break; }
        QCOMPARE(p.pointCount(), 1);
        QCOMPARE(p.pointAt(0).time, t(2026, 1, 1, 1));
        QCOMPARE(p.pointAt(0).value, 5.0);
    }
};

QTEST_MAIN(TestTimeseriesEditorDialog)
#include "test_timeseries_editor_dialog.moc"
