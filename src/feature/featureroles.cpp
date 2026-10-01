/*!
 * \file   featureroles.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Registry content: workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §4.
 */

#include "feature/featureroles.h"

#include "mesh/meshbctype.h"

#include <QCoreApplication>

using openswmmvis::feature::ChoiceSource;
using openswmmvis::feature::FeatureRoleSpec;
using openswmmvis::feature::FieldChoice;
using openswmmvis::feature::FieldDef;
using openswmmvis::feature::FieldType;
using openswmmvis::feature::FieldUnit;
using openswmmvis::feature::GeometryType;
using openswmmvis::feature::ModelList;
using openswmmvis::feature::Schema;

namespace {

QString tr_(const char *s) { return QCoreApplication::translate("FeatureRoles", s); }

// ----- Field builders --------------------------------------------------------

FieldDef textField(const char *name, const char *desc, bool required)
{
    FieldDef f;
    f.name        = QString::fromLatin1(name);
    f.type        = FieldType::Text;
    f.description = tr_(desc);
    f.required    = required;
    return f;
}

FieldDef realField(const char *name, const char *desc, bool required,
                   double def, FieldUnit unit = FieldUnit::None)
{
    FieldDef f;
    f.name         = QString::fromLatin1(name);
    f.type         = FieldType::Real;
    f.description  = tr_(desc);
    f.required     = required;
    f.defaultValue = def;
    f.unit         = unit;
    return f;
}

FieldDef suggestedField(const char *name, const char *desc)
{
    FieldDef f = textField(name, desc, /*required=*/false);
    f.choiceSource = ChoiceSource::Suggested;
    return f;
}

FieldDef modelField(const char *name, const char *desc, ModelList list)
{
    FieldDef f = textField(name, desc, /*required=*/false);
    f.choiceSource = ChoiceSource::Model;
    f.modelList    = list;
    return f;
}

FieldDef fixedField(const char *name, const char *desc, bool required,
                    QVector<FieldChoice> choices, const QString &def = QString())
{
    FieldDef f = textField(name, desc, required);
    f.choiceSource = ChoiceSource::Fixed;
    f.choices      = std::move(choices);
    if (!def.isEmpty()) f.defaultValue = def;
    return f;
}

QVector<FieldChoice> bcTypeChoices()
{
    using T = mesh::MeshBCTypes::Type;
    QVector<FieldChoice> out;
    for (T t : {T::Wall, T::NormalFlow, T::SpecifiedStageConst, T::SpecifiedStageTS,
                T::SpecifiedFlowConst, T::SpecifiedFlowTS, T::RatingCurve})
        out.append({mesh::MeshBCTypes::inpToken(t), mesh::MeshBCTypes::label(t)});
    return out;
}

const QVector<GeometryType> kPolygonKinds = {GeometryType::Polygon,
                                             GeometryType::MultiPolygon};
const QVector<GeometryType> kLineKinds    = {GeometryType::LineString,
                                             GeometryType::MultiLineString};
const QVector<GeometryType> kAllKinds     = {GeometryType::Point,
                                             GeometryType::LineString,
                                             GeometryType::Polygon,
                                             GeometryType::MultiPoint,
                                             GeometryType::MultiLineString,
                                             GeometryType::MultiPolygon};

QVector<FeatureRoleSpec> buildRegistry()
{
    QVector<FeatureRoleSpec> v;

    // §4.8 General — any geometry; the dialog adds custom columns.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::General;
        s.token = QStringLiteral("general");
        s.label = tr_("General");
        s.summary = tr_("Any features, with the columns you add.");
        s.geometries = kAllKinds;
        s.wantsZ = true;   // the Z section stays open: the user decides
        s.fields = {textField("name", "Label.", /*required=*/false)};
        v.append(s);
    }

    // §4.1 Mesh domain boundary.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::DomainBoundary;
        s.token = QStringLiteral("domain");
        s.label = tr_("Mesh domain boundary");
        s.summary = tr_("The area to mesh. Interior rings become holes.");
        s.geometries = kPolygonKinds;
        s.defaultGeometry = GeometryType::MultiPolygon;
        s.fields = {textField("name", "Label for this domain part.", /*required=*/true)};
        v.append(s);
    }

    // §4.2 Breaklines / hard points — the mesher reads geometry and Z only.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::Breakline;
        s.token = QStringLiteral("breakline");
        s.label = tr_("Breaklines / hard points");
        s.summary = tr_("Lines the mesh edges follow and points it must hit, "
                        "with their elevation (Z).");
        s.geometries = {GeometryType::LineString, GeometryType::MultiLineString,
                        GeometryType::Point, GeometryType::MultiPoint};
        s.defaultGeometry = GeometryType::MultiLineString;
        s.wantsZ = true;
        s.fields = {textField("name", "Label.", /*required=*/false)};
        s.retiredFields = {QStringLiteral("tag"), QStringLiteral("marker")};
        v.append(s);
    }

    // §4.3 Mesh regions — triangle engine D10–D12.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::Region;
        s.token = QStringLiteral("region");
        s.label = tr_("Mesh regions");
        s.summary = tr_("Areas with their own cell size; four-sided ones become "
                        "aligned quads.");
        s.geometries = kPolygonKinds;
        s.defaultGeometry = GeometryType::Polygon;
        s.fields = {
            realField("h", "Cell size inside the region, and the quad spacing "
                           "when it is four-sided. 0 = the model's cell size.",
                      /*required=*/true, 0.0, FieldUnit::Length),
            fixedField("cells", "Quads if four-sided: a four-sided region is "
                                "filled with quads aligned to its sides; any "
                                "other shape keeps triangles. Triangles: always "
                                "triangles.",
                       /*required=*/true,
                       {{QStringLiteral("auto"), tr_("Quads if four-sided")},
                        {QStringLiteral("triangles"), tr_("Triangles")}},
                       QStringLiteral("auto")),
            suggestedField("tag", "Tag written on every cell inside the region."),
        };
        s.retiredFields = {QStringLiteral("max_area"), QStringLiteral("min_cell"),
                           QStringLiteral("quad_mode"), QStringLiteral("quad_aspect"),
                           QStringLiteral("quad_angle")};
        s.legacyAliases.insert(QStringLiteral("quad_spacing"), QStringLiteral("h"));
        v.append(s);
    }

    // §4.4 Corridors (new) — lines with a width.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::Corridor;
        s.token = QStringLiteral("corridor");
        s.label = tr_("Corridors");
        s.summary = tr_("Road or river centrelines with a width; each becomes a "
                        "strip of quads (Mesh › Corridor sources).");
        s.geometries = kLineKinds;
        s.defaultGeometry = GeometryType::LineString;
        s.fields = {
            realField("width", "Total width across the line.",
                      /*required=*/true, 10.0, FieldUnit::Length),
            suggestedField("tag", "Optional label for the corridor."),
        };
        v.append(s);
    }

    // §4.5 Parameter zones.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::ParameterZone;
        s.token = QStringLiteral("zone");
        s.label = tr_("Parameter zones");
        s.summary = tr_("Areas that set cell roughness, initial depth, land use "
                        "and soil group (Mesh › Assign Cell Attributes).");
        s.geometries = kPolygonKinds;
        s.defaultGeometry = GeometryType::Polygon;
        s.fields = {
            realField("mannings_n", "Manning's n for cells in this zone.",
                      /*required=*/false, 0.035),
            realField("init_depth", "Initial depth for cells in this zone.",
                      /*required=*/false, 0.0, FieldUnit::Length),
            suggestedField("landuse", "Land-use class (the lookup table's first key)."),
            fixedField("hsg", "Hydrologic soil group (the lookup table's second key).",
                       /*required=*/false,
                       {{QStringLiteral("A"), QString()}, {QStringLiteral("B"), QString()},
                        {QStringLiteral("C"), QString()}, {QStringLiteral("D"), QString()},
                        {QStringLiteral("A/D"), QString()}, {QStringLiteral("B/D"), QString()},
                        {QStringLiteral("C/D"), QString()}}),
        };
        s.retiredFields = {QStringLiteral("infil_method")};
        v.append(s);
    }

    // §4.6 SWMM delineation — unchanged.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::SwmmDelineation;
        s.token = QStringLiteral("swmm");
        s.label = tr_("SWMM delineation");
        s.summary = tr_("Features to turn into SWMM objects with Import Feature Layer.");
        s.geometries = kAllKinds;
        s.fields = {
            textField("name",   "SWMM object name.", /*required=*/true),
            textField("outlet", "Outlet node or subcatchment.", /*required=*/false),
        };
        v.append(s);
    }

    // §4.7 Boundary-condition lines — mirrors mesh::MeshEdgeBC field for field.
    {
        FeatureRoleSpec s;
        s.role = FeatureLayerRole::BoundaryCondition;
        s.token = QStringLiteral("bc");
        s.label = tr_("Boundary-condition lines");
        s.summary = tr_("Lines that carry 2D boundary conditions.");
        s.note = tr_("Nothing assigns boundary conditions from these lines yet; "
                     "assign them from Mesh Editing for now.");
        s.geometries = {GeometryType::LineString};
        s.defaultGeometry = GeometryType::LineString;
        s.fields = {
            fixedField("bc_type", "Boundary-condition type.", /*required=*/true,
                       bcTypeChoices(),
                       mesh::MeshBCTypes::inpToken(mesh::MeshBCTypes::Type::Wall)),
            realField("head", "Water-surface elevation (Specified Stage).",
                      /*required=*/false, 0.0, FieldUnit::Length),
            realField("slope", "Bed slope (Normal Flow).", /*required=*/false, 0.001),
            realField("flow", "Discharge per unit length (Specified Flow).",
                      /*required=*/false, 0.0),
            modelField("tseries", "Time series (Stage or Flow time series).",
                       ModelList::TimeSeries),
            modelField("curve", "Rating curve (Rating Curve).", ModelList::Curves),
            suggestedField("group", "Boundary group."),
            realField("conveyance", "Flux multiplier in [0, 1]; 1 = unrestricted.",
                      /*required=*/false, 1.0),
        };
        v.append(s);
    }

    return v;
}

}   // namespace

// ---------------------------------------------------------------------------
// Global role helpers (declared in featureroles.h; featurelayer.h includes it)
// ---------------------------------------------------------------------------

QString featureLayerRoleLabel(FeatureLayerRole r)
{
    return openswmmvis::feature::featureRoleSpec(r).label;
}

QString featureLayerRoleToken(FeatureLayerRole r)
{
    return openswmmvis::feature::featureRoleSpec(r).token;
}

FeatureLayerRole featureLayerRoleFromToken(const QString &token)
{
    const QString t = token.trimmed().toLower();
    for (const FeatureRoleSpec &s : openswmmvis::feature::featureRoleSpecs())
        if (s.token == t) return s.role;
    return FeatureLayerRole::General;
}

Schema featureLayerRoleTemplate(FeatureLayerRole r)
{
    Schema s;
    for (const FieldDef &f : openswmmvis::feature::featureRoleSpec(r).fields)
        s.append(f);
    return s;
}

GeometryType featureLayerRoleGeometry(FeatureLayerRole r)
{
    return openswmmvis::feature::featureRoleSpec(r).defaultGeometry;
}

namespace openswmmvis::feature {

const FieldDef *FeatureRoleSpec::field(const QString &name) const
{
    for (const FieldDef &f : fields)
        if (f.name.compare(name, Qt::CaseInsensitive) == 0) return &f;
    return nullptr;
}

const QVector<FeatureRoleSpec> &featureRoleSpecs()
{
    static const QVector<FeatureRoleSpec> registry = buildRegistry();
    return registry;
}

const FeatureRoleSpec &featureRoleSpec(FeatureLayerRole r)
{
    const QVector<FeatureRoleSpec> &all = featureRoleSpecs();
    for (const FeatureRoleSpec &s : all)
        if (s.role == r) return s;
    return all.first();   // General
}

Schema editorSchema(FeatureLayerRole role, const Schema &stored)
{
    const FeatureRoleSpec &spec = featureRoleSpec(role);
    Schema out;
    for (FieldDef f : stored.fields()) {
        const FieldDef *reg = spec.field(f.name);
        if (!reg) {
            const auto alias = spec.legacyAliases.constFind(f.name.toLower());
            if (alias != spec.legacyAliases.constEnd()) reg = spec.field(alias.value());
        }
        // A registry hint applies only to a column of the same type: a Text
        // "h" a user made by hand is not the role's Real "h".
        if (reg && reg->type == f.type) {
            f.choiceSource = reg->choiceSource;
            f.modelList    = reg->modelList;
            f.unit         = reg->unit;
            f.required     = reg->required;
            // The registry's list when the table stores none, and also when
            // it stores the same set: GeoPackage returns a list sorted by
            // value, the registry has the display order and labels.
            if (f.choices.isEmpty() || sameChoiceSet(f.choices, reg->choices))
                f.choices = reg->choices;
            if (f.description.isEmpty()) f.description = reg->description;
        }
        out.append(f);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------

QStringList regionSizeFieldNames()
{
    return {QStringLiteral("h"), QStringLiteral("quad_spacing")};
}

QString regionCellsFieldName()   { return QStringLiteral("cells"); }
QString regionTagFieldName()     { return QStringLiteral("tag"); }
QString corridorWidthFieldName() { return QStringLiteral("width"); }
QString corridorTagFieldName()   { return QStringLiteral("tag"); }

RegionCells regionCellsFromValue(const QString &value, bool *known)
{
    const QString t = value.trimmed().toLower();
    if (known) *known = true;
    if (t.isEmpty() || t == regionCellsToken(RegionCells::Auto)) return RegionCells::Auto;
    if (t == regionCellsToken(RegionCells::Triangles))            return RegionCells::Triangles;
    if (known) *known = false;
    return RegionCells::Auto;
}

QString regionCellsToken(RegionCells c)
{
    return c == RegionCells::Triangles ? QStringLiteral("triangles")
                                       : QStringLiteral("auto");
}

bool bcFieldApplies(const QString &bcType, const QString &field)
{
    const QString f = field.toLower();
    static const QStringList kParameters = {
        QStringLiteral("head"), QStringLiteral("slope"), QStringLiteral("flow"),
        QStringLiteral("tseries"), QStringLiteral("curve")};
    if (!kParameters.contains(f))
        return true;   // bc_type, group, conveyance and every other field

    bool ok = false;
    using T = mesh::MeshBCTypes::Type;
    const T t = mesh::MeshBCTypes::fromInpToken(bcType, &ok);
    if (!ok) return true;   // an unknown type greys nothing out
    switch (t) {
    case T::Wall:                return false;
    case T::NormalFlow:          return f == QLatin1String("slope");
    case T::SpecifiedStageConst: return f == QLatin1String("head");
    case T::SpecifiedStageTS:    return f == QLatin1String("tseries");
    case T::SpecifiedFlowConst:  return f == QLatin1String("flow");
    case T::SpecifiedFlowTS:     return f == QLatin1String("tseries");
    case T::RatingCurve:         return f == QLatin1String("curve");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Update fields to role
// ---------------------------------------------------------------------------

bool isNonEmptyValue(const QVariant &v)
{
    if (!v.isValid() || v.isNull()) return false;
    if (v.userType() == QMetaType::QString) return !v.toString().trimmed().isEmpty();
    return true;
}

namespace {

/*! The stored metadata the update action can attach — defaults, descriptions
 *  and Fixed choices — compared the way the store persists them. */
bool sameStoredMetadata(const FieldDef &stored, const FieldDef &reg)
{
    const QVariant regDefault = reg.defaultValue.isValid()
                                    ? coerceToFieldType(reg.defaultValue, reg.type)
                                    : QVariant();
    const QVariant storedDefault = stored.defaultValue.isValid()
                                       ? coerceToFieldType(stored.defaultValue, stored.type)
                                       : QVariant();
    const QVector<FieldChoice> regChoices =
        reg.choiceSource == ChoiceSource::Fixed ? reg.choices : QVector<FieldChoice>();
    const QVector<FieldChoice> storedChoices =
        stored.choiceSource == ChoiceSource::Fixed ? stored.choices : QVector<FieldChoice>();
    return storedDefault == regDefault
        && stored.description == reg.description
        && sameChoiceSet(storedChoices, regChoices);
}

}   // namespace

FieldUpdatePlan planFieldUpdate(FeatureLayerRole role, const Schema &stored,
                                const QHash<QString, int> &nonEmpty)
{
    const FeatureRoleSpec &spec = featureRoleSpec(role);
    FieldUpdatePlan plan;

    for (const FieldDef &reg : spec.fields) {
        if (const FieldDef *have = stored.field(reg.name)) {
            if (have->type == reg.type && !sameStoredMetadata(*have, reg)) {
                FieldDef attach = reg;
                attach.name = have->name;   // keep the stored spelling
                plan.attach.append(attach);
            }
            continue;
        }
        // Absent under its current name: a legacy alias is renamed instead
        // of adding an empty twin beside it.
        QString legacy;
        for (auto it = spec.legacyAliases.constBegin(); it != spec.legacyAliases.constEnd(); ++it)
            if (it.value() == reg.name && stored.contains(it.key())) { legacy = it.key(); break; }
        if (!legacy.isEmpty()) {
            const FieldDef *old = stored.field(legacy);
            plan.rename.append({old->name, reg.name});
            if (old->type == reg.type && !sameStoredMetadata(*old, reg))
                plan.attach.append(reg);   // under the new name, after the rename
            continue;
        }
        plan.add.append(reg);
    }

    for (const QString &name : spec.retiredFields) {
        const FieldDef *have = stored.field(name);
        if (!have) continue;
        int count = 0;
        for (auto it = nonEmpty.constBegin(); it != nonEmpty.constEnd(); ++it)
            if (it.key().compare(have->name, Qt::CaseInsensitive) == 0) { count = it.value(); break; }
        plan.retired.append({have->name, count});
    }
    return plan;
}

}   // namespace openswmmvis::feature
