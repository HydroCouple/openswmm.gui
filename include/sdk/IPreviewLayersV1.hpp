/**
 * @file IPreviewLayersV1.hpp
 * @brief SDK 1.8 capability resolving an extension's native preview layer for selection sync.
 * @details Discover this optional interface with qobject_cast on ExtensionHost::lifetime().
 * IProject::showPreview() with `previewStyle.asFeatureLayer` creates a host-owned temporary
 * GIS layer. This capability names that layer's runtime attachment and maps each accepted
 * collection feature to its native FID, so an extension can read and replace the layer's
 * selection through IViewControlsV1 and observe LayerSelectionChanged on IEventSourceV3.
 * Existing SDK interfaces and their binary layouts remain unchanged.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QList>
#include <QObject>
#include <QUuid>

/** @brief Frozen discovery identity for native preview-layer resolution. */
#define OPENSWMM_PREVIEW_LAYERS_V1_IID "io.opensimulator.openswmm.IPreviewLayers/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Optional GUI-thread lookup of this extension's attached native preview layer. */
class IPreviewLayersV1
{
public:
    /** @brief Destroy an implementation; plugins only borrow the host capability. */
    virtual ~IPreviewLayersV1() = default;
    /** @brief Identify this extension's native preview layer in one Ready session.
     * @param[in] sessionId Ready per-open identity.
     * @param[out] featureIds Optional destination, replaced with native FIDs in the
     * feature order of the accepted showPreview() collection; empty when unavailable.
     * @return Runtime attachment UUID usable with IViewControlsV1 and IEventSourceV3, or
     * null without an attached native preview (none shown, scene-only preview, manually
     * removed, cleared, closed, wrong thread or host stopping).
     * @note FIDs belong to that exact attachment. A changed collection creates a new
     * attachment with new FIDs; never retain them as durable feature associations. */
    virtual QUuid previewLayer(const QUuid &sessionId,
                               QList<qint64> *featureIds = nullptr) const = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IPreviewLayersV1, OPENSWMM_PREVIEW_LAYERS_V1_IID)
