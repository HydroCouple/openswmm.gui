/**
 * @file ITimeSeriesLayersV1.hpp
 * @brief SDK 1.6 capability for durable native observation-layer imports.
 * @details Discover on ExtensionHost::lifetime(). GUI-thread requests target an
 * exact Ready model session/load. Parsing and package creation run in a host job;
 * publication revalidates the target. Published layers contain no plugin code.
 * Existing SDK interfaces and their layouts remain frozen.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QObject>
#include <QString>
#include <QUuid>
#include <QList>
#include <functional>

/** @brief Frozen discovery identity for the first native time-series service. */
#define OPENSWMM_TIMESERIES_LAYERS_V1_IID "io.opensimulator.openswmm.ITimeSeriesLayers/1.0"

namespace OpenSWMM::Sdk
{
class IJob;
/** @brief Owning request referencing a completed local source-neutral import descriptor. */
struct TimeSeriesImportRequest {
    QUuid sessionId; ///< Exact open model lifetime, never a persistent model UUID.
    quint64 loadGeneration = 0; ///< Generation captured when accepting the download.
    QUuid importId; ///< Idempotence identity; repeated accepted completion returns the same layer.
    QString descriptorPath; ///< Local JSON schema openswmm.timeseries-import/1 and CSV references.
    QString packagePath; ///< New durable .gpkg path; existing files are never overwritten.
};
/** @brief Terminal import outcome delivered on the GUI thread to a guarded receiver. */
struct TimeSeriesImportResult {
    QUuid importId; ///< Correlates the accepted request.
    QString layerId; ///< Persistent host-owned layer identity, empty if not published.
    QString datasetId; ///< Native dataset UUID, empty if preparation failed.
    QString packagePath; ///< Completed package path, retained if the target became unavailable.
    int seriesCount = 0; ///< Number of native series; null-only series remain represented.
    QString error; ///< Plain failure/refusal detail, empty on success.
    bool cancelled = false; ///< Cancellation won before publication.
};
/** @brief Bounded copied metadata for one attached native collection. */
struct TimeSeriesLayerInfo {
    QString layerId; ///< Persistent layer identity.
    QString datasetId; ///< Dataset UUID.
    QString title; ///< User-facing layer caption.
    QString packagePath; ///< Durable local native source.
    int seriesCount = 0; ///< Series descriptors, not observation rows.
};
/** @brief Optional host-owned import service; all public entry points use the GUI thread. */
class ITimeSeriesLayersV1
{
public:
    /** @brief Destroy an implementation; plugins only borrow this capability. */
    virtual ~ITimeSeriesLayersV1() = default;
    /** @brief Prepare and attach a native dataset through a cancellable host job.
     * @param[in] request Exact target and immutable local files; no samples are copied into widgets.
     * @param[in] context Required GUI-thread callback lifetime guard.
     * @param[in] finished Owning terminal receiver; must not throw or enter a native edit scope.
     * @param[out] error Immediate refusal, cleared on acceptance.
     * @return Borrowed cancellable IJob, or null when refused before starting work.
     * @note Accepted work delivers once while the host/context remain alive. Receiver destruction
     * cancels publication; host stop suppresses callbacks. Closed/reloaded targets never redirect
     * to another project. Completed source assets remain reviewable. Once attached, the layer
     * survives plugin unload. Import does not assign model forcing. */
    virtual IJob *importTimeSeries(const TimeSeriesImportRequest &request, QObject *context,
                                  std::function<void(const TimeSeriesImportResult &)> finished,
                                  QString *error = nullptr) = 0;
    /** @brief Enumerate attached collections without loading samples or creating facades.
     * @param[in] sessionId Exact Ready session identity.
     * @return Up to 1000 owning metadata records; empty off-thread/after closure/host stop. */
    virtual QList<TimeSeriesLayerInfo> timeSeriesLayers(const QUuid &sessionId) const = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::ITimeSeriesLayersV1, OPENSWMM_TIMESERIES_LAYERS_V1_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::TimeSeriesImportResult)
