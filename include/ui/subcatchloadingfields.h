// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QObject>
#include <QString>
#include <QVector>

namespace subcatchloadings {
struct Field {
    QString name, units, signature, reason;
    bool species = false;
    int index = -1; // Current catalog only; captured fields resolve by typed name/signature.
};
inline bool si(SWMM_Engine engine) {
    char value[32]{};
    swmm_options_get(engine, "FLOW_UNITS", value, sizeof(value));
    const QByteArray units(value);
    return units == "CMS" || units == "LPS" || units == "MLD";
}
inline QVector<Field> fields(SWMM_Engine engine) {
    QVector<Field> result;
    if (!engine) return result;
    const bool metric = si(engine);
    const auto area = metric ? QStringLiteral("ha") : QStringLiteral("ac");
    const auto mass = metric ? QStringLiteral("kg") : QStringLiteral("lb");
    const int np = swmm_pollutant_count(engine);
    for (int p = 0; p < np; ++p) {
        const char *name = swmm_pollutant_id(engine, p); int units = -1;
        if (!name || swmm_pollutant_get_units(engine, p, &units) != SWMM_OK) continue;
        result.append({QString::fromUtf8(name), (units == 2 ? QStringLiteral("counts") : mass) + '/' + area,
                       QString("pollutant:%1:%2").arg(units).arg(metric), {}, false, p});
    }
    for (int m = 0; m < swmm_reaction_species_count(engine); ++m) {
        char name[1024]{}, units[1024]{}; int wall = 0; double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(engine, m, name, sizeof(name), &wall, units, sizeof(units), &atol, &rtol) != SWMM_OK) continue;
        const auto declared = QString::fromUtf8(units);
        const auto upper = declared.toUpper();
        QString reason;
        if (wall) reason = QObject::tr("Wall species cannot build up on a subcatchment surface.");
        else if (swmm_pollutant_index(engine, name) >= 0)
            reason = QObject::tr("A pollutant owns this loading name. Give the pollutant and reaction species different names before assigning surface buildup.");
        result.append({QString::fromUtf8(name), (upper.startsWith("MG") || upper.startsWith("UG") ? mass : declared) + '/' + area,
                       QString("species:%1:%2:%3").arg(wall).arg(declared).arg(metric), reason, true, np + m});
    }
    return result;
}
inline int resolve(SWMM_Engine engine, const Field &captured) {
    if (!captured.reason.isEmpty()) return -1;
    for (const auto &field : fields(engine))
        if (field.species == captured.species && field.name == captured.name &&
            field.signature == captured.signature && field.reason.isEmpty()) return field.index;
    return -1;
}
inline QString summary(SWMM_Engine engine, int subcatch) {
    int assigned = 0;
    for (const auto &field : fields(engine)) {
        double value = 0;
        if (field.reason.isEmpty() && swmm_subcatch_get_initial_loading(engine, subcatch, field.index, &value) == SWMM_OK && value > 0) ++assigned;
    }
    return assigned ? QObject::tr("%1 constituent(s)").arg(assigned) : QObject::tr("(none)");
}
}
