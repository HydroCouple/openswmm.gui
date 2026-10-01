#ifndef OPENSWMMVIS_IO_MESH2DLIVEVARIABLES_H
#define OPENSWMMVIS_IO_MESH2DLIVEVARIABLES_H

#include "io/mesh2dresultvariable.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QMetaType>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace openswmmvis::io {

// A worker-owned value snapshot. Published only as shared_ptr<const>; it never
// contains engine handles or pointers into solver storage.
struct Mesh2DLiveVariable {
    Mesh2DResultVariable descriptor;
    std::vector<float> values;
    std::vector<Mesh2DValueStatus> status;
};
struct Mesh2DLiveVariables {
    int cellCount = 0;
    QVector<Mesh2DLiveVariable> variables;
    QStringList warnings;
    size_t payloadBytes() const {
        size_t bytes = 0;
        for (const auto &v : variables)
            bytes += v.values.size() * sizeof(float) + v.status.size() * sizeof(Mesh2DValueStatus);
        return bytes;
    }
};
using Mesh2DLiveVariablesPtr = std::shared_ptr<const Mesh2DLiveVariables>;

// Call only on the simulation worker while its engine is initialized. A null
// result with no error means inactive GW. Active GW always includes available
// table/base elevations, even without chemical species. Units come
// from public authoring metadata matched to actual runtime species identities.
// Unknown units remain explicit; raw values can still be inspected.
Mesh2DLiveVariablesPtr captureGroundwaterVariables(SWMM_Engine engine,
                                                  int expectedCells,
                                                  QString *error = nullptr);
}
Q_DECLARE_METATYPE(openswmmvis::io::Mesh2DLiveVariablesPtr)
#endif
