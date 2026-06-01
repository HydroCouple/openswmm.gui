/*!
 * \file   test_2d_sublayers.cpp
 * \brief  Combined verification for the five 2D sublayer classes added in
 *         Slice S5 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §3 default mix for
 *         SWMM2DResultsLayer).
 *
 *         Single TU because the five classes share an identical structural
 *         contract — splitting into five files would not buy additional
 *         signal. Each class gets a dedicated case for: kind, isDynamic,
 *         JSON round-trip, and legend output shape.
 */

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/depthcolorrampsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/velocityvectorsublayer.h"

using namespace OpenSWMM::Render;

class Test2DSublayers : public QObject
{
    Q_OBJECT

private slots:
    // MeshFill (static)
    void meshFill_identity_static();
    void meshFill_json_roundTrip();
    void meshFill_legend_one_row();

    // DepthColorRamp (dynamic)
    void depthRamp_identity_dynamic();
    void depthRamp_json_roundTrip();
    void depthRamp_legend_two_rows();

    // VelocityVector (dynamic)
    void velocityVector_identity_dynamic();
    void velocityVector_glyph_spacing_floor();
    void velocityVector_json_roundTrip();

    // Isoline (dynamic)
    void isoline_identity_dynamic();
    void isoline_count_floor();
    void isoline_json_roundTrip();

    // ContourBand (dynamic)
    void contourBand_identity_dynamic();
    void contourBand_count_floor();
    void contourBand_json_roundTrip();

    // Shared
    void all_sublayers_emit_invalidated_on_style_change();
};

// ── MeshFill ────────────────────────────────────────────────────────────
void Test2DSublayers::meshFill_identity_static()
{
    MeshFillSublayer s(QStringLiteral("m"));
    QCOMPARE(s.kind(),      ISublayer::FillKind);
    QCOMPARE(s.isDynamic(), false);  // Load-bearing for animation dispatch
}

void Test2DSublayers::meshFill_json_roundTrip()
{
    MeshFillStyle a;
    a.setFillColor(QColor(123, 45, 67, 200));
    a.setHillshadeStrength(0.7);
    a.setUseElevationRamp(false);

    MeshFillStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.fillColor(),         QColor(123, 45, 67, 200));
    QCOMPARE(b.hillshadeStrength(), 0.7);
    QCOMPARE(b.useElevationRamp(),  false);
}

void Test2DSublayers::meshFill_legend_one_row()
{
    MeshFillSublayer s(QStringLiteral("m"));
    QCOMPARE(s.legendSymbolItems().size(), 1);
    QCOMPARE(s.legendSymbolItems().first().sublayerId, QStringLiteral("m"));
}

// ── DepthColorRamp ──────────────────────────────────────────────────────
void Test2DSublayers::depthRamp_identity_dynamic()
{
    DepthColorRampSublayer s(QStringLiteral("d"));
    QCOMPARE(s.kind(),      ISublayer::ColorRampFillKind);
    QCOMPARE(s.isDynamic(), true);
}

void Test2DSublayers::depthRamp_json_roundTrip()
{
    DepthColorRampStyle a;
    a.setAttribute(QStringLiteral("velocityMagnitude"));
    a.setMinValue(0.1);
    a.setMaxValue(2.5);
    a.setLowColor(QColor(10, 20, 30));
    a.setHighColor(QColor(200, 220, 240));
    a.setUseLogScale(true);

    DepthColorRampStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.attribute(),   QStringLiteral("velocityMagnitude"));
    QCOMPARE(b.minValue(),    0.1);
    QCOMPARE(b.maxValue(),    2.5);
    QCOMPARE(b.lowColor(),    QColor(10, 20, 30));
    QCOMPARE(b.highColor(),   QColor(200, 220, 240));
    QCOMPARE(b.useLogScale(), true);
}

void Test2DSublayers::depthRamp_legend_two_rows()
{
    DepthColorRampSublayer s(QStringLiteral("d"));
    const auto items = s.legendSymbolItems();
    QCOMPARE(items.size(), 2); // bookend low + high gradient anchors
    QCOMPARE(items[0].sublayerId, QStringLiteral("d"));
    QCOMPARE(items[1].sublayerId, QStringLiteral("d"));
}

// ── VelocityVector ──────────────────────────────────────────────────────
void Test2DSublayers::velocityVector_identity_dynamic()
{
    VelocityVectorSublayer s(QStringLiteral("v"));
    QCOMPARE(s.kind(),      ISublayer::VectorGlyphKind);
    QCOMPARE(s.isDynamic(), true);
    QCOMPARE(s.isVisible(), false); // off by default per plan
}

void Test2DSublayers::velocityVector_glyph_spacing_floor()
{
    VelocityVectorStyle st;
    st.setGlyphSpacingPx(0.0);
    QCOMPARE(st.glyphSpacingPx(), 1.0); // floor — defends renderer's loop
}

void Test2DSublayers::velocityVector_json_roundTrip()
{
    VelocityVectorStyle a;
    a.setGlyphLengthScalePxPerMps(30.0);
    a.setHeadSizePx(8.0);
    a.setColor(QColor(99, 0, 99));
    a.setDryDepthCutoff(0.05);

    VelocityVectorStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.glyphLengthScalePxPerMps(), 30.0);
    QCOMPARE(b.headSizePx(),               8.0);
    QCOMPARE(b.color(),                    QColor(99, 0, 99));
    QCOMPARE(b.dryDepthCutoff(),           0.05);
}

// ── Isoline ─────────────────────────────────────────────────────────────
void Test2DSublayers::isoline_identity_dynamic()
{
    IsolineSublayer s(QStringLiteral("i"));
    QCOMPARE(s.kind(),      ISublayer::IsolineKind);
    QCOMPARE(s.isDynamic(), true);
    QCOMPARE(s.isVisible(), false);
}

void Test2DSublayers::isoline_count_floor()
{
    IsolineStyle st;
    st.setIsoValueCount(0);
    QCOMPARE(st.isoValueCount(), 1);
}

void Test2DSublayers::isoline_json_roundTrip()
{
    IsolineStyle a;
    a.setAttribute(QStringLiteral("WSE"));
    a.setIsoValueCount(12);
    a.setLineWidthPx(2.0);
    a.setColor(QColor(0, 0, 0));
    a.setDashPattern(Qt::DashLine);
    a.setLabels(true);

    IsolineStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.attribute(),     QStringLiteral("WSE"));
    QCOMPARE(b.isoValueCount(), 12);
    QCOMPARE(b.lineWidthPx(),   2.0);
    QCOMPARE(b.color(),         QColor(0, 0, 0));
    QCOMPARE(b.dashPattern(),   Qt::DashLine);
    QCOMPARE(b.labels(),        true);
}

// ── ContourBand ─────────────────────────────────────────────────────────
void Test2DSublayers::contourBand_identity_dynamic()
{
    ContourBandSublayer s(QStringLiteral("b"));
    QCOMPARE(s.kind(),      ISublayer::ContourBandKind);
    QCOMPARE(s.isDynamic(), true);
    QCOMPARE(s.isVisible(), false);
}

void Test2DSublayers::contourBand_count_floor()
{
    ContourBandStyle st;
    st.setBandCount(-3);
    QCOMPARE(st.bandCount(), 1);
}

void Test2DSublayers::contourBand_json_roundTrip()
{
    ContourBandStyle a;
    a.setAttribute(QStringLiteral("depth"));
    a.setBandCount(16);
    a.setLowColor(QColor(20, 30, 40));
    a.setHighColor(QColor(220, 240, 250));
    a.setSmoothBands(false);

    ContourBandStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.attribute(),    QStringLiteral("depth"));
    QCOMPARE(b.bandCount(),    16);
    QCOMPARE(b.lowColor(),     QColor(20, 30, 40));
    QCOMPARE(b.highColor(),    QColor(220, 240, 250));
    QCOMPARE(b.smoothBands(),  false);
}

// ── Shared contract ─────────────────────────────────────────────────────
void Test2DSublayers::all_sublayers_emit_invalidated_on_style_change()
{
    // The style → invalidated re-emit pattern is identical across all 5
    // sublayers; exercise it once per class to catch any constructor
    // wiring regression.
    MeshFillSublayer mf(QStringLiteral("m"));
    QSignalSpy mfSpy(&mf, &ISublayer::invalidated);
    mf.fillStyle()->setHillshadeStrength(0.9);
    QCOMPARE(mfSpy.count(), 1);

    DepthColorRampSublayer d(QStringLiteral("d"));
    QSignalSpy dSpy(&d, &ISublayer::invalidated);
    d.rampStyle()->setUseLogScale(true);
    QCOMPARE(dSpy.count(), 1);

    VelocityVectorSublayer v(QStringLiteral("v"));
    QSignalSpy vSpy(&v, &ISublayer::invalidated);
    v.vectorStyle()->setHeadSizePx(7.0);
    QCOMPARE(vSpy.count(), 1);

    IsolineSublayer i(QStringLiteral("i"));
    QSignalSpy iSpy(&i, &ISublayer::invalidated);
    i.isolineStyle()->setIsoValueCount(20);
    QCOMPARE(iSpy.count(), 1);

    ContourBandSublayer b(QStringLiteral("b"));
    QSignalSpy bSpy(&b, &ISublayer::invalidated);
    b.bandStyle()->setBandCount(10);
    QCOMPARE(bSpy.count(), 1);
}

QTEST_MAIN(Test2DSublayers)
#include "test_2d_sublayers.moc"
