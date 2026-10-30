// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
PatchAssessment assessPlannedPatch(const PatchMesh&p,const PatchLimits&l){return a::qt(n::assessPlannedPatch(a::native(p),a::native(l)));}
QuadPlan planQuadRegions(const QVector<QuadRegion>&r,const QuadPlanningGeometry&g,const QuadPlanningOptions&o){return a::qt(n::planQuadRegions(a::native(r),a::native(g),a::native(o)));}
PatchInterfaceReport auditPatchInterfaces(const MeshResult&m,const QVector<MeshEdge>&edges,double ratio,const std::function<bool()>&cancel,int cap){n::Vector<n::MeshEdge> converted;for(auto e:edges){n::MeshEdge v;v.v0=e.v0;v.v1=e.v1;v.marker=e.marker;converted.append(v);}return a::qt(n::auditPatchInterfaces(a::native(m),converted,ratio,cancel,cap));}
}
