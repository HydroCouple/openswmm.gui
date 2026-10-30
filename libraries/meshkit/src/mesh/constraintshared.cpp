// SPDX-License-Identifier: Apache-2.0
// Qt value adapter; numerical implementation is shared with the native package.
#include "mesh/qtnative.h"
namespace mesh {
namespace a=native_adapter;namespace n=meshkit::plan;
namespace detail {ConstraintSimplificationResult simplifySharedConstraints(const QVector<SimplificationFeature>&f,const ConstraintSimplificationOptions&o){return a::qt(n::detail::simplifySharedConstraints(a::native(f),a::native(o)));}}
}
