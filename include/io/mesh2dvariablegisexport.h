/**
 * @file      mesh2dvariablegisexport.h
 * @brief     GIS export of mesh result variables and their spatial geometry.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "io/mesh2dscalarframe.h"
#include "project/projectsaveoutputs.h"
#include <QDateTime>
#include <QPointF>
#include <QVector>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

class IMesh2DSource;
namespace openswmmvis::io
{
struct Mesh2DVariableGisSnapshot {
    QString sourcePath, crsWkt;
    QDateTime time;
    int requestedFrame = -1;
    QVector<QPointF> vertices; // map coordinates, not scene Y
    std::vector<std::array<int, 4>> cells;
    Mesh2DScalarFrame scalar;
};
using Mesh2DVariableGisSnapshotPtr = std::shared_ptr<const Mesh2DVariableGisSnapshot>;
// GUI-thread acquisition only. Values remain native; coordinateScale applies to
// geometry only. Restores any existing live history pin before returning.
/**
 * @brief Acquire an owning native-value slice and scaled cell geometry for GIS export.
 * @param[in,out] source Borrowed result source accessed on the GUI thread; a live source is temporarily history-pinned and its previous pin state is restored.
 * @param[in] key Unique semantic variable key in the current source catalog.
 * @param[in] time Zero-based report frame; static/envelope variables do not require a report frame.
 * @param[in] coordinateScale Finite positive multiplier from source coordinates to output CRS coordinates; values are not scaled.
 * @param[in] crsWkt CRS of the scaled geometry; must describe a valid spatial reference for validation.
 * @param[out] error Optional diagnostic, cleared on entry and set on invalid input, source changes, or acquisition/validation failure.
 * @return Immutable shared snapshot without engine/source pointers, or null on failure.
 */
Mesh2DVariableGisSnapshotPtr captureMesh2DVariableGis(IMesh2DSource &source, const QString &key,
                                                      int time, double coordinateScale,
                                                      const QString &crsWkt,
                                                      QString *error = nullptr);

enum class Mesh2DVariableGisFormat { GeoPackage, GeoTiff };
struct Mesh2DVariableGisOptions {
    QString path;
    Mesh2DVariableGisFormat format = Mesh2DVariableGisFormat::GeoPackage;
    double pixelSize = 1.0;                           // coordinate units, raster only
    ProjectSaveOutputs::DestinationState destination; // captured before dispatch
};
// Worker-safe: reads immutable memory only; writes/validates an adjacent stage
// and atomically replaces an unchanged destination. No QObject/source pointers.
/**
 * @brief Stage, verify, and atomically publish a scalar-variable GIS result.
 * @param[in] s Immutable owning snapshot with declared units, valid cell geometry, and matching values/status.
 * @param[in] options Destination, format, and pre-dispatch destination fingerprint; raster pixel size is in geometry coordinate units.
 * @param[in] cancelled Optional synchronous cancellation predicate; true cancels before publication.
 * @param[out] error Optional diagnostic cleared on entry; set on cancellation, validation, IO, or destination-change failure.
 * @param[in] progress Optional synchronous integer-percent callback invoked on the calling thread.
 * @return True after complete publication; false leaves a preexisting destination intact and removes the adjacent staging area.
 * @details Requires an unchanged destination without companion files and rejects aliases of the source. GeoPackage retains native values with explicit status; GeoTIFF uses separate Float64 value/status bands, populating values only for Valid cells. Raster limits are 64 million pixels and one million pixels per side. Safe on a worker with an immutable snapshot.
 */
bool exportMesh2DVariableGis(const Mesh2DVariableGisSnapshot &s,
                             const Mesh2DVariableGisOptions &options,
                             const std::function<bool()> &cancelled, QString *error = nullptr,
                             const std::function<void(int)> &progress = {});
}
