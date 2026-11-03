/**
 * @file      mesh2dderivedvariables.cpp
 * @brief     Groundwater variables computed in the GUI from saved or live 2D results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dderivedvariables.h"
#include "io/mesh2dresultvector.h"

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

namespace
{
const QString kMoistureDataset = QStringLiteral("gw_theta_unsat_mean");
const QString kMagnitudePrefix = QStringLiteral("magnitude|");

/** @return Groundwater descriptor for a plain dataset name. */
V groundwaterInput(const char *dataset, V::Temporal temporal = V::Temporal::Reported)
{
    V v;
    v.dataset = QString::fromLatin1(dataset);
    v.domain = V::Domain::Groundwater;
    v.temporal = temporal;
    return v;
}

/** @return Sigma water content layer descriptor. */
V sigmaLayerInput(int layer)
{
    V v = groundwaterInput("Mesh2_face_gw_theta_sigma");
    v.zone = V::Zone::Sigma;
    v.layer = layer;
    return v;
}

/**
 * @param[in] native Catalog.
 * @return Column-average descriptor when its inputs exist, else empty dataset.
 */
V moistureVariable(const QVector<V> &native)
{
    const QString closureKey = groundwaterInput("Mesh2_face_gw_closure").key(),
                  huKey = groundwaterInput("Mesh2_face_gw_hu").key(),
                  groundKey = groundInput().key(), tableKey = tableInput().key();
    const V *table = nullptr;
    bool closure = false, hu = false, ground = false;
    QStringList layers;
    for (const auto &v : native) {
        if (v.derived)
            continue;
        const QString key = v.key();
        if (v.zone == V::Zone::Sigma && v.dataset == QLatin1String("Mesh2_face_gw_theta_sigma") &&
            v.species.isEmpty() && v.layer >= 0)
            layers.append(key);
        else if (v.zone != V::Zone::None)
            continue;
        else if (key == closureKey)
            closure = true;
        else if (key == huKey)
            hu = true;
        else if (key == groundKey)
            ground = true;
        else if (key == tableKey)
            table = &v;
    }
    if (!closure || !table || (!layers.isEmpty() ? false : !(hu && ground)))
        return {};
    V out;
    out.dataset = kMoistureDataset;
    out.label = QStringLiteral("Groundwater — unsaturated water content, column average (derived)");
    out.units = QStringLiteral("1");
    out.unitsKnown = true;
    out.domain = V::Domain::Groundwater;
    out.temporal = table->temporal;
    out.frameCount = table->frameCount;
    out.derived = true;
    out.derivedFrom = QStringList{closureKey, groundKey, tableKey, hu ? huKey : QString()} + layers;
    return out;
}

/**
 * @brief Compute the unsaturated column average (see derivedResultVariables()).
 * @return False when a required input could not be read.
 */
bool readMoisture(const V &derived, int timeIdx, const Mesh2DFaceVariableReader &read,
                  std::vector<float> &values, std::vector<S> &status)
{
    std::vector<float> closure, ground, table, hu;
    std::vector<S> closureStatus, groundStatus, tableStatus, huStatus;
    if (!read(groundwaterInput("Mesh2_face_gw_closure", V::Temporal::Static), timeIdx, closure,
              closureStatus))
        return false;
    const size_t cells = closure.size();
    const bool haveHu = derived.derivedFrom.size() > 3 && !derived.derivedFrom[3].isEmpty();
    // Ground and table give every closure's unsaturated thickness.
    const bool thick = read(groundInput(), timeIdx, ground, groundStatus) &&
                       read(tableInput(), timeIdx, table, tableStatus) && ground.size() == cells &&
                       table.size() == cells && groundStatus.size() == cells &&
                       tableStatus.size() == cells;
    const bool bulk = thick && haveHu &&
                      read(groundwaterInput("Mesh2_face_gw_hu"), timeIdx, hu, huStatus) &&
                      hu.size() == cells && huStatus.size() == cells;
    const int layers = std::max(0, int(derived.derivedFrom.size()) - 4);
    std::vector<double> sum(cells, 0.0);
    std::vector<S> sigmaStatus(cells, layers > 0 ? S::Valid : S::Missing);
    std::vector<float> layer;
    std::vector<S> layerStatus;
    for (int j = 0; j < layers; ++j) {
        if (!read(sigmaLayerInput(j), timeIdx, layer, layerStatus) || layer.size() != cells ||
            layerStatus.size() != cells)
            return false;
        for (size_t c = 0; c < cells; ++c) {
            if (sigmaStatus[c] != S::Valid)
                continue;
            if (layerStatus[c] != S::Valid || !std::isfinite(layer[c]))
                sigmaStatus[c] = layerStatus[c] == S::Valid ? S::Missing : layerStatus[c];
            else
                sum[c] += layer[c];
        }
    }
    values.assign(cells, std::numeric_limits<float>::quiet_NaN());
    status.assign(cells, S::Missing);
    for (size_t c = 0; c < cells; ++c) {
        if (closureStatus[c] != S::Valid || !std::isfinite(closure[c]))
            continue;
        const bool sigma = closure[c] == 2;
        if (!sigma && closure[c] != 0 && closure[c] != 1)
            continue;
        const bool knownThickness =
            thick && groundStatus[c] == S::Valid && tableStatus[c] == S::Valid;
        const double thickness =
            knownThickness ? double(ground[c]) - double(table[c]) : std::nan("");
        if (knownThickness && !(thickness > 1e-9)) {
            status[c] = S::NotApplicable; // saturated to the ground: no unsaturated zone
            continue;
        }
        if (sigma && layers > 0) {
            status[c] = sigmaStatus[c];
            if (sigmaStatus[c] == S::Valid)
                values[c] = float(sum[c] / layers);
        } else if (bulk && knownThickness && huStatus[c] == S::Valid && std::isfinite(hu[c])) {
            // Closure A's bulk storage; for a sigma column without its layers
            // (live runs) hu is the engine's sigma column storage, the same mean.
            values[c] = float(std::max(0.0, double(hu[c])) / thickness);
            status[c] = S::Valid;
        }
    }
    return true;
}

/** @return The |v| descriptor of a vector. */
V magnitudeVariable(const Mesh2DResultVector &vector)
{
    V m;
    m.dataset = kMagnitudePrefix + vector.x.key() + QLatin1Char('|') + vector.y.key();
    m.label = vector.label + QStringLiteral(" — magnitude (derived)");
    m.units = vector.x.unitsKnown ? vector.x.units.trimmed() : QString();
    m.unitsKnown = vector.x.unitsKnown;
    m.domain = vector.x.domain;
    m.temporal = vector.x.temporal;
    m.frameCount = vector.x.frameCount;
    m.derived = true;
    m.derivedFrom = {vector.x.key(), vector.y.key()};
    return m;
}

/** @brief Compute |v| from both components; a cell is valid only when both are. */
bool readMagnitude(const V &derived, int timeIdx, const Mesh2DFaceVariableReader &read,
                   std::vector<float> &values, std::vector<S> &status)
{
    if (derived.derivedFrom.size() != 2)
        return false;
    const V x = plainVariableFromKey(derived.derivedFrom[0]),
            y = plainVariableFromKey(derived.derivedFrom[1]);
    std::vector<float> vx, vy;
    std::vector<S> sx, sy;
    if (x.dataset.isEmpty() || y.dataset.isEmpty() || !read(x, timeIdx, vx, sx) ||
        !read(y, timeIdx, vy, sy) || vy.size() != vx.size() || sx.size() != vx.size() ||
        sy.size() != vx.size())
        return false;
    values.resize(vx.size());
    status.resize(vx.size());
    for (size_t c = 0; c < vx.size(); ++c) {
        status[c] = sx[c] != S::Valid ? sx[c] : sy[c];
        const double m = std::hypot(double(vx[c]), double(vy[c]));
        if (status[c] != S::Valid || !std::isfinite(m)) {
            values[c] = std::numeric_limits<float>::quiet_NaN();
            if (status[c] == S::Valid)
                status[c] = S::Missing;
        } else
            values[c] = float(m);
    }
    return true;
}
} // namespace

QVector<Mesh2DResultVariable> derivedResultVariables(const QVector<Mesh2DResultVariable> &native,
                                                     QStringList *warnings)
{
    auto out = derivedGroundwaterVariables(native, warnings);
    if (const auto moisture = moistureVariable(native); !moisture.dataset.isEmpty())
        out.append(moisture);
    for (const auto &vector : pairResultVectors(native))
        out.append(magnitudeVariable(vector));
    return out;
}

bool readDerivedVariableAt(const Mesh2DResultVariable &derived, int timeIdx,
                           const Mesh2DFaceVariableReader &read, std::vector<float> &values,
                           std::vector<Mesh2DValueStatus> &status)
{
    values.clear();
    status.clear();
    if (!derived.derived || !read)
        return false;
    if (derived.dataset == kMoistureDataset)
        return readMoisture(derived, timeIdx, read, values, status);
    if (derived.dataset.startsWith(kMagnitudePrefix))
        return readMagnitude(derived, timeIdx, read, values, status);
    if (!definition(derived.dataset))
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
