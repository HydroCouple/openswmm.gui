// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
bool SizeField::build(const QRectF&b,const QVector<ConstraintSegment>&s,const QVector<QVector<QPointF>>&r,const QVector<SteinerPoint>&p,const SizeFieldOptions&o){return m_native.build(a::native(b),a::native(s),a::native(r),a::native(p),a::native(o));}
double SizeField::minimumSizeInBox(const QRectF&b)const{return m_native.minimumSizeInBox(a::native(b));}
double SizeField::minimumTerrainSizeInBox(const QRectF&b)const{return m_native.minimumTerrainSizeInBox(a::native(b));}
double SizeField::targetAreaAt(double x,double y)const{return m_native.targetAreaAt(x,y);}
double SizeField::sizeAt(double x,double y)const{return m_native.sizeAt(x,y);}
double SizeField::distanceAt(double x,double y)const{return m_native.distanceAt(x,y);}
}
