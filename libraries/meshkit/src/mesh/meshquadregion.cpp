// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
QPolygonF normalizeRingCCW(const QPolygonF&r){return a::qt(n::normalizeRingCCW(a::native(r)));}
bool polylineIntersectsRect(const QVector<QPointF>&p,const QRectF&r){return n::polylineIntersectsRect(a::native(p),a::native(r));}
double ringSignedArea(const QPolygonF&r){return n::ringSignedArea(a::native(r));}
bool pointInRing(const QPolygonF&r,const QPointF&p){return n::pointInRing(a::native(r),a::native(p));}
bool pointInRegion(const QPolygonF&r,const QVector<QPolygonF>&h,const QPointF&p){return n::pointInRegion(a::native(r),a::native(h),a::native(p));}
bool ringIsSimple(const QPolygonF&r){return n::ringIsSimple(a::native(r));}
QString validateQuadRegion(const QuadRegion&r,const QVector<QPolygonF>&d,const QVector<QPolygonF>&h){return a::qt(n::validateQuadRegion(a::native(r),a::native(d),a::native(h)));}
QString validateQuadRegionsDisjoint(const QVector<QuadRegion>&r){return a::qt(n::validateQuadRegionsDisjoint(a::native(r)));}
QuadRegionMode classifyQuadRegion(const QPolygonF&r,QVector<int>*c,double t,double rt){n::Vector<int> corners;auto mode=n::classifyQuadRegion(a::native(r),c?&corners:nullptr,t,rt);if(c)*c=a::qt(corners);return a::qt(mode);}
QPolygonF resampleRing(const QPolygonF&r,double h,double keep){return a::qt(n::resampleRing(a::native(r),h,keep));}
double ringTurnDeg(const QPolygonF&r,int i){return n::ringTurnDeg(a::native(r),i);}
}
