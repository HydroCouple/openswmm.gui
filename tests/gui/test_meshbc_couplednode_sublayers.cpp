/*!
 * \file   test_meshbc_couplednode_sublayers.cpp
 * \brief  Contract tests for the two sublayers carved out of the mesh
 *         edge/vertex style bags (mesh styling restructure):
 *           - MeshBcSublayer / MeshBcStyle — BC indicator ring with
 *             per-type visibility, colours, widths; legacy MeshEdgeStyle
 *             JSON seeding (the migration core).
 *           - CoupledNodeSublayer / CoupledNodeStyle — SWMM-coupled vertex
 *             markers, formerly MeshNodeStyle's tagged* group.
 *         Plus the ClassificationScheme embedded-custom-ramp contract that
 *         fixes the "custom ramp applies grayscale" defect: the payload
 *         must survive selection, serialization, and resolution.
 */

#include <QJsonObject>
#include <QtTest/QtTest>

#include "render/classificationscheme.h"
#include "render/sublayers/couplednodesublayer.h"
#include "render/sublayers/meshbcsublayer.h"

using namespace OpenSWMM::Render;

class TestMeshBcCoupledNodeSublayers : public QObject
{
    Q_OBJECT

private slots:
    // ── MeshBcSublayer ──────────────────────────────────────────────────
    void meshBc_identity_static_hiddenByDefault();
    void meshBc_typeVisibility_defaults_and_folding();
    void meshBc_json_roundTrip();
    void meshBc_legacy_edgeJson_seeding();
    void meshBc_legend_gates_on_present_and_visible();

    // ── CoupledNodeSublayer ─────────────────────────────────────────────
    void coupled_identity_static_visibleByDefault();
    void coupled_json_roundTrip();

    // ── ClassificationScheme custom-ramp payload ────────────────────────
    void scheme_customRamp_resolves_not_grayscale();
    void scheme_customRamp_json_roundTrip();
    void scheme_setRampName_clears_custom_only_on_change();
};

void TestMeshBcCoupledNodeSublayers::meshBc_identity_static_hiddenByDefault()
{
    MeshBcSublayer sub(QStringLiteral("mesh.bc"));
    QCOMPARE(sub.id(), QStringLiteral("mesh.bc"));
    QCOMPARE(sub.kind(), ISublayer::LineKind);
    QVERIFY(!sub.isDynamic());
    // Hidden by default — mirrors the legacy colorByBC=false default so a
    // fresh mesh renders byte-identically until the user opts in.
    QVERIFY(!sub.isVisible());
    QVERIFY(sub.bcStyle() != nullptr);
    QCOMPARE(sub.style(), sub.bcStyle());
}

void TestMeshBcCoupledNodeSublayers::meshBc_typeVisibility_defaults_and_folding()
{
    MeshBcStyle st;
    for (int t = 0; t < MeshBcStyle::kBcTypeCount; ++t)
        QVERIFY(st.bcTypeVisible(t));

    st.setBcTypeVisible(3, false);
    QVERIFY(!st.bcTypeVisible(3));
    QVERIFY(st.bcTypeVisible(2));

    // Out-of-range reads fold onto Wall; out-of-range writes are ignored.
    QCOMPARE(st.bcColorForType(-1), st.wallColor());
    QCOMPARE(st.bcColorForType(99), st.wallColor());
    QVERIFY(st.bcTypeVisible(99));          // folds to Wall (still visible)
    st.setBcTypeVisible(99, false);          // ignored
    QVERIFY(st.bcTypeVisible(0));
    st.setBcColor(99, Qt::red);              // ignored
    st.setBcWidth(-1, 5.0);                  // ignored
}

void TestMeshBcCoupledNodeSublayers::meshBc_json_roundTrip()
{
    MeshBcStyle a;
    a.setStageConstColor(QColor(10, 20, 30, 40));
    a.setStageConstWidthPx(4.5);
    a.setFlowTSVisible(false);
    a.setWallColor(QColor(1, 2, 3, 4));
    a.setWallVisible(false);

    MeshBcStyle b;
    b.fromJson(a.toJson());

    QCOMPARE(b.stageConstColor(), QColor(10, 20, 30, 40));
    QCOMPARE(b.stageConstWidthPx(), 4.5);
    QVERIFY(!b.flowTSVisible());
    QVERIFY(b.stageTSVisible());
    QCOMPARE(b.wallColor(), QColor(1, 2, 3, 4));
    QVERIFY(!b.wallVisible());
    // Untouched defaults survive.
    QCOMPARE(b.normalFlowColor(), a.normalFlowColor());
    QCOMPARE(b.ratingCurveWidthPx(), a.ratingCurveWidthPx());
}

void TestMeshBcCoupledNodeSublayers::meshBc_legacy_edgeJson_seeding()
{
    // A legacy MeshEdgeStyle JSON: per-type colours under bc*-prefixed keys,
    // one pre-per-type shared "bcWidthPx", and one per-type width override.
    QJsonObject legacy;
    legacy[QStringLiteral("bcStageConstColor")] = QColor(11, 22, 33, 44).name(QColor::HexArgb);
    legacy[QStringLiteral("bcWallColor")]       = QColor(9, 9, 9, 90).name(QColor::HexArgb);
    legacy[QStringLiteral("bcWidthPx")]         = 3.3;
    legacy[QStringLiteral("bcFlowTSWidthPx")]   = 7.7;

    MeshBcStyle st;
    st.seedFromLegacyEdgeJson(legacy);

    QCOMPARE(st.stageConstColor(), QColor(11, 22, 33, 44));
    QCOMPARE(st.wallColor(), QColor(9, 9, 9, 90));
    // Shared legacy width seeds all six boundary types...
    QCOMPARE(st.normalFlowWidthPx(), 3.3);
    QCOMPARE(st.stageConstWidthPx(), 3.3);
    // ...then per-type keys override where present.
    QCOMPARE(st.flowTSWidthPx(), 7.7);
    // Legacy styles had no per-type visibility — everything visible.
    for (int t = 0; t < MeshBcStyle::kBcTypeCount; ++t)
        QVERIFY(st.bcTypeVisible(t));
}

void TestMeshBcCoupledNodeSublayers::meshBc_legend_gates_on_present_and_visible()
{
    MeshBcSublayer sub(QStringLiteral("mesh.bc"));

    // Wall is always present; add stage-const (2) and flow-TS (5).
    sub.setBcTypesPresent({2, 5});
    QCOMPARE(sub.legendSymbolItems().size(), 3);   // Wall + 2 + 5

    // Hiding a present type removes its row.
    sub.bcStyle()->setFlowTSVisible(false);
    QCOMPARE(sub.legendSymbolItems().size(), 2);

    // Absent-but-visible types contribute nothing.
    sub.bcStyle()->setRatingCurveVisible(true);
    QCOMPARE(sub.legendSymbolItems().size(), 2);

    // Every row routes back to the sublayer.
    const auto rows = sub.legendSymbolItems();
    for (const auto &row : rows)
        QCOMPARE(row.sublayerId, QStringLiteral("mesh.bc"));
}

void TestMeshBcCoupledNodeSublayers::coupled_identity_static_visibleByDefault()
{
    CoupledNodeSublayer sub(QStringLiteral("mesh.coupledNodes"));
    QCOMPARE(sub.id(), QStringLiteral("mesh.coupledNodes"));
    QCOMPARE(sub.kind(), ISublayer::MarkerKind);
    QVERIFY(!sub.isDynamic());
    // Visible by default — mirrors the legacy highlightTagged=true default.
    QVERIFY(sub.isVisible());
    QCOMPARE(sub.legendSymbolItems().size(), 1);
    QCOMPARE(sub.legendSymbolItems().first().sublayerId,
             QStringLiteral("mesh.coupledNodes"));
}

void TestMeshBcCoupledNodeSublayers::coupled_json_roundTrip()
{
    CoupledNodeStyle a;
    a.setColor(QColor(5, 6, 7, 8));
    a.setMarkerSizePx(9.5);

    CoupledNodeStyle b;
    b.fromJson(a.toJson());
    QCOMPARE(b.color(), QColor(5, 6, 7, 8));
    QCOMPARE(b.markerSizePx(), 9.5);

    // Defaults mirror MeshNodeStyle's historic tagged look.
    CoupledNodeStyle c;
    QCOMPARE(c.color(), QColor(0xff, 0x8c, 0x00, 235));
    QCOMPARE(c.markerSizePx(), 5.0);
}

void TestMeshBcCoupledNodeSublayers::scheme_customRamp_resolves_not_grayscale()
{
    RasterColorRamp custom;
    custom.stops = { {0.0, QColor(255, 0, 0)}, {1.0, QColor(0, 255, 0)} };
    custom.interp = RampInterp::HsvShort;

    ClassificationScheme s;
    s.setCustomRamp(custom, QStringLiteral("My Ramp"));

    QVERIFY(s.hasCustomRamp());
    QCOMPARE(s.rampName(), QStringLiteral("My Ramp"));
    // The historic defect: "My Ramp" fell through builtin() to grayscale.
    const RasterColorRamp r = s.resolvedRamp();
    QCOMPARE(r.stops, custom.stops);
    QCOMPARE(int(r.interp), int(RampInterp::HsvShort));
    QCOMPARE(s.colorAtF(0.0), QColor(255, 0, 0));
}

void TestMeshBcCoupledNodeSublayers::scheme_customRamp_json_roundTrip()
{
    RasterColorRamp custom;
    custom.stops = { {0.0, QColor(255, 0, 0)},
                     {0.4, QColor(0, 0, 255, 128)},
                     {1.0, QColor(0, 255, 0)} };

    ClassificationScheme a;
    a.setCustomRamp(custom, QStringLiteral("Portable"));

    const ClassificationScheme b = ClassificationScheme::fromJson(a.toJson());
    QVERIFY(b.hasCustomRamp());
    QCOMPARE(b.rampName(), QStringLiteral("Portable"));
    QCOMPARE(b.resolvedRamp().stops, custom.stops);
    QVERIFY(a == b);
}

void TestMeshBcCoupledNodeSublayers::scheme_setRampName_clears_custom_only_on_change()
{
    RasterColorRamp custom;
    custom.stops = { {0.0, QColor(1, 1, 1)}, {1.0, QColor(2, 2, 2)} };

    ClassificationScheme s;
    s.setCustomRamp(custom, QStringLiteral("Mine"));

    // Redundant same-name writes (e.g. a refresh echo) keep the payload.
    s.setRampName(QStringLiteral("Mine"));
    QVERIFY(s.hasCustomRamp());

    // Picking another ramp drops the stale payload and resolves builtin.
    s.setRampName(QStringLiteral("viridis"));
    QVERIFY(!s.hasCustomRamp());
    QVERIFY(s.resolvedRamp().stops != custom.stops);
}

QTEST_MAIN(TestMeshBcCoupledNodeSublayers)
#include "test_meshbc_couplednode_sublayers.moc"
