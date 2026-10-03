/*!
 * \file   test_gageassignperf.cpp
 * \brief  Performance harness for Model → Tools → Assign Rain Gages
 *         (workplans/RAINGAGE_ASSIGNMENT_PERF_AND_RDII_PLAN_2026-10-02.md).
 *
 * Builds a synthetic model — S octagonal subcatchments on a jittered grid,
 * G gages scattered over the same extent, each with a P-entry 15-minute
 * intensity series — writes it to the output folder, loads it, and drives the
 * real AssignRainGagesDialog through its Preview / Apply buttons with an
 * Attribute Table bound to the same project (its refresh is the dominant view
 * listener in the app). Times Preview, Apply (including the queued refreshes
 * it leaves behind), Undo and Redo, and appends one CSV row per case to
 * `$SWMMVIS_GAGE_PERF_OUT/gageassign_perf.csv`.
 *
 * The default cases are small enough for CI. Set SWMMVIS_GAGE_PERF_SCALE=full
 * for the 10k-subcatchment cases the plan's targets refer to.
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/assignraingagesdialog.h"
#include "ui/panels/attributetablepanel.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTest>
#include <QTextStream>

#include <cmath>

namespace {

QString outDir()
{
    const QString d = qEnvironmentVariable("SWMMVIS_GAGE_PERF_OUT",
                                           QStringLiteral("gageassign_perf"));
    QDir().mkpath(d);
    return d;
}

/*! Deterministic LCG in [0,1) — the deck must be identical run to run. */
struct Lcg {
    quint64 s;
    double next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL;
                    return double(s >> 11) / double(1ULL << 53); }
};

/*! Write the synthetic deck; returns its path. */
QString writeDeck(int nSub, int nGage, int nPts)
{
    const QString path = QDir(outDir()).filePath(
        QStringLiteral("synthetic_S%1_G%2_P%3.inp").arg(nSub).arg(nGage).arg(nPts));
    if (QFile::exists(path)) return path;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream o(&f);
    const int cols = int(std::ceil(std::sqrt(double(nSub))));
    const double cell = 100.0, extent = cols * cell;
    Lcg rng{42};

    o << "[TITLE]\nSynthetic gage-assignment perf deck\n\n"
         "[OPTIONS]\nFLOW_UNITS CFS\nINFILTRATION HORTON\nFLOW_ROUTING KINWAVE\n"
         "START_DATE 01/01/2020\nSTART_TIME 00:00:00\nEND_DATE 01/02/2020\n"
         "END_TIME 00:00:00\n\n[RAINGAGES]\n";
    for (int g = 0; g < nGage; ++g)
        o << "G" << g << " INTENSITY 0:15 1.0 TIMESERIES TS_G" << g << "\n";

    o << "\n[SUBCATCHMENTS]\n";
    for (int s = 0; s < nSub; ++s)
        o << "S" << s << " G0 OUT1 5 25 500 0.5 0\n";
    o << "\n[SUBAREAS]\n";
    for (int s = 0; s < nSub; ++s)
        o << "S" << s << " 0.01 0.1 0.05 0.05 25 OUTLET\n";
    o << "\n[INFILTRATION]\n";
    for (int s = 0; s < nSub; ++s)
        o << "S" << s << " 3.0 0.5 4 7 0\n";

    o << "\n[OUTFALLS]\nOUT1 0 FREE NO\n\n[TIMESERIES]\n";
    for (int g = 0; g < nGage; ++g)
        for (int k = 0; k < nPts; ++k) {
            const int mins = k * 15;
            const QDateTime t = QDateTime(QDate(2020, 1, 1), QTime(0, 0))
                                    .addSecs(qint64(mins) * 60);
            o << "TS_G" << g << " " << t.toString(QStringLiteral("MM/dd/yyyy HH:mm"))
              << " " << QString::number(rng.next() * 2.0, 'f', 3) << "\n";
        }

    o << "\n[COORDINATES]\nOUT1 " << -cell << " " << -cell << "\n\n[POLYGONS]\n";
    for (int s = 0; s < nSub; ++s) {
        const double cx = (s % cols + 0.5) * cell + (rng.next() - 0.5) * 10.0;
        const double cy = (s / cols + 0.5) * cell + (rng.next() - 0.5) * 10.0;
        for (int v = 0; v < 8; ++v) {
            const double a = v * M_PI / 4.0;
            const double r = cell * (0.40 + 0.08 * rng.next());
            o << "S" << s << " " << QString::number(cx + r * std::cos(a), 'f', 3)
              << " " << QString::number(cy + r * std::sin(a), 'f', 3) << "\n";
        }
    }
    o << "\n[SYMBOLS]\n";
    for (int g = 0; g < nGage; ++g)
        o << "G" << g << " " << QString::number(rng.next() * extent, 'f', 3) << " "
          << QString::number(rng.next() * extent, 'f', 3) << "\n";
    return path;
}

template <class T>
T *findByText(QWidget *root, const QString &text)
{
    for (T *w : root->findChildren<T *>())
        if (w->text().remove(QLatin1Char('&')) == text) return w;
    return nullptr;
}

double timeMs(const std::function<void()> &fn)
{
    QElapsedTimer t;
    t.start();
    fn();
    return double(t.nsecsElapsed()) / 1e6;
}

/*! Let queued refreshes (dataObjectsChanged → AttributeTablePanel) land. */
void drain()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

} // namespace

class TestGageAssignPerf : public QObject
{
    Q_OBJECT
private slots:
    void run_data();
    void run();
};

void TestGageAssignPerf::run_data()
{
    QTest::addColumn<int>("nSub");
    QTest::addColumn<int>("nGage");
    QTest::addColumn<int>("nPts");
    QTest::addColumn<bool>("interp");
    QTest::newRow("thiessen-S500-G10")  << 500 << 10 << 96  << false;
    QTest::newRow("nn-S500-G10")        << 500 << 10 << 96  << true;
    if (qEnvironmentVariable("SWMMVIS_GAGE_PERF_SCALE") == QStringLiteral("full")) {
        QTest::newRow("thiessen-S10000-G25")  << 10000 << 25  << 96  << false;
        QTest::newRow("thiessen-S10000-G100") << 10000 << 100 << 96  << false;
        QTest::newRow("nn-S2000-G25")         << 2000  << 25  << 2000 << true;
        QTest::newRow("nn-S10000-G25")        << 10000 << 25  << 2000 << true;
    }
}

void TestGageAssignPerf::run()
{
    QFETCH(int, nSub);
    QFETCH(int, nGage);
    QFETCH(int, nPts);
    QFETCH(bool, interp);

    const QString deck = writeDeck(nSub, nGage, nPts);
    QVERIFY(!deck.isEmpty());

    MapCanvas canvas;
    auto *layer = new SWMMModelLayer(deck, nullptr);
    QList<QString> warnings, errors;
    QVERIFY2(layer->loadModel(warnings, errors), qPrintable(errors.join('\n')));
    canvas.addLayer(layer, /*pushUndo=*/false);
    SelectionManager sel;
    AttributeTablePanel table;
    table.setProject(layer, &sel, &canvas);
    drain();

    openswmmvis::ui::AssignRainGagesDialog dlg(layer, &canvas, &sel);
    if (interp) {
        auto *nn = findByText<QRadioButton>(&dlg,
            QStringLiteral("Natural-neighbour interpolation (creates gages)"));
        QVERIFY(nn);
        nn->setChecked(true);
    }
    QPushButton *preview = findByText<QPushButton>(&dlg, QStringLiteral("Preview"));
    QPushButton *apply   = findByText<QPushButton>(&dlg, QStringLiteral("Apply"));
    QVERIFY(preview && apply);

    // The plan is computed on a worker; wait for the dialog to settle.
    const auto settle = [&] { while (dlg.busy()) QCoreApplication::processEvents(
                                  QEventLoop::AllEvents, 5); };
    const double previewMs = timeMs([&] { preview->click(); settle(); });
    const double applyMs   = timeMs([&] { apply->click(); settle(); drain(); });
    MapUndoStack *stack = canvas.undoStack();
    QVERIFY(stack && stack->canUndo());
    const double undoMs = timeMs([&] { stack->undo(); drain(); });
    const double redoMs = timeMs([&] { stack->redo(); drain(); });

    QString status;
    for (QLabel *l : dlg.findChildren<QLabel *>())
        if (l->text().contains(QStringLiteral("subcatchment"))) status = l->text();

    const QString csv = QDir(outDir()).filePath(QStringLiteral("gageassign_perf.csv"));
    QFile f(csv);
    const bool fresh = !f.exists();
    QVERIFY(f.open(QIODevice::Append | QIODevice::Text));
    QTextStream o(&f);
    if (fresh)
        o << "timestamp,label,case,S,G,P,preview_ms,apply_ms,undo_ms,redo_ms\n";
    o << QDateTime::currentDateTime().toString(Qt::ISODate) << ","
      << qEnvironmentVariable("SWMMVIS_GAGE_PERF_LABEL", QStringLiteral("run")) << ","
      << QTest::currentDataTag() << "," << nSub << "," << nGage << "," << nPts << ","
      << previewMs << "," << applyMs << "," << undoMs << "," << redoMs << "\n";
    qInfo().noquote() << QTest::currentDataTag() << "preview" << previewMs << "ms, apply"
                      << applyMs << "ms, undo" << undoMs << "ms, redo" << redoMs
                      << "ms |" << status.left(160);
}

QTEST_MAIN(TestGageAssignPerf)
#include "test_gageassignperf.moc"
