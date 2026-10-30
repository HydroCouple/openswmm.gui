// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
QString validate(const StructuredPatch&p){return a::qt(n::validate(a::native(p)));}
QString validate(const SweptPatch&p){return a::qt(n::validate(a::native(p)));}
QString validate(const PatchMesh&p){return a::qt(n::validate(a::native(p)));}
QPolygonF orderedPatchBoundary(const PatchMesh&p,QString*err){n::Text message;auto out=n::orderedPatchBoundary(a::native(p),&message);if(err)*err=a::qt(message);return a::qt(out);}
PatchMesh makeTransfinitePatch(const StructuredPatch&p,QString*err){n::Text message;auto out=n::makeTransfinitePatch(a::native(p),&message);if(err)*err=a::qt(message);return a::qt(out);}
PatchMesh makeSweptPatch(const SweptPatch&p,QString*err){n::Text message;auto out=n::makeSweptPatch(a::native(p),&message);if(err)*err=a::qt(message);return a::qt(out);}
PatchMesh makeBankPairPatch(const BankPairPatch&p,QString*err){n::Text message;auto out=n::makeBankPairPatch(a::native(p),&message);if(err)*err=a::qt(message);return a::qt(out);}
PatchMesh makeTransfinitePatch(const QVector<QVector<QPointF>>&s,int x,int y,const QString&t,QString*err){n::Text m;auto out=n::makeTransfinitePatch(a::native(s),x,y,a::native(t),&m);if(err)*err=a::qt(m);return a::qt(out);}
PatchMesh makeTransfinitePatchAtStations(const QVector<QVector<QPointF>>&s,const QString&t,QString*err){n::Text m;auto out=n::makeTransfinitePatchAtStations(a::native(s),a::native(t),&m);if(err)*err=a::qt(m);return a::qt(out);}
PatchMesh makeMappedPatch(const QPolygonF&r,const QVector<int>&c,double h,const QString&t,QString*err){n::Text m;auto out=n::makeMappedPatch(a::native(r),a::native(c),h,a::native(t),&m);if(err)*err=a::qt(m);return a::qt(out);}
PatchMesh makeMappedPatch(const QPolygonF&r,const QVector<int>&c,double h,double v,double angle,const QString&t,QString*err){n::Text m;auto out=n::makeMappedPatch(a::native(r),a::native(c),h,v,angle,a::native(t),&m);if(err)*err=a::qt(m);return a::qt(out);}
}
