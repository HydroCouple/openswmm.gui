// SPDX-License-Identifier: Apache-2.0
// One algorithm implementation, instantiated with the existing Qt storage API.
#include "mesh/meshcdt.h"
#include "../native/cdt_impl.hpp"
template class meshkit::BasicConstrainedDelaunay<meshkit::detail::QtStorage>;
