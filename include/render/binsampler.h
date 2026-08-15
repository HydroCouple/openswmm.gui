/*!
 * \file   binsampler.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Compute frozen break values across animation frames (Slice Z.7).
 *
 *         Animation binning lifecycle (RENDERING_RULE_MODEL_PLAN.md §14.1):
 *           1. At Rule creation, sample the bound attribute across all
 *              frames at the configured sample rate and call
 *              `sampleBreaksAcrossFrames` to compute breaks once.
 *           2. The caller stashes those breaks into the binner via
 *              `setMethod(Manual) + setManualBreaks(...)` — freezing the
 *              breaks for the rest of the Rule's lifetime.
 *           3. The user can re-run step 1 explicitly (the Symbology tab's
 *              "Recompute breaks…" button) or opt into per-frame
 *              recomputation via Rule::setRebinPerFrame(true).
 *
 *         The sampler itself is stateless — pure function over input
 *         frames. Z.7 ships just the sampler + the Rule-side opt-in
 *         flag. Integration with AnimationController + the result-layer
 *         paint loop is the named Z.7a follow-up.
 */

#ifndef OPENSWMM_RENDER_BINSAMPLER_H
#define OPENSWMM_RENDER_BINSAMPLER_H

#include "render/intervalbinner.h"

#include <QVector>

namespace OpenSWMM::Render
{

/*! \brief Pick \p perFrameValues at \p sampleRate fraction (≥ 1 frame),
 *         flatten the selected frames' values, and call
 *         IntervalBinner::computeBreaks() on the flat sample.
 *
 *         \p sampleRate is clamped to [0, 1]. A sampleRate of 1.0 uses
 *         every frame; 0.2 (default) picks 20% of frames evenly spaced
 *         starting at index 0 — matching the plan's §14.1 default.
 *         A sampleRate ≤ 0 falls back to "always sample at least one
 *         frame" so callers don't have to special-case empty input.
 *
 *         Returns an empty QVector when \p perFrameValues is empty.
 *         When the binner's method is Manual, computeBreaks() simply
 *         returns the binner's existing manualBreaks() — so callers can
 *         use this helper uniformly without branching on method.
 *
 *         The set of selected frames is deterministic: index 0 first,
 *         then index = round(i * stride) for i in [1, k-1], where k =
 *         ceil(sampleRate * frameCount). This means the first and last
 *         frames are always sampled when sampleRate is high enough.
 */
[[nodiscard]] QVector<double>
sampleBreaksAcrossFrames(const IntervalBinner &binner,
                          const QVector<QVector<double>> &perFrameValues,
                          double sampleRate = 0.2);

/*! \brief Indices of the frames the sampler would pick, for the same
 *         \p frameCount and \p sampleRate. Pure function — useful for
 *         tests and for showing the user which frames were sampled.
 *         Returns an empty vector when frameCount == 0. */
[[nodiscard]] QVector<int>
sampledFrameIndices(int frameCount, double sampleRate);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_BINSAMPLER_H
