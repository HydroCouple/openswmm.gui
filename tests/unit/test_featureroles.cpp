/*!
 * \file   test_featureroles.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The feature-layer role registry
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md R1, R5, R6).
 *
 * Pure data: no GDAL, no widgets. Pins the persisted role tokens (an .oswp
 * written before the Corridor role existed must read back unchanged), the
 * §4 field tables, the readers' aliases, and the "Update fields to role…"
 * plan for an old Region layer.
 */
#include <gtest/gtest.h>

#include "feature/featureroles.h"
#include "mesh/meshbctype.h"

#include <QSet>
#include <QString>
#include <QStringList>

using namespace openswmmvis::feature;

namespace {

FieldDef field(const char *name, FieldType t)
{
    FieldDef f;
    f.name = QString::fromLatin1(name);
    f.type = t;
    return f;
}

/*! The Region schema the retired quad engine's template created
 *  (featurelayer.cpp before this plan). */
Schema oldRegionSchema()
{
    Schema s;
    s.append(field("max_area", FieldType::Real));
    s.append(field("min_cell", FieldType::Real));
    s.append(field("quad_mode", FieldType::Text));
    s.append(field("quad_spacing", FieldType::Real));
    s.append(field("quad_aspect", FieldType::Real));
    s.append(field("quad_angle", FieldType::Real));
    s.append(field("tag", FieldType::Text));
    return s;
}

}   // namespace

TEST(FeatureRoles, EveryRoleHasFieldsAndGeometryKinds)
{
    const auto &specs = featureRoleSpecs();
    ASSERT_EQ(specs.size(), 8);
    for (const FeatureRoleSpec &s : specs) {
        SCOPED_TRACE(s.token.toStdString());
        EXPECT_FALSE(s.label.isEmpty());
        EXPECT_FALSE(s.summary.isEmpty());
        EXPECT_FALSE(s.fields.isEmpty());
        EXPECT_FALSE(s.geometries.isEmpty());
        if (s.defaultGeometry != GeometryType::None) {
            EXPECT_TRUE(s.allowsGeometry(s.defaultGeometry));
        }
        QSet<QString> names;
        for (const FieldDef &f : s.fields) {
            EXPECT_EQ(sanitizeFieldName(f.name), f.name) << "unsafe column name";
            EXPECT_FALSE(names.contains(f.name.toLower())) << "duplicate field";
            names.insert(f.name.toLower());
        }
    }
}

TEST(FeatureRoles, TokensAreUniqueAndStable)
{
    // These tokens are what .oswp files store; changing one strands layers.
    const QList<QPair<FeatureLayerRole, QString>> pinned = {
        {FeatureLayerRole::General,           QStringLiteral("general")},
        {FeatureLayerRole::DomainBoundary,    QStringLiteral("domain")},
        {FeatureLayerRole::Breakline,         QStringLiteral("breakline")},
        {FeatureLayerRole::Region,            QStringLiteral("region")},
        {FeatureLayerRole::ParameterZone,     QStringLiteral("zone")},
        {FeatureLayerRole::SwmmDelineation,   QStringLiteral("swmm")},
        {FeatureLayerRole::BoundaryCondition, QStringLiteral("bc")},
        {FeatureLayerRole::Corridor,          QStringLiteral("corridor")},
    };
    QSet<QString> seen;
    for (const auto &p : pinned) {
        EXPECT_EQ(featureLayerRoleToken(p.first), p.second);
        EXPECT_EQ(featureLayerRoleFromToken(p.second), p.first);
        EXPECT_EQ(featureLayerRoleFromToken(p.second.toUpper()), p.first);
        seen.insert(p.second);
    }
    EXPECT_EQ(seen.size(), pinned.size());
    // Corridor is appended, so the integers of the old roles did not move.
    EXPECT_EQ(int(FeatureLayerRole::BoundaryCondition), 6);
    EXPECT_EQ(int(FeatureLayerRole::Corridor), 7);
    // Unknown tokens fall back to General rather than failing a project load.
    EXPECT_EQ(featureLayerRoleFromToken(QStringLiteral("no-such-role")),
              FeatureLayerRole::General);
}

TEST(FeatureRoles, FixedChoiceListsAreNonEmptyAndUnique)
{
    for (const FeatureRoleSpec &s : featureRoleSpecs()) {
        for (const FieldDef &f : s.fields) {
            if (f.choiceSource != ChoiceSource::Fixed) {
                EXPECT_TRUE(f.choices.isEmpty()) << f.name.toStdString();
                continue;
            }
            SCOPED_TRACE((s.token + QLatin1Char('.') + f.name).toStdString());
            EXPECT_EQ(f.type, FieldType::Text) << "choices store tokens";
            ASSERT_FALSE(f.choices.isEmpty());
            QSet<QString> values;
            for (const FieldChoice &c : f.choices) {
                EXPECT_FALSE(c.value.isEmpty());
                EXPECT_FALSE(values.contains(c.value));
                values.insert(c.value);
            }
            if (f.defaultValue.isValid()) {
                EXPECT_GE(f.choiceIndex(f.defaultValue.toString()), 0)
                    << "default must be one of the choices";
            }
        }
    }
}

TEST(FeatureRoles, RegionTemplateIsTheTriangleEngineOne)
{
    const Schema t = featureLayerRoleTemplate(FeatureLayerRole::Region);
    EXPECT_EQ(t.names(), (QStringList{QStringLiteral("h"), QStringLiteral("cells"),
                                      QStringLiteral("tag")}));
    const FieldDef *h = t.field(QStringLiteral("h"));
    ASSERT_TRUE(h);
    EXPECT_EQ(h->type, FieldType::Real);
    EXPECT_EQ(h->unit, FieldUnit::Length);
    EXPECT_TRUE(h->required);
    EXPECT_DOUBLE_EQ(h->defaultValue.toDouble(), 0.0);
    const FieldDef *cells = t.field(QStringLiteral("cells"));
    ASSERT_TRUE(cells);
    EXPECT_EQ(cells->choiceSource, ChoiceSource::Fixed);
    EXPECT_EQ(cells->defaultValue.toString(), QStringLiteral("auto"));
    ASSERT_EQ(cells->choices.size(), 2);
    EXPECT_EQ(cells->choices.at(1).value, QStringLiteral("triangles"));
    EXPECT_EQ(t.field(QStringLiteral("tag"))->choiceSource, ChoiceSource::Suggested);
    for (const char *dead : {"max_area", "min_cell", "quad_mode", "quad_aspect", "quad_angle"})
        EXPECT_FALSE(t.contains(QLatin1String(dead))) << dead;
}

TEST(FeatureRoles, BoundaryConditionTypesAreTheMeshTokens)
{
    const FieldDef *bc = featureRoleSpec(FeatureLayerRole::BoundaryCondition)
                             .field(QStringLiteral("bc_type"));
    ASSERT_TRUE(bc);
    ASSERT_EQ(bc->choices.size(), 7);
    for (const FieldChoice &c : bc->choices) {
        bool ok = false;
        const auto t = mesh::MeshBCTypes::fromInpToken(c.value, &ok);
        EXPECT_TRUE(ok) << c.value.toStdString();
        EXPECT_EQ(mesh::MeshBCTypes::inpToken(t), c.value);
        EXPECT_EQ(mesh::MeshBCTypes::label(t), c.label);
    }
    EXPECT_EQ(bc->defaultValue.toString(), QStringLiteral("WALL"));
    const FieldDef *conv = featureRoleSpec(FeatureLayerRole::BoundaryCondition)
                               .field(QStringLiteral("conveyance"));
    ASSERT_TRUE(conv);
    EXPECT_DOUBLE_EQ(conv->defaultValue.toDouble(), 1.0);
    const FieldDef *ts = featureRoleSpec(FeatureLayerRole::BoundaryCondition)
                             .field(QStringLiteral("tseries"));
    ASSERT_TRUE(ts);
    EXPECT_EQ(ts->choiceSource, ChoiceSource::Model);
    EXPECT_EQ(ts->modelList, ModelList::TimeSeries);
}

TEST(FeatureRoles, CorridorRoleIsLinesWithAWidth)
{
    const FeatureRoleSpec &s = featureRoleSpec(FeatureLayerRole::Corridor);
    EXPECT_TRUE(s.allowsGeometry(GeometryType::LineString));
    EXPECT_FALSE(s.allowsGeometry(GeometryType::Polygon));
    const FieldDef *w = s.field(corridorWidthFieldName());
    ASSERT_TRUE(w);
    EXPECT_EQ(w->type, FieldType::Real);
    EXPECT_DOUBLE_EQ(w->defaultValue.toDouble(), 10.0);
}

TEST(FeatureRoles, DeadFieldsAreGone)
{
    const Schema br = featureLayerRoleTemplate(FeatureLayerRole::Breakline);
    EXPECT_FALSE(br.contains(QStringLiteral("tag")));
    EXPECT_FALSE(br.contains(QStringLiteral("marker")));
    const Schema zone = featureLayerRoleTemplate(FeatureLayerRole::ParameterZone);
    EXPECT_FALSE(zone.contains(QStringLiteral("infil_method")));
    EXPECT_EQ(zone.field(QStringLiteral("hsg"))->choices.size(), 7);
}

TEST(FeatureRoles, ReadersAcceptTheLegacySizeName)
{
    EXPECT_EQ(regionSizeFieldNames(),
              (QStringList{QStringLiteral("h"), QStringLiteral("quad_spacing")}));
    bool known = false;
    EXPECT_EQ(regionCellsFromValue(QStringLiteral("triangles"), &known), RegionCells::Triangles);
    EXPECT_TRUE(known);
    EXPECT_EQ(regionCellsFromValue(QStringLiteral(" Triangles "), &known), RegionCells::Triangles);
    EXPECT_EQ(regionCellsFromValue(QString(), &known), RegionCells::Auto);
    EXPECT_TRUE(known);
    EXPECT_EQ(regionCellsFromValue(QStringLiteral("Mapped"), &known), RegionCells::Auto);
    EXPECT_FALSE(known) << "an unknown value is reported, then treated as auto";
}

TEST(FeatureRoles, EditorSchemaGivesOldLayersDropdowns)
{
    // A BC layer created before this plan: names and types only.
    Schema stored;
    stored.append(field("bc_type", FieldType::Text));
    stored.append(field("tseries", FieldType::Text));
    stored.append(field("notes", FieldType::Text));
    const Schema e = editorSchema(FeatureLayerRole::BoundaryCondition, stored);
    ASSERT_EQ(e.count(), 3);
    EXPECT_TRUE(e.field(QStringLiteral("bc_type"))->hasFixedChoices());
    EXPECT_EQ(e.field(QStringLiteral("tseries"))->choiceSource, ChoiceSource::Model);
    EXPECT_EQ(e.field(QStringLiteral("notes"))->choiceSource, ChoiceSource::None)
        << "a custom column is left alone";
    // The legacy alias gets the size field's unit.
    const Schema r = editorSchema(FeatureLayerRole::Region, oldRegionSchema());
    EXPECT_EQ(r.field(QStringLiteral("quad_spacing"))->unit, FieldUnit::Length);
    // A same-named column of another type is not the role's field.
    Schema odd;
    odd.append(field("h", FieldType::Text));
    EXPECT_EQ(editorSchema(FeatureLayerRole::Region, odd).field(QStringLiteral("h"))->unit,
              FieldUnit::None);
}

TEST(FeatureRoles, UpdatePlanForAnOldRegionLayer)
{
    QHash<QString, int> counts;
    counts.insert(QStringLiteral("max_area"), 3);
    counts.insert(QStringLiteral("quad_mode"), 2);
    const FieldUpdatePlan plan =
        planFieldUpdate(FeatureLayerRole::Region, oldRegionSchema(), counts);

    ASSERT_EQ(plan.rename.size(), 1);
    EXPECT_EQ(plan.rename.first().first, QStringLiteral("quad_spacing"));
    EXPECT_EQ(plan.rename.first().second, QStringLiteral("h"));

    ASSERT_EQ(plan.add.size(), 1);
    EXPECT_EQ(plan.add.first().name, QStringLiteral("cells"));

    ASSERT_EQ(plan.retired.size(), 5);
    QHash<QString, int> retired;
    for (const auto &r : plan.retired) retired.insert(r.first, r.second);
    EXPECT_EQ(retired.value(QStringLiteral("max_area")), 3);
    EXPECT_EQ(retired.value(QStringLiteral("quad_mode")), 2);
    EXPECT_EQ(retired.value(QStringLiteral("quad_angle")), 0);
    EXPECT_FALSE(retired.contains(QStringLiteral("quad_spacing")))
        << "renamed, not dropped";

    // tag exists but carries no stored description; h (after the rename)
    // gets its default and description attached.
    QStringList attached;
    for (const FieldDef &f : plan.attach) attached << f.name;
    EXPECT_TRUE(attached.contains(QStringLiteral("tag")));
    EXPECT_TRUE(attached.contains(QStringLiteral("h")));
    EXPECT_FALSE(plan.isEmpty());
}

TEST(FeatureRoles, UpdatePlanIsEmptyForACurrentLayer)
{
    const FieldUpdatePlan plan = planFieldUpdate(
        FeatureLayerRole::BoundaryCondition,
        featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition), {});
    EXPECT_TRUE(plan.isEmpty());
}

TEST(FeatureRoles, BcFieldsGreyOutByType)
{
    EXPECT_FALSE(bcFieldApplies(QStringLiteral("WALL"), QStringLiteral("head")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("SPECIFIED_STAGE"), QStringLiteral("head")));
    EXPECT_FALSE(bcFieldApplies(QStringLiteral("SPECIFIED_STAGE"), QStringLiteral("flow")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("TS_FLOW"), QStringLiteral("tseries")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("RATING_CURVE"), QStringLiteral("curve")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("WALL"), QStringLiteral("conveyance")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("WALL"), QStringLiteral("group")));
    EXPECT_TRUE(bcFieldApplies(QStringLiteral("garbage"), QStringLiteral("head")));
}

TEST(FeatureRoles, FieldDefJsonRoundTripsTheNewMembers)
{
    for (const FeatureRoleSpec &s : featureRoleSpecs())
        for (const FieldDef &f : s.fields)
            EXPECT_EQ(FieldDef::fromJson(f.toJson()), f)
                << (s.token + QLatin1Char('.') + f.name).toStdString();
}
