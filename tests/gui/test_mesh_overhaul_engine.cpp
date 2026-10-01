// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh overhaul Phase 7 step 4 (MESH_OVERHAUL_PHASE7_HANDOFF_2026-09-30.md):
// engine round trip on meshes from the NEW generator.
//
//  1. Lake at rest — the SWASHES immersed-bump deck re-meshed (quads and
//     triangles-only): the engine must hold max |h − h0| = 0 exactly and
//     conserve volume to 1e-9 over the run. Also proves Q1 by construction:
//     the engine accepts the mesh with no T-junction/degenerate-cell
//     complaints.
//  2. Sloping storm — bellinge_T8 with the acceptance harness's run-B mesh
//     substituted: the .rpt 2D continuity must read 0.000 %, matching the
//     shipped mesh's report.
//
// Gated on SWMMVIS_MESH_OVERHAUL_ENGINE=1 (QSKIP otherwise); the storm case
// additionally QSKIPs until the Bellinge harness has produced mesh_B.2dm.
#include "mesh/meshgenerator.h"
#include "mesh/meshcellgeom.h"
#include "mesh/inpmeshwriter.h"
#include "simulation/simulationrunner.h"
#include "io/mesh2dh5reader.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

namespace {

constexpr double kSqrt3_4 = 0.4330127018922193;
constexpr double kLakeWse = 0.5;                  // SWASHES lake at rest

struct ShippedMesh
{
    QVector<QPointF> xy;
    QVector<double>  z;
    QVector<std::array<int, 3>> tris;
};

ShippedMesh parseInlineMesh(const QString &inpPath)
{
    ShippedMesh m;
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return m;
    QString section;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(";;")) continue;
        if (line.startsWith('[')) { section = line.toUpper(); continue; }
        const QStringList p = line.split(QRegularExpression("\\s+"),
                                         Qt::SkipEmptyParts);
        if (section == "[2D_VERTICES]" && p.size() >= 3) {
            m.xy.append(QPointF(p[0].toDouble(), p[1].toDouble()));
            m.z.append(p[2].toDouble());
        } else if (section == "[2D_TRIANGLES]" && p.size() >= 3) {
            m.tris.append({p[0].toInt(), p[1].toInt(), p[2].toInt()});
        }
    }
    return m;
}

//! Bed elevation at (x, y): barycentric over the shipped triangles, nearest
//! shipped vertex as the fallback for boundary rounding.
double bedAt(const ShippedMesh &m, const QPointF &q)
{
    for (const auto &t : m.tris) {
        const QPointF a = m.xy[t[0]], b = m.xy[t[1]], c = m.xy[t[2]];
        const double det = (b.x() - a.x()) * (c.y() - a.y())
                         - (c.x() - a.x()) * (b.y() - a.y());
        if (std::abs(det) < 1e-12) continue;
        const double l1 = ((q.x() - a.x()) * (c.y() - a.y())
                         - (c.x() - a.x()) * (q.y() - a.y())) / det;
        const double l2 = ((b.x() - a.x()) * (q.y() - a.y())
                         - (q.x() - a.x()) * (b.y() - a.y())) / det;
        const double l0 = 1.0 - l1 - l2;
        const double eps = -1e-9;
        if (l0 >= eps && l1 >= eps && l2 >= eps)
            return l0 * m.z[t[0]] + l1 * m.z[t[1]] + l2 * m.z[t[2]];
    }
    int best = 0; double bestD = 1e300;
    for (int i = 0; i < m.xy.size(); ++i) {
        const double d = QLineF(m.xy[i], q).length();
        if (d < bestD) { bestD = d; best = i; }
    }
    return m.z[best];
}

//! Run one deck through the in-process engine; returns success and leaves
//! the rpt/out/h5 files beside the deck.
bool runDeck(const QString &inp, QString *errOut)
{
    static int jobId = 900;
    auto *runner = new SimulationRunner(++jobId, QStringLiteral("phase7"),
                                        inp, inp + ".rpt", inp + ".out");
    QSignalSpy spy(runner, &SimulationRunner::finished);
    runner->start();
    // bellinge_T8 on the run-B mesh took 4 h 19 min on the owner's Mac.
    if (!spy.wait(6 * 60 * 60 * 1000)) {
        if (errOut) *errOut = QStringLiteral("engine run timed out");
        return false;
    }
    const auto args = spy.takeFirst();
    if (!args.at(1).toBool() && errOut) *errOut = args.at(3).toString();
    return args.at(1).toBool();
}

} // namespace

class TestMeshOverhaulEngine : public QObject
{
    Q_OBJECT
    QString m_repo, m_outRoot;

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("SWMMVIS_MESH_OVERHAUL_ENGINE") != "1")
            QSKIP("Set SWMMVIS_MESH_OVERHAUL_ENGINE=1 to run the engine "
                  "round-trip gates (runs the 2D engine; minutes).");
        m_repo    = QStringLiteral(SWMMVIS_MESH_OVERHAUL_REPO_DIR);
        m_outRoot = QStringLiteral(SWMMVIS_MESH_OVERHAUL_OUT_DIR "/engine");
        QVERIFY(QDir().mkpath(m_outRoot));
    }

    void lakeAtRest_data()
    {
        QTest::addColumn<bool>("trianglesOnly");
        QTest::newRow("quads") << false;
        QTest::newRow("triangles") << true;
    }

    void lakeAtRest()
    {
        QFETCH(bool, trianglesOnly);
        const QString shippedInp =
            m_repo + "/examples/swashes_lake_at_rest_immersed/2d_explicit.inp";
        const ShippedMesh shipped = parseInlineMesh(shippedInp);
        QVERIFY(shipped.tris.size() > 0);

        const QDir dir(m_outRoot + "/lake_at_rest/"
                       + QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(dir.path()));

        // Domain = the shipped mesh's bounding rectangle (0..25 × -1..1).
        double x0 = shipped.xy.first().x(), x1 = x0;
        double y0 = shipped.xy.first().y(), y1 = y0;
        for (const QPointF &p : shipped.xy) {
            x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
        }
        mesh::MeshGenerator g;
        g.setDomain(QPolygonF(QVector<QPointF>{
            {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}));
        mesh::GenerationOptions o;
        o.maxArea       = kSqrt3_4;          // cell size 1 m ≈ the shipped grid
        o.minCellSize   = 0.25;
        o.trianglesOnly = trianglesOnly;
        g.setOptions(o);
        mesh::MeshResult m = g.generate();
        QVERIFY2(m.errorMsg.isEmpty(), qPrintable(m.errorMsg));
        QVERIFY(m.triangles.size() > 0);

        // Bed from the shipped surface; lake-at-rest initial condition.
        for (auto &v : m.vertices) v.z = bedAt(shipped, v.xy);
        for (auto &cell : m.triangles) {
            double zMean = m.vertices[cell.v0].z + m.vertices[cell.v1].z
                         + m.vertices[cell.v2].z;
            int nv = 3;
            if (cell.isQuad()) { zMean += m.vertices[cell.v3].z; nv = 4; }
            zMean /= nv;
            cell.mannings  = 1e-8;           // frictionless, like the deck
            cell.initDepth = std::max(0.0, kLakeWse - zMean);
        }

        const QString deck = dir.filePath("lake.inp");
        QFile::remove(deck);
        QVERIFY(QFile::copy(shippedInp, deck));
        QString err;
        QVERIFY2(mesh::InpMeshWriter::writeExternal(
                     deck, dir.filePath("lake.2dm"), m, mesh::CouplingMap{},
                     0.035, &err),
                 qPrintable(err));

        QVERIFY2(runDeck(deck, &err), qPrintable(err));

        // The engine resolves OUTPUT_FILE beside the deck.
        openswmmvis::io::Mesh2DH5Reader reader;
        QVERIFY2(reader.open(dir.filePath("surface.h5")),
                 "2D results file missing beside the deck");
        const int nt = reader.timeCount();
        QVERIFY(nt >= 2);
        std::vector<float> h0, h;
        QVERIFY(reader.readDepthsAt(0, h0));
        QCOMPARE(qsizetype(h0.size()), m.triangles.size());

        QVector<double> area(m.triangles.size());
        double v0 = 0.0;
        for (int c = 0; c < m.triangles.size(); ++c) {
            area[c] = mesh::cellGeom(m.vertices, m.triangles[c]).area;
            v0 += area[c] * h0[size_t(c)];
        }
        double maxDiff = 0.0, maxVolDrift = 0.0;
        for (int t = 1; t < nt; ++t) {
            QVERIFY(reader.readDepthsAt(t, h));
            double vol = 0.0;
            for (size_t i = 0; i < h.size(); ++i) {
                maxDiff = std::max(maxDiff,
                                   double(std::abs(h[i] - h0[i])));
                vol += area[int(i)] * h[i];
            }
            maxVolDrift = std::max(maxVolDrift, std::abs(vol - v0));
        }
        qInfo("[lake %s] cells=%lld frames=%d max|h-h0|=%.3g volDrift=%.3g",
              QTest::currentDataTag(), (long long) m.triangles.size(), nt,
              maxDiff, maxVolDrift);
        QVERIFY2(maxDiff == 0.0,
                 qPrintable(QString("lake not at rest: max|h-h0| = %1")
                                .arg(maxDiff, 0, 'g', 17)));
        QVERIFY2(maxVolDrift <= 1e-9 * std::max(v0, 1.0),
                 qPrintable(QString("volume drift %1 of %2")
                                .arg(maxVolDrift).arg(v0)));
    }

    void bellingeStorm()
    {
        const QString meshB = QStringLiteral(
            SWMMVIS_MESH_OVERHAUL_OUT_DIR "/bellinge/mesh_B.2dm");
        if (!QFileInfo::exists(meshB))
            QSKIP("Run the Bellinge acceptance harness first (mesh_B.2dm).");
        const QString srcDir =
            m_repo + "/tests/output/gui_perf_2026-09-09/decks_gui";
        const QDir dir(m_outRoot + "/bellinge");
        QVERIFY(QDir().mkpath(dir.path()));
        const QString deck = dir.filePath("bellinge_T8.inp");
        QFile::remove(deck);
        QVERIFY(QFile::copy(srcDir + "/bellinge_T8.inp", deck));
        // Sidecars the deck references by relative path (the rain file
        // lives with the example, not the perf deck).
        const QHash<QString, QString> sidecars = {
            {QStringLiteral("bellinge_T8.features.gpkg"), srcDir},
            {QStringLiteral("rg_bellinge_Jun2010_Aug2021.dat"),
             m_repo + "/examples/bellinge_2d"},
        };
        for (auto it = sidecars.cbegin(); it != sidecars.cend(); ++it) {
            QFile::remove(dir.filePath(it.key()));
            if (QFileInfo::exists(it.value() + "/" + it.key()))
                QVERIFY(QFile::copy(it.value() + "/" + it.key(),
                                    dir.filePath(it.key())));
        }
        QString err;
        QVERIFY2(mesh::InpMeshWriter::writeMeshFileRef(deck, meshB, &err),
                 qPrintable(err));
        QVERIFY2(runDeck(deck, &err), qPrintable(err));

        // 2D continuity from the report, compared with the shipped run.
        auto continuityLines = [](const QString &rptPath) {
            QFile f(rptPath);
            QStringList out;
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
            const QStringList lines =
                QString::fromUtf8(f.readAll()).split('\n');
            for (const QString &l : lines)
                if (l.contains(QStringLiteral("Continuity Error"),
                               Qt::CaseInsensitive))
                    out << l.trimmed();
            return out;
        };
        const QStringList mine = continuityLines(deck + ".rpt");
        const QStringList shipped = continuityLines(srcDir + "/bellinge_T8.rpt");
        QVERIFY2(!mine.isEmpty(), "no continuity lines in the new .rpt");
        qInfo("[bellinge storm] new: %s", qPrintable(mine.join(" | ")));
        qInfo("[bellinge storm] shipped: %s", qPrintable(shipped.join(" | ")));
        // Gate: the 2D continuity error reads 0.000 % like the shipped run.
        bool zero2d = false;
        for (const QString &l : mine)
            if (l.contains(QStringLiteral("2D"), Qt::CaseInsensitive)
                && l.contains(QStringLiteral("0.000")))
                zero2d = true;
        QVERIFY2(zero2d, qPrintable(
            QStringLiteral("2D continuity not 0.000%%: ") + mine.join(" | ")));

        // Q1: no degenerate-cell / T-junction complaints from the engine.
        QFile rpt(deck + ".rpt");
        QVERIFY(rpt.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(rpt.readAll());
        // "degenerate weights" in the rainfall-fallback note is not a mesh
        // complaint; only cell/triangle degeneracy counts here.
        QVERIFY2(!text.contains(QRegularExpression(
                     QStringLiteral("degenerate (cell|triangle|quad)"),
                     QRegularExpression::CaseInsensitiveOption)),
                 "engine reported degenerate cells");
        QVERIFY2(!text.contains(QStringLiteral("T-junction"),
                                Qt::CaseInsensitive),
                 "engine reported T-junctions");
    }
};

QTEST_MAIN(TestMeshOverhaulEngine)
#include "test_mesh_overhaul_engine.moc"
