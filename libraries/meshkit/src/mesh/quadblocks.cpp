// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
QuadBlockGrid quadBlockGridFromMesh(const MeshResult&m,double pitch,const QVector<QVector<QPointF>>&b){return a::qt(n::quadBlockGridFromMesh(a::native(m),pitch,a::native(b)));}
QuadBlockGrid quadBlockGridFromField(const QRectF&r,double pitch,const std::function<double(const QRectF&)>&f,const QVector<QVector<QPointF>>&b,const std::function<bool()>&cancel,qint64 cap){return a::qt(n::quadBlockGridFromField(a::native(r),pitch,[&](const meshkit::Box2&box){return f(a::qt(box));},a::native(b),cancel,cap));}
QVector<QuadRegion> placeQuadBlocks(const QuadBlockGrid&g,const QuadBlockOptions&o){return a::qt(n::placeQuadBlocks(a::native(g),a::native(o)));}
}
