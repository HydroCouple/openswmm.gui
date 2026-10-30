/**
 * @file      hydrothermalstate.h
 * @brief     Resolution of draft hydrothermal configuration and component state.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QString>
#include <QObject>

namespace openswmmvis::ui
{

/**
 * @brief Recognize draft soil-heat options supported by the UI.
 * @param[in] querySucceeded Whether the engine getter succeeded.
 * @param[in] optionValue Exact option token; only WATER_ONLY/POROUS_EQUILIBRIUM are accepted.
 * @return True for a successful query with either supported spelling; no case normalization occurs.
 */
inline bool porousSoilHeatOptionIsSupported(bool querySucceeded, const QString &optionValue)
{
    if (!querySucceeded)
        return false;
    return optionValue == QStringLiteral("WATER_ONLY") ||
           optionValue == QStringLiteral("POROUS_EQUILIBRIUM");
}

// Resolve the current draft, independently of the last initialized run.
struct GroundwaterEtState {
    QString mode;
    bool active = false;
    bool boundary = false;
    bool capillary = false;
    QString reason;
};

/**
 * @brief Resolve draft mesh-groundwater ET activation and transfer/removal flags without consulting runtime state.
 * @param[in] requested Exact mode; AUTO resolves to NONE for PER_SUBCATCH aquifers, otherwise BOTH.
 * @param[in] aquiferMode Exact draft aquifer mode token used by AUTO resolution.
 * @param[in] enabled Whether mesh groundwater is enabled in the draft.
 * @param[in] configured Whether an aquifer configuration exists.
 * @return Value state: active requires both flags, boundary matches BOUNDARY_ET/BOTH, capillary matches CAPILLARY_RISE/BOTH, and reason explains the draft resolution. Unknown explicit modes remain stored with both effect flags false.
 */
inline GroundwaterEtState groundwaterEtState(const QString &requested, const QString &aquiferMode,
                                             bool enabled, bool configured)
{
    GroundwaterEtState state;
    state.mode =
        requested == "AUTO" ? (aquiferMode == "PER_SUBCATCH" ? "NONE" : "BOTH") : requested;
    state.active = enabled && configured;
    state.boundary = state.active && (state.mode == "BOUNDARY_ET" || state.mode == "BOTH");
    state.capillary = state.active && (state.mode == "CAPILLARY_RISE" || state.mode == "BOTH");
    if (!enabled)
        state.reason =
            QObject::tr("Inactive: mesh groundwater is off. Saved settings are retained.");
    else if (!configured)
        state.reason =
            QObject::tr("Inactive: no aquifer configuration. Assign aquifer properties first.");
    else
        state.reason =
            QObject::tr(
                "Draft ET resolves to %1 in %2 mode. Boundary ET removes water; capillary rise transfers water internally.")
                .arg(state.mode, aquiferMode);
    return state;
}

} // namespace openswmmvis::ui
