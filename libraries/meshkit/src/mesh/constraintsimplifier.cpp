// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
QString ConstraintSimplificationPolicy::validate()const{return a::qt(a::native(*this).validate());}
QString ConstraintPolicyOverride::validate()const{return a::qt(a::native(*this).validate());}
ConstraintSimplificationPolicy ConstraintPolicyOverride::resolve(ConstraintSimplificationPolicy p)const{return a::qt(a::native(*this).resolve(a::native(p)));}
ConstraintSimplificationResult simplifyConstraints(const QVector<SimplificationFeature>&f,const ConstraintSimplificationOptions&o){return a::qt(n::simplifyConstraints(a::native(f),a::native(o)));}
}
