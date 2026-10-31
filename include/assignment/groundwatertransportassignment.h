/**
 * @file      groundwatertransportassignment.h
 * @brief     Spatial assignment of groundwater species concentrations to mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_GROUNDWATERTRANSPORTASSIGNMENT_H
#define OPENSWMMVIS_GROUNDWATERTRANSPORTASSIGNMENT_H
#include <openswmm/engine/openswmm_engine.h>
#include "assignment/groundwaterassignment.h"
#include <QMap>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>

namespace openswmmvis::assignment
{
struct TransportSpecies {
    QString id, nativeConcUnits, nativeMassRateUnits;
    bool massSupported = false, signedConcentration = false;
    /**
     * @brief Compare all stored TransportSpecies fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const TransportSpecies &other) const = default;
};
struct GroundwaterQualityRow {
    int scope = 0, cell = -1, zone = 0, layer = 0;
    QString tag, species;
    double value = 0;
    /**
     * @brief Compare all stored GroundwaterQualityRow fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const GroundwaterQualityRow &other) const = default;
};
struct GroundwaterSpeciesTerm {
    QString species, kind = QStringLiteral("CONC"), series;
    double value = 0;
    /**
     * @brief Compare all stored GroundwaterSpeciesTerm fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const GroundwaterSpeciesTerm &other) const = default;
};
struct GroundwaterSourceRow {
    QString name, tag, series;
    int scope = 2, cell = -1;
    double flow = 0, scale = 1;
    QVector<GroundwaterSpeciesTerm> terms;
    bool byXy = false; // located by `XY x y`; scope/cell then hold the cell it resolved to
    double x = 0, y = 0;
    /**
     * @brief Compare all stored GroundwaterSourceRow fields using their value equality operators.
     * @param[in] other Record to compare without modifying either value.
     * @return True for fieldwise equality; floating-point fields compare exactly.
     */
    bool operator==(const GroundwaterSourceRow &other) const = default;
};
struct GroundwaterTransportSnapshot {
    int cellCount = 0;
    AquiferSnapshot aquifers;
    QStringList cellTags;
    QVector<bool> activeCells;
    QVector<GroundwaterQualityRow> quality;
    QString qualityFile;
    QVector<GroundwaterSourceRow> sources;
    QVector<TransportSpecies> species;
    QMap<QString, QString> options;
    QMap<QString, QVector<QPointF>> timeSeries;
    /**
     * @brief Compare all snapshot inputs used to reject stale transport previews.
     * @param[in] other Other snapshot; time-series sample coordinates/values use exact double equality rather than QPointF fuzzy equality.
     * @return True only for equal metadata, ordered rows and identical time-series samples.
     */
    bool operator==(const GroundwaterTransportSnapshot &other) const;
};
/**
 * @brief Read enabled groundwater species and native concentration/mass-rate metadata.
 * @param[in] engine Borrowed engine supplying transport enable flags and species definitions.
 * @param[out] error Optional diagnostic, cleared at entry.
 * @return Species available for assignment; empty may mean no enabled species or a read failure, distinguished by error.
 */
QVector<TransportSpecies> groundwaterTransportSpecies(SWMM_Engine engine, QString *error = nullptr);
/**
 * @brief Read every authored [GW_INITIAL_QUALITY] row, including rows a FILE reference loaded
 * (those carry layer -2).
 * @param[in] engine Borrowed engine.
 * @param[out] rows Non-null destination, replaced only after a complete successful read.
 * @param[out] error Optional failure description, cleared at entry.
 * @return True on a complete read.
 */
bool readGroundwaterQualityRows(SWMM_Engine engine, QVector<GroundwaterQualityRow> *rows,
                                QString *error = nullptr);
/**
 * @brief Upsert one [GW_INITIAL_QUALITY] row by its scope, zone/layer and species key.
 * @param[in] engine Borrowed editable engine.
 * @param[in] row Row in species-native units; tag is passed only as given.
 * @return True when the engine setter succeeds.
 */
bool writeGroundwaterQualityRow(SWMM_Engine engine, const GroundwaterQualityRow &row);
/**
 * @brief Read every authored [GW_SOURCES] row with its scale, species terms and XY location.
 * @param[in] engine Borrowed engine.
 * @param[out] rows Non-null destination, replaced only after a complete successful read.
 * @param[out] error Optional failure description, cleared at entry.
 * @return True on a complete read. An XY row reads as CELL scope at its resolved cell, with
 * byXy set and its authored x/y.
 */
bool readGroundwaterSources(SWMM_Engine engine, QVector<GroundwaterSourceRow> *rows,
                            QString *error = nullptr);
/**
 * @brief Write one named source (re-locating it by XY when row.byXy), then its scale at
 * @p index and each species term.
 * @param[in] engine Borrowed editable engine.
 * @param[in] index Zero-based row index the source occupies after the upsert (the current count
 * for a new source).
 * @param[in] row Source in m³/s with species-native terms.
 * @param[in] allowed Optional callback asked before each write; false stops without writing.
 * @param[in,out] writes Optional counter incremented after each successful write.
 * @return True when every write succeeded; false can leave a partially written source.
 */
bool writeGroundwaterSource(SWMM_Engine engine, int index, const GroundwaterSourceRow &row,
                            const std::function<bool()> &allowed = {}, int *writes = nullptr);
/**
 * @brief Capture inputs needed for transport validation and stale-preview detection.
 * @param[in] engine Borrowed engine whose aquifers, active cells, quality, sources, species and time series are read.
 * @param[out] snapshot Non-null destination, replaced only after a complete successful read.
 * @param[out] error Optional failure description, cleared at entry.
 * @return True on a complete snapshot; false preserves the previous destination.
 */
bool readGroundwaterTransportSnapshot(SWMM_Engine engine, GroundwaterTransportSnapshot *snapshot,
                                      QString *error = nullptr);
enum class GroundwaterTransportTarget { InitialQuality, Source };
enum class GroundwaterSourceDistribution { PerCell, RegionTotal };
struct GroundwaterTransportRequest {
    GroundwaterTransportSnapshot before;
    GroundwaterTransportTarget target = GroundwaterTransportTarget::InitialQuality;
    QVector<int> cells;
    QVector<double> values; // initial values or per-cell constant source flow (m3/s)
    bool skipNoData = false;
    QString species;
    int zone = 0;  // SAT=0 / UNSAT=1 / explicit SIGMA layer=2
    int layer = 1; // one-based, used only for zone=2
    QString sourceName = QStringLiteral("Assigned"), flowSeries;
    QVector<GroundwaterSpeciesTerm> terms;
    GroundwaterSourceDistribution distribution = GroundwaterSourceDistribution::PerCell;
    QVector<double> areaWeights; // positive cell areas; normalized for RegionTotal
};
struct GroundwaterTransportPreview {
    GroundwaterTransportSnapshot before, after;
    GroundwaterTransportTarget target = GroundwaterTransportTarget::InitialQuality;
    QVector<int> cells, skippedCells;
    QString error;
};
// A true cancellation callback returns an error with after == before.
/**
 * @brief Build a validated initial-quality or groundwater-source edit using snapshot data only.
 * @param[in] request Distinct in-range cell IDs and parallel finite values; initial values use species native concentration units, source flows use signed m³/s. NaN is skipped only when requested.
 * @param[in] cancelled Optional callback evaluated in the calling thread; true discards actionable changes.
 * @return Before/after snapshots and affected/skipped IDs. On validation failure or cancellation, error is set and after equals before.
 * @details Source RegionTotal distribution normalizes positive cell-area weights; soil-layer quality requires enabled layered transport and a valid one-based SIGMA layer in every target cell.
 */
GroundwaterTransportPreview previewGroundwaterTransport(const GroundwaterTransportRequest &request,
                                                        std::function<bool()> cancelled = {});
struct GroundwaterTransportResult {
    bool success = false, changed = false, rollbackComplete = true;
    QString error;
};
// Full preflight and readback. A failed write attempts bounded restoration;
// changed remains true when restoration cannot be confirmed (owner must dirty).
/**
 * @brief Commit reviewed transport rows with preflight, readback and bounded restoration.
 * @param[in] engine Borrowed engine in BUILDING or OPENED state; its full snapshot must match the preview before state.
 * @param[in] preview Error-free validated edit produced by previewGroundwaterTransport().
 * @param[in] beforeWrite Optional synchronous hook receiving a one-based write ordinal; false prevents that write.
 * @return Success, mutation and restoration flags plus diagnostics. A true changed flag requires marking the project dirty, even after successful restoration, because authoring metadata may have changed.
 */
GroundwaterTransportResult applyGroundwaterTransport(SWMM_Engine engine,
                                                     const GroundwaterTransportPreview &preview,
                                                     std::function<bool(int)> beforeWrite = {});
/**
 * @brief Restore the before state only when the engine still matches the reviewed after state.
 * @param[in] engine Borrowed editing engine.
 * @param[in] preview Original applied edit.
 * @return Transaction status using the same changed/rollbackComplete contract as applyGroundwaterTransport(); newer edits cause refusal.
 */
GroundwaterTransportResult undoGroundwaterTransport(SWMM_Engine engine,
                                                    const GroundwaterTransportPreview &preview);
}
#endif
