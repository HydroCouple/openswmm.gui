/*!
 * \file   test_channelburn.cpp
 * \brief  Gate V1 (engine half) for the channel burn-in, phase P0
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §8, §16.7).
 *
 * The reconstructed bed must BE the section the engine routes flow through,
 * not a look-alike. This test drives the production path — the engine's own
 * width ladder via XsectSampler::widthsAtDepths, then
 * mesh::sectionFromWidths — and compares area and top width against the
 * engine's own xsect tables at ten depths.
 *
 * Tolerances (plan §16.7). Three classes, and the plan's single 1e-6 covers
 * only the first:
 *   - straight-sided analytic (RECT_OPEN, TRAPEZOIDAL, TRIANGULAR) — piecewise
 *     linear on both sides, so the reconstruction is EXACT: 1e-6 relative;
 *   - curved analytic (PARABOLIC, POWER) — the reconstruction reads a chord
 *     where the engine reads the curve. That is quadrature, so the gate is a
 *     looser bound PLUS a convergence check that the error falls with the
 *     ladder (parabolicConvergesWithTheLadder);
 *   - IRREGULAR — the engine tabulates a transect on N_TRANSECT_TBL = 51
 *     normalised points with linear interpolation, so the comparison is at
 *     table resolution, and the authored geometry is checked directly.
 */

#include <QtTest>

#include "mesh/channelburnprofile.h"
#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_xsect.h>

#include <cmath>

using openswmmvis::sectionview::XsectSampler;

class TestChannelBurn : public QObject
{
    Q_OBJECT

private slots:
    void reconstructionMatchesEngineTables();
    void reconstructionMatchesEngineTables_data();
    void parabolicConvergesWithTheLadder();
    void irregularKeepsTheAuthoredAsymmetry();
    void openChannelGateRejectsClosedShapes();
};

namespace {

/*! Flow area of a normalised section at \p d, by exact trapezoidal integration
 *  of the wetted part. The profile is piecewise linear, so this is exact. */
double areaAtDepth(const mesh::NormalizedSection &ns, double d)
{
    double a = 0.0;
    for (int i = 0; i + 1 < ns.station.size(); ++i)
    {
        const double x0 = ns.station[i], x1 = ns.station[i + 1];
        const double h0 = d - ns.relZ[i], h1 = d - ns.relZ[i + 1];
        if (h0 <= 0.0 && h1 <= 0.0) continue;
        if (h0 >= 0.0 && h1 >= 0.0) { a += 0.5 * (h0 + h1) * (x1 - x0); continue; }
        const double t  = h0 / (h0 - h1);
        const double xc = x0 + t * (x1 - x0);
        a += (h0 > 0.0) ? 0.5 * h0 * (xc - x0) : 0.5 * h1 * (x1 - xc);
    }
    return a;
}

double topWidthAtDepth(const mesh::NormalizedSection &ns, double d)
{
    double w = 0.0;
    for (int i = 0; i + 1 < ns.station.size(); ++i)
    {
        const double x0 = ns.station[i], x1 = ns.station[i + 1];
        const double h0 = d - ns.relZ[i], h1 = d - ns.relZ[i + 1];
        if (h0 <= 0.0 && h1 <= 0.0) continue;
        if (h0 >= 0.0 && h1 >= 0.0) { w += x1 - x0; continue; }
        const double t  = h0 / (h0 - h1);
        const double xc = x0 + t * (x1 - x0);
        w += (h0 > 0.0) ? (xc - x0) : (x1 - xc);
    }
    return w;
}

mesh::BurnOptions burnOptions()
{
    mesh::BurnOptions o;
    o.clipToBanks = false;      // compare the WHOLE section against the engine
    o.stringCount = 0;
    return o;
}

} // namespace

void TestChannelBurn::reconstructionMatchesEngineTables_data()
{
    QTest::addColumn<int>("shape");
    QTest::addColumn<double>("g1");
    QTest::addColumn<double>("g2");
    QTest::addColumn<double>("g3");
    QTest::addColumn<double>("g4");
    QTest::addColumn<double>("tol");

    // A straight-sided section is piecewise linear on both sides of the
    // comparison, so the reconstruction is EXACT. A curved one is sampled: the
    // reconstruction reads a chord where the engine reads the curve, which is a
    // quadrature error, not a defect — parabolicConvergesWithTheLadder pins that
    // it behaves like one (plan §16.7).
    QTest::newRow("trapezoidal") << int(SWMM_XSECT_TRAPEZOIDAL) << 3.0 << 4.0 << 2.0 << 2.0 << 1e-6;
    QTest::newRow("triangular")  << int(SWMM_XSECT_TRIANGULAR)  << 2.0 << 6.0 << 0.0 << 0.0 << 1e-6;
    QTest::newRow("rect_open")   << int(SWMM_XSECT_RECT_OPEN)   << 3.0 << 5.0 << 0.0 << 0.0 << 1e-6;
    QTest::newRow("parabolic")   << int(SWMM_XSECT_PARABOLIC)   << 2.0 << 6.0 << 0.0 << 0.0 << 1e-3;
}

void TestChannelBurn::reconstructionMatchesEngineTables()
{
    QFETCH(int, shape);
    QFETCH(double, g1);
    QFETCH(double, g2);
    QFETCH(double, g3);
    QFETCH(double, g4);
    QFETCH(double, tol);

    // Production path: the engine's own width ladder, cosine-spaced.
    XsectSampler s = XsectSampler::fromShape(shape, g1, g2, g3, g4, /*si*/ false);
    QVERIFY(s.isValid());
    const auto props = s.fullProps();
    QVERIFY(props.open);
    QVERIFY(props.yFull > 0.0);

    const QVector<double> depths = mesh::cosineDepthLadder(props.yFull, 128);
    const QVector<double> widths = s.widthsAtDepths(depths);
    QCOMPARE(widths.size(), depths.size());

    QString err;
    const mesh::NormalizedSection ns =
        mesh::normalizeSection(mesh::sectionFromWidths(depths, widths), burnOptions(),
                               nullptr, &err);
    QVERIFY2(ns.isValid(), qPrintable(err));

    // Reference: the engine's own tables, through a handle of our own.
    SWMM_XSect x = nullptr;
    QCOMPARE(swmm_xsect_create(shape, g1, g2, g3, g4, SWMM_UNITS_US, &x), SWMM_OK);
    QVERIFY(x != nullptr);

    // A vertical wall cannot live in a raster, so a constant-width section
    // reduces to its bed (plan §16.4); its AREA therefore diverges from the
    // engine above the invert by construction. Width is still exact.
    const bool verticalWalls = (shape == SWMM_XSECT_RECT_OPEN);

    for (int i = 1; i <= 10; ++i)
    {
        const double d = props.yFull * double(i) / 10.0;
        double refA = 0.0, refW = 0.0;
        QCOMPARE(swmm_xsect_area_of_depth(x, d, &refA), SWMM_OK);
        QCOMPARE(swmm_xsect_width_of_depth(x, d, &refW), SWMM_OK);

        const double gotW = topWidthAtDepth(ns, d);
        QVERIFY2(std::abs(gotW - refW) <= tol * std::max(1.0, refW),
                 qPrintable(QStringLiteral("depth %1: width %2 vs engine %3")
                                .arg(d).arg(gotW).arg(refW)));
        if (verticalWalls) continue;

        const double gotA = areaAtDepth(ns, d);
        QVERIFY2(std::abs(gotA - refA) <= tol * std::max(1.0, refA),
                 qPrintable(QStringLiteral("depth %1: area %2 vs engine %3")
                                .arg(d).arg(gotA).arg(refA)));
    }
    swmm_xsect_free(x);
}

void TestChannelBurn::parabolicConvergesWithTheLadder()
{
    // The parabolic row's slack is quadrature, so refining the depth ladder
    // must shrink it. A real reconstruction bug would not converge.
    XsectSampler s = XsectSampler::fromShape(SWMM_XSECT_PARABOLIC, 2.0, 6.0, 0, 0, false);
    QVERIFY(s.isValid());
    const double yFull = s.fullProps().yFull;

    SWMM_XSect x = nullptr;
    QCOMPARE(swmm_xsect_create(SWMM_XSECT_PARABOLIC, 2.0, 6.0, 0, 0, SWMM_UNITS_US, &x),
             SWMM_OK);

    auto maxWidthError = [&](int samples) {
        const QVector<double> depths = mesh::cosineDepthLadder(yFull, samples);
        const mesh::NormalizedSection ns =
            mesh::normalizeSection(mesh::sectionFromWidths(depths, s.widthsAtDepths(depths)),
                                   burnOptions());
        double worst = 0.0;
        for (int i = 1; i <= 10; ++i)
        {
            const double d = yFull * double(i) / 10.0;
            double refW = 0.0;
            swmm_xsect_width_of_depth(x, d, &refW);
            worst = std::max(worst, std::abs(topWidthAtDepth(ns, d) - refW));
        }
        return worst;
    };

    const double coarse = maxWidthError(32);
    const double fine   = maxWidthError(256);
    swmm_xsect_free(x);

    QVERIFY(coarse > 0.0);
    QVERIFY2(fine < coarse * 0.25,
             qPrintable(QStringLiteral("no convergence: 32 samples %1, 256 samples %2")
                            .arg(coarse).arg(fine)));
}

void TestChannelBurn::irregularKeepsTheAuthoredAsymmetry()
{
    // 12-point transect with a non-zero Xfactor / Yfactor, deliberately
    // asymmetric: the thalweg sits left of centre.
    const QVector<double> st = { 0.0,  5.0, 10.0, 14.0, 18.0, 20.0,
                                22.0, 26.0, 32.0, 40.0, 50.0, 60.0};
    const QVector<double> el = {812.0, 808.0, 804.0, 801.0, 799.5, 799.0,
                                800.0, 802.5, 805.0, 807.0, 810.0, 813.0};
    const double xLeft = 14.0, xRight = 26.0;

    const double xMul = 1.5, yOff = -799.0;
    const mesh::SectionGeometry g =
        mesh::sectionFromTransect(st, el, xLeft, xRight, 0.06, 0.035, 0.07, xMul, yOff);
    QVERIFY(mesh::validateSection(g).isEmpty());

    QString err;
    const mesh::NormalizedSection ns =
        mesh::normalizeSection(g, burnOptions(), nullptr, &err);
    QVERIFY2(ns.isValid(), qPrintable(err));

    // The thalweg (station 20 × 1.5 = 30) becomes s = 0, and the section is
    // NOT symmetric about it — which is exactly what outline() would destroy.
    QVERIFY(std::abs(relZAt(ns, 0.0)) < 1e-12);
    QCOMPARE(ns.station.first(), -30.0);
    QCOMPARE(ns.station.last(),   60.0);
    QVERIFY(std::abs(ns.sMin) < std::abs(ns.sMax));
    QVERIFY(std::abs(relZAt(ns, -6.0) - relZAt(ns, 6.0)) > 0.1);

    // The bank stations and the roughness triple survive the transform.
    QCOMPARE(ns.leftBank,  xLeft  * xMul - 30.0);
    QCOMPARE(ns.rightBank, xRight * xMul - 30.0);
    QCOMPARE(ns.nChannel,  0.035);

    // Against the engine's own handle for the SAME transformed transect: the
    // top width is geometry, not a conveyance-weighted table, so it agrees to
    // table resolution at every depth.
    QVector<double> sx, sy;
    for (int i = 0; i < st.size(); ++i)
    {
        sx.append(st[i] * xMul);
        sy.append(el[i] + yOff);
    }
    SWMM_XSect x = nullptr;
    QCOMPARE(swmm_xsect_create_irregular(sx.constData(), sy.constData(), sx.size(),
                                         xLeft * xMul, xRight * xMul,
                                         0.06, 0.035, 0.07, 1.0,
                                         SWMM_UNITS_US, &x),
             SWMM_OK);

    double yFull = 0.0, aFull = 0.0, rFull = 0.0, wMax = 0.0, sFull = 0.0, aMax = 0.0;
    QCOMPARE(swmm_xsect_full_properties(x, &yFull, &aFull, &rFull, &wMax, &sFull, &aMax),
             SWMM_OK);
    QVERIFY(yFull > 0.0);

    for (int i = 2; i <= 10; ++i)
    {
        const double d = yFull * double(i) / 10.0;
        double refW = 0.0;
        QCOMPARE(swmm_xsect_width_of_depth(x, d, &refW), SWMM_OK);
        const double gotW = topWidthAtDepth(ns, d);
        // 51-point normalised table + linear interpolation: a depth between
        // two table rows is read off the chord, so the tolerance is the table's,
        // not the geometry's.
        QVERIFY2(std::abs(gotW - refW) <= 2.0e-2 * std::max(1.0, refW),
                 qPrintable(QStringLiteral("depth %1: width %2 vs engine %3")
                                .arg(d).arg(gotW).arg(refW)));
    }
    swmm_xsect_free(x);
}

void TestChannelBurn::openChannelGateRejectsClosedShapes()
{
    // The burn's gate is XsectSampler::fullProps().open — swmm_xsect_is_open()
    // needs a handle, so it cannot be called on swmm_link_get_xsect output
    // (plan §16.4). A culvert must not be burned (D-E), and a STREET is
    // classified CLOSED by the engine, which delivers D-F's default-off.
    XsectSampler circ = XsectSampler::fromShape(SWMM_XSECT_CIRCULAR, 2.0, 0, 0, 0, false);
    QVERIFY(circ.isValid());
    QVERIFY(!circ.fullProps().open);

    XsectSampler trap = XsectSampler::fromShape(SWMM_XSECT_TRAPEZOIDAL, 3.0, 4.0, 2.0, 2.0, false);
    QVERIFY(trap.isValid());
    QVERIFY(trap.fullProps().open);
}

QTEST_MAIN(TestChannelBurn)
#include "test_channelburn.moc"
