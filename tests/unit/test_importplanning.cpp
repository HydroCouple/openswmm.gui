/*!
 * \file   test_importplanning.cpp
 * \brief  FEATURE_LAYER_TO_SWMM_IMPORT — unit coverage for the pure
 *         planning core: target registry schema, mapping JSON
 *         round-trip, value coercion, and the endpoint-resolution /
 *         conflict-policy matrix of buildImportPlan().
 *
 * Everything under test is Qt-Core-only (no GDAL / engine / widgets) —
 * see importplanning.h. Synthetic SourceFeatures play the role of OGR
 * features already transformed into the model CRS.
 */
#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QPointF>
#include <QVariantMap>

#include "ui/dialogs/import/importmapping.h"
#include "ui/dialogs/import/importplan.h"
#include "ui/dialogs/import/importplanning.h"
#include "ui/dialogs/import/importtargetregistry.h"

using namespace openswmmvis::import;

namespace {

SourceFeature pointFeat(long long fid, const QString &name,
                        double x, double y,
                        QVariantMap extra = {})
{
    SourceFeature f;
    f.fid = fid;
    f.points = { QPointF(x, y) };
    f.attrs = std::move(extra);
    f.attrs.insert(QStringLiteral("ID"), name);
    return f;
}

SourceFeature lineFeat(long long fid, const QString &name,
                       const QVector<QPointF> &pts,
                       QVariantMap extra = {})
{
    SourceFeature f;
    f.fid = fid;
    f.points = pts;
    f.attrs = std::move(extra);
    f.attrs.insert(QStringLiteral("ID"), name);
    return f;
}

ImportMapping baseMapping(TargetKind kind)
{
    ImportMapping m;
    m.kind = kind;
    AttributeBinding name;
    name.targetKey = QStringLiteral("name");
    name.sourceField = QStringLiteral("ID");
    m.bindings.append(name);
    return m;
}

} // namespace

// ===========================================================================
// Registry
// ===========================================================================

TEST(ImportTargetRegistry, NameIsFirstAndRequiredForEveryKind)
{
    for (TargetKind k : ImportTargetRegistry::allKinds()) {
        const auto attrs = ImportTargetRegistry::attributesFor(k);
        ASSERT_FALSE(attrs.isEmpty());
        EXPECT_EQ(attrs.first().key.toStdString(), "name");
        EXPECT_TRUE(attrs.first().required);
        // Exactly one required attribute in v1 — the identifier.
        int requiredCount = 0;
        for (const auto &a : attrs)
            if (a.required) ++requiredCount;
        EXPECT_EQ(requiredCount, 1);
    }
}

TEST(ImportTargetRegistry, KindClassificationAndEngineCodes)
{
    EXPECT_TRUE(ImportTargetRegistry::isNodeKind(TargetKind::Junction));
    EXPECT_FALSE(ImportTargetRegistry::isNodeKind(TargetKind::RainGage));
    EXPECT_TRUE(ImportTargetRegistry::isPointKind(TargetKind::RainGage));
    EXPECT_TRUE(ImportTargetRegistry::isLinkKind(TargetKind::Outlet));

    EXPECT_EQ(ImportTargetRegistry::swmmNodeType(TargetKind::Divider), 3);
    EXPECT_EQ(ImportTargetRegistry::swmmNodeType(TargetKind::Conduit), -1);
    EXPECT_EQ(ImportTargetRegistry::swmmLinkType(TargetKind::Weir), 3);
    EXPECT_EQ(ImportTargetRegistry::swmmLinkType(TargetKind::Junction), -1);
}

TEST(ImportTargetRegistry, LinkKindsCarryEndpointRows)
{
    const auto attrs = ImportTargetRegistry::attributesFor(TargetKind::Conduit);
    bool from = false, to = false;
    for (const auto &a : attrs) {
        if (a.key == QLatin1String("fromNode")) from = true;
        if (a.key == QLatin1String("toNode"))   to   = true;
        // Endpoint keys are ctor-consumed → no adapter property.
        if (a.key == QLatin1String("fromNode")
            || a.key == QLatin1String("toNode"))
            EXPECT_TRUE(a.adapterProperty.isEmpty());
    }
    EXPECT_TRUE(from);
    EXPECT_TRUE(to);
}

// ===========================================================================
// Mapping JSON round-trip
// ===========================================================================

TEST(ImportMapping, JsonRoundTripsLosslessly)
{
    ImportMapping m = baseMapping(TargetKind::Conduit);
    m.ensureBinding(QStringLiteral("roughness")).sourceField
        = QStringLiteral("N_VALUE");
    m.ensureBinding(QStringLiteral("barrels")).defaultValue = 2;
    m.endpointsFromFields   = true;
    m.endpointsSnap         = false;
    m.snapToleranceMapUnits = 3.5;
    m.autoCreateJunctions   = true;
    m.autoNodePrefix        = QStringLiteral("N_");
    m.conflict              = ImportMapping::Conflict::Update;
    m.updateAttributes      = false;
    m.updateGeometry        = true;
    m.selectedFeaturesOnly  = true;

    const auto back = ImportMapping::fromJson(m.toJson());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->kind, TargetKind::Conduit);
    EXPECT_EQ(back->binding(QStringLiteral("name"))->sourceField,
              QStringLiteral("ID"));
    EXPECT_EQ(back->binding(QStringLiteral("roughness"))->sourceField,
              QStringLiteral("N_VALUE"));
    EXPECT_EQ(back->binding(QStringLiteral("barrels"))->defaultValue.toInt(), 2);
    EXPECT_TRUE(back->endpointsFromFields);
    EXPECT_FALSE(back->endpointsSnap);
    EXPECT_DOUBLE_EQ(back->snapToleranceMapUnits, 3.5);
    EXPECT_TRUE(back->autoCreateJunctions);
    EXPECT_EQ(back->autoNodePrefix, QStringLiteral("N_"));
    EXPECT_EQ(back->conflict, ImportMapping::Conflict::Update);
    EXPECT_FALSE(back->updateAttributes);
    EXPECT_TRUE(back->updateGeometry);
    EXPECT_TRUE(back->selectedFeaturesOnly);
}

TEST(ImportMapping, FromJsonRejectsGarbage)
{
    QString err;
    EXPECT_FALSE(ImportMapping::fromJson(QJsonObject(), &err).has_value());
    EXPECT_FALSE(err.isEmpty());
}

// ===========================================================================
// coerceValue
// ===========================================================================

TEST(CoerceValue, NumbersAndNumericStrings)
{
    const TargetAttribute d =
        ImportTargetRegistry::attribute(TargetKind::Junction,
                                        QStringLiteral("invertElev"));
    bool ok = false;
    EXPECT_DOUBLE_EQ(coerceValue(12.5, d, &ok).toDouble(), 12.5);
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(coerceValue(QStringLiteral("101.25"), d, &ok).toDouble(),
                     101.25);
    EXPECT_TRUE(ok);
    coerceValue(QStringLiteral("not-a-number"), d, &ok);
    EXPECT_FALSE(ok);
}

TEST(CoerceValue, EnumAcceptsCodeAndLabel)
{
    const TargetAttribute t =
        ImportTargetRegistry::attribute(TargetKind::Outfall,
                                        QStringLiteral("outfallType"));
    ASSERT_FALSE(t.enumChoices.isEmpty());
    bool ok = false;
    EXPECT_EQ(coerceValue(QStringLiteral("fixed"), t, &ok).toInt(), 2);
    EXPECT_TRUE(ok);
    EXPECT_EQ(coerceValue(4, t, &ok).toInt(), 4);        // TIMESERIES
    EXPECT_TRUE(ok);
    coerceValue(QStringLiteral("BOGUS"), t, &ok);
    EXPECT_FALSE(ok);
    coerceValue(99, t, &ok);                              // out-of-range code
    EXPECT_FALSE(ok);
}

TEST(CoerceValue, NullMeansNotProvided)
{
    const TargetAttribute d =
        ImportTargetRegistry::attribute(TargetKind::Junction,
                                        QStringLiteral("maxDepth"));
    bool ok = false;
    const QVariant v = coerceValue(QVariant(), d, &ok);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(v.isValid());
}

// ===========================================================================
// buildImportPlan — points
// ===========================================================================

TEST(BuildImportPlan, CreatesPointNodesWithMappedAttributes)
{
    ImportMapping m = baseMapping(TargetKind::Junction);
    m.ensureBinding(QStringLiteral("invertElev")).sourceField
        = QStringLiteral("INV");

    ModelSnapshot snap;
    const QVector<SourceFeature> feats = {
        pointFeat(1, QStringLiteral("J1"), 10.0, 20.0,
                  { {QStringLiteral("INV"), 100.5} }),
        pointFeat(2, QStringLiteral("J2"), 30.0, 40.0,
                  { {QStringLiteral("INV"), QStringLiteral("88.25")} }),
    };

    const ImportPlan plan = buildImportPlan(m, snap, feats);
    ASSERT_EQ(plan.items.size(), 2);
    EXPECT_EQ(plan.createCount, 2);
    EXPECT_EQ(plan.errorCount, 0);
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Create);
    EXPECT_DOUBLE_EQ(plan.items[0].x, 10.0);
    EXPECT_DOUBLE_EQ(
        plan.items[0].attributeValues.value(QStringLiteral("invertElev"))
            .toDouble(), 100.5);
    EXPECT_DOUBLE_EQ(
        plan.items[1].attributeValues.value(QStringLiteral("invertElev"))
            .toDouble(), 88.25);
}

TEST(BuildImportPlan, DuplicateAndEmptyIdsBecomeErrors)
{
    const ImportMapping m = baseMapping(TargetKind::Junction);
    ModelSnapshot snap;
    const ImportPlan plan = buildImportPlan(m, snap, {
        pointFeat(1, QStringLiteral("A"), 0, 0),
        pointFeat(2, QStringLiteral("A"), 1, 1),     // duplicate
        pointFeat(3, QString(), 2, 2),               // empty id
    });
    EXPECT_EQ(plan.createCount, 1);
    EXPECT_EQ(plan.errorCount, 2);
}

TEST(BuildImportPlan, ConflictSkipAndUpdateSplit)
{
    ImportMapping m = baseMapping(TargetKind::Junction);
    m.ensureBinding(QStringLiteral("maxDepth")).sourceField
        = QStringLiteral("DEPTH");

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("EXISTING"), QPointF(5, 5));
    snap.nodeTypes.insert(QStringLiteral("EXISTING"), 0);   // Junction

    const QVector<SourceFeature> feats = {
        pointFeat(1, QStringLiteral("EXISTING"), 5, 5,
                  { {QStringLiteral("DEPTH"), 3.0} }),
        pointFeat(2, QStringLiteral("NEW"), 9, 9,
                  { {QStringLiteral("DEPTH"), 4.0} }),
    };

    // Skip policy.
    m.conflict = ImportMapping::Conflict::Skip;
    ImportPlan plan = buildImportPlan(m, snap, feats);
    EXPECT_EQ(plan.skipCount, 1);
    EXPECT_EQ(plan.createCount, 1);

    // Update policy.
    m.conflict = ImportMapping::Conflict::Update;
    plan = buildImportPlan(m, snap, feats);
    EXPECT_EQ(plan.updateCount, 1);
    EXPECT_EQ(plan.createCount, 1);
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Update);
    EXPECT_TRUE(plan.items[0].attributeValues.contains(
        QStringLiteral("maxDepth")));
}

TEST(BuildImportPlan, TypeMismatchKeepsOnlyCommonAttributes)
{
    // Incoming Outfall matches an existing Junction.
    ImportMapping m = baseMapping(TargetKind::Outfall);
    m.conflict = ImportMapping::Conflict::Update;
    m.ensureBinding(QStringLiteral("invertElev")).sourceField
        = QStringLiteral("INV");
    m.ensureBinding(QStringLiteral("outfallType")).defaultValue
        = QStringLiteral("FREE");

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("N1"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("N1"), 0);   // Junction ≠ Outfall

    const ImportPlan plan = buildImportPlan(m, snap, {
        pointFeat(1, QStringLiteral("N1"), 0, 0,
                  { {QStringLiteral("INV"), 42.0} }),
    });
    ASSERT_EQ(plan.items.size(), 1);
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Update);
    EXPECT_TRUE(plan.items[0].attributeValues.contains(
        QStringLiteral("invertElev")));                       // common → kept
    EXPECT_FALSE(plan.items[0].attributeValues.contains(
        QStringLiteral("outfallType")));                      // specific → dropped
}

TEST(BuildImportPlan, UpdateGeometryDetection)
{
    ImportMapping m = baseMapping(TargetKind::Junction);
    m.conflict = ImportMapping::Conflict::Update;
    m.updateGeometry = true;
    m.updateAttributes = false;

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("N1"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("N1"), 0);
    snap.nodes.insert(QStringLiteral("N2"), QPointF(7, 7));
    snap.nodeTypes.insert(QStringLiteral("N2"), 0);

    const ImportPlan plan = buildImportPlan(m, snap, {
        pointFeat(1, QStringLiteral("N1"), 100, 200),   // moved
        pointFeat(2, QStringLiteral("N2"), 7, 7),       // unchanged
    });
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Update);
    EXPECT_TRUE(plan.items[0].geometryDiffers);
    // Nothing to do for the unchanged one → downgraded to Skip.
    EXPECT_EQ(plan.items[1].action, PlannedItem::Action::Skip);
}

// ===========================================================================
// buildImportPlan — links & endpoint resolution
// ===========================================================================

TEST(BuildImportPlan, EndpointFieldsStrategy)
{
    ImportMapping m = baseMapping(TargetKind::Conduit);
    m.endpointsFromFields = true;
    m.endpointsSnap = false;
    m.autoCreateJunctions = false;
    m.ensureBinding(QStringLiteral("fromNode")).sourceField
        = QStringLiteral("US");
    m.ensureBinding(QStringLiteral("toNode")).sourceField
        = QStringLiteral("DS");

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("A"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("A"), 0);
    snap.nodes.insert(QStringLiteral("B"), QPointF(10, 0));
    snap.nodeTypes.insert(QStringLiteral("B"), 0);

    const ImportPlan plan = buildImportPlan(m, snap, {
        lineFeat(1, QStringLiteral("C1"),
                 { QPointF(0, 0), QPointF(5, 1), QPointF(10, 0) },
                 { {QStringLiteral("US"), QStringLiteral("A")},
                   {QStringLiteral("DS"), QStringLiteral("B")} }),
        lineFeat(2, QStringLiteral("C2"),
                 { QPointF(0, 0), QPointF(10, 0) },
                 { {QStringLiteral("US"), QStringLiteral("MISSING")},
                   {QStringLiteral("DS"), QStringLiteral("B")} }),
    });
    ASSERT_EQ(plan.items.size(), 2);
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Create);
    EXPECT_EQ(plan.items[0].fromNode, QStringLiteral("A"));
    EXPECT_EQ(plan.items[0].toNode, QStringLiteral("B"));
    ASSERT_EQ(plan.items[0].interiorVertices.size(), 1);
    EXPECT_DOUBLE_EQ(plan.items[0].interiorVertices[0].x(), 5.0);
    // Unknown column node with no fallback strategy → error.
    EXPECT_EQ(plan.items[1].action, PlannedItem::Action::Error);
}

TEST(BuildImportPlan, EndpointSnapStrategyHonorsTolerance)
{
    ImportMapping m = baseMapping(TargetKind::Conduit);
    m.endpointsFromFields = false;
    m.endpointsSnap = true;
    m.snapToleranceMapUnits = 0.5;
    m.autoCreateJunctions = false;

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("A"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("A"), 0);
    snap.nodes.insert(QStringLiteral("B"), QPointF(10, 0));
    snap.nodeTypes.insert(QStringLiteral("B"), 0);

    const ImportPlan plan = buildImportPlan(m, snap, {
        // Endpoints within 0.5 of A and B → snaps.
        lineFeat(1, QStringLiteral("OK"),
                 { QPointF(0.2, 0.1), QPointF(10.1, -0.2) }),
        // Downstream end 2 units away → unresolved.
        lineFeat(2, QStringLiteral("FAR"),
                 { QPointF(0.0, 0.0), QPointF(12.0, 0.0) }),
    });
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Create);
    EXPECT_EQ(plan.items[0].fromNode, QStringLiteral("A"));
    EXPECT_EQ(plan.items[0].toNode, QStringLiteral("B"));
    EXPECT_EQ(plan.items[1].action, PlannedItem::Action::Error);
}

TEST(BuildImportPlan, EndpointAutoCreateStrategy)
{
    ImportMapping m = baseMapping(TargetKind::Conduit);
    m.endpointsFromFields = false;
    m.endpointsSnap = true;
    m.snapToleranceMapUnits = 0.5;
    m.autoCreateJunctions = true;
    m.autoNodePrefix = QStringLiteral("AJ_");

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("A"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("A"), 0);

    const ImportPlan plan = buildImportPlan(m, snap, {
        lineFeat(1, QStringLiteral("C1"),
                 { QPointF(0, 0), QPointF(50, 50) }),
        // Second feature's upstream end coincides with the first's
        // auto-created node → snaps to it instead of creating another.
        lineFeat(2, QStringLiteral("C2"),
                 { QPointF(50, 50), QPointF(80, 80) }),
    });
    ASSERT_EQ(plan.items.size(), 2);
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Create);
    ASSERT_EQ(plan.items[0].autoNodeNames.size(), 1);
    EXPECT_TRUE(plan.items[0].autoNodeNames[0]
                    .startsWith(QStringLiteral("AJ_")));
    EXPECT_EQ(plan.items[0].toNode, plan.items[0].autoNodeNames[0]);

    EXPECT_EQ(plan.items[1].action, PlannedItem::Action::Create);
    EXPECT_EQ(plan.items[1].fromNode, plan.items[0].autoNodeNames[0]);
    ASSERT_EQ(plan.items[1].autoNodeNames.size(), 1);   // only downstream new
}

TEST(BuildImportPlan, SelfLoopAndShortLinesAreErrors)
{
    ImportMapping m = baseMapping(TargetKind::Conduit);
    m.endpointsSnap = true;
    m.snapToleranceMapUnits = 5.0;

    ModelSnapshot snap;
    snap.nodes.insert(QStringLiteral("A"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("A"), 0);

    SourceFeature bad = lineFeat(2, QStringLiteral("SHORT"),
                                 { QPointF(1, 1) });   // 1 vertex

    const ImportPlan plan = buildImportPlan(m, snap, {
        // Both ends within tolerance of the SAME node → self loop.
        lineFeat(1, QStringLiteral("LOOP"),
                 { QPointF(0.5, 0.0), QPointF(0.0, 0.5) }),
        bad,
    });
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Error);
    EXPECT_EQ(plan.items[1].action, PlannedItem::Action::Error);
    EXPECT_EQ(plan.errorCount, 2);
}

TEST(BuildImportPlan, GageNamespaceIsIndependentOfNodes)
{
    ImportMapping m = baseMapping(TargetKind::RainGage);
    ModelSnapshot snap;
    // A node named G1 exists; a gage named G1 does NOT → gage import
    // of "G1" is a Create, not a conflict.
    snap.nodes.insert(QStringLiteral("G1"), QPointF(0, 0));
    snap.nodeTypes.insert(QStringLiteral("G1"), 0);

    const ImportPlan plan = buildImportPlan(m, snap, {
        pointFeat(1, QStringLiteral("G1"), 3, 3),
    });
    EXPECT_EQ(plan.items[0].action, PlannedItem::Action::Create);
}
