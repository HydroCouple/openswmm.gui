#ifndef OPENSWMMVIS_IO_MESH2DVARIABLEEXPORT_H
#define OPENSWMMVIS_IO_MESH2DVARIABLEEXPORT_H

#include <QString>

class IMesh2DSource;

namespace openswmmvis::io {

// Export one native-unit cell slice with semantic identity and validity.
// Resolves the key against the source's current catalog; refuses unknown units
// and source-file aliases. Existing destinations survive any detected failure.
bool exportMesh2DVariableCsv(IMesh2DSource& source, const QString& key, int frame,
                             const QString& path, QString* error = nullptr);

} // namespace openswmmvis::io
#endif
