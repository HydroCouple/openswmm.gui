#pragma once

#include <QString>
#include <QObject>

namespace openswmmvis::ui {

inline bool porousSoilHeatOptionIsSupported(bool querySucceeded,
                                             const QString& optionValue)
{
    if (!querySucceeded) return false;
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

inline GroundwaterEtState groundwaterEtState(const QString& requested,
    const QString& aquiferMode, bool enabled, bool configured)
{
    GroundwaterEtState state;
    state.mode = requested == "AUTO"
        ? (aquiferMode == "PER_SUBCATCH" ? "NONE" : "BOTH") : requested;
    state.active = enabled && configured;
    state.boundary = state.active && (state.mode == "BOUNDARY_ET" || state.mode == "BOTH");
    state.capillary = state.active && (state.mode == "CAPILLARY_RISE" || state.mode == "BOTH");
    if (!enabled) state.reason = QObject::tr("Inactive: mesh groundwater is off. Saved settings are retained.");
    else if (!configured) state.reason = QObject::tr("Inactive: no aquifer configuration. Assign aquifer properties first.");
    else state.reason = QObject::tr("Draft ET resolves to %1 in %2 mode. Boundary ET removes water; capillary rise transfers water internally.")
        .arg(state.mode, aquiferMode);
    return state;
}

} // namespace openswmmvis::ui
