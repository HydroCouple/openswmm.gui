/**
 * @file      test_remap1d2d_dialog.cpp
 * @brief     Remap 1D↔2D dialog: per-phase launch and refresh timings, and a frozen dry-run
 *            baseline that performance changes must reproduce exactly.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Opens a model with a [2D_MESH_FILE] (default: examples/bellinge_2d; any other model through
 * SWMMVIS_REMAP_TIMING_INP), times each phase the toolbar's Remap 1D↔2D button runs, and appends
 * one row to timings.csv in the output folder. The model is opened read-only and nothing is
 * written next to it.
 *
 * The dry run (node table states, Current / Proposed / |rim − bed| columns, vertex changes and the
 * new cell rows, for Clear & rebuild and Preserve existing) is written to plan_<model>.txt. The
 * first run keeps it as plan_baseline_<model>.txt; later runs must match it byte for byte.
 * Delete the baseline file to re-baseline. SWMMVIS_REMAP_TIMING_LABEL tags the CSV row.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "mesh/inpmeshreader.h"
#include "mesh/meshenginesync.h"
#include "mesh/meshnodemapper.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/remap1d2ddialog.h"
#include "ui/theme/thememanager.h"
#include "ui/widgets/nodecouplingselector.h"

#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QTableView>
#include <QTest>
#include <QTextStream>
#include <QStyleFactory>

#include <algorithm>
#include <memory>

namespace
{

/** @brief Milliseconds since \a t started, to 0.01 ms. */
double ms(const QElapsedTimer &t)
{
    return t.nsecsElapsed() / 1.0e6;
}

/** @brief Deliver posted events and zero-timers (delayed layouts, header resizes, paints). */
void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

/** @brief Node ids + coordinates, built as the app's node locator builds them (swmmvis.cpp). */
QVector<QPair<QString, QPointF>> nodeList(SWMM_Engine e)
{
    QVector<QPair<QString, QPointF>> out;
    const int n = swmm_node_count(e);
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_node_id(e, i);
        if (!id || id[0] == '\0')
            continue;
        double x = 0.0, y = 0.0;
        if (swmm_spatial_get_node_coord(e, i, &x, &y) != 0)
            continue;
        out.append({QString::fromUtf8(id), QPointF(x, y)});
    }
    return out;
}

/** @brief The toolbar's rule for running the 2D aquifer preview (swmmvis.cpp). */
bool gwPreviewRuns(SWMM_Engine e)
{
    char gw[32] = {};
    const QString mode = swmm_options_get_ext(e, "GROUNDWATER", gw, sizeof(gw)) == 0
                             ? QString::fromUtf8(gw).trimmed().toUpper()
                             : QStringLiteral("AUTO");
    char configured[16] = {};
    swmm_gw2d_option_get(e, "CONFIGURED", configured, sizeof configured);
    return mode != QLatin1String("NO") &&
           (mode == QLatin1String("YES") || QString::fromUtf8(configured) == QLatin1String("YES"));
}

/** @brief Text dump of the dialog's node table and plan, in a deterministic order. */
QString dump(Remap1D2DDialog &dlg, const QString &title)
{
    QString s;
    QTextStream out(&s);
    out << "## " << title << '\n';
    const NodeCouplingTableModel *m = dlg.nodeModel();
    out << "# rows " << m->rowCount() << '\n';
    for (int r = 0; r < m->rowCount(); ++r) {
        const NodeCouplingTableModel::Row &row = m->rowAt(r);
        auto state = [](const nodecoupling2d::State &st) {
            QString t = QStringLiteral("type=%1 authored=%2").arg(st.type).arg(st.authored);
            for (auto it = st.params.cbegin(); it != st.params.cend(); ++it)
                t += QStringLiteral(" %1=%2").arg(it.key(), it.value());
            return t;
        };
        QStringList allowed;
        for (int a : row.allowed)
            allowed << QString::number(a);
        out << row.original.node << " | cat=" << row.category << ' ' << row.categoryLabel
            << " | default=" << row.defaultType << " allowed=" << allowed.join(',')
            << " | original " << state(row.original) << " | edited " << state(row.edited)
            << " | extra " << row.extra.join(QStringLiteral(" ; ")) << '\n';
    }

    const mesh::RemapPlan &p = dlg.plan();
    out << "# vertex changes " << p.vertices.size() << '\n';
    for (int i = 0; i < p.vertices.size(); ++i) {
        const mesh::MeshVertex &a = p.oldAttrs[i], &b = p.newAttrs[i];
        out << "v " << p.vertices[i] << ": '" << a.coupledNode << "' -> '" << b.coupledNode
            << "' cd=" << b.couplingCd << " area=" << b.couplingArea << " set=" << b.couplingCdSet
            << b.couplingAreaSet << '\n';
    }
    out << "# new cells " << p.newCells.size() << '\n';
    for (const mesh::CellCoupling &c : p.newCells)
        out << "c " << c.tri << ' ' << c.nodeId << " cd=" << c.cd << " area=" << c.area
            << " set=" << c.cdSet << c.areaSet << '\n';
    QList<int> vkeys = p.mapped.vertexMatches.keys();
    std::sort(vkeys.begin(), vkeys.end());
    out << "# vertex matches " << vkeys.size() << '\n';
    for (int v : vkeys)
        out << "vm " << v << ' ' << p.mapped.vertexMatches.value(v) << '\n';
    out << "# unmatched " << p.mapped.unmatched.join(',') << '\n';
    out << "# skipped " << p.mapped.skippedExisting.join(',') << '\n';
    out << "# shared cells " << p.mapped.sharedCells << '\n';
    for (const auto *h : {&p.current, &p.proposed}) {
        QStringList keys = h->keys();
        keys.sort();
        out << (h == &p.current ? "# current " : "# proposed ") << keys.size() << '\n';
        for (const QString &k : keys)
            out << k << " = " << h->value(k) << '\n';
    }
    return s;
}

/** @brief First row whose staged type is coupleable, or -1. */
int firstCoupleableRow(const NodeCouplingTableModel *m)
{
    for (int r = 0; r < m->rowCount(); ++r)
        if (m->rowAt(r).edited.type != nodecoupling2d::None)
            return r;
    return -1;
}

} // namespace

class TestRemap1D2DDialog : public QObject
{
    Q_OBJECT
private slots:
    void timingsAndBaseline();
};

void TestRemap1D2DDialog::timingsAndBaseline()
{
    const QString inp = qEnvironmentVariable(
        "SWMMVIS_REMAP_TIMING_INP",
        QStringLiteral(SWMMVIS_REMAP_REPO_DIR
                       "/examples/bellinge_2d/BellingeSWMM_v021_nopervious.inp"));
    QVERIFY2(QFileInfo::exists(inp), qPrintable(inp));
    const QString model = QFileInfo(inp).completeBaseName();
    const QString outDir = QStringLiteral(SWMMVIS_REMAP_OUTPUT_DIR);
    QVERIFY(QDir().mkpath(outDir));

    // The app's style: Fusion under the theme's application style sheet
    // (SWMMVisApplication). Item size hints then go through QStyleSheetStyle,
    // which is what the table's column sizing pays for in the app.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    openswmmvis::ui::ThemeManager::instance()->setMode(openswmmvis::ui::ThemeManager::Mode::Light);
    openswmmvis::ui::ThemeManager::instance()->apply();
    QVERIFY(!qApp->styleSheet().isEmpty());

    // ── Open the model and its mesh the way the app does ───────────────────
    std::unique_ptr<OpenSWMMVisWorkspace> ws(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
    std::unique_ptr<SWMMVisProjectWindow> window(new SWMMVisProjectWindow(ws.get(), inp, nullptr));
    QList<QString> warnings, errors;
    QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
    SWMMModelLayer *layer = window->modelLayer();
    QVERIFY(layer && layer->engine());
    const SWMM_Engine e = layer->engine();

    mesh::InpMeshReadResult read = mesh::InpMeshReader::read(inp);
    QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
    auto meshLayer = std::make_unique<SWMM2DMeshLayer>(std::move(read.mesh), read.sourcePath,
                                                       nullptr, /*deferHeavyGeometry=*/true);
    const mesh::MeshResult &mr = meshLayer->mesh();
    int vertexCouplings = 0;
    for (const mesh::MeshVertex &v : mr.vertices)
        vertexCouplings += !v.coupledNode.isEmpty();

    // ── Phases the toolbar runs before the dialog appears ──────────────────
    QElapsedTimer t;
    t.start();
    const QVector<QPair<QString, QPointF>> nodes = nodeList(e);
    const double nodesMs = ms(t);
    QVERIFY(!nodes.isEmpty());
    const bool gwPreview = gwPreviewRuns(e);

    // Constructor parts, measured on their own.
    NodeCouplingTableModel probe;
    t.restart();
    probe.load(layer);
    const double loadMs = ms(t);
    const bool authorCdArea = mesh::couplingAuthorsCdArea(e);
    auto coupleable = [&probe](const QString &n) {
        return probe.rowOf(n) < 0 || probe.isCoupleable(n);
    };
    t.restart();
    const mesh::RemapPlan plan = mesh::planRemap(mr, nodes, coupleable, true, authorCdArea);
    const double planMs = ms(t);
    QVERIFY(plan.mapped.vertexMatches.size() + plan.mapped.cellMatches.size() > 0);

    // ── The dialog: construct, show, toggle ────────────────────────────────
    t.restart();
    Remap1D2DDialog dlg(layer, meshLayer.get(), window->canvas(), nodes, {}, QString(), {});
    const double ctorMs = ms(t);
    const QString dumpRebuild = dump(dlg, QStringLiteral("clear & rebuild"));

    t.restart();
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    settle();
    const double showMs = ms(t);

    NodeCouplingTableModel *m = dlg.nodeModel();
    const int row = firstCoupleableRow(m);
    QVERIFY(row >= 0);
    auto toggle = [&](bool on) {
        QElapsedTimer tt;
        tt.start();
        m->setCoupleable({row}, on);
        settle();
        return ms(tt);
    };
    const double offMs = toggle(false);
    const double onMs = toggle(true);

    auto *view = dlg.findChild<QTableView *>();
    QVERIFY(view);
    view->sortByColumn(NodeCouplingTableModel::ColFirstExtra + 1, Qt::AscendingOrder); // Proposed
    settle();
    const double sortedOffMs = toggle(false);
    const double sortedOnMs = toggle(true);
    view->sortByColumn(NodeCouplingTableModel::ColName, Qt::AscendingOrder);
    settle();

    dlg.setClearAndRebuild(false);
    const QString dumpPreserve = dump(dlg, QStringLiteral("preserve existing"));
    dlg.setClearAndRebuild(true);
    QCOMPARE(dump(dlg, QStringLiteral("clear & rebuild")), dumpRebuild); // toggles round-tripped
    dlg.hide();

    // ── timings.csv ────────────────────────────────────────────────────────
    QFile csv(QDir(outDir).filePath(QStringLiteral("timings.csv")));
    const bool fresh = !csv.exists();
    QVERIFY(csv.open(QIODevice::Append | QIODevice::Text));
    QTextStream c(&csv);
    if (fresh)
        c << "when,label,build,model,nodes,vertices,cells,vertex_couplings,cell_couplings,"
             "gw_preview,nodes_ms,load_ms,plan_ms,ctor_ms,show_ms,toggle_off_ms,toggle_on_ms,"
             "sorted_toggle_off_ms,sorted_toggle_on_ms\n";
    auto f = [](double v) { return QString::number(v, 'f', 2); };
    c << QDateTime::currentDateTime().toString(Qt::ISODate) << ','
      << qEnvironmentVariable("SWMMVIS_REMAP_TIMING_LABEL", QStringLiteral("run")) << ','
      << SWMMVIS_REMAP_BUILD_TYPE << ',' << model << ',' << nodes.size() << ','
      << mr.vertices.size() << ',' << mr.triangles.size() << ',' << vertexCouplings << ','
      << mr.cellCouplings.size() << ',' << (gwPreview ? "yes" : "no") << ',' << f(nodesMs) << ','
      << f(loadMs) << ',' << f(planMs) << ',' << f(ctorMs) << ',' << f(showMs) << ',' << f(offMs)
      << ',' << f(onMs) << ',' << f(sortedOffMs) << ',' << f(sortedOnMs) << '\n';
    qInfo().noquote() << QStringLiteral(
                             "%1: nodes %2 ms, load %3 ms, plan %4 ms, ctor %5 ms, show %6 ms, "
                             "toggle %7/%8 ms, sorted toggle %9/%10 ms (gw preview: %11)")
                             .arg(model, f(nodesMs), f(loadMs), f(planMs), f(ctorMs), f(showMs),
                                  f(offMs), f(onMs), f(sortedOffMs))
                             .arg(f(sortedOnMs),
                                  gwPreview ? QStringLiteral("yes") : QStringLiteral("no"));

    // ── Dry-run baseline ───────────────────────────────────────────────────
    const QString current = dumpRebuild + dumpPreserve;
    QFile cur(QDir(outDir).filePath(QStringLiteral("plan_%1.txt").arg(model)));
    QVERIFY(cur.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    cur.write(current.toUtf8());
    cur.close();
    QFile base(QDir(outDir).filePath(QStringLiteral("plan_baseline_%1.txt").arg(model)));
    if (!base.exists()) {
        QVERIFY(QFile::copy(cur.fileName(), base.fileName()));
        qInfo().noquote() << "Baseline written:" << base.fileName();
        return;
    }
    QVERIFY(base.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList want = QString::fromUtf8(base.readAll()).split('\n');
    const QStringList got = current.split('\n');
    for (int i = 0; i < std::min(want.size(), got.size()); ++i)
        QVERIFY2(
            want[i] == got[i],
            qPrintable(QStringLiteral("line %1 differs from %2\n  baseline: %3\n  now:      %4")
                           .arg(i + 1)
                           .arg(base.fileName(), want[i], got[i])));
    QCOMPARE(got.size(), want.size());
}

QTEST_MAIN(TestRemap1D2DDialog)
#include "test_remap1d2d_dialog.moc"
