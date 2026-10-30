/**
 * @file IEditScope.hpp
 * @brief Mediated undoable model editing through the GUI property and apply paths.
 * @details Edits form one snapshot transaction. A failed operation prevents commit and causes rollback. Timestamps are timezone-naive SWMM wall-clock values; geometry uses the native model CRS.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "INativeEditScope.hpp"
#include <QDateTime>
#include <QPointF>
#include <QVariant>
#include <QVector>
namespace OpenSWMM::Sdk
{
/** Undoable changes through the same property adapters and model apply paths as the GUI. */
class IEditScope
{
public:
    /** @brief Abort uncommitted work and release its engine lease. */
    virtual ~IEditScope() = default;
    /** category is node/link/subcatchment/gage; field uses the adapter's Q_PROPERTY name. */
    /** @brief Set an existing object's writable GUI property.
     * @param[in] category Namespace: node, link, subcatchment or gage.
     * @param[in] name Existing model object ID.
     * @param[in] field Exact Q_PROPERTY name from the corresponding GUI property adapter.
     * @param[in] value New value in the adapter's existing project-unit conventions.
     * @param[out] error Optional unsupported-category/object/property detail.
     * @return True when the property write is accepted; false marks the complete transaction failed.
     * @note Uses the existing property adapters so geometry and engine setter conventions are shared. */
    virtual bool setAttribute(const QString &category, const QString &name, const QString &field,
                              const QVariant &value, QString *error = nullptr) = 0;
    /** @brief Add a node through SWMMModelLayer's existing apply path.
     * @param[in] name New unique node ID.
     * @param[in] type Public engine node-type enumeration value.
     * @param[in] point X/Y in the native model CRS.
     * @return True on creation; false prevents commit of the transaction. */
    virtual bool addNode(const QString &name, int type, QPointF point) = 0;
    /** @brief Add a link between existing nodes.
     * @param[in] name New unique link ID.
     * @param[in] type Public engine link-type enumeration value.
     * @param[in] from Upstream node ID.
     * @param[in] to Downstream node ID.
     * @param[in] vertices Interior bend vertices in the native model CRS, excluding endpoints.
     * @return True on creation; false prevents commit. */
    virtual bool addLink(const QString &name, int type, const QString &from, const QString &to,
                         const QVector<QPointF> &vertices = {}) = 0;
    /** @brief Add a spatial rain-gage object.
     * @param[in] name New unique gage ID.
     * @param[in] point Native model-map coordinates.
     * @return True on creation; configure forcing separately with setGageSeries(). */
    virtual bool addGage(const QString &name, QPointF point) = 0;
    /** @brief Add a subcatchment outline through the existing apply path.
     * @param[in] name New unique subcatchment ID.
     * @param[in] polygon Exterior outline in native model coordinates; engine limitations apply.
     * @return True on creation; hydraulic attributes and gage assignment remain explicit edits. */
    virtual bool addSubcatchment(const QString &name, const QVector<QPointF> &polygon) = 0;
    /** @brief Rename an object using the model's reference-aware rename path.
     * @param[in] category Node, link, subcatchment or gage namespace.
     * @param[in] name Existing identifier.
     * @param[in] newName New unique identifier in the same namespace.
     * @return True if accepted; false marks the transaction failed. */
    virtual bool rename(const QString &category, const QString &name, const QString &newName) = 0;
    /** @brief Create or replace authored time-series samples.
     * @param[in] name Time-series ID; curve names belong to a separate namespace.
     * @param[in] times Valid, strictly increasing timezone-naive SWMM wall-clock timestamps.
     * @param[in] values Finite sample values in the intended forcing's project units.
     * @param[in] replace Allow replacing an existing time series; default refuses duplicates.
     * @return False for invalid samples, mismatched/empty vectors or engine refusal; commit then rolls back. */
    virtual bool addTimeSeries(const QString &name, const QVector<QDateTime> &times,
                               const QVector<double> &values, bool replace = false) = 0;
    /** @brief Bind a rain gage to an authored precipitation time series.
     * @param[in] gage Existing gage ID.
     * @param[in] series Existing time-series ID.
     * @param[in] intervalSeconds Positive finite sample interval in seconds.
     * @param[in] rainfallFormat Public engine rain-type code (intensity, volume or cumulative).
     * @return True if every engine setter succeeds; false makes the transaction uncommittable. */
    virtual bool setGageSeries(const QString &gage, const QString &series, double intervalSeconds,
                               int rainfallFormat) = 0;
    /** @brief Append an external inflow row with explicit series and unit scaling.
     * @param[in] node Existing receiving node ID.
     * @param[in] constituent FLOW or an existing pollutant/thermal constituent name.
     * @param[in] series Time-series ID; empty denotes a baseline-only row.
     * @param[in] type Engine inflow type token: FLOW, CONCEN or MASS.
     * @param[in] scale Unit-conversion multiplier, default one.
     * @param[in] baseline Constant baseline in the corresponding project units.
     * @return True on engine acceptance; this appends rather than replacing a previous row. */
    virtual bool addInflow(const QString &node, const QString &constituent, const QString &series,
                           const QString &type = "FLOW", double scale = 1.,
                           double baseline = 0.) = 0;
    /** @brief Commit all accepted edits as one snapshot-backed undo command.
     * @param[out] error Optional failure detail.
     * @return False if any operation failed; the before snapshot is restored instead. */
    virtual bool commit(QString *error = nullptr) = 0;
    /** @brief Restore the pre-edit model without adding an undo command.
     * @param[out] error Optional restoration failure detail.
     * @return True after restoration or an already finished scope. */
    virtual bool abort(QString *error = nullptr) = 0;
};
}
