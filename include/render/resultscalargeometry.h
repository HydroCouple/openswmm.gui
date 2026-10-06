#pragma once
#include "io/mesh2dscalarframe.h"
#include "contour/marchingtriangles.h"
#include <QFont>
#include <QRectF>
#include <QTransform>
#include <QVector>
#include <array>
namespace OpenSWMM::Render {
struct ResultScalarLabel { QPointF point; double value=0; int cell=-1; };
struct ResultScalarGeometry {
    std::vector<OpenSWMM::Contour::IsoLineSegment> contours;
    QVector<ResultScalarLabel> labels;
    QString error;
    size_t bytes() const {return contours.size()*sizeof(OpenSWMM::Contour::IsoLineSegment)+size_t(labels.size())*sizeof(ResultScalarLabel);}
};
// Area-weighted nodal display reconstruction from valid cells only. Invalid
// cells are excluded entirely; this does not replace native cell values.
ResultScalarGeometry buildResultScalarGeometry(const QVector<QPointF> &vertices,
    const std::vector<std::array<int,4>> &cells,const std::vector<std::array<int,3>> &triangles,
    const std::vector<int> &triangleCells,const openswmmvis::io::Mesh2DScalarFrame &frame,
    const std::vector<double> &levels,bool cellLabels);
struct ResultScalarPlacedLabel {QString text;QRectF rect;};
QVector<ResultScalarPlacedLabel> placeResultScalarLabels(const ResultScalarGeometry &,
    const QTransform &sceneToScreen,const QRectF &viewport,const QFont &,int decimals,
    const QString &units,bool contourLabels,int maximum=256);
}
