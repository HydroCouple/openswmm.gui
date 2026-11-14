/**
 * @file IModelRainfall.hpp
 * @brief Read-only native rainfall units and authored gage locations for forcing tools.
 * @details SDK 1.12 optional GUI-thread capability discovered on ExtensionHost::lifetime().
 * Values are copied under a native read lease for an exact Ready session/load. Display
 * preferences never determine forcing units. Existing interface layouts remain frozen.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QUuid>

/** @brief Frozen discovery identity for model rainfall metadata. */
#define OPENSWMM_MODEL_RAINFALL_IID "io.opensimulator.openswmm.IModelRainfall/1.0"
namespace OpenSWMM::Sdk
{
/** @brief Authored gage identity and coordinates, never a display-relocated marker. */
struct RainGageInfo {
    QString id; ///< Exact existing model gage identifier.
    QPointF point; ///< Authored native-model coordinate; origin denotes an unlocated gage.
    QPointF lonLat; ///< WGS84 longitude/latitude when hasLonLat is true.
    bool hasLonLat = false; ///< False for unlocated gages, unknown CRS or failed transformation.
};
/** @brief Owning metadata for preparing rainfall in the engine's native input conventions. */
struct RainfallModelInfo {
    QString depthUnit; ///< Exactly mm for SI flow units or in for US flow units.
    QDateTime start; ///< Configured timezone-naive model start, UTC-labeled to avoid DST arithmetic.
    QList<RainGageInfo> gages; ///< At most 5000 copied gages; no engine/widget pointer escapes.
};
/** @brief Read rainfall metadata without acquiring a write scope or modifying the model. */
class IModelRainfall
{
public:
    /** @brief Destroy an implementation; extensions borrow this capability. */
    virtual ~IModelRainfall() = default;
    /** @brief Query exact-session rainfall metadata through the host's native read path.
     * @param[in] sessionId Open model lifetime, never a saved project UUID.
     * @param[in] generation Accepted Ready load generation.
     * @param[out] info Required destination; unchanged on refusal.
     * @param[out] error Optional unavailable, stale, locked or unsupported-model detail.
     * @return True after copying metadata; false has no model or output side effects. */
    virtual bool modelRainfall(const QUuid &sessionId, quint64 generation,
                               RainfallModelInfo *info, QString *error = nullptr) const = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IModelRainfall, OPENSWMM_MODEL_RAINFALL_IID)
