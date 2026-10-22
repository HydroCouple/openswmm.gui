/**
 * @file      profilesectionseries.h
 * @brief     Sampling and assembly of profile section result series.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_PROFILESECTIONSERIES_H
#define OPENSWMMVIS_PROFILESECTIONSERIES_H

#include "io/mesh2dresultvariable.h"
#include <QDateTime>
#include <QJsonObject>
#include <QPen>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>
#include <limits>

class IMesh2DSource;
namespace ProfileSection
{
struct Section;
enum class SeriesRole { Elevation, Scalar };
enum class TimePolicy { Exact, Hold };
struct SourceReference {
    QString id, path, verticalDatum;
};
struct SeriesDefinition {
    QString id, sourceId, variableKey, label;
    SeriesRole role = SeriesRole::Scalar;
    TimePolicy timePolicy = TimePolicy::Hold;
    bool visible = true;
    QPen pen{QColor(0x24, 0x70, 0xa0), 1.5};
    double opacity = 1.0;
    bool customRange = false;
    double minimum = 0.0, maximum = 1.0;
};
struct Definition {
    QString id, title;
    QVector<QPointF>
        scenePolyline; // scene Y is negative map Y; stored explicitly as scene coordinates
    QString sceneCRS, horizontalUnits, elevationUnits = QStringLiteral("m"), verticalDatum;
    QString primarySourceId;
    QVector<SourceReference> sources;
    QVector<SeriesDefinition> series;
    QJsonObject displayOptions; // viewer-owned versioned display-property values
};
struct SeriesStation {
    double chainage = 0.0;
    QPointF scenePt;
    int cellId = -1;
    bool breakBefore = false;
};
struct SeriesPoint : SeriesStation {
    double value = std::numeric_limits<double>::quiet_NaN();
    openswmmvis::io::Mesh2DValueStatus status = openswmmvis::io::Mesh2DValueStatus::Missing;
};
struct SampledSeries {
    SeriesDefinition definition;
    openswmmvis::io::Mesh2DResultVariable descriptor;
    QVector<SeriesPoint> points; // independent exact intervals for this source
    QString units, sourcePath, error;
    bool unitsKnown = false;
    QDateTime requestedTime, effectiveTime;
    int frame = -1; // static/envelope series have no report frame
};
struct SourceBinding {
    QString sourceId, sceneCRS, verticalDatum;
    IMesh2DSource *source = nullptr; // synchronous call only; owner guarantees lifetime
    QDateTime
        surfaceTime; // primary legacy surface geometry timestamp; never inferred from requested cursor
    QVector<SeriesStation>
        stations; // exact path intervals, independently mapped in the shared scene CRS
    std::function<int(const QPointF &)>
        mapCell; // optional primary-source fallback for legacy samples
};
// Shared terrain/current-WSE rows for the table and CSV; legacy values already use elevationUnits.
/**
 * @brief Assemble the reserved terrain and current surface-water rows from an already sampled section.
 * @param[in] section Existing stations/ground/current water geometry and requested/effective times.
 * @param[in] d Definition supplying primary-source identity/path and elevation units; values are assumed already in those units.
 * @return Two owned rows, terrain then current WSE, with parallel stations and reserved IDs. Nonfinite ground remains Missing. Water needs hasResults and a valid effectiveTime; finite signed/current depth gives ground+max(0,depth), Valid only when cellHasSurface and depth positive, otherwise Waterless. No source/frame reads or definition validation occurs.
 */
QVector<SampledSeries> builtInSeries(const Section &section, const Definition &d);
// Pure time selection: no extrapolation outside coverage and no interpolation.
/**
 * @brief Choose an exact or previous reported frame without interpolation or extrapolation.
 * @param[in] times Nonempty strictly increasing valid report instants; validated on every call.
 * @param[in] requested Valid requested instant within the inclusive coverage.
 * @param[in] policy Exact requires an equal report; Hold uses the greatest report below it. Any non-Exact enum value follows Hold behavior.
 * @param[out] error Optional diagnostic cleared on entry and assigned on failure.
 * @return Zero-based chosen frame, or -1 for invalid/empty/nonmonotonic times, out-of-coverage request or an absent Exact report.
 */
int selectFrame(const QVector<QDateTime> &times, const QDateTime &requested, TimePolicy policy,
                QString *error = nullptr);
// Atomic definition validation; unavailable source/key yields a retained series with error.
// Existing exact terrain/surface fields are untouched. Elevation roles accept the
// authoritative groundwater table/base quantities only, with checked length units.
/**
 * @brief Sample all declared additional series into independent source-mapped station rows.
 * @param[in,out] section Existing terrain/surface samples remain unchanged. On successful assembly replaces series/requestedTime/effectiveTime; source-specific errors are retained inside individual rows.
 * @param[in] definition Valid section identity, scene coordinate frame, source references and rendering/sampling definitions.
 * @param[in] sources Borrowed synchronous bindings; exactly one must match each used sourceId, with a stable source and exact sceneCRS match. Nonprimary sources require independent valid stations, and elevation roles additionally require matching nonempty vertical datums.
 * @param[in] requested Instant used for reported/held variables; static/envelope fields use frame zero and no effective report time.
 * @param[out] error Optional definition/exception diagnostic, cleared by validation on entry. Per-series source/read/clock errors are recorded in the series, not this output.
 * @return False for invalid definition or an exception before the assembled vector is installed. True may contain unavailable/error rows and means the request was assembled, not that all data exists.
 * @note Cell indices are mapped per binding; primary-source legacy samples are a fallback when stations are empty. Only groundwater table/base datasets may use the common elevation role; checked m/ft units are converted to definition elevationUnits. Scalar values retain source units/status, and invalid/nonfinite cells stay Missing/NaN. Source path/history-generation/cell/time metadata is checked after reading to reject mid-sample source changes.
 * @note No ownership or thread synchronization is provided. Caller must keep sources/callbacks valid through all synchronous reads; source checks detect certain changes but do not permit data races.
 */
bool sampleSeries(Section &section, const Definition &definition,
                  const QVector<SourceBinding> &sources, const QDateTime &requested,
                  QString *error = nullptr);
/**
 * @brief Validate section identity, path counts/coordinates, source references and series policy/style.
 * @param[in] d Definition to inspect without mutation. Requires a nonzero finite path of 2..100000 points, 1..64 unique sources containing primarySourceId, and at most 128 uniquely named nonreserved series.
 * @param[out] error Optional first-failure diagnostic cleared on entry.
 * @return True when structural requirements hold: nonblank identity/sceneCRS/horizontalUnits, m/ft elevation units, known roles/policies, valid colour, pen width (0,32], opacity [0,1], finite range bounds and increasing custom range. Does not access source files/catalogs, verify a CRS definition/horizontal-unit token or validate every pen enum/dash field.
 */
bool validateDefinition(const Definition &d, QString *error = nullptr);
/**
 * @brief Serialize a section definition into the version-one scene-coordinate schema.
 * @param[in] d Definition copied without validation, including source/series styles and viewer displayOptions.
 * @param[in] baseDirectory Directory used to relativize absolute resolved source paths; empty paths remain empty.
 * @return Owned JSON with scene_xy_y_negated convention and pen/brush metadata. No file/source IO occurs.
 */
QJsonObject definitionToJson(const Definition &d, const QString &baseDirectory);
/**
 * @brief Decode a strict saved schema and validate it before replacing a definition.
 * @param[in] o Version-one object with scene_xy_y_negated convention; unknown schema keys and invalid typed fields are rejected.
 * @param[in] baseDirectory Directory used to resolve nonempty relative source paths, without checking existence.
 * @param[out] out Replaced only on full successful decode/validateDefinition().
 * @param[out] error Optional diagnostic cleared on entry and assigned on failure.
 * @return False for malformed/unsupported schema, count/style/dash errors or invalid definition; true yields an owned validated value. Limits are 100000 points, 64 sources, 128 series and 64 positive dash entries (custom dash requires an even nonempty list).
 */
bool definitionFromJson(const QJsonObject &o, const QString &baseDirectory, Definition &out,
                        QString *error = nullptr);
// Atomic current-section table export. Unknown-unit visible series are refused.
/**
 * @brief Atomically export visible current section rows with status and provenance.
 * @param[in] section Sampled built-in/additional rows with valid finite stations and declared units for every visible series.
 * @param[in] d Valid definition matching visible sampled IDs/source IDs/variable keys.
 * @param[in] path Nonblank destination distinct from referenced result paths including filesystem aliases. Parent directories must exist.
 * @param[out] error Optional validation/IO diagnostic cleared on entry.
 * @return True after QSaveFile commit; false preserves the prior destination. Visible unavailable/unknown-unit or unsampled definitions are refused.
 * @note Writes chainage in declared horizontal units, map Y as negative scene Y, series-specific native/converted units and explicit statuses. Missing numbers become blank. Operates only on snapshots and never queries a live engine.
 */
bool exportSectionCsv(const Section &section, const Definition &d, const QString &path,
                      QString *error = nullptr);
} // namespace ProfileSection
#endif
