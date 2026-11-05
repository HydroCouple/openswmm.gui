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
#include "lid/lidcontrolprovider.h"
#include "lid/lidcontrolregistry.h"
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "ui/properties/dataobjectref.h"
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_snow.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <QSet>
#include <QUuid>
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
using openswmmvis::lid::LidControlProvider;
using openswmmvis::lid::LidControlRegistry;
using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;

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
    case DC::DataLIDControls: return layer->ensureLidControlRegistry();
    case DC::DataSnowpacks: return layer->ensureSnowpackRegistry();
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
    case DC::DataLIDControls: return providersOf<LidControlRegistry>(reg);
    case DC::DataSnowpacks: return providersOf<SnowpackRegistry>(reg);
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
    case DC::DataLIDControls: return static_cast<LidControlProvider *>(p)->name();
    case DC::DataSnowpacks: return static_cast<SnowpackProvider *>(p)->name();
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

const QStringList lidKeys[] = {
    {"surfStorage", "surfRoughness", "surfSlope"},
    {"soilThick", "soilPorosity", "soilFc", "soilWp", "soilKsat", "soilKslope"},
    {"storThick", "storVoidFrac", "storKsat"},
    {"drainCoeff", "drainExpon", "drainOffset"},
    {"paveThick", "paveVoidRatio", "paveImpervious", "paveKsat", "paveClogFactor", "paveRegenDays"},
    {"matThick", "matVoidFrac", "matRoughness"}};
const QStringList lidExtraKeys{"surfVegetation", "surfSideSlope", "soilSuction", "storClogging", "drainDelay", "drainOpen", "drainClose"};
const int lidExtraGroups[]{0, 0, 1, 2, 3, 3, 3};

#include "lidnoderecord_p.h"

bool lidApplies(int type, int group)
{
    switch (group) {
    case 0: return type >= 0 && type < 8 && type != 5;
    case 1: return type == 0 || type == 1 || type == 2 || type == 4;
    case 2: case 3: return type == 0 || type == 3 || type == 4 || type == 5;
    case 4: return type == 4;
    case 5: return type == 2;
    default: return false;
    }
}
QVariantList lidGroup(SWMM_Engine engine, int index, int group)
{
    double v[6]{};
    int result = SWMM_ERR_BADPARAM;
    switch (group) {
    case 0: result = swmm_lid_get_surface(engine, index, &v[0], &v[1], &v[2]); break;
    case 1: result = swmm_lid_get_soil(engine, index, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]); break;
    case 2: result = swmm_lid_get_storage(engine, index, &v[0], &v[1], &v[2]); break;
    case 3: result = swmm_lid_get_drain(engine, index, &v[0], &v[1], &v[2]); break;
    case 4: result = swmm_lid_get_pavement(engine, index, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]); break;
    case 5: result = swmm_lid_get_drainmat(engine, index, &v[0], &v[1], &v[2]); break;
    }
    QVariantList values;
    if (result == SWMM_OK)
        for (int i = 0; i < lidKeys[group].size(); ++i) values << v[i];
    return values;
}
QVariantMap lidRecord(SWMMModelLayer *layer, const QString &name)
{
    const auto engine = layer->engine();
    const int index = swmm_lid_index(engine, name.toUtf8().constData());
    int type = -1;
    if (swmm_lid_get_type(engine, index, &type) != SWMM_OK) return {};
    QVariantMap record{{"type", type}};
    for (int group = 0; group < 6; ++group) {
        const auto values = lidGroup(engine, index, group);
        if (values.size() != lidKeys[group].size()) return {};
        for (int i = 0; i < values.size(); ++i) record[lidKeys[group][i]] = values[i];
    }
    for (int field = 0; field < 7; ++field) {
        double value = 0;
        if (swmm_lid_get_extra(engine, index, field, &value) != SWMM_OK) return {};
        record[lidExtraKeys[field]] = value;
    }
    record["drainCurve"] = QString::fromUtf8(swmm_lid_get_drain_curve(engine, index));
    if (!readLidConstituents(layer, index, record)) return {};
    if (type == 8 && !readLidNode(layer, index, record)) return {};
    return record;
}
bool writeLidRecord(SWMMModelLayer *layer, const QString &name, const QVariantMap &record)
{
    const auto engine = layer->engine();
    const int index = swmm_lid_index(engine, name.toUtf8().constData());
    const auto current = lidRecord(layer, name);
    if (current.isEmpty()) return false;
    if (record["type"].toInt() == 8)
        return writeLidNode(layer, index, record, false);
    if (current["type"] != record["type"] && swmm_lid_set_type(engine, index, record["type"].toInt()) != SWMM_OK) return false;
    // Only the changed group is submitted: unused groups may be unconfigured,
    // and the narrower native setters preserve omitted vegetation/suction/etc.
    for (int group = 0; group < 6; ++group) {
        double v[6]{}; bool changed = false;
        for (int i = 0; i < lidKeys[group].size(); ++i) {
            const auto &key = lidKeys[group][i];
            v[i] = record[key].toDouble(); changed |= record[key] != current[key];
        }
        if (!changed) continue;
        int result = SWMM_ERR_BADPARAM;
        switch (group) {
        case 0: result = swmm_lid_set_surface(engine, index, v[0], v[1], v[2]); break;
        case 1: result = swmm_lid_set_soil(engine, index, v[0], v[1], v[2], v[3], v[4], v[5]); break;
        case 2: result = swmm_lid_set_storage(engine, index, v[0], v[1], v[2]); break;
        case 3: result = swmm_lid_set_drain(engine, index, v[0], v[1], v[2]); break;
        case 4: result = swmm_lid_set_pavement(engine, index, v[0], v[1], v[2], v[3], v[4], v[5]); break;
        case 5: result = swmm_lid_set_drainmat(engine, index, v[0], v[1], v[2]); break;
        }
        if (result != SWMM_OK) return false;
    }
    for (int field = 0; field < 7; ++field)
        if (current[lidExtraKeys[field]] != record[lidExtraKeys[field]] &&
            swmm_lid_set_extra(engine, index, field, record[lidExtraKeys[field]].toDouble()) != SWMM_OK) return false;
    if (!writeLidRemovals(layer, index, current, record)) return false;
    return true;
}
QString snowKey(int index) { return QStringLiteral("snow/%1").arg(index); }
const QStringList snowSiteKeys{snowKey(27), snowKey(28), snowKey(29), snowKey(30), snowKey(31)};
int snowDefault(SWMM_Engine engine)
{
    int model = -1;
    swmm_snow_get_model(engine, &model);
    return model;
}
QVariantMap snowRecord(SWMM_Engine engine, const QString &name)
{
    const int index = swmm_snowpack_index(engine, name.toUtf8().constData());
    double v[27]{};
    if (swmm_snowpack_get_plowable(engine, index, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) != SWMM_OK ||
        swmm_snowpack_get_impervious(engine, index, &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13]) != SWMM_OK ||
        swmm_snowpack_get_pervious(engine, index, &v[14], &v[15], &v[16], &v[17], &v[18], &v[19], &v[20]) != SWMM_OK ||
        swmm_snowpack_get_removal(engine, index, &v[21], &v[22], &v[23], &v[24], &v[25], &v[26]) != SWMM_OK)
        return {};
    SWMM_UebSnowpack ueb{};
    if (swmm_snowpack_get_ueb(engine, index, &ueb) != SWMM_OK) return {};
    QByteArray destination(256, '\0');
    for (;;) {
        if (swmm_snowpack_get_removal_subcatch(engine, index, destination.data(), destination.size()) != SWMM_OK) return {};
        if (qstrlen(destination.constData()) < destination.size() - 1) break;
        destination.resize(destination.size() * 2);
    }
    QVariantMap record{{"modelKind", ueb.model}, {"effectiveModel", ueb.model < 0 ? snowDefault(engine) : ueb.model},
                       {"removalSubcatch", QString::fromUtf8(destination.constData())}};
    for (int i = 0; i < 27; ++i) record[snowKey(i)] = v[i];
    const double site[]{ueb.forest_fraction, ueb.drift_factor, ueb.ground_heat_wm2, ueb.extinction_depth_m, ueb.initial_energy_kjm2};
    for (int i = 0; i < 5; ++i) record[snowKey(27 + i)] = site[i];
    return record;
}
bool writeSnowRecord(SWMM_Engine engine, const QString &name, const QVariantMap &record)
{
    const auto id = name.toUtf8(), destination = record["removalSubcatch"].toString().toUtf8();
    SWMM_SnowpackEdit edit{};
    edit.name = id.constData(); edit.removal_subcatch = destination.constData();
    for (int surface = 0; surface < 3; ++surface)
        for (int i = 0; i < 7; ++i) edit.surface[surface][i] = record[snowKey(surface * 7 + i)].toDouble();
    for (int i = 0; i < 6; ++i) edit.removal[i] = record[snowKey(21 + i)].toDouble();
    edit.ueb = {record["modelKind"].toInt(), record[snowKey(27)].toDouble(), record[snowKey(28)].toDouble(),
                record[snowKey(29)].toDouble(), record[snowKey(30)].toDouble(), record[snowKey(31)].toDouble()};
    char error[1024]{};
    return swmm_snowpack_apply(engine, &edit, 1, error, sizeof(error)) == SWMM_OK;
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
    case DC::DataLIDControls: {
        using P = LidControlProvider;
        auto *p = static_cast<LidControlRegistry *>(registry)->findByName(name);
        const auto v = lidRecord(layer, name);
        if (!p || v.isEmpty()) break;
        if (p->dirty()) break; // The complete-design editor owns this uncommitted draft.
        p->setType(v["type"].toInt());
        if (p->type() == 8) {
            QVector<SWMM_LidNodeLayer> rows; QVector<SWMM_LidRichardsMaterial> materials;
            QHash<int, int> occurrences; p->treatments.clear();
            for (const auto &kind : v["_kinds"].toList()) {
                SWMM_LidNodeLayer row{}; row.kind = kind.toInt(); const auto prefix = lidNodePrefix(row.kind, ++occurrences[row.kind]);
                for (int i = 0; i < lidNodeParamCounts[row.kind]; ++i) row.params[i] = v[prefix + "p/" + QString::number(i)].toDouble();
                rows << row;
                materials << SWMM_LidRichardsMaterial{v[prefix + "r/0"].toDouble(), v[prefix + "r/1"].toDouble(), v[prefix + "r/2"].toDouble(), v[prefix + "r/3"].toDouble(), v[prefix + "r/4"].toDouble()};
                for (auto *pollutant : lidPollutants(layer)) {
                    const auto key = prefix + "t/" + lidPollToken(pollutant) + "/";
                    if (v[key + "present"].toInt()) p->treatments << openswmmvis::lid::LidLayerTreatment{int(rows.size()), pollutant->name(), v[key + "removal"].toDouble(), v[key + "decay"].toDouble(), v[key + "expression"].toString()};
                }
            }
            p->setNodeLayers(rows);
            p->setNodeFlow({v["flowModel"].toInt(), v["flowCells"].toInt(), v["flowAtol"].toDouble(), v["flowRtol"].toDouble(), v["flowMaxStep"].toDouble()}, materials);
        } else {
            const decltype(&P::setSurfStorage) setters[]{&P::setSurfStorage, &P::setSurfRoughness, &P::setSurfSlope,
            &P::setSoilThick, &P::setSoilPorosity, &P::setSoilFc, &P::setSoilWp, &P::setSoilKsat, &P::setSoilKslope,
            &P::setStorThick, &P::setStorVoidFrac, &P::setStorKsat, &P::setDrainCoeff, &P::setDrainExpon, &P::setDrainOffset};
            int i = 0;
            for (int group = 0; group < 4; ++group)
                for (const auto &key : lidKeys[group]) (p->*setters[i++])(v[key].toDouble());
        }
        p->setProperty("lidAcceptedValues", v);
        QVariantList unitKinds;
        for (auto *pollutant : lidPollutants(layer)) unitKinds << v["_unit/" + lidPollToken(pollutant)];
        p->setProperty("lidAcceptedSchema", QVariantList{v["type"], v["_kinds"], v["_pollutants"], v["_species"], unitKinds, p->property("lidStructureRevision")});
        p->clearDirty();
        break;
    }
    case DC::DataSnowpacks: {
        auto *p = static_cast<SnowpackRegistry *>(registry)->findByName(name);
        const auto v = snowRecord(layer->engine(), name);
        if (!p || v.isEmpty()) break;
        p->setDefaultModel(snowDefault(layer->engine()));
        p->setModelKind(v["modelKind"].toInt());
        for (int i = 0; i < SnowpackProvider::ParamCount; ++i) p->setParam(i, v[snowKey(i)].toDouble());
        p->setRemovalSubcatch(v["removalSubcatch"].toString());
        break;
    }
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
    case DC::DataLIDControls: emit static_cast<LidControlProvider *>(provider)->paramsChanged(); break;
    case DC::DataSnowpacks: emit static_cast<SnowpackProvider *>(provider)->paramsChanged(); break;
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

QList<Field> lidFields(SWMMModelLayer *layer)
{
    if (layer)
        if (auto *reg = qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry()))
            for (auto *p : reg->providers()) {
                const QSignalBlocker ps(p), rs(reg);
                refreshFromEngine(reg, DC::DataLIDControls, layer, p->name());
            }
    const QStringList types{tr("Bio-Retention Cell"), tr("Rain Garden"), tr("Green Roof"),
        tr("Infiltration Trench"), tr("Permeable Pavement"), tr("Rain Barrel"),
        tr("Rooftop Disconnection"), tr("Vegetative Swale"), tr("Storage Node (ordered layers)")};
    auto type = enumField("type", tr("LID Type"), pairs(types, {0,1,2,3,4,5,6,7,8}),
                         [](QObject *o) { return static_cast<LidControlProvider *>(o)->type(); }, [](QObject *, const QVariant &) {});
    type.spec.tooltip = tr("Unassigned standard designs can change type while retaining their layer records. Assigned controls and applied NODE profiles retain their native type restrictions. Create a new layered control for NODE.");
    QList<Field> fields{type};
    const QStringList labels[]{
        {tr("Surface — Storage Depth"), tr("Surface — Roughness"), tr("Surface — Slope")},
        {tr("Soil — Thickness"), tr("Soil — Porosity"), tr("Soil — Field Capacity"), tr("Soil — Wilting Point"), tr("Soil — Conductivity"), tr("Soil — Conductivity Slope")},
        {tr("Storage — Thickness"), tr("Storage — Void Ratio"), tr("Storage — Seepage")},
        {tr("Drain — Coefficient"), tr("Drain — Exponent"), tr("Drain — Offset")},
        {tr("Pavement — Thickness"), tr("Pavement — Void Ratio"), tr("Pavement — Impervious Fraction"), tr("Pavement — Conductivity"), tr("Pavement — Clogging Factor"), tr("Pavement — Regeneration Interval (days)")},
        {tr("Drainage Mat — Thickness"), tr("Drainage Mat — Void Fraction"), tr("Drainage Mat — Roughness")}};
    const UnitKind units[][6]{{UnitKind::Depression, UnitKind::None, UnitKind::Percent},
        {UnitKind::Depression, UnitKind::None, UnitKind::None, UnitKind::None, UnitKind::Rate, UnitKind::None},
        {UnitKind::Depression, UnitKind::None, UnitKind::Rate},
        {UnitKind::None, UnitKind::None, UnitKind::Depression},
        {UnitKind::Depression, UnitKind::None, UnitKind::None, UnitKind::Rate, UnitKind::None, UnitKind::None},
        {UnitKind::Depression, UnitKind::None, UnitKind::None}};
    const QPointer<SWMMModelLayer> source(layer);
    auto add = [&](const QString &key, const QString &label, UnitKind unit, double maximum,
                   std::function<bool(QObject *)> applies, openswmmvis::EditorKind editor = EditorKind::Numeric) {
        Field f; f.spec.key = key; f.spec.label = label; f.spec.editor = editor; f.spec.setter = key;
        f.spec.minValue = 0; f.spec.maxValue = maximum; f.spec.decimals = 6; f.spec.unit = unit;
        f.applies = std::move(applies);
        f.get = [key](QObject *o) -> QVariant {
            const auto record = o->property("lidAcceptedValues").toMap();
            if (key.contains("/t/") && !key.endsWith("/present")) {
                const auto prefix = key.left(key.lastIndexOf('/') + 1);
                if (!record[prefix + "present"].toInt()) return {};
            }
            return record.value(key);
        };
        f.set = [](QObject *, const QVariant &) {};
        f.spec.tooltip = tr("Changes this field for every captured control. Other layer members and records are preserved.");
        fields << f;
    };
    for (int group = 0; group < 6; ++group)
        for (int i = 0; i < lidKeys[group].size(); ++i) {
            Field f; f.spec.key = lidKeys[group][i]; f.spec.label = labels[group][i];
            f.spec.setter = f.spec.key; f.spec.editor = EditorKind::Numeric;
            f.spec.minValue = 0; f.spec.decimals = 6; f.spec.unit = units[group][i];
            if ((group == 1 && i >= 1 && i <= 3) || (group == 4 && i == 2) || (group == 5 && i == 1)) f.spec.maxValue = 1;
            f.spec.tooltip = tr("Changes one member of this LID layer and preserves its other fields. Configure missing layer designs in the dedicated editor first.");
            if (group == 3 && i == 0) f.spec.tooltip = tr("Native input depth/hour power-law coefficient; its dimensional scale depends on Drain Exponent.");
            f.applies = [group](QObject *o) { return lidApplies(static_cast<LidControlProvider *>(o)->type(), group); };
            f.get = [source, group, i](QObject *o) -> QVariant {
                if (!source || !source->engine()) return {};
                return lidGroup(source->engine(), swmm_lid_index(source->engine(), static_cast<LidControlProvider *>(o)->name().toUtf8().constData()), group).value(i);
            };
            f.set = [](QObject *, const QVariant &) {}; // Checked native group writer below.
            fields << f;
        }
    const QStringList extras{tr("Surface — Vegetation Fraction"), tr("Surface — Side Slope"), tr("Soil — Suction"), tr("Storage — Clogging Factor"), tr("Drain — Delay (hours)"), tr("Drain — Opening Depth"), tr("Drain — Closing Depth")};
    for (int i = 0; i < 7; ++i) {
        const auto unit = i == 2 || i >= 5 ? UnitKind::Depression : UnitKind::None;
        add(lidExtraKeys[i], extras[i], unit, i == 0 ? 1. : kInf,
            [group = lidExtraGroups[i]](QObject *o) { return lidApplies(static_cast<LidControlProvider *>(o)->type(), group); });
    }
    Field curve; curve.spec.key = "drainCurve"; curve.spec.label = tr("Drain — Retained Curve");
    curve.spec.tooltip = tr("The native engine stores this token for persistence but does not yet evaluate a drain-curve law. It is read-only here.");
    curve.get = [](QObject *o) { return o->property("lidAcceptedValues").toMap().value("drainCurve"); };
    curve.applies = [](QObject *o) { return lidApplies(static_cast<LidControlProvider *>(o)->type(), 3); }; fields << curve;
    if (layer) {
        for (auto *p : lidPollutants(layer)) {
            add("removal/p/" + lidPollToken(p), tr("Drain Removal — %1").arg(p->name()), UnitKind::Percent, 100,
                [](QObject *o) { return lidApplies(static_cast<LidControlProvider *>(o)->type(), 3); });
        }
        QVariantList species;
        if (layer->dataObjectCount(DC::DataLIDControls)) {
            const auto r = lidRecord(layer, layer->dataObjectNameAt(DC::DataLIDControls, 0)); species = r["_species"].toList();
        }
        for (const auto &definition : species) {
            const auto row = definition.toList(); const auto name = row.value(0).toString();
            const auto key = QString("removal/s/") + QString::fromLatin1(name.toUtf8().toPercentEncoding());
            add(key, tr("Drain Removal — %1 (reaction species)").arg(name), UnitKind::Percent, 100,
                [](QObject *o) { return lidApplies(static_cast<LidControlProvider *>(o)->type(), 3); });
            if (row.value(1).toInt() || swmm_pollutant_index(layer->engine(), name.toUtf8().constData()) >= 0) {
                fields.last().set = {};
                fields.last().spec.tooltip = tr("Wall species do not leave in underdrain water; a species sharing a pollutant token also lacks an unambiguous persisted removal row.");
            }
        }
    }
    QHash<int, int> maxima;
    if (layer)
        for (QObject *o : providersOf(layer->ensureLidControlRegistry(), DC::DataLIDControls)) {
            QHash<int, int> counts;
            for (const auto &kind : o->property("lidAcceptedValues").toMap()["_kinds"].toList()) ++counts[kind.toInt()];
            for (auto it = counts.cbegin(); it != counts.cend(); ++it) maxima[it.key()] = qMax(maxima[it.key()], it.value());
        }
    const auto isNode = [](QObject *o) { return static_cast<LidControlProvider *>(o)->type() == 8; };
    auto flow = enumField("flowModel", tr("NODE Flow — Model"), pairs({tr("Gravity/backwater"), tr("Richards")}, {0, 1}),
        [](QObject *o) { return o->property("lidAcceptedValues").toMap().value("flowModel"); }, [](QObject *, const QVariant &) {});
    flow.applies = isNode; flow.spec.tooltip = tr("Richards requires complete valid material records. Switching to gravity resets inactive numerical settings to native defaults and retains materials."); fields << flow;
    const QStringList flowLabels{tr("NODE Flow — Cells per Layer"), tr("NODE Flow — Absolute Tolerance"), tr("NODE Flow — Relative Tolerance"), tr("NODE Flow — Maximum Step (s)")};
    for (int i = 1; i < 5; ++i) {
        add(lidFlowKeys[i], flowLabels[i - 1], UnitKind::None, i == 1 ? 256. : kInf,
            [](QObject *o) { return o->property("lidAcceptedValues").toMap().value("flowModel").toInt() == 1; },
            i == 1 ? EditorKind::Integer : EditorKind::Numeric);
        fields.last().spec.minValue = i == 1 ? 1 : 1.e-12; fields.last().spec.decimals = 12;
    }
    const QStringList physicalLabels[]{
        {tr("Thickness"), tr("Vegetation Fraction")},
        {tr("Thickness"), tr("Porosity"), tr("Field Capacity"), tr("Wilting Point"), tr("Conductivity"), tr("Conductivity Slope"), tr("Suction")},
        {tr("Thickness"), tr("Porosity"), tr("Conductivity")}, {tr("Seepage"), tr("Clogging")}};
    const QStringList retentionLabels{tr("Residual Water"), tr("Retention Alpha (1/m)"), tr("Retention n"), tr("Pore Connectivity"), tr("Specific Storage (1/m)")};
    for (int kind = 0; kind < 4; ++kind)
        for (int occurrence = 1; occurrence <= maxima.value(kind); ++occurrence) {
            const auto prefix = lidNodePrefix(kind, occurrence);
            const auto title = QStringList{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"}[kind] + " " + QString::number(occurrence);
            auto available = [prefix](QObject *o) { return o->property("lidAcceptedValues").toMap().contains(prefix + "p/0"); };
            for (int i = 0; i < lidNodeParamCounts[kind]; ++i) {
                const auto unit = (kind != 3 && i == 0) || (kind == 1 && i == 6) ? UnitKind::Depression
                    : (kind == 1 && i == 4) || (kind == 2 && i == 2) || (kind == 3 && i == 0) ? UnitKind::Rate : UnitKind::None;
                add(prefix + "p/" + QString::number(i), title + " — " + physicalLabels[kind][i], unit, kInf, available);
                fields.last().spec.tooltip = tr("Matches %1 by kind and occurrence counted from the top within that kind. Missing occurrences refuse the whole batch; other layers are preserved.").arg(title);
            }
            if (kind == 1 || kind == 2)
                for (int i = 0; i < 5; ++i) {
                    add(prefix + "r/" + QString::number(i), title + " — " + retentionLabels[i], UnitKind::None, kInf,
                        [prefix](QObject *o) { return lidMaterialValid(o->property("lidAcceptedValues").toMap(), prefix); });
                    fields.last().spec.decimals = 12;
                    fields.last().spec.tooltip = tr("Edit a complete valid material. Use Initialize materials in the design editor for an unconfigured profile. Alpha and specific storage are inverse metres in both unit systems.");
                }
            if (kind == 3 || !layer) continue;
            for (auto *p : lidPollutants(layer)) {
                const auto key = prefix + "t/" + lidPollToken(p) + "/";
                const auto treatmentTitle = title + " / " + p->name();
                auto present = enumField(key + "present", treatmentTitle + " — Treatment", pairs({tr("Not set"), tr("Enabled")}, {0, 1}),
                    [key](QObject *o) { return o->property("lidAcceptedValues").toMap().value(key + "present"); }, [](QObject *, const QVariant &) {});
                present.applies = available; fields << present;
                auto enabled = [key](QObject *o) { return o->property("lidAcceptedValues").toMap()[key + "present"].toInt() == 1; };
                add(key + "removal", treatmentTitle + " — Removal", UnitKind::Percent, 100, enabled);
                add(key + "decay", treatmentTitle + " — Decay (1/day)", UnitKind::None, kInf, enabled);
                add(key + "expression", treatmentTitle + " — Expression", UnitKind::None, kInf, enabled, EditorKind::Text);
            }
        }
    Field node;
    node.spec.key = "nodeLayers"; node.spec.label = tr("Ordered NODE Layers");
    node.spec.tooltip = tr("Shared layer fields match explicit kind/occurrence labels. Insert, remove and move layers in the single-control design editor.");
    node.applies = [](QObject *o) { return static_cast<LidControlProvider *>(o)->type() == 8; };
    node.get = [](QObject *o) { return static_cast<LidControlProvider *>(o)->nodeLayers().size(); };
    fields << node;
    return fields;
}

QList<Field> snowFields(SWMMModelLayer *layer)
{
    using P = SnowpackProvider;
    const auto modelChoices = pairs({tr("Project default"), tr("Degree day"), tr("UEB energy balance")}, {-1, 0, 1});
    QList<Field> fields{enumField("modelKind", tr("Snow Model"), modelChoices,
        [](QObject *o) { return static_cast<P *>(o)->modelKind(); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setModelKind(v.toInt()); })};
    auto effective = enumField("effectiveModel", tr("Effective Snow Model"), modelChoices,
                              [](QObject *o) { return static_cast<P *>(o)->effectiveModel(); }, {});
    effective.spec.tooltip = tr("The explicit snow model, or the current project default when inherited.");
    fields << effective;
    const QStringList groups{tr("Plowable"), tr("Impervious"), tr("Pervious")};
    const QStringList members{tr("Minimum Melt"), tr("Maximum Melt"), tr("Base Temperature"),
        tr("Free Water Capacity"), tr("Initial Snow Water Equivalent"), tr("Initial Free Water"), tr("Full Cover Depth")};
    const QStringList other{tr("Removal — Threshold Depth"), tr("Removal — Out of Watershed"),
        tr("Removal — To Impervious"), tr("Removal — To Pervious"), tr("Removal — Immediate Melt"), tr("Removal — To Subcatchment"),
        tr("UEB — Forest Fraction"), tr("UEB — Drift Factor"), tr("UEB — Ground Heat (W/m²)"), tr("UEB — Extinction Depth (m)"), tr("UEB — Initial Energy (kJ/m²)")};
    for (int i = 0; i < P::ParamCount; ++i) {
        Field f; f.spec.key = snowKey(i); f.spec.setter = f.spec.key;
        f.spec.editor = EditorKind::Numeric; f.spec.minValue = 0; f.spec.decimals = 6;
        if (i < 21) {
            const int member = i % 7;
            f.spec.label = groups[i / 7] + " — " + (i == 6 ? tr("Plowable Fraction") : members[member]);
            if (member < 2) f.spec.label += metric(layer) ? " (mm/hr/°C)" : " (in/hr/°F)";
            if (member == 2) { f.spec.label += metric(layer) ? " (°C)" : " (°F)"; f.spec.minValue = -kInf; }
            if (member == 3 || i == 6) f.spec.maxValue = 1;
            if (member >= 4 && i != 6) f.spec.unit = UnitKind::Depression;
        } else {
            f.spec.label = other[i - 21];
            if (i == 21) f.spec.unit = UnitKind::Depression;
            if ((i >= 22 && i <= 27)) f.spec.maxValue = 1;
            if (i == 29 || i == 31) f.spec.minValue = -kInf;
        }
        f.applies = [i](QObject *o) {
            const bool ueb = static_cast<P *>(o)->effectiveModel() == 1;
            return i >= 27 ? ueb : !(ueb && i < 21 && i % 7 < 4);
        };
        f.get = [i](QObject *o) { return static_cast<P *>(o)->param(i); };
        f.set = [i](QObject *o, const QVariant &v) { static_cast<P *>(o)->setParam(i, v.toDouble()); };
        f.spec.tooltip = i >= 27 ? tr("A UEB site edit on an inherited UEB pack creates an explicit UEB override. Project default clears the entire site override.")
                                : tr("Changes one snowpack member while retaining the other surfaces and removal fields. Removal fractions must total at most one.");
        fields << f;
    }
    QStringList names{tr("(none)")}; QVariantList values{QString()};
    if (layer)
        for (int i = 0; i < layer->categoryCount(DC::CatSubcatchments); ++i) {
            const auto name = layer->objectNameAt(DC::CatSubcatchments, i); names << name; values << name;
        }
    fields << enumField("removalSubcatch", tr("Removal — Destination Subcatchment"), pairs(names, values),
        [](QObject *o) { return static_cast<P *>(o)->removalSubcatch(); },
        [](QObject *o, const QVariant &v) { static_cast<P *>(o)->setRemovalSubcatch(v.toString()); });
    return fields;
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
    case DC::DataLIDControls:
        f = lidFields(layer);
        break;
    case DC::DataSnowpacks:
        f = snowFields(layer);
        break;
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
    case DC::DataLIDControls:
        return swmm_lid_index(e, id.constData());
    case DC::DataSnowpacks:
        return swmm_snowpack_index(e, id.constData());
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
    if (cat == DC::DataLIDControls)
        return lidRecord(layer, nameOf(provider, cat));
    if (cat == DC::DataSnowpacks)
        return snowRecord(layer->engine(), nameOf(provider, cat));
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
    if (cat == DC::DataLIDControls) {
        written = writeLidRecord(layer, name, record);
    } else if (cat == DC::DataSnowpacks) {
        written = writeSnowRecord(layer->engine(), name, record);
    } else if (cat == DC::DataPatterns) {
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
bool memberValid(SWMMModelLayer *layer, DC::DataCategory category, const QString &key, const QVariantMap &record, const QString &name)
{
    const auto engine = layer->engine();
    if (category == DC::DataLIDControls) {
        const int index = swmm_lid_index(engine, name.toUtf8().constData());
        if (record["type"].toInt() == 8)
            return writeLidNode(layer, index, record, true);
        if (key == "type")
            return record[key].toInt() < 8 && swmm_lid_validate_type(engine, index, record[key].toInt()) == SWMM_OK;
        if (lidExtraKeys.contains(key)) {
            const double v = record[key].toDouble();
            return std::isfinite(v) && v >= 0 && (key != "surfVegetation" || v < 1);
        }
        if (key.startsWith("removal/")) {
            const double v = record[key].toDouble();
            return std::isfinite(v) && v >= 0 && v <= 100;
        }
        for (int group = 0; group < 6; ++group) {
            if (!lidKeys[group].contains(key)) continue;
            for (const auto &member : lidKeys[group]) {
                const double value = record[member].toDouble();
                if (!std::isfinite(value) || value < 0) return false;
            }
            if (group == 1)
                return record["soilPorosity"].toDouble() <= 1 && record["soilPorosity"].toDouble() > record["soilFc"].toDouble() &&
                       record["soilFc"].toDouble() > record["soilWp"].toDouble();
            if (group == 2) return record["storVoidFrac"].toDouble() > 0;
            if (group == 4) return record["paveImpervious"].toDouble() <= 1;
            if (group == 5) return record["matVoidFrac"].toDouble() <= 1;
            return true;
        }
        return false;
    }
    if (category == DC::DataSnowpacks) {
        for (int i = 0; i < 32; ++i) {
            const double value = record[snowKey(i)].toDouble();
            if (!std::isfinite(value)) return false;
            if (i < 27 && !(i < 21 && i % 7 == 2) && value < 0) return false;
        }
        for (int surface = 0; surface < 3; ++surface)
            if (record[snowKey(surface * 7)].toDouble() > record[snowKey(surface * 7 + 1)].toDouble() ||
                record[snowKey(surface * 7 + 3)].toDouble() > 1) return false;
        if (record[snowKey(6)].toDouble() > 1) return false;
        double total = 0;
        for (int i = 22; i <= 26; ++i) total += record[snowKey(i)].toDouble();
        const auto destination = record["removalSubcatch"].toString();
        if (total > 1 + 1.e-12 || destination.contains(QChar::Null) || destination.contains('\n') || destination.contains('\r') || destination.contains('"') ||
            (record[snowKey(26)].toDouble() > 0 && swmm_subcatch_index(engine, destination.toUtf8().constData()) < 0)) return false;
        return record["modelKind"].toInt() < 0 ||
            (record[snowKey(27)].toDouble() >= 0 && record[snowKey(27)].toDouble() <= 1 &&
             record[snowKey(28)].toDouble() >= 0 && record[snowKey(30)].toDouble() > 0);
    }
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
             const QVariant &input, const QVariantMap &before, QVariantMap &after, QString *error,
             const QList<Field> &fields)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (cat == DC::DataLIDControls && key == "_nodeProfile") {
        const auto envelope = input.toMap();
        after = envelope["after"].toMap();
        if (envelope["before"].toMap() != before || before["type"].toInt() != 8 ||
            after["type"].toInt() != 8 || after["_pollutants"] != before["_pollutants"] ||
            after["_species"] != before["_species"] ||
            !memberValid(layer, cat, key, after, nameOf(provider, cat)))
            return fail(tr("The layer profile changed or is invalid. Review the current design and try again."));
        return true;
    }
    for (const auto &f : fields) {
        if (f.spec.key != key)
            continue;
        if (!f.set || (f.applies && !f.applies(provider)))
            return fail(tr("This field does not apply to %1.").arg(nameOf(provider, cat)));
        auto value = input;
        if (input.userType() == qMetaTypeId<DataObjectRef>()) {
            const auto ref = input.value<DataObjectRef>();
            const auto expected = key == "coPollutant"   ? DataObjectRef::Pollutant
                                  : key == "evapPattern" ? DataObjectRef::Pattern
                                  : key == "removalSubcatch" ? DataObjectRef::Subcatchment
                                                         : DataObjectRef::AnyCurve;
            if ((referenceCategory(key) == DC::NumDataCategories && key != "removalSubcatch") || ref.layer != layer ||
                ref.engine != layer->engine() || ref.kind != expected)
                return fail(tr("The reference belongs to another project or object kind."));
            value = ref.currentName;
        }
        if (key == "removalSubcatch" && !value.toString().isEmpty()) {
            const int index = swmm_subcatch_index(layer->engine(), value.toString().toUtf8().constData());
            if (index >= 0) value = QString::fromUtf8(swmm_subcatch_id(layer->engine(), index));
        }
        if (!value.convert(before.value(key).metaType()))
            return fail(tr("Invalid value for %1.").arg(f.spec.label));
        if (f.spec.editor == EditorKind::Numeric || f.spec.editor == EditorKind::Integer) {
            bool ok = false;
            const double number = input.toDouble(&ok);
            if (!ok || !std::isfinite(number) || number < f.spec.minValue ||
                number > f.spec.maxValue || (f.spec.editor == EditorKind::Integer && number != value.toLongLong()))
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
        if (cat == DC::DataLIDControls) {
            if (key.endsWith("/expression") && (value.toString().contains(QChar::Null) || value.toString().contains('\n') || value.toString().contains('\r')))
                return fail(tr("A treatment expression must fit on one input line."));
            if (key == "type" && (before["type"].toInt() == 8 || value.toInt() == 8))
                return fail(tr("Create a new layered control to change between standard and NODE designs."));
            if (key == "flowModel" && value.toInt() == 0) {
                after["flowCells"] = 8; after["flowAtol"] = 1.e-7;
                after["flowRtol"] = 1.e-5; after["flowMaxStep"] = 30.;
            }
            if (key.contains("/t/") && key.endsWith("/present")) {
                const auto prefix = key.left(key.lastIndexOf('/') + 1);
                if (!value.toInt()) { after[prefix + "removal"] = 0.; after[prefix + "decay"] = 0.; after[prefix + "expression"] = QString(); }
            }
        }
        if (cat == DC::DataSnowpacks) {
            if (snowSiteKeys.contains(key) && before["modelKind"].toInt() < 0) after["modelKind"] = 1;
            if (key == "modelKind" && value.toInt() < 0) {
                const double defaults[]{0, 1, 0, .1, 0};
                for (int i = 0; i < 5; ++i) after[snowSiteKeys[i]] = defaults[i];
            }
            after["effectiveModel"] = after["modelKind"].toInt() < 0 ? snowDefault(layer->engine()) : after["modelKind"].toInt();
        }
        if (!memberValid(layer, cat, key, after, nameOf(provider, cat))) {
            const auto reason = cat == DC::DataLIDControls ? tr("A complete valid layer is required: porosity > field capacity > wilting point, positive storage void ratio, and valid nonnegative layer values.")
                : cat == DC::DataSnowpacks ? tr("Check melt minimum/maximum, fractions totalling at most one, a destination for transferred snow, and positive UEB extinction depth.")
                                         : tr("Left stations must not exceed right stations; descriptions cannot contain null characters.");
            return fail(tr("%1: %2").arg(nameOf(provider, cat), reason));
        }
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
    QVariant beforeRevision, afterRevision;
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
          targets(std::move(targets)), units(metric(layer)), snowModel(snowDefault(engine)), subcatchCount(swmm_subcatch_count(engine))
    {
        setText(tr("Edit %1 on %2 data objects").arg(this->key).arg(this->targets.size()));
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &context,
                         [this] { valid = false; });
        for (auto &target : this->targets) {
            target.beforeRevision = target.provider->property("lidStructureRevision");
            target.afterRevision = category == DC::DataLIDControls && this->key == "_nodeProfile"
                ? QVariant(QUuid::createUuid().toString()) : target.beforeRevision;
            QObject::connect(target.provider, &QObject::destroyed, &context,
                             [this] { valid = false; });
            if (category == DC::DataLIDControls)
                for (const auto &object : target.before["_pollutants"].toList()) {
                    auto *p = object.value<QObject *>();
                    if (!p) valid = false;
                    else QObject::connect(p, &QObject::destroyed, &context, [this] { valid = false; });
                }
            if (category == DC::DataSnowpacks &&
                (this->key == "removalSubcatch" || QStringList{snowKey(21), snowKey(22), snowKey(23), snowKey(24), snowKey(25), snowKey(26)}.contains(this->key))) {
                for (const auto &record : {target.before, target.after}) {
                    const auto name = record["removalSubcatch"].toString();
                    if (!name.isEmpty() && swmm_subcatch_index(engine, name.toUtf8().constData()) >= 0)
                        spatialReferences.append({name, name});
                }
            }
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
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, &context,
            [this](const QString &from, const QString &to, int kind) {
                if (!(kind & DC::kKindCatch)) return;
                for (auto &ref : spatialReferences)
                    if (ref.second.compare(from, Qt::CaseInsensitive) == 0) ref.second = to;
            });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &context, [this] {
            if (!valid || !this->layer || this->layer->engine() != engine) return;
            const int count = swmm_subcatch_count(engine);
            if (count < subcatchCount)
                for (const auto &ref : spatialReferences)
                    if (swmm_subcatch_index(engine, ref.second.toUtf8().constData()) < 0) valid = false;
            subcatchCount = count;
        });
    }
    bool fullRecord() const
    {
        return (category == DC::DataInlets && (key == "type" || key == "curveId")) ||
            (category == DC::DataLIDControls && key == "_nodeProfile");
    }
    QStringList ownedKeys(const TargetEdit &target) const
    {
        if (fullRecord()) {
            auto keys = target.before.keys();
            for (const auto &member : target.after.keys()) if (!keys.contains(member)) keys << member;
            return keys;
        }
        if (category == DC::DataLIDControls) {
            if (key == "flowModel") return lidFlowKeys;
            if (key.contains("/t/") && key.endsWith("/present")) {
                QStringList result; const auto prefix = key.left(key.lastIndexOf('/') + 1);
                for (const auto &member : lidTreatmentMembers) result << prefix + member;
                return result;
            }
        }
        if (category == DC::DataSnowpacks && (key == "modelKind" || target.before["modelKind"] != target.after["modelKind"]))
            return snowSiteKeys + QStringList{"modelKind", "effectiveModel"};
        return {key};
    }
    QVariantMap resolveReferences(QVariantMap record) const
    {
        for (auto it = record.begin(); it != record.end(); ++it) {
            const auto original = it.value().toString();
            if (it.key() == "removalSubcatch")
                for (const auto &ref : spatialReferences)
                    if (original == ref.first) { it.value() = ref.second; break; }
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
            metric(layer) != units || (category == DC::DataSnowpacks && snowDefault(engine) != snowModel))
            return false;
        for (const auto &r : references)
            if (!r.provider ||
                referenceType(engine, r.category, nameOf(r.provider, r.category)) != r.type)
                return false;
        for (const auto &ref : spatialReferences)
            if (swmm_subcatch_index(engine, ref.second.toUtf8().constData()) < 0) return false;
        QList<QVariantMap> previous, next;
        for (const auto &target : targets) {
            if (!target.provider || findProvider(registryFor(layer, category), category,
                    nameOf(target.provider, category)) != target.provider) return false;
            const auto current = readRecord(layer, category, target.provider);
            const auto expected = resolveReferences(forward ? target.before : target.after);
            const auto desired = resolveReferences(forward ? target.after : target.before);
            if (current.isEmpty()) return false;
            if (category == DC::DataLIDControls) {
                if (target.provider->property("lidStructureRevision") != (forward ? target.beforeRevision : target.afterRevision)) return false;
                if (current["_pollutants"] != expected["_pollutants"] || current["_species"] != expected["_species"] ||
                    current["_kinds"] != expected["_kinds"]) return false;
                for (auto *p : lidPollutants(layer))
                    if (current["_unit/" + lidPollToken(p)] != expected["_unit/" + lidPollToken(p)]) return false;
                if (key.contains("/t/") && !key.endsWith("/present")) {
                    const auto prefix = key.left(key.lastIndexOf('/') + 1);
                    if (current[prefix + "present"] != expected[prefix + "present"]) return false;
                }
            }
            const auto members = ownedKeys(target);
            for (const auto &member : members)
                if (current.value(member) != expected.value(member)) return false;
            const bool changedKind =
                (category == DC::DataPollutants && current.value("units") != expected.value("units")) ||
                ((category == DC::DataInlets || category == DC::DataPatterns || category == DC::DataLIDControls) && current.value("type") != expected.value("type")) ||
                (category == DC::DataSnowpacks && key != "modelKind" && (current.value("modelKind") != expected.value("modelKind") || current.value("effectiveModel") != expected.value("effectiveModel"))) ||
                (category == DC::DataInlets && (key == "openArea" || key == "splashVeloc") && current.value("grateType") != expected.value("grateType"));
            if (!fullRecord() && changedKind)
                return false;
            auto updated = current;
            for (const auto &member : members) {
                if (desired.contains(member)) updated[member] = desired[member]; else updated.remove(member);
            }
            if (!memberValid(layer, category, key, updated, nameOf(target.provider, category)))
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
        if (category == DC::DataLIDControls && key == "_nodeProfile")
            for (const auto &target : targets) {
                // Kind/occurrence alone cannot detect two equal-kind rows exchanging places.
                // Invalidate pending editors after every explicit complete-profile action.
                target.provider->setProperty("lidStructureRevision", forward ? target.afterRevision : target.beforeRevision);
                const QSignalBlocker p(target.provider), r(registryFor(layer, category));
                refreshFromEngine(registryFor(layer, category), category, layer, nameOf(target.provider, category));
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
        if (category == DC::DataLIDControls) {
            QSet<int> controls, assignedNodes;
            for (const auto &target : targets) controls.insert(nativeIndex(engine, category, nameOf(target.provider, category)));
            for (int i = 0; i < swmm_node_count(engine); ++i) {
                const auto name = QString::fromUtf8(swmm_node_id(engine, i));
                SWMM_StorageLidState state{};
                if (swmm_storage_lid_get(engine, swmm_node_index(engine, name.toUtf8().constData()), &state) == SWMM_OK && controls.contains(state.control))
                    { assignedNodes.insert(i); emit layer->attributeChanged(name); }
            }
            for (int i = 0; i < swmm_link_count(engine); ++i) {
                const auto name = QString::fromUtf8(swmm_link_id(engine, i));
                SWMM_StorageLidAnchor anchor{};
                int from = -1, to = -1;
                swmm_link_get_from_node(engine, i, &from); swmm_link_get_to_node(engine, i, &to);
                if ((assignedNodes.contains(from) || assignedNodes.contains(to)) &&
                    swmm_storage_lid_anchor_get(engine, i, &anchor) == SWMM_OK && anchor.layer > 0)
                    emit layer->attributeChanged(name);
            }
        }
        layer->markEdited();
        if (category == DC::DataLIDControls) emit layer->repaintRequested();
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
    QVector<QPair<QString, QString>> spatialReferences; // original/current typed subcatchment IDs
    bool units, valid = true, first = true;
    int snowModel, subcatchCount;
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
    if (key == "_nodeProfile" && names.size() != 1)
        return fail(tr("Layer structure changes require one control."));
    auto *registry = registryFor(layer, category);
    if (!registry)
        return fail(tr("This data category has no shared field writer."));
    const auto fields = fieldsFor(layer, category);
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
        if (category == DC::DataLIDControls && static_cast<LidControlProvider *>(provider)->dirty())
            return fail(tr("%1 has a design draft in its LID editor. Finish that draft before editing its shared properties.").arg(name));
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
        if (!propose(layer, category, provider, key, value, before, after, error, fields))
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
    case DC::DataLIDControls:
    case DC::DataSnowpacks:
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
        connect(m_layer, &SWMMModelLayer::optionsChanged, this, [this](const QStringList &keys) {
            if (keys.contains("FLOW_UNITS")) emit headerDataChanged(Qt::Horizontal, 0, columnCount()-1);
            if (m_category == DC::DataSnowpacks) {
                for (int row = 0; row < rowCount(); ++row) refreshObject(objectNameAt(row));
                reload();
            }
        });
        if (cat == DC::DataSnowpacks)
            connect(m_layer, &SWMMModelLayer::geometryChanged, this, [this] {
                for (int row = 0; row < rowCount(); ++row) refreshObject(objectNameAt(row));
                reload();
            }, Qt::QueuedConnection);
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
    case DC::DataLIDControls:
        return SWMMObjectRef::LIDControl;
    case DC::DataSnowpacks:
        return SWMMObjectRef::Snowpack;
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

int DataObjectAttributeTableModel::rowForProvider(QObject *provider) const
{
    const int row = m_providerRows.value(provider, -1);
    // A registry can remove/recreate a name before its queued table reset runs.
    return row >= 0 && provider && m_registry &&
        findProvider(m_registry, m_category, nameOf(provider, m_category)) == provider ? row : -1;
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

QVariantMap DataObjectAttributeTableModel::lidProfile(SWMMModelLayer *layer, const QString &name)
{
    return editable(layer) ? lidRecord(layer, name) : QVariantMap{};
}

bool DataObjectAttributeTableModel::editLidProfile(SWMMModelLayer *layer, const QString &name,
                                                  const QString &operation, int row, int kind,
                                                  QUndoStack *undo, QString *error)
{
    const auto before = lidProfile(layer, name);
    if (before.isEmpty() || before["type"].toInt() != 8) return false;
    auto after = before;
    QVariantList kinds = before["_kinds"].toList();
    QList<QVariantMap> rows; QHash<int, int> counts;
    for (const auto &k : kinds) {
        const auto prefix = lidNodePrefix(k.toInt(), ++counts[k.toInt()]); QVariantMap values;
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            if (it.key().startsWith(prefix)) values[it.key().mid(prefix.size())] = it.value();
        rows << values;
    }
    auto starter = [&](int k) {
        const double depth = metric(layer) ? 25.4 : 1.;
        const double rate = metric(layer) ? 25.4 : 1.;
        const QList<QVariantList> values{{depth, .2}, {10*depth, .5, .25, .1, .2*rate, 10., 2*depth}, {6*depth, .4, .2*rate}, {0., 0.}};
        QVariantMap valuesByKey;
        for (int i = 0; i < values[k].size(); ++i) valuesByKey["p/" + QString::number(i)] = values[k][i];
        if (k == 1 || k == 2) {
            const double richardsDefaults[]{.02, 1., 1.6, .5, 1.e-4};
            for (int i = 0; i < 5; ++i)
                valuesByKey["r/" + QString::number(i)] = before["flowModel"].toInt() == 1 ? richardsDefaults[i] : (i == 3 ? .5 : 0.);
        }
        if (k != 3) for (auto *p : lidPollutants(layer)) {
            const auto key = "t/" + lidPollToken(p) + "/";
            valuesByKey[key + "present"] = 0; valuesByKey[key + "removal"] = 0.;
            valuesByKey[key + "decay"] = 0.; valuesByKey[key + "expression"] = QString();
        }
        return valuesByKey;
    };
    if (operation == "initialize" && kinds.isEmpty()) {
        for (int k = 0; k < 4; ++k) { kinds << k; rows << starter(k); }
    } else if (operation == "materials") {
        for (int i = 0; i < rows.size(); ++i) if (kinds[i].toInt() == 1 || kinds[i].toInt() == 2) {
            const double values[]{.02, 1., 1.6, .5, 1.e-4};
            for (int j = 0; j < 5; ++j) rows[i]["r/" + QString::number(j)] = values[j];
        }
    } else if (operation == "insert" && kind >= 0 && kind <= 3 && row >= 0 && row <= rows.size()) {
        kinds.insert(row, kind); rows.insert(row, starter(kind));
    } else if (operation == "remove" && row >= 0 && row < rows.size()) {
        kinds.removeAt(row); rows.removeAt(row);
    } else if ((operation == "up" || operation == "down") && row >= 0 && row < rows.size()) {
        const int other = row + (operation == "up" ? -1 : 1);
        if (other < 0 || other >= rows.size()) return false;
        kinds.swapItemsAt(row, other); rows.swapItemsAt(row, other);
    } else return false;
    for (auto it = after.begin(); it != after.end();) {
        if (it.key().startsWith("node/")) it = after.erase(it); else ++it;
    }
    counts.clear(); after["_kinds"] = kinds;
    for (int i = 0; i < kinds.size(); ++i) {
        const int k = kinds[i].toInt(); const auto prefix = lidNodePrefix(k, ++counts[k]);
        for (auto it = rows[i].cbegin(); it != rows[i].cend(); ++it) after[prefix + it.key()] = it.value();
    }
    return applyValues(layer, DC::DataLIDControls, {name}, "_nodeProfile",
                       QVariantMap{{"before", before}, {"after", after}}, undo, error);
}

bool DataObjectAttributeTableModel::initializeLidDesign(SWMMModelLayer *layer, const QString &name)
{
    auto *reg = qobject_cast<LidControlRegistry *>(registryFor(layer, DC::DataLIDControls));
    auto *p = reg ? reg->findByName(name) : nullptr;
    auto record = lidProfile(layer, name);
    if (!p || record.isEmpty()) return false;
    p->clearDirty();
    if (record["type"].toInt() == 8) return editLidProfile(layer, name, "initialize", 0, 0, nullptr);
    for (const auto &keys : lidKeys) for (const auto &key : keys) if (record[key].toDouble() != 0) return false;
    const double d = metric(layer) ? 25.4 : 1.;
    const QList<QVariantList> groups{{d, .1, 1.}, {10*d, .5, .25, .1, .2*d, 10.}, {6*d, .7, .1*d}, {.4, .5, d}, {5*d, .4, .1, .2*d, 0., 7.}, {d, .4, .1}};
    for (int g = 0; g < 6; ++g) for (int i = 0; i < lidKeys[g].size(); ++i) record[lidKeys[g][i]] = groups[g][i];
    if (!writeRecord(layer, DC::DataLIDControls, p, record)) return false;
    { const QSignalBlocker dirtyBridge(layer); notifyProvider(reg, DC::DataLIDControls, p); }
    emit layer->attributeChanged(name); layer->markEdited();
    return true;
}

DC::DataCategory DataObjectAttributeTableModel::categoryForType(SWMMObjectRef::ObjectType type)
{
    switch (type) {
    case SWMMObjectRef::LIDControl:
        return DC::DataLIDControls;
    case SWMMObjectRef::Snowpack:
        return DC::DataSnowpacks;
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
    case DC::DataLIDControls:
        hook(static_cast<LidControlRegistry *>(m_registry.data()));
        break;
    case DC::DataSnowpacks:
        hook(static_cast<SnowpackRegistry *>(m_registry.data()));
        break;
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
        if (m_category == DC::DataLIDControls && !m_lidSchemaPending) {
            m_lidSchemaPending = true;
            QMetaObject::invokeMethod(this, [this] {
                m_lidSchemaPending = false;
                if (m_category != DC::DataLIDControls) return;
                const auto fields = fieldsFor(m_layer, m_category);
                bool changed = fields.size() != m_fields.size();
                for (int i = 0; !changed && i < fields.size(); ++i)
                    changed = fields[i].spec.key != m_fields[i].spec.key || fields[i].spec.label != m_fields[i].spec.label;
                if (changed) reload();
            }, Qt::QueuedConnection);
        }
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
             f.spec.key == QLatin1String("removalSubcatch") ||
             f.spec.key == QLatin1String("curveId")) &&
            v.toString().isEmpty())
            return {};
        return f.spec.editor == EditorKind::Enum ? QVariant(enumLabel(f.spec, v)) : v;
    }
    case Qt::EditRole:
        return applies && f.get ? f.get(p) : QVariant();
    case Qt::ToolTipRole:
        if (m_category == DC::DataLIDControls && static_cast<LidControlProvider *>(p)->dirty())
            return tr("Finish the design draft in the LID editor before editing shared properties.");
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
        rowForProvider(m_rows[index.row()]) != index.row() || !editable(m_layer))
        return fl;
    if (m_category == DC::DataLIDControls && (static_cast<LidControlProvider *>(m_rows[index.row()].data())->dirty() ||
        (f.spec.key == "type" && static_cast<LidControlProvider *>(m_rows[index.row()].data())->type() == 8)))
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
