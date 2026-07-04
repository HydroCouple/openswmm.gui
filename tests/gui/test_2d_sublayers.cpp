/*!
 * \file   test_2d_sublayers.cpp
 * \brief  Combined verification for the 2D sublayer classes added in
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

#include <qpropertymodel.h>

#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/sublayers/velocityvectorsublayer.h"

using namespace OpenSWMM::Render;

namespace {
// QPropertyModel is a class-hierarchy tree: property rows are leaves under a
// class node, with the property name on column 0. Find the leaf row whose
// name column matches, searching recursively.
QModelIndex findPropertyRow(const QAbstractItemModel *m, const QModelIndex &parent,
                            const QString &name)
{
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        if (idx.data(Qt::DisplayRole).toString() == name) return idx;
        const QModelIndex hit = findPropertyRow(m, idx, name);
        if (hit.isValid()) return hit;
    }
    return {};
}
} // namespace

class Test2DSublayers : public QObject
{
    Q_OBJECT

private slots:
    // MeshFill (static)
    void meshFill_identity_static();
    void meshFill_json_roundTrip();
    void meshFill_legend_one_row();
    void meshFill_default_names_terrain_ramp();

    // MeshEdge (static)
    void meshEdge_identity_static();
    void meshEdge_default_scheme_legacy_two_class();
    void meshEdge_json_roundTrip();
    void meshEdge_legacy_json_seedsScheme();

    // D1-a — classification property surfaces as an editable grid leaf
    void meshEdge_classification_isEditableGridLeaf();

    // VelocityVector (dynamic)
    void velocityVector_identity_dynamic();
    void velocityVector_glyph_spacing_floor();
    void velocityVector_json_roundTrip();
    void velocityVector_legacy_json_seedsScheme();

    // Isoline (dynamic)
    void isoline_identity_dynamic();
    void isoline_count_floor();
    void isoline_json_roundTrip();

    // ContourBand (dynamic)
    void contourBand_identity_dynamic();
    void contourBand_count_floor();
    void contourBand_json_roundTrip();

    // Direct scalar fills (dynamic)
    void cellDepthFill_identity_dynamic();
    void smoothDepthFill_identity_dynamic();
    void scalarFill_json_roundTrip();

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

void Test2DSublayers::meshFill_default_names_terrain_ramp()
{
    // The default mesh-fill scheme names the "Terrain" builtin so the editor's
    // ramp combo and the renderer agree on the default (previously the scheme
    // had an empty ramp name while the renderer hard-coded the palette, so the
    // combo showed blank). It resolves to a real registered builtin.
    MeshFillStyle st;
    QCOMPARE(st.scheme().rampName(), QStringLiteral("Terrain"));
    QVERIFY(RasterColorRamp::builtinNames().contains(st.scheme().rampName()));
}

// ── MeshEdge ────────────────────────────────────────────────────────────
void Test2DSublayers::meshEdge_identity_static()
{
    MeshEdgeSublayer s(QStringLiteral("e"));
    QCOMPARE(s.kind(),      ISublayer::LineKind);
    QCOMPARE(s.isDynamic(), false);  // edges follow mesh topology, not the clock
}

void Test2DSublayers::meshEdge_default_scheme_legacy_two_class()
{
    // The default scheme reproduces the legacy thin/wide slope split: a 2-class
    // Manual scheme over [0,1] with the single break at slopeBreak. The renderer
    // keeps its dedicated two-tier path while the scheme stays at this default.
    MeshEdgeStyle st;
    const ClassificationScheme &s = st.scheme();
    QCOMPARE(s.mode(),       ClassificationScheme::ClassMode::Classified);
    QCOMPARE(s.classCount(), 2);
    QCOMPARE(s.method(),     BinMethod::Manual);
    QCOMPARE(s.manualBreaks().size(), 1);
    QCOMPARE(s.manualBreaks().first(), st.slopeBreak());
}

void Test2DSublayers::meshEdge_json_roundTrip()
{
    MeshEdgeStyle a;
    a.setColor(QColor(10, 20, 30, 180));
    a.setLineWidthPx(1.25);
    a.setDashPattern(Qt::DashDotLine);
    a.setUseSlopeDrivenWidth(false);
    a.setSlopeBreak(0.6);
    a.setWideWidthPx(2.5);
    a.setWideColor(QColor(200, 0, 0, 220));
    // Customize the scheme past its legacy seed so the "classification" key path
    // is exercised on round-trip (not just the legacy fallback).
    ClassificationScheme sc = a.scheme();
    sc.setMethod(BinMethod::Quantile);
    sc.setClassCount(4);
    a.setScheme(sc);

    MeshEdgeStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.color(),               QColor(10, 20, 30, 180));
    QCOMPARE(b.lineWidthPx(),         1.25);
    QCOMPARE(b.dashPattern(),         Qt::DashDotLine);
    QCOMPARE(b.useSlopeDrivenWidth(), false);
    QCOMPARE(b.slopeBreak(),          0.6);
    QCOMPARE(b.wideWidthPx(),         2.5);
    QCOMPARE(b.wideColor(),           QColor(200, 0, 0, 220));
    QVERIFY(b.scheme() == a.scheme());   // customized scheme survived round-trip
}

void Test2DSublayers::meshEdge_legacy_json_seedsScheme()
{
    // A pre-scheme file (no "classification" key) must reproduce the legacy
    // 2-class slope split via seedSchemeFromLegacy() on load.
    QJsonObject legacy;
    legacy.insert(QStringLiteral("color"),               QStringLiteral("#80112233"));
    legacy.insert(QStringLiteral("wideColor"),           QStringLiteral("#cc445566"));
    legacy.insert(QStringLiteral("slopeBreak"),          0.42);
    legacy.insert(QStringLiteral("useSlopeDrivenWidth"), true);

    MeshEdgeStyle st;
    st.fromJson(legacy);
    const ClassificationScheme &s = st.scheme();
    QCOMPARE(s.mode(),       ClassificationScheme::ClassMode::Classified);
    QCOMPARE(s.classCount(), 2);
    QCOMPARE(s.manualBreaks().size(), 1);
    QCOMPARE(s.manualBreaks().first(), 0.42);  // re-seeded from the legacy slopeBreak
}

void Test2DSublayers::meshEdge_classification_isEditableGridLeaf()
{
    // D1-a (the handoff's HIGHEST RISK): the auto-generated property grid must
    // surface the ClassificationScheme-typed Q_PROPERTY as an *editable leaf
    // row* whose value carries the ClassificationScheme metatype. If
    // QPropertyModel hid it or marked it read-only, the delegate's registered
    // ClassificationSchemeCellEditor creator (keyed on that metatype) could
    // never fire and the "Edit…" popup would be unreachable.
    MeshEdgeStyle style;
    QPropertyModel model(&style, nullptr);

    const QModelIndex nameIdx =
        findPropertyRow(&model, QModelIndex(), QStringLiteral("classification"));
    QVERIFY2(nameIdx.isValid(),
             "QPropertyModel did not surface a 'classification' property row");

    const QModelIndex valueIdx = model.index(nameIdx.row(), 1, nameIdx.parent());
    QVERIFY(valueIdx.isValid());
    QVERIFY2(valueIdx.flags().testFlag(Qt::ItemIsEditable),
             "classification value cell is not editable — the delegate's "
             "ClassificationScheme editor creator can never fire");
    QCOMPARE(valueIdx.data(Qt::EditRole).metaType().id(),
             qMetaTypeId<ClassificationScheme>());
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

void Test2DSublayers::velocityVector_legacy_json_seedsScheme()
{
    // Old files have no "classification" key — the loose legacy colour keys must
    // seed the embedded scheme through the forwarders so saved styles load.
    QJsonObject legacy;
    legacy.insert(QStringLiteral("colorByMagnitude"), true);
    legacy.insert(QStringLiteral("colorRampName"),    QStringLiteral("viridis"));
    legacy.insert(QStringLiteral("speedMinMps"),      0.1);
    legacy.insert(QStringLiteral("speedMaxMps"),      2.5);
    legacy.insert(QStringLiteral("colorClassCount"),  5);

    VelocityVectorStyle st;
    st.fromJson(legacy);
    QCOMPARE(st.colorByMagnitude(), true);
    QCOMPARE(st.colorRampName(),    QStringLiteral("viridis"));
    QCOMPARE(st.speedMinMps(),      0.1);
    QCOMPARE(st.speedMaxMps(),      2.5);
    QCOMPARE(st.colorClassCount(),  5);   // >= 2 → scheme switched to Classified
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

// ── Direct Scalar Fills ─────────────────────────────────────────────────
void Test2DSublayers::cellDepthFill_identity_dynamic()
{
    CellDepthFillSublayer s(QStringLiteral("cell"));
    QCOMPARE(s.kind(),      ISublayer::FillKind);
    QCOMPARE(s.isDynamic(), true);
    QCOMPARE(s.isVisible(), false);
    QCOMPARE(s.opacity(),   qreal(1.0));
    QVERIFY(s.fillStyle());
    QCOMPARE(s.displayName(), QStringLiteral("Cell Depth Fill"));
}

void Test2DSublayers::smoothDepthFill_identity_dynamic()
{
    SmoothDepthFillSublayer s(QStringLiteral("smooth"));
    QCOMPARE(s.kind(),      ISublayer::ColorRampFillKind);
    QCOMPARE(s.isDynamic(), true);
    QCOMPARE(s.isVisible(), false);
    QCOMPARE(s.opacity(),   qreal(0.85));
    QVERIFY(s.fillStyle());
    QCOMPARE(s.displayName(), QStringLiteral("Smooth Depth Fill"));
}

void Test2DSublayers::scalarFill_json_roundTrip()
{
    ScalarFillStyle a;
    a.setAttribute(QStringLiteral("velocity_magnitude"));
    a.setClassified(true);
    a.setBandCount(7);
    a.setColorRampName(QStringLiteral("magma"));
    a.setInvertRamp(true);
    a.setUseCustomRange(true);
    a.setRangeMin(0.2);
    a.setRangeMax(4.5);

    ScalarFillStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.attribute(),      QStringLiteral("velocity_magnitude"));
    QCOMPARE(b.classified(),     true);
    QCOMPARE(b.bandCount(),      7);
    QCOMPARE(b.colorRampName(),  QStringLiteral("magma"));
    QCOMPARE(b.invertRamp(),     true);
    QCOMPARE(b.useCustomRange(), true);
    QCOMPARE(b.rangeMin(),       0.2);
    QCOMPARE(b.rangeMax(),       4.5);
}

// ── Shared contract ─────────────────────────────────────────────────────
void Test2DSublayers::all_sublayers_emit_invalidated_on_style_change()
{
    // The style → invalidated re-emit pattern is identical across these
    // sublayers; exercise it once per class to catch any constructor
    // wiring regression.
    MeshFillSublayer mf(QStringLiteral("m"));
    QSignalSpy mfSpy(&mf, &ISublayer::invalidated);
    mf.fillStyle()->setHillshadeStrength(0.9);
    QCOMPARE(mfSpy.count(), 1);

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

    CellDepthFillSublayer c(QStringLiteral("c"));
    QSignalSpy cSpy(&c, &ISublayer::invalidated);
    c.fillStyle()->setBandCount(4);
    QCOMPARE(cSpy.count(), 1);

    SmoothDepthFillSublayer s(QStringLiteral("s"));
    QSignalSpy sSpy(&s, &ISublayer::invalidated);
    s.fillStyle()->setColorRampName(QStringLiteral("plasma"));
    QCOMPARE(sSpy.count(), 1);
}

QTEST_MAIN(Test2DSublayers)
#include "test_2d_sublayers.moc"
