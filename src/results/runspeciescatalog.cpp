/**
 * @file      runspeciescatalog.cpp
 * @brief     V0 — the single list of species carried by one loaded run.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "results/runspeciescatalog.h"

#include "layers/speciesattributes.h"

#include <openswmm/engine/openswmm_reactions.h>

#include <QHash>
#include <QObject>

#include <algorithm>

namespace openswmmvis::results
{

namespace
{
using Kind = RunSpecies::Kind;

Kind reservedKind(const QString &name, Kind fallback)
{
    if (name == QLatin1String(OpenSWMMVis::Species::kWaterAgeName))
        return Kind::WaterAge;
    if (name == QLatin1String(OpenSWMMVis::Species::kTemperatureName))
        return Kind::Temperature;
    return fallback;
}
} // namespace

QString RunSpecies::kindBadge() const
{
    switch (kind) {
    case Kind::Pollutant:
        return QObject::tr("pollutant");
    case Kind::WaterAge:
        return QObject::tr("water age");
    case Kind::Temperature:
        return QObject::tr("temperature");
    case Kind::ReactionBulk:
        return QObject::tr("MSX bulk");
    case Kind::ReactionWall:
        return QObject::tr("MSX wall");
    case Kind::Unclassified:
        break;
    }
    return QObject::tr("species");
}

QString RunSpecies::displayLabel() const
{
    return OpenSWMMVis::Species::speciesDisplayLabel(name);
}

QString RunSpeciesCatalog::unitsForCode(int code)
{
    switch (code) {
    case 0:
        return QStringLiteral("mg/L");
    case 1:
        return QStringLiteral("ug/L");
    case 2:
        return QStringLiteral("count/L");
    default:
        return {};
    }
}

QString RunSpeciesCatalog::normalizedUnits(const QString &units)
{
    const QString u = units.trimmed();
    const QString upper = u.toUpper();
    if (upper == QLatin1String("MG/L"))
        return QStringLiteral("mg/L");
    if (upper == QLatin1String("UG/L"))
        return QStringLiteral("ug/L");
    if (upper == QLatin1String("COUNT/L") || upper == QLatin1String("COUNTS/L") ||
        upper == QLatin1String("#/L"))
        return QStringLiteral("count/L");
    return u;
}

RunSpeciesCatalog RunSpeciesCatalog::build(const QVector<OutputSpecies> &reported,
                                           const QVector<io::Mesh2DResultVariable> &meshVariables,
                                           const QVector<DeclaredSpecies> &declared, bool has2D)
{
    RunSpeciesCatalog catalog;
    catalog.m_has2D = has2D || std::any_of(meshVariables.begin(), meshVariables.end(),
                                           [](const auto &v) { return !v.species.isEmpty(); });
    QHash<QString, DeclaredSpecies> declarations;
    for (const DeclaredSpecies &d : declared)
        if (!d.name.isEmpty())
            declarations.insert(d.name, d);
    QHash<QString, int> row; // name -> entry index

    // 1D: the .out columns, in file order. The column index IS the position
    // in this list (empty names keep their slot so later columns stay right).
    for (int column = 0; column < reported.size(); ++column) {
        const OutputSpecies &o = reported[column];
        if (o.name.isEmpty() || row.contains(o.name))
            continue;
        RunSpecies s;
        s.name = o.name;
        s.reportedIndex = column;
        s.concentrationCode = o.unitsCode;
        const auto decl = declarations.constFind(o.name);
        s.kind = reservedKind(o.name, decl != declarations.constEnd() ? decl->kind
                                                                       : Kind::Pollutant);
        // The reserved pair reuse the concentration-unit field: hours / deg C.
        const QString codeUnits = unitsForCode(o.unitsCode);
        s.units = OpenSWMMVis::Species::isReservedSpecies(o.name)
                      ? OpenSWMMVis::Species::speciesUnitLabel(o.name, codeUnits)
                      : codeUnits;
        if (s.units.isEmpty() && decl != declarations.constEnd())
            s.units = normalizedUnits(decl->units);
        s.unitsKnown = !s.units.isEmpty();
        row.insert(s.name, int(catalog.m_entries.size()));
        catalog.m_entries.append(s);
    }

    // 2D: every species slice; a name already listed gains its keys.
    for (const io::Mesh2DResultVariable &v : meshVariables) {
        if (v.species.isEmpty())
            continue;
        int index = row.value(v.species, -1);
        if (index < 0) {
            RunSpecies s;
            s.name = v.species;
            const auto decl = declarations.constFind(v.species);
            s.kind = reservedKind(v.species, decl != declarations.constEnd()
                                                 ? decl->kind
                                                 : Kind::Unclassified);
            if (decl != declarations.constEnd())
                s.units = normalizedUnits(decl->units);
            index = int(catalog.m_entries.size());
            row.insert(s.name, index);
            catalog.m_entries.append(s);
        }
        RunSpecies &s = catalog.m_entries[index];
        const QString key = v.key();
        if (!s.meshKeys.contains(key))
            s.meshKeys.append(key);
        const bool surfaceConc = v.dataset == QLatin1String("Mesh2_face_species_conc");
        if (surfaceConc && s.meshKey.isEmpty())
            s.meshKey = key;
        // A concentration slice states the species' units when the .out did not.
        if (s.units.isEmpty() && v.unitsKnown &&
            v.dataset != QLatin1String("Mesh2_face_buildup"))
            s.units = normalizedUnits(v.units);
        s.unitsKnown = !s.units.isEmpty();
    }
    return catalog;
}

RunSpeciesCatalog RunSpeciesCatalog::fromOutput(
    SWMM_Output handle, const QVector<io::Mesh2DResultVariable> &meshVariables,
    const QVector<DeclaredSpecies> &declared, bool has2D)
{
    QVector<OutputSpecies> reported;
    const int n = handle ? swmm_output_get_pollut_count(handle) : 0;
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_output_get_pollut_id(handle, i);
        reported.append({id ? QString::fromUtf8(id) : QString(),
                         swmm_output_get_pollut_units(handle, i)});
    }
    return build(reported, meshVariables, declared, has2D);
}

QVector<RunSpeciesCatalog::DeclaredSpecies> RunSpeciesCatalog::declaredFromEngine(
    SWMM_Engine engine)
{
    QVector<DeclaredSpecies> out;
    const int n = engine ? swmm_reaction_species_count(engine) : 0;
    for (int i = 0; i < n; ++i) {
        char name[256]{};
        char units[64]{};
        int wall = 0;
        double atol = 0.0, rtol = 0.0;
        if (swmm_reaction_species_get(engine, i, name, int(sizeof name), &wall, units,
                                      int(sizeof units), &atol, &rtol) != 0 ||
            !name[0])
            continue;
        out.append({QString::fromUtf8(name),
                    wall ? RunSpecies::Kind::ReactionWall : RunSpecies::Kind::ReactionBulk,
                    QString::fromUtf8(units)});
    }
    return out;
}

const RunSpecies *RunSpeciesCatalog::find(const QString &name) const
{
    for (const RunSpecies &s : m_entries)
        if (s.name == name)
            return &s;
    return nullptr;
}

QStringList RunSpeciesCatalog::reportedNames() const
{
    QVector<const RunSpecies *> reported;
    for (const RunSpecies &s : m_entries)
        if (s.in1D())
            reported.append(&s);
    std::sort(reported.begin(), reported.end(),
              [](const RunSpecies *a, const RunSpecies *b) {
                  return a->reportedIndex < b->reportedIndex;
              });
    QStringList names;
    for (const RunSpecies *s : reported)
        names.append(s->name);
    return names;
}

QString RunSpeciesCatalog::availabilityBadge(const RunSpecies &s) const
{
    if (!s.in1D())
        return QObject::tr("2D only");
    if (m_has2D && !s.in2D())
        return QObject::tr("1D only");
    return {};
}

QString RunSpeciesCatalog::pickerLabel(const RunSpecies &s) const
{
    QString badge = s.kindBadge();
    const QString availability = availabilityBadge(s);
    if (!availability.isEmpty())
        badge += QStringLiteral(" · ") + availability;
    QString label = QStringLiteral("%1 [%2]").arg(s.displayLabel(), badge);
    if (s.unitsKnown)
        label += QStringLiteral(" (%1)").arg(s.units);
    return label;
}

} // namespace openswmmvis::results
