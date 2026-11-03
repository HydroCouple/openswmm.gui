/**
 * @file      mesh2dresultvector.h
 * @brief     Vector results: pairs of x/y component variables on the same cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_IO_MESH2DRESULTVECTOR_H
#define OPENSWMMVIS_IO_MESH2DRESULTVECTOR_H

#include "io/mesh2dresultvariable.h"
#include <QVector>

namespace openswmmvis::io
{

/// A vector result: two plain (no species, no layer) component variables that
/// share domain, clock and units.
struct Mesh2DResultVector {
    Mesh2DResultVariable x, y;
    QString label;
    /**
     * @return "vector:<x key>|<y key>"; empty when either component has no key.
     */
    QString key() const;
    /**
     * @return Key of the derived |v| scalar (mesh2dderivedvariables) for this vector.
     */
    QString magnitudeKey() const;
    /** @return Domain of the components. */
    Mesh2DResultVariable::Domain domain() const { return x.domain; }
};

/**
 * @brief Pair time-dependent component variables whose dataset names differ only in a trailing
 *   x/y on one underscore token: Mesh2_face_vx/vy, Mesh2_face_gw_qx/qy, Mesh2_face_grad_hx/hy,
 *   Mesh2_face_grad_hx_lim/hy_lim.
 * @param[in] variables Catalog in source order; derived, species, layered, zoned and
 *   time-independent variables never pair.
 * @return Vectors in the x components' catalog order. Components must agree on domain, temporal
 *   kind, frame count and (when known) units.
 */
QVector<Mesh2DResultVector> pairResultVectors(const QVector<Mesh2DResultVariable> &variables);

/**
 * @param[in] vectors Paired vectors.
 * @param[in] key Scalar variable key.
 * @return True when key is the x or y component of one of the vectors.
 */
bool isVectorComponentKey(const QVector<Mesh2DResultVector> &vectors, const QString &key);

/**
 * @param[in] key Native key "surface:<dataset>" or "groundwater:<dataset>" with no species or
 *   layer suffix.
 * @return Minimal descriptor (dataset, domain) a source can read by key; empty dataset otherwise.
 */
Mesh2DResultVariable plainVariableFromKey(const QString &key);

} // namespace openswmmvis::io
#endif
