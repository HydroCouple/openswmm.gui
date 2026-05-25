/*!
 * \file   attributecandidates.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CTX.1 / CTX.3 — per-(layer, kind) attribute candidate lists.
 *
 * Shared between SymbologyDialog (CTX.1 — populates the Graduated /
 * Categorized / Single-tab attribute combos) and LayerTreePanel
 * (CTX.3 — greys out Style ▸ Graduated / Categorized when no numeric /
 * string attributes exist for the kind).
 *
 * v1 hardcodes per-kind lists matching the SWMM*PropertyAdapter
 * Q_PROPERTY declarations. A future pass can replace this with
 * QMetaObject introspection (see the "Q_PROPERTY auto-introspection"
 * deferred bullet in §L.BI.CTX of GUI_IMPLEMENTATION_PLAN.md).
 */
#ifndef OPENSWMMVIS_RENDER_ATTRIBUTECANDIDATES_H
#define OPENSWMMVIS_RENDER_ATTRIBUTECANDIDATES_H

#include "layers/swmmmodellayer.h"

#include <QStringList>

namespace OpenSWMM::Render::AttributeCandidates {

/*! Per-kind static numeric .inp attribute candidates for the SWMM model
 *  layer's Graduated + Single-tab data-defined-size combos. */
[[nodiscard]] QStringList modelLayerNumeric(SWMMModelLayer::Category cat);

/*! Per-kind string/enum .inp attribute candidates for Categorized. */
[[nodiscard]] QStringList modelLayerString(SWMMModelLayer::Category cat);

/*! SWMMResultVariable enumerator names appropriate for the kind's
 *  geometry scope (Nodes / Links / Subcatch). \p kindOrdinal of -1
 *  returns every result variable (for the pre-OUT.3 layer-scope dialog
 *  on SWMMResultsLayer). */
[[nodiscard]] QStringList resultsLayerNumeric(int kindOrdinal);

} // namespace OpenSWMM::Render::AttributeCandidates

#endif // OPENSWMMVIS_RENDER_ATTRIBUTECANDIDATES_H
