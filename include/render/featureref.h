/*!
 * \file   featureref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Stable identifier passed to IFeatureRenderer::symbolFor().
 *
 *         A FeatureRef is the minimum information a renderer needs to
 *         identify which feature in the layer it is being asked to paint:
 *         the owning layer's id and the feature's index within that layer.
 *         An optional category hint disambiguates layers that hold multiple
 *         element kinds (e.g. SWMMModelLayer's nodes vs. links).
 *
 *         Renderers should treat FeatureRef as opaque — the actual attribute
 *         data is supplied via the parallel QVariantMap argument so the
 *         renderer doesn't have to know about the layer's storage layout.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.1 — interface + types only.
 */

#ifndef OPENSWMM_RENDER_FEATUREREF_H
#define OPENSWMM_RENDER_FEATUREREF_H

#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \struct FeatureRef
 * \brief Identifies one feature in one layer for the renderer pipeline.
 */
struct FeatureRef
{
    QString layerId;          /*!< Owning layer's stable id. */
    int     featureIndex = -1;/*!< Index within the layer's feature list (-1 = unknown). */
    QString categoryHint;     /*!< Optional category disambiguator (e.g. "junction", "conduit"). */
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_FEATUREREF_H
