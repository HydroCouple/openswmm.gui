/**
 * @file      mesh2dderivedvariables.cpp
 * @brief     Groundwater variables computed in the GUI from saved or live 2D results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dderivedvariables.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::io
{
namespace
{
using V = Mesh2DResultVariable;
using S = Mesh2DValueStatus;

struct Definition {
    const char *dataset;
    const char *label;
    bool clampAtZero; // a saturated column has no unsaturated zone, not a negative one
};
// Ground is the cell bed elevation, which is also the aquifer top: the engine
// sets the aquifer bottom to the cell bed minus the soil column thickness.
constexpr Definition kDefinitions[] = {
    {"gw_depth_to_table", "Groundwater — depth to water table (derived)", false},
    {"gw_unsat_thickness", "Groundwater — unsaturated thickness (derived)", true},
};

/**
 * @brief Minimal descriptor naming the ground (cell bed) elevation input.
 * @return Static surface descriptor for `Mesh2_face_z`.
 */
V groundInput()
{
    V v;
    v.dataset = QStringLiteral("Mesh2_face_z");
    v.domain = V::Domain::Surface;
    v.temporal = V::Temporal::Static;
    return v;
}

/**
 * @brief Minimal descriptor naming the water-table elevation input.
 * @return Groundwater descriptor for `Mesh2_face_gw_table_elev`.
 */
V tableInput()
{
    V v;
    v.dataset = QStringLiteral("Mesh2_face_gw_table_elev");
    v.domain = V::Domain::Groundwater;
    return v;
}

/**
 * @brief Find a derived definition by dataset name.
 * @param[in] dataset Derived dataset name.
 * @return Matching definition or null.
 */
const Definition *definition(const QString &dataset)
{
    for (const auto &d : kDefinitions)
        if (dataset == QLatin1String(d.dataset))
            return &d;
    return nullptr;
}
} // namespace

QVector<Mesh2DResultVariable>
derivedGroundwaterVariables(const QVector<Mesh2DResultVariable> &native, QStringList *warnings)
{
    const QString groundKey = groundInput().key(), tableKey = tableInput().key();
    const V *ground = nullptr, *table = nullptr;
    for (const auto &v : native) {
        if (v.derived)
            continue;
        if (v.key() == groundKey && v.zone == V::Zone::None)
            ground = &v;
        else if (v.key() == tableKey && v.zone == V::Zone::None)
            table = &v;
    }
    QVector<V> out;
    if (!ground || !table)
        return out;
    const bool unitsKnown = ground->unitsKnown && table->unitsKnown;
    if (unitsKnown && ground->units.trimmed() != table->units.trimmed()) {
        if (warnings)
            warnings->append(
                QStringLiteral("Derived groundwater depths are unavailable: ground elevation (%1) "
                               "and water-table elevation (%2) use different units.")
                    .arg(ground->units, table->units));
        return out;
    }
    for (const auto &d : kDefinitions) {
        V v;
        v.dataset = QString::fromLatin1(d.dataset);
        v.label = QString::fromUtf8(d.label);
        v.domain = V::Domain::Groundwater;
        v.temporal = table->temporal;
        v.frameCount = table->frameCount;
        v.unitsKnown = unitsKnown;
        v.units = unitsKnown ? table->units.trimmed() : QString();
        v.derived = true;
        v.derivedFrom = {groundKey, tableKey};
        out.append(v);
    }
    return out;
}

bool computeDerivedGroundwater(const QString &dataset, const std::vector<float> &ground,
                               const std::vector<Mesh2DValueStatus> &groundStatus,
                               const std::vector<float> &table,
                               const std::vector<Mesh2DValueStatus> &tableStatus,
                               std::vector<float> &values, std::vector<Mesh2DValueStatus> &status)
{
    values.clear();
    status.clear();
    const Definition *d = definition(dataset);
    const size_t cells = ground.size();
    if (!d || groundStatus.size() != cells || table.size() != cells ||
        tableStatus.size() != cells)
        return false;
    values.resize(cells);
    status.resize(cells, S::Valid);
    for (size_t c = 0; c < cells; ++c) {
        // Difference in double: elevations carry the datum, the result does not.
        const double depth = double(ground[c]) - double(table[c]);
        if (groundStatus[c] != S::Valid || tableStatus[c] != S::Valid || !std::isfinite(depth)) {
            values[c] = std::numeric_limits<float>::quiet_NaN();
            status[c] = S::Missing;
        } else
            values[c] = float(d->clampAtZero ? std::max(0.0, depth) : depth);
    }
    return true;
}

bool readDerivedVariableAt(const Mesh2DResultVariable &derived, int timeIdx,
                           const Mesh2DFaceVariableReader &read, std::vector<float> &values,
                           std::vector<Mesh2DValueStatus> &status)
{
    values.clear();
    status.clear();
    if (!derived.derived || !definition(derived.dataset) || !read)
        return false;
    std::vector<float> ground, table;
    std::vector<S> groundStatus, tableStatus;
    if (!read(groundInput(), timeIdx, ground, groundStatus) ||
        !read(tableInput(), timeIdx, table, tableStatus))
        return false;
    return computeDerivedGroundwater(derived.dataset, ground, groundStatus, table, tableStatus,
                                     values, status);
}

} // namespace openswmmvis::io
