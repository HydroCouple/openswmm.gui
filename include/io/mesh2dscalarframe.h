#ifndef OPENSWMMVIS_IO_MESH2DSCALARFRAME_H
#define OPENSWMMVIS_IO_MESH2DSCALARFRAME_H

#include "io/mesh2dresultvariable.h"
#include <QVector>
#include <vector>
#include <limits>

namespace openswmmvis::io {

// Immutable once published. Values remain in the dataset's declared units;
// map coordinate conversions never alter concentrations or groundwater data.
struct Mesh2DScalarFrame
{
    Mesh2DResultVariable descriptor;
    std::vector<float> values;
    std::vector<Mesh2DValueStatus> status;
    QVector<double> samples;
    double minimum = std::numeric_limits<double>::quiet_NaN();
    double maximum = std::numeric_limits<double>::quiet_NaN();
    int time = -1;
    QString error;
};

} // namespace openswmmvis::io
#endif
