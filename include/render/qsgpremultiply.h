/*!
 * \file   qsgpremultiply.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Premultiplied-alpha helper shared by the QSG map renderers (terrain mesh,
 * 2D results, 1D network), which all emit vertex colours into
 * QSGVertexColorMaterial.
 *
 * Qt-Core-only so it can be unit-tested headlessly — see
 * tests/unit/test_qsgpremultiply.cpp.
 */
#ifndef OPENSWMM_RENDER_QSGPREMULTIPLY_H
#define OPENSWMM_RENDER_QSGPREMULTIPLY_H

#include <QtGlobal>

namespace OpenSWMM::Render
{

/*! Premultiply one straight-alpha colour channel for QSGVertexColorMaterial,
 *  which samples vertex colours as PREMULTIPLIED alpha (Qt scene-graph
 *  contract). Feeding straight-alpha colours makes the compositor
 *  un-premultiply them on blend, clamping every semi-transparent fill toward
 *  saturation — ochre → pure yellow, hillshaded tan → salmon pink, and the
 *  off-white high-terrain stops → background white, which reads as the mesh
 *  being "truncated" at far zoom. At alpha 0 a straight-alpha colour blends
 *  ADDITIVELY (One / OneMinusSrcAlpha), painting its raw RGB over whatever
 *  is beneath — a fully transparent classification band must premultiply to
 *  (0,0,0,0) to disappear. */
inline quint8 premul(quint8 c, quint8 a)
{
    return quint8((uint(c) * uint(a) + 127u) / 255u);
}

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSGPREMULTIPLY_H
