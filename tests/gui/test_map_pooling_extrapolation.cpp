/*!
 * \file   test_map_pooling_extrapolation.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * 2D map — pooling extrapolation into adjacent dry cells
 * (workplans/2D_MAP_POOLING_EXTRAPOLATION_PLAN_2026-08-04.md, Phase 1).
 *
 * The map's marching-triangles bands/isolines interpolate SceneTri::dv0/dv1/dv2
 * LINEARLY. Those carry a signed depth η−z whose value exactly 0 is a NO-DATA
 * sentinel, so before the fix the bands read the sentinel as a real depth of 0
 * and dragged the waterline out to the dry vertex — painting water above the
 * pool level on dry bed. VertexDepthReconstruct::extrapolateDryCorners replaces
 * the sentinel with the extrapolated signed depth maxEta − z_k, which restores
 * linearity (z is linear on a triangle, η is constant in the dry direction) and
 * puts the zero crossing on the true bed intercept.
 *
 * Fixture (the plan's measured case): a flat reach at z = 0 holding a pool at
 * η = 5, meeting an adverse bank climbing z = 0 → 8 over one coarse cell that
 * the solver marks dry. True waterline at x = 16.25.
 *
 * Per CLAUDE.md §4.1 the sampled series is written to
 * test_artifacts/map_pooling_extrapolation/ (under the ctest working directory)
 * so the before/after curves are reviewable.
 */

#include "layers/cellsurfaceinterp.h"
#include "layers/vertexdepthreconstruct.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>
#include <QTextStream>

#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr double kEta      = 5.0;     // pool free surface
constexpr double kDryDepth = 1e-4;    // layer dry_depth_ default
constexpr double kIntercept = 16.25;  // where the bed reaches η

// ── The two-cell strip ──────────────────────────────────────────────────────
// Stations x = {0, 10, 20} at bed z = {0, 0, 8}; rows y = {0, 10}.
// Vertex index = station*2 + row.
struct Strip
{
    std::vector<double>            vx, vy, vz;
    std::vector<std::array<int,3>> tris;
    std::vector<float>             cellDepth, cellZc;
    std::vector<float>             sd;        // per-vertex signed depth
};

Strip buildStrip()
{
    Strip s;
    const double xs[3] = {0.0, 10.0, 20.0};
    const double zs[3] = {0.0,  0.0,  8.0};
    for (int st = 0; st < 3; ++st)
        for (int r = 0; r < 2; ++r) {
            s.vx.push_back(xs[st]);
            s.vy.push_back(r * 10.0);
            s.vz.push_back(zs[st]);
        }
    auto V = [](int st, int r) { return st * 2 + r; };
    for (int st = 0; st < 2; ++st) {
        s.tris.push_back({V(st,0),   V(st+1,0), V(st,1)});
        s.tris.push_back({V(st+1,0), V(st+1,1), V(st,1)});
    }

    const int nTri = int(s.tris.size());
    s.cellZc.resize(nTri);
    s.cellDepth.resize(nTri);
    for (int t = 0; t < nTri; ++t) {
        s.cellZc[t] = float((s.vz[s.tris[t][0]] + s.vz[s.tris[t][1]]
                             + s.vz[s.tris[t][2]]) / 3.0);
        // Bank cells (any corner above the flat reach) are solver-dry; the
        // flat reach holds the pool.
        const bool bank = s.vz[s.tris[t][0]] > 0.0 || s.vz[s.tris[t][1]] > 0.0
                          || s.vz[s.tris[t][2]] > 0.0;
        s.cellDepth[t] = bank ? 0.0f : float(kEta);
    }

    std::vector<float> vsum, wsum;
    VertexDepthReconstruct::reconstructVertexSignedDepths(
        s.tris, s.cellDepth, s.cellZc, s.vz, float(kDryDepth),
        vsum, wsum, s.sd);
    return s;
}

/*! Per-triangle corner values as the renderer sees them (SceneTri::dv*),
 *  optionally after the extrapolation pass. */
struct Corners { double z[3]; float sd[3]; };

Corners cornersOf(const Strip &s, int tri, bool extrapolate)
{
    Corners c;
    for (int k = 0; k < 3; ++k) {
        c.z[k]  = s.vz[s.tris[tri][k]];
        c.sd[k] = s.sd[s.tris[tri][k]];
    }
    if (extrapolate)
        VertexDepthReconstruct::extrapolateDryCorners(
            c.z[0], c.z[1], c.z[2], c.sd[0], c.sd[1], c.sd[2]);
    return c;
}

/*! Barycentric weights of \p p in triangle \p tri (w,v,u for corners 0,1,2) —
 *  the same construction the marching passes and CellSurfaceInterp use. */
bool baryAt(const Strip &s, int tri, const QPointF &p, double w[3])
{
    const QPointF a(s.vx[s.tris[tri][0]], s.vy[s.tris[tri][0]]);
    const QPointF b(s.vx[s.tris[tri][1]], s.vy[s.tris[tri][1]]);
    const QPointF c(s.vx[s.tris[tri][2]], s.vy[s.tris[tri][2]]);
    const double v0x = c.x()-a.x(), v0y = c.y()-a.y();
    const double v1x = b.x()-a.x(), v1y = b.y()-a.y();
    const double v2x = p.x()-a.x(), v2y = p.y()-a.y();
    const double d00 = v0x*v0x + v0y*v0y, d01 = v0x*v1x + v0y*v1y;
    const double d11 = v1x*v1x + v1y*v1y;
    const double d20 = v2x*v0x + v2y*v0y, d21 = v2x*v1x + v2y*v1y;
    const double den = d00*d11 - d01*d01;
    if (den == 0.0) return false;
    const double u = (d11*d20 - d01*d21) / den;
    const double v = (d00*d21 - d01*d20) / den;
    w[0] = 1.0 - u - v; w[1] = v; w[2] = u;
    return w[0] >= -1e-9 && w[1] >= -1e-9 && w[2] >= -1e-9;
}

int locate(const Strip &s, const QPointF &p, double w[3])
{
    for (int t = 0; t < int(s.tris.size()); ++t)
        if (baryAt(s, t, p, w)) return t;
    return -1;
}

/*! What the marching bands/isolines see: the plain linear blend of the corner
 *  values. This is the quantity whose dryDepth crossing IS the painted
 *  shoreline. */
double marchingValueAt(const Strip &s, const QPointF &p, bool extrapolate,
                       double *groundOut = nullptr)
{
    double w[3];
    const int t = locate(s, p, w);
    if (t < 0) return 0.0;
    const Corners c = cornersOf(s, t, extrapolate);
    if (groundOut)
        *groundOut = w[0]*c.z[0] + w[1]*c.z[1] + w[2]*c.z[2];
    return w[0]*double(c.sd[0]) + w[1]*double(c.sd[1]) + w[2]*double(c.sd[2]);
}

/*! The widened fill gate from Phase 3 — strictly additive over the old
 *  cell-mean test, so no cell that painted before stops painting. */
bool fillGatePasses(double cellMeanDepth, const Corners &c, double dryDepth)
{
    if (cellMeanDepth >= dryDepth) return true;
    return double(c.sd[0]) >= dryDepth || double(c.sd[1]) >= dryDepth
           || double(c.sd[2]) >= dryDepth;
}

void writeSeriesCsv(const Strip &s)
{
    QDir().mkpath(QStringLiteral("test_artifacts/map_pooling_extrapolation"));
    QFile f(QStringLiteral(
        "test_artifacts/map_pooling_extrapolation/bank_transect.csv"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << "x,ground,before,after,truth\n";
    for (double x = 9.0; x <= 20.0001; x += 0.25) {
        const QPointF p(x, 5.0);
        double g = 0.0;
        const double before = marchingValueAt(s, p, false, &g);
        const double after  = marchingValueAt(s, p, true);
        const double truth  = kEta - g;
        ts << x << ',' << g << ',' << before << ',' << after << ','
           << truth << '\n';
    }
}

} // namespace

class TestMapPoolingExtrapolation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { writeSeriesCsv(buildStrip()); }

    /*! The extrapolated field is EXACT on this fixture: z is linear on the
     *  triangle and η is constant in the dry direction, so the linear blend the
     *  marching passes take reproduces max(0, η − z) everywhere. */
    void extrapolatedFieldMatchesTruth()
    {
        const Strip s = buildStrip();
        for (double x = 10.0; x <= 20.0001; x += 0.5) {
            const QPointF p(x, 5.0);
            double g = 0.0;
            const double after = marchingValueAt(s, p, true, &g);
            QVERIFY2(std::abs(after - (kEta - g)) < 1e-9,
                     qPrintable(QStringLiteral("x=%1 after=%2 truth=%3")
                                .arg(x).arg(after).arg(kEta - g)));
        }
    }

    /*! The painted shoreline (dryDepth crossing of the marching field) lands on
     *  the bed intercept, not on the dry vertex. Before the fix the sentinel
     *  pulled it out to x = 20. */
    void bandWaterlineLandsOnBedIntercept()
    {
        const Strip s = buildStrip();
        auto crossing = [&](bool extrapolate) {
            double lo = 10.0, hi = 20.0;
            for (int i = 0; i < 60; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (marchingValueAt(s, QPointF(mid, 5.0), extrapolate) > kDryDepth)
                    lo = mid;
                else
                    hi = mid;
            }
            return 0.5 * (lo + hi);
        };
        QVERIFY(std::abs(crossing(true) - kIntercept) < 1e-3);
        // Pin the defect being fixed: the sentinel put the waterline at the
        // crest vertex, a full 3.75 m of bank beyond the true shoreline.
        QVERIFY(crossing(false) > 19.9);
    }

    /*! No painted water anywhere the bed stands above the pool that supplies
     *  it. Before the fix the bands painted 1.5 m of depth at x = 17, where the
     *  bed is 0.6 m ABOVE the free surface. */
    void noWaterPaintedAbovePoolSurface()
    {
        const Strip s = buildStrip();
        double worstBefore = 0.0;
        for (double x = 10.0; x <= 20.0001; x += 0.25) {
            const QPointF p(x, 5.0);
            double g = 0.0;
            const double after = marchingValueAt(s, p, true, &g);
            if (g <= kEta) continue;
            QVERIFY2(after < kDryDepth,
                     qPrintable(QStringLiteral("x=%1 ground=%2 depth=%3")
                                .arg(x).arg(g).arg(after)));
            worstBefore = std::max(worstBefore,
                                   marchingValueAt(s, p, false));
        }
        QVERIFY(worstBefore > 1.0);   // the defect was gross, not marginal
    }

    /*! No extrapolated corner may sit above the pool surface that drives it. */
    void extrapolationRespectsDrivingHead()
    {
        const Strip s = buildStrip();
        for (int t = 0; t < int(s.tris.size()); ++t) {
            const Corners raw = cornersOf(s, t, false);
            double maxEta = 0.0; bool any = false;
            for (int k = 0; k < 3; ++k)
                if (raw.sd[k] > 0.0f) {
                    const double e = raw.z[k] + double(raw.sd[k]);
                    if (!any || e > maxEta) { maxEta = e; any = true; }
                }
            if (!any) continue;
            const Corners ex = cornersOf(s, t, true);
            for (int k = 0; k < 3; ++k)
                QVERIFY(ex.z[k] + double(ex.sd[k]) <= maxEta + 1e-9);
        }
    }

    /*! Fully-wet and fully-dry cells are untouched — the common case carries no
     *  regression risk and no cost. */
    void fullyWetAndFullyDryCellsUnchanged()
    {
        const Strip s = buildStrip();
        // Triangles 0/1 are the fully-wet flat reach.
        for (int t : {0, 1}) {
            const Corners raw = cornersOf(s, t, false);
            const Corners ex  = cornersOf(s, t, true);
            for (int k = 0; k < 3; ++k) {
                QVERIFY(raw.sd[k] > 0.0f);
                QCOMPARE(ex.sd[k], raw.sd[k]);
            }
        }
        // A synthetic all-no-data cell must stay all-no-data.
        float sd0 = 0.0f, sd1 = 0.0f, sd2 = 0.0f;
        VertexDepthReconstruct::extrapolateDryCorners(1.0, 2.0, 3.0,
                                                      sd0, sd1, sd2);
        QCOMPARE(sd0, 0.0f);
        QCOMPARE(sd1, 0.0f);
        QCOMPARE(sd2, 0.0f);
    }

    /*! ADVERSE SLOPE ONLY. A dry corner whose bed falls BELOW the driving head
     *  keeps the sentinel: extrapolating there would inject standing water into
     *  a dry cell and spread the pool one cell downhill in every direction —
     *  the flood-fill behaviour this feature does not do. Regression pin for
     *  the 2026-08-04 live-render breakage. */
    void doesNotExtrapolateDownhill()
    {
        // One wet corner at η = 5 (z = 0, sd = 5); one dry corner well BELOW
        // the pool (a channel bed at z = −2) and one above it (z = 8).
        float sd0 = 5.0f, sd1 = 0.0f, sd2 = 0.0f;
        VertexDepthReconstruct::extrapolateDryCorners(0.0, -2.0, 8.0,
                                                      sd0, sd1, sd2);
        QCOMPARE(sd0, 5.0f);    // wet corner untouched
        QCOMPARE(sd1, 0.0f);    // downhill dry corner keeps the sentinel
        QCOMPARE(sd2, -3.0f);   // adverse dry corner carries maxEta − z
    }

    /*! No corner may be turned wet by extrapolation — the fill gate and
     *  CellSurfaceInterp both key on sd > 0, so a positive extrapolated value
     *  would paint invented water and flip the profile's supplying set. */
    void extrapolationNeverCreatesAWetCorner()
    {
        const double beds[] = { -50.0, -2.0, 0.0, 4.999, 5.0, 5.001, 8.0, 90.0 };
        for (double zDry : beds) {
            float sd0 = 5.0f, sd1 = 0.0f, sd2 = 0.0f;
            VertexDepthReconstruct::extrapolateDryCorners(0.0, zDry, 40.0,
                                                          sd0, sd1, sd2);
            QVERIFY2(sd1 <= 0.0f,
                     qPrintable(QStringLiteral("zDry=%1 sd=%2")
                                .arg(zDry).arg(sd1)));
        }
    }

    /*! The profile path is bitwise unaffected on the canonical bank: an
     *  extrapolated corner below the pool (maxEta − z < 0) is non-supplying in
     *  CellSurfaceInterp exactly as the 0 sentinel was. */
    void profilePathUnchangedOnBank()
    {
        const Strip s = buildStrip();
        for (double x = 10.0; x <= 20.0001; x += 0.5) {
            const QPointF p(x, 5.0);
            double w[3];
            const int t = locate(s, p, w);
            QVERIFY(t >= 0);
            const QPointF a(s.vx[s.tris[t][0]], s.vy[s.tris[t][0]]);
            const QPointF b(s.vx[s.tris[t][1]], s.vy[s.tris[t][1]]);
            const QPointF c(s.vx[s.tris[t][2]], s.vy[s.tris[t][2]]);
            const Corners raw = cornersOf(s, t, false);
            const Corners ex  = cornersOf(s, t, true);
            const double dRaw = CellSurfaceInterp::depthAt(
                p, a, b, c, raw.z[0], raw.z[1], raw.z[2],
                raw.sd[0], raw.sd[1], raw.sd[2]);
            const double dEx = CellSurfaceInterp::depthAt(
                p, a, b, c, ex.z[0], ex.z[1], ex.z[2],
                ex.sd[0], ex.sd[1], ex.sd[2]);
            QCOMPARE(dEx, dRaw);
        }
    }

    /*! The widened fill gate admits the dry bank cell (it carries a wet corner)
     *  while a cell with no wet corner still paints nothing. */
    void widenedFillGateAdmitsBankCellOnly()
    {
        const Strip s = buildStrip();
        bool sawBank = false;
        for (int t = 0; t < int(s.tris.size()); ++t) {
            const Corners ex = cornersOf(s, t, true);
            const bool pass  = fillGatePasses(double(s.cellDepth[t]), ex,
                                              kDryDepth);
            QVERIFY(pass);   // every cell in this strip touches the pool
            if (s.cellDepth[t] < kDryDepth) sawBank = true;
        }
        QVERIFY(sawBank);    // at least one solver-dry cell now paints

        // A cell with no wet corner is still rejected.
        Corners dry{};
        dry.z[0] = dry.z[1] = dry.z[2] = 30.0;
        dry.sd[0] = dry.sd[1] = dry.sd[2] = 0.0f;
        QVERIFY(!fillGatePasses(0.0, dry, kDryDepth));
    }
};

QTEST_MAIN(TestMapPoolingExtrapolation)
#include "test_map_pooling_extrapolation.moc"
