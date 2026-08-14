/*!
 * \file   test_xsectsampler.cpp
 * \brief  Slice SP.1 — engine cross-section sampler contract.
 *
 * Pins the three things the section-preview panels depend on:
 *   1. Every shape the GUI's picker surfaces produces a valid engine handle
 *      from its nominal geoms — a shape that fails here silently degrades to
 *      the placeholder icon and an empty preview.
 *   2. The sampled outline agrees with the engine's own full properties
 *      (max width, full depth) — i.e. the drawing is the real geometry, not a
 *      decorative approximation.
 *   3. Shape ids round-trip. openswmm_xsect.h takes SWMM_XSectShape codes from
 *      openswmm_links.h, the same numbering xsectshapegeom.h uses; this test
 *      is what fails loudly if a future engine renumbering breaks that.
 */

#include <QtTest>

#include "ui/properties/xsectshapegeom.h"
#include "ui/sectionview/xsectsampler.h"
#include "ui/sectionview/xsecticonrenderer.h"

#include <openswmm/engine/openswmm_links.h>

#include <QtMath>   // guarantees M_PI on MSVC

#include <cmath>

using openswmmvis::sectionview::XsectFullProps;
using openswmmvis::sectionview::XsectSampler;

class TestXsectSampler : public QObject
{
    Q_OBJECT

private slots:
    void invalidSamplerIsInert();
    void circularMatchesClosedForm();
    void everySurfacedShapeBuilds();
    void everySurfacedShapeBuilds_data();
    void outlineMatchesFullProperties();
    void outlineMatchesFullProperties_data();
    void outlineIsSymmetricAndClosed();
    void dummyHasNoGeometry();
    void shapeIdRoundTrips();
    void moveTransfersOwnership();
    void openChannelsReportOpen();

    // Tabulated shapes — the ones geom1..4 cannot describe.
    void irregularFromTransectData();
    void irregularRejectsBadInput();
    void streetFromParameters();
    void customFromShapeCurve();
    void everyShapeGetsANonPlaceholderIcon();
    void everyShapeGetsANonPlaceholderIcon_data();
};

// ---------------------------------------------------------------------------

void TestXsectSampler::invalidSamplerIsInert()
{
    XsectSampler s;
    QVERIFY(!s.isValid());
    QCOMPARE(s.shape(), -1);
    QVERIFY(s.outline().isEmpty());
    QVERIFY(s.widthsAtDepths({ 0.0, 1.0 }).isEmpty());

    const XsectFullProps p = s.fullProps();
    QCOMPARE(p.yFull, 0.0);
    QCOMPARE(p.aFull, 0.0);

    // A degenerate request must fail rather than produce a zero-size section.
    XsectSampler bad = XsectSampler::fromShape(SWMM_XSECT_CIRCULAR,
                                               0.0, 0, 0, 0, true);
    QVERIFY(!bad.isValid());
}

void TestXsectSampler::circularMatchesClosedForm()
{
    constexpr double D = 2.0;
    XsectSampler s = XsectSampler::fromShape(SWMM_XSECT_CIRCULAR, D, 0, 0, 0, true);
    QVERIFY(s.isValid());

    const XsectFullProps p = s.fullProps();
    QVERIFY(qFuzzyCompare(p.yFull + 1.0, D + 1.0));
    QVERIFY(std::abs(p.wMax - D) < 1.0e-6);
    // Area of a full circle, and R = A/P = D/4.
    QVERIFY(std::abs(p.aFull - M_PI * D * D / 4.0) < 1.0e-3);
    QVERIFY(std::abs(p.rFull - D / 4.0) < 1.0e-3);

    // Width at mid-depth is the diameter; at the springline of a circle the
    // chord is widest.
    const QVector<double> w = s.widthsAtDepths({ D / 2.0 });
    QCOMPARE(w.size(), 1);
    QVERIFY(std::abs(w.at(0) - D) < 1.0e-3);
}

void TestXsectSampler::everySurfacedShapeBuilds_data()
{
    QTest::addColumn<int>("shape");
    QTest::addColumn<QString>("name");
    for (const auto &row : openswmmvis::kXsectShapes)
        QTest::newRow(row.name) << row.engineId << QString::fromLatin1(row.name);
}

void TestXsectSampler::everySurfacedShapeBuilds()
{
    QFETCH(int, shape);
    QFETCH(QString, name);

    // IRREGULAR / CUSTOM / STREET are tabulated — they have no standalone
    // geometry and are drawn from their tables, not from geom1..4. DUMMY has
    // no geometry by definition.
    if (shape == SWMM_XSECT_IRREGULAR || shape == SWMM_XSECT_CUSTOM
        || shape == SWMM_XSECT_STREET || shape == SWMM_XSECT_DUMMY)
        QSKIP("tabulated or geometry-less shape");

    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    openswmmvis::sectionview::nominalGeomsFor(shape, g1, g2, g3, g4);

    XsectSampler s = XsectSampler::fromShape(shape, g1, g2, g3, g4, true);
    QVERIFY2(s.isValid(),
             qPrintable(QStringLiteral("%1 rejected its nominal geoms").arg(name)));

    const QPolygonF outline = s.outline(64);
    QVERIFY2(outline.size() >= 3,
             qPrintable(QStringLiteral("%1 produced no outline").arg(name)));
}

void TestXsectSampler::outlineMatchesFullProperties_data()
{
    QTest::addColumn<int>("shape");
    QTest::addColumn<double>("g1");
    QTest::addColumn<double>("g2");

    QTest::newRow("circular")    << int(SWMM_XSECT_CIRCULAR)    << 1.5 << 0.0;
    QTest::newRow("rect_closed") << int(SWMM_XSECT_RECT_CLOSED) << 2.0 << 1.2;
    QTest::newRow("egg")         << int(SWMM_XSECT_EGGSHAPED)   << 1.8 << 0.0;
    QTest::newRow("horseshoe")   << int(SWMM_XSECT_HORSESHOE)   << 1.4 << 0.0;
    QTest::newRow("triangular")  << int(SWMM_XSECT_TRIANGULAR)  << 1.0 << 2.0;
    QTest::newRow("horiz_ell")   << int(SWMM_XSECT_HORIZ_ELLIPSE) << 1.0 << 1.6;
}

void TestXsectSampler::outlineMatchesFullProperties()
{
    QFETCH(int, shape);
    QFETCH(double, g1);
    QFETCH(double, g2);

    XsectSampler s = XsectSampler::fromShape(shape, g1, g2, 0, 0, true);
    QVERIFY(s.isValid());

    const XsectFullProps p = s.fullProps();
    const QPolygonF outline = s.outline(256);
    QVERIFY(outline.size() >= 3);

    const QRectF b = outline.boundingRect();

    // The outline's own extents must reproduce what the engine reports, which
    // is the whole point of sampling rather than approximating.
    QVERIFY2(std::abs(b.height() - p.yFull) < 1.0e-6,
             qPrintable(QStringLiteral("height %1 vs yFull %2")
                            .arg(b.height()).arg(p.yFull)));
    // Width gets a RELATIVE tolerance because the two numbers do not come from
    // the same place in the engine. wMax is reported from the shape parameter;
    // the widths come from swmm_xsect_width_of_depth, which for the shapes
    // SWMM stores as normalized tables (HORSESHOE, HORIZ_ELLIPSE, …) peaks at
    // exactly 0.9992 of it — the table's own peak entry. Measured with
    // tests/scratch/sp_wmax_probe.c: the shortfall is bit-identical at 64,
    // 256, 512 and 4096 rungs, so it is the table, not the depth ladder.
    // Analytic shapes (CIRCULAR) hit wMax exactly.
    QVERIFY2(b.width() <= p.wMax + 1.0e-9 && b.width() >= 0.999 * p.wMax,
             qPrintable(QStringLiteral("width %1 vs wMax %2 (ratio %3)")
                            .arg(b.width()).arg(p.wMax)
                            .arg(p.wMax > 0 ? b.width() / p.wMax : 0.0)));

    // y is measured up from the invert, so the ring starts on y = 0.
    QVERIFY(std::abs(b.top()) < 1.0e-9);
}

void TestXsectSampler::outlineIsSymmetricAndClosed()
{
    XsectSampler s = XsectSampler::fromShape(SWMM_XSECT_EGGSHAPED,
                                             1.5, 0, 0, 0, true);
    QVERIFY(s.isValid());

    constexpr int kSamples = 32;
    const QPolygonF outline = s.outline(kSamples);
    // Right half (kSamples+1 points) followed by the mirrored left half.
    QCOMPARE(outline.size(), 2 * (kSamples + 1));

    for (int i = 0; i <= kSamples; ++i) {
        const QPointF r = outline.at(i);
        const QPointF l = outline.at(outline.size() - 1 - i);
        QVERIFY(std::abs(r.x() + l.x()) < 1.0e-9);   // mirrored in x
        QVERIFY(std::abs(r.y() - l.y()) < 1.0e-9);   // at the same depth
    }
}

void TestXsectSampler::dummyHasNoGeometry()
{
    XsectSampler s = XsectSampler::fromShape(SWMM_XSECT_DUMMY, 1.0, 0, 0, 0, true);
    // DUMMY constructs successfully but every query returns 0, so there is
    // nothing to draw — the builders rely on the empty outline to show their
    // "no geometry" message rather than a zero-size blob.
    if (s.isValid())
        QVERIFY(s.outline().isEmpty());
}

void TestXsectSampler::shapeIdRoundTrips()
{
    for (const auto &row : openswmmvis::kXsectShapes) {
        if (row.engineId == SWMM_XSECT_IRREGULAR
            || row.engineId == SWMM_XSECT_CUSTOM
            || row.engineId == SWMM_XSECT_STREET)
            continue;

        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        openswmmvis::sectionview::nominalGeomsFor(row.engineId, g1, g2, g3, g4);
        XsectSampler s = XsectSampler::fromShape(row.engineId, g1, g2, g3, g4, true);
        if (!s.isValid()) continue;

        QVERIFY2(s.shape() == row.engineId,
                 qPrintable(QStringLiteral("%1: asked %2, engine reports %3")
                                .arg(row.name).arg(row.engineId).arg(s.shape())));
    }
}

void TestXsectSampler::moveTransfersOwnership()
{
    XsectSampler a = XsectSampler::fromShape(SWMM_XSECT_CIRCULAR, 1.0, 0, 0, 0, true);
    QVERIFY(a.isValid());

    XsectSampler b = std::move(a);
    QVERIFY(b.isValid());
    QVERIFY(!a.isValid());          // NOLINT(bugprone-use-after-move) — checked

    XsectSampler c;
    c = std::move(b);
    QVERIFY(c.isValid());
    QVERIFY(!b.isValid());          // NOLINT(bugprone-use-after-move) — checked
    // Destruction of all three must not double-free the handle; the test
    // passing under ASan is the assertion.
}

void TestXsectSampler::openChannelsReportOpen()
{
    XsectSampler open = XsectSampler::fromShape(SWMM_XSECT_RECT_OPEN,
                                                1.0, 1.0, 0, 0, true);
    QVERIFY(open.isValid());
    QVERIFY(open.fullProps().open);

    XsectSampler closed = XsectSampler::fromShape(SWMM_XSECT_CIRCULAR,
                                                  1.0, 0, 0, 0, true);
    QVERIFY(closed.isValid());
    QVERIFY(!closed.fullProps().open);
}

// ---------------------------------------------------------------------------
// Tabulated shapes
// ---------------------------------------------------------------------------

void TestXsectSampler::irregularFromTransectData()
{
    // Symmetric compound channel: overbanks at elevation 1.0, main channel
    // invert at 0.0 → 1.0 deep, 12.0 wide at the top of bank.
    const QVector<double> stations   { 0.0, 2.0, 4.0, 5.0,  7.0,  8.0, 10.0, 12.0 };
    const QVector<double> elevations { 1.0, 0.7, 0.55, 0.0, 0.0, 0.55, 0.7,  1.0 };

    XsectSampler s = XsectSampler::fromTransect(
        stations, elevations, 4.0, 8.0, 0.05, 0.03, 0.05, 1.0, true);
    QVERIFY2(s.isValid(), "engine rejected a well-formed transect");

    const XsectFullProps p = s.fullProps();
    QVERIFY(p.yFull > 0.0);
    QVERIFY(p.wMax  > 0.0);
    QVERIFY(p.open);                       // a natural channel is open at the top

    const QPolygonF outline = s.outline(128);
    QVERIFY(outline.size() >= 3);
    // The drawn extents must still agree with what the engine reports, exactly
    // as for the parametric shapes — that equivalence is the whole reason the
    // panels can treat tabulated and parametric sections identically.
    QVERIFY(std::abs(outline.boundingRect().height() - p.yFull) < 1.0e-6);
    QVERIFY(std::abs(outline.boundingRect().width()  - p.wMax)  < 1.0e-3);
}

void TestXsectSampler::irregularRejectsBadInput()
{
    // Fewer than two stations, and a zero channel roughness (which would make
    // the conveyance-weighted hydraulic-radius table meaningless), must both
    // fail rather than produce a bogus section.
    QVERIFY(!XsectSampler::fromTransect({ 0.0 }, { 1.0 },
                                        0.0, 0.0, 0.03, 0.03, 0.03, 1.0, true)
                 .isValid());
    QVERIFY(!XsectSampler::fromTransect({ 0.0, 1.0 }, { 1.0, 0.0 },
                                        0.0, 1.0, 0.03, /*nChannel=*/0.0, 0.03,
                                        1.0, true)
                 .isValid());
}

void TestXsectSampler::streetFromParameters()
{
    XsectSampler s = XsectSampler::fromStreet(
        /*width=*/6.0, /*curbHeight=*/0.15, /*slope=*/2.0, /*roughness=*/0.016,
        /*gutterDepression=*/0.05, /*gutterWidth=*/0.5, /*sides=*/2,
        /*backWidth=*/0.0, /*backSlope=*/0.0, /*backRoughness=*/0.016, true);
    QVERIFY2(s.isValid(), "engine rejected well-formed street parameters");

    const XsectFullProps p = s.fullProps();
    QVERIFY(p.yFull > 0.0);
    // A street is NOT an open section as far as the engine is concerned: its
    // isOpen() whitelist covers RECT_OPEN / TRAPEZOIDAL / TRIANGULAR /
    // PARABOLIC / POWER / IRREGULAR only, and STREET_XSECT falls to the
    // default. Verified live for exactly these parameters (is_open=false,
    // full_depth=0.2, full_area=1.22). The preview therefore strokes a street
    // as a closed outline — pinned here so a future engine change is noticed.
    QVERIFY(!p.open);
    QVERIFY(s.outline(128).size() >= 3);

    // A street with no width is not a street.
    QVERIFY(!XsectSampler::fromStreet(0.0, 0.15, 2.0, 0.016, 0.0, 0.0, 2,
                                      0.0, 0.0, 0.016, true).isValid());
    // sides must be 1 or 2.
    QVERIFY(!XsectSampler::fromStreet(6.0, 0.15, 2.0, 0.016, 0.0, 0.0, 3,
                                      0.0, 0.0, 0.016, true).isValid());
}

void TestXsectSampler::customFromShapeCurve()
{
    // Normalized SHAPE curve: y/yFull against w/wMax.
    const QVector<double> depths { 0.0, 0.25, 0.50, 0.75, 1.0 };
    const QVector<double> widths { 0.35, 0.85, 1.0, 0.85, 0.30 };

    XsectSampler s = XsectSampler::fromCurve(2.0, depths, widths, true);
    QVERIFY2(s.isValid(), "engine rejected a well-formed shape curve");
    QVERIFY(std::abs(s.fullProps().yFull - 2.0) < 1.0e-6);
    QVERIFY(s.outline(128).size() >= 3);

    QVERIFY(!XsectSampler::fromCurve(0.0, depths, widths, true).isValid());
    QVERIFY(!XsectSampler::fromCurve(2.0, { 0.0 }, { 1.0 }, true).isValid());
}

void TestXsectSampler::everyShapeGetsANonPlaceholderIcon_data()
{
    QTest::addColumn<int>("shape");
    for (const auto &row : openswmmvis::kXsectShapes)
        QTest::newRow(row.name) << row.engineId;
}

void TestXsectSampler::everyShapeGetsANonPlaceholderIcon()
{
    QFETCH(int, shape);

    // DUMMY has no geometry by definition and correctly renders the dashed
    // placeholder; every other surfaced shape — including the tabulated ones,
    // which use canned representative tables — must produce a real outline.
    if (shape == SWMM_XSECT_DUMMY)
        QSKIP("DUMMY has no geometry");

    const QSize sz(112, 84);
    const QPalette pal;

    const QImage rendered =
        openswmmvis::sectionview::xsectShapeIcon(shape, sz, pal).pixmap(sz).toImage();
    QVERIFY(!rendered.isNull());

    // DUMMY is the only shape that legitimately renders the dashed placeholder,
    // so its tile doubles as the reference: any other shape whose tile is
    // pixel-identical to it has silently fallen back, which is exactly the
    // regression this test exists to catch (a bare !isNull() cannot see it —
    // the placeholder isn't null either).
    const QImage placeholder =
        openswmmvis::sectionview::xsectShapeIcon(SWMM_XSECT_DUMMY, sz, pal)
            .pixmap(sz).toImage();
    QVERIFY2(rendered != placeholder,
             "shape fell back to the placeholder tile instead of drawing its section");
}

QTEST_MAIN(TestXsectSampler)
#include "test_xsectsampler.moc"
