// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Caleb Buahin. All rights reserved.
// Private helpers included by dataobjectattributetablemodel.cpp inside its namespace.
// Provider tokens are process-local identities; they are never persisted to model files.
QString lidPollToken(QObject *p) { return QString::number(reinterpret_cast<quintptr>(p), 16); }
QString lidSpeciesKey(const QString &name) { return "removal/s/" + QString::fromLatin1(name.toUtf8().toPercentEncoding()); }
QString lidNodePrefix(int kind, int occurrence) { return QString("node/%1/%2/").arg(kind).arg(occurrence); }
const QStringList lidFlowKeys{"flowModel", "flowCells", "flowAtol", "flowRtol", "flowMaxStep"};
const QStringList lidTreatmentMembers{"present", "removal", "decay", "expression"};
const int lidNodeParamCounts[]{2, 7, 3, 2};

QList<PollutantProvider *> lidPollutants(SWMMModelLayer *layer)
{
    QList<PollutantProvider *> result;
    if (auto *reg = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()))
        for (auto *p : reg->providers()) result << p;
    return result;
}
bool readLidConstituents(SWMMModelLayer *layer, int control, QVariantMap &record)
{
    const auto e = layer->engine();
    QVariantList objects;
    for (auto *p : lidPollutants(layer)) {
        const auto token = lidPollToken(p);
        const int index = swmm_pollutant_index(e, p->name().toUtf8().constData());
        double value = 0; int units = -1;
        if (index < 0 || swmm_lid_get_removal(e, control, index, &value) != SWMM_OK ||
            swmm_pollutant_get_units(e, index, &units) != SWMM_OK) return false;
        record["removal/p/" + token] = value;
        record["_unit/" + token] = units;
        objects << QVariant::fromValue<QObject *>(p);
    }
    record["_pollutants"] = objects;
    QVariantList species;
    const int np = swmm_pollutant_count(e);
    for (int i = 0; i < swmm_reaction_species_count(e); ++i) {
        char name[4096]{}, units[4096]{}; int wall = 0; double a = 0, b = 0, value = 0;
        if (swmm_reaction_species_get(e, i, name, sizeof(name), &wall, units, sizeof(units), &a, &b) != SWMM_OK ||
            swmm_lid_get_removal(e, control, np + i, &value) != SWMM_OK) return false;
        species << QVariant(QVariantList{QString::fromUtf8(name), wall, QString::fromUtf8(units), a, b});
        record[lidSpeciesKey(QString::fromUtf8(name))] = value;
    }
    record["_species"] = species;
    return true;
}
bool writeLidRemovals(SWMMModelLayer *layer, int control, const QVariantMap &before, const QVariantMap &after)
{
    const auto e = layer->engine();
    for (auto *p : lidPollutants(layer)) {
        const auto key = "removal/p/" + lidPollToken(p);
        if (before[key] != after[key] && swmm_lid_set_removal(e, control,
            swmm_pollutant_index(e, p->name().toUtf8().constData()), after[key].toDouble()) != SWMM_OK) return false;
    }
    const auto species = after["_species"].toList();
    for (int i = 0; i < species.size(); ++i) {
        const auto key = lidSpeciesKey(species[i].toList().value(0).toString());
        if (before[key] != after[key] && swmm_lid_set_removal(e, control, swmm_pollutant_count(e) + i, after[key].toDouble()) != SWMM_OK) return false;
    }
    return true;
}
bool readLidNode(SWMMModelLayer *layer, int control, QVariantMap &record)
{
    const auto e = layer->engine();
    QVariantList kinds; QHash<int, int> occurrences; QStringList prefixes;
    const auto pollutants = lidPollutants(layer);
    for (int row = 0; row < swmm_lid_node_layer_count(e, control); ++row) {
        SWMM_LidNodeLayer value{}; SWMM_LidRichardsMaterial material{};
        if (swmm_lid_node_layer_get(e, control, row, &value) != SWMM_OK ||
            swmm_lid_richards_material_get(e, control, row, &material) != SWMM_OK) return false;
        kinds << value.kind;
        const auto prefix = lidNodePrefix(value.kind, ++occurrences[value.kind]); prefixes << prefix;
        for (int i = 0; i < lidNodeParamCounts[value.kind]; ++i) record[prefix + "p/" + QString::number(i)] = value.params[i];
        if (value.kind == 1 || value.kind == 2) {
            const double values[]{material.theta_r, material.alpha, material.n, material.l, material.specific_storage};
            for (int i = 0; i < 5; ++i) record[prefix + "r/" + QString::number(i)] = values[i];
        }
        if (value.kind != 3)
            for (auto *p : pollutants) {
                const auto key = prefix + "t/" + lidPollToken(p) + "/";
                record[key + "present"] = 0; record[key + "removal"] = 0.; record[key + "decay"] = 0.; record[key + "expression"] = QString();
            }
    }
    record["_kinds"] = kinds;
    SWMM_LidRichardsOptions options{};
    if (swmm_lid_richards_options_get(e, control, &options) != SWMM_OK) return false;
    const QVariantList values{options.model, options.cells_per_layer, options.atol, options.rtol, options.max_step};
    for (int i = 0; i < 5; ++i) record[lidFlowKeys[i]] = values[i];
    for (int row = 0; row < swmm_lid_node_treatment_count(e, control); ++row) {
        SWMM_LidLayerTreatment value{};
        if (swmm_lid_node_treatment_get(e, control, row, &value) != SWMM_OK || value.layer < 1 || value.layer > prefixes.size()) return false;
        auto *registry = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
        auto *p = registry ? registry->findByName(QString::fromUtf8(swmm_pollutant_id(e, value.pollutant))) : nullptr;
        if (!p) return false;
        const auto key = prefixes[value.layer - 1] + "t/" + lidPollToken(p) + "/";
        record[key + "present"] = 1; record[key + "removal"] = value.removal_percent;
        record[key + "decay"] = value.decay_per_day; record[key + "expression"] = QString::fromUtf8(value.expression ? value.expression : "");
    }
    return true;
}
bool lidMaterialValid(const QVariantMap &record, const QString &prefix)
{
    for (int i = 0; i < 5; ++i)
        if (!std::isfinite(record[prefix + "r/" + QString::number(i)].toDouble())) return false;
    const double porosity = record[prefix + "p/1"].toDouble();
    const double theta = record[prefix + "r/0"].toDouble();
    const int kind = prefix.split('/').value(1).toInt();
    return theta >= 0 && theta < porosity && (kind != 1 || theta < record[prefix + "p/3"].toDouble()) &&
        record[prefix + "r/1"].toDouble() > 0 && record[prefix + "r/2"].toDouble() > 1 &&
        record[prefix + "r/3"].toDouble() >= 0 && record[prefix + "r/4"].toDouble() > 0;
}
bool writeLidNode(SWMMModelLayer *layer, int control, const QVariantMap &record, bool validateOnly)
{
    const auto kinds = record["_kinds"].toList();
    QVector<SWMM_LidNodeLayer> rows; QVector<SWMM_LidRichardsMaterial> materials;
    QVector<SWMM_LidLayerTreatment> treatments; QVector<QByteArray> expressions;
    QHash<int, int> occurrences;
    const auto pollutants = lidPollutants(layer);
    // Reserve expression storage before publishing its borrowed pointers.
    expressions.reserve(kinds.size() * pollutants.size());
    for (const auto &kind : kinds) {
        SWMM_LidNodeLayer row{}; row.kind = kind.toInt(); if (row.kind < 0 || row.kind > 3) return false;
        const auto prefix = lidNodePrefix(row.kind, ++occurrences[row.kind]);
        for (int i = 0; i < lidNodeParamCounts[row.kind]; ++i) row.params[i] = record[prefix + "p/" + QString::number(i)].toDouble();
        rows << row;
        SWMM_LidRichardsMaterial material{};
        material = {record[prefix + "r/0"].toDouble(), record[prefix + "r/1"].toDouble(), record[prefix + "r/2"].toDouble(), record[prefix + "r/3"].toDouble(), record[prefix + "r/4"].toDouble()};
        if (row.kind == 1 || row.kind == 2) {
            bool configured = false;
            for (int i = 0; i < 5; ++i) configured |= record[prefix + "r/" + QString::number(i)].toDouble() != (i == 3 ? .5 : 0.);
            if (configured && !lidMaterialValid(record, prefix)) return false;
        }
        materials << material;
        if (row.kind == 3) continue;
        for (auto *p : pollutants) {
            const auto key = prefix + "t/" + lidPollToken(p) + "/";
            if (!record[key + "present"].toInt()) continue;
            expressions << record[key + "expression"].toString().toUtf8();
            treatments << SWMM_LidLayerTreatment{int(rows.size()), swmm_pollutant_index(layer->engine(), p->name().toUtf8().constData()),
                record[key + "removal"].toDouble(), record[key + "decay"].toDouble(), expressions.last().constData()};
        }
    }
    const SWMM_LidRichardsOptions options{record["flowModel"].toInt(), record["flowCells"].toInt(), record["flowAtol"].toDouble(), record["flowRtol"].toDouble(), record["flowMaxStep"].toDouble()};
    const auto call = validateOnly ? swmm_lid_node_validate_flow : swmm_lid_node_configure_flow;
    return call(layer->engine(), control, rows.constData(), rows.size(), treatments.constData(), treatments.size(), &options, materials.constData()) == SWMM_OK;
}
