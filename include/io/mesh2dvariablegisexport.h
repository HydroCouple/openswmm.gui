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
namespace openswmmvis::io {
struct Mesh2DVariableGisSnapshot {
    QString sourcePath, crsWkt;
    QDateTime time;
    int requestedFrame = -1;
    QVector<QPointF> vertices; // map coordinates, not scene Y
    std::vector<std::array<int,4>> cells;
    Mesh2DScalarFrame scalar;
};
using Mesh2DVariableGisSnapshotPtr = std::shared_ptr<const Mesh2DVariableGisSnapshot>;
// GUI-thread acquisition only. Values remain native; coordinateScale applies to
// geometry only. Restores any existing live history pin before returning.
Mesh2DVariableGisSnapshotPtr captureMesh2DVariableGis(IMesh2DSource &, const QString &key,
    int time, double coordinateScale, const QString &crsWkt, QString *error = nullptr);

enum class Mesh2DVariableGisFormat { GeoPackage, GeoTiff };
struct Mesh2DVariableGisOptions {
    QString path;
    Mesh2DVariableGisFormat format = Mesh2DVariableGisFormat::GeoPackage;
    double pixelSize = 1.0; // coordinate units, raster only
    ProjectSaveOutputs::DestinationState destination; // captured before dispatch
};
// Worker-safe: reads immutable memory only; writes/validates an adjacent stage
// and atomically replaces an unchanged destination. No QObject/source pointers.
bool exportMesh2DVariableGis(const Mesh2DVariableGisSnapshot &, const Mesh2DVariableGisOptions &,
    const std::function<bool()> &cancelled, QString *error = nullptr,
    const std::function<void(int)> &progress = {});
}
