/**
 * @file      test_attrtableperf_baseline.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Attribute Table performance harness for large (all-pipes) models.
 *
 *         Measures, per category, the paths that were unusable at 100k+
 *         objects before the query/cell-read work:
 *
 *           - loadModel ms                         (baseline, not our target)
 *           - category bind ms                     (schema + model reset)
 *           - viewport read ms                     (what painting costs)
 *           - query Apply ms, one-column predicate (filter + selection)
 *           - query Apply ms, LIKE predicate
 *           - query Apply ms, two bounds on one numeric field
 *           - sort-by-tagged-column ms
 *           - select-all bus replacement ms        (large selection)
 *
 *         Fixtures are real all-pipes models that live OUTSIDE the repo
 *         (the corpus under tests/output/ is symlinks to them), so this
 *         self-skips when SWMMVIS_ATTR_PERF_INP names nothing readable —
 *         a fast no-op on CI, exactly like test_meshperf_baseline.
 *
 *         Point it at one model per run:
 *           SWMMVIS_ATTR_PERF_INP=tests/output/gui_perf_2026-08-13/corpus/ww_2024.inp \
 *             ctest --test-dir build -R test_attrtableperf_baseline -V
 *
 *         Timings also land in a CSV under tests/output/attrtable_perf/ so
 *         a before/after pair is reviewable rather than scraped from
 *         scrollback.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/swmmattributetablemodel.h"

#include <openswmm/engine/openswmm_nodes.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QObject>
#include <QTableView>
#include <QTest>
#include <QTextStream>

#include <memory>

namespace
{

/**
 *
 * @brief Read the optional external performance-fixture filename without opening it.
 * @return SWMMVIS_ATTR_PERF_INP verbatim, empty if unset; no canonicalization or creation.
 *
 */
QString perfInp()
{
    return qEnvironmentVariable("SWMMVIS_ATTR_PERF_INP");
}

/**
 *
 * @brief Resolve the persistent timing CSV directory without creating it.
 * @return SWMMVIS_ATTR_PERF_OUT or relative tests/output/attrtable_perf. Repeated runs
 * append to the same CSV unless the caller configures another directory.
 *
 */
QString outDir()
{
    return qEnvironmentVariable("SWMMVIS_ATTR_PERF_OUT",
                                QStringLiteral("tests/output/attrtable_perf"));
}

struct Row {
    QString model, category, phase;
    qint64 ms = 0;
    int detail = 0; //!< rows matched / rows visited — phase-specific
};

/**
 *
 * @brief Attempt to append one raw timing row and create the CSV header for an empty file.
 * @param r Borrowed phase record containing model/category/phase, elapsed milliseconds
 * and phase-specific count. String fields are not CSV-escaped.
 * Creates output directories without checking success, opens attrtable_perf.csv in append
 * text mode and silently returns if opening fails. Stream-write status is unchecked;
 * closes on return, no atomicity, synchronization across processes or failure result.
 *
 */
void appendRow(const Row &r)
{
    QDir().mkpath(outDir());
    QFile f(QDir(outDir()).filePath(QStringLiteral("attrtable_perf.csv")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream s(&f);
    if (f.size() == 0)
        s << "model,category,phase,ms,detail\n";
    s << r.model << ',' << r.category << ',' << r.phase << ',' << r.ms << ',' << r.detail << '\n';
}

/**
 *
 * @brief Retain a timing record in memory, attempt CSV append and emit the same fields to the
 * log.
 * @param rows Borrowed mutable collection receiving a copy.
 * @param r Borrowed source row copied before append/logging.
 * No success result: CSV failures can leave memory/log evidence without a persisted row.
 * Elapsed values are supplied by callers, not measured within this helper.
 *
 */
void record(QList<Row> &rows, const Row &r)
{
    rows << r;
    appendRow(r);
    qInfo().noquote() << QStringLiteral("%1,%2,%3,%4,%5")
                             .arg(r.model, r.category, r.phase)
                             .arg(r.ms)
                             .arg(r.detail);
}

//! Categories worth timing: the two that dominate an all-pipes model, plus
//! subcatchments (whose compound cells used to run engine-wide scans).
struct Cat {
    const char *comboPrefix;
    SWMMModelLayer::Category cat;
};
constexpr Cat kCats[] = {
    {"Junctions", SWMMModelLayer::CatJunctions},
    {"Conduits", SWMMModelLayer::CatConduits},
    {"Subcatchments", SWMMModelLayer::CatSubcatchments},
};

} // namespace

class TestAttrTablePerfBaseline : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Skip the performance suite unless the configured input is nonempty and exists.
     * Checks existence only, not readability or file type; actual load failure is asserted
     * later. Does not create or download the external corpus.
     *
     */
    void initTestCase()
    {
        const QString inp = perfInp();
        if (inp.isEmpty() || !QFileInfo::exists(inp))
            QSKIP("SWMMVIS_ATTR_PERF_INP not set to a readable .inp — "
                  "see the file header for how to run this.");
    }

    /**
     *
     * @brief Measure model load, category binding, viewport reads, bounded queries, sorting and
     * selection-bus replacement.
     * Loads one external input and checks category row identity for junction/conduit/
     * subcatchment when present. Records milliseconds/counts immediately, without performance
     * thresholds. The local applyQuery lambda borrows phase/query strings synchronously,
     * sets text, sends Return and records visible rows; it does not assert query validity.
     * The two-condition query uses the same numeric field twice, not two distinct columns.
     * Selection phase replaces the bus; it does not click Show selected only. No visible
     * paint timing, input-file save or query-result correctness benchmark is performed.
     *
     */
    void attributeTablePhases()
    {
        const QString inp = perfInp();
        const QString modelName = QFileInfo(inp).fileName();
        QList<Row> rows;

        QElapsedTimer t;
        t.start();
        auto layer = std::make_unique<SWMMModelLayer>(inp, nullptr);
        QList<QString> warnings, errors;
        const bool ok = layer->loadModel(warnings, errors);
        const qint64 loadMs = t.elapsed();
        QVERIFY2(
            ok, qPrintable(
                    QStringLiteral("loadModel failed: %1").arg(errors.join(QStringLiteral("; ")))));
        record(rows, Row{modelName, QStringLiteral("-"), QStringLiteral("load_model"), loadMs, 0});

        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>();
        QVERIFY(combo && view && edit);

        for (const auto &c : kCats) {
            int catIdx = -1;
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemText(i).startsWith(QLatin1String(c.comboPrefix)))
                    catIdx = i;
            if (catIdx < 0)
                continue; // model has none of this kind

            const QString cat = QString::fromLatin1(c.comboPrefix);

            // ---- bind -------------------------------------------------
            // The combo's currentIndexChanged handling reaches the model
            // through queued connections, so rowCount() read straight after
            // setCurrentIndex still reports the PREVIOUS category — which
            // silently timed the wrong table before this drain was added.
            t.restart();
            combo->setCurrentIndex(catIdx);
            QCoreApplication::processEvents();
            record(rows, Row{modelName, cat, QStringLiteral("bind"), t.elapsed(), 0});

            auto *m = view->model();
            QVERIFY(m);
            const int nRow = m->rowCount();
            const int nCol = m->columnCount();
            if (nRow == 0)
                continue;
            // Guard against the stale-category trap above coming back: the
            // proxy must now agree with the layer about this category.
            QCOMPARE(nRow, layer->categoryCount(c.cat));

            // ---- viewport read ----------------------------------------
            // What a repaint costs: every column of a screenful of rows,
            // through the same data() the delegate calls.
            const int window = qMin(nRow, 50);
            t.restart();
            for (int r = 0; r < window; ++r)
                for (int col = 0; col < nCol; ++col)
                    (void)m->data(m->index(r, col), Qt::DisplayRole);
            record(rows, Row{modelName, cat, QStringLiteral("viewport_read"), t.elapsed(),
                             window * nCol});

            // ---- queries ----------------------------------------------
            // Use bounded plain decimals. A parse error would leave the prior
            // filter unchanged and make this phase time a refused draft.
            auto applyQuery = [&](const QString &phase, const QString &q) {
                edit->setText(q);
                t.restart();
                QTest::keyClick(edit, Qt::Key_Return);
                const qint64 ms = t.elapsed();
                record(rows, Row{modelName, cat, phase, ms, view->model()->rowCount()});
            };

            const QString numField = numericFieldFor(c.cat);
            if (!numField.isEmpty()) {
                applyQuery(QStringLiteral("query_one_column"),
                           QStringLiteral("[%1] > -99999").arg(numField));
                applyQuery(QStringLiteral("query_two_column"),
                           QStringLiteral("[%1] > -99999 AND [%1] < 99999").arg(numField));
            }
            applyQuery(QStringLiteral("query_like"), QStringLiteral("Name LIKE '%'"));

            // Clear so the sort and selection phases see the full table.
            edit->setText(QString());
            QTest::keyClick(edit, Qt::Key_Return);

            // ---- sort --------------------------------------------------
            // Column 1 is the first attribute past Name, i.e. a tagged
            // column that reads through the engine.
            if (nCol > 1) {
                t.restart();
                view->sortByColumn(1, Qt::AscendingOrder);
                record(rows, Row{modelName, cat, QStringLiteral("sort_tagged_column"), t.elapsed(),
                                 nRow});
                view->sortByColumn(-1, Qt::AscendingOrder);
            }

            // ---- show selected only ------------------------------------
            // Selects the whole category through the bus, which is the
            // shape that used to build an N-name alternation regex.
            const auto type = refTypeFor(c.cat);
            if (type != SWMMObjectRef::Unknown) {
                QSet<SWMMObjectRef> all;
                all.reserve(nRow);
                for (int r = 0; r < nRow; ++r) {
                    const QString n = layer->objectNameAt(c.cat, r);
                    if (!n.isEmpty())
                        all.insert(SWMMObjectRef(type, n));
                }
                t.restart();
                selMgr.select(all, SelectionManager::Replace);
                record(rows, Row{modelName, cat, QStringLiteral("select_all_bus"), t.elapsed(),
                                 int(all.size())});
                selMgr.select(QSet<SWMMObjectRef>{}, SelectionManager::Replace);
            }
        }

        report(rows);
    }

    /**
     *
     * @brief Measure opt-in native deletion and GUI snapshot/redo/undo/replay using separate
     * loaded models.
     * Skips unless SWMMVIS_ATTR_PERF_DELETE_N is positive. Native floor repeatedly deletes
     * index-zero nodes; GUI path deletes up to that many junction names with stack-owned
     * BulkEditCommand children and a null canvas. Counts/object families can differ, so
     * subtracting timings is only a rough overhead indicator. Records requested/successful
     * counts without asserting final topology, numerical equivalence or timing thresholds.
     * Only in-memory layers are mutated; external fixture is not saved.
     *
     */
    void bulkDeletePhases()
    {
        const int nDel = qEnvironmentVariableIntValue("SWMMVIS_ATTR_PERF_DELETE_N");
        if (nDel <= 0)
            QSKIP("SWMMVIS_ATTR_PERF_DELETE_N not set — bulk-delete timing skipped.");

        const QString inp = perfInp();
        const QString modelName = QFileInfo(inp).fileName();
        QList<Row> rows;
        QElapsedTimer t;

        // ---- engine floor, on a throwaway layer --------------------------
        {
            auto floorLayer = std::make_unique<SWMMModelLayer>(inp, nullptr);
            QList<QString> w, e;
            QVERIFY2(floorLayer->loadModel(w, e), "floor layer failed to load");
            SWMM_Engine eng = floorLayer->engine();
            QVERIFY(eng);
            const int avail = qMin(nDel, swmm_node_count(eng));
            // Delete index 0 repeatedly: always valid, and it forces the
            // maximum renumbering work, which is the cost being measured.
            t.restart();
            int done = 0;
            for (int i = 0; i < avail; ++i)
                if (swmm_node_delete(eng, 0, nullptr) == SWMM_OK)
                    ++done;
            record(rows, Row{modelName, QStringLiteral("-"), QStringLiteral("engine_delete_floor"),
                             t.elapsed(), done});
        }

        // ---- the real GUI path -------------------------------------------
        auto layer = std::make_unique<SWMMModelLayer>(inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors), "layer failed to load");

        MapUndoStack stack;
        QStringList names;
        names.reserve(nDel);
        const int nJunc = layer->categoryCount(SWMMModelLayer::CatJunctions);
        for (int r = 0; r < qMin(nDel, nJunc); ++r) {
            const QString n = layer->objectNameAt(SWMMModelLayer::CatJunctions, r);
            if (!n.isEmpty())
                names << n;
        }
        QVERIFY2(!names.isEmpty(), "no junctions to delete");
        const int nNames = static_cast<int>(names.size());

        // MapCommand only stores the canvas pointer; redo()/undo() never
        // dereference it, so a null canvas is a valid rig here.
        //
        // BulkEditCommand is what the three production call sites now build.
        // Swap it for a plain QUndoCommand to reproduce the pre-fix numbers.
        t.restart();
        auto *macro =
            new BulkEditCommand(layer.get(), QStringLiteral("Delete %1 objects").arg(nNames));
        for (const QString &n : std::as_const(names))
            new DeleteObjectCommand(layer.get(), n, DeleteObjectCommand::DeleteNode, nullptr,
                                    macro);
        record(rows, Row{modelName, QStringLiteral("Junctions"),
                         QStringLiteral("bulk_delete_snapshot"), t.elapsed(), nNames});

        t.restart();
        stack.push(macro);
        record(rows, Row{modelName, QStringLiteral("Junctions"), QStringLiteral("bulk_delete_redo"),
                         t.elapsed(), nNames});

        t.restart();
        stack.undo();
        record(rows, Row{modelName, QStringLiteral("Junctions"), QStringLiteral("bulk_delete_undo"),
                         t.elapsed(), nNames});

        t.restart();
        stack.redo();
        record(rows, Row{modelName, QStringLiteral("Junctions"),
                         QStringLiteral("bulk_delete_redo2"), t.elapsed(), nNames});

        report(rows);
    }

private:
    /**
     *
     * @brief Choose the representative numeric query alias for a timed category.
     * @param cat Model category enumeration.
     * @return Invert elev, Length or Area for junction/conduit/subcatchment respectively,
     * empty otherwise. Metadata string only; no schema resolution or numeric read.
     *
     */
    static QString numericFieldFor(SWMMModelLayer::Category cat)
    {
        switch (cat) {
        case SWMMModelLayer::CatJunctions:
            return QStringLiteral("Invert elev");
        case SWMMModelLayer::CatConduits:
            return QStringLiteral("Length");
        case SWMMModelLayer::CatSubcatchments:
            return QStringLiteral("Area");
        default:
            return {};
        }
    }

    /**
     *
     * @brief Map timed spatial categories to the selection bus identity family.
     * @param cat Model category enumeration.
     * @return Node/Link/Subcatchment for supported categories, Unknown otherwise; no model
     * lookup, name validation or retained reference.
     *
     */
    static SWMMObjectRef::ObjectType refTypeFor(SWMMModelLayer::Category cat)
    {
        switch (cat) {
        case SWMMModelLayer::CatJunctions:
            return SWMMObjectRef::Node;
        case SWMMModelLayer::CatConduits:
            return SWMMObjectRef::Link;
        case SWMMModelLayer::CatSubcatchments:
            return SWMMObjectRef::Subcatchment;
        default:
            return SWMMObjectRef::Unknown;
        }
    }

    /**
     *
     * @brief Log how many phase rows were collected and the intended persistent CSV path.
     * @param rows Borrowed collected records; only size is read.
     * Does not write rows again or prove every prior append succeeded.
     *
     */
    static void report(const QList<Row> &rows)
    {
        qInfo().noquote() << rows.size() << "phases appended to"
                          << QDir(outDir()).filePath(QStringLiteral("attrtable_perf.csv"));
    }
};

QTEST_MAIN(TestAttrTablePerfBaseline)
#include "test_attrtableperf_baseline.moc"
