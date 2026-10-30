// SPDX-License-Identifier: GPL-3.0-or-later
// Qt storage facade over the shared constrained triangulation implementation.
#pragma once
#include "meshkit/cdt.hpp"
#include "mesh/qtcdtstorage.h"
#include "mesh/terrainpolicy.h"
#include "mesh/meshprogress.h"
namespace mesh {
using ConstrainedDelaunay=meshkit::BasicConstrainedDelaunay<meshkit::detail::QtStorage>;
}
