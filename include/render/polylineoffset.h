/*!
 * \file   polylineoffset.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Perpendicular polyline offset — closes the Z.5 offset gap (Z.5b).
 *
 *         LineSymbolLayerSpec::offsetPx (Slice Z.5) ships the field but
 *         doesn't apply it at paint time — the helper for that lives
 *         here. The algorithm shifts every segment perpendicular to its
 *         own direction by \p offsetPx pixels (positive = right of
 *         forward direction), then joins the shifted segments at each
 *         interior vertex.
 *
 *         Join handling:
 *           - Interior vertices use a **miter** join — the intersection
 *             of the two shifted segments. When the miter length would
 *             exceed \ref kDefaultMiterLimit times \p offsetPx (sharp
 *             angles, near-fold cases), the algorithm falls back to a
 *             **bevel** join: it emits two points (the shifted end of
 *             segment N and the shifted start of segment N+1) rather
 *             than the single miter intersection. This prevents the
 *             extreme spikes that pure miters produce at sharp angles.
 *           - Endpoints use the perpendicular of their single adjacent
 *             segment.
 *
 *         Degenerate input handling:
 *           - Empty polyline → empty result.
 *           - Single vertex → unchanged (one-vertex polylines have no
 *             direction).
 *           - Zero \p offsetPx → identical input (no normal shift).
 *           - Two vertices coincident → that segment is skipped at join
 *             time; the perpendicular falls back to the previous valid
 *             segment.
 *
 *         The helper is a pure function; no Qt painter state involved.
 *         Callers can wire it into `LineSymbolLayerSpec::offsetPx`
 *         consumers at paint time without further plumbing.
 */

#ifndef OPENSWMM_RENDER_POLYLINEOFFSET_H
#define OPENSWMM_RENDER_POLYLINEOFFSET_H

#include <QPolygonF>

namespace OpenSWMM::Render
{

/*! \brief Default miter limit (length ratio) before the algorithm
 *         falls back to a bevel join. Mirrors QPen's default of 2.0 —
 *         a miter is rejected when its length exceeds 2× the offset
 *         distance. Lower → more bevels; higher → spikier joins at
 *         sharp angles. */
inline constexpr qreal kDefaultMiterLimit = 2.0;

/*! \brief Offset \p input perpendicular to each segment by \p offsetPx.
 *
 *         Positive \p offsetPx shifts to the right of the polyline's
 *         forward direction (start → end). Negative shifts to the left.
 *         Zero returns \p input verbatim.
 *
 *         \p miterLimit caps how long a miter join is allowed to be
 *         relative to \p offsetPx. Joins exceeding this limit fall back
 *         to bevels (two vertices instead of one at the corner).
 *         Defaults to \ref kDefaultMiterLimit.
 *
 *         The returned polyline may have more vertices than the input
 *         when bevel joins kick in, and fewer when consecutive vertices
 *         coincide.
 */
[[nodiscard]] QPolygonF offsetPolyline(const QPolygonF &input,
                                        qreal offsetPx,
                                        qreal miterLimit = kDefaultMiterLimit);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_POLYLINEOFFSET_H
