/**
 * @file      mesh2dlivevariables.cpp
 * @brief     Worker-owned snapshots of live 2D simulation variables.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dlivevariables.h"
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_snow2d.h>
#include <openswmm/engine/openswmm_sq2d.h>
#include <openswmm/plugin_sdk/SnowResults.hpp>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::io
{
namespace
{
// Declared units of the engine's species, matched by name: the groundwater and
// surface rows need not follow the authoring order. An ambiguous name has
// unknown units instead of a guessed winner.
struct SpeciesUnits {
    QHash<QString, QString> units;
    QSet<QString> ambiguous;
    QSet<QString> massPollutants; // MG/L or UG/L: buildup is mass per area
    /**
     * @brief Declared unit of one species.
     * @param[in] species Runtime species name.
     * @return Unit text, or empty when unknown or ambiguous.
     */
    QString of(const QString &species) const
    {
        return ambiguous.contains(species) ? QString() : units.value(species);
    }
};
/**
 * @brief Collect pollutant, reaction-species, age and temperature units from the engine.
 * @param[in] engine Borrowed initialized engine; use only on its simulation worker.
 * @return Name-keyed units; age in seconds and temperature in degrees C (the live API units).
 */
SpeciesUnits speciesUnits(SWMM_Engine engine)
{
    SpeciesUnits known;
    const auto addUnit = [&](const QString &name, const QString &unit) {
        if (known.units.contains(name) && known.units.value(name) != unit)
            known.ambiguous.insert(name);
        else
            known.units.insert(name, unit);
    };
    for (int i = 0, n = swmm_pollutant_count(engine); i < n; ++i) {
        const char *name = swmm_pollutant_id(engine, i);
        int unit = -1;
        if (!name || swmm_pollutant_get_units(engine, i, &unit) != SWMM_OK)
            continue;
        addUnit(QString::fromUtf8(name), unit == 0   ? QStringLiteral("MG/L")
                                         : unit == 1 ? QStringLiteral("UG/L")
                                         : unit == 2 ? QStringLiteral("#/L")
                                                     : QString());
        if (unit == 0 || unit == 1)
            known.massPollutants.insert(QString::fromUtf8(name));
    }
    for (int i = 0, n = swmm_reaction_species_count(engine); i < n; ++i) {
        char name[4096]{}, unit[4096]{};
        int wall = 0;
        double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(engine, i, name, sizeof name, &wall, unit, sizeof unit, &atol,
                                      &rtol) == SWMM_OK &&
            !wall)
            addUnit(QString::fromUtf8(name), QString::fromUtf8(unit).trimmed());
    }
    // The live APIs return raw age in seconds (surface HDF's hours do not
    // apply), and raw temperature in degrees C, including negative values.
    addUnit(QStringLiteral("__WATER_AGE__"), QStringLiteral("s"));
    addUnit(QStringLiteral("__TEMPERATURE__"), QStringLiteral("degC"));
    return known;
}

/**
 * @brief Copy surface species concentrations and buildup (R3) into an owning snapshot.
 * @param[in] engine Borrowed initialized engine; use only on its simulation worker.
 * @param[in] cells Positive native surface cell count.
 * @return Snapshot with `Mesh2_face_species_conc` per transported surface species and
 * `Mesh2_face_buildup` per surface-quality species, under the saved file's identities;
 * null when the surface carries neither. A concentration is Waterless where the cell depth
 * is zero and Missing where the depth is unknown, as the saved reader masks it. Buildup is
 * mass per area (kg ha-1 SI, lb acre-1 US) for MG/L and UG/L pollutants; other units are
 * left unknown.
 */
Mesh2DLiveVariablesPtr captureSurfaceQualityVariables(SWMM_Engine engine, int cells)
{
    auto frame = std::make_shared<Mesh2DLiveVariables>();
    frame->cellCount = cells;
    const SpeciesUnits known = speciesUnits(engine);
    int count = 0, meshCells = 0;
    if (swmm_2d_species_count(engine, &count) == SWMM_OK && count > 0) {
        std::vector<double> depth(static_cast<size_t>(cells));
        // The bulk getter has no capacity argument: check the count first.
        const bool depthKnown = swmm_2d_cell_count(engine, &meshCells) == SWMM_OK &&
                                meshCells == cells &&
                                swmm_2d_get_depths_bulk(engine, depth.data()) == SWMM_OK;
        for (int row = 0; row < count; ++row) {
            char name[4096]{};
            if (swmm_2d_species_name(engine, row, name, sizeof name) != SWMM_OK)
                continue;
            Mesh2DLiveVariable v;
            auto &d = v.descriptor;
            d.dataset = QStringLiteral("Mesh2_face_species_conc");
            d.species = QString::fromUtf8(name);
            d.units = known.of(d.species);
            d.unitsKnown = !d.units.isEmpty();
            d.label = QStringLiteral("Surface — %1 — concentration").arg(d.species);
            std::vector<double> raw(size_t(cells), 0.0);
            int written = 0;
            if (d.species.isEmpty() ||
                swmm_2d_get_cell_conc(engine, row, raw.data(), cells, &written) != SWMM_OK ||
                written != cells) {
                frame->warnings.append(
                    QStringLiteral("Cannot capture surface concentration for %1.").arg(d.species));
                continue;
            }
            if (!d.unitsKnown)
                frame->warnings.append(
                    QStringLiteral("Surface units unresolved for %1; scientific export is "
                                   "unavailable.")
                        .arg(d.species));
            v.values.resize(size_t(cells));
            v.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
            for (size_t c = 0; c < size_t(cells); ++c) {
                v.values[c] = static_cast<float>(raw[c]);
                if (!std::isfinite(v.values[c]) || !depthKnown || !std::isfinite(depth[c]) ||
                    depth[c] < 0) {
                    v.values[c] = std::numeric_limits<float>::quiet_NaN();
                    v.status[c] = Mesh2DValueStatus::Missing;
                } else if (depth[c] == 0)
                    v.status[c] = Mesh2DValueStatus::Waterless;
            }
            frame->variables.append(std::move(v));
        }
    }
    // Buildup belongs to the pollutant and reaction species of the surface
    // quality store; the getter refuses other names and an inactive store.
    int flowUnits = -1;
    const bool si = swmm_get_flow_units(engine, &flowUnits) == SWMM_OK && flowUnits >= 3;
    QStringList names;
    for (int i = 0, n = swmm_pollutant_count(engine); i < n; ++i)
        if (const char *name = swmm_pollutant_id(engine, i))
            names.append(QString::fromUtf8(name));
    for (int i = 0, n = swmm_reaction_species_count(engine); i < n; ++i) {
        char name[4096]{}, unit[4096]{};
        int wall = 0;
        double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(engine, i, name, sizeof name, &wall, unit, sizeof unit, &atol,
                                      &rtol) == SWMM_OK &&
            !wall)
            names.append(QString::fromUtf8(name));
    }
    for (const QString &species : names) {
        std::vector<double> raw(size_t(cells), 0.0);
        const QByteArray utf8 = species.toUtf8();
        if (swmm_2d_get_buildup_bulk(engine, utf8.constData(), raw.data(), cells) != SWMM_OK)
            continue;
        Mesh2DLiveVariable v;
        auto &d = v.descriptor;
        d.dataset = QStringLiteral("Mesh2_face_buildup");
        d.species = species;
        d.label = QStringLiteral("Surface — %1 — buildup").arg(species);
        if (known.massPollutants.contains(species) && !known.ambiguous.contains(species))
            d.units = si ? QStringLiteral("kg ha-1") : QStringLiteral("lb acre-1");
        else
            frame->warnings.append(
                QStringLiteral("Surface buildup units unresolved for %1.").arg(species));
        d.unitsKnown = !d.units.isEmpty();
        v.values.resize(size_t(cells));
        v.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
        for (size_t c = 0; c < size_t(cells); ++c) {
            v.values[c] = static_cast<float>(raw[c]);
            if (!std::isfinite(v.values[c])) {
                v.values[c] = std::numeric_limits<float>::quiet_NaN();
                v.status[c] = Mesh2DValueStatus::Missing;
            }
        }
        frame->variables.append(std::move(v));
    }
    return frame->variables.isEmpty() ? Mesh2DLiveVariablesPtr{} : frame;
}
} // namespace

Mesh2DLiveVariablesPtr captureSurfaceProcessVariables(SWMM_Engine engine, int cells, QString *error)
{
    auto groundwater = captureGroundwaterVariables(engine, cells, error);
    if (!engine || cells <= 0)
        return groundwater;
    // R3: surface species and buildup join the same frame (below, snow too),
    // so one process never replaces another's variables on the results layer.
    if (auto quality = captureSurfaceQualityVariables(engine, cells)) {
        auto merged = groundwater ? std::make_shared<Mesh2DLiveVariables>(*groundwater)
                                  : std::make_shared<Mesh2DLiveVariables>();
        merged->cellCount = cells;
        merged->variables += quality->variables;
        merged->warnings += quality->warnings;
        groundwater = merged;
    }
    std::vector<SWMM_SnowState> state(cells);
    std::vector<SWMM_MeshSnowLedger> ledger(cells);
    if (swmm_snow2d_get_states(engine, 0, cells, state.data()) != SWMM_OK ||
        swmm_snow2d_get_ledgers(engine, 0, cells, ledger.data()) != SWMM_OK) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Cannot capture live snow state.");
        return groundwater;
    }
    if (std::none_of(state.begin(), state.end(),
                     [](const auto &s) { return s.model != SWMM_SNOW_DEFAULT; }))
        return groundwater;
    auto frame = groundwater ? std::make_shared<Mesh2DLiveVariables>(*groundwater)
                             : std::make_shared<Mesh2DLiveVariables>();
    frame->cellCount = cells;
    const auto offset = frame->variables.size();
    for (const auto &field : openswmm::meshSnowFields) {
        Mesh2DLiveVariable v;
        auto &d = v.descriptor;
        d.dataset = QString::fromLatin1(field.name);
        d.label = QStringLiteral("Surface — ") + QString::fromLatin1(field.label);
        d.units = QString::fromLatin1(field.units);
        d.unitsKnown = true;
        d.temporal = Mesh2DResultVariable::Temporal::Held;
        v.values.resize(cells);
        v.status.resize(cells, Mesh2DValueStatus::Valid);
        frame->variables.append(std::move(v));
    }
    for (int c = 0; c < cells; ++c) {
        const auto values = openswmm::meshSnowValues(state[c], ledger[c]);
        for (std::size_t k = 0; k < values.size(); ++k) {
            auto &v = frame->variables[offset + int(k)];
            v.values[c] = static_cast<float>(values[k]);
            if (!std::isfinite(v.values[c]))
                v.status[c] = Mesh2DValueStatus::Missing;
        }
    }
    return frame;
}
Mesh2DLiveVariablesPtr captureGroundwaterVariables(SWMM_Engine engine, int expectedCells,
                                                   QString *error)
{
    if (error)
        error->clear();
    const auto fail = [&](const QString &message) -> Mesh2DLiveVariablesPtr {
        if (error)
            *error = message;
        return {};
    };
    if (!engine || expectedCells <= 0)
        return fail(QStringLiteral("Invalid live groundwater source."));
    int active = 0;
    if (swmm_gw2d_is_active(engine, &active) != SWMM_OK)
        return fail(QStringLiteral("Cannot inspect live groundwater state."));
    if (!active)
        return {};
    int cells = 0, speciesCount = 0;
    if (swmm_gw2d_get_dimensions(engine, &cells, nullptr) != SWMM_OK || cells != expectedCells)
        return fail(QStringLiteral("Live groundwater dimensions do not match the results mesh."));
    auto frame = std::make_shared<Mesh2DLiveVariables>();
    frame->cellCount = cells;
    const auto read = [&](int variable, std::vector<double> &values) {
        values.resize(size_t(cells));
        int written = 0;
        return swmm_gw2d_get_cell_bulk(engine, variable, values.data(), cells, &written) ==
                   SWMM_OK &&
               written == cells;
    };
    std::vector<double> water[2], table;
    const bool waterKnown[2] = {read(SWMM_GW2D_VAR_HG, water[0]), read(SWMM_GW2D_VAR_HU, water[1])};
    const bool tableKnown = read(SWMM_GW2D_VAR_TABLE_EL, table);
    const auto elevation = [&](const QString &dataset, const QString &label, bool base) {
        Mesh2DLiveVariable v;
        auto &d = v.descriptor;
        d.dataset = dataset;
        d.label = label;
        d.units = QStringLiteral("m");
        d.unitsKnown = true;
        d.domain = Mesh2DResultVariable::Domain::Groundwater;
        if (base)
            d.temporal = Mesh2DResultVariable::Temporal::Static;
        v.values.resize(size_t(cells));
        v.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
        for (int cell = 0; cell < cells; ++cell) {
            const size_t c = size_t(cell);
            const float value = static_cast<float>(base ? table[c] - water[0][c] : table[c]);
            if (!std::isfinite(value) ||
                (base && (!std::isfinite(water[0][c]) || water[0][c] < 0))) {
                v.values[c] = std::numeric_limits<float>::quiet_NaN();
                v.status[c] = Mesh2DValueStatus::Missing;
            } else
                v.values[c] = value;
        }
        frame->variables.append(std::move(v));
    };
    if (tableKnown) {
        elevation(QStringLiteral("Mesh2_face_gw_table_elev"),
                  QStringLiteral("Groundwater — water table elevation"), false);
        // ApiGw2D::cellVar(TABLE_EL) = z_bed + hg. Subtract the HG API value
        // in double, before display storage, to recover the same SI datum.
        if (waterKnown[0])
            elevation(QStringLiteral("Mesh2_face_gw_bed_elev"),
                      QStringLiteral("Groundwater — aquifer base elevation"), true);
    } else
        frame->warnings.append(QStringLiteral("Live groundwater table elevation is unavailable."));
    // R3: the saturated thickness and the held per-cell fluxes, under the
    // saved file's names, labels and units (Default2DOutputPlugin), so a saved
    // style or plot finds the same variable live.
    const auto scalar = [&](const std::vector<double> &raw, const char *dataset, const char *label,
                            const char *units, Mesh2DResultVariable::Temporal temporal) {
        Mesh2DLiveVariable v;
        auto &d = v.descriptor;
        d.dataset = QString::fromLatin1(dataset);
        d.label = QStringLiteral("Groundwater — ") + QString::fromLatin1(label);
        d.units = QString::fromLatin1(units);
        d.unitsKnown = true;
        d.domain = Mesh2DResultVariable::Domain::Groundwater;
        d.temporal = temporal;
        v.values.resize(size_t(cells));
        v.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
        for (size_t c = 0; c < size_t(cells); ++c) {
            v.values[c] = static_cast<float>(raw[c]);
            if (!std::isfinite(v.values[c])) {
                v.values[c] = std::numeric_limits<float>::quiet_NaN();
                v.status[c] = Mesh2DValueStatus::Missing;
            }
        }
        frame->variables.append(std::move(v));
    };
    if (waterKnown[0])
        scalar(water[0], "Mesh2_face_gw_hg", "saturated thickness", "m",
               Mesh2DResultVariable::Temporal::Reported);
    // Inputs of the derived unsaturated water content column average.
    if (waterKnown[1])
        scalar(water[1], "Mesh2_face_gw_hu",
               "unsaturated storage (equivalent depth of water; the sigma column total under "
               "closure B)",
               "m", Mesh2DResultVariable::Temporal::Reported);
    // Same label and (absent) units as the saved file's static closure mask.
    if (std::vector<double> closure; read(SWMM_GW2D_VAR_CLOSURE, closure)) {
        scalar(closure, "Mesh2_face_gw_closure", "resolved unsaturated-zone closure per cell", "",
               Mesh2DResultVariable::Temporal::Static);
        frame->variables.last().descriptor.unitsKnown = false;
    }
    struct Flux {
        int selector;
        const char *dataset, *label, *units;
    };
    static constexpr Flux fluxes[] = {
        {SWMM_GW2D_VAR_Q0, "Mesh2_face_gw_recharge",
         "recharge from the unsaturated to the saturated zone, positive down, negative = "
         "capillary rise (last firing, held)",
         "m s-1"},
        {SWMM_GW2D_VAR_QLAT, "Mesh2_face_gw_lateral",
         "net lateral Darcy flow into the cell (last firing, held)", "m3 s-1"},
        {SWMM_GW2D_VAR_QNODE, "Mesh2_face_gw_node_exchange",
         "exchange with the 1D node bed, positive out of the aquifer into the pipe (last firing, "
         "held)",
         "m3 s-1"},
        {SWMM_GW2D_VAR_QDEEP, "Mesh2_face_gw_deep", "deep percolation loss (last firing, held)",
         "m s-1"},
        {SWMM_GW2D_VAR_QET, "Mesh2_face_gw_et", "subsurface evapotranspiration (last firing, held)",
         "m s-1"},
        {SWMM_GW2D_VAR_DUNNE, "Mesh2_face_gw_dunne",
         "saturation excess returned to the surface (last firing, held)", "m3 s-1"},
        {SWMM_GW2D_VAR_QPLUS, "Mesh2_face_gw_infil_in",
         "infiltration delivered from the surface (last firing, held)", "m s-1"},
        {SWMM_GW2D_VAR_QLINK, "Mesh2_face_gw_link_seepage",
         "conduit seepage delivered into the cell, length-weighted over the conduits crossing it "
         "(last firing, held)",
         "m3 s-1"},
#ifdef SWMM_GW2D_VAR_QX // engines that publish the cell Darcy velocity
        {SWMM_GW2D_VAR_QX, "Mesh2_face_gw_qx",
         "groundwater Darcy velocity, x component (last firing, held)", "m s-1"},
        {SWMM_GW2D_VAR_QY, "Mesh2_face_gw_qy",
         "groundwater Darcy velocity, y component (last firing, held)", "m s-1"},
#endif
    };
    for (const auto &flux : fluxes) {
        std::vector<double> raw;
        if (read(flux.selector, raw))
            scalar(raw, flux.dataset, flux.label, flux.units, Mesh2DResultVariable::Temporal::Held);
    }
    // The cell bed elevation (also the aquifer top): input of the derived
    // depth to water table and unsaturated thickness (mesh2dderivedvariables).
    {
        std::vector<double> ground(size_t(cells), std::numeric_limits<double>::quiet_NaN());
        bool any = false;
        for (int cell = 0; cell < cells; ++cell) {
            double x = 0, y = 0, z = 0;
            if (swmm_2d_triangle_get_centroid(engine, cell, &x, &y, &z) == SWMM_OK) {
                ground[size_t(cell)] = z;
                any = true;
            }
        }
        if (any) {
            scalar(ground, "Mesh2_face_z", "", "m", Mesh2DResultVariable::Temporal::Static);
            auto &d = frame->variables.last().descriptor;
            d.domain = Mesh2DResultVariable::Domain::Surface;
            d.label = QStringLiteral("Surface — cell bed elevation");
        }
    }
    if (!waterKnown[0] || !waterKnown[1])
        frame->warnings.append(QStringLiteral(
            "Live groundwater water-state metadata is unavailable; affected concentrations are missing."));
    if (swmm_gw2d_species_count(engine, &speciesCount) != SWMM_OK || speciesCount < 0) {
        frame->warnings.append(QStringLiteral("Live groundwater species catalog is unavailable."));
        return frame;
    }
    if (!speciesCount)
        return frame;
    const auto failChemistry = [&](const QString &message) -> Mesh2DLiveVariablesPtr {
        frame->warnings.append(message);
        return frame;
    };
    // Match names, never assume the groundwater and authoring row orders agree.
    const SpeciesUnits known = speciesUnits(engine);

    // satVolume = hg * theta_s * area; unsatVolume = hu * area for all closures.
    // These are the actual API concentration denominators, not surface depths.
    QVector<Mesh2DLiveVariable> chemistry;
    QSet<QString> identities;
    for (int row = 0; row < speciesCount; ++row) {
        char name[4096]{};
        if (swmm_gw2d_species_name(engine, row, name, sizeof name) != SWMM_OK)
            return failChemistry(QStringLiteral("Cannot capture groundwater species identity."));
        const QString species = QString::fromUtf8(name);
        if (species.isEmpty() || identities.contains(species.toCaseFolded()))
            return failChemistry(
                QStringLiteral("Live groundwater species identities are empty or ambiguous."));
        identities.insert(species.toCaseFolded());
        const QString unit = known.of(species);
        if (unit.isEmpty())
            frame->warnings.append(
                QStringLiteral(
                    "Groundwater units unresolved for %1; scientific export is unavailable.")
                    .arg(species));
        for (int zone = 0; zone < 2; ++zone) {
            Mesh2DLiveVariable variable;
            auto &d = variable.descriptor;
            d.dataset = zone == 0 ? QStringLiteral("Mesh2_face_gw_sat_conc")
                                  : QStringLiteral("Mesh2_face_gw_unsat_conc");
            d.species = species;
            d.units = unit;
            d.unitsKnown = !unit.isEmpty();
            d.domain = Mesh2DResultVariable::Domain::Groundwater;
            d.zone = zone == 0 ? Mesh2DResultVariable::Zone::Saturated
                               : Mesh2DResultVariable::Zone::Unsaturated;
            d.label = QStringLiteral("Groundwater — %1 — %2 concentration")
                          .arg(species, zone == 0 ? QStringLiteral("saturated")
                                                  : QStringLiteral("unsaturated"));
            std::vector<double> raw(size_t(cells), 0.0);
            int written = 0;
            if (swmm_gw2d_get_cell_conc(engine, zone, row, raw.data(), cells, &written) !=
                    SWMM_OK ||
                written != cells)
                return failChemistry(
                    QStringLiteral("Cannot capture groundwater concentration for %1.")
                        .arg(species));
            variable.values.resize(size_t(cells));
            variable.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
            for (int cell = 0; cell < cells; ++cell) {
                const size_t c = size_t(cell);
                const float value = static_cast<float>(raw[c]);
                if (!std::isfinite(value) || !waterKnown[zone] || !std::isfinite(water[zone][c]) ||
                    water[zone][c] < 0) {
                    variable.values[c] = std::numeric_limits<float>::quiet_NaN();
                    variable.status[c] = Mesh2DValueStatus::Missing;
                } else {
                    variable.values[c] = value;
                    if (water[zone][c] == 0)
                        variable.status[c] = Mesh2DValueStatus::Waterless;
                }
            }
            chemistry.append(std::move(variable));
        }
    }
    for (auto &variable : chemistry)
        frame->variables.append(std::move(variable));
    return frame;
}
}
