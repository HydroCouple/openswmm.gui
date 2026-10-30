/**
 * @file      groundwaterassignment.h
 * @brief     Spatial assignment of groundwater forcing to selected mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_GROUNDWATERASSIGNMENT_H
#define OPENSWMMVIS_GROUNDWATERASSIGNMENT_H
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infil2d.h>
#include <QMap>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>
#include <limits>

namespace openswmmvis::assignment
{
struct InfiltrationOwnershipSnapshot {
    QVector<SWMM_Infil2DAuthoredRow> rows;
    QString destination;
};
struct InfiltrationOwnershipCell {
    int cell = 0, owner = 0, conflict = 0;
    QString source, reason;
};
struct InfiltrationOwnershipPreview {
    InfiltrationOwnershipSnapshot before, after;
    QVector<InfiltrationOwnershipCell> cells;
    QStringList summaries;
    QString error;
    bool changed = false;
};
/**
 * @brief Review surface infiltration conflicts with aquifer-owned cells without changing inputs.
 * @param[in] engine Borrowed editing engine containing the current mesh and authored infiltration rows.
 * @param[in] tags One exact engine region tag per cell, in engine cell-index order; unsaved mesh-tag changes are rejected.
 * @param[in] removeExplicit Whether the proposed rows remove explicit surface methods from aquifer-owned cells.
 * @return Before/after authored rows, per-cell ownership explanations, and migration summaries; nonempty error prevents applying the preview. Obsolete AQUIFER_2D destinations are proposed as LOST.
 */
InfiltrationOwnershipPreview
previewInfiltrationOwnership(SWMM_Engine engine, const QStringList &tags, bool removeExplicit);
/**
 * @brief Replace reviewed infiltration inputs after checking for intervening changes.
 * @param[in] engine Borrowed engine in BUILDING or OPENED state.
 * @param[in] expected Exact authored rows and destination that must still exist.
 * @param[in] desired Reviewed replacement rows and destination; may be a prior snapshot for undo.
 * @param[out] error Optional failure message; unchanged on success.
 * @return True after both writes succeed. A failed row replacement attempts to restore the old destination.
 */
bool applyInfiltrationOwnership(SWMM_Engine engine, const InfiltrationOwnershipSnapshot &expected,
                                const InfiltrationOwnershipSnapshot &desired, QString *error);
struct AquiferRow {
    int scope = 0, cell = -1;
    QString tag;
    double ks = 0, zs = 0, thetaS = 0, thetaR = 0, alpha = 0;
    QMap<QString, double> optional;
    /**
     * @brief Compare all stored AquiferRow fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const AquiferRow &other) const = default;
};
struct AquiferSnapshot {
    QVector<AquiferRow> rows;
    QMap<QString, QString> options;
    /**
     * @brief Compare all stored AquiferSnapshot fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const AquiferSnapshot &other) const = default;
};
struct AquiferTarget {
    QString key, label, unitKind;
    double minimum = 0, maximum = std::numeric_limits<double>::max();
    bool minimumExclusive = false, supported = true;
    QString unavailableReason;
};
struct AquiferProcessPreview {
    AquiferSnapshot before, after;
    QString groundwater, beforeEffective, afterEffective, forcing, error;
    bool changed = false;
};
/**
 * @brief Describe the authored atmospheric demand that can drive groundwater ET.
 * @param[in] engine Borrowed engine whose mesh/climate evaporation settings are inspected.
 * @param[in] meshMode Optional mesh evaporation mode override; empty reads the current engine option.
 * @return Human-readable forcing review, including missing climate configuration; this is a configuration check rather than simulation-period validation.
 */
QString groundwaterEtForcingStatus(SWMM_Engine engine, const QString &meshMode = {});
/**
 * @brief Validate and review groundwater ET, seepage, and wilting choices without writing.
 * @param[in] engine Borrowed engine supplying the current aquifer snapshot and enable flag.
 * @param[in] et AUTO, NONE, BOUNDARY_ET, CAPILLARY_RISE or BOTH.
 * @param[in] link DEFAULT, ONE_WAY, NONE or TWO_WAY.
 * @param[in] wilting AUTO or a finite positive suction expressed as text in project length units.
 * @return Before/after options, effective behavior descriptions and forcing review; nonempty error rejects the proposed edit.
 */
AquiferProcessPreview previewAquiferProcesses(SWMM_Engine engine, const QString &et,
                                              const QString &link, const QString &wilting);
/**
 * @brief Apply reviewed groundwater process options after stale-input checks.
 * @param[in] engine Borrowed editing engine.
 * @param[in] expected Snapshot that must still match current rows, options and forcing review.
 * @param[in] desired Snapshot supplying GW_ET, LINK_SEEPAGE, WILTING_SUCTION and OPTIONS_AUTHORED.
 * @param[in] groundwater Expected GROUNDWATER enable token captured by the preview.
 * @param[out] error Optional failure description.
 * @return True when the atomic engine process-options update succeeds; false on stale inputs or engine refusal.
 */
bool applyAquiferProcesses(SWMM_Engine engine, const AquiferSnapshot &expected,
                           const AquiferSnapshot &desired, const QString &groundwater,
                           QString *error);
/**
 * @brief Describe assignable aquifer properties and their permitted value ranges.
 * @return Ordered target metadata including unit kinds, exclusive lower bounds and an explicit unsupported FLOW target with its refusal reason.
 */
QVector<AquiferTarget> aquiferTargets();
/**
 * @brief Capture authored aquifer rows, inheritance flags, options and forcing context.
 * @param[in] engine Borrowed engine; nullptr is rejected.
 * @param[out] snapshot Non-null destination, replaced only after every read succeeds.
 * @param[out] error Optional message, cleared at entry and populated on failure.
 * @return True on a complete capture; false leaves the destination unchanged.
 */
bool readAquiferSnapshot(SWMM_Engine engine, AquiferSnapshot *snapshot, QString *error);
struct AquiferRequest {
    AquiferSnapshot before;
    QVector<int> cells;
    QStringList cellTags; // Parallel to cells; engine cell indices, not display triangles.
    QString target;
    QVector<double> values; // One sampled value per cell; NaN is explicit NoData.
    bool skipNoData = false;
};
struct AquiferPreview {
    AquiferSnapshot before;
    QVector<AquiferRow> appended;
    QVector<int> cells, skippedCells;
    QVector<double> oldValues, newValues;
    QString target, error;
};
// A true cancellation callback discards all actionable rows; engine state is untouched.
/**
 * @brief Resolve inherited aquifer properties into appended per-cell overrides.
 * @param[in] request Immutable snapshot, distinct nonnegative engine cell IDs, parallel tags and sampled values. Values use the target property units; NaN may be skipped only with skipNoData.
 * @param[in] cancelled Optional callback polled in the calling thread; true cancels the preview.
 * @return Validated changed rows and old/new values. Missing base properties, invalid ranges or cancellation set error and clear actionable rows. The engine is never accessed.
 */
AquiferPreview previewAquiferAssignment(const AquiferRequest &request,
                                        std::function<bool()> cancelled = {});
// Append-only transaction: original authored records remain byte-for-byte in
// engine storage. Undo removes only a verified, unchanged appended suffix.
// The optional hook injects a failure before a numbered write for regression.
/**
 * @brief Append validated overrides while preserving all original authored records.
 * @param[in] engine Borrowed engine in BUILDING or OPENED state.
 * @param[in] preview Error-free preview whose before snapshot must still match the engine.
 * @param[out] error Optional message, cleared at entry; a rollback failure explicitly requires reloading.
 * @param[in] beforeWrite Optional synchronous hook receiving a one-based write ordinal; false injects failure before that write.
 * @return True on success, including an empty append. Failure attempts to remove the appended suffix; inspect error if restoration fails.
 */
bool applyAquiferPreview(SWMM_Engine engine, const AquiferPreview &preview, QString *error,
                         std::function<bool(int)> beforeWrite = {});
/**
 * @brief Remove only the unchanged suffix produced by an aquifer assignment.
 * @param[in] engine Borrowed engine in BUILDING or OPENED state.
 * @param[in] preview Original preview; current rows/options must equal before plus appended rows.
 * @param[out] error Optional message, cleared at entry.
 * @return True after suffix removal; false for stale edits, unavailable state, read failures or failed deletion.
 */
bool undoAquiferPreview(SWMM_Engine engine, const AquiferPreview &preview, QString *error);
}
#endif
