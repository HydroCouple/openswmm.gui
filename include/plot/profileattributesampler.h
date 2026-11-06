/**
 * @file      profileattributesampler.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Attribute-tracks feature — fetches an along-the-path profile of
 *         one generic result attribute (node depth / head / …, link flow /
 *         velocity / …) from an open .out file.
 *
 * Counterpart to ProfileSourceFetcher, which pulls the three hard-coded
 * variables the HGL/EGL computation needs. This one is generic over
 * `PlotAttribute`, reusing `SwmmOutRunLayer::variableCodeFor` as the single
 * source of truth for the engine variable codes.
 *
 * Same contracts as the rest of the profile pipeline:
 *  - arrays are addressed by PATH index, not engine index;
 *  - a path element the output file doesn't know leaves an EMPTY row
 *    (never a zero-filled one), so "no data" is distinguishable from 0.0;
 *  - short series (fewer periods than the longest source) are permitted —
 *    consumers render the frames that exist (LIVE_1D plan contract).
 *
 * This sampler uses the layer's output reader directly. Callers must keep
 * the layer alive and serialize access to that reader and its metadata.
 * ProfileSourceFetcher instead opens a private reader for worker use.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_PROFILEATTRIBUTESAMPLER_H
#define OPENSWMMVIS_PLOT_PROFILEATTRIBUTESAMPLER_H

#include "plot/plotattribute.h"
#include "plot/profilebuilder.h"

#include <QVector>

class SWMMResultsLayer;

namespace ProfileAttributeSampler
{

/*!
 * \struct AttributeProfile
 * \brief One attribute × one source, sampled along the whole path for the
 *        whole simulation.
 *
 * Node attributes: `byPath.size() == path.nodes.size()`, one value per node.
 * Link attributes: `byPath.size() == path.links.size()`, one value per link.
 * Each inner row is `[period]` — empty when the element is unknown to the
 * output file. `minByPath`/`maxByPath` are the per-element envelope over
 * all periods; NaN where the row is empty.
 */
struct AttributeProfile {
    openswmmvis::plot::PlotAttribute attribute = openswmmvis::plot::PlotAttribute::Unknown;
    bool isNodeAttribute = true;
    int periodCount = 0;

    QVector<QVector<float>> byPath; ///< [pathIdx][period]
    QVector<float> minByPath;       ///< [pathIdx], NaN when no data
    QVector<float> maxByPath;       ///< [pathIdx], NaN when no data
};

/*! \brief True for the six Node* attributes, false for everything else.
 *  Inline (header-only) so UI code and tests can classify attributes
 *  without linking the sampler's engine/results-layer dependency chain.
 * @param[in] attr Fixed result identifier.
 * @return True only for the six node quantities; does not inspect a source or species scope.
 */
[[nodiscard]] inline bool isNodeAttribute(openswmmvis::plot::PlotAttribute attr)
{
    using PA = openswmmvis::plot::PlotAttribute;
    switch (attr) {
    case PA::NodeDepth:
    case PA::NodeHead:
    case PA::NodeVolume:
    case PA::NodeLateralInflow:
    case PA::NodeTotalInflow:
    case PA::NodeOverflow:
        return true;
    default:
        return false;
    }
}

/*! \brief True when \p attr is one of the 11 attributes a track can show.
 * @param[in] attr Fixed identifier.
 * @return True for six node or five link fields, false for subcatchment/system/mesh/unknown quantities.
 */
[[nodiscard]] inline bool isTrackableAttribute(openswmmvis::plot::PlotAttribute attr)
{
    using PA = openswmmvis::plot::PlotAttribute;
    if (isNodeAttribute(attr))
        return true;
    switch (attr) {
    case PA::LinkFlow:
    case PA::LinkDepth:
    case PA::LinkVelocity:
    case PA::LinkVolume:
    case PA::LinkCapacity:
        return true;
    default:
        return false;
    }
}

/*!
 * \brief Fetches the full-simulation profile of \p attr along \p path from
 *        \p resultsLayer's open output file.
 *
 * Returns a default-constructed (empty `byPath`) profile when the layer is
 * null, has no open output handle, or \p attr is not trackable.
 * @param[in] resultsLayer Optional borrowed results layer; its handle, names and period metadata must stay valid/stable during the call.
 * @param[in] path Ordered path names used to resolve output-file indices; no path geometry is modified.
 * @param[in] attr One of the eleven trackable fixed quantities.
 * @return Owned path-indexed rows in raw output units and finite-only min/max envelopes; unreadable/unknown objects have empty rows and NaN extrema. Metadata records attr/isNodeAttribute even on an early empty return. Read failures are represented by missing rows, without a diagnostic list.
 * @note Uses the layer's shared output handle directly. Caller must serialize access with other consumers of that reader; layer lifetime alone does not establish thread safety.
 */
[[nodiscard]] AttributeProfile fetch(SWMMResultsLayer *resultsLayer,
                                     const ProfileBuilder::PathStatic &path,
                                     openswmmvis::plot::PlotAttribute attr);

/*!
 * \brief Species variant (Y2b-2 follow-up, amendment D-Y4): fetches the
 *        along-the-path profile of one species — a pollutant or the
 *        reserved water-age column — BY NAME, in node or link scope.
 *
 * The name resolves against the run's live species list at call time
 * (SwmmOutRunLayer::speciesVariableCodeFor), so a reordered model can't
 * repoint the track at a different column. A run that doesn't carry the
 * species leaves every row EMPTY — same "no data ≠ 0.0" contract as the
 * enum overload. `attribute` in the result stays `Unknown`; the species
 * identity lives with the caller.
 * @param[in] resultsLayer Optional borrowed layer/handle; keep stable and serialize shared-reader access throughout.
 * @param[in] path Node/link names in desired path order.
 * @param[in] species Exact case-sensitive run species name; empty returns an empty profile.
 * @param[in] nodeScope True samples nodes, false links.
 * @return Owned rows and finite-only extrema in source units. Missing species leaves correctly sized empty rows/NaN extrema when a handle exists; attribute remains Unknown. No interpolated timestamps or unit conversion are supplied.
 */
[[nodiscard]] AttributeProfile fetchSpecies(SWMMResultsLayer *resultsLayer,
                                            const ProfileBuilder::PathStatic &path,
                                            const QString &species, bool nodeScope);

} // namespace ProfileAttributeSampler

#endif // OPENSWMMVIS_PLOT_PROFILEATTRIBUTESAMPLER_H
