/*!
 * \file   test_attrtableperf_baseline.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Attribute Table performance harness for large (all-pipes) models.
 *
 *         Measures, per category, the paths that were unusable at 100k+
 *         objects before the query/cell-read work:
 *
 *           - loadModel ms                         (baseline, not our target)
 *           - category bind ms                     (schema + model reset)
 *           - viewport read ms                     (what painting costs)
 *           - query Apply ms, one-column predicate (filter + selection)
 *           - query Apply ms, LIKE predicate
 *           - query Apply ms, two-column predicate
 *           - sort-by-tagged-column ms
 *           - show-selected-only toggle ms         (large selection)
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
 */
#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/swmmattributetablemodel.h"

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

namespace {

QString perfInp()
{
    return qEnvironmentVariable("SWMMVIS_ATTR_PERF_INP");
}

QString outDir()
{
    return qEnvironmentVariable("SWMMVIS_ATTR_PERF_OUT",
                                QStringLiteral("tests/output/attrtable_perf"));
}

struct Row {
    QString model, category, phase;
    qint64  ms = 0;
    int     detail = 0;      //!< rows matched / rows visited — phase-specific
};

//! Append one row to the CSV as soon as it is measured.
//!
//! Deliberately streamed rather than written once at the end: on slow
//! (pre-fix) code a single phase can run for tens of minutes, and a run
//! killed in that phase must still leave the phases that DID complete on
//! disk. Batching the write meant killing a stuck run threw away
//! everything it had already measured.
void appendRow(const Row &r)
{
    QDir().mkpath(outDir());
    QFile f(QDir(outDir()).filePath(QStringLiteral("attrtable_perf.csv")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream s(&f);
    if (f.size() == 0) s << "model,category,phase,ms,detail\n";
    s << r.model << ',' << r.category << ',' << r.phase << ','
      << r.ms << ',' << r.detail << '\n';
}

//! Record + emit in one step so no call site can log a phase it forgot to
//! persist (or persist one it forgot to log).
void record(QList<Row> &rows, const Row &r)
{
    rows << r;
    appendRow(r);
    qInfo().noquote() << QStringLiteral("%1,%2,%3,%4,%5")
                             .arg(r.model, r.category, r.phase)
                             .arg(r.ms).arg(r.detail);
}

//! Categories worth timing: the two that dominate an all-pipes model, plus
//! subcatchments (whose compound cells used to run engine-wide scans).
struct Cat { const char *comboPrefix; SWMMModelLayer::Category cat; };
constexpr Cat kCats[] = {
    {"Junctions",      SWMMModelLayer::CatJunctions},
    {"Conduits",       SWMMModelLayer::CatConduits},
    {"Subcatchments",  SWMMModelLayer::CatSubcatchments},
};

} // namespace

class TestAttrTablePerfBaseline : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        const QString inp = perfInp();
        if (inp.isEmpty() || !QFileInfo::exists(inp))
            QSKIP("SWMMVIS_ATTR_PERF_INP not set to a readable .inp — "
                  "see the file header for how to run this.");
    }

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
        QVERIFY2(ok, qPrintable(QStringLiteral("loadModel failed: %1")
                                    .arg(errors.join(QStringLiteral("; ")))));
        record(rows, Row{modelName, QStringLiteral("-"),
                    QStringLiteral("load_model"), loadMs, 0});

        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        auto *view  = panel.findChild<QTableView *>();
        auto *edit  = panel.findChild<QLineEdit *>();
        QVERIFY(combo && view && edit);

        for (const auto &c : kCats) {
            int catIdx = -1;
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemText(i).startsWith(QLatin1String(c.comboPrefix)))
                    catIdx = i;
            if (catIdx < 0) continue;          // model has none of this kind

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
            if (nRow == 0) continue;
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
            record(rows, Row{modelName, cat, QStringLiteral("viewport_read"),
                        t.elapsed(), window * nCol});

            // ---- queries ----------------------------------------------
            // Plain decimals only: the tokenizer has no scientific
            // notation, and a parse error makes Apply return early without
            // touching the filter — which would silently time nothing.
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
                           QStringLiteral("[%1] > -99999 AND [%1] < 99999")
                               .arg(numField));
            }
            applyQuery(QStringLiteral("query_like"),
                       QStringLiteral("Name LIKE '%'"));

            // Clear so the sort and selection phases see the full table.
            edit->setText(QString());
            QTest::keyClick(edit, Qt::Key_Return);

            // ---- sort --------------------------------------------------
            // Column 1 is the first attribute past Name, i.e. a tagged
            // column that reads through the engine.
            if (nCol > 1) {
                t.restart();
                view->sortByColumn(1, Qt::AscendingOrder);
                record(rows, Row{modelName, cat, QStringLiteral("sort_tagged_column"),
                            t.elapsed(), nRow});
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
                    if (!n.isEmpty()) all.insert(SWMMObjectRef(type, n));
                }
                t.restart();
                selMgr.select(all, SelectionManager::Replace);
                record(rows, Row{modelName, cat, QStringLiteral("select_all_bus"),
                            t.elapsed(), int(all.size())});
                selMgr.select(QSet<SWMMObjectRef>{}, SelectionManager::Replace);
            }
        }

        report(rows);
    }

private:
    static QString numericFieldFor(SWMMModelLayer::Category cat)
    {
        switch (cat) {
        case SWMMModelLayer::CatJunctions:     return QStringLiteral("Invert elev");
        case SWMMModelLayer::CatConduits:      return QStringLiteral("Length");
        case SWMMModelLayer::CatSubcatchments: return QStringLiteral("Area");
        default:                               return {};
        }
    }

    static SWMMObjectRef::ObjectType refTypeFor(SWMMModelLayer::Category cat)
    {
        switch (cat) {
        case SWMMModelLayer::CatJunctions:     return SWMMObjectRef::Node;
        case SWMMModelLayer::CatConduits:      return SWMMObjectRef::Link;
        case SWMMModelLayer::CatSubcatchments: return SWMMObjectRef::Subcatchment;
        default:                               return SWMMObjectRef::Unknown;
        }
    }

    //! Rows are already streamed to the CSV by record(); this just says
    //! where they went and how many completed.
    static void report(const QList<Row> &rows)
    {
        qInfo().noquote() << rows.size() << "phases appended to"
                          << QDir(outDir()).filePath(
                                 QStringLiteral("attrtable_perf.csv"));
    }
};

QTEST_MAIN(TestAttrTablePerfBaseline)
#include "test_attrtableperf_baseline.moc"
