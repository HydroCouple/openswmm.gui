// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
ConstraintAuditReport auditConstraintSimplification(const QVector<SimplificationFeature>&f,const ConstraintSimplificationResult&r,const ConstraintAuditOptions&o){return a::qt(n::auditConstraintSimplification(a::native(f),a::native(r),a::native(o)));}
ConstraintAuditReport auditMeshConstraints(const QVector<SimplificationFeature>&f,const MeshResult&m,const ConstraintAuditOptions&o){return a::qt(n::auditMeshConstraints(a::native(f),a::native(m),a::native(o)));}
}
