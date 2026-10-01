#include "io/mesh2dlivevariables.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <QHash>
#include <QSet>
#include <cmath>
#include <limits>

namespace openswmmvis::io {
Mesh2DLiveVariablesPtr captureGroundwaterVariables(SWMM_Engine engine,
                                                  int expectedCells, QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) -> Mesh2DLiveVariablesPtr {
        if (error) *error = message;
        return {};
    };
    if (!engine || expectedCells <= 0) return fail(QStringLiteral("Invalid live groundwater source."));
    int active = 0;
    if (swmm_gw2d_is_active(engine, &active) != SWMM_OK)
        return fail(QStringLiteral("Cannot inspect live groundwater state."));
    if (!active) return {};
    int cells = 0, speciesCount = 0;
    if (swmm_gw2d_get_dimensions(engine, &cells, nullptr) != SWMM_OK || cells != expectedCells)
        return fail(QStringLiteral("Live groundwater dimensions do not match the results mesh."));
    auto frame = std::make_shared<Mesh2DLiveVariables>(); frame->cellCount = cells;
    const auto read = [&](int variable, std::vector<double> &values) {
        values.resize(size_t(cells)); int written = 0;
        return swmm_gw2d_get_cell_bulk(engine, variable, values.data(), cells, &written) == SWMM_OK
            && written == cells;
    };
    std::vector<double> water[2], table;
    const bool waterKnown[2] = {read(SWMM_GW2D_VAR_HG, water[0]), read(SWMM_GW2D_VAR_HU, water[1])};
    const bool tableKnown = read(SWMM_GW2D_VAR_TABLE_EL, table);
    const auto elevation = [&](const QString &dataset, const QString &label, bool base) {
        Mesh2DLiveVariable v; auto &d = v.descriptor;
        d.dataset = dataset; d.label = label; d.units = QStringLiteral("m"); d.unitsKnown = true;
        d.domain = Mesh2DResultVariable::Domain::Groundwater;
        if (base) d.temporal = Mesh2DResultVariable::Temporal::Static;
        v.values.resize(size_t(cells)); v.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
        for (int cell = 0; cell < cells; ++cell) {
            const size_t c = size_t(cell);
            const float value = static_cast<float>(base ? table[c] - water[0][c] : table[c]);
            if (!std::isfinite(value) || (base && (!std::isfinite(water[0][c]) || water[0][c] < 0))) {
                v.values[c] = std::numeric_limits<float>::quiet_NaN(); v.status[c] = Mesh2DValueStatus::Missing;
            } else v.values[c] = value;
        }
        frame->variables.append(std::move(v));
    };
    if (tableKnown) {
        elevation(QStringLiteral("Mesh2_face_gw_table_elev"), QStringLiteral("Groundwater — water table elevation"), false);
        // ApiGw2D::cellVar(TABLE_EL) = z_bed + hg. Subtract the HG API value
        // in double, before display storage, to recover the same SI datum.
        if (waterKnown[0]) elevation(QStringLiteral("Mesh2_face_gw_bed_elev"), QStringLiteral("Groundwater — aquifer base elevation"), true);
    } else frame->warnings.append(QStringLiteral("Live groundwater table elevation is unavailable."));
    if (!waterKnown[0] || !waterKnown[1])
        frame->warnings.append(QStringLiteral("Live groundwater water-state metadata is unavailable; affected concentrations are missing."));
    if (swmm_gw2d_species_count(engine, &speciesCount) != SWMM_OK || speciesCount < 0) {
        frame->warnings.append(QStringLiteral("Live groundwater species catalog is unavailable.")); return frame;
    }
    if (!speciesCount) return frame;
    const auto failChemistry = [&](const QString &message) -> Mesh2DLiveVariablesPtr {
        frame->warnings.append(message); return frame;
    };
    // Match names, never assume the groundwater and authoring row orders agree.
    // An ambiguous metadata name has unknown units instead of a guessed winner.
    QHash<QString, QString> units;
    QSet<QString> ambiguous;
    const auto addUnit = [&](const QString &name, const QString &unit) {
        if (units.contains(name) && units.value(name) != unit) ambiguous.insert(name);
        else units.insert(name, unit);
    };
    for (int i = 0, n = swmm_pollutant_count(engine); i < n; ++i) {
        const char *name = swmm_pollutant_id(engine, i); int unit = -1;
        if (!name || swmm_pollutant_get_units(engine, i, &unit) != SWMM_OK) continue;
        addUnit(QString::fromUtf8(name), unit == 0 ? QStringLiteral("MG/L")
            : unit == 1 ? QStringLiteral("UG/L") : unit == 2 ? QStringLiteral("#/L") : QString());
    }
    for (int i = 0, n = swmm_reaction_species_count(engine); i < n; ++i) {
        char name[4096]{}, unit[4096]{}; int wall = 0; double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(engine, i, name, sizeof name, &wall, unit, sizeof unit,
                                      &atol, &rtol) == SWMM_OK && !wall)
            addUnit(QString::fromUtf8(name), QString::fromUtf8(unit).trimmed());
    }
    // Groundwater API returns raw age in seconds (surface HDF's hours do not
    // apply), and raw temperature in degrees C, including negative values.
    addUnit(QStringLiteral("__WATER_AGE__"), QStringLiteral("s"));
    addUnit(QStringLiteral("__TEMPERATURE__"), QStringLiteral("degC"));

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
            return failChemistry(QStringLiteral("Live groundwater species identities are empty or ambiguous."));
        identities.insert(species.toCaseFolded());
        const QString unit = ambiguous.contains(species) ? QString() : units.value(species);
        if (unit.isEmpty()) frame->warnings.append(QStringLiteral("Groundwater units unresolved for %1; scientific export is unavailable.").arg(species));
        for (int zone = 0; zone < 2; ++zone) {
            Mesh2DLiveVariable variable;
            auto &d = variable.descriptor;
            d.dataset = zone == 0 ? QStringLiteral("Mesh2_face_gw_sat_conc") : QStringLiteral("Mesh2_face_gw_unsat_conc");
            d.species = species; d.units = unit; d.unitsKnown = !unit.isEmpty();
            d.domain = Mesh2DResultVariable::Domain::Groundwater;
            d.zone = zone == 0 ? Mesh2DResultVariable::Zone::Saturated : Mesh2DResultVariable::Zone::Unsaturated;
            d.label = QStringLiteral("Groundwater — %1 — %2 concentration").arg(species,
                zone == 0 ? QStringLiteral("saturated") : QStringLiteral("unsaturated"));
            std::vector<double> raw(size_t(cells), 0.0); int written = 0;
            if (swmm_gw2d_get_cell_conc(engine, zone, row, raw.data(), cells, &written) != SWMM_OK
                || written != cells)
                return failChemistry(QStringLiteral("Cannot capture groundwater concentration for %1.").arg(species));
            variable.values.resize(size_t(cells));
            variable.status.resize(size_t(cells), Mesh2DValueStatus::Valid);
            for (int cell = 0; cell < cells; ++cell) {
                const size_t c = size_t(cell);
                const float value = static_cast<float>(raw[c]);
                if (!std::isfinite(value) || !waterKnown[zone] || !std::isfinite(water[zone][c]) || water[zone][c] < 0) {
                    variable.values[c] = std::numeric_limits<float>::quiet_NaN();
                    variable.status[c] = Mesh2DValueStatus::Missing;
                } else {
                    variable.values[c] = value;
                    if (water[zone][c] == 0) variable.status[c] = Mesh2DValueStatus::Waterless;
                }
            }
            chemistry.append(std::move(variable));
        }
    }
    for (auto &variable : chemistry) frame->variables.append(std::move(variable));
    return frame;
}
}
