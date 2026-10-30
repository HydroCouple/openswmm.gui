/**
 * @file      dataobjectattributetablemodel.cpp
 * @brief     Tabular model over one category of non-spatial data objects — pollutants, land uses,
 *            aquifers, streets or inlets — so the Attribute Table dock can list and bulk-edit them
 *            the same way it lists SWMM objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/attributevalue.h"
#include "ui/panels/dataobjectattributetablemodel.h"

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "core/unitsystem.h"
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/dialogs/inletpropertybag.h"

#include <QBrush>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QUndoCommand>
#include <QUndoStack>

#include <openswmm/engine/openswmm_engine.h>

#include <limits>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
using openswmmvis::UnitKind;
using Field = DataObjectAttributeTableModel::Field;
using DC = SWMMModelLayer;

namespace
{

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;
using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletRegistry;
using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;
using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;
using openswmmvis::street::StreetProvider;
using openswmmvis::street::StreetRegistry;

constexpr double kInf = std::numeric_limits<double>::infinity();

/*! Header unit suffix for the active flow-units system. Mirrors the private
 *  helpers in swmmattributetablemodel.cpp / meshattributetablemodel.cpp; only
 *  the kinds this model uses are distinguished.
 * @param[in] kind Requested unit metadata; only Length/Velocity/Depression/Percent/Rate recognized.
 * @return Active global UnitSystem label, US fallback without singleton; empty for other kinds.
 *   Presentation only, no numeric conversion or source-project lookup.
 */
QString unitLabel(UnitKind kind)
{
    auto *us = UnitSystem::instance();
    const bool si = us && us->isSI();
    switch (kind) {
    case UnitKind::Length:
        return us ? us->lengthLabel() : QStringLiteral("ft");
    case UnitKind::Velocity:
        return us ? us->velocityLabel() : QStringLiteral("ft/s");
    case UnitKind::Depression:
        return si ? QStringLiteral("mm") : QStringLiteral("in");
    case UnitKind::Percent:
        return QStringLiteral("%");
    case UnitKind::Rate:
        return si ? QStringLiteral("mm/hr") : QStringLiteral("in/hr");
    default:
        return {};
    }
}

// ── Registry access ────────────────────────────────────────────────────────

/**
 * @param[in] layer Optional borrowed source; null yields null.
 * @param[in] cat Registry category, five supported only.
 * @return Borrowed project-owned ensured registry, or null unsupported/missing source. May lazily
 *   allocate/load staged providers, never transfers ownership. Caller must preserve source
 *   lifetime.
 */
QObject *registryFor(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat)
{
    if (!layer)
        return nullptr;
    switch (cat) {
    case DC::DataPollutants:
        return layer->ensurePollutantRegistry();
    case DC::DataLandUses:
        return layer->ensureLandUseRegistry();
    case DC::DataAquifers:
        return layer->ensureAquiferRegistry();
    case DC::DataStreets:
        return layer->ensureStreetRegistry();
    case DC::DataInlets:
        return layer->ensureInletRegistry();
    default:
        return nullptr;
    }
}

/**
 * @param[in] r Optional borrowed registry QObject, checked with qobject_cast to template Reg.
 * @return New list of borrowed provider pointers in registry order, empty on null/type mismatch. No
 *   ownership pin, copy of provider data or storage mutation.
 */
template <class Reg> QList<QObject *> providersOf(QObject *r)
{
    QList<QObject *> out;
    if (auto *reg = qobject_cast<Reg *>(r))
        for (auto *p : reg->providers())
            out << p;
    return out;
}

/**
 * @param[in] reg Optional borrowed registry QObject; checked by typed template cast.
 * @param[in] cat Category selects expected registry type; unsupported empty.
 * @return Borrowed provider list copy in source order, empty for mismatch/null/unsupported;
 *   providers remain registry-owned.
 */
QList<QObject *> providersOf(QObject *reg, SWMMModelLayer::DataCategory cat)
{
    switch (cat) {
    case DC::DataPollutants:
        return providersOf<PollutantRegistry>(reg);
    case DC::DataLandUses:
        return providersOf<LandUseRegistry>(reg);
    case DC::DataAquifers:
        return providersOf<AquiferRegistry>(reg);
    case DC::DataStreets:
        return providersOf<StreetRegistry>(reg);
    case DC::DataInlets:
        return providersOf<InletRegistry>(reg);
    default:
        return {};
    }
}

/**
 * @param[in] p Optional borrowed provider; if nonnull MUST have type matching cat, unchecked static
 *   casts.
 * @param[in] cat Provider category, unsupported yields empty.
 * @return Current exact stored name by value, empty for null/unsupported; no registry query or
 *   source mutation.
 */
QString nameOf(QObject *p, SWMMModelLayer::DataCategory cat)
{
    if (!p)
        return {};
    switch (cat) {
    case DC::DataPollutants:
        return static_cast<PollutantProvider *>(p)->name();
    case DC::DataLandUses:
        return static_cast<LandUseProvider *>(p)->name();
    case DC::DataAquifers:
        return static_cast<AquiferProvider *>(p)->name();
    case DC::DataStreets:
        return static_cast<StreetProvider *>(p)->name();
    case DC::DataInlets:
        return static_cast<InletProvider *>(p)->name();
    default:
        return {};
    }
}

/**
 * @param[in] reg Optional borrowed registry, checked through providersOf.
 * @param[in] cat Expected supported category/type.
 * @param[in] name Exact case-sensitive name; no trim/canonicalization.
 * @return Borrowed first current provider with matching name or null, no ownership pin or creation.
 */
QObject *findProvider(QObject *reg, SWMMModelLayer::DataCategory cat, const QString &name)
{
    for (QObject *p : providersOf(reg, cat))
        if (nameOf(p, cat) == name)
            return p;
    return nullptr;
}

/*! Same flush the editors run when they close
 *  (comprehensiveeditorregistry.cpp openXxxBrowse).
 * @param[in] reg Optional registry with concrete type matching cat; unchecked static casts after
 *   null guard.
 * @param[in] cat Supported registry category; others no-op.
 * @param[in] layer Optional source supplying engine handle; null no-op, null engine forwarded to
 *   registry handling.
 * @note Flush entire staged registry via its saveToEngine, not merely edited cell/provider. Ignore
 *   result/errors, no local simulation gate, rollback, dialog or objectEdited. Provider/engine
 *   lifecycle governed by registry/source contracts.
 */
void saveToEngine(QObject *reg, SWMMModelLayer::DataCategory cat, SWMMModelLayer *layer)
{
    if (!reg || !layer)
        return;
    void *eng = layer->engine();
    switch (cat) {
    case DC::DataPollutants:
        static_cast<PollutantRegistry *>(reg)->saveToEngine(eng);
        break;
    case DC::DataLandUses:
        static_cast<LandUseRegistry *>(reg)->saveToEngine(eng);
        break;
    case DC::DataAquifers:
        static_cast<AquiferRegistry *>(reg)->saveToEngine(eng);
        break;
    case DC::DataStreets:
        static_cast<StreetRegistry *>(reg)->saveToEngine(eng);
        break;
    case DC::DataInlets:
        static_cast<InletRegistry *>(reg)->saveToEngine(eng);
        break;
    default:
        break;
    }
}

// ── Column builders ────────────────────────────────────────────────────────

/**
 * @param[in] labels Ordered display labels.
 * @param[in] data Corresponding raw enum/reference data, may have different size.
 * @return New nested two-entry [label,data] list for common prefix min(sizes); unmatched tail
 *   ignored, no deduplication/conversion.
 */
QVariantList pairs(const QStringList &labels, const QVariantList &data)
{
    QVariantList out;
    for (int i = 0; i < labels.size() && i < data.size(); ++i)
        out << QVariant(QVariantList{labels[i], data[i]});
    return out;
}

/*! "(none)" + every object name of \p cat — the choices behind a
 *  name-reference column. Stored values are the names themselves.
 * @param[in] layer Optional borrowed source queried through current dataObjectCount/nameAt; null
 *   produces none entry only.
 * @param[in] cat Requested reference category, can be patterns/curves beyond five editable
 *   categories.
 * @return New enum pair list beginning translated (none)/empty QString, then exact current names in
 *   source order. Snapshot only, no sorting/deduplication/self-reference exclusion or future
 *   refresh binding.
 */
QVariantList nameChoices(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat)
{
    QStringList labels{QCoreApplication::translate("DataObjectAttributeTableModel", "(none)")};
    QVariantList data{QString()};
    if (layer) {
        const int n = layer->dataObjectCount(cat);
        for (int i = 0; i < n; ++i) {
            const QString name = layer->dataObjectNameAt(cat, i);
            labels << name;
            data << name;
        }
    }
    return pairs(labels, data);
}

/**
 * @return Read-only Name spec with empty getter/setter; fieldsFor installs category-specific getter
 *   later. No provider ownership or mutation.
 */
Field nameField()
{
    Field f;
    f.spec.key = QStringLiteral("Name");
    f.spec.label = QStringLiteral("Name");
    return f; // read-only: get is filled per category
}

template <class Prov, class G, class S>
/**
 * @param[in] key Exact field key and setter token copied into metadata.
 * @param[in] label Display heading copied unchanged.
 * @param[in] g Provider member getter, invoked on unchecked static_cast to template Prov.
 * @param[in] s Provider member setter, invoked with QVariant::toDouble without success/range check.
 * @param[in] minV Editor lower bound, default -infinity; not a write validator.
 * @param[in] maxV Editor upper bound, default +infinity; not a write validator.
 * @param[in] decimals Editor precision, default four; does not round stored getter/setter value.
 * @param[in] unit Presentation unit kind, default None; no conversion performed.
 * @return New editable Numeric Field with captured typed accessors; caller guarantees matching live
 *   provider type and units. No source/engine writes during construction.
 */
Field numField(const QString &key, const QString &label, G g, S s, double minV = -kInf,
               double maxV = kInf, int decimals = 4, UnitKind unit = UnitKind::None)
{
    Field f;
    f.spec.key = key;
    f.spec.label = label;
    f.spec.editor = EditorKind::Numeric;
    f.spec.setter = key;
    f.spec.minValue = minV;
    f.spec.maxValue = maxV;
    f.spec.decimals = decimals;
    f.spec.unit = unit;
    f.get = [g](QObject *o) { return QVariant((static_cast<Prov *>(o)->*g)()); };
    f.set = [s](QObject *o, const QVariant &v) { (static_cast<Prov *>(o)->*s)(v.toDouble()); };
    return f;
}

/**
 * @param[in] key Exact field key and setter token copied.
 * @param[in] label Display label copied.
 * @param[in] values Copied nested [display,data] enum choices snapshot; no
 *   validation/deduplication.
 * @param[in] get Moved optional getter callback, caller preserves captured context lifetime.
 * @param[in] set Moved optional setter callback, empty makes flags read-only.
 * @return New Enum Field; no choice membership/type/range enforcement in callback wrapper and no
 *   callback executed during construction.
 */
Field enumField(const QString &key, const QString &label, const QVariantList &values,
                std::function<QVariant(QObject *)> get,
                std::function<void(QObject *, const QVariant &)> set)
{
    Field f;
    f.spec.key = key;
    f.spec.label = label;
    f.spec.editor = EditorKind::Enum;
    f.spec.setter = key;
    f.spec.enumValues = values;
    f.get = std::move(get);
    f.set = std::move(set);
    return f;
}

/**
 * @param[in] s Required null-terminated translation source string, not retained.
 * @return QCoreApplication translation in DataObjectAttributeTableModel context, no model/source
 *   mutation.
 */
QString tr(const char *s)
{
    return QCoreApplication::translate("DataObjectAttributeTableModel", s);
}

/**
 * @param[in] layer Optional reference-name source; names snapshot includes same pollutant, no
 *   self-reference filtering.
 * @return Ten editable pollutant fields excluding Name: units MG/L, UG/L or #/L; concentration
 *   values in selected pollutant units, decay per day, molecular weight, snow flag, co-pollutant
 *   name/fraction. Numeric editor minima zero, no upper cap including coFraction. Accessors operate
 *   staged provider only; no engine write here.
 */
QList<Field> pollutantFields(SWMMModelLayer *layer)
{
    using P = PollutantProvider;
    QList<Field> f;
    f << enumField(
        QStringLiteral("units"), tr("Units"),
        pairs({QStringLiteral("MG/L"), QStringLiteral("UG/L"), QStringLiteral("#/L")}, {0, 1, 2}),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->units()); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setUnits(v.toInt()); });
    f << numField<P>(QStringLiteral("rainConc"), tr("Rain Concen."), &P::rainConc, &P::setRainConc,
                     0.0);
    f << numField<P>(QStringLiteral("gwConc"), tr("GW Concen."), &P::gwConc, &P::setGwConc, 0.0);
    f << numField<P>(QStringLiteral("rdiiConc"), tr("I&I Concen."), &P::rdiiConc, &P::setRdiiConc,
                     0.0);
    f << numField<P>(QStringLiteral("initConc"), tr("Init. Concen."), &P::initConc, &P::setInitConc,
                     0.0);
    f << numField<P>(QStringLiteral("kDecay"), tr("Decay Coeff. (1/days)"), &P::kDecay,
                     &P::setKDecay, 0.0);
    f << numField<P>(QStringLiteral("mwt"), tr("Molecular Weight"), &P::mwt, &P::setMwt, 0.0);
    f << enumField(
        QStringLiteral("snowOnly"), tr("Snow Only"), pairs({tr("NO"), tr("YES")}, {0, 1}),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->snowOnly() ? 1 : 0); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setSnowOnly(v.toInt() != 0); });
    f << enumField(
        QStringLiteral("coPollutant"), tr("Co-Pollutant"), nameChoices(layer, DC::DataPollutants),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->coPollutant()); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setCoPollutant(v.toString()); });
    f << numField<P>(QStringLiteral("coFraction"), tr("Co-Fraction"), &P::coFraction,
                     &P::setCoFraction, 0.0);
    return f;
}

/**
 * @return Sweeping interval in days (editor minimum zero) and available fraction (editor 0..1),
 *   excluding Name and buildup/washoff tables. Captured staged-provider accessors, no write/query
 *   at construction.
 */
QList<Field> landUseFields()
{
    using P = LandUseProvider;
    QList<Field> f;
    f << numField<P>(QStringLiteral("sweepInterval"), tr("Sweeping Interval (days)"),
                     &P::sweepInterval, &P::setSweepInterval, 0.0);
    f << numField<P>(QStringLiteral("sweepRemoval"), tr("Fraction Available"), &P::sweepRemoval,
                     &P::setSweepRemoval, 0.0, 1.0);
    return f;
}

/**
 * @param[in] layer Optional evaporation-pattern reference source, names copied now.
 * @return Twelve numeric aquifer parameters plus evaporation-pattern name, excluding Name.
 *   Moisture/porosity/fractions editor 0..1; conductivity/lower-loss rates nonnegative native
 *   rainfall-rate units, depths/elevations native length, slopes nonnegative, bottom/water-table
 *   elevations unbounded. Parameter accessors do not convert units or enforce cross-parameter
 *   constraints. No provider/engine write at construction.
 */
QList<Field> aquiferFields(SWMMModelLayer *layer)
{
    using P = AquiferProvider;
    struct Def {
        P::Param p;
        const char *key;
        const char *label;
        double minV;
        double maxV;
        UnitKind unit;
    };
    const Def defs[] = {
        {P::Porosity, "porosity", "Porosity", 0.0, 1.0, UnitKind::None},
        {P::WiltingPoint, "wiltingPoint", "Wilting Point", 0.0, 1.0, UnitKind::None},
        {P::FieldCapacity, "fieldCapacity", "Field Capacity", 0.0, 1.0, UnitKind::None},
        {P::Conductivity, "conductivity", "Conductivity", 0.0, kInf, UnitKind::Rate},
        {P::ConductSlope, "conductSlope", "Conductivity Slope", 0.0, kInf, UnitKind::None},
        {P::TensionSlope, "tensionSlope", "Tension Slope", 0.0, kInf, UnitKind::None},
        {P::UpperEvapFrac, "upperEvapFrac", "Upper Evap. Fraction", 0.0, 1.0, UnitKind::None},
        {P::LowerEvapDepth, "lowerEvapDepth", "Lower Evap. Depth", 0.0, kInf, UnitKind::Length},
        {P::LowerLossCoeff, "lowerLossCoeff", "Lower GW Loss Rate", 0.0, kInf, UnitKind::Rate},
        {P::BottomElev, "bottomElev", "Bottom Elevation", -kInf, kInf, UnitKind::Length},
        {P::WaterTableElev, "waterTableElev", "Water Table Elevation", -kInf, kInf,
         UnitKind::Length},
        {P::UpperMoisture, "upperMoisture", "Unsat. Zone Moisture", 0.0, 1.0, UnitKind::None},
    };
    QList<Field> f;
    for (const Def &d : defs) {
        Field c;
        c.spec.key = QString::fromLatin1(d.key);
        c.spec.label = tr(d.label);
        c.spec.editor = EditorKind::Numeric;
        c.spec.setter = c.spec.key;
        c.spec.minValue = d.minV;
        c.spec.maxValue = d.maxV;
        c.spec.unit = d.unit;
        const int k = d.p;
        c.get = [k](QObject *o) { return QVariant(static_cast<P *>(o)->param(k)); };
        c.set = [k](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setParam(k, v.toDouble());
        };
        f << c;
    }
    f << enumField(
        QStringLiteral("evapPattern"), tr("Upper Evap. Pattern"),
        nameChoices(layer, DC::DataPatterns),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->evapPattern()); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setEvapPattern(v.toString()); });
    return f;
}

/**
 * @return Ten street columns excluding Name, typed staged-provider accessors: native length
 *   widths/heights, depression in mm/in, slopes percent, roughness and one/two sides. Numeric
 *   editor minimum zero without local write enforcement; no unit conversion, provider read or
 *   engine write during construction.
 */
QList<Field> streetFields()
{
    using P = StreetProvider;
    QList<Field> f;
    f << numField<P>(QStringLiteral("crownWidth"), tr("Road Width"), &P::crownWidth,
                     &P::setCrownWidth, 0.0, kInf, 4, UnitKind::Length);
    f << numField<P>(QStringLiteral("curbHeight"), tr("Curb Height"), &P::curbHeight,
                     &P::setCurbHeight, 0.0, kInf, 4, UnitKind::Length);
    f << numField<P>(QStringLiteral("crossSlope"), tr("Road Cross Slope"), &P::crossSlope,
                     &P::setCrossSlope, 0.0, kInf, 4, UnitKind::Percent);
    f << numField<P>(QStringLiteral("roadRoughness"), tr("Road Roughness"), &P::roadRoughness,
                     &P::setRoadRoughness, 0.0);
    f << numField<P>(QStringLiteral("gutterDepression"), tr("Gutter Depression"),
                     &P::gutterDepression, &P::setGutterDepression, 0.0, kInf, 4,
                     UnitKind::Depression);
    f << numField<P>(QStringLiteral("gutterWidth"), tr("Gutter Width"), &P::gutterWidth,
                     &P::setGutterWidth, 0.0, kInf, 4, UnitKind::Length);
    f << enumField(
        QStringLiteral("sides"), tr("Street Sides"),
        pairs({QStringLiteral("1"), QStringLiteral("2")}, {1, 2}),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->sides()); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setSides(v.toInt()); });
    f << numField<P>(QStringLiteral("backingWidth"), tr("Backing Width"), &P::backingWidth,
                     &P::setBackingWidth, 0.0, kInf, 4, UnitKind::Length);
    f << numField<P>(QStringLiteral("backingSlope"), tr("Backing Slope"), &P::backingSlope,
                     &P::setBackingSlope, 0.0, kInf, 4, UnitKind::Percent);
    f << numField<P>(QStringLiteral("backingRoughness"), tr("Backing Roughness"),
                     &P::backingRoughness, &P::setBackingRoughness, 0.0);
    return f;
}

/**
 * @param[in] layer Optional capture-curve name source, snapshot of all curve names without type
 *   filtering.
 * @return Thirteen inlet fields excluding Name, type-dependent applicability matching
 *   InletPropertyBag: grate/curb/slot/custom groups, Generic grate fraction/velocity only, throat
 *   absent for DropCurb. Length/velocity metadata native project units, open fraction editor 0..1,
 *   other numeric minima zero. Callbacks unchecked provider casts, no source writes while building;
 *   static applyValue does not apply this visibility gate.
 */
QList<Field> inletFields(SWMMModelLayer *layer)
{
    using P = InletProvider;
    using Bag = openswmmvis::ui::InletPropertyBag;
    namespace in = openswmmvis::inlet;

    // Row applicability mirrors InletPropertyBag::isPropertyVisible: the
    // type's groups, plus GENERIC-only open area / splash velocity and no
    // throat angle on a DROP CURB.
    auto inGroup = [](Bag::Group g) {
        return [g](QObject *o) { return Bag::groupsFor(static_cast<P *>(o)->type()).testFlag(g); };
    };
    auto genericGrate = [](QObject *o) {
        auto *p = static_cast<P *>(o);
        return Bag::groupsFor(p->type()).testFlag(Bag::GrateGroup) &&
               p->grateType() == in::GrateType::Generic;
    };

    QStringList typeLabels;
    QVariantList typeData;
    for (int t = int(in::InletType::Grate); t <= int(in::InletType::Custom); ++t) {
        typeLabels << in::inletTypeLabel(in::InletType(t));
        typeData << t;
    }
    QStringList grateLabels;
    QVariantList grateData;
    for (int t = int(in::GrateType::PBar50); t <= int(in::GrateType::Generic); ++t) {
        grateLabels << in::grateTypeLabel(in::GrateType(t));
        grateData << t;
    }

    QList<Field> f;
    f << enumField(
        QStringLiteral("type"), tr("Inlet Type"), pairs(typeLabels, typeData),
        [](QObject *o) { return QVariant(int(static_cast<P *>(o)->type())); },
        [](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setType(in::InletType(v.toInt()));
        });

    Field c = numField<P>(QStringLiteral("grateLength"), tr("Grate Length"), &P::grateLength,
                          &P::setGrateLength, 0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::GrateGroup);
    f << c;
    c = numField<P>(QStringLiteral("grateWidth"), tr("Grate Width"), &P::grateWidth,
                    &P::setGrateWidth, 0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::GrateGroup);
    f << c;
    c = enumField(
        QStringLiteral("grateType"), tr("Grate Type"), pairs(grateLabels, grateData),
        [](QObject *o) { return QVariant(int(static_cast<P *>(o)->grateType())); },
        [](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setGrateType(in::GrateType(v.toInt()));
        });
    c.applies = inGroup(Bag::GrateGroup);
    f << c;
    c = numField<P>(QStringLiteral("openArea"), tr("Grate Open Fraction"), &P::openArea,
                    &P::setOpenArea, 0.0, 1.0);
    c.applies = genericGrate;
    f << c;
    c = numField<P>(QStringLiteral("splashVeloc"), tr("Splash Over Velocity"), &P::splashVeloc,
                    &P::setSplashVeloc, 0.0, kInf, 4, UnitKind::Velocity);
    c.applies = genericGrate;
    f << c;

    c = numField<P>(QStringLiteral("curbLength"), tr("Curb Opening Length"), &P::curbLength,
                    &P::setCurbLength, 0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::CurbGroup);
    f << c;
    c = numField<P>(QStringLiteral("curbHeight"), tr("Curb Opening Height"), &P::curbHeight,
                    &P::setCurbHeight, 0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::CurbGroup);
    f << c;
    c = enumField(
        QStringLiteral("throat"), tr("Throat Angle"),
        pairs(
            {QStringLiteral("HORIZONTAL"), QStringLiteral("INCLINED"), QStringLiteral("VERTICAL")},
            {0, 1, 2}),
        [](QObject *o) { return QVariant(int(static_cast<P *>(o)->throat())); },
        [](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setThroat(in::ThroatType(v.toInt()));
        });
    c.applies = [](QObject *o) {
        const auto t = static_cast<P *>(o)->type();
        return Bag::groupsFor(t).testFlag(Bag::CurbGroup) && t != in::InletType::DropCurb;
    };
    f << c;

    c = numField<P>(QStringLiteral("slotLength"), tr("Slot Length"), &P::slotLength,
                    &P::setSlotLength, 0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::SlottedGroup);
    f << c;
    c = numField<P>(QStringLiteral("slotWidth"), tr("Slot Width"), &P::slotWidth, &P::setSlotWidth,
                    0.0, kInf, 4, UnitKind::Length);
    c.applies = inGroup(Bag::SlottedGroup);
    f << c;

    c = enumField(
        QStringLiteral("curveId"), tr("Capture Curve"), nameChoices(layer, DC::DataCurves),
        [](QObject *o) { return QVariant(static_cast<P *>(o)->curveId()); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setCurveId(v.toString()); });
    c.applies = inGroup(Bag::CustomGroup);
    f << c;
    c = enumField(
        QStringLiteral("curveKind"), tr("Capture Curve Type"),
        pairs({tr("NONE"), tr("DIVERSION"), tr("RATING")}, {0, 1, 2}),
        [](QObject *o) { return QVariant(int(static_cast<P *>(o)->curveKind())); },
        [](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setCurveKind(in::InletCurveKind(v.toInt()));
        });
    c.applies = inGroup(Bag::CustomGroup);
    f << c;
    return f;
}

/*! Every column of \p cat, Name first. Empty for unsupported categories.
 * @param[in] layer Optional source for reference-choice snapshots, not captured/owned by resulting
 *   accessor lambdas.
 * @param[in] cat Editable category, unsupported returns empty.
 * @return New fields with read-only exact-name getter prepended to category-specific fields. Staged
 *   provider type must match category. No registry ensuring, callbacks executed, provider mutation
 *   or engine flush here.
 */
QList<Field> fieldsFor(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat)
{
    QList<Field> f;
    switch (cat) {
    case DC::DataPollutants:
        f = pollutantFields(layer);
        break;
    case DC::DataLandUses:
        f = landUseFields();
        break;
    case DC::DataAquifers:
        f = aquiferFields(layer);
        break;
    case DC::DataStreets:
        f = streetFields();
        break;
    case DC::DataInlets:
        f = inletFields(layer);
        break;
    default:
        return f;
    }
    Field name = nameField();
    name.get = [cat](QObject *o) { return QVariant(nameOf(o, cat)); };
    f.prepend(name);
    return f;
}

/*! Label of the enum pair whose data equals \p v, or \p v as text.
 * @param[in] spec Copied enum metadata; malformed pair sizes skipped.
 * @param[in] v Raw candidate compared through QVariant equality, not label text.
 * @return First matching pair label or v.toString if no match; no trimming/case folding/name
 *   resolution or model mutation.
 */
QString enumLabel(const ColumnSpec &spec, const QVariant &v)
{
    for (const QVariant &pv : spec.enumValues) {
        const QVariantList pair = pv.toList();
        if (pair.size() == 2 && pair[1] == v)
            return pair[0].toString();
    }
    return v.toString();
}

// One undo step per cell edit. Keyed by (layer, category, name, column key)
// rather than by model row/column so it replays correctly however the table
// has since been re-bound, sorted or filtered.
class DataObjectFieldEditCommand : public QUndoCommand
{
public:
    /**
     * @param[in] layer Optional borrowed source stored in QPointer, not model/view owner.
     * @param[in] cat Original provider category, preserved across table rebinding.
     * @param[in] name Moved exact provider name, replay resolves current provider with this name.
     * @param[in] key Moved exact mutable column key.
     * @param[in] oldVal Moved undo candidate, not validated during construction.
     * @param[in] newVal Moved redo candidate, not validated during construction.
     * @param[in] label Copied into translated command caption; not field lookup key.
     * @brief Capture one nonmergeable cell edit without executing or owning providers.
     * @note Caller/undo stack owns command. Renamed/deleted provider makes replay no-op unless
     *   another provider now uses same name. No obsolete/error state recorded.
     */
    DataObjectFieldEditCommand(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat,
                               QString name, QString key, QVariant oldVal, QVariant newVal,
                               const QString &label)
        : m_layer(layer), m_cat(cat), m_name(std::move(name)), m_key(std::move(key)),
          m_old(std::move(oldVal)), m_new(std::move(newVal))
    {
        setText(QObject::tr("Edit %1 of \"%2\"").arg(label, m_name));
    }
    /**
     * @brief Apply stored new value to original guarded source/category/name/key through static
     *   applyValue; ignore failure.
     * @note No current table required, no model objectEdited emission, flags/applicability/running
     *   gate or rollback. Registry signals may refresh bound tables.
     */
    void redo() override
    {
        DataObjectAttributeTableModel::applyValue(m_layer, m_cat, m_name, m_key, m_new);
    }
    /**
     * @brief Apply stored old value through original source/category/name/key, ignoring missing
     *   target/setter/engine-save failure.
     * @note Does not restore source binding, provider identity/name, reference choices or report
     *   objectEdited. Command remains in history on unsuccessful replay.
     */
    void undo() override
    {
        DataObjectAttributeTableModel::applyValue(m_layer, m_cat, m_name, m_key, m_old);
    }

private:
    QPointer<SWMMModelLayer> m_layer;
    SWMMModelLayer::DataCategory m_cat;
    QString m_name, m_key;
    QVariant m_old, m_new;
};

// Resolve the field/providers once and flush the registry once on apply,
// undo and redo; the single-cell command deliberately keeps its existing path.
class DataObjectBulkFieldEditCommand : public QUndoCommand
{
public:
    DataObjectBulkFieldEditCommand(SWMMModelLayer *layer, DC::DataCategory category,
                                  QString key, QStringList names, QList<QVariant> oldValues,
                                  QList<QVariant> newValues, const QString &label)
        : QUndoCommand(QObject::tr("Apply %1 to %2 rows").arg(label).arg(names.size())),
          m_layer(layer), m_category(category), m_key(std::move(key)),
          m_names(std::move(names)), m_old(std::move(oldValues)), m_new(std::move(newValues))
    {
    }

    void undo() override { apply(m_old); }
    void redo() override { apply(m_new); }

private:
    void apply(const QList<QVariant> &values)
    {
        if (!m_layer)
            return;
        auto *registry = registryFor(m_layer, m_category);
        QHash<QString, QPointer<QObject>> providers;
        for (auto *provider : providersOf(registry, m_category))
            providers.insert(nameOf(provider, m_category), provider);
        for (const auto &field : fieldsFor(m_layer, m_category)) {
            if (field.spec.key != m_key || !field.set)
                continue;
            bool changed = false;
            for (int i = 0; i < m_names.size(); ++i) {
                if (auto *provider = providers.value(m_names[i]).data()) {
                    field.set(provider, values[i]);
                    changed = true;
                }
            }
            if (changed)
                saveToEngine(registry, m_category, m_layer);
            return;
        }
    }

    QPointer<SWMMModelLayer> m_layer;
    DC::DataCategory m_category;
    QString m_key;
    QStringList m_names;
    QList<QVariant> m_old, m_new;
};

} // anonymous namespace

// ===========================================================================

DataObjectAttributeTableModel::DataObjectAttributeTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

DataObjectAttributeTableModel::~DataObjectAttributeTableModel() = default;

bool DataObjectAttributeTableModel::supportsCategory(SWMMModelLayer::DataCategory cat)
{
    switch (cat) {
    case DC::DataPollutants:
    case DC::DataLandUses:
    case DC::DataAquifers:
    case DC::DataStreets:
    case DC::DataInlets:
        return true;
    default:
        return false;
    }
}

void DataObjectAttributeTableModel::setSource(SWMMModelLayer *layer,
                                              SWMMModelLayer::DataCategory cat)
{
    if (m_registry)
        disconnect(m_registry, nullptr, this, nullptr);
    m_layer = supportsCategory(cat) ? layer : nullptr;
    m_category = cat;
    m_registry = registryFor(m_layer, cat);
    connectRegistry();
    reload();
}

void DataObjectAttributeTableModel::setUndoStack(QUndoStack *stack)
{
    m_undoStack = stack;
}

QUndoStack *DataObjectAttributeTableModel::undoStack() const
{
    return m_undoStack.data();
}

QList<ColumnSpec> DataObjectAttributeTableModel::columnSpecs() const
{
    QList<ColumnSpec> out;
    out.reserve(m_fields.size());
    for (const Field &f : m_fields)
        out << f.spec;
    return out;
}

SWMMObjectRef::ObjectType DataObjectAttributeTableModel::objectType() const
{
    switch (m_category) {
    case DC::DataPollutants:
        return SWMMObjectRef::Pollutant;
    case DC::DataLandUses:
        return SWMMObjectRef::LandUse;
    case DC::DataAquifers:
        return SWMMObjectRef::Aquifer;
    case DC::DataStreets:
        return SWMMObjectRef::Street;
    case DC::DataInlets:
        return SWMMObjectRef::Inlet;
    default:
        return SWMMObjectRef::Unknown;
    }
}

QString DataObjectAttributeTableModel::objectNameAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return nameOf(m_rows[row].data(), m_category);
}

int DataObjectAttributeTableModel::rowForName(const QString &name) const
{
    for (int r = 0; r < m_rows.size(); ++r)
        if (nameOf(m_rows[r].data(), m_category) == name)
            return r;
    return -1;
}

bool DataObjectAttributeTableModel::applyValue(SWMMModelLayer *layer,
                                               SWMMModelLayer::DataCategory cat,
                                               const QString &name, const QString &key,
                                               const QVariant &value)
{
    QObject *reg = registryFor(layer, cat);
    QObject *p = findProvider(reg, cat, name);
    if (!p)
        return false;
    for (const Field &f : fieldsFor(layer, cat)) {
        if (f.spec.key != key || !f.set)
            continue;
        f.set(p, value);
        saveToEngine(reg, cat, layer);
        return true;
    }
    return false;
}

void DataObjectAttributeTableModel::reload()
{
    beginResetModel();
    m_fields = fieldsFor(m_layer, m_category);
    m_rows.clear();
    m_providerRows.clear();
    for (QObject *p : providersOf(m_registry, m_category)) {
        m_providerRows.insert(p, m_rows.size());
        m_rows << p;
    }
    endResetModel();
}

void DataObjectAttributeTableModel::connectRegistry()
{
    if (!m_registry)
        return;
    // Structural changes re-read the list. Queued so an about-to-be-removed
    // provider is already gone when the list is rebuilt.
    const auto queued = Qt::QueuedConnection;
    auto hook = [this, queued](auto *reg) {
        using Reg = std::remove_pointer_t<decltype(reg)>;
        connect(reg, &Reg::providerAdded, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &Reg::providerAboutToBeRemoved, this, &DataObjectAttributeTableModel::reload,
                queued);
        connect(reg, &Reg::providerRenamed, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &Reg::providerParamsChanged, this,
                [this](QObject *p) { onProviderParamsChanged(p); });
    };
    switch (m_category) {
    case DC::DataPollutants:
        hook(static_cast<PollutantRegistry *>(m_registry.data()));
        break;
    case DC::DataLandUses:
        hook(static_cast<LandUseRegistry *>(m_registry.data()));
        break;
    case DC::DataAquifers:
        hook(static_cast<AquiferRegistry *>(m_registry.data()));
        break;
    case DC::DataStreets:
        hook(static_cast<StreetRegistry *>(m_registry.data()));
        break;
    case DC::DataInlets:
        hook(static_cast<InletRegistry *>(m_registry.data()));
        break;
    default:
        break;
    }
}

void DataObjectAttributeTableModel::onProviderParamsChanged(QObject *provider)
{
    const int r = m_providerRows.value(provider, -1);
    if (r >= 0) {
        // Whole row: an inlet type change flips which cells apply.
        emit dataChanged(index(r, 0), index(r, columnCount() - 1));
    }
}

bool DataObjectAttributeTableModel::cellApplies(int row, int col) const
{
    if (row < 0 || row >= m_rows.size() || col < 0 || col >= m_fields.size())
        return false;
    const Field &f = m_fields[col];
    return !f.applies || f.applies(m_rows[row].data());
}

bool DataObjectAttributeTableModel::simulationRunning() const
{
    if (!m_layer || !m_layer->engine())
        return false;
    int state = 0;
    return swmm_engine_get_state(m_layer->engine(), &state) == SWMM_OK &&
           state == SWMM_STATE_RUNNING;
}

int DataObjectAttributeTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int DataObjectAttributeTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_fields.size());
}

QVariant DataObjectAttributeTableModel::data(const QModelIndex &index, int role) const
{
    if (role == openswmmvis::kAttributeValueRole || role == openswmmvis::kAttributeSortRole)
        return openswmmvis::attributeValue(data(index, Qt::DisplayRole));
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_fields.size())
        return {};
    QObject *p = m_rows[index.row()].data();
    if (!p)
        return {};
    const Field &f = m_fields[index.column()];
    const bool applies = cellApplies(index.row(), index.column());

    switch (role) {
    case Qt::DisplayRole: {
        if (!applies)
            return {};
        const QVariant v = f.get ? f.get(p) : QVariant();
        // These enum editors contain optional object names, not enum states.
        if ((f.spec.key == QLatin1String("coPollutant") ||
             f.spec.key == QLatin1String("evapPattern") ||
             f.spec.key == QLatin1String("curveId")) &&
            v.toString().isEmpty())
            return {};
        return f.spec.editor == EditorKind::Enum ? QVariant(enumLabel(f.spec, v)) : v;
    }
    case Qt::EditRole:
        return applies && f.get ? f.get(p) : QVariant();
    case Qt::ToolTipRole:
        return applies ? QVariant() : QVariant(tr("Not used by this inlet design."));
    case Qt::ForegroundRole:
        if (!applies)
            return QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        return {};
    default:
        return {};
    }
}

bool DataObjectAttributeTableModel::setData(const QModelIndex &index, const QVariant &value,
                                            int role)
{
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return false;
    QObject *p = m_rows[index.row()].data();
    const Field &f = m_fields[index.column()];
    const QVariant oldVal = f.get(p);
    QVariant newVal = value;
    if (!newVal.convert(oldVal.metaType()))
        return false;
    if (newVal == oldVal)
        return true;

    const QString name = nameOf(p, m_category);
    if (m_undoStack) {
        m_undoStack->push(new DataObjectFieldEditCommand(m_layer, m_category, name, f.spec.key,
                                                         oldVal, newVal, f.spec.label));
    } else {
        applyValue(m_layer, m_category, name, f.spec.key, newVal);
    }
    emit objectEdited(name);
    return true;
}

void DataObjectAttributeTableModel::setDataForRows(int column, const QList<int> &rows,
                                                  const QVariant &value)
{
    if (column < 0 || column >= m_fields.size())
        return;
    const auto &field = m_fields[column];
    QStringList names;
    QList<QVariant> oldValues, newValues;
    for (int row : rows) {
        const auto cell = index(row, column);
        if (!(flags(cell) & Qt::ItemIsEditable))
            continue;
        const auto oldValue = field.get(m_rows[row].data());
        auto newValue = value;
        if (!newValue.convert(oldValue.metaType()) || newValue == oldValue)
            continue;
        names.append(objectNameAt(row));
        oldValues.append(oldValue);
        newValues.append(newValue);
    }
    if (names.isEmpty())
        return;
    auto *command = new DataObjectBulkFieldEditCommand(m_layer, m_category, field.spec.key,
                                                      names, oldValues, newValues,
                                                      field.spec.label);
    if (m_undoStack)
        m_undoStack->push(command);
    else {
        command->redo();
        delete command;
    }
    for (const auto &name : names)
        emit objectEdited(name);
}

Qt::ItemFlags DataObjectAttributeTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags fl = QAbstractTableModel::flags(index);
    if (!index.isValid() || index.column() >= m_fields.size())
        return fl;
    const Field &f = m_fields[index.column()];
    if (!f.set || !cellApplies(index.row(), index.column()) || !m_rows[index.row()] ||
        simulationRunning())
        return fl;
    return fl | Qt::ItemIsEditable;
}

QVariant DataObjectAttributeTableModel::headerData(int section, Qt::Orientation orientation,
                                                   int role) const
{
    if (orientation == Qt::Horizontal && section >= 0 && section < m_fields.size()) {
        const ColumnSpec &spec = m_fields[section].spec;
        const QString u = unitLabel(spec.unit);
        if (role == Qt::ToolTipRole)
            return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
        if (role == Qt::DisplayRole)
            return u.isEmpty() ? spec.label : tr("%1 (%2)").arg(spec.label, u);
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return {};
}
