// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      tracedata.h
 * @brief     Value types for flow balance and travel-time analysis results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <memory>
#include <openswmm/engine/openswmm_trace.h>

class SWMMModelLayer;
namespace openswmmvis::trace
{
struct Node {
    QString id;
    int type = 0, flags = 0;
    QPointF point;
    bool hasGeometry = false;
};
struct Link {
    QString id;
    int from = -1, to = -1, type = 0;
    double length = 0;
    QVector<QPointF> points;
};
struct Snapshot {
    QVector<Node> nodes;
    QVector<Link> links;
    QString wkt, modelFingerprint;
    /**
     * @brief Copy topology/geometry and model fingerprint into a JSON value.
     * @return Owning node/link/CRS metadata object, with no validation or unit conversion.
     */
    QJsonObject toJson() const;
    /**
     * @brief Decode saved topology using Qt default conversions.
     * @param[in] j Snapshot node/link/CRS document.
     * @return Owning snapshot; malformed/missing fields use defaults and must be checked with valid().
     */
    static Snapshot fromJson(const QJsonObject &j);
    /**
     * @brief Copy editing-model topology/geometry and classify unresolved exchange/loss flags.
     * @param[in] model Borrowed loaded model on the GUI/engine-owning thread; no running handle crosses to a worker.
     * @param[out] error Optional diagnostic on unavailable/invalid topology, unchanged on success.
     * @return Owning validated snapshot, or empty on failure. Link lengths are converted to metres; geometry stays in model map coordinates and captured CRS.
     * @details SHA-256 modelFingerprint includes IDs/types/topology/lengths/flags but excludes coordinate geometry and CRS, so geometry-only changes do not invalidate hydraulic averages.
     */
    static Snapshot capture(SWMMModelLayer *model, QString *error);
    /**
     * @brief Validate supported node identity/type, link connectivity/length, and supplied geometry.
     * @param[out] error Optional first-failure diagnostic, unchanged on success.
     * @return False for no nodes, exact duplicate/empty IDs, node types outside 0..3, invalid endpoints/nonfinite negative lengths, or nonfinite supplied coordinates.
     * @details Does not validate CRS, link type, case-insensitive ID collisions, or require geometry for every object.
     */
    bool valid(QString *error = nullptr) const;
};
struct Dataset {
    Snapshot snapshot;
    QString sourceId, runId, label, outputPath, fingerprint, packagePath;
    QString provenance =
        QStringLiteral("User-supplied model pairing; original topology unverified");
    SWMM_TraceOptions options{};
    SWMM_TraceInfo info{};
    bool partial = false; ///< Built from an incomplete output; never reused.
    QVector<SWMM_TraceNodeAverage> nodes;
    QVector<SWMM_TraceLinkAverage> links;
    /**
     * @brief Create empty value storage with native trace default numerical options.
     */
    Dataset() { swmm_trace_default_options(&options); }
};
struct Result {
    std::shared_ptr<const Dataset> dataset;
    QString id;
    int seed = 0, direction = 0;
    bool saved = false;
    QVector<SWMM_TraceValue> nodes, links;
    SWMM_TraceSummary summary{};
    QJsonObject style;
    /**
     * @brief Describe source label, direction, and seed identity.
     * @return Empty without a dataset; otherwise a display title using downstream for direction zero and upstream for nonzero. Invalid seed index produces an empty seed name.
     */
    QString title() const;
};
/**
 * @brief Format trace availability/approximation flags for display and persisted diagnostics.
 * @param[in] flags SWMM_TRACE_* bitmask.
 * @return Semicolon-separated recognized conditions, or OK when none are recognized; Unknown time suppresses the Partial time label.
 */
QString statusText(int flags);
/**
 * @brief Encode trace source/algorithm coverage metadata.
 * @param[in] i Native trace info with SWMM report dates and duration in seconds.
 * @return Owning JSON fields, preserving native SI/source-unit provenance.
 */
QJsonObject infoJson(const SWMM_TraceInfo &i);
/**
 * @brief Decode trace coverage metadata without schema validation.
 * @param[in] j Serialized native info fields.
 * @return Value struct using Qt zero defaults for missing fields and zero reserved field.
 */
SWMM_TraceInfo infoFromJson(const QJsonObject &j);
/**
 * @brief Encode trace numerical thresholds for package provenance.
 * @param[in] o Native options: flow epsilon m3/s, velocity epsilon m/s, dimensionless dominance/tolerance, and iteration count.
 * @return Owning JSON object without conversion.
 */
QJsonObject optionsJson(const SWMM_TraceOptions &o);
/**
 * @brief Overlay saved numerical options onto native defaults.
 * @param[in] j Optional threshold/iteration fields; absent or nonnumeric values retain defaults.
 * @return Options without physical/algorithm-range validation; native create validates applicability.
 */
SWMM_TraceOptions optionsFromJson(const QJsonObject &j);
/**
 * @brief Encode terminal fractions and SI trace-budget diagnostics.
 * @param[in] s Native summary with cubic-metre budgets, dimensionless accounting/solver residuals, and reach counts.
 * @return Owning JSON object; nonfinite partial-balance residual is null.
 */
QJsonObject summaryJson(const SWMM_TraceSummary &s);
/**
 * @brief Decode persisted trace-budget diagnostics.
 * @param[in] j Saved summary object.
 * @return Value summary with zero defaults for missing fields, bounded terminal-array copy, and NaN for absent/nonnumeric partial residual.
 */
SWMM_TraceSummary summaryFromJson(const QJsonObject &j);
/**
 * @brief Create native trace topology/options on the calling thread.
 * @param[in] s Valid owning topology snapshot; coordinates are not passed to the hydraulic estimator.
 * @param[in] o Native trace solver options.
 * @param[out] error Optional topology/native-create diagnostic, unchanged on success.
 * @return Owned native trace handle requiring swmm_trace_close() on the owning thread, or null on failure. Node/link ID bytes are copied by the native creation API.
 */
SWMM_Trace createHandle(const Snapshot &s, const SWMM_TraceOptions &o, QString *error);
} // namespace openswmmvis::trace
