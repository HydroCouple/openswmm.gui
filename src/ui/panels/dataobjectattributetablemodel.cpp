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
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/dialogs/inletpropertybag.h"
#include "curve/curveregistry.h"
#include "curve/curveprovider.h"
#include "pattern/patternregistry.h"
#include "pattern/patternprovider.h"
#include "transect/transectregistry.h"
#include "transect/transectprovider.h"
#include "ui/properties/dataobjectref.h"
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_tables.h>
#include <QSet>
#include <memory>

#include <QBrush>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalBlocker>
#include <QUndoCommand>
#include <QUndoStack>

#include <openswmm/engine/openswmm_engine.h>

#include <cmath>
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
using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::transect::TransectProvider;
using openswmmvis::transect::TransectRegistry;

constexpr double kInf = std::numeric_limits<double>::infinity();

/*! Source-project input units, independent of the active MDI project's display settings. */
bool metric(SWMMModelLayer *layer) {
    char text[32]{};
    if (layer && layer->engine()) swmm_options_get(layer->engine(), "FLOW_UNITS", text, sizeof(text));
    const QByteArray units(text);
    return units == "CMS" || units == "LPS" || units == "MLD";
}
QString unitLabel(UnitKind kind, SWMMModelLayer *layer) {
    const bool si = metric(layer);
    switch (kind) {
    case UnitKind::Length: return si ? "m" : "ft";
    case UnitKind::Velocity: return si ? "m/s" : "ft/s";
    case UnitKind::Depression: return si ? "mm" : "in";
    case UnitKind::Rate: return si ? "mm/hr" : "in/hr";
    case UnitKind::Percent: return "%";
    default: return {};
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
    case DC::DataCurves: return layer->ensureCurveRegistry();
    case DC::DataPatterns: return layer->ensurePatternRegistry();
    case DC::DataTransects: return layer->ensureTransectRegistry();
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
    case DC::DataCurves: return providersOf<openswmmvis::curve::CurveRegistry>(reg);
    case DC::DataPatterns: return providersOf<openswmmvis::pattern::PatternRegistry>(reg);
    case DC::DataTransects: return providersOf<TransectRegistry>(reg);
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
    case DC::DataCurves: return static_cast<openswmmvis::curve::CurveProvider *>(p)->name();
    case DC::DataPatterns: return static_cast<openswmmvis::pattern::PatternProvider *>(p)->name();
    case DC::DataTransects: return static_cast<TransectProvider *>(p)->name();
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
        if (nameOf(p, cat).compare(name, Qt::CaseInsensitive) == 0)
            return p;
    return nullptr;
}

/*! Save the named provider through the same registry path used by its editor. */
int saveToEngine(QObject *reg, SWMMModelLayer::DataCategory cat, SWMMModelLayer *layer,
                  const QString &name)
{
    if (!reg || !layer)
        return 0;
    void *eng = layer->engine();
    switch (cat) {
    case DC::DataPollutants:
        return static_cast<PollutantRegistry *>(reg)->saveToEngine(eng, name);

    case DC::DataLandUses:
        return static_cast<LandUseRegistry *>(reg)->saveToEngine(eng, name);

    case DC::DataAquifers:
        return static_cast<AquiferRegistry *>(reg)->saveToEngine(eng, name);

    case DC::DataStreets:
        return static_cast<StreetRegistry *>(reg)->saveToEngine(eng, name);

    case DC::DataInlets:
        return static_cast<InletRegistry *>(reg)->saveToEngine(eng, name);

    default: return 0;
    }
}

QString factorKey(int type, int index)
{
    return QStringLiteral("factor/%1/%2").arg(type).arg(index);
}

// Read native values directly: these registries' load methods only add missing
// providers, and their save methods flush every object, including unrelated drafts.
QVariantMap patternRecord(SWMM_Engine engine, const QString &name)
{
    const int idx = swmm_pattern_index(engine, name.toUtf8().constData());
    int type = -1, count = 0;
    if (swmm_pattern_get_type(engine, idx, &type) != SWMM_OK ||
        swmm_pattern_get_factor_count(engine, idx, &count) != SWMM_OK || count <= 0 ||
        count != PatternProvider::factorCountFor(openswmmvis::pattern::PatternType(type)))
        return {};
    QVariantMap result{{"type", type}, {"factorCount", count}};
    for (int i = 0; i < count; ++i) {
        double value = 0;
        if (swmm_pattern_get_factor(engine, idx, i, &value) != SWMM_OK)
            return {};
        result[factorKey(type, i)] = value;
    }
    return result;
}

QVariantMap transectRecord(SWMM_Engine engine, const QString &name)
{
    const int idx = swmm_transect_index(engine, name.toUtf8().constData());
    double nl, nr, nc, bl, br, el, er, x, y, length;
    const int count = swmm_transect_get_station_count(engine, idx);
    if (count < 0 || swmm_transect_get_roughness(engine, idx, &nl, &nr, &nc) != SWMM_OK ||
        swmm_transect_get_bank_stations(engine, idx, &bl, &br) != SWMM_OK ||
        swmm_transect_get_encroachment_stations(engine, idx, &el, &er) != SWMM_OK ||
        swmm_transect_get_modifiers(engine, idx, &x, &y, &length) != SWMM_OK)
        return {};
    // This getter truncates without reporting the required length. Grow until
    // the whole comment fits, so editing a number never truncates its description.
    QByteArray comment(1024, '\0');
    for (;;) {
        if (swmm_transect_get_comments(engine, idx, comment.data(), comment.size()) != SWMM_OK)
            return {};
        if (qstrlen(comment.constData()) < comment.size() - 1)
            break;
        comment.resize(comment.size() * 2);
    }
    return {{"nLeftBank", nl}, {"nRightBank", nr}, {"nChannel", nc},
            {"xLeftBank", bl}, {"xRightBank", br}, {"xLeftEncroachment", el},
            {"xRightEncroachment", er}, {"stationMultiplier", x},
            {"elevationOffset", y}, {"meanderFactor", length},
            {"comments", QString::fromUtf8(comment.constData())}, {"stationCount", count}};
}

void refreshFromEngine(QObject *registry, DC::DataCategory category, SWMMModelLayer *layer,
                       const QString &name)
{
    if (!registry || !layer || !layer->engine() || name.isEmpty())
        return;
    switch (category) {
    case DC::DataPatterns: {
        const auto record = patternRecord(layer->engine(), name);
        auto *p = static_cast<PatternRegistry *>(registry)->findByName(name);
        if (record.isEmpty() || !p) break;
        const int type = record["type"].toInt();
        p->setType(openswmmvis::pattern::PatternType(type));
        QVector<double> factors;
        for (int i = 0; i < record["factorCount"].toInt(); ++i)
            factors << record[factorKey(type, i)].toDouble();
        p->setAllFactors(factors);
        break;
    }
    case DC::DataTransects: {
        const auto v = transectRecord(layer->engine(), name);
        auto *p = static_cast<TransectRegistry *>(registry)->findByName(name);
        if (v.isEmpty() || !p) break;
        p->setRoughness(v["nLeftBank"].toDouble(), v["nRightBank"].toDouble(), v["nChannel"].toDouble());
        p->setBankStations(v["xLeftBank"].toDouble(), v["xRightBank"].toDouble());
        p->setEncroachmentStations(v["xLeftEncroachment"].toDouble(), v["xRightEncroachment"].toDouble());
        p->setModifiers(v["stationMultiplier"].toDouble(), v["elevationOffset"].toDouble(), v["meanderFactor"].toDouble());
        p->setComments(v["comments"].toString());
        break;
    }
    case DC::DataPollutants:
        static_cast<PollutantRegistry *>(registry)->loadFromEngine(layer->engine(), name);
        break;
    case DC::DataLandUses:
        static_cast<LandUseRegistry *>(registry)->loadFromEngine(layer->engine(), name);
        break;
    case DC::DataAquifers:
        static_cast<AquiferRegistry *>(registry)->loadFromEngine(layer->engine(), name);
        break;
    case DC::DataStreets:
        static_cast<StreetRegistry *>(registry)->loadFromEngine(layer->engine(), name);
        break;
    case DC::DataInlets:
        static_cast<InletRegistry *>(registry)->loadFromEngine(layer->engine(), name);
        break;
    default:
        break;
    }
}

void notifyProvider(QObject *, DC::DataCategory category, QObject *provider) {
    if (!provider) return;
    switch (category) {
    case DC::DataPatterns: emit static_cast<PatternProvider *>(provider)->factorsChanged(); break;
    case DC::DataTransects: {
        auto *p = static_cast<TransectProvider *>(provider);
        emit p->roughnessChanged();
        emit p->bankStationsChanged();
        emit p->encroachmentStationsChanged();
        emit p->modifiersChanged();
        emit p->commentsChanged();
        break;
    }
    case DC::DataPollutants: emit static_cast<PollutantProvider *>(provider)->paramsChanged(); break;
    case DC::DataLandUses: emit static_cast<LandUseProvider *>(provider)->paramsChanged(); break;
    case DC::DataAquifers: emit static_cast<AquiferProvider *>(provider)->paramsChanged(); break;
    case DC::DataStreets: emit static_cast<StreetProvider *>(provider)->paramsChanged(); break;
    case DC::DataInlets: emit static_cast<InletProvider *>(provider)->paramsChanged(); break;
    default: break;
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
            if (cat == DC::DataPatterns) {
                int type = -1; const int item = swmm_pattern_index(layer->engine(), name.toUtf8().constData());
                if (swmm_pattern_get_type(layer->engine(), item, &type) != SWMM_OK || type != 0) continue;
            }
            if (cat == DC::DataCurves) {
                bool compatible = false;
                for (int item = 0; item < swmm_table_count(layer->engine()); ++item) {
                    int type = -1; const char *id = swmm_table_id(layer->engine(), item);
                    if (id && QString::fromUtf8(id) == name && swmm_table_get_type(layer->engine(), item, &type) == SWMM_OK && (type == 2 || type == 3)) compatible = true;
                }
                if (!compatible) continue;
            }
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
 * @return Pollutant fields excluding Name: read-only units MG/L, UG/L or #/L; concentration
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
        {}); // Unit changes reinterpret concentrations; retain their individual owner.
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

QList<Field> patternFields()
{
    using T = openswmmvis::pattern::PatternType;
    const QStringList labels{tr("Monthly"), tr("Daily"), tr("Hourly"), tr("Weekend")};
    QList<Field> fields;
    auto type = enumField("type", tr("Pattern Type"), pairs(labels, {0, 1, 2, 3}),
                          [](QObject *o) { return int(static_cast<PatternProvider *>(o)->type()); }, {});
    type.spec.tooltip = tr("Pattern type determines the meaning and count of factors and is chosen when creating a pattern in its editor.");
    fields << type;
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < PatternProvider::factorCountFor(T(t)); ++i) {
            Field f;
            f.spec.key = factorKey(t, i);
            f.spec.label = labels[t] + QStringLiteral(" — ") + PatternProvider::rowLabel(T(t), i);
            f.spec.editor = EditorKind::Numeric;
            f.spec.setter = f.spec.key;
            f.spec.minValue = 0;
            f.spec.decimals = 6;
            f.spec.tooltip = tr("A dimensionless factor matched by pattern type and calendar/hour position. Other factors are preserved.");
            f.applies = [t](QObject *o) { return int(static_cast<PatternProvider *>(o)->type()) == t; };
            f.get = [t, i](QObject *o) -> QVariant {
                auto *p = static_cast<PatternProvider *>(o);
                return int(p->type()) == t ? QVariant(p->factor(i)) : QVariant();
            };
            f.set = [i](QObject *o, const QVariant &v) { static_cast<PatternProvider *>(o)->setFactor(i, v.toDouble()); };
            fields << f;
        }
    return fields;
}

QList<Field> transectFields()
{
    using P = TransectProvider;
    QList<Field> fields;
    // Each accessor changes one member of a native tuple and retains its siblings.
    auto add = [&](const char *key, const char *label, auto get, auto set,
                   double min = -kInf, UnitKind unit = UnitKind::None) {
        Field f;
        f.spec.key = QString::fromLatin1(key);
        f.spec.label = tr(label);
        f.spec.editor = EditorKind::Numeric;
        f.spec.setter = f.spec.key;
        f.spec.minValue = min;
        f.spec.unit = unit;
        f.spec.decimals = 6;
        f.get = [get](QObject *o) { return QVariant((static_cast<P *>(o)->*get)()); };
        f.set = [set](QObject *o, const QVariant &v) { set(static_cast<P *>(o), v.toDouble()); };
        fields << f;
    };
    add("nLeftBank", "Roughness — Left Bank n", &P::nLeftBank,
        [](P *p, double v) { p->setRoughness(v, p->nRightBank(), p->nChannel()); }, 0);
    add("nRightBank", "Roughness — Right Bank n", &P::nRightBank,
        [](P *p, double v) { p->setRoughness(p->nLeftBank(), v, p->nChannel()); }, 0);
    add("nChannel", "Roughness — Channel n", &P::nChannel,
        [](P *p, double v) { p->setRoughness(p->nLeftBank(), p->nRightBank(), v); }, 0);
    add("xLeftBank", "Bank Stations — Left", &P::xLeftBank,
        [](P *p, double v) { p->setBankStations(v, p->xRightBank()); }, -kInf, UnitKind::Length);
    add("xRightBank", "Bank Stations — Right", &P::xRightBank,
        [](P *p, double v) { p->setBankStations(p->xLeftBank(), v); }, -kInf, UnitKind::Length);
    add("xLeftEncroachment", "Encroachment Stations — Left", &P::xLeftEncroachment,
        [](P *p, double v) { p->setEncroachmentStations(v, p->xRightEncroachment()); }, -kInf, UnitKind::Length);
    add("xRightEncroachment", "Encroachment Stations — Right", &P::xRightEncroachment,
        [](P *p, double v) { p->setEncroachmentStations(p->xLeftEncroachment(), v); }, -kInf, UnitKind::Length);
    add("stationMultiplier", "Modifiers — Stations Multiplier", &P::stationMultiplier,
        [](P *p, double v) { p->setModifiers(v, p->elevationOffset(), p->meanderFactor()); }, 0);
    add("elevationOffset", "Modifiers — Elevations Offset", &P::elevationOffset,
        [](P *p, double v) { p->setModifiers(p->stationMultiplier(), v, p->meanderFactor()); }, -kInf, UnitKind::Length);
    add("meanderFactor", "Modifiers — Meander Factor", &P::meanderFactor,
        [](P *p, double v) { p->setModifiers(p->stationMultiplier(), p->elevationOffset(), v); }, 0);
    Field comments;
    comments.spec.key = "comments";
    comments.spec.label = tr("Description");
    comments.spec.editor = EditorKind::Text;
    comments.spec.setter = "comments";
    comments.get = [](QObject *o) { return static_cast<P *>(o)->comments(); };
    comments.set = [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setComments(v.toString()); };
    fields << comments;
    Field count;
    count.spec.key = "stationCount";
    count.spec.label = tr("Station Count");
    count.spec.tooltip = tr("Ordered station/elevation lists retain their individual transect editor.");
    count.get = [](QObject *o) { return static_cast<P *>(o)->pointCount(); };
    fields << count;
    return fields;
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
        {P::TensionSlope, "tensionSlope", "Tension Slope", 0.0, kInf, UnitKind::Depression},
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
 * @param[in] layer Optional source of compatible DIVERSION/RATING capture-curve choices.
 * @return Thirteen inlet fields excluding Name, type-dependent applicability matching
 *   InletPropertyBag: grate/curb/slot/custom groups, Generic grate fraction/velocity only, throat
 *   absent for DropCurb. Length/velocity metadata native project units, open fraction editor 0..1,
 *   other numeric minima zero. Callbacks unchecked provider casts, no source writes while building;
 *   static applyValue checks this applicability before writing.
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
    f << c; // A capture-curve choice enters CUSTOM for any complete target.
    c = enumField(
        QStringLiteral("curveKind"), tr("Capture Curve Type"),
        pairs({tr("NONE"), tr("DIVERSION"), tr("RATING")}, {0, 1, 2}),
        [](QObject *o) { return QVariant(int(static_cast<P *>(o)->curveKind())); },
        [](QObject *o, const QVariant &v) {
            static_cast<P *>(o)->setCurveKind(in::InletCurveKind(v.toInt()));
        });
    c.applies = inGroup(Bag::CustomGroup);
    c.set = {}; c.spec.editor = EditorKind::ReadOnly; c.spec.setter.clear();
    c.spec.tooltip = tr("Derived from the selected capture curve.");
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
    case DC::DataPatterns:
        f = patternFields();
        break;
    case DC::DataTransects:
        f = transectFields();
        break;
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

int nativeIndex(SWMM_Engine e, DC::DataCategory cat, const QString &name)
{
    const auto id = name.toUtf8();
    switch (cat) {
    case DC::DataPollutants:
        return swmm_pollutant_index(e, id.constData());
    case DC::DataLandUses:
        return swmm_landuse_index(e, id.constData());
    case DC::DataAquifers:
        return swmm_aquifer_index(e, id.constData());
    case DC::DataStreets:
        return swmm_street_index(e, id.constData());
    case DC::DataInlets:
        return swmm_inlet_index(e, id.constData());
    case DC::DataPatterns:
        return swmm_pattern_index(e, id.constData());
    case DC::DataTransects:
        return swmm_transect_index(e, id.constData());
    case DC::DataCurves:
        for (int i = 0; i < swmm_table_count(e); ++i) {
            int type = 0;
            const char *value = swmm_table_id(e, i);
            if (value && name.compare(QString::fromUtf8(value), Qt::CaseInsensitive) == 0 &&
                swmm_table_get_type(e, i, &type) == SWMM_OK && type > 0)
                return i;
        }
        return -1;
    default:
        return -1;
    }
}
bool editable(SWMMModelLayer *layer)
{
    int state = -1;
    return layer && layer->engine() && swmm_engine_get_state(layer->engine(), &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
QVariantMap inletValues(const SWMM_InletDesign &d)
{
    return {{"type", d.type},
            {"grateLength", d.grate_length},
            {"grateWidth", d.grate_width},
            {"grateType", d.grate_type},
            {"openArea", d.open_area},
            {"splashVeloc", d.splash_veloc},
            {"curbLength", d.curb_length},
            {"curbHeight", d.curb_height},
            {"throat", d.throat},
            {"slotLength", d.slot_length},
            {"slotWidth", d.slot_width},
            {"curveId", QString::fromUtf8(d.curve_id)},
            {"curveKind", d.curve_kind}};
}
SWMM_InletDesign inletDesign(const QVariantMap &v)
{
    SWMM_InletDesign d{};
    d.type = v["type"].toInt();
    d.grate_type = v["grateType"].toInt();
    d.throat = v["throat"].toInt();
    d.curve_kind = v["curveKind"].toInt();
    d.grate_length = v["grateLength"].toDouble();
    d.grate_width = v["grateWidth"].toDouble();
    d.open_area = v["openArea"].toDouble();
    d.splash_veloc = v["splashVeloc"].toDouble();
    d.curb_length = v["curbLength"].toDouble();
    d.curb_height = v["curbHeight"].toDouble();
    d.slot_length = v["slotLength"].toDouble();
    d.slot_width = v["slotWidth"].toDouble();
    qstrncpy(d.curve_id, v["curveId"].toString().toUtf8().constData(), sizeof(d.curve_id));
    return d;
}
QVariantMap readRecord(SWMMModelLayer *layer, DC::DataCategory cat, QObject *provider)
{
    if (!editable(layer) || !provider ||
        nativeIndex(layer->engine(), cat, nameOf(provider, cat)) < 0)
        return {};
    if (cat == DC::DataPatterns)
        return patternRecord(layer->engine(), nameOf(provider, cat));
    if (cat == DC::DataTransects)
        return transectRecord(layer->engine(), nameOf(provider, cat));
    if (cat == DC::DataInlets) {
        SWMM_InletDesign d{};
        if (swmm_inlet_get_design(layer->engine(),
                                  nativeIndex(layer->engine(), cat, nameOf(provider, cat)),
                                  &d) != SWMM_OK)
            return {};
        return inletValues(d);
    }
    auto *reg = registryFor(layer, cat);
    const QSignalBlocker p(provider), r(reg);
    refreshFromEngine(reg, cat, layer, nameOf(provider, cat));
    QVariantMap result;
    for (const auto &f : fieldsFor(layer, cat))
        if (f.get && f.spec.key != "Name")
            result[f.spec.key] = f.get(provider);
    return result;
}
bool writeRecord(SWMMModelLayer *layer, DC::DataCategory cat, QObject *provider,
                 const QVariantMap &record)
{
    if (!editable(layer) || !provider)
        return false;
    const auto name = nameOf(provider, cat);
    const int idx = nativeIndex(layer->engine(), cat, name);
    if (idx < 0)
        return false; // A stale provider must never recreate a deleted engine record.
    auto *reg = registryFor(layer, cat);
    const QSignalBlocker p(provider), r(reg);
    bool written = false;
    if (cat == DC::DataPatterns) {
        const int type = record["type"].toInt();
        QVector<double> factors;
        for (int i = 0; i < record["factorCount"].toInt(); ++i)
            factors << record[factorKey(type, i)].toDouble();
        written = swmm_pattern_set_factors(layer->engine(), idx, factors.constData(), factors.size()) == SWMM_OK;
    } else if (cat == DC::DataTransects) {
        const auto e = layer->engine();
        written = swmm_transect_set_roughness(e, idx, record["nLeftBank"].toDouble(), record["nRightBank"].toDouble(), record["nChannel"].toDouble()) == SWMM_OK &&
            swmm_transect_set_bank_stations(e, idx, record["xLeftBank"].toDouble(), record["xRightBank"].toDouble()) == SWMM_OK &&
            swmm_transect_set_encroachment_stations(e, idx, record["xLeftEncroachment"].toDouble(), record["xRightEncroachment"].toDouble()) == SWMM_OK &&
            swmm_transect_set_modifiers(e, idx, record["stationMultiplier"].toDouble(), record["elevationOffset"].toDouble(), record["meanderFactor"].toDouble()) == SWMM_OK &&
            swmm_transect_set_comments(e, idx, record["comments"].toString().toUtf8().constData()) == SWMM_OK;
    } else if (cat == DC::DataInlets) {
        const auto design = inletDesign(record);
        written = swmm_inlet_set_design(layer->engine(), idx, &design) == SWMM_OK;
    } else {
        for (const auto &f : fieldsFor(layer, cat))
            if (f.set && record.contains(f.spec.key))
                f.set(provider, record[f.spec.key]);
        written = saveToEngine(reg, cat, layer, name) == 1;
    }
    refreshFromEngine(reg, cat, layer, name);
    return written && readRecord(layer, cat, provider) == record;
}
DC::DataCategory referenceCategory(const QString &key)
{
    return key == "coPollutant"   ? DC::DataPollutants
           : key == "evapPattern" ? DC::DataPatterns
           : key == "curveId"     ? DC::DataCurves
                                  : DC::NumDataCategories;
}
int referenceType(SWMM_Engine e, DC::DataCategory cat, const QString &name)
{
    int type = -1;
    const int idx = nativeIndex(e, cat, name);
    if (idx < 0)
        return -1;
    if (cat == DC::DataPollutants)
        swmm_pollutant_get_units(e, idx, &type);
    if (cat == DC::DataCurves)
        swmm_table_get_type(e, idx, &type);
    if (cat == DC::DataPatterns)
        swmm_pattern_get_type(e, idx, &type);
    return type;
}
bool referenceValid(SWMM_Engine e, const QString &key, const QString &name)
{
    if (name.isEmpty())
        return true;
    const auto category = referenceCategory(key);
    const int type = referenceType(e, category, name);
    return category == DC::DataPatterns ? type == 0
           : category == DC::DataCurves ? type == 2 || type == 3
                                        : type >= 0;
}
bool memberValid(DC::DataCategory category, const QString &key, const QVariantMap &record)
{
    if (category != DC::DataTransects)
        return true;
    if (key == "comments")
        return !record[key].toString().contains(QChar::Null);
    if (key == "xLeftBank" || key == "xRightBank")
        return record["xLeftBank"].toDouble() <= record["xRightBank"].toDouble();
    if (key == "xLeftEncroachment" || key == "xRightEncroachment")
        return record["xLeftEncroachment"].toDouble() <= record["xRightEncroachment"].toDouble();
    return true;
}
bool propose(SWMMModelLayer *layer, DC::DataCategory cat, QObject *provider, const QString &key,
             const QVariant &input, const QVariantMap &before, QVariantMap &after, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    for (const auto &f : fieldsFor(layer, cat)) {
        if (f.spec.key != key)
            continue;
        if (!f.set || (f.applies && !f.applies(provider)))
            return fail(tr("This field does not apply to %1.").arg(nameOf(provider, cat)));
        auto value = input;
        if (input.userType() == qMetaTypeId<DataObjectRef>()) {
            const auto ref = input.value<DataObjectRef>();
            const auto expected = key == "coPollutant"   ? DataObjectRef::Pollutant
                                  : key == "evapPattern" ? DataObjectRef::Pattern
                                                         : DataObjectRef::AnyCurve;
            if (referenceCategory(key) == DC::NumDataCategories || ref.layer != layer ||
                ref.engine != layer->engine() || ref.kind != expected)
                return fail(tr("The reference belongs to another project or object kind."));
            value = ref.currentName;
        }
        if (!value.convert(before.value(key).metaType()))
            return fail(tr("Invalid value for %1.").arg(f.spec.label));
        if (f.spec.editor == EditorKind::Numeric || f.spec.editor == EditorKind::Integer) {
            bool ok = false;
            const double number = input.toDouble(&ok);
            if (!ok || !std::isfinite(number) || number < f.spec.minValue ||
                number > f.spec.maxValue)
                return fail(tr("%1 is outside its valid range.").arg(f.spec.label));
        }
        if (f.spec.editor == EditorKind::Enum) {
            bool known = false;
            for (const auto &choice : f.spec.enumValues)
                if (choice.toList().value(1) == value)
                    known = true;
            if (!known ||
                (value.metaType().id() == QMetaType::Int && input.toDouble() != value.toInt()))
                return fail(tr("Choose a current %1 value.").arg(f.spec.label));
        }
        after = before;
        after[key] = value;
        if (!memberValid(cat, key, after))
            return fail(tr("%1 needs left stations no greater than right stations and description text without null characters.").arg(nameOf(provider, cat)));
        if (referenceCategory(key) != DC::NumDataCategories &&
            !referenceValid(layer->engine(), key, value.toString()))
            return fail(tr("The referenced definition is missing or has an incompatible type."));
        if (cat == DC::DataInlets) {
            if (key == "curveId" && !value.toString().isEmpty()) {
                if (value.toString().toUtf8().size() >= 64)
                    return fail(tr("The native capture-curve name is too long."));
                after["type"] = SWMM_INLET_CUSTOM;
                after["curveKind"] =
                    referenceType(layer->engine(), DC::DataCurves, value.toString()) == 2
                        ? SWMM_INLET_CURVE_DIVERSION
                        : SWMM_INLET_CURVE_RATING;
            }
            const auto draft = inletDesign(after);
            SWMM_InletDesign normalized{};
            if (swmm_inlet_validate_design(layer->engine(), &draft, &normalized) != SWMM_OK)
                return fail(
                    tr("%1 needs a complete valid inlet design. Configure its required dimensions or capture curve in the inlet editor first.")
                        .arg(nameOf(provider, cat)));
            after = inletValues(normalized);
        }
        return true;
    }
    return fail(tr("The field is read-only or no longer available."));
}
struct TargetEdit {
    QPointer<QObject> provider;
    QVariantMap before, after;
};
struct Reference {
    DC::DataCategory category;
    QPointer<QObject> provider;
    QString original;
    int type;
};
class DataObjectBatchCommand final : public QUndoCommand
{
public:
    DataObjectBatchCommand(SWMMModelLayer *layer, DC::DataCategory category, QString key,
                           QVector<TargetEdit> targets)
        : layer(layer), engine(layer->engine()), category(category), key(std::move(key)),
          targets(std::move(targets)), units(metric(layer))
    {
        setText(tr("Edit %1 on %2 data objects").arg(this->key).arg(this->targets.size()));
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &context,
                         [this] { valid = false; });
        for (const auto &target : this->targets) {
            QObject::connect(target.provider, &QObject::destroyed, &context,
                             [this] { valid = false; });
            for (const auto &record : {target.before, target.after})
                for (auto it = record.cbegin(); it != record.cend(); ++it) {
                    if (referenceCategory(it.key()) == DC::NumDataCategories ||
                        it.value().toString().isEmpty())
                        continue;
                    if (!fullRecord() && it.key() != this->key)
                        continue;
                    const auto cat = referenceCategory(it.key());
                    auto *p = findProvider(registryFor(layer, cat), cat, it.value().toString());
                    if (!p || !referenceValid(engine, it.key(), it.value().toString())) {
                        valid = false;
                        continue;
                    }
                    references.append({cat, p, it.value().toString(),
                                       referenceType(engine, cat, it.value().toString())});
                    QObject::connect(p, &QObject::destroyed, &context, [this] { valid = false; });
                }
        }
        QObject::connect(layer, &SWMMModelLayer::dataObjectsChanged, &context, [this] {
            if (!valid || !this->layer || this->layer->engine() != engine)
                return;
            for (const auto &t : this->targets)
                if (!t.provider ||
                    nativeIndex(engine, this->category, nameOf(t.provider, this->category)) < 0)
                    valid = false;
        });
    }
    bool fullRecord() const
    {
        return category == DC::DataInlets && (key == "type" || key == "curveId");
    }
    QVariantMap resolveReferences(QVariantMap record) const
    {
        for (auto it = record.begin(); it != record.end(); ++it) {
            const auto original = it.value().toString();
            for (const auto &ref : references)
                if (referenceCategory(it.key()) == ref.category && original == ref.original) {
                    it.value() = nameOf(ref.provider, ref.category);
                    break;
                }
        }
        return record;
    }
    bool commit(bool forward)
    {
        if (!valid || !layer || layer->engine() != engine || !editable(layer) ||
            metric(layer) != units)
            return false;
        for (const auto &r : references)
            if (!r.provider ||
                referenceType(engine, r.category, nameOf(r.provider, r.category)) != r.type)
                return false;
        QList<QVariantMap> previous, next;
        for (const auto &target : targets) {
            const auto current = readRecord(layer, category, target.provider);
            const auto expected = resolveReferences(forward ? target.before : target.after);
            const auto desired = resolveReferences(forward ? target.after : target.before);
            if (current.isEmpty() ||
                (fullRecord() ? current != expected : current.value(key) != expected.value(key)))
                return false;
            const bool changedKind =
                (category == DC::DataPollutants && current.value("units") != expected.value("units")) ||
                ((category == DC::DataInlets || category == DC::DataPatterns) && current.value("type") != expected.value("type")) ||
                (category == DC::DataInlets && (key == "openArea" || key == "splashVeloc") && current.value("grateType") != expected.value("grateType"));
            if (!fullRecord() && changedKind)
                return false;
            auto updated = fullRecord() ? desired : current;
            if (!fullRecord())
                updated[key] = desired[key];
            if (!memberValid(category, key, updated))
                return false;
            previous << current;
            next << updated;
        }
        int attempted = 0;
        for (; attempted < targets.size(); ++attempted)
            if (!writeRecord(layer, category, targets[attempted].provider, next[attempted]))
                break;
        if (attempted != targets.size()) {
            for (int i = attempted; i >= 0; --i)
                writeRecord(layer, category, targets[i].provider, previous[i]);
            return false;
        }
        {
            const QSignalBlocker dirtyBridge(layer);
            for (const auto &target : targets)
                notifyProvider(registryFor(layer, category), category, target.provider);
        }
        for (const auto &target : targets) {
            emit layer->attributeChanged(nameOf(target.provider, category));
            if (category == DC::DataTransects)
                emit layer->transectChanged(nameOf(target.provider, category));
        }
        layer->markEdited();
        return true;
    }
    void redo() override
    {
        if (first)
            first = false;
        else if (!commit(true))
            setObsolete(true);
    }
    void undo() override
    {
        if (!commit(false))
            setObsolete(true);
    }

private:
    QObject context;
    QPointer<SWMMModelLayer> layer;
    SWMM_Engine engine;
    DC::DataCategory category;
    QString key;
    QVector<TargetEdit> targets;
    QVector<Reference> references;
    bool units, valid = true, first = true;
};

bool applyDataObjectValues(SWMMModelLayer *layer, DC::DataCategory category,
                           const QStringList &names, const QString &key, const QVariant &value,
                           QUndoStack *undo, QString *error, QStringList *changedNames)
{
    if (changedNames)
        changedNames->clear();
    auto fail = [&](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (!editable(layer))
        return fail(tr("The model is closed or initialized. Reopen the current selection."));
    if (names.isEmpty())
        return fail(tr("No data objects were captured."));
    auto *registry = registryFor(layer, category);
    if (!registry)
        return fail(tr("This data category has no shared field writer."));
    QSet<QObject *> seen;
    QVector<TargetEdit> targets;
    QSet<int> concentrationUnits;
    const bool concentration =
        category == DC::DataPollutants &&
        QStringList{"rainConc", "gwConc", "rdiiConc", "initConc"}.contains(key);
    for (const auto &name : names) {
        auto *provider = findProvider(registry, category, name);
        if (!provider)
            return fail(tr("%1 is no longer available. No objects changed.").arg(name));
        if (seen.contains(provider))
            continue;
        seen.insert(provider);
        const auto before = readRecord(layer, category, provider);
        if (before.isEmpty())
            return fail(tr("%1 cannot be read from the current model.").arg(name));
        if (concentration) {
            concentrationUnits.insert(before.value("units").toInt());
            if (concentrationUnits.size() > 1)
                return fail(tr(
                    "Concentration units differ. Choose pollutants with the same units before editing a concentration."));
        }
        // Refresh staged provider applicability without publishing a draft.
        {
            const QSignalBlocker p(provider), r(registry);
            refreshFromEngine(registry, category, layer, nameOf(provider, category));
        }
        QVariantMap after;
        if (!propose(layer, category, provider, key, value, before, after, error))
            return false;
        if (before != after)
            targets.append({provider, before, after});
    }
    if (targets.isEmpty())
        return true;
    auto command = std::make_unique<DataObjectBatchCommand>(layer, category, key, targets);
    if (!command->commit(true))
        return fail(
            tr("A selected record refused the edit or changed. Previous values were restored."));
    if (changedNames)
        for (const auto &target : targets)
            changedNames->append(nameOf(target.provider, category));
    if (undo)
        undo->push(command.release());
    return true;
}

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
    case DC::DataPatterns:
    case DC::DataTransects:
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
    if (m_category == DC::DataPatterns)
        for (const auto &p : m_rows)
            if (p) disconnect(p, nullptr, this, nullptr);
    if (m_registry)
        disconnect(m_registry, nullptr, this, nullptr);
    if (m_layer) disconnect(m_layer, nullptr, this, nullptr);
    m_layer = supportsCategory(cat) ? layer : nullptr;
    m_category = cat;
    m_registry = registryFor(m_layer, cat);
    connectRegistry();
    if (m_layer) {
        connect(m_layer, &SWMMModelLayer::attributeChanged, this, &DataObjectAttributeTableModel::refreshObject);
        connect(m_layer, &SWMMModelLayer::dataObjectsChanged, this, &DataObjectAttributeTableModel::reload, Qt::QueuedConnection);
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] { setSource(nullptr, m_category); });
        connect(m_layer, &SWMMModelLayer::optionsChanged, this, [this](const QStringList &keys) { if (keys.contains("FLOW_UNITS")) emit headerDataChanged(Qt::Horizontal, 0, columnCount()-1); });
    }
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
    case DC::DataPatterns:
        return SWMMObjectRef::TimePattern;
    case DC::DataTransects:
        return SWMMObjectRef::Transect;
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
        if (nameOf(m_rows[r].data(), m_category).compare(name, Qt::CaseInsensitive) == 0)
            return r;
    return -1;
}

bool DataObjectAttributeTableModel::applyValue(SWMMModelLayer *layer,
                                               SWMMModelLayer::DataCategory cat,
                                               const QString &name, const QString &key,
                                               const QVariant &value)
{
    return applyValues(layer, cat, {name}, key, value, nullptr);
}

bool DataObjectAttributeTableModel::applyValues(SWMMModelLayer *layer, DC::DataCategory category,
                                                const QStringList &names, const QString &key,
                                                const QVariant &value, QUndoStack *undo,
                                                QString *error)
{
    return applyDataObjectValues(layer, category, names, key, value, undo, error, nullptr);
}

DC::DataCategory DataObjectAttributeTableModel::categoryForType(SWMMObjectRef::ObjectType type)
{
    switch (type) {
    case SWMMObjectRef::TimePattern:
        return DC::DataPatterns;
    case SWMMObjectRef::Transect:
        return DC::DataTransects;
    case SWMMObjectRef::Pollutant:
        return DC::DataPollutants;
    case SWMMObjectRef::LandUse:
        return DC::DataLandUses;
    case SWMMObjectRef::Aquifer:
        return DC::DataAquifers;
    case SWMMObjectRef::Street:
        return DC::DataStreets;
    case SWMMObjectRef::Inlet:
        return DC::DataInlets;
    default:
        return DC::NumDataCategories;
    }
}

void DataObjectAttributeTableModel::refreshObject(const QString &name)
{
    QObject *provider = findProvider(m_registry, m_category, name);
    if (!provider) return;
    {
        const QSignalBlocker providerSignals(provider);
        refreshFromEngine(m_registry, m_category, m_layer, name);
    }
    onProviderParamsChanged(provider);
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
    case DC::DataPatterns: {
        auto *reg = static_cast<PatternRegistry *>(m_registry.data());
        connect(reg, &PatternRegistry::providerAdded, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &PatternRegistry::providerAboutToBeRemoved, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &PatternRegistry::providerRenamed, this, &DataObjectAttributeTableModel::reload, queued);
        auto watch = [this](PatternProvider *p) {
            connect(p, &PatternProvider::factorChanged, this, [this, p] { onProviderParamsChanged(p); });
            connect(p, &PatternProvider::factorsChanged, this, [this, p] { onProviderParamsChanged(p); });
            connect(p, &PatternProvider::typeChanged, this, [this, p] { onProviderParamsChanged(p); });
        };
        connect(reg, &PatternRegistry::providerAdded, this, watch);
        for (auto *p : reg->providers()) watch(p);
        break;
    }
    case DC::DataTransects: {
        auto *reg = static_cast<TransectRegistry *>(m_registry.data());
        connect(reg, &TransectRegistry::providerAdded, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &TransectRegistry::providerAboutToBeRemoved, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &TransectRegistry::providerRenamed, this, &DataObjectAttributeTableModel::reload, queued);
        connect(reg, &TransectRegistry::providerMetadataChanged, this, [this](QObject *p) { onProviderParamsChanged(p); });
        connect(reg, &TransectRegistry::providerPointsChanged, this, [this](QObject *p) { onProviderParamsChanged(p); });
        break;
    }
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
    return m_rows[row] && (!f.applies || f.applies(m_rows[row].data()));
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
        if (f.spec.key == "Name") return tr("Unique identity. Rename an individual object in its dedicated editor.");
        if (!f.set && m_category == DC::DataPollutants && f.spec.key == "units") return tr("Concentration units remain read-only here; changing them reinterprets existing concentrations.");
        if (!f.spec.tooltip.isEmpty()) return f.spec.tooltip;
        return applies ? QVariant() : QVariant(tr("Not used by this object type. Choose a compatible counted group."));
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
    return setDataForRows(index.column(), {index.row()}, value);
}

bool DataObjectAttributeTableModel::setDataForRows(int column, const QList<int> &rows,
                                                   const QVariant &value, QString *error)
{
    if (column < 0 || column >= m_fields.size())
        return false;
    QStringList names;
    for (int row : rows) {
        if (row < 0 || row >= rowCount() || !(flags(index(row, column)) & Qt::ItemIsEditable)) {
            if (error)
                *error = tr(
                    "The field is unavailable for at least one selected row. Choose a compatible group.");
            return false;
        }
        names << objectNameAt(row);
    }
    QStringList changedNames;
    if (!applyDataObjectValues(m_layer, m_category, names, m_fields[column].spec.key, value,
                               m_undoStack, error, &changedNames))
        return false;
    // Retain the table's edit hook. The writer already published one dirty
    // update; forwarding listeners must not publish a duplicate through the layer.
    const QSignalBlocker dirtyBridge(m_layer);
    for (const auto &name : changedNames)
        emit objectEdited(name);
    return true;
}

Qt::ItemFlags DataObjectAttributeTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags fl = QAbstractTableModel::flags(index);
    if (!index.isValid() || index.column() >= m_fields.size())
        return fl;
    const Field &f = m_fields[index.column()];
    if (!f.set || !cellApplies(index.row(), index.column()) || !m_rows[index.row()] ||
        !editable(m_layer))
        return fl;
    return fl | Qt::ItemIsEditable;
}

QVariant DataObjectAttributeTableModel::headerData(int section, Qt::Orientation orientation,
                                                   int role) const
{
    if (orientation == Qt::Horizontal && section >= 0 && section < m_fields.size()) {
        const ColumnSpec &spec = m_fields[section].spec;
        const QString u = unitLabel(spec.unit, m_layer);
        if (role == Qt::ToolTipRole)
            return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
        if (role == Qt::DisplayRole)
            return u.isEmpty() ? spec.label : tr("%1 (%2)").arg(spec.label, u);
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return {};
}
