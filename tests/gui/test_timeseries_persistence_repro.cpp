/**
 * @file      test_timeseries_persistence_repro.cpp
 * @brief     Do time series edits made in the editor dialog reach a saved
 *            .inp and a saved GeoPackage?
 *            (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md, section 6)
 *
 * Each case edits a series through TimeseriesEditorDialog, applies it as a user
 * would, saves through the real SWMMVisProjectWindow::saveAs, reopens the saved
 * file in a fresh window and checks the edit is there. Phase 0 recorded these
 * outcomes; Phase 3 asserts them. The GeoPackage rows for relative time, an
 * external file and a file holding run rows need the engine GeoPackage fixes
 * (track E) in the engine the GUI links. Outcomes are written to
 * workplans/data-object-apply/phase3/review/results_table.md together with
 * every saved file.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/panels/timeseriestablemodel.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRadioButton>
#include <QTableView>
#include <QTest>
#include <QTextStream>
#include <QUndoStack>

#include <functional>

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::ui::TimeseriesEditorDialog;

namespace
{

/** @brief Fixture directory (tests/gui/data). */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Reviewable output folder: &lt;repo&gt;/workplans/data-object-apply/phase3/review. */
QString outDir()
{
    QDir d(dataDir()); // tests/gui/data
    d.cdUp();
    d.cdUp();
    d.cdUp(); // repo root
    const QString out = d.filePath(QStringLiteral("workplans/data-object-apply/phase3/review"));
    QDir().mkpath(out);
    return out;
}

/** @brief One-line summary of a series for the results table. */
QString describe(const TimeseriesProvider *p)
{
    if (!p)
        return QStringLiteral("MISSING");
    static const char *src[] = {"Inline", "ExternalFile", "Geopackage"};
    static const char *tm[] = {"Absolute", "Relative", "Mixed"};
    QString s = QStringLiteral("%1/%2 n=%3")
                    .arg(src[int(p->sourceMode())], tm[int(p->timeMode())])
                    .arg(p->pointCount());
    for (int i = 0; i < qMin(3, p->pointCount()); ++i)
        s += QStringLiteral(" %1=%2")
                 .arg(p->pointAt(i).time.toString(QStringLiteral("MM-dd HH:mm")))
                 .arg(p->pointAt(i).value);
    if (!p->filePath().isEmpty())
        s += QStringLiteral(" file=") + QFileInfo(p->filePath()).fileName();
    return s;
}

TimeseriesRegistry *registryOf(SWMMVisProjectWindow *w)
{
    return qobject_cast<TimeseriesRegistry *>(w->modelLayer()->ensureTimeseriesRegistry());
}

SWMMVisProjectWindow *openWindow(const QString &path, QString *why)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(workspace, path, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = QStringLiteral("loadModel failed: %1").arg(errors.join(QLatin1String("; ")));
        delete window;
        return nullptr;
    }
    return window;
}

struct Row {
    QString tag, ext, inGui, reopened, verdict, note, expected;
};
QList<Row> g_rows;

/** @brief What a case did and how to judge the reopened series. */
struct Edit {
    QString id;                                              ///< series judged after reopen
    QString note;                                            ///< free text for the table
    std::function<bool(const TimeseriesProvider *)> present; ///< true when the edit survived
    bool closeEditorBeforeSave = false;
    bool saveTwiceClosingBetween = false;
    QString expected = QStringLiteral("persisted"); ///< asserted verdict; empty = record only
};

/** @brief Apply the editor's drafts as the user would; reports a refusal in \p err. */
void applyEdits(TimeseriesEditorDialog *dlg, QString *err)
{
    if (dlg && !dlg->draftSession()->apply())
        *err = QStringLiteral("Apply refused: ") + dlg->draftSession()->message();
}

/** @brief Close every editor without prompting: unapplied drafts are discarded. */
void closeEditors(QWidget *host)
{
    for (auto *d : host->findChildren<TimeseriesEditorDialog *>()) {
        d->draftSession()->revert();
        d->close();
    }
}

using EditFn =
    std::function<Edit(SWMMVisProjectWindow *, QWidget *host, QUndoStack *, QString *err)>;

TimeseriesEditorDialog *openEditor(SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                                   const QString &id)
{
    ComprehensiveEditorRegistry::instance().openForObject(SWMMModelLayer::DataTimeSeries,
                                                          w->modelLayer(), undo, id, host);
    for (auto *dialog : host->findChildren<TimeseriesEditorDialog *>())
        if (dialog->isVisible() && dialog->currentName() == id)
            return dialog;
    return nullptr;
}

/** @brief Run one case for one file extension and append a row. Returns a harness error or empty. */
QString runCase(const QString &tag, const QString &ext, const EditFn &fn)
{
    const QString src = outDir() + QStringLiteral("/%1_%2_src.inp").arg(tag, ext);
    const QString saved = outDir() + QStringLiteral("/%1_saved.%2").arg(tag, ext);
    QFile::remove(src);
    QFile::remove(saved);
    if (!QFile::copy(QDir(dataDir()).filePath(QStringLiteral("single_rain_series.inp")), src))
        return QStringLiteral("cannot copy fixture");

    Row row{tag, ext, {}, {}, {}, {}};
    QString why;
    {
        SWMMVisProjectWindow *w = openWindow(src, &why);
        if (!w)
            return why;
        auto *host = new QWidget;
        QUndoStack undo;
        QString err;
        const Edit edit = fn(w, host, &undo, &err);
        if (!err.isEmpty()) {
            delete host;
            delete w;
            return err;
        }
        row.note = edit.note;
        row.expected = edit.expected;
        row.inGui = describe(registryOf(w)->findByName(edit.id));
        if (edit.closeEditorBeforeSave)
            closeEditors(host);
        QString saveErr;
        bool ok = w->saveAs(saved, &saveErr);
        if (ok && edit.saveTwiceClosingBetween) {
            closeEditors(host);
            ok = w->saveAs(saved, &saveErr);
        }
        if (!ok)
            row.note += QStringLiteral("; saveAs FAILED: ") + saveErr;
        for (const QString &warning : w->lastSaveWarnings())
            if (warning.contains(QStringLiteral("Time series")))
                row.note += QStringLiteral("; Save warned: ") + warning;
        closeEditors(host);
        delete host;
        delete w;

        if (ok) {
            SWMMVisProjectWindow *r = openWindow(saved, &why);
            if (!r) {
                row.reopened = QStringLiteral("REOPEN FAILED: ") + why;
                row.verdict = QStringLiteral("LOST");
            } else {
                const TimeseriesProvider *p = registryOf(r)->findByName(edit.id);
                row.reopened = describe(p);
                row.verdict = edit.present(p) ? QStringLiteral("persisted") : QStringLiteral("LOST");
                delete r;
            }
        } else {
            row.verdict = QStringLiteral("SAVE FAILED");
        }
    }
    g_rows.append(row);
    return {};
}

/** @brief runCase, then compare the outcome with the case's expectation. */
QString runAndCheck(const QString &tag, const QString &ext, const EditFn &fn)
{
    const QString harness = runCase(tag, ext, fn);
    if (!harness.isEmpty())
        return tag + QStringLiteral(": ") + harness;
    const Row &r = g_rows.last();
    if (!r.expected.isEmpty() && r.verdict != r.expected)
        return QStringLiteral("%1 .%2: expected %3, got %4 (reopened: %5; %6)")
            .arg(r.tag, r.ext, r.expected, r.verdict, r.reopened, r.note);
    if (r.tag == QStringLiteral("zero_rows") && !r.note.contains(QStringLiteral("Save warned")))
        return QStringLiteral("zero_rows .%1: Save did not warn about the empty series").arg(r.ext);
    return {};
}

#define CHECK_CASE(tag, fn)                                                                   \
    do {                                                                                      \
        const QString problem = runAndCheck(QStringLiteral(tag), ext, fn);                    \
        QVERIFY2(problem.isEmpty(), qPrintable(problem));                                     \
    } while (false)

bool valueAt(const TimeseriesProvider *p, int i, double v)
{
    return p && p->pointCount() > i && qFuzzyCompare(p->pointAt(i).value, v);
}

const QString kSeries = QStringLiteral("TSERIES1");

} // namespace

class TestTimeseriesPersistenceRepro : public QObject
{
    Q_OBJECT
private slots:
    void cases_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::newRow("inp") << QStringLiteral("inp");
        QTest::newRow("gpkg") << QStringLiteral("gpkg");
    }

    void cases()
    {
        QFETCH(QString, ext);

        // 1. Inline value edit through the grid, editor left open during Save.
        EditFn inlineEdit = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                               QString *err) {
            auto *dlg = openEditor(w, host, undo, kSeries);
            if (!dlg || !dlg->tableModel()->setData(dlg->tableModel()->index(0, 1), 5.5))
                *err = QStringLiteral("inline edit refused");
            applyEdits(dlg, err);
            return Edit{kSeries, QStringLiteral("row 0 value 0.0 -> 5.5, Apply, editor open at Save"),
                        [](const TimeseriesProvider *p) { return valueAt(p, 0, 5.5); }};
        };
        CHECK_CASE("inline_edit", inlineEdit);

        // 1b. The same edit NOT applied: Save writes the applied state only.
        EditFn unapplied = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                              QString *err) {
            auto *dlg = openEditor(w, host, undo, kSeries);
            if (!dlg || !dlg->tableModel()->setData(dlg->tableModel()->index(0, 1), 5.5))
                *err = QStringLiteral("inline edit refused");
            Edit e{kSeries, QStringLiteral("row 0 value -> 5.5, NOT applied, editor open at Save"),
                   [](const TimeseriesProvider *p) { return valueAt(p, 0, 5.5); }};
            e.expected = QStringLiteral("LOST"); // by design (D1): drafts are not saved
            return e;
        };
        CHECK_CASE("draft_not_applied", unapplied);

        // 2. Same edit, editor closed before Save.
        EditFn closedFirst = [&](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                                 QString *err) {
            Edit e = inlineEdit(w, host, undo, err);
            e.note = QStringLiteral("row 0 value -> 5.5, Apply, editor closed before Save");
            e.closeEditorBeforeSave = true;
            return e;
        };
        CHECK_CASE("inline_edit_closed", closedFirst);

        // 3. Save, close the editor, save again.
        EditFn saveCloseSave = [&](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                                   QString *err) {
            Edit e = inlineEdit(w, host, undo, err);
            e.note = QStringLiteral("row 0 value -> 5.5, Apply, Save, close editor, Save again");
            e.saveTwiceClosingBetween = true;
            return e;
        };
        CHECK_CASE("save_close_save", saveCloseSave);

        // 4. A cell editor still open (value typed, Enter not pressed) at Save.
        EditFn cellOpen = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                             QString *err) {
            auto *dlg = openEditor(w, host, undo, kSeries);
            auto *view = dlg ? dlg->findChild<QTableView *>() : nullptr;
            QDoubleSpinBox *spin = nullptr;
            if (view) {
                view->setCurrentIndex(dlg->tableModel()->index(1, 1));
                view->edit(view->currentIndex());
                spin = view->findChild<QDoubleSpinBox *>();
            }
            if (spin)
                spin->setValue(7.25);
            else
                *err = QStringLiteral("no cell editor");
            applyEdits(dlg, err); // Apply hands the open cell's value to the draft first
            return Edit{kSeries,
                        QStringLiteral("row 1 value typed 7.25 in an open cell, Apply without Enter"),
                        [](const TimeseriesProvider *p) { return valueAt(p, 1, 7.25); }};
        };
        CHECK_CASE("cell_open_at_apply", cellOpen);

        // 5. New relative-time (elapsed) series.
        EditFn relative = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                             QString *err) {
            const QString id = QStringLiteral("REL1");
            auto *reg = registryOf(w);
            auto *p = reg->create(id);
            auto *dlg = p ? openEditor(w, host, undo, id) : nullptr;
            if (!dlg) {
                *err = QStringLiteral("cannot create/open REL1");
                return Edit{};
            }
            const QDateTime t0 = reg->simulationStart();
            auto *draft = dlg->editedSeries();
            draft->insertPoint(t0.addSecs(900), 1.0);
            draft->insertPoint(t0.addSecs(1800), 2.0);
            draft->setTimeMode(TimeseriesProvider::TimeMode::Relative, t0);
            applyEdits(dlg, err);
            Edit e{id, QStringLiteral("new series, 2 rows, time mode Relative, Apply"),
                   [](const TimeseriesProvider *q) {
                       return valueAt(q, 1, 2.0) &&
                              q->timeMode() == TimeseriesProvider::TimeMode::Relative;
                   }};
            return e;
        };
        CHECK_CASE("relative_time", relative);

        // 6. New series linked to an external file.
        EditFn external = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                             QString *err) {
            const QString id = QStringLiteral("EXT1");
            const QString dat = outDir() + QStringLiteral("/ext_series.dat");
            QFile f(dat);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << "01/18/1990 00:00 1.5\n01/18/1990 00:15 2.5\n01/18/1990 00:30 3.5\n";
            }
            f.close();
            auto *p = registryOf(w)->create(id);
            auto *dlg = p ? openEditor(w, host, undo, id) : nullptr;
            if (!dlg) {
                *err = QStringLiteral("cannot create/open EXT1");
                return Edit{};
            }
            const int n = dlg->linkExternalFile(dat);
            applyEdits(dlg, err);
            Edit e{id,
                   QStringLiteral("new series linked to ext_series.dat (%1 points parsed), Apply").arg(n),
                   [](const TimeseriesProvider *q) {
                       return q && q->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile &&
                              q->filePath().endsWith(QStringLiteral("ext_series.dat"));
                   }};
            return e;
        };
        CHECK_CASE("external_file", external);

        // 7. Geopackage source: Apply is refused until a series is chosen; an
        //    observation copy then saves as ordinary rows. (Its binding goes to
        //    the .oswp — checked in test_dataobject_draft_session.)
        EditFn gpkgMode = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                             QString *err) {
            auto *dlg = openEditor(w, host, undo, kSeries);
            QRadioButton *radio = nullptr;
            if (dlg)
                for (auto *r : dlg->findChildren<QRadioButton *>())
                    if (r->text() == QStringLiteral("Geopackage"))
                        radio = r;
            if (!radio || !radio->isEnabled()) {
                *err = QStringLiteral("no enabled Geopackage radio");
                return Edit{};
            }
            radio->click();
            if (dlg->draftSession()->apply()) {
                *err = QStringLiteral("Apply accepted a Geopackage source without a series");
                return Edit{};
            }
            const QDateTime t0 = dlg->editedSeries()->pointAt(0).time;
            openswmmvis::ui::ObservationCopy copy;
            copy.points = {{t0, 5.5}, {t0.addSecs(900), 6.5}};
            copy.binding = QJsonObject{{"datasetTitle", "fixture"}, {"seriesId", "s1"}};
            dlg->useObservationCopy(copy);
            applyEdits(dlg, err);
            return Edit{kSeries,
                        QStringLiteral("Geopackage source: Apply refused without a series; a 2-row "
                                       "observation copy applied"),
                        [](const TimeseriesProvider *p) {
                            return valueAt(p, 0, 5.5) && valueAt(p, 1, 6.5) && p->pointCount() == 2;
                        }};
        };
        CHECK_CASE("geopackage_mode", gpkgMode);

        // 9. Rename through the grid's value-column header.
        EditFn headerRename = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                                 QString *err) {
            const QString id = QStringLiteral("RENAMED1");
            auto *dlg = openEditor(w, host, undo, kSeries);
            if (!dlg || !dlg->tableModel()->setHeaderData(1, Qt::Horizontal, id, Qt::EditRole))
                *err = QStringLiteral("header rename refused");
            return Edit{id, QStringLiteral("TSERIES1 renamed RENAMED1 through the grid header"),
                        [](const TimeseriesProvider *p) { return p && p->pointCount() == 10; }};
        };
        CHECK_CASE("header_rename", headerRename);

        // 8. New series with no rows.
        EditFn zeroRow = [](SWMMVisProjectWindow *w, QWidget *host, QUndoStack *undo,
                            QString *err) {
            const QString id = QStringLiteral("EMPTY1");
            if (!registryOf(w)->create(id) || !openEditor(w, host, undo, id))
                *err = QStringLiteral("cannot create/open EMPTY1");
            Edit e{id, QStringLiteral("new series with zero rows"),
                   [](const TimeseriesProvider *p) { return p != nullptr; }};
            e.expected = QStringLiteral("LOST"); // by design: no rows, nothing to write; Save warns
            return e;
        };
        CHECK_CASE("zero_rows", zeroRow);
    }

    void gpkgSaveInPlace_data()
    {
        QTest::addColumn<bool>("priorRunRow");
        QTest::newRow("gpkg_in_place") << false;
        QTest::newRow("gpkg_in_place_after_run_rows") << true;
    }

    /*! Open a GeoPackage, edit, Save to the same file, reopen. With
     *  priorRunRow the file first gets a `simulations` row "run_1" owning the
     *  model rows — a stand-in for a file that also received run output (the
     *  output plugin was not run here; the rows are relabeled with sqlite3). */
    void gpkgSaveInPlace()
    {
        QFETCH(bool, priorRunRow);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        const QString src = outDir() + QStringLiteral("/%1_src.inp").arg(tag);
        const QString gpkg = outDir() + QStringLiteral("/%1.gpkg").arg(tag);
        for (const QString &f : {src, gpkg, gpkg + "-wal", gpkg + "-shm"})
            QFile::remove(f);
        QVERIFY(QFile::copy(QDir(dataDir()).filePath(QStringLiteral("single_rain_series.inp")), src));
        QString why, err;
        {
            SWMMVisProjectWindow *w = openWindow(src, &why);
            QVERIFY2(w, qPrintable(why));
            QVERIFY2(w->saveAs(gpkg, &err), qPrintable(err));
            delete w;
        }
        if (priorRunRow) {
            const QString script = QStringLiteral(
                "sqlite3 '%1' \"select 'update '||m.name||' set simulation_id=''run_1'';' from "
                "sqlite_master m join pragma_table_info(m.name) p on p.name='simulation_id' where "
                "m.type='table' and m.name<>'simulations';\" | sqlite3 '%1' && sqlite3 '%1' \"insert "
                "into simulations(simulation_id,name,created_at,engine_version) "
                "values('run_1','run_1','2026-11-11T00:00:00Z','repro');\"").arg(gpkg);
            QCOMPARE(QProcess::execute(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}), 0);
        }
        Row row{tag, QStringLiteral("gpkg"), {}, {}, {},
                priorRunRow ? QStringLiteral("open .gpkg whose rows belong to simulation run_1, row 0 -> 5.5, Apply, Save in place")
                            : QStringLiteral("open .gpkg, row 0 -> 5.5, Apply, Save in place"),
                QStringLiteral("persisted")};
        {
            SWMMVisProjectWindow *w = openWindow(gpkg, &why);
            QVERIFY2(w, qPrintable(why));
            QWidget host;
            QUndoStack undo;
            auto *dlg = openEditor(w, &host, &undo, kSeries);
            QVERIFY(dlg);
            QVERIFY(dlg->tableModel()->setData(dlg->tableModel()->index(0, 1), 5.5));
            QVERIFY(dlg->draftSession()->apply());
            row.inGui = describe(registryOf(w)->findByName(kSeries));
            const bool ok = w->saveAs(gpkg, &err);
            dlg->close();
            if (!ok)
                row.note += QStringLiteral("; saveAs FAILED: ") + err;
            delete w;
        }
        SWMMVisProjectWindow *r = openWindow(gpkg, &why);
        if (r) {
            const TimeseriesProvider *p = registryOf(r)->findByName(kSeries);
            row.reopened = describe(p);
            row.verdict = valueAt(p, 0, 5.5) ? QStringLiteral("persisted") : QStringLiteral("LOST");
            delete r;
        } else {
            row.reopened = QStringLiteral("REOPEN FAILED: ") + why;
            row.verdict = QStringLiteral("LOST");
        }
        g_rows.append(row);
        if (!row.expected.isEmpty())
            QCOMPARE(row.verdict, row.expected);
    }

    void cleanupTestCase()
    {
        QFile f(outDir() + QStringLiteral("/results_table.md"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        QTextStream ts(&f);
        ts << "| Case | Saved as | Edit | In the model before Save | After reopening the saved file | "
              "Outcome | Expected |\n|---|---|---|---|---|---|---|\n";
        for (const Row &r : g_rows)
            ts << "| " << r.tag << " | ." << r.ext << " | " << r.note << " | " << r.inGui << " | "
               << r.reopened << " | " << r.verdict << " | "
               << (r.expected.isEmpty() ? QStringLiteral("not asserted (engine track E)") : r.expected)
               << " |\n";
    }
};

QTEST_MAIN(TestTimeseriesPersistenceRepro)
#include "test_timeseries_persistence_repro.moc"
