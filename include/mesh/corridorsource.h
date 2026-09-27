#ifndef OPENSWMMVIS_MESH_CORRIDORSOURCE_H
#define OPENSWMMVIS_MESH_CORRIDORSOURCE_H

#include "mesh/meshpatch.h"
#include <QStringList>
#include <functional>

namespace mesh {

// A selected-feature recipe. Width and spacing are in meshCRSWkt linear units.
// Runtime layer UUIDs are deliberately excluded: they are not persisted by GIS layers.
struct CorridorSource {
    QString path, layerName, sourceCRSWkt, meshCRSWkt;
    QVector<qint64> featureIds; // Explicit selection; empty never means all features.
    QString widthField;
    double width = 10.0, along = 0.0;
    int across = 2;
    QString tag;
    QString geometryDigest; // Empty captures the first successful read.
    QStringList sourceFiles;
};

struct CorridorSourceStamp {
    QString path, canonicalPath;
    qint64 size = -1, modifiedMs = -1;
};

struct CorridorReadResult {
    QVector<PatchMesh> patches;
    QVector<CorridorSource> resolvedSources;
    QVector<CorridorSourceStamp> sourceStamps;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// Worker-safe: opens owned datasets and returns values only. Cancellation returns
// an error and no partial patches or resolved sources. No live GIS layer is read.
CorridorReadResult readCorridorSources(const QVector<CorridorSource> &sources,
                                      const QString &targetCRSWkt,
                                      const std::function<bool()> &cancel = {});

// Recheck captured local dependencies before adopting a completed worker result.
bool corridorSourceFilesUnchanged(const QVector<CorridorSourceStamp> &stamps,
                                  QString *error = nullptr);

} // namespace mesh
#endif
